#include "Mmcss.h"

#include "DebugOptions.h"

#include <QMutex>
#include <QMutexLocker>

#ifdef Q_OS_WIN
#include <windows.h>
#include <avrt.h>
#endif

namespace miacode::mmcss {

namespace {

// Cross-thread snapshot of the most recent registration attempt. Updated by
// registerCurrentThread (any thread) and read by lastRegistrationStatus (any thread).
QMutex& statusMutex()
{
    static QMutex m;
    return m;
}
LastRegistrationStatus& statusStorage()
{
    static LastRegistrationStatus s;
    return s;
}
void recordRegistrationOutcome(const RegistrationResult& result)
{
    QMutexLocker lock(&statusMutex());
    LastRegistrationStatus& s = statusStorage();
    s.everAttempted = true;
    if (result.registered) {
        s.everRegistered = true;
    }
    s.lastTaskClass = result.taskClassUsed;
    s.lastSkipReason = result.skipReason;
    s.lastErrorCode = result.lastErrorCode;
}

}  // namespace

LastRegistrationStatus lastRegistrationStatus()
{
    QMutexLocker lock(&statusMutex());
    return statusStorage();
}

#ifdef Q_OS_WIN

namespace {

// Per-thread storage for the MMCSS task handle so we can release it on the same thread
// that acquired it. AvRevertMmThreadCharacteristics requires the original handle.
thread_local HANDLE g_threadMmcssHandle = nullptr;
thread_local DWORD g_threadMmcssTaskIndex = 0;
thread_local bool g_threadMmcssActive = false;

bool tryRegisterTaskClass(const QString& taskClass, RegistrationResult* outResult)
{
    DWORD taskIndex = 0;
    const std::wstring wideName = taskClass.toStdWString();
    HANDLE handle = ::AvSetMmThreadCharacteristicsW(wideName.c_str(), &taskIndex);
    if (handle == nullptr) {
        outResult->lastErrorCode = static_cast<quint32>(::GetLastError());
        return false;
    }
    // Bump priority within the class. AVRT_PRIORITY_HIGH gives the thread the highest
    // band MMCSS exposes without claiming the "critical" tier, which is reserved for
    // genuine pro-audio low-latency paths and can starve other threads.
    ::AvSetMmThreadPriority(handle, AVRT_PRIORITY_HIGH);

    g_threadMmcssHandle = handle;
    g_threadMmcssTaskIndex = taskIndex;
    g_threadMmcssActive = true;

    outResult->registered = true;
    outResult->taskClassUsed = taskClass;
    outResult->lastErrorCode = 0;
    return true;
}

}  // namespace

RegistrationResult registerCurrentThread(const QString& preferredTaskClass)
{
    RegistrationResult result;
    if (g_threadMmcssActive) {
        result.registered = true;
        result.taskClassUsed = QStringLiteral("(already registered)");
        recordRegistrationOutcome(result);
        return result;
    }
    if (miacode::debug_options::envFlagEnabled("MIACODE_DISABLE_MMCSS")) {
        result.skipReason = QStringLiteral("MIACODE_DISABLE_MMCSS=1");
        recordRegistrationOutcome(result);
        return result;
    }

    const QString primary = preferredTaskClass.trimmed().isEmpty()
        ? QStringLiteral("Games")
        : preferredTaskClass;
    if (tryRegisterTaskClass(primary, &result)) {
        recordRegistrationOutcome(result);
        return result;
    }

    // Some Windows installs don't expose every task class (e.g. server SKUs may lack
    // "Games"). Fall back to "Audio" which is virtually always present.
    if (primary != QStringLiteral("Audio")) {
        const quint32 firstError = result.lastErrorCode;
        if (tryRegisterTaskClass(QStringLiteral("Audio"), &result)) {
            recordRegistrationOutcome(result);
            return result;
        }
        result.skipReason = QStringLiteral("AvSetMmThreadCharacteristicsW failed for both '%1' and 'Audio' (first errno=%2, second errno=%3)")
            .arg(primary)
            .arg(firstError)
            .arg(result.lastErrorCode);
        recordRegistrationOutcome(result);
        return result;
    }

    result.skipReason = QStringLiteral("AvSetMmThreadCharacteristicsW failed for '%1' (errno=%2)")
        .arg(primary)
        .arg(result.lastErrorCode);
    recordRegistrationOutcome(result);
    return result;
}

void unregisterCurrentThread()
{
    if (!g_threadMmcssActive || g_threadMmcssHandle == nullptr) {
        return;
    }
    ::AvRevertMmThreadCharacteristics(g_threadMmcssHandle);
    g_threadMmcssHandle = nullptr;
    g_threadMmcssTaskIndex = 0;
    g_threadMmcssActive = false;
}

#else  // Q_OS_WIN

RegistrationResult registerCurrentThread(const QString& /*preferredTaskClass*/)
{
    RegistrationResult result;
    result.skipReason = QStringLiteral("MMCSS is Windows-only");
    return result;
}

void unregisterCurrentThread() {}

#endif  // Q_OS_WIN

}  // namespace miacode::mmcss
