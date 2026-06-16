#include "MainWindow.ExportSection.h"
#include "../../MainWindowShared.h"
#include "../dialogs/MainWindow.DialogsSection.h"
#include "../window/MainWindow.WindowSection.h"

#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/ChartAssetPaths.h"
#include "common/ChartClockCount.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/cover_export/ExportCoverDialog.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/video_export/BatchVideoExportDialog.h"
#include "tools/video_export/VideoExportController.h"
#include "tools/video_export/VideoExportDialog.h"
#include "tools/video_export/VideoExportPreferences.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {
QString sanitizeExportFileStem(QString text, const QString& fallback = QStringLiteral("out"))
{
    text = text.trimmed();
    QString sanitized;
    sanitized.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.isNull() || ch.isLowSurrogate() || ch.isHighSurrogate()) {
            continue;
        }
        if (ch.unicode() < 0x20 || QStringLiteral("<>:\"/\\|?*").contains(ch)) {
            sanitized.append(QLatin1Char('_'));
            continue;
        }
        sanitized.append(ch);
    }
    sanitized = sanitized.trimmed();
    while (!sanitized.isEmpty() && (sanitized.endsWith(QLatin1Char('.')) || sanitized.endsWith(QLatin1Char(' ')))) {
        sanitized.chop(1);
    }
    return sanitized.isEmpty() ? fallback : sanitized;
}

QString appendMp4SuffixIfMissing(QString outputPath)
{
    outputPath = QDir::cleanPath(QDir::fromNativeSeparators(outputPath.trimmed()));
    if (outputPath.isEmpty()) {
        return QString();
    }
    if (!outputPath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) {
        outputPath += QStringLiteral(".mp4");
    }
    return outputPath;
}

QString makeUniqueVideoExportOutputPath(const QString& outputPath)
{
    const QString normalizedPath = appendMp4SuffixIfMissing(outputPath);
    if (normalizedPath.isEmpty()) {
        return QString();
    }

    const QFileInfo outputInfo(normalizedPath);
    const QDir outputDir = outputInfo.absoluteDir();
    QString fileStem = outputInfo.fileName().trimmed();
    if (fileStem.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) {
        fileStem.chop(4);
    }
    if (fileStem.isEmpty()) {
        fileStem = QStringLiteral("out");
    }

    const auto buildCandidatePath = [&outputDir, &fileStem](int duplicateIndex) {
        const QString fileName = duplicateIndex <= 0
            ? QStringLiteral("%1.mp4").arg(fileStem)
            : QStringLiteral("%1(%2).mp4").arg(fileStem).arg(duplicateIndex);
        return QDir::cleanPath(outputDir.filePath(fileName));
    };

    QString candidatePath = buildCandidatePath(0);
    if (!QFileInfo::exists(candidatePath)) {
        return candidatePath;
    }

    for (int duplicateIndex = 1;; ++duplicateIndex) {
        candidatePath = buildCandidatePath(duplicateIndex);
        if (!QFileInfo::exists(candidatePath)) {
            return candidatePath;
        }
    }
}

QString resolveVideoExportOutputPath(
    const QString& requestedOutputPath,
    const QString& defaultDirectory,
    const QString& defaultOutputName
)
{
    const QString baseDirectory = defaultDirectory.trimmed().isEmpty()
        ? QDir::currentPath()
        : QDir::cleanPath(QDir::fromNativeSeparators(defaultDirectory.trimmed()));
    const QString normalizedDefaultName = appendMp4SuffixIfMissing(
        defaultOutputName.trimmed().isEmpty() ? QStringLiteral("out.mp4") : defaultOutputName
    );

    const QString trimmedRequestedOutput = requestedOutputPath.trimmed();
    QString resolvedOutputPath = QDir::fromNativeSeparators(trimmedRequestedOutput);
    if (resolvedOutputPath.isEmpty()) {
        resolvedOutputPath = QDir(baseDirectory).filePath(normalizedDefaultName);
    } else {
        const bool trailingSeparator = trimmedRequestedOutput.endsWith('/') || trimmedRequestedOutput.endsWith('\\');
        const QFileInfo requestedInfo(resolvedOutputPath);
        if (requestedInfo.isRelative()) {
            resolvedOutputPath = QDir(baseDirectory).absoluteFilePath(resolvedOutputPath);
        }

        const QFileInfo absoluteInfo(resolvedOutputPath);
        if ((absoluteInfo.exists() && absoluteInfo.isDir()) || trailingSeparator) {
            const QString outputDirPath = absoluteInfo.exists() && absoluteInfo.isDir()
                ? absoluteInfo.absoluteFilePath()
                : resolvedOutputPath;
            resolvedOutputPath = QDir(outputDirPath).filePath(normalizedDefaultName);
        }
    }

    return makeUniqueVideoExportOutputPath(resolvedOutputPath);
}

int difficultyIdFromCliToken(const QString& rawToken)
{
    const QString token = rawToken.trimmed().toUpper();
    if (token.isEmpty()) {
        return 0;
    }
    bool numericOk = false;
    const int numericId = token.toInt(&numericOk);
    if (numericOk && SimaiDocument::isDifficultyId(numericId)) {
        return numericId;
    }
    for (int id = 1; id <= 7; ++id) {
        if (SimaiDocument::difficultyShortName(id).compare(token, Qt::CaseInsensitive) == 0) {
            return id;
        }
        if (SimaiDocument::difficultyName(id).compare(token, Qt::CaseInsensitive) == 0) {
            return id;
        }
    }
    return 0;
}

