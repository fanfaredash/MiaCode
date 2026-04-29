#include "sources/chart/HudSource.h"

#include "core/scene/PreviewLayerOrder.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "render/snapshot_builder.h"

#include <QImage>
#include <QPainter>
#include <QtConcurrent/QtConcurrentRun>

#include <chrono>

namespace miacode::sources::chart {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

namespace {

inline qint64 monotonicNanoseconds()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

HudSource::HudSource(QSharedPointer<QImage>* hudImage,
                     qint64* lastHudRebuildNs,
                     QFutureWatcher<QSharedPointer<QImage>>* watcher,
                     bool* inFlight)
    : hudImage_(hudImage),
      lastHudRebuildNs_(lastHudRebuildNs),
      watcher_(watcher),
      inFlight_(inFlight)
{}

bool HudSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return scene::previewRenderLayerEnabled(ctx.layerFlags, scene::HudLayer);
}

void HudSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    if (hudImage_ == nullptr || lastHudRebuildNs_ == nullptr
        || watcher_ == nullptr || inFlight_ == nullptr) {
        return;
    }

    // Rasterise at PHYSICAL pixel size (logical × DPR), not logical.
    // DComp's viewport is physical pixels, so a logical-size texture
    // gets upscaled by DPR through bilinear filtering and the text
    // looks blurry. The bigger texture maps 1:1 to physical viewport
    // pixels = crisp text. paintPreviewHudOverlay's font scaling
    // hinges on shortSide / kHudReferenceShortSide (1024), so feeding
    // it the larger size auto-scales fonts to the right pixel size.
    const qreal hudDpr = ctx.devicePixelRatio > 0.0 ? ctx.devicePixelRatio : 1.0;
    const QSize hudPixelSize(
        qMax(1, qRound(ctx.logicalSize.width() * hudDpr)),
        qMax(1, qRound(ctx.logicalSize.height() * hudDpr)));

    constexpr qint64 kHudRebuildIntervalNs = 200LL * 1000LL * 1000LL;
    const qint64 nowNs = monotonicNanoseconds();
    const bool needsRebuild =
        !(*hudImage_)
        || (*hudImage_)->size() != hudPixelSize
        || *lastHudRebuildNs_ == 0
        || (nowNs - *lastHudRebuildNs_) >= kHudRebuildIntervalNs;
    if (needsRebuild && !*inFlight_) {
        // Post the rebuild to a worker thread. Cost on the GUI hot
        // path is one PreviewFrameState copy (cheap due to Qt COW
        // containers + std::shared_ptr refcounts) plus a
        // QtConcurrent::run task post (~µs). The synchronous QPainter
        // rasterisation that used to live here moves off the GUI
        // thread entirely; the result lands in the surface's
        // onHudRebuildFinished slot, which is connected to the
        // watcher's `finished` signal.
        const scene::PreviewFrameState stateCopy = ctx.frameState;
        const auto layerFlagsCopy = ctx.layerFlags;
        const QSize hudPixelSizeCopy = hudPixelSize;
        *inFlight_ = true;
        *lastHudRebuildNs_ = nowNs;
        auto future = QtConcurrent::run(
            [stateCopy, layerFlagsCopy, hudPixelSizeCopy]() {
                auto fresh = QSharedPointer<QImage>::create(
                    hudPixelSizeCopy,
                    QImage::Format_RGBA8888_Premultiplied);
                fresh->fill(Qt::transparent);
                QPainter p(fresh.data());
                miacode::preview::hud::paintPreviewHudOverlay(
                    p, stateCopy, hudPixelSizeCopy, layerFlagsCopy);
                p.end();
                return fresh;
            });
        watcher_->setFuture(future);
    }
    if (*hudImage_ && !(*hudImage_)->isNull()) {
        snapshot.retainedImages.append(*hudImage_);
        scene::PreviewSpriteDescriptor hud;
        hud.image = (*hudImage_).data();
        hud.center = QPointF(ctx.logicalSize.width() / 2.0,
                              ctx.logicalSize.height() / 2.0);
        hud.width = ctx.logicalSize.width();
        hud.height = ctx.logicalSize.height();
        hud.rotationDegrees = 0.0;
        hud.opacity = 1.0;
        hud.effect = scene::PreviewAnimatedSpriteEffect::None;
        hud.cacheable = true;
        scene::PreviewSpriteDescriptors batch;
        batch.append(hud);
        sb::pushSpriteBatch(snapshot, batch);
    }
}

}  // namespace miacode::sources::chart
