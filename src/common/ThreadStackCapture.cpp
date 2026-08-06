#include "common/ThreadStackCapture.h"

#include <QFileInfo>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#if defined(_MSC_VER)
#pragma comment(lib, "dbghelp.lib")
#endif
#endif

namespace miacode::diag {

namespace {

#ifdef Q_OS_WIN

// dbghelp is explicitly documented as single-threaded per process handle: every Sym*
// call has to be serialised. Only the watchdog thread calls into this file today, but
// the mutex makes that a property of the module instead of a caller convention.
std::mutex g_captureMutex;
HANDLE g_targetThread = nullptr;
DWORD g_targetThreadId = 0;
bool g_symbolHandlerReady = false;
bool g_symbolHandlerTried = false;
// GetLastError() from a failed SymInitialize. Kept so the log can separate "no PDB on
// this machine" (the normal cause of symbol=(nosym)) from "the symbol handler never came
// up", which no frame-level field can distinguish on its own.
DWORD g_symbolHandlerErrorCode = 0;
// Set once a capture has timed out and its worker has been abandoned. The abandoned
// worker may still be blocked inside StackWalk64 and may still touch g_targetThread, so
// after this point nothing may suspend the target again and the handle must not be closed.
bool g_stackWalkAbandoned = false;

// State shared between the caller and its short-lived stack-walk worker. Held by
// shared_ptr so an abandoned worker writing into it after the caller has returned is
// well-defined rather than a use-after-free.
struct StackWalkJob {
    HANDLE thread = nullptr;
    int maxFrames = kMaxCapturedStackFrames;
    LARGE_INTEGER frequency = {};

    std::mutex mutex;
    std::condition_variable done;
    bool finished = false;

    // Exactly one of the worker and the caller performs the resume. Whoever wins the
    // exchange owns it; the loser must not call ResumeThread, or it could cancel a
    // suspension it does not own.
    std::atomic<bool> resumeClaimed{false};

