#include "sources/timeline/TimelineNotesSource.h"

#include "core/scene/PreviewSpriteDescriptor.h"
#include "render/snapshot_builder.h"
#include "sources/timeline/TimelineSpriteAssetCache.h"

namespace miacode::sources::timeline {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

TimelineNotesSource::TimelineNotesSource(TimelineSpriteAssetCache* assetCache)
    : assetCache_(assetCache)
{}

bool TimelineNotesSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineNotesSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;

    const qreal scrollOffsetX =
        static_cast<qreal>(state->horizontalScrollValue);

    // Phase 4d-fix4 — clarity. Round sprite centres to PHYSICAL-pixel
    // boundaries (via ctx.devicePixelRatio) instead of just integer
    // logical pixels. At fractional DPR (1.25/1.5/1.75) integer-
    // logical centres still produce non-integer physical-pixel
    // corners in the GPU rasteriser → bilinear smear. Snapping to
    // physical pixels keeps the texel-to-pixel mapping 1:1 regardless
    // of DPR.
    const qreal dpr = ctx.devicePixelRatio > 0.0 ? ctx.devicePixelRatio : 1.0;
    const auto snapToPhysicalPixel = [dpr](qreal v) -> qreal {
        return qRound(v * dpr) / dpr;
    };

    // Lambda for note + track sprites. Defined here so it can be
    // invoked in QSG-matching stacking order below.
    const auto rasteriseSprites =
        [&](const QVector<miacode::timeline::TimelineSceneSprite>& sprites) {
            if (assetCache_ == nullptr) return;
            scene::PreviewSpriteDescriptors batch;
            batch.reserve(sprites.size());
            for (const auto& s : sprites) {
                if (s.spriteType.isEmpty()) continue;
                auto image = assetCache_->lookupOrTransform(
                    s.spriteType, s.scale, s.rotationDegrees, s.mirrorX,
                    dpr);
                if (!image || image->isNull()) continue;
                snapshot.retainedImages.append(image);
                scene::PreviewSpriteDescriptor sprite;
                sprite.image = image.data();
                sprite.center = QPointF(
                    snapToPhysicalPixel(s.center.x() - scrollOffsetX),
                    snapToPhysicalPixel(s.center.y()));
                sprite.width = image->width()
                    / (image->devicePixelRatio() > 0.0
                            ? image->devicePixelRatio() : 1.0);
                sprite.height = image->height()
                    / (image->devicePixelRatio() > 0.0
                            ? image->devicePixelRatio() : 1.0);
                // The asset cache already applied scale + rotation +
                // mirror at transform time, so the descriptor's own
                // rotation stays at zero (the bitmap is pre-rotated).
                sprite.rotationDegrees = 0.0;
                sprite.opacity = 1.0;
                sprite.effect = scene::PreviewAnimatedSpriteEffect::None;
                sprite.cacheable = true;
                batch.append(sprite);
            }
            if (!batch.isEmpty()) {
                sb::pushSpriteBatch(snapshot, batch);
            }
        };

