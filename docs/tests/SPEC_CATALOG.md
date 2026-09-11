# Spec catalog

Generated from `cmake/devtools/specs/` by `cmake -P cmake/devtools/SpecCatalog.cmake`.
Check without writing: `cmake -DMIACODE_SPEC_CATALOG_CHECK=ON -P cmake/devtools/SpecCatalog.cmake`.

116 independent specs; source lists and link dependencies are maintained only in CMake.
All existing assertions and target/CTest names are retained. No bundles or retirements.

`platform:all` means the target is registered on every platform, not that all platforms
have been tested. Platform-specific source branches and link additions remain in their manifests.
`blocked-link` records the existing PlaybackCoordinator construction experiment: it is
`EXCLUDE_FROM_ALL`, has no CTest entry, and its recorded source closure does not link.
Its `compile-only` execution field does not claim a successful compile/link validation.
Contract IDs are stable identifiers: keep them when renaming a source or target.
Owners name production modules; kinds describe the checked boundary, not runtime diagnostics.

| Source | Target / CTest name | Owner | Contract | Domain | Kind | Risk | Platform | Execution | Status |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `src/tools/chart_document/SimaiDocumentSpec.cpp` | `simai_document_spec` | `src/core/chart/document` | `chart-document.simai-document` | chart_document | behavior | normal | all | ctest | active |
| `src/tools/chart_selection/ChartSelectionBeatSummarySpec.cpp` | `chart_selection_beat_summary_spec` | `src/core/chart/selection` | `chart-selection.chart-selection-beat-summary` | chart_selection | behavior | normal | all | ctest | active |
| `src/tools/chart_transform/ChartBatchTransformSpec.cpp` | `chart_batch_transform_spec` | `src/core/chart/transform` | `chart-transform.chart-batch-transform` | chart_transform | behavior | normal | all | ctest | active |
| `src/tools/cover_export/CoverFramePlaybackControllerSpec.cpp` | `cover_frame_playback_controller_spec` | `src/tools/cover_export` | `cover-export.cover-frame-playback-controller` | cover_export | behavior | normal | all | ctest | active |
| `src/tools/cover_export/CoverFrameSceneBinderSpec.cpp` | `cover_frame_scene_binder_spec` | `src/tools/cover_export` | `cover-export.cover-frame-scene-binder` | cover_export | behavior | normal | all | ctest | active |
| `src/tools/cover_export/CoverLayoutModelSpec.cpp` | `cover_layout_model_spec` | `src/tools/cover_export` | `cover-export.cover-layout-model` | cover_export | behavior | normal | all | ctest | active |
| `src/tools/debug_index/DebugFlagIndexSpec.cpp` | `debug_flag_index_spec` | `src/common` | `debug-index.debug-flag-index` | debug_index | source-contract | normal | all | ctest | active |
| `src/tools/debug_index/DebugOptionsSpec.cpp` | `debug_options_spec` | `src/common` | `debug-index.debug-options` | debug_index | behavior | normal | all | ctest | active |
| `src/tools/debug_index/IdleFreezeReproScriptSpec.cpp` | `idle_freeze_repro_script_spec` | `scripts/debug` | `debug-index.idle-freeze-repro-script` | debug_index | source-contract | normal | all | ctest | active |
| `src/tools/debug_index/LogPruningPolicySpec.cpp` | `log_pruning_policy_spec` | `src/common` | `debug-index.log-pruning-policy` | debug_index | behavior | normal | all | ctest | active |
| `src/tools/debug_index/ProcessDiagnosticsSpec.cpp` | `process_diagnostics_spec` | `src/common` | `debug-index.process-diagnostics` | debug_index | behavior | normal | all | ctest | active |
| `src/tools/debug_index/ProcessIdentityFieldsSpec.cpp` | `process_identity_fields_spec` | `src/app` | `debug-index.process-identity-fields` | debug_index | behavior | normal | all | ctest | active |
| `src/tools/debug_index/UiHangWatchdogLifecycleSpec.cpp` | `ui_hang_watchdog_lifecycle_spec` | `src/common` | `debug-index.ui-hang-watchdog-lifecycle` | debug_index | behavior | high | all | ctest | active |
| `src/tools/debug_index/UiHangWatchdogPolicySpec.cpp` | `ui_hang_watchdog_policy_spec` | `src/common` | `debug-index.ui-hang-watchdog-policy` | debug_index | behavior | normal | all | ctest | active |
| `src/tools/debug_index/WindowVisibilityDiagnosticsSpec.cpp` | `window_visibility_diagnostics_spec` | `src/app` | `debug-index.window-visibility-diagnostics` | debug_index | behavior | normal | all | ctest | active |
| `src/tools/deps/DependencyAllowlistSpec.cpp` | `dependency_allowlist_spec` | `src/common` | `deps.dependency-allowlist` | deps | source-contract | normal | all | ctest | active |
| `src/tools/editor/SimaiCompletionCatalogSpec.cpp` | `simai_completion_catalog_spec` | `src/editor` | `editor.simai-completion-catalog` | editor | behavior | normal | all | ctest | active |
| `src/tools/editor/SimaiTextEditPolicySpec.cpp` | `simai_text_edit_policy_spec` | `src/editor` | `editor.simai-text-edit-policy` | editor | behavior | normal | all | ctest | active |
| `src/tools/editor/TouchPadAuthoringEditSpec.cpp` | `touch_pad_authoring_edit_spec` | `src/editor` | `editor.touch-pad-authoring-edit` | editor | behavior | normal | all | ctest | active |
| `src/tools/extensions/ExtensionManifestSpec.cpp` | `extension_manifest_spec` | `src/extensions` | `extensions.extension-manifest` | extensions | behavior | normal | all | ctest | active |
| `src/tools/extensions/ExtensionProductBoundarySpec.cpp` | `extension_product_boundary_spec` | `src/extensions` | `extensions.extension-product-boundary` | extensions | boundary | normal | all | ctest | active |
| `src/tools/media/PvCompressionPolicySpec.cpp` | `pv_compression_policy_spec` | `src/tools/media` | `media.pv-compression-policy` | media | behavior | normal | all | ctest | active |
| `src/tools/muri/MuriSpec.cpp` | `muri_spec` | `src/tools/muri` | `muri.muri` | muri | behavior | normal | all | ctest | active |
| `src/tools/net/NetClientSpec.cpp` | `net_client_spec` | `src/tools/net` | `net.net-client` | net | behavior | normal | all | ctest | active |
| `src/tools/oplog/OperationLogSpec.cpp` | `oplog_self_test` | `src/common` | `oplog.oplog` | oplog | behavior | normal | all | ctest | active |
| `src/tools/preview/BassPreviewDebugLogRoutingSpec.cpp` | `bass_preview_debug_log_routing_spec` | `src/audio` | `preview.bass-preview-debug-log-routing` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/BassPreviewRetainedStateSpec.cpp` | `bass_preview_retained_state_spec` | `src/audio` | `preview.bass-preview-retained-state` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/BassPreviewSfxSchedulerPolicySpec.cpp` | `bass_preview_sfx_scheduler_policy_spec` | `src/audio` | `preview.bass-preview-sfx-scheduler-policy` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PausedSeekHandshakeSpec.cpp` | `paused_seek_handshake_spec` | `src/preview/runtime` | `preview.paused-seek-handshake` | preview | integration | high | all | ctest | active |
| `src/tools/preview/PreviewAudioCommandQueueSpec.cpp` | `preview_audio_command_queue_spec` | `src/audio` | `preview.preview-audio-command-queue` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewAudioDeviceChangePolicySpec.cpp` | `preview_audio_device_change_policy_spec` | `src/audio` | `preview.preview-audio-device-change-policy` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewAudioHealthSpec.cpp` | `preview_audio_health_spec` | `src/audio` | `preview.preview-audio-health` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewAudioNonGuiBarrierSpec.cpp` | `preview_audio_non_gui_barrier_spec` | `src/audio` | `preview.preview-audio-non-gui-barrier` | preview | integration | high | all | ctest | active |
| `src/tools/preview/PreviewAudioOutputGlitchProbeSpec.cpp` | `preview_audio_output_glitch_probe_spec` | `src/audio` | `preview.preview-audio-output-glitch-probe` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewAudioPlaybackFlowPolicySpec.cpp` | `preview_audio_playback_flow_policy_spec` | `src/audio` | `preview.preview-audio-playback-flow-policy` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewAudioSettingsSpec.cpp` | `preview_audio_settings_spec` | `src/audio` | `preview.preview-audio-settings` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewAudioWorkerProtocolSpec.cpp` | `preview_audio_worker_protocol_spec` | `src/audio` | `preview.preview-audio-worker-protocol` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewAudioWorkerSpec.cpp` | `preview_audio_worker_spec` | `src/audio` | `preview.preview-audio-worker` | preview | integration | high | all | ctest | active |
| `src/tools/preview/PreviewBassDeviceLeaseSpec.cpp` | `preview_bass_device_lease_spec` | `src/audio` | `preview.preview-bass-device-lease` | preview | integration | high | all | ctest | active |
| `src/tools/preview/PreviewEndOfMediaPolicySpec.cpp` | `preview_end_of_media_policy_spec` | `src/core/video` | `preview.preview-end-of-media-policy` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewFireworkLifecycleSpec.cpp` | `preview_firework_lifecycle_spec` | `src/core/scene` | `preview.preview-firework-lifecycle` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewFireworkWarmupPolicySpec.cpp` | `preview_firework_warmup_policy_spec` | `src/core/scene` | `preview.preview-firework-warmup-policy` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewGuideLayerSpec.cpp` | `preview_guide_layer_spec` | `src/core/scene` | `preview.preview-guide-layer` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewHeadLayerSpec.cpp` | `preview_head_layer_spec` | `src/core/scene` | `preview.preview-head-layer` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewMediaCacheStampSpec.cpp` | `preview_media_cache_stamp_spec` | `src/common` | `preview.preview-media-cache-stamp` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewQuickSpriteBatchSpec.cpp` | `preview_quick_sprite_batch_spec` | `src/preview/quick_scene` | `preview.preview-quick-sprite-batch` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewRealtimeObjectHotPathSpec.cpp` | `preview_realtime_object_hot_path_spec` | `src/core/scene` | `preview.preview-realtime-object-hot-path` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewSceneAssetLoaderSpec.cpp` | `preview_asset_loader_spec` | `src/preview/runtime` | `preview.preview-asset-loader` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewSfxTimelineSpec.cpp` | `preview_sfx_timeline_spec` | `src/common` | `preview.preview-sfx-timeline` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewSlideEraseByAreaSpec.cpp` | `preview_slide_erase_by_area_spec` | `src/core/scene` | `preview.preview-slide-erase-by-area` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PreviewTextureGenerationPolicySpec.cpp` | `preview_texture_generation_policy_spec` | `src/preview/quick_scene` | `preview.preview-texture-generation-policy` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PvMemoryDiagnosticsSpec.cpp` | `pv_memory_diagnostics_spec` | `src/preview/runtime` | `preview.pv-memory-diagnostics` | preview | behavior | high | all | ctest | active |
| `src/tools/preview/PvMemoryHostContractSpec.cpp` | `pv_memory_host_contract_spec` | `src/preview/runtime` | `preview.pv-memory-host-contract` | preview | source-contract | high | all | ctest | active |
| `src/tools/preview/QtAVPlayerPlatformSpec.cpp` | `qtavplayer_platform_spec` | `src/preview/runtime` | `preview.qtavplayer-platform` | preview | source-contract | high | all | ctest | active |
| `src/tools/preview/QuickShellPreviewSurfacePolicySpec.cpp` | `quickshell_preview_surface_policy_spec` | `src/app/quick_shell` | `preview.quickshell-preview-surface-policy` | preview | source-contract | high | all | ctest | active |
| `src/tools/preview/TouchPadAuthoringStateSpec.cpp` | `touch_pad_authoring_state_spec` | `src/core/scene` | `preview.touch-pad-authoring-state` | preview | source-contract | high | all | ctest | active |
| `src/tools/ui/PreviewTransportPushSpec.cpp` | `preview_transport_push_spec` | `src/app/ui` | `qml-ui.preview-transport-push` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/QmlAnalysisModelSpec.cpp` | `qml_analysis_model_spec` | `src/app/ui` | `qml-ui.qml-analysis-model` | qml_ui | behavior | normal | all | ctest | active |
| `src/tools/ui/QmlAppBackgroundModelSpec.cpp` | `qml_app_background_model_spec` | `src/app/ui` | `qml-ui.qml-app-background-model` | qml_ui | behavior | normal | all | ctest | active |
| `src/tools/ui/QmlChartDropBridgeSpec.cpp` | `qml_chart_drop_bridge_spec` | `src/app/ui` | `qml-ui.qml-chart-drop-bridge` | qml_ui | integration | normal | all | ctest | active |
| `src/tools/ui/QmlCoverExportContractSpec.cpp` | `qml_cover_export_contract_spec` | `src/app/ui` | `qml-ui.qml-cover-export-contract` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/QmlDocumentLifecycleContractSpec.cpp` | `qml_document_lifecycle_contract_spec` | `src/app/ui` | `qml-ui.qml-document-lifecycle-contract` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/QmlDocumentProjectionSpec.cpp` | `qml_document_projection_spec` | `src/app/ui` | `qml-ui.qml-document-projection` | qml_ui | behavior | normal | all | ctest | active |
| `src/tools/ui/QmlDocumentReplacementSequenceSpec.cpp` | `qml_document_replacement_sequence_spec` | `src/app/ui` | `qml-ui.qml-document-replacement-sequence` | qml_ui | integration | normal | all | ctest | active |
| `src/tools/ui/QmlEditorControllerSpec.cpp` | `qml_editor_controller_spec` | `src/app/ui` | `qml-ui.qml-editor-controller` | qml_ui | integration | normal | all | ctest | active |
| `src/tools/ui/QmlExportFontContractSpec.cpp` | `qml_export_font_contract_spec` | `src/app/ui` | `qml-ui.qml-export-font-contract` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/QmlExportIntroSoundContractSpec.cpp` | `qml_export_intro_sound_contract_spec` | `src/app/ui` | `qml-ui.qml-export-intro-sound-contract` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/QmlExportVideoPageSpec.cpp` | `qml_export_video_page_spec` | `src/app/ui` | `qml-ui.qml-export-video-page` | qml_ui | integration | normal | all | ctest | active |
| `src/tools/ui/QmlMainMenuSpec.cpp` | `qml_main_menu_spec` | `src/app/ui` | `qml-ui.qml-main-menu` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/QmlPreviewRateFeedbackSpec.cpp` | `qml_preview_rate_feedback_spec` | `src/app/ui` | `qml-ui.qml-preview-rate-feedback` | qml_ui | behavior | high | all | ctest | active |
| `src/tools/ui/QmlPreviewRateSpec.cpp` | `qml_preview_rate_spec` | `src/app/ui` | `qml-ui.qml-preview-rate` | qml_ui | integration | normal | all | ctest | active |
| `src/tools/ui/QmlSelectionRangeExportContractSpec.cpp` | `qml_selection_range_export_contract_spec` | `src/app/ui` | `qml-ui.qml-selection-range-export-contract` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/QmlShortcutBindingSpec.cpp` | `qml_shortcut_binding_spec` | `src/app/ui` | `qml-ui.qml-shortcut-binding` | qml_ui | integration | normal | all | ctest | active |
| `src/tools/ui/QmlUiBackendSurfaceSpec.cpp` | `qml_ui_backend_surface_spec` | `src/app/ui` | `qml-ui.qml-ui-backend-surface` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/QmlUiBootstrapLifecycleSpec.cpp` | `qml_ui_bootstrap_lifecycle_spec` | `src/app/ui` | `qml-ui.qml-ui-bootstrap-lifecycle` | qml_ui | behavior | normal | all | ctest | active |
| `src/tools/ui/QmlUiThemeContractSpec.cpp` | `qml_ui_theme_contract_spec` | `src/app/ui` | `qml-ui.qml-ui-theme-contract` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/TimelineSurfaceReadySpec.cpp` | `timeline_surface_ready_spec` | `src/app/ui` | `qml-ui.timeline-surface-ready` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/ui/V1ShellRemovalSpec.cpp` | `v1_shell_removal_spec` | `src/app/ui` | `qml-ui.v1-shell-removal` | qml_ui | source-contract | normal | all | ctest | active |
| `src/tools/simai_parser/SimaiParserSpec.cpp` | `simai_parser_spec` | `src/core/chart/parser` | `simai-parser.simai-parser` | simai_parser | behavior | normal | all | ctest | active |
| `src/tools/timeline/TimelineCadenceArbitrationPolicySpec.cpp` | `timeline_cadence_arbitration_policy_spec` | `src/timeline` | `timeline.timeline-cadence-arbitration-policy` | timeline | behavior | normal | all | ctest | active |
| `src/tools/timeline/TimelineMarkerOffsetSpec.cpp` | `timeline_marker_offset_spec` | `src/timeline` | `timeline.timeline-marker-offset` | timeline | behavior | normal | all | ctest | active |
| `src/tools/timeline/TimelineModelSpec.cpp` | `timeline_model_spec` | `src/timeline` | `timeline.timeline-model` | timeline | integration | normal | all | ctest | active |
| `src/tools/timeline/TimelineQuickTextureCachePolicySpec.cpp` | `timeline_quick_texture_cache_policy_spec` | `src/timeline` | `timeline.timeline-quick-texture-cache-policy` | timeline | behavior | normal | all | ctest | active |
| `src/tools/ui/AppBackgroundSettingsSpec.cpp` | `app_background_settings_spec` | `src/app/ui` | `ui.app-background-settings` | ui | behavior | normal | all | ctest | active |
| `src/tools/ui/NativeChromePolicySpec.cpp` | `native_chrome_policy_spec` | `src/app/ui` | `ui.native-chrome-policy` | ui | behavior | normal | all | ctest | active |
| `src/tools/ui/ThemeVariantResolverSpec.cpp` | `theme_variant_resolver_spec` | `src/app/ui` | `ui.theme-variant-resolver` | ui | behavior | normal | all | ctest | active |
| `src/tools/ui_text/UiTextLocaleSpec.cpp` | `ui_text_locale_spec` | `src/app/ui` | `ui-text.ui-text-locale` | ui_text | behavior | normal | all | ctest | active |
| `src/tools/ui_text/UiTextPreferencesSpec.cpp` | `ui_text_preferences_spec` | `src/app/ui` | `ui-text.ui-text-preferences` | ui_text | behavior | normal | all | ctest | active |
| `src/tools/services/AnalysisServiceSpec.cpp` | `analysis_service_spec` | `src/app/services` | `v2.analysis-service` | v2 | integration | high | all | ctest | active |
| `src/tools/services/ApplicationServicesSpec.cpp` | `application_services_spec` | `src/app/services` | `v2.application-services` | v2 | integration | high | all | ctest | active |
| `src/tools/services/ChartMediaServiceSpec.cpp` | `chart_media_service_spec` | `src/app/services` | `v2.chart-media-service` | v2 | behavior | high | all | ctest | active |
| `src/tools/services/ChartWorkspaceFileServiceSpec.cpp` | `chart_workspace_file_service_spec` | `src/app/services` | `v2.chart-workspace-file-service` | v2 | behavior | high | all | ctest | active |
| `src/tools/services/ChartWorkspaceSpec.cpp` | `chart_workspace_spec` | `src/app/services` | `v2.chart-workspace` | v2 | behavior | high | all | ctest | active |
| `src/tools/services/DocumentPortSpec.cpp` | `document_port_spec` | `src/app/services` | `v2.document-port` | v2 | boundary | high | all | ctest | active |
| `src/tools/services/EditorPageRouterSpec.cpp` | `editor_page_router_spec` | `src/app/services` | `v2.editor-page-router` | v2 | integration | high | all | ctest | active |
| `src/tools/services/EditorSyncControllerSpec.cpp` | `editor_sync_controller_spec` | `src/app/services` | `v2.editor-sync-controller` | v2 | integration | high | all | ctest | active |
| `src/tools/services/ExportEngineSpec.cpp` | `export_engine_spec` | `src/app/services` | `v2.export-engine` | v2 | integration | high | all | ctest | active |
| `src/tools/services/JobProgressServiceSpec.cpp` | `job_progress_service_spec` | `src/app/services` | `v2.job-progress-service` | v2 | behavior | high | all | ctest | active |
| `src/tools/services/PlaybackCoordinatorConstructionSpec.cpp` | `playback_coordinator_construction_spec` | `src/app/runtime` | `v2.playback-coordinator-construction` | v2 | boundary | high | all | compile-only | blocked-link |
| `src/tools/services/PlaybackCoordinatorSpec.cpp` | `playback_coordinator_spec` | `src/app/runtime` | `v2.playback-coordinator` | v2 | source-contract | high | all | ctest | active |
| `src/tools/services/PlaybackStateAuthoritySpec.cpp` | `playback_state_authority_spec` | `src/app/services` | `v2.playback-state-authority` | v2 | behavior | high | all | ctest | active |
| `src/tools/services/PlaybackStorageBoundarySpec.cpp` | `playback_storage_boundary_spec` | `src/app/runtime` | `v2.playback-storage-boundary` | v2 | boundary | high | all | ctest | active |
| `src/tools/services/PreferencesPortSpec.cpp` | `preferences_port_spec` | `src/app/services` | `v2.preferences-port` | v2 | boundary | high | all | ctest | active |
| `src/tools/services/PreviewAppearanceStateSpec.cpp` | `preview_appearance_state_spec` | `src/app/services` | `v2.preview-appearance-state` | v2 | behavior | high | all | ctest | active |
| `src/tools/services/PreviewHostSpec.cpp` | `preview_host_spec` | `src/app/runtime` | `v2.preview-host` | v2 | source-contract | high | all | ctest | active |
| `src/tools/services/PreviewPortSpec.cpp` | `preview_port_spec` | `src/app/services` | `v2.preview-port` | v2 | boundary | high | all | ctest | active |
| `src/tools/services/RuntimeContextBoundarySpec.cpp` | `runtime_context_boundary_spec` | `src/app/runtime` | `v2.runtime-context-boundary` | v2 | boundary | high | all | ctest | active |
| `src/tools/services/TimelineHostSpec.cpp` | `timeline_host_spec` | `src/app/runtime` | `v2.timeline-host` | v2 | behavior | high | all | ctest | active |
| `src/tools/services/UiRequestServiceSpec.cpp` | `ui_request_service_spec` | `src/app/services` | `v2.ui-request-service` | v2 | behavior | high | all | ctest | active |
| `src/tools/services/ValidationPortSpec.cpp` | `validation_port_spec` | `src/app/services` | `v2.validation-port` | v2 | boundary | high | all | ctest | active |
| `src/tools/video_export/VideoExportAudioRenderPlanSpec.cpp` | `video_export_audio_render_plan_spec` | `src/tools/video_export` | `video-export.video-export-audio-render-plan` | video_export | behavior | high | all | ctest | active |
| `src/tools/video_export/VideoExportIntroModeSpec.cpp` | `video_export_intro_mode_spec` | `src/tools/video_export` | `video-export.video-export-intro-mode` | video_export | behavior | high | all | ctest | active |
| `src/tools/video_export/VideoExportIntroSoundSpec.cpp` | `video_export_intro_sound_spec` | `src/tools/video_export` | `video-export.video-export-intro-sound` | video_export | behavior | high | all | ctest | active |
| `src/tools/video_export/VideoExportMediaTimelineSpec.cpp` | `video_export_media_timeline_spec` | `src/tools/video_export` | `video-export.video-export-media-timeline` | video_export | behavior | high | all | ctest | active |
| `src/tools/video_export/VideoExportRuntimePolicySpec.cpp` | `video_export_runtime_policy_spec` | `src/tools/video_export` | `video-export.video-export-runtime-policy` | video_export | behavior | high | all | ctest | active |
| `src/tools/zip_export/ChartZipPackagerSpec.cpp` | `chart_zip_packager_spec` | `src/tools/zip_export` | `zip-export.chart-zip-packager` | zip_export | integration | normal | all | ctest | active |
