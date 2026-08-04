#pragma once

#include <QString>
#include <QWindow>
#include <Qt>

// Window visibility / occlusion transition diagnostics for the DEFAULT (QSG) render path.
//
// Why this exists: the reported freeze happens while MiaCode is minimized or sitting
// behind a browser that is playing video — i.e. "quiet app, busy machine" — and the
// default QSG path has no occlusion awareness whatsoever, so that condition is currently
// invisible in the logs. (DXGI_STATUS_OCCLUDED is handled only in the DComp backend,
// which is off by default and no longer maintained, so it never runs for users.)
//
// Design: transition-triggered only. One line per visibility / expose / window-state
// change, never per frame. The exit transition carries how long the surface stayed
// occluded, which is the number the freeze report needs.
namespace miacode::app::window_diag {

// Stable log tokens for QWindow::Visibility / Qt::WindowStates. Kept as free functions so
// the log vocabulary is pinned by a spec instead of by whatever Qt's enum ordering is.
QString visibilityName(QWindow::Visibility visibility);
QString windowStatesText(Qt::WindowStates states);

// True for the visibility values that mean "this surface is not presenting to the user":
// Minimized and Hidden. This is the condition symptom 1 is reported under.
bool isOccludedVisibility(QWindow::Visibility visibility);

// Build the transition payload. Pure, so the exact key set an operator greps is covered
// by a spec on every platform. `occludedForMs < 0` omits the duration field (it is only
// meaningful on the transition OUT of an occluded state).
QString visibilityTransitionPayload(
    const QString& surface,
    QWindow::Visibility previous,
    QWindow::Visibility current,
    bool exposed,
    bool active,
    Qt::WindowStates states,
    qint64 occludedForMs);

// Attach the observer to `window`. No-op when runtime debug output is off, when `window`
// is null, or when the window already has an observer. `surface` names the surface in the
// log (e.g. "root_window", "preview_composite") so two surfaces can be told apart.
void installWindowVisibilityDiagnostics(QWindow* window, const QString& surface);

// Record, once, that a surface keeps its graphics + scene-graph resources resident while
// hidden. Worth stating explicitly in the log: QuickShellPreviewCompositeSurface sets
// setPersistentGraphics(true) / setPersistentSceneGraph(true), so minimizing does NOT
// release VRAM — which matters a great deal on a 2 GB card shared with an encoder.
void logSurfaceGraphicsPersistence(
    const QString& surface, bool persistentGraphics, bool persistentSceneGraph);

}  // namespace miacode::app::window_diag
