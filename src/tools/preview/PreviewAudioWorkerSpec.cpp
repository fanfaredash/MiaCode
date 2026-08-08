#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "audio/MiniaudioPreviewAudioBackend.h"
#include "audio/PreviewAudioWorker.h"
#ifdef MIACODE_HAS_BASS_AUDIO
#include "audio/BassPreviewAudioBackend.h"
#endif

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined"
#endif

namespace {

using namespace miacode::preview_audio;
using namespace std::chrono_literals;

using MiniaudioNativeErrorGetter = int (MiniaudioPreviewAudioBackend::*)() const noexcept;
static_assert(
    std::is_same_v<decltype(&MiniaudioPreviewAudioBackend::nativeErrorCode), MiniaudioNativeErrorGetter>,
    "MiniaudioPreviewAudioBackend must override nativeErrorCode");
#ifdef MIACODE_HAS_BASS_AUDIO
using BassNativeErrorGetter = int (BassPreviewAudioBackend::*)() const noexcept;
static_assert(
    std::is_same_v<decltype(&BassPreviewAudioBackend::nativeErrorCode), BassNativeErrorGetter>,
    "BassPreviewAudioBackend must override nativeErrorCode");
#endif

bool expect(bool condition, const char* message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool tokensAppearInOrder(const QString& source, std::initializer_list<QString> tokens)
{
    qsizetype cursor = 0;
    for (const QString& token : tokens) {
        const qsizetype found = source.indexOf(token, cursor);
        if (found < 0) {
            return false;
        }
        cursor = found + token.size();
    }
    return true;
}

bool verifyWindowsBassFxLoaderCachesImmediateErrors(QTextStream& err)
{
    QFile file(
        QStringLiteral(MIACODE_SOURCE_ROOT)
        + QStringLiteral("/src/audio/BassPreviewAudioBackend_EngineInit.cpp"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return expect(false, "BASS engine-init source is readable", err);
    }
    const QString source = QString::fromUtf8(file.readAll());
    const qsizetype functionStart = source.indexOf(
        QStringLiteral("bool BassPreviewAudioBackend::ensureBassFxLoaded()"));
    const qsizetype windowsStart = source.indexOf(QStringLiteral("#ifdef Q_OS_WIN"), functionStart);
    const qsizetype windowsEnd = source.indexOf(
        QStringLiteral("#elif defined(Q_OS_MACOS)"), windowsStart);
    if (functionStart < 0 || windowsStart < 0 || windowsEnd < 0) {
        return expect(false, "Windows BASS-FX loader branch is present", err);
    }

    const QString windowsBranch = source.mid(windowsStart, windowsEnd - windowsStart);
    const qsizetype loadFailureStart = windowsBranch.indexOf(QStringLiteral("if (module == nullptr)"));
    const qsizetype loadFailureEnd = windowsBranch.indexOf(QStringLiteral("return false;"), loadFailureStart);
    const qsizetype symbolFailureStart = windowsBranch.indexOf(QStringLiteral("if (proc == nullptr)"));
    const qsizetype symbolFailureEnd = windowsBranch.indexOf(QStringLiteral("return false;"), symbolFailureStart);
    if (loadFailureStart < 0 || loadFailureEnd < 0 || symbolFailureStart < 0 || symbolFailureEnd < 0) {
        return expect(false, "Windows BASS-FX failure branches are present", err);
    }

    const QString loadFailure = windowsBranch.mid(
        loadFailureStart, loadFailureEnd + qsizetype(QStringLiteral("return false;").size()) - loadFailureStart);
    const QString symbolFailure = windowsBranch.mid(
        symbolFailureStart, symbolFailureEnd + qsizetype(QStringLiteral("return false;").size()) - symbolFailureStart);
    bool ok = true;
    ok &= expect(tokensAppearInOrder(loadFailure,
                                    {QStringLiteral("const DWORD errorCode = ::GetLastError();"),
                                     QStringLiteral("lastNativeErrorCode_ = static_cast<int>(errorCode);"),
                                     QStringLiteral("_mc_op_.fail(")}),
                 "LoadLibraryW failure caches GetLastError before reporting", err);
    ok &= expect(tokensAppearInOrder(symbolFailure,
                                    {QStringLiteral("const DWORD errorCode = ::GetLastError();"),
                                     QStringLiteral("lastNativeErrorCode_ = static_cast<int>(errorCode);"),
                                     QStringLiteral("FreeLibrary(module);"),
                                     QStringLiteral("_mc_op_.fail(")}),
                 "GetProcAddress failure caches GetLastError before FreeLibrary", err);
    return ok;
}

struct CallRecord {
    QString name;
    std::thread::id threadId;
};

struct FakeFailureRule {
    QString method;
    bool unknownException = false;
    int nativeError = 77;
};

struct FakeState {
    void record(const QString& name)
    {
        std::lock_guard lock(mutex);
        calls.push_back({name, std::this_thread::get_id()});
        cv.notify_all();
    }

    bool waitForCall(const QString& name, std::chrono::milliseconds timeout = 2s)
    {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [&] {
            return std::any_of(calls.cbegin(), calls.cend(), [&](const CallRecord& call) {
                return call.name == name;
            });
        });
    }

    qsizetype callCount(const QString& name) const
    {
        std::lock_guard lock(mutex);
        return qsizetype(std::count_if(calls.cbegin(), calls.cend(), [&](const CallRecord& call) {
            return call.name == name;
        }));
    }

    bool waitForCallCount(
        const QString& name,
        qsizetype expectedCount,
        std::chrono::milliseconds timeout = 2s)
    {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [&] {
            return std::count_if(calls.cbegin(), calls.cend(), [&](const CallRecord& call) {
                return call.name == name;
            }) >= expectedCount;
        });
    }

    std::vector<QString> callNames() const
    {
        std::lock_guard lock(mutex);
        std::vector<QString> names;
        names.reserve(calls.size());
        for (const CallRecord& call : calls) {
            names.push_back(call.name);
        }
        return names;
    }

    mutable std::mutex mutex;
    std::condition_variable cv;
    std::vector<CallRecord> calls;
    QString blockedMethod;
    QString throwingMethod;
    QString delayedMethod;
    std::chrono::milliseconds methodDelay{0};
    std::vector<FakeFailureRule> failureRules;
    bool releaseBlockedMethod = false;
    bool engineInitialized = true;
    bool auditionAccepted = true;
    int nativeError = 77;
    int factoryAttempts = 0;
    int factoryFailuresRemaining = 0;
    int factoryNullsRemaining = 0;
    bool blockFactory = false;
    bool releaseFactory = false;
};

class FakeBackend final : public PreviewAudioBackend
{
public:
    explicit FakeBackend(std::shared_ptr<FakeState> state)
        : state_(std::move(state))
    {
        call(QStringLiteral("construct"));
    }

    ~FakeBackend() override { state_->record(QStringLiteral("destruct")); }

    QString backendId() const override
    {
        call(QStringLiteral("backendId"));
        return QStringLiteral("fake");
    }

    bool canBePrimary(QString* reason) const override
    {
        call(QStringLiteral("canBePrimary"));
        if (reason != nullptr) {
            *reason = QStringLiteral("fake ready");
        }
        return true;
    }

    int nativeErrorCode() const noexcept override
    {
        try {
            state_->record(QStringLiteral("nativeErrorCode"));
        } catch (...) {
        }
        return state_->nativeError;
    }

    void clearNativeErrorCode() noexcept override
    {
        try {
            std::lock_guard lock(state_->mutex);
            state_->nativeError = 0;
            state_->calls.push_back(
                {QStringLiteral("clearNativeErrorCode"), std::this_thread::get_id()});
            state_->cv.notify_all();
        } catch (...) {
        }
    }

    void setWarmupResolvedPaths(const QString&, const QString&, const QString&) override
    {
        call(QStringLiteral("setWarmupResolvedPaths"));
    }

    void reloadAssets(const PreviewAudioSettings&) override
    {
        call(QStringLiteral("reloadAssets"));
        std::lock_guard lock(state_->mutex);
        if (!state_->engineInitialized) {
            state_->nativeError = 77;
        }
    }

    bool audioEngineInitialized() const override
    {
        call(QStringLiteral("audioEngineInitialized"));
        std::lock_guard lock(state_->mutex);
        return state_->engineInitialized;
    }

