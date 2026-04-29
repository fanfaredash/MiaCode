#pragma once

#include "render/backend_d3d11/PreviewDCompCore.h"
#include "render/PreviewDCompRenderer.h"
#include "core/scene/PreviewHeadLayerState.h"
#include "core/scene/PreviewLayerOrder.h"
#include "core/scene/PreviewPreparedSceneCache.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QSharedPointer>
#include <QSize>
#include <QTimer>

#include <atomic>

class QQuickItem;
class QQuickWindow;
class PreviewRuntime;

namespace miacode::preview::dcomp {

// Phase 1 orchestrator. Holds a PreviewDCompCore and ties its lifecycle to
// a QQuickWindow:
//   - on attach: get the parent HWND, initialise the Core, render the
//     test frame.
//   - on QQuickWindow geometry change: resize the swap chain + update the
//     visual transform.
//   - on detach / window destroyed: shutdown the Core.
//
// Env-flag gated: only created (and only initialise()d) when
// MIACODE_PREVIEW_USE_DCOMP=1. Without the flag, the class is harmless —
// no D3D11 device created, no DComp visual attached, no behavioural
// difference vs. the legacy QSG path.
//
// Phase 1 scope: render a static red 200×200 rectangle pinned to the
// top-left of the QQuickWindow's client area, scaling proportionally as
// the window resizes. Confirms the visual tree is correctly attached and
// behaves under resize. Phase 4 replaces the fixed top-left geometry with
// a QML placeholder driving the visual's transform.
class PreviewDCompSurface : public QObject
{
    Q_OBJECT
public:
    explicit PreviewDCompSurface(QObject* parent = nullptr);
    ~PreviewDCompSurface() override;

    // Bind the surface to a QQuickWindow. Connects to its size-change
    // signals and, if the window is already showing, immediately
    // initialises the DComp visual. If the window is not yet exposed,
    // initialisation is deferred until the first sceneGraphInitialized.
    void attachToWindow(QQuickWindow* window);

    // Phase 3.2: hand the surface a PreviewRuntime. The surface listens
    // to frameStateChanged on the GUI thread, builds a thread-safe
    // snapshot, and publishes it to the renderer (which the render
    // thread reads at the top of each frame). Pass nullptr to detach.
    void setRuntime(PreviewRuntime* runtime);

    // Phase 3.6: per-layer enable bitmap. Mirrors PreviewQuickSceneRoot's
    // layerFlags_; defaults to kPreviewAllRenderLayers for live preview.
    // Each onRuntimeFrameStateChanged() honours the current flags before
    // pushing a batch, so a disabled layer skips both the build cost
    // and the draw-list contribution. Used by the future export path
    // when DComp replaces QSG there too.
    void setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags flags);

    // Releases all resources and disconnects from the window. Idempotent.
    void detach();

    bool isActive() const;

private slots:
    void onWindowSceneGraphInitialized();
    void onWindowGeometryChanged();
    void onWindowVisibilityChanged();
    void onRuntimeFrameStateChanged();
    void onRendererPresented(qint64 emittedAtNs);
    void onTrackedItemGeometryChanged();
    void onHudRebuildFinished();

private:
    // Internal publish helper. emittedAtNs is the render thread's
    // monotonic Present timestamp when this call was triggered by
    // onRendererPresented; 0 for direct calls (e.g., setRuntime, the
    // runtime → surface queued connection). When non-zero it's used
    // for the renderPlayheadSeconds_ delta so the clock advances by
    // the actual vsync interval rather than the GUI-thread queued-
    // dispatch interval (which inherits scheduler jitter).
    void buildAndPublishSnapshot(qint64 emittedAtNs);

    bool initialiseIfReady();
    void teardownCore();
    QSize currentClientPixelSize() const;
    void* currentParentHwnd() const;

