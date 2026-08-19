# Explicit spec targets; contract IDs stay stable across source/target renames.

miacode_add_spec(preview_asset_loader_spec
    OWNER src/preview/runtime
    CONTRACT preview.preview-asset-loader
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(preview_firework_lifecycle_spec
    OWNER src/core/scene
    CONTRACT preview.preview-firework-lifecycle
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(preview_end_of_media_policy_spec
    OWNER src/core/video
    CONTRACT preview.preview-end-of-media-policy
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewEndOfMediaPolicySpec.cpp
        src/core/video/PreviewEndOfMediaPolicy.h
    LIBS Qt6::Core
    INCLUDES src src/common src/core/video
)

miacode_add_spec(preview_firework_warmup_policy_spec
    OWNER src/core/scene
    CONTRACT preview.preview-firework-warmup-policy
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(preview_head_layer_spec
    OWNER src/core/scene
    CONTRACT preview.preview-head-layer
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewHeadLayerSpec.cpp
        ${_miacode_chart_core}
        ${_miacode_log_core}
        src/core/scene/PreviewFrameState.h
        src/core/scene/PreviewPreparedSceneCache.h
        src/core/scene/PreviewPreparedSceneCache.cpp
        src/core/scene/PreviewChartReviewLayerState.h
        src/core/scene/PreviewChartReviewLayerState.cpp
        src/core/scene/PreviewMaimuriDxJudgeLayerState.h
        src/core/scene/PreviewMaimuriDxJudgeLayerState.cpp
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

miacode_add_spec(preview_guide_layer_spec
    OWNER src/core/scene
    CONTRACT preview.preview-guide-layer
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(preview_slide_erase_by_area_spec
    OWNER src/core/scene
    CONTRACT preview.preview-slide-erase-by-area
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewSlideEraseByAreaSpec.cpp
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

miacode_add_spec(preview_realtime_object_hot_path_spec
    OWNER src/core/scene
    CONTRACT preview.preview-realtime-object-hot-path
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(preview_quick_sprite_batch_spec
    OWNER src/preview/quick_scene
    CONTRACT preview.preview-quick-sprite-batch
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewQuickSpriteBatchSpec.cpp
        src/preview/quick_scene/PreviewQuickSpriteBatchPolicy.h
    LIBS Qt6::Core
    INCLUDES src src/common src/preview src/preview/quick_scene
)

miacode_add_spec(preview_texture_generation_policy_spec
    OWNER src/preview/quick_scene
    CONTRACT preview.preview-texture-generation-policy
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewTextureGenerationPolicySpec.cpp
        src/preview/quick_scene/PreviewTextureGenerationPolicy.h
    LIBS Qt6::Core
    INCLUDES src src/preview src/preview/quick_scene
)

miacode_add_spec(preview_sfx_timeline_spec
    OWNER src/common
    CONTRACT preview.preview-sfx-timeline
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(preview_audio_settings_spec
    OWNER src/audio
    CONTRACT preview.preview-audio-settings
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewAudioSettingsSpec.cpp
        src/audio/PreviewAudioSettings.h
        src/audio/PreviewAudioSettings.cpp
        src/common/PreviewSfxAssets.h
        src/common/PreviewSfxSemantics.h
    LIBS Qt6::Core
    INCLUDES src src/common src/preview src/audio
)

miacode_add_spec(preview_audio_command_queue_spec
    OWNER src/audio
    CONTRACT preview.preview-audio-command-queue
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewAudioCommandQueueSpec.cpp
        src/audio/PreviewAudioCommandQueue.h
        src/audio/PreviewAudioCommandQueue.cpp
        src/audio/PreviewAudioWorkerProtocol.h
    LIBS Qt6::Core
    INCLUDES src src/common src/audio src/timeline
)

miacode_add_spec(preview_audio_worker_protocol_spec
    OWNER src/audio
    CONTRACT preview.preview-audio-worker-protocol
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewAudioWorkerProtocolSpec.cpp
        src/audio/PreviewAudioWorkerProtocol.h
    LIBS Qt6::Core
    INCLUDES src src/common src/audio src/timeline
)

miacode_add_spec(preview_audio_playback_flow_policy_spec
    OWNER src/audio
    CONTRACT preview.preview-audio-playback-flow-policy
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewAudioPlaybackFlowPolicySpec.cpp
        src/audio/PreviewAudioPlaybackFlowPolicy.h
    LIBS Qt6::Core
    INCLUDES src src/common src/audio src/timeline
)
# Keep this copy of the production factory on the no-BASS route even when
# the worker spec also compiles the platform BASS backend sources.
add_library(preview_audio_worker_spec_miniaudio_factory OBJECT
    src/audio/PreviewAudioWorkerFactory.cpp
)
target_link_libraries(preview_audio_worker_spec_miniaudio_factory PRIVATE Qt6::Core)
target_include_directories(preview_audio_worker_spec_miniaudio_factory PRIVATE src src/audio)

miacode_add_spec(preview_audio_worker_spec
    OWNER src/audio
    CONTRACT preview.preview-audio-worker
    DOMAIN preview KIND integration RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(preview_audio_non_gui_barrier_spec
    OWNER src/audio
    CONTRACT preview.preview-audio-non-gui-barrier
    DOMAIN preview KIND integration RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(touch_pad_authoring_state_spec
    OWNER src/core/scene
    CONTRACT preview.touch-pad-authoring-state
    DOMAIN preview KIND source-contract RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/TouchPadAuthoringStateSpec.cpp
        src/core/scene/TouchPadAuthoringState.h
    LIBS Qt6::Core Qt6::Gui
    INCLUDES src
)
target_compile_definitions(touch_pad_authoring_state_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(bass_preview_retained_state_spec
    OWNER src/audio
    CONTRACT preview.bass-preview-retained-state
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/BassPreviewRetainedStateSpec.cpp
        src/audio/BassPreviewRetainedState.h
        src/audio/PreviewAudioBackend.h
    LIBS Qt6::Core
    INCLUDES src src/common src/preview src/audio
)

miacode_add_spec(bass_preview_debug_log_routing_spec
    OWNER src/audio
    CONTRACT preview.bass-preview-debug-log-routing
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/BassPreviewDebugLogRoutingSpec.cpp
        src/audio/BassPreviewDebugLogRouting.h
    LIBS Qt6::Core
    INCLUDES src src/common src/preview src/audio
)

miacode_add_spec(preview_bass_device_lease_spec
    OWNER src/audio
    CONTRACT preview.preview-bass-device-lease
    DOMAIN preview KIND integration RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewBassDeviceLeaseSpec.cpp
        src/audio/PreviewBassDeviceLease.h
        src/audio/PreviewBassDeviceLease.cpp
    LIBS Qt6::Core
    INCLUDES src src/audio
)

miacode_add_spec(preview_audio_health_spec
    OWNER src/audio
    CONTRACT preview.preview-audio-health
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewAudioHealthSpec.cpp
        src/audio/PreviewAudioHealth.h
    LIBS Qt6::Core
    INCLUDES src src/common src/preview src/audio
)

miacode_add_spec(preview_audio_device_change_policy_spec
    OWNER src/audio
    CONTRACT preview.preview-audio-device-change-policy
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewAudioDeviceChangePolicySpec.cpp
        src/audio/PreviewAudioDeviceChangePolicy.h
    LIBS Qt6::Core
    INCLUDES src src/common src/preview src/audio
)

miacode_add_spec(bass_preview_sfx_scheduler_policy_spec
    OWNER src/audio
    CONTRACT preview.bass-preview-sfx-scheduler-policy
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(preview_media_cache_stamp_spec
    OWNER src/common
    CONTRACT preview.preview-media-cache-stamp
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PreviewMediaCacheStampSpec.cpp
        src/common/FileContentStamp.h
        src/common/ChartAssetPaths.h
    LIBS Qt6::Core
    INCLUDES src src/common
)

miacode_add_spec(pv_memory_diagnostics_spec
    OWNER src/preview/runtime
    CONTRACT preview.pv-memory-diagnostics
    DOMAIN preview KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PvMemoryDiagnosticsSpec.cpp
        src/preview/runtime/PvMemoryDiagnostics.h
        src/preview/runtime/PvMemoryDiagnostics.cpp
    LIBS Qt6::Core
    INCLUDES src src/preview src/preview/runtime
)
target_compile_definitions(pv_memory_diagnostics_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(pv_memory_host_contract_spec
    OWNER src/preview/runtime
    CONTRACT preview.pv-memory-host-contract
    DOMAIN preview KIND source-contract RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PvMemoryHostContractSpec.cpp
    LIBS Qt6::Core
    INCLUDES src src/preview src/preview/runtime
)
target_compile_definitions(pv_memory_host_contract_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

# Regression spec for the paused-seek media handshake surviving
# clearMedia() (chart/difficulty switch mid-seek) — see
# PausedSeekHandshakeSpec.cpp's header comment for the full story. Links
# the real PreviewStageMediaHost*.cpp TUs, built against the
# HAVE_QT_MULTIMEDIA (QMediaPlayer) backend branch rather than this
# machine's shipped MIACODE_USE_QTAVPLAYER backend, so the dev-tool link
# graph stays Qt-Multimedia-only instead of pulling in the vendored
# QtAVPlayer/FFmpeg sources. clearMedia() itself is not `#ifdef`-split
# between the two backends, so the bug under test is backend-independent.
miacode_add_spec(paused_seek_handshake_spec
    OWNER src/preview/runtime
    CONTRACT preview.paused-seek-handshake
    DOMAIN preview KIND integration RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/PausedSeekHandshakeSpec.cpp
        src/preview/runtime/PreviewStageMediaHost.h
        src/preview/runtime/PreviewStageMediaHost.cpp
        src/preview/runtime/PreviewStageMediaHost_Backend.cpp
        src/preview/runtime/PreviewStageMediaHost_Media.cpp
        src/preview/runtime/PreviewStageMediaHost_Playback.cpp
        src/preview/runtime/PreviewStageMediaHost_Diagnostics.cpp
        src/preview/runtime/PreviewStageMediaHost_Timeout.cpp
        src/preview/runtime/PreviewStageMediaHostInternal.h
        src/preview/runtime/PreviewSharedD3D11Device.h
        src/preview/runtime/PreviewSharedD3D11Device.cpp
        src/preview/runtime/PvMemoryDiagnostics.h
        src/preview/runtime/PvMemoryDiagnostics.cpp
        src/common/ChartAssetPaths.h
        src/common/DebugOptions.h
        src/common/FileContentStamp.h
        src/common/LayoutRingConfig.h
        src/common/LogEmissionPolicy.h
        src/common/PreviewVideoGeometryConfig.h
        src/common/ProcessDiagnostics.h
        src/common/ProcessDiagnostics.cpp
        src/core/video/PreviewEndOfMediaPolicy.h
        src/core/video/PreviewRenderSettings.h
        ${_miacode_log_core}
    LIBS Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Quick Qt6::Test
    INCLUDES src
)
target_compile_definitions(paused_seek_handshake_spec PRIVATE
    HAVE_QT_MULTIMEDIA=1
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(qtavplayer_platform_spec
    OWNER src/preview/runtime
    CONTRACT preview.qtavplayer-platform
    DOMAIN preview KIND source-contract RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/QtAVPlayerPlatformSpec.cpp
    LIBS Qt6::Core
    INCLUDES src src/preview src/preview/runtime
)
target_compile_definitions(qtavplayer_platform_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(quickshell_preview_surface_policy_spec
    OWNER src/app/quick_shell
    CONTRACT preview.quickshell-preview-surface-policy
    DOMAIN preview KIND source-contract RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/preview/QuickShellPreviewSurfacePolicySpec.cpp
        src/app/quick_shell/QuickShellPreviewSurfacePolicy.h
    LIBS Qt6::Core
    INCLUDES src src/app/quick_shell
)
target_compile_definitions(quickshell_preview_surface_policy_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
