#include "runtime/playback/PlaybackHost.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/ContentDurationConfig.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
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

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace miacode::runtime::shared;

namespace {

void appendPreviewFramePacingDiagLog(const QString& action, const QString& payload = QString())
{
    if (!miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        return;
    }
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/frame_pacing"),
        text,
        true
    );
}

void appendPreviewFramePacingStatusLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/frame_pacing"),
        text,
        true
    );
}

int previewFrameSwapWatchdogTimeoutMs(qint64 frameIntervalNs)
{
    return qMax(
        24,
        qMax(1, qRound(static_cast<double>(frameIntervalNs) / 1000000.0)) * 2
    );
}

}  // namespace

void Session::setPreviewFixedTimerHighResolutionActive(bool active)
{
#ifdef Q_OS_WIN
    const bool envRequested = miacode::debug_options::previewFixedTimerHighResolutionEnabled();
    const bool desired =
        active && envRequested;
    if (active && scene_ != nullptr) {
        scene_->noteFixedTimerHighResolutionRequest(envRequested);
    }
    if (qtPreviewFixedTimerHighResResolutionActive_ == desired) {
        if (active) {
            appendPreviewFramePacingStatusLog(
                QStringLiteral("fixed_timer_high_res_requested"),
                QStringLiteral("env=%1 active=%2 already_active=%3")
                    .arg(envRequested ? 1 : 0)
                    .arg(active ? 1 : 0)
                    .arg(qtPreviewFixedTimerHighResResolutionActive_ ? 1 : 0)
            );
        }
        return;
    }
    if (desired) {
        appendPreviewFramePacingStatusLog(
            QStringLiteral("fixed_timer_high_res_requested"),
            QStringLiteral("env=1 active=1 already_active=0")
        );
        const MMRESULT result = timeBeginPeriod(1);
        if (result == TIMERR_NOERROR) {
            qtPreviewFixedTimerHighResResolutionActive_ = true;
            if (scene_ != nullptr) {
                scene_->noteFixedTimerHighResolutionBeginResult(true);
            }
            appendPreviewFramePacingStatusLog(
                QStringLiteral("fixed_timer_high_res_enabled"),
                QStringLiteral("result=0")
            );
            return;
        }
        if (scene_ != nullptr) {
            scene_->noteFixedTimerHighResolutionBeginResult(false);
        }
        appendPreviewFramePacingStatusLog(
            QStringLiteral("fixed_timer_high_res_failed"),
            QStringLiteral("result=%1").arg(static_cast<unsigned int>(result))
        );
        return;
    }
    const bool activeAtStop = qtPreviewFixedTimerHighResResolutionActive_;
    if (!activeAtStop) {
        return;
    }
    if (scene_ != nullptr) {
        scene_->noteFixedTimerHighResolutionStopState(true);
    }
    timeEndPeriod(1);
    qtPreviewFixedTimerHighResResolutionActive_ = false;
    appendPreviewFramePacingStatusLog(
        QStringLiteral("fixed_timer_high_res_disabled")
    );
#else
    Q_UNUSED(active);
#endif
}

Session::PreviewCanvasFrameRateMode Session::previewFrameRateModeFromStorageValue(
    const QString& value,
    PreviewCanvasFrameRateMode fallback) const
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("30") || normalized == QLatin1String("30fps")) {
        return PreviewCanvasFrameRateMode::Fps30;
    }
    if (normalized == QLatin1String("60") || normalized == QLatin1String("60fps")) {
        return PreviewCanvasFrameRateMode::Fps60;
    }
    if (normalized == QLatin1String("120") || normalized == QLatin1String("120fps")) {
        return PreviewCanvasFrameRateMode::Fps120;
    }
    if (normalized == QLatin1String("display")
        || normalized == QLatin1String("display_max")
        || normalized == QLatin1String("screen")
        || normalized == QLatin1String("unlimited")) {
        return PreviewCanvasFrameRateMode::DisplayRefresh;
    }
    return fallback;
}

Session::PreviewCanvasFrameRateMode Session::previewCanvasFrameRateModeFromStorageValue(const QString& value) const
{
    return previewFrameRateModeFromStorageValue(value, PreviewCanvasFrameRateMode::Fps60);
}

