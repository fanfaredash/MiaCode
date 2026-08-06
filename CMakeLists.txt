cmake_minimum_required(VERSION 3.21)

set(MIACODE_VERSION_MAJOR "1")
set(MIACODE_VERSION_MINOR "1")
set(MIACODE_VERSION_PATCH "0")
# Single source of truth for the prerelease tag. Both the package
# filename (MiaCode-v<version>-win64.zip via package-win.ps1) and the
# user-visible "About" dialog read this. Beta20 unified the previously
# split MIACODE_VERSION_PRERELEASE / MIACODE_DISPLAY_PRERELEASE pair —
# they always drifted apart in practice (e.g. beta20 zip shipping with
# beta19 in the About dialog because only one variable got bumped).
set(MIACODE_VERSION_PRERELEASE "beta.7-test.4")
set(MIACODE_BUILD_LAYOUT_REVISION "2026-04-21-first-unification")
set(MIACODE_VERSION "${MIACODE_VERSION_MAJOR}.${MIACODE_VERSION_MINOR}.${MIACODE_VERSION_PATCH}")
set(MIACODE_VERSION_STRING "${MIACODE_VERSION}")
if (NOT "${MIACODE_VERSION_PRERELEASE}" STREQUAL "")
    set(MIACODE_VERSION_STRING "${MIACODE_VERSION}-${MIACODE_VERSION_PRERELEASE}")
endif()
# Display version is identical to the package version. Kept as a
# distinct preprocessor define so consumers in `app.setApplicationVersion`
# and the About dialog don't need a code change; they just both resolve
# to the same string now.
set(MIACODE_DISPLAY_VERSION_STRING "${MIACODE_VERSION_STRING}")

if (APPLE)
    # MiaCode's macOS release is Apple-Silicon-only. Keeping this fixed before
    # project() also makes every bundled dependency and helper inherit the same
    # architecture contract instead of silently following the build machine.
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Supported macOS architecture" FORCE)
endif()

