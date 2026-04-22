cmake_minimum_required(VERSION 3.21)

set(MIACODE_VERSION_MAJOR "0")
set(MIACODE_VERSION_MINOR "4")
set(MIACODE_VERSION_PATCH "0")
set(MIACODE_VERSION_PRERELEASE "beta16")
set(MIACODE_BUILD_LAYOUT_REVISION "2026-04-21-first-unification")
set(MIACODE_VERSION "${MIACODE_VERSION_MAJOR}.${MIACODE_VERSION_MINOR}.${MIACODE_VERSION_PATCH}")
set(MIACODE_VERSION_STRING "${MIACODE_VERSION}")
if (NOT "${MIACODE_VERSION_PRERELEASE}" STREQUAL "")
    set(MIACODE_VERSION_STRING "${MIACODE_VERSION}-${MIACODE_VERSION_PRERELEASE}")
endif()
set(MIACODE_DISPLAY_PRERELEASE "beta16")
set(MIACODE_DISPLAY_VERSION_STRING "${MIACODE_VERSION}")
if (NOT "${MIACODE_DISPLAY_PRERELEASE}" STREQUAL "")
    set(MIACODE_DISPLAY_VERSION_STRING "${MIACODE_DISPLAY_VERSION_STRING}-${MIACODE_DISPLAY_PRERELEASE}")
endif()

