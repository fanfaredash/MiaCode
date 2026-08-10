# Windows Idle Freeze Diagnostics Design

## Context

The affected user ran a transient build identified as `1.1.0-beta.2` on Windows with an Intel Iris Xe integrated GPU and an NVIDIA GeForce MX450 discrete GPU, 16 GB RAM, and no enabled extensions. The exact beta build is not represented by a committed `CMakeLists.txt` version transition, so version text alone cannot identify its source snapshot.

The audit in `docs/audit/WINDOWS_IDLE_FREEZE_AUDIT_REVIEW_ZH.md` found that the high-performance GPU binding became enabled by default immediately before the public `1.1.0-beta` line. On a dual-GPU machine, the root Qt Quick window can therefore use the discrete adapter while the video surface stays on the default adapter. This is the strongest current candidate, but neither an application log nor a Windows dump has yet proved the causal step from an idle power/session transition to the freeze.

The user selected a diagnostics-only approach: keep the current GPU default enabled and add enough evidence collection to reproduce and classify the failure on the affected machine. This change must not claim to fix the unknown underlying freeze.

## Goals

1. Detect a stalled GUI heartbeat even when no `MIACODE_HANG_PHASE` is active.
2. Preserve the hang report synchronously so queue overflow or log rotation cannot erase it.
3. Sample process memory and handle counters every 30 seconds during a debug run.
4. Persist Windows power, display, device, and session lock/unlock transitions so a freeze can be correlated with its environmental trigger.
5. Put the build's Git revision and dirty state beside the displayed version and executable identity in startup logs, while the launcher records the exact executable SHA-256.
6. Provide one PowerShell entry point that collects Phase 0 machine/build evidence and launches either side of the existing GPU-binding A/B switch into a separate log directory.
7. Document the exact user-machine reproduction matrix, expected evidence, freeze-time process classification, and dump collection steps in a Chinese operator guide at `docs/ops/WINDOWS_IDLE_FREEZE_REPRO_ZH.md`.

## Non-goals

- Do not change the default of `MIACODE_GPU_BIND_HIGH_PERFORMANCE`; an unset value continues to enable binding.
- Do not implement speculative GPU-device recovery before a dump identifies the blocked component.
- Do not modify waveform decoding, autosave behavior, QML timers, or rendering topology without evidence that one of them is causal.
- Do not fix extension event-bus or pet-overlay findings in this change. The reported configuration has no enabled extensions, and those findings are independent defects.
- Do not claim that macOS or unit-test verification reproduces the Windows freeze.
- Do not add a new `MIACODE_*` environment variable.

## Architecture

### Exact build identity

CMake resolves the current Git revision and whether tracked or non-ignored untracked source files are dirty at configure time, then writes both values into `generated/AppVersion.h`. A source archive uses `git_revision=unknown dirty=unknown`. `startup/process_identity` logs `version=<display version>`, `git_revision=<revision>`, and `git_dirty=<0|1|unknown>` on the boot and log-directory-rebound copies. A clean revision identifies the source snapshot; a dirty revision explicitly records that the source cannot be reconstructed from the commit alone. The PowerShell launcher's executable SHA-256 is the exact binary identity in either case.

The revision and dirty state are diagnostic metadata only. They do not change package naming or the About dialog. An `unknown` source-archive build is intentionally reported as not source-identifiable rather than being assigned misleading metadata.

### Idle-aware GUI watchdog

The watchdog keeps its existing 250 ms GUI timer, 500 ms worker cadence, 2 second active-phase threshold, and 5 second repeated-report suppression. A small pure decision policy distinguishes two triggers:

- `active_phase`: an instrumented phase remains active for at least 2 seconds;
- `idle_heartbeat`: the GUI heartbeat is at least 5 seconds old, regardless of whether a phase is active.

The idle trigger is evaluated before the existing `!phase.active` early exit. A report includes `trigger`, `active`, `active_ms`, `heartbeat_age_ms`, phase details when present, generation, and log-writer counters. It continues to use `Level::Fatal`, which synchronously flushes the Runtime channel.

Repeated idle reports are rate-limited by trigger and report time. An active phase retains generation-aware reporting. The installation breadcrumb records both thresholds so support can verify the diagnostic build is running.

### Periodic resource gauge

`ProcessDiagnostics` gains an installation function that is called beside the GUI heartbeat installer. It is enabled only when runtime debug output is enabled. It creates a named 30-second `QTimer` owned by the application, emits one baseline sample immediately, then writes one `idle/resource_gauge` Runtime line per timeout.

Each line includes a monotonically increasing sample number, elapsed milliseconds from installation, Qt application state, and the existing `processResourceGaugePayload()`. On Windows that payload already contains GDI objects, USER objects, kernel handles, working set, peak working set, private bytes, commit, paged/nonpaged pool, and page faults. A stalled GUI timer intentionally stops sampling: the last samples show whether resources grew before the heartbeat failure, while the independent watchdog thread records the stall.

### Windows environmental breadcrumbs

The existing QuickShell native event filter remains the single native-filter owner. Its native-message handler forwards relevant messages to a focused Windows idle-event diagnostic helper:

- `WM_POWERBROADCAST`, including suspend and resume variants plus `PBT_POWERSETTINGCHANGE`;
- `WM_DISPLAYCHANGE`;
- `WM_DEVICECHANGE`;
- `WM_WTSSESSION_CHANGE`, especially lock, unlock, logon, logoff, and remote connect/disconnect.

