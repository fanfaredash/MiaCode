#include "LatencySandboxController.h"

#include "LatencyTestChartBuilder.h"

#include "mainwindow/MainWindow.h"
#include "mainwindow/MainWindowShared.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "preview/runtime/PreviewRuntime.h"
#include "timeline/TimelineSlowRefresh.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QFileInfo>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <limits>

namespace miacode::latency {

namespace {

constexpr int kTickIntervalMs = 33;  // ~30Hz playhead updates
constexpr double kFallbackAudioDurationSeconds = 180.0;

}  // namespace

LatencySandboxController::LatencySandboxController(MainWindow* owner, QObject* parent)
    : QObject(parent)
    , owner_(owner)
    , tickTimer_(new QTimer(this))
{
    tickTimer_->setInterval(kTickIntervalMs);
    tickTimer_->setTimerType(Qt::PreciseTimer);
    connect(tickTimer_, &QTimer::timeout, this, &LatencySandboxController::onTick);
}

LatencySandboxController::~LatencySandboxController()
{
    exitIfActive();
}

void LatencySandboxController::setOnPage(bool onPage)
{
    if (onPage_ == onPage) {
        return;
    }
    onPage_ = onPage;
    if (!onPage_) {
        exitIfActive();
    }
}

double LatencySandboxController::playheadSeconds() const
{
    if (!auditionRunning_ || owner_.isNull() || owner_->state_.previewSfxRuntime_ == nullptr) {
        return 0.0;
    }
    return qMax(0.0, owner_->state_.previewSfxRuntime_->authoritativePlaybackSecond());
}

void LatencySandboxController::setBpm(double bpm)
{
    if (!qIsFinite(bpm) || bpm <= 0.0) {
        return;
    }
    if (qFuzzyCompare(bpm_, bpm)) {
        return;
    }
    bpm_ = bpm;
    emit parametersChanged();
    regenerateAndPushIfRunning();
}

void LatencySandboxController::setOffsetSeconds(double seconds)
{
    if (!qIsFinite(seconds)) {
        seconds = 0.0;
    }
    if (qFuzzyCompare(offsetSeconds_ + 1.0, seconds + 1.0)) {
        return;
    }
    offsetSeconds_ = seconds;
    emit parametersChanged();
    regenerateAndPushIfRunning();
}

void LatencySandboxController::setSubdivision(int subdivision)
{
    if (subdivision != 4 && subdivision != 8) {
        subdivision = 4;
    }
    if (subdivision_ == subdivision) {
        return;
    }
    subdivision_ = subdivision;
    emit parametersChanged();
    regenerateAndPushIfRunning();
}

void LatencySandboxController::setSfxVolumePercent(int percent)
{
    percent = qBound(0, percent, 100);
    if (sfxVolumePercent_ == percent) {
        return;
    }
    sfxVolumePercent_ = percent;
    emit parametersChanged();
    if (auditionRunning_) {
        applyOverrideAudioSettings();
    }
}

bool LatencySandboxController::startAudition()
{
    if (auditionRunning_) {
        return true;
    }
    if (owner_.isNull() || owner_->state_.previewSfxRuntime_ == nullptr) {
        return false;
    }
    if (!(bpm_ > 0.0)) {
        return false;
    }

    // Cache the pre-sandbox audio + timeline state so we can roll back
    // cleanly when audition stops.
    cachedAudioSettings_ = owner_->state_.previewAudioSettings_;
    hasCachedAudioSettings_ = true;
    cachedNoteMarkers_ = owner_->state_.latestTimelineNoteMarkers_;
    cachedNoteMarkerSignature_ = owner_->state_.latestTimelineNoteMarkerSignature_;
    if (owner_->state_.timelineQuickStateBridge_ != nullptr) {
        cachedSnapshot_ = owner_->state_.timelineQuickStateBridge_->renderSnapshot();
    } else {
        cachedSnapshot_ = TimelineRenderSnapshot();
    }
    hasCachedTimeline_ = true;

    owner_->state_.latencySandboxAuditionActive_ = true;
    auditionRunning_ = true;
    lastSentPlayheadSeconds_ = -1.0;

    applyOverrideAudioSettings();
    pushSyntheticTimeline();

    // Start the playback transaction. The background track + SFX engine
    // were already wired up when the user opened the chart, so a plain
    // transaction start is enough — we don't need the full stage-media /
    // video plumbing that startQtPreviewPlayback() runs.
    QtPreviewSfxRuntime* runtime = owner_->state_.previewSfxRuntime_;
    runtime->startPreviewPlaybackTransaction(0.0, false, 1.0);
    tickTimer_->start();

    emit auditionStateChanged(true);
    return true;
}

void LatencySandboxController::stopAudition()
{
    if (!auditionRunning_) {
        return;
    }
    auditionRunning_ = false;
    tickTimer_->stop();

    if (!owner_.isNull() && owner_->state_.previewSfxRuntime_ != nullptr) {
        owner_->state_.previewSfxRuntime_->stopAll();
    }

    restoreOriginalTimeline();
    restoreOriginalAudioSettings();

    if (!owner_.isNull()) {
        owner_->state_.latencySandboxAuditionActive_ = false;
    }
    lastSentPlayheadSeconds_ = -1.0;
    emit auditionStateChanged(false);
    emit playheadAdvanced(0.0);
}

void LatencySandboxController::toggleAudition()
{
    if (auditionRunning_) {
        stopAudition();
    } else {
        startAudition();
    }
}

void LatencySandboxController::exitIfActive()
{
    if (auditionRunning_) {
        stopAudition();
    }
}

void LatencySandboxController::onTick()
{
    if (!auditionRunning_ || owner_.isNull() || owner_->state_.previewSfxRuntime_ == nullptr) {
        return;
    }
    const double second = qMax(0.0, owner_->state_.previewSfxRuntime_->authoritativePlaybackSecond());
    if (qAbs(second - lastSentPlayheadSeconds_) < 1e-4) {
        return;
    }
    lastSentPlayheadSeconds_ = second;
    if (owner_->state_.timelineQuickStateBridge_ != nullptr) {
        owner_->state_.timelineQuickStateBridge_->setPlayheadSeconds(second, false);
    }
    if (owner_->state_.previewCanvas_ != nullptr) {
        owner_->state_.previewCanvas_->setPlayheadSeconds(second, false);
    }
    emit playheadAdvanced(second);
}

void LatencySandboxController::regenerateAndPushIfRunning()
{
    if (!auditionRunning_) {
        return;
    }
    pushSyntheticTimeline();
    if (owner_.isNull() || owner_->state_.previewSfxRuntime_ == nullptr) {
        return;
    }
    // Re-arm SFX timeline so future taps line up with the new chart.
    owner_->state_.previewSfxRuntime_->configureTimeline(
        owner_->state_.latestTimelineNoteMarkers_,
        owner_->state_.previewPlaybackRate_ > 0.0 ? owner_->state_.previewPlaybackRate_ : 1.0,
        owner_->state_.previewTimingSettings_);
}

void LatencySandboxController::pushSyntheticTimeline()
{
    if (owner_.isNull()) {
        return;
    }
    const double duration = resolveAudioDurationSeconds();
    const QString chartText = buildTestChartText(bpm_, subdivision_, duration);
    SimaiNativeParseResult parseResult = SimaiNativeParser::parseForTimeline(chartText);
    const TimelinePreviewRefreshState previewState =
        buildTimelinePreviewRefreshState(parseResult, offsetSeconds_);

    owner_->state_.latestTimelineNoteMarkers_ = previewState.shiftedNoteMarkers;
    owner_->state_.latestTimelineNoteMarkerSignature_ = previewState.noteMarkerSignature;

    if (owner_->state_.previewCanvas_ != nullptr) {
        owner_->state_.previewCanvas_->setNoteMarkers(previewState.shiftedNoteMarkers);
    }

    TimelineRenderSnapshot snapshot = buildSyntheticSnapshot(duration);
    if (owner_->state_.timelineQuickStateBridge_ != nullptr) {
        owner_->state_.timelineQuickStateBridge_->setTimelineData(snapshot);
    }

    if (owner_->state_.previewSfxRuntime_ != nullptr) {
        owner_->state_.previewSfxRuntime_->configureTimeline(
            previewState.shiftedNoteMarkers,
            owner_->state_.previewPlaybackRate_ > 0.0 ? owner_->state_.previewPlaybackRate_ : 1.0,
            owner_->state_.previewTimingSettings_);
    }
}

void LatencySandboxController::restoreOriginalTimeline()
{
    if (owner_.isNull() || !hasCachedTimeline_) {
        return;
    }
    owner_->state_.latestTimelineNoteMarkers_ = cachedNoteMarkers_;
    owner_->state_.latestTimelineNoteMarkerSignature_ = cachedNoteMarkerSignature_;
    if (owner_->state_.previewCanvas_ != nullptr) {
        owner_->state_.previewCanvas_->setNoteMarkers(cachedNoteMarkers_);
    }
    if (owner_->state_.timelineQuickStateBridge_ != nullptr) {
        owner_->state_.timelineQuickStateBridge_->setTimelineData(cachedSnapshot_);
    }
    if (owner_->state_.previewSfxRuntime_ != nullptr) {
        owner_->state_.previewSfxRuntime_->configureTimeline(
            cachedNoteMarkers_,
            owner_->state_.previewPlaybackRate_ > 0.0 ? owner_->state_.previewPlaybackRate_ : 1.0,
            owner_->state_.previewTimingSettings_);
    }
    cachedNoteMarkers_.clear();
    cachedNoteMarkerSignature_.clear();
    cachedSnapshot_ = TimelineRenderSnapshot();
    hasCachedTimeline_ = false;
}

void LatencySandboxController::applyOverrideAudioSettings()
{
    if (owner_.isNull() || owner_->state_.previewSfxRuntime_ == nullptr) {
        return;
    }
    const PreviewAudioSettings base = hasCachedAudioSettings_
        ? cachedAudioSettings_
        : owner_->state_.previewAudioSettings_;
    const PreviewAudioSettings override = buildOverrideAudioSettings(base, sfxVolumePercent_);
    owner_->state_.previewSfxRuntime_->applyLevels(override);
}

void LatencySandboxController::restoreOriginalAudioSettings()
{
    if (owner_.isNull() || owner_->state_.previewSfxRuntime_ == nullptr || !hasCachedAudioSettings_) {
        hasCachedAudioSettings_ = false;
        return;
    }
    owner_->state_.previewSfxRuntime_->applyLevels(cachedAudioSettings_);
    hasCachedAudioSettings_ = false;
}

PreviewAudioSettings LatencySandboxController::buildOverrideAudioSettings(
    const PreviewAudioSettings& base, int sfxPercent) const
{
    PreviewAudioSettings override = base;
    // Keep the song audio at the user's normal effective volume by
    // collapsing the chained (global * track) multiplier into a single
    // trackVolume term and pinning globalVolume to 1.0. SFX kinds then
    // get the sandbox slider's value directly (independent of the
    // user's normal mix), so the test taps are easy to hear regardless
    // of how quietly the user runs the song.
    const double cachedEffectiveTrackVolume = previewTrackVolume(base);
    override.globalVolume = 1.0;
    override.globalRestoreVolume = 1.0;
    override.trackVolume = PreviewAudioSettings::clamp(cachedEffectiveTrackVolume);
    override.trackRestoreVolume = override.trackVolume;

    const double sfxLevel = qBound(0.0, static_cast<double>(sfxPercent) / 100.0, 1.0);
    override.tapVolume = sfxLevel;
    override.tapRestoreVolume = sfxLevel;
    override.exVolume = sfxLevel;
    override.exRestoreVolume = sfxLevel;
    override.breakVolume = sfxLevel;
    override.breakRestoreVolume = sfxLevel;
    override.breakSlideVolume = sfxLevel;
    override.breakSlideRestoreVolume = sfxLevel;
    override.slideVolume = sfxLevel;
    override.slideRestoreVolume = sfxLevel;
    override.touchVolume = sfxLevel;
    override.touchRestoreVolume = sfxLevel;
    override.fireworkVolume = sfxLevel;
    override.fireworkRestoreVolume = sfxLevel;
    override.answerVolume = sfxLevel;
    override.answerRestoreVolume = sfxLevel;
    override.normalize();
    return override;
}

TimelineRenderSnapshot LatencySandboxController::buildSyntheticSnapshot(double durationSeconds) const
{
    TimelineRenderSnapshot snapshot;
    snapshot.durationSeconds = qMax(0.0, durationSeconds);
    snapshot.minimumSecond = 0.0;
    snapshot.maximumSecond = snapshot.durationSeconds;

    TimelineRenderLine line;
    line.lineId = 1;
    line.lineNumber = 1;
    line.startPosition = 0;
    line.startSecond = 0.0;
    line.endSecond = snapshot.durationSeconds;

    if (bpm_ > 0.0) {
        const double beatPeriod = 60.0 / bpm_;
        const int safeBarPulseCount = 4;
        if (beatPeriod > 0.0) {
            const double renderMinSecond = -0.5;
            qint64 beatIndex = static_cast<qint64>(qFloor((renderMinSecond - offsetSeconds_) / beatPeriod));
            while (offsetSeconds_ + static_cast<double>(beatIndex) * beatPeriod < renderMinSecond - 1e-6) {
                ++beatIndex;
            }
            int sourceCol = 1;
            for (;; ++beatIndex) {
                const double beatSecond = offsetSeconds_ + static_cast<double>(beatIndex) * beatPeriod;
                if (beatSecond > snapshot.durationSeconds + 1e-6) {
                    break;
                }
                const bool isBarLine = beatIndex >= 0
                    ? ((beatIndex % safeBarPulseCount) == 0)
                    : (((-beatIndex) % safeBarPulseCount) == 0);
                if (isBarLine) {
                    if (snapshot.measureLineSeconds.isEmpty()
                        || qAbs(snapshot.measureLineSeconds.constLast() - beatSecond) > 1e-6) {
                        snapshot.measureLineSeconds.append(beatSecond);
                    }
                } else {
                    TimelineRenderBeat beat;
                    beat.secondOffset = beatSecond;
                    beat.sourceCol = sourceCol;
                    beat.subdivisionBeats = safeBarPulseCount;
                    beat.subdivisionIndex = safeBarPulseCount > 0
                        ? static_cast<int>((beatIndex % safeBarPulseCount + safeBarPulseCount) % safeBarPulseCount)
                        : 0;
                    line.beats.append(beat);
                }
                ++sourceCol;
            }
        }
    }

    snapshot.lines.append(line);
    snapshot.noteVisualEndPrefixMaxWithSlideTracks.append(-std::numeric_limits<double>::infinity());
    snapshot.noteVisualEndPrefixMaxWithoutSlideTracks.append(-std::numeric_limits<double>::infinity());
    return snapshot;
}

double LatencySandboxController::resolveAudioDurationSeconds() const
{
    if (owner_.isNull()) {
        return kFallbackAudioDurationSeconds;
    }
    const double trackDuration = owner_->state_.previewTrackDurationSeconds_;
    if (trackDuration > 1.0) {
        return trackDuration;
    }
    return kFallbackAudioDurationSeconds;
}

}  // namespace miacode::latency
