#include "BassPreviewAudioBackend.h"

#include "PreviewBassEmergencyPause.h"

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
#include <mutex>
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
        disarmSfxScheduler("reset_cursor");
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

void BassPreviewAudioBackend::triggerGroup(
    const CollapsedEventGroup& group,
    QString* playedKindsOut,
    TouchholdTransition* touchholdOut,
    miacode::preview_audio::bass::PlayedSfxSnapshot* playedSnapshotOut)
{
    const auto record = [playedKindsOut, playedSnapshotOut](
                            const QString& kind, double gain, bool started) {
        if (!started) {
            return;
        }
        if (playedSnapshotOut != nullptr) {
            playedSnapshotOut->record(kind, gain);
        }
        if (playedKindsOut != nullptr) {
            if (!playedKindsOut->isEmpty()) {
                playedKindsOut->append(QLatin1Char(','));
            }
            playedKindsOut->append(QStringLiteral("%1:%2").arg(kind).arg(gain, 0, 'f', 2));
        }
    };

    for (const Event& event : group.orderedEvents) {
        if (event.kind == QLatin1String("touchhold_start")
            || event.kind == QLatin1String("touchhold_stop")) {
            // Latest-wins ownership: rather than naively start/stop the single
            // shared touch-hold sample per event (which let a prior span's stop
            // clobber the next span's start at a seamless join, and let an older
            // span's stop kill a newer overlapping one), re-derive who should own
            // the voice at this instant and reconcile. Order-independent.
            // touchholdOut is non-null exactly when the caller holds schedulerMutex_, which
            // defers this transition's log line until after the unlock. If a group somehow
            // produces two ownership changes, the slot keeps the last one -- reconcile
            // leaves it untouched when nothing changed -- so the logged row always
            // describes the voice state this group actually ended on.
            reconcileTouchholdVoice(event.second, touchholdOut);
            continue;
        }
        record(event.kind, event.gain, playKindInternal(event.kind, event.gain));
    }

    for (const miacode::preview_sfx_timeline::AggregatedPlayback& playback : group.aggregatedPlaybacks) {
        const double gain = miacode::preview_sfx_timeline::aggregatedPlaybackGain(playback);
        record(playback.kind, gain, playKindInternal(playback.kind, gain));
    }
}

void BassPreviewAudioBackend::drainEvents(double second)
{
    serviceSfxScheduler();
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
    QString playedKinds;
    while (playbackSession_.eventGroupIndex < preparedGroups_.size()) {
        const CollapsedEventGroup& group = preparedGroups_[playbackSession_.eventGroupIndex];
        if (group.second > second + kBassPreviewEpsilonSeconds) {
            break;
        }
        // This is the compatibility backend path. A live BASS session returns
        // above before reaching it, so it cannot duplicate a mixer sync.
        triggerGroup(group, runtimeAudioDebugEnabled() ? &playedKinds : nullptr);
        playbackSession_.lastTriggeredGroupIndex = playbackSession_.eventGroupIndex;
        playbackSession_.lastTriggeredGroupSecond = group.second;
        playbackSession_.triggeredGroupCount += 1;
        lastTriggeredIdx = playbackSession_.eventGroupIndex;
        ++drainedCount;
        ++playbackSession_.eventGroupIndex;
    }
    if (drainedCount > 0) {
        appendAudioDebugLog(
            QString("bass_sfx_drain at_chart=%1 drained=%2 first_idx=%3 last_idx=%4 played=%5")
                .arg(second, 0, 'f', 6)
                .arg(drainedCount)
                .arg(firstIdxBeforeDrain)
                .arg(lastTriggeredIdx)
                .arg(playedKinds.isEmpty() ? QStringLiteral("(none)") : playedKinds));
    }
}

