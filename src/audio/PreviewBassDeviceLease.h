#pragma once

#include <cstdint>
#include <functional>
#include <limits>

namespace miacode::preview_audio {

enum class BassDeviceLeaseDomain : std::uint8_t {
    Output,
    NoSound,
};

struct BassDeviceLeaseApi {
    using DeviceId = std::uint32_t;

    static constexpr DeviceId kNoDevice = std::numeric_limits<DeviceId>::max();

    // Selects this domain's device for the calling thread and returns its ID.
    std::function<DeviceId()> selectDevice;
    std::function<bool()> initialize;
    std::function<void()> free;
    BassDeviceLeaseDomain domain = BassDeviceLeaseDomain::Output;
};

// Serializes BASS device lifetime within an output or no-sound domain. A held
// lease keeps its domain's process-owned device alive but never holds the
// lifecycle mutex during decode or stream/mixer work.
class PreviewBassDeviceLease final
{
public:
    PreviewBassDeviceLease() = default;
    ~PreviewBassDeviceLease();

    PreviewBassDeviceLease(const PreviewBassDeviceLease&) = delete;
    PreviewBassDeviceLease& operator=(const PreviewBassDeviceLease&) = delete;
    PreviewBassDeviceLease(PreviewBassDeviceLease&& other) noexcept;
    PreviewBassDeviceLease& operator=(PreviewBassDeviceLease&& other) noexcept;

    static PreviewBassDeviceLease acquire(BassDeviceLeaseApi api);

    bool acquired() const noexcept;
    bool borrowedExistingDevice() const noexcept;
    void release() noexcept;

private:
    enum class Kind {
        None,
        ProcessOwned,
        Borrowed,
    };

    explicit PreviewBassDeviceLease(Kind kind, BassDeviceLeaseDomain domain) noexcept;

    Kind kind_ = Kind::None;
    BassDeviceLeaseDomain domain_ = BassDeviceLeaseDomain::Output;
};

}  // namespace miacode::preview_audio
