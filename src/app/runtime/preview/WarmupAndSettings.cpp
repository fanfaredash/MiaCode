#include "runtime/preview/StageMediaHost.h"
#include "runtime/Shared.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewSfxAssets.h"
#include "common/ProjectPreferences.h"
#include "common/AssetPaths.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/latency/LatencySandboxController.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QApplication>

using namespace miacode::runtime::shared;

namespace {

// Project-preferences key holding the per-chart preview mixer
// (<chartDir>/.miacode/preferences.json).
constexpr auto kProjectAudioPreferencesKey = "preview_audio";

struct PreviewMediaWarmupResult {
    quint64 generation = 0;
    QString chartPath;
    QString trackPath;
    QString resolvedMediaPath;
    qint64 workerElapsedMs = -1;
};

struct PreviewSfxWarmupResult {
    quint64 generation = 0;
    QString chartPath;
    QString trackPath;
    QString sfxDir;
    qint64 workerElapsedMs = -1;
};

QStringList previewSfxWarmupKinds()
{
    static const QStringList kinds{
        QStringLiteral("answer"),
        QStringLiteral("judge"),
        QStringLiteral("judge_break"),
        QStringLiteral("slide"),
        QStringLiteral("break"),
        QStringLiteral("break_slide_start"),
        QStringLiteral("break_slide"),
        QStringLiteral("judge_break_slide"),
        QStringLiteral("ex"),
        QStringLiteral("touch"),
        QStringLiteral("touchhold"),
        QStringLiteral("firework"),
    };
    return kinds;
}

void warmupFileIntoOsCache(const QString& path, qint64 maxBytes = -1)
{
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    constexpr qint64 kChunkBytes = 64 * 1024;
    qint64 remainingBytes = maxBytes;
    while (!file.atEnd()) {
        if (remainingBytes == 0) {
            break;
        }
        const qint64 requestBytes = remainingBytes > 0 ? qMin(remainingBytes, kChunkBytes) : kChunkBytes;
        const QByteArray chunk = file.read(requestBytes);
        if (chunk.isEmpty()) {
            break;
        }
        if (remainingBytes > 0) {
            remainingBytes -= chunk.size();
        }
    }
}

}  // namespace

// Stage 4.9d-4a: standardPreviewSkinDirectoryName / dxPreviewSkinDirectoryName /
// legacyStandardPreviewSkinDirectoryName / normalizePreviewSkinDirectoryName /
// hasCorePreviewSkinAssets moved from this file's anonymous namespace to
// runtime::shared (runtime/Shared.h/.cpp) — availablePreviewSkinDirectoryNames()
// and previewSkinDisplayName() below now call the shared:: versions (see their
// forwarding bodies), and this file's other, unmoved callers of those helpers
// (previewSkinVariantStorageValue, resolvePreviewSkinDir) reach them through the
// `using namespace miacode::runtime::shared;` at the top of this file.

void miacode::runtime::StageMediaHost::ensurePreviewSfxRuntimePrepared()
{
    // Stage 4.9d-4a: body moved to runtime::shared — see runtime/Shared.cpp.
    // Qualified because this member shares the free function's name.
    miacode::runtime::shared::ensurePreviewSfxRuntimePrepared(state_);
}

void miacode::runtime::StageMediaHost::schedulePreviewSubsystemWarmup()
{
    if (state_.previewWarmupPool_ == nullptr) {
        return;
    }
    const quint64 generation = ++state_.previewWarmupGeneration_;
    PreviewAudioSettings audioSettingsSnapshot = state_.previewAudioSettings_;
    audioSettingsSnapshot.normalize();
    const QString chartPathSnapshot = state_.currentFilePath_;
    const QString trackPathSnapshot = state_.lastTrackPath_;
    // Phase 4c — capture &video= override snapshot so the worker
    // pre-resolves the right path (explicit override beats sibling).
    const QString chartVideoOverrideSnapshot = session_.applicationServices_.workspace().document().videoPath;
    const double playbackRateSnapshot = state_.previewPlaybackRate_;
    schedulePreviewMediaWarmup(generation, chartPathSnapshot, trackPathSnapshot, chartVideoOverrideSnapshot);
    schedulePreviewSfxWarmup(generation, chartPathSnapshot, trackPathSnapshot, audioSettingsSnapshot, playbackRateSnapshot);
}

