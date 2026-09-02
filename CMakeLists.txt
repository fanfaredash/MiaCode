cmake_minimum_required(VERSION 3.21)

set(MIACODE_VERSION_MAJOR "2")
set(MIACODE_VERSION_MINOR "0")
set(MIACODE_VERSION_PATCH "0")
# Single source of truth for the prerelease tag and package version. The
# About dialog appends its compile date to this version at runtime.
set(MIACODE_VERSION_PRERELEASE "alpha")
set(MIACODE_BUILD_LAYOUT_REVISION "2026-04-21-first-unification")
set(MIACODE_VERSION "${MIACODE_VERSION_MAJOR}.${MIACODE_VERSION_MINOR}.${MIACODE_VERSION_PATCH}")
set(MIACODE_VERSION_STRING "${MIACODE_VERSION}")
if (NOT "${MIACODE_VERSION_PRERELEASE}" STREQUAL "")
    set(MIACODE_VERSION_STRING "${MIACODE_VERSION}-${MIACODE_VERSION_PRERELEASE}")
endif()
# The application metadata version remains the package version. The About
# dialog adds its compile date to the visible version separately.
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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Multimedia Quick Qml QuickControls2 ShaderTools Svg)

qt_policy(SET QTP0001 NEW)

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
# QAVVideoFrame -> QVideoFrame bridge needs. The Windows and macOS
# QtAVPlayer preview backends use it (see the QtAVPlayer block below).
if (WIN32 OR APPLE)
    find_package(Qt6 6.8 REQUIRED COMPONENTS MultimediaQuickPrivate)
endif()