void BassPreviewAudioBackend::disarmSfxScheduler(const char* reason)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    bool wasActive = false;
    bool hadSync = false;
    int groupIndex = -1;
    int removeSyncError = 0;
    quint32 syncToRemove = 0;
    {
        QMutexLocker locker(&schedulerMutex_);
        wasActive = sfxSchedulerActive_;
        hadSync = scheduledGroupSync_ != 0;
        groupIndex = scheduledGroupIndex_;
        // Take ownership of the handle here, but do NOT call into BASS yet -- see below.
        syncToRemove = scheduledGroupSync_;
        scheduledGroupSync_ = 0;
        scheduledGroupIndex_ = -1;
        scheduledMixerAction_ = ScheduledMixerAction::None;
        scheduledGroupTargetPosition_ = 0;
        sfxSchedulerActive_ = false;
        sfxSchedulerAnchorDecodePosition_ = 0;
    }
    // BASS_ChannelRemoveSync remains OUTSIDE schedulerMutex_. Before A6, the two locks
    // were taken in opposite orders on the two threads that matter:
    //
    //   GUI thread    : schedulerMutex_ -> BASS internal sync lock (inside RemoveSync)
    //   BASS callback : BASS internal sync lock -> schedulerMutex_ (handleMixerGroupSync)
    //
    // RemoveSync waits for an in-flight sync callback to finish. The callback now uses
    // tryLock and defers on contention, which breaks that ABBA cycle; keeping the native
    // removal outside the mutex makes the invariant structural and prevents a future
    // callback change from silently restoring the deadlock.
    //
    // Clearing the scheduler state above is what makes the hoist safe rather than merely
    // narrower: sfxSchedulerActive_ is already false by the time the lock is dropped, so a
    // callback firing in the window between the unlock and the removal takes its own
    // early-out instead of acting on a scheduler that is being torn down.
    if (syncToRemove != 0 && masterMixer_ != 0) {
        BASS_ChannelRemoveSync(masterMixer_, syncToRemove);
        // Read now (BASS keeps only the most recent per-thread code), report below.
        removeSyncError = static_cast<int>(BASS_ErrorGetCode());
    }
    // A callback that lost tryLock() may have published this handle while
    // BASS_ChannelRemoveSync waited for it to return. The disarm owns cancellation, so
    // it also owns discarding that deferred action after the native callback is gone.
    deferredMixerSyncHandle_.store(0, std::memory_order_release);
    drainSfxCallbackEvents();
    // Both lines land after the locker's scope ends. The callback no longer waits for
    // this lock, but formatting and I/O still do not belong in a scheduler critical
    // section. The anchor side has always been logged; without the disarm side
    // a session that never re-anchors just stops producing anchor rows, which reads
    // identically to a session that was never armed.
    noteBassErrCode("sfx_scheduler/remove_sync", removeSyncError);
    if (miacode::preview_audio::bass::shouldLogDisarm(wasActive, hadSync, groupIndex)) {
        appendAudioDebugLog(
            QString("bass_sfx_scheduler action=disarm reason=%1 was_active=%2 had_sync=%3 group_idx=%4")
                .arg(QLatin1String(reason))
                .arg(wasActive ? 1 : 0)
                .arg(hadSync ? 1 : 0)
                .arg(groupIndex));
    }
#else
    Q_UNUSED(reason);
#endif
}

