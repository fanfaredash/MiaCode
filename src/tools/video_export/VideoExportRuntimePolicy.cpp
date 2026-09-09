#include "tools/video_export/VideoExportRuntimePolicy.h"

namespace miacode::video_export {

VideoExportSizePolicy videoExportSizePolicy(VideoExportSizePreset preset)
{
    VideoExportSizePolicy policy;
    switch (preset) {
    case VideoExportSizePreset::Compact:
        policy.bitrateCoefficient = 0.070;
        policy.minBitrateKbps = 1800;
        policy.maxBitrateKbps = 8000;
        policy.maxRateMultiplier = 1.35;
        policy.bufferMultiplier = 2.0;
        policy.gopSeconds = 4;
        policy.maxAudioBitrateKbps = 160;
        policy.x264CrfAdjustment = 1;
        policy.usePeakConstrainedVbr = true;
        return policy;
    case VideoExportSizePreset::UltraCompactWithPv:
    case VideoExportSizePreset::UltraCompact:
        policy.bitrateCoefficient = 0.035;
        policy.minBitrateKbps = 4000;
        policy.maxBitrateKbps = 4000;
        policy.maxRateMultiplier = 1.0;
        policy.bufferMultiplier = 2.0;
        policy.gopSeconds = 6;
        policy.maxAudioBitrateKbps = 128;
        policy.x264CrfAdjustment = 3;
        policy.disableVideoBackground = preset == VideoExportSizePreset::UltraCompact;
        policy.usePeakConstrainedVbr = true;
        return policy;
    case VideoExportSizePreset::Standard:
    default:
        return policy;
    }
}

QString videoExportSizePresetToken(VideoExportSizePreset preset)
{
    switch (preset) {
    case VideoExportSizePreset::Compact:
        return QStringLiteral("compact");
    case VideoExportSizePreset::UltraCompact:
        return QStringLiteral("ultra_compact");
    case VideoExportSizePreset::UltraCompactWithPv:
        return QStringLiteral("ultra_compact_with_pv");
    case VideoExportSizePreset::Standard:
    default:
        return QStringLiteral("standard");
    }
}

int effectiveVideoExportAudioBitrateKbps(VideoExportSizePreset preset, int requestedKbps)
{
    const VideoExportSizePolicy policy = videoExportSizePolicy(preset);
    return qBound(96, requestedKbps, policy.maxAudioBitrateKbps);
}

int effectiveVideoExportX264Crf(VideoExportSizePreset preset, int baseCrf)
{
    const VideoExportSizePolicy policy = videoExportSizePolicy(preset);
    return qBound(16, baseCrf + policy.x264CrfAdjustment, 28);
}

bool shouldRequestOffscreenPboReadback(
    const std::optional<bool>& enableOverride,
    const std::optional<bool>& disableOverride
)
{
    if (disableOverride.value_or(false)) {
        return false;
    }
    if (enableOverride.has_value()) {
        return enableOverride.value();
    }
    return true;
}

bool shouldUsePremultipliedExportPipe(
    bool d3d11BackendActive,
    bool fastPreset,
    const std::optional<bool>& overrideValue
)
{
    return d3d11BackendActive && fastPreset && overrideValue.value_or(true);
}

bool shouldRetryVideoExportWorkerAfterCrash(
    bool crashExit,
    bool pboRequestedForAttempt,
    bool cancelRequested,
    int completedAttemptCount
)
{
    return crashExit
        && pboRequestedForAttempt
        && !cancelRequested
        && completedAttemptCount <= kVideoExportWorkerMaxCrashRetries;
}

}  // namespace miacode::video_export
