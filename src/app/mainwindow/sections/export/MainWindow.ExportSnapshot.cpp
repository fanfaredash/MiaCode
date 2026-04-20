#include "MainWindow.ExportSection.h"
#include "../../MainWindowShared.h"

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
double parsedDocumentFirstSeconds(const QString& rawValue, bool* ok = nullptr)
{
    const QString trimmed = rawValue.trimmed();
    bool localOk = false;
    const double value = trimmed.isEmpty() ? 0.0 : trimmed.toDouble(&localOk);
    if (ok != nullptr) {
        *ok = trimmed.isEmpty() ? true : localOk;
    }
    return (trimmed.isEmpty() || localOk) ? value : 0.0;
}

double shiftedTimelineSecond(double second, double offsetSeconds)
{
    return second + offsetSeconds;
}

QVector<TimelineNoteMarker> shiftedNoteMarkers(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double offsetSeconds)
{
    QVector<TimelineNoteMarker> shifted = noteMarkers;
    for (TimelineNoteMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds);
        if (marker.endSecond >= 0.0) {
            marker.endSecond = shiftedTimelineSecond(marker.endSecond, offsetSeconds);
        }
        if (marker.slideTraceSecond >= 0.0) {
            marker.slideTraceSecond = shiftedTimelineSecond(marker.slideTraceSecond, offsetSeconds);
        }
        if (marker.availableSecond >= 0.0) {
            marker.availableSecond = shiftedTimelineSecond(marker.availableSecond, offsetSeconds);
        }
        for (double& shootSecond : marker.slideSegmentShootSeconds) {
            shootSecond = shiftedTimelineSecond(shootSecond, offsetSeconds);
        }
    }
    return shifted;
}

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

bool MainWindow::ExportSection::buildVideoExportSnapshot(
    const VideoExportTask& requestedTask,
    VideoExportSnapshot* snapshot,
    QString* errorMessage
)
{
    if (snapshot == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export snapshot output is null");
        }
        return false;
    }
    if (!owner_.hasActiveDifficulty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.no_difficulty", "No active difficulty is selected.");
        }
        return false;
    }
    if (!owner_.applyCurrentFieldToDocument()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.sync_failed", "Failed to sync current editor state.");
        }
        return false;
    }

    owner_.refreshTimelineMetadata();
    if (owner_.latestTimelineNoteMarkers_.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.no_markers", "No parsed note markers are available for export.");
        }
        return false;
    }

    VideoExportSnapshot built;
    built.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    built.createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    built.chartTextUtf8 = owner_.document_.toText();
    built.difficultyId = owner_.activeDifficultyId_;
    built.difficultyName = SimaiDocument::difficultyShortName(owner_.activeDifficultyId_);
    built.originalChartPath = owner_.currentFilePath_;
    built.projectDir = owner_.currentFilePath_.isEmpty()
        ? QString()
        : QFileInfo(owner_.currentFilePath_).absolutePath();
    built.trackPath = owner_.resolveDefaultTrackPath();
    built.backgroundMediaPath = miacode::chart_assets::resolveBackgroundMediaPath(owner_.currentFilePath_);
    built.skinDirectory = owner_.resolvePreviewSkinDir();
    built.audioSettings = owner_.previewAudioSettings_;
    built.audioSettings.normalize();
    built.timingSettings = owner_.previewTimingSettings_;
    built.timingSettings.normalize();
    built.backgroundBrightnessOuter = requestedTask.backgroundBrightnessOuter;
    built.backgroundBrightnessInner = requestedTask.backgroundBrightnessInner;
    built.layoutSquareScale = requestedTask.layoutSquareScale;
    built.smoothBrightness = requestedTask.smoothBrightness;
    built.outlineVariant = requestedTask.outlineVariant;
    built.backgroundScaleMode = requestedTask.backgroundScaleMode;
    built.tapFlowSpeed = requestedTask.tapFlowSpeed;
    built.touchFlowSpeed = requestedTask.touchFlowSpeed;
    built.slideEarlierSecondAndTextOnTop = requestedTask.slideEarlierSecondAndTextOnTop;
    built.muriRenderOptions = requestedTask.muriRenderOptions;
    built.staticTapOnSlideThresholdSeconds = requestedTask.staticTapOnSlideThresholdSeconds;
    built.exportStartSeconds = requestedTask.exportStartSeconds;
    built.contentDurationSeconds = requestedTask.contentDurationSeconds;
    built.outputWidth = requestedTask.outputWidth;
    built.outputHeight = requestedTask.outputHeight;
    built.fps = requestedTask.fps;
    built.preset = requestedTask.preset;
    built.fullRangeExport = requestedTask.fullRangeExport;
    const QString exportStem = sanitizeExportFileStem(owner_.document_.title, QStringLiteral("out"));
    const QString difficultyName = SimaiDocument::difficultyShortName(owner_.activeDifficultyId_).replace(':', '_');
    const QString defaultOutputName = QStringLiteral("%1_%2.mp4").arg(exportStem, difficultyName);
    built.outputPath = resolveVideoExportOutputPath(
        requestedTask.outputPath,
        built.projectDir,
        defaultOutputName
    );
    built.showTimestamp = requestedTask.showTimestamp;
    built.showObjectStatsHud = requestedTask.showObjectStatsHud;
    built.skinLoadWaitMs = requestedTask.skinLoadWaitMs;

    if (built.skinDirectory.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.skin_missing", "Preview skin assets were not found.");
        }
        return false;
    }

    *snapshot = built;
    return true;
}