    quint64 addresses[kMaxCapturedStackFrames] = {};
    int frameCount = 0;
    qint64 suspendedUs = 0;
    bool suspendFailed = false;
    DWORD suspendErrorCode = 0;
    DWORD contextFailureCode = 0;
    bool resumeFailed = false;
    DWORD resumeErrorCode = 0;
};

DWORD stackWalkMachineType();
void seedStackFrame(STACKFRAME64* frame, const CONTEXT& context);

// The ONLY code that runs while the target is suspended. Everything here is
// allocation-free and Qt-free by construction; see the hazard notes in the header.
void runStackWalkJob(std::shared_ptr<StackWalkJob> job)
{
    LARGE_INTEGER suspendStart = {};
    LARGE_INTEGER suspendEnd = {};
    QueryPerformanceCounter(&suspendStart);

    if (SuspendThread(job->thread) == static_cast<DWORD>(-1)) {
        const DWORD error = GetLastError();
        std::lock_guard<std::mutex> lock(job->mutex);
        job->suspendFailed = true;
        job->suspendErrorCode = error;
        job->finished = true;
        job->done.notify_one();
        return;
    }

    // ---- SUSPENSION WINDOW OPENS. No allocation, no Sym*, no Qt below this line. ----
    int frameCount = 0;
    DWORD contextFailureCode = 0;
    {
        CONTEXT context;
        ZeroMemory(&context, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(job->thread, &context)) {
            STACKFRAME64 frame;
            seedStackFrame(&frame, context);
            const DWORD machine = stackWalkMachineType();
            const HANDLE process = GetCurrentProcess();
            while (frameCount < job->maxFrames) {
                // If this blocks against a lock held by the suspended thread, the caller's
                // bounded wait expires, it force-resumes the target, and this thread is
                // abandoned. Should it ever unblock afterwards, resumeClaimed below stops
                // it from issuing a second, unowned resume.
                if (!StackWalk64(
                        machine,
                        process,
                        job->thread,
                        &frame,
                        &context,
                        nullptr,
                        SymFunctionTableAccess64,
                        SymGetModuleBase64,
                        nullptr)) {
                    break;
                }
                if (frame.AddrPC.Offset == 0) {
                    break;
                }
                job->addresses[frameCount++] = static_cast<quint64>(frame.AddrPC.Offset);
            }
        } else {
            contextFailureCode = GetLastError();
        }
    }

    bool resumeFailed = false;
    DWORD resumeErrorCode = 0;
    if (!job->resumeClaimed.exchange(true, std::memory_order_acq_rel)) {
        if (ResumeThread(job->thread) == static_cast<DWORD>(-1)) {
            resumeFailed = true;
            resumeErrorCode = GetLastError();
        }
    }
    QueryPerformanceCounter(&suspendEnd);
    // ---- SUSPENSION WINDOW CLOSED. ----

    std::lock_guard<std::mutex> lock(job->mutex);
    job->frameCount = frameCount;
    job->contextFailureCode = contextFailureCode;
    job->resumeFailed = resumeFailed;
    job->resumeErrorCode = resumeErrorCode;
    if (job->frequency.QuadPart > 0) {
        job->suspendedUs = static_cast<qint64>(
            ((suspendEnd.QuadPart - suspendStart.QuadPart) * 1000000LL) / job->frequency.QuadPart);
    }
    job->finished = true;
    job->done.notify_one();
}

// Initialise dbghelp's symbol handler. MUST be called with NOTHING suspended:
// SymInitialize enumerates loaded modules and allocates, i.e. it takes exactly the locks
// that make it unsafe inside the suspension window. Called once, lazily, before the
// first SuspendThread of a capture.
void ensureSymbolHandler()
{
    if (g_symbolHandlerTried) {
        return;
    }
    g_symbolHandlerTried = true;
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES
                  | SYMOPT_FAIL_CRITICAL_ERRORS);
    g_symbolHandlerReady = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    g_symbolHandlerErrorCode = g_symbolHandlerReady ? 0 : GetLastError();
}

// Caller must hold g_captureMutex.
SymbolHandlerStatus symbolHandlerStatusLocked()
{
    SymbolHandlerStatus status;
    status.attempted = g_symbolHandlerTried;
    status.ready = g_symbolHandlerReady;
    status.lastErrorCode = static_cast<quint32>(g_symbolHandlerErrorCode);
    return status;
}

DWORD stackWalkMachineType()
{
#if defined(_M_ARM64) || defined(__aarch64__)
    return IMAGE_FILE_MACHINE_ARM64;
#elif defined(_M_X64) || defined(__x86_64__)
    return IMAGE_FILE_MACHINE_AMD64;
#else
    return IMAGE_FILE_MACHINE_I386;
#endif
}

void seedStackFrame(STACKFRAME64* frame, const CONTEXT& context)
{
    ZeroMemory(frame, sizeof(STACKFRAME64));
#if defined(_M_ARM64) || defined(__aarch64__)
    frame->AddrPC.Offset = context.Pc;
    frame->AddrFrame.Offset = context.Fp;
    frame->AddrStack.Offset = context.Sp;
#elif defined(_M_X64) || defined(__x86_64__)
    frame->AddrPC.Offset = context.Rip;
    frame->AddrFrame.Offset = context.Rbp;
    frame->AddrStack.Offset = context.Rsp;
#else
    frame->AddrPC.Offset = context.Eip;
    frame->AddrFrame.Offset = context.Ebp;
    frame->AddrStack.Offset = context.Esp;
#endif
    frame->AddrPC.Mode = AddrModeFlat;
    frame->AddrFrame.Mode = AddrModeFlat;
    frame->AddrStack.Mode = AddrModeFlat;
}

// Resolve module base + file name WITHOUT dbghelp, so a frame is still attributable even
// when SymInitialize failed or no PDB is present. Runs after ResumeThread.
QString moduleForAddress(quint64 address, quint64* outOffset)
{
    *outOffset = 0;
    HMODULE moduleHandle = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(address)),
            &moduleHandle)
        || moduleHandle == nullptr) {
        return QString();
    }
    *outOffset = address - static_cast<quint64>(reinterpret_cast<ULONG_PTR>(moduleHandle));
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(moduleHandle, path, MAX_PATH);
    if (length == 0) {
        return QString();
    }
    return QFileInfo(QString::fromWCharArray(path, static_cast<int>(length))).fileName();
}

