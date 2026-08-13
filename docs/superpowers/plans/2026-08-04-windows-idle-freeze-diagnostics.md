# Windows Idle Freeze Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add diagnostics that make the Windows idle freeze reproducible and classifiable on the affected dual-GPU machine without changing the default GPU-binding behavior.

**Architecture:** Preserve the existing startup and QuickShell paths, extracting only small testable policies/helpers. Extend the independent GUI watchdog with an idle-heartbeat trigger, install a debug-only periodic resource timer, observe registered Win32 display-power/session events through the existing native filter, stamp source identity into startup logs, and provide an explicit PowerShell A/B evidence launcher plus Chinese operator guide.

**Tech Stack:** C++20, Qt 6 Core/Gui/Quick, Win32 User32/WTS APIs, CMake/CTest, Windows PowerShell 5.1.

---

## File map

- `CMakeLists.txt`: configure Git metadata, compile new helpers/specs, and link the Windows session-notification library.
- `src/app/AppVersion.h.in`: expose configured revision and dirty-state strings.
- `src/app/ProcessIdentityFields.h`: format stable build-identity fields independently of process discovery.
- `src/app/process_identity.cpp`: add build identity to the existing startup/process_identity record.
- `src/common/UiHangWatchdogPolicy.h`: pure trigger/repeat decision policy.
- `src/common/UiHangWatchdog.cpp`: apply idle-heartbeat trigger and include trigger in durable reports.
- `src/common/ProcessDiagnostics.h/.cpp`: install the debug-only periodic resource gauge.
- `src/app/WindowsIdleEventDiagnostics.h/.cpp`: translate and durably log Windows power/display/device/session events; own registration handles.
- `src/app/quick_shell/QuickShellBootstrap.h/.cpp`: connect the focused Windows monitor to the existing native event filter and root HWND lifetime.
- `src/tools/debug_index/*Spec.cpp`: focused behavior specs for each new unit.
- `.gitignore`: allowlist the new debug PowerShell script.
- `scripts/debug/Start_MiaCode_IdleFreezeRepro.ps1`: Phase 0 collector and explicit GPU A/B launcher.
- `docs/ops/WINDOWS_IDLE_FREEZE_REPRO_ZH.md`: Chinese user-machine reproduction and verification guide.
- `docs/ops/DEBUG_INDEX.md`: document new diagnostic log scopes and launcher.

### Task 1: Stamp and test source-build identity

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/app/AppVersion.h.in`
- Create: `src/app/ProcessIdentityFields.h`
- Modify: `src/app/process_identity.cpp`
- Create: `src/tools/debug_index/ProcessIdentityFieldsSpec.cpp`

- [ ] **Step 1: Write the failing identity helper spec**

Create `ProcessIdentityFieldsSpec.cpp` with assertions equivalent to:

```cpp
require(formatProcessIdentityBuildFields("1.1.0-beta.7", "677a962574bd", "0")
        == "version=1.1.0-beta.7 git_revision=677a962574bd git_dirty=0");
require(formatProcessIdentityBuildFields("1.1.0-beta.7", "677a962574bd", "1")
        .endsWith("git_dirty=1"));
require(formatProcessIdentityBuildFields("1.1.0-beta.7", "unknown", "unknown")
        .endsWith("git_revision=unknown git_dirty=unknown"));
```

Register `process_identity_fields_spec` in the dev-tools CMake block, referring to the not-yet-existing helper.

- [ ] **Step 2: Run the spec and verify RED**

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMIACODE_BUILD_DEV_TOOLS=ON -DCMAKE_PREFIX_PATH=/Users/caoyusen/Desktop/MiaCode/.qt/6.10.2/macos
cmake --build build --target process_identity_fields_spec -j2
```

Expected: compilation fails because `ProcessIdentityFields.h` / its formatter does not exist.

- [ ] **Step 3: Implement minimal metadata generation and formatting**

Before `configure_file`, use `git rev-parse --short=12 HEAD` and `git status --porcelain --untracked-files=normal -- .`, each with checked result codes. Produce `unknown/unknown` when Git metadata is unavailable and `0|1` dirty state otherwise. Add:

```cpp
#define MIACODE_GIT_REVISION "@MIACODE_GIT_REVISION@"
#define MIACODE_GIT_DIRTY "@MIACODE_GIT_DIRTY@"
```

Implement the pure formatter in `ProcessIdentityFields.h`, then prepend/append its result to `startup/process_identity` using `MIACODE_DISPLAY_VERSION_STRING`, `MIACODE_GIT_REVISION`, and `MIACODE_GIT_DIRTY`.

- [ ] **Step 4: Run GREEN and regression checks**

