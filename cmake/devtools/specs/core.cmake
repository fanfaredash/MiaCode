# Explicit spec targets; contract IDs stay stable across source/target renames.

# ---- Spec executables (registered with CTest) ----
miacode_add_spec(oplog_self_test
    OWNER src/common
    CONTRACT oplog.oplog
    DOMAIN oplog KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/oplog/OperationLogSpec.cpp
        src/common/CrashRecovery.h
        src/common/CrashRecovery.cpp
        ${_miacode_log_core}
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(simai_parser_spec
    OWNER src/core/chart/parser
    CONTRACT simai-parser.simai-parser
    DOMAIN simai_parser KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/simai_parser/SimaiParserSpec.cpp
        ${_miacode_chart_core}
        ${_miacode_log_core}
        resources/fonts.qrc
        resources/slide_data.qrc
    LIBS Qt6::Core Qt6::Gui Qt6::Widgets
    INCLUDES src src/app/ui src/core/chart src/core/chart/parser src/timeline
)

miacode_add_spec(chart_batch_transform_spec
    OWNER src/core/chart/transform
    CONTRACT chart-transform.chart-batch-transform
    DOMAIN chart_transform KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(chart_selection_beat_summary_spec
    OWNER src/core/chart/selection
    CONTRACT chart-selection.chart-selection-beat-summary
    DOMAIN chart_selection KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/chart_selection/ChartSelectionBeatSummarySpec.cpp
        src/core/chart/selection/ChartSelectionBeatSummary.h
        src/core/chart/selection/ChartSelectionBeatSummary.cpp
        src/core/chart/parser/SimaiCommentScan.h
        src/core/chart/parser/SimaiCommentScan.cpp
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(extension_manifest_spec
    OWNER src/extensions
    CONTRACT extensions.extension-manifest
    DOMAIN extensions KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/extensions/ExtensionManifestSpec.cpp
        src/extensions/ExtensionManifest.h
        src/extensions/ExtensionManifest.cpp
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(app_background_settings_spec
    OWNER src/app/ui
    CONTRACT ui.app-background-settings
    DOMAIN ui KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/ui/AppBackgroundSettingsSpec.cpp
        src/app/ui/preferences/AppBackgroundSettings.h
        src/app/ui/preferences/AppBackgroundSettings.cpp
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(theme_variant_resolver_spec
    OWNER src/app/ui
    CONTRACT ui.theme-variant-resolver
    DOMAIN ui KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/ui/ThemeVariantResolverSpec.cpp
        src/app/ui/theme/ThemeVariantResolver.h
        src/app/ui/theme/ThemeVariantResolver.cpp
        src/app/ui/preferences/PreferenceDocument.h
    LIBS Qt6::Core Qt6::Gui
    INCLUDES src
)

miacode_add_spec(extension_product_boundary_spec
    OWNER src/extensions
    CONTRACT extensions.extension-product-boundary
    DOMAIN extensions KIND boundary RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/extensions/ExtensionProductBoundarySpec.cpp
    LIBS Qt6::Core
    INCLUDES src
)
target_compile_definitions(extension_product_boundary_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(simai_document_spec
    OWNER src/core/chart/document
    CONTRACT chart-document.simai-document
    DOMAIN chart_document KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/chart_document/SimaiDocumentSpec.cpp
        src/core/chart/document/SimaiDocument.h
        src/core/chart/document/SimaiDocument.cpp
    LIBS Qt6::Core
    INCLUDES src src/core/chart/document
)

miacode_add_spec(cover_layout_model_spec
    OWNER src/tools/cover_export
    CONTRACT cover-export.cover-layout-model
    DOMAIN cover_export KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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
        src/app/ui/preferences/PreferenceDocument.h
        src/app/ui/preferences/PreferenceDocument.cpp
    LIBS Qt6::Core Qt6::Gui Qt6::Widgets
    INCLUDES src src/app/ui
)

miacode_add_spec(cover_frame_playback_controller_spec
    OWNER src/tools/cover_export
    CONTRACT cover-export.cover-frame-playback-controller
    DOMAIN cover_export KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/cover_export/CoverFramePlaybackControllerSpec.cpp
        src/tools/cover_export/CoverFramePlaybackController.h
        src/tools/cover_export/CoverFramePlaybackController.cpp
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(cover_frame_scene_binder_spec
    OWNER src/tools/cover_export
    CONTRACT cover-export.cover-frame-scene-binder
    DOMAIN cover_export KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/cover_export/CoverFrameSceneBinderSpec.cpp
        src/tools/cover_export/CoverFrameSceneBinder.h
        src/tools/cover_export/CoverFrameSceneBinder.cpp
    LIBS Qt6::Core Qt6::Gui
    INCLUDES src
)

miacode_add_spec(muri_spec
    OWNER src/tools/muri
    CONTRACT muri.muri
    DOMAIN muri KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(touch_pad_authoring_edit_spec
    OWNER src/editor
    CONTRACT editor.touch-pad-authoring-edit
    DOMAIN editor KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/editor/TouchPadAuthoringEditSpec.cpp
        src/editor/TouchPadAuthoringEdit.h
        src/editor/TouchPadAuthoringEdit.cpp
        src/core/chart/parser/SimaiCommentScan.h
        src/core/chart/parser/SimaiCommentScan.cpp
    LIBS Qt6::Core Qt6::Gui
    INCLUDES src src/editor
)

miacode_add_spec(simai_completion_catalog_spec
    OWNER src/editor
    CONTRACT editor.simai-completion-catalog
    DOMAIN editor KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/editor/SimaiCompletionCatalogSpec.cpp
        src/editor/SimaiCompletionCatalog.h
        src/editor/SimaiCompletionCatalog.cpp
    LIBS Qt6::Core
    INCLUDES src src/editor
)

miacode_add_spec(simai_text_edit_policy_spec
    OWNER src/editor
    CONTRACT editor.simai-text-edit-policy
    DOMAIN editor KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/editor/SimaiTextEditPolicySpec.cpp
        src/editor/SimaiTextEditPolicy.h
        src/editor/SimaiTextEditPolicy.cpp
        src/editor/SimaiCompletionCatalog.h
        src/editor/SimaiCompletionCatalog.cpp
    LIBS Qt6::Core
    INCLUDES src src/editor
)

miacode_add_spec(video_export_runtime_policy_spec
    OWNER src/tools/video_export
    CONTRACT video-export.video-export-runtime-policy
    DOMAIN video_export KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/video_export/VideoExportRuntimePolicySpec.cpp
        src/tools/video_export/VideoExportRuntimePolicy.h
        src/tools/video_export/VideoExportRuntimePolicy.cpp
    LIBS Qt6::Core
    INCLUDES src src/tools
)

miacode_add_spec(raw_video_pipe_frame_conservation_spec
    OWNER src/tools/video_export
    CONTRACT video-export.raw-video-pipe-frame-conservation
    DOMAIN video_export KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/video_export/RawVideoPipeFrameConservationSpec.cpp
        src/tools/video_export/RawVideoPipeTransport.h
        src/tools/video_export/RawVideoPipeTransport.cpp
    LIBS Qt6::Core Qt6::Gui
    INCLUDES src src/tools src/tools/video_export
)

miacode_add_spec(video_export_pending_frame_redraw_spec
    OWNER src/tools/video_export
    CONTRACT video-export.video-export-pending-frame-redraw
    DOMAIN video_export KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/video_export/VideoExportPendingFrameRedrawSpec.cpp
        src/tools/video_export/VideoExportPendingFrameRedraw.h
    LIBS Qt6::Core
    INCLUDES src src/tools src/tools/video_export
)

miacode_add_spec(video_export_intro_mode_spec
    OWNER src/tools/video_export
    CONTRACT video-export.video-export-intro-mode
    DOMAIN video_export KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/video_export/VideoExportIntroModeSpec.cpp
        src/tools/video_export/VideoExportController.h
        src/timeline/TimelineData.h
    LIBS Qt6::Core Qt6::Gui
    INCLUDES src src/common src/audio src/core/video src/timeline src/tools src/tools/video_export
)

miacode_add_spec(video_export_intro_sound_spec
    OWNER src/tools/video_export
    CONTRACT video-export.video-export-intro-sound
    DOMAIN video_export KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(video_export_audio_render_plan_spec
    OWNER src/tools/video_export
    CONTRACT video-export.video-export-audio-render-plan
    DOMAIN video_export KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(video_export_media_timeline_spec
    OWNER src/tools/video_export
    CONTRACT video-export.video-export-media-timeline
    DOMAIN video_export KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/video_export/VideoExportMediaTimelineSpec.cpp
        src/tools/video_export/VideoExportMediaTimeline.h
        src/tools/video_export/VideoExportMediaTimeline.cpp
        src/common/PreviewSfxTimeline.h
    LIBS Qt6::Core Qt6::Gui
    INCLUDES src src/common src/timeline src/tools src/tools/video_export
)

miacode_add_spec(chart_zip_packager_spec
    OWNER src/tools/zip_export
    CONTRACT zip-export.chart-zip-packager
    DOMAIN zip_export KIND integration RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(net_client_spec
    OWNER src/tools/net
    CONTRACT net.net-client
    DOMAIN net KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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
        src/app/ui/preferences/PreferenceDocument.h
        src/app/ui/preferences/PreferenceDocument.cpp
        src/common/ChartAssetPaths.h
    LIBS Qt6::Core Qt6::Network miniz
    INCLUDES src src/common src/tools src/app/ui
)

miacode_add_spec(pv_compression_policy_spec
    OWNER src/tools/media
    CONTRACT media.pv-compression-policy
    DOMAIN media KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/media/PvCompressionPolicySpec.cpp
        src/tools/media/PvCompressionPolicy.h
        src/tools/media/PvCompressionPolicy.cpp
    LIBS Qt6::Core
    INCLUDES src
)

# Drift guard for docs/ops/DEPENDENCY_ALLOWLIST.md (stage 3.5, item 4).
# Parses every target_link_libraries(MiaCode …) call plus the doc's
# allow/forbid/media-adapter tables, so an undocumented dependency, a stale
# doc row, a forbidden link (Qt6::Network today, Qt6::Widgets after the
# product source migration),
# an unpinned Qt version, or QtAVPlayer headers leaking outside the media
# adapter layer all fail the build's test suite.
miacode_add_spec(dependency_allowlist_spec
    OWNER src/common
    CONTRACT deps.dependency-allowlist
    DOMAIN deps KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/deps/DependencyAllowlistSpec.cpp
    LIBS Qt6::Core
)
target_compile_definitions(dependency_allowlist_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

# Drift guard for ID-based QTranslator catalogs (.ts parity + source literal ids).
miacode_add_spec(ui_text_locale_spec
    OWNER src/app/ui
    CONTRACT ui-text.ui-text-locale
    DOMAIN ui_text KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/ui_text/UiTextLocaleSpec.cpp
        src/app/ui/preferences/PreferenceDocument.h
        src/app/ui/preferences/PreferenceDocument.cpp
    LIBS Qt6::Core
    INCLUDES src src/app/ui
)
target_compile_definitions(ui_text_locale_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\""
    "MIACODE_EN_US_QM_PATH=\"${CMAKE_CURRENT_BINARY_DIR}/en_US.qm\"")
add_dependencies(ui_text_locale_spec miacode_lrelease)

miacode_add_spec(native_chrome_policy_spec
    OWNER src/app/ui
    CONTRACT ui.native-chrome-policy
    DOMAIN ui KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/ui/NativeChromePolicySpec.cpp
        src/app/quick_shell/QuickShellPopupPosition.h
        src/app/ui/chrome/NativeWindowThemePolicy.h
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(ui_text_preferences_spec
    OWNER src/app/ui
    CONTRACT ui-text.ui-text-preferences
    DOMAIN ui_text KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/ui_text/UiTextPreferencesSpec.cpp
        src/app/ui/preferences/PreferenceDocument.h
        src/app/ui/preferences/PreferenceDocument.cpp
    LIBS Qt6::Core
    INCLUDES src src/app/ui
)
