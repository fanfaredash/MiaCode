# Remove Watchdog GUI Stack Capture

## Goal

Keep MiaCode's GUI heartbeat, sub-hang stall, and hang-report diagnostics while
removing the Windows GUI-thread stack capture path that can trigger permission or
security-product error dialogs on user machines.

## Scope

- Remove `ThreadStackCapture` and its CMake/spec registration.
- Remove watchdog stack-target registration, symbol preparation, capture budgets,
  and `gui_thread_stack` emission.
- Keep `UiHangWatchdog` heartbeat monitoring, phase/idle trigger reports, stall
  episode logging, report-gate suppression summaries, and clean shutdown.
- Remove stack-specific policy APIs, tests, documentation, and diagnostic index
  claims. The remaining `gui_thread_stale` row keeps its hang timing and phase
  fields; it must never attempt a Windows thread handle operation.

## Safety Invariants

- No `SuspendThread`, `GetThreadContext`, `StackWalk64`, `SymInitialize`, or
  GUI-thread handle duplication remains in the MiaCode target.
- Watchdog installation and shutdown do not acquire or release a GUI thread
  handle and do not load `dbghelp`.
- A stale/hang report still flushes its durable runtime evidence and the
  `gui_thread_stall` began/ended edges remain unchanged.
- Existing pure report-gate and log-pruning behavior remains covered by its
  policy spec.

## Verification

- Keep policy assertions for heartbeat, hang classification, stall edges, and
  report-gate summaries; the source/CMake scan is the negative assertion that
  no stack-capture path remains.
- Build affected Release targets with `--parallel 1`.
- Run watchdog/log-pruning focused CTest and the complete configured CTest suite.
- Scan source, CMake, docs, and the built target inputs for stack-capture symbols
  and `dbghelp` references.
