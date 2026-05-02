#include "timeline/quick/TimelineQuickNotesLayer.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGTransformNode>
#include <QTransform>

#include "timeline/quick/TimelineQuickLayerUtils.h"
#include "timeline/quick/TimelineQuickTextureCache.h"

namespace {

struct TimelineQuickNotesRootNode : public QSGNode {
    quint64 revision = 0;
    quint64 appearanceRevision = 0;
};

void clearChildren(QSGNode* node)
{
    if (node == nullptr) {
        return;
    }
    while (QSGNode* child = node->firstChild()) {
        node->removeChildNode(child);
        delete child;
    }
}

void rebuildNoteSlots(TimelineQuickNotesRootNode* root)
{
    if (root == nullptr) {
        return;
    }
    clearChildren(root);
    auto* clipRoot = new QSGClipNode();
    clipRoot->setIsRectangular(true);
    auto* transformRoot = new QSGTransformNode();
    transformRoot->appendChildNode(new QSGNode());
    clipRoot->appendChildNode(transformRoot);
    root->appendChildNode(clipRoot);
}

QSGClipNode* clipRootFor(TimelineQuickNotesRootNode* root)
{
    return root != nullptr ? dynamic_cast<QSGClipNode*>(root->firstChild()) : nullptr;
}

QSGTransformNode* transformRootFor(QSGClipNode* clipRoot)
{
    return clipRoot != nullptr ? dynamic_cast<QSGTransformNode*>(clipRoot->firstChild()) : nullptr;
}

QSGNode* contentRootFor(QSGTransformNode* transformRoot)
{
    return transformRoot != nullptr ? transformRoot->firstChild() : nullptr;
}

QSizeF rotatedSpriteLogicalSize(const QSize& baseSize, qreal rotationDegrees)
{
    if (!baseSize.isValid()) {
        return QSizeF();
    }
    const QRectF rect(0.0, 0.0, baseSize.width(), baseSize.height());
    if (qFuzzyIsNull(rotationDegrees)) {
        return rect.size();
    }
    return QTransform().rotate(rotationDegrees).mapRect(rect).size();
}

// Physical-pixel snap, mirroring the new pipeline's Phase 4d-fix4 fix
// (see TimelineNotesSource.cpp). At fractional DPR (1.25 / 1.5 / 1.75)
// integer-logical centres still produce non-integer physical-pixel
// corners, which the GPU rasteriser bilinear-smears. Snapping the
// CENTRE to the physical-pixel grid keeps texel→pixel mapping 1:1.
inline qreal snapToPhysicalPixel(qreal v, qreal dpr)
{
    return qRound(v * dpr) / dpr;
}

QRectF centeredSpriteRect(const QPointF& center, const QSizeF& size, qreal dpr)
{
    if (!size.isValid()) {
        return QRectF();
    }
    const qreal cx = snapToPhysicalPixel(center.x(), dpr);
    const qreal cy = snapToPhysicalPixel(center.y(), dpr);
    return QRectF(
        cx - (size.width() / 2.0),
        cy - (size.height() / 2.0),
        size.width(),
        size.height());
}

// Hold span emit — feeds a sprite-batch builder so that all hold caps
// across all hold spans (sharing texture) coalesce into one draw call.
// In the legacy per-sprite path each hold produced 3 QSGSimpleTextureNode
// children; with batching, N adjacent same-(spriteType, holdScale) holds
// collapse to ~3 nodes total (one per cap/body texture).
void appendHoldSpanBatched(
    QSGNode* fallbackParent,
    TimelineQuickSpriteBatchBuilder& batch,
    const miacode::timeline::TimelineSceneHoldSpan& holdSpan,
    TimelineQuickTextureCache* textures,
    qreal dpr)
{
    if (textures == nullptr) {
        return;
    }

    const qreal holdScale = textures->holdScaleForBaseIconScale(
        holdSpan.spriteType, holdSpan.baseIconScale);
    const TimelineQuickHoldTextureParts parts =
        textures->holdTextureParts(holdSpan.spriteType, holdScale);
    if (parts.isValid()) {
        const qreal capY = holdSpan.rowTop
            + ((holdSpan.laneHeight - parts.leftCap.logicalSize.height()) / 2.0);
        const qreal leftCapX = holdSpan.startX - parts.leftCap.logicalSize.width();
        const qreal rightCapX = holdSpan.endX;
        const qreal bodyStartX = leftCapX + parts.leftCap.logicalSize.width();
        const qreal bodyWidth = qMax<qreal>(0.0, rightCapX - bodyStartX);

        // Body slice — emit first so caps render visually adjacent.
        // Bodies, left caps, and right caps are 3 distinct textures so
        // they'll naturally split into 3 batches; emitting body first
        // matches the legacy QSG order.
        if (bodyWidth > 0.0) {
            batch.appendQuad(
                parts.bodySlice.texture,
                QRectF(bodyStartX,
                       snapToPhysicalPixel(capY, dpr),
                       bodyWidth,
                       parts.bodySlice.logicalSize.height()));
        }
        batch.appendQuad(
            parts.leftCap.texture,
            QRectF(leftCapX,
                   snapToPhysicalPixel(capY, dpr),
                   parts.leftCap.logicalSize.width(),
                   parts.leftCap.logicalSize.height()));
        batch.appendQuad(
            parts.rightCap.texture,
            QRectF(rightCapX,
                   snapToPhysicalPixel(capY, dpr),
                   parts.rightCap.logicalSize.width(),
                   parts.rightCap.logicalSize.height()));
        return;
    }

    // Fallback path — assets missing. Flush any pending sprite batch
    // first so the line node retains correct z-order, then add a line
    // and the start/end markers via the (still single-node) line code
    // path. Volume here is rare enough that batching isn't worth the
    // extra code complexity.
    batch.flush();
    fallbackParent->appendChildNode(buildTimelineLineNode(miacode::timeline::TimelineSceneLine{
        QPointF(holdSpan.startX, holdSpan.rowTop + (holdSpan.laneHeight / 2.0)),
        QPointF(holdSpan.endX, holdSpan.rowTop + (holdSpan.laneHeight / 2.0)),
        holdSpan.fallbackColor,
        holdSpan.fallbackWidth,
    }));
    const QSize targetSize = textures->noteTargetSize(holdSpan.spriteType, holdScale);
    if (!targetSize.isValid()) {
        return;
    }
    if (QSGTexture* texture = textures->noteTexture(holdSpan.spriteType, targetSize);
        texture != nullptr) {
        const qreal iconY = holdSpan.rowTop + ((holdSpan.laneHeight - targetSize.height()) / 2.0);
        batch.appendQuad(
            texture,
            QRectF(
                holdSpan.startX - (targetSize.width() / 2.0),
                snapToPhysicalPixel(iconY, dpr),
                targetSize.width(),
                targetSize.height()));
        batch.appendQuad(
            texture,
            QRectF(
                holdSpan.endX - (targetSize.width() / 2.0),
                snapToPhysicalPixel(iconY, dpr),
                targetSize.width(),
                targetSize.height()));
    }
}

}  // namespace