void miacode::runtime::StageMediaHost::schedulePreviewMediaWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot,
    const QString& chartVideoOverrideSnapshot)
{
    if (state_.previewWarmupPool_ == nullptr) {
        return;
    }
    QPointer<Session> guard(&session_);
    state_.previewWarmupPool_->start([guard, generation, chartPathSnapshot, trackPathSnapshot, chartVideoOverrideSnapshot]() {
        QElapsedTimer timer;
        timer.start();
        PreviewMediaWarmupResult result;
        result.generation = generation;
        result.chartPath = chartPathSnapshot;
        result.trackPath = trackPathSnapshot;
        // Phase 4c — unified resolver honours `&video=` first, then
        // falls back to sibling `bg.mp4`/`pv.mp4`/`bg.png` etc. Same
        // file the live preview will load — pre-warming the OS cache
        // here so the host's first decode/load runs cheap.
#ifdef HAVE_QT_MULTIMEDIA
        result.resolvedMediaPath = miacode::chart_assets::resolveChartVideoPath(chartPathSnapshot, chartVideoOverrideSnapshot);
        if (result.resolvedMediaPath.isEmpty()) {
            result.resolvedMediaPath = miacode::chart_assets::resolveBackgroundMediaPath(chartPathSnapshot, true);
        }
#else
        result.resolvedMediaPath = miacode::chart_assets::resolveBackgroundMediaPath(chartPathSnapshot, false);
#endif
        if (!result.resolvedMediaPath.isEmpty()) {
            const QString suffix = QFileInfo(result.resolvedMediaPath).suffix().toLower();
            if (suffix == QStringLiteral("mp4")) {
                warmupFileIntoOsCache(result.resolvedMediaPath, 256 * 1024);
            } else {
                warmupFileIntoOsCache(result.resolvedMediaPath);
            }
        }
        result.workerElapsedMs = timer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                if (guard.isNull()) {
                    return;
                }
                guard->applyPreviewMediaWarmupResult(
                    result.generation,
                    result.chartPath,
                    result.resolvedMediaPath,
                    result.trackPath,
                    result.workerElapsedMs
                );
            },
            Qt::QueuedConnection
        );
    });
}

void miacode::runtime::StageMediaHost::schedulePreviewSfxWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot,
    const PreviewAudioSettings& audioSettingsSnapshot,
    double playbackRateSnapshot)
{
    Q_UNUSED(audioSettingsSnapshot);
    Q_UNUSED(playbackRateSnapshot);
    if (state_.previewWarmupPool_ == nullptr) {
        return;
    }
    QPointer<Session> guard(&session_);
    state_.previewWarmupPool_->start([guard, generation, chartPathSnapshot, trackPathSnapshot]() {
        QElapsedTimer timer;
        timer.start();
        PreviewSfxWarmupResult result;
        result.generation = generation;
        result.chartPath = chartPathSnapshot;
        result.trackPath = trackPathSnapshot;
        result.sfxDir = miacode::preview_sfx::resolveSfxDirectory();
        for (const QString& kind : previewSfxWarmupKinds()) {
            const QString path = miacode::preview_sfx::assetFilePathForKind(result.sfxDir, kind);
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                warmupFileIntoOsCache(path);
            }
        }
        if (!result.trackPath.isEmpty()) {
            warmupFileIntoOsCache(result.trackPath, 256 * 1024);
        }
        result.workerElapsedMs = timer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                if (guard.isNull()) {
                    return;
                }
                guard->applyPreviewSfxWarmupResult(
                    result.generation,
                    result.chartPath,
                    result.trackPath,
                    result.sfxDir,
                    result.workerElapsedMs
                );
            },
            Qt::QueuedConnection
        );
    });
}

void miacode::runtime::StageMediaHost::applyPreviewMediaWarmupResult(
    quint64 generation,
    const QString& chartPath,
    const QString& resolvedMediaPath,
    const QString& trackPath,
    qint64 workerElapsedMs)
{
    if (generation != state_.previewWarmupGeneration_) {
        return;
    }
    state_.previewMediaWarmupAppliedGeneration_ = generation;
    state_.previewMediaWarmupChartPath_ = chartPath;
    state_.previewMediaWarmupResolvedPath_ = resolvedMediaPath;
    state_.previewMediaWarmupTrackPath_ = trackPath;
    appendStartupTimingStage("mainwindow/preview_media_data_warmup", workerElapsedMs, workerElapsedMs);
    applyPreviewMediaWarmupToStageMediaRoute(chartPath, resolvedMediaPath, trackPath);
}

void miacode::runtime::StageMediaHost::applyPreviewSfxWarmupResult(
    quint64 generation,
    const QString& chartPath,
    const QString& trackPath,
    const QString& sfxDir,
    qint64 workerElapsedMs)
{
    if (generation != state_.previewWarmupGeneration_) {
        return;
    }
    state_.previewSfxWarmupAppliedGeneration_ = generation;
    state_.previewSfxWarmupChartPath_ = chartPath;
    state_.previewSfxWarmupTrackPath_ = trackPath;
    state_.previewSfxWarmupSfxDir_ = sfxDir;
    appendStartupTimingStage("mainwindow/preview_sfx_data_warmup", workerElapsedMs, workerElapsedMs);
    if (state_.previewSfxRuntime_ == nullptr
        || state_.previewSfxRuntimePrepared_
        || state_.previewSfxRuntimePreparationSequence_ != 0
        || state_.previewSfxRuntime_->isDeviceCutoffActive()
        || chartPath != state_.currentFilePath_) {
        return;
    }
    QElapsedTimer preloadTimer;
    preloadTimer.start();
    const QtPreviewSfxRuntime::AssetSubmission reload =
        state_.previewSfxRuntime_->reloadAssetsForChartWithWarmupPaths(
            chartPath, trackPath, sfxDir, state_.previewAudioSettings_);
    state_.previewSfxRuntimePrepared_ = false;
    state_.previewSfxRuntimePreparationAssetGeneration_ = reload.post.accepted
        ? reload.identity.assetGeneration
        : 0;
    state_.previewSfxRuntimePreparationSequence_ = reload.post.accepted
        ? reload.identity.sequence
        : 0;
    state_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(state_.previewPlaybackRate_);
    const qint64 preloadElapsedMs = preloadTimer.elapsed();
    appendStartupTimingStage(
        "mainwindow/preview_sfx_runtime_preload_queued",
        preloadElapsedMs,
        preloadElapsedMs);
}

