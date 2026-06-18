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

#include <cstdio>   // G1 Commit 8 followup: std::snprintf for startup-beacon lines

#ifdef Q_OS_WIN
#include <windows.h>

#include "bass.h"
#include "bassmix.h"
#endif

#include "BassPreviewAudioBackendImpl.h"
#include "BassPreviewAudioBackendSample.h"

using namespace miacode::audio::bass_detail;

void BassPreviewAudioBackend::resetCursor(double second, bool includeCurrentSecond)
{
    MC_OP("BassPreviewAudioBackend::resetCursor");
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
        // G1 Commit 7: pre-G1 each drain had to cancel a matching BASS_SYNC_POS arm
        // so the same group wasn't triggered twice. No arms exist anymore (the SYNC
        // scheduler is gone), so the cancellation block has been deleted.
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

void BassPreviewAudioBackend::reconcileTouchholdVoice(double second)
{
#ifdef Q_OS_WIN
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
#ifdef Q_OS_WIN
    if (touchholdSample_ != nullptr) {
        touchholdSample_->stop();
    }
#endif
    touchholdOwnerSpanIndex_ = -1;
}

void BassPreviewAudioBackend::restoreTouchholdVoices(double second)
{
    MC_OP("BassPreviewAudioBackend::restoreTouchholdVoices");
#ifdef Q_OS_WIN
    pauseTouchholdVoices();
    reconcileTouchholdVoice(second);
#else
    Q_UNUSED(second);
#endif
}


bool BassPreviewAudioBackend::playKindInternal(const QString& kind, double gain)
{
#ifdef Q_OS_WIN
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
#ifdef Q_OS_WIN
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
