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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui LinguistTools Multimedia Quick Qml QuickControls2 ShaderTools)

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
# QAVVideoFrame -> QVideoFrame bridge needs. The Windows, macOS, and Linux
# QtAVPlayer preview backends use it (see the QtAVPlayer block below).
if (WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 6.8 REQUIRED COMPONENTS MultimediaQuickPrivate)
endif()

add_executable(MiaCode WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/services/ApplicationServices.cpp
    src/app/services/ApplicationServices.h
    src/app/services/DocumentBridge.h
    src/app/services/EditorPageRouter.h
    src/app/services/ExportEngine.h
    src/app/services/LatencyEngine.h
    src/app/services/MediaToolsEngine.h
    src/app/services/PreferencesStore.h
    src/app/services/PreviewSurface.h
    src/app/services/PlaybackControl.h
    src/app/services/PlaybackStateAuthority.h
    src/app/services/SessionGeneration.h
    src/app/services/AudioClockSource.h
    src/app/services/PreviewPlaybackPort.h
    src/app/services/ShellNotifications.cpp
    src/app/services/ShellNotifications.h
    src/app/services/TimelineSurface.h
    src/app/services/PreviewAppearanceState.cpp
    src/app/services/PreviewAppearanceState.h
    src/app/services/ChartWorkspace.cpp
    src/app/services/ChartWorkspace.h
    src/app/services/ChartMediaService.cpp
    src/app/services/ChartMediaService.h
    src/common/ChartMediaImport.cpp
    src/common/ChartMediaImport.h
    src/app/services/ChartWorkspaceFileService.cpp
    src/app/services/ChartWorkspaceFileService.h
    src/app/services/AnalysisService.cpp
    src/app/services/AnalysisService.h
    src/app/services/EditorSyncController.cpp
    src/app/services/EditorSyncController.h
    src/app/services/UiRequestService.cpp
    src/app/services/UiRequestService.h
    src/app/services/JobProgressService.cpp
    src/app/services/JobProgressService.h
    src/app/ui/media/MediaToolsModel.cpp
    src/app/ui/media/MediaToolsModel.h
    src/app/ui/preview/AudioSettingsModel.cpp
    src/app/ui/preview/AudioSettingsModel.h
    src/app/ui/preview/PreviewSettingsModel.cpp
    src/app/ui/preview/PreviewSettingsModel.h
    src/app/ui/preferences/PreferencesModel.cpp
    src/app/ui/preferences/PreferencesModel.h
    src/app/ui/preferences/AppBackgroundModel.cpp
    src/app/ui/preferences/AppBackgroundModel.h
    src/app/ui/latency/LatencyModel.cpp
    src/app/ui/latency/LatencyModel.h
    src/app/startup_diagnostics_win32.cpp
    src/app/graphics_backend.cpp
    src/app/process_identity.cpp
    src/app/WindowVisibilityDiagnostics.h
    src/app/WindowVisibilityDiagnostics.cpp
    src/app/gpu_adapter_probe.cpp
    src/app/gpu_device_provider.cpp
    src/preview/quick_scene/PreviewQuickGraphicsInfo.h
    src/preview/quick_scene/PreviewQuickGraphicsInfo.cpp
    src/app/gpu_high_performance_hint.cpp
    src/app/cli_shared.cpp
    src/app/cli_video_export.cpp
    src/app/cli_video_export_worker.cpp
    src/app/quick_shell/QuickShellPreviewCompositeSurface.h
    src/app/quick_shell/QuickShellPreviewCompositeSurface.cpp
    src/app/quick_shell/QuickShellPreviewSurfacePolicy.h
    src/app/runtime/Session.h
    src/app/runtime/SessionLifecycle.cpp
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
    src/app/runtime/shell/ShellHost.cpp
    src/app/ui/preferences/PreferenceDocument.h
    src/app/ui/preferences/PreferenceDocument.cpp
    src/app/ui/preferences/LocaleService.h
    src/app/ui/preferences/LocaleService.cpp
    src/app/ui/preferences/AppBackgroundSettings.h
    src/app/ui/preferences/AppBackgroundSettings.cpp
    src/app/ui/theme/ThemeVariantResolver.h
    src/app/ui/theme/ThemeVariantResolver.cpp
    src/app/ui/chrome/ShortcutRegistry.h
    src/app/ui/chrome/ShortcutRegistry.cpp
    src/app/ui/theme/UiTheme.h
    src/app/ui/theme/UiTheme.cpp
    src/app/ui/chrome/NativeWindowTheme.h
    src/app/ui/chrome/NativeWindowTheme.cpp
    src/app/ui/chrome/NativeWindowThemePolicy.h
    src/app/ui/chrome/WindowParityMetrics.h
    src/app/ui/chrome/WindowParityMetrics.cpp
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
    src/tools/media/PvCompressionPolicy.h
    src/tools/media/PvCompressionPolicy.cpp
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
    src/core/chart/selection/ChartSelectionBeatSummary.h
    src/core/chart/selection/ChartSelectionBeatSummary.cpp
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
    src/app/ui/drop/ChartDropBridge.h
    src/app/ui/drop/ChartDropBridge.cpp
    src/app/services/ChartDropImportService.h
    src/app/services/ChartDropImportService.cpp
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
qt_add_shaders(MiaCode "ui_shaders"
    PREFIX "/"
    FILES
        src/app/ui/shaders/corner_mask.frag
)
set_target_properties(MiaCode PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "MiaCode"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.fanfaredash.MiaCode"
)

if(UNIX AND NOT APPLE)
    set_target_properties(MiaCode PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
    )
endif()

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
    src/app/ui/Main.qml
    src/app/ui/ViewState.qml
    src/app/ui/theme/Theme.qml
    src/app/ui/components/IconButton.qml
    src/app/ui/components/AppCheckBox.qml
    src/app/ui/components/AppDropDownButton.qml
    src/app/ui/components/AppTab.qml
    src/app/ui/components/DifficultySwatch.qml
    src/app/ui/components/PanelHeader.qml
    src/app/ui/components/SplitHandle.qml
    src/app/ui/components/CornerMask.qml
    src/app/ui/components/Tooltip.qml
    src/app/ui/components/HoverChrome.qml
    src/app/ui/components/ChromeRow.qml
    src/app/ui/components/NavRow.qml
    src/app/ui/components/AppTextField.qml
    src/app/ui/components/AppTextArea.qml
    src/app/ui/components/AppButton.qml
    src/app/ui/components/AppSwitch.qml
    src/app/ui/components/AppComboBox.qml
    src/app/ui/components/AppSlider.qml
    src/app/ui/components/AppScrollBar.qml
    src/app/ui/components/AppMenu.qml
    src/app/ui/components/AppMenuAction.qml
    src/app/ui/components/FloatingCard.qml
    src/app/ui/components/BackdropBlur.qml
    src/app/ui/components/FadeTransition.qml
    src/app/ui/components/AppDialog.qml
    src/app/ui/components/AppStickyPopup.qml
    src/app/ui/components/AppDropdownPanel.qml
    src/app/ui/components/AppMenuItem.qml
    src/app/ui/components/AppMenuSeparator.qml
    src/app/ui/components/UiRequestHost.qml
    src/app/ui/components/DialogFooter.qml
    src/app/ui/components/ChoiceDialog.qml
    src/app/ui/components/EditableValue.qml
    src/app/ui/components/LabeledCombo.qml
    src/app/ui/components/LabeledSlider.qml
    src/app/ui/components/SettingsSection.qml
    src/app/ui/components/AboutDialog.qml
    src/app/ui/components/JobProgressOverlay.qml
    src/app/ui/chrome/WindowGestureArea.qml
    src/app/ui/chrome/WindowTitleBar.qml
    src/app/ui/chrome/WindowCaptionButtons.qml
    src/app/ui/chrome/WindowResizeBorder.qml
    src/app/ui/chrome/MainMenu.qml
    src/app/ui/chrome/ShortcutBindings.qml
    src/app/ui/chrome/MainMenuCommands.qml
    src/app/ui/chrome/MainToolBar.qml
    src/app/ui/chrome/StatusBar.qml
    src/app/ui/layout/MainView.qml
    src/app/ui/layout/MainSplitView.qml
    src/app/ui/layout/CompactPanelLayer.qml
    src/app/ui/sidebar/Sidebar.qml
    src/app/ui/sidebar/ActivityBar.qml
    src/app/ui/sidebar/ChartFieldSidebar.qml
    src/app/ui/sidebar/DifficultyList.qml
    src/app/ui/sidebar/ExportSidebarPage.qml
    src/app/ui/export/ExportRangeSelector.qml
    src/app/ui/export/ExportVideoPage.qml
    src/app/ui/export/CoverExportPage.qml
    src/app/ui/export/CoverExportWindow.qml
    src/app/ui/editor/EditorPane.qml
    src/app/ui/editor/EditorTabBar.qml
    src/app/ui/editor/SourceEditor.qml
    src/app/ui/editor/EditorPointerArea.qml
    src/app/ui/editor/CompletionPopup.qml
    src/app/ui/editor/NormalizeOptionsDialog.qml
    src/app/ui/editor/DesignerSlotsDialog.qml
    src/app/ui/media/MediaToolsDialog.qml
    src/app/ui/media/PvBatchCompressionDialog.qml
    src/app/ui/media/PrependBlankDialog.qml
    src/app/ui/preferences/PreferencesDialog.qml
    src/app/ui/latency/LatencyPage.qml
    src/app/ui/editor/FindReplaceBar.qml
    src/app/ui/editor/BookmarkList.qml
    src/app/ui/editor/LineNumberGutter.qml
    src/app/ui/timeline/BottomPanel.qml
    src/app/ui/timeline/BottomTabBar.qml
    src/app/ui/timeline/TimelineZoomMenu.qml
    src/app/ui/timeline/TimelineBrightnessMenu.qml
    src/app/ui/preview/PreviewPane.qml
    src/app/ui/preview/PreviewRenderModeMenu.qml
    src/app/ui/preview/PreviewRateMenu.qml
    src/app/ui/preview/PreviewRateToast.qml
    src/app/ui/preview/PreviewCanvasMenu.qml
    src/app/ui/preview/PreviewTransport.qml
    src/app/ui/preview/NoteStatistics.qml
    src/app/ui/preview/AudioSettingsDialog.qml
    src/app/ui/preview/PreviewSettingsDialog.qml
)
foreach(qml_file IN LISTS MIACODE_UI_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
set_source_files_properties(
    src/app/ui/theme/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

set(MIACODE_UI_ICON_FILES
    src/app/ui/resources/icons/folder-open.svg
    src/app/ui/resources/icons/save.svg
    src/app/ui/resources/icons/undo.svg
    src/app/ui/resources/icons/redo.svg
    src/app/ui/resources/icons/play.svg
    src/app/ui/resources/icons/pause.svg
    src/app/ui/resources/icons/stop.svg
    src/app/ui/resources/icons/fullscreen.svg
    src/app/ui/resources/icons/panel-left.svg
    src/app/ui/resources/icons/panel-bottom.svg
    src/app/ui/resources/icons/panel-right.svg
    src/app/ui/resources/icons/settings.svg
    src/app/ui/resources/icons/chart.svg
    src/app/ui/resources/icons/export.svg
    src/app/ui/resources/icons/tools.svg
    src/app/ui/resources/icons/audio-settings.svg
    src/app/ui/resources/icons/preview-settings.svg
    src/app/ui/resources/icons/file.svg
    src/app/ui/resources/icons/sliders-horizontal.svg
    src/app/ui/resources/icons/volume-2.svg
    src/app/ui/resources/icons/volume-x.svg
    src/app/ui/resources/icons/close.svg
    src/app/ui/resources/icons/more.svg
    src/app/ui/resources/icons/metadata.svg
    src/app/ui/resources/icons/app-icon.svg
    src/app/ui/resources/icons/app-titlebar.png
    src/app/ui/resources/icons/app-titlebar@2x.png
    src/app/ui/resources/icons/chevron-down.svg
    src/app/ui/resources/icons/add.svg
    src/app/ui/resources/icons/remove.svg
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
        src/app/ui/editor/SimaiSyntaxHighlighter.cpp
        src/app/ui/editor/SimaiSyntaxHighlighter.h
        src/app/ui/editor/EditorTextStyle.cpp
        src/app/ui/editor/EditorTextStyle.h
        src/app/ui/ApplicationContext.cpp
        src/app/ui/ApplicationContext.h
        src/app/ui/document/CommandService.cpp
        src/app/ui/document/CommandService.h
        src/app/ui/chrome/ShortcutCommands.h
        src/app/ui/document/DocumentModel.cpp
        src/app/ui/document/DocumentModel.h
        src/core/chart/selection/ChartSelectionBeatSummary.h
        src/core/chart/selection/ChartSelectionBeatSummary.cpp
        src/app/ui/document/DocumentProjection.cpp
        src/app/ui/document/DocumentProjection.h
        src/app/ui/document/AnalysisProjection.cpp
        src/app/ui/document/AnalysisProjection.h
        src/app/ui/document/AnalysisModel.cpp
        src/app/ui/document/AnalysisModel.h
        src/app/ui/preview/PreviewModel.cpp
        src/app/ui/preview/PreviewModel.h
        src/app/ui/timeline/TimelineModel.cpp
        src/app/ui/timeline/TimelineModel.h
        src/app/ui/shell/ShellLifecycle.cpp
        src/app/ui/shell/ShellLifecycle.h
        src/app/ui/preview/NoteImageProvider.cpp
        src/app/ui/preview/NoteImageProvider.h
        src/app/ui/layout/WorkbenchSettings.cpp
        src/app/ui/layout/WorkbenchSettings.h
        src/app/ui/chrome/PlatformChrome.h
        src/app/ui/layout/PageHost.cpp
        src/app/ui/layout/PageHost.h
        src/app/ui/editor/EditorController.cpp
        src/app/ui/editor/EditorController.h
        src/app/ui/chrome/ShortcutModel.cpp
        src/app/ui/chrome/ShortcutModel.h
        src/app/ui/editor/EditorInputBridge.cpp
        src/app/ui/editor/EditorInputBridge.h
        src/app/ui/export/ExportSession.cpp
        src/app/ui/export/ExportSession.h
        src/app/ui/export/CoverExportSession.cpp
        src/app/ui/export/CoverExportSession.h
        src/app/ui/export/CoverExportWindow.cpp
        src/app/ui/export/CoverExportWindow.h
        src/app/ui/chrome/WindowChrome.cpp
        src/app/ui/chrome/WindowChrome.h
        src/app/ui/Bootstrap.cpp
        src/app/ui/Bootstrap.h
        src/app/ui/shell/RootLifecycle.h
        src/app/ui/timeline/TimelineThemeBridge.cpp
        src/app/ui/timeline/TimelineThemeBridge.h
    QML_FILES
        ${MIACODE_UI_QML_FILES}
    RESOURCES
        ${MIACODE_UI_ICON_FILES}
)

set(MIACODE_TRANSLATION_TS_FILES
    translations/en_US.ts
    translations/zh_CN.ts
    translations/ja_JP.ts
)
qt_add_lrelease(
    LRELEASE_TARGET miacode_lrelease
    TS_FILES ${MIACODE_TRANSLATION_TS_FILES}
    QM_FILES_OUTPUT_VARIABLE MIACODE_TRANSLATION_QM_FILES
    OPTIONS -idbased
)
qt_add_resources(MiaCode "miacode_i18n"
    PREFIX "/i18n"
    BASE "${CMAKE_CURRENT_BINARY_DIR}"
    FILES ${MIACODE_TRANSLATION_QM_FILES}
)
add_dependencies(MiaCode miacode_lrelease)

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
    src/app/ui/editor
    src/app/ui/timeline
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

if (WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(MIACODE_BASS_WINDOWS_ARCH "winarm64")
    else()
        set(MIACODE_BASS_WINDOWS_ARCH "win64")
    endif()
    set(MIACODE_BASS_WINDOWS_LIB_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/${MIACODE_BASS_WINDOWS_ARCH}")
    set(MIACODE_BASS_WINDOWS_BIN_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/bin/${MIACODE_BASS_WINDOWS_ARCH}")
    # bass_aac ships for x86/x64 only; the arm64 runtime uses the remaining
    # plugins and the audio backend already tolerates a missing add-on.
    set(MIACODE_BASS_WINDOWS_DLLS bass.dll bassmix.dll bass_fx.dll bassopus.dll)
    if (MIACODE_BASS_WINDOWS_ARCH STREQUAL "win64")
        list(APPEND MIACODE_BASS_WINDOWS_DLLS bass_aac.dll)
    endif()
    foreach(MIACODE_BASS_DLL IN LISTS MIACODE_BASS_WINDOWS_DLLS)
        if (NOT EXISTS "${MIACODE_BASS_WINDOWS_BIN_DIR}/${MIACODE_BASS_DLL}")
            message(FATAL_ERROR
                "Missing required Windows BASS runtime: ${MIACODE_BASS_WINDOWS_BIN_DIR}/${MIACODE_BASS_DLL}")
        endif()
    endforeach()

    target_link_libraries(MiaCode PRIVATE
        "${MIACODE_BASS_WINDOWS_LIB_DIR}/bass.lib"
        "${MIACODE_BASS_WINDOWS_LIB_DIR}/bassmix.lib"
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
        dwmapi       # v2 WindowChrome DwmExtendFrameIntoClientArea
    )
    add_custom_command(TARGET MiaCode POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MIACODE_BASS_WINDOWS_DLLS}
            $<TARGET_FILE_DIR:MiaCode>
        WORKING_DIRECTORY "${MIACODE_BASS_WINDOWS_BIN_DIR}"
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
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(MIACODE_BASS_LINUX_ARCH "x86_64")
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(MIACODE_BASS_LINUX_ARCH "aarch64")
    else()
        message(FATAL_ERROR "MiaCode's bundled Linux BASS runtime currently supports x86_64 and aarch64 only")
    endif()

    set(MIACODE_BASS_LINUX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/linux/${MIACODE_BASS_LINUX_ARCH}")
    set(MIACODE_BASS_LINUX_LIBRARIES
        "${MIACODE_BASS_LINUX_DIR}/libbass.so"
        "${MIACODE_BASS_LINUX_DIR}/libbassmix.so"
        "${MIACODE_BASS_LINUX_DIR}/libbass_fx.so"
    )
    foreach(MIACODE_BASS_LIBRARY IN LISTS MIACODE_BASS_LINUX_LIBRARIES)
        if (NOT EXISTS "${MIACODE_BASS_LIBRARY}")
            message(FATAL_ERROR "Missing required Linux BASS runtime: ${MIACODE_BASS_LIBRARY}")
        endif()
    endforeach()

    target_link_libraries(MiaCode PRIVATE
        "${MIACODE_BASS_LINUX_DIR}/libbass.so"
        "${MIACODE_BASS_LINUX_DIR}/libbassmix.so"
        ${CMAKE_DL_LIBS}
    )
    set_target_properties(MiaCode PROPERTIES
        BUILD_RPATH "$ORIGIN"
        INSTALL_RPATH "$ORIGIN"
    )
    add_custom_command(TARGET MiaCode POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${MIACODE_BASS_LINUX_LIBRARIES}
            $<TARGET_FILE_DIR:MiaCode>
        COMMENT "Deploying Linux BASS runtime libraries"
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
        src/app/ui/chrome/NativeWindowThemeMac.h
        src/app/ui/chrome/NativeWindowThemeMac.mm
        src/app/ui/chrome/WindowChrome.mm
    )
    target_link_libraries(MiaCode PRIVATE "-framework AppKit")
endif()

target_link_libraries(MiaCode PRIVATE Qt6::Multimedia)
target_compile_definitions(MiaCode PRIVATE HAVE_QT_MULTIMEDIA=1)
# ---------------------------------------------------------------------------
# QtAVPlayer (FFmpeg) — preview background-video decode backend.
#
# Replaces QMediaPlayer for PV/BG decoding inside PreviewStageMediaHost; see
# docs/ops/DEBUG_INDEX.md. QtAVPlayer is the
# MIT-licensed source vendored under third_party/QtAVPlayer, compiled straight
# into MiaCode. Windows and macOS use the project-selected FFmpeg SDK through
# MIACODE_FFMPEG_DEV_DIR. Linux resolves FFmpeg and VA-API from the host
# toolchain through pkg-config.
#
# All desktop platforms use QtAVPlayer. Windows selects D3D11VA, macOS selects
# VideoToolbox/Metal, and Linux selects VA-API through DRM/EGL.
# ---------------------------------------------------------------------------
if (WIN32 OR APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (WIN32)
        if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
            set(MIACODE_FFMPEG_DEV_DIR_DEFAULT
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/windows/winarm64/dev")
        else()
            set(MIACODE_FFMPEG_DEV_DIR_DEFAULT
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/windows/win64/dev")
        endif()
    elseif (APPLE)
        set(MIACODE_FFMPEG_DEV_DIR_DEFAULT
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/macos/dev")
    endif()
    if (WIN32 OR APPLE)
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
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MIACODE_FFMPEG REQUIRED IMPORTED_TARGET
            libavfilter libavcodec libavformat libavutil libswresample libswscale)
        pkg_check_modules(MIACODE_VAAPI REQUIRED IMPORTED_TARGET libva libva-drm libdrm)
        find_library(AVFILTER_LIBRARY NAMES avfilter
            HINTS ${MIACODE_FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH REQUIRED NO_CACHE)
        find_library(AVCODEC_LIBRARY NAMES avcodec
            HINTS ${MIACODE_FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH REQUIRED NO_CACHE)
        find_library(AVFORMAT_LIBRARY NAMES avformat
            HINTS ${MIACODE_FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH REQUIRED NO_CACHE)
        find_library(AVUTIL_LIBRARY NAMES avutil
            HINTS ${MIACODE_FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH REQUIRED NO_CACHE)
        find_library(SWRESAMPLE_LIBRARY NAMES swresample
            HINTS ${MIACODE_FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH REQUIRED NO_CACHE)
        find_library(SWSCALE_LIBRARY NAMES swscale
            HINTS ${MIACODE_FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH REQUIRED NO_CACHE)
        set(QT_AVPLAYER_VA_DRM ON CACHE BOOL "" FORCE)
    endif()

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
    )
    if (WIN32 OR APPLE)
        target_include_directories(MiaCode PRIVATE
            "${MIACODE_FFMPEG_DEV_DIR}/include")
    endif()
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
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(MiaCode PRIVATE
            PkgConfig::MIACODE_FFMPEG
            PkgConfig::MIACODE_VAAPI)
    endif()

    if (WIN32)
        # Runtime: the preview backend loads these next to MiaCode.exe. Resolve
        # the six families from the project FFmpeg 8.1 SDK; the package contract
        # in scripts/build/windows-toolchain.psd1 pins the exact names.
        file(GLOB MIACODE_FFMPEG_WINDOWS_RUNTIME_DLLS
            "${MIACODE_FFMPEG_DEV_DIR}/bin/avcodec-*.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/avformat-*.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/avutil-*.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/swresample-*.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/swscale-*.dll"
            "${MIACODE_FFMPEG_DEV_DIR}/bin/avfilter-*.dll"
        )
        if (NOT MIACODE_FFMPEG_WINDOWS_RUNTIME_DLLS)
            message(FATAL_ERROR
                "No FFmpeg preview runtime DLLs found under '${MIACODE_FFMPEG_DEV_DIR}/bin'.")
        endif()
        add_custom_command(TARGET MiaCode POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${MIACODE_FFMPEG_WINDOWS_RUNTIME_DLLS}
                $<TARGET_FILE_DIR:MiaCode>
            VERBATIM
        )
    elseif (APPLE)
        # The project-local macOS SDK has @rpath install names. Make the local
        # app bundle runnable too, then let the release packager stage the
        # same six dylibs in its final Frameworks directory.
        set(MIACODE_MACOS_FFMPEG_RUNTIME_LIBRARIES
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libavcodec.62.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libavfilter.11.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libavformat.62.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libavutil.60.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libswresample.6.dylib"
            "${MIACODE_FFMPEG_DEV_DIR}/lib/libswscale.9.dylib"
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
    if (MINGW)
        # MinGW counterpart of MSVC_RUNTIME_LIBRARY MultiThreaded above: the
        # launcher sits at the dist root while the packaged libstdc++-6 /
        # libgcc_s_seh-1 / libwinpthread-1 DLLs live in app/, so the launcher
        # has to carry no MinGW runtime dependency of its own. -municode
        # selects the wWinMain entry point the source defines; MinGW's default
        # crt startup looks for WinMain and fails to link without it.
        target_link_options(MiaCodeLauncher PRIVATE -municode -static -static-libgcc -static-libstdc++)
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
    include(cmake/devtools/SharedSources.cmake)

    # Qt Test and Qt Widgets are dev-tools-only dependencies: QML input
    # regressions need a supported way to deliver real mouse/key events into a
    # QQuickWindow, while a few legacy specs still exercise widget helpers.
    # Neither component is part of the MiaCode product target.
    #
    # Qt Network is dev-tools-only for the same reason: the Net page was removed
    # from the v2 product runtime, so its engine lives on in net_client_spec
    # alone. See docs/ops/DEPENDENCY_ALLOWLIST.md — linking it into MiaCode is a
    # spec failure, not a style preference.
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test Network Widgets)

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
        if (_qml_name STREQUAL "Theme.qml")
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
            "${MIACODE_BASS_WINDOWS_LIB_DIR}/bass.lib"
            "${MIACODE_BASS_WINDOWS_LIB_DIR}/bassmix.lib"
            avrt
        )
        add_custom_command(TARGET soundtouch_probe POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${MIACODE_BASS_WINDOWS_DLLS}
                $<TARGET_FILE_DIR:soundtouch_probe>
            WORKING_DIRECTORY "${MIACODE_BASS_WINDOWS_BIN_DIR}"
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

    include(cmake/devtools/MiaCodeSpecRegistry.cmake)
    include(cmake/devtools/specs/index.cmake)
    miacode_finalize_spec_registry()
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
