#include "runtime/export/VideoExportHost.h"
#include "runtime/Shared.h"
#include "runtime/media/MediaJobsHost.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/shell/ShellHost.h"

#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "common/ChartAssetPaths.h"
#include "common/ChartClockCount.h"
#include "common/ContentDurationConfig.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/PreviewSfxAssets.h"
#include "common/UiHangWatchdog.h"
#include "preview/runtime/PreviewRuntime.h"
#include "app/qml_ui/export/QmlExportSession.h"
#include "app/qml_ui/export/QmlExportSession.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/video_export/VideoExportController.h"
#include "tools/video_export/VideoExportPreferences.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::runtime::shared;

namespace {
QString exportFlowWidgetSummary(QWidget* widget)
{
    if (widget == nullptr) {
        return QStringLiteral("(null)");
    }
    return QStringLiteral("class=%1 name=%2 size=%3x%4 visible=%5")
        .arg(QString::fromUtf8(widget->metaObject()->className()))
        .arg(widget->objectName().isEmpty() ? QStringLiteral("(empty)") : widget->objectName())
        .arg(widget->width())
        .arg(widget->height())
        .arg(widget->isVisible() ? 1 : 0);
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
        return UiText::text(QStringLiteral("dialog.video_export.progress.rendering_count"))
            .arg(renderMatch.captured(1), renderMatch.captured(2));
    }

    if (trimmed == QLatin1String("Preparing SFX track...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.preparing_audio"));
    }
    if (trimmed == QLatin1String("Starting ffmpeg...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.starting_ffmpeg"));
    }
    if (trimmed == QLatin1String("Rendering frames and encoding...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.rendering"));
    }
    if (trimmed == QLatin1String("Finalizing encoded video stream...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.finalizing_encode"));
    }
    if (trimmed == QLatin1String("Repacking MP4 for fast start...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.repacking"));
    }
    if (trimmed == QLatin1String("Collecting export summary...")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.finishing"));
    }
    if (trimmed == QLatin1String("Export completed.")) {
        return UiText::text(QStringLiteral("dialog.video_export.progress.done"));
    }
    return rawMessage;
}


}  // namespace

void miacode::runtime::VideoExportHost::applySharedExportTaskSettings(const VideoExportTask& task)
{
    session_.previewShowTimestamp_ = task.showTimestamp;
    session_.previewShowObjectStatsHud_ = task.showObjectStatsHud;
    session_.exportShowObjectStatsHud_ = task.showObjectStatsHud;
    session_.previewShowChartInfoHud_ = task.showChartInfoHud;
    session_.exportShowChartInfoHud_ = task.showChartInfoHud;
    session_.previewBackgroundBrightnessOuter_ = qBound(0.0, task.backgroundBrightnessOuter, 1.0);
    session_.previewBackgroundBrightnessInner_ = qBound(0.0, task.backgroundBrightnessInner, 1.0);
    session_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(task.layoutSquareScale);
    session_.previewSmoothBrightness_ = task.smoothBrightness;
    session_.previewBackgroundScaleMode_ = task.backgroundScaleMode;
    session_.previewTapFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(task.tapFlowSpeed);
    session_.previewTouchFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(task.touchFlowSpeed);
    session_.previewSlideEarlierSecondAndTextOnTop_ = task.slideEarlierSecondAndTextOnTop;
    session_.previewTapJudgeTextDistance_ = task.tapJudgeTextDistance;
    session_.previewJudgeEffectStyle_ = task.judgeEffectStyle;

    session_.applyPreviewStageMediaRouteVisualSettings();
    if (session_.scene_ != nullptr) {
        session_.scene_->setShowTimestamp(session_.previewShowTimestamp_);
        session_.scene_->setShowObjectStatsHud(session_.previewShowObjectStatsHud_);
        // Chart info HUD is deliberately *not* pushed to the live editor
        // preview here — the persisted value drives the export dialog's
        // initial checkbox state and the next export render, but the
        // editor's normal preview never shows it (debug HUD owns the
        // top-left corner outside the export dialog window).
        session_.scene_->setCenterDisplayMode(session_.previewCenterDisplayMode_);
        session_.scene_->setBackgroundBrightnessOuter(session_.previewBackgroundBrightnessOuter_);
        session_.scene_->setBackgroundBrightnessInner(session_.previewBackgroundBrightnessInner_);
        session_.scene_->setLayoutSquareScale(session_.previewLayoutSquareScale_);
        session_.scene_->setSmoothBrightness(session_.previewSmoothBrightness_);
        session_.scene_->setBackgroundScaleMode(session_.previewBackgroundScaleMode_);
        session_.scene_->setTapFlowSpeed(session_.previewTapFlowSpeed_);
        session_.scene_->setTouchFlowSpeed(session_.previewTouchFlowSpeed_);
        session_.scene_->setSlideEarlierSecondAndTextOnTop(session_.previewSlideEarlierSecondAndTextOnTop_);
        session_.scene_->setTapJudgeTextDistance(session_.previewTapJudgeTextDistance_);
        session_.scene_->setJudgeEffectStyle(session_.previewJudgeEffectStyle_);
    }

    session_.savePortableState();
}

