#pragma once

#include <optional>

namespace miacode::video_export {

constexpr int kVideoExportWorkerMaxCrashRetries = 1;

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
