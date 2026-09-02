#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Shared.h"

#include "app/qml_ui/export/QmlExportSession.h"
#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "tools/video_export/FontLibrary.h"
#include "common/IntroConfig.h"
#include "tools/video_export/VideoExportController.h"
#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <cstdio>  // G2 Diag: std::snprintf for sync rate-change beacon lines
#include "runtime/playback/Playback.Internal.h"

using namespace miacode::runtime::shared;
using namespace miacode::runtime::playback_detail;

namespace {
// Spec -> IntroOverlay.qml banner template (the JSON layout the export overlay
// mount also reads). Kept file-local; introBannerTrackMap is the shared inline
// in VideoExportController.h.
QVariantMap introLeadInBannerTemplateMap()
{
    QVariantMap templateMap;
    QFile templateFile(QString::fromLatin1(miacode::intro::kBannerTemplateUrl).mid(3));  // "qrc:" -> ":"
    if (templateFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(templateFile.readAll());
        if (doc.isObject()) {
            templateMap = doc.object().toVariantMap();
        }
    }
    return templateMap;
}
}  // namespace

// qmlExportSession_ is a RuntimeContext::Ui field (see SessionMembers.inc), so
// this only touches ui_.
bool miacode::runtime::PlaybackCoordinator::currentExportIntroLeadInSpec(IntroBannerSpec* outSpec) const
{
    // The export session owns the shared 片头 settings. The audition reads them
    // at play time so both single and batch preview stay WYSIWYG.
    if (ui_.qmlExportSession_ != nullptr
        && ui_.qmlExportSession_->pageSessionActive()
        && ui_.qmlExportSession_->previewIntroSpec().enabled) {
        if (outSpec != nullptr) {
            *outSpec = ui_.qmlExportSession_->previewIntroSpec();
        }
        return true;
    }
    return false;
}

bool miacode::runtime::PlaybackCoordinator::exportIntroEnabled() const
{
    // Authoritative, read live from the panel each time (NO cached flag — a
    // cached duration could be reset to 0 by a transient refresh while 添加片头
    // is steadily on, collapsing the slider range; the bug we saw).
    return state_.exportPreviewAuditionActive_ && currentExportIntroLeadInSpec(nullptr);
}

double miacode::runtime::PlaybackCoordinator::exportIntroLowerBoundSeconds() const
{
    // The intro occupies negative time [-duration, 0) only while the export
    // audition is up and 添加片头 is on; otherwise the slider starts at 0.
    return exportIntroEnabled() ? -miacode::intro::kDurationSeconds : 0.0;
}

void miacode::runtime::PlaybackCoordinator::setupExportIntroOverlayData()
{
    if (state_.scene_ == nullptr) {
        return;
    }
    IntroBannerSpec spec;
    if (!currentExportIntroLeadInSpec(&spec)) {
        return;
    }
    // backgroundImage is ALWAYS the 曲绘 jacket (it feeds the card's jacket slot
    // AND the backdrop fallback) — mirror the export mount, which passes jacketUrl
    // here and routes the 片头 tab's 背景虚化/自定义背景/卡片阴影 through the style
    // map (introBannerStyleMap → backdropImage/backdropBlurEnabled/cardShadowEnabled).
    // Passing the custom backdrop as backgroundImage (the old behavior) wrongly
    // replaced the card jacket and ignored the blur toggle.
    const QUrl jacketUrl =
        spec.jacketPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(spec.jacketPath);
    // Overlay the dialog's difficulty-card custom fonts onto the lead-in template
    // copy so the main-timeline audition matches the export (same FontLibrary
    // override as the export mount + the dialog preview).
    QVariantMap templateMap = introLeadInBannerTemplateMap();
    miacode::video_export::applyBannerFontOverride(templateMap, spec.fontDisplayPath, spec.fontBodyPath);
    state_.scene_->setIntroOverlayData(
        introBannerTrackMap(spec),
        templateMap,
        jacketUrl,
        QUrl(QString::fromLatin1(miacode::intro::kLogoFallbackUrl)),
        introBannerStyleMap(spec));
}

void miacode::runtime::PlaybackCoordinator::renderExportIntroFrame(double positionSeconds)
{
    if (state_.scene_ == nullptr) {
        return;
    }
    // position in [-duration, 0] maps to authoring frame 0..kDurationFrames.
    const double into = positionSeconds + miacode::intro::kDurationSeconds;
    const int frame = qBound(
        0, qRound(into * static_cast<double>(miacode::intro::kAuthoringFps)),
        miacode::intro::kDurationFrames);
    state_.scene_->setIntroOverlayFrame(frame, true);
}

