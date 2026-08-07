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
    TouchholdTransition* touchholdOut)
{
    const auto record = [playedKindsOut](const QString& kind, double gain, bool started) {
        if (playedKindsOut == nullptr || !started) {
            return;
        }
        if (!playedKindsOut->isEmpty()) {
            playedKindsOut->append(QLatin1Char(','));
        }
        playedKindsOut->append(QStringLiteral("%1:%2").arg(kind).arg(gain, 0, 'f', 2));
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
        sfxSchedulerActive_ = false;
        sfxSchedulerAnchorDecodePosition_ = 0;
    }
    // BASS_ChannelRemoveSync runs OUTSIDE schedulerMutex_ because the two locks involved
    // are otherwise taken in opposite orders on the two threads that matter:
    //
    //   GUI thread    : schedulerMutex_ -> BASS internal sync lock (inside RemoveSync)
    //   BASS callback : BASS internal sync lock -> schedulerMutex_ (handleMixerGroupSync)
    //
    // RemoveSync waits for an in-flight sync callback to finish, and that callback can be
    // blocked acquiring schedulerMutex_ -- whose holder is the very thread sitting inside
    // RemoveSync. Textbook ABBA, and its symptom is an unresponsive GUI thread, which is
    // indistinguishable from the freeze this branch exists to diagnose. The exposure is
    // every ordinary interaction, not an edge case: this function has 16 call sites
    // covering pause, seek, chart switch, volume and rate.
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
    // Both lines land after the locker's scope ends: schedulerMutex_ is also taken by
    // the mixer sync callback on the BASS audio thread, so a log write underneath it
    // stalls playback. The anchor side has always been logged; without the disarm side
    // a session that never re-anchors just stops producing anchor rows, which reads
    // identically to a session that was never armed.
    noteBassErrCode("sfx_scheduler/remove_sync", removeSyncError);
    appendAudioDebugLog(
        QString("bass_sfx_scheduler action=disarm reason=%1 was_active=%2 had_sync=%3 group_idx=%4")
            .arg(QLatin1String(reason))
            .arg(wasActive ? 1 : 0)
            .arg(hadSync ? 1 : 0)
            .arg(groupIndex));
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

    // Set only when this callback actually emits sound, so the log write below can
    // happen after the scheduler mutex is released. This runs on the BASS mixer
    // thread: holding an audio-callback lock across a log write is exactly the kind
    // of stall the buffer-health probe exists to catch.
    bool triggered = false;
    int triggeredGroupIndex = -1;
    double triggeredGroupSecond = 0.0;
    quint64 triggeredCount = 0;
    bool startedBackground = false;
    QString playedKinds;
    SfxSchedulerArmFailure armFailure;
    QString dropReason;
    quint32 expectedSyncHandle = 0;
    // Same deal as playedKinds: filled under the lock, logged after it. Touch-hold was the
    // one sound source still writing its row from inside the critical section.
    TouchholdTransition touchholdTransition;

    {
        QMutexLocker locker(&schedulerMutex_);
        if (shuttingDown_.load(std::memory_order_acquire)) {
            return;  // teardown, not a dropped group
        }
        // Either branch means the group this sync was armed for is discarded outright:
        // silence where the chart has a note. Recorded rather than logged here for the
        // same reason as the trigger row below -- this is the BASS mixer thread holding
        // the scheduler lock.
        if (!sfxSchedulerActive_) {
            dropReason = QStringLiteral("inactive");
        } else if (handle == 0 || handle != scheduledGroupSync_) {
            dropReason = QStringLiteral("stale_handle");
        }
        if (!dropReason.isEmpty()) {
            expectedSyncHandle = scheduledGroupSync_;
        } else {
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
                startedBackground = true;
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
                triggerGroup(
                    group,
                    runtimeAudioDebugEnabled() ? &playedKinds : nullptr,
                    &touchholdTransition);
                triggered = true;
                triggeredGroupIndex = groupIndex;
                triggeredGroupSecond = group.second;
                triggeredCount = playbackSession_.triggeredGroupCount;
            }
            armNextGroupSyncLocked();
            armFailure = sfxSchedulerArmFailure_;
            sfxSchedulerArmFailure_ = SfxSchedulerArmFailure();
        }
    }

    if (!dropReason.isEmpty()) {
        appendAudioDebugLog(
            QString("bass_sfx_mixer_drop reason=%1 handle=%2 expected=%3")
                .arg(dropReason)
                .arg(handle)
                .arg(expectedSyncHandle));
        return;
    }

    // The GUI fallback path logs every drain as `bass_sfx_drain`, but this path --
    // the one a LIVE session actually uses -- logged nothing, which made "did an SFX
    // fire after the transport was paused?" unanswerable from a capture. It is the
    // only remaining way a note sound can be emitted while playing, so it has to be
    // visible. One line per triggered group, `--debug` only, same cadence as the
    // groups themselves.
    if (triggered || startedBackground) {
        appendAudioDebugLog(
            QString("bass_sfx_mixer_trigger group_idx=%1 group_second=%2 count=%3 started_bgm=%4 played=%5")
                .arg(triggeredGroupIndex)
                .arg(triggeredGroupSecond, 0, 'f', 6)
                .arg(triggeredCount)
                .arg(startedBackground ? 1 : 0)
                .arg(playedKinds.isEmpty() ? QStringLiteral("(none)") : playedKinds));
    }
    // After the trigger row, so a touch-hold ownership change reads as a consequence of
    // the group that caused it. No-op unless the voice actually changed hands.
    logTouchholdTransition(touchholdTransition);
    // Last, so the row order matches the order of events: this group fired, and then
    // re-arming for the next one failed and left the scheduler off.
    logSfxSchedulerArmFailure(armFailure);
#else
    Q_UNUSED(handle);
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
        audioHealthPlaybackRunning_.store(true, std::memory_order_release);
    }
    const bool started = playKindInternal(kind, gain);
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
    stopAll();
}
