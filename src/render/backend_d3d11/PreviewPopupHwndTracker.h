#pragma once

// Phase 4e — owner-followed popup HWND coordinator.
//
// Background. Both the chart-preview surface (PreviewDCompSurface) and the
// timeline (TimelineRenderView) are top-level borderless popup HWNDs owned
// by the editor's main HWND. Their geometry is driven by Qt signals
// (xChanged/widthChanged on the tracked QQuickItem) which queue through
// the GUI event loop and lag the editor window's DWM compositor tick by
// 1-3 frames during drag/maximize/restore. The user sees a visible shear
// between the editor frame and the popups during the animation.
//
// Solution (matches WebView2 Visual hosting and Chromium Aura). Hook
// WM_WINDOWPOSCHANGED on the editor HWND — the canonical "your window
// state changed" message fired once per DWM tick during animations.
// Inside the hook, batch-reposition every registered popup via a single
// BeginDeferWindowPos / DeferWindowPos / EndDeferWindowPos pair so all
// popups commit in the same DWM tick the editor itself does.
//
// Why batched DeferWindowPos. Per Win32 docs, multiple SetWindowPos
// calls each bump the compositor; a deferred batch commits atomically.
// With two popups (chart + timeline), the batched path eliminates the
// inter-popup tear that would otherwise be visible during a fast drag.
//
// Thread safety. All methods run on the GUI thread:
//   - registerSurface/unregisterSurface called from PreviewDCompSurface /
//     TimelineRenderView attach/detach (both GUI-thread).
//   - notifyOwnerWindowPosChanged called from the QAbstractNativeEventFilter
//     installed in QuickShellBootstrap, which Qt routes through QApplication
//     on the GUI thread.
// No mutex needed for the registry.

// Pull in QtGlobal first so Q_OS_WIN is defined before the conditional
// below decides whether to include <windows.h>. Without this, callers
// that include this header before any other Qt header would see the
// no-Win stub branch even on Windows — and the .cpp would then provide
// a second definition out-of-line, blowing up with C2084 redefinitions.
#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <functional>

namespace miacode::preview::dcomp {

class PreviewPopupHwndTracker
{
public:
#ifdef Q_OS_WIN
    // The callback receives a reference to the in-flight HDWP. It should
    // compute its popup's target screen rect and either call
    // ::DeferWindowPos to extend the batch (updating hdwp to the new
    // handle and returning true), or — if the surface is not ready or
    // geometry hasn't changed — leave hdwp untouched and return false.
    // Set hdwp = nullptr to signal an error; the caller will skip and
    // continue with remaining entries.
    //
    // Why bool instead of comparing return-vs-input HDWP: on modern
    // Windows, ::DeferWindowPos almost always returns the SAME handle
    // pointer on success (the handle is an internal ID, not a heap
    // pointer that gets reallocated). Pointer-comparison heuristics
    // can't distinguish "DeferWindowPos succeeded" from "callback
    // early-out'd without doing anything", which produced misleading
    // extends=0 noops=2 diagnostic readings on a working setup.
    using DeferCallback = std::function<bool(HDWP& hdwp)>;

    // Register a surface. `token` must be unique per registered entry —
    // pass the surface's `this` pointer. If the same token is already
    // registered the callback is replaced (idempotent re-attach).
    static void registerSurface(void* token, DeferCallback callback);

    // Unregister. Must be called from the surface's detach() before the
    // popup HWND is destroyed, otherwise notifyOwnerWindowPosChanged
    // would call DeferWindowPos on a stale HWND.
    static void unregisterSurface(void* token);

    // Called by the native event filter on every WM_WINDOWPOSCHANGED
    // for the owner window. No-op when no surfaces are registered.
    static void notifyOwnerWindowPosChanged();
#else
    using DeferCallback = std::function<void()>;
    static void registerSurface(void*, DeferCallback) {}
    static void unregisterSurface(void*) {}
    static void notifyOwnerWindowPosChanged() {}
#endif
};

}  // namespace miacode::preview::dcomp
