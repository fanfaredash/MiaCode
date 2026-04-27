#pragma once

#include "preview/dcomp/PreviewDCompCore.h"
#include "preview/dcomp/PreviewDCompRenderer.h"
#include "preview/scene/PreviewHeadLayerState.h"
#include "preview/scene/PreviewLayerOrder.h"
#include "preview/scene/PreviewPreparedSceneCache.h"

#include <QObject>
#include <QPointer>
#include <QSize>

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
    void onTrackedItemGeometryChanged();

private:
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
    QMetaObject::Connection runtimeFrameStateConnection_;
    QVector<QMetaObject::Connection> trackedItemConnections_;
    // Phase 4b-perf: timestamp of the last snapshot publish, used to
    // throttle rebuild work when the runtime emits frameStateChanged
    // faster than the render thread can consume it.
    qint64 lastPublishNs_ = 0;

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
};

}  // namespace miacode::preview::dcomp
