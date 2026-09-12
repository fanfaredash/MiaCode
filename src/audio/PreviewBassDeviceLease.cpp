#include "PreviewBassDeviceLease.h"

#include <array>
#include <mutex>
#include <utility>

namespace miacode::preview_audio {

namespace {

struct ProcessBassDeviceState {
    std::mutex mutex;
    int ownedReferences = 0;
    std::function<void()> freeOwnedDevice;
};

ProcessBassDeviceState& processBassDeviceState(BassDeviceLeaseDomain domain)
{
    static std::array<ProcessBassDeviceState, 2> states;
    return states.at(static_cast<std::size_t>(domain));
}

}  // namespace

PreviewBassDeviceLease::PreviewBassDeviceLease(
    Kind kind,
    BassDeviceLeaseDomain domain) noexcept
    : kind_(kind)
    , domain_(domain)
{
}

PreviewBassDeviceLease::~PreviewBassDeviceLease()
{
    release();
}

PreviewBassDeviceLease::PreviewBassDeviceLease(PreviewBassDeviceLease&& other) noexcept
    : kind_(std::exchange(other.kind_, Kind::None))
    , domain_(other.domain_)
{
}

PreviewBassDeviceLease& PreviewBassDeviceLease::operator=(PreviewBassDeviceLease&& other) noexcept
{
    if (this != &other) {
        release();
        kind_ = std::exchange(other.kind_, Kind::None);
        domain_ = other.domain_;
    }
    return *this;
}

PreviewBassDeviceLease PreviewBassDeviceLease::acquire(BassDeviceLeaseApi api)
{
    if (!api.selectDevice || !api.initialize || !api.free) {
        return {};
    }

    auto& state = processBassDeviceState(api.domain);
    std::lock_guard lock(state.mutex);
    if (state.ownedReferences > 0) {
        if (api.selectDevice() == BassDeviceLeaseApi::kNoDevice) {
            return {};
        }
        ++state.ownedReferences;
        return PreviewBassDeviceLease(Kind::ProcessOwned, api.domain);
    }

    if (api.selectDevice() != BassDeviceLeaseApi::kNoDevice) {
        return PreviewBassDeviceLease(Kind::Borrowed, api.domain);
    }

    if (!api.initialize()) {
        return {};
    }

    state.ownedReferences = 1;
    state.freeOwnedDevice = std::move(api.free);
    return PreviewBassDeviceLease(Kind::ProcessOwned, api.domain);
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

    auto& state = processBassDeviceState(domain_);
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
