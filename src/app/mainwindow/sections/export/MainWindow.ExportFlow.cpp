#include "MainWindow.ExportSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/video_export/BatchVideoExportDialog.h"
#include "tools/video_export/VideoExportController.h"
#include "tools/video_export/VideoExportDialog.h"

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
        owner_.previewCanvas_->setBackgroundBrightnessOuter(owner_.previewBackgroundBrightnessOuter_);
        owner_.previewCanvas_->setBackgroundBrightnessInner(owner_.previewBackgroundBrightnessInner_);
        owner_.previewCanvas_->setLayoutSquareScale(owner_.previewLayoutSquareScale_);
        owner_.previewCanvas_->setSmoothBrightness(owner_.previewSmoothBrightness_);
        owner_.previewCanvas_->setBackgroundScaleMode(owner_.previewBackgroundScaleMode_);
        owner_.previewCanvas_->setTapFlowSpeed(owner_.previewTapFlowSpeed_);
        owner_.previewCanvas_->setTouchFlowSpeed(owner_.previewTouchFlowSpeed_);
        owner_.previewCanvas_->setSlideEarlierSecondAndTextOnTop(owner_.previewSlideEarlierSecondAndTextOnTop_);
    }

    owner_.saveProjectRenderState();
    owner_.savePortableState();
}

