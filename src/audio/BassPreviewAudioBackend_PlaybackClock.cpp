#include "BassPreviewAudioBackend.h"

#include "BassPreviewDebugLogRouting.h"
#include "BassPreviewRetainedState.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/FileContentStamp.h"
#include "common/Mmcss.h"
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

#ifdef MIACODE_HAS_BASS_AUDIO
#include "bass.h"
#include "bassmix.h"
#endif

#include "BassPreviewAudioBackendImpl.h"
#include "BassPreviewAudioBackendSample.h"

using namespace miacode::audio::bass_detail;

double BassPreviewAudioBackend::retainedTransportSecond() const
{
    return clampTimelineSecond(playbackSession_.lastAuthoritativeSecond);
}

bool BassPreviewAudioBackend::retainedSecondMatches(double targetSecond) const
{
    return miacode::preview_audio::bass::retainedSecondsMatch(
        retainedTransportSecond(),
        clampTimelineSecond(targetSecond));
}

void BassPreviewAudioBackend::noteInitWindowOpened(const QString& reason)
{
    Q_UNUSED(reason);
    initWindowActive_ = true;
}

void BassPreviewAudioBackend::noteTransportReady(const QString& reason)
{
    if (!initWindowActive_) {
        return;
    }
    initWindowActive_ = false;
    ++transportReadyGeneration_;
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::TransportReady,
        QString("reason=%1 generation=%2")
            .arg(reason)
            .arg(transportReadyGeneration_));
}

void BassPreviewAudioBackend::appendBassDebugLog(
    miacode::preview_audio::bass::BassDebugOperation operation,
    const QString& payload,
    bool initWindowContext) const
{
    const auto route = miacode::preview_audio::bass::bassDebugRouteForOperation(operation, initWindowContext);
    QString text = QStringLiteral("%1 op=%2")
        .arg(miacode::preview_audio::bass::bassDebugRouteName(route))
        .arg(bassDebugOperationLabel(operation));
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    appendAudioDebugLog(text);
}


void BassPreviewAudioBackend::invalidateRetainedPlaybackState(const QString& reason)
{
    const RetainedPlaybackMode previousMode = retainedPlaybackMode_;
    noteInitWindowOpened(reason);
    retainedPlaybackMode_ = RetainedPlaybackMode::Invalidated;
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::InvalidateRetainedState,
        QString("reason=%1 previous=%2")
            .arg(reason)
            .arg(retainedPlaybackModeLabel(previousMode)),
        true);
}

void BassPreviewAudioBackend::updateRetainedBgmState()
{
    if (backgroundTrackSample_ != nullptr) {
        if (retainedBgmState_ != RetainedBgmState::MissingOnDiskIgnored) {
            retainedBgmState_ = RetainedBgmState::LoadedUsable;
        }
        return;
    }
    retainedBgmState_ = RetainedBgmState::NoneLoaded;
}

void BassPreviewAudioBackend::logTrackFileMissingAfterLoadIfNeeded()
{
    if (backgroundTrackSample_ == nullptr
        || preparedAssets_.trackPath.isEmpty()
        || trackMissingAfterLoadLogged_
        || QFileInfo::exists(preparedAssets_.trackPath)) {
        return;
    }
    trackMissingAfterLoadLogged_ = true;
    retainedBgmState_ = RetainedBgmState::MissingOnDiskIgnored;
    appendAudioDebugLog(
        QString("bgm_disk_missing_after_load path=%1 txn=%2")
            .arg(preparedAssets_.trackPath)
            .arg(playbackTransactionId_));
}


void BassPreviewAudioBackend::setPlaybackTransactionId(quint64 transactionId)
{
    playbackTransactionId_ = transactionId;
}