    // Phase 4a — try to find the QQuickItem the DComp surface should
    // shadow on screen. Looks up by objectName so the surface stays
    // decoupled from PreviewQuickSceneRoot's type. Called whenever a
    // window-level lifecycle signal fires; once an item is tracked
    // these calls are short-circuit.
    void tryDiscoverTrackedItem();
    void setTrackedItem(QQuickItem* item);
    void applyTrackedItemGeometry();

    QPointer<QQuickWindow> window_;
    QPointer<QQuickItem> trackedItem_;
    QPointer<PreviewRuntime> runtime_;

    // When previewDCompTopLevelHwndEnabled, we create a top-level
    // borderless transparent owned popup HWND, give the DComp visual
    // tree to *that* HWND, and reposition it via MoveWindow on QML
    // geometry change. nullptr when the feature is off (visual is
    // parented to the QQuickWindow's HWND directly).
    // (Name is historical — kept until Phase 3 renames to popupHwnd_.)
    void* childHwnd_ = nullptr;
    QMetaObject::Connection runtimeFrameStateConnection_;
    QVector<QMetaObject::Connection> trackedItemConnections_;
    // Wall-clock timestamp of the last snapshot publish. Used by
    // diagnostics; the publish path itself does NOT throttle.
    qint64 lastPublishNs_ = 0;

    // Render-thread playhead clock. Advances at wall-clock rate
    // between publish calls; drifts toward the runtime's audio-time
    // playhead via per-publish drift correction; snaps when drift
    // exceeds threshold (seek / pause / resume). This decouples the
    // rendered playhead from GUI-tick scheduler jitter — sampled at
    // uniform vsync intervals (because publishes are vsync-driven),
    // it advances by uniform amounts per sample, eliminating the
    // 6-8 ms stddev seen in playhead_delta_stats with the old
    // path that read frameState_.playheadSeconds directly.
    double renderPlayheadSeconds_ = 0.0;
    qint64 renderPlayheadLastSampleNs_ = 0;
    bool renderPlayheadInitialized_ = false;

    // Phase 4f — HUD overlay rasterised here (was QQuickPaintedItem on
    // QSG). The legacy PreviewQuickHudLayer's draw logic now lives in
    // the standalone paintPreviewHudOverlay free function; we run it
    // every ~200 ms into hudImage_ and push that as a sprite at the
    // top of the snapshot's draw batches. Between rebuilds the same
    // QImage's cacheKey stays stable so the texture cache hits.
    QSharedPointer<QImage> hudImage_;
    qint64 lastHudRebuildNs_ = 0;
    PreviewDCompCore core_;
    PreviewDCompRenderer renderer_;
    qint64 snapshotRevision_ = 0;
    bool initialised_ = false;

    // Phase 3.4 — sprite-layer windowing. Mirrors PreviewQuickSceneRoot's
    // per-frame cache + per-layer cursor pattern: preparedCache_.sync()
    // rebuilds when the chart content changes, after which all cursors
    // are reset; otherwise each cursor advances incrementally with the
    // playhead so per-frame layer assembly only walks active markers,
    // not the full chart.
    miacode::preview::scene::PreviewPreparedSceneCache preparedCache_;
    miacode::preview::scene::PreviewLayerWindowCursor guideCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor headCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor trackCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor slideMotionCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor judgeEffectCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor judgeFireworkCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor touchCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor touchJudgeCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor touchHoldCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor chartReviewCursor_;
    miacode::preview::scene::PreviewLayerWindowCursor maimuriDxJudgeCursor_;

    // Head layer composites tinted base + overlay images per marker; the
    // cache short-circuits redundant compositions when the same triplet
    // (base, overlay, tint) recurs across frames. Lives on the GUI
    // thread (built here, never touched by the render thread).
    miacode::preview::scene::PreviewHeadRenderAssetCache headRenderAssetCache_;

    // Phase 3.6 — per-layer enable bitmap. All layers on by default.
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags_ =
        miacode::preview::scene::kPreviewAllRenderLayers;

