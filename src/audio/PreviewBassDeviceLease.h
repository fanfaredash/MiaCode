#pragma once

#include <cstdint>
#include <functional>
#include <limits>

namespace miacode::preview_audio {

struct BassDeviceLeaseApi {
    using DeviceId = std::uint32_t;

    static constexpr DeviceId kNoDevice = std::numeric_limits<DeviceId>::max();

    std::function<DeviceId()> getDevice;
    std::function<bool()> initialize;
    std::function<void()> free;
};

// Serializes only BASS process-wide device lifetime. A held lease keeps a
// process-owned device alive but never holds the lifecycle mutex during decode
// or stream/mixer work.
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

    explicit PreviewBassDeviceLease(Kind kind) noexcept;

    Kind kind_ = Kind::None;
};

}  // namespace miacode::preview_audio