// Resolve a symbol name. Runs after ResumeThread; a miss is the normal case on user
// machines (no PDB) and is reported as an empty name rather than an error.
QString symbolForAddress(quint64 address, quint64* outOffset)
{
    *outOffset = 0;
    if (!g_symbolHandlerReady) {
        return QString();
    }
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    if (!SymFromAddr(GetCurrentProcess(), address, &displacement, symbol)) {
        return QString();
    }
    *outOffset = static_cast<quint64>(displacement);
    return QString::fromLatin1(symbol->Name);
}

#endif  // Q_OS_WIN

}  // namespace

QString formatStackFrameLine(
    int index,
    quint64 address,
    const QString& moduleName,
    quint64 moduleOffset,
    const QString& symbolName,
    quint64 symbolOffset)
{
    return QStringLiteral("frame=%1 addr=0x%2 module=%3 module_offset=0x%4 symbol=%5 symbol_offset=0x%6")
        .arg(index, 2, 10, QLatin1Char('0'))
        .arg(address, 0, 16)
        .arg(moduleName.isEmpty() ? QStringLiteral("(unknown)") : moduleName)
        .arg(moduleOffset, 0, 16)
        .arg(symbolName.isEmpty() ? QStringLiteral("(nosym)") : symbolName)
        .arg(symbolOffset, 0, 16);
}

#ifdef Q_OS_WIN

bool registerStackWalkTargetThread(QString* outReason)
{
    std::lock_guard<std::mutex> lock(g_captureMutex);
    if (g_targetThread != nullptr) {
        if (outReason != nullptr) {
            *outReason = QStringLiteral("already_registered");
        }
        return true;
    }
    HANDLE duplicated = nullptr;
    // GetCurrentThread() is a pseudo-handle: handing it to the watchdog thread would make
    // the watchdog suspend ITSELF. Duplicate it into a real, thread-agnostic handle.
    if (!DuplicateHandle(
            GetCurrentProcess(),
            GetCurrentThread(),
            GetCurrentProcess(),
            &duplicated,
            THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME,
            FALSE,
            0)
        || duplicated == nullptr) {
        if (outReason != nullptr) {
            *outReason = QStringLiteral("duplicate_handle_failed_err=%1").arg(GetLastError());
        }
        return false;
    }
    g_targetThread = duplicated;
    g_targetThreadId = GetCurrentThreadId();
    if (outReason != nullptr) {
        *outReason = QStringLiteral("registered");
    }
    return true;
}

void releaseStackWalkTargetThread()
{
    std::lock_guard<std::mutex> lock(g_captureMutex);
    if (g_targetThread != nullptr && !g_stackWalkAbandoned) {
        CloseHandle(g_targetThread);
    }
    // When a worker was abandoned it may still be inside StackWalk64 holding this handle,
    // so closing it here would be a use-after-close in that thread. Deliberately leak the
    // handle instead — same trade as leaking the thread itself.
    g_targetThread = nullptr;
    g_targetThreadId = 0;
}

bool hasStackWalkTargetThread()
{
    std::lock_guard<std::mutex> lock(g_captureMutex);
    return g_targetThread != nullptr;
}

SymbolHandlerStatus prepareStackWalkSymbols()
{
    std::lock_guard<std::mutex> lock(g_captureMutex);
    ensureSymbolHandler();
    return symbolHandlerStatusLocked();
}

SymbolHandlerStatus stackWalkSymbolStatus()
{
    std::lock_guard<std::mutex> lock(g_captureMutex);
    return symbolHandlerStatusLocked();
}