namespace {

#ifdef MIACODE_HAS_BASS_AUDIO

namespace audio_health = miacode::preview_audio::health;

// BASS_ACTIVE_* -> the backend-neutral vocabulary in PreviewAudioHealth.h. Kept in this
// TU because it is the one that owns bass.h.
audio_health::ChannelActivity activityFromBass(DWORD active)
{
    switch (active) {
    case BASS_ACTIVE_STOPPED:
        return audio_health::ChannelActivity::Stopped;
    case BASS_ACTIVE_PLAYING:
        return audio_health::ChannelActivity::Playing;
    case BASS_ACTIVE_STALLED:
        return audio_health::ChannelActivity::Stalled;
    case BASS_ACTIVE_PAUSED:
        return audio_health::ChannelActivity::Paused;
    case BASS_ACTIVE_PAUSED_DEVICE:
        return audio_health::ChannelActivity::PausedDevice;
    default:
        return audio_health::ChannelActivity::Unknown;
    }
}

// The master mixer is the real playback channel (BASS_ChannelPlay in engine init), so it
// is the one whose STALLED state means "the device buffer ran dry" — i.e. BASS's update
// thread lost its CPU race. That is the underrun this probe is hunting.
audio_health::ChannelActivity playbackActivityFor(DWORD handle)
{
    if (handle == 0) {
        return audio_health::ChannelActivity::Unknown;
    }
    return activityFromBass(BASS_ChannelIsActive(handle));
}

// Sources live inside the mixer graph (source -> per-sample resampler -> master mixer),
// so their state must be read with BASS_Mixer_ChannelIsActive, matching Sample::isPlaying.
// A STALLED source means the decode side ran dry (slow disk / starved decode), which is a
// different failure from a device-buffer underrun and worth distinguishing.
audio_health::ChannelActivity mixerSourceActivityFor(DWORD handle)
{
    if (handle == 0) {
        return audio_health::ChannelActivity::Unknown;
    }
    return activityFromBass(BASS_Mixer_ChannelIsActive(handle));
}

audio_health::BufferSnapshot bufferSnapshotFor(DWORD mixerHandle)
{
    audio_health::BufferSnapshot snapshot;
    BASS_INFO info = {};
    if (BASS_GetInfo(&info)) {
        snapshot.minBufferMs = static_cast<qint64>(info.minbuf);
        // BASS_INFO::latency is only populated when BASS_Init was given
        // BASS_DEVICE_LATENCY. MiaCode does not request it (it costs a test-buffer
        // playback at init), so this reads 0 — recorded anyway so the field's meaning is
        // unambiguous rather than silently absent.
        snapshot.initLatencyMs = static_cast<qint64>(info.latency);
        snapshot.deviceFreq = static_cast<qint64>(info.freq);
    }
    const DWORD configBuffer = BASS_GetConfig(BASS_CONFIG_BUFFER);
    if (configBuffer != static_cast<DWORD>(-1)) {
        snapshot.configBufferMs = static_cast<qint64>(configBuffer);
    }
    const DWORD updatePeriod = BASS_GetConfig(BASS_CONFIG_UPDATEPERIOD);
    if (updatePeriod != static_cast<DWORD>(-1)) {
        snapshot.updatePeriodMs = static_cast<qint64>(updatePeriod);
    }
    const DWORD updateThreads = BASS_GetConfig(BASS_CONFIG_UPDATETHREADS);
    if (updateThreads != static_cast<DWORD>(-1)) {
        snapshot.updateThreads = static_cast<qint64>(updateThreads);
    }
    if (mixerHandle != 0) {
        // Playback buffer fill level. This is the number that collapses first when the
        // update thread is starved of CPU — it drops toward zero right before BASS
        // reports STALLED.
        const DWORD available = BASS_ChannelGetData(mixerHandle, nullptr, BASS_DATA_AVAILABLE);
        if (available != static_cast<DWORD>(-1)) {
            snapshot.bufferedBytes = static_cast<qint64>(available);
            const double seconds =
                BASS_ChannelBytes2Seconds(mixerHandle, static_cast<QWORD>(available));
            if (seconds >= 0.0) {
                snapshot.bufferedMs = static_cast<qint64>(seconds * 1000.0);
            }
        }
    }
    return snapshot;
}

#endif  // MIACODE_HAS_BASS_AUDIO

}  // namespace

// Publishes the two BASS handles the sampler is allowed to touch. Called from the GUI
// thread whenever they change; the sampler only ever sees integers, never objects.
void BassPreviewAudioBackend::publishAudioHealthHandles()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    audioHealthMixerHandle_.store(masterMixer_, std::memory_order_release);
    const quint32 source =
        (backgroundTrackSample_ != nullptr && backgroundTrackSample_->valid())
            ? static_cast<quint32>(backgroundTrackSample_->source)
            : 0u;
    audioHealthSourceHandle_.store(source, std::memory_order_release);
#endif
}

void BassPreviewAudioBackend::startAudioHealthSampler()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (audioHealthSamplerThread_.joinable()) {
        return;
    }
    // Diagnostics-only, exactly as the probe it replaces: no --debug, no sampler, and the
    // GUI thread makes none of these calls either way.
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    audioHealthSamplerStop_.store(false, std::memory_order_release);
    publishAudioHealthHandles();
    audioHealthSamplerThread_ = std::thread([this]() {
        // 100 ms: fast enough to keep the tick-rate stall detection this replaces
        // meaningful, slow enough that the device lock is not hammered. The GUI thread no
        // longer depends on this cadence for anything -- it reads whatever is published.
        constexpr auto kInterval = std::chrono::milliseconds(100);
        while (!audioHealthSamplerStop_.load(std::memory_order_acquire)) {
            const quint32 mixer = audioHealthMixerHandle_.load(std::memory_order_acquire);
            const quint32 source = audioHealthSourceHandle_.load(std::memory_order_acquire);
            // THE BLOCKING CALLS. On this thread they are harmless: a multi-second stall
            // here delays the next sample and nothing else.
            HealthSample sample;
            sample.mixerActivity = playbackActivityFor(static_cast<DWORD>(mixer));
            sample.backgroundActivity = source != 0
                ? mixerSourceActivityFor(static_cast<DWORD>(source))
                : audio_health::ChannelActivity::Unknown;
            sample.buffer = bufferSnapshotFor(static_cast<DWORD>(mixer));
            if (source != 0) {
                // Same arithmetic Sample::currentSec() does, from the handle alone -- the
                // sampler never touches the Sample object, so there is no lifetime race
                // with a chart switch freeing it.
                const QWORD position =
                    BASS_Mixer_ChannelGetPosition(static_cast<DWORD>(source), BASS_POS_BYTE);
                sample.bgmRawSecond =
                    BASS_ChannelBytes2Seconds(static_cast<DWORD>(source), position);
            }
            sample.sampledAtMs = QDateTime::currentMSecsSinceEpoch();
            {
                QMutexLocker locker(&audioHealthSampleMutex_);
                sample.sequence = audioHealthSample_.sequence + 1;
                audioHealthSample_ = sample;
            }
            std::this_thread::sleep_for(kInterval);
        }
    });