QString resolveChartPathFromCliInput(const QString& inputPath)
{
    const QString cleaned = QDir::cleanPath(inputPath.trimmed());
    if (cleaned.isEmpty()) {
        return QString();
    }

    const QFileInfo info(cleaned);
    if (info.isFile()) {
        return info.absoluteFilePath();
    }
    if (!info.isDir()) {
        return QString();
    }

    const QDir dir(info.absoluteFilePath());
    const QStringList preferredNames{
        QStringLiteral("majdata.txt"),
        QStringLiteral("maidata.txt"),
        QStringLiteral("majdata.simai"),
        QStringLiteral("maidata.simai"),
        QStringLiteral("chart.txt"),
        QStringLiteral("chart.simai"),
    };
    for (const QString& name : preferredNames) {
        const QString candidate = dir.filePath(name);
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    QStringList filters;
    filters << QStringLiteral("*.simai") << QStringLiteral("*.txt");
    const QStringList files = dir.entryList(filters, QDir::Files | QDir::Readable, QDir::Name);
    if (!files.isEmpty()) {
        return QDir::cleanPath(dir.filePath(files.constFirst()));
    }
    return QString();
}

QString readTextFileWithFallbackEncoding(const QString& path, bool* usedSystemEncoding)
{
    if (usedSystemEncoding != nullptr) {
        *usedSystemEncoding = false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    const QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(bytes.mid(3));
    }

    QStringDecoder utf8Decoder(QStringConverter::Utf8);
    const QString utf8Text = utf8Decoder.decode(bytes);
    if (!utf8Decoder.hasError()) {
        return utf8Text;
    }

    if (usedSystemEncoding != nullptr) {
        *usedSystemEncoding = true;
    }
    QStringDecoder systemDecoder(QStringConverter::System);
    return systemDecoder.decode(bytes);
}

QString normalizeLanguageToken(QString token)
{
    token = token.trimmed().toLower();
    token.replace('-', '_');
    return token;
}

bool systemLanguagePrefersChinese()
{
    static const bool prefersChinese = []() {
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        for (const QString& language : uiLanguages) {
            const QString token = normalizeLanguageToken(language);
            if (token.startsWith(QStringLiteral("zh"))) {
                return true;
            }
            if (token.startsWith(QStringLiteral("en"))) {
                return false;
            }
        }
        return normalizeLanguageToken(QLocale::system().name()).startsWith(QStringLiteral("zh"));
    }();
    return prefersChinese;
}

QString systemL10n(const QString& en, const QString& zh)
{
    return systemLanguagePrefersChinese() ? zh : en;
}

QString localizeExportWorkerMessageForUiLanguage(const QString& rawMessage)
{
    const QString trimmed = rawMessage.trimmed();
    if (trimmed.isEmpty()) {
        return trimmed;
    }

    static const QRegularExpression renderProgressPattern(
        QStringLiteral("^Rendering frames and encoding\\.\\.\\.\\s+(\\d+)/(\\d+)$")
    );
    const QRegularExpressionMatch renderMatch = renderProgressPattern.match(trimmed);
    if (renderMatch.hasMatch()) {
        return uiText("dialog.video_export.progress.rendering_count", "Rendering frames... %1/%2")
            .arg(renderMatch.captured(1), renderMatch.captured(2));
    }

    if (trimmed == QLatin1String("Preparing SFX track...")) {
        return uiText("dialog.video_export.progress.preparing_audio", "Preparing audio...");
    }
    if (trimmed == QLatin1String("Starting ffmpeg...")) {
        return uiText("dialog.video_export.progress.starting_ffmpeg", "Starting ffmpeg...");
    }
    if (trimmed == QLatin1String("Rendering frames and encoding...")) {
        return uiText("dialog.video_export.progress.rendering", "Rendering frames...");
    }
    if (trimmed == QLatin1String("Finalizing encoded video stream...")) {
        return uiText("dialog.video_export.progress.finalizing_encode", "Finalizing video...");
    }
    if (trimmed == QLatin1String("Repacking MP4 for fast start...")) {
        return uiText("dialog.video_export.progress.repacking", "Finalizing video...");
    }
    if (trimmed == QLatin1String("Collecting export summary...")) {
        return uiText("dialog.video_export.progress.finishing", "Finishing up...");
    }
    if (trimmed == QLatin1String("Export completed.")) {
        return uiText("dialog.video_export.progress.done", "Done.");
    }
    return rawMessage;
}


}  // namespace

void MainWindow::ExportSection::applySharedExportTaskSettings(const VideoExportTask& task)
{
    owner_.previewShowTimestamp_ = task.showTimestamp;
    owner_.previewShowObjectStatsHud_ = task.showObjectStatsHud;
    owner_.exportShowObjectStatsHud_ = task.showObjectStatsHud;
    owner_.previewShowChartInfoHud_ = task.showChartInfoHud;
    owner_.exportShowChartInfoHud_ = task.showChartInfoHud;
    owner_.previewBackgroundBrightnessOuter_ = qBound(0.0, task.backgroundBrightnessOuter, 1.0);
    owner_.previewBackgroundBrightnessInner_ = qBound(0.0, task.backgroundBrightnessInner, 1.0);
    owner_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(task.layoutSquareScale);
    owner_.previewSmoothBrightness_ = task.smoothBrightness;
    owner_.previewBackgroundScaleMode_ = task.backgroundScaleMode;
    owner_.previewTapFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(task.tapFlowSpeed);
    owner_.previewTouchFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(task.touchFlowSpeed);
    owner_.previewSlideEarlierSecondAndTextOnTop_ = task.slideEarlierSecondAndTextOnTop;

    owner_.applyPreviewStageMediaRouteVisualSettings();
    if (owner_.previewCanvas_ != nullptr) {
        owner_.previewCanvas_->setShowTimestamp(owner_.previewShowTimestamp_);
        owner_.previewCanvas_->setShowObjectStatsHud(owner_.previewShowObjectStatsHud_);
        // Chart info HUD is deliberately *not* pushed to the live editor
        // preview here — the persisted value drives the export dialog's
        // initial checkbox state and the next export render, but the
        // editor's normal preview never shows it (debug HUD owns the
        // top-left corner outside the export dialog window).
        owner_.previewCanvas_->setCenterDisplayMode(owner_.previewCenterDisplayMode_);
        owner_.previewCanvas_->setBackgroundBrightnessOuter(owner_.previewBackgroundBrightnessOuter_);
        owner_.previewCanvas_->setBackgroundBrightnessInner(owner_.previewBackgroundBrightnessInner_);
        owner_.previewCanvas_->setLayoutSquareScale(owner_.previewLayoutSquareScale_);
        owner_.previewCanvas_->setSmoothBrightness(owner_.previewSmoothBrightness_);
        owner_.previewCanvas_->setBackgroundScaleMode(owner_.previewBackgroundScaleMode_);
        owner_.previewCanvas_->setTapFlowSpeed(owner_.previewTapFlowSpeed_);
        owner_.previewCanvas_->setTouchFlowSpeed(owner_.previewTouchFlowSpeed_);
        owner_.previewCanvas_->setSlideEarlierSecondAndTextOnTop(owner_.previewSlideEarlierSecondAndTextOnTop_);
    }

    owner_.savePortableState();
}

