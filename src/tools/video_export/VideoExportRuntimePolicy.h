#pragma once

#include <optional>

#include <QString>
#include <QtGlobal>

namespace miacode::video_export {

enum class VideoExportSizePreset {
    Standard,
    Compact,
    UltraCompactWithPv,
    UltraCompact,
};

constexpr int kVideoExportWorkerMaxCrashRetries = 1;

struct VideoExportSizePolicy {
    double bitrateCoefficient = 0.0;
    qint64 minBitrateKbps = 0;
    qint64 maxBitrateKbps = 0;
    double maxRateMultiplier = 1.0;
    double bufferMultiplier = 1.0;
    int gopSeconds = 2;
    int maxAudioBitrateKbps = 320;
    int x264Crf = -1;
    bool disableVideoBackground = false;
    bool usePeakConstrainedVbr = false;
};

VideoExportSizePolicy videoExportSizePolicy(VideoExportSizePreset preset);
QString videoExportSizePresetToken(VideoExportSizePreset preset);
int effectiveVideoExportAudioBitrateKbps(VideoExportSizePreset preset, int requestedKbps);

bool shouldRequestOffscreenPboReadback(
    const std::optional<bool>& enableOverride,
    const std::optional<bool>& disableOverride
);

bool shouldUsePremultipliedExportPipe(
    bool d3d11BackendActive,
    bool fastPreset,
    const std::optional<bool>& overrideValue
);

bool shouldRetryVideoExportWorkerAfterCrash(
    bool crashExit,
    bool pboRequestedForAttempt,
    bool cancelRequested,
    int completedAttemptCount
);

}  // namespace miacode::video_export
