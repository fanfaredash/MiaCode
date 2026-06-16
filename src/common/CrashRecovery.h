#pragma once

// Crash-time autosave for unsaved chart edits.
//
// The legacy autosave timer writes `<chart>.bak` every 2 seconds after the
// last edit and on window-deactivation. That covers normal exit and "user
// alt-tabbed away" cases, but NOT abnormal termination (segfault, abort(),
// unhandled C++ exception, std::terminate, runaway-then-killed). The
// idle window between an edit and the next debounce can drop several
// seconds of work on the floor.
//
// This module fills the gap with a two-part mechanism:
//
//   1. **Per-edit in-memory snapshot**. On every `markCurrentFieldDirty()`,
//      the GUI thread copies the document's UTF-8 text into a static
//      double-buffered struct and swaps the active index atomically. No
//      disk I/O. Cost is one memcpy + atomic store.
//
//   2. **Crash handlers**. Win32 SetUnhandledExceptionFilter,
//      std::set_terminate, and signal(SIGABRT) all funnel into a
//      `flushSnapshotToDisk` function that uses pure Win32
//      (CreateFileW + WriteFile + CloseHandle) — no Qt, no C++ heap,
//      no locks, safe to call from a corrupted process. The recovery
//      file is `<chart>.crash_recovery` next to the existing `.bak`.
//
// Recovery flow on chart open: if a recovery file exists, the document
// surface records the newest File -> Restore Backup entry, lets the
// normal window/document load finish, then invokes the same restore path
// the menu uses. The original chart file is not overwritten; the restored
// text becomes dirty editor content until the user saves.
//
// **Session marker (abnormal-exit detection).** The crash handlers above
// only cover terminations that actually run them. A task-manager kill,
// power loss, or a fail-fast abort (__fastfail, heap-corruption
// termination) writes no recovery file — but the regular 2-second
// debounced autosave (`.miacode/.autosave/<file>/<file>.bak`) usually
// holds the unsaved edits. To know whether a previous session ended
// abnormally, each GUI process writes a small **per-instance** marker
// while a chart is open and deletes it on clean close:
//
//     <AppConfigLocation>/sessions/session-<pid>.marker
//
// The marker records `pid`, the process `created` time (Windows, for
// PID-reuse defense), and the current `chart` path. Markers are
// per-instance — keyed by PID — so multiple MiaCode windows can run at
// once without a second instance mistaking the first's live marker for an
// abnormal exit (the old single `session.marker` design did exactly that,
// and even deleted the running instance's marker). At startup a process
// scans `sessions/` ONCE and treats a marker as abandoned only if its
// owning process is no longer alive (OpenProcess + WaitForSingleObject,
// with a creation-time match to reject recycled PIDs). Live peers' markers
// are left untouched; each process only ever writes/deletes its own.
// An abandoned marker for chart X ⇒ a prior session died without a clean
// shutdown while X was open ⇒ the chart-open recovery prompt for X may also
// offer the autosave-latest snapshot, not just the crash-handler file. The
// marker is GUI-session-only: CLI export / export-worker processes share
// this binary and must never touch it (setSessionMarkerEnabled stays false
// there).
//
// Lifecycle:
//   main() [GUI only]      → install() + setSessionMarkerEnabled(true)
//   chart open/close       → updateSnapshot()  (initial state)
//                          → updateSessionMarker(path)  (via setCurrentFilePath)
//   markCurrentFieldDirty  → updateSnapshot()  (per edit)
//   explicit save / chart close → clearSnapshot() + deleteRecoveryFile()
//   clean app close        → clearSessionMarker()
//   crash                  → handler → flushSnapshotToDisk()  (writes file)
//   next chart open of same path → readRecoveryFile() + (if marker matched)
//                                  autosave-latest → prompt → load

#include <QByteArray>
#include <QString>

