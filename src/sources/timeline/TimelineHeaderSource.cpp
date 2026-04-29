#include "sources/timeline/TimelineHeaderSource.h"

#include "core/scene/PreviewSpriteDescriptor.h"
#include "render/snapshot_builder.h"
#include "sources/timeline/TimelineLabelCache.h"

namespace miacode::sources::timeline {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

TimelineHeaderSource::TimelineHeaderSource(TimelineLabelCache* labelCache)
    : labelCache_(labelCache)
{}

bool TimelineHeaderSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineHeaderSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;

    // Triangles → real GPU pipeline (Phase 3d-1).
    sb::pushTimelineTriangleBatch(snapshot, state->headerMarkers);

    // Text labels → CPU-rasterise to QImage via the cache, emit as
    // chart-style sprites. Phase 3 / Phase 5 polish may move this to
    // a font-atlas-based GPU pipeline if the per-label QImage cost
    // becomes a hot spot.
    if (labelCache_ == nullptr) {
        return;
    }
    const auto rasteriseLabels =
        [&](const QVector<miacode::timeline::TimelineSceneTextLabel>& labels) {
            scene::PreviewSpriteDescriptors batch;
            batch.reserve(labels.size());
            for (const auto& label : labels) {
                if (label.text.isEmpty()
                    || label.logicalSize.width() <= 0.0
                    || label.logicalSize.height() <= 0.0) {
                    continue;
                }
                auto image = labelCache_->lookupOrRasterise(
                    label.text, label.font, label.color,
                    label.logicalSize, ctx.devicePixelRatio);
                if (!image || image->isNull()) {
                    continue;
                }
                snapshot.retainedImages.append(image);
                scene::PreviewSpriteDescriptor sprite;
                sprite.image = image.data();
                sprite.center = QPointF(
                    label.topLeft.x() + label.logicalSize.width() / 2.0,
                    label.topLeft.y() + label.logicalSize.height() / 2.0);
                sprite.width = label.logicalSize.width();
                sprite.height = label.logicalSize.height();
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
    rasteriseLabels(state->laneLabels);
    rasteriseLabels(state->headerLabels);
}

}  // namespace miacode::sources::timeline
