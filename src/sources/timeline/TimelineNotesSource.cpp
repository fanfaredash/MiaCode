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

    // === Solid-fill primitives (Phase 3d-1 GPU pipeline) ===
    sb::pushTimelineRectBatch(snapshot, state->laneOverlayRects);
    sb::pushTimelineRectBatch(snapshot, state->fireworkBands);
    sb::pushTimelineLineBatch(snapshot, state->trackLines);
    sb::pushTimelineLineBatch(snapshot, state->touchHoldLines);
    sb::pushTimelineGlyphBatch(snapshot, state->muriDots);

    // === Hold spans → fallback colour rects (Phase 3d-3 simplified) ===
    // The QSG path renders hold spans as left-cap + tiled body +
    // right-cap textured pixmaps. For DComp we currently emit each
    // span as a single solid rect using the descriptor's fallback
    // colour and width — the same fallback the QSG path uses when
    // an asset is missing. Visual fidelity is acceptable because the
    // hold-span colour matches the note colour family. Phase 5 polish
    // can layer the proper textured caps + body if pixel parity
    // becomes important.
    if (!state->holdSpans.isEmpty()) {
        QVector<miacode::timeline::TimelineSceneRect> holdRects;
        holdRects.reserve(state->holdSpans.size());
        for (const auto& span : state->holdSpans) {
            if (span.fallbackColor.alpha() == 0) continue;
            const int x0 = qMin(span.startX, span.endX);
            const int x1 = qMax(span.startX, span.endX);
            if (x1 <= x0) continue;
            const qreal w = static_cast<qreal>(span.fallbackWidth > 0.0
                                                    ? span.fallbackWidth
                                                    : 1.0);
            const qreal cy = span.rowTop + span.laneHeight / 2.0;
            const qreal y0 = cy - w * 0.5;
            const qreal y1 = cy + w * 0.5;
            miacode::timeline::TimelineSceneRect r;
            r.rect = QRectF(QPointF(x0, y0), QPointF(x1, y1));
            r.color = span.fallbackColor;
            holdRects.append(r);
        }
        sb::pushTimelineRectBatch(snapshot, holdRects);
    }

    // === Note + track sprites → CPU-rasterised pixmaps as sprites ===
    if (assetCache_ == nullptr) return;
    const auto rasteriseSprites =
        [&](const QVector<miacode::timeline::TimelineSceneSprite>& sprites) {
            scene::PreviewSpriteDescriptors batch;
            batch.reserve(sprites.size());
            for (const auto& s : sprites) {
                if (s.spriteType.isEmpty()) continue;
                auto image = assetCache_->lookupOrTransform(
                    s.spriteType, s.scale, s.rotationDegrees, s.mirrorX);
                if (!image || image->isNull()) continue;
                snapshot.retainedImages.append(image);
                scene::PreviewSpriteDescriptor sprite;
                sprite.image = image.data();
                sprite.center = s.center;
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
    rasteriseSprites(state->noteSprites);
    rasteriseSprites(state->trackSprites);
}

}  // namespace miacode::sources::timeline
