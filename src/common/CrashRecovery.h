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
// Recovery flow on chart open: the surface checks for a recovery file,
// compares its content to the on-disk chart, and if it differs offers
// the user a one-click restore. Always cleared on clean save / clean
// app close so it never lingers across normal sessions.
//
// Lifecycle:
//   main()                 → install()         (once)
//   chart open/close       → updateSnapshot()  (initial state)
//   markCurrentFieldDirty  → updateSnapshot()  (per edit)
//   explicit save / chart close → clearSnapshot() + deleteRecoveryFile()
//   crash                  → handler → flushSnapshotToDisk()  (writes file)
//   next chart open of same path → readRecoveryFile() → prompt → load

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
//   <chartDir>/.miacode/autosave/<chartFile>/<chartFile>.crash_recovery
// Returns empty if chartFilePath is empty / can't be resolved.
QString crashRecoveryFilePath(const QString& chartFilePath);

// Read the recovery file as UTF-8 bytes. Returns empty QByteArray if
// the file doesn't exist or can't be read.
QByteArray readRecoveryFile(const QString& chartFilePath);

// Delete the recovery file. Called after the user accepts/declines
// recovery, after a successful explicit save, and on clean app close.
// Returns true if the file was deleted (or didn't exist).
bool deleteRecoveryFile(const QString& chartFilePath);

}  // namespace miacode::crash_recovery