    void setChartPath(const QString&) override { call(QStringLiteral("setChartPath")); }
    void setBackgroundTrackOffsetSeconds(double) override { call(QStringLiteral("setBackgroundOffset")); }
    void setBackgroundTrackPlaybackRate(double) override { call(QStringLiteral("setBackgroundRate")); }
    void applyPlaybackRateAtChartSecond(double, double) override { call(QStringLiteral("applyRateAtSecond")); }
    void applyLevels(const PreviewAudioSettings&) override { call(QStringLiteral("applyLevels")); }
    void configureTimeline(const QVector<TimelineNoteMarker>&, double, const PreviewTimingSettings&) override
    {
        call(QStringLiteral("configureTimeline"));
    }
    void clearTimeline() override { call(QStringLiteral("clearTimeline")); }
    void setPlaybackTransactionId(quint64) override { call(QStringLiteral("setPlaybackTransactionId")); }
    double preparePreviewPlaybackTransaction(double second, bool, double) override
    {
        call(QStringLiteral("prepare"));
        return second + 0.25;
    }
    void commitPreparedPreviewPlayback() override { call(QStringLiteral("commit")); }
    void cancelPreparedPreviewPlayback() override { call(QStringLiteral("cancel")); }
    double preparedStartSecond() const override
    {
        call(QStringLiteral("preparedStartSecond"));
        return 3.25;
    }
    void applyPausedPreviewState(const QVector<TimelineNoteMarker>&, bool, double, double, const PreviewTimingSettings&) override
    {
        call(QStringLiteral("applyPausedState"));
    }
    double startPreviewPlaybackTransaction(double second, bool, double) override
    {
        call(QStringLiteral("start"));
        return second + 0.5;
    }
    PausePreviewResult capturePausedPreviewTransaction() override
    {
        call(QStringLiteral("capturePause"));
        return {true, 4.5, RetainedPlaybackMode::PausedExact, RetainedBgmState::LoadedUsable};
    }
    PausePreviewResult pausePreviewPlaybackTransaction() override
    {
        call(QStringLiteral("pause"));
        return {true, 4.75, RetainedPlaybackMode::PausedExact, RetainedBgmState::LoadedUsable};
    }
    double resumeRetainedPreviewPlaybackTransaction() override
    {
        call(QStringLiteral("resumeRetained"));
        return 5.0;
    }
    double seekRetainedPreviewPlaybackTransaction(double second, bool) override
    {
        call(QStringLiteral("seekRetained"));
        return second + 0.75;
    }
    void resetRetainedPreviewPlaybackTransaction(double) override { call(QStringLiteral("resetRetained")); }
    void clearRetainedPreviewPlaybackTransaction() override { call(QStringLiteral("clearRetained")); }
    RetainedPlaybackMode retainedPlaybackMode() const override
    {
        call(QStringLiteral("retainedPlaybackMode"));
        return RetainedPlaybackMode::PausedExact;
    }
    RetainedBgmState retainedBgmState() const override
    {
        call(QStringLiteral("retainedBgmState"));
        return RetainedBgmState::LoadedUsable;
    }
    double authoritativePlaybackSecond() const override
    {
        call(QStringLiteral("authoritativeSecond"));
        return 6.0;
    }
    double syncPreviewPlaybackClockTransaction(double second) override
    {
        call(QStringLiteral("syncClock"));
        return second + 1.0;
    }
    void resetCursor(double, bool) override { call(QStringLiteral("resetCursor")); }
    void drainEvents(double) override { call(QStringLiteral("drainEvents")); }
    void pauseTouchholdVoices() override { call(QStringLiteral("pauseTouchhold")); }
    void restoreTouchholdVoices(double) override { call(QStringLiteral("restoreTouchhold")); }
    void syncBackgroundTrack(double) override { call(QStringLiteral("syncBackgroundTrack")); }
    bool hasBackgroundTrack() const override
    {
        call(QStringLiteral("hasBackgroundTrack"));
        return true;
    }
    bool isBackgroundTrackRunning() const override
    {
        call(QStringLiteral("isBackgroundTrackRunning"));
        return true;
    }
    void startBackgroundTrack(double) override { call(QStringLiteral("startBackground")); }
    void seekBackgroundTrack(double) override { call(QStringLiteral("seekBackground")); }
    void pauseBackgroundTrack() override { call(QStringLiteral("pauseBackground")); }
    double backgroundPlaybackSecond() const override
    {
        call(QStringLiteral("backgroundSecond"));
        return 6.5;
    }
    bool audition(const QString&, double) override
    {
        call(QStringLiteral("audition"));
        std::lock_guard lock(state_->mutex);
        if (!state_->auditionAccepted) {
            state_->nativeError = 77;
        }
        return state_->auditionAccepted;
    }
    void stopSfxVoices() override { call(QStringLiteral("stopSfxVoices")); }
    void stopAll() override { call(QStringLiteral("stopAll")); }
    void prepareForShutdown() override { call(QStringLiteral("prepareForShutdown")); }

private:
    void call(const QString& name) const
    {
        std::unique_lock lock(state_->mutex);
        state_->calls.push_back({name, std::this_thread::get_id()});
        state_->cv.notify_all();
        if (state_->delayedMethod == name && state_->methodDelay.count() > 0) {
            std::this_thread::sleep_for(state_->methodDelay);
        }
        if (state_->blockedMethod == name) {
            state_->cv.wait(lock, [&] { return state_->releaseBlockedMethod; });
        }
        const auto failure = std::find_if(
            state_->failureRules.cbegin(),
            state_->failureRules.cend(),
            [&](const FakeFailureRule& rule) { return rule.method == name; });
        if (failure != state_->failureRules.cend()) {
            state_->nativeError = failure->nativeError;
            if (failure->unknownException) {
                throw 42;
            }
            throw std::runtime_error(QStringLiteral("fake failure in %1").arg(name).toStdString());
        }
        if (state_->throwingMethod == name) {
            throw std::runtime_error(QStringLiteral("fake failure in %1").arg(name).toStdString());
        }
    }

    std::shared_ptr<FakeState> state_;
};

PreviewAudioBackendFactory fakeFactory(const std::shared_ptr<FakeState>& state)
{
    return [state]() -> std::unique_ptr<PreviewAudioBackend> {
        {
            std::unique_lock lock(state->mutex);
            ++state->factoryAttempts;
            state->calls.push_back({QStringLiteral("factory"), std::this_thread::get_id()});
            state->cv.notify_all();
            if (state->blockFactory) {
                state->cv.wait(lock, [&] { return state->releaseFactory; });
            }
            if (state->factoryFailuresRemaining > 0) {
                --state->factoryFailuresRemaining;
                throw std::runtime_error("factory failed");
            }
            if (state->factoryNullsRemaining > 0) {
                --state->factoryNullsRemaining;
                return nullptr;
            }
        }
        return std::make_unique<FakeBackend>(state);
    };
}

struct CompletionLog {
    void append(const PreviewAudioCompletion& completion)
    {
        std::lock_guard lock(mutex);
        completions.push_back(completion);
        cv.notify_all();
    }

    bool waitFor(quint64 sequence, PreviewAudioCompletion* result, std::chrono::milliseconds timeout = 2s)
    {
        std::unique_lock lock(mutex);
        if (!cv.wait_for(lock, timeout, [&] {
                return std::any_of(completions.cbegin(), completions.cend(), [&](const auto& completion) {
                    return completion.identity.sequence == sequence;
                });
            })) {
            return false;
        }
        const auto found = std::find_if(completions.cbegin(), completions.cend(), [&](const auto& completion) {
            return completion.identity.sequence == sequence;
        });
        *result = *found;
        return true;
    }

    qsizetype size() const
    {
        std::lock_guard lock(mutex);
        return qsizetype(completions.size());
    }

    mutable std::mutex mutex;
    std::condition_variable cv;
    std::vector<PreviewAudioCompletion> completions;
};

struct ThrowingCopyCallbackState {
    bool waitFor(quint64 sequence, std::chrono::milliseconds timeout = 2s)
    {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [&] {
            return std::find(sequences.cbegin(), sequences.cend(), sequence) != sequences.cend();
        });
    }

    std::atomic_bool throwOnCopy{false};
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<quint64> sequences;
};

class ThrowingCopyCallback
{
public:
    explicit ThrowingCopyCallback(std::shared_ptr<ThrowingCopyCallbackState> state)
        : state_(std::move(state))
    {
    }

    ThrowingCopyCallback(const ThrowingCopyCallback& other)
        : state_(other.state_)
    {
        if (state_->throwOnCopy.load(std::memory_order_acquire)) {
            throw std::runtime_error("persistent callback copy failure");
        }
    }

    ThrowingCopyCallback(ThrowingCopyCallback&&) noexcept = default;

    void operator()(const PreviewAudioCompletion& completion) const
    {
        std::lock_guard lock(state_->mutex);
        state_->sequences.push_back(completion.identity.sequence);
        state_->cv.notify_all();
    }

private:
    std::shared_ptr<ThrowingCopyCallbackState> state_;
};

