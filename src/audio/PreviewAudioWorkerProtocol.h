#pragma once

#include "PreviewAudioSettings.h"
#include "common/PreviewTimingSettings.h"
#include "timeline/TimelineData.h"

#include <QString>
#include <QVector>
#include <QtGlobal>

#include <limits>

namespace miacode::preview_audio {

enum class CommandKind {
    Shutdown,
    DeviceChangePause,
    ManualPause,
    StopAll,
    SetWarmupResolvedPaths,
    ReloadAssets,
    SetChartPath,
    SetBackgroundOffset,
    SetBackgroundRate,
    ApplyRateAtSecond,
    ApplyLevels,
    ConfigureTimeline,
    ClearTimeline,
    ApplyPausedState,
    Prepare,
    Commit,
    Cancel,
    Start,
    ResumeRetained,
    SeekRetained,
    ResetRetained,
    ClearRetained,
    ResetCursor,
    PauseTouchhold,
    RestoreTouchhold,
    StartBackground,
    SeekBackground,
    PauseBackground,
    StopSfxVoices,
    SyncBackgroundTrack,
    DrainEvents,
    Audition,
};

enum class CommandError {
    None,
    QueueFull,
    ShuttingDown,
    BackendUnavailable,
    BackendFailure,
    Stale,
};

enum class WorkerLifecycle {
    Constructing,
    Ready,
    Loading,
    Degraded,
    ShuttingDown,
    Stopped,
};

enum class CommandClass {
    High,
    Ordered,
    Latest,
    Audition,
};

struct CommandIdentity {
    quint64 sequence = 0;
    quint64 generation = 0;
    quint64 assetGeneration = 0;
    quint64 transactionId = 0;
    quint64 deviceSequence = 0;
    quint64 pauseToken = 0;
};

struct PreviewAudioCommand {
    CommandKind kind = CommandKind::StopAll;
    CommandIdentity identity;

    QString chartPath;
    QString trackPath;
    QString sfxDirectory;
    QString auditionKind;
    QVector<TimelineNoteMarker> noteMarkers;
    PreviewAudioSettings settings;
    PreviewTimingSettings timingSettings;

