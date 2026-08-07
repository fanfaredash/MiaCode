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

#ifdef MIACODE_HAS_BASS_AUDIO
#include "bass.h"
#include "bassmix.h"
#endif

#include "BassPreviewAudioBackendImpl.h"
#include "BassPreviewAudioBackendSample.h"

using namespace miacode::audio::bass_detail;

void BassPreviewAudioBackend::refreshPreparedAssets()
{
    preparedAssets_.trackPath = resolveTrackPath(preparedAssets_.chartPath);
    preparedAssets_.sfxDir = resolveSfxDir();
}

void BassPreviewAudioBackend::resetAssets()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    disarmSfxScheduler("reset_assets");
    int releasedSampleCount = 0;
    samplesByKind_.clear();
    backgroundTrackSample_ = nullptr;
    // Keep the sampler's handle in step; it never dereferences the object itself.
    publishAudioHealthHandles();
    touchholdSample_ = nullptr;
    retainedBgmState_ = RetainedBgmState::NoneLoaded;
    trackMissingAfterLoadLogged_ = false;

    auto resetSample = [&releasedSampleCount](std::unique_ptr<Sample>& sample) {
        if (sample) {
            sample->free();
            sample.reset();
            ++releasedSampleCount;
        }
    };

    resetSample(answerSample_);
    resetSample(judgeSample_);
    resetSample(judgeBreakSample_);
    resetSample(slideSample_);
    resetSample(breakSample_);
    resetSample(breakSlideStartSample_);
    resetSample(breakSlideFinishSample_);
    resetSample(breakSlideTailBreakSample_);
    resetSample(judgeBreakSlideSample_);
    resetSample(exSample_);
    resetSample(touchSample_);
    resetSample(touchholdSampleOwner_);
    resetSample(fireworkSample_);
    resetSample(clockSample_);
    resetSample(trackStartSample_);
    resetSample(backgroundTrackSampleOwner_);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::ResetAssets,
        QString("released=%1").arg(releasedSampleCount),
        true);
#endif
}