bool waitForLifecycle(PreviewAudioWorker& worker, WorkerLifecycle lifecycle, std::chrono::milliseconds timeout = 2s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (worker.snapshot().lifecycle == lifecycle) {
            return true;
        }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

PreviewAudioCommand commandFor(CommandKind kind, quint64 generation, quint64 assetGeneration, quint64 transactionId)
{
    PreviewAudioCommand command;
    command.kind = kind;
    command.identity.generation = generation;
    command.identity.assetGeneration = assetGeneration;
    command.identity.transactionId = transactionId;
    command.chartPath = QStringLiteral("chart");
    command.trackPath = QStringLiteral("track");
    command.sfxDirectory = QStringLiteral("sfx");
    command.auditionKind = QStringLiteral("answer");
    command.second = 2.0;
    command.value = 2.0;
    command.rate = 1.25;
    command.gain = 0.8;
    command.option = true;
    return command;
}

QString expectedBackendCall(CommandKind kind)
{
    switch (kind) {
    case CommandKind::Shutdown:
        return {};
    case CommandKind::DeviceChangePause:
    case CommandKind::ManualPause:
        return QStringLiteral("pause");
    case CommandKind::StopAll:
        return QStringLiteral("stopAll");
    case CommandKind::SetWarmupResolvedPaths:
        return QStringLiteral("setWarmupResolvedPaths");
    case CommandKind::ReloadAssets:
        return QStringLiteral("reloadAssets");
    case CommandKind::SetChartPath:
        return QStringLiteral("setChartPath");
    case CommandKind::SetBackgroundOffset:
        return QStringLiteral("setBackgroundOffset");
    case CommandKind::SetBackgroundRate:
        return QStringLiteral("setBackgroundRate");
    case CommandKind::ApplyRateAtSecond:
        return QStringLiteral("applyRateAtSecond");
    case CommandKind::ApplyLevels:
        return QStringLiteral("applyLevels");
    case CommandKind::ConfigureTimeline:
        return QStringLiteral("configureTimeline");
    case CommandKind::ClearTimeline:
        return QStringLiteral("clearTimeline");
    case CommandKind::ApplyPausedState:
        return QStringLiteral("applyPausedState");
    case CommandKind::Prepare:
        return QStringLiteral("prepare");
    case CommandKind::Commit:
        return QStringLiteral("commit");
    case CommandKind::Cancel:
        return QStringLiteral("cancel");
    case CommandKind::Start:
        return QStringLiteral("start");
    case CommandKind::ResumeRetained:
        return QStringLiteral("resumeRetained");
    case CommandKind::SeekRetained:
        return QStringLiteral("seekRetained");
    case CommandKind::ResetRetained:
        return QStringLiteral("resetRetained");
    case CommandKind::ClearRetained:
        return QStringLiteral("clearRetained");
    case CommandKind::ResetCursor:
        return QStringLiteral("resetCursor");
    case CommandKind::PauseTouchhold:
        return QStringLiteral("pauseTouchhold");
    case CommandKind::RestoreTouchhold:
        return QStringLiteral("restoreTouchhold");
    case CommandKind::StartBackground:
        return QStringLiteral("startBackground");
    case CommandKind::SeekBackground:
        return QStringLiteral("seekBackground");
    case CommandKind::PauseBackground:
        return QStringLiteral("pauseBackground");
    case CommandKind::StopSfxVoices:
        return QStringLiteral("stopSfxVoices");
    case CommandKind::SyncBackgroundTrack:
        return QStringLiteral("syncBackgroundTrack");
    case CommandKind::DrainEvents:
        return QStringLiteral("drainEvents");
    case CommandKind::Audition:
        return QStringLiteral("audition");
    }
    return {};
}

bool verifyOwnershipLifecycleAndDispatch(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    state->blockFactory = true;
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });

    bool ok = true;
    ok &= expect(state->waitForCall(QStringLiteral("factory")), "factory starts on worker", err);
    const PreviewAudioSnapshot constructing = worker.snapshot();
    ok &= expect(constructing.lifecycle == WorkerLifecycle::Constructing
                     && !constructing.backendReady,
                 "worker publishes Constructing before factory returns", err);
    {
        std::lock_guard lock(state->mutex);
        state->releaseFactory = true;
        state->cv.notify_all();
    }
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "worker reaches Ready", err);
    const PreviewAudioSnapshot ready = worker.snapshot();
    ok &= expect(ready.sequence > constructing.sequence && ready.backendReady,
                 "Ready follows Constructing monotonically", err);

    const std::vector<CommandKind> kinds{
        CommandKind::SetWarmupResolvedPaths, CommandKind::ReloadAssets,
        CommandKind::SetChartPath, CommandKind::SetBackgroundOffset,
        CommandKind::SetBackgroundRate, CommandKind::ApplyRateAtSecond,
        CommandKind::ApplyLevels, CommandKind::ConfigureTimeline,
        CommandKind::ClearTimeline, CommandKind::ApplyPausedState,
        CommandKind::Prepare, CommandKind::Commit, CommandKind::Cancel,
        CommandKind::Start, CommandKind::ResumeRetained, CommandKind::SeekRetained,
        CommandKind::ResetRetained, CommandKind::ClearRetained,
        CommandKind::ResetCursor, CommandKind::PauseTouchhold,
        CommandKind::RestoreTouchhold, CommandKind::StartBackground,
        CommandKind::SeekBackground, CommandKind::PauseBackground,
        CommandKind::StopSfxVoices, CommandKind::SyncBackgroundTrack,
        CommandKind::DrainEvents, CommandKind::Audition,
        CommandKind::ManualPause, CommandKind::DeviceChangePause,
        CommandKind::StopAll,
    };

    quint64 lastSnapshotSequence = ready.sequence;
    quint64 assetGeneration = 1;
    for (CommandKind kind : kinds) {
        if (commandPolicy(kind).usesAssetGeneration && commandPolicy(kind).assetCompletionEligible) {
            ++assetGeneration;
        }
        PreviewAudioCommand command = commandFor(kind, 9, assetGeneration, 44);
        if (!commandPolicy(kind).requiresTransaction) {
            command.identity.transactionId = 0;
        }
        const std::vector<QString> callsBefore = state->callNames();
        const WorkerPostResult posted = worker.post(std::move(command));
        ok &= expect(posted.accepted && posted.sequence != 0, "dispatch command accepted", err);
        PreviewAudioCompletion completion;
        ok &= expect(completions->waitFor(posted.sequence, &completion), "dispatch completion received", err);
        ok &= expect(completion.success && completion.error == CommandError::None,
                     "dispatch completion succeeds", err);
        ok &= expect(completion.workerThreadId != 0 && completion.queueDelayNs >= 0
                         && completion.executionDurationNs >= 0,
                     "completion carries worker and timing diagnostics", err);
        if (kind == CommandKind::ManualPause || kind == CommandKind::DeviceChangePause) {
            ok &= expect(completion.pauseResult.usedBackgroundTrack
                             && completion.pauseResult.pauseSecond == 4.75
                             && completion.pauseResult.retainedMode == RetainedPlaybackMode::PausedExact
                             && completion.pauseResult.retainedBgmState == RetainedBgmState::LoadedUsable
                             && completion.value == completion.pauseResult.pauseSecond,
                         "pause completion carries the complete operation result", err);
        }
        const std::vector<QString> callsAfter = state->callNames();
        const QString expectedCall = expectedBackendCall(kind);
        ok &= expect(
            std::find(
                callsAfter.cbegin() + qsizetype(callsBefore.size()),
                callsAfter.cend(),
                expectedCall) != callsAfter.cend(),
            "command dispatch invokes its corresponding backend method",
            err);
        const PreviewAudioSnapshot snapshot = worker.snapshot();
        ok &= expect(snapshot.sequence > lastSnapshotSequence, "snapshot sequence strictly increases", err);
        lastSnapshotSequence = snapshot.sequence;
    }

    worker.shutdownAndJoin();
    const PreviewAudioSnapshot stopped = worker.snapshot();
    ok &= expect(stopped.lifecycle == WorkerLifecycle::Stopped && !stopped.backendReady
                     && !stopped.backgroundTrackAvailable && !stopped.backgroundTrackRunning,
                 "shutdown publishes Stopped with no live backend state", err);
    ok &= expect(stopped.sequence > lastSnapshotSequence, "shutdown snapshot remains monotonic", err);

    std::thread::id workerThread;
    {
        std::lock_guard lock(state->mutex);
        ok &= expect(!state->calls.empty(), "fake backend recorded calls", err);
        workerThread = state->calls.front().threadId;
        for (const CallRecord& call : state->calls) {
            ok &= expect(call.threadId == workerThread, "factory/backend calls share one thread", err);
        }
    }
    ok &= expect(workerThread != std::this_thread::get_id(), "backend owner differs from caller", err);

    const std::vector<QString> names = state->callNames();
    const auto has = [&names](const char* name) {
        return std::find(names.cbegin(), names.cend(), QString::fromLatin1(name)) != names.cend();
    };
    ok &= expect(has("factory") && has("construct") && has("prepareForShutdown") && has("destruct"),
                 "factory construction shutdown and destruction are covered", err);
    ok &= expect(has("pauseBackground") && has("pauseTouchhold") && has("stopSfxVoices")
                     && has("resetCursor"),
                 "device pause performs its complete backend safety sequence", err);
    return ok;
}