QString miacode::runtime::StageMediaHost::resolveDefaultTrackPath() const
{
    const QString envTrack = qEnvironmentVariable("MIACODE_TRACK_PATH", qEnvironmentVariable("MAIMURI_TRACK_PATH"));
    if (!envTrack.isEmpty() && QFileInfo::exists(envTrack)) {
        return envTrack;
    }
    if (!state_.currentFilePath_.isEmpty()) {
        const QString siblingTrack = miacode::chart_assets::resolveTrackPath(state_.currentFilePath_);
        if (!siblingTrack.isEmpty()) {
            return siblingTrack;
        }
    }
    if (!state_.lastTrackPath_.isEmpty() && QFileInfo::exists(state_.lastTrackPath_)) {
        return state_.lastTrackPath_;
    }
    return QString();
}

PreviewOutlineVariant miacode::runtime::StageMediaHost::previewOutlineVariantFromStorageValue(const QString& value) const
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("point")) {
        return PreviewOutlineVariant::Point;
    }
    if (normalized == QLatin1String("judge_area")
        || normalized == QLatin1String("judgearea")
        || normalized == QLatin1String("area")) {
        return PreviewOutlineVariant::JudgeArea;
    }
    if (normalized == QLatin1String("judge_area_labeled")
        || normalized == QLatin1String("judgearea_labeled")
        || normalized == QLatin1String("judge_area_numbered")
        || normalized == QLatin1String("numbered_area")
        || normalized == QLatin1String("area_labeled")) {
        return PreviewOutlineVariant::JudgeAreaLabeled;
    }
    return PreviewOutlineVariant::Line;
}

QString miacode::runtime::StageMediaHost::previewOutlineVariantStorageValue() const
{
    switch (previewAppearanceValues_.outlineVariant) {
    case PreviewOutlineVariant::Point:
        return QStringLiteral("point");
    case PreviewOutlineVariant::JudgeArea:
        return QStringLiteral("judge_area");
    case PreviewOutlineVariant::JudgeAreaLabeled:
        return QStringLiteral("judge_area_labeled");
    case PreviewOutlineVariant::Line:
    default:
        return QStringLiteral("line");
    }
}

PreviewOutlineVariant miacode::runtime::StageMediaHost::autoPreviewOutlineVariantForChart(const QString& chartPath) const
{
    // Phase 4c — also count `&video=` overrides as "has background"
    // so charts that exclusively rely on the explicit-video path
    // (no sibling bg.* file) get the right outline variant. Without
    // this, a chart with `&video=bg.mp4` but no sibling files would
    // fall through to JudgeAreaLabeled even though the video does
    // render behind the playfield.
    return miacode::chart_assets::hasChartBackgroundMedia(
                chartPath, session_.applicationServices_.workspace().document().videoPath)
        ? PreviewOutlineVariant::Line
        : PreviewOutlineVariant::JudgeAreaLabeled;
}

PreviewOutlineVariant miacode::runtime::StageMediaHost::effectivePreviewOutlineVariant() const
{
    // The export-preview dialog ignores the pause-hide option so the on-screen
    // preview matches the exported video (the user's chosen outline variant, PV/BG
    // visible) instead of the paused judge-area view.
    // Holding Alt while paused (pauseDisplayAltHoldActive_) inverts the option
    // so the user can peek at the other view without opening 视频设置.
    const bool forceJudgeAreaWhenPaused =
        state_.previewForceLabeledJudgeLineWhenPaused_ != state_.pauseDisplayAltHoldActive_;
    if (forceJudgeAreaWhenPaused
        && !state_.playing_
        && !state_.exportPreviewActive_) {
        return PreviewOutlineVariant::JudgeAreaLabeled;
    }
    return previewAppearanceValues_.outlineVariant;
}

void miacode::runtime::StageMediaHost::setPauseDisplayAltHoldActive(bool active)
{
    // Transient Alt-hold inversion of the "暂停时显示判定区" option (see
    // WindowSection::eventFilter). Engaging is pointless while the preview is
    // playing or while the export-preview dialog pins PV visible — both paths
    // ignore the pause-hide option entirely — so only the release/clear is
    // accepted unconditionally.
    if (active == state_.pauseDisplayAltHoldActive_) {
        return;
    }
    if (active && (state_.playing_ || state_.exportPreviewActive_)) {
        return;
    }
    state_.pauseDisplayAltHoldActive_ = active;
    applyEffectivePreviewOutlineVariantToCanvas();
    applyPreviewStageMediaRouteVisualSettings();
}

void miacode::runtime::StageMediaHost::setTouchPadAuthoringCtrlHoldActive(bool active)
{
    if (state_.touchPadAuthoringCtrlHoldActive_ == active) {
        return;
    }
    state_.touchPadAuthoringCtrlHoldActive_ = active;
    applyEffectivePreviewOutlineVariantToCanvas();
}

void Session::applyPreviewAudioSettingsFromUi(const PreviewAudioSettings& settings)
{
    state_.previewAudioSettings_ = settings;
    state_.previewAudioSettings_.normalize();
    // Break-slide tail cheer is an app-scoped sound-design choice, not a
    // per-chart mix, so the page's value is what the preference becomes.
    state_.breakSlideTailCheerMutedPreference_ =
        state_.previewAudioSettings_.breakSlideTailCheerMuted;
    applyPreviewAudioSettingsToRuntime();
    // The mixer is project-scoped; savePortableState carries the app-level
    // companions edited from the same page.
    saveProjectAudioPreferences();
    savePortableState();
}