#endif
}

void BassPreviewAudioBackend::stopAudioHealthSampler()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    audioHealthSamplerStop_.store(true, std::memory_order_release);
    if (audioHealthSamplerThread_.joinable()) {
        // Joined, not detached: the sampler calls into BASS, so it must be gone before
        // BASS_Free runs in the destructor. Worst case this waits out one blocked query,
        // which is bounded by the endpoint switch itself.
        audioHealthSamplerThread_.join();
    }
#endif
}

void BassPreviewAudioBackend::logAudioHealth(double authoritativeSecond)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!engineInitialized_) {
        return;
    }
    // Reads the sampler's published snapshot. Deliberately NO BASS calls on this path --
    // see the HealthSample comment in the header for what they used to cost.
    HealthSample sample;
    {
        QMutexLocker locker(&audioHealthSampleMutex_);
        sample = audioHealthSample_;
    }
    const audio_health::ChannelActivity mixerActivity = sample.mixerActivity;
    const audio_health::ChannelActivity backgroundActivity = sample.backgroundActivity;
    const bool underrun =
        audio_health::isUnderrun(mixerActivity) || audio_health::isUnderrun(backgroundActivity);

    // Stall edges still log on the first tick that observes them. The observation itself
    // now happens on the sampler at 100 ms rather than on this thread at tick rate, so a
    // shorter underrun than that is the one case this can miss -- the price of not parking
    // the GUI thread on BASS's device lock, and a cheap one next to a 4 s freeze.
    const audio_health::StallEdge edge = audio_health::updateStall(
        &playbackSession_.stallTracker, underrun, authoritativeSecond);
    if (edge != audio_health::StallEdge::None) {
        appendAudioDebugLog(audio_health::stallEdgePayload(
            edge,
            playbackTransactionId_,
            authoritativeSecond,
            mixerActivity,
            backgroundActivity,
            playbackSession_.stallTracker));
    }

    if (!audio_health::shouldLogHealth(
            authoritativeSecond, playbackSession_.lastHealthLogSecond)) {
        return;
    }
    playbackSession_.lastHealthLogSecond = authoritativeSecond;

    const miacode::mmcss::LastRegistrationStatus mmcss = miacode::mmcss::lastRegistrationStatus();
    appendAudioDebugLog(audio_health::healthPayload(
        playbackTransactionId_,
        authoritativeSecond,
        mixerActivity,
        backgroundActivity,
        playbackSession_.stallTracker,
        // From the snapshot, not a fresh query: this was the third BASS call this function
        // used to make on the GUI thread.
        sample.buffer,
        // Nothing in MiaCode registers a BASS thread with MMCSS. The only registration in
        // the default path is the QSG render thread
        // (preview/quick_scene/PreviewQuickSceneRoot.cpp), so BASS's own update/mixer
        // threads run at normal priority and are fully exposed to CPU contention.
        /*mmcssRegisteredOnAudioThreads=*/false,
        mmcss.everRegistered ? mmcss.lastTaskClass : QString()));
#else
    Q_UNUSED(authoritativeSecond);
#endif
}

QString BassPreviewAudioBackend::scheduledMixerActionLabel(ScheduledMixerAction action)
{
    switch (action) {
    case ScheduledMixerAction::SfxGroup:
        return QStringLiteral("sfx_group");
    case ScheduledMixerAction::StartPendingBackgroundTrack:
        return QStringLiteral("start_pending_bgm");
    case ScheduledMixerAction::SfxGroupAndStartPendingBackgroundTrack:
        return QStringLiteral("sfx_group_and_start_pending_bgm");
    case ScheduledMixerAction::None:
    default:
        return QStringLiteral("none");
    }
}

