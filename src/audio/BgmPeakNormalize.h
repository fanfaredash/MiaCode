#pragma once

#include <QString>

namespace miacode::audio {

struct BgmPeakNormalizationResult {
    // Multiplier to apply to the playback gain so the loudest sample
    // reaches full scale. Capped at 4.0 (~+12 dB) to avoid amplifying
    // noise floor on near-silent files; defaults to 1.0 on any error.
    double gain = 1.0;

    // Maximum absolute sample value observed across both channels in [0, 1].
    // Zero when no audio could be decoded.
    double peak = 0.0;

    // False when the file is missing, unreadable, or fails to decode —
    // in which case gain is forced to 1.0 (no normalization).
    bool decoded = false;
};

// Decodes the file with miniaudio, scans for the peak sample value across
// channels, and computes a peak-normalization gain factor. Used by both
// the live-preview Bass backend and the video-export plan builder so the
// two paths apply identical gain to the same source file.
BgmPeakNormalizationResult computeBgmPeakNormalization(const QString& filePath);

}  // namespace miacode::audio