void BassPreviewAudioBackend::anchorSfxScheduler(double chartSecond)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (masterMixer_ == 0 || !playbackSession_.masterRunning
        || shuttingDown_.load(std::memory_order_acquire)) {
        return;
    }

    disarmSfxScheduler("anchor_rearm");
    const QWORD position = BASS_ChannelGetPosition(
        masterMixer_, BASS_POS_BYTE | BASS_POS_DECODE);
    if (position == static_cast<QWORD>(-1)) {
        noteBassErr("sfx_scheduler/get_decode_position");
        return;
    }

    int nextGroupIndex = 0;
    bool backgroundPendingStart = false;
    double playbackRate = 1.0;
    SfxSchedulerArmFailure armFailure;
    {
        QMutexLocker locker(&schedulerMutex_);
        if (shuttingDown_.load(std::memory_order_acquire)
            || !playbackSession_.masterRunning) {
            return;
        }
        sfxSchedulerAnchor_.chartSecond = clampTimelineSecond(chartSecond);
        sfxSchedulerAnchor_.mixerSecond = BASS_ChannelBytes2Seconds(masterMixer_, position);
        sfxSchedulerAnchor_.playbackRate = playbackSession_.backgroundTrackPlaybackRate;
        sfxSchedulerAnchor_.outputBufferSeconds = masterMixerOutputBufferSeconds_;
        sfxSchedulerAnchorDecodePosition_ = position;
        sfxSchedulerActive_ = true;
        armNextGroupSyncLocked();
        armFailure = sfxSchedulerArmFailure_;
        sfxSchedulerArmFailure_ = SfxSchedulerArmFailure();
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
    // After the anchor row, so the pair reads in the order it happened: the anchor was
    // taken, then arming its first sync failed and the scheduler switched itself off.
    logSfxSchedulerArmFailure(armFailure);
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
        // Live SFX just switched to the GUI drainEvents fallback for the rest of the
        // session — audible, and previously reported only by a bass_err row that
        // noteBassErr suppresses when BASS left no code behind. Recorded rather than
        // logged: this runs under schedulerMutex_, on the BASS mixer thread when the
        // caller is handleMixerGroupSync.
        sfxSchedulerArmFailure_.pending = true;
        sfxSchedulerArmFailure_.bassError = static_cast<int>(BASS_ErrorGetCode());
        sfxSchedulerArmFailure_.targetChartSecond = targetChartSecond;
        sfxSchedulerActive_ = false;
        return;
    }
    scheduledGroupSync_ = syncHandle;
    scheduledGroupTargetPosition_ = targetPosition;
    scheduledGroupIndex_ = startBackgroundFirst && !sameInstant ? -1 : groupIndex;
    scheduledMixerAction_ = sameInstant
        ? ScheduledMixerAction::SfxGroupAndStartPendingBackgroundTrack
        : (startBackgroundFirst
            ? ScheduledMixerAction::StartPendingBackgroundTrack
            : ScheduledMixerAction::SfxGroup);
#endif
}

void BassPreviewAudioBackend::logSfxSchedulerArmFailure(const SfxSchedulerArmFailure& failure) const
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!failure.pending) {
        return;
    }
    noteBassErrCode("sfx_scheduler/set_sync", failure.bassError);
    appendAudioDebugLog(
        QString("bass_sfx_scheduler action=deactivated reason=set_sync_failed bass_err=%1 target_chart_second=%2")
            .arg(failure.bassError)
            .arg(failure.targetChartSecond, 0, 'f', 6));
#else
    Q_UNUSED(failure);
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

    using namespace miacode::preview_audio::bass;
    if (!schedulerMutex_.tryLock()) {
        quint32 emptyHandle = 0;
        deferredMixerSyncHandle_.compare_exchange_strong(
            emptyHandle, handle, std::memory_order_release, std::memory_order_relaxed);
        SfxCallbackEvent event;
        event.kind = SfxCallbackEventKind::Deferred;
        event.handle = handle;
        event.expectedHandle = emptyHandle;
        sfxCallbackEventRing_.tryPush(event);
        return;
    }

    int callbackBassError = 0;
    ScopedRealtimeBassErrorSink errorSink(&callbackBassError);
    SfxCallbackEvent event;
    {
        std::lock_guard<QMutex> locker(schedulerMutex_, std::adopt_lock);
        processMixerGroupSyncLocked(handle, false, &event);
    }
    event.callbackBassError = callbackBassError;
    if (event.kind != SfxCallbackEventKind::None || callbackBassError != 0) {
        sfxCallbackEventRing_.tryPush(event);
    }
#else
    Q_UNUSED(handle);
#endif
}