add_executable(MiaCode WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/v2/ApplicationServices.cpp
    src/app/v2/ApplicationServices.h
    src/app/v2/DocumentBridge.h
    src/app/v2/EditorPageRouter.h
    src/app/v2/ExportEngine.h
    src/app/v2/LatencyEngine.h
    src/app/v2/MediaToolsEngine.h
    src/app/v2/PreferencesStore.h
    src/app/v2/PreviewSurface.h
    src/app/v2/PlaybackControl.h
    src/app/v2/SessionGeneration.h
    src/app/v2/AudioClockSource.h
    src/app/v2/PreviewPlaybackPort.h
    src/app/v2/ShellNotifications.cpp
    src/app/v2/ShellNotifications.h
    src/app/v2/TimelineSurface.h
    src/app/v2/PreviewAppearanceState.cpp
    src/app/v2/PreviewAppearanceState.h
    src/app/v2/ChartWorkspace.cpp
    src/app/v2/ChartWorkspace.h
    src/app/v2/ChartWorkspaceFileService.cpp
    src/app/v2/ChartWorkspaceFileService.h
    src/app/v2/AnalysisService.cpp
    src/app/v2/AnalysisService.h
    src/app/v2/EditorSyncController.cpp
    src/app/v2/EditorSyncController.h
    src/app/v2/UiRequestService.cpp
    src/app/v2/UiRequestService.h
    src/app/v2/JobProgressService.cpp
    src/app/v2/JobProgressService.h
    src/app/qml_ui/media/QmlMediaToolsModel.cpp
    src/app/qml_ui/media/QmlMediaToolsModel.h
    src/app/qml_ui/preview/QmlAudioSettingsModel.cpp
    src/app/qml_ui/preview/QmlAudioSettingsModel.h
    src/app/qml_ui/preview/QmlPreviewSettingsModel.cpp
    src/app/qml_ui/preview/QmlPreviewSettingsModel.h
    src/app/qml_ui/preferences/QmlPreferencesModel.cpp
    src/app/qml_ui/preferences/QmlPreferencesModel.h
    src/app/qml_ui/preferences/QmlAppBackgroundModel.cpp
    src/app/qml_ui/preferences/QmlAppBackgroundModel.h
    src/app/qml_ui/latency/QmlLatencyModel.cpp
    src/app/qml_ui/latency/QmlLatencyModel.h
    src/app/startup_diagnostics_win32.cpp
    src/app/graphics_backend.cpp
    src/app/process_identity.cpp
    src/app/WindowVisibilityDiagnostics.h
    src/app/WindowVisibilityDiagnostics.cpp
    src/app/gpu_adapter_probe.cpp
    src/app/gpu_device_provider.cpp
    src/app/gpu_high_performance_hint.cpp
    src/app/cli_shared.cpp
    src/app/cli_video_export.cpp
    src/app/cli_video_export_worker.cpp
    src/app/quick_shell/QuickShellPreviewCompositeSurface.h
    src/app/quick_shell/QuickShellPreviewCompositeSurface.cpp
    src/app/quick_shell/QuickShellPreviewSurfacePolicy.h
    src/common/AdoptedWidgetCoordinates.h
    src/common/AdoptedWidgetCoordinates.cpp
    src/common/AdoptedSurfaceDragAutoScroll.h
    src/common/AdoptedSurfaceDragAutoScroll.cpp
    src/app/runtime/Session.h
    src/app/runtime/Session.cpp
    src/app/runtime/RuntimeContext.h
    src/app/runtime/Shared.h
    src/app/runtime/Shared.cpp
    src/app/runtime/Shared.Preview.cpp
    src/app/runtime/document/DocumentFlow.cpp
    src/app/runtime/document/DocumentBridge.cpp
    src/app/runtime/document/DocumentFileFlow.cpp
    src/app/runtime/document/DocumentDesignerFlow.cpp
    src/app/runtime/document/DocumentAutosave.cpp
    src/app/runtime/document/DocumentEditorState.cpp
    src/app/runtime/document/DocumentTransforms.cpp
    src/app/runtime/document/DocumentPages.cpp
    src/app/runtime/media/MediaJobs.cpp
    src/app/runtime/media/MediaTools.cpp
    src/app/runtime/media/MediaJobsHost.cpp
    src/app/runtime/editor/EditorDisplay.cpp
    src/app/runtime/editor/EditorState.cpp
    src/app/runtime/export/ExportFlow.cpp
    src/app/runtime/export/ExportSnapshot.cpp
    src/app/runtime/export/VideoExportHost.cpp
    src/app/runtime/export/ExportWorker.cpp
    src/app/runtime/export/PackZip.cpp
    src/app/runtime/SessionBootstrap.cpp
    src/app/runtime/SessionBootstrapFinalize.cpp
    src/app/runtime/settings/Settings.cpp
    src/app/runtime/preview/StageMediaRoute.cpp
    src/app/runtime/preview/WarmupAndSettings.cpp
    src/app/runtime/playback/TimelineFlow.cpp
    src/app/runtime/playback/SessionForwarding.TimelineFlow.cpp
    src/app/runtime/playback/FollowSync.cpp
    src/app/runtime/playback/AnalysisFlow.cpp
    src/app/runtime/playback/QuickParse.cpp
    src/app/runtime/playback/Layout.cpp
    src/app/runtime/playback/FramePacing.cpp
    src/app/runtime/playback/SessionForwarding.FramePacing.cpp
    src/app/runtime/playback/Fullscreen.cpp
    src/app/runtime/playback/LayoutUi.cpp
    src/app/runtime/playback/SessionForwarding.cpp
    src/app/runtime/playback/Playback.cpp
    src/app/runtime/playback/SessionForwarding.Playback.cpp
    src/app/runtime/playback/Seek.cpp
    src/app/runtime/playback/PlaybackState.cpp
    src/app/runtime/playback/IntroRegion.cpp
    src/app/runtime/playback/Tick.cpp
    src/app/runtime/playback/PlaybackGlue.cpp
    src/app/runtime/playback/SessionForwarding.PlaybackGlue.cpp
    src/app/runtime/playback/PlaybackCoordinator.h
    src/app/runtime/playback/CoordinatorContract.cpp
    src/app/runtime/playback/PlaybackIdentityGate.cpp
    src/app/runtime/playback/PlaybackIdentityGate.h
    src/app/runtime/playback/PlaybackSurfaceAdapters.cpp
    src/app/runtime/playback/PlaybackSurfaceAdapters.h
    src/app/runtime/timeline/TimelineCommandGate.cpp
    src/app/runtime/timeline/TimelineCommandGate.h
    src/app/runtime/timeline/TimelineHost.cpp
    src/app/runtime/timeline/TimelineHost.h
    src/app/runtime/preview/PreviewHost.cpp
    src/app/runtime/preview/PreviewHost.h
    src/app/runtime/validation/ValidationFlow.cpp
    src/app/runtime/validation/ValidationListUi.cpp
    src/app/runtime/validation/ValidationRender.cpp
    src/app/runtime/validation/ValidationRuntime.cpp
    src/app/runtime/playback/SurfaceContract.cpp
    src/app/runtime/playback/SessionForwarding.SurfaceContract.cpp
    src/app/runtime/shell/Interaction.cpp
    src/app/runtime/shell/Runtime.cpp
    src/app/runtime/shell/ShellHost.cpp
    src/app/runtime/shell/Shell.cpp
    src/app/ui/UiText.h
    src/app/ui/UiText.cpp
    src/app/ui/AppBackgroundSettings.h
    src/app/ui/AppBackgroundSettings.cpp
    src/app/ui/ThemeVariantResolver.h
    src/app/ui/ThemeVariantResolver.cpp
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
    src/common/MuriTypes.h
    src/common/MuriTypes.cpp
    src/common/MuriRenderOptions.h
    src/common/MuriConfig.h
    src/common/DebugLog.h
    src/common/DebugLog.cpp
    src/common/UiHangWatchdog.h
    src/common/UiHangWatchdog.cpp
    src/common/UiHangWatchdogPolicy.h
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
    src/common/InputShortcutGesture.h
    src/common/InputShortcutGesture.cpp
    src/common/ChartAssetPaths.h
    src/audio/OfflineAudioDecoder.h
    src/audio/OfflineAudioDecoder.cpp
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
    src/editor/BookmarkCommentSyntax.h
    src/editor/BookmarkCommentSyntax.cpp
    src/editor/TouchPadAuthoringEdit.h
    src/editor/TouchPadAuthoringEdit.cpp
    src/editor/SimaiCompletionCatalog.h
    src/editor/SimaiCompletionCatalog.cpp
    src/editor/SimaiTextEditPolicy.h
    src/editor/SimaiTextEditPolicy.cpp
    src/editor/BracketScopeHighlighter.h
    src/editor/BracketScopeHighlighter.cpp
    src/tools/latency/LatencyAnalysis.h
    src/tools/latency/LatencyAnalysis.cpp
    src/tools/latency/LatencyTestChartBuilder.h
    src/tools/latency/LatencyTestChartBuilder.cpp
    src/tools/latency/LatencySandboxController.h
    src/tools/latency/LatencySandboxController.cpp
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
    src/tools/video_export/VideoExportSettings.h
    src/tools/video_export/VideoExportSettings.cpp
    src/tools/video_export/FontLibrary.h
    src/tools/video_export/FontLibrary.cpp
    src/tools/video_export/VideoExportController.h
    src/tools/video_export/VideoExportController.cpp
    src/tools/video_export/VideoExportEncoder.cpp
    src/tools/video_export/VideoExportDiagnostics.cpp
    src/tools/video_export/VideoExportFrameRender.cpp
    src/tools/video_export/VideoExportPipeline.cpp
    src/tools/video_export/VideoExportPreparedTask.cpp
    src/tools/video_export/VideoExportMediaTimeline.h
    src/tools/video_export/VideoExportMediaTimeline.cpp
    src/tools/video_export/VideoExportRuntimePolicy.h
    src/tools/video_export/VideoExportRuntimePolicy.cpp
    src/tools/video_export/VideoExportQuickRenderBackend.h
    src/tools/video_export/VideoExportQuickRenderBackend.cpp
    src/tools/video_export/RawVideoPipeTransport.h
    src/tools/video_export/RawVideoPipeTransport.cpp
    src/tools/video_export/VideoExportSnapshot.h
    src/tools/video_export/VideoExportSnapshot.cpp
    src/tools/zip_export/ChartZipPackager.h
    src/tools/zip_export/ChartZipPackager.cpp
    src/tools/media/PvBatchCompressionScanner.h
    src/tools/media/PvBatchCompressionScanner.cpp
    src/tools/media/PvBatchCompressionWorker.h
    src/tools/media/PvBatchCompressionWorker.cpp
    src/tools/cover_export/CoverLayoutModel.h
    src/tools/cover_export/CoverLayoutModel.cpp
    src/tools/cover_export/CoverCompositionState.h
    src/tools/cover_export/CoverCompositionState.cpp
    src/tools/cover_export/CoverCompositeRenderer.h
    src/tools/cover_export/CoverCompositeRenderer.cpp
    src/tools/cover_export/SceneFrameRenderer.h
    src/tools/cover_export/SceneFrameRenderer.cpp
    src/tools/cover_export/CoverFramePlaybackController.h
    src/tools/cover_export/CoverFramePlaybackController.cpp
    src/tools/cover_export/CoverFrameSceneBinder.h
    src/tools/cover_export/CoverFrameSceneBinder.cpp
    src/tools/cover_export/CoverFrameExportPlan.h
    src/tools/cover_export/CoverFrameExportPlan.cpp
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
    src/preview/runtime/PvMemoryDiagnostics.h
    src/preview/runtime/PvMemoryDiagnostics.cpp
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
    src/audio/PreviewAudioBackend.h
    src/audio/PreviewAudioWorkerProtocol.h
    src/audio/PreviewAudioPlaybackFlowPolicy.h
    src/audio/PreviewAudioCommandQueue.h
    src/audio/PreviewAudioCommandQueue.cpp
    src/audio/PreviewBassEmergencyPause.h
    src/audio/PreviewBassEmergencyPause.cpp
    src/audio/PreviewAudioSettings.h
    src/audio/PreviewAudioSettings.cpp
    src/audio/MiniaudioPreviewAudioBackend.h
    src/audio/MiniaudioPreviewAudioBackend.cpp
    src/audio/PreviewAudioDeviceChangePolicy.h
    src/audio/PreviewAudioDeviceCutoff.h
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
    src/core/chart/parser/SimaiCommentScan.h
    src/core/chart/parser/SimaiCommentScan.cpp
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
    src/app/qml_ui/drop/QmlChartDropBridge.h
    src/app/qml_ui/drop/QmlChartDropBridge.cpp
    src/app/v2/ChartDropImportService.h
    src/app/v2/ChartDropImportService.cpp
    resources/app_icons.qrc
    resources/fonts.qrc
    resources/slide_data.qrc
    resources/preview_judge_effects.qrc
    resources/preview_runtime_qml.qrc
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

# Default UI (v2).
# QTP0001 NEW embeds this module at :/qt/qml/MiaCode/UI/.
set(MIACODE_UI_QML_FILES
    src/app/qml_ui/Main.qml
    src/app/qml_ui/UiText.qml
    src/app/qml_ui/ViewState.qml
    src/app/qml_ui/theme/Theme.qml
    src/app/qml_ui/components/IconButton.qml
    src/app/qml_ui/components/AppCheckBox.qml
    src/app/qml_ui/components/AppDropDownButton.qml
    src/app/qml_ui/components/AppTab.qml
    src/app/qml_ui/components/DifficultySwatch.qml
    src/app/qml_ui/components/PanelHeader.qml
    src/app/qml_ui/components/SplitHandle.qml
    src/app/qml_ui/components/Tooltip.qml
    src/app/qml_ui/components/HoverChrome.qml
    src/app/qml_ui/components/ChromeRow.qml
    src/app/qml_ui/components/NavRow.qml
    src/app/qml_ui/components/AppTextField.qml
    src/app/qml_ui/components/AppTextArea.qml
    src/app/qml_ui/components/AppButton.qml
    src/app/qml_ui/components/AppSwitch.qml
    src/app/qml_ui/components/AppComboBox.qml
    src/app/qml_ui/components/AppSlider.qml
    src/app/qml_ui/components/AppScrollBar.qml
    src/app/qml_ui/components/AppMenu.qml
    src/app/qml_ui/components/AppMenuAction.qml
    src/app/qml_ui/components/AppMenuItem.qml
    src/app/qml_ui/components/AppMenuSeparator.qml
    src/app/qml_ui/components/UiRequestHost.qml
    src/app/qml_ui/components/DialogFooter.qml
    src/app/qml_ui/components/DialogDrag.qml
    src/app/qml_ui/components/ChoiceDialog.qml
    src/app/qml_ui/components/EditableValue.qml
    src/app/qml_ui/components/LabeledCombo.qml
    src/app/qml_ui/components/LabeledSlider.qml
    src/app/qml_ui/components/AboutDialog.qml
    src/app/qml_ui/components/JobProgressOverlay.qml
    src/app/qml_ui/chrome/WindowTitleBar.qml
    src/app/qml_ui/chrome/WindowCaptionButtons.qml
    src/app/qml_ui/chrome/MainMenu.qml
    src/app/qml_ui/chrome/ShortcutBindings.qml
    src/app/qml_ui/chrome/MainMenuCommands.qml
    src/app/qml_ui/chrome/MainToolBar.qml
    src/app/qml_ui/chrome/StatusBar.qml
    src/app/qml_ui/layout/MainView.qml
    src/app/qml_ui/layout/MainSplitView.qml
    src/app/qml_ui/layout/CompactPanelLayer.qml
    src/app/qml_ui/sidebar/Sidebar.qml
    src/app/qml_ui/sidebar/ActivityBar.qml
    src/app/qml_ui/sidebar/ChartFieldSidebar.qml
    src/app/qml_ui/sidebar/DifficultyList.qml
    src/app/qml_ui/sidebar/ExportSidebarPage.qml
    src/app/qml_ui/sidebar/ToolsSidebarPage.qml
    src/app/qml_ui/export/ExportRangeSelector.qml
    src/app/qml_ui/export/ExportVideoPage.qml
    src/app/qml_ui/export/CoverExportPage.qml
    src/app/qml_ui/editor/EditorPane.qml
    src/app/qml_ui/editor/EditorTabBar.qml
    src/app/qml_ui/editor/SourceEditor.qml
    src/app/qml_ui/editor/CompletionPopup.qml
    src/app/qml_ui/editor/NormalizeOptionsDialog.qml
    src/app/qml_ui/media/MediaToolsDialog.qml
    src/app/qml_ui/media/PvBatchCompressionDialog.qml
    src/app/qml_ui/media/PrependBlankDialog.qml
    src/app/qml_ui/preferences/PreferencesDialog.qml
    src/app/qml_ui/latency/LatencyPage.qml
    src/app/qml_ui/editor/FindReplaceBar.qml
    src/app/qml_ui/editor/BookmarkList.qml
    src/app/qml_ui/editor/LineNumberGutter.qml
    src/app/qml_ui/timeline/BottomPanel.qml
    src/app/qml_ui/timeline/BottomTabBar.qml
    src/app/qml_ui/timeline/TimelineZoomMenu.qml
    src/app/qml_ui/timeline/TimelineBrightnessMenu.qml
    src/app/qml_ui/preview/PreviewPane.qml
    src/app/qml_ui/preview/PreviewRenderModeMenu.qml
    src/app/qml_ui/preview/PreviewRateMenu.qml
    src/app/qml_ui/preview/PreviewTransport.qml
    src/app/qml_ui/preview/NoteStatistics.qml
    src/app/qml_ui/preview/AudioSettingsDialog.qml
    src/app/qml_ui/preview/PreviewSettingsDialog.qml
)
foreach(qml_file IN LISTS MIACODE_UI_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
set_source_files_properties(
    src/app/qml_ui/theme/Theme.qml
    src/app/qml_ui/UiText.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

set(MIACODE_UI_ICON_FILES
    src/app/qml_ui/resources/icons/folder-open.svg
    src/app/qml_ui/resources/icons/save.svg
    src/app/qml_ui/resources/icons/undo.svg
    src/app/qml_ui/resources/icons/redo.svg
    src/app/qml_ui/resources/icons/play.svg
    src/app/qml_ui/resources/icons/pause.svg
    src/app/qml_ui/resources/icons/stop.svg
    src/app/qml_ui/resources/icons/fullscreen.svg
    src/app/qml_ui/resources/icons/panel-left.svg
    src/app/qml_ui/resources/icons/panel-bottom.svg
    src/app/qml_ui/resources/icons/panel-right.svg
    src/app/qml_ui/resources/icons/settings.svg
    src/app/qml_ui/resources/icons/chart.svg
    src/app/qml_ui/resources/icons/export.svg
    src/app/qml_ui/resources/icons/tools.svg
    src/app/qml_ui/resources/icons/audio-settings.svg
    src/app/qml_ui/resources/icons/preview-settings.svg
    src/app/qml_ui/resources/icons/close.svg
    src/app/qml_ui/resources/icons/more.svg
    src/app/qml_ui/resources/icons/metadata.svg
    src/app/qml_ui/resources/icons/app-icon.svg
    src/app/qml_ui/resources/icons/chevron-down.svg
    # Canonical MiaCode brand PNG (same asset as resources/app_icons.qrc).
    resources/icons/app.png
)
foreach(icon_file IN LISTS MIACODE_UI_ICON_FILES)
    get_filename_component(icon_name "${icon_file}" NAME)
    set_source_files_properties("${icon_file}" PROPERTIES QT_RESOURCE_ALIAS "icons/${icon_name}")
endforeach()

qt_add_qml_module(MiaCode
    URI MiaCode.UI
    VERSION 1.0
    SOURCES
        src/app/qml_ui/SimaiSyntaxHighlighter.cpp
        src/app/qml_ui/SimaiSyntaxHighlighter.h
        src/app/qml_ui/EditorTextStyle.cpp
        src/app/qml_ui/EditorTextStyle.h
        src/app/qml_ui/QmlApplicationContext.cpp
        src/app/qml_ui/QmlApplicationContext.h
        src/app/qml_ui/QmlCommandService.cpp
        src/app/qml_ui/QmlCommandService.h
        src/app/qml_ui/QmlShortcutCommands.h
        src/app/qml_ui/QmlDocumentModel.cpp
        src/app/qml_ui/QmlDocumentModel.h
        src/app/qml_ui/QmlDocumentProjection.cpp
        src/app/qml_ui/QmlDocumentProjection.h
        src/app/qml_ui/QmlAnalysisProjection.cpp
        src/app/qml_ui/QmlAnalysisProjection.h
        src/app/qml_ui/QmlAnalysisModel.cpp
        src/app/qml_ui/QmlAnalysisModel.h
        src/app/qml_ui/QmlPreviewModel.cpp
        src/app/qml_ui/QmlPreviewModel.h
        src/app/qml_ui/QmlTimelineModel.cpp
        src/app/qml_ui/QmlTimelineModel.h
        src/app/qml_ui/QmlShellLifecycle.cpp
        src/app/qml_ui/QmlShellLifecycle.h
        src/app/qml_ui/QmlNoteImageProvider.cpp
        src/app/qml_ui/QmlNoteImageProvider.h
        src/app/qml_ui/QmlUiSettings.cpp
        src/app/qml_ui/QmlUiSettings.h
        src/app/qml_ui/QmlUiPlatformChrome.h
        src/app/qml_ui/QmlEditorPageHost.cpp
        src/app/qml_ui/QmlEditorPageHost.h
        src/app/qml_ui/QmlEditorController.cpp
        src/app/qml_ui/QmlEditorController.h
        src/app/qml_ui/QmlShortcutModel.cpp
        src/app/qml_ui/QmlShortcutModel.h
        src/app/qml_ui/QmlEditorInputBridge.cpp
        src/app/qml_ui/QmlEditorInputBridge.h
        src/app/qml_ui/export/QmlExportSession.cpp
        src/app/qml_ui/export/QmlExportSession.h
        src/app/qml_ui/export/QmlCoverExportSession.cpp
        src/app/qml_ui/export/QmlCoverExportSession.h
        src/app/qml_ui/QmlUiWindowChrome.cpp
        src/app/qml_ui/QmlUiWindowChrome.h
        src/app/qml_ui/QmlUiBootstrap.cpp
        src/app/qml_ui/QmlUiBootstrap.h
        src/app/qml_ui/QmlUiRootLifecycle.h
    QML_FILES
        ${MIACODE_UI_QML_FILES}
    RESOURCES
        ${MIACODE_UI_ICON_FILES}
)

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
    soundtouch
    miniz
)
target_include_directories(MiaCode PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    third_party/bass/include
    src
    src/app
    src/app/qml_ui
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
)

target_sources(MiaCode PRIVATE
    src/common/PreviewAudioMixConfig.h
    src/audio/PreviewAudioWorkerFactory.h
    src/audio/PreviewAudioWorkerFactory.cpp
    src/audio/PreviewAudioWorker.h
    src/audio/PreviewAudioWorker.cpp
    src/audio/PreviewBassDeviceLease.h
    src/audio/PreviewBassDeviceLease.cpp
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
    target_link_libraries(MiaCode PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bass.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bassmix.lib"
        winmm
        ole32       # Windows Core Audio endpoint notification COM API
        avrt  # Multimedia Class Scheduler Service (AvSetMmThreadCharacteristicsW)
        d3d11        # Shared preview device, D3D11 export session, QuickShell composite
                     # surface, stage-media host (src/preview/runtime, src/app)
        dxgi         # Adapter enumeration + per-process VRAM gauge
                     # (GpuDevicePolicy, ProcessDiagnostics, TimelineQuickItem)
        User32       # RegisterPowerSettingNotification
        Wtsapi32     # Session lock/unlock notifications for idle-freeze diagnostics
        version      # Startup diagnostic file-version queries (MinGW)
        rstrtmgr     # Restart Manager process lookup for media tools (MinGW)
        opengl32     # QtAVPlayer D3D11/OpenGL texture interop (MinGW)
        d3dcompiler  # QtAVPlayer D3DCompile (MinGW; MSVC uses pragma comment)
        dwmapi       # v2 QmlUiWindowChrome DwmExtendFrameIntoClientArea
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

# Extension packages are archive/offline inputs only. Remove any stale
# install-root payload left by an older build so an incremental build cannot
# accidentally turn historical extension folders into shipped product data.
add_custom_command(TARGET MiaCode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        "$<TARGET_FILE_DIR:MiaCode>/extensions"
    COMMENT "Removing archived extension payload from product output"
    VERBATIM
)

if (APPLE)
    target_sources(MiaCode PRIVATE
        src/app/ui/UiNativeWindowThemeMac.h
        src/app/ui/UiNativeWindowThemeMac.mm
        src/app/qml_ui/QmlUiWindowChrome.mm
    )
    target_link_libraries(MiaCode PRIVATE "-framework AppKit")
endif()

target_link_libraries(MiaCode PRIVATE Qt6::Multimedia)
target_compile_definitions(MiaCode PRIVATE HAVE_QT_MULTIMEDIA=1)
# Qt SVG — renders the Google Material "settings" gear toolbar icon
# (makeSettingsGearIcon, MainWindowShared.cpp) via QSvgRenderer.
target_link_libraries(MiaCode PRIVATE Qt6::Svg)

# ---------------------------------------------------------------------------
# QtAVPlayer (FFmpeg) — preview background-video decode backend.
#
# Replaces QMediaPlayer for PV/BG decoding inside PreviewStageMediaHost; see
# docs/ops/DEBUG_INDEX.md. QtAVPlayer is the
# MIT-licensed source vendored under third_party/QtAVPlayer, compiled straight
# into MiaCode. It links external FFmpeg shared libraries whose dev SDK
# (headers + libraries) is supplied through MIACODE_FFMPEG_DEV_DIR. Windows
# uses the provisioned n7.1 LGPL SDK; macOS uses the project-provisioned 6.1.2
# SDK (`scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh`).
#
# Windows and macOS use QtAVPlayer. Windows selects D3D11VA where available;
# macOS selects the vendored VideoToolbox/Metal hwdevice. Other platforms keep
# the QMediaPlayer path — MIACODE_USE_QTAVPLAYER stays undefined there.
# ---------------------------------------------------------------------------
if (WIN32 OR APPLE)
    if (WIN32)
        set(MIACODE_FFMPEG_DEV_DIR_DEFAULT
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/windows/dev")
    else()
        set(MIACODE_FFMPEG_DEV_DIR_DEFAULT
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/macos/dev")
    endif()
    set(MIACODE_FFMPEG_DEV_DIR "${MIACODE_FFMPEG_DEV_DIR_DEFAULT}"
        CACHE PATH "FFmpeg shared dev SDK root (include/ + lib/) for the QtAVPlayer preview backend")
    if (NOT EXISTS "${MIACODE_FFMPEG_DEV_DIR}/include" OR NOT EXISTS "${MIACODE_FFMPEG_DEV_DIR}/lib")
        if (APPLE)
            set(MIACODE_FFMPEG_PROVISION_COMMAND
                "bash scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh")
        else()
            set(MIACODE_FFMPEG_PROVISION_COMMAND
                "scripts/ffmpeg/ensure-windows-ffmpeg-dev.ps1")
        endif()
        message(FATAL_ERROR
            "FFmpeg dev SDK not found at '${MIACODE_FFMPEG_DEV_DIR}'. "
            "Run '${MIACODE_FFMPEG_PROVISION_COMMAND}', or pass "
            "-DMIACODE_FFMPEG_DEV_DIR=<root containing include/ lib/>.")
    endif()
    if (APPLE AND MIACODE_FFMPEG_DEV_DIR MATCHES "^/(opt/homebrew|usr/local)/")
        message(FATAL_ERROR
            "macOS release builds must use the project-provisioned FFmpeg SDK, not Homebrew: "
            "${MIACODE_FFMPEG_DEV_DIR}. Run bash scripts/ffmpeg/ensure-macos-ffmpeg-dev.sh.")
    endif()

    # find_library() in QtAVPlayer.cmake resolves the FFmpeg import libs from
    # <prefix>/lib; prepending the dev root makes our SDK win over any system one.
    list(PREPEND CMAKE_PREFIX_PATH "${MIACODE_FFMPEG_DEV_DIR}")

    set(QTAVPLAYER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/QtAVPlayer/src")
    set(QT_AVPLAYER_DIR "${QTAVPLAYER_SRC}/QtAVPlayer")
    set(QT_AVPLAYER_MULTIMEDIA ON CACHE BOOL "" FORCE)
    # Drop avdevice (capture-device I/O only, unused for file playback):
    # QtAVPlayer skips finding/linking it and qavdemuxer compiles out its lone
    # avdevice_register_all() call.
    set(QT_AVPLAYER_NO_AVDEVICE ON)
    if (APPLE)
        # QtAVPlayer.cmake uses find_library(). These cache entries otherwise
        # survive an SDK change and can keep linking an old Homebrew path even
        # though CMAKE_PREFIX_PATH now starts with the project-local SDK.
        foreach(MIACODE_QTAVPLAYER_FFMPEG_LIBRARY
                AVFILTER_LIBRARY AVCODEC_LIBRARY AVFORMAT_LIBRARY AVUTIL_LIBRARY
                SWRESAMPLE_LIBRARY SWSCALE_LIBRARY)
            unset(${MIACODE_QTAVPLAYER_FFMPEG_LIBRARY} CACHE)
            unset(${MIACODE_QTAVPLAYER_FFMPEG_LIBRARY})
        endforeach()
    endif()
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

    if (WIN32)
        # Runtime: the preview backend loads these next to MiaCode.exe. The
        # Windows package script stages the same pinned majors.
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
    else()
        # The project-local macOS SDK has @rpath install names. Make the local
        # app bundle runnable too, then let the release packager stage the
        # same six dylibs in its final Frameworks directory.
        set(MIACODE_MACOS_FFMPEG_RUNTIME_LIBRARIES
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libavcodec.60.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libavfilter.9.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libavformat.60.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libavutil.58.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libswresample.4.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libswscale.7.dylib"
        )
        foreach(MIACODE_MACOS_FFMPEG_RUNTIME_LIBRARY IN LISTS MIACODE_MACOS_FFMPEG_RUNTIME_LIBRARIES)
            if (NOT EXISTS "${MIACODE_MACOS_FFMPEG_RUNTIME_LIBRARY}")
                message(FATAL_ERROR
                    "macOS FFmpeg SDK is missing required runtime library: "
                    "${MIACODE_MACOS_FFMPEG_RUNTIME_LIBRARY}")
            endif()
        endforeach()
        set_property(TARGET MiaCode APPEND PROPERTY BUILD_RPATH
            "@executable_path/../Frameworks")
        set_property(TARGET MiaCode APPEND PROPERTY INSTALL_RPATH
            "@executable_path/../Frameworks")
        add_custom_command(TARGET MiaCode POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:MiaCode>/../Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${MIACODE_MACOS_FFMPEG_RUNTIME_LIBRARIES}
                "$<TARGET_FILE_DIR:MiaCode>/../Frameworks"
            VERBATIM
        )
    endif()
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
    set(_miacode_muri_analysis_core
        src/common/MuriTypes.h
        src/common/MuriTypes.cpp
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
        src/tools/muri/MuriStaticChecker.h
        src/tools/muri/MuriStaticChecker.cpp
    )

    # Qt Test is a dev-tools-only dependency: QML input regressions need a
    # supported way to deliver real mouse/key events into a QQuickWindow.
    #
    # Qt Network is dev-tools-only for the same reason: the Net page was removed
    # from the v2 product runtime, so its engine lives on in net_client_spec
    # alone. See docs/ops/DEPENDENCY_ALLOWLIST.md — linking it into MiaCode is a
    # spec failure, not a style preference.
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test Network)

    # MiaCode.UI is compiled into the app target, so a spec executable cannot
    # import it. Mirror the module into a plain file-system import directory so
    # QML regressions can instantiate the real .qml files instead of retyping a
    # copy of them. configure_file() re-runs CMake when a source .qml changes,
    # which keeps the mirror in step with the module.
    set(MIACODE_QML_SPEC_IMPORT_ROOT "${CMAKE_CURRENT_BINARY_DIR}/qml_spec_imports")
    set(_miacode_qml_spec_module_dir "${MIACODE_QML_SPEC_IMPORT_ROOT}/MiaCode/UI")
    set(_miacode_qml_spec_qmldir "module MiaCode.UI\n")
    foreach(qml_file IN LISTS MIACODE_UI_QML_FILES)
        get_filename_component(_qml_name "${qml_file}" NAME)
        get_filename_component(_qml_type "${qml_file}" NAME_WE)
        if (_qml_name STREQUAL "Theme.qml" OR _qml_name STREQUAL "UiText.qml")
            string(APPEND _miacode_qml_spec_qmldir "singleton ${_qml_type} 1.0 ${_qml_name}\n")
        else()
            string(APPEND _miacode_qml_spec_qmldir "${_qml_type} 1.0 ${_qml_name}\n")
        endif()
        configure_file("${qml_file}" "${_miacode_qml_spec_module_dir}/${_qml_name}" COPYONLY)
    endforeach()
    file(WRITE "${_miacode_qml_spec_module_dir}/qmldir" "${_miacode_qml_spec_qmldir}")

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
        # Keep QObject-based specs self-contained even when a source header is
        # also part of the main application target.
        set_target_properties(${NAME} PROPERTIES AUTOMOC ON)
        if (DT_LIBS)
            target_link_libraries(${NAME} PRIVATE ${DT_LIBS})
        endif()
        if (DT_INCLUDES)
            target_include_directories(${NAME} PRIVATE ${DT_INCLUDES})
        endif()
        if (DT_TEST)
            # Run from the source tree: specs that touch runtime assets resolve
            # them relative to the working directory (e.g. resolveSfxDirectory in
            # VideoExportAudioRenderPlan), so launching from the build dir made
            # them fail with "preview SFX directory could not be resolved".
            add_test(NAME ${NAME} COMMAND ${NAME} WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
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
            src/audio/PreviewAudioWorkerProtocol.h
            src/audio/PreviewAudioCommandQueue.h
            src/audio/PreviewAudioCommandQueue.cpp
            src/audio/PreviewBassEmergencyPause.h
            src/audio/PreviewBassEmergencyPause.cpp
            src/audio/PreviewAudioWorkerFactory.h
            src/audio/PreviewAudioWorkerFactory.cpp
            src/audio/PreviewAudioWorker.h
            src/audio/PreviewAudioWorker.cpp
            src/audio/PreviewBassDeviceLease.h
            src/audio/PreviewBassDeviceLease.cpp
            src/tools/probe/SoundtouchProbe.cpp
            src/audio/PreviewAudioSettings.h
            src/audio/PreviewAudioSettings.cpp
            src/audio/MiniaudioPreviewAudioBackend.h
            src/audio/MiniaudioPreviewAudioBackend.cpp
            src/audio/QtPreviewSfxRuntime.h
            src/audio/QtPreviewSfxRuntime.cpp
            src/common/Mmcss.h
            src/common/Mmcss.cpp
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
            avrt
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
            src/audio/OfflineAudioDecoder.h
            src/audio/OfflineAudioDecoder.cpp
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

    miacode_add_dev_tool(app_background_settings_spec TEST
        SOURCES
            src/tools/ui/AppBackgroundSettingsSpec.cpp
            src/app/ui/AppBackgroundSettings.h
            src/app/ui/AppBackgroundSettings.cpp
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(theme_variant_resolver_spec TEST
        SOURCES
            src/tools/ui/ThemeVariantResolverSpec.cpp
            src/app/ui/ThemeVariantResolver.h
            src/app/ui/ThemeVariantResolver.cpp
            src/app/ui/UiText.h
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src
    )

    miacode_add_dev_tool(qml_app_background_model_spec TEST
        SOURCES
            src/tools/qml_ui/QmlAppBackgroundModelSpec.cpp
            src/app/qml_ui/preferences/QmlAppBackgroundModel.h
            src/app/qml_ui/preferences/QmlAppBackgroundModel.cpp
            src/app/ui/AppBackgroundSettings.h
            src/app/ui/AppBackgroundSettings.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/app/v2/UiRequestService.h
            src/app/v2/UiRequestService.cpp
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src
    )

    miacode_add_dev_tool(qml_chart_drop_bridge_spec TEST
        SOURCES
            src/tools/qml_ui/QmlChartDropBridgeSpec.cpp
            src/app/qml_ui/drop/QmlChartDropBridge.h
            src/app/qml_ui/drop/QmlChartDropBridge.cpp
            src/app/v2/ChartDropImportService.h
            src/app/v2/ChartDropImportService.cpp
        LIBS Qt6::Core Qt6::Gui Qt6::Quick
        INCLUDES src
    )

    miacode_add_dev_tool(extension_product_boundary_spec TEST
        SOURCES
            src/tools/extensions/ExtensionProductBoundarySpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(extension_product_boundary_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

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
            src/tools/cover_export/CoverFrameExportPlan.h
            src/tools/cover_export/CoverFrameExportPlan.cpp
            src/tools/cover_export/CoverCompositionState.h
            src/tools/cover_export/CoverCompositionState.cpp
            src/tools/cover_export/CoverCompositionPersistenceGuard.h
            src/tools/cover_export/CoverCompositionPersistenceGuard.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES src src/app/ui
    )

    miacode_add_dev_tool(cover_frame_playback_controller_spec TEST
        SOURCES
            src/tools/cover_export/CoverFramePlaybackControllerSpec.cpp
            src/tools/cover_export/CoverFramePlaybackController.h
            src/tools/cover_export/CoverFramePlaybackController.cpp
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(cover_frame_scene_binder_spec TEST
        SOURCES
            src/tools/cover_export/CoverFrameSceneBinderSpec.cpp
            src/tools/cover_export/CoverFrameSceneBinder.h
            src/tools/cover_export/CoverFrameSceneBinder.cpp
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src
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
    # Source-contract assertions in TimelineModelSpec must work from both the
    # standard `build/` CTest directory and the developer build tree.
    target_compile_definitions(timeline_model_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(timeline_marker_offset_spec TEST
        SOURCES
            src/tools/timeline/TimelineMarkerOffsetSpec.cpp
            src/timeline/TimelineMarkerOffset.h
            src/timeline/TimelineData.h
        LIBS Qt6::Core
        INCLUDES src src/common src/timeline
    )

    miacode_add_dev_tool(qml_document_projection_spec TEST
        SOURCES
            src/tools/qml_ui/QmlDocumentProjectionSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            src/app/v2/ChartWorkspace.cpp
            src/app/v2/ChartWorkspace.h
            src/app/v2/AnalysisService.h
            src/app/qml_ui/QmlDocumentProjection.cpp
            src/app/qml_ui/QmlDocumentProjection.h
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(chart_workspace_spec TEST
        SOURCES
            src/tools/v2/ChartWorkspaceSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            src/app/v2/ChartWorkspace.cpp
            src/app/v2/ChartWorkspace.h
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(chart_workspace_file_service_spec TEST
        SOURCES
            src/tools/v2/ChartWorkspaceFileServiceSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            src/app/v2/ChartWorkspace.cpp
            src/app/v2/ChartWorkspace.h
            src/app/v2/ChartWorkspaceFileService.cpp
            src/app/v2/ChartWorkspaceFileService.h
        LIBS Qt6::Core
        INCLUDES src
    )

    # Qt6::Core + Qt6::Test only: this target failing to link is how we notice a
    # QFileDialog / QMessageBox creeping back into the request boundary.
    miacode_add_dev_tool(ui_request_service_spec TEST
        SOURCES
            src/tools/v2/UiRequestServiceSpec.cpp
            src/app/v2/UiRequestService.cpp
            src/app/v2/UiRequestService.h
        LIBS Qt6::Core Qt6::Test
        INCLUDES src
    )

    miacode_add_dev_tool(editor_sync_controller_spec TEST
        SOURCES
            src/tools/v2/EditorSyncControllerSpec.cpp
            src/app/v2/EditorSyncController.cpp
            src/app/v2/EditorSyncController.h
        LIBS Qt6::Core Qt6::Test
        INCLUDES src
    )

    miacode_add_dev_tool(job_progress_service_spec TEST
        SOURCES
            src/tools/v2/JobProgressServiceSpec.cpp
            src/app/v2/JobProgressService.cpp
            src/app/v2/JobProgressService.h
        LIBS Qt6::Core Qt6::Test
        INCLUDES src
    )

    miacode_add_dev_tool(analysis_service_spec TEST
        SOURCES
            src/tools/v2/AnalysisServiceSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            ${_miacode_muri_analysis_core}
            src/timeline/TimelineSlowRefresh.h
            src/timeline/TimelineSlowRefresh.cpp
            src/app/v2/ChartWorkspace.cpp
            src/app/v2/ChartWorkspace.h
            src/app/v2/AnalysisService.cpp
            src/app/v2/AnalysisService.h
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/app/ui src/common src/core/chart src/core/chart/parser src/timeline src/tools
    )

    # Stage 3.5 items 2-3: the editor page-routing seam. EditorPageRouter is not
    # even a QObject, so anything Qt-GUI-shaped creeping into the contract fails
    # to link here.
    miacode_add_dev_tool(editor_page_router_spec TEST
        SOURCES
            src/tools/v2/EditorPageRouterSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            ${_miacode_muri_analysis_core}
            src/timeline/TimelineSlowRefresh.h
            src/timeline/TimelineSlowRefresh.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/app/v2/ApplicationServices.h
            src/app/v2/ApplicationServices.cpp
            src/app/v2/AnalysisService.h
            src/app/v2/AnalysisService.cpp
            src/app/v2/ChartDropImportService.h
            src/app/v2/ChartDropImportService.cpp
            src/app/v2/ChartWorkspace.h
            src/app/v2/ChartWorkspace.cpp
            src/app/v2/ChartWorkspaceFileService.h
            src/app/v2/ChartWorkspaceFileService.cpp
            src/app/v2/EditorPageRouter.h
            src/app/v2/EditorSyncController.h
            src/app/v2/EditorSyncController.cpp
            src/app/v2/JobProgressService.h
            src/app/v2/JobProgressService.cpp
            src/app/v2/PreviewAppearanceState.h
            src/app/v2/PreviewAppearanceState.cpp
            src/app/v2/ShellNotifications.h
            src/app/v2/ShellNotifications.cpp
            src/app/v2/UiRequestService.h
            src/app/v2/UiRequestService.cpp
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Test
        INCLUDES src src/app/ui src/audio src/common src/core/chart src/core/chart/parser src/core/video src/timeline src/tools
    )
    target_compile_definitions(editor_page_router_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    # Stage 3.5 item 2: the export page's engine seam. The implementation still
    # lives inside a QMainWindow, so linking Core+Gui+Test only is how a
    # QtWidgets type creeping into the contract gets caught.
    miacode_add_dev_tool(export_engine_spec TEST
        SOURCES
            src/tools/v2/ExportEngineSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            ${_miacode_muri_analysis_core}
            src/timeline/TimelineSlowRefresh.h
            src/timeline/TimelineSlowRefresh.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/app/v2/ApplicationServices.h
            src/app/v2/ApplicationServices.cpp
            src/app/v2/AnalysisService.h
            src/app/v2/AnalysisService.cpp
            src/app/v2/ChartDropImportService.h
            src/app/v2/ChartDropImportService.cpp
            src/app/v2/ChartWorkspace.h
            src/app/v2/ChartWorkspace.cpp
            src/app/v2/ChartWorkspaceFileService.h
            src/app/v2/ChartWorkspaceFileService.cpp
            src/app/v2/EditorSyncController.h
            src/app/v2/EditorSyncController.cpp
            src/app/v2/ExportEngine.h
            src/app/v2/JobProgressService.h
            src/app/v2/JobProgressService.cpp
            src/app/v2/PreviewAppearanceState.h
            src/app/v2/PreviewAppearanceState.cpp
            src/app/v2/ShellNotifications.h
            src/app/v2/ShellNotifications.cpp
            src/app/v2/UiRequestService.h
            src/app/v2/UiRequestService.cpp
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Test
        INCLUDES src src/app/ui src/audio src/common src/core/chart src/core/chart/parser src/core/video src/timeline src/tools
    )
    target_compile_definitions(export_engine_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    # Stage 3.5 item 2: the preview appearance settings must have an owner that
    # is not a window. Core+Gui+Test only — a QtWidgets or Qt Quick include
    # reaching this state fails to link here.
    miacode_add_dev_tool(preview_appearance_state_spec TEST
        SOURCES
            src/tools/v2/PreviewAppearanceStateSpec.cpp
            src/app/v2/PreviewAppearanceState.h
            src/app/v2/PreviewAppearanceState.cpp
        LIBS Qt6::Core Qt6::Gui Qt6::Test
        INCLUDES src src/common src/core/video
    )

    # Stage 3.5 item 1: the application service assembly must stand up with no
    # window and no QApplication. Linking Qt6::Core + Qt6::Gui only is the
    # guarantee — a QtWidgets include reaching ApplicationServices fails to link
    # here rather than silently re-coupling the document domain to the shell.
    miacode_add_dev_tool(application_services_spec TEST
        SOURCES
            src/tools/v2/ApplicationServicesSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            ${_miacode_muri_analysis_core}
            src/timeline/TimelineSlowRefresh.h
            src/timeline/TimelineSlowRefresh.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/app/v2/ApplicationServices.h
            src/app/v2/ApplicationServices.cpp
            src/app/v2/AnalysisService.h
            src/app/v2/AnalysisService.cpp
            src/app/v2/ChartDropImportService.h
            src/app/v2/ChartDropImportService.cpp
            src/app/v2/ChartWorkspace.h
            src/app/v2/ChartWorkspace.cpp
            src/app/v2/ChartWorkspaceFileService.h
            src/app/v2/ChartWorkspaceFileService.cpp
            src/app/v2/EditorSyncController.h
            src/app/v2/EditorSyncController.cpp
            src/app/v2/JobProgressService.h
            src/app/v2/JobProgressService.cpp
            src/app/v2/PreviewAppearanceState.h
            src/app/v2/PreviewAppearanceState.cpp
            src/app/v2/ShellNotifications.h
            src/app/v2/ShellNotifications.cpp
            src/app/v2/UiRequestService.h
            src/app/v2/UiRequestService.cpp
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui Qt6::Test
        INCLUDES src src/app/ui src/common src/core/chart src/core/chart/parser src/core/video src/timeline src/tools
    )
    target_compile_definitions(application_services_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    # Stage 4.9d-4b-2a: the playback coordinator's first narrow port onto
    # preferences and persisted state. Session implements it, but the port
    # itself must not need Session, QWidget, or QML/QSG to be implemented —
    # linking Core+Test only (no Gui) is the guarantee: if the port ever grows
    # a method whose type reaches beyond Qt6::Core, this target fails to LINK.
    miacode_add_dev_tool(preferences_port_spec TEST
        SOURCES
            src/tools/v2/PreferencesPortSpec.cpp
            src/app/v2/PlaybackPreferencesPort.h
            src/audio/PreviewAudioSettings.h
            src/audio/PreviewAudioSettings.cpp
            src/common/PreviewSfxAssets.h
            src/common/PreviewSfxSemantics.h
        LIBS Qt6::Core Qt6::Test
        INCLUDES src src/common src/audio src/core/video
    )

    # Stage 4.9d-4b-2b: the playback coordinator's second narrow port, this one
    # onto muri validation/analysis presentation. All five methods already
    # belong to one host (ValidationHost), so the port is cut by host rather
    # than by capability — see PlaybackValidationPort.h. Linking Core+Test only
    # (no Gui) is the same link-time guarantee as preferences_port_spec above.
    miacode_add_dev_tool(validation_port_spec TEST
        SOURCES
            src/tools/v2/ValidationPortSpec.cpp
            src/app/v2/PlaybackValidationPort.h
            src/common/MuriRenderOptions.h
        LIBS Qt6::Core Qt6::Test
        INCLUDES src src/common
    )

    # Stage 4.9d-4b-2c: the playback coordinator's third narrow port, onto
    # document state (dirty tracking, committing the field QML is holding,
    # editor navigation, and a read-only query for the applied workspace
    # revision). All four methods already belong to one host
    # (DocumentSessionHost), so the port is cut by host, same as
    # validation_port_spec above. Linking Core+Test only (no Gui) is the same
    # link-time guarantee.
    miacode_add_dev_tool(document_port_spec TEST
        SOURCES
            src/tools/v2/DocumentPortSpec.cpp
            src/app/v2/PlaybackDocumentPort.h
        LIBS Qt6::Core Qt6::Test
        INCLUDES src
    )

    # Stage 4.9d-4b-2d: the playback coordinator's fourth narrow port, onto
    # the preview stage-media route (warmup, chart-path resync,
    # initialization-on-demand), the audio-runtime/outline-canvas re-applies,
    # and preview shutdown. Cut by capability, same as preferences_port_spec
    # above (eight of the nine methods' eventual owner is StageMediaHost, the
    # ninth is Session's own orchestration). Linking Core+Test only (no Gui)
    # is the same link-time guarantee as the other three port specs.
    miacode_add_dev_tool(preview_port_spec TEST
        SOURCES
            src/tools/v2/PreviewPortSpec.cpp
            src/app/v2/PlaybackPreviewPort.h
            src/core/video/PreviewRenderSettings.h
        LIBS Qt6::Core Qt6::Test
        INCLUDES src src/core/video
    )

    # Stage 4.6: Timeline commands receive their own identity before the
    # compatibility host forwards them to the current composite implementation.
    miacode_add_dev_tool(timeline_host_spec TEST
        SOURCES
            src/tools/v2/TimelineHostSpec.cpp
            src/app/runtime/timeline/TimelineCommandGate.cpp
            src/app/runtime/timeline/TimelineCommandGate.h
            src/app/runtime/timeline/TimelineHost.cpp
            src/app/runtime/timeline/TimelineHost.h
            src/app/v2/TimelineSurface.h
            src/app/v2/SessionGeneration.h
        LIBS Qt6::Core
        INCLUDES src
    )

    # Stage 4.7: PreviewHost keeps rendering/settings projection separate from
    # the playback authority and consumes transport through typed ports.
    miacode_add_dev_tool(preview_host_spec TEST
        SOURCES
            src/tools/v2/PreviewHostSpec.cpp
            src/app/runtime/preview/PreviewHost.cpp
            src/app/runtime/preview/PreviewHost.h
            src/app/v2/AudioClockSource.h
            src/app/v2/PreviewPlaybackPort.h
            src/app/v2/PlaybackControl.h
            src/app/v2/PreviewSurface.h
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/audio src/common src/core/video
    )

    # Stage 4.8: the coordinator owns playback contracts; legacy Preview and
    # Timeline surface compatibility lives in explicit projection adapters.
    miacode_add_dev_tool(playback_coordinator_spec TEST
        SOURCES
            src/tools/v2/PlaybackCoordinatorSpec.cpp
            src/app/runtime/playback/PlaybackIdentityGate.cpp
            src/app/runtime/playback/PlaybackIdentityGate.h
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(playback_coordinator_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    # Stage 4.9f pre-work probe (result: does NOT link). SOURCES below is the
    # snapshot reached while chasing undefined symbols two rounds deep: round 1
    # (TimelineFlow.cpp + PlaybackIdentityGate, the TU that defines the
    # constructor) needed 30 more files just to satisfy its own undefined
    # symbols; round 2, adding all 30, still leaves 171 undefined symbols
    # (PreviewStageMediaHost's whole video-decode surface, MuriAnalyzer,
    # QuickShellPreviewCompositeSurface, DocumentSessionHost's vtable, a
    # miniaudio implementation TU, ...) and was still growing. A symbol-closure
    # projection over the full object graph (nm on the already-built MiaCode
    # objects) shows the same trajectory does not converge before pulling in
    # SessionBootstrap.cpp: 2 -> 32 -> 75 -> 166 -> 246 files across five
    # rounds, still 37 files short of closure when the probe was stopped.
    #
    # Conclusion: PlaybackCoordinator's implementation TUs are not
    # link-independent of the Session assembly today, because those TUs still
    # define Session::-owned methods (TimelineFlow.cpp alone has 41 of them)
    # that a direct (non-archive) link must resolve even though the coordinator
    # never calls them. Left EXCLUDE_FROM_ALL and out of CTest: it does not
    # link, so it must not be part of the default build or the full suite.
    # SOURCES/LIBS/INCLUDES are kept as the recorded evidence of how far this
    # got; do not extend them to force a link — see the Result Packet for the
    # full undefined-symbol trace.
    miacode_add_dev_tool(playback_coordinator_construction_spec
        SOURCES
            src/tools/v2/PlaybackCoordinatorConstructionSpec.cpp
            src/app/runtime/playback/PlaybackIdentityGate.cpp
            src/app/runtime/playback/PlaybackIdentityGate.h
            src/app/runtime/playback/TimelineFlow.cpp
            # Round 2: TimelineFlow.cpp.o's undefined symbols (linker probe run),
            # added as a batch rather than one at a time to keep the probe's
            # iteration count down. See the Result Packet for the per-round counts.
            src/app/process_identity.cpp
            src/app/runtime/Shared.Preview.cpp
            src/app/runtime/Shared.cpp
            src/app/runtime/document/DocumentFlow.cpp
            src/app/runtime/playback/AnalysisFlow.cpp
            src/app/runtime/playback/FollowSync.cpp
            src/app/runtime/playback/Fullscreen.cpp
            src/app/runtime/playback/LayoutUi.cpp
            src/app/runtime/playback/Playback.cpp
            src/app/runtime/playback/PlaybackState.cpp
            src/app/runtime/playback/QuickParse.cpp
            src/app/runtime/playback/Seek.cpp
            src/app/runtime/playback/SurfaceContract.cpp
            src/app/runtime/playback/Tick.cpp
            src/app/v2/ChartWorkspace.cpp
            src/app/v2/EditorSyncController.cpp
            src/audio/QtPreviewSfxRuntime.cpp
            src/common/CrashRecovery.cpp
            src/common/DebugLog.cpp
            src/common/OperationLog.cpp
            src/common/ProcessDiagnostics.cpp
            src/common/WaveformCache.cpp
            src/core/chart/document/SimaiDocument.cpp
            src/core/chart/document/SimaiTimingMetadata.cpp
            src/core/chart/parser/SimaiNativeParser.cpp
            src/preview/runtime/PreviewRuntime.cpp
            src/timeline/TimelineQuickModel.cpp
            src/timeline/TimelineSlowRefresh.cpp
            src/timeline/quick/TimelineQuickStateBridge.cpp
            src/tools/muri/MuriStaticChecker.cpp
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::Svg Qt6::Quick
        INCLUDES
            ${CMAKE_CURRENT_BINARY_DIR}/generated
            third_party/bass/include
            src
            src/app
            src/app/qml_ui
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
    )
    target_compile_definitions(playback_coordinator_construction_spec PRIVATE HAVE_QT_MULTIMEDIA=1)
    # This target does not link (see the comment above) — excluded so a
    # default `cmake --build build-macos` and the full `ctest` run are not
    # broken by a probe that was expected to possibly fail.
    set_target_properties(playback_coordinator_construction_spec PROPERTIES EXCLUDE_FROM_ALL TRUE)

    # Stage 4.9b: the independent translation unit that parses RuntimeContext.h.
    # Compile-only — the static assertions inside fail the build if the timeline
    # storage split regresses.
    miacode_add_dev_tool(runtime_context_boundary_spec TEST
        SOURCES
            src/tools/v2/RuntimeContextBoundarySpec.cpp
            src/app/runtime/RuntimeContext.h
            src/app/runtime/SessionMembers.inc
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES
            src
            src/app
            src/audio
            src/common
            src/core/video
            src/core/chart/document
            src/core/chart/parser
            src/timeline
            src/tools/video_export
    )

    miacode_add_dev_tool(qml_analysis_model_spec TEST
        SOURCES
            src/tools/qml_ui/QmlAnalysisModelSpec.cpp
            src/app/v2/AnalysisService.h
            src/app/qml_ui/QmlAnalysisProjection.cpp
            src/app/qml_ui/QmlAnalysisProjection.h
            src/app/qml_ui/QmlDocumentProjection.h
        LIBS Qt6::Core
        INCLUDES src
    )

    miacode_add_dev_tool(qml_shortcut_binding_spec TEST
        SOURCES
            src/tools/qml_ui/QmlShortcutBindingSpec.cpp
            src/app/qml_ui/QmlShortcutModel.cpp
            src/app/qml_ui/QmlShortcutModel.h
            src/app/qml_ui/QmlShortcutCommands.h
            src/app/qml_ui/ChartTransformCommands.h
            src/core/chart/transform/ChartBatchTransform.h
            src/core/chart/transform/ChartBatchTransform.Parsers.cpp
            src/core/chart/transform/ChartBatchTransform.Subdivision.cpp
            src/core/chart/transform/ChartBatchTransform.Selection.cpp
            src/core/chart/transform/ChartBatchTransform.Transform.cpp
            src/app/ui/ShortcutRegistry.cpp
            src/app/ui/ShortcutRegistry.h
            src/common/InputShortcutGesture.h
            src/common/InputShortcutGesture.cpp
            ${_miacode_log_core}
            resources/app_icons.qrc
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/app/ui src/app/qml_ui src/core/chart src/core/chart/transform
    )
    miacode_add_dev_tool(v1_shell_removal_spec TEST
        SOURCES
            src/tools/qml_ui/V1ShellRemovalSpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(v1_shell_removal_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(preview_transport_push_spec TEST
        SOURCES
            src/tools/qml_ui/PreviewTransportPushSpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(preview_transport_push_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    # The timeline half of the same push: the QSG surface has to tell the window
    # it is writable before playback may move the playhead. Links the real
    # QmlTimelineModel so the readiness report is exercised through the contract
    # rather than described.
    miacode_add_dev_tool(timeline_surface_ready_spec TEST
        SOURCES
            src/tools/qml_ui/TimelineSurfaceReadySpec.cpp
            src/app/qml_ui/QmlTimelineModel.cpp
            src/app/qml_ui/QmlTimelineModel.h
            src/app/v2/ShellNotifications.cpp
            src/app/v2/ShellNotifications.h
            src/app/v2/TimelineSurface.h
            src/app/ui/UiText.cpp
            src/app/ui/UiText.h
        LIBS Qt6::Core
        INCLUDES src src/app src/app/ui
    )
    target_compile_definitions(timeline_surface_ready_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(qml_ui_bootstrap_lifecycle_spec TEST
        SOURCES
            src/tools/qml_ui/QmlUiBootstrapLifecycleSpec.cpp
            src/app/qml_ui/QmlUiRootLifecycle.h
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(qml_ui_bootstrap_lifecycle_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(qml_document_lifecycle_contract_spec TEST
        SOURCES
            src/tools/qml_ui/QmlDocumentLifecycleContractSpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(qml_document_lifecycle_contract_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(qml_export_intro_sound_contract_spec TEST
        SOURCES
            src/tools/qml_ui/QmlExportIntroSoundContractSpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(qml_export_intro_sound_contract_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(qml_export_font_contract_spec TEST
        SOURCES
            src/tools/qml_ui/QmlExportFontContractSpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(qml_export_font_contract_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(qml_cover_export_contract_spec TEST
        SOURCES
            src/tools/qml_ui/QmlCoverExportContractSpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(qml_cover_export_contract_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(qml_export_video_page_spec TEST
        SOURCES
            src/tools/qml_ui/QmlExportVideoPageSpec.cpp
        LIBS Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test
        INCLUDES src
    )
    target_compile_definitions(qml_export_video_page_spec PRIVATE
        "MIACODE_QML_SPEC_IMPORT_ROOT=\"${MIACODE_QML_SPEC_IMPORT_ROOT}\"")

    miacode_add_dev_tool(qml_main_menu_spec TEST
        SOURCES
            src/tools/qml_ui/QmlMainMenuSpec.cpp
        LIBS Qt6::Core
        INCLUDES src
    )
    target_compile_definitions(qml_main_menu_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(qml_document_replacement_sequence_spec TEST
        SOURCES
            src/tools/qml_ui/QmlDocumentReplacementSequenceSpec.cpp
        LIBS Qt6::Core Qt6::Qml Qt6::Quick
        INCLUDES src
    )
    target_compile_definitions(qml_document_replacement_sequence_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(adopted_surface_geometry_spec TEST
        SOURCES
            src/tools/ui/AdoptedSurfaceGeometrySpec.cpp
            src/common/AdoptedWidgetCoordinates.h
            src/common/AdoptedWidgetCoordinates.cpp
            src/common/AdoptedSurfaceDragAutoScroll.h
            src/common/AdoptedSurfaceDragAutoScroll.cpp
        LIBS Qt6::Core Qt6::Gui Qt6::Widgets
        INCLUDES src src/common
    )

    miacode_add_dev_tool(touch_pad_authoring_edit_spec TEST
        SOURCES
            src/tools/editor/TouchPadAuthoringEditSpec.cpp
            src/editor/TouchPadAuthoringEdit.h
            src/editor/TouchPadAuthoringEdit.cpp
            src/core/chart/parser/SimaiCommentScan.h
            src/core/chart/parser/SimaiCommentScan.cpp
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/editor
    )

    miacode_add_dev_tool(simai_completion_catalog_spec TEST
        SOURCES
            src/tools/editor/SimaiCompletionCatalogSpec.cpp
            src/editor/SimaiCompletionCatalog.h
            src/editor/SimaiCompletionCatalog.cpp
        LIBS Qt6::Core
        INCLUDES src src/editor
    )

    miacode_add_dev_tool(simai_text_edit_policy_spec TEST
        SOURCES
            src/tools/editor/SimaiTextEditPolicySpec.cpp
            src/editor/SimaiTextEditPolicy.h
            src/editor/SimaiTextEditPolicy.cpp
            src/editor/SimaiCompletionCatalog.h
            src/editor/SimaiCompletionCatalog.cpp
        LIBS Qt6::Core
        INCLUDES src src/editor
    )

    miacode_add_dev_tool(qml_editor_controller_spec TEST
        SOURCES
            src/tools/qml_ui/QmlEditorControllerSpec.cpp
            ${_miacode_chart_core}
            src/app/v2/ChartWorkspace.cpp
            src/app/v2/ChartWorkspace.h
            src/app/qml_ui/QmlEditorController.h
            src/app/qml_ui/QmlEditorController.cpp
            src/app/qml_ui/SimaiSyntaxHighlighter.h
            src/app/qml_ui/SimaiSyntaxHighlighter.cpp
            src/app/qml_ui/EditorTextStyle.h
            src/app/qml_ui/EditorTextStyle.cpp
            src/app/qml_ui/QmlEditorInputBridge.cpp
            src/app/qml_ui/QmlEditorInputBridge.h
            ${_miacode_log_core}
            src/editor/TouchPadAuthoringEdit.h
            src/editor/TouchPadAuthoringEdit.cpp
            src/core/chart/parser/SimaiCommentScan.h
            src/core/chart/parser/SimaiCommentScan.cpp
            src/editor/SimaiTextEditPolicy.h
            src/editor/SimaiTextEditPolicy.cpp
            src/editor/SimaiCompletionCatalog.h
            src/editor/SimaiCompletionCatalog.cpp
            src/editor/BookmarkCommentSyntax.h
            src/editor/BookmarkCommentSyntax.cpp
        LIBS Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test
        INCLUDES src src/editor src/app/qml_ui
    )
    target_compile_definitions(qml_editor_controller_spec PRIVATE
        "MIACODE_QML_SPEC_IMPORT_ROOT=\"${MIACODE_QML_SPEC_IMPORT_ROOT}\"")

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

    miacode_add_dev_tool(preview_end_of_media_policy_spec TEST
        SOURCES
            src/tools/preview/PreviewEndOfMediaPolicySpec.cpp
            src/core/video/PreviewEndOfMediaPolicy.h
        LIBS Qt6::Core
        INCLUDES src src/common src/core/video
    )

    miacode_add_dev_tool(preview_firework_warmup_policy_spec TEST
        SOURCES
            src/tools/preview/PreviewFireworkWarmupPolicySpec.cpp
            src/core/scene/PreviewFireworkWarmupPolicy.h
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

    miacode_add_dev_tool(preview_guide_layer_spec TEST
        SOURCES
            src/tools/preview/PreviewGuideLayerSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            src/core/scene/PreviewFrameState.h
            src/core/scene/PreviewActiveMarkerView.h
            src/core/scene/PreviewPreparedSceneCache.h
            src/core/scene/PreviewPreparedSceneCache.cpp
            src/core/scene/PreviewChartReviewLayerState.h
            src/core/scene/PreviewChartReviewLayerState.cpp
            src/core/scene/PreviewGuideLayerState.h
            src/core/scene/PreviewGuideLayerState.cpp
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

    miacode_add_dev_tool(preview_slide_vanilla_trim_spec TEST
        SOURCES
            src/tools/preview/PreviewSlideVanillaTrimSpec.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            src/core/scene/PreviewTrackShared.h
            src/core/scene/PreviewTrackShared.cpp
            src/core/scene/PreviewSceneConstants.h
            src/common/MuriTypes.h
            src/common/MuriTypes.cpp
            src/timeline/TimelineData.h
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui
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

    miacode_add_dev_tool(preview_audio_command_queue_spec TEST
        SOURCES
            src/tools/preview/PreviewAudioCommandQueueSpec.cpp
            src/audio/PreviewAudioCommandQueue.h
            src/audio/PreviewAudioCommandQueue.cpp
            src/audio/PreviewAudioWorkerProtocol.h
        LIBS Qt6::Core
        INCLUDES src src/common src/audio src/timeline
    )

    miacode_add_dev_tool(preview_audio_worker_protocol_spec TEST
        SOURCES
            src/tools/preview/PreviewAudioWorkerProtocolSpec.cpp
            src/audio/PreviewAudioWorkerProtocol.h
        LIBS Qt6::Core
        INCLUDES src src/common src/audio src/timeline
    )

    miacode_add_dev_tool(preview_audio_playback_flow_policy_spec TEST
        SOURCES
            src/tools/preview/PreviewAudioPlaybackFlowPolicySpec.cpp
            src/audio/PreviewAudioPlaybackFlowPolicy.h
        LIBS Qt6::Core
        INCLUDES src src/common src/audio src/timeline
    )

    miacode_add_dev_tool(timeline_cadence_arbitration_policy_spec TEST
        SOURCES
            src/tools/timeline/TimelineCadenceArbitrationPolicySpec.cpp
            src/timeline/TimelineCadenceArbitrationPolicy.h
        LIBS Qt6::Core
        INCLUDES src src/common src/timeline
    )

    # Keep this copy of the production factory on the no-BASS route even when
    # the worker spec also compiles the platform BASS backend sources.
    add_library(preview_audio_worker_spec_miniaudio_factory OBJECT
        src/audio/PreviewAudioWorkerFactory.cpp
    )
    target_link_libraries(preview_audio_worker_spec_miniaudio_factory PRIVATE Qt6::Core)
    target_include_directories(preview_audio_worker_spec_miniaudio_factory PRIVATE src src/audio)

    miacode_add_dev_tool(preview_audio_worker_spec TEST
        SOURCES
            src/tools/preview/PreviewAudioWorkerSpec.cpp
            ${_miacode_log_core}
            src/common/Mmcss.h
            src/common/Mmcss.cpp
            src/audio/PreviewAudioSettings.h
            src/audio/PreviewAudioSettings.cpp
            src/audio/MiniaudioPreviewAudioBackend.h
            src/audio/MiniaudioPreviewAudioBackend.cpp
            src/audio/PreviewAudioCommandQueue.h
            src/audio/PreviewAudioCommandQueue.cpp
            src/audio/PreviewAudioWorkerProtocol.h
            src/audio/PreviewAudioWorkerFactory.h
            src/audio/PreviewAudioWorker.h
            src/audio/PreviewAudioWorker.cpp
            src/audio/QtPreviewSfxRuntime.h
            src/audio/QtPreviewSfxRuntime.cpp
            src/audio/PreviewBassEmergencyPause.h
            src/audio/PreviewBassEmergencyPause.cpp
            src/audio/PreviewBassDeviceLease.h
            src/audio/PreviewBassDeviceLease.cpp
        LIBS Qt6::Core soundtouch
        INCLUDES src src/audio src/common src/preview src/timeline
    )
    target_sources(preview_audio_worker_spec PRIVATE
        $<TARGET_OBJECTS:preview_audio_worker_spec_miniaudio_factory>
    )
    target_compile_definitions(preview_audio_worker_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
    if (WIN32 OR APPLE)
        target_sources(preview_audio_worker_spec PRIVATE
            src/audio/BassPreviewAudioBackend.h
            src/audio/BassPreviewAudioBackend.cpp
            src/audio/BassPreviewAudioBackend_EngineInit.cpp
            src/audio/BassPreviewAudioBackend_Assets.cpp
            src/audio/BassPreviewAudioBackend_Transport.cpp
            src/audio/BassPreviewAudioBackend_PlaybackClock.cpp
            src/audio/BassPreviewAudioBackend_EventDrain.cpp
        )
        target_compile_definitions(preview_audio_worker_spec PRIVATE MIACODE_HAS_BASS_AUDIO=1)
        target_include_directories(preview_audio_worker_spec PRIVATE third_party/bass/include)
        if (WIN32)
            target_link_libraries(preview_audio_worker_spec PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bass.lib"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bassmix.lib"
                avrt
            )
            add_custom_command(TARGET preview_audio_worker_spec POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bass.dll"
                    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/win64/bassmix.dll"
                    $<TARGET_FILE_DIR:preview_audio_worker_spec>
            )
        elseif (APPLE)
            target_link_libraries(preview_audio_worker_spec PRIVATE
                "${MIACODE_BASS_MACOS_DIR}/libbass.dylib"
                "${MIACODE_BASS_MACOS_DIR}/libbassmix.dylib"
            )
        endif()
    endif()

    miacode_add_dev_tool(preview_audio_non_gui_barrier_spec TEST
        SOURCES
            src/tools/preview/PreviewAudioNonGuiBarrierSpec.cpp
            ${_miacode_log_core}
            src/common/Mmcss.h
            src/common/Mmcss.cpp
            src/audio/PreviewAudioSettings.h
            src/audio/PreviewAudioSettings.cpp
            src/audio/PreviewAudioCommandQueue.h
            src/audio/PreviewAudioCommandQueue.cpp
            src/audio/PreviewAudioWorkerProtocol.h
            src/audio/PreviewAudioWorkerFactory.h
            src/audio/PreviewAudioWorker.h
            src/audio/PreviewAudioWorker.cpp
        LIBS Qt6::Core
        INCLUDES src src/audio src/common src/timeline
    )
    if (WIN32)
        target_link_libraries(preview_audio_non_gui_barrier_spec PRIVATE avrt)
    endif()

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

    miacode_add_dev_tool(preview_bass_device_lease_spec TEST
        SOURCES
            src/tools/preview/PreviewBassDeviceLeaseSpec.cpp
            src/audio/PreviewBassDeviceLease.h
            src/audio/PreviewBassDeviceLease.cpp
        LIBS Qt6::Core
        INCLUDES src src/audio
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

    miacode_add_dev_tool(pv_memory_diagnostics_spec TEST
        SOURCES
            src/tools/preview/PvMemoryDiagnosticsSpec.cpp
            src/preview/runtime/PvMemoryDiagnostics.h
            src/preview/runtime/PvMemoryDiagnostics.cpp
        LIBS Qt6::Core
        INCLUDES src src/preview src/preview/runtime
    )
    target_compile_definitions(pv_memory_diagnostics_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(pv_memory_host_contract_spec TEST
        SOURCES
            src/tools/preview/PvMemoryHostContractSpec.cpp
        LIBS Qt6::Core
        INCLUDES src src/preview src/preview/runtime
    )
    target_compile_definitions(pv_memory_host_contract_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(qtavplayer_platform_spec TEST
        SOURCES
            src/tools/preview/QtAVPlayerPlatformSpec.cpp
        LIBS Qt6::Core
        INCLUDES src src/preview src/preview/runtime
    )
    target_compile_definitions(qtavplayer_platform_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(quickshell_preview_surface_policy_spec TEST
        SOURCES
            src/tools/preview/QuickShellPreviewSurfacePolicySpec.cpp
            src/app/quick_shell/QuickShellPreviewSurfacePolicy.h
        LIBS Qt6::Core
        INCLUDES src src/app/quick_shell
    )
    target_compile_definitions(quickshell_preview_surface_policy_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    miacode_add_dev_tool(video_export_runtime_policy_spec TEST
        SOURCES
            src/tools/video_export/VideoExportRuntimePolicySpec.cpp
            src/tools/video_export/VideoExportRuntimePolicy.h
            src/tools/video_export/VideoExportRuntimePolicy.cpp
        LIBS Qt6::Core
        INCLUDES src src/tools
    )

    miacode_add_dev_tool(video_export_intro_mode_spec TEST
        SOURCES
            src/tools/video_export/VideoExportIntroModeSpec.cpp
            src/tools/video_export/VideoExportController.h
            src/timeline/TimelineData.h
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/common src/audio src/core/video src/timeline src/tools src/tools/video_export
    )

    miacode_add_dev_tool(video_export_intro_sound_spec TEST
        SOURCES
            src/tools/video_export/VideoExportIntroSoundSpec.cpp
            src/tools/video_export/VideoExportSettings.cpp
            src/tools/video_export/VideoExportRuntimePolicy.cpp
            src/tools/video_export/VideoExportSnapshot.cpp
            src/timeline/TimelineMarkerOffset.h
            src/audio/PreviewAudioSettings.h
            src/audio/PreviewAudioSettings.cpp
            ${_miacode_chart_core}
            ${_miacode_log_core}
            ${_miacode_muri_analysis_core}
            resources/fonts.qrc
            resources/slide_data.qrc
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/app/ui src/common src/audio src/core/chart src/core/chart/document src/core/chart/parser src/core/video src/timeline src/tools src/tools/video_export
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

    miacode_add_dev_tool(video_export_media_timeline_spec TEST
        SOURCES
            src/tools/video_export/VideoExportMediaTimelineSpec.cpp
            src/tools/video_export/VideoExportMediaTimeline.h
            src/tools/video_export/VideoExportMediaTimeline.cpp
            src/common/PreviewSfxTimeline.h
        LIBS Qt6::Core Qt6::Gui
        INCLUDES src src/common src/timeline src/tools src/tools/video_export
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

    # The Net engine's only build home. The Net page was removed from the v2
    # product runtime, so src/tools/net/ is compiled here and nowhere else —
    # that is what keeps Qt6::Network out of MiaCode (docs/ops/DEPENDENCY_ALLOWLIST.md).
    # The two batch workers carry no assertions yet; they are listed so the
    # engine keeps compiling instead of rotting while it waits for the page.
    miacode_add_dev_tool(net_client_spec TEST
        SOURCES
            src/tools/net/NetClientSpec.cpp
            src/tools/net/NetClient.h
            src/tools/net/NetClient.cpp
            src/tools/net/NetBatchUploadScanner.h
            src/tools/net/NetBatchUploadScanner.cpp
            src/tools/net/NetUploadDiagnostics.h
            src/tools/net/NetUploadDiagnostics.cpp
            src/tools/net/NetBatchDownloadWorker.h
            src/tools/net/NetBatchDownloadWorker.cpp
            src/tools/net/NetBatchUploadWorker.h
            src/tools/net/NetBatchUploadWorker.cpp
            src/tools/media/PvBatchCompressionScanner.h
            src/tools/media/PvBatchCompressionScanner.cpp
            src/tools/zip_export/ChartZipPackager.h
            src/tools/zip_export/ChartZipPackager.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
            src/common/ChartAssetPaths.h
        LIBS Qt6::Core Qt6::Network miniz
        INCLUDES src src/common src/tools src/app/ui
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

    # Ratchet for docs/specs/ui/QML_UI_V2_BACKEND_SURFACE_ZH.md (stage 3.5, item 2).
    # Set-equality between the MainWindow surface src/app/qml_ui actually reaches
    # and the inventory the doc lists, plus the friend grants and the recorded
    # counts. New coupling fails; a migration that forgets the doc fails too,
    # which is what keeps the remaining count honest and monotonic.
    miacode_add_dev_tool(qml_ui_backend_surface_spec TEST
        SOURCES
            src/tools/qml_ui/QmlUiBackendSurfaceSpec.cpp
        LIBS Qt6::Core
    )
    target_compile_definitions(qml_ui_backend_surface_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    # A theme change has two entry points — the OS colour scheme and the user's
    # own choice — and only the first used to notify QML, so picking a theme
    # repainted the timeline and nothing else. Source contract because
    # QmlUiSettings pulls in MainWindowShared and cannot link Core-only.
    miacode_add_dev_tool(qml_ui_theme_contract_spec TEST
        SOURCES
            src/tools/qml_ui/QmlUiThemeContractSpec.cpp
        LIBS Qt6::Core
    )
    target_compile_definitions(qml_ui_theme_contract_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    # Drift guard for docs/ops/DEPENDENCY_ALLOWLIST.md (stage 3.5, item 4).
    # Parses every target_link_libraries(MiaCode …) call plus the doc's
    # allow/forbid/media-adapter tables, so an undocumented dependency, a stale
    # doc row, a forbidden link (Qt6::Network today, Qt6::Widgets after stage 4),
    # an unpinned Qt version, or QtAVPlayer headers leaking outside the media
    # adapter layer all fail the build's test suite.
    miacode_add_dev_tool(dependency_allowlist_spec TEST
        SOURCES
            src/tools/deps/DependencyAllowlistSpec.cpp
        LIBS Qt6::Core
    )
    target_compile_definitions(dependency_allowlist_spec PRIVATE
        "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

    # Drift guard for the UiText localization tables: built-in en/zh/ja key parity plus
    # source/resource literal key checks. Reads the tree from disk; repo root
    # injected as a compile definition.
    miacode_add_dev_tool(ui_text_locale_spec TEST
        SOURCES
            src/tools/ui_text/UiTextLocaleSpec.cpp
            src/app/ui/UiText.h
            src/app/ui/UiText.cpp
        LIBS Qt6::Core
        INCLUDES src src/app/ui
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
        LIBS Qt6::Core
        INCLUDES src src/app/ui
    )

    # Behavior spec locking DebugOptions.h accessor semantics (defaults, clamps,
    # debug-category gating), the safety net for any future table-driven
    # registry refactor.
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

    miacode_add_dev_tool(log_pruning_policy_spec TEST
        SOURCES
            src/tools/debug_index/LogPruningPolicySpec.cpp
            src/common/LogEmissionPolicy.h
            src/common/UiHangWatchdogPolicy.h
            src/audio/BassPreviewSfxSchedulerPolicy.h
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