QString miacode::runtime::PlaybackHost::previewFrameRateModeStorageValue(PreviewCanvasFrameRateMode mode) const
{
    switch (mode) {
    case PreviewCanvasFrameRateMode::Fps30:
        return QStringLiteral("30");
    case PreviewCanvasFrameRateMode::Fps120:
        return QStringLiteral("120");
    case PreviewCanvasFrameRateMode::DisplayRefresh:
        return QStringLiteral("display_max");
    case PreviewCanvasFrameRateMode::Fps60:
    default:
        return QStringLiteral("60");
    }
}

QString miacode::runtime::PlaybackHost::previewCanvasFrameRateModeStorageValue() const
{
    return previewFrameRateModeStorageValue(state_.previewCanvasFrameRateMode_);
}

QString miacode::runtime::PlaybackHost::previewStageMediaFrameRateModeStorageValue() const
{
    return previewFrameRateModeStorageValue(state_.previewStageMediaFrameRateMode_);
}

QString miacode::runtime::PlaybackHost::timelineFrameRateModeStorageValue() const
{
    return previewFrameRateModeStorageValue(state_.timelineFrameRateMode_);
}

Session::PreviewCanvasFrameRateMode Session::currentPreviewCanvasFrameRateMode() const
{
    return state_.previewCanvasFrameRateMode_;
}

Session::PreviewCanvasFrameRateMode Session::currentPreviewStageMediaFrameRateMode() const
{
    return playback_->currentPreviewStageMediaFrameRateMode();
}

bool Session::currentVideoDecodePrefersSoftware() const
{
    return playback_->currentVideoDecodePrefersSoftware();
}

Session::PreviewCanvasFrameRateMode Session::currentTimelineFrameRateMode() const
{
    return playback_->currentTimelineFrameRateMode();
}

Session::PreviewCanvasFrameRateMode miacode::runtime::PlaybackHost::currentPreviewStageMediaFrameRateMode() const
{
    return state_.previewStageMediaFrameRateMode_;
}

bool miacode::runtime::PlaybackHost::currentVideoDecodePrefersSoftware() const
{
    return state_.videoDecodePrefersSoftware_;
}

Session::PreviewCanvasFrameRateMode miacode::runtime::PlaybackHost::currentTimelineFrameRateMode() const
{
    return state_.timelineFrameRateMode_;
}

double miacode::runtime::PlaybackHost::currentPreviewCanvasRefreshRate() const
{
    QScreen* targetScreen = nullptr;
    if (session_.rootWindow_ != nullptr) {
        targetScreen = session_.rootWindow_->screen();
    }
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    const double refreshRate = targetScreen != nullptr ? targetScreen->refreshRate() : 0.0;
    if (!qIsFinite(refreshRate) || refreshRate < 1.0) {
        return 60.0;
    }
    return refreshRate;
}

bool miacode::runtime::PlaybackHost::previewCanvasUsesFrameSwappedPacing() const
{
    // Disabled (2026-04-27): the present-driven gate from 927322b was reverted
    // because coupling the playback tick to frameSwapped meant any render
    // hiccup directly stalled the playback clock — visible as choppiness on
    // hardware that can't sustain 60fps. Reverting to the beta19 timer-driven
    // path (qtPreviewTimer_ firing on a fixed interval regardless of render
    // throughput) preserves audio-video sync at the cost of being able to
    // over-tick under heavy load. The rest of 927322b — TripleBuffer, async
    // log writer, MMCSS, sprite batching, visual-clock smoothing — is kept;
    // only the gate is dropped. The gate code paths are left in place but
    // unreachable so the surgery is one line.
    return false;
}

qint64 miacode::runtime::PlaybackHost::previewCanvasTargetFrameIntervalNs() const
{
    // The render thread Presents at vsync (syncInterval >= 1), so it
    // physically cannot sustain a target rate above displayHz. The GUI
    // publish timer must match the *effective* render cadence — if it
    // ticks faster, snapshots queue up + get dropped, producing visible
    // stutter. Reported by users selecting 120 FPS on a 65 Hz display:
    // GUI ticked at 120 Hz, render Presented at 65 Hz, mismatch
    // manifested as periodic frame-skip artefacts.
    //
    // Fix: clamp targetHz to displayHz before deriving the interval.
    // Same clamp guards Fps60 against (rare) sub-60 Hz displays.
    const double displayHz = currentPreviewCanvasRefreshRate();
    double targetHz;
    switch (state_.previewCanvasFrameRateMode_) {
    case PreviewCanvasFrameRateMode::Fps30:
        targetHz = qMin(30.0, displayHz);
        break;
    case PreviewCanvasFrameRateMode::Fps120:
        targetHz = qMin(120.0, displayHz);
        break;
    case PreviewCanvasFrameRateMode::Fps60:
        targetHz = qMin(60.0, displayHz);
        break;
    case PreviewCanvasFrameRateMode::DisplayRefresh:
    default:
        targetHz = displayHz;
        break;
    }
    return qMax<qint64>(1LL, qRound64(1000000000.0 / qMax(1.0, targetHz)));
}

