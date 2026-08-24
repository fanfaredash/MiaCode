#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QTextStream>

#include "audio/PreviewBassDeviceLease.h"

namespace {

using miacode::preview_audio::BassDeviceLeaseApi;
using miacode::preview_audio::PreviewBassDeviceLease;

using namespace std::chrono_literals;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

class FakeBassDevice final
{
public:
    BassDeviceLeaseApi api()
    {
        return {
            [this] { return getDevice(); },
            [this] { return initialize(); },
            [this] { free(); },
        };
    }

    void setExistingDevice()
    {
        std::lock_guard lock(mutex_);
        device_ = 0;
    }

    void setInitSucceeds(bool succeeds)
    {
        std::lock_guard lock(mutex_);
        initSucceeds_ = succeeds;
    }

    void beginDecodeWork()
    {
        {
            std::lock_guard lock(mutex_);
            decoding_ = true;
        }
        cv_.notify_all();
    }

    bool waitForDecodeWork(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [this] { return decoding_; });
    }

    void simulateDecodeWork(std::chrono::milliseconds duration)
    {
        beginDecodeWork();
        std::this_thread::sleep_for(duration);
    }

    int initCalls() const
    {
        std::lock_guard lock(mutex_);
        return initCalls_;
    }

    int freeCalls() const
    {
        std::lock_guard lock(mutex_);
        return freeCalls_;
    }

    int maxNativeCallsInFlight() const
    {
        std::lock_guard lock(mutex_);
        return maxNativeCallsInFlight_;
    }

private:
    quint32 getDevice()
    {
        NativeCallScope scope(*this);
        std::lock_guard lock(mutex_);
        return device_;
    }

    bool initialize()
    {
        NativeCallScope scope(*this);
        std::lock_guard lock(mutex_);
        ++initCalls_;
        if (!initSucceeds_) {
            return false;
        }
        device_ = 0;
        return true;
    }

    void free()
    {
        NativeCallScope scope(*this);
        std::lock_guard lock(mutex_);
        ++freeCalls_;
        device_ = BassDeviceLeaseApi::kNoDevice;
    }

    class NativeCallScope final
    {
    public:
        explicit NativeCallScope(FakeBassDevice& device)
            : device_(device)
        {
            {
                std::lock_guard lock(device_.mutex_);
                ++device_.nativeCallsInFlight_;
                device_.maxNativeCallsInFlight_ = std::max(
                    device_.maxNativeCallsInFlight_,
                    device_.nativeCallsInFlight_);
            }
            std::this_thread::sleep_for(2ms);
        }

        ~NativeCallScope()
        {
            std::lock_guard lock(device_.mutex_);
            --device_.nativeCallsInFlight_;
        }

    private:
        FakeBassDevice& device_;
    };

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    quint32 device_ = BassDeviceLeaseApi::kNoDevice;
    bool initSucceeds_ = true;
    bool decoding_ = false;
    int initCalls_ = 0;
    int freeCalls_ = 0;
    int nativeCallsInFlight_ = 0;
    int maxNativeCallsInFlight_ = 0;
};

bool verifyOwnedDeviceRefcount(QTextStream& err)
{
    FakeBassDevice device;
    auto first = PreviewBassDeviceLease::acquire(device.api());
    auto second = PreviewBassDeviceLease::acquire(device.api());

    if (!require(first.acquired() && second.acquired(), QStringLiteral("leases should acquire initialized device"), err)
        || !require(device.initCalls() == 1, QStringLiteral("one initialized device should initialize once"), err)) {
        return false;
    }

    first.release();
    if (!require(device.freeCalls() == 0, QStringLiteral("non-final release must not free device"), err)) {
        return false;
    }
    second.release();
    return require(device.freeCalls() == 1, QStringLiteral("final release should free initialized device once"), err);
}

bool verifyInitFailureDoesNotAcquire(QTextStream& err)
{
    FakeBassDevice device;
    device.setInitSucceeds(false);
    auto lease = PreviewBassDeviceLease::acquire(device.api());

    return require(!lease.acquired(), QStringLiteral("failed BASS_Init should not produce a lease"), err)
        && require(device.initCalls() == 1, QStringLiteral("failed acquire should try initialization once"), err)
        && require(device.freeCalls() == 0, QStringLiteral("failed initialization must not free a device"), err);
}

bool verifyBorrowedDeviceIsNeverFreed(QTextStream& err)
{
    FakeBassDevice device;
    device.setExistingDevice();
    {
        auto lease = PreviewBassDeviceLease::acquire(device.api());
        if (!require(lease.acquired(), QStringLiteral("existing BASS device should be borrowable"), err)
            || !require(lease.borrowedExistingDevice(), QStringLiteral("existing BASS device should stay borrowed"), err)) {
            return false;
        }
    }

    return require(device.initCalls() == 0, QStringLiteral("borrowed device must not initialize again"), err)
        && require(device.freeCalls() == 0, QStringLiteral("borrowed device must not be freed"), err);
}

bool verifyConcurrentLifecycleSerialization(QTextStream& err)
{
    FakeBassDevice device;
    std::atomic_bool start{false};
    std::atomic_bool acquired{true};
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int index = 0; index < 8; ++index) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            auto lease = PreviewBassDeviceLease::acquire(device.api());
            if (!lease.acquired()) {
                acquired.store(false, std::memory_order_release);
                return;
            }
            std::this_thread::sleep_for(5ms);
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }

    return require(acquired.load(std::memory_order_acquire), QStringLiteral("concurrent acquires should all succeed"), err)
        && require(device.initCalls() == 1, QStringLiteral("concurrent acquires should initialize once"), err)
        && require(device.freeCalls() == 1, QStringLiteral("concurrent final release should free once"), err)
        && require(
            device.maxNativeCallsInFlight() == 1,
            QStringLiteral("BASS_GetDevice, BASS_Init, and BASS_Free must be serialized"),
            err);
}

bool verifyLeaseLockDoesNotCoverDecode(QTextStream& err)
{
    FakeBassDevice device;
    auto first = PreviewBassDeviceLease::acquire(device.api());
    if (!require(first.acquired(), QStringLiteral("initial lease should acquire device"), err)) {
        return false;
    }

    std::thread decoder([&] {
        auto lease = PreviewBassDeviceLease::acquire(device.api());
        if (lease.acquired()) {
            device.simulateDecodeWork(200ms);
        }
    });
    if (!require(
            device.waitForDecodeWork(100ms),
            QStringLiteral("second lease should acquire before simulated decode begins"),
            err)) {
        decoder.join();
        return false;
    }

    const auto startedAt = std::chrono::steady_clock::now();
    auto probe = PreviewBassDeviceLease::acquire(device.api());
    const auto acquireDuration = std::chrono::steady_clock::now() - startedAt;
    const bool acquiredBeforeDecodeCompleted = acquireDuration < 100ms;
    decoder.join();

    return require(probe.acquired(), QStringLiteral("lease should remain available during decode"), err)
        && require(
            acquiredBeforeDecodeCompleted,
            QStringLiteral("lease mutex must be released before simulated decode work"),
            err);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyOwnedDeviceRefcount(err)
        || !verifyInitFailureDoesNotAcquire(err)
        || !verifyBorrowedDeviceIsNeverFreed(err)
        || !verifyConcurrentLifecycleSerialization(err)
        || !verifyLeaseLockDoesNotCoverDecode(err)) {
        return 1;
    }

    out << "preview_bass_device_lease_spec ok" << Qt::endl;
    return 0;
}
