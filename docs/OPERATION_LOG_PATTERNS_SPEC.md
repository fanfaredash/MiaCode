# Operation Log Patterns — Triage Reference

This document is a reference for reading MiaCode's runtime logs. It catalogues every log-line shape the breadcrumb / fatal / supervisor pipeline emits, what each field means, and which channel to consult for which symptom. Read this before opening a bug report's logs cold.

Pair docs (design / rollout history): `OPERATION_BREADCRUMB_LOGGING_PLAN.md` (English) and `_ZH.md` (Chinese), both kept local-only per repo convention.

## 1. Channels at a glance

| Channel | File | Purpose | Sync | Trim |
|---|---|---|---|---|
| `Operation` | `miacode_operation.log` | Op-chain failures from `MC_OP` `~Scope` / `fail()` | Async | 2 MB cap, `--debug` only |
| `Fatal` | `miacode_fatal.log` | Death events; `appendFatalMessage` (chain auto-inlined) | **Synchronous + fsync** | 2 MB cap, `--debug` only |
| `Runtime` | `miacode_runtime_debug.log` | General trace events, supervisor events, IPC events | Async | 2 MB cap, `--debug` only |
| `Audio` | `miacode_audio_debug.log` | BASS / miniaudio engine trace | Async | 2 MB cap, `--debug` only |
| `Export` | `miacode_video_export.log` | Video export pipeline; `fail_*` stages also fan to `Fatal` | Async | 2 MB cap, `--debug` only |
| `StartupTiming` | `miacode_startup_timing.log` | Stage timings during application bringup | Async | 2 MB cap, always-on if `MIACODE_STARTUP_TIMING=1` |
| `PreviewProfile` | `miacode_preview_profile_summary.txt` | Per-session render profile summary | Async | Not auto-trimmed |
| Shadow | `miacode_op_chain_<pid>.log` | Heap-free op-chain on hard SEH crash | One-shot, `CREATE_ALWAYS` | Overwritten each crash |

**Caveat — trim only fires under `--debug`.** Outside `--debug` mode, files grow unbounded. See [DebugLog.cpp:541](../src/common/DebugLog.cpp:541). Production retention is up to the user.

## 2. File location

All files land in `MIACODE_LOG_DIR` if set, else in the session's project log dir, else in `<exe>/logs` under `--debug`, else `<TEMP>`. See [DebugLog.cpp::logDirectory](../src/common/DebugLog.cpp:622).

The parent process passes `MIACODE_LOG_DIR` to spawned children via `QProcessEnvironment` so editor + worker logs co-locate **in the same directory but in separate files**. Specifically, [`PreviewWorkerSupervisor::startProcess`](../src/preview/ipc/PreviewWorkerSupervisor.cpp:120) injects per-channel overrides:

- `MIACODE_RUNTIME_LOG_PATH=<dir>/miacode_runtime_debug_worker.log`
- `MIACODE_FATAL_LOG_PATH=<dir>/miacode_fatal_worker.log`

So a typical session log directory contains:

| File | Writer |
|---|---|
| `miacode_runtime_debug.log` | editor |
| `miacode_runtime_debug_worker.log` | worker |
| `miacode_fatal.log` | editor |
| `miacode_fatal_worker.log` | worker |
| `miacode_audio_debug.log` | editor (audio engine is editor-side) |
| `miacode_video_export.log` | editor + export worker (split is via per-job sub-files inside) |
| `miacode_worker_trace.log` | worker (always lands at this fixed name) |
| `miacode_startup_timing.log` | latest process to write — editor + worker share, last write wins |
| `miacode_op_chain_<pid>.log` | the process that crashed (one file per crashed PID) |
| `miacode_startup_beacon_<pid>.txt` | one-shot heap-free beacon per process at startup |
| `preview_worker_latency_<session>.csv` | worker per-session render latency dump |

The reason for the editor/worker split on `runtime` and `fatal`: on Windows the `AsyncLogWriter` holds an exclusive handle on the file it's writing, so a second process's `QFile::open(WriteOnly | Append)` would silently fail. With separate filenames both processes can append independently.

Per-channel path can be overridden by `MIACODE_<CHANNEL>_LOG_PATH` env var (see [DebugLog.cpp:113-126](../src/common/DebugLog.cpp:113)).

## 3. Line shapes