Before running, extend `DebugOptionsSpec.cpp` with explicit assertions that an unset `MIACODE_GPU_BIND_HIGH_PERFORMANCE` returns true, `0` returns false, and `1` returns true. This is a regression lock for the user-approved unchanged default, not a behavior change.

Run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMIACODE_BUILD_DEV_TOOLS=ON -DCMAKE_PREFIX_PATH=/Users/caoyusen/Desktop/MiaCode/.qt/6.10.2/macos
cmake --build build --target process_identity_fields_spec debug_options_spec -j2
ctest --test-dir build -R '^(process_identity_fields_spec|debug_options_spec)$' --output-on-failure
```

Expected: 2/2 tests pass; generated `AppVersion.h` contains a 12-character revision and clean/dirty state.

- [ ] **Step 5: Commit Task 1**

```bash
git add CMakeLists.txt src/app/AppVersion.h.in src/app/ProcessIdentityFields.h src/app/process_identity.cpp src/tools/debug_index/ProcessIdentityFieldsSpec.cpp src/tools/debug_index/DebugOptionsSpec.cpp
git commit -m "feat(diagnostics): stamp source identity in startup logs"
```

### Task 2: Detect idle GUI heartbeat stalls

**Files:**
- Create: `src/common/UiHangWatchdogPolicy.h`
- Modify: `src/common/UiHangWatchdog.cpp`
- Create: `src/tools/debug_index/UiHangWatchdogPolicySpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing watchdog policy spec**

Cover these behaviors with a pure API:

```cpp
classify(false, 0, 4999) == Trigger::None;
classify(false, 0, 5000) == Trigger::IdleHeartbeat;
classify(true, 1999, 100) == Trigger::None;
classify(true, 2000, 100) == Trigger::ActivePhase;
classify(true, 5000, 5000) == Trigger::IdleHeartbeat;
shouldReport(IdleHeartbeat, same generation, now=10000, last=9000, repeat=5000) == false;
shouldReport(IdleHeartbeat, same generation, now=14000, last=9000, repeat=5000) == true;
shouldReport(ActivePhase, new generation, below repeat interval) == true;
```

Register `ui_hang_watchdog_policy_spec` while the policy header is absent.

- [ ] **Step 2: Run the spec and verify RED**

Run:

```bash
cmake --build build --target ui_hang_watchdog_policy_spec -j2
```

Expected: compile failure because the policy API is missing.

- [ ] **Step 3: Implement the pure policy and integrate it**

Create a header-only policy with `Trigger::{None, ActivePhase, IdleHeartbeat}`, `classify(...)`, `shouldReport(...)`, and `triggerName(...)`. In `watchdogLoop()`:

1. calculate heartbeat age and phase active duration;
2. classify without an early return for inactive phases;
3. rate-limit by previous trigger, phase generation, and report time;
4. include `trigger=active_phase|idle_heartbeat` in `appendWatchdogReport`;
5. retain `Level::Fatal` and shadow flush;
6. advertise `idle_heartbeat_timeout_ms=5000` in the installation line.

- [ ] **Step 4: Run GREEN and build the real watchdog TU**

Run:

```bash
cmake --build build --target ui_hang_watchdog_policy_spec MiaCode -j2
ctest --test-dir build -R '^ui_hang_watchdog_policy_spec$' --output-on-failure
```

Expected: policy spec passes and `UiHangWatchdog.cpp` compiles in MiaCode.

- [ ] **Step 5: Commit Task 2**

```bash
git add CMakeLists.txt src/common/UiHangWatchdogPolicy.h src/common/UiHangWatchdog.cpp src/tools/debug_index/UiHangWatchdogPolicySpec.cpp
git commit -m "feat(diagnostics): detect idle GUI heartbeat stalls"
```

### Task 3: Install a 30-second periodic process-resource gauge