// Build the seed VideoExportTask shared by the export dialog AND the direct
// cover export: parsed note markers + muri + render settings + skin /
// outline assets + chart metadata + the banner-card payload. Callers must
// have validated the target difficulty / scene_ and paused playback
// first. difficultyId 0 = active difficulty (the pre-export-page behavior).
VideoExportTask miacode::runtime::VideoExportHost::buildVideoExportSeedTask(int difficultyId)
{
    const int resolvedDifficultyId = difficultyId > 0 ? difficultyId : session_.activeDifficultyId_;
    // The live timeline markers / muri report belong to the ACTIVE difficulty.
    // An export-page launch targeting another difficulty parses that chart
    // directly instead (the same parse the worker re-runs from the snapshot).
    const bool usesActiveTimeline =
        session_.hasActiveDifficulty() && resolvedDifficultyId == session_.activeDifficultyId_;

    session_.refreshTimelineMetadata();

    QVector<TimelineNoteMarker> seedMarkers;
    MuriAnalysisReport seedMuriReport;
    if (usesActiveTimeline) {
        seedMarkers = session_.latestTimelineNoteMarkers_;
        seedMuriReport = session_.muriAnalysisReport_;
    } else {
        seedMarkers = buildParsedMarkersForDifficulty(resolvedDifficultyId);
        seedMuriReport = MuriAnalyzer::analyze(
            seedMarkers,
            session_.muriRenderOptions_,
            static_cast<double>(session_.staticTapOnSlideThresholdMs_) / 1000.0);
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
    // Unified content-duration policy = max(chartEnd + tail, music).
    const double unifiedExportEndSecond = miacode::content_duration::totalContentDurationSeconds(
        lastMarkerEndSecond, session_.previewTrackDurationSeconds_);

    VideoExportTask task;
    task.chartPath = session_.currentFilePath_;
    task.trackPath = session_.resolveDefaultTrackPath();
    // Seed the skin dir so the dialog's "Export Cover" chart-frame renderer can
    // load the note sprites without building a full export snapshot.
    task.skinDirectory = session_.resolvePreviewSkinDir();
    task.noteMarkers = seedMarkers;
    task.muriAnalysisReport = seedMuriReport;
    task.muriRenderOptions = session_.muriRenderOptions_;
    task.staticTapOnSlideThresholdSeconds = static_cast<double>(session_.staticTapOnSlideThresholdMs_) / 1000.0;
    task.audioSettings = session_.previewAudioSettings_;
    task.timingSettings = session_.previewTimingSettings_;
    task.introSoundFileName = session_.previewIntroSoundFileName_;
    task.backgroundBrightnessOuter = session_.previewBackgroundBrightnessOuter_;
    task.backgroundBrightnessInner = session_.previewBackgroundBrightnessInner_;
    task.layoutSquareScale = session_.previewLayoutSquareScale_;
    task.smoothBrightness = session_.previewSmoothBrightness_;
    task.outlineVariant = session_.previewOutlineVariant_;
    // Pair the variant with its custom-outline path (same resolver the real export
    // snapshot uses) so the cover's chart-frame ring matches the video export.
    task.outlineImagePath = session_.resolvePreviewCustomOutlinePath();
    task.backgroundScaleMode = session_.previewBackgroundScaleMode_;
    task.tapFlowSpeed = session_.previewTapFlowSpeed_;
    task.touchFlowSpeed = session_.previewTouchFlowSpeed_;
    task.slideEarlierSecondAndTextOnTop = session_.previewSlideEarlierSecondAndTextOnTop_;
    task.tapJudgeTextDistance = session_.previewTapJudgeTextDistance_;
    task.judgeEffectStyle = session_.previewJudgeEffectStyle_;
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = unifiedExportEndSecond;
    task.fullRangeExport = true;
    task.outputWidth = 1024;
    task.outputHeight = 1024;
    task.fps = 60;
        task.showTimestamp = session_.previewShowTimestamp_;
    task.showObjectStatsHud = session_.exportShowObjectStatsHud_;
    task.showChartInfoHud = session_.exportShowChartInfoHud_;
    task.centerDisplayMode = session_.previewCenterDisplayMode_;

    const QFileInfo chartInfo(session_.currentFilePath_);
    QString chartTitle = session_.applicationServices_.workspace().document().title;
    if (session_.titleEdit_ != nullptr) {
        chartTitle = session_.titleEdit_->text();
    }
    // Per-difficulty designer overrides the document-level &des field when
    // populated; otherwise we fall back so projects using the shared-designer
    // convention still surface a name in the HUD. The trimmed() gate is the
    // export-side fallback contract (feature-index §3) — keep all five sites
    // aligned.
    QString chartDesigner = session_.applicationServices_.workspace().document().designer;
    QString chartDifficultyLabel;
    const SimaiDifficultyData* resolvedDifficulty = session_.applicationServices_.workspace().document().difficulty(resolvedDifficultyId);
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
    const QString chartArtist = session_.applicationServices_.workspace().document().artist;
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
    task.clockCount = miacode::chart_clock::clockCountFromDocument(session_.applicationServices_.workspace().document());
    return task;
}

void miacode::runtime::VideoExportHost::beginExportPreviewSession(const VideoExportTask& task)
{
    ++session_.previewPaneRestoreGeneration_;
    // The export dialog/panel drives the (paused) on-screen preview. Force PV/BG
    // visible + the export's chosen outline variant for its lifetime so the
    // user sees the real exported look, ignoring the "暂停时显示判定区" pause-hide option.
    session_.exportPreviewActive_ = true;
    session_.applyEffectivePreviewOutlineVariantToCanvas();
    session_.applyPreviewStageMediaRouteVisualSettings();
    // While the export dialog/panel is up the debug HUD is replaced by
    // the optional chart info HUD — the debug numbers don't reach the
    // exported video anyway, and the user wants to see chart metadata
    // here. Suppress the debug HUD without clearing the persisted
    // showDebugInfo preference so it returns intact afterwards.
    if (session_.scene_ != nullptr) {
        session_.scene_->setSuppressDebugInfo(true);
        applyExportPreviewChartInfo(task);
    }
}

void miacode::runtime::VideoExportHost::applyExportPreviewChartInfo(const VideoExportTask& task)
{
    if (session_.scene_ == nullptr) {
        return;
    }
    // The seed task already carries the resolved chart metadata (built in
    // buildVideoExportSeedTask).
    session_.scene_->setChartInfo(
        task.chartTitle, task.chartArtist, task.chartDifficultyLabel, task.chartDesigner);
    session_.scene_->setShowChartInfoHud(session_.previewShowChartInfoHud_);
}

void miacode::runtime::VideoExportHost::endExportPreviewSession()
{
    // Stop + tear down the playable preview audition (no-op for the modal path,
    // which never installs it). Must run before the canvas/aspect restore below.
    teardownExportPreviewAuditionScene();
    if (session_.scene_ != nullptr) {
        session_.scene_->setSuppressDebugInfo(false);
        session_.scene_->setFixHudTextLayout(false);
        session_.scene_->setShowChartInfoHud(false);
        session_.scene_->setChartInfo(QString(), QString(), QString(), QString());
    }
    // Restore normal paused-preview behaviour — the pause-hide option applies again.
    session_.exportPreviewActive_ = false;
    session_.applyEffectivePreviewOutlineVariantToCanvas();
    session_.applyPreviewStageMediaRouteVisualSettings();
    session_.setPreviewCanvasAspectRatio(1.0, false);
    session_.restoreSquareAfterVideoExport_ = false;
}

// Opens the pure-QML export centre on its single-export tab. Extensions reach
// this through "export.video.start" / "export/startVideoExport"; there is no
// modal export dialog behind it any more.
void miacode::runtime::VideoExportHost::onExportPreviewVideo(int difficultyId)
{
    MC_OP("miacode::runtime::VideoExportHost::onExportPreviewVideo");
    // Select the tab BEFORE switching: switchToExportField defers the page
    // build one event-loop tick, so a tab set afterwards would race it.
    if (session_.qmlExportSession_ != nullptr) {
        session_.qmlExportSession_->setActiveTab(QStringLiteral("export"));
    }
    if (session_.documents_ == nullptr || !session_.documents_->switchToExportField()) {
        _mc_op_.fail(QStringLiteral("export field unavailable"));
        return;
    }
    // The page seeds itself from the difficulty that was active on entry, which
    // is what the caller resolved in every reachable case. Honour an explicit
    // request anyway, queued behind the deferred page build.
    if (!SimaiDocument::isDifficultyId(difficultyId)) {
        return;
    }
    QTimer::singleShot(0, &session_, [this, difficultyId]() {
        if (session_.qmlExportSession_ != nullptr) {
            session_.qmlExportSession_->selectDifficulty(difficultyId);
        }
    });
}

void miacode::runtime::VideoExportHost::onBatchExportPreviewVideo(int difficultyId)
{
    MC_OP("miacode::runtime::VideoExportHost::onBatchExportPreviewVideo");
    Q_UNUSED(difficultyId);
    // The Tools menu follows the same embedded page route as clicking the
    // Batch Export sub-nav. It deliberately never constructs a modal dialog.
    // Select the tab BEFORE switching: switchToExportField defers the page
    // build one event-loop tick, so a tab set afterwards would race it.
    if (session_.qmlExportSession_ != nullptr) {
        session_.qmlExportSession_->setActiveTab(QStringLiteral("batch"));
    }
    if (session_.documents_ != nullptr) {
        session_.documents_->switchToExportField();
    }
}

bool miacode::runtime::VideoExportHost::runBatchExport(
    const VideoExportTask& templateTask,
    const QStringList& chartDirectories,
    const QList<int>& selectedDifficultyIds,
    const QString& outputDirectory,
    BatchExportResult* result,
    const BatchExportCallbacks& callbacks,
    QString* errorMessage)
{
    if (result == nullptr) {
        return false;
    }
    *result = BatchExportResult{};
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (chartDirectories.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.no_chart_dirs"));
        }
        return false;
    }
    if (selectedDifficultyIds.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.no_difficulties"));
        }
        return false;
    }
    if (outputDirectory.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.no_output_dir"));
        }
        return false;
    }
    if (!QDir().mkpath(outputDirectory)) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("dialog.batch_export.error.output_dir_create_failed"))
                + QStringLiteral("\n") + QDir::toNativeSeparators(outputDirectory);
        }
        return false;
    }

    struct BatchExportJob {
        QString chartDirectory;
        int difficultyId = 0;
        QString difficultyToken;
        QString displayName;
    };
    QVector<BatchExportJob> jobs;
    jobs.reserve(chartDirectories.size() * selectedDifficultyIds.size());
    for (const QString& chartDirectory : chartDirectories) {
        const QFileInfo directoryInfo(chartDirectory);
        const QString folderName = directoryInfo.fileName();
        const QString trackPath = miacode::chart_assets::resolveTrackPathForDirectory(directoryInfo.absoluteFilePath());
        const QString chartPath = resolveChartPathFromCliInput(directoryInfo.absoluteFilePath());
        if (trackPath.isEmpty() || chartPath.isEmpty()) {
            result->failedCharts.append(QDir::toNativeSeparators(chartDirectory) + QStringLiteral(" - ")
                + UiText::text(trackPath.isEmpty()
                    ? QStringLiteral("dialog.batch_export.error.missing_track_file")
                    : QStringLiteral("dialog.batch_export.error.missing_chart_file")));
            continue;
        }
        bool usedSystemEncoding = false;
        const QString chartText = readTextFileWithFallbackEncoding(chartPath, &usedSystemEncoding);
        if (chartText.isNull()) {
            result->failedCharts.append(QDir::toNativeSeparators(chartDirectory) + QStringLiteral(" - ")
                + UiText::text(QStringLiteral("dialog.batch_export.error.read_chart_failed"))
                    .arg(QFileInfo(chartPath).fileName()));
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
            jobs.append({chartDirectory, difficultyId, token,
                         QStringLiteral("%1 [%2]").arg(folderName, token)});
        }
        if (matchedDifficulties == 0) {
            QStringList requested;
            for (int difficultyId : selectedDifficultyIds) {
                requested.append(SimaiDocument::difficultyShortName(difficultyId));
            }
            result->failedCharts.append(QDir::toNativeSeparators(chartDirectory) + QStringLiteral(" - ")
                + UiText::text(QStringLiteral("dialog.batch_export.error.no_selected_difficulties_in_folder"))
                    .arg(requested.join(QStringLiteral(", "))));
        }
    }

    const int totalJobs = qMax(1, jobs.size());
    for (int index = 0; index < jobs.size(); ++index) {
        const BatchExportJob& job = jobs.at(index);
        if (callbacks.progressChanged) {
            callbacks.progressChanged(
                qRound(static_cast<double>(index) * 100.0 / totalJobs),
                UiText::text(QStringLiteral("dialog.batch_export.progress.exporting_named"))
                    .arg(index + 1).arg(jobs.size()).arg(job.displayName));
        }
        if (callbacks.cancellationRequested && callbacks.cancellationRequested()) {
            result->canceled = true;
            break;
        }

        VideoExportSnapshot snapshot;
        QString failureText;
        if (!buildVideoExportSnapshotForChartDirectory(
                job.chartDirectory,
                job.difficultyId,
                job.difficultyToken,
                templateTask,
                outputDirectory,
                &snapshot,
                &failureText)) {
            result->failedCharts.append(job.displayName + QStringLiteral(" - ") + failureText);
            continue;
        }
        bool canceledThisItem = false;
        const auto updateBatchProgress = [&callbacks, index, totalJobs, &job](
                                             int percent,
                                             const QString& rawMessage) {
            if (!callbacks.progressChanged) {
                return;
            }
            const double overall = (static_cast<double>(index) + qBound(0, percent, 100) / 100.0)
                / static_cast<double>(totalJobs);
            callbacks.progressChanged(
                qBound(0, qRound(overall * 100.0), 100),
                UiText::text(QStringLiteral("dialog.batch_export.progress.current_item"))
                    .arg(job.displayName).arg(localizeExportWorkerMessageForUiLanguage(rawMessage)));
        };
        if (!runVideoExportWorkerSync(
                snapshot,
                &canceledThisItem,
                &failureText,
                updateBatchProgress,
                callbacks.cancellationRequested,
                callbacks.retrying)) {
            if (canceledThisItem) {
                result->canceled = true;
                break;
            }
            result->failedCharts.append(job.displayName + QStringLiteral(" - ") + failureText);
            continue;
        }
        ++result->successCount;
        result->exportedFiles.append(QFileInfo(snapshot.outputPath).fileName());
    }
    if (callbacks.progressChanged) {
        callbacks.progressChanged(100, QString());
    }
    return true;
}