double miacode::runtime::PlaybackHost::targetRefreshRateForFrameRateMode(PreviewCanvasFrameRateMode mode) const
{
    const double displayHz = currentPreviewCanvasRefreshRate();
    switch (mode) {
    case PreviewCanvasFrameRateMode::Fps30:
        return qMin(30.0, displayHz);
    case PreviewCanvasFrameRateMode::Fps60:
        return qMin(60.0, displayHz);
    case PreviewCanvasFrameRateMode::Fps120:
        return qMin(120.0, displayHz);
    case PreviewCanvasFrameRateMode::DisplayRefresh:
    default:
        return displayHz;
    }
}

qint64 miacode::runtime::PlaybackHost::targetFrameIntervalNsForFrameRateMode(PreviewCanvasFrameRateMode mode) const
{
    return qMax<qint64>(1LL, qRound64(1000000000.0 / qMax(1.0, targetRefreshRateForFrameRateMode(mode))));
}

qint64 miacode::runtime::PlaybackHost::timelineTargetFrameIntervalNs() const
{
    const double timelineTargetFps =
        qMin(targetRefreshRateForFrameRateMode(state_.timelineFrameRateMode_),
             miacode::runtime::shared::kTimelineMaxUiUpdateFps);
    return qMax<qint64>(1LL, qRound64(1000000000.0 / qMax(1.0, timelineTargetFps)));
}

void miacode::runtime::PlaybackHost::refreshTimelineWaveformPhaseCompensation()
{
    if (state_.timelineQuickStateBridge_ == nullptr) {
        return;
    }
    const qint64 frameIntervalNs = qMax<qint64>(1, previewCanvasTargetFrameIntervalNs());
    state_.timelineQuickStateBridge_->setWaveformPhaseCompensationSeconds(
        static_cast<double>(frameIntervalNs) / 1000000000.0);
}

void miacode::runtime::PlaybackHost::resetQtPreviewFixedFramePacing()
{
    state_.qtPreviewNextFixedTickDueNs_ = -1;
    state_.qtPreviewFixedTickOriginNs_ = -1;
    if (previewCanvasUsesFrameSwappedPacing()) {
        return;
    }
    state_.qtPreviewFixedTickOriginNs_ = state_.qtPreviewWatchdogElapsed_.nsecsElapsed();
    state_.qtPreviewNextFixedTickDueNs_ =
        state_.qtPreviewFixedTickOriginNs_ + previewCanvasTargetFrameIntervalNs();
}

void miacode::runtime::PlaybackHost::scheduleNextQtPreviewTick()
{
    // Doc section 4.3: qtPreviewTimer_ is a watchdog in both modes. Normal visual cadence is
    // driven by framePresented -> queued tick; this timer only kicks if present never arrives.
    if (ui_.qtPreviewTimer_ == nullptr || !state_.playing_) {
        return;
    }
    qint64 awaitingSinceMs = -1;
    bool awaiting = false;
    if (previewCanvasUsesFrameSwappedPacing()) {
        awaiting = state_.qtPreviewAwaitingFrameSwap_;
        awaitingSinceMs = state_.qtPreviewAwaitingFrameSwapSinceMs_;
    } else {
        awaiting = state_.qtPreviewFixedAwaitingFrame_;
        awaitingSinceMs = state_.qtPreviewFixedAwaitingFrameSinceMs_;
    }
    if (!awaiting) {
        ui_.qtPreviewTimer_->stop();
        return;
    }
    const qint64 nowMs = state_.qtPreviewWatchdogElapsed_.elapsed();
    const qint64 elapsedMs =
        awaitingSinceMs >= 0 ? qMax<qint64>(0, nowMs - awaitingSinceMs) : 0;
    const int timeoutMs = previewFrameSwapWatchdogTimeoutMs(previewCanvasTargetFrameIntervalNs());
    const int delayMs =
        qMax(1, timeoutMs - static_cast<int>(qMin<qint64>(elapsedMs, timeoutMs)));
    ui_.qtPreviewTimer_->setInterval(std::chrono::milliseconds(delayMs));
    ui_.qtPreviewTimer_->start();
}