    // Lambda for hold spans (textured 3-piece, with fallback rect).
    // Defined here so we can call it in the right stacking position.
    const auto emitHoldSpans = [&]() {
        if (state->holdSpans.isEmpty()) return;
        QVector<miacode::timeline::TimelineSceneRect> holdFallbackRects;
        scene::PreviewSpriteDescriptors holdSpriteBatch;
        holdSpriteBatch.reserve(state->holdSpans.size() * 3);
        for (const auto& span : state->holdSpans) {
            const int x0 = qMin(span.startX, span.endX);
            const int x1 = qMax(span.startX, span.endX);
            if (x1 <= x0) continue;

            TimelineSpriteAssetCache::HoldParts parts;
            if (assetCache_ != nullptr && !span.spriteType.isEmpty()) {
                parts = assetCache_->lookupOrBuildHoldParts(
                    span.spriteType, span.baseIconScale, dpr);
            }

            if (parts.isValid()) {
                const qreal capW = static_cast<qreal>(parts.capLogicalWidth);
                const qreal capH = static_cast<qreal>(parts.capLogicalHeight);
                const qreal capCenterY = span.rowTop + span.laneHeight / 2.0;
                const qreal leftCapCenterX = static_cast<qreal>(span.startX) - capW / 2.0;
                const qreal rightCapCenterX = static_cast<qreal>(span.endX) + capW / 2.0;
                const qreal bodyStartX = static_cast<qreal>(span.startX);
                const qreal bodyEndX = static_cast<qreal>(span.endX);
                const qreal bodyW = qMax<qreal>(0.0, bodyEndX - bodyStartX);

                // Left cap — physical-pixel snap for sub-pixel
                // sharpness (Phase 4d-fix4).
                snapshot.retainedImages.append(parts.leftCap);
                {
                    scene::PreviewSpriteDescriptor s;
                    s.image = parts.leftCap.data();
                    s.center = QPointF(
                        snapToPhysicalPixel(leftCapCenterX - scrollOffsetX),
                        snapToPhysicalPixel(capCenterY));
                    s.width = capW;
                    s.height = capH;
                    s.rotationDegrees = 0.0;
                    s.opacity = 1.0;
                    s.effect = scene::PreviewAnimatedSpriteEffect::None;
                    s.cacheable = true;
                    holdSpriteBatch.append(s);
                }
                // Body slice — don't snap body width, only centre.
                if (bodyW > 0.0) {
                    snapshot.retainedImages.append(parts.bodySlice);
                    scene::PreviewSpriteDescriptor s;
                    s.image = parts.bodySlice.data();
                    s.center = QPointF(
                        snapToPhysicalPixel(bodyStartX + bodyW / 2.0 - scrollOffsetX),
                        snapToPhysicalPixel(capCenterY));
                    s.width = bodyW;
                    s.height = capH;
                    s.rotationDegrees = 0.0;
                    s.opacity = 1.0;
                    s.effect = scene::PreviewAnimatedSpriteEffect::None;
                    s.cacheable = true;
                    holdSpriteBatch.append(s);
                }
                // Right cap
                snapshot.retainedImages.append(parts.rightCap);
                {
                    scene::PreviewSpriteDescriptor s;
                    s.image = parts.rightCap.data();
                    s.center = QPointF(
                        snapToPhysicalPixel(rightCapCenterX - scrollOffsetX),
                        snapToPhysicalPixel(capCenterY));
                    s.width = capW;
                    s.height = capH;
                    s.rotationDegrees = 0.0;
                    s.opacity = 1.0;
                    s.effect = scene::PreviewAnimatedSpriteEffect::None;
                    s.cacheable = true;
                    holdSpriteBatch.append(s);
                }
                continue;
            }

            // Fallback — solid colour rect when assets are missing.
            if (span.fallbackColor.alpha() == 0) continue;
            const qreal w = static_cast<qreal>(span.fallbackWidth > 0.0
                                                    ? span.fallbackWidth
                                                    : 1.0);
            const qreal cy = span.rowTop + span.laneHeight / 2.0;
            const qreal y0 = cy - w * 0.5;
            const qreal y1 = cy + w * 0.5;
            miacode::timeline::TimelineSceneRect r;
            r.rect = QRectF(QPointF(x0, y0), QPointF(x1, y1));
            r.color = span.fallbackColor;
            holdFallbackRects.append(r);
        }
        if (!holdSpriteBatch.isEmpty()) {
            sb::pushSpriteBatch(snapshot, holdSpriteBatch);
        }
        if (!holdFallbackRects.isEmpty()) {
            sb::pushTimelineRectBatch(snapshot, holdFallbackRects);
        }
    };

    // Phase 9a-fix1 — laneOverlayRects are emitted at viewport-relative
    // X coordinates (state.timelineLeft + viewport-width span), and in
    // the QSG path live OUTSIDE the gridTransformRoot, so they never
    // translate with scroll. The DComp TimelineRects batch applies a
    // unified Phase-8 scroll translate to all rects, which would push
    // these off-screen as the user scrolls. Bake +scroll into their X
    // coords here so the pipeline's -scroll cancels out and they stay
    // pinned to the viewport — matching the QSG `staticRoot` placement.
    if (scrollOffsetX != 0.0 && !state->laneOverlayRects.isEmpty()) {
        QVector<miacode::timeline::TimelineSceneRect> shifted;
        shifted.reserve(state->laneOverlayRects.size());
        for (const auto& r : state->laneOverlayRects) {
            miacode::timeline::TimelineSceneRect copy = r;
            copy.rect.translate(scrollOffsetX, 0.0);
            shifted.append(copy);
        }
        sb::pushTimelineRectBatch(snapshot, shifted);
    } else {
        sb::pushTimelineRectBatch(snapshot, state->laneOverlayRects);
    }
    // fireworkBands ARE chart-content (each band is positioned at a
    // firework trigger second) and MUST scroll with the chart.
    sb::pushTimelineRectBatch(snapshot, state->fireworkBands);

    // Phase 4d-fix5 — stacking order matches the QSG NotesLayer in
    // TimelineQuickNotesLayer.cpp:appendChildNode order:
    //   1. fireworkBands (drawn above)
    //   2. trackSprites + trackLines   ← under holds + notes
    //   3. holdSpans                   ← above tracks, under notes
    //   4. touchHoldLines              ← above holds, under notes
    //   5. muriDots                    ← in QSG these live in the
    //      OverlayLayer (drawn AFTER notes). For DComp parity we
    //      emit them after notes too — see below.
    //   6. noteSprites                 ← top of NotesLayer
    //
    // Previous order had trackSprites at the very END, making slide
    // arrows render OVER taps/holds — inverted from QSG canonical.
    rasteriseSprites(state->trackSprites);
    sb::pushTimelineLineBatch(snapshot, state->trackLines);
    emitHoldSpans();
    sb::pushTimelineLineBatch(snapshot, state->touchHoldLines);
    rasteriseSprites(state->noteSprites);
    // muriDots live in QSG OverlayLayer (drawn after the NotesLayer).
    // The DComp Overlay source is z=4 > Notes z=2, so emitting muriDots
    // here means they paint after note sprites within this source. The
    // next-source overlay (z=4) still paints after, preserving the
    // QSG-relative "muri above notes" appearance.
    sb::pushTimelineGlyphBatch(snapshot, state->muriDots);
}

}  // namespace miacode::sources::timeline
