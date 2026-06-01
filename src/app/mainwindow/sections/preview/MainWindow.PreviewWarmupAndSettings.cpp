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
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/latency/LatencySandboxController.h"
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

QString standardPreviewSkinDirectoryName()
{
    return QStringLiteral("skinSTD");
}

QString dxPreviewSkinDirectoryName()
{
    return QStringLiteral("skinDX");
}

bool hasCorePreviewSkinAssets(const QString& directory)
{
    if (directory.isEmpty()) {
        return false;
    }
    const QDir dir(directory);
    return QFileInfo::exists(dir.filePath(QStringLiteral("tap.png")))
        && QFileInfo::exists(dir.filePath(QStringLiteral("hold.png")))
        && QFileInfo::exists(dir.filePath(QStringLiteral("star.png")));
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
    state_.previewSfxRuntimePrepared_ = state_.previewSfxRuntime_->audioEngineInitialized();
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
    // Phase 4c — capture &video= override snapshot so the worker
    // pre-resolves the right path (explicit override beats sibling).
    const QString chartVideoOverrideSnapshot = state_.document_.videoPath;
    const double playbackRateSnapshot = state_.previewPlaybackRate_;
    schedulePreviewMediaWarmup(generation, chartPathSnapshot, trackPathSnapshot, chartVideoOverrideSnapshot);
    schedulePreviewSfxWarmup(generation, chartPathSnapshot, trackPathSnapshot, audioSettingsSnapshot, playbackRateSnapshot);
}

void MainWindow::PreviewSection::schedulePreviewMediaWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot,
    const QString& chartVideoOverrideSnapshot)
{
    if (state_.previewWarmupPool_ == nullptr) {
        return;
    }
    QPointer<MainWindow> guard(&owner_);
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
    // Phase 4c — also count `&video=` overrides as "has background"
    // so charts that exclusively rely on the explicit-video path
    // (no sibling bg.* file) get the right outline variant. Without
    // this, a chart with `&video=bg.mp4` but no sibling files would
    // fall through to JudgeAreaLabeled even though the video does
    // render behind the playfield.
    return miacode::chart_assets::hasChartBackgroundMedia(
                chartPath, state_.document_.videoPath)
        ? PreviewOutlineVariant::Line
        : PreviewOutlineVariant::JudgeAreaLabeled;
}

PreviewOutlineVariant MainWindow::PreviewSection::effectivePreviewOutlineVariant() const
{
    // The export-preview dialog ignores the pause-hide option so the on-screen
    // preview matches the exported video (the user's chosen outline variant, PV/BG
    // visible) instead of the paused judge-area view.
    if (state_.previewForceLabeledJudgeLineWhenPaused_
        && !state_.qtPreviewPlaying_
        && !state_.exportPreviewActive_) {
        return PreviewOutlineVariant::JudgeAreaLabeled;
    }
    return state_.previewOutlineVariant_;
}

void MainWindow::PreviewSection::applyEffectivePreviewOutlineVariantToCanvas()
{
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->setOutlineVariant(effectivePreviewOutlineVariant());
        state_.previewCanvas_->setOutlineImagePath(resolvePreviewCustomOutlinePath());
    }
}

void MainWindow::PreviewSection::applyPreviewOutlineVariant(
    PreviewOutlineVariant variant,
    bool useAutoSelection,
    bool persistState)
{
    state_.previewOutlineVariant_ = variant;
    state_.previewOutlineVariantUsesAutoSelection_ = useAutoSelection;
    state_.previewCustomOutlineFileName_.clear();
    applyEffectivePreviewOutlineVariantToCanvas();
    if (persistState) {
        owner_.savePortableState();
    }
}

QString MainWindow::PreviewSection::resolvePreviewCustomOutlineDir() const
{
    return miacode::assets::customOutlineRootPath();
}

QString MainWindow::PreviewSection::resolvePreviewCustomOutlinePath() const
{
    return miacode::assets::customOutlinePathForFileName(state_.previewCustomOutlineFileName_);
}

QStringList MainWindow::PreviewSection::availablePreviewCustomOutlineFileNames() const
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

