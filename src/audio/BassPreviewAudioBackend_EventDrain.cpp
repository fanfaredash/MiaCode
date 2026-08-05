#include "BassPreviewAudioBackend.h"

#include "BassPreviewDebugLogRouting.h"
#include "BassPreviewRetainedState.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/FileContentStamp.h"
#include "common/OperationLog.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewSfxTimeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QtMath>

#include <limits>
#include <cstdio>   // G1 Commit 8 followup: std::snprintf for startup-beacon lines

#ifdef MIACODE_HAS_BASS_AUDIO
#include "bass.h"
#include "bassmix.h"
#endif

#include "BassPreviewAudioBackendImpl.h"
#include "BassPreviewAudioBackendSample.h"

using namespace miacode::audio::bass_detail;

void BassPreviewAudioBackend::resetCursor(double second, bool includeCurrentSecond)
{
    MC_OP("BassPreviewAudioBackend::resetCursor");
    bool rearmScheduler = false;
#ifdef MIACODE_HAS_BASS_AUDIO
    {
        QMutexLocker locker(&schedulerMutex_);
        rearmScheduler = sfxSchedulerActive_;
    }
    if (rearmScheduler) {
        disarmSfxScheduler();
    }
#endif
    playbackSession_.eventGroupIndex = 0;
    while (playbackSession_.eventGroupIndex < preparedGroups_.size()) {
        const double groupSecond = preparedGroups_[playbackSession_.eventGroupIndex].second;
        const bool beforeStart = includeCurrentSecond
            ? (groupSecond + kBassPreviewEpsilonSeconds < second)
            : (groupSecond <= second + kBassPreviewEpsilonSeconds);
        if (!beforeStart) {
            break;
        }
        ++playbackSession_.eventGroupIndex;
    }
#ifdef MIACODE_HAS_BASS_AUDIO
    if (rearmScheduler && playbackSession_.masterRunning) {
        anchorSfxScheduler(second);
    }
#endif
}

void BassPreviewAudioBackend::triggerGroup(const CollapsedEventGroup& group)
{
    for (const Event& event : group.orderedEvents) {
        if (event.kind == QLatin1String("touchhold_start")
            || event.kind == QLatin1String("touchhold_stop")) {
            // Latest-wins ownership: rather than naively start/stop the single
            // shared touch-hold sample per event (which let a prior span's stop
            // clobber the next span's start at a seamless join, and let an older
            // span's stop kill a newer overlapping one), re-derive who should own
            // the voice at this instant and reconcile. Order-independent.
            reconcileTouchholdVoice(event.second);
            continue;
        }
        playKindInternal(event.kind, event.gain);
    }

    for (const miacode::preview_sfx_timeline::AggregatedPlayback& playback : group.aggregatedPlaybacks) {
        playKindInternal(playback.kind, miacode::preview_sfx_timeline::aggregatedPlaybackGain(playback));
    }
}

void BassPreviewAudioBackend::drainEvents(double second)
{
    // A live session is scheduled by the master mixer's decode cursor.  Keeping
    // this fallback only for the pre-commit edge avoids a GUI wake-up replaying
    // the groups that BASS already emitted while the GUI thread was stalled.
#ifdef MIACODE_HAS_BASS_AUDIO
    {
        QMutexLocker locker(&schedulerMutex_);
        if (sfxSchedulerActive_) {
            return;
        }
    }
#endif
    // G1 Commit 8: bass_sfx_drain per §7.2. Emit one line per tick that actually
    // triggered something, with the chart-second the tick was draining toward,
    // the count, and the first/last group indices. Quiet ticks (drained=0) stay
    // out of the log so the channel isn't dominated by no-ops. Track the range
    // around the loop so we can read it in the log line afterward.
    const int firstIdxBeforeDrain = playbackSession_.eventGroupIndex;
    int drainedCount = 0;
    int lastTriggeredIdx = -1;
    while (playbackSession_.eventGroupIndex < preparedGroups_.size()) {
        const CollapsedEventGroup& group = preparedGroups_[playbackSession_.eventGroupIndex];
        if (group.second > second + kBassPreviewEpsilonSeconds) {
            break;
        }
        // This is the compatibility backend path. A live BASS session returns
        // above before reaching it, so it cannot duplicate a mixer sync.
        triggerGroup(group);
        playbackSession_.lastTriggeredGroupIndex = playbackSession_.eventGroupIndex;
        playbackSession_.lastTriggeredGroupSecond = group.second;
        playbackSession_.triggeredGroupCount += 1;
        lastTriggeredIdx = playbackSession_.eventGroupIndex;
        ++drainedCount;
        ++playbackSession_.eventGroupIndex;
    }
    if (drainedCount > 0) {
        appendAudioDebugLog(
            QString("bass_sfx_drain at_chart=%1 drained=%2 first_idx=%3 last_idx=%4")
                .arg(second, 0, 'f', 6)
                .arg(drainedCount)
                .arg(firstIdxBeforeDrain)
                .arg(lastTriggeredIdx));
    }
}

