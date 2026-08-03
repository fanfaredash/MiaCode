#include "tools/video_export/VideoExportRuntimePolicy.h"

namespace miacode::video_export {

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