void Session::savePreviewAudioSettingsAsSoftwareDefault()
{
    state_.previewAudioSettings_.normalize();
    state_.softwarePreviewAudioSettings_ = previewAudioSettingsWithBreakSlideTailCheerPreference(
        state_.previewAudioSettings_, state_.breakSlideTailCheerMutedPreference_);
    savePortableState();
}

void Session::restorePreviewAudioSettingsFromSoftwareDefault()
{
    applyPreviewAudioSettingsFromUi(previewAudioSettingsWithBreakSlideTailCheerPreference(
        state_.softwarePreviewAudioSettings_, state_.breakSlideTailCheerMutedPreference_));
}

namespace {

// The percent sliders on the 预览设置 page speak whole percent; the members they
// stand for are 0..1 doubles.
int percentOf(double unitValue)
{
    return qRound(unitValue * 100.0);
}

double unitOfPercent(const QVariant& percent)
{
    return qBound(0.0, percent.toInt() / 100.0, 1.0);
}

// Flow speed is typed, not dragged, so a value arrives raw and has to be put on
// the step grid before anything sees it.
double snappedFlowSpeed(double flowSpeed)
{
    using namespace miacode::preview_gameplay;
    const double steps = qRound((flowSpeed - kPreviewTimingFlowSpeedMin) / kPreviewTimingFlowSpeedStep);
    return qBound(
        kPreviewTimingFlowSpeedMin,
        kPreviewTimingFlowSpeedMin + steps * kPreviewTimingFlowSpeedStep,
        kPreviewTimingFlowSpeedMax);
}

}  // namespace

QVariantMap Session::previewRenderSettings() const
{
    using namespace miacode::preview_video;
    using namespace miacode::preview_gameplay;
    return QVariantMap{
        // 视频
        {QStringLiteral("brightnessOuter"), percentOf(state_.previewBackgroundBrightnessOuter_)},
        {QStringLiteral("brightnessInner"), percentOf(state_.previewBackgroundBrightnessInner_)},
        {QStringLiteral("layoutSquareScale"), percentOf(state_.previewLayoutSquareScale_)},
        {QStringLiteral("layoutSquareScaleMin"), percentOf(kLayoutSquareScaleMin)},
        {QStringLiteral("layoutSquareScaleMax"), percentOf(kLayoutSquareScaleMax)},
        {QStringLiteral("layoutSquareScaleStep"), percentOf(kLayoutSquareScaleStep)},
        {QStringLiteral("scaleMode"), static_cast<int>(state_.previewBackgroundScaleMode_)},
        {QStringLiteral("smoothBrightness"), state_.previewSmoothBrightness_},
        {QStringLiteral("showTimestamp"), state_.previewShowTimestamp_},
        {QStringLiteral("touchPadAuthoringShortcut"), state_.previewTouchPadAuthoringShortcutEnabled_},
        {QStringLiteral("showDebugInfo"), state_.previewShowDebugInfo_},
        {QStringLiteral("forceLabeledJudgeLineWhenPaused"), state_.previewForceLabeledJudgeLineWhenPaused_},
        // 玩法
        {QStringLiteral("tapFlowSpeed"), snappedFlowSpeed(state_.previewTapFlowSpeed_)},
        {QStringLiteral("touchFlowSpeed"), snappedFlowSpeed(state_.previewTouchFlowSpeed_)},
        {QStringLiteral("flowSpeedMin"), kPreviewTimingFlowSpeedMin},
        {QStringLiteral("flowSpeedMax"), kPreviewTimingFlowSpeedMax},
        {QStringLiteral("flowSpeedStep"), kPreviewTimingFlowSpeedStep},
        {QStringLiteral("judgeEffectSlide"), state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay},
        {QStringLiteral("judgeEffectTap"), state_.muriRenderOptions_.showChartReviewTapJudgeOverlay},
        {QStringLiteral("judgeEffectBreak"), state_.muriRenderOptions_.showChartReviewBreakJudgeOverlay},
        {QStringLiteral("judgeEffectTouch"), state_.muriRenderOptions_.showChartReviewTouchJudgeOverlay},
        {QStringLiteral("slideEarlierOnTop"), previewAppearanceValues_.slideEarlierSecondAndTextOnTop},
        {QStringLiteral("centerDisplay"), static_cast<int>(previewAppearanceValues_.centerDisplayMode)},
        {QStringLiteral("tapJudgeTextDistance"), static_cast<int>(previewAppearanceValues_.tapJudgeTextDistance)},
    };
}