bool verifyFactoryFailureAndExplicitRecovery(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    state->factoryNullsRemaining = 1;
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });

    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Degraded), "null factory degrades safely", err);
    PreviewAudioCommand warmup = commandFor(CommandKind::SetWarmupResolvedPaths, 0, 7, 0);
    const WorkerPostResult warmupPost = worker.post(warmup);
    PreviewAudioCompletion warmupCompletion;
    ok &= expect(completions->waitFor(warmupPost.sequence, &warmupCompletion)
                     && warmupCompletion.success,
                 "warmup paths are stored while degraded", err);

    const WorkerPostResult unavailablePost = worker.post(commandFor(CommandKind::StopAll, 1, 0, 0));
    PreviewAudioCompletion unavailable;
    ok &= expect(completions->waitFor(unavailablePost.sequence, &unavailable)
                     && unavailable.error == CommandError::BackendUnavailable,
                 "unrelated degraded command completes unavailable", err);
    {
        std::lock_guard lock(state->mutex);
        ok &= expect(state->factoryAttempts == 1, "unrelated command does not retry factory", err);
    }

    const WorkerPostResult reloadPost = worker.post(commandFor(CommandKind::ReloadAssets, 0, 7, 0));
    PreviewAudioCompletion reload;
    ok &= expect(completions->waitFor(reloadPost.sequence, &reload) && reload.success,
                 "explicit reload recovers missing backend", err);
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "recovered worker publishes Ready", err);
    const std::vector<QString> names = state->callNames();
    const auto secondFactory = std::find(names.cbegin() + 1, names.cend(), QStringLiteral("factory"));
    const auto warmupCall = std::find(names.cbegin(), names.cend(), QStringLiteral("setWarmupResolvedPaths"));
    const auto reloadCall = std::find(names.cbegin(), names.cend(), QStringLiteral("reloadAssets"));
    ok &= expect(secondFactory != names.cend() && warmupCall != names.cend() && reloadCall != names.cend()
                     && secondFactory < warmupCall && warmupCall < reloadCall,
                 "retry applies stored warmup before reload", err);
    worker.shutdownAndJoin();

    auto throwingState = std::make_shared<FakeState>();
    throwingState->factoryFailuresRemaining = 1;
    PreviewAudioWorker throwingWorker(fakeFactory(throwingState), {});
    ok &= expect(waitForLifecycle(throwingWorker, WorkerLifecycle::Degraded)
                     && throwingWorker.snapshot().lastError == CommandError::BackendUnavailable,
                 "throwing factory degrades safely", err);
    throwingWorker.shutdownAndJoin();
    return ok;
}

bool verifyReloadFailureAndAssetStaleness(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });
    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "stale test worker ready", err);

    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod = QStringLiteral("reloadAssets");
        state->releaseBlockedMethod = false;
    }
    const WorkerPostResult reloadA = worker.post(commandFor(CommandKind::ReloadAssets, 0, 10, 0));
    ok &= expect(state->waitForCall(QStringLiteral("reloadAssets")), "reload A enters backend", err);
    const WorkerPostResult warmupB = worker.post(commandFor(CommandKind::SetWarmupResolvedPaths, 0, 11, 0));
    const WorkerPostResult reloadB = worker.post(commandFor(CommandKind::ReloadAssets, 0, 11, 0));
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod.clear();
        state->releaseBlockedMethod = true;
        state->cv.notify_all();
    }
    PreviewAudioCompletion completionA;
    PreviewAudioCompletion completionWarmup;
    PreviewAudioCompletion completionB;
    ok &= expect(completions->waitFor(reloadA.sequence, &completionA)
                     && completionA.error == CommandError::Stale && !completionA.success,
                 "old blocked reload completes Stale", err);
    ok &= expect(completions->waitFor(warmupB.sequence, &completionWarmup), "new warmup completes", err);
    ok &= expect(completions->waitFor(reloadB.sequence, &completionB) && completionB.success,
                 "exact latest reload succeeds", err);
    const PreviewAudioSnapshot recovered = worker.snapshot();
    ok &= expect(recovered.lifecycle == WorkerLifecycle::Ready
                     && recovered.identity.assetGeneration == 11,
                 "only latest asset generation publishes Ready", err);

    {
        std::lock_guard lock(state->mutex);
        state->engineInitialized = false;
    }
    const WorkerPostResult failedReload = worker.post(commandFor(CommandKind::ReloadAssets, 0, 12, 0));
    const WorkerPostResult dependent = worker.post(commandFor(CommandKind::Audition, 0, 12, 0));
    PreviewAudioCompletion failedReloadCompletion;
    PreviewAudioCompletion dependentCompletion;
    ok &= expect(completions->waitFor(failedReload.sequence, &failedReloadCompletion)
                     && failedReloadCompletion.error == CommandError::BackendFailure
                     && failedReloadCompletion.nativeErrorCode == 77,
                 "uninitialized reload reports backend failure", err);
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Degraded), "reload failure publishes Degraded", err);
    const PreviewAudioSnapshot degraded = worker.snapshot();
    ok &= expect(!degraded.backendReady && !degraded.backgroundTrackAvailable
                     && !degraded.backgroundTrackRunning,
                 "degraded snapshot clears live backend state", err);
    ok &= expect(completions->waitFor(dependent.sequence, &dependentCompletion)
                     && dependentCompletion.error == CommandError::BackendUnavailable
                     && dependentCompletion.nativeErrorCode == 77,
                 "load-dependent queued work resolves unavailable", err);
    ok &= expect(worker.snapshot().nativeErrorCode == 77,
                 "unavailable dependent failure preserves the degraded backend native error", err);
    const PreviewAudioSnapshot degradedCause = worker.snapshot();

    {
        std::lock_guard lock(state->mutex);
        state->engineInitialized = true;
    }
    const qsizetype warmupCallsBeforeRecovery = state->callCount(
        QStringLiteral("setWarmupResolvedPaths"));
    const qsizetype reloadCallsBeforeRecovery = state->callCount(QStringLiteral("reloadAssets"));
    const qsizetype callsBeforeRecovery = qsizetype(state->callNames().size());
    const WorkerPostResult recoveryWarmup = worker.post(
        commandFor(CommandKind::SetWarmupResolvedPaths, 0, 13, 0));
    PreviewAudioCompletion recoveryWarmupCompletion;
    ok &= expect(completions->waitFor(recoveryWarmup.sequence, &recoveryWarmupCompletion)
                     && recoveryWarmupCompletion.success,
                 "recovery applies a newer warmup command first", err);
    const PreviewAudioSnapshot awaitingRecoveryReload = worker.snapshot();
    ok &= expect(awaitingRecoveryReload.lifecycle == WorkerLifecycle::Degraded
                     && awaitingRecoveryReload.lastError == degradedCause.lastError
                     && awaitingRecoveryReload.lastSuccess == degradedCause.lastSuccess
                     && awaitingRecoveryReload.detail == degradedCause.detail
                     && awaitingRecoveryReload.nativeErrorCode == degradedCause.nativeErrorCode,
                 "recovery warmup preserves the degraded cause until reload begins", err);
    const WorkerPostResult recoveredReload = worker.post(commandFor(CommandKind::ReloadAssets, 0, 13, 0));
    PreviewAudioCompletion recoveredCompletion;
    ok &= expect(completions->waitFor(recoveredReload.sequence, &recoveredCompletion)
                     && recoveredCompletion.success
                     && waitForLifecycle(worker, WorkerLifecycle::Ready),
                 "later warmup/reload path recovers Ready", err);
    ok &= expect(
        state->callCount(QStringLiteral("setWarmupResolvedPaths")) == warmupCallsBeforeRecovery + 1
            && state->callCount(QStringLiteral("reloadAssets")) == reloadCallsBeforeRecovery + 1,
        "recovery invokes one warmup followed by one reload",
        err);
    const std::vector<QString> recoveryCalls = state->callNames();
    const auto recoveryBegin = recoveryCalls.cbegin() + callsBeforeRecovery;
    const auto recoveryWarmupCall = std::find(
        recoveryBegin,
        recoveryCalls.cend(),
        QStringLiteral("setWarmupResolvedPaths"));
    const auto recoveryReloadCall = recoveryWarmupCall == recoveryCalls.cend()
        ? recoveryCalls.cend()
        : std::find(
              std::next(recoveryWarmupCall),
              recoveryCalls.cend(),
              QStringLiteral("reloadAssets"));
    ok &= expect(recoveryWarmupCall != recoveryCalls.cend()
                     && recoveryReloadCall != recoveryCalls.cend()
                     && recoveryWarmupCall < recoveryReloadCall,
                 "recovery backend call order is warmup then reload", err);
    worker.shutdownAndJoin();
    return ok;
}