void miacode::runtime::PlaybackHost::requestNextDisplayRefreshPreviewFrame()
{
    if (!state_.playing_
        || state_.scene_ == nullptr
        || !previewCanvasUsesFrameSwappedPacing()
        || state_.qtPreviewAwaitingFrameSwap_) {
        return;
    }
    state_.qtPreviewAwaitingFrameSwap_ = true;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = state_.qtPreviewWatchdogElapsed_.elapsed();
    state_.qtPreviewAwaitingFrameSwapSinceNs_ = state_.qtPreviewWatchdogElapsed_.nsecsElapsed();
    state_.qtPreviewDisplayRefreshFrameRequestSeq_ += 1;
    state_.scene_->noteDisplayRefreshFrameRequest();
    if (miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
        if (state_.qtPreviewFramePacingDiagLastRequestLogMs_ < 0
            || state_.qtPreviewAwaitingFrameSwapSinceMs_ - state_.qtPreviewFramePacingDiagLastRequestLogMs_ >= sampleMs) {
            state_.qtPreviewFramePacingDiagLastRequestLogMs_ = state_.qtPreviewAwaitingFrameSwapSinceMs_;
            appendPreviewFramePacingDiagLog(
                QStringLiteral("display_request"),
                QStringLiteral("seq=%1 playhead=%2 target_ms=%3")
                    .arg(state_.qtPreviewDisplayRefreshFrameRequestSeq_)
                    .arg(state_.pauseSecond_, 0, 'f', 6)
                    .arg(previewFrameSwapWatchdogTimeoutMs(previewCanvasTargetFrameIntervalNs()))
            );
        }
    }
    state_.scene_->update();
    scheduleNextQtPreviewTick();
}

void miacode::runtime::PlaybackHost::requestNextFixedIntervalPreviewFrame()
{
    // Doc section 4.1: fixed 60/120 is now present-driven. This mirrors the DisplayRefresh
    // request path — set awaiting flag, ask the canvas to update, arm the watchdog.
    if (!state_.playing_
        || state_.scene_ == nullptr
        || previewCanvasUsesFrameSwappedPacing()
        || state_.qtPreviewFixedAwaitingFrame_) {
        return;
    }
    state_.qtPreviewFixedAwaitingFrame_ = true;
    state_.qtPreviewFixedAwaitingFrameSinceMs_ = state_.qtPreviewWatchdogElapsed_.elapsed();
    state_.qtPreviewFixedAwaitingFrameSinceNs_ = state_.qtPreviewWatchdogElapsed_.nsecsElapsed();
    state_.qtPreviewFixedGateFrameRequestSeq_ += 1;
    state_.scene_->update();
    scheduleNextQtPreviewTick();
}

void miacode::runtime::PlaybackHost::requestNextPreviewCanvasFrame()
{
    if (previewCanvasUsesFrameSwappedPacing()) {
        requestNextDisplayRefreshPreviewFrame();
    } else {
        requestNextFixedIntervalPreviewFrame();
    }
}