void Session::setPreviewRenderSetting(const QString& key, const QVariant& value)
{
    PreviewRuntime* canvas = state_.scene_;
    // Each branch is the Widgets dialog's own handler for that control: the
    // member, then whichever apply call actually moves the picture.
    if (key == QLatin1String("brightnessOuter")) {
        state_.previewBackgroundBrightnessOuter_ = unitOfPercent(value);
        applyPreviewStageMediaRouteVisualSettings();
        if (canvas != nullptr) {
            canvas->setBackgroundBrightnessOuter(state_.previewBackgroundBrightnessOuter_);
        }
    } else if (key == QLatin1String("brightnessInner")) {
        state_.previewBackgroundBrightnessInner_ = unitOfPercent(value);
        if (canvas != nullptr) {
            canvas->setBackgroundBrightnessInner(state_.previewBackgroundBrightnessInner_);
        }
    } else if (key == QLatin1String("layoutSquareScale")) {
        state_.previewLayoutSquareScale_ =
            miacode::preview_video::normalizedLayoutSquareScale(value.toInt() / 100.0);
        applyPreviewStageMediaRouteVisualSettings();
        if (canvas != nullptr) {
            canvas->setLayoutSquareScale(state_.previewLayoutSquareScale_);
        }
    } else if (key == QLatin1String("scaleMode")) {
        const auto mode = static_cast<PreviewBackgroundScaleMode>(value.toInt());
        if (state_.previewBackgroundScaleMode_ == mode) {
            return;
        }
        state_.previewBackgroundScaleMode_ = mode;
        applyPreviewStageMediaRouteVisualSettings();
        if (canvas != nullptr) {
            canvas->setBackgroundScaleMode(mode);
        }
    } else if (key == QLatin1String("smoothBrightness")) {
        state_.previewSmoothBrightness_ = value.toBool();
        if (canvas != nullptr) {
            canvas->setSmoothBrightness(state_.previewSmoothBrightness_);
        }
    } else if (key == QLatin1String("showTimestamp")) {
        state_.previewShowTimestamp_ = value.toBool();
        if (canvas != nullptr) {
            canvas->setShowTimestamp(state_.previewShowTimestamp_);
        }
    } else if (key == QLatin1String("touchPadAuthoringShortcut")) {
        state_.previewTouchPadAuthoringShortcutEnabled_ = value.toBool();
        applyEffectivePreviewOutlineVariantToCanvas();
    } else if (key == QLatin1String("showDebugInfo")) {
        state_.previewShowDebugInfo_ = value.toBool();
        if (canvas != nullptr) {
            canvas->setShowDebugInfo(state_.previewShowDebugInfo_);
        }
    } else if (key == QLatin1String("forceLabeledJudgeLineWhenPaused")) {
        state_.previewForceLabeledJudgeLineWhenPaused_ = value.toBool();
        applyEffectivePreviewOutlineVariantToCanvas();
        applyPreviewStageMediaRouteVisualSettings();
    } else if (key == QLatin1String("tapFlowSpeed")) {
        state_.previewTapFlowSpeed_ = snappedFlowSpeed(value.toDouble());
        if (canvas != nullptr) {
            canvas->setTapFlowSpeed(state_.previewTapFlowSpeed_);
        }
    } else if (key == QLatin1String("touchFlowSpeed")) {
        state_.previewTouchFlowSpeed_ = snappedFlowSpeed(value.toDouble());
        if (canvas != nullptr) {
            canvas->setTouchFlowSpeed(state_.previewTouchFlowSpeed_);
        }
    } else if (key == QLatin1String("judgeEffectSlide")
               || key == QLatin1String("judgeEffectTap")
               || key == QLatin1String("judgeEffectBreak")
               || key == QLatin1String("judgeEffectTouch")) {
        // The four overlays are one control on the page (a multi-pick), and one
        // re-apply covers whichever of them moved.
        bool MuriRenderOptions::*overlay = &MuriRenderOptions::showChartReviewSlideJudgeOverlay;
        if (key == QLatin1String("judgeEffectTap")) {
            overlay = &MuriRenderOptions::showChartReviewTapJudgeOverlay;
        } else if (key == QLatin1String("judgeEffectBreak")) {
            overlay = &MuriRenderOptions::showChartReviewBreakJudgeOverlay;
        } else if (key == QLatin1String("judgeEffectTouch")) {
            overlay = &MuriRenderOptions::showChartReviewTouchJudgeOverlay;
        }
        if (state_.muriRenderOptions_.*overlay == value.toBool()) {
            return;
        }
        state_.muriRenderOptions_.*overlay = value.toBool();
        applyMuriRenderOptions();
    } else if (key == QLatin1String("slideEarlierOnTop")) {
        const bool earlierOnTop = value.toBool();
        if (previewAppearanceValues_.slideEarlierSecondAndTextOnTop == earlierOnTop) {
            return;
        }
        previewAppearanceValues_.slideEarlierSecondAndTextOnTop = earlierOnTop;
        if (canvas != nullptr) {
            canvas->setSlideEarlierSecondAndTextOnTop(earlierOnTop);
        }
    } else if (key == QLatin1String("centerDisplay")) {
        const auto mode = static_cast<miacode::preview_gameplay::CenterDisplayMode>(value.toInt());
        if (previewAppearanceValues_.centerDisplayMode == mode) {
            return;
        }
        previewAppearanceValues_.centerDisplayMode = mode;
        if (canvas != nullptr) {
            canvas->setCenterDisplayMode(mode);
        }
    } else if (key == QLatin1String("tapJudgeTextDistance")) {
        const auto distance = static_cast<PreviewTapJudgeTextDistance>(value.toInt());
        if (previewAppearanceValues_.tapJudgeTextDistance == distance) {
            return;
        }
        previewAppearanceValues_.tapJudgeTextDistance = distance;
        if (canvas != nullptr) {
            canvas->setTapJudgeTextDistance(distance);
        }
    } else {
        // Unknown key: nothing changed, so nothing to persist.
        return;
    }
    savePortableState();
}