void BassPreviewAudioBackend::logPlaybackStatus(double authoritativeSecond, double fallbackSecond)
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    // Runs before the bass_status interval gate below so stall detection polls at tick
    // rate while the buffer-health line keeps its own, much coarser cadence.
    logAudioHealth(authoritativeSecond);
    const double statusLogIntervalSeconds =
        miacode::debug_options::previewWaveformAlignmentDiagnosticsEnabled()
            ? qMax(0.001, static_cast<double>(
                  miacode::debug_options::previewWaveformAlignmentDiagnosticSampleMs()) / 1000.0)
            : kBassPreviewStatusLogIntervalSeconds;
    if (playbackSession_.lastStatusLogSecond >= 0.0
        && authoritativeSecond - playbackSession_.lastStatusLogSecond >= 0.0
        && authoritativeSecond - playbackSession_.lastStatusLogSecond < statusLogIntervalSeconds) {
        return;
    }
    playbackSession_.lastStatusLogSecond = authoritativeSecond;

    // The mixer callback advances the SFX cursor and can unpause a pending BGM, so the
    // fields below have to be read as one consistent set under the scheduler lock.
    //
    // What must NOT happen under that lock is the formatting and the file write. This log
    // line has 26 substitutions and ends in appendAudioDebugLog; schedulerMutex_ is also
    // taken by handleMixerGroupSync on the BASS mixer thread, so every millisecond spent
    // holding it here directly delays the next group of note sounds. One slow disk write
    // on the GUI thread becomes a late note or an underrun -- the exact stall the
    // buffer-health probe was added to catch, manufactured by the probe's sibling.
    // handleMixerGroupSync already follows this rule (its own comment says so) and
    // disarmSfxScheduler was fixed to follow it; this was the last holdout.
    //
    // So: copy into locals, release, then format and write. The snapshot stays atomic;
    // the lock is held for a few reads instead of for a formatted I/O.
    double mixerSecond = 0.0;
    double bgmRawSecond = -1.0;
    double bgmChartSecond = -1.0;
    double bgmExpectedRawSecond = -1.0;
    double bgmDeltaMs = 0.0;
    double bgmRawDeltaMs = 0.0;
    double bgmLengthSecond = -1.0;
    int nextGroupIndex = -1;
    double nextGroupSecond = -1.0;
    QString bgmSpeedModeLabel = QStringLiteral("none");
    double backgroundTrackPlaybackRate = 0.0;
    double backgroundTrackOffsetSeconds = 0.0;
    int lastTriggeredGroupIndex = -1;
    double lastTriggeredGroupSecond = 0.0;
    quint64 triggeredGroupCount = 0;
    bool backgroundTrackRunning = false;
    bool backgroundTrackPendingStart = false;
    bool masterRunning = false;
    int armedGroupIndex = -1;
    QString armedActionLabel;
    // Read before the scheduler lock, and off the sampler's own mutex: nothing here may
    // call into BASS, least of all under schedulerMutex_.
    HealthSample healthSample;
    {
        QMutexLocker healthLocker(&audioHealthSampleMutex_);
        healthSample = audioHealthSample_;
    }
    {
        QMutexLocker schedulerLocker(&schedulerMutex_);
        mixerSecond = (authoritativeSecond - playbackSession_.sessionStartSecond)
            / qMax(kBassPreviewMinRate, playbackSession_.sessionPlaybackRate);
        if (backgroundTrackSample_ != nullptr) {
            // From the sampler's snapshot, NOT backgroundTrackSample_->currentSec(). That
            // call is BASS_Mixer_ChannelGetPosition + BASS_ChannelBytes2Seconds on the
            // GUI thread, and it ran here while holding schedulerMutex_ -- so an endpoint
            // switch blocked the GUI thread and the mixer callback behind it at once. The
            // value is at most one sampler interval stale, which for a diagnostic row is
            // immaterial next to what it cost.
            bgmRawSecond = healthSample.bgmRawSecond;
            bgmChartSecond = bgmRawSecond - playbackSession_.backgroundTrackOffsetSeconds;
            bgmExpectedRawSecond =
                authoritativeSecond + playbackSession_.backgroundTrackOffsetSeconds;
            bgmDeltaMs = (authoritativeSecond - bgmChartSecond) * 1000.0;
            bgmRawDeltaMs = (bgmRawSecond - bgmExpectedRawSecond) * 1000.0;
            bgmLengthSecond = backgroundTrackSample_->lengthSeconds;
            bgmSpeedModeLabel = sampleSpeedModeLabel(backgroundTrackSample_->speedMode);
        }
        // `next_group_idx` is the event-group cursor: what the timeline will trigger next.
        // It is NOT what the mixer sync is armed for -- scheduledGroupIndex_ is -1 while
        // the armed sync exists only to start a pending BGM, and the two also diverge
        // between a group firing and the next arm. `armed_group_idx` / `armed_action`
        // report the scheduler's own view so that difference is readable, not inferred.
        nextGroupIndex = playbackSession_.eventGroupIndex;
        nextGroupSecond = (nextGroupIndex >= 0 && nextGroupIndex < preparedGroups_.size())
            ? preparedGroups_[nextGroupIndex].second
            : -1.0;
        backgroundTrackPlaybackRate = playbackSession_.backgroundTrackPlaybackRate;
        backgroundTrackOffsetSeconds = playbackSession_.backgroundTrackOffsetSeconds;
        lastTriggeredGroupIndex = playbackSession_.lastTriggeredGroupIndex;
        lastTriggeredGroupSecond = playbackSession_.lastTriggeredGroupSecond;
        triggeredGroupCount = playbackSession_.triggeredGroupCount;
        backgroundTrackRunning = playbackSession_.backgroundTrackRunning;
        backgroundTrackPendingStart = playbackSession_.backgroundTrackPendingStart;
        masterRunning = playbackSession_.masterRunning;
        armedGroupIndex = scheduledGroupIndex_;
        armedActionLabel = scheduledMixerActionLabel(scheduledMixerAction_);
    }
    const double driftMs = (authoritativeSecond - fallbackSecond) * 1000.0;
    appendAudioDebugLog(
        QString("bass_status txn=%1 auth=%2 mixer=%3 bgm_raw=%4 bgm_chart=%5 fallback=%6 drift_ms=%7 next_group_idx=%8 next_group_second=%9 last_trigger_idx=%10 last_trigger_second=%11 triggered_count=%12 rate=%13 speed_mode=%14 bgm_delta_ms=%15 bgm_raw_expected=%16 bgm_raw_delta_ms=%17 bgm_offset=%18 bgm_len=%19 bgm_running=%20 bgm_pending=%21 master_running=%22 retained_mode=%23 status_interval_ms=%24 armed_group_idx=%25 armed_action=%26")
            .arg(playbackTransactionId_)
            .arg(authoritativeSecond, 0, 'f', 6)
            .arg(mixerSecond, 0, 'f', 6)
            .arg(bgmRawSecond, 0, 'f', 6)
            .arg(bgmChartSecond, 0, 'f', 6)
            .arg(fallbackSecond, 0, 'f', 6)
            .arg(driftMs, 0, 'f', 3)
            .arg(nextGroupIndex)
            .arg(nextGroupSecond, 0, 'f', 6)
            .arg(lastTriggeredGroupIndex)
            .arg(lastTriggeredGroupSecond, 0, 'f', 6)
            .arg(triggeredGroupCount)
            .arg(backgroundTrackPlaybackRate, 0, 'f', 3)
            .arg(bgmSpeedModeLabel)
            .arg(bgmDeltaMs, 0, 'f', 3)
            .arg(bgmExpectedRawSecond, 0, 'f', 6)
            .arg(bgmRawDeltaMs, 0, 'f', 3)
            .arg(backgroundTrackOffsetSeconds, 0, 'f', 6)
            .arg(bgmLengthSecond, 0, 'f', 6)
            .arg(backgroundTrackRunning ? 1 : 0)
            .arg(backgroundTrackPendingStart ? 1 : 0)
            .arg(masterRunning ? 1 : 0)
            .arg(retainedPlaybackModeLabel(retainedPlaybackMode_))
            .arg(statusLogIntervalSeconds * 1000.0, 0, 'f', 3)
            .arg(armedGroupIndex)
            .arg(armedActionLabel));
