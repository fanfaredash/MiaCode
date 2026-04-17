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