void Session::refreshEditorAuthoringContext()
{
    if (stageMedia_ != nullptr) {
        stageMedia_->applyEffectivePreviewOutlineVariantToCanvas();
    }
}

void Session::setTouchPadAuthoringCtrlHold(bool active)
{
    setTouchPadAuthoringCtrlHoldActive(active && editorAuthoringContextActive());
}

void miacode::runtime::StageMediaHost::applyEffectivePreviewOutlineVariantToCanvas()
{
    if (state_.scene_ != nullptr) {
        const bool editableAuthoringContext = session_.editorAuthoringContextActive()
            && !state_.exportPreviewActive_
            && QApplication::activeModalWidget() == nullptr
            && QApplication::activePopupWidget() == nullptr;
        if (!editableAuthoringContext) {
            state_.touchPadAuthoringCtrlHoldActive_ = false;
        }
        const bool forceJudgeAreaWhenPaused =
            state_.previewForceLabeledJudgeLineWhenPaused_ != state_.pauseDisplayAltHoldActive_;
        const bool pausedJudgeAreaView =
            forceJudgeAreaWhenPaused
            && !state_.playing_
            && !state_.exportPreviewActive_;
        const QString customOutlinePath = effectivePreviewCustomOutlinePath();
        const auto outlineImageMode =
            pausedJudgeAreaView && !customOutlinePath.isEmpty()
            ? miacode::preview::runtime::PreviewOutlineImageMode::PausedJudgeAreaComposite
            : miacode::preview::runtime::PreviewOutlineImageMode::Direct;
        state_.scene_->setOutlineSelection(
            effectivePreviewOutlineVariant(),
            customOutlinePath,
            outlineImageMode);
        state_.scene_->setTouchPadAuthoringEnabled(
            state_.touchPadAuthoringCtrlHoldActive_
            && state_.previewTouchPadAuthoringShortcutEnabled_
            && editableAuthoringContext);
    }
}

void miacode::runtime::StageMediaHost::applyPreviewOutlineVariant(
    PreviewOutlineVariant variant,
    bool useAutoSelection,
    bool persistState)
{
    previewAppearanceValues_.outlineVariant = variant;
    state_.previewOutlineVariantUsesAutoSelection_ = useAutoSelection;
    state_.previewCustomOutlineFileName_.clear();
    applyEffectivePreviewOutlineVariantToCanvas();
    if (persistState) {
        session_.savePortableState();
    }
}

QString miacode::runtime::StageMediaHost::resolvePreviewCustomOutlineDir() const
{
    return miacode::assets::customOutlineRootPath();
}

QString miacode::runtime::StageMediaHost::resolvePreviewCustomOutlinePath() const
{
    return miacode::assets::customOutlinePathForFileName(state_.previewCustomOutlineFileName_);
}

QString miacode::runtime::StageMediaHost::effectivePreviewCustomOutlinePath() const
{
    return resolvePreviewCustomOutlinePath();
}

QStringList miacode::runtime::StageMediaHost::availablePreviewCustomOutlineFileNames() const
{
    const QString root = resolvePreviewCustomOutlineDir();
    if (root.isEmpty()) {
        return {};
    }
    const QDir dir(root);
    const QFileInfoList entries = dir.entryInfoList(QStringList{QStringLiteral("*.png")}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    QStringList names;
    names.reserve(entries.size());
    for (const QFileInfo& entry : entries) {
        names.append(entry.fileName());
    }
    return names;
}

void miacode::runtime::StageMediaHost::applyPreviewCustomOutlineFileName(const QString& fileName, bool persistState)
{
    const QString normalized = QFileInfo(fileName.trimmed()).fileName();
    state_.previewCustomOutlineFileName_ = normalized;
    state_.previewOutlineVariantUsesAutoSelection_ = false;
    applyEffectivePreviewOutlineVariantToCanvas();
    if (persistState) {
        session_.savePortableState();
    }
}

Session::PreviewSkinVariant miacode::runtime::StageMediaHost::previewSkinVariantFromStorageValue(const QString& value) const
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QLatin1String("dx")
        || normalized == QLatin1String("skin_dx")
        || normalized == QLatin1String("skindx")
        ? PreviewSkinVariant::Dx
        : PreviewSkinVariant::Standard;
}

QString miacode::runtime::StageMediaHost::previewSkinVariantStorageValue() const
{
    const QString normalized = normalizePreviewSkinDirectoryName(previewAppearanceValues_.skinDirectoryName);
    return normalized.isEmpty()
        ? standardPreviewSkinDirectoryName()
        : normalized;
}

QString miacode::runtime::StageMediaHost::resolvePreviewSkinRootDir() const
{
    return miacode::assets::assetPath(QStringLiteral("skin"));
}

QStringList miacode::runtime::StageMediaHost::availablePreviewSkinDirectoryNames() const
{
    // Stage 4.9d-4a: body moved to runtime::shared — see runtime/Shared.cpp.
    // Qualified because this member shares the free function's name.
    return miacode::runtime::shared::availablePreviewSkinDirectoryNames();
}

QString miacode::runtime::StageMediaHost::previewSkinDisplayName(const QString& directoryName) const
{
    // Stage 4.9d-4a: body moved to runtime::shared — see runtime/Shared.cpp.
    return miacode::runtime::shared::previewSkinDisplayName(directoryName);
}