void miacode::runtime::PlaybackCoordinator::enterExportIntroRegion(double positionSeconds)
{
    if (state_.scene_ == nullptr || !exportIntroEnabled()) {
        return;
    }
    if (state_.playing_) {
        stopQtPreviewPlayback(true);  // freeze the chart behind the overlay
    }
    if (!state_.exportIntroRegionActive_) {
        requestPausedPreviewSeek(0.0, false, true);
        setupExportIntroOverlayData();
    }
    state_.exportIntroRegionActive_ = true;
    state_.previewTransportState_ = miacode::v2::PlaybackTransportState::Paused;
    state_.exportIntroPlayheadSeconds_ =
        qBound(-miacode::intro::kDurationSeconds, positionSeconds, 0.0);
    renderExportIntroFrame(state_.exportIntroPlayheadSeconds_);
    updatePreviewSliderPosition(state_.exportIntroPlayheadSeconds_);
}

void miacode::runtime::PlaybackCoordinator::exitExportIntroRegion()
{
    const bool wasActive = state_.exportIntroRegionActive_ || state_.exportIntroLeadInActive_;
    state_.exportIntroLeadInActive_ = false;
    state_.exportIntroRegionActive_ = false;
    // Drop the negative playhead so a later stray read can't resurrect a frozen
    // intro position (the region flags above are authoritative; this is hygiene).
    state_.exportIntroPlayheadSeconds_ = 0.0;
    if (state_.exportIntroLeadInTimer_ != nullptr) {
        state_.exportIntroLeadInTimer_->stop();
    }
    if (wasActive && state_.scene_ != nullptr) {
        state_.scene_->clearIntroOverlay(true);
    }
    if (wasActive) {
        updatePauseButtonAppearance();
    }
    if (state_.previewTransportState_ == miacode::v2::PlaybackTransportState::Playing) {
        state_.previewTransportState_ = miacode::v2::PlaybackTransportState::Paused;
    }
}

bool miacode::runtime::PlaybackCoordinator::exportIntroLeadInPlaying() const
{
    return state_.exportIntroLeadInActive_;
}

void miacode::runtime::PlaybackCoordinator::cancelExportIntroLeadIn()
{
    // Full exit (clears the overlay) — used by stop / teardown.
    exitExportIntroRegion();
}

void miacode::runtime::PlaybackCoordinator::pauseExportIntroAdvance()
{
    if (!state_.exportIntroLeadInActive_) {
        return;
    }
    state_.exportIntroLeadInActive_ = false;
    if (state_.exportIntroLeadInTimer_ != nullptr) {
        state_.exportIntroLeadInTimer_->stop();
    }
    // Keep the region + static frame so the paused intro stays on screen.
    state_.previewTransportState_ = miacode::v2::PlaybackTransportState::Paused;
    updatePauseButtonAppearance();
}

void miacode::runtime::PlaybackCoordinator::startExportIntroAdvance(double fromPositionSeconds)
{
    if (state_.scene_ == nullptr || !exportIntroEnabled()) {
        return;
    }
    enterExportIntroRegion(fromPositionSeconds);
    state_.exportIntroAdvanceFromSeconds_ = state_.exportIntroPlayheadSeconds_;

    // Opening jingle — only when advancing from at/near the intro head. Played
    // through the SAME BASS audition path as the note SFX / clock count-in: the
    // QSoundEffect path was inaudible on this Windows/Qt build (GUI 2026-06-16),
    // while audition() is proven (clock_count works). The SFX runtime is already
    // prepared by installExportPreviewAuditionScene; ensure it anyway (idempotent).
    if (state_.exportIntroPlayheadSeconds_ <= -miacode::intro::kDurationSeconds + 0.1) {
        ensurePreviewSfxRuntimePrepared(state_);
        if (state_.previewSfxRuntime_ != nullptr) {
            state_.previewSfxRuntime_->audition(QStringLiteral("track_start"), 1.0);
        }
    }

    if (state_.exportIntroLeadInTimer_ == nullptr) {
        state_.exportIntroLeadInTimer_ = new QTimer(&session_);
        state_.exportIntroLeadInTimer_->setInterval(16);  // ~60 fps overlay frame stepping
        QObject::connect(state_.exportIntroLeadInTimer_, &QTimer::timeout, &session_, [this]() {
            tickExportIntroLeadIn();
        });
    }
    state_.exportIntroLeadInActive_ = true;
    state_.previewTransportState_ = miacode::v2::PlaybackTransportState::Playing;
    state_.exportIntroLeadInElapsed_.restart();
    state_.exportIntroLeadInTimer_->start();
    updatePauseButtonAppearance();
}

void miacode::runtime::PlaybackCoordinator::tickExportIntroLeadIn()
{
    if (!state_.exportIntroLeadInActive_) {
        return;
    }
    const double elapsedSeconds = static_cast<double>(state_.exportIntroLeadInElapsed_.elapsed()) / 1000.0;
    const double position = state_.exportIntroAdvanceFromSeconds_ + elapsedSeconds;
    if (position >= 0.0) {
        // Crossed 0 -> hand off to the normal chart audition from the chart head.
        exitExportIntroRegion();
        startQtPreviewPlayback(0.0, true);
        return;
    }
    state_.exportIntroPlayheadSeconds_ = position;
    renderExportIntroFrame(position);
    updatePreviewSliderPosition(position);
}