void MainWindow::ExportSection::onExportPreviewVideo()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage(QStringLiteral("当前未选中难度，无法导出视频。"));
        return;
    }
    if (owner_.previewCanvas_ == nullptr) {
        owner_.statusBar()->showMessage(QStringLiteral("预览画布未初始化，无法导出视频。"));
        return;
    }
    if (owner_.qtPreviewPlaying_) {
        owner_.onTogglePreviewPause();
    }

    owner_.refreshTimelineMetadata();

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
    for (const TimelineNoteMarker& marker : owner_.latestTimelineNoteMarkers_) {
        lastMarkerEndSecond = qMax(lastMarkerEndSecond, previewMarkerEndSecond(marker));
    }
    const double cappedExportEndSecond = qMax(
        0.0,
        qMin(owner_.previewDurationSeconds(), lastMarkerEndSecond + 3.0)
    );

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
    task.contentDurationSeconds = cappedExportEndSecond;
    task.fullRangeExport = true;
    task.outputWidth = 1024;
    task.outputHeight = 1024;
    task.fps = 60;
    task.showTimestamp = owner_.previewShowTimestamp_;
    task.showObjectStatsHud = owner_.exportShowObjectStatsHud_;

    const QFileInfo chartInfo(owner_.currentFilePath_);
    QString chartTitle = owner_.document_.title;
    if (owner_.editorStack_ != nullptr && owner_.editorStack_->currentWidget() == owner_.metadataPage_ && owner_.titleEdit_ != nullptr) {
        chartTitle = owner_.titleEdit_->text();
    }
    const QString exportStem = sanitizeExportFileStem(chartTitle, QStringLiteral("out"));
    const QString difficultyName = owner_.hasActiveDifficulty()
        ? SimaiDocument::difficultyShortName(owner_.activeDifficultyId_).replace(':', '_')
        : QStringLiteral("chart");
    const QString outputName = QString("%1_%2.mp4")
        .arg(exportStem)
        .arg(difficultyName);
    task.outputPath = outputName;

    const auto currentPreviewSecond = [this]() -> double {
        return qMax(0.0, owner_.currentPreviewAuthoritativeAudioClockSecond());
    };
    VideoExportDialog dialog(
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
            owner_.saveProjectRenderState();
            owner_.savePortableState();
        },
        [this](bool showObjectStatsHud) {
            owner_.previewShowObjectStatsHud_ = showObjectStatsHud;
            owner_.exportShowObjectStatsHud_ = showObjectStatsHud;
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setShowObjectStatsHud(owner_.previewShowObjectStatsHud_);
            }
            owner_.saveProjectRenderState();
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
            owner_.saveProjectRenderState();
            owner_.savePortableState();
        },
        [this](double scale) {
            owner_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(scale);
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setLayoutSquareScale(owner_.previewLayoutSquareScale_);
            }
            owner_.saveProjectRenderState();
            owner_.savePortableState();
        },
        [this](bool smooth) {
            owner_.previewSmoothBrightness_ = smooth;
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setSmoothBrightness(owner_.previewSmoothBrightness_);
            }
            owner_.saveProjectRenderState();
            owner_.savePortableState();
        },
        [this](PreviewBackgroundScaleMode mode) {
            owner_.previewBackgroundScaleMode_ = mode;
            owner_.applyPreviewStageMediaRouteVisualSettings();
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setBackgroundScaleMode(owner_.previewBackgroundScaleMode_);
            }
            owner_.saveProjectRenderState();
            owner_.savePortableState();
        },
        [this](double flowSpeed) {
            owner_.previewTapFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setTapFlowSpeed(owner_.previewTapFlowSpeed_);
            }
            owner_.saveProjectRenderState();
            owner_.savePortableState();
        },
        [this](double flowSpeed) {
            owner_.previewTouchFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
            if (owner_.previewCanvas_ != nullptr) {
                owner_.previewCanvas_->setTouchFlowSpeed(owner_.previewTouchFlowSpeed_);
            }
            owner_.saveProjectRenderState();
            owner_.savePortableState();
        },
        UiDialogs::effectiveParentWidget(&owner_)
    );
    UiDialogs::prepareDialogWindow(
        &dialog,
        &owner_,
        true,
        UiDialogs::PreviewShortcutPolicy::LocalPlaybackControls
    );

    dialog.adjustSize();
    QRect anchorRect = owner_.geometry();
    bool hasAnchor = false;
    auto mergeGlobalRect = [&anchorRect, &hasAnchor](const QWidget* widget) {
        if (widget == nullptr || !widget->isVisible()) {
            return;
        }
        const QRect local = widget->rect();
        const QRect global(widget->mapToGlobal(local.topLeft()), local.size());
        if (!hasAnchor) {
            anchorRect = global;
            hasAnchor = true;
            return;
        }
        anchorRect = anchorRect.united(global);
    };
    mergeGlobalRect(owner_.outlineList_);
    mergeGlobalRect(owner_.previewLeftColumn_);
    if (!hasAnchor && owner_.workspaceSplitter_ != nullptr && owner_.previewPanel_ != nullptr && owner_.previewPanel_->isVisible()) {
        const QRect splitterRect = owner_.workspaceSplitter_->rect();
        const QRect previewRect = owner_.previewPanel_->geometry();
        const int leftWidth = qMax(1, previewRect.left());
        const QRect localLeftArea(0, 0, leftWidth, splitterRect.height());
        anchorRect = QRect(owner_.workspaceSplitter_->mapToGlobal(localLeftArea.topLeft()), localLeftArea.size());
    }
    if (hasAnchor) {
        const int preferredWidth = qRound(anchorRect.width() * 0.5);
        dialog.resize(qMax(dialog.minimumWidth(), preferredWidth), dialog.height());
    }
    QPoint targetTopLeft(
        anchorRect.center().x() - dialog.width() / 2,
        anchorRect.center().y() - dialog.height() / 2
    );
    QScreen* targetScreen = QGuiApplication::screenAt(anchorRect.center());
    if (targetScreen == nullptr && owner_.windowHandle() != nullptr) {
        targetScreen = owner_.windowHandle()->screen();
    }
    if (targetScreen != nullptr) {
        const QRect avail = targetScreen->availableGeometry();
        targetTopLeft.setX(qBound(avail.left(), targetTopLeft.x(), avail.right() - dialog.width() + 1));
        targetTopLeft.setY(qBound(avail.top(), targetTopLeft.y(), avail.bottom() - dialog.height() + 1));
    }
    dialog.move(targetTopLeft);
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    ++owner_.previewPaneRestoreGeneration_;
    dialog.exec();
    owner_.setPreviewCanvasAspectRatio(1.0, false);
    owner_.restoreSquareAfterVideoExport_ = false;
    if (dialog.exportRequested()) {
        const VideoExportTask requestedTask = dialog.requestedExportTask();
        this->applySharedExportTaskSettings(requestedTask);
        VideoExportSnapshot snapshot;
        QString launchError;
        if (!this->buildVideoExportSnapshot(requestedTask, &snapshot, &launchError)
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

void MainWindow::ExportSection::onBatchExportPreviewVideo()
{
    if (!owner_.hasActiveDifficulty()) {
        owner_.statusBar()->showMessage(uiText("dialog.batch_export.error.no_difficulty", QStringLiteral("No active difficulty is selected.")));
        return;
    }
    if (owner_.previewCanvas_ == nullptr) {
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

    const QString difficultyToken = SimaiDocument::difficultyShortName(owner_.activeDifficultyId_);
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

