#include <QTextStream>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "audio/PreviewAudioWorker.h"

namespace miacode::preview_audio {

class PreviewAudioNonGuiBarrierSpecAccess
{
public:
    static void setWaitEnrollmentObserver(
        PreviewAudioWorker& worker,
        std::function<void()> observer)
    {
        worker.setNonGuiWaitEnrollmentObserverForTest(std::move(observer));
    }
};

}  // namespace miacode::preview_audio

namespace {

using namespace miacode::preview_audio;
using namespace std::chrono_literals;

bool expect(bool condition, const char* message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

class FakeBackend final : public PreviewAudioBackend
{
public:
    explicit FakeBackend(std::shared_ptr<struct BackendCallGate> gate = {})
        : gate_(std::move(gate))
    {
    }

    QString backendId() const override { return QStringLiteral("non-gui-barrier-fake"); }
    bool canBePrimary(QString*) const override { return true; }
    void setWarmupResolvedPaths(const QString&, const QString&, const QString&) override {}
    void reloadAssets(const PreviewAudioSettings&) override {}
    bool audioEngineInitialized() const override { return true; }
    void setChartPath(const QString&) override {}
    void setBackgroundTrackOffsetSeconds(double) override {}
    void setBackgroundTrackPlaybackRate(double) override {}
    void applyLevels(const PreviewAudioSettings&) override {}
    void configureTimeline(const QVector<TimelineNoteMarker>&, double, const PreviewTimingSettings&) override {}
    void clearTimeline() override {}
    void setPlaybackTransactionId(quint64) override {}
    double preparePreviewPlaybackTransaction(double second, bool, double) override { return second; }
    void commitPreparedPreviewPlayback() override {}
    void cancelPreparedPreviewPlayback() override {}
    double preparedStartSecond() const override { return 0.0; }
    void applyPausedPreviewState(const QVector<TimelineNoteMarker>&, bool, double, double, const PreviewTimingSettings&) override {}
    double startPreviewPlaybackTransaction(double second, bool, double) override { return second; }
    PausePreviewResult capturePausedPreviewTransaction() override { return {}; }
    PausePreviewResult pausePreviewPlaybackTransaction() override { return {}; }
    double resumeRetainedPreviewPlaybackTransaction() override { return 0.0; }
    double seekRetainedPreviewPlaybackTransaction(double second, bool) override { return second; }
    void resetRetainedPreviewPlaybackTransaction(double) override {}
    void clearRetainedPreviewPlaybackTransaction() override {}
    RetainedPlaybackMode retainedPlaybackMode() const override { return RetainedPlaybackMode::None; }
    RetainedBgmState retainedBgmState() const override { return RetainedBgmState::NoneLoaded; }
    double authoritativePlaybackSecond() const override { return 0.0; }
    double syncPreviewPlaybackClockTransaction(double second) override { return second; }
    void resetCursor(double, bool) override {}
    void drainEvents(double) override {}
    void pauseTouchholdVoices() override {}
    void restoreTouchholdVoices(double) override {}
    void syncBackgroundTrack(double) override {}
    bool hasBackgroundTrack() const override { return false; }
    bool isBackgroundTrackRunning() const override { return false; }
    void startBackgroundTrack(double) override;
    void seekBackgroundTrack(double) override {}
    void pauseBackgroundTrack() override {}
    double backgroundPlaybackSecond() const override { return 0.0; }
    bool audition(const QString&, double) override { return true; }
    void stopAll() override {}

private:
    std::shared_ptr<struct BackendCallGate> gate_;
};

struct FactoryGate {
    std::mutex mutex;
    std::condition_variable cv;
    bool factoryEntered = false;
    bool releaseFactory = false;
};

struct BackendCallGate {
    std::mutex mutex;
    std::condition_variable cv;
    bool startBackgroundEntered = false;
    bool releaseStartBackground = false;
};

void FakeBackend::startBackgroundTrack(double)
{
    if (!gate_) {
        return;
    }
    std::unique_lock lock(gate_->mutex);
    gate_->startBackgroundEntered = true;
    gate_->cv.notify_all();
    gate_->cv.wait(lock, [&] { return gate_->releaseStartBackground; });
}

PreviewAudioBackendFactory fakeFactory(const std::shared_ptr<BackendCallGate>& gate)
{
    return [gate] { return std::make_unique<FakeBackend>(gate); };
}

bool waitForStartBackground(const std::shared_ptr<BackendCallGate>& gate)
{
    std::unique_lock lock(gate->mutex);
    return gate->cv.wait_for(lock, 1s, [&] { return gate->startBackgroundEntered; });
}

void releaseStartBackground(const std::shared_ptr<BackendCallGate>& gate)
{
    {
        std::lock_guard lock(gate->mutex);
        gate->releaseStartBackground = true;
    }
    gate->cv.notify_all();
}

template <typename Function>
NonGuiBarrierWaitStatus waitFromPlainThread(Function&& function)
{
    std::promise<NonGuiBarrierWaitStatus> result;
    std::future<NonGuiBarrierWaitStatus> future = result.get_future();
    std::thread caller([&result, function = std::forward<Function>(function)]() mutable {
        result.set_value(function());
    });
    caller.join();
    return future.get();
}

bool verifyNonGuiBarrier(QTextStream& err)
{
    PreviewAudioWorker worker(
        [] { return std::make_unique<FakeBackend>(); },
        {},
        std::this_thread::get_id());
    bool ok = true;

    ok &= expect(
        waitFromPlainThread([&] { return worker.waitForReadyForNonGui(1s); })
            == NonGuiBarrierWaitStatus::Ready,
        "plain thread observes worker readiness without Qt events",
        err);

    PreviewAudioCommand reload = makeOrdered(CommandKind::ReloadAssets);
    reload.identity.assetGeneration = 1;
    const WorkerPostResult reloadPost = worker.post(std::move(reload));
    const WorkerPostResult startPost = worker.post(makeOrdered(CommandKind::Start, 1, 1));
    ok &= expect(reloadPost.accepted && startPost.accepted,
                 "fake worker accepts reload and start commands",
                 err);
    ok &= expect(
        waitFromPlainThread([&] {
            return worker.waitForCompletionForNonGui(reloadPost.sequence, 1s);
        }) == NonGuiBarrierWaitStatus::Completed,
        "plain thread observes reload completion without queued signals",
        err);
    std::promise<NonGuiBarrierWaitStatus> firstStartObserver;
    std::future<NonGuiBarrierWaitStatus> firstStartObserverFuture = firstStartObserver.get_future();
    std::promise<NonGuiBarrierWaitStatus> secondStartObserver;
    std::future<NonGuiBarrierWaitStatus> secondStartObserverFuture = secondStartObserver.get_future();
    std::thread firstStartWaiter([&] {
        firstStartObserver.set_value(
            worker.waitForCompletionForNonGui(startPost.sequence, 1s));
    });
    std::thread secondStartWaiter([&] {
        secondStartObserver.set_value(
            worker.waitForCompletionForNonGui(startPost.sequence, 1s));
    });
    firstStartWaiter.join();
    secondStartWaiter.join();
    ok &= expect(firstStartObserverFuture.get() == NonGuiBarrierWaitStatus::Completed
                     && secondStartObserverFuture.get() == NonGuiBarrierWaitStatus::Completed,
                 "concurrent plain-thread observers retain delivered completion status",
                 err);

    ok &= expect(
        waitFromPlainThread([&] {
            return worker.waitForCompletionForNonGui(startPost.sequence + 1000, 20ms);
        }) == NonGuiBarrierWaitStatus::Timeout,
        "non-gui completion barrier reports timeout",
        err);
    ok &= expect(
        worker.waitForReadyForNonGui(1ms) == NonGuiBarrierWaitStatus::FacadeOwningThread,
        "facade-owning thread is rejected from non-gui readiness barrier",
        err);
    ok &= expect(
        worker.waitForCompletionForNonGui(startPost.sequence, 1ms)
            == NonGuiBarrierWaitStatus::FacadeOwningThread,
        "facade-owning thread is rejected from non-gui completion barrier",
        err);

    std::promise<void> completionWaiterEnrolled;
    std::future<void> completionWaiterEnrolledFuture = completionWaiterEnrolled.get_future();
    PreviewAudioNonGuiBarrierSpecAccess::setWaitEnrollmentObserver(
        worker,
        [&] { completionWaiterEnrolled.set_value(); });
    std::promise<NonGuiBarrierWaitStatus> shutdownResult;
    std::future<NonGuiBarrierWaitStatus> shutdownFuture = shutdownResult.get_future();
    std::thread waiter([&] {
        shutdownResult.set_value(
            worker.waitForCompletionForNonGui(startPost.sequence + 2000, 5s));
    });
    completionWaiterEnrolledFuture.get();
    worker.shutdownAndJoin();
    waiter.join();
    ok &= expect(shutdownFuture.get() == NonGuiBarrierWaitStatus::ShuttingDown,
                 "shutdown wakes non-gui completion waiters",
                 err);
    return ok;
}

bool verifyDisplacedCommandBarrier(QTextStream& err)
{
    bool ok = true;
    {
        const auto gate = std::make_shared<BackendCallGate>();
        PreviewAudioWorker worker(fakeFactory(gate), {}, std::this_thread::get_id());
        ok &= expect(
            waitFromPlainThread([&] { return worker.waitForReadyForNonGui(1s); })
                == NonGuiBarrierWaitStatus::Ready,
            "latest replacement worker becomes ready",
            err);
        const WorkerPostResult blocker = worker.post(makeOrdered(CommandKind::StartBackground, 1));
        ok &= expect(blocker.accepted && waitForStartBackground(gate),
                     "ordinary backend call holds latest replacement worker",
                     err);

        const WorkerPostResult first = worker.post(makeLatest(CommandKind::SyncBackgroundTrack, 1, 3.0));
        const WorkerPostResult second = worker.post(makeLatest(CommandKind::SyncBackgroundTrack, 1, 4.0));
        ok &= expect(first.accepted && second.accepted && second.replaced,
                     "second latest command reports replacement",
                     err);
        ok &= expect(
            waitFromPlainThread([&] {
                return worker.waitForCompletionForNonGui(first.sequence, 1s);
            }) == NonGuiBarrierWaitStatus::CompletionRetired,
            "replaced latest command is retired instead of timing out",
            err);

        releaseStartBackground(gate);
        ok &= expect(
            waitFromPlainThread([&] {
                return worker.waitForCompletionForNonGui(second.sequence, 1s);
            }) == NonGuiBarrierWaitStatus::Completed,
            "replacement latest command completes after ordinary call releases",
            err);
        worker.shutdownAndJoin();
    }
    {
        const auto gate = std::make_shared<BackendCallGate>();
        PreviewAudioWorker worker(fakeFactory(gate), {}, std::this_thread::get_id());
        ok &= expect(
            waitFromPlainThread([&] { return worker.waitForReadyForNonGui(1s); })
                == NonGuiBarrierWaitStatus::Ready,
            "device-pause coalescing worker becomes ready",
            err);
        const WorkerPostResult blocker = worker.post(makeOrdered(CommandKind::StartBackground, 1));
        ok &= expect(blocker.accepted && waitForStartBackground(gate),
                     "ordinary backend call holds device-pause coalescing worker",
                     err);

        const WorkerPostResult first = worker.post(makeHigh(CommandKind::DeviceChangePause, 1, 9));
        const WorkerPostResult second = worker.post(makeHigh(CommandKind::DeviceChangePause, 1, 9));
        ok &= expect(first.accepted && second.accepted && second.coalesced,
                     "second device pause reports coalescing",
                     err);
        ok &= expect(
            waitFromPlainThread([&] {
                return worker.waitForCompletionForNonGui(second.sequence, 1s);
            }) == NonGuiBarrierWaitStatus::CompletionRetired,
            "coalesced incoming device pause is retired instead of timing out",
            err);

        releaseStartBackground(gate);
        worker.shutdownAndJoin();
    }
    return ok;
}

bool verifyShutdownWakesReadyBarrier(QTextStream& err)
{
    const auto gate = std::make_shared<FactoryGate>();
    PreviewAudioWorker worker(
        [gate] {
            std::unique_lock lock(gate->mutex);
            gate->factoryEntered = true;
            gate->cv.notify_all();
            gate->cv.wait(lock, [&] { return gate->releaseFactory; });
            return std::make_unique<FakeBackend>();
        },
        {},
        std::this_thread::get_id());
    bool ok = true;
    {
        std::unique_lock lock(gate->mutex);
        ok &= expect(gate->cv.wait_for(lock, 1s, [&] { return gate->factoryEntered; }),
                     "fake worker blocks before ready publication",
                     err);
    }

    std::promise<void> waiterEnrolled;
    std::future<void> waiterEnrolledFuture = waiterEnrolled.get_future();
    PreviewAudioNonGuiBarrierSpecAccess::setWaitEnrollmentObserver(
        worker,
        [&] { waiterEnrolled.set_value(); });
    std::promise<NonGuiBarrierWaitStatus> waiterResult;
    std::future<NonGuiBarrierWaitStatus> waiterResultFuture = waiterResult.get_future();
    std::thread waiter([&] {
        waiterResult.set_value(worker.waitForReadyForNonGui(5s));
    });
    waiterEnrolledFuture.get();

    std::thread shutdown([&] { worker.shutdownAndJoin(); });
    const auto shutdownDeadline = std::chrono::steady_clock::now() + 1s;
    WorkerPostResult postResult;
    do {
        postResult = worker.post(makeOrdered(CommandKind::Start, 1, 1));
        if (postResult.error == CommandError::ShuttingDown) {
            break;
        }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < shutdownDeadline);
    ok &= expect(postResult.error == CommandError::ShuttingDown,
                 "shutdown state is visible before worker startup is released",
                 err);
    ok &= expect(waiterResultFuture.wait_for(1s) == std::future_status::ready,
                 "shutdown wakes the ready barrier before the worker can progress",
                 err);
    if (waiterResultFuture.wait_for(0ms) == std::future_status::ready) {
        ok &= expect(waiterResultFuture.get() == NonGuiBarrierWaitStatus::ShuttingDown,
                     "ready barrier reports shutdown instead of stale readiness",
                     err);
    }

    {
        std::lock_guard lock(gate->mutex);
        gate->releaseFactory = true;
    }
    gate->cv.notify_all();
    waiter.join();
    shutdown.join();
    return ok;
}

}  // namespace

int main()
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    bool ok = true;
    ok &= verifyNonGuiBarrier(err);
    ok &= verifyDisplacedCommandBarrier(err);
    ok &= verifyShutdownWakesReadyBarrier(err);
    if (ok) {
        out << "PreviewAudioNonGuiBarrierSpec: PASS" << Qt::endl;
        return 0;
    }
    return 1;
}
