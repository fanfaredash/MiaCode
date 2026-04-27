#pragma once

#include "preview/dcomp/PreviewDCompCore.h"
#include "preview/dcomp/PreviewDCompRenderer.h"

#include <QObject>
#include <QPointer>
#include <QSize>

class QQuickWindow;

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

    // Releases all resources and disconnects from the window. Idempotent.
    void detach();

    bool isActive() const;

private slots:
    void onWindowSceneGraphInitialized();
    void onWindowGeometryChanged();
    void onWindowVisibilityChanged();

private:
    bool initialiseIfReady();
    void teardownCore();
    QSize currentClientPixelSize() const;
    void* currentParentHwnd() const;

    QPointer<QQuickWindow> window_;
    PreviewDCompCore core_;
    PreviewDCompRenderer renderer_;
    bool initialised_ = false;
};

}  // namespace miacode::preview::dcomp