Every channel uses the same prefix: `<ISO timestamp> [<channel>/<scope>] <payload>`. The patterns below show only the `<channel>/<scope>` and `<payload>` parts — timestamps elided for clarity.

### 3.1 `Operation` — explicit `fail()` return

```
[op/failed] op=<class>::<method> chain=<class>::<method> reason=<text> at=<file>:<line> notes=[<key>=<val>; …]
```

Emitted when a function calls `_mc_op_.fail(reason)` and returns false. The destructor recognises that `fail()` was invoked and skips its own emission, so this produces exactly one line.

**Example:**
```
[op/failed] op=MainWindow::DocumentSection::saveToPath chain=MainWindow::DocumentSection::saveToPath ← MainWindow::DocumentSection::onSaveFile reason=QFile::open: permission denied at=MainWindow.DocumentFlow.cpp:1064 notes=[path=D:/charts/readonly/maidata.txt]
```

Fields:
- `op=` — the failing scope's name (matches the string passed to `MC_OP(…)`).
- `chain=` — leaf-first walk of all currently-active scopes on this thread, joined by ` ← `.
- `reason=` — the argument to `fail(…)`. Free-form text, typically `<api_call>: <error>`.
- `at=` — `__FILE__:__LINE__` of the `MC_OP` site (via `std::source_location`). Click-jump target.
- `notes=` — comma-separated list of `note(…)` calls accumulated up to the point of failure.

### 3.2 `Operation` — exception unwind

```
[op/failed] op=<class>::<method> chain=<…> reason=exception what=(unwind) at=<file>:<line> notes=[…]
```

Emitted by `~Scope` when the destructor sees an in-flight exception (`std::uncaught_exceptions()` increased since constructor). One line per active scope as the stack unwinds, in leaf-first order.

**Example (3 lines for a chain throwing through 3 frames):**
```
[op/failed] op=oplog_test::leaf chain=oplog_test::leaf ← oplog_test::mid ← oplog_test::root reason=exception what=(unwind) at=…
[op/failed] op=oplog_test::mid  chain=oplog_test::mid  ← oplog_test::root  reason=exception what=(unwind) at=…
[op/failed] op=oplog_test::root chain=oplog_test::root                     reason=exception what=(unwind) at=…
```

