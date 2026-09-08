# Explicit spec targets; contract IDs stay stable across source/target renames.

# Drift guard: every MIACODE_* env flag read in src/ must be documented in
# docs/ops/DEBUG_INDEX.md, and every flag the doc names must still be read in code
# (or be in the spec's retired allowlist). Reads the tree + doc from disk;
# the repo root is injected as a compile definition.
miacode_add_spec(debug_flag_index_spec
    OWNER src/common
    CONTRACT debug-index.debug-flag-index
    DOMAIN debug_index KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/debug_index/DebugFlagIndexSpec.cpp
    LIBS Qt6::Core
)
target_compile_definitions(debug_flag_index_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")

# Behavior spec locking DebugOptions.h accessor semantics (defaults, clamps,
# debug-category gating), the safety net for any future table-driven
# registry refactor.
miacode_add_spec(debug_options_spec
    OWNER src/common
    CONTRACT debug-index.debug-options
    DOMAIN debug_index KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/debug_index/DebugOptionsSpec.cpp
        src/common/DebugOptions.h
    LIBS Qt6::Core
    INCLUDES src src/common
)

miacode_add_spec(process_identity_fields_spec
    OWNER src/app
    CONTRACT debug-index.process-identity-fields
    DOMAIN debug_index KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/debug_index/ProcessIdentityFieldsSpec.cpp
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(ui_hang_watchdog_policy_spec
    OWNER src/common
    CONTRACT debug-index.ui-hang-watchdog-policy
    DOMAIN debug_index KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/debug_index/UiHangWatchdogPolicySpec.cpp
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(ui_hang_watchdog_lifecycle_spec
    OWNER src/common
    CONTRACT debug-index.ui-hang-watchdog-lifecycle
    DOMAIN debug_index KIND behavior RISK high
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/debug_index/UiHangWatchdogLifecycleSpec.cpp
        src/common/UiHangWatchdog.h
        src/common/UiHangWatchdog.cpp
        src/common/DebugOptions.h
        ${_miacode_log_core}
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(log_pruning_policy_spec
    OWNER src/common
    CONTRACT debug-index.log-pruning-policy
    DOMAIN debug_index KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/debug_index/LogPruningPolicySpec.cpp
        src/common/LogEmissionPolicy.h
        src/common/UiHangWatchdogPolicy.h
        src/audio/BassPreviewSfxSchedulerPolicy.h
    LIBS Qt6::Core
    INCLUDES src
)

miacode_add_spec(process_diagnostics_spec
    OWNER src/common
    CONTRACT debug-index.process-diagnostics
    DOMAIN debug_index KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
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

miacode_add_spec(window_visibility_diagnostics_spec
    OWNER src/app
    CONTRACT debug-index.window-visibility-diagnostics
    DOMAIN debug_index KIND behavior RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/debug_index/WindowVisibilityDiagnosticsSpec.cpp
        src/app/WindowVisibilityDiagnostics.h
        src/app/WindowVisibilityDiagnostics.cpp
        ${_miacode_log_core}
    LIBS Qt6::Core Qt6::Gui
    INCLUDES src
)

miacode_add_spec(idle_freeze_repro_script_spec
    OWNER scripts/debug
    CONTRACT debug-index.idle-freeze-repro-script
    DOMAIN debug_index KIND source-contract RISK normal
    EXECUTION ctest STATUS active PLATFORM all
    SOURCES
        src/tools/debug_index/IdleFreezeReproScriptSpec.cpp
    LIBS Qt6::Core
)
target_compile_definitions(idle_freeze_repro_script_spec PRIVATE
    "MIACODE_SOURCE_ROOT=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