StackCaptureResult captureRegisteredThreadStack(int maxFrames)
{
    StackCaptureResult result;
    result.supported = true;
    if (maxFrames <= 0 || maxFrames > kMaxCapturedStackFrames) {
        maxFrames = kMaxCapturedStackFrames;
    }

    std::lock_guard<std::mutex> lock(g_captureMutex);
    if (g_targetThread == nullptr) {
        result.skipReason = QStringLiteral("not_registered");
        return result;
    }
    if (GetCurrentThreadId() == g_targetThreadId) {
        result.skipReason = QStringLiteral("same_thread");
        return result;
    }

    if (g_stackWalkAbandoned) {
        // A previous capture timed out and its worker was abandoned. That worker may still
        // be blocked inside StackWalk64 and may still touch the target handle, so nothing
        // here may suspend the target again.
        result.skipReason = QStringLiteral("disabled_after_timeout");
        return result;
    }

    // Before the suspension window: everything that allocates or touches the loader.
    ensureSymbolHandler();

    // Shared with the worker by value, so it stays alive even if the worker is abandoned
    // and writes into it long after this function has returned.
    auto job = std::make_shared<StackWalkJob>();
    job->thread = g_targetThread;
    job->maxFrames = maxFrames;
    QueryPerformanceFrequency(&job->frequency);

    std::thread worker(runStackWalkJob, job);

    // Bounded wait. The worker owns the suspension window; this thread only waits.
    std::unique_lock<std::mutex> jobLock(job->mutex);
    const bool finished = job->done.wait_for(
        jobLock,
        std::chrono::milliseconds(kStackWalkTimeoutMs),
        [job]() { return job->finished; });

    if (!finished) {
        jobLock.unlock();
        // The worker is wedged — almost certainly blocked in StackWalk64 against a dbghelp
        // or loader lock held by the very thread it suspended. Claim the resume before the
        // worker can, so the target comes back no matter what the worker does later.
        DWORD forcedResumeError = 0;
        if (!job->resumeClaimed.exchange(true, std::memory_order_acq_rel)) {
            if (ResumeThread(g_targetThread) == static_cast<DWORD>(-1)) {
                forcedResumeError = GetLastError();
            }
        }
        // Abandon the worker rather than join it: joining would reintroduce the very
        // unbounded wait this design exists to remove.
        worker.detach();
        g_stackWalkAbandoned = true;
        result.timedOut = true;
        result.skipReason = QStringLiteral("timeout");
        result.lastErrorCode = forcedResumeError;
        result.suspendedUs = static_cast<qint64>(kStackWalkTimeoutMs) * 1000;
        return result;
    }

    jobLock.unlock();
    worker.join();
    // ---- Target is resumed and the worker has exited. join() gives the happens-before
    // edge, so every job field the worker wrote is readable here without the lock, and
    // allocation plus symbolization are safe again. ----

    const int frameCount = job->frameCount;
    result.suspendedUs = job->suspendedUs;

    if (job->suspendFailed) {
        result.skipReason = QStringLiteral("suspend_failed");
        result.lastErrorCode = job->suspendErrorCode;
        return result;
    }
    if (job->resumeFailed) {
        // The GUI thread is now stuck suspended; say so loudly rather than silently.
        result.skipReason = QStringLiteral("resume_failed");
        result.lastErrorCode = job->resumeErrorCode;
        return result;
    }
    if (job->contextFailureCode != 0) {
        result.skipReason = QStringLiteral("get_thread_context_failed");
        result.lastErrorCode = job->contextFailureCode;
        return result;
    }
    if (frameCount == 0) {
        result.skipReason = QStringLiteral("no_frames");
        return result;
    }

    result.frames.reserve(frameCount);
    for (int index = 0; index < frameCount; ++index) {
        quint64 moduleOffset = 0;
        quint64 symbolOffset = 0;
        const QString moduleName = moduleForAddress(job->addresses[index], &moduleOffset);
        const QString symbolName = symbolForAddress(job->addresses[index], &symbolOffset);
        result.frames.append(formatStackFrameLine(
            index, job->addresses[index], moduleName, moduleOffset, symbolName, symbolOffset));
    }
    result.frameCount = frameCount;
    result.captured = true;
    return result;
}

#else  // !Q_OS_WIN

bool registerStackWalkTargetThread(QString* outReason)
{
    if (outReason != nullptr) {
        *outReason = QStringLiteral("unsupported_platform");
    }
    return false;
}

void releaseStackWalkTargetThread()
{
}

bool hasStackWalkTargetThread()
{
    return false;
}

SymbolHandlerStatus prepareStackWalkSymbols()
{
    // No dbghelp here, so nothing is ever attempted — distinct from a Windows run where
    // the attempt happened and failed.
    return SymbolHandlerStatus{};
}

SymbolHandlerStatus stackWalkSymbolStatus()
{
    return SymbolHandlerStatus{};
}

StackCaptureResult captureRegisteredThreadStack(int maxFrames)
{
    Q_UNUSED(maxFrames);
    StackCaptureResult result;
    result.skipReason = QStringLiteral("unsupported_platform");
    return result;
}

#endif  // Q_OS_WIN

}  // namespace miacode::diag