namespace miacode::crash_recovery {

// Install the SEH / terminate / signal handlers. Idempotent. Call once
// from main() as early as possible (before any window construction so a
// crash during startup is also covered, where possible).
void install();

// Eagerly create the crash-recovery directory for a chart. Must be
// called when the chart is opened, BEFORE the user has a chance to
// edit. This is the critical reliability fix — without it, the
// race window is:
//   open chart  →  user edits 1 keystroke  →  crash within 1ms
// On that race the crash handler runs CreateFileW against a directory
// that's never been mkpath'd, the call fails with ERROR_PATH_NOT_FOUND,
// and the recovery file is never written. Calling this on chart open
// guarantees the directory is present before any updateSnapshot can
// matter. Idempotent and cheap (a stat() on warm runs).
//
// Returns true on success, false if the path can't be resolved or
// mkpath fails (e.g. read-only filesystem). On failure, crash recovery
// for this chart is effectively disabled — log accordingly.
bool prepareForChart(const QString& chartFilePath);

// Update the in-memory snapshot. Called from the GUI thread on every
// edit (via markCurrentFieldDirty). Cheap — bounded memcpy + atomic
// store. If chartFilePath is empty, clears the snapshot.
//
// **Encoding contract: utf8DocumentText is converted to UTF-8 (no
// BOM) via QString::toUtf8(). The crash handler writes the raw
// UTF-8 bytes. The recovery prompt reads them back via
// QString::fromUtf8(). Both sides must agree — do not change one
// without the other.**
//
// utf8DocumentText is the full chart text that should be recovered if
// the process dies right after this call returns. If the UTF-8
// encoding exceeds the 4 MB cap, the snapshot is INVALIDATED (cleared)
// instead of truncated — a partial chart is worse than none, and any
// prior valid snapshot is now stale.
void updateSnapshot(const QString& chartFilePath,
                    const QString& utf8DocumentText);

// Clear the snapshot. After this, a crash will produce no recovery file.
// Called on clean save / clean chart close.
void clearSnapshot();

// Resolve the recovery file path for a chart:
//   <chartDir>/.miacode/.autosave/<chartFile>/<chartFile>.crash_recovery
// Returns empty if chartFilePath is empty / can't be resolved.
QString crashRecoveryFilePath(const QString& chartFilePath);

// Read the recovery file as UTF-8 bytes. Returns empty QByteArray if
// the file doesn't exist or can't be read.
QByteArray readRecoveryFile(const QString& chartFilePath);

// Delete the recovery file. Called after the user accepts/declines
// recovery, after a successful explicit save, and on clean app close.
// Returns true if the file was deleted (or didn't exist).
bool deleteRecoveryFile(const QString& chartFilePath);

// ---- Session marker (abnormal-exit detection) ----------------------

// Enable the session-marker mechanism for this process. Default OFF so
// CLI video export and export-worker runs (which construct MainWindow
// and open charts in this same binary) never create or destroy the GUI
// session's marker. Call once from main(), only on the GUI path.
void setSessionMarkerEnabled(bool enabled);

// Record that a chart is currently open in this GUI session. Writes this
// process's own per-instance marker (`sessions/session-<pid>.marker`) with
// the chart path. An empty path clears this process's marker (no chart open
// → nothing to recover). The first marker operation of the process captures
// the abandoned markers other (dead) sessions left behind (see
// consumeAbandonedSessionChartMatch) before writing. Concurrent live
// instances are never touched. No-op while the mechanism is disabled.
void updateSessionMarker(const QString& chartFilePath);

// Delete THIS process's own marker. Called on clean app close, after
// autosave/crash cleanup. Never touches other instances' markers. No-op
// while the mechanism is disabled.
void clearSessionMarker();

// True once per abandoned chart: when chartFilePath matches a chart recorded
// by a marker that a NON-LIVE previous session left behind (that session
// ended without a clean close while this chart was open). Markers belonging
// to still-running instances are excluded. Callers use this to widen the
// chart-open recovery prompt to the debounced autosave snapshot. Always
// false while disabled.
bool consumeAbandonedSessionChartMatch(const QString& chartFilePath);

}  // namespace miacode::crash_recovery