VideoExportTask miacode::runtime::VideoExportHost::buildVideoExportSeedTaskPublic(int difficultyId)
{
    return buildVideoExportSeedTask(difficultyId);
}

bool miacode::runtime::VideoExportHost::startQmlExportAudition(int difficultyId, const VideoExportTask& visualTask)
{
    if (!SimaiDocument::isDifficultyId(difficultyId)
        || session_.applicationServices_.workspace().document().difficulty(difficultyId) == nullptr
        || session_.scene_ == nullptr) {
        return false;
    }
    if (session_.playing_) {
        session_.onTogglePreviewPause();
    }

    beginExportPreviewSession(visualTask);
    const double aspectRatio =
        (visualTask.outputWidth > 0 && visualTask.outputHeight > 0)
            ? (static_cast<double>(visualTask.outputWidth)
               / static_cast<double>(visualTask.outputHeight))
            : 1.0;
    session_.setPreviewCanvasAspectRatio(aspectRatio, false);
    installExportPreviewAuditionScene(difficultyId);
    if (session_.scene_ != nullptr) {
        session_.scene_->setBackgroundBrightnessOuter(visualTask.backgroundBrightnessOuter);
        session_.scene_->setBackgroundBrightnessInner(visualTask.backgroundBrightnessInner);
        session_.scene_->setLayoutSquareScale(visualTask.layoutSquareScale);
        session_.scene_->setSmoothBrightness(visualTask.smoothBrightness);
        session_.scene_->setBackgroundScaleMode(visualTask.backgroundScaleMode);
        session_.scene_->setTapFlowSpeed(visualTask.tapFlowSpeed);
        session_.scene_->setTouchFlowSpeed(visualTask.touchFlowSpeed);
        session_.scene_->setFixHudTextLayout(visualTask.fixHudTextLayout);
        session_.scene_->setShowTimestamp(visualTask.showTimestamp);
        session_.scene_->setShowObjectStatsHud(visualTask.showObjectStatsHud);
        session_.scene_->setShowChartInfoHud(visualTask.showChartInfoHud);
    }
    if (const SimaiDifficultyData* difficulty = session_.applicationServices_.workspace().document().difficulty(difficultyId);
        difficulty != nullptr) {
        session_.setExportAuditionClockSchedule(
            visualTask.clockCountEnabled
                ? miacode::chart_clock::clockCountFromDocument(session_.applicationServices_.workspace().document())
                : 0,
            miacode::chart_clock::clockBpmForChart(session_.applicationServices_.workspace().document(), difficulty->chart));
    }
    session_.refreshExportIntroState();
    return true;
}

