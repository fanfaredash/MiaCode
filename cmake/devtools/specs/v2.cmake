# Explicit spec targets; contract IDs stay stable across source/target renames.

miacode_add_spec(chart_workspace_spec
    OWNER src/app/v2
    CONTRACT v2.chart-workspace
    DOMAIN v2 KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/v2/ChartWorkspaceSpec.cpp
        ${_miacode_chart_core}
        ${_miacode_log_core}
        src/app/v2/ChartWorkspace.cpp
        src/app/v2/ChartWorkspace.h
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(chart_media_service_spec
    OWNER src/app/v2
    CONTRACT v2.chart-media-service
    DOMAIN v2 KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/v2/ChartMediaServiceSpec.cpp
        src/app/v2/ChartMediaService.cpp
        src/app/v2/ChartMediaService.h
        src/common/ChartMediaImport.cpp
        src/common/ChartMediaImport.h
    LIBS Qt6::Core Qt6::Gui
    INCLUDES src
)

miacode_add_spec(chart_workspace_file_service_spec
    OWNER src/app/v2
    CONTRACT v2.chart-workspace-file-service
    DOMAIN v2 KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(ui_request_service_spec
    OWNER src/app/v2
    CONTRACT v2.ui-request-service
    DOMAIN v2 KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/v2/UiRequestServiceSpec.cpp
        src/app/v2/UiRequestService.cpp
        src/app/v2/UiRequestService.h
    LIBS Qt6::Core Qt6::Test
    INCLUDES src
)

miacode_add_spec(editor_sync_controller_spec
    OWNER src/app/v2
    CONTRACT v2.editor-sync-controller
    DOMAIN v2 KIND integration RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/v2/EditorSyncControllerSpec.cpp
        src/app/v2/EditorSyncController.cpp
        src/app/v2/EditorSyncController.h
    LIBS Qt6::Core Qt6::Test
    INCLUDES src
)

miacode_add_spec(job_progress_service_spec
    OWNER src/app/v2
    CONTRACT v2.job-progress-service
    DOMAIN v2 KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/v2/JobProgressServiceSpec.cpp
        src/app/v2/JobProgressService.cpp
        src/app/v2/JobProgressService.h
    LIBS Qt6::Core Qt6::Test
    INCLUDES src
)

miacode_add_spec(analysis_service_spec
    OWNER src/app/v2
    CONTRACT v2.analysis-service
    DOMAIN v2 KIND integration RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(editor_page_router_spec
    OWNER src/app/v2
    CONTRACT v2.editor-page-router
    DOMAIN v2 KIND integration RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(export_engine_spec
    OWNER src/app/v2
    CONTRACT v2.export-engine
    DOMAIN v2 KIND integration RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(preview_appearance_state_spec
    OWNER src/app/v2
    CONTRACT v2.preview-appearance-state
    DOMAIN v2 KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(application_services_spec
    OWNER src/app/v2
    CONTRACT v2.application-services
    DOMAIN v2 KIND integration RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(preferences_port_spec
    OWNER src/app/v2
    CONTRACT v2.preferences-port
    DOMAIN v2 KIND boundary RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(validation_port_spec
    OWNER src/app/v2
    CONTRACT v2.validation-port
    DOMAIN v2 KIND boundary RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(document_port_spec
    OWNER src/app/v2
    CONTRACT v2.document-port
    DOMAIN v2 KIND boundary RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(preview_port_spec
    OWNER src/app/v2
    CONTRACT v2.preview-port
    DOMAIN v2 KIND boundary RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/v2/PreviewPortSpec.cpp
        src/app/v2/PlaybackPreviewPort.h
        src/core/video/PreviewRenderSettings.h
    LIBS Qt6::Core Qt6::Test
    INCLUDES src src/core/video
)

# Stage 4.9e-3: the coordinator's second playback contract, alongside
# PlaybackControl — non-command state writes rather than user transport
# commands (see PlaybackStateAuthority.h). Linking Core+Test only (no Gui)
# is the same link-time guarantee as the four port specs above.
miacode_add_spec(playback_state_authority_spec
    OWNER src/app/v2
    CONTRACT v2.playback-state-authority
    DOMAIN v2 KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/v2/PlaybackStateAuthoritySpec.cpp
        src/app/v2/PlaybackStateAuthority.h
    LIBS Qt6::Core Qt6::Test
    INCLUDES src
)

# Stage 4.6: Timeline commands receive their own identity before the
# compatibility host forwards them to the current composite implementation.
miacode_add_spec(timeline_host_spec
    OWNER src/app/runtime
    CONTRACT v2.timeline-host
    DOMAIN v2 KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(preview_host_spec
    OWNER src/app/runtime
    CONTRACT v2.preview-host
    DOMAIN v2 KIND source-contract RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
target_compile_definitions(preview_host_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

# Stage 4.8: the coordinator owns playback contracts; legacy Preview and
# Timeline surface compatibility lives in explicit projection adapters.
miacode_add_spec(playback_coordinator_spec
    OWNER src/app/runtime
    CONTRACT v2.playback-coordinator
    DOMAIN v2 KIND source-contract RISK high
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(playback_coordinator_construction_spec
    OWNER src/app/runtime
    CONTRACT v2.playback-coordinator-construction
    DOMAIN v2 KIND boundary RISK high
    EXECUTION compile-only STATUS blocked-link PLATFORM all
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
    LIBS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia Qt6::Quick
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
miacode_add_spec(runtime_context_boundary_spec
    OWNER src/app/runtime
    CONTRACT v2.runtime-context-boundary
    DOMAIN v2 KIND boundary RISK high
    EXECUTION ctest STATUS active PLATFORM all
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

# Stage 4.9e-4: same shape as runtime_context_boundary_spec above, but for
# the canonical playback-authority storage split (RuntimeContext::PlaybackState).
miacode_add_spec(playback_storage_boundary_spec
    OWNER src/app/runtime
    CONTRACT v2.playback-storage-boundary
    DOMAIN v2 KIND boundary RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/v2/PlaybackStorageBoundarySpec.cpp
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