bool verifyAcceptedAssetGenerationGuardsFinalPublication(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });
    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "publication gate worker ready", err);

    const qsizetype backendIdCalls = state->callCount(QStringLiteral("backendId"));
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod = QStringLiteral("backendId");
        state->releaseBlockedMethod = false;
    }
    const WorkerPostResult reloadA = worker.post(commandFor(CommandKind::ReloadAssets, 0, 20, 0));
    ok &= expect(
        state->waitForCallCount(QStringLiteral("backendId"), backendIdCalls + 1),
        "reload A blocks in final backend snapshot capture",
        err);
    const WorkerPostResult warmupB = worker.post(
        commandFor(CommandKind::SetWarmupResolvedPaths, 0, 21, 0));
    ok &= expect(warmupB.accepted, "asset generation A+1 accepted during getter window", err);
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod.clear();
        state->releaseBlockedMethod = true;
        state->cv.notify_all();
    }

    PreviewAudioCompletion completionA;
    PreviewAudioCompletion completionB;
    ok &= expect(completions->waitFor(reloadA.sequence, &completionA)
                     && completionA.error == CommandError::Stale && !completionA.success,
                 "accepted A+1 prevents reload A final publication", err);
    ok &= expect(completions->waitFor(warmupB.sequence, &completionB) && completionB.success,
                 "newer asset command continues after stale reload", err);
    const PreviewAudioSnapshot awaitingReloadB = worker.snapshot();
    ok &= expect(awaitingReloadB.identity.assetGeneration == 21
                     && awaitingReloadB.lifecycle == WorkerLifecycle::Loading
                     && !awaitingReloadB.backendReady,
                 "newer warmup stays not-ready until its reload completes", err);

    const WorkerPostResult reloadB = worker.post(
        commandFor(CommandKind::ReloadAssets, 0, 21, 0));
    PreviewAudioCompletion reloadBCompletion;
    ok &= expect(reloadB.accepted
                     && completions->waitFor(reloadB.sequence, &reloadBCompletion)
                     && reloadBCompletion.success
                     && waitForLifecycle(worker, WorkerLifecycle::Ready)
                     && worker.snapshot().backendReady,
                 "newer reload is the operation that restores Ready", err);
    worker.shutdownAndJoin();
    return ok;
}

bool verifyQueuedReloadUsesLoadingGenerationGate(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    struct StartGate {
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false;
        bool release = false;
    };
    auto startGate = std::make_shared<StartGate>();
    PreviewAudioWorker worker(
        fakeFactory(state),
        [completions, startGate](const PreviewAudioCompletion& completion) {
            completions->append(completion);
            if (completion.kind != CommandKind::Start) {
                return;
            }
            std::unique_lock lock(startGate->mutex);
            startGate->entered = true;
            startGate->cv.notify_all();
            startGate->cv.wait(lock, [&] { return startGate->release; });
        });

    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "queued reload gate worker ready", err);
    const WorkerPostResult start = worker.post(commandFor(CommandKind::Start, 1, 0, 1));
    ok &= expect(start.accepted, "non-asset start accepted before queued reloads", err);
    {
        std::unique_lock lock(startGate->mutex);
        ok &= expect(startGate->cv.wait_for(lock, 2s, [&] { return startGate->entered; }),
                     "worker blocks in non-asset start completion", err);
    }

    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod = QStringLiteral("setWarmupResolvedPaths");
        state->releaseBlockedMethod = false;
    }
    const qsizetype reloadCallsBefore = state->callCount(QStringLiteral("reloadAssets"));
    const WorkerPostResult reloadA = worker.post(commandFor(CommandKind::ReloadAssets, 0, 70, 0));
    const WorkerPostResult warmupB = worker.post(
        commandFor(CommandKind::SetWarmupResolvedPaths, 0, 71, 0));
    ok &= expect(reloadA.accepted && warmupB.accepted,
                 "older reload and newer warmup queue behind start", err);
    {
        std::lock_guard lock(startGate->mutex);
        startGate->release = true;
        startGate->cv.notify_all();
    }

    PreviewAudioCompletion reloadCompletion;
    ok &= expect(
        completions->waitFor(reloadA.sequence, &reloadCompletion)
            && reloadCompletion.error == CommandError::Stale
            && reloadCompletion.detail.contains(QStringLiteral("loading publication")),
        "queued stale reload is rejected by the Loading publication gate",
        err);
    ok &= expect(state->waitForCall(QStringLiteral("setWarmupResolvedPaths")),
                 "newer warmup blocks after stale reload completion", err);
    const PreviewAudioSnapshot betweenCompletions = worker.snapshot();
    ok &= expect(
        betweenCompletions.lifecycle != WorkerLifecycle::Loading
            || betweenCompletions.identity.assetGeneration != 70,
        "stale reload cannot publish Loading after newer warmup is accepted",
        err);
    ok &= expect(state->callCount(QStringLiteral("reloadAssets")) == reloadCallsBefore,
                 "stale reload never reaches the backend", err);
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod.clear();
        state->releaseBlockedMethod = true;
        state->cv.notify_all();
    }
    PreviewAudioCompletion warmupCompletion;
    ok &= expect(completions->waitFor(warmupB.sequence, &warmupCompletion)
                     && warmupCompletion.success,
                 "newer warmup completes after Loading gate assertion", err);
    worker.shutdownAndJoin();
    return ok;
}

bool verifyRejectedAssetPostDoesNotSupersedeReload(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });
    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "rejected asset worker ready", err);
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod = QStringLiteral("reloadAssets");
        state->releaseBlockedMethod = false;
    }
    const WorkerPostResult reloadA = worker.post(commandFor(CommandKind::ReloadAssets, 0, 40, 0));
    ok &= expect(state->waitForCall(QStringLiteral("reloadAssets")), "reload blocks before queue fill", err);
    for (qsizetype index = 0; index < PreviewAudioCommandQueue::kOrderedCapacity; ++index) {
        ok &= expect(
            worker.post(commandFor(CommandKind::Prepare, 1, 0, quint64(index + 1))).accepted,
            "ordered queue fill accepted",
            err);
    }
    const WorkerPostResult rejected = worker.post(
        commandFor(CommandKind::ReloadAssets, 0, 41, 0));
    ok &= expect(!rejected.accepted && rejected.error == CommandError::QueueFull,
                 "newer asset post is rejected when ordered queue is full", err);
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod.clear();
        state->releaseBlockedMethod = true;
        state->cv.notify_all();
    }
    PreviewAudioCompletion completionA;
    ok &= expect(completions->waitFor(reloadA.sequence, &completionA) && completionA.success,
                 "rejected newer asset state does not stale active reload", err);
    worker.shutdownAndJoin();
    return ok;
}

bool verifyReloadExceptionPublishesDegraded(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });
    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "reload exception worker ready", err);
    {
        std::lock_guard lock(state->mutex);
        state->throwingMethod = QStringLiteral("reloadAssets");
    }
    const WorkerPostResult reload = worker.post(commandFor(CommandKind::ReloadAssets, 0, 30, 0));
    PreviewAudioCompletion completion;
    ok &= expect(completions->waitFor(reload.sequence, &completion)
                     && completion.error == CommandError::BackendFailure
                     && completion.nativeErrorCode == 0
                     && waitForLifecycle(worker, WorkerLifecycle::Degraded),
                 "reload C++ exception has no stale native error and publishes Degraded", err);
    const PreviewAudioSnapshot degraded = worker.snapshot();
    ok &= expect(!degraded.backgroundTrackAvailable && !degraded.backgroundTrackRunning,
                 "reload exception clears live backend snapshot state", err);
    worker.shutdownAndJoin();
    return ok;
}

