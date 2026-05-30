#include "LatencySandboxController.h"

#include "LatencyTestChartBuilder.h"

#include "mainwindow/MainWindow.h"
#include "mainwindow/MainWindowShared.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "preview/runtime/PreviewRuntime.h"
#include "timeline/TimelineQuickModel.h"
#include "timeline/TimelineSlowRefresh.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QTimer>
#include <QtMath>

namespace miacode::latency {

namespace {

constexpr int kPollIntervalMs = 33;  // ~30Hz UI poll for the page's own widgets
constexpr double kFallbackAudioDurationSeconds = 180.0;

}  // namespace

LatencySandboxController::LatencySandboxController(MainWindow* owner, QObject* parent)
    : QObject(parent)
    , owner_(owner)
    , tickTimer_(new QTimer(this))
{
    tickTimer_->setInterval(kPollIntervalMs);
    connect(tickTimer_, &QTimer::timeout, this, &LatencySandboxController::onTick);
}

LatencySandboxController::~LatencySandboxController()
{
    if (onPage_) {
        setOnPage(false);
    }
}

void LatencySandboxController::setOnPage(bool onPage)
{
    if (onPage_ == onPage) {
        return;
    }
    onPage_ = onPage;
    if (onPage_) {
        installSandboxScene();
    } else {
        teardownSandboxScene();
    }
}

double LatencySandboxController::playheadSeconds() const
{
    if (owner_.isNull()) {
        return 0.0;
    }
    return qMax(0.0, owner_->state_.qtPreviewPauseSecond_);
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
    regenerateAndPushIfActive();
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
    regenerateAndPushIfActive();
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
    regenerateAndPushIfActive();
}

void LatencySandboxController::setSfxVolumePercent(int percent)
{
    percent = qBound(0, percent, 100);
    if (sfxVolumePercent_ == percent) {
        return;
    }
    sfxVolumePercent_ = percent;
    emit parametersChanged();
    if (onPage_) {
        applyOverrideAudioSettings();
    }
}

void LatencySandboxController::installSandboxScene()
{
    if (owner_.isNull()) {
        return;
    }
    // Make sure the SFX engine + assets are loaded (the page may be opened
    // before any difficulty was previewed). Mirrors startQtPreviewPlayback().
    owner_->ensurePreviewSfxRuntimePrepared();

    // Cache the pre-sandbox audio + timeline state so we can roll back cleanly
    // when the user leaves the page.
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

    // Drop any retained pause transaction a previously-paused difficulty left
    // behind, so the first audition Play starts cleanly from 0 instead of
    // resuming that difficulty's transaction.
    if (owner_->state_.previewSfxRuntime_ != nullptr) {
        owner_->state_.previewSfxRuntime_->stopAll();
        owner_->state_.previewSfxRuntime_->clearRetainedPreviewPlaybackTransaction();
    }

    auditionRunning_ = false;
    lastPolledSecond_ = -1.0;
    // Tell the playback gates the test chart is a previewable chart, so the
    // real transport (startQtPreviewPlayback / onTogglePreviewPause) plays it.
    owner_->state_.latencySandboxAuditionActive_ = true;
    owner_->state_.qtPreviewPauseSecond_ = 0.0;

    // Install the test chart as the preview source + apply the per-page SFX mix.
    applyOverrideAudioSettings();
    setupSandboxPreviewState();
    applyPlayheadToScene(0.0);

    if (tickTimer_ != nullptr) {
        tickTimer_->start();
    }
    emit auditionStateChanged(false);
    emit playheadAdvanced(0.0);
}

void LatencySandboxController::teardownSandboxScene()
{
    if (tickTimer_ != nullptr) {
        tickTimer_->stop();
    }
    if (!owner_.isNull()) {
        owner_->stopQtPreviewPlayback(true);  // stop the real transport if it's running
        owner_->state_.latencySandboxAuditionActive_ = false;
        // Don't let a later difficulty reuse the sandbox's "ready" snapshot.
        owner_->state_.latestTimelinePreviewSnapshotReady_ = false;
    }
    auditionRunning_ = false;
    lastPolledSecond_ = -1.0;

    restoreOriginalTimeline();
    restoreOriginalAudioSettings();

    emit auditionStateChanged(false);
    emit playheadAdvanced(0.0);
}

void LatencySandboxController::toggleAudition()
{
    // Reuse the real Play/Pause transport so the audition is identical to a
    // difficulty page (preview render, bottom timeline, slider, SFX, song).
    if (!owner_.isNull()) {
        owner_->onTogglePreviewPause();
    }
}

void LatencySandboxController::exitIfActive()
{
    // Only stop in-progress audition playback; never touch normal-difficulty
    // playback (onPage_ is true only while the latency page is selected).
    if (onPage_ && !owner_.isNull()) {
        owner_->stopQtPreviewPlayback(true);
    }
}

void LatencySandboxController::onTick()
{
    // Lightweight poll: mirror the real transport's state onto the page's own
    // widgets (audition button + position label). The preview/timeline/slider
    // are driven by the real transport itself.
    if (owner_.isNull()) {
        return;
    }
    const bool playing = owner_->state_.qtPreviewPlaying_;
    const double second = qMax(0.0, owner_->state_.qtPreviewPauseSecond_);
    if (playing != auditionRunning_) {
        auditionRunning_ = playing;
        emit auditionStateChanged(playing);
    }
    if (lastPolledSecond_ < 0.0 || qAbs(second - lastPolledSecond_) > 1e-3) {
        lastPolledSecond_ = second;
        emit playheadAdvanced(second);
    }
}

void LatencySandboxController::regenerateAndPushIfActive()
{
    if (!onPage_ || owner_.isNull()) {
        return;
    }
    if (owner_->state_.qtPreviewPlaying_) {
        // Don't hot-swap the chart mid-playback (matches difficulty
        // edit-while-play deferral); new params take effect on the next play.
        return;
    }
    setupSandboxPreviewState();
    applyPlayheadToScene(qMax(0.0, owner_->state_.qtPreviewPauseSecond_));
}

void LatencySandboxController::applyPlayheadToScene(double seconds)
{
    if (owner_.isNull()) {
        return;
    }
    const double clamped = qMax(0.0, seconds);
    owner_->state_.qtPreviewPauseSecond_ = clamped;
    if (owner_->state_.timelineQuickStateBridge_ != nullptr) {
        owner_->state_.timelineQuickStateBridge_->setPlayheadSeconds(clamped, false);
    }
    if (owner_->state_.previewCanvas_ != nullptr) {
        owner_->state_.previewCanvas_->setPlayheadSeconds(clamped, true);
    }
    owner_->updatePreviewSliderPosition(clamped);
}

void LatencySandboxController::setupSandboxPreviewState()
{
    if (owner_.isNull()) {
        return;
    }
    const double duration = resolveAudioDurationSeconds();
    const QString chartText = buildTestChartText(bpm_, subdivision_, duration);
    const SimaiNativeParseResult parseResult = SimaiNativeParser::parseForTimeline(chartText);
    const TimelinePreviewRefreshState previewState =
        buildTimelinePreviewRefreshState(parseResult, offsetSeconds_);

    // Preview note markers + "snapshot ready" flags — the same state a
    // slow-refresh publishes for a real difficulty, so preparePreviewStartState
    // accepts it and the real transport can play the test chart.
    owner_->state_.latestTimelineNoteMarkers_ = previewState.shiftedNoteMarkers;
    owner_->state_.latestTimelineNoteMarkerSignature_ = previewState.noteMarkerSignature;
    owner_->state_.latestTimelinePreviewRevision_ = owner_->state_.timelineRevision_;
    owner_->state_.latestTimelinePreviewSnapshotReady_ = true;
    if (owner_->state_.previewCanvas_ != nullptr) {
        owner_->state_.previewCanvas_->setNoteMarkers(previewState.shiftedNoteMarkers);
    }

    // Bottom timeline — reuse the real timeline model so the test chart's notes
    // (and beat/measure grid) render exactly like a difficulty's timeline.
    owner_->state_.timelineQuickModel_.rebuildFromText(chartText, offsetSeconds_);
    if (owner_->state_.timelineQuickStateBridge_ != nullptr) {
        owner_->state_.timelineQuickStateBridge_->setTimelineData(
            owner_->state_.timelineQuickModel_.snapshot());
    }
    owner_->updatePreviewSliderRange();

    // SFX timeline for the test taps.
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
    owner_->updatePreviewSliderRange();
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
    // Keep the song audio at the user's normal effective volume by collapsing
    // the chained (global * track) multiplier into a single trackVolume term
    // and pinning globalVolume to 1.0. SFX kinds then get the sandbox slider's
    // value directly (independent of the user's normal mix), so the test taps
    // are easy to hear regardless of how quietly the user runs the song.
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
