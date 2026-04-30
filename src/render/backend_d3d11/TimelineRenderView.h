#pragma once

// Phase 3c of the v2-refactor — sibling of PreviewDCompSurface for the
// timeline pane. Owns its own DComp surface (Core + Renderer + texture
// cache via the renderer), and runs the same OBS-style source ↔
// compositor walk via the 5 timeline IPreviewSource implementations
// registered on construction.
//
// Differences from PreviewDCompSurface:
//   - No PreviewRuntime hookup. Timeline state arrives via
//     setSceneState(); the bridge that builds TimelineSceneState
//     (TimelineQuickStateBridge) emits renderStateChanged() whenever
//     it changes.
//   - No render-thread playhead clock. Timeline has no per-frame
//     animation between state changes — the playhead-line position
//     is part of the state, so a publish on state change is enough.
//   - No HUD overlay. The HUD is preview-specific.
//   - No chart cursors / head-render-asset cache. Timeline has no
//     active-marker windowing.
//
// Same as PreviewDCompSurface:
//   - Top-level HWND default (Phase 3a).
//   - Tracked-QQuickItem geometry shadowing for placement under the
//     editor's QML timeline pane.
//   - Visibility-based pause via PreviewDCompRenderer::setPaused.
//   - Cross-thread UAF protection on shutdown (rendererPresentedConnection_
//     disconnected in destructor before the render thread joins).

#include "render/backend_d3d11/PreviewDCompCore.h"
#include "render/PreviewDCompRenderer.h"
#include "render/compositor.h"
#include "sources/timeline/TimelineLabelCache.h"
#include "sources/timeline/TimelineSpriteAssetCache.h"
#include "timeline/TimelineSceneState.h"

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTimer>
#include <QVector>

#include <climits>

class QQuickItem;
class QQuickWindow;

namespace miacode::preview::dcomp {

class TimelineRenderView : public QObject
{
    Q_OBJECT
public:
    explicit TimelineRenderView(QObject* parent = nullptr);
    ~TimelineRenderView() override;

    // Bind the view to a QQuickWindow. Same pattern as
    // PreviewDCompSurface::attachToWindow — connects to its
    // size-change signals and, if the window is exposed, immediately
    // initialises the DComp visual. Looks up the tracked
    // QQuickItem with objectName "timeline_dcomp_track_target" via
    // QObject::findChild and shadows its geometry.
    void attachToWindow(QQuickWindow* window);

    // Hand over a fresh timeline scene state. Triggers a snapshot
    // publish if the view is initialised. Cheap — the state struct
    // contains Qt COW vectors plus a few scalars; no allocation in
    // steady state.
    void setSceneState(const miacode::timeline::TimelineSceneState& state);

    // Direct tracked-item set. Avoids the findChild-by-objectName
    // dance for callers that already hold a pointer (e.g. the
    // QQuickItem itself owning this view). Wires the same geometry
    // signals as the discover path.
    void setTrackedQuickItem(QQuickItem* item);

    // Releases all resources and disconnects from the window. Idempotent.
    void detach();

    bool isActive() const;

private slots:
    void onWindowSceneGraphInitialized();
    void onWindowGeometryChanged();
    void onWindowVisibilityChanged();
    void onRendererPresented(qint64 emittedAtNs);
    void onTrackedItemGeometryChanged();

private:
    void buildAndPublishSnapshot();
    bool initialiseIfReady();
    void teardownCore();
    QSize currentClientPixelSize() const;
    void* currentParentHwnd() const;
    void tryDiscoverTrackedItem();
    void setTrackedItem(QQuickItem* item);
    void applyTrackedItemGeometry();
    void ensureCompositorInitialized();

    QPointer<QQuickWindow> window_;
    QPointer<QQuickItem> trackedItem_;
    void* childHwnd_ = nullptr;
    QMetaObject::Connection rendererPresentedConnection_;
    QVector<QMetaObject::Connection> trackedItemConnections_;

    miacode::timeline::TimelineSceneState sceneState_;
    bool sceneStateValid_ = false;

    PreviewDCompCore core_;
    PreviewDCompRenderer renderer_;
    qint64 snapshotRevision_ = 0;
    bool initialised_ = false;
    // Phase 3e-fix — Win32 reentry guard for initialiseIfReady. Inside
    // currentParentHwnd, Win32 calls (CreateWindowExW + ShowWindow +
    // SetLayeredWindowAttributes) can pump messages, which Qt
    // dispatches as queued events. If one of those queued events calls
    // initialiseIfReady recursively (e.g., a windowChanged slot fires
    // because a parent reparented us during the popup-creation pump),
    // both calls would see initialised_=false (the first hasn't yet
    // reached its `initialised_=true` line), both would create
    // separate popup HWNDs, and we'd end up with two visible-but-
    // empty popups stacked on top of each other in DWM.
    bool initialising_ = false;

    miacode::render::Compositor compositor_;
    bool compositorInitialized_ = false;

    // Phase 3d-2 — CPU label rasterisation cache. Owned by the view
    // so all timeline sources that produce text (currently only
    // TimelineHeaderSource for laneLabels + headerLabels) share one
    // cache and one set of QImages. Lifetime tied to the view; sources
    // hold a non-owning pointer.
    miacode::sources::timeline::TimelineLabelCache labelCache_;

    // Phase 3d-3 — sprite asset cache. Wraps TimelineNoteAssets
    // (loaded lazily on first lookup) and caches transformed
    // QImages keyed on (type, scale, rotation, mirror). Shared by
    // TimelineNotesSource for both noteSprites and trackSprites.
    miacode::sources::timeline::TimelineSpriteAssetCache spriteAssetCache_;

    // Phase 3f-1 — startup visibility gate. The popup HWND is created
    // hidden in currentParentHwnd; applyTrackedItemGeometry calls
    // MoveWindow as the QQuickItem's anchors-fill resolves through the
    // QML layout cascade (observed: 10+ rapid geometry transitions over
    // ~230 ms), but the popup stays SW_HIDE the whole time. A debounce
    // timer (single-shot, restarted on every geometry change) only
    // ShowWindows the popup once geometry has been quiescent for one
    // settle window, eliminating the "black square dancing around
    // during startup" jank the user reported.
    //
    // Once shown, popupShown_ stays true for the lifetime of the
    // popup; subsequent geometry changes (e.g., user resizes the
    // editor pane) re-fire MoveWindow but the popup stays visible.
    QTimer popupVisibilityDebounce_;
    bool popupShown_ = false;
    int lastAppliedXPx_ = INT_MIN;
    int lastAppliedYPx_ = INT_MIN;
    QSize lastAppliedPixelSize_;
};

}  // namespace miacode::preview::dcomp