// Build the seed VideoExportTask shared by the export dialog AND the direct
// cover export: parsed note markers + muri + render settings + skin /
// outline assets + chart metadata + the banner-card payload. Callers must
// have validated the target difficulty / previewCanvas_ and paused playback
// first. difficultyId 0 = active difficulty (the pre-export-page behavior).
VideoExportTask MainWindow::ExportSection::buildVideoExportSeedTask(int difficultyId)
{
    const int resolvedDifficultyId = difficultyId > 0 ? difficultyId : owner_.activeDifficultyId_;
    // The live timeline markers / muri report belong to the ACTIVE difficulty.
    // An export-page launch targeting another difficulty parses that chart
    // directly instead (the same parse the worker re-runs from the snapshot).
    const bool usesActiveTimeline =
        owner_.hasActiveDifficulty() && resolvedDifficultyId == owner_.activeDifficultyId_;

    owner_.refreshTimelineMetadata();

    QVector<TimelineNoteMarker> seedMarkers;
    MuriAnalysisReport seedMuriReport;
    if (usesActiveTimeline) {
        seedMarkers = owner_.latestTimelineNoteMarkers_;
        seedMuriReport = owner_.muriAnalysisReport_;
    } else {
        seedMarkers = buildParsedMarkersForDifficulty(resolvedDifficultyId);
        seedMuriReport = MuriAnalyzer::analyze(
            seedMarkers,
            owner_.muriRenderOptions_,
            static_cast<double>(owner_.staticTapOnSlideThresholdMs_) / 1000.0);
    }

    const auto previewMarkerEndSecond = [](const TimelineNoteMarker& marker) {
        double markerEnd = qMax(marker.second, marker.endSecond);
        markerEnd = qMax(markerEnd, marker.slideTraceSecond);
        markerEnd = qMax(markerEnd, marker.availableSecond);
        for (double shootSecond : marker.slideSegmentShootSeconds) {
            markerEnd = qMax(markerEnd, shootSecond);
        }
        return qMax(0.0, markerEnd);
    };
    double lastMarkerEndSecond = 0.0;
    for (const TimelineNoteMarker& marker : seedMarkers) {
        lastMarkerEndSecond = qMax(lastMarkerEndSecond, previewMarkerEndSecond(marker));
    }
    const double cappedExportEndSecond = qMax(
        0.0,
        qMin(owner_.previewDurationSeconds(), lastMarkerEndSecond + 3.0)
    );

    VideoExportTask task;
    task.chartPath = owner_.currentFilePath_;
    task.trackPath = owner_.resolveDefaultTrackPath();
    // Seed the skin dir so the dialog's "Export Cover" chart-frame renderer can
    // load the note sprites without building a full export snapshot.
    task.skinDirectory = owner_.resolvePreviewSkinDir();
    task.noteMarkers = seedMarkers;
    task.muriAnalysisReport = seedMuriReport;
    task.muriRenderOptions = owner_.muriRenderOptions_;
    task.staticTapOnSlideThresholdSeconds = static_cast<double>(owner_.staticTapOnSlideThresholdMs_) / 1000.0;
    task.audioSettings = owner_.previewAudioSettings_;
    task.timingSettings = owner_.previewTimingSettings_;
    task.backgroundBrightnessOuter = owner_.previewBackgroundBrightnessOuter_;
    task.backgroundBrightnessInner = owner_.previewBackgroundBrightnessInner_;
    task.layoutSquareScale = owner_.previewLayoutSquareScale_;
    task.smoothBrightness = owner_.previewSmoothBrightness_;
    task.outlineVariant = owner_.previewOutlineVariant_;
    // Pair the variant with its custom-outline path (same resolver the real export
    // snapshot uses) so the cover's chart-frame ring matches the video export.
    task.outlineImagePath = owner_.resolvePreviewCustomOutlinePath();
    task.backgroundScaleMode = owner_.previewBackgroundScaleMode_;
    task.tapFlowSpeed = owner_.previewTapFlowSpeed_;
    task.touchFlowSpeed = owner_.previewTouchFlowSpeed_;
    task.slideEarlierSecondAndTextOnTop = owner_.previewSlideEarlierSecondAndTextOnTop_;
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = cappedExportEndSecond;
    task.fullRangeExport = true;
    task.outputWidth = 1024;
    task.outputHeight = 1024;
    task.fps = 60;
        task.showTimestamp = owner_.previewShowTimestamp_;
    task.showObjectStatsHud = owner_.exportShowObjectStatsHud_;
    task.showChartInfoHud = owner_.exportShowChartInfoHud_;
    task.centerDisplayMode = owner_.previewCenterDisplayMode_;

    const QFileInfo chartInfo(owner_.currentFilePath_);
    QString chartTitle = owner_.document_.title;
    if (owner_.editorStack_ != nullptr && owner_.editorStack_->currentWidget() == owner_.metadataPage_ && owner_.titleEdit_ != nullptr) {
        chartTitle = owner_.titleEdit_->text();
    }
    // Per-difficulty designer overrides the document-level &des field when
    // populated; otherwise we fall back so projects using the shared-designer
    // convention still surface a name in the HUD. The trimmed() gate is the
    // export-side fallback contract (feature-index §3) — keep all five sites
    // aligned.
    QString chartDesigner = owner_.document_.designer;
    QString chartDifficultyLabel;
    const SimaiDifficultyData* resolvedDifficulty = owner_.document_.difficulty(resolvedDifficultyId);
    if (resolvedDifficulty != nullptr) {
        if (!resolvedDifficulty->designer.trimmed().isEmpty()) {
            chartDesigner = resolvedDifficulty->designer;
        }
        const QString diffShort = SimaiDocument::difficultyShortName(resolvedDifficultyId);
        const QString diffLevel = resolvedDifficulty->level.trimmed();
        if (!diffShort.isEmpty() || !diffLevel.isEmpty()) {
            chartDifficultyLabel = QStringLiteral("%1 %2")
                .arg(diffShort, diffLevel)
                .trimmed();
        }
    }
    const QString chartArtist = owner_.document_.artist;
    task.chartTitle = chartTitle;
    task.chartArtist = chartArtist;
    task.chartDifficultyLabel = chartDifficultyLabel;
    task.chartDesigner = chartDesigner;
    const QString exportStem = sanitizeExportFileStem(chartTitle, QStringLiteral("out"));
    const QString difficultyName = resolvedDifficulty != nullptr
        ? SimaiDocument::difficultyShortName(resolvedDifficultyId).replace(':', '_')
        : QStringLiteral("chart");
    const QString outputName = QString("%1_%2.mp4")
        .arg(exportStem)
        .arg(difficultyName);
    task.outputPath = outputName;
    // Seed the banner-card payload so the dialog's "Export Cover" can render the
    // difficulty card without building a full export snapshot.
    task.intro = buildIntroBannerSpecForDifficulty(resolvedDifficultyId);
    // Seed clock_count so the dialog's "Enable clock_count (N)" checkbox shows the
    // chart's value; the checkbox then gates whether it reaches the export.
    task.clockCount = miacode::chart_clock::clockCountFromDocument(owner_.document_);
    return task;
}