After `winId()` is available, the QuickShell root HWND registers for both WTS session notifications and `GUID_CONSOLE_DISPLAY_STATE` power-setting notifications. The latter is required because `WM_DISPLAYCHANGE` reports display-mode changes, not monitor power transitions. The registration handle is retained and unregistered during teardown. Registration success or the Win32 error is logged independently for each facility.

For `PBT_POWERSETTINGCHANGE`, the helper validates the `POWERBROADCAST_SETTING` payload, matches `GUID_CONSOLE_DISPLAY_STATE`, and records its documented DWORD state (`0=off`, `1=on`, `2=dimmed`, otherwise `unknown`). Suspend/resume broadcasts remain separate breadcrumbs. This makes the acceptance requirement for display sleep/wake observable rather than inferring it from an unrelated display-mode event.

The helper translates stable numeric message and reason codes into deterministic payloads. Unit tests exercise this translation on all platforms; Windows builds additionally assert that the copied message constants match SDK constants. Runtime logging remains Windows-only.

Every relevant event is rare and diagnostically critical. The handler appends a forced Runtime line under `windows/environment_event` and immediately drains the asynchronous writer with a bounded timeout. It observes messages and always returns control to Qt/DefWindowProc; it does not consume power, display, session, or device events and performs no recovery action.

### A/B evidence launcher

`scripts/debug/Start_MiaCode_IdleFreezeRepro.ps1` targets Windows PowerShell 5.1 and accepts the real GUI executable, optional chart path, output root, and a required profile:

- `GpuBound`: explicitly sets `MIACODE_GPU_BIND_HIGH_PERFORMANCE=1`;
- `GpuOff`: explicitly sets `MIACODE_GPU_BIND_HIGH_PERFORMANCE=0`.

Both profiles add `--debug`, create timestamped independent log directories, and launch the same executable with otherwise identical arguments. Before launch, the script records:

- executable path, file version, SHA-256, size, and modification time;
- OS, CPU, physical memory, and video-controller inventory;
- active Windows power scheme and power-query output;
- current `MIACODE_*` environment values;
- command line and selected profile.

The script does not wait for or kill MiaCode and does not automate sleep, lock, or dump collection. Those actions stay explicit so the operator can classify and capture the frozen process before termination.

## Data flow

1. CMake stamps the source revision into the executable.
2. `--debug` enables startup identity, watchdog, resource gauge, GPU-provider, and environmental logging.
3. The PowerShell profile writes machine/build metadata and starts the real packaged `app\\MiaCode.exe` with an isolated `MIACODE_LOG_DIR`.
4. Startup logs establish the exact executable, revision, requested GPU policy, and actual `startup/gpu_provider` action.
5. While idle, the GUI timer records resource history and the Windows filter records environmental transitions.
6. If the GUI stops processing events, the watchdog thread emits a durable `trigger=idle_heartbeat` line.
7. The operator records CPU/memory/handles and captures a full dump before closing the process.
8. The A/B outcome is compared only between runs of equal duration and equal sleep/lock conditions.

## Error handling and safety

- Git metadata lookup failure produces `git_revision=unknown git_dirty=unknown` and does not block source-archive builds.
- All new runtime diagnostics are gated by `--debug`; normal runs pay no timer or event-log cost beyond existing filter dispatch.
- Null application/owner pointers result in no installation or fall back to the application owner, matching the existing watchdog convention.
- Duplicate installation is idempotent so a future bootstrap refactor cannot create multiple gauges.
- Session- or display-power-notification registration failure is logged with `GetLastError`; startup continues.
- Log-drain timeout is included in the event payload or a follow-up warning, but native events are never consumed.
- The PowerShell script validates that the executable exists, refuses an unknown profile, and never deletes prior evidence directories.

## Testing strategy

Implementation follows red-green-refactor cycles:

1. A watchdog policy spec covers active-phase timeout, idle-heartbeat timeout with no phase, below-threshold cases, and repeat suppression.
2. A ProcessDiagnostics spec verifies debug gating, one named active timer, the 30-second production interval, immediate baseline emission wiring, and idempotent installation.
3. A Windows idle-event spec covers relevant/irrelevant message classification and symbolic reason names without requiring a Windows host.
4. The existing DebugOptions spec continues to assert that high-performance GPU binding defaults on and respects explicit `0`/`1` overrides.
5. A startup identity helper spec verifies the exact formatted `version`, `git_revision`, and `git_dirty` fields for clean, dirty, and source-archive inputs.
6. The PowerShell script is syntax-checked with Windows PowerShell 5.1 when available and is reviewed for parameter/profile behavior; its collection steps are also documented for manual verification.
7. Fresh CMake configure, affected spec targets, full available CTest suite, and a MiaCode build are run before completion. Cross-platform build success is reported separately from unavailable Windows runtime reproduction.

## User-machine acceptance criteria

For both `GpuBound` and `GpuOff` profiles, collected evidence must contain:

- Phase 0 machine/build metadata with the same executable SHA-256;
- `startup/process_identity` with version, Git revision, and dirty state, plus launcher metadata containing the executable SHA-256;
- `startup/gpu_provider` showing `action=bound` for the bound profile or `reason=bind_disabled_by_env` for the off profile;
- an initial resource sample and subsequent samples approximately every 30 seconds;
- durable `console_display_state=off|on|dimmed` lines when the display changes power state and session-event lines when the session locks/unlocks;
- a durable `trigger=idle_heartbeat` watchdog line if the GUI freezes.

A causal conclusion is allowed only after matching the A/B outcome with a full dump or discriminating thread stack. If the bound profile freezes and the off profile does not under equal conditions, the result supports O-1 but still requires the frozen stack to distinguish Qt/DXGI blocking from another correlated path.