void BassPreviewAudioBackend::processMixerGroupSyncLocked(
    quint32 handle,
    bool processedAfterContention,
    miacode::preview_audio::bass::SfxCallbackEvent* event)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    using namespace miacode::preview_audio::bass;
    if (event == nullptr || shuttingDown_.load(std::memory_order_acquire)) {
        return;
    }
    event->handle = handle;
    event->processedAfterContention = processedAfterContention;
    if (!sfxSchedulerActive_) {
        event->kind = SfxCallbackEventKind::Drop;
        event->dropReason = SfxCallbackDropReason::Inactive;
        event->expectedHandle = scheduledGroupSync_;
        return;
    }
    if (handle == 0 || handle != scheduledGroupSync_) {
        event->kind = SfxCallbackEventKind::Drop;
        event->dropReason = SfxCallbackDropReason::StaleHandle;
        event->expectedHandle = scheduledGroupSync_;
        return;
    }

    const int groupIndex = scheduledGroupIndex_;
    const ScheduledMixerAction action = scheduledMixerAction_;
    scheduledGroupSync_ = 0;
    scheduledGroupIndex_ = -1;
    scheduledMixerAction_ = ScheduledMixerAction::None;
    scheduledGroupTargetPosition_ = 0;

    const bool startBackground = action == ScheduledMixerAction::StartPendingBackgroundTrack
        || action == ScheduledMixerAction::SfxGroupAndStartPendingBackgroundTrack;
    if (startBackground && backgroundTrackSample_ != nullptr
        && playbackSession_.backgroundTrackPendingStart
        && !playbackSession_.backgroundTrackPastEnd) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = true;
        event->startedBackground = true;
    }

    const bool shouldTriggerGroup = action == ScheduledMixerAction::SfxGroup
        || action == ScheduledMixerAction::SfxGroupAndStartPendingBackgroundTrack;
    if (shouldTriggerGroup && groupIndex >= 0 && groupIndex < preparedGroups_.size()) {
        const CollapsedEventGroup& group = preparedGroups_[groupIndex];
        if (playbackSession_.eventGroupIndex <= groupIndex) {
            playbackSession_.eventGroupIndex = groupIndex + 1;
        }
        playbackSession_.lastTriggeredGroupIndex = groupIndex;
        playbackSession_.lastTriggeredGroupSecond = group.second;
        ++playbackSession_.triggeredGroupCount;
        TouchholdTransition touchholdTransition;
        triggerGroup(group, nullptr, &touchholdTransition, &event->played);
        event->kind = SfxCallbackEventKind::Trigger;
        event->groupIndex = groupIndex;
        event->groupSecond = group.second;
        event->triggeredCount = playbackSession_.triggeredGroupCount;
        event->touchholdChanged = touchholdTransition.changed;
        event->touchholdOwner = touchholdTransition.owner;
        event->touchholdPreviousOwner = touchholdTransition.previousOwner;
        event->touchholdSecond = touchholdTransition.second;
        event->touchholdSpanStartSecond = touchholdTransition.spanStartSecond;
    } else if (event->startedBackground) {
        event->kind = SfxCallbackEventKind::Trigger;
    }

    armNextGroupSyncLocked();
    event->armFailurePending = sfxSchedulerArmFailure_.pending;
    event->armFailureBassError = sfxSchedulerArmFailure_.bassError;
    event->armFailureTargetChartSecond = sfxSchedulerArmFailure_.targetChartSecond;
    sfxSchedulerArmFailure_ = SfxSchedulerArmFailure();
#else
    Q_UNUSED(handle);
    Q_UNUSED(processedAfterContention);
    Q_UNUSED(event);
#endif
}