    // Diagnostic: count Qt's QQuickWindow::frameSwapped emissions so we
    // can compare Qt's Present rate to DComp's Present rate at runtime.
    // Incremented on the QSG render thread (DirectConnection), read on
    // the GUI thread by the periodic logger. Used to disambiguate
    // "DWM compositing two swap chains" from "Qt's render loop is
    // hot driving Presents". See logPresentRateDiagnostic().
    std::atomic<qint64> qtFrameSwapCount_{ 0 };
    QMetaObject::Connection qtFrameSwapDiagConnection_;
    QTimer presentRateDiagTimer_;
    qint64 lastDiagDCompFrames_ = 0;
    qint64 lastDiagQtFrames_ = 0;
    qint64 lastDiagNs_ = 0;
    void logPresentRateDiagnostic();

    // Per-tick timing of onRuntimeFrameStateChanged. Reset by
    // logPresentRateDiagnostic each second. Tells us whether the slow
    // GUI tick rate (~46 Hz vs the 60 Hz target) is from the snapshot
    // build itself (high tickBuildNs_) or from somewhere else (build
    // is fast but rate is still low, meaning slot dispatch is delayed).
    qint64 tickBuildCount_ = 0;
    qint64 tickBuildSumNs_ = 0;
    qint64 tickBuildMaxNs_ = 0;

    // Inter-call gap distribution on the renderer-driven slot path.
    // Tells us how often the GUI thread fails to dispatch a queued
    // `presented` signal within a vsync window — the actual root
    // cause of the playhead-delta clustering. avg ≈ 16.67 ms = healthy,
    // long_gap_count > 0 = GUI thread is being blocked by something
    // else (Qt sync handoff, timeline tick, paint events, …).
    qint64 lastPresentedSlotNs_ = 0;
    qint64 presentedGapCount_ = 0;
    qint64 presentedGapSumNs_ = 0;
    qint64 presentedGapMaxNs_ = 0;
    qint64 presentedGapMinNs_ = 0;
    qint64 presentedGapLongCount_ = 0;  // gaps > 20 ms (one missed vsync)
    qint64 presentedGapShortCount_ = 0;  // gaps < 8 ms (queue clusters)

    // Dispatch latency: time from render thread emitting `presented`
    // to the GUI-thread slot starting to run. High latency means the
    // GUI thread was blocked when the signal was queued — the actual
    // root cause of the long_gap pattern. The slot-time inside
    // tick_build_stats only measures what we did, not what kept the
    // GUI thread busy beforehand.
    qint64 presentedLatencyCount_ = 0;
    qint64 presentedLatencySumNs_ = 0;
    qint64 presentedLatencyMaxNs_ = 0;
    qint64 presentedLatencyMinNs_ = 0;
    qint64 presentedLatencyLongCount_ = 0;  // > 5 ms latency

    // HUD rebuild timing — wraps the QImage allocation + fill +
    // paintPreviewHudOverlay block inside onRuntimeFrameStateChanged.
    // Fires every 200 ms (≈ 5/s), so if max_ms is in the 5-25 ms range
    // it's likely the residual presented_latency long_count source we
    // see after the overlay-cache fix landed. If max_ms is < 1 ms,
    // HUD isn't the suspect and we look elsewhere.
    qint64 hudRebuildCount_ = 0;
    qint64 hudRebuildSumNs_ = 0;
    qint64 hudRebuildMaxNs_ = 0;

    // Async HUD rebuild. paintPreviewHudOverlay's full QPainter raster
    // costs ~5 ms (text rendering dominates) and historically ran
    // synchronously inside onRuntimeFrameStateChanged at the 200 ms
    // boundary, contributing ~5 of the residual presented_latency
    // long_count events per second. Now we kick off a QtConcurrent::run
    // task that builds the QImage on a worker thread, the watcher's
    // `finished` signal swaps it into hudImage_ on the GUI thread.
    // While the worker is in flight, we don't queue another rebuild —
    // worst case the HUD updates slightly later, which is invisible
    // for an FPS overlay.
    QFutureWatcher<QSharedPointer<QImage>> hudRebuildWatcher_;
    bool hudRebuildInFlight_ = false;
};

}  // namespace miacode::preview::dcomp