QString miacode::runtime::StageMediaHost::resolvePreviewSkinDir() const
{
    const QString root = resolvePreviewSkinRootDir();
    if (root.isEmpty()) {
        return QString();
    }

    const QString normalizedSelected = normalizePreviewSkinDirectoryName(previewAppearanceValues_.skinDirectoryName);
    const QString selected = normalizedSelected.isEmpty()
        ? standardPreviewSkinDirectoryName()
        : normalizedSelected;
    const QString selectedDir = QDir(root).filePath(selected);
    if (hasCorePreviewSkinAssets(selectedDir)) {
        return QDir::cleanPath(selectedDir);
    }

    const QStringList fallbackNames{
        standardPreviewSkinDirectoryName(),
        dxPreviewSkinDirectoryName(),
    };
    for (const QString& name : fallbackNames) {
        const QString candidate = QDir(root).filePath(name);
        if (hasCorePreviewSkinAssets(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    const QStringList availableNames = availablePreviewSkinDirectoryNames();
    if (!availableNames.isEmpty()) {
        return QDir::cleanPath(QDir(root).filePath(availableNames.first()));
    }

    return QString();
}

void miacode::runtime::StageMediaHost::applyPreviewAudioSettingsToRuntime()
{
    state_.previewAudioSettings_.normalize();
    applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_, "preview_audio_settings");
    if (state_.previewSfxRuntime_ == nullptr) {
        return;
    }
    // Single level-dispatch entry. The runtime's SFX/track levels are a PURE
    // FUNCTION of the current audio mode, recomputed and pushed here on every
    // dispatch point (chart load, play start, latency page enter/leave, latency
    // slider, audio-settings dialog) — never a snapshot that has to be restored on
    // the way out. While the latency page owns the shared runtime (isOnPage()),
    // the mode is LatencyAudition: the test taps use the page's independent SFX
    // slider and the song keeps its normal effective volume; otherwise the user's
    // real mix is applied verbatim. Because the levels are re-derived from
    // (mode, previewAudioSettings_, latencySfx), a missed page-exit can never
    // linger an audition override into the normal preview — the next dispatch
    // self-corrects from the settled mode. (This replaces the old snapshot/restore
    // + latencySandboxAuditionActive_ gate, which leaked when an exit path skipped
    // the controller's restore.)
    auto* sandbox = session_.latencySandboxController();
    const bool latencyAudition = sandbox != nullptr && sandbox->isOnPage();
    const PreviewAudioSettings levels = latencyAudition
        ? makePreviewLatencyAuditionLevels(state_.previewAudioSettings_, sandbox->sfxVolumePercent())
        : state_.previewAudioSettings_;
    state_.previewSfxRuntime_->applyLevels(levels);
}

void miacode::runtime::StageMediaHost::loadProjectAudioPreferences()
{
    // The mixer is PROJECT-scoped: every chart remembers the volumes and mutes
    // it was last edited with, in <chartDir>/.miacode/preferences.json. A
    // project that has never stored one — a brand-new chart, or one authored
    // before the mixer became project-scoped — starts from the app-level
    // 本地预设 (softwarePreviewAudioSettings_). Seeding is the preset's only
    // role; it is never written back to from here.
    const QJsonObject projectPreferences =
        miacode::project_preferences::load(state_.currentFilePath_);
    const QJsonValue storedAudio =
        projectPreferences.value(QLatin1String(kProjectAudioPreferencesKey));
    const bool projectHasStoredMixer = storedAudio.isObject();
    // Edits made with no project open are carried into the first project that
    // has no mixer of its own (typically: tweak the mix on a new chart, then
    // save it), instead of being thrown away in favor of the preset.
    const bool adoptSessionMixer =
        !projectHasStoredMixer && state_.previewAudioSettingsEditedWithoutProject_;
    PreviewAudioSettings loaded = projectHasStoredMixer
        ? PreviewAudioSettings::fromJson(storedAudio.toObject())
        : (adoptSessionMixer ? state_.previewAudioSettings_ : state_.softwarePreviewAudioSettings_);
    loaded.normalize();
    // break-slide tail cheer stays an APP-scoped preference (it is a sound-design
    // choice, not a per-chart mix), so it wins over whatever the project blob
    // happens to carry in that field.
    state_.previewAudioSettings_ = previewAudioSettingsWithBreakSlideTailCheerPreference(
        loaded, state_.breakSlideTailCheerMutedPreference_);
    state_.previewAudioSettingsEditedWithoutProject_ = false;
    if (adoptSessionMixer) {
        saveProjectAudioPreferences();
    }
    applyPreviewAudioSettingsToRuntime();
}

void miacode::runtime::StageMediaHost::saveProjectAudioPreferences() const
{
    if (state_.currentFilePath_.isEmpty()) {
        // No chart open (or never saved) — there is no project file to write
        // to. The mixer still applies for this session, and is deliberately
        // NOT promoted to an app-level setting: only 保存为本地预设 does that.
        // Flag it so the next project bind adopts these edits (see
        // loadProjectAudioPreferences).
        state_.previewAudioSettingsEditedWithoutProject_ = true;
        return;
    }
    PreviewAudioSettings stored = previewAudioSettingsWithBreakSlideTailCheerPreference(
        state_.previewAudioSettings_, state_.breakSlideTailCheerMutedPreference_);
    stored.normalize();
    QJsonObject projectPreferences = miacode::project_preferences::load(state_.currentFilePath_);
    projectPreferences.insert(QLatin1String(kProjectAudioPreferencesKey), stored.toJson());
    miacode::project_preferences::save(state_.currentFilePath_, projectPreferences);
}

void Session::loadProjectAudioPreferences()
{
    stageMedia_->loadProjectAudioPreferences();
}

void Session::saveProjectAudioPreferences() const
{
    stageMedia_->saveProjectAudioPreferences();
}

void Session::ensurePreviewSfxRuntimePrepared()
{
    stageMedia_->ensurePreviewSfxRuntimePrepared();
}

void Session::schedulePreviewSubsystemWarmup()
{
    stageMedia_->schedulePreviewSubsystemWarmup();
}

void Session::schedulePreviewMediaWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot,
    const QString& chartVideoOverrideSnapshot)
{
    stageMedia_->schedulePreviewMediaWarmup(generation, chartPathSnapshot, trackPathSnapshot, chartVideoOverrideSnapshot);
}

void Session::schedulePreviewSfxWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot,
    const PreviewAudioSettings& audioSettingsSnapshot,
    double playbackRateSnapshot)
{
    stageMedia_->schedulePreviewSfxWarmup(
        generation,
        chartPathSnapshot,
        trackPathSnapshot,
        audioSettingsSnapshot,
        playbackRateSnapshot
    );
}

