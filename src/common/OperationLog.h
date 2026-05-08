#pragma once

// Operation breadcrumb logging — see docs/OPERATION_BREADCRUMB_LOGGING_PLAN.md
//
// Lightweight RAII helper that records a thread-local chain of "what
// operation am I currently inside". Designed to add ~1 line per
// endpoint and be zero-cost on the happy path (no log line emitted
// unless the scope exits via exception or fail() is called).
//
// On unwind via exception, ~Scope writes a structured failure line to
// Channel::Operation containing op name, the chain (leaf ← parent ← …),
// std::exception::what() if available, and any notes the caller added.
//
// On explicit fail(reason), the current scope writes a similar line and
// the destructor becomes a no-op for that scope (so we don't double-log
// when a function returns false after fail()).
//
// currentChain() returns the current thread's chain as a string in the
// "leaf ← parent ← grandparent" form. Used by appendFatalMessage so the
// existing top-level catch (...) sites pick up chain context for free.
//
// Reentrance / safety: Scope allocates QString. Do NOT call from the
// SEH filter / signal handler / std::terminate path — heap may be
// corrupted there. Safe from any normal code path including catch
// blocks (heap is intact, unwind has not freed our parent_ pointer).

#include <QString>
#include <QStringList>

#include <source_location>

namespace miacode::oplog {

class Scope {
public:
    explicit Scope(
        const char* op,
        std::source_location loc = std::source_location::current()) noexcept;
    Scope(
        const char* op,
        QString detail,
        std::source_location loc = std::source_location::current()) noexcept;
    ~Scope() noexcept;

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;

    // Add a key=value (or any free-form) note. Stored on the scope and
    // only emitted if this op later fails. Cheap when not failing
    // (one QString append into a small list).
    void note(QString kv) noexcept;

    // Mark this op as failed without throwing. Logs once. Subsequent
    // destruction is a no-op so we don't double-log on the
    // "fail(...) → return false" pattern.
    void fail(QString reason) noexcept;

    const char* opName() const noexcept { return op_; }

private:
    const char* op_;
    int entryUncaught_;
    QStringList notes_;
    bool emitted_ = false;
    Scope* parent_ = nullptr;
    std::source_location location_;

    friend QString currentChain();
};

// Snapshot the current thread's chain as "leaf ← parent ← …".
// Empty string if no Scope is active. Cheap (linked-list walk).
QString currentChain();

// Phase 4 — heap-free shadow chain for hard crashes.
//
// The Scope class allocates QString op names and notes via Qt's heap,
// so its destructor is unsafe to call from a corrupted process (SEH
// filter, signal handler). The shadow is a separate, static-memory
// mirror of just the op-name chain that's safe to walk from any of
// those handlers using only Win32 APIs.
//
// Cost: one bounded strncpy + one atomic store per MC_OP push/pop.
// Memory: ~50 KB of zero-initialised static buffer at process start.
// Per-thread; up to 32 threads, 16 frames deep, 96-byte names.
//
// Lifecycle:
//   crash_recovery::install()  →  oplog::installShadow()  (path resolve, once)
//   MC_OP construct/destruct   →  push/pop shadow frame    (always-on, free)
//   SEH/terminate/signal       →  oplog::flushShadowToDisk() (heap-free write)
//
// On non-Windows builds, push/pop still maintain the buffers (so the
// cost is uniform across platforms in benchmarks) but
// flushShadowToDisk is a no-op — there's no SEH equivalent to wire
// it into.
void installShadow();

// Crash-handler-safe. Walks every active thread's shadow chain and
// writes a UTF-8 dump to the path captured at install time. Uses
// only Win32 (CreateFileW / WriteFile / FlushFileBuffers / CloseHandle)
// and static buffers — no heap, no locks, no Qt.
//
// Idempotent. Silent on failure (path unset, file open fails, etc.).
void flushShadowToDisk();

// Phase 6 — startup beacon for "the process exited before any log line
// was written". Writes a tiny `miacode_startup_beacon_<pid>.txt` next
// to the eventual op-chain shadow log. Heap-free pure-Win32; safe to
// call as the very first line of main(), before crash_recovery::install
// or QApplication construction.
//
// Resolves the same path priority as installShadow (env override →
// MIACODE_LOG_DIR → %TEMP%) but is independent of the SEH machinery —
// the file is always written when reached. Its absence after a silent
// startup crash means the process died before main() ran (DLL import
// failure, static-init crash); its presence proves we made it to line
// 1 of main(), narrowing the search to subsequent code.
//
// Two-line content:
//   pid=<n> utc=<ISO8601-ish> version=<MIACODE_DISPLAY_VERSION_STRING>
//   logdir=<resolved path>  (the dir we'd write into; empty if unset)
//
// Idempotent — calling more than once is harmless (overwrites with
// the most recent timestamp).
void writeStartupBeacon(const char* versionUtf8);

}  // namespace miacode::oplog

// Convenience macro. The variable name _mc_op_ is conventional so
// callers can do _mc_op_.note(...) / .fail(...) consistently.
//
//   bool foo() {
//       MC_OP("Class::foo");
//       _mc_op_.note(QStringLiteral("path=%1").arg(p));
//       if (bad) { _mc_op_.fail(QStringLiteral("bad")); return false; }
//       ...
//   }
#define MC_OP(name) ::miacode::oplog::Scope _mc_op_(name)
