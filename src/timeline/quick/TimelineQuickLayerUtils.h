#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QRectF>
#include <QSGGeometry>
#include <QSizeF>
#include <QVector>

#include "timeline/TimelineSceneState.h"

class QSGNode;
class QSGTexture;
class QQuickWindow;

QImage makeTimelineGlyphImage(const miacode::timeline::TimelineSceneGlyph& glyph);
QSGNode* buildTimelineTextNode(
    QQuickWindow* window,
    const miacode::timeline::TimelineSceneTextLabel& label);
QSGNode* buildTimelineLineNode(const miacode::timeline::TimelineSceneLine& line);
QSGNode* buildTimelineRectNode(const miacode::timeline::TimelineSceneRect& rect);
QSGNode* buildTimelineTriangleNode(const miacode::timeline::TimelineSceneTriangle& triangle);

// beta7 leak gauge — cumulative count of QSGGeometry objects allocated by ALL timeline render
// layers (the per-rebuild churn that backs Qt's RHI deferred buffer release). Monotonic; read
// once per pause. A per-cycle delta that tracks the GPU-memory climb convicts geometry churn.
quint64 timelineQuickGeometryCreateTotal();

// ---------------------------------------------------------------------
// Flat-color rect/line batch builder. Streams axis-aligned rectangles
// (and orthogonal lines, which are just thin rectangles). Each
// transition between colors starts a new batch — sibling of the sprite
// builder below, but no texture/material binding to flush; just a
// QSGFlatColorMaterial per run. For chart data with thousands of grid
// lines or waveform bars sharing 1-2 colors, the result is 1-2 nodes
// instead of N nodes + N draw calls.
class TimelineQuickFlatColorBatchBuilder
{
public:
    explicit TimelineQuickFlatColorBatchBuilder(QSGNode* parent);

    // Append an axis-aligned rectangle. `rect` is in logical
    // coordinates. Color comparison is by exact rgba() match — themes
    // with subtly different colors won't merge.
    void appendRect(const QColor& color, const QRectF& rect);

    // Convenience: append an orthogonal line (vertical or horizontal)
    // by inflating to a thin rectangle. Lines that aren't strictly
    // orthogonal fall through to the legacy buildTimelineLineNode path
    // (the caller should flush() first to preserve z-order).
    bool tryAppendOrthogonalLine(const miacode::timeline::TimelineSceneLine& line);

    void flush();

    int batchCount() const { return totalBatches_ + (pendingVertices_.isEmpty() ? 0 : 1); }
    int totalQuads() const { return totalQuads_; }

private:
    QSGNode* parent_ = nullptr;
    QColor currentColor_;
    bool currentColorValid_ = false;
    QVector<QSGGeometry::Point2D> pendingVertices_;
    int totalBatches_ = 0;
    int totalQuads_ = 0;
};

// ---------------------------------------------------------------------
// Optimization A — geometry pool.
//
// The QSG batched renderer doesn't directly own QRhiBuffers per
// QSGGeometry, but constructing/destroying QSGGeometry objects every
// rebuild still triggers a malloc()+free() of the CPU vertex buffer
// and a fresh allocation of the QSGGeometry object itself. With the
// notes layer producing ~10-30 sprite batches per rebuild, that's
// 10-30 small mallocs per follow-toggle / scroll-bucket-cross /
// playback-tick.
//
// The pool keeps released QSGGeometry instances alive across
// rebuilds. claim() resizes a pooled instance to the requested vertex
// count via QSGGeometry::allocate() — the SAME path Qt would take if
// constructing a fresh one, just with the QSGGeometry CPU object
// reused. release() pushes the instance onto the LIFO stack.
//
// Pool nodes are emitted with OwnsGeometry=false, so when their
// QSGGeometryNode is deleted (clearChildren), ~QSGGeometryNode does
// NOT delete the geometry. Geometry survives, gets harvested back
// into the pool by reclaimGeometriesFromTree().
//
// IMPORTANT lessons from the crash bisect:
// 1. DON'T call setGeometry(nullptr) on the old node before deletion
//    — markDirty(DirtyGeometry) fires the renderer's nodeChanged,
//    which dereferences the now-NULL geometry. Just leave it
//    attached; clearChildren's delete on the OwnsGeometry=false node
//    won't free the geometry.
// 2. DON'T skip allocate() on exact-size match. Qt's allocate(N)
//    free()s and malloc()s the CPU buffer when the size changes;
//    skipping it leaves m_data pointing at the previous frame's
//    buffer with the previous frame's vertex count. If anything in
//    the renderer's bookkeeping read m_vertex_count between frames
//    and the buffer didn't actually match (e.g. a re-allocation
//    happened in some Qt internal path), that's a buffer-overrun
//    write into adjacent heap memory — exactly the heap-corruption
//    pattern we observed. Always call allocate(N).
class TimelineQuickGeometryPool
{
public:
    ~TimelineQuickGeometryPool();
    // Returns a QSGGeometry sized to `vertexCount`. Reuses a pooled
    // instance if available (calls allocate() to fit), else
    // constructs a fresh one with `defaultAttributes_TexturedPoint2D`
    // and DrawTriangles mode. Always calls allocate() so the CPU
    // vertex buffer matches `vertexCount` exactly — never returns a
    // geometry whose buffer was sized for a previous, different
    // request.
    QSGGeometry* claim(int vertexCount);
    // Returns a QSGGeometry to the pool for later reuse. Geometry
    // pointer ownership transfers to the pool.
    void release(QSGGeometry* geometry);
    int pooledCount() const { return available_.size(); }

private:
    QVector<QSGGeometry*> available_;
};

