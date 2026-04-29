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
#include "timeline/TimelineSceneState.h"

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QVector>

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

    miacode::render::Compositor compositor_;
    bool compositorInitialized_ = false;
};

}  // namespace miacode::preview::dcomp