// MODAL twin of the embedded export panel. Since 2026-06-12 no UI entrance
// reaches it (the Tools-menu 「导出谱面」 jumps to the Export page; spec
// decision D6 overturned) — kept because it exercises the same
// VideoExportDialog in window form and may be rewired later.
void MainWindow::ExportSection::onExportPreviewVideo(int difficultyId)
{
    MC_OP("MainWindow::ExportSection::onExportPreviewVideo");
    const int resolvedDifficultyId = difficultyId > 0 ? difficultyId : owner_.activeDifficultyId_;
    if (!SimaiDocument::isDifficultyId(resolvedDifficultyId)
        || owner_.document_.difficulty(resolvedDifficultyId) == nullptr) {
        _mc_op_.fail(QStringLiteral("no target difficulty"));
        owner_.statusBar()->showMessage(QStringLiteral("当前未选中难度，无法导出视频。"));
        return;
    }
    if (owner_.previewCanvas_ == nullptr) {
        _mc_op_.fail(QStringLiteral("previewCanvas_ null"));
        owner_.statusBar()->showMessage(QStringLiteral("预览画布未初始化，无法导出视频。"));
        return;
    }
    if (owner_.qtPreviewPlaying_) {
        owner_.onTogglePreviewPause();
    }

    VideoExportTask task = buildVideoExportSeedTask(resolvedDifficultyId);

    VideoExportDialog* dialog =
        buildConfiguredVideoExportDialog(task, UiDialogs::effectiveParentWidget(&owner_));
    UiDialogs::prepareDialogWindow(
        dialog,
        &owner_,
        true,
        UiDialogs::PreviewShortcutPolicy::LocalPlaybackControls
    );

    dialog->adjustSize();
    // Center the export dialog on the program window EXCLUDING the preview
    // area, so the live preview stays visible beside it. The preview panel sits
    // on the right, so the non-preview region is the main window rect trimmed
    // to the left of the preview panel.
    QRect anchorRect(owner_.mapToGlobal(QPoint(0, 0)), owner_.size());
    if (owner_.previewPanel_ != nullptr && owner_.previewPanel_->isVisible()) {
        const int previewLeftGlobalX = owner_.previewPanel_->mapToGlobal(QPoint(0, 0)).x();
        const int nonPreviewWidth = previewLeftGlobalX - anchorRect.left();
        if (nonPreviewWidth > dialog->width() / 2) {
            anchorRect.setWidth(nonPreviewWidth);
        }
    }
    QPoint targetTopLeft(
        anchorRect.center().x() - dialog->width() / 2,
        anchorRect.center().y() - dialog->height() / 2
    );
    QScreen* targetScreen = QGuiApplication::screenAt(anchorRect.center());
    if (targetScreen == nullptr && owner_.windowHandle() != nullptr) {
        targetScreen = owner_.windowHandle()->screen();
    }
    if (targetScreen != nullptr) {
        const QRect avail = targetScreen->availableGeometry();
        targetTopLeft.setX(qBound(avail.left(), targetTopLeft.x(), avail.right() - dialog->width() + 1));
        targetTopLeft.setY(qBound(avail.top(), targetTopLeft.y(), avail.bottom() - dialog->height() + 1));
    }
    dialog->move(targetTopLeft);
    owner_.windowSection_->applySystemWindowBackdrop(dialog);
    beginExportPreviewSession(task);
    dialog->exec();
    endExportPreviewSession();
    const bool exportWasRequested = dialog->exportRequested();
    VideoExportTask requestedTask = dialog->requestedExportTask();
    delete dialog;
    dialog = nullptr;
    if (exportWasRequested) {
        // The injected Gameplay/Video-extra controls drive owner_ live rather
        // than baking into the dialog's task, so re-source those fields from
        // owner_ here — the dialog's task snapshot predates the user's edits.
        requestedTask.outlineVariant = owner_.previewOutlineVariant_;
        requestedTask.slideEarlierSecondAndTextOnTop = owner_.previewSlideEarlierSecondAndTextOnTop_;
        requestedTask.centerDisplayMode = owner_.previewCenterDisplayMode_;
        requestedTask.muriRenderOptions = owner_.muriRenderOptions_;
        this->applySharedExportTaskSettings(requestedTask);
        VideoExportSnapshot snapshot;
        QString launchError;
        // Menu-launched exports keep the QProgressDialog (the inline
        // preview-transport progress belongs to the embedded panel path).
        owner_.videoExportUseInlineProgress_ = false;
        if (!this->buildVideoExportSnapshot(requestedTask, &snapshot, &launchError, resolvedDifficultyId)
            || !this->launchVideoExportWorker(snapshot, &launchError)) {
            UiDialogs::showMessageBox(
                QMessageBox::Critical,
                &owner_,
                uiText("dialog.video_export.title", "Export Video"),
                launchError.isEmpty()
                    ? uiText("dialog.video_export.error.launch_failed", "Failed to start background export.")
                    : launchError
            );
        }
    }
}