**Files:**
- Modify: `src/common/ProcessDiagnostics.h`
- Modify: `src/common/ProcessDiagnostics.cpp`
- Modify: `src/app/main.cpp`
- Create: `src/tools/debug_index/ProcessDiagnosticsSpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing ProcessDiagnostics spec**

Use a `QTemporaryDir` as `MIACODE_LOG_DIR`. Verify:

```cpp
debug off -> install creates no MiaCodePeriodicResourceGauge timer;
debug on -> exactly one active direct-child timer with interval 30000;
install twice -> still exactly one timer;
runtime log after install -> contains [Runtime/idle/resource_gauge] action=sample sample=0;
invoke timer timeout + flush -> contains sample=1;
```

Register a spec linked with Qt Core/Gui plus `ProcessDiagnostics.cpp` and the existing logging core.

- [ ] **Step 2: Run the spec and verify RED**

Run:

```bash
cmake --build build --target process_diagnostics_spec -j2
```

Expected: compile failure because `installPeriodicProcessResourceGauge` does not exist.

- [ ] **Step 3: Implement the debug-only idempotent timer**

Add:

```cpp
void installPeriodicProcessResourceGauge(QObject* owner);
```

Resolve a null owner to `QCoreApplication::instance()`, gate on `runtimeDebugOutputEnabled()`, find an existing named direct child before creating a timer, emit sample 0 immediately, then emit every 30 seconds. Payload fields: `action=sample sample=<n> uptime_ms=<n> app_state=<name>` plus `processResourceGaugePayload()`.

Call it immediately after `installGuiHeartbeat(&app)` in `main.cpp`.

- [ ] **Step 4: Run GREEN and the affected build**

Run:

```bash
cmake --build build --target process_diagnostics_spec MiaCode -j2
ctest --test-dir build -R '^process_diagnostics_spec$' --output-on-failure
```

Expected: spec passes; MiaCode links with no duplicate timer or missing QtGui symbol.

- [ ] **Step 5: Commit Task 3**

```bash
git add CMakeLists.txt src/common/ProcessDiagnostics.h src/common/ProcessDiagnostics.cpp src/app/main.cpp src/tools/debug_index/ProcessDiagnosticsSpec.cpp
git commit -m "feat(diagnostics): sample idle process resources"
```

### Task 4: Observe Windows environment transitions durably

**Files:**
- Create: `src/app/WindowsIdleEventDiagnostics.h`
- Create: `src/app/WindowsIdleEventDiagnostics.cpp`
- Modify: `src/app/quick_shell/QuickShellBootstrap.h`
- Modify: `src/app/quick_shell/QuickShellBootstrap.cpp`
- Create: `src/tools/debug_index/WindowsIdleEventDiagnosticsSpec.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the cross-platform translation spec**

Define stable numeric inputs and assert:

```cpp
message 0x0218 is relevant power broadcast;
power reason 0x0004 -> suspend;
power reason 0x0012 -> resume_automatic;
console display DWORD 0/1/2 -> off/on/dimmed;
message 0x007E + bpp/width/height -> display_change payload;
message 0x02B1 + reason 7/8 -> session_lock/session_unlock;
message 0x0219 -> device-change payload;
unrelated message -> empty classification;
```

Register `windows_idle_event_diagnostics_spec` with only cross-platform Qt dependencies.

- [ ] **Step 2: Run the spec and verify RED**

Run:

```bash
cmake --build build --target windows_idle_event_diagnostics_spec -j2
```

Expected: compile failure because the translator is missing.

- [ ] **Step 3: Implement translation, registration, and durable logging**

Implement a focused monitor that:

1. observes only when runtime debug output is enabled;
2. registers the root HWND with `WTSRegisterSessionNotification(..., NOTIFY_FOR_THIS_SESSION)`;
3. registers `GUID_CONSOLE_DISPLAY_STATE` using `RegisterPowerSettingNotification`;
4. retains/unregisters both handles during teardown;
5. validates `POWERBROADCAST_SETTING::DataLength >= sizeof(DWORD)` before reading display state;
6. appends a forced `windows/environment_event` Runtime record;
7. calls `flushAsyncLogWriter(1000)` and logs registration/flush failures without consuming the native message;
8. uses Windows SDK `static_assert`s for copied message/reason constants.

Construct the monitor with the QuickShell bootstrap, forward messages before close-message handling, register once after the root `winId()` is captured, and unregister before the window is destroyed. Link `Wtsapi32` on Windows.

- [ ] **Step 4: Run GREEN, app build, and platform guards**

Run:

```bash
cmake --build build --target windows_idle_event_diagnostics_spec MiaCode -j2
ctest --test-dir build -R '^windows_idle_event_diagnostics_spec$' --output-on-failure
```

Expected on macOS: translator spec and non-Windows stubs build/pass. Expected on Windows CI/user build: WTS and power-notification code compiles and links.

- [ ] **Step 5: Commit Task 4**

```bash
git add CMakeLists.txt src/app/WindowsIdleEventDiagnostics.h src/app/WindowsIdleEventDiagnostics.cpp src/app/quick_shell/QuickShellBootstrap.h src/app/quick_shell/QuickShellBootstrap.cpp src/tools/debug_index/WindowsIdleEventDiagnosticsSpec.cpp
git commit -m "feat(diagnostics): log Windows idle environment events"
```

### Task 5: Add the user-machine A/B collector and Chinese runbook

**Files:**
- Modify: `.gitignore`
- Create: `scripts/debug/Start_MiaCode_IdleFreezeRepro.ps1`
- Create: `src/tools/debug_index/IdleFreezeReproScriptSpec.cpp`
- Modify: `CMakeLists.txt`
- Create: `docs/ops/WINDOWS_IDLE_FREEZE_REPRO_ZH.md`
- Modify: `docs/ops/DEBUG_INDEX.md`

