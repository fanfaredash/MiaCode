#include "PreviewBassDeviceLease.h"

#include <mutex>
#include <utility>

namespace miacode::preview_audio {

namespace {

struct ProcessBassDeviceState {
    std::mutex mutex;
    int ownedReferences = 0;
    std::function<void()> freeOwnedDevice;
};

ProcessBassDeviceState& processBassDeviceState()
{
    static ProcessBassDeviceState state;
    return state;
}

}  // namespace

PreviewBassDeviceLease::PreviewBassDeviceLease(Kind kind) noexcept
    : kind_(kind)
{
}

PreviewBassDeviceLease::~PreviewBassDeviceLease()
{
    release();
}

PreviewBassDeviceLease::PreviewBassDeviceLease(PreviewBassDeviceLease&& other) noexcept
    : kind_(std::exchange(other.kind_, Kind::None))
{
}

PreviewBassDeviceLease& PreviewBassDeviceLease::operator=(PreviewBassDeviceLease&& other) noexcept
{
    if (this != &other) {
        release();
        kind_ = std::exchange(other.kind_, Kind::None);
    }
    return *this;
}

PreviewBassDeviceLease PreviewBassDeviceLease::acquire(BassDeviceLeaseApi api)
{
    if (!api.getDevice || !api.initialize || !api.free) {
        return {};
    }

    auto& state = processBassDeviceState();
    std::lock_guard lock(state.mutex);
    if (state.ownedReferences > 0) {
        ++state.ownedReferences;
        return PreviewBassDeviceLease(Kind::ProcessOwned);
    }

    if (api.getDevice() != BassDeviceLeaseApi::kNoDevice) {
        return PreviewBassDeviceLease(Kind::Borrowed);
    }

    if (!api.initialize()) {
        return {};
    }

    state.ownedReferences = 1;
    state.freeOwnedDevice = std::move(api.free);
    return PreviewBassDeviceLease(Kind::ProcessOwned);
}

bool PreviewBassDeviceLease::acquired() const noexcept
{
    return kind_ != Kind::None;
}

bool PreviewBassDeviceLease::borrowedExistingDevice() const noexcept
{
    return kind_ == Kind::Borrowed;
}

void PreviewBassDeviceLease::release() noexcept
{
    if (kind_ != Kind::ProcessOwned) {
        kind_ = Kind::None;
        return;
    }

    auto& state = processBassDeviceState();
    std::lock_guard lock(state.mutex);
    kind_ = Kind::None;
    if (state.ownedReferences <= 0 || --state.ownedReferences != 0) {
        return;
    }

    auto freeOwnedDevice = std::move(state.freeOwnedDevice);
    state.freeOwnedDevice = {};
    try {
        freeOwnedDevice();
    } catch (...) {
        // The production BASS_Free callback cannot throw. Keep destructors safe
        // for injected test callbacks while allowing a later acquire to recover.
    }
}

}  // namespace miacode::preview_audio
