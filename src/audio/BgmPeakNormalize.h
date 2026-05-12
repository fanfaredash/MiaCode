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

// First draft, unverified. Used by the video-export plan builder so
// exports match the loudness of live preview, which runs an equivalent
// scan via BASS_ChannelGetLevel inside BassPreviewAudioBackend.
//
// Caveats:
//   - Not cross-checked against the BASS scan on real-world tracks; the
//     two decoders (miniaudio's dr_mp3 vs BASS's MP3 decoder) may differ
//     by a fraction of a dB at peaks. Preview and export gains may drift
//     by that margin until this is validated.
//   - No caching: every plan build re-decodes the full track.
BgmPeakNormalizationResult computeBgmPeakNormalization(const QString& filePath);

}  // namespace miacode::audio
