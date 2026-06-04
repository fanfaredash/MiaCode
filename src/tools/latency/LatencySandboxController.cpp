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
    if (onPage_ && !owner_.isNull()) {
        // Re-dispatch the audition mix with the new slider value (mode is
        // LatencyAudition while on the page). The slider value never touches the
        // user's normal mix — it is a separate input to the pure level function.
        owner_->applyPreviewAudioSettingsToRuntime();
    }
}

// NOTE (FAILED ATTEMPT, reverted): playSfxAuditionSample() used to fire a single
// "tap" SFX while the SFX-volume slider was dragged, so the user could hear the
// level. It could not be kept silent during playback - the tap still sounded
// while the chart was playing despite guarding on qtPreviewPlaying_. Primary
// suspicion: a stray play/audition event is scheduled elsewhere. The method (and
// its page-side caller) were removed; the slider now only sets the volume.

void LatencySandboxController::installSandboxScene()
{
    if (owner_.isNull()) {
        return;
    }
    // Make sure the SFX engine + assets are loaded (the page may be opened
    // before any difficulty was previewed). Mirrors startQtPreviewPlayback().
    owner_->ensurePreviewSfxRuntimePrepared();

    // Cache the pre-sandbox timeline state so we can roll back cleanly when the
    // user leaves the page. Audio levels are NOT snapshotted: they are re-derived
    // from the current mode by MainWindow::applyPreviewAudioSettingsToRuntime, so
    // entering the page dispatches the audition mix and leaving it re-dispatches
    // the user's normal mix (see the dispatch calls in install/teardown below).
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
    // Preserve the playhead carried over from the previous page (set by
    // switchToLatencyField) instead of snapping to 0, clamped to the test
    // chart's duration. Mirrors difficulty-switch behaviour.
    const double sandboxDuration = resolveAudioDurationSeconds();
    const double restoreSecond = sandboxDuration > 0.0
        ? qBound(0.0, owner_->state_.qtPreviewPauseSecond_, sandboxDuration)
        : qMax(0.0, owner_->state_.qtPreviewPauseSecond_);

    // Install the test chart as the preview source + dispatch the per-page
    // audition mix. onPage_ is already true (setOnPage flips it before calling
    // this), so the single mode-aware dispatch resolves to LatencyAudition levels.
    owner_->applyPreviewAudioSettingsToRuntime();
    setupSandboxPreviewState();
    applyPlayheadToScene(restoreSecond);

    if (tickTimer_ != nullptr) {
        tickTimer_->start();
    }
    emit auditionStateChanged(false);
    emit playheadAdvanced(restoreSecond);
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
    if (!owner_.isNull()) {
        // onPage_ is already false here (setOnPage flips it before calling
        // teardown), so this dispatch resolves to the user's normal mix — no
        // snapshot needed. This is the deterministic restore on every page exit.
        owner_->applyPreviewAudioSettingsToRuntime();
    }

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
    // Hot-apply BPM / offset / subdivision changes even mid-playback: the song
    // audio is the fixed master clock and is never re-seeked here — we only
    // rebuild the test-chart notes + SFX timeline and re-anchor them to the
    // current play position (see setupSandboxPreviewState), so the music keeps
    // playing while only the SFX and the on-screen notes move to the new params.
    setupSandboxPreviewState();
    const double second = owner_->state_.qtPreviewPlaying_
        ? owner_->currentPreviewAuthoritativeAudioClockSecond()
        : qMax(0.0, owner_->state_.qtPreviewPauseSecond_);
    applyPlayheadToScene(second);
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
        // configureTimeline() rebuilds the event list and resets the SFX cursor
        // to the start. When we rebuild mid-playback (a hot offset/BPM/细分
        // change), re-anchor the cursor to the live audio second so the next
        // drainEvents() does not re-fire every tap scheduled before "now".
        // Only the SFX event cursor is touched — the background music is never
        // seeked, so it keeps playing. drainEvents() runs on this same (UI)
        // thread via the preview tick, so the reposition is atomic w.r.t. it.
        if (owner_->state_.qtPreviewPlaying_) {
            owner_->state_.previewSfxRuntime_->resetCursor(
                owner_->currentPreviewAuthoritativeAudioClockSecond(), false);
        }
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
