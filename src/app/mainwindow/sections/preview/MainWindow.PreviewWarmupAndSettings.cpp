#include "MainWindow.PreviewSection.h"
#include "../../MainWindowShared.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewSfxAssets.h"
#include "common/AssetPaths.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/scene/PreviewProgressStatsCache.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

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

void MainWindow::PreviewSection::ensurePreviewSfxRuntimePrepared()
{
    if (state_.previewSfxRuntime_ == nullptr || state_.previewSfxRuntimePrepared_) {
        return;
    }
    QElapsedTimer initTimer;
    initTimer.start();
    state_.previewSfxRuntime_->setWarmupResolvedPaths(
        state_.previewSfxWarmupChartPath_,
        state_.previewSfxWarmupTrackPath_,
        state_.previewSfxWarmupSfxDir_
    );
    state_.previewSfxRuntime_->reloadAssets(state_.previewAudioSettings_);
    state_.previewSfxRuntime_->setChartPath(state_.currentFilePath_);
    state_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(state_.previewPlaybackRate_);
    state_.previewSfxRuntimePrepared_ = true;
    const qint64 elapsedMs = initTimer.elapsed();
    appendStartupTimingStage("mainwindow/preview_sfx_runtime_prepare_on_demand", elapsedMs, elapsedMs);
}

void MainWindow::PreviewSection::schedulePreviewSubsystemWarmup()
{
    if (state_.previewWarmupPool_ == nullptr) {
        return;
    }
    const quint64 generation = ++state_.previewWarmupGeneration_;
    PreviewAudioSettings audioSettingsSnapshot = state_.previewAudioSettings_;
    audioSettingsSnapshot.normalize();
    const QString chartPathSnapshot = state_.currentFilePath_;
    const QString trackPathSnapshot = state_.lastTrackPath_;
    const double playbackRateSnapshot = state_.previewPlaybackRate_;
    schedulePreviewMediaWarmup(generation, chartPathSnapshot, trackPathSnapshot);
    schedulePreviewSfxWarmup(generation, chartPathSnapshot, trackPathSnapshot, audioSettingsSnapshot, playbackRateSnapshot);
}

void MainWindow::PreviewSection::schedulePreviewMediaWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot)
{
    if (state_.previewWarmupPool_ == nullptr) {
        return;
    }
    QPointer<MainWindow> guard(&owner_);
    state_.previewWarmupPool_->start([guard, generation, chartPathSnapshot, trackPathSnapshot]() {
        QElapsedTimer timer;
        timer.start();
        PreviewMediaWarmupResult result;
        result.generation = generation;
        result.chartPath = chartPathSnapshot;
        result.trackPath = trackPathSnapshot;
#ifdef HAVE_QT_MULTIMEDIA
        result.resolvedMediaPath = miacode::chart_assets::resolveBackgroundMediaPath(chartPathSnapshot, true);
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

void MainWindow::PreviewSection::schedulePreviewSfxWarmup(
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
    QPointer<MainWindow> guard(&owner_);
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

void MainWindow::PreviewSection::applyPreviewMediaWarmupResult(
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

void MainWindow::PreviewSection::applyPreviewSfxWarmupResult(
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
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setWarmupResolvedPaths(chartPath, trackPath, sfxDir);
    }
}

QString MainWindow::PreviewSection::resolveDefaultTrackPath() const
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

PreviewOutlineVariant MainWindow::PreviewSection::previewOutlineVariantFromStorageValue(const QString& value) const
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

QString MainWindow::PreviewSection::previewOutlineVariantStorageValue() const
{
    switch (state_.previewOutlineVariant_) {
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

PreviewOutlineVariant MainWindow::PreviewSection::autoPreviewOutlineVariantForChart(const QString& chartPath) const
{
    return miacode::chart_assets::hasBackgroundMedia(chartPath)
        ? PreviewOutlineVariant::Line
        : PreviewOutlineVariant::JudgeAreaLabeled;
}

void MainWindow::PreviewSection::applyPreviewOutlineVariant(
    PreviewOutlineVariant variant,
    bool useAutoSelection,
    bool persistState)
{
    state_.previewOutlineVariant_ = variant;
    state_.previewOutlineVariantUsesAutoSelection_ = useAutoSelection;
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setOutlineVariant(state_.previewOutlineVariant_);
    }
    if (persistState) {
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    }
}

MainWindow::PreviewSkinVariant MainWindow::PreviewSection::previewSkinVariantFromStorageValue(const QString& value) const
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QLatin1String("dx")
        || normalized == QLatin1String("skin_dx")
        || normalized == QLatin1String("skindx")
        ? PreviewSkinVariant::Dx
        : PreviewSkinVariant::Standard;
}

QString MainWindow::PreviewSection::previewSkinVariantStorageValue() const
{
    return state_.previewSkinVariant_ == PreviewSkinVariant::Dx
        ? QStringLiteral("dx")
        : QStringLiteral("standard");
}

QString MainWindow::PreviewSection::resolvePreviewSkinDir() const
{
    const QStringList candidateDirs = state_.previewSkinVariant_ == PreviewSkinVariant::Dx
        ? QStringList{QStringLiteral("skinDX"), QStringLiteral("skin")}
        : QStringList{QStringLiteral("skin"), QStringLiteral("skinDX")};
    for (const QString& candidateDirName : candidateDirs) {
        const QString candidateDir = miacode::assets::assetPath(candidateDirName);
        if (QFileInfo::exists(QDir(candidateDir).filePath("tap.png"))) {
            return candidateDir;
        }
    }
    return QString();
}

void MainWindow::PreviewSection::applyPreviewAudioSettingsToRuntime()
{
    state_.previewAudioSettings_.normalize();
    applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_);
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->applyLevels(state_.previewAudioSettings_);
    }
}

void MainWindow::ensurePreviewSfxRuntimePrepared()
{
    previewSection_->ensurePreviewSfxRuntimePrepared();
}

void MainWindow::schedulePreviewSubsystemWarmup()
{
    previewSection_->schedulePreviewSubsystemWarmup();
}

void MainWindow::schedulePreviewMediaWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot)
{
    previewSection_->schedulePreviewMediaWarmup(generation, chartPathSnapshot, trackPathSnapshot);
}

void MainWindow::schedulePreviewSfxWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot,
    const PreviewAudioSettings& audioSettingsSnapshot,
    double playbackRateSnapshot)
{
    previewSection_->schedulePreviewSfxWarmup(
        generation,
        chartPathSnapshot,
        trackPathSnapshot,
        audioSettingsSnapshot,
        playbackRateSnapshot
    );
}