VideoExportDialog* MainWindow::ExportSection::buildConfiguredVideoExportDialog(
    const VideoExportTask& task,
    QWidget* parent)
{
    const auto currentPreviewSecond = [this]() -> double {
        return qMax(0.0, owner_.currentPreviewAuthoritativeAudioClockSecond());
    };
    auto* dialog = new VideoExportDialog(
        task,
        [this](double second) {
            owner_.seekPreviewToSecond(second, false);
        },
        [this](double second) {
            owner_.startQtPreviewPlayback(second, true);
            owner_.updatePauseButtonAppearance();
        },
        [this]() {
            if (owner_.qtPreviewPlaying_) {
                owner_.pauseQtPreviewPlaybackExact();
                owner_.updatePauseButtonAppearance();
            }
        },
        [this]() -> bool {
            return owner_.qtPreviewPlaying_;
        },
        currentPreviewSecond,
        [this](bool showTimestamp) {
            owner_.previewShowTimestamp_ = showTimestamp;
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setShowTimestamp(owner_.previewShowTimestamp_);
            }
            owner_.savePortableState();
        },
        [this](bool showObjectStatsHud) {
            owner_.previewShowObjectStatsHud_ = showObjectStatsHud;
            owner_.exportShowObjectStatsHud_ = showObjectStatsHud;
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setShowObjectStatsHud(owner_.previewShowObjectStatsHud_);
                owner_.previewCanvas_->setCenterDisplayMode(owner_.previewCenterDisplayMode_);
            }
            owner_.savePortableState();
        },
        [this](bool showChartInfoHud) {
            owner_.previewShowChartInfoHud_ = showChartInfoHud;
            owner_.exportShowChartInfoHud_ = showChartInfoHud;
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setShowChartInfoHud(owner_.previewShowChartInfoHud_);
            }
            owner_.savePortableState();
        },
        [this](double ratio) {
            owner_.setPreviewCanvasAspectRatio(ratio, false);
        },
        [this](double outer, double inner) {
            owner_.previewBackgroundBrightnessOuter_ = qBound(0.0, outer, 1.0);
            owner_.previewBackgroundBrightnessInner_ = qBound(0.0, inner, 1.0);
            owner_.applyPreviewStageMediaRouteVisualSettings();
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setBackgroundBrightnessOuter(owner_.previewBackgroundBrightnessOuter_);
                owner_.previewCanvas_->setBackgroundBrightnessInner(owner_.previewBackgroundBrightnessInner_);
            }
            owner_.savePortableState();
        },
        [this](double scale) {
            owner_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(scale);
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setLayoutSquareScale(owner_.previewLayoutSquareScale_);
            }
            owner_.savePortableState();
        },
        [this](bool smooth) {
            owner_.previewSmoothBrightness_ = smooth;
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setSmoothBrightness(owner_.previewSmoothBrightness_);
            }
            owner_.savePortableState();
        },
        [this](PreviewBackgroundScaleMode mode) {
            owner_.previewBackgroundScaleMode_ = mode;
            owner_.applyPreviewStageMediaRouteVisualSettings();
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setBackgroundScaleMode(owner_.previewBackgroundScaleMode_);
            }
            owner_.savePortableState();
        },
        [this](double flowSpeed) {
            owner_.previewTapFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setTapFlowSpeed(owner_.previewTapFlowSpeed_);
            }
            owner_.savePortableState();
        },
        [this](double flowSpeed) {
            owner_.previewTouchFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setTouchFlowSpeed(owner_.previewTouchFlowSpeed_);
            }
            owner_.savePortableState();
        },
        parent
    );
    // Inject the owner-wired Gameplay controls (skin / judge line / judge
    // effect / slide stack order / center display), built by the DialogsSection
    // so they can reach MainWindow-side data the decoupled dialog can't. They
    // mutate owner_ live; their values are re-sourced into the task when the
    // export is confirmed.
    QWidget* injectedGameplay = nullptr;
    if (owner_.dialogsSection_ != nullptr) {
        owner_.dialogsSection_->buildExportInjectedSettings(dialog, &injectedGameplay);
        dialog->injectOwnerWiredSettings(nullptr, injectedGameplay);
    }
    return dialog;
}

void MainWindow::ExportSection::beginExportPreviewSession(const VideoExportTask& task)
{
    ++owner_.previewPaneRestoreGeneration_;
    // The export dialog/panel drives the (paused) on-screen preview. Force PV/BG
    // visible + the export's chosen outline variant for its lifetime so the
    // user sees the real exported look, ignoring the "暂停时显示判定区" pause-hide option.
    owner_.exportPreviewActive_ = true;
    owner_.applyEffectivePreviewOutlineVariantToCanvas();
    owner_.applyPreviewStageMediaRouteVisualSettings();
    // While the export dialog/panel is up the debug HUD is replaced by
    // the optional chart info HUD — the debug numbers don't reach the
    // exported video anyway, and the user wants to see chart metadata
    // here. Suppress the debug HUD without clearing the persisted
    // showDebugInfo preference so it returns intact afterwards.
    if (owner_.previewCanvas_ != nullptr) {
        owner_.previewCanvas_->setSuppressDebugInfo(true);
        // The seed task already carries the resolved chart metadata (built in
        // buildVideoExportSeedTask).
        owner_.previewCanvas_->setChartInfo(
            task.chartTitle, task.chartArtist, task.chartDifficultyLabel, task.chartDesigner);
        owner_.previewCanvas_->setShowChartInfoHud(owner_.previewShowChartInfoHud_);
    }
}

void MainWindow::ExportSection::endExportPreviewSession()
{
    // Stop + tear down the playable preview audition (no-op for the modal path,
    // which never installs it). Must run before the canvas/aspect restore below.
    teardownExportPreviewAuditionScene();
    if (owner_.previewCanvas_ != nullptr) {
        owner_.previewCanvas_->setSuppressDebugInfo(false);
        owner_.previewCanvas_->setShowChartInfoHud(false);
        owner_.previewCanvas_->setChartInfo(QString(), QString(), QString(), QString());
    }
    // Restore normal paused-preview behaviour — the pause-hide option applies again.
    owner_.exportPreviewActive_ = false;
    owner_.applyEffectivePreviewOutlineVariantToCanvas();
    owner_.applyPreviewStageMediaRouteVisualSettings();
    owner_.setPreviewCanvasAspectRatio(1.0, false);
    owner_.restoreSquareAfterVideoExport_ = false;
}

QWidget* MainWindow::ExportSection::createEmbeddedVideoExportPanel(int difficultyId, QWidget* parent)
{
    destroyEmbeddedVideoExportPanel();
    const int resolvedDifficultyId = difficultyId > 0 ? difficultyId : owner_.activeDifficultyId_;
    if (!SimaiDocument::isDifficultyId(resolvedDifficultyId)
        || owner_.document_.difficulty(resolvedDifficultyId) == nullptr
        || owner_.previewCanvas_ == nullptr) {
        return nullptr;
    }
    if (owner_.qtPreviewPlaying_) {
        owner_.onTogglePreviewPause();
    }

    VideoExportTask task = buildVideoExportSeedTask(resolvedDifficultyId);
    VideoExportDialog* panel = buildConfiguredVideoExportDialog(task, parent);
    panel->setEmbeddedPanelMode(true);
    owner_.embeddedVideoExportPanel_ = panel;
    owner_.embeddedVideoExportDifficultyId_ = resolvedDifficultyId;
    connect(panel, &VideoExportDialog::exportConfirmed, &owner_, [this]() {
        this->handleEmbeddedExportConfirmed();
    });
    connect(panel, &VideoExportDialog::exportCancelRequested, &owner_, [this]() {
        this->cancelVideoExportWorker();
    });
    connect(panel, &VideoExportDialog::clockCountEnabledChanged, &owner_, [this](bool enabled) {
        // WYSIWYG: re-seed the audition count-in to match the export setting. The
        // VALUE / BPM still come from the chart; disabled → 0 ticks. The document's
        // &clock_count= is never touched.
        const SimaiDifficultyData* difficulty =
            owner_.document_.difficulty(owner_.embeddedVideoExportDifficultyId_);
        if (difficulty == nullptr) {
            return;
        }
        owner_.setExportAuditionClockSchedule(
            enabled ? miacode::chart_clock::clockCountFromDocument(owner_.document_) : 0,
            miacode::chart_clock::clockBpmForChart(owner_.document_, difficulty->chart));
    });
    connect(panel, &VideoExportDialog::introPreviewSettingsChanged, &owner_, [this]() {
        owner_.refreshExportIntroState();
    });
    beginExportPreviewSession(task);
    // Install the badge-selected difficulty as a playable preview audition so
    // the right-side transport plays/seeks it like the editor (所见即所导). This
    // also covers badge switches — syncEmbeddedVideoPanel recreates the panel,
    // which re-installs the newly-selected difficulty.
    installExportPreviewAuditionScene(resolvedDifficultyId);
    // Re-entering the video sub-page while an inline-launched export is still
    // rendering: re-arm the cancel affordance on the fresh panel.
    if (owner_.videoExportUseInlineProgress_
        && owner_.videoExportWorkerProcess_ != nullptr
        && owner_.videoExportWorkerProcess_->state() != QProcess::NotRunning) {
        panel->setEmbeddedExportRunning(true);
    }
    return panel;
}