void MainWindow::PreviewSection::applyPreviewCustomOutlineFileName(const QString& fileName, bool persistState)
{
    const QString normalized = QFileInfo(fileName.trimmed()).fileName();
    state_.previewCustomOutlineFileName_ = normalized;
    state_.previewOutlineVariantUsesAutoSelection_ = false;
    applyEffectivePreviewOutlineVariantToCanvas();
    if (persistState) {
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
    return state_.previewSkinDirectoryName_.trimmed().isEmpty()
        ? standardPreviewSkinDirectoryName()
        : state_.previewSkinDirectoryName_.trimmed();
}

QString MainWindow::PreviewSection::resolvePreviewSkinRootDir() const
{
    return miacode::assets::assetPath(QStringLiteral("skin"));
}

QStringList MainWindow::PreviewSection::availablePreviewSkinDirectoryNames() const
{
    const QString root = resolvePreviewSkinRootDir();
    if (root.isEmpty()) {
        return {};
    }

    QStringList names;
    const QStringList builtInNames{
        standardPreviewSkinDirectoryName(),
        dxPreviewSkinDirectoryName(),
    };
    const QDir rootDir(root);
    for (const QString& name : builtInNames) {
        if (hasCorePreviewSkinAssets(rootDir.filePath(name))) {
            names.append(name);
        }
    }

    const QFileInfoList entries = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries) {
        const QString name = entry.fileName();
        bool isBuiltIn = false;
        for (const QString& builtInName : builtInNames) {
            if (name.compare(builtInName, Qt::CaseInsensitive) == 0) {
                isBuiltIn = true;
                break;
            }
        }
        if (!isBuiltIn && hasCorePreviewSkinAssets(entry.absoluteFilePath())) {
            names.append(name);
        }
    }
    return names;
}

QString MainWindow::PreviewSection::previewSkinDisplayName(const QString& directoryName) const
{
    if (directoryName.compare(standardPreviewSkinDirectoryName(), Qt::CaseInsensitive) == 0) {
        return uiText("dialog.render_settings.video.skin.standard", "Standard");
    }
    if (directoryName.compare(dxPreviewSkinDirectoryName(), Qt::CaseInsensitive) == 0) {
        return uiText("dialog.render_settings.video.skin.dx", "DX");
    }
    return directoryName;
}

QString MainWindow::PreviewSection::resolvePreviewSkinDir() const
{
    const QString root = resolvePreviewSkinRootDir();
    if (root.isEmpty()) {
        return QString();
    }

    const QString selected = state_.previewSkinDirectoryName_.trimmed().isEmpty()
        ? standardPreviewSkinDirectoryName()
        : state_.previewSkinDirectoryName_.trimmed();
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

void MainWindow::PreviewSection::applyPreviewAudioSettingsToRuntime()
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
    auto* sandbox = owner_.latencySandboxController();
    const bool latencyAudition = sandbox != nullptr && sandbox->isOnPage();
    const PreviewAudioSettings levels = latencyAudition
        ? makePreviewLatencyAuditionLevels(state_.previewAudioSettings_, sandbox->sfxVolumePercent())
        : state_.previewAudioSettings_;
    state_.previewSfxRuntime_->applyLevels(levels);
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
    const QString& trackPathSnapshot,
    const QString& chartVideoOverrideSnapshot)
{
    previewSection_->schedulePreviewMediaWarmup(generation, chartPathSnapshot, trackPathSnapshot, chartVideoOverrideSnapshot);
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

PreviewOutlineVariant MainWindow::effectivePreviewOutlineVariant() const
{
    return previewSection_->effectivePreviewOutlineVariant();
}

void MainWindow::applyEffectivePreviewOutlineVariantToCanvas()
{
    previewSection_->applyEffectivePreviewOutlineVariantToCanvas();
}

void MainWindow::applyPreviewOutlineVariant(
    PreviewOutlineVariant variant,
    bool useAutoSelection,
    bool persistState)
{
    previewSection_->applyPreviewOutlineVariant(variant, useAutoSelection, persistState);
}

QString MainWindow::resolvePreviewCustomOutlineDir() const
{
    return previewSection_->resolvePreviewCustomOutlineDir();
}

QString MainWindow::resolvePreviewCustomOutlinePath() const
{
    return previewSection_->resolvePreviewCustomOutlinePath();
}

QStringList MainWindow::availablePreviewCustomOutlineFileNames() const
{
    return previewSection_->availablePreviewCustomOutlineFileNames();
}

void MainWindow::applyPreviewCustomOutlineFileName(const QString& fileName, bool persistState)
{
    previewSection_->applyPreviewCustomOutlineFileName(fileName, persistState);
}

MainWindow::PreviewSkinVariant MainWindow::previewSkinVariantFromStorageValue(const QString& value) const
{
    return previewSection_->previewSkinVariantFromStorageValue(value);
}

QString MainWindow::previewSkinVariantStorageValue() const
{
    return previewSection_->previewSkinVariantStorageValue();
}

QStringList MainWindow::availablePreviewSkinDirectoryNames() const
{
    return previewSection_->availablePreviewSkinDirectoryNames();
}

QString MainWindow::previewSkinDisplayName(const QString& directoryName) const
{
    return previewSection_->previewSkinDisplayName(directoryName);
}

QString MainWindow::resolvePreviewSkinDir() const
{
    return previewSection_->resolvePreviewSkinDir();
}

QString MainWindow::resolvePreviewSkinRootDir() const
{
    return previewSection_->resolvePreviewSkinRootDir();
}

void MainWindow::applyPreviewAudioSettingsToRuntime()
{
    previewSection_->applyPreviewAudioSettingsToRuntime();
}