void BassPreviewAudioBackend::disarmSfxScheduler()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    QMutexLocker locker(&schedulerMutex_);
    if (scheduledGroupSync_ != 0 && masterMixer_ != 0) {
        BASS_ChannelRemoveSync(masterMixer_, scheduledGroupSync_);
        noteBassErr("sfx_scheduler/remove_sync");
    }
    scheduledGroupSync_ = 0;
    scheduledGroupIndex_ = -1;
    scheduledMixerAction_ = ScheduledMixerAction::None;
    sfxSchedulerActive_ = false;
    sfxSchedulerAnchorDecodePosition_ = 0;
#endif
}

void BassPreviewAudioBackend::anchorSfxScheduler(double chartSecond)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (masterMixer_ == 0 || !playbackSession_.masterRunning
        || shuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    disarmSfxScheduler();
    const QWORD position = BASS_ChannelGetPosition(
        masterMixer_, BASS_POS_BYTE | BASS_POS_DECODE);
    if (position == static_cast<QWORD>(-1)) {
        noteBassErr("sfx_scheduler/get_decode_position");
        return;
    }

    int nextGroupIndex = 0;
    bool backgroundPendingStart = false;
    double playbackRate = 1.0;
    {
        QMutexLocker locker(&schedulerMutex_);
        if (shuttingDown_.load(std::memory_order_acquire)
            || !playbackSession_.masterRunning) {
            return;
        }
        sfxSchedulerAnchor_.chartSecond = clampTimelineSecond(chartSecond);
        sfxSchedulerAnchor_.mixerSecond = BASS_ChannelBytes2Seconds(masterMixer_, position);
        sfxSchedulerAnchor_.playbackRate = playbackSession_.backgroundTrackPlaybackRate;
        sfxSchedulerAnchorDecodePosition_ = position;
        sfxSchedulerActive_ = true;
        armNextGroupSyncLocked();
        nextGroupIndex = playbackSession_.eventGroupIndex;
        backgroundPendingStart = playbackSession_.backgroundTrackPendingStart;
        playbackRate = playbackSession_.backgroundTrackPlaybackRate;
    }
    appendAudioDebugLog(
        QString("bass_sfx_scheduler action=anchor chart_second=%1 rate=%2 next_group_idx=%3 bg_pending=%4")
            .arg(chartSecond, 0, 'f', 6)
            .arg(playbackRate, 0, 'f', 3)
            .arg(nextGroupIndex)
            .arg(backgroundPendingStart ? 1 : 0));
#else
    Q_UNUSED(chartSecond);
#endif
}

double BassPreviewAudioBackend::currentSfxSchedulerChartSecond(double fallbackSecond) const
{
#ifdef MIACODE_HAS_BASS_AUDIO
    miacode::preview_audio::bass::SfxSchedulerAnchor anchor;
    quint64 anchorDecodePosition = 0;
    {
        QMutexLocker locker(&schedulerMutex_);
        if (!sfxSchedulerActive_ || masterMixer_ == 0) {
            return fallbackSecond;
        }
        anchor = sfxSchedulerAnchor_;
        anchorDecodePosition = sfxSchedulerAnchorDecodePosition_;
    }
    const QWORD currentDecodePosition = BASS_ChannelGetPosition(
        masterMixer_, BASS_POS_BYTE | BASS_POS_DECODE);
    if (currentDecodePosition == static_cast<QWORD>(-1)
        || currentDecodePosition < anchorDecodePosition) {
        return fallbackSecond;
    }
    const double mixerElapsedSeconds = BASS_ChannelBytes2Seconds(
        masterMixer_, currentDecodePosition - anchorDecodePosition);
    if (!qIsFinite(mixerElapsedSeconds)) {
        return fallbackSecond;
    }
    return clampTimelineSecond(
        miacode::preview_audio::bass::chartSecondForMixerSecond(
            anchor, anchor.mixerSecond + mixerElapsedSeconds));
#else
    return fallbackSecond;
#endif
}