bool verifyNativeErrorPropagationAndClearing(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });
    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready),
                 "native-error worker ready", err);

    {
        std::lock_guard lock(state->mutex);
        state->auditionAccepted = false;
    }
    const WorkerPostResult rejected = worker.post(commandFor(CommandKind::Audition, 1, 1, 0));
    PreviewAudioCompletion rejectedCompletion;
    ok &= expect(completions->waitFor(rejected.sequence, &rejectedCompletion)
                     && !rejectedCompletion.success
                     && rejectedCompletion.error == CommandError::BackendFailure
                     && rejectedCompletion.nativeErrorCode == 77
                     && worker.snapshot().nativeErrorCode == 77,
                 "audition rejection publishes the backend native error", err);

    {
        std::lock_guard lock(state->mutex);
        state->auditionAccepted = true;
    }
    const WorkerPostResult succeeded = worker.post(commandFor(CommandKind::SetChartPath, 2, 1, 0));
    PreviewAudioCompletion succeededCompletion;
    ok &= expect(completions->waitFor(succeeded.sequence, &succeededCompletion)
                     && succeededCompletion.success
                     && succeededCompletion.nativeErrorCode == 0
                     && worker.snapshot().nativeErrorCode == 0,
                 "successful completion clears a stale native error", err);

    {
        std::lock_guard lock(state->mutex);
        state->throwingMethod = QStringLiteral("setChartPath");
    }
    const WorkerPostResult failed = worker.post(commandFor(CommandKind::SetChartPath, 3, 1, 0));
    PreviewAudioCompletion failedCompletion;
    ok &= expect(completions->waitFor(failed.sequence, &failedCompletion)
                     && failedCompletion.nativeErrorCode == 0,
                 "command exception cannot inherit a previous command's native error", err);
    {
        std::lock_guard lock(state->mutex);
        state->throwingMethod.clear();
        state->blockedMethod = QStringLiteral("reloadAssets");
        state->releaseBlockedMethod = false;
    }
    const WorkerPostResult reload = worker.post(commandFor(CommandKind::ReloadAssets, 0, 2, 0));
    ok &= expect(reload.accepted && state->waitForCall(QStringLiteral("reloadAssets")),
                 "reload reaches backend after Loading publication", err);
    const PreviewAudioSnapshot loading = worker.snapshot();
    ok &= expect(loading.lifecycle == WorkerLifecycle::Loading
                     && loading.lastError == CommandError::None
                     && loading.lastSuccess
                     && loading.nativeErrorCode == 0,
                 "successful Loading lifecycle clears stale native error state", err);
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod.clear();
        state->releaseBlockedMethod = true;
        state->cv.notify_all();
    }
    PreviewAudioCompletion reloadCompletion;
    ok &= expect(completions->waitFor(reload.sequence, &reloadCompletion)
                     && reloadCompletion.success,
                 "reload completes after native-error Loading assertion", err);

    {
        std::lock_guard lock(state->mutex);
        state->engineInitialized = false;
    }
    const WorkerPostResult failedReload = worker.post(
        commandFor(CommandKind::ReloadAssets, 0, 3, 0));
    PreviewAudioCompletion failedReloadCompletion;
    ok &= expect(completions->waitFor(failedReload.sequence, &failedReloadCompletion)
                     && failedReloadCompletion.error == CommandError::BackendFailure
                     && failedReloadCompletion.nativeErrorCode == 77
                     && worker.snapshot().nativeErrorCode == 77,
                 "uninitialized reload publishes native error to completion and snapshot", err);
    worker.shutdownAndJoin();
    return ok;
}

bool verifyLifecycleFailuresCaptureNativeError(QTextStream& err)
{
    bool ok = true;
    auto unreadyState = std::make_shared<FakeState>();
    unreadyState->engineInitialized = false;
    PreviewAudioWorker unreadyWorker(fakeFactory(unreadyState), {});
    ok &= expect(waitForLifecycle(unreadyWorker, WorkerLifecycle::Degraded),
                 "uninitialized backend publishes Degraded", err);
    const PreviewAudioSnapshot unready = unreadyWorker.snapshot();
    ok &= expect(unready.lastError == CommandError::BackendUnavailable
                     && !unready.lastSuccess
                     && unready.nativeErrorCode == 77,
                 "uninitialized backend lifecycle captures native error", err);
    unreadyWorker.shutdownAndJoin();

    auto shutdownState = std::make_shared<FakeState>();
    PreviewAudioWorker shutdownWorker(fakeFactory(shutdownState), {});
    ok &= expect(waitForLifecycle(shutdownWorker, WorkerLifecycle::Ready),
                 "shutdown native-error worker ready", err);
    {
        std::lock_guard lock(shutdownState->mutex);
        shutdownState->throwingMethod = QStringLiteral("prepareForShutdown");
    }
    shutdownWorker.shutdownAndJoin();
    const PreviewAudioSnapshot stopped = shutdownWorker.snapshot();
    ok &= expect(stopped.lifecycle == WorkerLifecycle::Stopped
                     && stopped.lastError == CommandError::BackendFailure
                     && !stopped.lastSuccess
                     && stopped.nativeErrorCode == 0,
                 "shutdown C++ failure cannot inherit a stale native error", err);
    return ok;
}

bool verifyProductionNativeErrorCachesStartClear(QTextStream& err)
{
    bool ok = true;
    {
        PreviewAudioWorker worker(
            [] { return std::make_unique<MiniaudioPreviewAudioBackend>(); },
            {});
        ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Degraded),
                     "uninitialized production miniaudio worker publishes Degraded", err);
        const PreviewAudioSnapshot snapshot = worker.snapshot();
        ok &= expect(snapshot.nativeErrorCode == 0,
                     "miniaudio native-error cache starts clear on its worker thread", err);
        worker.shutdownAndJoin();
    }
#ifdef MIACODE_HAS_BASS_AUDIO
    {
        PreviewAudioWorker worker(
            [] { return std::make_unique<BassPreviewAudioBackend>(); },
            {});
        ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Degraded),
                     "uninitialized production BASS worker publishes Degraded", err);
        const PreviewAudioSnapshot snapshot = worker.snapshot();
        ok &= expect(snapshot.nativeErrorCode == 0,
                     "BASS native-error cache starts clear on its worker thread", err);
        worker.shutdownAndJoin();
    }
#endif
    return ok;
}

bool verifyExceptionBoundaryAndReservedPauseTiming(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });
    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "exception test worker ready", err);

    {
        std::lock_guard lock(state->mutex);
        state->throwingMethod = QStringLiteral("setChartPath");
    }
    const WorkerPostResult failed = worker.post(commandFor(CommandKind::SetChartPath, 1, 1, 0));
    PreviewAudioCompletion failure;
    ok &= expect(completions->waitFor(failed.sequence, &failure)
                     && failure.error == CommandError::BackendFailure
                     && failure.nativeErrorCode == 0
                     && failure.detail.contains(QStringLiteral("fake failure")),
                 "backend C++ exception becomes a typed completion without stale native error", err);
    const std::vector<CallRecord> failureCalls = [&] {
        std::lock_guard lock(state->mutex);
        return state->calls;
    }();
    const auto nativeErrorCall = std::find_if(
        failureCalls.cbegin(),
        failureCalls.cend(),
        [](const CallRecord& call) { return call.name == QStringLiteral("nativeErrorCode"); });
    quint64 expectedWorkerThreadId = nativeErrorCall == failureCalls.cend()
        ? 0
        : quint64(std::hash<std::thread::id>{}(nativeErrorCall->threadId));
    if (expectedWorkerThreadId == 0 && nativeErrorCall != failureCalls.cend()) {
        expectedWorkerThreadId = 1;
    }
    ok &= expect(nativeErrorCall != failureCalls.cend()
                     && failure.workerThreadId == expectedWorkerThreadId,
                 "native error capture and exception completion share the worker thread", err);
    {
        std::lock_guard lock(state->mutex);
        state->throwingMethod.clear();
        state->blockedMethod = QStringLiteral("applyLevels");
        state->releaseBlockedMethod = false;
    }

    std::promise<WorkerPostResult> blockedPostPromise;
    std::future<WorkerPostResult> blockedPostFuture = blockedPostPromise.get_future();
    std::thread blockedPoster([&] {
        blockedPostPromise.set_value(
            worker.post(commandFor(CommandKind::ApplyLevels, 2, 2, 0)));
    });
    const bool backendGateEntered = state->waitForCall(QStringLiteral("applyLevels"));
    const bool blockedPostReturned =
        blockedPostFuture.wait_for(2s) == std::future_status::ready;
    if (!backendGateEntered || !blockedPostReturned) {
        std::lock_guard lock(state->mutex);
        state->blockedMethod.clear();
        state->releaseBlockedMethod = true;
        state->cv.notify_all();
    }
    blockedPoster.join();
    const WorkerPostResult blocked = blockedPostFuture.get();
    ok &= expect(backendGateEntered, "ordinary command blocks inside fake only", err);
    ok &= expect(blockedPostReturned,
                 "ordinary post returns while its backend call remains blocked", err);
    if (!backendGateEntered || !blockedPostReturned) {
        worker.shutdownAndJoin();
        return false;
    }

    std::promise<WorkerPostResult> pausePostPromise;
    std::future<WorkerPostResult> pausePostFuture = pausePostPromise.get_future();
    std::thread pausePoster([&] {
        pausePostPromise.set_value(
            worker.post(commandFor(CommandKind::ManualPause, 3, 2, 55)));
    });
    const bool pausePostReturned = pausePostFuture.wait_for(2s) == std::future_status::ready;
    bool backendGateStillBlocked = false;
    {
        std::lock_guard lock(state->mutex);
        backendGateStillBlocked = !state->releaseBlockedMethod;
        if (!pausePostReturned) {
            state->blockedMethod.clear();
            state->releaseBlockedMethod = true;
            state->cv.notify_all();
        }
    }
    pausePoster.join();
    const WorkerPostResult pause = pausePostFuture.get();
    ok &= expect(pausePostReturned && backendGateStillBlocked && pause.accepted,
                 "reserved pause post returns while the backend gate remains blocked", err);
    if (!pausePostReturned) {
        worker.shutdownAndJoin();
        return false;
    }

    // Hold a known interval only to validate the reported queue-delay measurement.
    std::this_thread::sleep_for(60ms);
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod.clear();
        state->releaseBlockedMethod = true;
        state->cv.notify_all();
    }
    PreviewAudioCompletion blockedCompletion;
    PreviewAudioCompletion pauseCompletion;
    ok &= expect(completions->waitFor(blocked.sequence, &blockedCompletion), "blocked command completes", err);
    ok &= expect(completions->waitFor(pause.sequence, &pauseCompletion) && pauseCompletion.success,
                 "worker survives and executes reserved pause", err);
    ok &= expect(pauseCompletion.queueDelayNs >= 40'000'000,
                 "reserved pause records native-call queue delay", err);
    const std::vector<QString> names = state->callNames();
    const auto levels = std::find(names.cbegin(), names.cend(), QStringLiteral("applyLevels"));
    const auto pauseCall = std::find(names.cbegin(), names.cend(), QStringLiteral("pause"));
    ok &= expect(levels != names.cend() && pauseCall != names.cend() && levels < pauseCall,
                 "reserved pause executes immediately after uninterruptible call", err);
    worker.shutdownAndJoin();
    return ok;
}

