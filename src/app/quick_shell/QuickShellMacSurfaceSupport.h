#pragma once

// macOS-only helper that neutralizes the orphan native window left behind by a
// QuickShell bridge surface.
//
// Background: each bridge surface (top-chrome / sidebar / workspace / bottom-tabs
// / status) is a top-level QWidget created with the Qt::Tool flag so it owns a
// native window whose winId can be adopted by a QML WindowContainer. On Windows
// this reparents the child HWND cleanly and the original top-level frame is
// consumed. On macOS, Qt::Tool maps to an NSPanel; WindowContainer reparents the
// panel's *content NSView* into the QQuickWindow (so the menu/sidebar/editor show
// embedded correctly), but the now-empty NSPanel shell is NOT destroyed. It
// lingers as a system-coloured (white in light mode) floating block that covers
// the UI, blocks clicks, does not track the main window's move/resize, and
// vanishes on deactivate (the tell-tale NSPanel hidesOnDeactivate behaviour).
//
// The fix keeps the content NSView untouched and only neutralises the empty
// panel shell. See QuickShellNativeSurfaceHost for the capture/retry wiring.
//
// These declarations are pure C++ (no Objective-C types) so the host can call
// them without being an Objective-C++ translation unit. The implementation
// (QuickShellMacSurfaceSupport.mm) is compiled only on APPLE.
//
// `nativeViewHandle` is the surface's content NSView as returned by
// QWindow::winId() (cast to void*). It MUST come from the *foreign* QWindow
// (QWindow::fromWinId) adopted by the WindowContainer, whose winId is stable
// across adoption — unlike the bridge QWidget's own window, whose contentView Qt
// may swap out during reparenting. What changes over time is only which NSWindow
// that stable view reports as its parent ([view window]).

namespace miacode::quick_shell::mac {

// Capture the native NSPanel currently hosting `nativeViewHandle` (at construction
// time, before any QML adoption). Returns an opaque, non-owning handle to that
// NSWindow, or nullptr if unavailable. The panel is owned by Qt's platform window
// and outlives the capture, so no retain is taken.
void* captureOrphanShellWindow(void* nativeViewHandle);

// Neutralize the captured orphan panel — but only once the view has been
// reparented OUT of it (i.e. adopted by the QML WindowContainer).
//
// Returns:
//   true  — captured handle was null (nothing to do), or the panel was
//           successfully neutralised (made invisible + click-through).
//   false — the view is still inside the captured panel, so hiding it now would
//           also hide the embedded content; the caller should retry.
//
// `capturedPanel` must be the value previously returned by
// captureOrphanShellWindow(nativeViewHandle).
bool neutralizeOrphanShellWindow(void* nativeViewHandle, void* capturedPanel);

// Force the NSView's hidden state directly, bypassing Qt's cached QWindow
// visibility. Needed because the WindowContainer's initial setVisible(false)
// can be clobbered by the native reparent during adoption: Qt then believes
// the window is hidden while the NSView is actually showing (validation page
// painted over the QML timeline at startup). Idempotent.
void setContentViewHidden(void* nativeViewHandle, bool hidden);

}  // namespace miacode::quick_shell::mac