void BassPreviewAudioBackend::drainDeferredMixerSync()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    const quint32 handle = deferredMixerSyncHandle_.exchange(0, std::memory_order_acq_rel);
    if (handle == 0) {
        return;
    }
    // The sync already fired and BASS removed it; nothing arms the next group until it is
    // processed here. A group still near its note time is played now, one that waited out a
    // stalled worker is skipped and the scheduler re-anchored at the live mixer position.
    const QWORD decodePosition = masterMixer_ != 0
        ? BASS_ChannelGetPosition(masterMixer_, BASS_POS_BYTE | BASS_POS_DECODE)
        : static_cast<QWORD>(-1);
    int callbackBassError = 0;
    bool reanchor = false;
    int lateGroupIndex = -1;
    double lateSeconds = 0.0;
    miacode::preview_audio::bass::SfxCallbackEvent event;
    {
        ScopedRealtimeBassErrorSink errorSink(&callbackBassError);
        QMutexLocker locker(&schedulerMutex_);
        if (sfxSchedulerActive_ && handle == scheduledGroupSync_
            && decodePosition != static_cast<QWORD>(-1)) {
            lateSeconds = decodePosition >= scheduledGroupTargetPosition_
                ? BASS_ChannelBytes2Seconds(masterMixer_, decodePosition - scheduledGroupTargetPosition_)
                : -BASS_ChannelBytes2Seconds(masterMixer_, scheduledGroupTargetPosition_ - decodePosition);
            reanchor = !miacode::preview_audio::bass::shouldReplayDeferredSync(lateSeconds);
        }
        if (reanchor) {
            // Already removed by BASS; clearing it keeps the re-anchor's disarm from asking
            // BASS to remove a handle that no longer exists.
            lateGroupIndex = scheduledGroupIndex_;
            scheduledGroupSync_ = 0;
            scheduledGroupIndex_ = -1;
            scheduledMixerAction_ = ScheduledMixerAction::None;
            scheduledGroupTargetPosition_ = 0;
        } else {
            processMixerGroupSyncLocked(handle, true, &event);
        }
    }
    event.callbackBassError = callbackBassError;
    logSfxCallbackEvent(event);
    if (reanchor) {
        const double liveChartSecond = currentSfxSchedulerChartSecond(
            playbackSession_.lastAuthoritativeSecond);
        appendAudioDebugLog(
            QString("bass_sfx_scheduler action=recover reason=deferred_sync_late group_idx=%1 late_ms=%2 live_chart=%3")
                .arg(lateGroupIndex)
                .arg(lateSeconds * 1000.0, 0, 'f', 1)
                .arg(liveChartSecond, 0, 'f', 6));
        resetCursor(liveChartSecond, false);
    }
#endif
}

void BassPreviewAudioBackend::recoverMissedSfxSync()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (masterMixer_ == 0 || !playbackSession_.masterRunning
        || shuttingDown_.load(std::memory_order_acquire)) {
        return;
    }
    const QWORD decodePosition = BASS_ChannelGetPosition(
        masterMixer_, BASS_POS_BYTE | BASS_POS_DECODE);
    if (decodePosition == static_cast<QWORD>(-1)) {
        return;
    }
    const QWORD graceBytes = BASS_ChannelSeconds2Bytes(
        masterMixer_, miacode::preview_audio::bass::kMissedSyncGraceSeconds);
    int groupIndex = -1;
    double lateSeconds = 0.0;
    {
        QMutexLocker locker(&schedulerMutex_);
        if (!sfxSchedulerActive_ || scheduledGroupSync_ == 0
            // Fired but lost tryLock(): drainDeferredMixerSync() owns this one.
            || deferredMixerSyncHandle_.load(std::memory_order_acquire) == scheduledGroupSync_
            || !miacode::preview_audio::bass::scheduledSyncWasMissed(
                scheduledGroupTargetPosition_, decodePosition, graceBytes)) {
            return;
        }
        groupIndex = scheduledGroupIndex_;
        lateSeconds = BASS_ChannelBytes2Seconds(
            masterMixer_, decodePosition - scheduledGroupTargetPosition_);
    }
    // The dead sync is still registered with BASS. The disarm inside resetCursor removes it
    // outside schedulerMutex_, the cursor skips the groups the mixer has already passed, and
    // the anchor arms the next one ahead of the live position.
    const double liveChartSecond = currentSfxSchedulerChartSecond(
        playbackSession_.lastAuthoritativeSecond);
    appendAudioDebugLog(
        QString("bass_sfx_scheduler action=recover reason=missed_sync group_idx=%1 late_ms=%2 live_chart=%3")
            .arg(groupIndex)
            .arg(lateSeconds * 1000.0, 0, 'f', 1)
            .arg(liveChartSecond, 0, 'f', 6));
    resetCursor(liveChartSecond, false);
#endif
}