#else
    Q_UNUSED(authoritativeSecond);
    Q_UNUSED(fallbackSecond);
#endif
}

QString BassPreviewAudioBackend::groupSignature(const CollapsedEventGroup& group) const
{
    QStringList parts;
    parts.reserve(group.orderedEvents.size() + group.aggregatedPlaybacks.size());
    for (const Event& event : group.orderedEvents) {
        parts.append(QStringLiteral("e:%1").arg(event.kind));
    }
    for (const miacode::preview_sfx_timeline::AggregatedPlayback& playback : group.aggregatedPlaybacks) {
        parts.append(QStringLiteral("a:%1#%2").arg(playback.kind).arg(playback.count));
    }
    return parts.join(QLatin1Char('|'));
}

void BassPreviewAudioBackend::logPreparedEventWindow(double startSecond) const
{
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    const int firstIndex = qBound(0, playbackSession_.eventGroupIndex, preparedGroups_.size());
    const int lastIndex = qMin(preparedGroups_.size(), firstIndex + 8);
    for (int index = firstIndex; index < lastIndex; ++index) {
        const CollapsedEventGroup& group = preparedGroups_[index];
        appendAudioDebugLog(
            QString("bass_prepare_event txn=%1 idx=%2 second=%3 lead_ms=%4 signature=%5")
                .arg(playbackTransactionId_)
                .arg(index)
                .arg(group.second, 0, 'f', 6)
                .arg((group.second - startSecond) * 1000.0, 0, 'f', 3)
                .arg(groupSignature(group)));
    }
}

double BassPreviewAudioBackend::preparePreviewPlaybackTransaction(
    double startSecond,
    bool resumeFromPause,
    double playbackRate)
{
    MC_OP("BassPreviewAudioBackend::preparePreviewPlaybackTransaction");
    QElapsedTimer timer;
    timer.start();
    noteInitWindowOpened(QStringLiteral("prepare_preview_playback"));
    if (!initializeAudioEngine()) {
        return startSecond;
    }
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
    setBackgroundTrackPlaybackRate(playbackRate);
    stopPlaybackSession();
    // G1 Commit 6: re-apply rate now that stopPlaybackSession has put every Sample
    // back into BASS_MIXER_CHAN_PAUSE state. setBackgroundTrackPlaybackRate above
    // may have skipped its internal setSpeed if the previous session was still
    // playing when this prepare came in (the new Sample::setSpeed guard short-
    // circuits BGM rate-attribute writes whenever the source is being actively
    // pulled — see PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.1 / §6.1).
    // The rate value is already stored in playbackSession_; this call just
    // pushes it onto the now-paused BGM source.
    setBackgroundTrackSampleSpeed(playbackSession_.backgroundTrackPlaybackRate);
    resetMasterMixerClock(startSecond);
    configureBackgroundTrackForSecond(
        startSecond,
        QStringLiteral("prepare_preview_playback"),
        miacode::preview_audio::bass::BassDebugRoute::Init);
    resetCursor(startSecond, !resumeFromPause);
    pauseTouchholdVoices();
    preparedPlayback_.pending = true;
    preparedPlayback_.resumeFromPause = resumeFromPause;
    preparedPlayback_.startSecond = clampTimelineSecond(startSecond);
    logPreparedEventWindow(preparedPlayback_.startSecond);
    appendAudioDebugLog(
        QString("bass_prepare txn=%1 start=%2 resume=%3 rate=%4 groups=%5")
            .arg(playbackTransactionId_)
            .arg(preparedPlayback_.startSecond, 0, 'f', 6)
            .arg(resumeFromPause ? 1 : 0)
            .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
            .arg(preparedGroups_.size()));
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::PreparePreviewPlayback,
        QString("elapsed_ms=%1 start=%2 resume=%3 rate=%4 groups=%5")
            .arg(timer.elapsed())
            .arg(preparedPlayback_.startSecond, 0, 'f', 6)
            .arg(resumeFromPause ? 1 : 0)
            .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
            .arg(preparedGroups_.size()),
        true);
    noteTransportReady(QStringLiteral("prepare_preview_playback"));
    return preparedPlayback_.startSecond;
}