void BassPreviewAudioBackend::armNextGroupSyncLocked()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!sfxSchedulerActive_ || scheduledGroupSync_ != 0 || masterMixer_ == 0
        || !playbackSession_.masterRunning || shuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    const int groupIndex = playbackSession_.eventGroupIndex;
    const bool hasGroup = groupIndex >= 0 && groupIndex < preparedGroups_.size();
    const bool hasPendingBackground = backgroundTrackSample_ != nullptr
        && playbackSession_.backgroundTrackPendingStart;
    if (!hasGroup && !hasPendingBackground) {
        return;
    }

    const double groupSecond = hasGroup ? preparedGroups_[groupIndex].second : std::numeric_limits<double>::infinity();
    const double pendingSecond = hasPendingBackground
        ? playbackSession_.backgroundTrackPendingStartSecond
        : std::numeric_limits<double>::infinity();
    const bool startBackgroundFirst = pendingSecond <= groupSecond + kBassPreviewEpsilonSeconds;
    const double targetChartSecond = startBackgroundFirst ? pendingSecond : groupSecond;
    const bool sameInstant = hasGroup && hasPendingBackground
        && qAbs(groupSecond - pendingSecond) <= kBassPreviewEpsilonSeconds;

    const double targetMixerSecond =
        miacode::preview_audio::bass::mixerSecondForChartSecond(sfxSchedulerAnchor_, targetChartSecond);
    const double relativeSecond = qMax(0.0, targetMixerSecond - sfxSchedulerAnchor_.mixerSecond);
    const QWORD targetPosition = sfxSchedulerAnchorDecodePosition_
        + BASS_ChannelSeconds2Bytes(masterMixer_, relativeSecond);
    const quint32 syncHandle = BASS_ChannelSetSync(
        masterMixer_,
        BASS_SYNC_POS | BASS_SYNC_MIXTIME | BASS_SYNC_ONETIME,
        targetPosition,
        reinterpret_cast<SYNCPROC*>(BassPreviewAudioBackend::onMixerGroupSync),
        this);
    if (syncHandle == 0) {
        noteBassErr("sfx_scheduler/set_sync");
        sfxSchedulerActive_ = false;
        return;
    }
    scheduledGroupSync_ = syncHandle;
    scheduledGroupIndex_ = startBackgroundFirst && !sameInstant ? -1 : groupIndex;
    scheduledMixerAction_ = sameInstant
        ? ScheduledMixerAction::SfxGroupAndStartPendingBackgroundTrack
        : (startBackgroundFirst
            ? ScheduledMixerAction::StartPendingBackgroundTrack
            : ScheduledMixerAction::SfxGroup);
#endif
}

void BassPreviewAudioBackend::onMixerGroupSync(quint32 handle, quint32 channel, quint32 data, void* user)
{
    Q_UNUSED(channel);
    Q_UNUSED(data);
    auto* backend = static_cast<BassPreviewAudioBackend*>(user);
    if (backend != nullptr) {
        backend->handleMixerGroupSync(handle);
    }
}