bool verifyWarmupTimingIncludesSnapshotGetters(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });
    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "warmup timing worker ready", err);
    {
        std::lock_guard lock(state->mutex);
        state->delayedMethod = QStringLiteral("backendId");
        state->methodDelay = 50ms;
    }
    const WorkerPostResult warmup = worker.post(
        commandFor(CommandKind::SetWarmupResolvedPaths, 0, 60, 0));
    PreviewAudioCompletion completion;
    ok &= expect(completions->waitFor(warmup.sequence, &completion) && completion.success,
                 "delayed-getter warmup completes", err);
    ok &= expect(completion.executionDurationNs >= 40'000'000,
                 "warmup execution timing includes snapshot getter work", err);
    worker.shutdownAndJoin();
    return ok;
}

bool verifyDevicePauseRetainsCoreResultWhenCleanupFails(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });

    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready),
                 "device-pause cleanup worker ready", err);
    const std::vector<QString> expectedCalls{
        QStringLiteral("pause"),
        QStringLiteral("pauseBackground"),
        QStringLiteral("pauseTouchhold"),
        QStringLiteral("stopSfxVoices"),
        QStringLiteral("resetCursor"),
    };
    struct PauseFailureCase {
        std::vector<FakeFailureRule> failures;
        QString expectedDetail;
        int expectedNativeError = 0;
        bool corePauseSucceeds = false;
    };
    const std::vector<PauseFailureCase> cases{
        {{{QStringLiteral("setPlaybackTransactionId"), false, 100}},
         QStringLiteral("fake failure in setPlaybackTransactionId"), 100, true},
        {{{QStringLiteral("pause"), false, 101}},
         QStringLiteral("fake failure in pause"), 101, false},
        {{{QStringLiteral("pauseBackground"), false, 102}},
         QStringLiteral("fake failure in pauseBackground"), 102, true},
        {{{QStringLiteral("pauseTouchhold"), false, 103}},
         QStringLiteral("fake failure in pauseTouchhold"), 103, true},
        {{{QStringLiteral("stopSfxVoices"), false, 104}},
         QStringLiteral("fake failure in stopSfxVoices"), 104, true},
        {{{QStringLiteral("resetCursor"), false, 105}},
         QStringLiteral("fake failure in resetCursor"), 105, true},
        {{{QStringLiteral("pause"), false, 201},
          {QStringLiteral("pauseBackground"), true, 202}},
         QStringLiteral("fake failure in pause"), 201, false},
        {{{QStringLiteral("pauseBackground"), true, 301},
          {QStringLiteral("pauseTouchhold"), false, 302}},
         QStringLiteral("unknown backend command failure"), 301, true},
    };
    quint64 generation = 1;
    for (const PauseFailureCase& testCase : cases) {
        const qsizetype callsBefore = qsizetype(state->callNames().size());
        {
            std::lock_guard lock(state->mutex);
            state->failureRules = testCase.failures;
            state->nativeError = 0;
        }
        PreviewAudioCommand command = commandFor(CommandKind::DeviceChangePause, generation++, 1, 70);
        command.identity.pauseToken = generation;
        const WorkerPostResult posted = worker.post(std::move(command));
        PreviewAudioCompletion completion;
        ok &= expect(posted.accepted && completions->waitFor(posted.sequence, &completion),
                     "device-pause cleanup failure completes", err);
        ok &= expect(!completion.success
                         && completion.error == CommandError::BackendFailure
                         && completion.detail == testCase.expectedDetail
                         && completion.nativeErrorCode == testCase.expectedNativeError,
                     "device-pause preserves the first failure detail and native error", err);
        if (testCase.corePauseSucceeds) {
            ok &= expect(completion.pauseResult.usedBackgroundTrack
                             && completion.pauseResult.pauseSecond == 4.75
                             && completion.pauseResult.retainedMode == RetainedPlaybackMode::PausedExact
                             && completion.pauseResult.retainedBgmState == RetainedBgmState::LoadedUsable
                             && completion.value == 4.75,
                         "cleanup failure preserves the core pause result", err);
        } else {
            ok &= expect(!completion.pauseResult.usedBackgroundTrack
                             && completion.pauseResult.pauseSecond == 0.0
                             && completion.pauseResult.retainedMode == RetainedPlaybackMode::None
                             && completion.pauseResult.retainedBgmState == RetainedBgmState::NoneLoaded
                             && completion.value == 0.0,
                         "core pause failure leaves the default pause result", err);
        }
        const std::vector<QString> allCalls = state->callNames();
        std::vector<QString> observedCalls;
        for (auto it = allCalls.cbegin() + callsBefore; it != allCalls.cend(); ++it) {
            if (std::find(expectedCalls.cbegin(), expectedCalls.cend(), *it)
                != expectedCalls.cend()) {
                observedCalls.push_back(*it);
            }
        }
        ok &= expect(observedCalls == expectedCalls,
                     "device-pause runs the core pause and every cleanup in order", err);
        {
            std::lock_guard lock(state->mutex);
            state->failureRules.clear();
        }
    }

    worker.shutdownAndJoin();
    return ok;
}

bool verifyShutdownWaitsForAuthorizedCallback(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    struct CallbackGate {
        std::mutex mutex;
        std::condition_variable cv;
        bool entered = false;
        bool release = false;
    };
    auto gate = std::make_shared<CallbackGate>();
    PreviewAudioWorker worker(fakeFactory(state), [gate](const PreviewAudioCompletion& completion) {
        if (completion.kind != CommandKind::SetChartPath) {
            return;
        }
        std::unique_lock lock(gate->mutex);
        gate->entered = true;
        gate->cv.notify_all();
        gate->cv.wait(lock, [&] { return gate->release; });
    });
    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "callback gate worker ready", err);
    worker.post(commandFor(CommandKind::SetChartPath, 1, 1, 0));
    {
        std::unique_lock lock(gate->mutex);
        ok &= expect(gate->cv.wait_for(lock, 2s, [&] { return gate->entered; }),
                     "completion callback enters before shutdown", err);
    }

    std::thread shutdownThread([&] { worker.shutdownAndJoin(); });
    bool producerGateClosed = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        PreviewAudioCommand pause = commandFor(CommandKind::DeviceChangePause, 3, 0, 55);
        pause.identity.pauseToken = 91;
        const WorkerPostResult posted = worker.post(std::move(pause));
        if (!posted.accepted && posted.error == CommandError::ShuttingDown) {
            producerGateClosed = true;
            break;
        }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    ok &= expect(producerGateClosed,
                 "shutdown closes producer gate before an authorized callback exits", err);
    {
        std::lock_guard lock(gate->mutex);
        gate->release = true;
        gate->cv.notify_all();
    }
    shutdownThread.join();
    return ok;
}