void BassPreviewAudioBackend::commitPreparedPreviewPlayback()
{
    MC_OP("BassPreviewAudioBackend::commitPreparedPreviewPlayback");
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!preparedPlayback_.pending || masterMixer_ == 0) {
        return;
    }
    // G1 Commit 6: master mixer was started at engine init. The commit step is now
    // purely about unsetting BASS_MIXER_CHAN_PAUSE on the BGM sample below.
    playbackSession_.masterRunning = true;
    playbackSession_.lastAuthoritativeSecond = preparedPlayback_.startSecond;
    if (backgroundTrackSample_ != nullptr && !playbackSession_.backgroundTrackPendingStart) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackRunning = true;
        // G1 Commit 8: bass_sample_play per §7.2 — confirms the BGM flag flipped
        // after the rate attribute was set and records where in the
        // source we resumed reading.
        appendAudioDebugLog(
            QString("bass_sample_play kind=bgm rate_at_play=%1 offset_sec=%2 reason=commit")
                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
                .arg(backgroundTrackSample_->currentSec(), 0, 'f', 6));
    }
    if (!preparedPlayback_.resumeFromPause) {
        drainEvents(preparedPlayback_.startSecond);
    }
    restoreTouchholdVoices(preparedPlayback_.startSecond);
    anchorSfxScheduler(preparedPlayback_.startSecond);
    logTrackFileMissingAfterLoadIfNeeded();
    // G1 Commit 8: rename `bass_commit` → `bass_play` per §7.2 of
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md, and add the rate field so
    // ECHO validation can read out the speed each play session started with
    // without cross-referencing other lines.
    appendAudioDebugLog(
        QString("bass_play txn=%1 start=%2 rate=%3 resume=%4 bg_pending=%5")
            .arg(playbackTransactionId_)
            .arg(preparedPlayback_.startSecond, 0, 'f', 6)
            .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
            .arg(preparedPlayback_.resumeFromPause ? 1 : 0)
            .arg(playbackSession_.backgroundTrackPendingStart ? 1 : 0));
#endif
    preparedPlayback_ = PreparedPlaybackState();
}

void BassPreviewAudioBackend::cancelPreparedPreviewPlayback()
{
    MC_OP("BassPreviewAudioBackend::cancelPreparedPreviewPlayback");
    if (!preparedPlayback_.pending) {
        return;
    }
    stopPlaybackSession();
    preparedPlayback_ = PreparedPlaybackState();
}

double BassPreviewAudioBackend::preparedStartSecond() const
{
    return preparedPlayback_.pending ? preparedPlayback_.startSecond : playbackSession_.lastAuthoritativeSecond;
}

void BassPreviewAudioBackend::applyPausedPreviewState(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool noteMarkersChanged,
    double pauseSecond,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    MC_OP("BassPreviewAudioBackend::applyPausedPreviewState");
    preparedPlayback_ = PreparedPlaybackState();
    const bool transportInvalidated =
        noteMarkersChanged
        || preparedTimeline_.sourceNoteMarkers.size() != noteMarkers.size()
        || qAbs(preparedTimelinePlaybackRate_ - playbackRate) > kBassPreviewEpsilonSeconds
        || !previewTimingSettingsEqual(timingSettings_, timingSettings);
    if (transportInvalidated) {
        noteInitWindowOpened(QStringLiteral("apply_paused_state_invalidated"));
        rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
    }
    const double clampedPauseSecond = clampTimelineSecond(pauseSecond);
    // G1 fix (seek-into-片头 A/V desync): the reuse "same second" check must
    // compare against where the BGM ACTUALLY is, not retainedTransportSecond()
    // (= lastAuthoritativeSecond) — that field no longer tracks the live BGM
    // cursor (G1 Commit 6, see the KeepPaused note below). A drag into the
    // negative 片头 region moves it to match the target while the BGM stays parked
    // at its old position; trusting it then "reuses" the transport and resumes the
    // BGM from the wrong second. When a BGM is the live clock, use its real
    // chart-second so a mismatch falls through to repositionPausedTransportToSecond.
    bool backgroundTrackPendingStart = false;
    double backgroundTrackOffsetSeconds = 0.0;
    {
        QMutexLocker locker(&schedulerMutex_);
        backgroundTrackPendingStart = playbackSession_.backgroundTrackPendingStart;
        backgroundTrackOffsetSeconds = playbackSession_.backgroundTrackOffsetSeconds;
    }
    const double reuseCompareSecond =
        (hasBackgroundTrack() && backgroundTrackSample_ != nullptr
         && !backgroundTrackPendingStart)
            ? backgroundTrackSample_->currentSec() - backgroundTrackOffsetSeconds
            : retainedTransportSecond();
    if (miacode::preview_audio::bass::canReusePausedTransport(
            retainedPlaybackMode_,
            transportInvalidated,
            reuseCompareSecond,
            clampedPauseSecond)) {
        playbackSession_.lastAuthoritativeSecond = clampedPauseSecond;
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::RetainedReset,
            QString("action=reuse mode=%1 second=%2")
                .arg(retainedPlaybackModeLabel(retainedPlaybackMode_))
                .arg(clampedPauseSecond, 0, 'f', 6));
        noteTransportReady(QStringLiteral("apply_paused_state_reuse"));
        return;
    }
    if (!transportInvalidated
        && (retainedPlaybackMode_ == RetainedPlaybackMode::PausedExact
            || retainedPlaybackMode_ == RetainedPlaybackMode::PausedAnchored)) {
        repositionPausedTransportToSecond(clampedPauseSecond, QStringLiteral("apply_paused_state"));
        return;
    }
    anchorTransportToSecond(clampedPauseSecond, QStringLiteral("apply_paused_state"));
    noteTransportReady(QStringLiteral("apply_paused_state_anchor"));
}

