#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QRectF>
#include <QSGGeometry>
#include <QSizeF>
#include <QVector>

#include "timeline/TimelineSceneState.h"

class QSGNode;
class QSGTexture;

struct TimelineQuickRasterizedImage {
    QImage image;
    QSizeF logicalSize;
};

QImage makeTimelineGlyphImage(const miacode::timeline::TimelineSceneGlyph& glyph);
TimelineQuickRasterizedImage makeTimelineTextImage(
    const miacode::timeline::TimelineSceneTextLabel& label,
    qreal devicePixelRatio = 1.0);
QSGNode* buildTimelineLineNode(const miacode::timeline::TimelineSceneLine& line);
QSGNode* buildTimelineRectNode(const miacode::timeline::TimelineSceneRect& rect);
QSGNode* buildTimelineTriangleNode(const miacode::timeline::TimelineSceneTriangle& triangle);

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
// Sprite batch builder (parity with PreviewDCompSpritePipeline's draw-run
// coalescing). Stream textured quads in; each transition between
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
    explicit TimelineQuickSpriteBatchBuilder(QSGNode* parent);

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
    QSGTexture* currentTexture_ = nullptr;
    QVector<QSGGeometry::TexturedPoint2D> pendingVertices_;
    int totalBatches_ = 0;
    int totalQuads_ = 0;
};