- [ ] **Step 1: Write the failing script contract spec**

The spec reads the script from `MIACODE_SOURCE_ROOT` and requires tokens/structure for:

- Windows PowerShell 5.1-compatible syntax;
- `ValidateSet("GpuBound", "GpuOff")`;
- `MIACODE_GPU_BIND_HIGH_PERFORMANCE` set explicitly to `1` or `0`;
- `MIACODE_LOG_DIR` set to a new timestamped directory;
- `--debug` and the exact real executable path;
- `Get-FileHash -Algorithm SHA256`, OS/CPU/memory/GPU CIM collection, `powercfg`, and MIACODE environment capture;
- absence of process termination commands.

Register `idle_freeze_repro_script_spec` before allowlisting/creating the script.

- [ ] **Step 2: Run the spec and verify RED**

Run:

```bash
cmake --build build --target idle_freeze_repro_script_spec -j2
```

Expected: spec fails because the script does not exist.

- [ ] **Step 3: Implement the PowerShell 5.1 launcher**

Use parameters `MiaCodeExe`, `Profile`, `LogRoot`, and optional `ChartPath`. Resolve/validate paths, create `<timestamp>-<profile>` without deleting prior evidence, write JSON/text metadata, explicitly set both existing environment variables for the child, start the real executable with `--debug`, and return immediately. Do not sleep, lock, wait for, kill, or dump the process.

Add a narrow `.gitignore` exception for this exact script.

- [ ] **Step 4: Write the Chinese runbook and index updates**

The runbook must include:

1. confirmed Phase 0 baseline (`1.1.0-beta.2`, i5-1155G7, 16 GB, MX450 + Iris Xe, no extensions);
2. command examples for both profiles using the same `app\\MiaCode.exe`;
3. B-first matrix with display sleep and lock/unlock;
4. expected startup identity/GPU-provider/resource/environment/watchdog lines;
5. freeze-time `Get-Process` classification and `procdump -ma` command;
6. evidence checklist and causal-claim limitations.

Update `DEBUG_INDEX.md` with the new log scopes, thresholds, revision fields, and script link. Do not add a new environment flag.

- [ ] **Step 5: Run GREEN plus debug-index drift guard**

Run:

```bash
cmake --build build --target idle_freeze_repro_script_spec debug_flag_index_spec -j2
ctest --test-dir build -R '^(idle_freeze_repro_script_spec|debug_flag_index_spec)$' --output-on-failure
git diff --check
```

When Windows PowerShell 5.1 is available, also parse the whole script without executing it:

```powershell
powershell.exe -NoProfile -Command "[void][scriptblock]::Create((Get-Content -LiteralPath 'scripts/debug/Start_MiaCode_IdleFreezeRepro.ps1' -Raw))"
```

Expected: both specs pass, the optional PowerShell parser exits 0, and there are no whitespace errors.

- [ ] **Step 6: Commit Task 5**

```bash
git add .gitignore CMakeLists.txt scripts/debug/Start_MiaCode_IdleFreezeRepro.ps1 src/tools/debug_index/IdleFreezeReproScriptSpec.cpp docs/ops/WINDOWS_IDLE_FREEZE_REPRO_ZH.md docs/ops/DEBUG_INDEX.md
git commit -m "docs(diagnostics): add Windows idle freeze A-B runbook"
```

### Task 6: Full verification and requirements audit

**Files:**
- Modify only if verification reveals a scoped defect.

- [ ] **Step 1: Reconfigure from current branch state**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMIACODE_BUILD_DEV_TOOLS=ON -DCMAKE_PREFIX_PATH=/Users/caoyusen/Desktop/MiaCode/.qt/6.10.2/macos
```

Expected: configure succeeds and generated build identity matches `git rev-parse --short=12 HEAD`; dirty state truthfully reflects any uncommitted verification edits.

- [ ] **Step 2: Build MiaCode and all dev tools**

```bash
cmake --build build -j2
```

Expected: exit 0.

- [ ] **Step 3: Run the full available CTest suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 0 failures.

- [ ] **Step 4: Run source and branch hygiene checks**

```bash
git diff --check HEAD~5..HEAD
git status --short
git branch --show-current
```

Expected: no whitespace errors, clean worktree, branch `codex/windows-idle-freeze-diagnostics`.

- [ ] **Step 5: Audit every approved requirement**

Re-read the design spec and verify each acceptance item has a code path, automated guard where possible, and documented user-machine validation step. Explicitly report that no Windows reproduction/dump was performed locally.
