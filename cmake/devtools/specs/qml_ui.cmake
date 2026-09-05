# Explicit spec targets; contract IDs stay stable across source/target renames.

miacode_add_spec(qml_app_background_model_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-app-background-model
    DOMAIN qml_ui KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(qml_chart_drop_bridge_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-chart-drop-bridge
    DOMAIN qml_ui KIND integration RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlChartDropBridgeSpec.cpp
        src/app/qml_ui/drop/QmlChartDropBridge.h
        src/app/qml_ui/drop/QmlChartDropBridge.cpp
        src/app/v2/ChartDropImportService.h
        src/app/v2/ChartDropImportService.cpp
    LIBS Qt6::Core Qt6::Gui Qt6::Quick
    INCLUDES src
)

miacode_add_spec(qml_document_projection_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-document-projection
    DOMAIN qml_ui KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(qml_analysis_model_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-analysis-model
    DOMAIN qml_ui KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlAnalysisModelSpec.cpp
        src/app/v2/AnalysisService.h
        src/app/qml_ui/QmlAnalysisProjection.cpp
        src/app/qml_ui/QmlAnalysisProjection.h
        src/app/qml_ui/QmlDocumentProjection.h
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(qml_shortcut_binding_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-shortcut-binding
    DOMAIN qml_ui KIND integration RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(v1_shell_removal_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.v1-shell-removal
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/V1ShellRemovalSpec.cpp
    LIBS Qt6::Core
    INCLUDES src
)
target_compile_definitions(v1_shell_removal_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(preview_transport_push_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.preview-transport-push
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(timeline_surface_ready_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.timeline-surface-ready
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(qml_ui_bootstrap_lifecycle_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-ui-bootstrap-lifecycle
    DOMAIN qml_ui KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlUiBootstrapLifecycleSpec.cpp
        src/app/qml_ui/QmlUiRootLifecycle.h
    LIBS Qt6::Core
    INCLUDES src
)
target_compile_definitions(qml_ui_bootstrap_lifecycle_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(qml_document_lifecycle_contract_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-document-lifecycle-contract
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlDocumentLifecycleContractSpec.cpp
    LIBS Qt6::Core
    INCLUDES src
)
target_compile_definitions(qml_document_lifecycle_contract_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(qml_export_intro_sound_contract_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-export-intro-sound-contract
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlExportIntroSoundContractSpec.cpp
    LIBS Qt6::Core
    INCLUDES src
)
target_compile_definitions(qml_export_intro_sound_contract_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(qml_export_font_contract_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-export-font-contract
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlExportFontContractSpec.cpp
    LIBS Qt6::Core
    INCLUDES src
)
target_compile_definitions(qml_export_font_contract_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(qml_cover_export_contract_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-cover-export-contract
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlCoverExportContractSpec.cpp
    LIBS Qt6::Core
    INCLUDES src
)
target_compile_definitions(qml_cover_export_contract_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(qml_export_video_page_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-export-video-page
    DOMAIN qml_ui KIND integration RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlExportVideoPageSpec.cpp
    LIBS Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test
    INCLUDES src
)
target_compile_definitions(qml_export_video_page_spec PRIVATE
    "MIACODE_QML_SPEC_IMPORT_ROOT=\"${MIACODE_QML_SPEC_IMPORT_ROOT}\"")

miacode_add_spec(qml_main_menu_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-main-menu
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlMainMenuSpec.cpp
    LIBS Qt6::Core
    INCLUDES src
)
target_compile_definitions(qml_main_menu_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(qml_document_replacement_sequence_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-document-replacement-sequence
    DOMAIN qml_ui KIND integration RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlDocumentReplacementSequenceSpec.cpp
    LIBS Qt6::Core Qt6::Qml Qt6::Quick
    INCLUDES src
)
target_compile_definitions(qml_document_replacement_sequence_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

miacode_add_spec(qml_editor_controller_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-editor-controller
    DOMAIN qml_ui KIND integration RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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

# Ratchet for docs/specs/ui/QML_UI_V2_BACKEND_SURFACE_ZH.md (stage 3.5, item 2).
# Set-equality between the MainWindow surface src/app/qml_ui actually reaches
# and the inventory the doc lists, plus the friend grants and the recorded
# counts. New coupling fails; a migration that forgets the doc fails too,
# which is what keeps the remaining count honest and monotonic.
miacode_add_spec(qml_ui_backend_surface_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-ui-backend-surface
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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
miacode_add_spec(qml_ui_theme_contract_spec
    OWNER src/app/qml_ui
    CONTRACT qml-ui.qml-ui-theme-contract
    DOMAIN qml_ui KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/qml_ui/QmlUiThemeContractSpec.cpp
    LIBS Qt6::Core
)
target_compile_definitions(qml_ui_theme_contract_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