// Walks `parent`'s sprite-batch children and harvests their
// QSGGeometry into the pool. Geometry is NOT detached from the node
// — leaving it attached avoids the nodeChanged-on-NULL-geometry
// crash that an earlier setGeometry(nullptr) variant produced.
// ~QSGGeometryNode honours OwnsGeometry, so the upcoming
// clearChildren + delete won't free pool-owned geometries. Call
// BEFORE clearChildren to capture the pointers while the tree is
// still intact. Non-QSGGeometryNode children (opacity, line, text)
// and OwnsGeometry=true children (legacy-builder fallback,
// QSGSimpleRectNode) are skipped.
void reclaimGeometriesFromTree(QSGNode* parent, TimelineQuickGeometryPool* pool);

// ---------------------------------------------------------------------
// Sprite batch builder with draw-run coalescing. Stream textured quads
// in; each transition between
// textures starts a new batch. flush() emits one QSGGeometryNode per
// batch as a child of `parent`, with one QSGTextureMaterial pointing at
// the run's shared texture. For chart data with N sprites and K distinct
// textures, the result is K nodes + K draw calls instead of N nodes +
// N draw calls. With ~200k objects/frame and a typical chart's ~20-30
// distinct textures, that's a 4 orders-of-magnitude reduction in QSG
// node count.
//
// Usage: instantiate per layer pass, append sprites in z-order, call
// flush() at the end (or before emitting a non-sprite node that has to
// preserve z-order). Single-use; do not reuse across rebuilds.
class TimelineQuickSpriteBatchBuilder
{
public:
    // When `pool` is non-null, flush() reuses pooled QSGGeometry
    // instances; when null, allocates a fresh geometry per batch
    // (legacy behaviour).
    explicit TimelineQuickSpriteBatchBuilder(QSGNode* parent,
                                              TimelineQuickGeometryPool* pool = nullptr);

    // Append one textured quad. `rect` is in logical coordinates (QSG
    // applies the DPR projection). Texture must be valid; null-texture
    // sprites are silently dropped (matching the prior path's
    // appendTextureNode behaviour).
    void appendQuad(QSGTexture* texture, const QRectF& rect);

    // Optionally pin a custom UV rect (defaults to (0,0)-(1,1)). Used
    // by hold body-slice sprites that may need a non-1:1 UV if/when
    // future sub-rect texturing arrives. Currently always 0..1.
    void appendQuad(QSGTexture* texture,
                    const QRectF& rect,
                    const QRectF& uvRect);

    // Emit any pending batch as a child of `parent`. Safe to call
    // multiple times (subsequent calls are no-ops if no quads were
    // queued in between). Must be called before parent goes out of
    // scope, otherwise the last batch is lost.
    void flush();

    // Convenience: number of batches emitted so far + the pending one.
    int batchCount() const { return totalBatches_ + (pendingVertexCount() > 0 ? 1 : 0); }
    int totalQuads() const { return totalQuads_; }

private:
    int pendingVertexCount() const { return static_cast<int>(pendingVertices_.size()); }

    QSGNode* parent_ = nullptr;
    TimelineQuickGeometryPool* pool_ = nullptr;
    QSGTexture* currentTexture_ = nullptr;
    QVector<QSGGeometry::TexturedPoint2D> pendingVertices_;
    int totalBatches_ = 0;
    int totalQuads_ = 0;
};

// ---------------------------------------------------------------------
// Optimization B — texture-grouped sprite builder.
//
// The ordered builder above emits a new node every time the texture
// transitions in append order: with input stream tex_A, tex_B, tex_A,
// tex_B you get 4 nodes (one per run) even though there are only 2
// distinct textures. That works when sprites in the stream visually
// overlap (the on-top sprite must come last); for chart sprites at
// distinct logical positions (track sprites, note sprites — each
// lives at its own (x, y) and doesn't share pixels with siblings of
// the same group), the visual result is identical regardless of
// intra-group order.
//
// This builder buckets all appended quads by texture and emits one
// geometry node per UNIQUE texture on flush(). Same input would
// produce 2 nodes instead of 4. Combined with the pool, the bucketed
// nodes also reuse pooled QSGGeometry instances.
//
// Z-order policy: nodes are appended to `parent` in the order
// textures are first seen. Caller MUST only use this builder for
// sprite collections where intra-group reordering is visually safe.
// For collections with stacking semantics (hold body must render
// before its caps), use the ordered builder above.
class TimelineQuickGroupedSpriteBatchBuilder
{
public:
    explicit TimelineQuickGroupedSpriteBatchBuilder(
        QSGNode* parent,
        TimelineQuickGeometryPool* pool = nullptr);

    void appendQuad(QSGTexture* texture, const QRectF& rect);
    void appendQuad(QSGTexture* texture,
                    const QRectF& rect,
                    const QRectF& uvRect);

    // Emit one QSGGeometryNode per unique texture as children of
    // `parent`, in first-seen order. Buckets are cleared after emit.
    void flush();

    int batchCount() const { return totalBatches_ + buckets_.size(); }
    int totalQuads() const { return totalQuads_; }

private:
    QSGNode* parent_ = nullptr;
    TimelineQuickGeometryPool* pool_ = nullptr;
    // First-seen order of textures, used so the emitted nodes
    // preserve a deterministic z-position (insertion order matches
    // QHash iteration only by accident, so we track explicit order
    // here).
    QVector<QSGTexture*> textureOrder_;
    QHash<QSGTexture*, QVector<QSGGeometry::TexturedPoint2D>> buckets_;
    int totalBatches_ = 0;
    int totalQuads_ = 0;
};