void miacode::runtime::PlaybackHost::advanceFixedIntervalGateAfterPresent()
{
    // Doc section 4.1: FPS gate between presents. Only run a visual tick when the target
    // interval has elapsed since the *scheduled* time of the previous tick (phase-locked),
    // not since the tick's end-of-work wall time. Otherwise tick execution time (2-5ms) would
    // cause every other present at 60Hz to miss the gate, halving effective tick rate.
    if (!state_.playing_
        || previewCanvasUsesFrameSwappedPacing()
        || state_.qtPreviewFixedAwaitingFrame_) {
        return;
    }
    const qint64 nowNs = state_.qtPreviewWatchdogElapsed_.nsecsElapsed();
    const qint64 targetIntervalNs = qMax<qint64>(1, previewCanvasTargetFrameIntervalNs());
    // Allow ~12.5% of the target as slop so vsync jitter (e.g. 16.4ms on a 60Hz panel) does
    // not cause the gate to reject a present that arrived a fraction earlier than target.
    const qint64 slopNs = targetIntervalNs / 8;
    const bool firstTick = state_.qtPreviewLastVisualTickNs_ < 0;
    const qint64 sinceLastNs =
        firstTick ? targetIntervalNs : qMax<qint64>(0, nowNs - state_.qtPreviewLastVisualTickNs_);
    const bool gatePassed = firstTick || (sinceLastNs + slopNs) >= targetIntervalNs;
    if (gatePassed) {
        // Record the canonical "tick time" now, BEFORE the tick runs. Using phase-locked
        // advancement (previous + targetInterval) keeps the tick cadence aligned with the
        // target rate instead of drifting by the tick's work latency. Two clamps:
        //   1. Way late (display hidden, scheduler stall): resync to now — doc section 3.2
        //      forbids chasing multiple missed slots.
        //   2. Slightly ahead of nowNs (presents arriving faster than target, e.g. on a
        //      144Hz panel with 60Hz target): cap to nowNs so lastVisualTickNs_ cannot
        //      accumulate ahead of wall time and eventually starve the gate.
        if (firstTick) {
            state_.qtPreviewLastVisualTickNs_ = nowNs;
        } else {
            const qint64 idealNextNs = state_.qtPreviewLastVisualTickNs_ + targetIntervalNs;
            if (nowNs - idealNextNs > targetIntervalNs * 2) {
                state_.qtPreviewLastVisualTickNs_ = nowNs;
            } else {
                state_.qtPreviewLastVisualTickNs_ = qMin(idealNextNs, nowNs);
            }
        }
        if (state_.scene_ != nullptr) {
            state_.scene_->noteFixedGateVisualTick(sinceLastNs);
            if (!firstTick && sinceLastNs > targetIntervalNs * 2) {
                const qint64 missed = (sinceLastNs / targetIntervalNs) - 1;
                if (missed > 0) {
                    state_.scene_->noteFixedGateMissedTargetSlots(missed);
                }
            }
        }
        if (miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
            const qint64 nowMs = state_.qtPreviewWatchdogElapsed_.elapsed();
            const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
            if (state_.qtPreviewFramePacingDiagLastFixedGateLogMs_ < 0
                || nowMs - state_.qtPreviewFramePacingDiagLastFixedGateLogMs_ >= sampleMs) {
                state_.qtPreviewFramePacingDiagLastFixedGateLogMs_ = nowMs;
                appendPreviewFramePacingDiagLog(
                    QStringLiteral("fixed_gate_tick"),
                    QStringLiteral("source=present gate_wait_ms=%1 target_ms=%2 awaiting_frame=0 time_authority=%3")
                        .arg(static_cast<double>(sinceLastNs) / 1000000.0, 0, 'f', 3)
                        .arg(static_cast<double>(targetIntervalNs) / 1000000.0, 0, 'f', 3)
                        .arg(state_.previewSfxRuntime_ != nullptr
                            ? QStringLiteral("audio")
                            : QStringLiteral("elapsed_fallback"))
                );
            }
        }
        onQtPreviewTick();
    } else {
        if (state_.scene_ != nullptr) {
            state_.scene_->noteFixedGatePresentWithoutTick(sinceLastNs);
        }
        if (miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
            const qint64 nowMs = state_.qtPreviewWatchdogElapsed_.elapsed();
            const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
            if (state_.qtPreviewFramePacingDiagLastFixedGateLogMs_ < 0
                || nowMs - state_.qtPreviewFramePacingDiagLastFixedGateLogMs_ >= sampleMs) {
                state_.qtPreviewFramePacingDiagLastFixedGateLogMs_ = nowMs;
                appendPreviewFramePacingDiagLog(
                    QStringLiteral("fixed_gate_present_without_tick"),
                    QStringLiteral("gate_wait_ms=%1 target_ms=%2")
                        .arg(static_cast<double>(sinceLastNs) / 1000000.0, 0, 'f', 3)
                        .arg(static_cast<double>(targetIntervalNs) / 1000000.0, 0, 'f', 3)
                );
            }
        }
        // Keep the present pump alive so the next present can re-check the gate.
        requestNextFixedIntervalPreviewFrame();
    }
}