void BassPreviewAudioBackend::handleMixerGroupSync(quint32 handle)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (shuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    QMutexLocker locker(&schedulerMutex_);
    if (shuttingDown_.load(std::memory_order_acquire)
        || !sfxSchedulerActive_ || handle == 0 || handle != scheduledGroupSync_) {
        return;
    }

    const int groupIndex = scheduledGroupIndex_;
    const ScheduledMixerAction action = scheduledMixerAction_;
    scheduledGroupSync_ = 0;
    scheduledGroupIndex_ = -1;
    scheduledMixerAction_ = ScheduledMixerAction::None;

    const bool startBackground = action == ScheduledMixerAction::StartPendingBackgroundTrack
        || action == ScheduledMixerAction::SfxGroupAndStartPendingBackgroundTrack;
    if (startBackground && backgroundTrackSample_ != nullptr
        && playbackSession_.backgroundTrackPendingStart) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = true;
    }

    const bool shouldTriggerGroup = action == ScheduledMixerAction::SfxGroup
        || action == ScheduledMixerAction::SfxGroupAndStartPendingBackgroundTrack;
    if (shouldTriggerGroup && groupIndex >= 0 && groupIndex < preparedGroups_.size()) {
        const CollapsedEventGroup group = preparedGroups_[groupIndex];
        if (playbackSession_.eventGroupIndex <= groupIndex) {
            playbackSession_.eventGroupIndex = groupIndex + 1;
        }
        playbackSession_.lastTriggeredGroupIndex = groupIndex;
        playbackSession_.lastTriggeredGroupSecond = group.second;
        ++playbackSession_.triggeredGroupCount;
        triggerGroup(group);
    }
    armNextGroupSyncLocked();
#else
    Q_UNUSED(handle);
#endif
}

void BassPreviewAudioBackend::reconcileTouchholdVoice(double second)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (touchholdSample_ == nullptr) {
        return;
    }
    const int owner = miacode::preview_sfx_timeline::touchholdOwnerSpanIndexAt(
        preparedTimeline_.touchholdSpans, second);
    if (owner == touchholdOwnerSpanIndex_) {
        return;  // voice already belongs to the right span — leave it playing
    }
    touchholdOwnerSpanIndex_ = owner;
    if (owner < 0) {
        touchholdSample_->stop();
        return;
    }
    const TouchholdSpan& span = preparedTimeline_.touchholdSpans[owner];
    touchholdSample_->setCurrentSec(qMax(0.0, second - span.startSecond));
    touchholdSample_->play();
#else
    Q_UNUSED(second);
#endif
}

void BassPreviewAudioBackend::pauseTouchholdVoices()
{
    MC_OP("BassPreviewAudioBackend::pauseTouchholdVoices");
#ifdef MIACODE_HAS_BASS_AUDIO
    if (touchholdSample_ != nullptr) {
        touchholdSample_->stop();
    }
#endif
    touchholdOwnerSpanIndex_ = -1;
}

void BassPreviewAudioBackend::restoreTouchholdVoices(double second)
{
    MC_OP("BassPreviewAudioBackend::restoreTouchholdVoices");
#ifdef MIACODE_HAS_BASS_AUDIO
    pauseTouchholdVoices();
    reconcileTouchholdVoice(second);
#else
    Q_UNUSED(second);
#endif
}


bool BassPreviewAudioBackend::playKindInternal(const QString& kind, double gain)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    Sample* sample = sampleForKind(kind);
    if (sample == nullptr) {
        return false;
    }
    sample->playOneShot(gain);
    return true;
#else
    Q_UNUSED(kind);
    Q_UNUSED(gain);
    return false;
#endif
}

bool BassPreviewAudioBackend::audition(const QString& kind, double gain)
{
    MC_OP("BassPreviewAudioBackend::audition");
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!initializeAudioEngine() || masterMixer_ == 0) {
        return false;
    }
    if (!playbackSession_.masterRunning) {
        resetMasterMixerClock(0.0);
        // G1 Commit 6: master mixer was started at engine init and never stops.
        playbackSession_.masterRunning = true;
    }
    return playKindInternal(kind, gain);
#else
    Q_UNUSED(kind);
    Q_UNUSED(gain);
    return false;
#endif
}

void BassPreviewAudioBackend::stopAll()
{
    MC_OP("BassPreviewAudioBackend::stopAll");
    stopPlaybackSession();
    preparedPlayback_ = PreparedPlaybackState();
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
}

void BassPreviewAudioBackend::prepareForShutdown()
{
    MC_OP("BassPreviewAudioBackend::prepareForShutdown");
    shuttingDown_.store(true, std::memory_order_release);
    stopAll();
}
