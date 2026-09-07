// Session::-owned frame-pacing accessors, split out of FramePacing.cpp.
//
// Stage 4.9d-6: PlaybackCoordinator's implementation TUs are being separated
// from the Session assembly so the coordinator can eventually link on its
// own (see the Result Packet for the link-probe evidence). FramePacing.cpp
// now holds only PlaybackCoordinator::-owned frame-pacing logic; this file
// holds the Session::-owned storage-value parsing and thin read accessors
// that used to share that TU.

#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"
#include "runtime/Shared.h"

void Session::setPreviewFixedTimerHighResolutionActive(bool active)
{
    // Stage 4.9d-4a: body (including its #ifdef Q_OS_WIN / Windows timeBeginPeriod
    // path) moved to runtime::shared — see runtime/Shared.cpp. Qualified because
    // this member shares the free function's name.
    miacode::runtime::shared::setPreviewFixedTimerHighResolutionActive(state_, active);
}

Session::PreviewCanvasFrameRateMode Session::previewFrameRateModeFromStorageValue(
    const QString& value,
    PreviewCanvasFrameRateMode fallback) const
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("30") || normalized == QLatin1String("30fps")) {
        return PreviewCanvasFrameRateMode::Fps30;
    }
    if (normalized == QLatin1String("60") || normalized == QLatin1String("60fps")) {
        return PreviewCanvasFrameRateMode::Fps60;
    }
    if (normalized == QLatin1String("120") || normalized == QLatin1String("120fps")) {
        return PreviewCanvasFrameRateMode::Fps120;
    }
    if (normalized == QLatin1String("display")
        || normalized == QLatin1String("display_max")
        || normalized == QLatin1String("screen")
        || normalized == QLatin1String("unlimited")) {
        return PreviewCanvasFrameRateMode::DisplayRefresh;
    }
    return fallback;
}

Session::PreviewCanvasFrameRateMode Session::previewCanvasFrameRateModeFromStorageValue(const QString& value) const
{
    return previewFrameRateModeFromStorageValue(value, PreviewCanvasFrameRateMode::Fps60);
}

Session::PreviewCanvasFrameRateMode Session::currentPreviewCanvasFrameRateMode() const
{
    return state_.previewCanvasFrameRateMode_;
}

Session::PreviewCanvasFrameRateMode Session::currentPreviewStageMediaFrameRateMode() const
{
    return playback_->currentPreviewStageMediaFrameRateMode();
}

bool Session::currentVideoDecodePrefersSoftware() const
{
    return playback_->currentVideoDecodePrefersSoftware();
}

Session::PreviewCanvasFrameRateMode Session::currentTimelineFrameRateMode() const
{
    return playback_->currentTimelineFrameRateMode();
}