QSGNode* TimelineQuickNotesLayer::updateNode(
    QSGNode* oldNode,
    const miacode::timeline::TimelineSceneState& state,
    QQuickWindow* window,
    TimelineQuickTextureCache* textures) const
{
    auto* root = dynamic_cast<TimelineQuickNotesRootNode*>(oldNode);
    if (root == nullptr) {
        delete oldNode;
        root = new TimelineQuickNotesRootNode();
    }
    if (clipRootFor(root) == nullptr || transformRootFor(clipRootFor(root)) == nullptr
        || contentRootFor(transformRootFor(clipRootFor(root))) == nullptr) {
        rebuildNoteSlots(root);
    }
    QSGClipNode* clipRoot = clipRootFor(root);
    QSGTransformNode* transformRoot = transformRootFor(clipRoot);
    QSGNode* bodyRoot = contentRootFor(transformRoot);
    if (clipRoot != nullptr) {
        clipRoot->setClipRect(QRectF(
            state.timelineLeft,
            state.timelineTop,
            qMax(0, state.viewportSize.width() - state.timelineLeft),
            state.timelineHeight));
    }
    if (transformRoot != nullptr) {
        QMatrix4x4 matrix;
        matrix.translate(-static_cast<float>(state.horizontalScrollValue), 0.0f);
        transformRoot->setMatrix(matrix);
    }
    if (root->revision == state.notesRevision && root->appearanceRevision == state.appearanceRevision) {
        return root;
    }
    clearChildren(bodyRoot);

    // DPR for physical-pixel snap. QQuickWindow returns 0 before exposed,
    // in which case fall back to 1.0 (no snapping).
    const qreal dpr = (window != nullptr
                       && window->effectiveDevicePixelRatio() > 0.0)
        ? window->effectiveDevicePixelRatio()
        : 1.0;

    // Firework bands — translucent rect overlays drawn UNDER all
    // sprites. Volume is small (a handful per chart at most), so
    // batching them isn't worth the extra material kind. Keep the
    // legacy QSGOpacityNode + QSGSimpleRectNode pattern.
    for (const auto& rect : state.fireworkBands) {
        auto* opacity = new QSGOpacityNode();
        opacity->setOpacity(0.55f);
        opacity->appendChildNode(buildTimelineRectNode(rect));
        bodyRoot->appendChildNode(opacity);
    }

    // Track sprites + hold spans + touch-hold lines + note sprites.
    // All sprite emits stream through ONE batch builder so adjacent
    // same-texture quads coalesce across all groups (e.g., a track
    // segment sprite of type X immediately followed by a hold cap of
    // type X uses the same texture and merges into one run). The
    // builder flushes between sprite and non-sprite emits, preserving
    // z-order semantics with the legacy path.
    if (textures != nullptr) {
        TimelineQuickSpriteBatchBuilder batch(bodyRoot);

        // Track sprites — slide arrows and segments. Most charts have
        // long runs of the same slide-shape texture, so this batch
        // typically collapses to a small number of nodes.
        for (const auto& sprite : state.trackSprites) {
            const QSize targetSize = textures->noteTargetSize(sprite.spriteType, sprite.scale);
            if (!targetSize.isValid()) continue;
            QSGTexture* tex = textures->noteTexture(
                sprite.spriteType, targetSize, sprite.rotationDegrees, sprite.mirrorX);
            if (tex == nullptr) continue;
            const QSizeF logicalSize = rotatedSpriteLogicalSize(targetSize, sprite.rotationDegrees);
            batch.appendQuad(tex, centeredSpriteRect(sprite.center, logicalSize, dpr));
        }

        // Hold spans. Bodies, left caps, right caps are all distinct
        // textures, so within-hold the batch transitions 3 times. But
        // ACROSS holds of the same (spriteType, holdScale), each piece
        // shares its texture, so the cumulative batches stay small —
        // typically 3 batches total per (spriteType, holdScale) group.
        for (const auto& holdSpan : state.holdSpans) {
            appendHoldSpanBatched(bodyRoot, batch, holdSpan, textures, dpr);
        }

        // Flush before the line emits so touch-hold lines render in
        // their correct z-position (above holds, below note sprites,
        // matching the legacy ordering).
        batch.flush();

        for (const auto& line : state.touchHoldLines) {
            bodyRoot->appendChildNode(buildTimelineLineNode(line));
        }

        // Note sprites — taps, slide heads, holds (the icon at start
        // position). Same batching benefit as track sprites.
        for (const auto& sprite : state.noteSprites) {
            const QSize targetSize = textures->noteTargetSize(sprite.spriteType, sprite.scale);
            if (!targetSize.isValid()) continue;
            QSGTexture* tex = textures->noteTexture(
                sprite.spriteType, targetSize, sprite.rotationDegrees, sprite.mirrorX);
            if (tex == nullptr) continue;
            const QSizeF logicalSize = rotatedSpriteLogicalSize(targetSize, sprite.rotationDegrees);
            batch.appendQuad(tex, centeredSpriteRect(sprite.center, logicalSize, dpr));
        }

        batch.flush();
    } else {
        // No texture cache — still need to emit lines so empty-but-
        // not-failing layouts look right.
        for (const auto& line : state.touchHoldLines) {
            bodyRoot->appendChildNode(buildTimelineLineNode(line));
        }
    }

    root->revision = state.notesRevision;
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