void BassPreviewAudioBackend::initializeAssets()
{
    MC_OP("BassPreviewAudioBackend::initializeAssets");
#ifdef MIACODE_HAS_BASS_AUDIO
    QElapsedTimer timer;
    timer.start();
    resetAssets();
    if (!engineInitialized_ || masterMixer_ == 0) {
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAssets,
            QString("elapsed_ms=%1 loaded=0 skipped=1").arg(timer.elapsed()),
            true);
        return;
    }

    int loadedSampleCount = 0;
    const auto loadSample = [this](std::unique_ptr<Sample>& slot, const QString& kind, bool speedChange, bool requiredForMap) {
        const QString path = miacode::preview_sfx::assetFilePathForKind(preparedAssets_.sfxDir, kind);
        if (!QFileInfo::exists(path)) {
            return;
        }
        slot = std::make_unique<Sample>();
        if (!slot->create(this, path, kind, kind, false, speedChange ? SampleSpeedMode::Tempo : SampleSpeedMode::None)) {
            slot.reset();
            return;
        }
        if (requiredForMap) {
            samplesByKind_.insert(kind, slot.get());
        }
    };

    loadSample(answerSample_, QStringLiteral("answer"), false, true);
    loadSample(judgeSample_, QStringLiteral("judge"), false, true);
    loadSample(judgeBreakSample_, QStringLiteral("judge_break"), false, true);
    loadSample(slideSample_, QStringLiteral("slide"), false, true);
    loadSample(breakSample_, QStringLiteral("break"), false, true);
    loadSample(breakSlideStartSample_, QStringLiteral("break_slide_start"), false, true);
    loadSample(breakSlideFinishSample_, QStringLiteral("break_slide_finish"), false, true);
    loadSample(breakSlideTailBreakSample_, QStringLiteral("break_slide_tail_break"), false, true);
    loadSample(judgeBreakSlideSample_, QStringLiteral("judge_break_slide"), false, true);
    loadSample(exSample_, QStringLiteral("ex"), false, true);
    loadSample(touchSample_, QStringLiteral("touch"), false, true);
    loadSample(touchholdSampleOwner_, QStringLiteral("touchhold"), false, false);
    loadSample(fireworkSample_, QStringLiteral("firework"), false, true);
    loadSample(clockSample_, QStringLiteral("clock"), false, true);
    loadSample(trackStartSample_, QStringLiteral("track_start"), false, true);

    samplesByKind_.insert(QStringLiteral("break_touch"), judgeBreakSample_.get());
    samplesByKind_.insert(QStringLiteral("break_slide"), breakSlideStartSample_.get());

    if (!preparedAssets_.trackPath.isEmpty() && QFileInfo::exists(preparedAssets_.trackPath)) {
        backgroundTrackSampleOwner_ = std::make_unique<Sample>();
        const SampleSpeedMode bgmSpeedMode = backgroundTrackSpeedMode();
        if (backgroundTrackSampleOwner_->create(
                this,
                preparedAssets_.trackPath,
                QStringLiteral("bgm"),
                QStringLiteral("bgm"),
                true,
                bgmSpeedMode)) {
            backgroundTrackSample_ = backgroundTrackSampleOwner_.get();
            // Keep the sampler's handle in step; it never dereferences the object itself.
            publishAudioHealthHandles();
            backgroundTrackSample_->setLoop(false);
            backgroundTrackSample_->setSpeed(playbackSession_.backgroundTrackPlaybackRate);
            ++loadedSampleCount;
            appendAudioDebugLog(
                QString("bgm_speed_mode selected=%1 rate=%2 env=%3 tempo_preset=%4 tempo_params=%5")
                    .arg(sampleSpeedModeLabel(bgmSpeedMode))
                    .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
                    .arg(qEnvironmentVariable("MIACODE_BASS_BGM_RATE_MODE"))
                    .arg(qEnvironmentVariable("MIACODE_BASS_BGM_TEMPO_PRESET"))
                    .arg(qEnvironmentVariable("MIACODE_BASS_BGM_TEMPO_PARAMS")));
        } else {
            backgroundTrackSampleOwner_.reset();
            backgroundTrackSample_ = nullptr;
            // Keep the sampler's handle in step; it never dereferences the object itself.
            publishAudioHealthHandles();
        }
    }

    touchholdSample_ = touchholdSampleOwner_.get();
    const Sample* countedSamples[] = {
        answerSample_.get(),
        judgeSample_.get(),
        judgeBreakSample_.get(),
        slideSample_.get(),
        breakSample_.get(),
        breakSlideStartSample_.get(),
        breakSlideFinishSample_.get(),
        breakSlideTailBreakSample_.get(),
        judgeBreakSlideSample_.get(),
        exSample_.get(),
        touchSample_.get(),
        touchholdSampleOwner_.get(),
        fireworkSample_.get()
    };
    for (const Sample* sample : countedSamples) {
        if (sample != nullptr) {
            ++loadedSampleCount;
        }
    }
    applySampleLevels();
    updateRetainedBgmState();
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::InitializeAssets,
        QString("elapsed_ms=%1 loaded=%2 has_bgm=%3")
            .arg(timer.elapsed())
            .arg(loadedSampleCount)
            .arg(backgroundTrackSample_ != nullptr ? 1 : 0),
        true);
#endif
}

