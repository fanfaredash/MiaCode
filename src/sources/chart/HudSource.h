#pragma once

#include "render/source.h"

#include <QFutureWatcher>
#include <QSharedPointer>

class QImage;

namespace miacode::sources::chart {

// z=15: FPS / debug HUD overlay. Wraps the asynchronous QPainter-based
// rasterisation that lives in PreviewDCompSurface. Draws on top of
// every chart layer, mirroring the legacy QSG path's HudLayer (z=2 in
// PreviewQuickSceneRoot's child list — the highest there).
//
// Pattern: the QPainter rasterisation is too expensive (~5 ms text
// rendering) to run on the GUI hot path every frame, so it's posted to
// a worker thread via QtConcurrent::run at ~5 Hz (200 ms cadence). The
// surface owns:
//   - hudImage_         — the most recent rasterised QImage (read by
//                         this source for the sprite push)
//   - lastHudRebuildNs_ — gating timestamp (read+written by this
//                         source's contributeToSnapshot)
//   - watcher_          — QFutureWatcher whose `finished` signal is
//                         handled by PreviewDCompSurface::onHudRebuildFinished
//                         (which writes hudImage_)
//   - inFlight_         — guard against posting a second rebuild while
//                         the worker is still running
//
// HudSource holds non-owning pointers to those four fields. Single-
// threaded GUI access throughout, so no locking.
class HudSource final : public render::IPreviewSource
{
public:
    HudSource(QSharedPointer<QImage>* hudImage,
              qint64* lastHudRebuildNs,
              QFutureWatcher<QSharedPointer<QImage>>* watcher,
              bool* inFlight);

    int zOrder() const override { return 15; }
    bool isEnabled(const render::PreviewBuildContext& ctx) const override;
    void contributeToSnapshot(
        const render::PreviewBuildContext& ctx,
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot) override;

private:
    QSharedPointer<QImage>* hudImage_;
    qint64* lastHudRebuildNs_;
    QFutureWatcher<QSharedPointer<QImage>>* watcher_;
    bool* inFlight_;
};

}  // namespace miacode::sources::chart