void miacode::runtime::PlaybackHost::refreshPreviewFrameRateTimers()
{
    const qint64 targetIntervalNs = previewCanvasTargetFrameIntervalNs();
    const qint64 timelineIntervalNs = timelineTargetFrameIntervalNs();
    if (ui_.qtPreviewTimer_ != nullptr) {
        ui_.qtPreviewTimer_->setInterval(std::chrono::nanoseconds(qMax<qint64>(1, targetIntervalNs)));
    }
    if (ui_.qtPreviewTimelineTimer_ != nullptr) {
        ui_.qtPreviewTimelineTimer_->setInterval(std::chrono::nanoseconds(qMax<qint64>(1, timelineIntervalNs)));
    }
    applyPreviewStageMediaFrameRateMode();
    if (state_.scene_ != nullptr) {
        state_.scene_->setFramePacingDebugState(
            previewCanvasUsesFrameSwappedPacing(),
            targetIntervalNs > 0 ? (1000000000.0 / static_cast<double>(targetIntervalNs)) : 0.0,
            currentPreviewCanvasRefreshRate()
        );
    }
    refreshTimelineWaveformPhaseCompensation();
}

void miacode::runtime::PlaybackHost::applyPreviewStageMediaFrameRateMode()
{
    if (state_.previewStageMediaHost_ == nullptr) {
        return;
    }
    state_.previewStageMediaHost_->setVideoFrameToImageMaxFps(
        targetRefreshRateForFrameRateMode(state_.previewStageMediaFrameRateMode_));
}

void miacode::runtime::PlaybackHost::setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    if (state_.previewCanvasFrameRateMode_ == mode) {
        refreshPreviewFrameRateTimers();
        return;
    }
    state_.previewCanvasFrameRateMode_ = mode;
    refreshPreviewFrameRateTimers();
    if (ui_.qtPreviewTimer_ != nullptr) {
        ui_.qtPreviewTimer_->stop();
    }
    state_.qtPreviewAwaitingFrameSwap_ = false;
    state_.qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    state_.qtPreviewAwaitingFrameSwapSinceNs_ = -1;
    state_.qtPreviewDisplayRefreshTickQueued_ = false;
    state_.qtPreviewDisplayRefreshConsecutiveWatchdogs_ = 0;
    state_.qtPreviewFixedAwaitingFrame_ = false;
    state_.qtPreviewFixedAwaitingFrameSinceMs_ = -1;
    state_.qtPreviewFixedAwaitingFrameSinceNs_ = -1;
    state_.qtPreviewFixedFrameTickQueued_ = false;
    state_.qtPreviewLastVisualTickNs_ = -1;
    state_.qtPreviewFramePacingDiagLastFixedGateLogMs_ = -1;
    resetQtPreviewFixedFramePacing();
    if (state_.playing_) {
        // Doc 4.3: high-res timer is no longer the cadence source; keep the call to preserve
        // existing Windows timer resolution hygiene for the watchdog path.
        session_.setPreviewFixedTimerHighResolutionActive(!previewCanvasUsesFrameSwappedPacing());
        if (state_.scene_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
            requestNextDisplayRefreshPreviewFrame();
        } else {
            requestNextFixedIntervalPreviewFrame();
        }
    } else {
        session_.setPreviewFixedTimerHighResolutionActive(false);
    }
    if (persistState) {
        session_.savePortableState();
    }
}

void miacode::runtime::PlaybackHost::setPreviewStageMediaFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    state_.previewStageMediaFrameRateMode_ = mode;
    applyPreviewStageMediaFrameRateMode();
    if (persistState) {
        session_.savePortableState();
    }
}

void miacode::runtime::PlaybackHost::setVideoDecodePrefersSoftware(bool preferSoftware, bool persistState)
{
    if (state_.videoDecodePrefersSoftware_ == preferSoftware) {
        return;
    }
    state_.videoDecodePrefersSoftware_ = preferSoftware;
    if (persistState) {
        session_.savePortableState();
    }
    // Push to the host if it already exists; otherwise the cached value is
    // applied once at host construction (ensurePreviewStageMediaHostInitialized).
    if (auto* host = session_.previewStageMediaHost()) {
        host->setVideoDecodePreference(preferSoftware);
    }
}

void miacode::runtime::PlaybackHost::setTimelineFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    state_.timelineFrameRateMode_ = mode;
    refreshPreviewFrameRateTimers();
    if (persistState) {
        session_.savePortableState();
    }
}