void BassPreviewAudioBackend::serviceSfxScheduler()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    // PreviewAudioWorker never dispatches syncPreviewPlaybackClockTransaction(), so the
    // chain's worker-side upkeep has to ride on what it does execute: DrainEvents and
    // SyncBackgroundTrack every playback tick, plus its own health tick when the GUI stalls.
    drainSfxCallbackEvents();
    drainDeferredMixerSync();
    recoverMissedSfxSync();
#endif
}

void BassPreviewAudioBackend::drainSfxCallbackEvents()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    using namespace miacode::preview_audio::bass;
    SfxCallbackEvent event;
    for (std::size_t drained = 0;
         drained < SfxCallbackEventRing::kCapacity && sfxCallbackEventRing_.tryPop(&event);
         ++drained) {
        logSfxCallbackEvent(event);
    }
    const quint64 dropped = sfxCallbackEventRing_.takeDroppedCount();
    if (dropped > 0 && runtimeAudioDebugEnabled()) {
        appendAudioDebugLog(QString("bass_sfx_mixer_diag_drop count=%1").arg(dropped));
    }
#endif
}

void BassPreviewAudioBackend::logSfxCallbackEvent(
    const miacode::preview_audio::bass::SfxCallbackEvent& event) const
{
#ifdef MIACODE_HAS_BASS_AUDIO
    using namespace miacode::preview_audio::bass;
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    if (event.kind == SfxCallbackEventKind::Deferred) {
        appendAudioDebugLog(
            QString("bass_sfx_mixer_deferred reason=scheduler_busy handle=%1 pending=%2")
                .arg(event.handle)
                .arg(event.expectedHandle));
        return;
    }
    if (event.kind == SfxCallbackEventKind::Drop) {
        const char* reason = event.dropReason == SfxCallbackDropReason::Inactive
            ? "inactive"
            : "stale_handle";
        appendAudioDebugLog(
            QString("bass_sfx_mixer_drop reason=%1 handle=%2 expected=%3 deferred=%4")
                .arg(QLatin1String(reason))
                .arg(event.handle)
                .arg(event.expectedHandle)
                .arg(event.processedAfterContention ? 1 : 0));
    } else if (event.kind == SfxCallbackEventKind::Trigger) {
        QString playedKinds;
        for (std::size_t index = 0; index < kPlayedSfxKindCount; ++index) {
            if ((event.played.mask & (quint32(1) << static_cast<quint32>(index))) == 0) {
                continue;
            }
            if (!playedKinds.isEmpty()) {
                playedKinds.append(QLatin1Char(','));
            }
            playedKinds.append(QStringLiteral("%1:%2")
                .arg(QLatin1String(playedSfxKindName(index)))
                .arg(static_cast<double>(event.played.gains[index]), 0, 'f', 2));
        }
        appendAudioDebugLog(
            QString("bass_sfx_mixer_trigger group_idx=%1 group_second=%2 count=%3 started_bgm=%4 played=%5 deferred=%6")
                .arg(event.groupIndex)
                .arg(event.groupSecond, 0, 'f', 6)
                .arg(event.triggeredCount)
                .arg(event.startedBackground ? 1 : 0)
                .arg(playedKinds.isEmpty() ? QStringLiteral("(none)") : playedKinds)
                .arg(event.processedAfterContention ? 1 : 0));
    }

    if (event.touchholdChanged) {
        TouchholdTransition transition;
        transition.changed = true;
        transition.owner = event.touchholdOwner;
        transition.previousOwner = event.touchholdPreviousOwner;
        transition.second = event.touchholdSecond;
        transition.spanStartSecond = event.touchholdSpanStartSecond;
        logTouchholdTransition(transition);
    }
    if (event.callbackBassError != 0) {
        noteBassErrCode("sfx_scheduler/mixer_callback", event.callbackBassError);
    }
    if (event.armFailurePending) {
        SfxSchedulerArmFailure failure;
        failure.pending = true;
        failure.bassError = event.armFailureBassError;
        failure.targetChartSecond = event.armFailureTargetChartSecond;
        logSfxSchedulerArmFailure(failure);
    }
#else
    Q_UNUSED(event);
#endif
}