double BassPreviewAudioBackend::startPreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate)
{
    MC_OP("BassPreviewAudioBackend::startPreviewPlaybackTransaction");
    const double preparedSecond = preparePreviewPlaybackTransaction(startSecond, resumeFromPause, playbackRate);
    commitPreparedPreviewPlayback();
    return preparedSecond;
}

miacode::preview_audio::PausePreviewResult BassPreviewAudioBackend::capturePausedPreviewTransaction()
{
    return pausePreviewPlaybackTransaction();
}

miacode::preview_audio::PausePreviewResult BassPreviewAudioBackend::pausePreviewPlaybackTransaction()
{
    MC_OP("BassPreviewAudioBackend::pausePreviewPlaybackTransaction");
    PausePreviewResult result;
    result.usedBackgroundTrack = hasBackgroundTrack();
    result.pauseSecond = authoritativeSecond();
    // G1 fix (A/V desync on resume): authoritativeSecond() is the last externally
    // recorded snapshot, which on the export-page audition pause path can be stale
    // — reset toward the session start (0) — while the BGM sample kept advancing to
    // the true playhead. That desynced resume (transport from 0, BGM from its real
    // position). When a BGM is the live clock, take ITS position (the ground truth
    // of where the audio actually is) as the pause second so transport and BGM
    // resume in lockstep. No-op in the editor preview, where the BGM already tracks
    // the playhead, so authoritativeSecond() and the BGM position agree.
    bool backgroundTrackPendingStart = false;
    double backgroundTrackOffsetSeconds = 0.0;
    {
        QMutexLocker locker(&schedulerMutex_);
        backgroundTrackPendingStart = playbackSession_.backgroundTrackPendingStart;
        backgroundTrackOffsetSeconds = playbackSession_.backgroundTrackOffsetSeconds;
    }
    if (playbackSession_.masterRunning
        && result.usedBackgroundTrack
        && backgroundTrackSample_ != nullptr
        && !backgroundTrackPendingStart) {
        const double bgmChartSecond =
            backgroundTrackSample_->currentSec() - backgroundTrackOffsetSeconds;
        if (qIsFinite(bgmChartSecond) && bgmChartSecond >= 0.0) {
            result.pauseSecond = bgmChartSecond;
        }
    }
    playbackSession_.lastAuthoritativeSecond = result.pauseSecond;
    if (playbackSession_.masterRunning) {
        suspendPlaybackTransport();
    }
    result.retainedMode = retainedPlaybackMode_;
    result.retainedBgmState = retainedBgmState_;
    return result;
}

double BassPreviewAudioBackend::resumeRetainedPreviewPlaybackTransaction()
{
    MC_OP("BassPreviewAudioBackend::resumeRetainedPreviewPlaybackTransaction");
    if (retainedPlaybackMode_ != RetainedPlaybackMode::PausedExact
        && retainedPlaybackMode_ != RetainedPlaybackMode::PausedAnchored) {
        return authoritativeSecond();
    }
    startTransportFromCurrentAnchor();
    return authoritativeSecond();
}

