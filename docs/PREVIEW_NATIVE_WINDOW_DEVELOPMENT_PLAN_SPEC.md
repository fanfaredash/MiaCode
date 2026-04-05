# Preview Native Window Development Plan

## Context

MiaCode's realtime preview is hosted as a `QQuickView` embedded into the widget UI through `QWidget::createWindowContainer()`.

That embedding model is convenient for the current architecture, but it also means the preview is a native child window with Qt-documented stacking, focus, and performance limitations.

## Problem Statement

On some Windows setups, opening native dialogs could immediately turn the embedded realtime preview black. The reproduced triggers included:

- `Open File`
- `Export`
- `Audio Settings`
- `Video Settings`
- `Preferences`

The black screen appeared when the dialog was shown, not only after a document switch or a preview rebuild.

## Investigation Summary

We added runtime instrumentation in these files:

- `src/app/mainwindow/MainWindow.cpp`
- `src/app/mainwindow/sections/document/MainWindow.DocumentFlow.cpp`
- `src/preview/runtime/PreviewQuickRuntimeSurface.cpp`
- `src/preview/quick_scene/PreviewQuickSceneRoot.cpp`
- `src/app/main.cpp`

Primary runtime tags:

- `window/dialog_event`
- `window/native`
- `window/native_related`
- `window/native_hook`
- `preview/host_window_event`
- `preview/embedded_refresh`
- `preview/quick_runtime`
- `preview/quick_scene`
- `startup/qt_config`

Observed facts from the logs:

1. The Quick scene graph was not being destroyed or invalidated when the black screen happened.
2. The preview host window remained visible and exposed.
3. The failure correlated with native window activity around dialog presentation.
4. Forcing `QSG_RENDER_LOOP=basic` was not the decisive fix.
5. Enabling `Qt::AA_DontCreateNativeWidgetSiblings` before `QApplication` construction removed the black-screen repro in the user's validated A/B run.

## External Evidence

Relevant upstream/community references line up with the local findings:

- [Qt `QWidget::createWindowContainer()` docs](https://doc.qt.io/qt-6/qwidget.html#createWindowContainer) document that the container becomes a native child window and call out stacking-order, focus-chain, and performance limitations.
- [Qt `Qt::AA_DontCreateNativeWidgetSiblings` docs](https://doc.qt.io/qt-6/qt.html#ApplicationAttribute-enum) document that the attribute keeps siblings of native widgets non-native unless explicitly requested.
- [Qt Forum: "Using QGLWidget and QQuickWidget a the same time?"](https://forum.qt.io/topic/41799/using-qglwidget-and-qquickwidget-a-the-same-time) reports Windows black rendering once `createWindowContainer()` is involved and points to `Qt::AA_DontCreateNativeWidgetSiblings` as a workaround.
- [Qt Forum: "QML App stops refreshing after screen sleep"](https://forum.qt.io/topic/129780/qml-app-stops-refreshing-after-screen-sleep) reports device-specific black or frozen `QQuickView + createWindowContainer()` behavior on one Windows 10 IoT tablet while other Windows machines keep working, which matches the cross-device compatibility concern.

## Decision

Adopt `Qt::AA_DontCreateNativeWidgetSiblings` as the default startup behavior for the embedded preview host.

Implementation rule:

- Set the attribute before constructing `QApplication`.
- Keep `startup/qt_config` logging so the effective startup state is always visible in debug logs.
- Preserve an explicit opt-out env var for regression A/B:
  - `MIACODE_PREVIEW_DISABLE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS=1`

## Delivery Steps

### Step 1. Default Startup Fix

Status: Done in code.

- Enable `Qt::AA_DontCreateNativeWidgetSiblings` by default in `src/app/main.cpp`.
- Log the default state, opt-out state, and effective state through `startup/qt_config`.

### Step 2. Keep Investigation Hooks

Status: Keep for now.

- Retain the runtime black-screen instrumentation while we continue validating dialog flows and mixed-DPI/multi-monitor machines.
- Do not remove the A/B env knobs until we have enough confidence from user reports.

### Step 3. Validation Sweep

Status: Next verification pass.

Run debug-mode validation on at least these flows:

- `Open File`
- `Export`
- `Audio Settings`
- `Video Settings`
- `Preferences`
- dialog open/close while preview is playing
- dialog open/close while preview is paused
- file-open cancel path with no document change

Watch for:

- preview black frame or frozen frame
- repeated `preview/quick_runtime action=frame_stall`
- unexpected scene invalidation or rebuild
- regressions in focus return after closing dialogs

## Longer-Term Follow-Up

This fix stabilizes the current embedding model, but it does not remove the structural limitations of `createWindowContainer()`.

If native-window regressions continue on other machines, the next design discussion should compare:

- keeping the current embedded `QQuickView` path with defensive startup defaults
- moving the preview host away from `createWindowContainer()`-style embedding
- reducing native child and sibling interactions around modal flows

## References

- [Qt `QWidget::createWindowContainer()` docs](https://doc.qt.io/qt-6/qwidget.html#createWindowContainer)
- [Qt `Qt::AA_DontCreateNativeWidgetSiblings` docs](https://doc.qt.io/qt-6/qt.html#ApplicationAttribute-enum)
- [Qt Forum: "Using QGLWidget and QQuickWidget a the same time?"](https://forum.qt.io/topic/41799/using-qglwidget-and-qquickwidget-a-the-same-time)
- [Qt Forum: "QML App stops refreshing after screen sleep"](https://forum.qt.io/topic/129780/qml-app-stops-refreshing-after-screen-sleep)
- Local runtime debug logs collected during the black-screen investigation