void MainWindow::ExportSection::destroyEmbeddedVideoExportPanel()
{
    if (owner_.embeddedVideoExportPanel_.isNull()) {
        return;
    }
    VideoExportDialog* panel = owner_.embeddedVideoExportPanel_;
    owner_.embeddedVideoExportPanel_.clear();
    owner_.embeddedVideoExportDifficultyId_ = 0;
    panel->finalizeEmbeddedSession();
    panel->hide();
    // deleteLater: this may run from inside the panel's own signal handlers.
    panel->deleteLater();
    endExportPreviewSession();
}

void MainWindow::ExportSection::handleEmbeddedExportConfirmed()
{
    VideoExportDialog* panel = owner_.embeddedVideoExportPanel_;
    if (panel == nullptr) {
        return;
    }
    VideoExportTask requestedTask = panel->requestedExportTask();
    // Same re-source as the modal path: the injected Gameplay/Video-extra
    // controls drive owner_ live rather than baking into the panel's task.
    requestedTask.outlineVariant = owner_.previewOutlineVariant_;
    requestedTask.slideEarlierSecondAndTextOnTop = owner_.previewSlideEarlierSecondAndTextOnTop_;
    requestedTask.centerDisplayMode = owner_.previewCenterDisplayMode_;
    requestedTask.muriRenderOptions = owner_.muriRenderOptions_;
    this->applySharedExportTaskSettings(requestedTask);
    VideoExportSnapshot snapshot;
    QString launchError;
    // Panel-launched exports show progress on the preview-area transport
    // (A3 as amended 2026-06-11) — no QProgressDialog.
    owner_.videoExportUseInlineProgress_ = true;
    if (!this->buildVideoExportSnapshot(
            requestedTask, &snapshot, &launchError, owner_.embeddedVideoExportDifficultyId_)
        || !this->launchVideoExportWorker(snapshot, &launchError)) {
        owner_.videoExportUseInlineProgress_ = false;
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            &owner_,
            uiText("dialog.video_export.title", "Export Video"),
            launchError.isEmpty()
                ? uiText("dialog.video_export.error.launch_failed", "Failed to start background export.")
                : launchError
        );
        return;
    }
    panel->setEmbeddedExportRunning(true);
}

bool MainWindow::currentExportIntroLeadInSpec(IntroBannerSpec* outSpec) const
{
    // The embedded video panel owns the live 片头 settings; the audition reads
    // them at play time so the intro preview always reflects current settings.
    if (embeddedVideoExportPanel_.isNull()
        || !embeddedVideoExportPanel_->isAddIntroActiveForPreview()) {
        return false;
    }
    if (outSpec != nullptr) {
        *outSpec = embeddedVideoExportPanel_->previewIntroSpec();
    }
    return true;
}

void MainWindow::ExportSection::onExportCover(int difficultyId)
{
    MC_OP("MainWindow::ExportSection::onExportCover");
    const int resolvedDifficultyId = difficultyId > 0 ? difficultyId : owner_.activeDifficultyId_;
    if (!SimaiDocument::isDifficultyId(resolvedDifficultyId)
        || owner_.document_.difficulty(resolvedDifficultyId) == nullptr) {
        _mc_op_.fail(QStringLiteral("no target difficulty"));
        owner_.statusBar()->showMessage(QStringLiteral("当前未选中难度，无法导出封面。"));
        return;
    }
    if (owner_.previewCanvas_ == nullptr) {
        _mc_op_.fail(QStringLiteral("previewCanvas_ null"));
        owner_.statusBar()->showMessage(QStringLiteral("预览画布未初始化，无法导出封面。"));
        return;
    }
    if (owner_.qtPreviewPlaying_) {
        owner_.onTogglePreviewPause();
    }

    const VideoExportTask task = buildVideoExportSeedTask(resolvedDifficultyId);

    // Seed size: the video-export dialog's persisted resolution (the cover
    // dialog's own app preferences override it when present).
    const QJsonObject videoPrefs = miacode::video_export::loadDialogPreferences();
    QSize seedSize(videoPrefs.value(QStringLiteral("resolution_width")).toInt(1024),
                   videoPrefs.value(QStringLiteral("resolution_height")).toInt(1024));
    if (seedSize.width() <= 0 || seedSize.height() <= 0) {
        seedSize = QSize(1024, 1024);
    }

    ExportCoverDialog dialog(task, seedSize, UiDialogs::effectiveParentWidget(&owner_));
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // The cover lands next to the chart (the same base the video export resolves
    // relative output paths against).
    const QFileInfo chartInfo(task.chartPath);
    const QString outputDirectory = !chartInfo.absoluteDir().path().isEmpty()
        ? chartInfo.absoluteDir().absolutePath()
        : QDir::currentPath();

    const miacode::cover_export::CoverExportResult result = dialog.exportCover(outputDirectory);
    const QString title = uiText("action.export_cover", QStringLiteral("Export Cover"));
    if (result.success) {
        UiDialogs::showMessageBox(
            QMessageBox::Information,
            &owner_,
            title,
            (UiText::isChineseUi() ? QStringLiteral("封面已导出：\n%1") : QStringLiteral("Cover exported:\n%1"))
                .arg(QDir::toNativeSeparators(result.outputPath))
        );
    } else {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            &owner_,
            title,
            (UiText::isChineseUi() ? QStringLiteral("封面导出失败：\n%1") : QStringLiteral("Cover export failed:\n%1"))
                .arg(result.errorMessage)
        );
    }
}