void MainWindow::applyPreviewMediaWarmupResult(
    quint64 generation,
    const QString& chartPath,
    const QString& resolvedMediaPath,
    const QString& trackPath,
    qint64 workerElapsedMs)
{
    previewSection_->applyPreviewMediaWarmupResult(
        generation,
        chartPath,
        resolvedMediaPath,
        trackPath,
        workerElapsedMs
    );
}

void MainWindow::applyPreviewSfxWarmupResult(
    quint64 generation,
    const QString& chartPath,
    const QString& trackPath,
    const QString& sfxDir,
    qint64 workerElapsedMs)
{
    previewSection_->applyPreviewSfxWarmupResult(generation, chartPath, trackPath, sfxDir, workerElapsedMs);
}

QString MainWindow::resolveDefaultTrackPath() const
{
    return previewSection_->resolveDefaultTrackPath();
}

PreviewOutlineVariant MainWindow::previewOutlineVariantFromStorageValue(const QString& value) const
{
    return previewSection_->previewOutlineVariantFromStorageValue(value);
}

QString MainWindow::previewOutlineVariantStorageValue() const
{
    return previewSection_->previewOutlineVariantStorageValue();
}

PreviewOutlineVariant MainWindow::autoPreviewOutlineVariantForChart(const QString& chartPath) const
{
    return previewSection_->autoPreviewOutlineVariantForChart(chartPath);
}

void MainWindow::applyPreviewOutlineVariant(
    PreviewOutlineVariant variant,
    bool useAutoSelection,
    bool persistState)
{
    previewSection_->applyPreviewOutlineVariant(variant, useAutoSelection, persistState);
}

MainWindow::PreviewSkinVariant MainWindow::previewSkinVariantFromStorageValue(const QString& value) const
{
    return previewSection_->previewSkinVariantFromStorageValue(value);
}

QString MainWindow::previewSkinVariantStorageValue() const
{
    return previewSection_->previewSkinVariantStorageValue();
}

QString MainWindow::resolvePreviewSkinDir() const
{
    return previewSection_->resolvePreviewSkinDir();
}

void MainWindow::applyPreviewAudioSettingsToRuntime()
{
    previewSection_->applyPreviewAudioSettingsToRuntime();
}