void BassPreviewAudioBackend::applySampleLevels()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    const auto apply = [](Sample* sample, double volume) {
        if (sample != nullptr) {
            sample->baseVolume = clampSampleVolume(volume);
            sample->applyVolume();
        }
    };

    apply(answerSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("answer")));
    apply(judgeSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("judge")));
    apply(judgeBreakSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("judge_break")));
    apply(slideSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("slide")));
    apply(breakSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("break")));
    apply(breakSlideStartSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("break_slide_start")));
    apply(breakSlideFinishSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("break_slide_finish")));
    apply(breakSlideTailBreakSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("break_slide_tail_break")));
    apply(judgeBreakSlideSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("judge_break_slide")));
    apply(exSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("ex")));
    apply(touchSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("touch")));
    apply(touchholdSample_, previewSfxVolumeForKind(settings_, QStringLiteral("touchhold")));
    apply(fireworkSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("firework")));
    apply(clockSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("clock")));
    apply(trackStartSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("track_start")));
    apply(backgroundTrackSample_, previewTrackVolume(settings_));
#endif
}

BassPreviewAudioBackend::Sample* BassPreviewAudioBackend::sampleForKind(const QString& kind) const
{
    return samplesByKind_.value(previewSfxNormalizedKind(kind), nullptr);
}


void BassPreviewAudioBackend::reloadAssets(const PreviewAudioSettings& settings)
{
    MC_OP("BassPreviewAudioBackend::reloadAssets");
    settings_ = settings;
    settings_.normalize();
    invalidateRetainedPlaybackState(QStringLiteral("reload_assets"));
    trackMissingAfterLoadLogged_ = false;
    refreshPreparedAssets();
    if (!initializeAudioEngine()) {
        _mc_op_.fail(QStringLiteral("initializeAudioEngine failed"));
        updateRetainedBgmState();
        return;
    }
    initializeAssets();
    updateRetainedBgmState();
}


void BassPreviewAudioBackend::setChartPath(const QString& chartPath)
{
    MC_OP("BassPreviewAudioBackend::setChartPath");
    const QString normalized = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    // Content-aware skip: the same chart path can point at a track whose bytes
    // were rewritten in place (the in-app audio tools rewrite track.mp3 at the
    // same path, and users swap same-named files in Explorer). A path-only
    // equality check would wrongly skip the reload and keep playing the old
    // audio, recoverable only by switching files. Stamp the resolved track by
    // (size, mtime) and skip ONLY when both the path and the stamp are unchanged.
    const QString trackStamp = miacode::fs::fileContentStamp(resolveTrackPath(normalized));
    if (preparedAssets_.chartPath == normalized && preparedAssets_.trackStamp == trackStamp) {
        return;
    }
    invalidateRetainedPlaybackState(QStringLiteral("chart_path_changed"));
    trackMissingAfterLoadLogged_ = false;
    preparedAssets_.chartPath = normalized;
    preparedAssets_.trackStamp = trackStamp;
    refreshPreparedAssets();
    if (engineInitialized_) {
        initializeAssets();
    }
    updateRetainedBgmState();
}


void BassPreviewAudioBackend::applyLevels(const PreviewAudioSettings& settings)
{
    MC_OP("BassPreviewAudioBackend::applyLevels");
    const bool rebuildMineSfx = settings_.mineSfxEnabled != settings.mineSfxEnabled;
    const bool rearmScheduler = playbackSession_.masterRunning;
    const double liveChartSecond = currentSfxSchedulerChartSecond(
        playbackSession_.lastAuthoritativeSecond);
    // The mixer callback can read a group concurrently with the GUI applying a
    // setting. Remove its one outstanding sync before reading or rebuilding the
    // event vector, then resume from the same fired/unfired boundary.
    if (rearmScheduler) {
        disarmSfxScheduler("apply_levels");
    }
    settings_ = settings;
    settings_.normalize();
    if (rebuildMineSfx && !preparedTimeline_.sourceNoteMarkers.isEmpty()) {
        rebuildPreparedTimeline(
            preparedTimeline_.sourceNoteMarkers,
            preparedTimelinePlaybackRate_,
            timingSettings_);
        pauseTouchholdVoices();
    }
    applySampleLevels();
    // rebuildPreparedGroups() re-collapses the event groups (breakSlideTailCheer
    // muting changes grouping), which can shift indices, so the cursor must be
    // re-anchored afterward. The master decode position supplies the live chart
    // second; `lastAuthoritativeSecond` is only a transport snapshot and may be
    // far behind a running session. Using it here would replay old groups or
    // schedule the next group tens of seconds late after a setting change.
    rebuildPreparedGroups();
    resetCursor(liveChartSecond, false);
    if (rebuildMineSfx && playbackSession_.masterRunning) {
        restoreTouchholdVoices(authoritativeSecond());
    }
    if (rearmScheduler && playbackSession_.masterRunning) {
        anchorSfxScheduler(liveChartSecond);
    }
}