void MainWindow::ExportSection::onBatchExportPreviewVideo(int difficultyId)
{
    MC_OP("MainWindow::ExportSection::onBatchExportPreviewVideo");
    const int resolvedDifficultyId = difficultyId > 0 ? difficultyId : owner_.activeDifficultyId_;
    if (!SimaiDocument::isDifficultyId(resolvedDifficultyId)
        || owner_.document_.difficulty(resolvedDifficultyId) == nullptr) {
        _mc_op_.fail(QStringLiteral("no target difficulty"));
        owner_.statusBar()->showMessage(uiText("dialog.batch_export.error.no_difficulty", QStringLiteral("No active difficulty is selected.")));
        return;
    }
    if (owner_.previewCanvas_ == nullptr) {
        _mc_op_.fail(QStringLiteral("previewCanvas_ null"));
        owner_.statusBar()->showMessage(uiText("dialog.batch_export.error.no_preview", QStringLiteral("Preview canvas is not initialized.")));
        return;
    }
    if (owner_.videoExportWorkerProcess_ != nullptr && owner_.videoExportWorkerProcess_->state() != QProcess::NotRunning) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            &owner_,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.video_export.error.worker_busy", QStringLiteral("Another export is already running."))
        );
        return;
    }
    if (owner_.qtPreviewPlaying_) {
        owner_.onTogglePreviewPause();
    }

    owner_.refreshTimelineMetadata();

    VideoExportTask task;
    task.chartPath = owner_.currentFilePath_;
    task.trackPath = owner_.resolveDefaultTrackPath();
    task.noteMarkers = owner_.latestTimelineNoteMarkers_;
    task.muriAnalysisReport = owner_.muriAnalysisReport_;
    task.muriRenderOptions = owner_.muriRenderOptions_;
    task.staticTapOnSlideThresholdSeconds = static_cast<double>(owner_.staticTapOnSlideThresholdMs_) / 1000.0;
    task.audioSettings = owner_.previewAudioSettings_;
    task.timingSettings = owner_.previewTimingSettings_;
    task.backgroundBrightnessOuter = owner_.previewBackgroundBrightnessOuter_;
    task.backgroundBrightnessInner = owner_.previewBackgroundBrightnessInner_;
    task.layoutSquareScale = owner_.previewLayoutSquareScale_;
    task.smoothBrightness = owner_.previewSmoothBrightness_;
    task.outlineVariant = owner_.previewOutlineVariant_;
    task.backgroundScaleMode = owner_.previewBackgroundScaleMode_;
    task.tapFlowSpeed = owner_.previewTapFlowSpeed_;
    task.touchFlowSpeed = owner_.previewTouchFlowSpeed_;
    task.slideEarlierSecondAndTextOnTop = owner_.previewSlideEarlierSecondAndTextOnTop_;
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = 0.0;
    task.fullRangeExport = true;
    task.outputWidth = 1024;
    task.outputHeight = 1024;
    task.fps = 60;
    task.showTimestamp = owner_.previewShowTimestamp_;
    task.showObjectStatsHud = owner_.exportShowObjectStatsHud_;
    task.showChartInfoHud = owner_.exportShowChartInfoHud_;
    // Batch export carries the *current* chart's metadata only as a hint
    // for the dialog UI; the per-job chartTitle / chartArtist /
    // chartDifficultyLabel / chartDesigner used by each render is
    // re-derived inside buildVideoExportTaskFromSnapshot from the
    // snapshot's chartTextUtf8 + difficulty id for that job.
    task.chartTitle = owner_.document_.title;
    task.chartArtist = owner_.document_.artist;
    if (const SimaiDifficultyData* difficulty = owner_.document_.difficulty(resolvedDifficultyId);
        difficulty != nullptr) {
        task.chartDesigner = !difficulty->designer.trimmed().isEmpty()
            ? difficulty->designer
            : owner_.document_.designer;
        const QString diffShort = SimaiDocument::difficultyShortName(resolvedDifficultyId);
        const QString diffLevel = difficulty->level.trimmed();
        if (!diffShort.isEmpty() || !diffLevel.isEmpty()) {
            task.chartDifficultyLabel = QStringLiteral("%1 %2")
                .arg(diffShort, diffLevel)
                .trimmed();
        }
    } else {
        task.chartDesigner = owner_.document_.designer;
    }
    task.centerDisplayMode = owner_.previewCenterDisplayMode_;

    const QString difficultyToken = SimaiDocument::difficultyShortName(resolvedDifficultyId);
    BatchVideoExportDialog dialog(
        task,
        difficultyToken,
        [this](const VideoExportTask& sharedTask) {
            this->applySharedExportTaskSettings(sharedTask);
        },
        UiDialogs::effectiveParentWidget(&owner_)
    );
    dialog.adjustSize();
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);
    dialog.exec();
    if (!dialog.exportRequested()) {
        return;
    }

    const QStringList chartDirectories = dialog.selectedChartDirectories();
    const QList<int> selectedDifficultyIds = dialog.selectedDifficultyIds();
    const QString outputDirectory = dialog.outputDirectory();
    const VideoExportTask requestedTask = dialog.requestedTaskTemplate();
    this->applySharedExportTaskSettings(requestedTask);
    if (chartDirectories.isEmpty()) {
        return;
    }
    if (selectedDifficultyIds.isEmpty()) {
        return;
    }
    if (outputDirectory.trimmed().isEmpty()) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            &owner_,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.error.no_output_dir", QStringLiteral("Please choose an output folder."))
        );
        return;
    }
    if (!QDir().mkpath(outputDirectory)) {
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            &owner_,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.error.output_dir_create_failed", QStringLiteral("Failed to create output folder."))
                + QStringLiteral("\n") + QDir::toNativeSeparators(outputDirectory)
        );
        return;
    }

    struct BatchExportJob {
        QString chartDirectory;
        int difficultyId = 0;
        QString difficultyToken;
        QString displayName;
    };
    QStringList failedCharts;
    QVector<BatchExportJob> jobs;
    jobs.reserve(chartDirectories.size() * selectedDifficultyIds.size());
    for (const QString& chartDirectory : chartDirectories) {
        const QFileInfo directoryInfo(chartDirectory);
        const QString folderName = directoryInfo.fileName();
        const QString trackPath = miacode::chart_assets::resolveTrackPathForDirectory(directoryInfo.absoluteFilePath());
        if (trackPath.isEmpty()) {
            failedCharts.append(
                QDir::toNativeSeparators(chartDirectory)
                + QStringLiteral(" - ")
                + uiText("dialog.batch_export.error.missing_track_file", QStringLiteral("Missing track.mp3."))
            );
            continue;
        }
        const QString chartPath = resolveChartPathFromCliInput(directoryInfo.absoluteFilePath());
        if (chartPath.isEmpty()) {
            failedCharts.append(
                QDir::toNativeSeparators(chartDirectory)
                + QStringLiteral(" - ")
                + uiText("dialog.batch_export.error.missing_chart_file", QStringLiteral("Missing majdata.txt (or maidata.txt)."))
            );
            continue;
        }

        bool usedSystemEncoding = false;
        const QString chartText = readTextFileWithFallbackEncoding(chartPath, &usedSystemEncoding);
        if (chartText.isNull()) {
            failedCharts.append(
                QDir::toNativeSeparators(chartDirectory)
                + QStringLiteral(" - ")
                + uiText("dialog.batch_export.error.read_chart_failed", QStringLiteral("Failed to read %1."))
                    .arg(QFileInfo(chartPath).fileName())
            );
            continue;
        }

        const SimaiDocument document = SimaiDocument::fromText(chartText);
        int matchedDifficulties = 0;
        for (int difficultyId : selectedDifficultyIds) {
            if (document.difficulty(difficultyId) == nullptr) {
                continue;
            }
            ++matchedDifficulties;
            const QString token = SimaiDocument::difficultyShortName(difficultyId);
            BatchExportJob job;
            job.chartDirectory = chartDirectory;
            job.difficultyId = difficultyId;
            job.difficultyToken = token;
            job.displayName = QStringLiteral("%1 [%2]").arg(folderName, token);
            jobs.append(job);
        }
        if (matchedDifficulties == 0) {
            const QString requested = [&selectedDifficultyIds]() {
                QStringList names;
                for (int id : selectedDifficultyIds) {
                    names.append(SimaiDocument::difficultyShortName(id));
                }
                return names.join(QStringLiteral(", "));
            }();
            failedCharts.append(
                QDir::toNativeSeparators(chartDirectory)
                + QStringLiteral(" - ")
                + uiText(
                    "dialog.batch_export.error.no_selected_difficulties_in_folder",
                    QStringLiteral("None of the selected difficulties exist in this folder: %1")
                ).arg(requested)
            );
        }
    }

    QProgressDialog progress(
        uiText("dialog.batch_export.progress.preparing", QStringLiteral("Preparing batch export...")),
        systemL10n(QStringLiteral("Cancel"), QStringLiteral("取消")),
        0,
        100,
        &owner_
    );
    progress.setWindowTitle(uiText("dialog.batch_export.title", QStringLiteral("Batch Export")));
    progress.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);
    UiDialogs::configureDialogPreviewShortcuts(&progress);
    owner_.windowSection_->applySystemWindowBackdrop(&progress);
    progress.show();

    QStringList exportedFiles;
    int successCount = 0;
    bool canceled = false;
    const int totalJobs = qMax(1, jobs.size());
    for (int index = 0; index < jobs.size(); ++index) {
        const BatchExportJob& job = jobs.at(index);
        progress.setValue(qRound(static_cast<double>(index) * 100.0 / totalJobs));
        progress.setLabelText(
            uiText("dialog.batch_export.progress.exporting_named", QStringLiteral("Exporting %1/%2\n%3"))
                .arg(index + 1)
                .arg(jobs.size())
                .arg(job.displayName)
        );
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress.wasCanceled()) {
            canceled = true;
            break;
        }

        VideoExportSnapshot snapshot;
        QString validationFailure;
        if (!this->buildVideoExportSnapshotForChartDirectory(
                job.chartDirectory,
                job.difficultyId,
                job.difficultyToken,
                requestedTask,
                outputDirectory,
                &snapshot,
                &validationFailure)) {
            failedCharts.append(job.displayName + QStringLiteral(" - ") + validationFailure);
            continue;
        }

        QString failureText;
        bool canceledThisItem = false;
        const auto updateBatchProgress = [this, &progress, index, totalJobs, &job](int percent, const QString& rawMessage) {
            const int clampedPercent = qBound(0, percent, 100);
            const double overall = (static_cast<double>(index) + static_cast<double>(clampedPercent) / 100.0)
                / static_cast<double>(totalJobs);
            progress.setValue(qBound(0, qRound(overall * 100.0), 100));
            progress.setLabelText(
                uiText("dialog.batch_export.progress.current_item", QStringLiteral("%1\n%2"))
                    .arg(job.displayName)
                    .arg(localizeExportWorkerMessageForUiLanguage(rawMessage))
            );
        };
        if (!this->runVideoExportWorkerSync(snapshot, &progress, &canceledThisItem, &failureText, updateBatchProgress)) {
            if (canceledThisItem) {
                canceled = true;
                break;
            }
            failedCharts.append(job.displayName + QStringLiteral(" - ") + failureText);
            continue;
        }

        ++successCount;
        exportedFiles.append(QFileInfo(snapshot.outputPath).fileName());
    }

    progress.setValue(100);
    progress.hide();

    if (canceled) {
        UiDialogs::showMessageBox(
            QMessageBox::Information,
            &owner_,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.message.canceled", QStringLiteral("Batch export canceled."))
        );
        return;
    }

    if (failedCharts.isEmpty()) {
        QString details = exportedFiles.join(QLatin1Char('\n'));
        if (details.size() > 3000) {
            details = details.left(3000) + QStringLiteral("\n...");
        }
        UiDialogs::showMessageBox(
            QMessageBox::Information,
            &owner_,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.message.success", QStringLiteral("Batch export completed: %1 file(s)."))
                .arg(successCount)
                + (details.isEmpty() ? QString() : QStringLiteral("\n\n") + details)
        );
        return;
    }

    QString details = failedCharts.join(QLatin1Char('\n'));
    if (details.size() > 3000) {
        details = details.left(3000) + QStringLiteral("\n...");
    }
    QString successDetails = exportedFiles.join(QLatin1Char('\n'));
    if (successDetails.size() > 3000) {
        successDetails = successDetails.left(3000) + QStringLiteral("\n...");
    }
    UiDialogs::showMessageBox(
        QMessageBox::Warning,
        &owner_,
        uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
        uiText("dialog.batch_export.message.partial_failed", QStringLiteral("Batch export finished with failures.\nSucceeded: %1\nFailed: %2"))
            .arg(successCount)
            .arg(failedCharts.size())
            + (successDetails.isEmpty() ? QString() : QStringLiteral("\n\n") + uiText("dialog.batch_export.message.output_files", QStringLiteral("Output files:")) + QStringLiteral("\n") + successDetails)
            + QStringLiteral("\n\n") + details
    );
}