bool verifyWorkerThreadShutdownIsRejected(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    struct ReentrantShutdownResult {
        std::mutex mutex;
        std::condition_variable cv;
        bool callbackCompleted = false;
        bool caughtLogicError = false;
        QString detail;
    };
    auto result = std::make_shared<ReentrantShutdownResult>();
    PreviewAudioWorker* workerAddress = nullptr;
    PreviewAudioWorker worker(
        fakeFactory(state),
        [&workerAddress, result](const PreviewAudioCompletion& completion) {
            if (completion.kind != CommandKind::SetChartPath) {
                return;
            }

            bool caughtLogicError = false;
            QString detail;
            try {
                workerAddress->shutdownAndJoin();
            } catch (const std::logic_error& error) {
                caughtLogicError = true;
                detail = QString::fromUtf8(error.what());
            } catch (const std::exception& error) {
                detail = QString::fromUtf8(error.what());
            }

            std::lock_guard lock(result->mutex);
            result->caughtLogicError = caughtLogicError;
            result->detail = std::move(detail);
            result->callbackCompleted = true;
            result->cv.notify_all();
        });
    workerAddress = &worker;

    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "reentrant shutdown worker ready", err);
    worker.post(commandFor(CommandKind::SetChartPath, 1, 1, 0));
    {
        std::unique_lock lock(result->mutex);
        ok &= expect(
            result->cv.wait_for(lock, 2s, [&] { return result->callbackCompleted; }),
            "worker-thread shutdown fails without hanging the callback",
            err);
        ok &= expect(
            result->caughtLogicError
                && result->detail.contains(QStringLiteral("worker thread")),
            "worker-thread shutdown reports a clear logic_error",
            err);
    }

    worker.shutdownAndJoin();
    ok &= expect(worker.snapshot().lifecycle == WorkerLifecycle::Stopped,
                 "owner thread can join after rejected callback shutdown", err);
    return ok;
}

bool verifyShutdownGateAndIdempotence(QTextStream& err)
{
    auto state = std::make_shared<FakeState>();
    auto completions = std::make_shared<CompletionLog>();
    PreviewAudioWorker worker(fakeFactory(state), [completions](const auto& completion) {
        completions->append(completion);
    });
    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready), "shutdown test worker ready", err);
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod = QStringLiteral("setChartPath");
        state->releaseBlockedMethod = false;
    }
    worker.post(commandFor(CommandKind::SetChartPath, 1, 1, 0));
    ok &= expect(state->waitForCall(QStringLiteral("setChartPath")), "shutdown blocker entered", err);
    const qsizetype syncCallsBeforeShutdown = state->callCount(QStringLiteral("syncBackgroundTrack"));
    const qsizetype auditionCallsBeforeShutdown = state->callCount(QStringLiteral("audition"));
    const WorkerPostResult queuedSync = worker.post(
        commandFor(CommandKind::SyncBackgroundTrack, 1, 1, 0));
    const WorkerPostResult queuedAudition = worker.post(
        commandFor(CommandKind::Audition, 0, 1, 0));
    ok &= expect(queuedSync.accepted && queuedAudition.accepted,
                 "latest sync and audition are queued before shutdown", err);

    std::thread shutdownThread([&] { worker.shutdownAndJoin(); });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    WorkerPostResult rejected;
    do {
        rejected = worker.post(commandFor(CommandKind::StopAll, 2, 1, 0));
        if (!rejected.accepted) {
            break;
        }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    ok &= expect(!rejected.accepted && rejected.error == CommandError::ShuttingDown,
                 "shutdown rejects new producers", err);
    const qsizetype callbacksAtGate = completions->size();
    {
        std::lock_guard lock(state->mutex);
        state->blockedMethod.clear();
        state->releaseBlockedMethod = true;
        state->cv.notify_all();
    }
    shutdownThread.join();
    worker.shutdownAndJoin();
    ok &= expect(worker.snapshot().lifecycle == WorkerLifecycle::Stopped,
                 "shutdown and join are idempotent", err);
    ok &= expect(completions->size() == callbacksAtGate,
                 "callback delivery is disabled before shutdown draining", err);
    const std::vector<QString> names = state->callNames();
    ok &= expect(std::count(names.cbegin(), names.cend(), QStringLiteral("prepareForShutdown")) == 1
                     && std::count(names.cbegin(), names.cend(), QStringLiteral("destruct")) == 1,
                 "shutdown prepares and destroys backend exactly once", err);
    ok &= expect(state->callCount(QStringLiteral("syncBackgroundTrack")) == syncCallsBeforeShutdown
                     && state->callCount(QStringLiteral("audition")) == auditionCallsBeforeShutdown,
                 "shutdown drains latest and audition commands without executing them", err);
    const auto prepare = std::find(names.cbegin(), names.cend(), QStringLiteral("prepareForShutdown"));
    const auto destruct = std::find(names.cbegin(), names.cend(), QStringLiteral("destruct"));
    ok &= expect(prepare != names.cend() && destruct != names.cend() && prepare < destruct,
                 "backend prepareForShutdown precedes destruction", err);
    return ok;
}

int runThrowingCopyCallbackChild(QTextStream& err)
{
    auto backendState = std::make_shared<FakeState>();
    auto callbackState = std::make_shared<ThrowingCopyCallbackState>();
    PreviewAudioWorker::CompletionCallback callback = ThrowingCopyCallback(callbackState);
    PreviewAudioWorker worker(fakeFactory(backendState), std::move(callback));

    bool ok = true;
    ok &= expect(waitForLifecycle(worker, WorkerLifecycle::Ready),
                 "throwing-copy child worker ready", err);
    callbackState->throwOnCopy.store(true, std::memory_order_release);

    const WorkerPostResult first = worker.post(commandFor(CommandKind::SetChartPath, 1, 1, 0));
    ok &= expect(first.accepted && callbackState->waitFor(first.sequence),
                 "throwing-copy callback receives first completion", err);
    const WorkerPostResult second = worker.post(commandFor(CommandKind::StopAll, 2, 1, 0));
    ok &= expect(second.accepted && callbackState->waitFor(second.sequence),
                 "worker continues after callback copy would throw", err);
    worker.shutdownAndJoin();
    return ok ? 0 : 2;
}

bool verifyThrowingCallbackCopyCannotEscapeWorker(QTextStream& err)
{
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(
        QCoreApplication::applicationFilePath(),
        {QStringLiteral("--throwing-copy-callback-child")});
    if (!child.waitForStarted(2'000)) {
        return expect(false, "throwing-copy child process starts", err);
    }
    if (!child.waitForFinished(5'000)) {
        child.kill();
        child.waitForFinished();
        return expect(false, "throwing-copy child process finishes", err);
    }
    const bool exitedNormally = child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
    if (!exitedNormally) {
        err << child.readAll() << Qt::endl;
    }
    return expect(exitedNormally,
                  "persistent throwing-copy callback cannot terminate the worker process", err);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);
    if (QCoreApplication::arguments().contains(QStringLiteral("--throwing-copy-callback-child"))) {
        return runThrowingCopyCallbackChild(err);
    }

    bool ok = true;
    ok &= verifyOwnershipLifecycleAndDispatch(err);
    ok &= verifyFactoryFailureAndExplicitRecovery(err);
    ok &= verifyReloadFailureAndAssetStaleness(err);
    ok &= verifyAcceptedAssetGenerationGuardsFinalPublication(err);
    ok &= verifyQueuedReloadUsesLoadingGenerationGate(err);
    ok &= verifyRejectedAssetPostDoesNotSupersedeReload(err);
    ok &= verifyReloadExceptionPublishesDegraded(err);
    ok &= verifyNativeErrorPropagationAndClearing(err);
    ok &= verifyLifecycleFailuresCaptureNativeError(err);
    ok &= verifyProductionNativeErrorCachesStartClear(err);
    ok &= verifyWindowsBassFxLoaderCachesImmediateErrors(err);
    ok &= verifyExceptionBoundaryAndReservedPauseTiming(err);
    ok &= verifyWarmupTimingIncludesSnapshotGetters(err);
    ok &= verifyDevicePauseRetainsCoreResultWhenCleanupFails(err);
    ok &= verifyShutdownWaitsForAuthorizedCallback(err);
    ok &= verifyWorkerThreadShutdownIsRejected(err);
    ok &= verifyShutdownGateAndIdempotence(err);
    ok &= verifyThrowingCallbackCopyCannotEscapeWorker(err);
    if (ok) {
        out << "PreviewAudioWorkerSpec: PASS" << Qt::endl;
        return 0;
    }
    return 1;
}