bool MainWindow::ExportSection::buildVideoExportSnapshotForChartDirectory(
    const QString& chartDirectory,
    int difficultyId,
    const QString& difficultyToken,
    const VideoExportTask& requestedTask,
    const QString& outputDirectory,
    VideoExportSnapshot* snapshot,
    QString* errorMessage
)
{
    if (snapshot != nullptr) {
        *snapshot = VideoExportSnapshot();
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QFileInfo directoryInfo(chartDirectory);
    if (!directoryInfo.exists() || !directoryInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.batch_export.error.invalid_folder", "The selected path is not a valid folder.");
        }
        return false;
    }

    const QString chartPath = resolveChartPathFromCliInput(directoryInfo.absoluteFilePath());
    if (chartPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.batch_export.error.missing_chart_file", "Missing majdata.txt (or maidata.txt).");
        }
        return false;
    }

    const QString trackPath = miacode::chart_assets::resolveTrackPathForDirectory(directoryInfo.absoluteFilePath());
    if (trackPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.batch_export.error.missing_track_file", "Missing track.mp3.");
        }
        return false;
    }

    bool usedSystemEncoding = false;
    const QString chartText = readTextFileWithFallbackEncoding(chartPath, &usedSystemEncoding);
    if (chartText.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.read_chart_failed",
                "Failed to read %1."
            ).arg(QFileInfo(chartPath).fileName());
        }
        return false;
    }

    const SimaiDocument document = SimaiDocument::fromText(chartText);
    const SimaiDifficultyData* difficulty = document.difficulty(difficultyId);
    if (difficulty == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.missing_requested_difficulty",
                "Requested difficulty is missing."
            );
        }
        return false;
    }

    const auto validationLocale = UiText::isChineseUi()
        ? SimaiNativeValidationLocale::Chinese
        : SimaiNativeValidationLocale::English;
    const miacode::simai::SimaiTimingMetadata timingMetadata = miacode::simai::buildTimingMetadata(document);
    const SimaiNativeValidationReport report =
        SimaiNativeParser::buildValidationReport(difficulty->chart, validationLocale, nullptr, timingMetadata);
    if (report.errorCount > 0) {
        QString issueSummary;
        if (!report.issues.isEmpty()) {
            issueSummary = report.issues.constFirst().displayMessage.trimmed();
        }
        if (errorMessage != nullptr) {
            *errorMessage = issueSummary.isEmpty()
                ? uiText(
                      "dialog.batch_export.error.validation_failed_count",
                      "Syntax check failed with %1 error(s)."
                  ).arg(report.errorCount)
                : uiText(
                      "dialog.batch_export.error.validation_failed_detail",
                      "Syntax check failed: %1"
                  ).arg(issueSummary);
        }
        return false;
    }

    const SimaiNativeParseResult parsedTimeline = SimaiNativeParser::parseForTimeline(
        difficulty->chart,
        timingMetadata);
    if (parsedTimeline.noteMarkers.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.no_markers",
                "No parsed note markers are available for this difficulty."
            );
        }
        return false;
    }

    const auto markerEndSecond = [](const TimelineNoteMarker& marker) {
        double value = qMax(marker.second, marker.endSecond);
        value = qMax(value, marker.slideTraceSecond);
        value = qMax(value, marker.availableSecond);
        for (double shootSecond : marker.slideSegmentShootSeconds) {
            value = qMax(value, shootSecond);
        }
        return qMax(0.0, value);
    };
    double lastMarkerEndSecond = 0.0;
    bool firstOk = false;
    const double firstSeconds = parsedDocumentFirstSeconds(document.first, &firstOk);
    if (!firstOk) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.invalid_first",
                "Invalid &first value in chart metadata."
            );
        }
        return false;
    }
    const QVector<TimelineNoteMarker> shiftedMarkers = shiftedNoteMarkers(parsedTimeline.noteMarkers, firstSeconds);
    for (const TimelineNoteMarker& marker : shiftedMarkers) {
        lastMarkerEndSecond = qMax(lastMarkerEndSecond, markerEndSecond(marker));
    }
    const double trackDurationSeconds = probeAudioDurationSeconds(trackPath);
    double contentDurationSeconds = qMax(0.0, lastMarkerEndSecond + 3.0);
    if (trackDurationSeconds > 0.0) {
        contentDurationSeconds = qMax(0.0, qMin(trackDurationSeconds, contentDurationSeconds));
    }
    if (contentDurationSeconds <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.invalid_duration",
                "Failed to determine export duration for this chart."
            );
        }
        return false;
    }

    if (snapshot == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export snapshot output is null");
        }
        return false;
    }

    const QString exportStem = sanitizeExportFileStem(document.title, directoryInfo.completeBaseName());
    const QString difficultyName = SimaiDocument::difficultyShortName(difficultyId).replace(':', '_');
    const QString defaultOutputName = QStringLiteral("%1_%2.mp4").arg(exportStem, difficultyName);

    VideoExportSnapshot built;
    built.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    built.createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    built.chartTextUtf8 = chartText;
    built.difficultyId = difficultyId;
    built.difficultyName = difficultyToken;
    built.originalChartPath = chartPath;
    built.projectDir = QFileInfo(chartPath).absolutePath();
    built.trackPath = trackPath;
    built.backgroundMediaPath = miacode::chart_assets::resolveBackgroundMediaPath(chartPath);
    built.skinDirectory = owner_.resolvePreviewSkinDir();
    built.audioSettings = requestedTask.audioSettings;
    built.audioSettings.normalize();
    built.timingSettings = requestedTask.timingSettings;
    built.timingSettings.normalize();
    built.backgroundBrightnessOuter = requestedTask.backgroundBrightnessOuter;
    built.backgroundBrightnessInner = requestedTask.backgroundBrightnessInner;
    built.layoutSquareScale = requestedTask.layoutSquareScale;
    built.smoothBrightness = requestedTask.smoothBrightness;
    built.outlineVariant = requestedTask.outlineVariant;
    built.backgroundScaleMode = requestedTask.backgroundScaleMode;
    built.tapFlowSpeed = requestedTask.tapFlowSpeed;
    built.touchFlowSpeed = requestedTask.touchFlowSpeed;
    built.slideEarlierSecondAndTextOnTop = requestedTask.slideEarlierSecondAndTextOnTop;
    built.muriRenderOptions = requestedTask.muriRenderOptions;
    built.staticTapOnSlideThresholdSeconds = requestedTask.staticTapOnSlideThresholdSeconds;
    built.exportStartSeconds = 0.0;
    built.contentDurationSeconds = contentDurationSeconds;
    built.outputWidth = requestedTask.outputWidth;
    built.outputHeight = requestedTask.outputHeight;
    built.fps = requestedTask.fps;
    built.preset = requestedTask.preset;
    built.fullRangeExport = true;
    built.outputPath = resolveVideoExportOutputPath(
        QString(),
        outputDirectory,
        defaultOutputName
    );
    built.showTimestamp = requestedTask.showTimestamp;
    built.showObjectStatsHud = requestedTask.showObjectStatsHud;
    built.skinLoadWaitMs = requestedTask.skinLoadWaitMs;

    if (built.skinDirectory.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.skin_missing",
                "Preview skin assets were not found."
            );
        }
        return false;
    }

    *snapshot = built;
    return true;
}