`what=(unwind)` is intentional — see [§7.1](#71-whatunwind-placeholder). The actual exception text comes through `Channel::Fatal` from inside the catch handler.

### 3.3 `Fatal` — `appendFatalMessage` with chain inlined

```
[fatal/<scope>] <payload> | chain=<…>
```

Written from inside `catch (...) { appendFatalMessage("scope/tag", message); }` — the seam at [DebugLog.cpp:850](../src/common/DebugLog.cpp:850) automatically appends `| chain=<currentChain()>`.

**Example:**
```
[fatal/preview/worker_exception] Unhandled preview worker exception. details=SharedMemory::attach: slot byte size mismatch (got 65536, expected 524288) | chain=runCliPreviewWorker ← main
```

This is the death-event entry. `Channel::Operation` will have produced sibling lines as the destructors unwound; cross-reference by timestamp.

**Synchronous + fsync.** Unlike all other channels, `Fatal` writes are flushed to disk immediately — see [DebugLog.cpp:769](../src/common/DebugLog.cpp:769). The bytes survive process termination.

### 3.4 Shadow log — heap-free SEH dump

File: `<log_dir>/miacode_op_chain_<pid>.log`, written by [`oplog::flushShadowToDisk`](../src/common/OperationLog.cpp:388) via the SEH top-level filter / signal handler / explicit pre-`__fastfail` flush. Pure Win32 — no Qt, no heap.

**Format:**
```
miacode operation breadcrumb shadow
pid=<pid> utc=<ISO timestamp>

thread tid=<thread_id> depth=<N>
  [<N-1>] <leaf op name>
  [<N-2>] <parent op name>
  …
  [0] <root op name>

thread tid=<other_thread_id> depth=<M>
  [<M-1>] <leaf>
  …
```

One `thread …` block per thread that had `MC_OP` frames active. Leaf-first within each thread. Truncation marker `(truncated_from=N)` appears if a single thread had > 16 frames.

### 3.5 `Fatal` — worker shadow collated

```
[fatal/preview/worker_shadow_chain] worker_pid=<pid> path=<shadow_path>
miacode operation breadcrumb shadow
pid=<pid> utc=…
…
```

Written by [`PreviewWorkerSupervisor::onProcessFinished`](../src/preview/ipc/PreviewWorkerSupervisor.cpp:560) when the worker exits via `QProcess::CrashExit` and a corresponding shadow file is found. The full shadow text is inlined into the parent's `Fatal` log so the editor's bug report is self-contained — no need to chase down per-process files.

## 4. Triage recipes

### 4.1 Symptom: "feature did nothing, no error message"

This is the **soft failure** case. Open `miacode_operation.log`, grep for `[op/failed]`. Recent entries show what gave up and where in the call chain it was. The `chain=` field includes the user-action endpoint (e.g. `onSaveFile`), so you can map the action to the failing internal step.

If `miacode_operation.log` is empty for the time window, the failure didn't propagate through any `MC_OP` site. That's a coverage gap — see [`OPERATION_BREADCRUMB_LOGGING_PLAN.md` §4](OPERATION_BREADCRUMB_LOGGING_PLAN.md) for the deliberate-exclusion list and consider whether the failing endpoint should be instrumented.

### 4.2 Symptom: "caught exception" (toast, status bar, error dialog)

Open `miacode_fatal.log` first — find the `[fatal/<scope>]` line for the time window. The `details=` field has the exception text, the `chain=` field has the inferred call chain at the time of catch.

Then open `miacode_operation.log` and find lines with the same timestamp ± a few ms. These are the per-frame unwind entries from `~Scope`. The leaf entry's `notes=` carries the per-frame context (e.g. `path=…`, `cmd=…`) that was set by `note()` calls before the throw.

### 4.3 Symptom: "process crashed" (no message, just gone)

Three layers, in this order:

1. **`miacode_fatal.log`** — look for `[fatal/preview/worker_shadow_chain]` for that PID. If present, you're done — the inlined shadow text shows the chain at crash time. Done.
2. **`miacode_op_chain_<pid>.log`** — if step 1 didn't find a collation entry (e.g. it was the editor that crashed, or an export worker without auto-collation), open the shadow file directly. Same format, one-shot per crash, overwritten on next crash by the same PID.
3. **`miacode_runtime_debug.log`** — last few entries before the timestamp gap show what the process was last doing. Combined with the `Fatal` `[fatal/<source>/finished]` supervisor entry, this gives "what was happening" + "how it died."

### 4.4 Symptom: "cross-process bug — editor and worker disagree"

Editor and worker write to **separate files in the same directory** (see §2). Open both halves side-by-side and merge by timestamp:

| Editor side | Worker side |
|---|---|
| `miacode_runtime_debug.log` | `miacode_runtime_debug_worker.log` |
| `miacode_fatal.log` | `miacode_fatal_worker.log` |

Tags help disambiguate further:

- Editor: `[runtime/preview/worker_supervisor]`, `[runtime/MainWindow/…]`, `[runtime/quick_shell/…]`, etc.
- Worker: `[preview/worker]`, `[fatal/preview/worker_exception]`, etc.

The supervisor's stdin/stdout-routed events ([`PreviewWorkerSupervisor::parseStdoutLine`](../src/preview/ipc/PreviewWorkerSupervisor.cpp:436)) appear in the **editor**'s runtime log even though they describe worker state — those are the supervisor's view of what the worker reported. Cross-reference with the worker's own emissions in `miacode_runtime_debug_worker.log` to see whether they agreed.

### 4.5 Symptom: "editor went silent, worker shut down later via stdin_eof"

This is the **editor crashed, worker survived** pattern. Diagnostic procedure:

1. Open `miacode_runtime_debug.log` (editor side) and find the **last** entry. That's the moment the editor process died — note the timestamp.
2. Open `miacode_runtime_debug_worker.log` and find the `tag=stdin_eof treated as shutdown` entry. Its timestamp will be **N seconds after** the editor's last entry — that's how long it took the OS to drain the editor's pipe handles.
3. The editor's last log entry shows what it was doing right before death. Look for bursts of repeated actions (rapid wheel-scroll, repeated rebuild events, etc.) that may indicate a load-dependent crash.
4. Check for `miacode_fatal.log` — if **missing**, the editor never reached any `catch (...) → appendFatalMessage` site. Hard SEH or OS termination.
5. Check for `miacode_op_chain_<pid>.log` matching what should have been the editor's PID. **If missing, the build predates Phase 4** (heap-free shadow). Recommend the user upgrade and reproduce.
6. If the shadow file IS present, open it directly — it has the leaf-first chain at crash time.

### 4.5 Symptom: nothing in any log

Check that:
- `MIACODE_LOG_DIR` is set (or `--debug` enables a default dir).
- The process actually launched. Check `Channel::StartupTiming` if available.
- For Release builds without `--debug`, async writes may have been lost on a hard crash (but `Fatal` is synchronous so it should survive — if `Fatal` is empty, the process didn't reach a fatal seam).

## 5. The MC_OP idiom

Every endpoint in the codebase that follows the convention has the shape:

```cpp
bool MyClass::myMethod(const QString& arg) {
    MC_OP("MyClass::myMethod");                            // (1)
    _mc_op_.note(QStringLiteral("arg=%1").arg(arg));       // (2)
    if (someEarlyFailure) {
        _mc_op_.fail(QStringLiteral("reason"));            // (3)
        return false;
    }
    // ... body ...
    return true;
}
```

1. **`MC_OP(name)`** — pushes a frame onto the thread-local chain + the heap-free shadow. RAII, scope-bound. The `_mc_op_` variable is fixed-name; multiple in the same lexical scope conflict (use nested blocks if needed).
2. **`note(kv)`** — accumulates context. Only emitted on failure. Use sparingly — variables that materially help triage, not arbitrary state.
3. **`fail(reason)`** — explicit non-throw failure. Logs once, suppresses the destructor's emission so we don't double-log on the "fail then return false" pattern.

Operation-name convention: `<Class>::<Method>` for member functions, `<freeFunction>` for free functions. See [`OPERATION_BREADCRUMB_LOGGING_PLAN.md` §7.2](OPERATION_BREADCRUMB_LOGGING_PLAN.md) for the full convention.

## 6. Cross-process crash logging

End-to-end pipeline:

| Stage | Code |
|---|---|
| Parent passes log dir | `process->setProcessEnvironment(env);` with `MIACODE_LOG_DIR` set ([PreviewWorkerSupervisor.cpp:88](../src/preview/ipc/PreviewWorkerSupervisor.cpp:88)) |
| Child resolves shadow path | [OperationLog.cpp::installShadow](../src/common/OperationLog.cpp:357) — `MIACODE_LOG_DIR/miacode_op_chain_<pid>.log` |
| Child installs SEH filter | [`crash_recovery::install`](../src/common/CrashRecovery.cpp:188) calls `oplog::installShadow` plus the SEH/signal/terminate handlers |
| Child crashes | SEH filter fires → `flushSnapshotToDisk` → `flushShadowToDisk` writes the shadow file |
| Parent observes crash | `QProcess::CrashExit` from [`PreviewWorkerSupervisor::onProcessFinished`](../src/preview/ipc/PreviewWorkerSupervisor.cpp:525) |
| Parent collates | Reads `miacode_op_chain_<dead_pid>.log` and inlines into `Channel::Fatal` via `appendFatalMessage` ([PreviewWorkerSupervisor.cpp:566](../src/preview/ipc/PreviewWorkerSupervisor.cpp:566)) |

Demo executable: `oplog_self_test --xprocess-demo` runs both sides in a single test, prints the parent's collation result.

## 7. Anti-patterns and known quirks

### 7.1 `what=(unwind)` placeholder

`~Scope` runs during stack unwind, **before** the matching `catch` handler is entered. On MSVC, `std::current_exception()` returns null in this state — see [OperationLog.cpp:53-59](../src/common/OperationLog.cpp:53). So the destructor's `Channel::Operation` line carries `what=(unwind)` instead of the real exception text.

This is **intentional**, not a bug. The actual exception text is captured via the catch handler's `appendFatalMessage` path, which does see the exception correctly. The two channels are designed to be complementary.

A future "fix" that tries to populate `what=` in the destructor will likely re-introduce the `std::terminate` bug we worked around. Don't.

### 7.2 Where `MC_OP` is deliberately not used

Per `OPERATION_BREADCRUMB_LOGGING_PLAN.md` §4 exclusion list:

- Per-frame render loop bodies (would log thousands per second)
- Pure compute helpers, getters, transforms (failure isn't endpoint-grade)
- Trivial wrapper slots that just delegate to a section method (the section method is the endpoint; the wrapper is noise)
- Render thread hot paths

If you find a log gap that hit one of these areas, you don't need `MC_OP` at the helper — instrument the **caller** that consumed the helper's output.

### 7.3 Async writer drops under load

`Channel::Operation` and most others use the async writer at [DebugLog.cpp::AsyncLogWriter](../src/common/DebugLog.cpp:296). Queue cap is 4096 entries; overflow drops the oldest. Drop count is exposed via `LogWriterStats::droppedCount`. If you see suspiciously few `op/failed` lines for a noisy time window, check `Channel::Runtime` for writer-stats summaries.

`Channel::Fatal` is synchronous and never drops.

### 7.4 Editor/worker file split (was originally a multi-writer race)

Earlier drafts of this doc assumed editor and worker shared the same `miacode_fatal.log` / `miacode_runtime_debug.log` files (interleaved by timestamp). They don't — see §2. The supervisor splits them via `MIACODE_FATAL_LOG_PATH` / `MIACODE_RUNTIME_LOG_PATH` env overrides. The split exists because Windows `AsyncLogWriter` holds an exclusive handle on its target file; a second writer would silently fail to open.

If you grep across both files and merge by timestamp, you get the interleaved view manually. There's no automated merger today — operator-side `Sort-Object LastWriteTime` or similar.

### 7.5 Shadow buffer pool exhaustion

The shadow pool is fixed at 32 thread slots ([OperationLog.cpp:206](../src/common/OperationLog.cpp:206)). Slot allocation is one-way — once a thread takes a slot, it's not returned even on thread exit. After 32 distinct threads-ever, new threads have no shadow representation; their crashes won't appear in the dump.

Typical MiaCode session: ~10 threads (GUI, render, audio, asset loader, BASS callback × 2, QThreadPool × N). Pool exhaustion has not been observed.

## 8. Reading order on a fresh bug report

For a triage starting cold from a user-supplied log bundle:

1. **Open `miacode_fatal.log`.** Read every line. This is the synchronous-fsync channel; if anything caught a bad event, it's here.
2. **For each `[fatal/…]` entry, note the timestamp.** Open `miacode_operation.log` and `miacode_runtime_debug.log` filtered to a few seconds before each fatal timestamp. The `Operation` lines show the chain; `Runtime` shows the surrounding events.
3. **Check for shadow-collation entries** (`[fatal/preview/worker_shadow_chain]`) — these inline a crashed child's chain.
4. **Check for orphan shadow files** (`miacode_op_chain_*.log`) without a matching collation entry — usually means the editor itself crashed and there's no parent to collate.
5. **`Channel::StartupTiming`** if the bug looks startup-related.
6. **`Channel::Audio` / `Channel::Export`** for subsystem-specific reports.

A clean report fits on one screen of `Channel::Fatal` plus the matched chains from `Channel::Operation`. If you find yourself paging through more than that, either the log is noisy (bug in the trim policy) or there's a cascading failure with multiple distinct fatal events worth investigating separately.

## 9. Source-of-truth file map

| Concept | File |
|---|---|
| Channel enum + `appendFatalMessage` | [src/common/DebugLog.h](../src/common/DebugLog.h), [src/common/DebugLog.cpp](../src/common/DebugLog.cpp) |
| `MC_OP`, `Scope`, `currentChain`, shadow buffer | [src/common/OperationLog.h](../src/common/OperationLog.h), [src/common/OperationLog.cpp](../src/common/OperationLog.cpp) |
| SEH filter installation, `flushSnapshotToDisk` integration | [src/common/CrashRecovery.cpp](../src/common/CrashRecovery.cpp) |
| Worker supervisor — shadow auto-collation | [src/preview/ipc/PreviewWorkerSupervisor.cpp:560](../src/preview/ipc/PreviewWorkerSupervisor.cpp:560) |
| Worker session — IPC dispatch + crash-injection seam | [src/preview/runtime/PreviewWorkerSession.cpp](../src/preview/runtime/PreviewWorkerSession.cpp) |
| Self-test + cross-process demo | [src/tools/oplog/OperationLogSpec.cpp](../src/tools/oplog/OperationLogSpec.cpp) |