void BassPreviewAudioBackend::reconcileTouchholdVoice(double second, TouchholdTransition* out)
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
    const int previousOwner = touchholdOwnerSpanIndex_;
    touchholdOwnerSpanIndex_ = owner;

    TouchholdTransition transition;
    transition.changed = true;
    transition.owner = owner;
    transition.previousOwner = previousOwner;
    transition.second = second;
    if (owner < 0) {
        touchholdSample_->stop();
    } else {
        const TouchholdSpan& span = preparedTimeline_.touchholdSpans[owner];
        touchholdSample_->setCurrentSec(qMax(0.0, second - span.startSecond));
        touchholdSample_->play();
        transition.spanStartSecond = span.startSecond;
    }
    // Recorded for the caller when it holds schedulerMutex_, logged inline when it does
    // not. The audio-thread route arrives here from triggerGroup() with that lock held, and
    // this used to write the file underneath it -- the same defect as logPlaybackStatus,
    // on the worse thread, and the one instance the branch audit's T-1 missed.
    if (out != nullptr) {
        *out = transition;
        return;
    }
    logTouchholdTransition(transition);
#else
    Q_UNUSED(second);
    Q_UNUSED(out);
#endif
}

void BassPreviewAudioBackend::logTouchholdTransition(const TouchholdTransition& transition) const
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!transition.changed) {
        return;
    }
    if (transition.owner < 0) {
        appendAudioDebugLog(
            QString("bass_sfx_touchhold action=stop prev_owner=%1 second=%2")
                .arg(transition.previousOwner)
                .arg(transition.second, 0, 'f', 6));
        return;
    }
    // The third sound source with no log of its own. Only fires on an ownership
    // change (reconcileTouchholdVoice returns early when the voice already belongs to
    // the right span), so this stays rare even during dense touch-hold sections.
    appendAudioDebugLog(
        QString("bass_sfx_touchhold action=start owner=%1 prev_owner=%2 second=%3 span_start=%4")
            .arg(transition.owner)
            .arg(transition.previousOwner)
            .arg(transition.second, 0, 'f', 6)
            .arg(transition.spanStartSecond, 0, 'f', 6));
#else
    Q_UNUSED(transition);
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


bool BassPreviewAudioBackend::playKindInternal(
    const QString& kind,
    double gain,
    int* nativeErrorCode)
{
    if (nativeErrorCode != nullptr) {
        *nativeErrorCode = 0;
    }
#ifdef MIACODE_HAS_BASS_AUDIO
    Sample* sample = sampleForKind(kind);
    if (sample == nullptr) {
        return false;
    }
    return sample->playOneShot(gain, nativeErrorCode);
#else
    Q_UNUSED(kind);
    Q_UNUSED(gain);
    return false;
#endif
}

bool BassPreviewAudioBackend::audition(const QString& kind, double gain)
{
    MC_OP("BassPreviewAudioBackend::audition");
    lastNativeErrorCode_ = 0;
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!initializeAudioEngine() || masterMixer_ == 0) {
        return false;
    }
    if (!playbackSession_.masterRunning) {
        resetMasterMixerClock(0.0);
        // G1 Commit 6: master mixer was started at engine init and never stops.
        playbackSession_.masterRunning = true;
        audioHealthPlaybackRunning_.store(true, std::memory_order_release);
    }
    const bool started = playKindInternal(kind, gain, &lastNativeErrorCode_);
    // This path emits a real note sound while bypassing the scheduler, the group
    // cursor, and therefore both group-level logs. Unlogged, an audition was
    // indistinguishable from "no sound was played at all" in a capture — which is
    // precisely the ambiguity that stalled the device-change investigation.
    appendAudioDebugLog(
        QString("bass_sfx_audition kind=%1 gain=%2 started=%3")
            .arg(kind)
            .arg(gain, 0, 'f', 2)
            .arg(started ? 1 : 0));
    return started;
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
    miacode::preview_audio::PreviewBassEmergencyPause::disarm();
    stopAll();
}