bool miacode::runtime::PlaybackCoordinator::handleExportIntroSliderSeek(double second)
{
    if (!exportIntroEnabled()) {
        return false;
    }
    if (second >= 0.0) {
        // Back in the chart region: drop the overlay and let the normal seek run.
        if (state_.exportIntroRegionActive_) {
            exitExportIntroRegion();
        }
        return false;
    }
    // In the intro region: render the frame statically (no chart audio/advance).
    if (state_.exportIntroLeadInActive_) {
        pauseExportIntroAdvance();
    }
    enterExportIntroRegion(second);
    return true;
}

void miacode::runtime::PlaybackCoordinator::refreshExportIntroState()
{
    const bool introOn = exportIntroEnabled();
    if (!introOn) {
        // 添加片头 off (or left the page): leave the intro region, back to chart 0.
        if (state_.exportIntroRegionActive_ || state_.exportIntroLeadInActive_) {
            exitExportIntroRegion();
            miacode::runtime::shared::writePreviewPauseSecond(
                state_.pauseSecond_, 0.0, state_.playing_, "refresh_export_intro_state");
            seekPreviewDiscreteToSecond(0.0, true);
        }
        updatePreviewSliderRange();
        updatePreviewSliderPosition(qMax(0.0, state_.pauseSecond_));
        return;
    }
    updatePreviewSliderRange();
    if (state_.exportIntroRegionActive_) {
        // Refresh the overlay with the new 片头 settings, keep the position.
        setupExportIntroOverlayData();
        renderExportIntroFrame(state_.exportIntroPlayheadSeconds_);
    } else if (!state_.playing_ && qAbs(state_.pauseSecond_) <= 0.05) {
        // Default the playhead to the intro head so the user sees it first.
        enterExportIntroRegion(-miacode::intro::kDurationSeconds);
    }
}

void miacode::runtime::PlaybackCoordinator::setExportAuditionClockSchedule(int clockCount, double clockBpm)
{
    // clock_count count-in for the export audition. clock ticks live at chart-time
    // [0, count*beat) (beat = 60/clockBpm), mirroring the export's
    // appendClockCountPlaybacks — they sound on the chart audition AFTER the 片头
    // hands off at chart 0.
    const bool valid = clockCount > 0 && qIsFinite(clockBpm) && clockBpm > 0.0;
    state_.exportAuditionClockCount_ = valid ? clockCount : 0;
    state_.exportAuditionClockBeatSeconds_ = valid ? (60.0 / clockBpm) : 0.0;
    state_.exportAuditionClockNextIndex_ = 0;
}

void miacode::runtime::PlaybackCoordinator::clearExportAuditionClockSchedule()
{
    state_.exportAuditionClockCount_ = 0;
    state_.exportAuditionClockBeatSeconds_ = 0.0;
    state_.exportAuditionClockNextIndex_ = 0;
}

void miacode::runtime::PlaybackCoordinator::resetExportAuditionClockCursor(double startSecond)
{
    // Skip ticks that already elapsed before startSecond WITHOUT firing them, so
    // resuming mid-chart or seeking past the count-in doesn't replay it.
    int index = 0;
    if (state_.exportAuditionClockBeatSeconds_ > 0.0) {
        while (index < state_.exportAuditionClockCount_
               && index * state_.exportAuditionClockBeatSeconds_
                      + kTimelineZeroSecondTolerance < startSecond) {
            ++index;
        }
    }
    state_.exportAuditionClockNextIndex_ = index;
}

void miacode::runtime::PlaybackCoordinator::maybeFireExportAuditionClockTicks(double second)
{
    if (!state_.exportPreviewAuditionActive_
        || state_.exportAuditionClockCount_ <= 0
        || state_.exportAuditionClockBeatSeconds_ <= 0.0
        || state_.previewSfxRuntime_ == nullptr) {
        return;
    }
    while (state_.exportAuditionClockNextIndex_ < state_.exportAuditionClockCount_) {
        const double tickSecond =
            state_.exportAuditionClockNextIndex_ * state_.exportAuditionClockBeatSeconds_;
        if (tickSecond > second + kTimelineZeroSecondTolerance) {
            break;  // not yet due
        }
        // Don't machine-gun a backlog: skip (without playing) any tick we blew past
        // by more than one beat (a forward seek during playback). The downbeat at 0
        // and on-time ticks (≤ one frame late) still fire — gain 1.0 so the loaded
        // clock sample's own clock-volume level applies.
        if (second - tickSecond <= state_.exportAuditionClockBeatSeconds_) {
            state_.previewSfxRuntime_->audition(QStringLiteral("clock"), 1.0);
        }
        ++state_.exportAuditionClockNextIndex_;
    }
}
