#pragma once

#include <optional>

namespace miacode::video_export {

constexpr int kVideoExportWorkerMaxCrashRetries = 1;

bool shouldRequestOffscreenPboReadback(
    const std::optional<bool>& enableOverride,
    const std::optional<bool>& disableOverride
);

bool shouldRetryVideoExportWorkerAfterCrash(
    bool crashExit,
    bool pboRequestedForAttempt,
    bool cancelRequested,
    int completedAttemptCount
);

}  // namespace miacode::video_export