bool MainWindow::ExportSection::exportPreviewVideoFromCli(
    const CliVideoExportRequest& request,
    QString* resolvedOutputPath,
    QString* errorMessage,
    QString* details
)
{
    if (resolvedOutputPath != nullptr) {
        resolvedOutputPath->clear();
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (details != nullptr) {
        details->clear();
    }

    const auto fail = [errorMessage, details](const QString& message, const QString& detail = QString()) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        if (details != nullptr) {
            *details = detail;
        }
        return false;
    };

    if (request.outputWidth <= 0 || request.outputHeight <= 0 || request.fps <= 0) {
        return fail(QStringLiteral("output width/height and fps must be positive integers"));
    }
    if (request.outputWidth < request.outputHeight) {
        return fail(QStringLiteral("output size currently requires width >= height"));
    }

    const QString chartPath = resolveChartPathFromCliInput(request.chartPathOrDirectory);
    if (chartPath.isEmpty()) {
        return fail(
            QStringLiteral("cannot resolve chart file from input path"),
            request.chartPathOrDirectory
        );
    }

    bool usedSystemEncoding = false;
    const QString chartText = readTextFileWithFallbackEncoding(chartPath, &usedSystemEncoding);
    if (chartText.isNull()) {
        return fail(QStringLiteral("failed to read chart file"), chartPath);
    }

    owner_.setCurrentFilePath(chartPath);
    owner_.loadDocument(SimaiDocument::fromText(chartText));
    owner_.refreshWaveformCache();

    const int difficultyId = difficultyIdFromCliToken(request.difficulty);
    if (!SimaiDocument::isDifficultyId(difficultyId)) {
        return fail(
            QStringLiteral("invalid difficulty token"),
            QStringLiteral("expected one of: ESY/BAS/ADV/EXP/MAS/REM/UTG or 1..7")
        );
    }

    if (owner_.document_.difficulty(difficultyId) == nullptr) {
        QStringList available;
        const QVector<int> ids = owner_.document_.difficultyIds();
        available.reserve(ids.size());
        for (int id : ids) {
            available.append(SimaiDocument::difficultyShortName(id));
        }
        return fail(
            QStringLiteral("requested difficulty is missing in chart"),
            QStringLiteral("requested=%1 available=%2")
                .arg(SimaiDocument::difficultyShortName(difficultyId))
                .arg(available.join(','))
        );
    }
    if (!owner_.switchToDifficultyField(difficultyId)) {
        return fail(QStringLiteral("failed to switch to requested difficulty"));
    }

    owner_.refreshTimelineMetadata();
    if (owner_.latestTimelineNoteMarkers_.isEmpty()) {
        return fail(QStringLiteral("no parsed note markers for requested difficulty"));
    }

    const QString previewSkinDir = owner_.resolvePreviewSkinDir();
    if (previewSkinDir.trimmed().isEmpty()) {
        return fail(QStringLiteral("preview skin directory is empty"));
    }

    const QFileInfo chartInfo(owner_.currentFilePath_);
    const QString exportStem = sanitizeExportFileStem(owner_.document_.title, QStringLiteral("out"));
    const QString difficultyName = SimaiDocument::difficultyShortName(difficultyId).replace(':', '_');
    const QString defaultOutputName = QString("%1_%2.mp4")
        .arg(exportStem)
        .arg(difficultyName);

    QString outputPath = resolveVideoExportOutputPath(
        request.outputPath,
        chartInfo.absoluteDir().absolutePath(),
        defaultOutputName
    );

    const QFileInfo outputInfo(outputPath);
    const QString outputDirPath = outputInfo.absolutePath();
    if (!outputDirPath.isEmpty() && !QDir(outputDirPath).exists()) {
        if (!QDir().mkpath(outputDirPath)) {
            return fail(QStringLiteral("cannot create output directory"), outputDirPath);
        }
    }

    const double exportStartSeconds = qMax(0.0, request.exportStartSeconds);
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
    const double maxDuration = qMax(0.0, cappedExportEndSecond - exportStartSeconds);
    const double contentDurationSeconds = request.contentDurationSeconds > 0.0
        ? request.contentDurationSeconds
        : maxDuration;
    const bool fullRangeExport =
        exportStartSeconds <= 1e-6
        && exportStartSeconds + contentDurationSeconds + 1e-6 >= cappedExportEndSecond;
    if (contentDurationSeconds <= 0.0) {
        return fail(
            QStringLiteral("content duration is not positive"),
            QStringLiteral("start=%1 total=%2")
                .arg(exportStartSeconds, 0, 'f', 3)
                .arg(cappedExportEndSecond, 0, 'f', 3)
        );
    }

    VideoExportTask task;
    task.chartPath = owner_.currentFilePath_;
    task.trackPath = owner_.resolveDefaultTrackPath();
    task.skinDirectory = previewSkinDir;
    task.noteMarkers = owner_.latestTimelineNoteMarkers_;
    task.muriAnalysisReport = owner_.muriAnalysisReport_;
    task.muriRenderOptions = owner_.muriRenderOptions_;
    task.staticTapOnSlideThresholdSeconds = static_cast<double>(owner_.staticTapOnSlideThresholdMs_) / 1000.0;
    task.audioSettings = owner_.previewAudioSettings_;
    task.timingSettings = owner_.previewTimingSettings_;
    task.backgroundBrightnessOuter = request.backgroundBrightnessOuter;
    task.backgroundBrightnessInner = request.backgroundBrightnessInner;
    task.layoutSquareScale = request.layoutSquareScale;
    task.smoothBrightness = request.smoothBrightness;
    task.outlineVariant = request.outlineVariant;
    task.backgroundScaleMode = request.backgroundScaleMode;
    task.tapFlowSpeed = request.noteFlowSpeed;
    task.touchFlowSpeed = request.noteFlowSpeed;
    task.slideEarlierSecondAndTextOnTop = miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop;
    task.exportStartSeconds = exportStartSeconds;
    task.contentDurationSeconds = contentDurationSeconds;
    task.fullRangeExport = fullRangeExport;
    task.outputWidth = request.outputWidth;
    task.outputHeight = request.outputHeight;
    task.fps = request.fps;
    task.showTimestamp = request.showTimestamp;
    task.showObjectStatsHud = request.showObjectStatsHud;
    task.outputPath = outputPath;

    const VideoExportResult exportResult = VideoExportController::exportFullPreview(task, nullptr);
    if (!exportResult.success) {
        return fail(exportResult.message, exportResult.details);
    }

    if (resolvedOutputPath != nullptr) {
        *resolvedOutputPath = outputPath;
    }
    if (details != nullptr) {
        QStringList detailLines;
        detailLines << QStringLiteral("chart=%1").arg(chartPath);
        detailLines << QStringLiteral("difficulty=%1").arg(SimaiDocument::difficultyShortName(difficultyId));
        detailLines << QStringLiteral("encoding=%1").arg(usedSystemEncoding ? QStringLiteral("system") : QStringLiteral("utf8"));
        detailLines << QStringLiteral("noteCount=%1").arg(owner_.latestTimelineNoteMarkers_.size());
        detailLines << QStringLiteral("trackPath=%1").arg(task.trackPath.isEmpty() ? QStringLiteral("(none)") : task.trackPath);
        detailLines << QStringLiteral("skinLoaded=%1").arg(previewSkinDir.isEmpty() ? 0 : 1);
        *details = detailLines.join('\n');
    }
    return true;
}