void BassPreviewAudioBackend::clearPreparedTimeline()
{
    preparedTimeline_ = TimelineProgramState();
    preparedGroups_.clear();
}

void BassPreviewAudioBackend::rebuildPreparedGroups()
{
    preparedGroups_.clear();
    int index = 0;
    while (index < preparedTimeline_.events.size()) {
        const int groupEnd = miacode::preview_sfx_timeline::eventGroupEndIndex(preparedTimeline_.events, index);
        preparedGroups_.append(
            miacode::preview_sfx_timeline::collapseEventGroup(
                preparedTimeline_.events,
                index,
                groupEnd,
                settings_.breakSlideTailCheerMuted));
        index = groupEnd;
    }
}

void BassPreviewAudioBackend::rebuildPreparedTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    QElapsedTimer timer;
    timer.start();
    noteInitWindowOpened(QStringLiteral("rebuild_timeline"));
    timingSettings_ = timingSettings;
    timingSettings_.normalize();
    preparedTimeline_.sourceNoteMarkers = noteMarkers;
    preparedTimelinePlaybackRate_ = playbackRate;
    miacode::preview_sfx_timeline::buildTimeline(
        noteMarkers,
        playbackRate,
        timingSettings_,
        &preparedTimeline_.events,
        &preparedTimeline_.touchholdSpans,
        settings_.mineSfxEnabled);
    rebuildPreparedGroups();
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::RebuildTimeline,
        QString("elapsed_ms=%1 markers=%2 events=%3 groups=%4 touchhold_spans=%5 rate=%6")
            .arg(timer.elapsed())
            .arg(noteMarkers.size())
            .arg(preparedTimeline_.events.size())
            .arg(preparedGroups_.size())
            .arg(preparedTimeline_.touchholdSpans.size())
            .arg(playbackRate, 0, 'f', 3),
        true);
    // G1 Commit 8: §7.2 / §7.4 alias for the rebuild_timeline row, with just the
    // two fields a human reading the validation log cares about. The verbose
    // `bass_init op=rebuild_timeline ...` line above keeps its existing format
    // so log scrapers can still index it; this `bass_timeline_built` row is the
    // one ECHO step 1 reads.
    appendAudioDebugLog(
        QString("bass_timeline_built groups=%1 rate=%2")
            .arg(preparedGroups_.size())
            .arg(playbackRate, 0, 'f', 3));
}

void BassPreviewAudioBackend::configureTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    MC_OP("BassPreviewAudioBackend::configureTimeline");
    const bool rearmScheduler = playbackSession_.masterRunning;
    const double liveChartSecond = currentSfxSchedulerChartSecond(
        playbackSession_.lastAuthoritativeSecond);
    if (rearmScheduler) {
        disarmSfxScheduler("configure_timeline");
    }
    rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
    if (rearmScheduler && playbackSession_.masterRunning) {
        resetCursor(liveChartSecond, false);
        anchorSfxScheduler(liveChartSecond);
    }
}

void BassPreviewAudioBackend::clearTimeline()
{
    MC_OP("BassPreviewAudioBackend::clearTimeline");
    disarmSfxScheduler("clear_timeline");
    clearPreparedTimeline();
    stopAll();
}