    double value = 0.0;
    double second = 0.0;
    double rate = 1.0;
    double gain = 1.0;
    bool option = false;
};

struct PreviewAudioCompletion {
    CommandKind kind = CommandKind::StopAll;
    CommandIdentity identity;
    CommandError error = CommandError::None;
    bool success = true;
    double value = 0.0;
    QString detail;
};

struct PreviewAudioSnapshot {
    quint64 sequence = 0;
    CommandIdentity identity;
    WorkerLifecycle lifecycle = WorkerLifecycle::Constructing;
    CommandError lastError = CommandError::None;
    bool backendReady = false;
    bool backgroundTrackAvailable = false;
    bool backgroundTrackRunning = false;
    double preparedSecond = 0.0;
    double authoritativeSecond = 0.0;
};

constexpr CommandClass commandClass(CommandKind kind) noexcept
{
    switch (kind) {
    case CommandKind::Shutdown:
    case CommandKind::DeviceChangePause:
    case CommandKind::ManualPause:
    case CommandKind::StopAll:
        return CommandClass::High;
    case CommandKind::SyncBackgroundTrack:
    case CommandKind::DrainEvents:
        return CommandClass::Latest;
    case CommandKind::Audition:
        return CommandClass::Audition;
    default:
        return CommandClass::Ordered;
    }
}

constexpr bool isPlaybackCommand(CommandKind kind) noexcept
{
    switch (kind) {
    case CommandKind::DeviceChangePause:
    case CommandKind::ManualPause:
    case CommandKind::StopAll:
    case CommandKind::ApplyRateAtSecond:
    case CommandKind::ApplyPausedState:
    case CommandKind::Prepare:
    case CommandKind::Commit:
    case CommandKind::Cancel:
    case CommandKind::Start:
    case CommandKind::ResumeRetained:
    case CommandKind::SeekRetained:
    case CommandKind::ResetRetained:
    case CommandKind::ClearRetained:
    case CommandKind::ResetCursor:
    case CommandKind::PauseTouchhold:
    case CommandKind::RestoreTouchhold:
    case CommandKind::StartBackground:
    case CommandKind::SeekBackground:
    case CommandKind::PauseBackground:
    case CommandKind::StopSfxVoices:
    case CommandKind::SyncBackgroundTrack:
    case CommandKind::DrainEvents:
        return true;
    default:
        return false;
    }
}

constexpr bool isTransactionalCommand(CommandKind kind) noexcept
{
    switch (kind) {
    case CommandKind::DeviceChangePause:
    case CommandKind::ManualPause:
    case CommandKind::ApplyPausedState:
    case CommandKind::Prepare:
    case CommandKind::Commit:
    case CommandKind::Cancel:
    case CommandKind::Start:
    case CommandKind::ResumeRetained:
    case CommandKind::SeekRetained:
    case CommandKind::ResetRetained:
    case CommandKind::ClearRetained:
        return true;
    default:
        return false;
    }
}

constexpr bool isAssetCommand(CommandKind kind) noexcept
{
    return kind == CommandKind::SetWarmupResolvedPaths
        || kind == CommandKind::ReloadAssets
        || kind == CommandKind::SetChartPath
        || kind == CommandKind::Audition;
}

inline PreviewAudioCommand makeHigh(
    CommandKind kind,
    quint64 generation = 0,
    quint64 transactionId = 0)
{
    PreviewAudioCommand command;
    command.kind = kind;
    command.identity.generation = generation;
    command.identity.transactionId = transactionId;
    return command;
}

inline PreviewAudioCommand makeOrdered(
    CommandKind kind,
    quint64 generation = 0,
    quint64 transactionId = 0)
{
    PreviewAudioCommand command;
    command.kind = kind;
    command.identity.generation = generation;
    command.identity.transactionId = transactionId;
    return command;
}

inline PreviewAudioCommand makeLatest(CommandKind kind, quint64 generation, double value)
{
    PreviewAudioCommand command;
    command.kind = kind;
    command.identity.generation = generation;
    command.value = value;
    command.second = value;
    return command;
}

inline PreviewAudioCommand makeAudition(quint64 assetGeneration, const QString& kind, double gain = 1.0)
{
    PreviewAudioCommand command;
    command.kind = CommandKind::Audition;
    command.identity.assetGeneration = assetGeneration;
    command.auditionKind = kind;
    command.gain = gain;
    return command;
}

inline bool canCoalesce(
    const PreviewAudioCommand& existing,
    const PreviewAudioCommand& incoming) noexcept
{
    if (existing.kind != incoming.kind) {
        return false;
    }
    if (existing.kind == CommandKind::DeviceChangePause) {
        return existing.identity.generation == incoming.identity.generation
            && existing.identity.transactionId == incoming.identity.transactionId;
    }
    return commandClass(existing.kind) == CommandClass::Latest
        && existing.identity.generation == incoming.identity.generation;
}

constexpr quint64 nextSnapshotSequence(quint64 current) noexcept
{
    return current == std::numeric_limits<quint64>::max() ? current : current + 1;
}

constexpr bool isNewerSnapshotSequence(quint64 candidate, quint64 current) noexcept
{
    return candidate > current;
}

inline bool acceptsPlaybackCompletion(
    quint64 currentGeneration,
    quint64 activeTransactionId,
    const PreviewAudioCompletion& completion) noexcept
{
    if (!isPlaybackCommand(completion.kind)
        || completion.identity.generation != currentGeneration) {
        return false;
    }
    if (isTransactionalCommand(completion.kind)) {
        return activeTransactionId != 0
            && completion.identity.transactionId == activeTransactionId;
    }
    return completion.identity.transactionId == 0;
}

inline bool acceptsDevicePauseCompletion(
    quint64 currentGeneration,
    quint64 pendingTransactionId,
    quint64 pendingPauseToken,
    const PreviewAudioCompletion& completion) noexcept
{
    return completion.kind == CommandKind::DeviceChangePause
        && completion.identity.generation == currentGeneration
        && pendingTransactionId != 0
        && completion.identity.transactionId == pendingTransactionId
        && pendingPauseToken != 0
        && completion.identity.pauseToken == pendingPauseToken;
}

inline bool acceptsAssetCompletion(
    quint64 latestAssetGeneration,
    const PreviewAudioCompletion& completion) noexcept
{
    return completion.identity.assetGeneration >= latestAssetGeneration;
}

}  // namespace miacode::preview_audio
