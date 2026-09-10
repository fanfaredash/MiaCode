#pragma once

namespace miacode {

// The preview-facing read port for the one canonical chart-time clock. A
// Preview projection may observe it, but must not integrate its own time.
class AudioClockSource
{
public:
    virtual ~AudioClockSource() = default;
    virtual double currentAudioClockSecond() const = 0;
};

}  // namespace miacode