project(MiaCode VERSION ${MIACODE_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum macOS deployment version" FORCE)
endif()

if (MSVC)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"
    )
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(MIACODE_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico" CACHE FILEPATH "Optional application icon (.ico) for Windows builds")
set(MIACODE_MACOS_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.icns" CACHE FILEPATH "Optional application icon (.icns) for macOS bundles")
option(MIACODE_BUILD_DEV_TOOLS "Build developer helper tools" OFF)

configure_file(
    src/app/AppVersion.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/AppVersion.h
    @ONLY
)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets OpenGL Multimedia Quick Qml QuickControls2 ShaderTools)

add_executable(MiaCode WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/quick_shell/QuickShellBootstrap.h
    src/app/quick_shell/QuickShellBootstrap.cpp
    src/app/quick_shell/QuickShellContracts.h
    src/app/quick_shell/QuickShellNativeSurfaceHost.h
    src/app/quick_shell/QuickShellNativeSurfaceHost.cpp
    src/app/quick_shell/QuickShellPreviewCompositeSurface.h
    src/app/quick_shell/QuickShellPreviewCompositeSurface.cpp
    src/app/quick_shell/QuickShellPreviewSurfacePolicy.h
    src/app/quick_shell/QuickShellController.h
    src/app/quick_shell/QuickShellController.cpp
    src/app/quick_shell/QuickShellStyleBridge.h
    src/app/quick_shell/QuickShellStyleBridge.cpp
    src/app/mainwindow/MainWindow.h
    src/app/mainwindow/MainWindow.cpp
    src/app/mainwindow/MainWindowShared.h
    src/app/mainwindow/MainWindowShared.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentFlow.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentEditorState.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentTransforms.cpp
    src/app/mainwindow/sections/document/MainWindow.DocumentUi.cpp
    src/app/mainwindow/sections/dialogs/MainWindow.Dialogs.cpp
    src/app/mainwindow/sections/dialogs/MainWindow.DialogsSection.cpp
    src/app/mainwindow/sections/editor/MainWindow.EditorDisplay.cpp
    src/app/mainwindow/sections/editor/MainWindow.EditorState.cpp
    src/app/mainwindow/sections/export/MainWindow.ExportFlow.cpp
    src/app/mainwindow/sections/export/MainWindow.ExportSnapshot.cpp
    src/app/mainwindow/sections/export/MainWindow.ExportSection.cpp
    src/app/mainwindow/sections/export/MainWindow.ExportWorker.cpp
    src/app/mainwindow/sections/frame/MainWindow.BootstrapAndMenus.cpp
    src/app/mainwindow/sections/frame/MainWindow.FrameBootstrap.cpp
    src/app/mainwindow/sections/frame/MainWindow.FrameBootstrapFinalize.cpp
    src/app/mainwindow/sections/preferences/MainWindow.PreferencesDialog.cpp
    src/app/mainwindow/sections/preview/MainWindow.PreviewStageMediaRoute.cpp
    src/app/mainwindow/sections/preview/MainWindow.PreviewWarmupAndSettings.cpp
    src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelineLayout.cpp
    src/app/mainwindow/sections/timeline/MainWindow.TimelinePlayback.cpp
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
    src/app/ui/UiTheme.h
    src/app/ui/UiTheme.cpp
    src/app/ui/WindowParityMetrics.h
    src/app/ui/WindowParityMetrics.cpp
    src/common/MuriTypes.h
    src/common/MuriTypes.cpp
    src/common/MuriRenderOptions.h
    src/common/MuriConfig.h
    src/common/DebugLog.h
    src/common/DebugLog.cpp
    src/common/DebugImageCompare.h
    src/common/ChartAssetPaths.h
    src/common/TimelineThemeConfig.h
    src/common/WaveformCache.h
    src/common/WaveformCache.cpp
    src/common/PreviewTimingSettings.h
    src/common/PreviewSfxTiming.h
    src/common/PreviewSfxTimeline.h
    src/editor/PlainCodeEditor.h
    src/editor/PlainCodeEditor.cpp
    src/editor/BracketScopeHighlighter.h
    src/editor/BracketScopeHighlighter.cpp
    src/tools/latency/LatencyDetectorDialog.h
    src/tools/latency/LatencyDetectorDialog.cpp
    src/tools/muri/MuriAnalyzer.h
    src/tools/muri/MuriAnalyzer.cpp
    src/tools/muri/MuriPanelEntries.h
    src/tools/muri/MuriPanelEntries.cpp
    src/tools/muri/MuriStaticChecker.h
    src/tools/muri/MuriStaticChecker.cpp
    src/tools/video_export/VideoExportDialog.h
    src/tools/video_export/VideoExportDialog.cpp
    src/tools/video_export/BatchVideoExportDialog.h
    src/tools/video_export/BatchVideoExportDialog.cpp
    src/tools/video_export/VideoExportController.h
    src/tools/video_export/VideoExportController.cpp
    src/tools/video_export/VideoExportRuntimePolicy.h
    src/tools/video_export/VideoExportRuntimePolicy.cpp
    src/tools/video_export/VideoExportQuickRenderBackend.h
    src/tools/video_export/VideoExportQuickRenderBackend.cpp
    src/tools/video_export/RawVideoPipeTransport.h
    src/tools/video_export/RawVideoPipeTransport.cpp
    src/tools/video_export/VideoExportSnapshot.h
    src/tools/video_export/VideoExportSnapshot.cpp
    src/preview/runtime/PreviewQuickExportSession.h
    src/preview/runtime/PreviewQuickExportSession.cpp
    src/preview/runtime/PreviewSceneAssetLoader.h
    src/preview/runtime/PreviewSceneAssetLoader.cpp
    src/preview/runtime/PreviewSceneAssetRepository.h
    src/preview/runtime/PreviewSceneAssetRepository.cpp
    src/preview/runtime/PreviewStageMediaHost.h
    src/preview/runtime/PreviewStageMediaHost.cpp
    src/preview/runtime/PreviewRuntime.h
    src/preview/runtime/PreviewRuntime.cpp
    src/preview/scene/PreviewFrameState.h
    src/preview/scene/PreviewHudState.h
    src/preview/scene/PreviewHudState.cpp
    src/preview/scene/PreviewProgressStatsCache.h
    src/preview/scene/PreviewProgressStatsCache.cpp
    src/preview/scene/PreviewPreparedSceneCache.h
    src/preview/scene/PreviewPreparedSceneCache.cpp
    src/preview/scene/PreviewLayerOrder.h
    src/preview/scene/PreviewOpacityCurves.h
    src/preview/scene/PreviewOpacityCurves.cpp
    src/preview/scene/PreviewSceneConstants.h
    src/preview/scene/PreviewSceneMath.h
    src/preview/scene/PreviewSceneMath.cpp
    src/preview/scene/PreviewSceneGeometry.h
    src/preview/scene/PreviewSceneGeometry.cpp
    src/preview/scene/PreviewMarkerDrawOrder.h
    src/preview/scene/PreviewMarkerDrawOrder.cpp
    src/preview/scene/PreviewSkinSelectors.h
    src/preview/scene/PreviewSkinSelectors.cpp
    src/preview/scene/PreviewSpriteDescriptor.h
    src/preview/scene/PreviewCircleDescriptor.h
    src/preview/scene/PreviewSectorDescriptor.h
    src/preview/scene/PreviewTrackShared.h
    src/preview/scene/PreviewTrackShared.cpp
    src/preview/scene/PreviewJudgeOverlayShared.h
    src/preview/scene/PreviewJudgeOverlayShared.cpp
    src/preview/scene/PreviewGuideLayerState.h
    src/preview/scene/PreviewGuideLayerState.cpp
    src/preview/scene/PreviewTrackLayerState.h
    src/preview/scene/PreviewTrackLayerState.cpp
    src/preview/scene/PreviewSlideMotionLayerState.h
    src/preview/scene/PreviewSlideMotionLayerState.cpp
    src/preview/scene/PreviewJudgeEffectLayerState.h
    src/preview/scene/PreviewJudgeEffectLayerState.cpp
    src/preview/scene/PreviewChartReviewLayerState.h
    src/preview/scene/PreviewChartReviewLayerState.cpp
    src/preview/scene/PreviewJudgeFireworkLayerState.h
    src/preview/scene/PreviewJudgeFireworkLayerState.cpp
    src/preview/scene/PreviewMuriPadLayerState.h
    src/preview/scene/PreviewMuriPadLayerState.cpp
    src/preview/scene/PreviewMuriActionLayerState.h
    src/preview/scene/PreviewMuriActionLayerState.cpp
    src/preview/scene/PreviewTouchJudgeLayerState.h
    src/preview/scene/PreviewTouchJudgeLayerState.cpp
    src/preview/scene/PreviewTouchLayerState.h
    src/preview/scene/PreviewTouchLayerState.cpp
    src/preview/scene/PreviewAnimatedSpriteHelpers.h
    src/preview/scene/PreviewAnimatedSpriteHelpers.cpp
    src/preview/scene/PreviewArcDescriptor.h
    src/preview/scene/PreviewHeadLayerState.h
    src/preview/scene/PreviewHeadLayerState.cpp
    src/preview/scene/PreviewTouchHoldLayerState.h
    src/preview/scene/PreviewTouchHoldLayerState.cpp
    src/preview/scene/PreviewMaimuriDxJudgeLayerState.h
    src/preview/scene/PreviewMaimuriDxJudgeLayerState.cpp
    src/preview/quick_scene/PreviewTextureRepository.h
    src/preview/quick_scene/PreviewTextureRepository.cpp
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
    src/preview/audio/PreviewAudioBackend.h
    src/preview/audio/PreviewAudioSettings.h
    src/preview/audio/PreviewAudioSettings.cpp
    src/preview/audio/BassPreviewAudioBackend.h
    src/preview/audio/BassPreviewAudioBackend.cpp
    src/preview/audio/MiniaudioPreviewAudioBackend.h
    src/preview/audio/MiniaudioPreviewAudioBackend.cpp
    src/preview/audio/QtPreviewSfxRuntime.h
    src/preview/audio/QtPreviewSfxRuntime.cpp
    src/simai/transform/ChartBatchTransform.h
    src/simai/transform/ChartBatchTransform.cpp
    src/simai/transform/ChartNormalization.h
    src/simai/transform/ChartNormalization.cpp
    src/simai/document/SimaiDocument.h
    src/simai/document/SimaiDocument.cpp
    src/simai/document/SimaiTimingMetadata.h
    src/simai/document/SimaiTimingMetadata.cpp
    src/simai/parser/SimaiNativeParser.h
    src/simai/parser/SimaiNativeParser.cpp
    src/timeline/TimelineData.h
    src/timeline/TimelineRenderData.h
    src/timeline/TimelineQuickModel.h
    src/timeline/TimelineQuickModel.cpp
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
    src/timeline/quick/TimelineQuickLayerUtils.h
    src/timeline/quick/TimelineQuickLayerUtils.cpp
    src/timeline/quick/TimelineQuickGridLayer.h
    src/timeline/quick/TimelineQuickGridLayer.cpp
    src/timeline/quick/TimelineQuickWaveformLayer.h
    src/timeline/quick/TimelineQuickWaveformLayer.cpp
    src/timeline/quick/TimelineQuickHeaderLayer.h
    src/timeline/quick/TimelineQuickHeaderLayer.cpp
    src/timeline/quick/TimelineQuickNotesLayer.h
    src/timeline/quick/TimelineQuickNotesLayer.cpp
    src/timeline/quick/TimelineQuickOverlayLayer.h
    src/timeline/quick/TimelineQuickOverlayLayer.cpp
    resources/app_icons.qrc
    resources/fonts.qrc
    resources/preview_runtime_qml.qrc
    resources/quick_shell_qml.qrc
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
set_target_properties(MiaCode PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "MiaCode"
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

target_link_libraries(MiaCode PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    soundtouch
)
target_include_directories(MiaCode PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    third_party/bass/include
    src
    src/app
    src/app/quick_shell
    src/app/ui
    src/editor
    src/preview
    src/preview/audio
    src/preview/quick_scene
    src/preview/runtime
    src/preview/scene
    src/preview/video
    src/simai
    src/simai/document
    src/simai/parser
    src/timeline
    src/tools/latency
    src/tools/video_export
)

target_sources(MiaCode PRIVATE
    src/common/PreviewAudioMixConfig.h
    src/tools/video_export/VideoExportAudioRenderPlan.h
    src/tools/video_export/VideoExportAudioRenderPlan.cpp
    src/tools/video_export/VideoExportAudioBackend.h
    src/tools/video_export/LegacyExportAudioBackend.h
    src/tools/video_export/LegacyExportAudioBackend.cpp
    src/tools/video_export/BassExportAudioBackend.h
    src/tools/video_export/BassExportAudioBackend.cpp
)

if (WIN32)
    target_link_libraries(MiaCode PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bass.lib"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/bass/lib/win64/bassmix.lib"
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
endif()

target_link_libraries(MiaCode PRIVATE Qt6::Multimedia)
target_compile_definitions(MiaCode PRIVATE HAVE_QT_MULTIMEDIA=1)

if (MSVC)
    target_compile_options(MiaCode PRIVATE /FS)
endif()

if (MIACODE_BUILD_DEV_TOOLS)
    add_executable(miacode_muri_dump
        src/tools/muri/MuriDump.cpp
        src/common/MuriTypes.h
        src/common/MuriTypes.cpp
        src/simai/document/SimaiDocument.h
        src/simai/document/SimaiDocument.cpp
        src/simai/document/SimaiTimingMetadata.h
        src/simai/document/SimaiTimingMetadata.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        src/tools/muri/MuriAnalyzer.h
        src/tools/muri/MuriAnalyzer.cpp
        src/tools/muri/MuriStaticChecker.h
        src/tools/muri/MuriStaticChecker.cpp
        resources/fonts.qrc
    )

    add_executable(simai_native_dump
        src/simai/parser/SimaiNativeDump.cpp
        src/simai/document/SimaiDocument.h
        src/simai/document/SimaiDocument.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        src/simai/document/SimaiTimingMetadata.h
        src/simai/document/SimaiTimingMetadata.cpp
        resources/fonts.qrc
    )

    add_executable(soundtouch_probe
        src/preview/audio/PreviewAudioBackend.h
        src/tools/probe/SoundtouchProbe.cpp
        src/preview/audio/PreviewAudioSettings.h
        src/preview/audio/PreviewAudioSettings.cpp
        src/preview/audio/BassPreviewAudioBackend.h
        src/preview/audio/BassPreviewAudioBackend.cpp
        src/preview/audio/MiniaudioPreviewAudioBackend.h
        src/preview/audio/MiniaudioPreviewAudioBackend.cpp
        src/preview/audio/QtPreviewSfxRuntime.h
        src/preview/audio/QtPreviewSfxRuntime.cpp
        src/common/DebugLog.h
        src/common/DebugLog.cpp
    )

    add_executable(simai_parser_spec
        src/tools/simai_parser/SimaiParserSpec.cpp
        src/simai/document/SimaiDocument.h
        src/simai/document/SimaiDocument.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        src/simai/document/SimaiTimingMetadata.h
        src/simai/document/SimaiTimingMetadata.cpp
        resources/fonts.qrc
    )

    add_executable(chart_batch_transform_spec
        src/tools/chart_transform/ChartBatchTransformSpec.cpp
        src/simai/transform/ChartBatchTransform.h
        src/simai/transform/ChartBatchTransform.cpp
        src/simai/transform/ChartNormalization.h
        src/simai/transform/ChartNormalization.cpp
        src/simai/document/SimaiDocument.h
        src/simai/document/SimaiDocument.cpp
        src/simai/document/SimaiTimingMetadata.h
        src/simai/document/SimaiTimingMetadata.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        resources/fonts.qrc
    )

    add_executable(muri_spec
        src/tools/muri/MuriSpec.cpp
        src/common/MuriTypes.h
        src/common/MuriTypes.cpp
        src/simai/document/SimaiDocument.h
        src/simai/document/SimaiDocument.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        src/simai/document/SimaiTimingMetadata.h
        src/simai/document/SimaiTimingMetadata.cpp
        src/tools/muri/MuriAnalyzer.h
        src/tools/muri/MuriAnalyzer.cpp
        src/tools/muri/MuriPanelEntries.h
        src/tools/muri/MuriPanelEntries.cpp
        src/tools/muri/MuriStaticChecker.h
        src/tools/muri/MuriStaticChecker.cpp
        resources/fonts.qrc
    )

    add_executable(timeline_model_spec
        src/tools/timeline/TimelineModelSpec.cpp
        src/simai/document/SimaiDocument.h
        src/simai/document/SimaiDocument.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        src/simai/document/SimaiTimingMetadata.h
        src/simai/document/SimaiTimingMetadata.cpp
        src/timeline/TimelineQuickModel.h
        src/timeline/TimelineQuickModel.cpp
        resources/fonts.qrc
    )

    add_executable(preview_asset_loader_spec
        src/tools/preview/PreviewSceneAssetLoaderSpec.cpp
        src/preview/runtime/PreviewSceneAssetLoader.h
        src/preview/runtime/PreviewSceneAssetLoader.cpp
        src/preview/scene/PreviewFrameState.h
    )

    add_executable(preview_firework_lifecycle_spec
        src/tools/preview/PreviewFireworkLifecycleSpec.cpp
        src/preview/scene/PreviewFrameState.h
        src/preview/scene/PreviewJudgeFireworkLayerState.h
        src/preview/scene/PreviewJudgeFireworkLayerState.cpp
        src/preview/scene/PreviewSceneMath.h
        src/preview/scene/PreviewSceneMath.cpp
    )

    add_executable(preview_head_layer_spec
        src/tools/preview/PreviewHeadLayerSpec.cpp
        src/simai/document/SimaiDocument.h
        src/simai/document/SimaiDocument.cpp
        src/simai/parser/SimaiNativeParser.h
        src/simai/parser/SimaiNativeParser.cpp
        src/simai/document/SimaiTimingMetadata.h
        src/simai/document/SimaiTimingMetadata.cpp
        src/preview/scene/PreviewFrameState.h
        src/preview/scene/PreviewPreparedSceneCache.h
        src/preview/scene/PreviewPreparedSceneCache.cpp
        src/preview/scene/PreviewChartReviewLayerState.h
        src/preview/scene/PreviewChartReviewLayerState.cpp
        src/preview/scene/PreviewHeadLayerState.h
        src/preview/scene/PreviewHeadLayerState.cpp
        src/preview/scene/PreviewMarkerDrawOrder.h
        src/preview/scene/PreviewMarkerDrawOrder.cpp
        src/preview/scene/PreviewAnimatedSpriteHelpers.h
        src/preview/scene/PreviewAnimatedSpriteHelpers.cpp
        src/preview/scene/PreviewJudgeOverlayShared.h
        src/preview/scene/PreviewJudgeOverlayShared.cpp
        src/preview/scene/PreviewOpacityCurves.h
        src/preview/scene/PreviewOpacityCurves.cpp
        src/preview/scene/PreviewSceneConstants.h
        src/preview/scene/PreviewSceneMath.h
        src/preview/scene/PreviewSceneMath.cpp
        src/preview/scene/PreviewSkinSelectors.h
        src/preview/scene/PreviewSkinSelectors.cpp
        src/common/MuriTypes.h
        src/common/MuriTypes.cpp
        src/timeline/TimelineData.h
        resources/fonts.qrc
    )

    add_executable(preview_realtime_object_hot_path_spec
        src/tools/preview/PreviewRealtimeObjectHotPathSpec.cpp
        src/preview/scene/PreviewActiveMarkerView.h
        src/preview/scene/PreviewPreparedSceneCache.h
        src/preview/scene/PreviewPreparedSceneCache.cpp
        src/preview/scene/PreviewFrameState.h
        src/preview/scene/PreviewChartReviewLayerState.h
        src/preview/scene/PreviewChartReviewLayerState.cpp
        src/preview/scene/PreviewMarkerDrawOrder.h
        src/preview/scene/PreviewMarkerDrawOrder.cpp
        src/preview/scene/PreviewSlideMotionLayerState.h
        src/preview/scene/PreviewSlideMotionLayerState.cpp
        src/preview/scene/PreviewTrackLayerState.h
        src/preview/scene/PreviewTrackLayerState.cpp
        src/preview/scene/PreviewTrackShared.h
        src/preview/scene/PreviewTrackShared.cpp
        src/preview/scene/PreviewMuriActionLayerState.h
        src/preview/scene/PreviewMuriActionLayerState.cpp
        src/preview/scene/PreviewAnimatedSpriteHelpers.h
        src/preview/scene/PreviewAnimatedSpriteHelpers.cpp
        src/preview/scene/PreviewJudgeOverlayShared.h
        src/preview/scene/PreviewJudgeOverlayShared.cpp
        src/preview/scene/PreviewOpacityCurves.h
        src/preview/scene/PreviewOpacityCurves.cpp
        src/preview/scene/PreviewSceneConstants.h
        src/preview/scene/PreviewSceneMath.h
        src/preview/scene/PreviewSceneMath.cpp
        src/preview/scene/PreviewSkinSelectors.h
        src/preview/scene/PreviewSkinSelectors.cpp
        src/common/MuriTypes.h
        src/common/MuriTypes.cpp
        src/timeline/TimelineData.h
    )

    add_executable(preview_quick_sprite_batch_spec
        src/tools/preview/PreviewQuickSpriteBatchSpec.cpp
        src/preview/quick_scene/PreviewQuickSpriteBatchPolicy.h
    )

    add_executable(preview_sfx_timeline_spec
        src/tools/preview/PreviewSfxTimelineSpec.cpp
        src/common/PreviewTimingSettings.h
        src/common/PreviewSfxTiming.h
        src/common/PreviewSfxTimeline.h
        src/common/PreviewGameplayConfig.h
        src/timeline/TimelineData.h
    )

    add_executable(preview_audio_settings_spec
        src/tools/preview/PreviewAudioSettingsSpec.cpp
        src/preview/audio/PreviewAudioSettings.h
        src/preview/audio/PreviewAudioSettings.cpp
        src/common/PreviewSfxAssets.h
        src/common/PreviewSfxSemantics.h
    )

    add_executable(bass_preview_retained_state_spec
        src/tools/preview/BassPreviewRetainedStateSpec.cpp
        src/preview/audio/BassPreviewRetainedState.h
        src/preview/audio/PreviewAudioBackend.h
    )

    add_executable(bass_preview_debug_log_routing_spec
        src/tools/preview/BassPreviewDebugLogRoutingSpec.cpp
        src/preview/audio/BassPreviewDebugLogRouting.h
    )

    add_executable(quickshell_preview_surface_policy_spec
        src/tools/preview/QuickShellPreviewSurfacePolicySpec.cpp
        src/app/quick_shell/QuickShellPreviewSurfacePolicy.h
    )

    add_executable(video_export_runtime_policy_spec
        src/tools/video_export/VideoExportRuntimePolicySpec.cpp
        src/tools/video_export/VideoExportRuntimePolicy.h
        src/tools/video_export/VideoExportRuntimePolicy.cpp
    )

    add_executable(video_export_audio_render_plan_spec
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
        src/preview/audio/PreviewAudioSettings.h
        src/preview/audio/PreviewAudioSettings.cpp
        src/timeline/TimelineData.h
    )

    target_link_libraries(miacode_muri_dump PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(miacode_muri_dump PRIVATE
        src
        src/app/ui
        src/common
        src/simai
        src/simai/document
        src/simai/parser
        src/timeline
        src/tools
    )

    target_link_libraries(simai_native_dump PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(simai_native_dump PRIVATE
        src
        src/simai
        src/simai/parser
        src/timeline
    )

    target_link_libraries(simai_parser_spec PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(simai_parser_spec PRIVATE
        src
        src/app/ui
        src/simai
        src/simai/parser
        src/timeline
    )

    target_link_libraries(soundtouch_probe PRIVATE
        Qt6::Concurrent
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        soundtouch
    )
    target_include_directories(soundtouch_probe PRIVATE
        third_party/bass/include
        src
        src/preview
        src/preview/audio
        src/preview/video
        src/timeline
        src/tools
    )

    if (WIN32)
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

    target_link_libraries(chart_batch_transform_spec PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(chart_batch_transform_spec PRIVATE
        src
        src/app/ui
        src/common
        src/simai
        src/simai/document
        src/simai/parser
        src/simai/transform
        src/timeline
    )

    target_link_libraries(muri_spec PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(muri_spec PRIVATE
        src
        src/app/ui
        src/common
        src/simai
        src/simai/parser
        src/timeline
        src/tools
    )

    target_link_libraries(timeline_model_spec PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(timeline_model_spec PRIVATE
        src
        src/app/ui
        src/simai
        src/simai/parser
        src/timeline
        src/tools
    )

    target_link_libraries(preview_asset_loader_spec PRIVATE
        Qt6::Core
        Qt6::Gui
    )
    target_include_directories(preview_asset_loader_spec PRIVATE
        src
        src/common
        src/preview
        src/preview/runtime
        src/preview/scene
        src/preview/video
        src/timeline
    )

    target_link_libraries(preview_firework_lifecycle_spec PRIVATE
        Qt6::Core
        Qt6::Gui
    )
    target_include_directories(preview_firework_lifecycle_spec PRIVATE
        src
        src/common
        src/preview
        src/preview/scene
        src/preview/video
        src/timeline
    )

    target_link_libraries(preview_head_layer_spec PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    )
    target_include_directories(preview_head_layer_spec PRIVATE
        src
        src/common
        src/preview
        src/preview/scene
        src/simai
        src/simai/document
        src/simai/parser
        src/timeline
    )

    target_link_libraries(preview_realtime_object_hot_path_spec PRIVATE
        Qt6::Core
        Qt6::Gui
    )
    target_include_directories(preview_realtime_object_hot_path_spec PRIVATE
        src
        src/common
        src/preview
        src/preview/scene
        src/timeline
    )

    target_link_libraries(preview_quick_sprite_batch_spec PRIVATE
        Qt6::Core
    )
    target_include_directories(preview_quick_sprite_batch_spec PRIVATE
        src
        src/common
        src/preview
        src/preview/quick_scene
    )

    target_link_libraries(preview_sfx_timeline_spec PRIVATE
        Qt6::Core
    )
    target_include_directories(preview_sfx_timeline_spec PRIVATE
        src
        src/common
        src/timeline
    )

    target_link_libraries(preview_audio_settings_spec PRIVATE
        Qt6::Core
    )
    target_include_directories(preview_audio_settings_spec PRIVATE
        src
        src/common
        src/preview
        src/preview/audio
    )

    target_link_libraries(bass_preview_retained_state_spec PRIVATE
        Qt6::Core
    )
    target_include_directories(bass_preview_retained_state_spec PRIVATE
        src
        src/common
        src/preview
        src/preview/audio
    )

    target_link_libraries(bass_preview_debug_log_routing_spec PRIVATE
        Qt6::Core
    )
    target_include_directories(bass_preview_debug_log_routing_spec PRIVATE
        src
        src/common
        src/preview
        src/preview/audio
    )

    target_link_libraries(quickshell_preview_surface_policy_spec PRIVATE
        Qt6::Core
    )
    target_include_directories(quickshell_preview_surface_policy_spec PRIVATE
        src
        src/app/quick_shell
    )

    target_link_libraries(video_export_runtime_policy_spec PRIVATE
        Qt6::Core
    )
    target_include_directories(video_export_runtime_policy_spec PRIVATE
        src
        src/tools
    )

    target_link_libraries(video_export_audio_render_plan_spec PRIVATE
        Qt6::Core
        Qt6::Gui
    )
    target_include_directories(video_export_audio_render_plan_spec PRIVATE
        src
        src/common
        src/preview/audio
        src/preview/video
        src/timeline
        src/tools
        src/tools/video_export
    )
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