double BassPreviewAudioBackend::seekRetainedPreviewPlaybackTransaction(double targetSecond, bool continuePlaying)
{
    MC_OP("BassPreviewAudioBackend::seekRetainedPreviewPlaybackTransaction");
    const double clampedSecond = clampTimelineSecond(targetSecond);
    const bool sameSecond = retainedSecondMatches(clampedSecond);
    const auto action = miacode::preview_audio::bass::retainedSeekAction(
        retainedPlaybackMode_,
        sameSecond,
        continuePlaying);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::RetainedSeek,
        QString("mode=%1 action=%2 target=%3 continue=%4 same_second=%5")
            .arg(retainedPlaybackModeLabel(retainedPlaybackMode_))
            .arg(retainedSeekActionLabel(action))
            .arg(clampedSecond, 0, 'f', 6)
            .arg(continuePlaying ? 1 : 0)
            .arg(sameSecond ? 1 : 0));
    switch (action) {
    case miacode::preview_audio::bass::RetainedSeekAction::KeepPaused:
        // beta50 followup — same root cause as resetRetainedPreviewPlaybackTransaction's
        // matching block. Post-G1 lastAuthoritativeSecond doesn't track the
        // live BGM cursor / SFX event cursor / touchhold, so sameSecond=true
        // (which got us here) doesn't mean those three are aligned at
        // clampedSecond. Route through repositionPausedTransportToSecond so
        // BGM, SFX-event-index, and touchhold all re-anchor in one shot.
        repositionPausedTransportToSecond(clampedSecond, QStringLiteral("retained_seek_paused"));
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::ResumeExact:
    case miacode::preview_audio::bass::RetainedSeekAction::ResumeAnchored:
        startTransportFromCurrentAnchor();
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::RepositionPaused:
        repositionPausedTransportToSecond(clampedSecond, QStringLiteral("retained_seek_paused"));
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::RepositionAndResume:
        repositionPausedTransportToSecond(clampedSecond, QStringLiteral("retained_seek_resume"));
        startTransportFromCurrentAnchor();
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::AnchorPaused:
        noteInitWindowOpened(QStringLiteral("retained_seek_paused"));
        anchorTransportToSecond(clampedSecond, QStringLiteral("retained_seek_paused"));
        noteTransportReady(QStringLiteral("retained_seek_paused_anchor"));
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::AnchorAndResume:
    default:
        noteInitWindowOpened(QStringLiteral("retained_seek_resume"));
        anchorTransportToSecond(clampedSecond, QStringLiteral("retained_seek_resume"));
        startTransportFromCurrentAnchor();
        break;
    }
    return authoritativeSecond();
}

void BassPreviewAudioBackend::resetRetainedPreviewPlaybackTransaction(double targetSecond)
{
    MC_OP("BassPreviewAudioBackend::resetRetainedPreviewPlaybackTransaction");
    const double clampedSecond = clampTimelineSecond(targetSecond);
    const bool sameSecond = retainedSecondMatches(clampedSecond);
    const auto action = miacode::preview_audio::bass::retainedSeekAction(
        retainedPlaybackMode_,
        sameSecond,
        false);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::RetainedReset,
        QString("mode=%1 action=%2 target=%3 same_second=%4")
            .arg(retainedPlaybackModeLabel(retainedPlaybackMode_))
            .arg(retainedSeekActionLabel(action))
            .arg(clampedSecond, 0, 'f', 6)
            .arg(sameSecond ? 1 : 0));
    if (action == miacode::preview_audio::bass::RetainedSeekAction::KeepPaused
        || action == miacode::preview_audio::bass::RetainedSeekAction::RepositionPaused) {
        // beta50 followup — Stop-button audio sync fix:
        //
        // sameSecond is computed against lastAuthoritativeSecond, but G1
        // Commit 6 stopped that field from tracking the live BGM cursor
        // (and the live SFX event cursor) — it now only carries whatever
        // value was last *recorded* at the previous transport state
        // change. So "sameSecond=true" no longer implies "BGM cursor +
        // SFX event cursor + touchhold are already at clampedSecond".
        //
        // Concretely: Play 0 → ⏸10 → ▶ resume → ⏸30 → ⏹ Stop back to 10
        // landed here with sameSecond=true → KeepPaused → only
        // lastAuthoritativeSecond got updated, while BGM cursor stayed
        // near ~30 and the SFX event-group cursor still pointed past
        // 10's groups. Result: next ▶ played the audio from chart-30
        // while visuals were on chart-10, and SFX skipped every group
        // between 10 and 30.
        //
        // Fix: collapse KeepPaused into the same repositionPausedTransportToSecond
        // path RepositionPaused uses. That routine seeks all three cursors
        // (BGM source via configureBackgroundTrackForSecond, SFX event
        // index via resetCursor, touchhold via pauseTouchholdVoices) plus
        // resets session-state housekeeping. Cost is one BASS_ChannelSetPosition
        // + one event-cursor scan; both are cheap.
        repositionPausedTransportToSecond(clampedSecond, QStringLiteral("retained_reset"));
        return;
    }
    noteInitWindowOpened(QStringLiteral("retained_reset"));
    anchorTransportToSecond(clampedSecond, QStringLiteral("retained_reset"));
    noteTransportReady(QStringLiteral("retained_reset_anchor"));
}

void BassPreviewAudioBackend::clearRetainedPreviewPlaybackTransaction()
{
    MC_OP("BassPreviewAudioBackend::clearRetainedPreviewPlaybackTransaction");
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
}

BassPreviewAudioBackend::RetainedPlaybackMode BassPreviewAudioBackend::retainedPlaybackMode() const
{
    return retainedPlaybackMode_;
}

BassPreviewAudioBackend::RetainedBgmState BassPreviewAudioBackend::retainedBgmState() const
{
    return retainedBgmState_;
}

double BassPreviewAudioBackend::authoritativePlaybackSecond() const
{
    return authoritativeSecond();
}

double BassPreviewAudioBackend::syncPreviewPlaybackClockTransaction(double fallbackSecond)
{
    if (shuttingDown_.load(std::memory_order_acquire)) {
        return playbackSession_.lastAuthoritativeSecond;
    }
    logTrackFileMissingAfterLoadIfNeeded();
    const double second = authoritativeSecond();
    playbackSession_.lastAuthoritativeSecond = second;
    maybeStartPendingBackgroundTrack(second);
    drainEvents(second);
    logPlaybackStatus(second, fallbackSecond);
    return second;
}
