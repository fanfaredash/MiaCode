#pragma once

#include <QString>

namespace miacode::preview_audio::bass {

enum class BassDebugRoute {
    Init,
    Transport,
};

enum class BassDebugOperation {
    InitializeAudioEngine,
    ResetAssets,
    InitializeAssets,
    InvalidateRetainedState,
    RebuildTimeline,
    AnchorTransport,
    PreparePreviewPlayback,
    ConfigureBackgroundTrack,
    PauseExact,
    ResumeTransport,
    RetainedSeek,
    RetainedReset,
    StartBackgroundTrack,
    SeekBackgroundTrack,
    TransportReady,
};

inline QString bassDebugRouteName(BassDebugRoute route)
{
    switch (route) {
    case BassDebugRoute::Init:
        return QStringLiteral("bass_init");
    case BassDebugRoute::Transport:
    default:
        return QStringLiteral("bass_transport");
    }
}

inline BassDebugRoute bassDebugRouteForOperation(BassDebugOperation operation, bool initWindowActive)
{
    switch (operation) {
    case BassDebugOperation::ConfigureBackgroundTrack:
        return initWindowActive ? BassDebugRoute::Init : BassDebugRoute::Transport;
    case BassDebugOperation::PauseExact:
    case BassDebugOperation::ResumeTransport:
    case BassDebugOperation::RetainedSeek:
    case BassDebugOperation::RetainedReset:
    case BassDebugOperation::StartBackgroundTrack:
    case BassDebugOperation::SeekBackgroundTrack:
    case BassDebugOperation::TransportReady:
        return BassDebugRoute::Transport;
    case BassDebugOperation::InitializeAudioEngine:
    case BassDebugOperation::ResetAssets:
    case BassDebugOperation::InitializeAssets:
    case BassDebugOperation::InvalidateRetainedState:
    case BassDebugOperation::RebuildTimeline:
    case BassDebugOperation::AnchorTransport:
    case BassDebugOperation::PreparePreviewPlayback:
    default:
        return BassDebugRoute::Init;
    }
}

}  // namespace miacode::preview_audio::bass