void Session::applyPreviewMediaWarmupResult(
    quint64 generation,
    const QString& chartPath,
    const QString& resolvedMediaPath,
    const QString& trackPath,
    qint64 workerElapsedMs)
{
    stageMedia_->applyPreviewMediaWarmupResult(
        generation,
        chartPath,
        resolvedMediaPath,
        trackPath,
        workerElapsedMs
    );
}

void Session::applyPreviewSfxWarmupResult(
    quint64 generation,
    const QString& chartPath,
    const QString& trackPath,
    const QString& sfxDir,
    qint64 workerElapsedMs)
{
    stageMedia_->applyPreviewSfxWarmupResult(generation, chartPath, trackPath, sfxDir, workerElapsedMs);
}

QString Session::resolveDefaultTrackPath() const
{
    return stageMedia_->resolveDefaultTrackPath();
}

PreviewOutlineVariant Session::previewOutlineVariantFromStorageValue(const QString& value) const
{
    return stageMedia_->previewOutlineVariantFromStorageValue(value);
}

QString Session::previewOutlineVariantStorageValue() const
{
    return stageMedia_->previewOutlineVariantStorageValue();
}

PreviewOutlineVariant Session::autoPreviewOutlineVariantForChart(const QString& chartPath) const
{
    return stageMedia_->autoPreviewOutlineVariantForChart(chartPath);
}

PreviewOutlineVariant Session::effectivePreviewOutlineVariant() const
{
    return stageMedia_->effectivePreviewOutlineVariant();
}

void Session::applyEffectivePreviewOutlineVariantToCanvas()
{
    stageMedia_->applyEffectivePreviewOutlineVariantToCanvas();
}

void Session::setPauseDisplayAltHoldActive(bool active)
{
    stageMedia_->setPauseDisplayAltHoldActive(active);
}

void Session::setTouchPadAuthoringCtrlHoldActive(bool active)
{
    stageMedia_->setTouchPadAuthoringCtrlHoldActive(active);
}

void Session::applyPreviewOutlineVariant(
    PreviewOutlineVariant variant,
    bool useAutoSelection,
    bool persistState)
{
    stageMedia_->applyPreviewOutlineVariant(variant, useAutoSelection, persistState);
}

QString Session::resolvePreviewCustomOutlinePath() const
{
    return stageMedia_->resolvePreviewCustomOutlinePath();
}

QStringList Session::availablePreviewCustomOutlineFileNames() const
{
    return stageMedia_->availablePreviewCustomOutlineFileNames();
}

void Session::applyPreviewCustomOutlineFileName(const QString& fileName, bool persistState)
{
    stageMedia_->applyPreviewCustomOutlineFileName(fileName, persistState);
}

Session::PreviewSkinVariant Session::previewSkinVariantFromStorageValue(const QString& value) const
{
    return stageMedia_->previewSkinVariantFromStorageValue(value);
}

QString Session::previewSkinVariantStorageValue() const
{
    return stageMedia_->previewSkinVariantStorageValue();
}

QString Session::resolvePreviewSkinDir() const
{
    return stageMedia_->resolvePreviewSkinDir();
}

void Session::applyPreviewSfxLevels(bool reloadAssets)
{
    if (previewSfxRuntime_ == nullptr || !previewSfxRuntime_->audioEngineInitialized()) {
        return;
    }
    if (reloadAssets) {
        previewSfxRuntime_->reloadAssets(previewAudioSettings_);
        return;
    }
    previewSfxRuntime_->applyLevels(previewAudioSettings_);
}

void Session::applyPreviewSkinDirectoryToSurfaces()
{
    const QString skinDir = resolvePreviewSkinDir();
    if (scene_ != nullptr) {
        scene_->setSkinDirectory(skinDir);
    }
    if (timelineQuickStateBridge_ != nullptr) {
        timelineQuickStateBridge_->setSkinDirectory(skinDir);
    }
    emit previewSkinDirectoryChanged();
}

void Session::applyPreviewAudioSettingsToRuntime()
{
    stageMedia_->applyPreviewAudioSettingsToRuntime();
}