void miacode::runtime::VideoExportHost::stopQmlExportAudition()
{
    endExportPreviewSession();
}

bool miacode::runtime::VideoExportHost::launchQmlVideoExport(
    const VideoExportTask& requestedTask,
    int difficultyId,
    QString* errorMessage)
{
    VideoExportTask task = requestedTask;
    applySharedExportTaskSettings(task);
    VideoExportSnapshot snapshot;
    if (!buildVideoExportSnapshot(task, &snapshot, errorMessage, difficultyId)
        || !launchVideoExportWorker(snapshot, errorMessage)) {
        return false;
    }
    return true;
}

bool miacode::runtime::VideoExportHost::launchQmlBatchExport(
    const VideoExportTask& templateTask,
    const QStringList& chartDirectories,
    const QList<int>& selectedDifficultyIds,
    const QString& outputDirectory,
    BatchExportResult* result,
    const BatchExportCallbacks& callbacks,
    QString* errorMessage)
{
    VideoExportTask requestedTask = templateTask;
    applySharedExportTaskSettings(requestedTask);
    requestedTask.exportStartSeconds = 0.0;
    requestedTask.fullRangeExport = true;
    return runBatchExport(
        requestedTask,
        chartDirectories,
        selectedDifficultyIds,
        outputDirectory,
        result,
        callbacks,
        errorMessage);
}