project(MiaCode VERSION ${MIACODE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum macOS deployment version" FORCE)
endif()

if (MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"
    )
    add_compile_options(/MP)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(MIACODE_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico" CACHE FILEPATH "Optional application icon (.ico) for Windows builds")
set(MIACODE_MACOS_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.icns" CACHE FILEPATH "Optional application icon (.icns) for macOS bundles")
option(MIACODE_BUILD_DEV_TOOLS "Build developer helper tools" OFF)

# Dev-tool spec executables register themselves with CTest (see the
# MIACODE_BUILD_DEV_TOOLS block below). enable_testing() must be called at
# top level for `ctest` to discover them; it is harmless when no tests exist.
enable_testing()

set(MIACODE_GIT_REVISION "unknown")
set(MIACODE_GIT_DIRTY "unknown")
find_package(Git QUIET)
if (GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE MIACODE_GIT_REVISION_RESULT
        OUTPUT_VARIABLE MIACODE_GIT_REVISION_OUTPUT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (MIACODE_GIT_REVISION_RESULT EQUAL 0
        AND NOT "${MIACODE_GIT_REVISION_OUTPUT}" STREQUAL "")
        set(MIACODE_GIT_REVISION "${MIACODE_GIT_REVISION_OUTPUT}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal -- .
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE MIACODE_GIT_DIRTY_RESULT
            OUTPUT_VARIABLE MIACODE_GIT_DIRTY_OUTPUT
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if (MIACODE_GIT_DIRTY_RESULT EQUAL 0)
            if ("${MIACODE_GIT_DIRTY_OUTPUT}" STREQUAL "")
                set(MIACODE_GIT_DIRTY "0")
            else()
                set(MIACODE_GIT_DIRTY "1")
            endif()
        endif()
    endif()
endif()

configure_file(
    src/app/AppVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/AppVersion.h
    @ONLY
)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network OpenGL Multimedia Quick Qml QuickControls2 ShaderTools Svg)

if (APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    find_library(MIACODE_AGL_FRAMEWORK NAMES AGL)
    if (NOT MIACODE_AGL_FRAMEWORK)
        # Xcode 27 SDK no longer ships AGL.framework, but Qt 6.8's
        # WrapOpenGL helper still injects -framework AGL on macOS.
        # QtGui already links AppKit/OpenGL, so drop the stale AGL edge.
        set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "")
    endif()
endif()

# MultimediaQuickPrivate exposes the private QtMultimedia headers
# (qhwvideobuffer_p.h / qvideotexturehelper_p.h) that QtAVPlayer's
# QAVVideoFrame -> QVideoFrame bridge needs. Optional: only the Windows
# QtAVPlayer preview backend uses it (see the QtAVPlayer block below).
if (WIN32)
    find_package(Qt6 6.8 REQUIRED COMPONENTS MultimediaQuickPrivate)
endif()

add_executable(MiaCode WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/startup_diagnostics_win32.cpp
    src/app/graphics_backend.cpp
    src/app/process_identity.cpp
    src/app/WindowsIdleEventDiagnostics.h
    src/app/WindowsIdleEventDiagnostics.cpp
    src/app/WindowVisibilityDiagnostics.h
    src/app/WindowVisibilityDiagnostics.cpp
    src/app/gpu_adapter_probe.cpp
    src/app/gpu_device_provider.cpp
    src/app/gpu_high_performance_hint.cpp
    src/app/cli_shared.cpp
    src/app/cli_video_export.cpp
    src/app/cli_video_export_worker.cpp
    src/app/quick_shell/QuickShellBootstrap.h
    src/app/quick_shell/QuickShellBootstrap.cpp
    src/app/quick_shell/QuickShellContracts.h
    src/app/quick_shell/QuickShellNativeSurfaceHost.h
    src/app/quick_shell/QuickShellNativeSurfaceHost.cpp
    src/app/quick_shell/QuickShellMacSurfaceSupport.h
    src/app/quick_shell/QuickShellPreviewCompositeSurface.h
    src/app/quick_shell/QuickShellPreviewCompositeSurface.cpp
    src/app/quick_shell/QuickShellPreviewSurfacePolicy.h
    src/app/quick_shell/QuickShellController.h
    src/app/quick_shell/QuickShellController.cpp
    src/app/quick_shell/QuickShellStyleBridge.h
    src/app/quick_shell/QuickShellStyleBridge.cpp
    src/common/AdoptedWidgetCoordinates.h
    src/common/AdoptedWidgetCoordinates.cpp
    src/app/mainwindow/MainWindow.h
    src/app/mainwindow/MainWindow.cpp
    src/app/mainwindow/MainWindowShared.h
    src/app/mainwindow/MainWindowShared.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentFlow.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentFileFlow.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentDesignerFlow.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentAutosaveFlow.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentEditorState.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentTransforms.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp
    src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp
    src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.AudioSettings.cpp
    src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.MediaTools.cpp
    src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.TrackMetadata.cpp
    src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.ExportSettings.cpp
    src/app/mainwindow/sections/dialogs/MainWindow.DialogsSection.cpp
    src/app/mainwindow/sections/editor/MainWindow.EditorDisplay.cpp
    src/app/mainwindow/sections/editor/MainWindow.EditorState.cpp
    src/app/mainwindow/sections/export/MainWindow.ExportFlow.cpp
    src/app/mainwindow/sections/export/MainWindow.ExportSnapshot.cpp
    src/app/mainwindow/sections/export/MainWindow.ExportSection.cpp
    src/app/mainwindow/sections/export/MainWindow.ExportWorker.cpp
    src/app/mainwindow/sections/export/MainWindow.PackZip.cpp
    src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp
    src/app/mainwindow/sections/frame/MainWindow.ExtensionHostRequests.cpp
    src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp
    src/app/mainwindow/sections/frame/MainWindow.FrameBootstrapFinalize.cpp
    src/app/mainwindow/sections/preferences/MainWindow.PreferencesDialog.cpp
    src/app/mainwindow/sections/preferences/MainWindow.WelcomeDialog.cpp
    src/app/mainwindow/sections/preview/MainWindow.PreviewStageMediaRoute.cpp
    src/app/mainwindow/sections/preview/MainWindow.PreviewWarmupAndSettings.cpp
    src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelinePreviewFollowSync.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelineAnalysisFlow.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelineQuickParse.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelineLayout.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelineFramePacing.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelineFullscreenUI.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelineLayoutUI.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelineForwarding.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp
    src/app/mainwindow/sections/timeline/MainWindow.PreviewSeek.cpp
    src/app/mainwindow/sections/timeline/MainWindow.PreviewPlaybackState.cpp
    src/app/mainwindow/sections/timeline/MainWindow.PreviewIntroRegion.cpp
    src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp
    src/app/mainwindow/sections/timeline/MainWindow.PreviewPlaybackGlue.cpp
    src/app/mainwindow/sections/validation/MainWindow.ValidationFlow.cpp
    src/app/mainwindow/sections/validation/MainWindow.ValidationListUi.cpp
    src/app/mainwindow/sections/validation/MainWindow.ValidationRender.cpp
    src/app/mainwindow/sections/validation/MainWindow.ValidationRuntime.cpp
    src/app/mainwindow/sections/window/MainWindow.BottomTabsHost.cpp
    src/app/mainwindow/sections/window/MainWindow.WindowInteraction.cpp
    src/app/mainwindow/sections/window/MainWindow.WindowRuntime.cpp
    src/app/mainwindow/sections/window/MainWindow.WindowSection.cpp
    src/app/mainwindow/sections/window/MainWindow.WindowShell.cpp
    src/app/ui/UiText.h
    src/app/ui/UiText.cpp
    src/app/ui/AppBackgroundSettings.h
    src/app/ui/AppBackgroundSettings.cpp
    src/app/ui/AppBackgroundPainter.h
    src/app/ui/AppBackgroundPainter.cpp
    src/app/ui/ShortcutRegistry.h
    src/app/ui/ShortcutRegistry.cpp
    src/app/ui/UiTheme.h
    src/app/ui/UiTheme.cpp
    src/app/ui/UiComponents.h
    src/app/ui/UiComponents.cpp
    src/app/ui/UiNativeWindowTheme.h
    src/app/ui/UiNativeWindowTheme.cpp
    src/app/ui/UiNativeWindowThemePolicy.h
    src/app/ui/WindowParityMetrics.h
    src/app/ui/WindowParityMetrics.cpp
    src/app/ui/FlowLayout.h
    src/app/ui/FlowLayout.cpp
    src/app/ui/EditableValueLabel.h
    src/app/ui/EditableValueLabel.cpp
    src/app/ui/BusySpinner.h
    src/app/ui/BusySpinner.cpp
    src/common/MuriTypes.h
    src/common/MuriTypes.cpp
    src/common/MuriRenderOptions.h
    src/common/MuriConfig.h
    src/common/DebugLog.h
    src/common/DebugLog.cpp
    src/common/UiHangWatchdog.h
    src/common/UiHangWatchdog.cpp
    src/common/UiHangWatchdogPolicy.h
    src/common/ThreadStackCapture.h
    src/common/ThreadStackCapture.cpp
    src/common/ProcessDiagnostics.h
    src/common/ProcessDiagnostics.cpp
    src/common/GpuDevicePolicy.h
    src/common/GpuDevicePolicy.cpp
    src/common/OperationLog.h
    src/common/OperationLog.cpp
    src/common/CrashRecovery.h
    src/common/CrashRecovery.cpp
    src/common/Mmcss.h
    src/common/Mmcss.cpp
    src/common/DebugImageCompare.h
    src/common/InputShortcutGesture.h
    src/common/InputShortcutGesture.cpp
    src/common/ChartAssetPaths.h
    src/common/Id3TagReader.h
    src/common/Id3TagReader.cpp
    src/common/TimelineThemeConfig.h
    src/common/WaveformCache.h
    src/common/WaveformCache.cpp
    src/common/ProjectPreferences.h
    src/common/ProjectPreferences.cpp
    src/common/PreviewTimingSettings.h
    src/common/ChartClockCount.h
    src/common/PreviewSfxTiming.h
    src/common/PreviewSfxTimeline.h
    src/editor/PlainCodeEditor.h
    src/editor/PlainCodeEditor.cpp
    src/editor/PlainCodeEditor.Layout.cpp
    src/editor/PlainCodeEditor.HighlightAndCaret.cpp
    src/editor/PlainCodeEditor.BracketCompletion.cpp
    src/editor/PlainCodeEditor.Input.cpp
    src/editor/PlainCodeEditor.Bookmarks.cpp
    src/editor/TouchPadAuthoringEdit.h
    src/editor/TouchPadAuthoringEdit.cpp
    src/editor/BracketCompletionPopup.h
    src/editor/BracketCompletionPopup.cpp
    src/editor/SimaiCompletionCatalog.h
    src/editor/SimaiCompletionCatalog.cpp
    src/editor/BracketScopeHighlighter.h
    src/editor/BracketScopeHighlighter.cpp
    src/tools/latency/LatencyAnalysis.h
    src/tools/latency/LatencyAnalysis.cpp
    src/tools/latency/LatencyTestChartBuilder.h
    src/tools/latency/LatencyTestChartBuilder.cpp
    src/tools/latency/LatencySandboxController.h
    src/tools/latency/LatencySandboxController.cpp
    src/tools/latency/LatencyDetectionPage.h
    src/tools/latency/LatencyDetectionPage.cpp
    src/tools/export_page/ExportLauncherPage.h
    src/tools/export_page/ExportLauncherPage.cpp
    src/tools/muri/MuriAnalyzer.h
    src/tools/muri/MuriAnalyzer.cpp
    src/tools/muri/MuriAnalyzerGeometry.h
    src/tools/muri/MuriAnalyzerGeometry.cpp
    src/tools/muri/MuriAnalyzerModel.h
    src/tools/muri/MuriSlideReferenceData.h
    src/tools/muri/MuriSlideReferenceData.cpp
    src/tools/muri/MuriAnalyzerInternal.h
    src/tools/muri/MuriDiagnosticCollector.h
    src/tools/muri/MuriDiagnosticCollector.cpp
    src/tools/muri/MuriDiagnosticLabels.h
    src/tools/muri/MuriDiagnosticLabels.cpp
    src/tools/muri/MuriRuntimeModelBuilder.h
    src/tools/muri/MuriRuntimeModelBuilder.cpp
    src/tools/muri/MuriOverlayBuilder.h
    src/tools/muri/MuriOverlayBuilder.cpp
    src/tools/muri/MuriSlideWifiJudge.h
    src/tools/muri/MuriSlideWifiJudge.cpp
    src/tools/muri/MuriSimpleNoteJudge.h
    src/tools/muri/MuriSimpleNoteJudge.cpp
    src/tools/muri/MuriPanelEntries.h
    src/tools/muri/MuriPanelEntries.cpp
    src/tools/muri/MuriStaticChecker.h
    src/tools/muri/MuriStaticChecker.cpp
    src/tools/video_export/VideoExportDialog.h
    src/tools/video_export/VideoExportDialog.cpp
    src/tools/video_export/VideoExportDialog.SettingsPersistence.cpp
    src/tools/video_export/VideoExportDialog.IntroControls.cpp
    src/tools/video_export/VideoExportDialog.ExportFlow.cpp
    src/tools/video_export/HudFontSettings.h
    src/tools/video_export/HudFontSettings.cpp
    src/tools/video_export/FontLibrary.h
    src/tools/video_export/FontLibrary.cpp
    src/tools/video_export/CardFontSettings.h
    src/tools/video_export/CardFontSettings.cpp
    src/tools/video_export/BatchExportPanel.h
    src/tools/video_export/BatchExportTaskLayout.h
    src/tools/video_export/BatchExportTaskLayout.cpp
    src/tools/video_export/BatchExportSelectionState.cpp
    src/tools/video_export/BatchExportPanel.cpp
    src/tools/video_export/VideoExportController.h
    src/tools/video_export/VideoExportController.cpp
    src/tools/video_export/VideoExportEncoder.cpp
    src/tools/video_export/VideoExportDiagnostics.cpp
    src/tools/video_export/VideoExportFrameRender.cpp
    src/tools/video_export/VideoExportPipeline.cpp
    src/tools/video_export/VideoExportPreparedTask.cpp
    src/tools/video_export/VideoExportRuntimePolicy.h
    src/tools/video_export/VideoExportRuntimePolicy.cpp
    src/tools/video_export/VideoExportQuickRenderBackend.h
    src/tools/video_export/VideoExportQuickRenderBackend.cpp
    src/tools/video_export/IntroPreviewWidget.h
    src/tools/video_export/IntroPreviewWidget.cpp
    src/tools/video_export/RawVideoPipeTransport.h
    src/tools/video_export/RawVideoPipeTransport.cpp
    src/tools/video_export/VideoExportSnapshot.h
    src/tools/video_export/VideoExportSnapshot.cpp
    src/tools/zip_export/ChartZipPackager.h
    src/tools/zip_export/ChartZipPackager.cpp
    src/tools/net/NetClient.h
    src/tools/net/NetClient.cpp
    src/tools/net/NetBatchDownloadWorker.h
    src/tools/net/NetBatchDownloadWorker.cpp
    src/tools/net/NetBatchDownloadDialog.h
    src/tools/net/NetBatchDownloadDialog.cpp
    src/tools/net/NetBatchUploadWorker.h
    src/tools/net/NetBatchUploadScanner.h
    src/tools/net/NetBatchUploadScanner.cpp
    src/tools/net/NetUploadDiagnostics.h
    src/tools/net/NetUploadDiagnostics.cpp
    src/tools/net/NetBatchUploadWorker.cpp
    src/tools/net/NetBatchUploadDialog.h
    src/tools/net/NetBatchUploadDialog.cpp
    src/tools/media/PvBatchCompressionScanner.h
    src/tools/media/PvBatchCompressionScanner.cpp
    src/tools/media/PvBatchCompressionWorker.h
    src/tools/media/PvBatchCompressionWorker.cpp
    src/tools/media/PvBatchCompressionDialog.h
    src/tools/media/PvBatchCompressionDialog.cpp
    src/tools/cover_export/CoverLayoutModel.h
    src/tools/cover_export/CoverLayoutModel.cpp
    src/tools/cover_export/CoverCompositionState.h
    src/tools/cover_export/CoverCompositionState.cpp
    src/tools/cover_export/CoverCompositionPersistenceGuard.h
    src/tools/cover_export/CoverCompositionPersistenceGuard.cpp
    src/tools/cover_export/CoverLayerListModel.h
    src/tools/cover_export/CoverLayerListModel.cpp
    src/tools/cover_export/CoverLayerListPanel.h
    src/tools/cover_export/CoverLayerListPanel.cpp
    src/tools/cover_export/CoverInspectorPanel.h
    src/tools/cover_export/CoverInspectorPanel.cpp
    src/tools/cover_export/CoverFramePickerPanel.h
    src/tools/cover_export/CoverFramePickerPanel.cpp
    src/tools/cover_export/CoverStudioPanel.h
    src/tools/cover_export/CoverStudioPanel.cpp
    src/tools/cover_export/CoverStudioWindow.h
    src/tools/cover_export/CoverStudioWindow.cpp
    src/tools/cover_export/CoverComposerView.h
    src/tools/cover_export/CoverComposerView.cpp
    src/tools/cover_export/SceneFrameRenderer.h
    src/tools/cover_export/SceneFrameRenderer.cpp
    src/tools/cover_export/ExportCoverDialog.h
    src/tools/cover_export/ExportCoverDialog.cpp
    src/preview/runtime/PreviewQuickExportSession.h
    src/preview/runtime/PreviewQuickExportSession.cpp
    src/preview/runtime/PreviewQuickD3D11ExportSession.h
    src/preview/runtime/PreviewQuickD3D11ExportSession.cpp
    src/preview/runtime/PreviewSceneAssetLoader.h
    src/preview/runtime/PreviewSceneAssetLoader.cpp
    src/preview/runtime/PreviewSceneAssetRepository.h
    src/preview/runtime/PreviewSceneAssetRepository.cpp
    src/preview/runtime/PreviewStageMediaHost.h
    src/preview/runtime/PreviewStageMediaHost.cpp
    src/preview/runtime/PreviewStageMediaHost_Backend.cpp
    src/preview/runtime/PreviewStageMediaHost_Media.cpp
    src/preview/runtime/PreviewStageMediaHost_Playback.cpp
    src/preview/runtime/PreviewStageMediaHost_Diagnostics.cpp
    src/preview/runtime/PreviewStageMediaHost_Timeout.cpp
    src/preview/runtime/PreviewSharedD3D11Device.h
    src/preview/runtime/PreviewSharedD3D11Device.cpp
    src/preview/runtime/PreviewRuntime.h
    src/preview/runtime/PreviewRuntime.cpp
    src/core/scene/PreviewFrameState.h
    src/core/scene/PreviewHudState.h
    src/core/scene/PreviewHudState.cpp
    src/core/scene/PreviewProgressStatsCache.h
    src/core/scene/PreviewProgressStatsCache.cpp
    src/core/scene/PreviewPreparedSceneCache.h
    src/core/scene/PreviewPreparedSceneCache.cpp
    src/core/scene/PreviewLayerOrder.h
    src/core/scene/PreviewOpacityCurves.h
    src/core/scene/PreviewOpacityCurves.cpp
    src/core/scene/PreviewSceneConstants.h
    src/core/scene/PreviewSceneMath.h
    src/core/scene/PreviewSceneMath.cpp
    src/core/scene/PreviewSceneGeometry.h
    src/core/scene/PreviewSceneGeometry.cpp
    src/core/scene/PreviewMarkerDrawOrder.h
    src/core/scene/PreviewMarkerDrawOrder.cpp
    src/core/scene/PreviewSkinSelectors.h
    src/core/scene/PreviewSkinSelectors.cpp
    src/core/scene/PreviewSpriteDescriptor.h
    src/core/scene/PreviewCircleDescriptor.h
    src/core/scene/PreviewSectorDescriptor.h
    src/core/scene/PreviewTrackShared.h
    src/core/scene/PreviewTrackShared.cpp
    src/core/scene/PreviewJudgeOverlayShared.h
    src/core/scene/PreviewJudgeOverlayShared.cpp
    src/core/scene/PreviewGuideLayerState.h
    src/core/scene/PreviewGuideLayerState.cpp
    src/core/scene/PreviewTrackLayerState.h
    src/core/scene/PreviewTrackLayerState.cpp
    src/core/scene/PreviewSlideMotionLayerState.h
    src/core/scene/PreviewSlideMotionLayerState.cpp
    src/core/scene/PreviewJudgeEffectLayerState.h
    src/core/scene/PreviewJudgeEffectLayerState.cpp
    src/core/scene/PreviewChartReviewLayerState.h
    src/core/scene/PreviewChartReviewLayerState.cpp
    src/core/scene/PreviewJudgeFireworkLayerState.h
    src/core/scene/PreviewJudgeFireworkLayerState.cpp
    src/core/scene/PreviewMuriPadLayerState.h
    src/core/scene/PreviewMuriPadLayerState.cpp
    src/core/scene/PreviewMuriActionLayerState.h
    src/core/scene/PreviewMuriActionLayerState.cpp
    src/core/scene/PreviewTouchJudgeLayerState.h
    src/core/scene/PreviewTouchJudgeLayerState.cpp
    src/core/scene/PreviewTouchLayerState.h
    src/core/scene/PreviewTouchLayerState.cpp
    src/core/scene/PreviewAnimatedSpriteHelpers.h
    src/core/scene/PreviewAnimatedSpriteHelpers.cpp
    src/core/scene/PreviewArcDescriptor.h
    src/core/scene/PreviewHeadLayerState.h
    src/core/scene/PreviewHeadLayerState.cpp
    src/core/scene/PreviewTouchHoldLayerState.h
    src/core/scene/PreviewTouchHoldLayerState.cpp
    src/core/scene/PreviewMaimuriDxJudgeLayerState.h
    src/core/scene/PreviewMaimuriDxJudgeLayerState.cpp
    src/preview/quick_scene/PreviewTextureRepository.h
    src/preview/quick_scene/PreviewTextureRepository.cpp
    src/preview/quick_scene/PreviewTextureGenerationPolicy.h
    src/preview/quick_scene/PreviewQuickStageBackgroundLayer.h
    src/preview/quick_scene/PreviewQuickStageBackgroundLayer.cpp
    src/preview/quick_scene/PreviewQuickBackdropLayer.h
    src/preview/quick_scene/PreviewQuickBackdropLayer.cpp
    src/preview/quick_scene/PreviewQuickCircleNodes.h
    src/preview/quick_scene/PreviewQuickCircleNodes.cpp
    src/preview/quick_scene/PreviewQuickSpriteNodes.h
    src/preview/quick_scene/PreviewQuickSpriteNodes.cpp
    src/preview/quick_scene/PreviewQuickArcNodes.h
    src/preview/quick_scene/PreviewQuickArcNodes.cpp
    src/preview/quick_scene/PreviewQuickSectorNodes.h
    src/preview/quick_scene/PreviewQuickSectorNodes.cpp
    src/preview/quick_scene/PreviewQuickGuideLayer.h
    src/preview/quick_scene/PreviewQuickGuideLayer.cpp
    src/preview/quick_scene/PreviewQuickTouchHoverLayer.h
    src/preview/quick_scene/PreviewQuickTouchHoverLayer.cpp
    src/preview/quick_scene/PreviewQuickTrackLayer.h
    src/preview/quick_scene/PreviewQuickTrackLayer.cpp
    src/preview/quick_scene/PreviewQuickSlideMotionLayer.h
    src/preview/quick_scene/PreviewQuickSlideMotionLayer.cpp
    src/preview/quick_scene/PreviewQuickJudgeEffectLayer.h
    src/preview/quick_scene/PreviewQuickJudgeEffectLayer.cpp
    src/preview/quick_scene/PreviewQuickChartReviewLayer.h
    src/preview/quick_scene/PreviewQuickChartReviewLayer.cpp
    src/preview/quick_scene/PreviewQuickHeadLayer.h
    src/preview/quick_scene/PreviewQuickHeadLayer.cpp
    src/preview/quick_scene/PreviewQuickTouchLayer.h
    src/preview/quick_scene/PreviewQuickTouchLayer.cpp
    src/preview/quick_scene/PreviewQuickTouchHoldLayer.h
    src/preview/quick_scene/PreviewQuickTouchHoldLayer.cpp
    src/preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.h
    src/preview/quick_scene/PreviewQuickMaimuriDxJudgeLayer.cpp
    src/preview/quick_scene/PreviewQuickMuriPadLayer.h
    src/preview/quick_scene/PreviewQuickMuriPadLayer.cpp
    src/preview/quick_scene/PreviewQuickMuriActionLayer.h
    src/preview/quick_scene/PreviewQuickMuriActionLayer.cpp
    src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.h
    src/preview/quick_scene/PreviewQuickJudgeFireworkLayer.cpp
    src/preview/quick_scene/PreviewQuickTouchJudgeLayer.h
    src/preview/quick_scene/PreviewQuickTouchJudgeLayer.cpp
    src/preview/quick_scene/PreviewQuickHudLayer.h
    src/preview/quick_scene/PreviewQuickHudLayer.cpp
    src/preview/quick_scene/PreviewQuickSceneRoot.h
    src/preview/quick_scene/PreviewQuickSceneRoot.cpp
    src/preview/quick_scene/PreviewQuickSceneInvalidationPolicy.h
    src/render/source.h
    src/render/compositor.h
    src/render/compositor.cpp
    src/render/snapshot_builder.h
    src/sources/chart/StageBackgroundSource.h
    src/sources/chart/StageBackgroundSource.cpp
    src/sources/chart/BackdropSource.h
    src/sources/chart/BackdropSource.cpp
    src/sources/chart/MuriPadSource.h
    src/sources/chart/MuriPadSource.cpp
    src/sources/chart/MuriActionSource.h
    src/sources/chart/MuriActionSource.cpp
    src/sources/chart/JudgeFireworkSource.h
    src/sources/chart/JudgeFireworkSource.cpp
    src/sources/chart/GuideSource.h
    src/sources/chart/GuideSource.cpp
    src/sources/chart/TrackSource.h
    src/sources/chart/TrackSource.cpp
    src/sources/chart/SlideMotionSource.h
    src/sources/chart/SlideMotionSource.cpp
    src/sources/chart/JudgeEffectSource.h
    src/sources/chart/JudgeEffectSource.cpp
    src/sources/chart/TouchJudgeSource.h
    src/sources/chart/TouchJudgeSource.cpp
    src/sources/chart/HeadSource.h
    src/sources/chart/HeadSource.cpp
    src/sources/chart/TouchSource.h
    src/sources/chart/TouchSource.cpp
    src/sources/chart/TouchHoldSource.h
    src/sources/chart/TouchHoldSource.cpp
    src/sources/chart/ChartReviewSource.h
    src/sources/chart/ChartReviewSource.cpp
    src/sources/chart/MaimuriDxJudgeSource.h
    src/sources/chart/MaimuriDxJudgeSource.cpp
    src/sources/chart/HudSource.h
    src/sources/chart/HudSource.cpp
    src/sources/timeline/TimelineGridSource.h
    src/sources/timeline/TimelineGridSource.cpp
    src/sources/timeline/TimelineGridLinesSource.h
    src/sources/timeline/TimelineGridLinesSource.cpp
    src/sources/timeline/TimelineLaneOverlaySource.h
    src/sources/timeline/TimelineLaneOverlaySource.cpp
    src/sources/timeline/TimelineWaveformSource.h
    src/sources/timeline/TimelineWaveformSource.cpp
    src/sources/timeline/TimelineNotesSource.h
    src/sources/timeline/TimelineNotesSource.cpp
    src/sources/timeline/TimelineHeaderSource.h
    src/sources/timeline/TimelineHeaderSource.cpp
    src/sources/timeline/TimelineOverlaySource.h
    src/sources/timeline/TimelineOverlaySource.cpp
    src/sources/timeline/TimelineLabelCache.h
    src/sources/timeline/TimelineLabelCache.cpp
    src/sources/timeline/TimelineSpriteAssetCache.h
    src/sources/timeline/TimelineSpriteAssetCache.cpp
    src/audio/PreviewAudioBackend.h
    src/audio/PreviewAudioSettings.h
    src/audio/PreviewAudioSettings.cpp
    src/audio/MiniaudioPreviewAudioBackend.h
    src/audio/MiniaudioPreviewAudioBackend.cpp
    src/audio/PreviewAudioDeviceChangePolicy.h
    src/audio/PreviewAudioDeviceWatcher.h
    src/audio/PreviewAudioDeviceWatcher.cpp
    src/audio/QtPreviewSfxRuntime.h
    src/audio/QtPreviewSfxRuntime.cpp
    src/core/chart/transform/ChartBatchTransform.h
    src/core/chart/transform/ChartBatchTransform.Parsers.cpp
    src/core/chart/transform/ChartBatchTransform.Subdivision.cpp
    src/core/chart/transform/ChartBatchTransform.Selection.cpp
    src/core/chart/transform/ChartBatchTransform.Transform.cpp
    src/core/chart/transform/ChartNormalization.h
    src/core/chart/transform/ChartNormalization.cpp
    src/core/chart/transform/ChartNormalizationSegmentPolicy.h
    src/core/chart/transform/ChartNormalizationSegmentPolicy.cpp
    src/core/chart/transform/Non384SnapTable.h
    src/core/chart/transform/Non384SnapTable.cpp
    src/core/chart/document/SimaiDocument.h
    src/core/chart/document/SimaiDocument.cpp
    src/core/chart/document/SimaiTimingMetadata.h
    src/core/chart/document/SimaiTimingMetadata.cpp
    src/core/chart/parser/SimaiNativeParser.h
    src/core/chart/parser/SimaiNativeParser.cpp
    src/timeline/TimelineData.h
    src/timeline/TimelineRenderData.h
    src/timeline/TimelineQuickModel.h
    src/timeline/TimelineQuickModel.cpp
    src/timeline/TimelineQuickModelParser.cpp
    src/timeline/TimelineQuickModelSnapshot.cpp
    src/timeline/TimelineQuickModelIndexing.cpp
    src/timeline/TimelineSceneState.h
    src/timeline/TimelineSceneState.cpp
    src/timeline/TimelineSceneStateBuilder.h
    src/timeline/TimelineSceneStateBuilder.cpp
    src/timeline/TimelineNoteAssets.h
    src/timeline/TimelineNoteAssets.cpp
    src/timeline/TimelineSlowRefresh.h
    src/timeline/TimelineSlowRefresh.cpp
    src/timeline/TimelineView.h
    src/timeline/TimelineView.cpp
    src/timeline/quick/TimelineQuickItem.h
    src/timeline/quick/TimelineQuickItem.cpp
    src/timeline/quick/TimelineQuickStateBridge.h
    src/timeline/quick/TimelineQuickStateBridge.cpp
    src/timeline/quick/TimelineQuickTextureCache.h
    src/timeline/quick/TimelineQuickTextureCache.cpp
    src/timeline/quick/TimelineQuickTextureCachePolicy.h
    src/timeline/quick/TimelineQuickLayerUtils.h
    src/timeline/quick/TimelineQuickLayerUtils.cpp
    src/timeline/quick/TimelineQuickGridLayer.h
    src/timeline/quick/TimelineQuickGridLayer.cpp
    src/timeline/quick/TimelineQuickWaveformLayer.h
    src/timeline/quick/TimelineQuickWaveformLayer.cpp
    src/timeline/quick/TimelineQuickHeaderLayer.h
    src/timeline/quick/TimelineQuickHeaderLayer.cpp
    src/timeline/quick/TimelineQuickGridLinesLayer.h
    src/timeline/quick/TimelineQuickGridLinesLayer.cpp
    src/timeline/quick/TimelineQuickNotesLayer.h
    src/timeline/quick/TimelineQuickNotesLayer.cpp
    src/timeline/quick/TimelineQuickOverlayLayer.h
    src/timeline/quick/TimelineQuickOverlayLayer.cpp
    src/extensions/ExtensionManifest.h
    src/extensions/ExtensionManifest.cpp
    src/extensions/ExtensionOpenBridge.h
    src/extensions/ExtensionOpenBridge.cpp
    src/extensions/EmbeddedExtensionRuntime.h
    src/extensions/EmbeddedExtensionRuntime.cpp
    src/extensions/ExtensionManager.h
    src/extensions/ExtensionManager.cpp
    resources/app_icons.qrc
    resources/fonts.qrc
    resources/slide_data.qrc
    resources/preview_judge_effects.qrc
    resources/preview_runtime_qml.qrc
    resources/quick_shell_qml.qrc
    resources/intro.qrc
)

qt_add_shaders(MiaCode "preview_sprite_shaders"
    PREFIX "/"
    FILES
        src/preview/quick_scene/shaders/PreviewSpriteMaterial.vert
        src/preview/quick_scene/shaders/PreviewSpriteMaterial.frag
        src/preview/quick_scene/shaders/PreviewStageDimMaterial.vert
        src/preview/quick_scene/shaders/PreviewStageDimMaterial.frag
        src/preview/quick_scene/shaders/PreviewFireworkMaterial.vert
        src/preview/quick_scene/shaders/PreviewFireworkMaterial.frag
)
qt_add_shaders(MiaCode "intro_shaders"
    PREFIX "/"
    FILES
        src/intro/shaders/bg_texture.frag
)
set_target_properties(MiaCode PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "MiaCode"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.fanfaredash.MiaCode"
)

if (WIN32 AND EXISTS "${MIACODE_APP_ICON}")
    file(TO_CMAKE_PATH "${MIACODE_APP_ICON}" MIACODE_APP_ICON_RC_PATH)
    set(MIACODE_APP_RC "${CMAKE_CURRENT_BINARY_DIR}/generated/miacode_app_icon.rc")
    file(WRITE "${MIACODE_APP_RC}" "IDI_APP_ICON ICON \"${MIACODE_APP_ICON_RC_PATH}\"\n")
    target_sources(MiaCode PRIVATE "${MIACODE_APP_RC}")
endif()

if (APPLE AND EXISTS "${MIACODE_MACOS_APP_ICON}")
    set_source_files_properties("${MIACODE_MACOS_APP_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(MiaCode PRIVATE "${MIACODE_MACOS_APP_ICON}")
    set_target_properties(MiaCode PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "app.icns"
    )
endif()

add_library(soundtouch STATIC
    third_party/soundtouch/source/SoundTouch/AAFilter.cpp
    third_party/soundtouch/source/SoundTouch/BPMDetect.cpp
    third_party/soundtouch/source/SoundTouch/cpu_detect_x86.cpp
    third_party/soundtouch/source/SoundTouch/FIFOSampleBuffer.cpp
    third_party/soundtouch/source/SoundTouch/FIRFilter.cpp
    third_party/soundtouch/source/SoundTouch/InterpolateCubic.cpp
    third_party/soundtouch/source/SoundTouch/InterpolateLinear.cpp
    third_party/soundtouch/source/SoundTouch/InterpolateShannon.cpp
    third_party/soundtouch/source/SoundTouch/mmx_optimized.cpp
    third_party/soundtouch/source/SoundTouch/PeakFinder.cpp
    third_party/soundtouch/source/SoundTouch/RateTransposer.cpp
    third_party/soundtouch/source/SoundTouch/SoundTouch.cpp
    third_party/soundtouch/source/SoundTouch/sse_optimized.cpp
    third_party/soundtouch/source/SoundTouch/TDStretch.cpp
)
target_include_directories(soundtouch PUBLIC
    third_party/soundtouch/include
    third_party/soundtouch/source/SoundTouch
)
target_compile_definitions(soundtouch PUBLIC SOUNDTOUCH_FLOAT_SAMPLES=1)

# Vendored single-file ZIP writer (third_party/miniz), used by the
# "Export as ZIP" feature. Public stable API, no new runtime DLL — the
# amalgamated miniz.c links straight into the static lib.
add_library(miniz STATIC
    third_party/miniz/miniz.c
)
target_include_directories(miniz PUBLIC
    third_party/miniz
)
# Plain C, no Qt — keep the global AUTOMOC/UIC/RCC off this target.
set_target_properties(miniz PROPERTIES
    AUTOMOC OFF
    AUTOUIC OFF
    AUTORCC OFF
)

target_link_libraries(MiaCode PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::Network
    soundtouch
    miniz
)
target_include_directories(MiaCode PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    third_party/bass/include
    src
    src/app
    src/app/quick_shell
    src/app/ui
    src/core/video
    src/editor
    src/preview
    src/audio
    src/preview/quick_scene
    src/preview/runtime
    src/core/scene
    src/core/chart
    src/core/chart/document
    src/core/chart/parser
    src/timeline
    src/tools/latency
    src/tools/video_export
    src/tools/net
)

target_sources(MiaCode PRIVATE
    src/common/PreviewAudioMixConfig.h
    src/tools/video_export/VideoExportAudioRenderPlan.h
    src/tools/video_export/VideoExportAudioRenderPlan.cpp
    src/tools/video_export/VideoExportAudioBackend.h
    src/tools/video_export/LegacyExportAudioBackend.h
    src/tools/video_export/LegacyExportAudioBackend.cpp
)

if (WIN32 OR APPLE)
    target_sources(MiaCode PRIVATE
        src/audio/BassPreviewAudioBackend.h
        src/audio/BassPreviewAudioBackend.cpp
        src/audio/BassPreviewAudioBackend_EngineInit.cpp
        src/audio/BassPreviewAudioBackend_Assets.cpp
        src/audio/BassPreviewAudioBackend_Transport.cpp
        src/audio/BassPreviewAudioBackend_PlaybackClock.cpp
        src/audio/BassPreviewAudioBackend_EventDrain.cpp
        src/tools/video_export/BassExportAudioBackend.h
        src/tools/video_export/BassExportAudioBackend.cpp
    )
    target_compile_definitions(MiaCode PRIVATE MIACODE_HAS_BASS_AUDIO=1)
endif()

if (WIN32)
    target_sources(MiaCode PRIVATE
        src/render/backend_d3d11/PreviewDCompCore.h
        src/render/backend_d3d11/PreviewDCompCore.cpp
        src/render/backend_d3d11/TimelineRenderView.h
        src/render/backend_d3d11/TimelineRenderView.cpp
        src/render/backend_d3d11/PreviewDCompFrameStateSnapshot.h
        src/render/PreviewDCompRenderer.h
        src/render/PreviewDCompRenderer.cpp
        src/render/backend_d3d11/PreviewDCompSpritePipeline.h
        src/render/backend_d3d11/PreviewDCompSpritePipeline.cpp
        src/render/backend_d3d11/PreviewDCompTextureCache.h
        src/render/backend_d3d11/PreviewDCompTextureCache.cpp
        src/render/backend_d3d11/PreviewDCompSurface.h
        src/render/backend_d3d11/PreviewDCompSurface.cpp
        src/render/backend_d3d11/PreviewPopupHwndTracker.h
        src/render/backend_d3d11/PreviewPopupHwndTracker.cpp
        src/render/backend_d3d11/OwnerHwndTracker.h
        src/render/backend_d3d11/OwnerHwndTracker.cpp
    )
    target_link_libraries(MiaCode PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bass.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bassmix.lib"
        winmm
        ole32       # Windows Core Audio endpoint notification COM API
        avrt  # Multimedia Class Scheduler Service (AvSetMmThreadCharacteristicsW)
        d3d11        # DComp preview path (src/render/backend_d3d11) — see docs plan §3 Phase 1
        dxgi         # IDXGIFactory2::CreateSwapChainForComposition + frame latency waitable
        dcomp        # DCompositionCreateDevice + visual tree
        d3dcompiler  # Phase 3 sprite pipeline runtime HLSL compile (D3DCompile)
        User32       # RegisterPowerSettingNotification
        Wtsapi32     # Session lock/unlock notifications for idle-freeze diagnostics
        dbghelp      # StackWalk64 + Sym* for the hang watchdog's GUI-thread self stack walk
    )
    add_custom_command(TARGET MiaCode POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bass.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bassmix.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bass_fx.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bass_aac.dll"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bassopus.dll"
            $<TARGET_FILE_DIR:MiaCode>
    )
elseif (APPLE)
    set(MIACODE_BASS_MACOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/macos/universal")
    set(MIACODE_BASS_MACOS_LIBRARIES
        "${MIACODE_BASS_MACOS_DIR}/libbass.dylib"
        "${MIACODE_BASS_MACOS_DIR}/libbassmix.dylib"
        "${MIACODE_BASS_MACOS_DIR}/libbass_fx.dylib"
        "${MIACODE_BASS_MACOS_DIR}/libbassopus.dylib"
    )
    foreach(MIACODE_BASS_LIBRARY IN LISTS MIACODE_BASS_MACOS_LIBRARIES)
        if (NOT EXISTS "${MIACODE_BASS_LIBRARY}")
            message(FATAL_ERROR "Missing required macOS BASS runtime: ${MIACODE_BASS_LIBRARY}")
        endif()
    endforeach()

    target_link_libraries(MiaCode PRIVATE
        "${MIACODE_BASS_MACOS_DIR}/libbass.dylib"
        "${MIACODE_BASS_MACOS_DIR}/libbassmix.dylib"
    )
    set_target_properties(MiaCode PROPERTIES
        BUILD_RPATH "@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
    add_custom_command(TARGET MiaCode POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:MiaCode>/../Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MIACODE_BASS_MACOS_LIBRARIES}
            "$<TARGET_FILE_DIR:MiaCode>/../Frameworks"
        COMMENT "Deploying macOS BASS runtime libraries"
    )
endif()

if (APPLE)
    # macOS-only Objective-C++ helper that hides the orphan Qt::Tool NSPanel left
    # behind by each QuickShell bridge surface once its content view is adopted by
    # the QML WindowContainer (otherwise it floats over the UI as a white block).
    target_sources(MiaCode PRIVATE
        src/app/quick_shell/QuickShellMacSurfaceSupport.mm
        src/app/ui/UiNativeWindowThemeMac.h
        src/app/ui/UiNativeWindowThemeMac.mm
    )
    target_link_libraries(MiaCode PRIVATE "-framework AppKit")
endif()

target_link_libraries(MiaCode PRIVATE Qt6::Multimedia)
target_compile_definitions(MiaCode PRIVATE HAVE_QT_MULTIMEDIA=1)
# Qt SVG — renders the Google Material "settings" gear toolbar icon
# (makeSettingsGearIcon, MainWindowShared.cpp) via QSvgRenderer.
target_link_libraries(MiaCode PRIVATE Qt6::Svg)

set(MIACODE_BUNDLED_EXTENSIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources/extensions/bundled")
add_custom_command(TARGET MiaCode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:MiaCode>/extensions"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MIACODE_BUNDLED_EXTENSIONS_DIR}"
        "$<TARGET_FILE_DIR:MiaCode>/extensions"
    COMMENT "Deploying bundled extensions next to MiaCode.exe"
    VERBATIM
)

# ---------------------------------------------------------------------------
# QtAVPlayer (FFmpeg) — preview background-video decode backend.
#
# Replaces QMediaPlayer for PV/BG decoding inside PreviewStageMediaHost; see
# docs/ops/DEBUG_INDEX.md. QtAVPlayer is the
# MIT-licensed source vendored under third_party/QtAVPlayer, compiled straight
# into MiaCode. It links the external FFmpeg shared libraries (n7.1 LGPL) whose
# dev SDK (headers + import libs + runtime DLLs) is provisioned under
# third_party/ffmpeg/windows/dev by scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1.
#
# Windows-only for now (D3D11VA hwdevice + provisioned FFmpeg dev SDK). Other
# platforms keep the QMediaPlayer path — MIACODE_USE_QTAVPLAYER stays undefined
# there, and PreviewStageMediaHost compiles its legacy backend.
# ---------------------------------------------------------------------------
if (WIN32)
    set(MIACODE_FFMPEG_DEV_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/windows/dev"
        CACHE PATH "FFmpeg shared dev SDK root (include/ + lib/ + bin/) for the QtAVPlayer preview backend")
    if (NOT EXISTS "${MIACODE_FFMPEG_DEV_DIR}/include" OR NOT EXISTS "${MIACODE_FFMPEG_DEV_DIR}/lib")
        message(FATAL_ERROR
            "FFmpeg dev SDK not found at '${MIACODE_FFMPEG_DEV_DIR}'. "
            "Run scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1, or pass "
            "-DMIACODE_FFMPEG_DEV_DIR=<root containing include/ lib/ bin/>.")
    endif()

    # find_library() in QtAVPlayer.cmake resolves the FFmpeg import libs from
    # <prefix>/lib; prepending the dev root makes our SDK win over any system one.
    list(PREPEND CMAKE_PREFIX_PATH "${MIACODE_FFMPEG_DEV_DIR}")

    set(QTAVPLAYER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/QtAVPlayer/src")
    set(QT_AVPLAYER_DIR "${QTAVPLAYER_SRC}/QtAVPlayer")
    set(QT_AVPLAYER_MULTIMEDIA ON CACHE BOOL "" FORCE)
    # Drop avdevice (capture-device I/O only, unused for file playback) to trim
    # ~7 MB + one DLL: QtAVPlayer.cmake skips finding/linking it and qavdemuxer
    # compiles out its lone avdevice_register_all() call.
    set(QT_AVPLAYER_NO_AVDEVICE ON)
    # Sets QtAVPlayer_SOURCES / QtAVPlayer_LIBS (absolute import-lib paths via
    # find_library) and globally defines QT_BUILD_QTAVPLAYER_LIB.
    include("${QT_AVPLAYER_DIR}/QtAVPlayer.cmake")

    target_sources(MiaCode PRIVATE ${QtAVPlayer_SOURCES})
    target_include_directories(MiaCode PRIVATE
        "${QTAVPLAYER_SRC}"                  # makes <QtAVPlayer/...> resolve
        "${MIACODE_FFMPEG_DEV_DIR}/include"  # makes <libavcodec/...> resolve
    )
    # QT_AVPLAYER_MULTIMEDIA : build the QAVVideoFrame -> QVideoFrame bridge.
    # QT_BUILD_QTAVPLAYER_LIB: export QtAVPlayer symbols (sources live in the exe).
    # MIACODE_USE_QTAVPLAYER  : switch PreviewStageMediaHost onto the new backend.
    # Re-asserted per-target because QtAVPlayer.cmake's add_compile_definitions
    # is directory-scoped and MiaCode is created earlier in this file.
    target_compile_definitions(MiaCode PRIVATE
        QT_AVPLAYER_MULTIMEDIA
        QT_BUILD_QTAVPLAYER_LIB
        QT_AVPLAYER_NO_AVDEVICE
        MIACODE_USE_QTAVPLAYER=1
    )
    target_link_libraries(MiaCode PRIVATE
        Qt6::MultimediaQuickPrivate
        ${QtAVPlayer_LIBS}
    )

    # Runtime: the preview backend dlopen's these next to MiaCode.exe.
    # avfilter-10 is NET-NEW vs the existing package (QtAVPlayer links it); the
    # other five match the majors Qt already ships. avdevice is intentionally
    # NOT shipped (dropped above). windeployqt in package-win.ps1 stages the
    # packaged set — this POST_BUILD keeps the local build/ tree runnable
    # without a full package step.
    add_custom_command(TARGET MiaCode POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MIACODE_FFMPEG_DEV_DIR}/bin/avcodec-61.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/avformat-61.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/avutil-59.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/swresample-5.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/swscale-8.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/avfilter-10.dll"
            $<TARGET_FILE_DIR:MiaCode>
        VERBATIM
    )
endif()

if (MSVC)
    target_compile_options(MiaCode PRIVATE /FS)
endif()

# Tiny launcher exe that lives at the dist-package root and forwards
# execution to app/MiaCode.exe. Lets the package keep the ~40 Qt/BASS/
# Qt6Multimedia-ffmpeg/D3D DLLs inside app/ rather than scattering them
# across the root the user sees in File Explorer. Built only on Windows
# — other platforms ship MiaCode.exe at the bundle root directly.
if (WIN32)
    add_executable(MiaCodeLauncher WIN32
        src/wrapper/MiaCodeLauncher.cpp
        # P2 — export the same NVIDIA Optimus / AMD PowerXpress high-performance
        # GPU hint symbols from the launcher's export table as well as the real
        # app, so the preference is declared whichever binary the OS inspects.
        src/app/gpu_high_performance_hint.cpp
    )
    set_target_properties(MiaCodeLauncher PROPERTIES
        OUTPUT_NAME "MiaCodeLauncher"
        # Static-link the MSVC runtime so the launcher itself has zero
        # MSVCP140/VCRUNTIME140 dependency. Without this, the preflight
        # probe is useless: the launcher would itself fail to load when
        # the user is missing the same DLLs we're trying to detect for
        # them. Pure Win32 from here on — kernel32 + user32 only.
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    if (MSVC)
        # /utf-8: parse source as UTF-8 (default would be the system
        # codepage — GBK on zh-CN Windows — which mangles the CJK
        # characters embedded in the bilingual MessageBox strings).
        # Without this, MessageBoxW shows garbage where Chinese should
        # be. Only needed on the launcher since the main MiaCode target
        # routes all user-visible text through Qt's tr() / QStringLiteral
        # which encodes correctly regardless of compiler defaults.
        target_compile_options(MiaCodeLauncher PRIVATE /utf-8)
    endif()
    # Shared app icon — the wrapper exe lives at the dist root and is the
    # user-clickable face of MiaCode in File Explorer, so it must show
    # the same icon as the real binary inside app/. Reuses the generated
    # .rc emitted alongside the MiaCode target above.
    if (EXISTS "${MIACODE_APP_ICON}" AND DEFINED MIACODE_APP_RC)
        target_sources(MiaCodeLauncher PRIVATE "${MIACODE_APP_RC}")
    endif()
endif()

if (MIACODE_BUILD_DEV_TOOLS)
    # Source groups shared across several dev tools / specs, listed once
    # instead of being copy-pasted into every target.
    set(_miacode_chart_core
        src/core/chart/document/SimaiDocument.h
        src/core/chart/document/SimaiDocument.cpp
        src/core/chart/document/SimaiTimingMetadata.h
        src/core/chart/document/SimaiTimingMetadata.cpp
        src/core/chart/parser/SimaiNativeParser.h
        src/core/chart/parser/SimaiNativeParser.cpp
    )
    set(_miacode_log_core
        src/common/OperationLog.h
        src/common/OperationLog.cpp
        src/common/DebugLog.h
        src/common/DebugLog.cpp
    )

    # Declare a dev-only helper / spec executable. Collapses the repeated
    # add_executable + target_link_libraries + target_include_directories
    # triple into a single call. Pass TEST to also register it with CTest
    # (run via: ctest --test-dir build -C Release).
    #
    #   miacode_add_dev_tool(<name> [TEST]
    #       SOURCES  <files...>
    #       LIBS     <link libraries...>
    #       INCLUDES <include dirs...>)
    function(miacode_add_dev_tool NAME)
        cmake_parse_arguments(DT "TEST" "" "SOURCES;LIBS;INCLUDES" ${ARGN})
        add_executable(${NAME} ${DT_SOURCES})
        if (DT_LIBS)
            target_link_libraries(${NAME} PRIVATE ${DT_LIBS})
        endif()
        if (DT_INCLUDES)
            target_include_directories(${NAME} PRIVATE ${DT_INCLUDES})
        endif()
        if (DT_TEST)
            add_test(NAME ${NAME} COMMAND ${NAME})
            # Spec exes are not deployed with the Qt runtime, so prepend Qt's
            # bin directory to PATH for the test run. Without this, ctest
            # launches them and Windows raises "Qt6*.dll not found" loader
            # dialogs (STATUS_DLL_NOT_FOUND / 0xc0000135) and every test fails.
            set_tests_properties(${NAME} PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        endif()
    endfunction()

    # ---- CLI dump / probe helpers (manual diagnostics, not CTest cases) ----
    miacode_add_dev_tool(miacode_muri_dump
        SOURCES
            src/tools/muri/MuriDump.cpp
            src/common/MuriTypes.h
            src/common/MuriTypes.cpp
            src/common/DebugOptions.h
            ${_miacode_log_core}
            ${_miacode_chart_core}
            src/tools/muri/MuriAnalyzer.h
            src/tools/muri/MuriAnalyzer.cpp
            src/tools/muri/MuriAnalyzerGeometry.h
            src/tools/muri/MuriAnalyzerGeometry.cpp
            src/tools/muri/MuriAnalyzerModel.h
            src/tools/muri/MuriSlideReferenceData.h
            src/tools/muri/MuriSlideReferenceData.cpp
            src/tools/muri/MuriAnalyzerInternal.h
            src/tools/muri/MuriDiagnosticCollector.h
            src/tools/muri/MuriDiagnosticCollector.cpp
            src/tools/muri/MuriDiagnosticLabels.h
            src/tools/muri/MuriDiagnosticLabels.cpp
            src/tools/muri/MuriRuntimeModelBuilder.h
            src/tools/muri/MuriRuntimeModelBuilder.cpp
            src/tools/muri/MuriOverlayBuilder.h
            src/tools/muri/MuriOverlayBuilder.cpp
            src/tools/muri/MuriSlideWifiJudge.h
            src/tools/muri/MuriSlideWifiJudge.cpp
            src/tools/muri/MuriSimpleNoteJudge.h
            src/tools/muri/MuriSimpleNoteJudge.cpp
            src/tools/muri/MuriPanelEntries.h
            src/tools/muri/MuriPanelEntries.cpp
            src/tools/muri/MuriStaticChecker.h
            src/tools/muri/MuriStaticChecker.cpp
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES
            src src/app/ui src/common src/core/chart
            src/core/chart/document src/core/chart/parser src/timeline src/tools
    )

    miacode_add_dev_tool(simai_native_dump
        SOURCES
            src/core/chart/parser/SimaiNativeDump.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES src src/core/chart src/core/chart/parser src/timeline
    )

    miacode_add_dev_tool(soundtouch_probe
        SOURCES
            src/audio/PreviewAudioBackend.h
            src/tools/probe/SoundtouchProbe.cpp
            src/audio/PreviewAudioSettings.h
            src/audio/PreviewAudioSettings.cpp
            src/audio/MiniaudioPreviewAudioBackend.h
            src/audio/MiniaudioPreviewAudioBackend.cpp
            src/audio/QtPreviewSfxRuntime.h
            src/audio/QtPreviewSfxRuntime.cpp
            ${_miacode_log_core}
        LIBS Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Widgets soundtouch
        INCLUDES
            src src/preview src/audio
            src/core/video src/timeline src/tools
    )
    if (WIN32)
        target_sources(soundtouch_probe PRIVATE
            src/audio/BassPreviewAudioBackend.h
            src/audio/BassPreviewAudioBackend.cpp
            src/audio/BassPreviewAudioBackend_EngineInit.cpp
            src/audio/BassPreviewAudioBackend_Assets.cpp
            src/audio/BassPreviewAudioBackend_Transport.cpp
            src/audio/BassPreviewAudioBackend_PlaybackClock.cpp
            src/audio/BassPreviewAudioBackend_EventDrain.cpp
        )
        target_compile_definitions(soundtouch_probe PRIVATE MIACODE_HAS_BASS_AUDIO=1)
        target_include_directories(soundtouch_probe PRIVATE third_party/bass/include)
        target_link_libraries(soundtouch_probe PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bass.lib"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bassmix.lib"
        )
        add_custom_command(TARGET soundtouch_probe POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bass.dll"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bassmix.dll"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bass_fx.dll"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bass_aac.dll"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bassopus.dll"
                $<TARGET_FILE_DIR:soundtouch_probe>
        )
    endif()

    # Batch offset-detection evaluator. Walks a chart corpus and scores the
    # offset detector against each chart's &first (error folded mod one
    # 8th-note). Manual diagnostic — needs a real corpus, so NOT a CTest case.
    # Links only LatencyAnalysis + its own miniaudio impl TU (decode-only).
    miacode_add_dev_tool(latency_offset_batch
        SOURCES
            src/tools/latency/LatencyBatchTest.cpp
            src/tools/latency/LatencyBatchAudioImpl.cpp
            src/tools/latency/LatencyAnalysis.h
            src/tools/latency/LatencyAnalysis.cpp
        LIBS Qt6::Core
        INCLUDES src src/tools/latency
    )

    # ---- Spec executables (registered with CTest) ----
    miacode_add_dev_tool(oplog_self_test TEST
        SOURCES
            src/tools/oplog/OperationLogSpec.cpp
            src/common/CrashRecovery.h
            src/common/CrashRecovery.cpp
            ${_miacode_log_core}
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(simai_parser_spec TEST
        SOURCES
            src/tools/simai_parser/SimaiParserSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES src src/app/ui src/core/chart src/core/chart/parser src/timeline
    )

    miacode_add_dev_tool(chart_batch_transform_spec TEST
        SOURCES
            src/tools/chart_transform/ChartBatchTransformSpec.cpp
            src/core/chart/transform/ChartBatchTransform.h
            src/core/chart/transform/ChartBatchTransform.Parsers.cpp
            src/core/chart/transform/ChartBatchTransform.Subdivision.cpp
            src/core/chart/transform/ChartBatchTransform.Selection.cpp
            src/core/chart/transform/ChartBatchTransform.Transform.cpp
            src/core/chart/transform/ChartNormalization.h
            src/core/chart/transform/ChartNormalization.cpp
            src/core/chart/transform/ChartNormalizationSegmentPolicy.h
            src/core/chart/transform/ChartNormalizationSegmentPolicy.cpp
            src/core/chart/transform/Non384SnapTable.h
            src/core/chart/transform/Non384SnapTable.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES
            src src/app/ui src/common src/core/chart
            src/core/chart/document src/core/chart/parser src/core/chart/transform src/timeline
    )

    miacode_add_dev_tool(extension_manifest_spec TEST
        SOURCES
            src/tools/extensions/ExtensionManifestSpec.cpp
            src/extensions/ExtensionManifest.h
            src/extensions/ExtensionManifest.cpp
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(extension_open_bridge_spec TEST
        SOURCES
            src/tools/extensions/ExtensionOpenBridgeSpec.cpp
            src/extensions/ExtensionOpenBridge.h
            src/extensions/ExtensionOpenBridge.cpp
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(extension_runtime_spec TEST
        SOURCES
            src/tools/extensions/ExtensionRuntimeSpec.cpp
            src/extensions/EmbeddedExtensionRuntime.h
            src/extensions/EmbeddedExtensionRuntime.cpp
        LIBS Qt6::Core Qt6::Qml
        INCLUDES src
    )

    miacode_add_dev_tool(simai_document_spec TEST
        SOURCES
            src/tools/chart_document/SimaiDocumentSpec.cpp
            src/core/chart/document/SimaiDocument.h
            src/core/chart/document/SimaiDocument.cpp
        LIBS Qt6::Core
        INCLUDES src src/core/chart/document
    )

    miacode_add_dev_tool(cover_layout_model_spec TEST
        SOURCES
            src/tools/cover_export/CoverLayoutModelSpec.cpp
            src/tools/cover_export/CoverLayoutModel.h
            src/tools/cover_export/CoverLayoutModel.cpp
            src/tools/cover_export/CoverCompositionState.h
            src/tools/cover_export/CoverCompositionState.cpp
            src/tools/cover_export/CoverCompositionPersistenceGuard.h
            src/tools/cover_export/CoverCompositionPersistenceGuard.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/extensions/ExtensionManifest.h
            src/extensions/ExtensionManifest.cpp
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES src src/app/ui src/extensions
    )

    miacode_add_dev_tool(font_combo_width_spec TEST
        SOURCES
            src/tools/video_export/FontComboWidthSpec.cpp
            src/tools/video_export/FontLibrary.h
            src/tools/video_export/FontLibrary.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/extensions/ExtensionManifest.h
            src/extensions/ExtensionManifest.cpp
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES src src/app/ui src/extensions
    )

    miacode_add_dev_tool(muri_spec TEST
        SOURCES
            src/tools/muri/MuriSpec.cpp
            src/common/MuriTypes.h
            src/common/MuriTypes.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            src/tools/muri/MuriAnalyzer.h
            src/tools/muri/MuriAnalyzer.cpp
            src/tools/muri/MuriAnalyzerGeometry.h
            src/tools/muri/MuriAnalyzerGeometry.cpp
            src/tools/muri/MuriAnalyzerModel.h
            src/tools/muri/MuriSlideReferenceData.h
            src/tools/muri/MuriSlideReferenceData.cpp
            src/tools/muri/MuriAnalyzerInternal.h
            src/tools/muri/MuriDiagnosticCollector.h
            src/tools/muri/MuriDiagnosticCollector.cpp
            src/tools/muri/MuriDiagnosticLabels.h
            src/tools/muri/MuriDiagnosticLabels.cpp
            src/tools/muri/MuriRuntimeModelBuilder.h
            src/tools/muri/MuriRuntimeModelBuilder.cpp
            src/tools/muri/MuriOverlayBuilder.h
            src/tools/muri/MuriOverlayBuilder.cpp
            src/tools/muri/MuriSlideWifiJudge.h
            src/tools/muri/MuriSlideWifiJudge.cpp
            src/tools/muri/MuriSimpleNoteJudge.h
            src/tools/muri/MuriSimpleNoteJudge.cpp
            src/tools/muri/MuriPanelEntries.h
            src/tools/muri/MuriPanelEntries.cpp
            src/tools/muri/MuriStaticChecker.h
            src/tools/muri/MuriStaticChecker.cpp
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES src src/app/ui src/common src/core/chart src/core/chart/parser src/timeline src/tools
    )

    miacode_add_dev_tool(timeline_model_spec TEST
        SOURCES
            src/tools/timeline/TimelineModelSpec.cpp
            ${_miacode_chart_core}
            src/timeline/TimelineQuickModel.h
            src/timeline/TimelineQuickModel.cpp
            src/timeline/TimelineQuickModelParser.cpp
            src/timeline/TimelineQuickModelSnapshot.cpp
            src/timeline/TimelineQuickModelIndexing.cpp
            ${_miacode_log_core}
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES src src/app/ui src/core/chart src/core/chart/parser src/timeline src/tools
    )

    miacode_add_dev_tool(plain_code_editor_spec TEST
        SOURCES
            src/tools/editor/PlainCodeEditorSpec.cpp
            src/editor/PlainCodeEditor.h
            src/editor/PlainCodeEditor.cpp
            src/editor/PlainCodeEditor.Layout.cpp
            src/editor/PlainCodeEditor.HighlightAndCaret.cpp
            src/editor/PlainCodeEditor.BracketCompletion.cpp
            src/editor/PlainCodeEditor.Input.cpp
            src/editor/PlainCodeEditor.Bookmarks.cpp
            src/editor/TouchPadAuthoringEdit.h
            src/editor/TouchPadAuthoringEdit.cpp
            src/editor/BracketCompletionPopup.h
            src/editor/BracketCompletionPopup.cpp
            src/editor/SimaiCompletionCatalog.h
            src/editor/SimaiCompletionCatalog.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/extensions/ExtensionManifest.h
            src/extensions/ExtensionManifest.cpp
            src/app/ui/AppBackgroundSettings.h
            src/app/ui/AppBackgroundSettings.cpp
            src/app/ui/AppBackgroundPainter.h
            src/app/ui/AppBackgroundPainter.cpp
            src/app/ui/UiTheme.h
            src/app/ui/UiTheme.cpp
            src/app/ui/ShortcutRegistry.h
            src/app/ui/ShortcutRegistry.cpp
            src/common/InputShortcutGesture.h
            src/common/InputShortcutGesture.cpp
            src/common/AdoptedWidgetCoordinates.h
            src/common/AdoptedWidgetCoordinates.cpp
            ${_miacode_log_core}
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES src src/app/ui src/common src/extensions
    )

    miacode_add_dev_tool(simai_completion_catalog_spec TEST
        SOURCES
            src/tools/editor/SimaiCompletionCatalogSpec.cpp
            src/editor/SimaiCompletionCatalog.h
            src/editor/SimaiCompletionCatalog.cpp
        LIBS Qt6::Core
        INCLUDES src src/editor
    )

    miacode_add_dev_tool(preview_asset_loader_spec TEST
        SOURCES
            src/tools/preview/PreviewSceneAssetLoaderSpec.cpp
            src/preview/runtime/PreviewSceneAssetLoader.h
            src/preview/runtime/PreviewSceneAssetLoader.cpp
            src/core/scene/PreviewFrameState.h
            ${_miacode_log_core}
            resources/preview_judge_effects.qrc
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/common src/preview src/preview/runtime src/core/scene src/core/video src/timeline
    )

    miacode_add_dev_tool(preview_firework_lifecycle_spec TEST
        SOURCES
            src/tools/preview/PreviewFireworkLifecycleSpec.cpp
            src/core/scene/PreviewFrameState.h
            src/core/scene/PreviewJudgeFireworkLayerState.h
            src/core/scene/PreviewJudgeFireworkLayerState.cpp
            src/core/scene/PreviewSceneMath.h
            src/core/scene/PreviewSceneMath.cpp
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/common src/preview src/core/scene src/core/video src/timeline
    )

    miacode_add_dev_tool(preview_head_layer_spec TEST
        SOURCES
            src/tools/preview/PreviewHeadLayerSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            src/core/scene/PreviewFrameState.h
            src/core/scene/PreviewPreparedSceneCache.h
            src/core/scene/PreviewPreparedSceneCache.cpp
            src/core/scene/PreviewChartReviewLayerState.h
            src/core/scene/PreviewChartReviewLayerState.cpp
            src/core/scene/PreviewHeadLayerState.h
            src/core/scene/PreviewHeadLayerState.cpp
            src/core/scene/PreviewMarkerDrawOrder.h
            src/core/scene/PreviewMarkerDrawOrder.cpp
            src/core/scene/PreviewAnimatedSpriteHelpers.h
            src/core/scene/PreviewAnimatedSpriteHelpers.cpp
            src/core/scene/PreviewJudgeOverlayShared.h
            src/core/scene/PreviewJudgeOverlayShared.cpp
            src/core/scene/PreviewOpacityCurves.h
            src/core/scene/PreviewOpacityCurves.cpp
            src/core/scene/PreviewSceneConstants.h
            src/core/scene/PreviewSceneMath.h
            src/core/scene/PreviewSceneMath.cpp
            src/core/scene/PreviewSkinSelectors.h
            src/core/scene/PreviewSkinSelectors.cpp
            src/common/MuriTypes.h
            src/common/MuriTypes.cpp
            src/timeline/TimelineData.h
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES
            src src/common src/preview src/core/scene src/core/chart
            src/core/chart/document src/core/chart/parser src/timeline
    )

    miacode_add_dev_tool(preview_realtime_object_hot_path_spec TEST
        SOURCES
            src/tools/preview/PreviewRealtimeObjectHotPathSpec.cpp
            ${_miacode_log_core}
            src/core/scene/PreviewActiveMarkerView.h
            src/core/scene/PreviewPreparedSceneCache.h
            src/core/scene/PreviewPreparedSceneCache.cpp
            src/core/scene/PreviewFrameState.h
            src/core/scene/PreviewChartReviewLayerState.h
            src/core/scene/PreviewChartReviewLayerState.cpp
            src/core/scene/PreviewMarkerDrawOrder.h
            src/core/scene/PreviewMarkerDrawOrder.cpp
            src/core/scene/PreviewSlideMotionLayerState.h
            src/core/scene/PreviewSlideMotionLayerState.cpp
            src/core/scene/PreviewTouchJudgeLayerState.h
            src/core/scene/PreviewTouchJudgeLayerState.cpp
            src/core/scene/PreviewTrackLayerState.h
            src/core/scene/PreviewTrackLayerState.cpp
            src/core/scene/PreviewTrackShared.h
            src/core/scene/PreviewTrackShared.cpp
            src/core/scene/PreviewMuriActionLayerState.h
            src/core/scene/PreviewMuriActionLayerState.cpp
            src/core/scene/PreviewAnimatedSpriteHelpers.h
            src/core/scene/PreviewAnimatedSpriteHelpers.cpp
            src/core/scene/PreviewJudgeOverlayShared.h
            src/core/scene/PreviewJudgeOverlayShared.cpp
            src/core/scene/PreviewOpacityCurves.h
            src/core/scene/PreviewOpacityCurves.cpp
            src/core/scene/PreviewSceneConstants.h
            src/core/scene/PreviewSceneMath.h
            src/core/scene/PreviewSceneMath.cpp
            src/core/scene/PreviewSkinSelectors.h
            src/core/scene/PreviewSkinSelectors.cpp
            src/common/MuriTypes.h
            src/common/MuriTypes.cpp
            src/timeline/TimelineData.h
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/common src/preview src/core/scene src/timeline
    )

    miacode_add_dev_tool(preview_quick_sprite_batch_spec TEST
        SOURCES
            src/tools/preview/PreviewQuickSpriteBatchSpec.cpp
            src/preview/quick_scene/PreviewQuickSpriteBatchPolicy.h
        LIBS Qt6::Core
        INCLUDES src src/common src/preview src/preview/quick_scene
    )

    miacode_add_dev_tool(preview_texture_generation_policy_spec TEST
        SOURCES
            src/tools/preview/PreviewTextureGenerationPolicySpec.cpp
            src/preview/quick_scene/PreviewTextureGenerationPolicy.h
        LIBS Qt6::Core
        INCLUDES src src/preview src/preview/quick_scene
    )

    miacode_add_dev_tool(timeline_quick_texture_cache_policy_spec TEST
        SOURCES
            src/tools/timeline/TimelineQuickTextureCachePolicySpec.cpp
            src/timeline/quick/TimelineQuickTextureCachePolicy.h
        LIBS Qt6::Core
        INCLUDES src src/timeline src/timeline/quick
    )

    miacode_add_dev_tool(preview_sfx_timeline_spec TEST
        SOURCES
            src/tools/preview/PreviewSfxTimelineSpec.cpp
            src/common/PreviewTimingSettings.h
            src/common/PreviewSfxTiming.h
            src/common/PreviewSfxTimeline.h
            src/common/PreviewGameplayConfig.h
            src/timeline/TimelineData.h
        LIBS Qt6::Core
        INCLUDES src src/common src/timeline
    )

    miacode_add_dev_tool(preview_audio_settings_spec TEST
        SOURCES
            src/tools/preview/PreviewAudioSettingsSpec.cpp
            src/audio/PreviewAudioSettings.h
            src/audio/PreviewAudioSettings.cpp
            src/common/PreviewSfxAssets.h
            src/common/PreviewSfxSemantics.h
        LIBS Qt6::Core
        INCLUDES src src/common src/preview src/audio
    )

    miacode_add_dev_tool(touch_pad_authoring_state_spec TEST
        SOURCES
            src/tools/preview/TouchPadAuthoringStateSpec.cpp
            src/core/scene/TouchPadAuthoringState.h
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src
    )
    target_compile_definitions(touch_pad_authoring_state_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(bass_preview_retained_state_spec TEST
        SOURCES
            src/tools/preview/BassPreviewRetainedStateSpec.cpp
            src/audio/BassPreviewRetainedState.h
            src/audio/PreviewAudioBackend.h
        LIBS Qt6::Core
        INCLUDES src src/common src/preview src/audio
    )

    miacode_add_dev_tool(bass_preview_debug_log_routing_spec TEST
        SOURCES
            src/tools/preview/BassPreviewDebugLogRoutingSpec.cpp
            src/audio/BassPreviewDebugLogRouting.h
        LIBS Qt6::Core
        INCLUDES src src/common src/preview src/audio
    )

    miacode_add_dev_tool(preview_audio_health_spec TEST
        SOURCES
            src/tools/preview/PreviewAudioHealthSpec.cpp
            src/audio/PreviewAudioHealth.h
        LIBS Qt6::Core
        INCLUDES src src/common src/preview src/audio
    )

    miacode_add_dev_tool(preview_audio_device_change_policy_spec TEST
        SOURCES
            src/tools/preview/PreviewAudioDeviceChangePolicySpec.cpp
            src/audio/PreviewAudioDeviceChangePolicy.h
        LIBS Qt6::Core
        INCLUDES src src/common src/preview src/audio
    )

    miacode_add_dev_tool(bass_preview_sfx_scheduler_policy_spec TEST
        SOURCES
            src/tools/preview/BassPreviewSfxSchedulerPolicySpec.cpp
            src/audio/BassPreviewSfxSchedulerPolicy.h
        LIBS Qt6::Core
        INCLUDES src src/common src/preview src/audio
    )

    # Simulation spec for the preview audio/stage-media cache mechanism: the
    # content-stamp skip decision (size:mtime, NOT path-only) and the live,
    # uncached track/bg resolvers. Drives the real production functions against
    # real temp files, simulating an in-place track.mp3/bg.png rewrite.
    miacode_add_dev_tool(preview_media_cache_stamp_spec TEST
        SOURCES
            src/tools/preview/PreviewMediaCacheStampSpec.cpp
            src/common/FileContentStamp.h
            src/common/ChartAssetPaths.h
        LIBS Qt6::Core
        INCLUDES src src/common
    )

    miacode_add_dev_tool(quickshell_preview_surface_policy_spec TEST
        SOURCES
            src/tools/preview/QuickShellPreviewSurfacePolicySpec.cpp
            src/app/quick_shell/QuickShellPreviewSurfacePolicy.h
        LIBS Qt6::Core
        INCLUDES src src/app/quick_shell
    )

    miacode_add_dev_tool(video_export_runtime_policy_spec TEST
        SOURCES
            src/tools/video_export/VideoExportRuntimePolicySpec.cpp
            src/tools/video_export/VideoExportRuntimePolicy.h
            src/tools/video_export/VideoExportRuntimePolicy.cpp
        LIBS Qt6::Core
        INCLUDES src src/tools
    )

    miacode_add_dev_tool(batch_export_panel_spec TEST
        SOURCES
            src/tools/video_export/BatchExportPanelSpec.cpp
            src/tools/video_export/BatchExportPanel.h
            src/tools/video_export/BatchExportTaskLayout.h
            src/tools/video_export/BatchExportTaskLayout.cpp
            src/tools/video_export/BatchExportSelectionState.cpp
        LIBS Qt6::Core Qt6::Widgets
        INCLUDES src src/app/ui src/common src/audio src/core/video src/timeline src/tools src/tools/video_export
    )

    miacode_add_dev_tool(video_export_intro_mode_spec TEST
        SOURCES
            src/tools/video_export/VideoExportIntroModeSpec.cpp
            src/tools/video_export/VideoExportController.h
            src/timeline/TimelineData.h
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/common src/audio src/core/video src/timeline src/tools src/tools/video_export
    )

    miacode_add_dev_tool(video_export_audio_render_plan_spec TEST
        SOURCES
            src/tools/video_export/VideoExportAudioRenderPlanSpec.cpp
            src/tools/video_export/VideoExportAudioRenderPlan.h
            src/tools/video_export/VideoExportAudioRenderPlan.cpp
            src/tools/video_export/VideoExportController.h
            src/common/PreviewAudioMixConfig.h
            src/common/PreviewTimingSettings.h
            src/common/PreviewSfxTiming.h
            src/common/PreviewSfxTimeline.h
            src/common/PreviewSfxAssets.h
            src/common/PreviewSfxSemantics.h
            src/common/VideoExportConfig.h
            src/audio/PreviewAudioSettings.h
            src/audio/PreviewAudioSettings.cpp
            src/timeline/TimelineData.h
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/common src/audio src/core/video src/timeline src/tools src/tools/video_export
    )

    miacode_add_dev_tool(chart_zip_packager_spec TEST
        SOURCES
            src/tools/zip_export/ChartZipPackagerSpec.cpp
            src/tools/zip_export/ChartZipPackager.h
            src/tools/zip_export/ChartZipPackager.cpp
            src/common/ChartAssetPaths.h
        LIBS Qt6::Core Qt6::Gui miniz
        INCLUDES src src/common src/tools
    )

    miacode_add_dev_tool(net_client_spec TEST
        SOURCES
            src/tools/net/NetClientSpec.cpp
            src/tools/net/NetClient.h
            src/tools/net/NetClient.cpp
            src/tools/net/NetBatchUploadScanner.h
            src/tools/net/NetBatchUploadScanner.cpp
            src/tools/net/NetUploadDiagnostics.h
            src/tools/net/NetUploadDiagnostics.cpp
            src/tools/media/PvBatchCompressionScanner.h
            src/tools/media/PvBatchCompressionScanner.cpp
            src/tools/zip_export/ChartZipPackager.h
            src/tools/zip_export/ChartZipPackager.cpp
            src/common/ChartAssetPaths.h
        LIBS Qt6::Core Qt6::Network miniz
        INCLUDES src src/common src/tools
    )

    # Drift guard: every MIACODE_* env flag read in src/ must be documented in
    # docs/ops/DEBUG_INDEX.md, and every flag the doc names must still be read in code
    # (or be in the spec's retired allowlist). Reads the tree + doc from disk;
    # the repo root is injected as a compile definition.
    miacode_add_dev_tool(debug_flag_index_spec TEST
        SOURCES
            src/tools/debug_index/DebugFlagIndexSpec.cpp
        LIBS Qt6::Core
    )
    target_compile_definitions(debug_flag_index_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    # Drift guard for the UiText localization tables: built-in en/zh/ja key parity plus
    # source/resource literal key checks. Reads the tree from disk; repo root
    # injected as a compile definition.
    miacode_add_dev_tool(ui_text_locale_spec TEST
        SOURCES
            src/tools/ui_text/UiTextLocaleSpec.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/extensions/ExtensionManifest.h
            src/extensions/ExtensionManifest.cpp
        LIBS Qt6::Core
        INCLUDES src src/app/ui src/extensions
    )
    target_compile_definitions(ui_text_locale_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(native_chrome_policy_spec TEST
        SOURCES
            src/tools/ui/NativeChromePolicySpec.cpp
            src/app/quick_shell/QuickShellPopupPosition.h
            src/app/ui/UiNativeWindowThemePolicy.h
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(ui_text_preferences_spec TEST
        SOURCES
            src/tools/ui_text/UiTextPreferencesSpec.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/extensions/ExtensionManifest.h
            src/extensions/ExtensionManifest.cpp
        LIBS Qt6::Core
        INCLUDES src src/app/ui src/extensions
    )

    # Behavior spec locking DebugOptions.h accessor semantics (defaults, clamps,
    # debug-category gating, DComp cascade), the safety net for any future
    # table-driven registry refactor.
    miacode_add_dev_tool(debug_options_spec TEST
        SOURCES
            src/tools/debug_index/DebugOptionsSpec.cpp
            src/common/DebugOptions.h
        LIBS Qt6::Core
        INCLUDES src src/common
    )

    miacode_add_dev_tool(process_identity_fields_spec TEST
        SOURCES
            src/tools/debug_index/ProcessIdentityFieldsSpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(ui_hang_watchdog_policy_spec TEST
        SOURCES
            src/tools/debug_index/UiHangWatchdogPolicySpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(process_diagnostics_spec TEST
        SOURCES
            src/tools/debug_index/ProcessDiagnosticsSpec.cpp
            src/common/ProcessDiagnostics.h
            src/common/ProcessDiagnostics.cpp
            ${_miacode_log_core}
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src
    )
    if (WIN32)
        # Per-adapter VRAM gauge: CreateDXGIFactory1 + IDXGIAdapter3::QueryVideoMemoryInfo.
        target_link_libraries(process_diagnostics_spec PRIVATE dxgi)
    endif()

    miacode_add_dev_tool(thread_stack_capture_spec TEST
        SOURCES
            src/tools/debug_index/ThreadStackCaptureSpec.cpp
            src/common/ThreadStackCapture.h
            src/common/ThreadStackCapture.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    if (WIN32)
        # StackWalk64 / SymFromAddr for the GUI-thread self stack walk.
        target_link_libraries(thread_stack_capture_spec PRIVATE dbghelp)
    endif()

    miacode_add_dev_tool(windows_idle_event_diagnostics_spec TEST
        SOURCES
            src/tools/debug_index/WindowsIdleEventDiagnosticsSpec.cpp
            src/app/WindowsIdleEventDiagnostics.h
            src/app/WindowsIdleEventDiagnostics.cpp
            ${_miacode_log_core}
        LIBS Qt6::Core
        INCLUDES src
    )
    if (WIN32)
        target_link_libraries(windows_idle_event_diagnostics_spec PRIVATE User32 Wtsapi32)
    endif()

    miacode_add_dev_tool(window_visibility_diagnostics_spec TEST
        SOURCES
            src/tools/debug_index/WindowVisibilityDiagnosticsSpec.cpp
            src/app/WindowVisibilityDiagnostics.h
            src/app/WindowVisibilityDiagnostics.cpp
            ${_miacode_log_core}
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src
    )

    miacode_add_dev_tool(idle_freeze_repro_script_spec TEST
        SOURCES
            src/tools/debug_index/IdleFreezeReproScriptSpec.cpp
        LIBS Qt6::Core
    )
    target_compile_definitions(idle_freeze_repro_script_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
endif()

if (WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt HINTS "${QT_BIN_DIR}")
    set(MIACODE_REQUIRED_QT_RUNTIME_DLLS
        Qt6Quick
        Qt6Qml
        Qt6QmlMeta
        Qt6QmlModels
        Qt6QmlWorkerScript
    )
    set(MIACODE_REMOVED_QT_RUNTIME_DLLS
        Qt6OpenGLWidgets
        Qt6Concurrent
    )

    if (WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET MiaCode POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                "$<$<CONFIG:Debug>:--debug>$<$<NOT:$<CONFIG:Debug>>:--release>"
                --compiler-runtime
                --no-translations
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/src"
                "$<TARGET_FILE:MiaCode>"
            COMMENT "Deploying Qt runtime next to MiaCode.exe"
            VERBATIM
        )
        foreach(runtime_dll_base IN LISTS MIACODE_REQUIRED_QT_RUNTIME_DLLS)
            add_custom_command(TARGET MiaCode POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${QT_BIN_DIR}/${runtime_dll_base}$<$<CONFIG:Debug>:d>.dll"
                    "$<TARGET_FILE_DIR:MiaCode>/${runtime_dll_base}$<$<CONFIG:Debug>:d>.dll"
                VERBATIM
            )
        endforeach()
        foreach(runtime_dll_base IN LISTS MIACODE_REMOVED_QT_RUNTIME_DLLS)
            add_custom_command(TARGET MiaCode POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E rm -f
                    "$<TARGET_FILE_DIR:MiaCode>/${runtime_dll_base}$<$<CONFIG:Debug>:d>.dll"
                VERBATIM
            )
        endforeach()
    else()
        message(WARNING "windeployqt not found. Qt runtime DLLs will not be auto-deployed.")
    endif()
endif()
