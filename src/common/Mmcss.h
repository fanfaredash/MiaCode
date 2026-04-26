#pragma once

#include <QString>
#include <QtGlobal>

namespace miacode::mmcss {

// Result of attempting to register a thread with the Multimedia Class Scheduler Service
// (MMCSS) on Windows. On non-Windows platforms all calls are no-ops and report `false`.
//
// What MMCSS does (Windows): registering a thread under a "task class" (e.g. "Games",
// "Pro Audio") asks the OS scheduler to give that thread elevated, jitter-resistant CPU
// time — protecting it from being preempted by background work like indexing, antivirus
// scans, and other normal-priority threads. This is the standard mechanism real-time
// rhythm games and DAWs use on Windows; it is the most reliable single fix for the
// "render thread occasionally takes 22 ms because the OS scheduled it out for one
// quantum" pattern.
//
// Lifetime: the registration is stored per-thread. Call `registerCurrentThread` from
// the thread you want to elevate (typically Qt's QSG render thread) and call
// `unregisterCurrentThread` from the same thread before it exits.
//
// Opt-out: setting MIACODE_DISABLE_MMCSS=1 in the environment makes
// `registerCurrentThread` a no-op so users can recover if a buggy driver mishandles
// MMCSS. This avoids needing a separate code path or rebuild.

struct RegistrationResult {
    bool registered = false;     // true if AvSetMmThreadCharacteristicsW succeeded
    QString taskClassUsed;       // e.g. "Games" or empty if registration was skipped/failed
    QString skipReason;          // populated when registered==false to explain why
    quint32 lastErrorCode = 0;   // GetLastError() if the Windows call failed; 0 otherwise
};

// Register the calling thread with MMCSS. `preferredTaskClass` is one of the documented
// Windows task class names ("Games", "Pro Audio", "Audio", "DisplayPostProcessing",
// "Capture", "Distribution", "Window Manager"). On failure the function falls back to
// "Audio" once, then gives up — both task classes are usually present on Windows
// installs that aren't stripped down.
RegistrationResult registerCurrentThread(const QString& preferredTaskClass);

// Release the MMCSS registration held by the calling thread. Safe to call even if
// `registerCurrentThread` failed or was never called.
void unregisterCurrentThread();

// Snapshot of the most recent registration attempt across all threads. Safe to call from
// any thread. Used by the preview profile summary so we can confirm MMCSS actually took
// effect even after the runtime debug log has rotated past the original log line.
struct LastRegistrationStatus {
    bool everAttempted = false;
    bool everRegistered = false;
    QString lastTaskClass;
    QString lastSkipReason;
    quint32 lastErrorCode = 0;
};
LastRegistrationStatus lastRegistrationStatus();

}  // namespace miacode::mmcss
