#include "VideoExportController.h"

#include "BassExportAudioBackend.h"
#include "LegacyExportAudioBackend.h"
#include "RawVideoPipeTransport.h"
#include "VideoExportAudioRenderPlan.h"
#include "VideoExportQuickRenderBackend.h"
#include "VideoExportRuntimePolicy.h"
#include "common/AssetPaths.h"
#include "common/ChartAssetPaths.h"
#include "common/IntroConfig.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/DebugOptions.h"
#include "common/LayoutRingConfig.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "core/scene/PreviewSceneGeometry.h"
#include "common/PreviewSfxTimeline.h"
#include "preview/runtime/PreviewSceneAssetLoader.h"
#include "tools/muri/MuriAnalyzer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
#include <QRect>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "VideoExportControllerInternal.h"

// VideoExportPreparedTask.cpp — temp-dir registry (single-TU mutable state) and the VideoExportController::exportPreparedTask orchestration method.
//
// Definitions extracted verbatim from the original VideoExportController.cpp
// during the god-file split. All helpers live in the shared
// miacode::video_export::detail namespace (declared in
// VideoExportControllerInternal.h).
using namespace miacode::video_export::detail;

namespace miacode::video_export::detail {

QString exportTempDirTemplate()
{
    return QDir(QDir::tempPath()).filePath(QStringLiteral("miacode-video-export-XXXXXX"));
}

class ExportTempDirRegistry
{
public:
    static ExportTempDirRegistry& instance()
    {
        static ExportTempDirRegistry registry;
        return registry;
    }

    void initialize()
    {
        QMutexLocker locker(&mutex_);
        if (initialized_) {
            return;
        }
        initialized_ = true;
        cleanupStaleDirsLocked();
        if (QCoreApplication* app = QCoreApplication::instance()) {
            QObject::connect(app, &QCoreApplication::aboutToQuit, app, [this]() {
                cleanupActiveDirs();
            }, Qt::DirectConnection);
        }
    }

    void track(const QString& path)
    {
        if (path.isEmpty()) {
            return;
        }
        QMutexLocker locker(&mutex_);
        activeDirs_.insert(QDir::cleanPath(path));
    }

    void untrack(const QString& path)
    {
        if (path.isEmpty()) {
            return;
        }
        QMutexLocker locker(&mutex_);
        activeDirs_.remove(QDir::cleanPath(path));
    }

    void cleanupActiveDirs()
    {
        QElapsedTimer timer;
        timer.start();
        QStringList paths;
        {
            QMutexLocker locker(&mutex_);
            paths = activeDirs_.values();
            activeDirs_.clear();
        }
        for (const QString& path : paths) {
            QDir(path).removeRecursively();
        }
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/export_temp_dirs"),
            QStringLiteral("cleanup_active_dirs"),
            timer.elapsed(),
            QStringLiteral("path_count=%1").arg(paths.size())
        );
    }

private:
    void cleanupStaleDirsLocked()
    {
        QDir tempRoot(QDir::tempPath());
        const QFileInfoList entries = tempRoot.entryInfoList(
            QStringList(QStringLiteral("miacode-video-export-*")),
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::Writable
        );
        for (const QFileInfo& entry : entries) {
            QDir(entry.absoluteFilePath()).removeRecursively();
        }
    }

    bool initialized_ = false;
    QSet<QString> activeDirs_;
    QMutex mutex_;
};

class ScopedExportTempDirTracker
{
public:
    explicit ScopedExportTempDirTracker(const QString& path)
        : path_(QDir::cleanPath(path))
    {
        ExportTempDirRegistry::instance().track(path_);
    }

    ~ScopedExportTempDirTracker()
    {
        ExportTempDirRegistry::instance().untrack(path_);
    }

private:
    QString path_;
};

}  // namespace miacode::video_export::detail

VideoExportResult VideoExportController::exportPreparedTask(
    const VideoExportTask& task,
    const VideoExportProgressCallback& progressCallback
)
{
    MC_OP("VideoExportController::exportPreparedTask");
    _mc_op_.note(QStringLiteral("output=%1 chart=%2 size=%3x%4 fps=%5")
                     .arg(task.outputPath, task.chartPath)
                     .arg(task.outputWidth)
                     .arg(task.outputHeight)
                     .arg(task.fps));
    VideoExportResult result;
    const ExportRuntimeConfig exportConfig = loadExportRuntimeConfig();
    QElapsedTimer exportTimer;
    exportTimer.start();
    appendVideoExportLog(
        QStringLiteral("export_begin"),
        QStringLiteral("output=%1 chart=%2 media=%3 track=%4 skin=%5 notes=%6 start=%7 duration=%8 size=%9x%10 fps=%11 preset=%12")
            .arg(task.outputPath, task.chartPath, task.backgroundMediaPath, task.trackPath, task.skinDirectory)
            .arg(task.noteMarkers.size())
            .arg(task.exportStartSeconds, 0, 'f', 6)
            .arg(task.contentDurationSeconds, 0, 'f', 6)
            .arg(task.outputWidth)
            .arg(task.outputHeight)
            .arg(task.fps)
            .arg(videoExportPresetToken(task.preset))
    );
    if (task.skinDirectory.trimmed().isEmpty()) {
        result.message = QStringLiteral("Skin directory is empty.");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("fail_validation"), result.message);
        return result;
    }
    if (task.outputPath.trimmed().isEmpty()) {
        result.message = QStringLiteral("Output path is empty.");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("fail_validation"), result.message);
        return result;
    }
    if (task.outputWidth <= 0 || task.outputHeight <= 0 || task.fps <= 0) {
        result.message = QStringLiteral("Export parameters are invalid.");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("fail_validation"), result.message);
        return result;
    }
    if (task.outputWidth < task.outputHeight) {
        result.message = QStringLiteral("Output size currently requires width >= height.");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("fail_validation"), result.message);
        return result;
    }
    if (task.contentDurationSeconds <= 0.0) {
        result.message = QStringLiteral("Content duration is invalid.");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("fail_validation"), result.message);
        return result;
    }

    const QString ffmpegPath = resolveFfmpegExecutable();
    appendVideoExportLog(QStringLiteral("resolve_ffmpeg"), QStringLiteral("path=%1").arg(ffmpegPath));
    if (ffmpegPath.isEmpty()) {
        result.message = QStringLiteral("ffmpeg executable was not found.");
        result.details = QStringLiteral("Place ffmpeg under app/ffmpeg or set MIACODE_FFMPEG_PATH.");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("fail_ffmpeg_missing"), result.message);
        return result;
    }
    const QString ffprobePath = resolveFfprobeExecutable(ffmpegPath);
    appendVideoExportLog(QStringLiteral("resolve_ffprobe"), QStringLiteral("path=%1").arg(ffprobePath));

    const auto setProgressPercent = [progressCallback](int percent, const QString& text) {
        if (!progressCallback) {
            return false;
        }
        return progressCallback(percent, text);
    };

    miacode::video_export::VideoExportAudioRenderPlan audioRenderPlan;
    QString audioPlanError;
    if (!miacode::video_export::buildVideoExportAudioRenderPlan(task, &audioRenderPlan, &audioPlanError)) {
        result.message = QStringLiteral("Unable to build export audio render plan.");
        result.details = withExportLogPath(audioPlanError);
        appendVideoExportLog(QStringLiteral("fail_audio_plan"), audioPlanError);
        return result;
    }

    const int fullFrameCount = audioRenderPlan.frameCount;
    // Dev preview cap: render only the leading frames (intro preview) when
    // task.previewMaxOutputSeconds > 0, leaving the audio/intro plan untouched so
    // timing matches a real export. -shortest (added below) trims the mux.
    const bool previewCapped = task.previewMaxOutputSeconds > 0.0;
    const int frameCount = previewCapped
        ? qBound(1, qRound(task.previewMaxOutputSeconds * qMax(1, task.fps)), fullFrameCount)
        : fullFrameCount;
    const double alignedTotalSeconds = audioRenderPlan.alignedTotalSeconds;
    const double timelineOriginSecond = audioRenderPlan.timelineOriginSecond;
    const bool partialRangeExport = !task.fullRangeExport;
    const int frameWidth = qMax(1, task.outputWidth);
    const int frameHeight = qMax(1, task.outputHeight);
    const QSize frameSize(frameWidth, frameHeight);
    const QString explicitMediaPath = normalizePath(task.backgroundMediaPath);
    // Phase 4c — `task.backgroundMediaPath` is filled by the snapshot
    // builder using `resolveChartVideoPath` (`&video=` override first,
    // then sibling fallback). Trust that as the final choice when it
    // points at a supported file; the legacy `resolvePreferredBackground…`
    // path inverts the priority (sibling first), which would silently
    // override the chart-author's explicit `&video=` choice.
    QString mediaPath;
    if (miacode::chart_assets::isSupportedBackgroundMediaPath(
            explicitMediaPath, /*includeVideoCandidates=*/true)) {
        mediaPath = explicitMediaPath;
    } else {
        mediaPath = miacode::chart_assets::resolvePreferredBackgroundMediaPath(
            task.chartPath,
            explicitMediaPath);
    }
    const bool hasMedia = !mediaPath.isEmpty();
    const bool mediaIsImage = hasMedia && isImageMediaPath(mediaPath);
    const QString trackPath = audioRenderPlan.backgroundTrack.enabled
        ? audioRenderPlan.backgroundTrack.path
        : QString();
    const bool hasTrack = !trackPath.isEmpty();
    const QVector<TimelineNoteMarker>& exportMarkers = audioRenderPlan.exportMarkers;
    const MuriAnalysisReport exportMuriAnalysisReport = exportMarkers.isEmpty()
        ? MuriAnalysisReport{}
        : MuriAnalyzer::analyze(
              exportMarkers,
              task.muriRenderOptions,
              task.staticTapOnSlideThresholdSeconds);
    appendVideoExportLog(
        QStringLiteral("input_probe"),
        QStringLiteral("media=%1 hasMedia=%2 mediaIsImage=%3 track=%4 hasTrack=%5 segmentStart=%6 segmentEnd=%7 leadIn=%8 timelineOrigin=%9 fullRange=%10 markerFilter=marker.second within simulatedWindow frameWindow=%11..%12 visibleWindow=%13..%14 totalSeconds=%15 alignedSeconds=%16 frameCount=%17 size=%18x%19")
            .arg(mediaPath)
            .arg(hasMedia ? 1 : 0)
            .arg(mediaIsImage ? 1 : 0)
            .arg(trackPath)
            .arg(hasTrack ? 1 : 0)
            .arg(audioRenderPlan.segmentStartSecond, 0, 'f', 6)
            .arg(audioRenderPlan.segmentEndSecond, 0, 'f', 6)
            .arg(audioRenderPlan.leadInSeconds, 0, 'f', 6)
            .arg(timelineOriginSecond, 0, 'f', 6)
            .arg(task.fullRangeExport ? 1 : 0)
            .arg(timelineOriginSecond, 0, 'f', 6)
            .arg(audioRenderPlan.segmentEndSecond, 0, 'f', 6)
            .arg(audioRenderPlan.segmentStartSecond, 0, 'f', 6)
            .arg(audioRenderPlan.segmentEndSecond, 0, 'f', 6)
            .arg(audioRenderPlan.totalSeconds, 0, 'f', 6)
            .arg(alignedTotalSeconds, 0, 'f', 6)
            .arg(frameCount)
            .arg(frameWidth)
            .arg(frameHeight)
    );
    appendVideoExportLog(
        QStringLiteral("render_plan"),
        QStringLiteral("fps=%1 frameBudgetMs=%2 frameCount=%3 bgm=%4 sfx=%5 touchhold=%6")
            .arg(task.fps)
            .arg(1000.0 / static_cast<double>(qMax(1, task.fps)), 0, 'f', 4)
            .arg(frameCount)
            .arg(audioRenderPlan.backgroundTrack.enabled ? 1 : 0)
            .arg(audioRenderPlan.scheduledSfxPlaybacks.size())
            .arg(audioRenderPlan.mergedTouchholdSpans.size())
    );

    if (setProgressPercent(0, QStringLiteral("Preparing SFX track..."))) {
        result.message = QStringLiteral("canceled");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("canceled"), QStringLiteral("stage=prepare_sfx_progress"));
        return result;
    }

    ExportTempDirRegistry::instance().initialize();
    QTemporaryDir tempDir(exportTempDirTemplate());
    if (!tempDir.isValid()) {
        result.message = QStringLiteral("Unable to create temporary directory.");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("fail_temp_dir"), result.message);
        return result;
    }
    ScopedExportTempDirTracker tempDirTracker(tempDir.path());
    const QString encodedTempPath = QDir(tempDir.path()).filePath(QStringLiteral("encoded_raw.mp4"));
    const QString remuxStagePath = makeRemuxStageOutputPath(task.outputPath);
    appendVideoExportLog(
        QStringLiteral("output_staging"),
        QStringLiteral("encodeTemp=%1 remuxStage=%2 final=%3")
            .arg(encodedTempPath, remuxStagePath, task.outputPath)
    );

    QString ffmpegMediaPath = mediaPath;
    bool mediaUsesPreprocessedImage = false;
    if (hasMedia && mediaIsImage) {
        const QString stagedImagePath = QDir(tempDir.path()).filePath(QStringLiteral("background_media_staged.png"));
        QString stagedImageDetail;
        if (stageStaticBackgroundImageForExport(
                mediaPath,
                frameSize,
                task.backgroundScaleMode,
                stagedImagePath,
                &stagedImageDetail)) {
            ffmpegMediaPath = stagedImagePath;
            mediaUsesPreprocessedImage = true;
            appendVideoExportLog(QStringLiteral("stage_static_media"), stagedImageDetail);
        } else {
            appendVideoExportLog(
                QStringLiteral("stage_static_media_fallback"),
                QStringLiteral("source=%1 failure=%2")
                    .arg(mediaPath)
                    .arg(stagedImageDetail.isEmpty() ? QStringLiteral("unknown") : stagedImageDetail)
            );
        }
    }

    const QString mixedAudioWavPath = QDir(tempDir.path()).filePath(QStringLiteral("export_audio.wav"));
    QString audioBackendError;
    std::unique_ptr<miacode::video_export::VideoExportAudioBackend> audioBackend =
        createExportAudioBackend(&audioBackendError);
    if (!audioBackend) {
        result.message = QStringLiteral("Unable to select export audio backend.");
        result.details = withExportLogPath(audioBackendError);
        appendVideoExportLog(QStringLiteral("fail_audio_backend_select"), audioBackendError);
        return result;
    }
    appendVideoExportLog(
        QStringLiteral("audio_backend_select"),
        QStringLiteral("backend=%1 path=%2").arg(audioBackend->backendId(), mixedAudioWavPath));
    if (!audioBackend->renderMixedTrackToWav(audioRenderPlan, mixedAudioWavPath, &audioBackendError)) {
        result.message = QStringLiteral("Unable to generate mixed export audio track.");
        result.details = withExportLogPath(audioBackendError);
        appendVideoExportLog(
            QStringLiteral("fail_audio_mix"),
            QStringLiteral("backend=%1 output=%2 error=%3")
                .arg(audioBackend->backendId(), mixedAudioWavPath, audioBackendError));
        return result;
    }
    appendVideoExportLog(
        QStringLiteral("audio_mix_ok"),
        QStringLiteral("backend=%1 output=%2").arg(audioBackend->backendId(), mixedAudioWavPath));

    if (setProgressPercent(5, QStringLiteral("Starting ffmpeg..."))) {
        result.message = QStringLiteral("canceled");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("canceled"), QStringLiteral("stage=start_ffmpeg_progress"));
        return result;
    }

    const RawVideoPipePlan rawVideoPipePlan = chooseRawVideoPipePlan(frameSize);
    RawVideoPipePump rawVideoPipePump;
    rawVideoPipePump.plan = rawVideoPipePlan;
    QString rawVideoPipeFailure;
    if (!startRawVideoPipe(&rawVideoPipePump.pipe, tempDir.path(), rawVideoPipePlan, &rawVideoPipeFailure)) {
        result.message = QStringLiteral("Failed to create raw video pipe.");
        result.details = withExportLogPath(rawVideoPipeFailure);
        appendVideoExportLog(
            QStringLiteral("fail_raw_pipe_start"),
            QStringLiteral("failure=%1").arg(truncateForLog(rawVideoPipeFailure, 400))
        );
        return result;
    }
    struct RawVideoPipePumpCleanup {
        RawVideoPipePump* pump = nullptr;
        ~RawVideoPipePumpCleanup()
        {
            shutdownRawVideoPipePump(pump);
        }
    } rawVideoPipePumpCleanup{&rawVideoPipePump};
    appendVideoExportLog(
        QStringLiteral("raw_pipe_plan"),
        QStringLiteral("transport=%1 frameMiB=%2 bufferMiB=%3 maxBufferedFrames=%4 connectTimeoutMs=%5")
            .arg(rawVideoPipeTransportName(rawVideoPipePump.pipe.transport))
            .arg(rawVideoPipePlan.frameBytes / (1024.0 * 1024.0), 0, 'f', 2)
            .arg(rawVideoPipePlan.requestedBufferBytes / (1024.0 * 1024.0), 0, 'f', 2)
            .arg(rawVideoPipePlan.maxBufferedFrames)
            .arg(rawVideoPipePlan.connectTimeoutMs)
    );

    // Opening SFX: when the intro front-pad is present, extract the bundled
    // WAV from qrc to the temp dir so ffmpeg can read it (ffmpeg can't open
    // qrc). It is mixed at output t=0 (== intro start) over the silent pad.
    const bool introAudioEnabled = audioRenderPlan.introLeadSeconds > 0.0;
    QString introSfxTempPath;
    if (introAudioEnabled) {
        introSfxTempPath = QDir(tempDir.path()).filePath(QStringLiteral("intro_sfx.wav"));
        if (QFile::exists(introSfxTempPath)) {
            QFile::remove(introSfxTempPath);
        }
        if (!QFile::copy(QString::fromLatin1(miacode::intro::kOpeningSfxResource), introSfxTempPath)) {
            appendVideoExportLog(QStringLiteral("intro_sfx_extract_failed"), introSfxTempPath);
            introSfxTempPath.clear();  // fall back to a silent front-pad
        }
    }

    QStringList args;
    args << QStringLiteral("-y")
         << QStringLiteral("-hide_banner")
         << QStringLiteral("-loglevel")
         << QStringLiteral("error");
    args << QStringLiteral("-f")
         << QStringLiteral("rawvideo")
         << QStringLiteral("-pix_fmt")
         << QStringLiteral("rgba")
         << QStringLiteral("-s:v")
         << QStringLiteral("%1x%2").arg(frameWidth).arg(frameHeight)
         << QStringLiteral("-framerate")
         << QString::number(task.fps)
         << QStringLiteral("-i")
         << rawVideoPipePump.pipe.inputPath;

    const double outerDimAlpha = qBound(0.0, 1.0 - task.backgroundBrightnessOuter, 1.0);
    const double innerDimAlpha = qBound(0.0, 1.0 - task.backgroundBrightnessInner, 1.0);
    const bool hasDimMask = outerDimAlpha > 1e-6 || innerDimAlpha > 1e-6;

    int mediaInputIndex = -1;
    int dimMaskInputIndex = -1;
    int audioInputIndex = -1;
    int currentInputIndex = 1;
    if (hasMedia) {
        mediaInputIndex = currentInputIndex++;
        if (mediaIsImage) {
            args << QStringLiteral("-loop")
                 << QStringLiteral("1")
                 << QStringLiteral("-framerate")
                 << QString::number(task.fps);
        }
        args << QStringLiteral("-i") << ffmpegMediaPath;
    }
    if (hasDimMask) {
        const QString dimMaskPath = QDir(tempDir.path()).filePath(QStringLiteral("dim_mask.png"));
        const double ringRatio =
            miacode::preview::runtime::PreviewSceneAssetLoader::loadAssetState(
                task.outlineVariant,
                task.outlineImagePath).layoutRingDiameterRatio;
        const QImage dimMask = buildCircularDimMaskImage(
            frameWidth,
            frameHeight,
            outerDimAlpha,
            innerDimAlpha,
            ringRatio,
            task.layoutSquareScale,
            task.smoothBrightness
        );
        if (dimMask.isNull() || !dimMask.save(dimMaskPath)) {
            result.message = QStringLiteral("Unable to create dim mask image.");
            result.details = withExportLogPath(dimMaskPath);
            appendVideoExportLog(
                QStringLiteral("fail_dim_mask"),
                QStringLiteral("path=%1 outer=%2 inner=%3 ratio=%4")
                    .arg(dimMaskPath)
                    .arg(outerDimAlpha, 0, 'f', 6)
                    .arg(innerDimAlpha, 0, 'f', 6)
                    .arg(ringRatio, 0, 'f', 6)
            );
            return result;
        }
        dimMaskInputIndex = currentInputIndex++;
        args << QStringLiteral("-loop")
             << QStringLiteral("1")
             << QStringLiteral("-framerate")
             << QString::number(task.fps)
             << QStringLiteral("-i")
             << dimMaskPath;
        appendVideoExportLog(
            QStringLiteral("dim_mask"),
            QStringLiteral("path=%1 inputIndex=%2 outer=%3 inner=%4 ringRatio=%5")
                .arg(dimMaskPath)
                .arg(dimMaskInputIndex)
                .arg(outerDimAlpha, 0, 'f', 6)
                .arg(innerDimAlpha, 0, 'f', 6)
                .arg(ringRatio, 0, 'f', 6)
        );
    }
    audioInputIndex = currentInputIndex++;
    args << QStringLiteral("-i") << mixedAudioWavPath;
    int introAudioInputIndex = -1;
    if (!introSfxTempPath.isEmpty()) {
        introAudioInputIndex = currentInputIndex++;
        args << QStringLiteral("-i") << introSfxTempPath;
    }

    const QString totalSecondsText = QString::number(alignedTotalSeconds, 'f', 6);
    const QString timelineOriginText = QString::number(timelineOriginSecond, 'f', 6);
    const QString baseFillColor = hasMedia ? QStringLiteral("#000000") : QStringLiteral("#1F2833");
    QStringList filterParts;
    filterParts << QStringLiteral("color=c=%1:s=%2x%3:r=%4:d=%5[base_fill]")
                       .arg(baseFillColor)
                       .arg(frameWidth)
                       .arg(frameHeight)
                       .arg(task.fps)
                       .arg(totalSecondsText);
    if (hasMedia) {
        QString mediaChain = QStringLiteral("[%1:v]").arg(mediaInputIndex);
        QStringList mediaFilters;
        const int squareSide = qMax(1, qMin(frameWidth, frameHeight));
        const int squareOffsetX = (frameWidth - squareSide) / 2;
        const int squareOffsetY = (frameHeight - squareSide) / 2;
        if (!(mediaIsImage && mediaUsesPreprocessedImage)) {
            if (task.backgroundScaleMode == PreviewBackgroundScaleMode::FitContain) {
                mediaFilters << QStringLiteral(
                    "scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black")
                                    .arg(frameWidth)
                                    .arg(frameHeight);
            } else if (task.backgroundScaleMode == PreviewBackgroundScaleMode::SquareFitContain) {
                mediaFilters << QStringLiteral(
                    "scale=%1:%1:force_original_aspect_ratio=decrease,pad=%1:%1:(ow-iw)/2:(oh-ih)/2:color=black")
                                    .arg(squareSide);
            } else {
                mediaFilters << QStringLiteral(
                    "scale=%1:%2:force_original_aspect_ratio=increase,crop=%1:%2")
                                    .arg(frameWidth)
                                    .arg(frameHeight);
            }
        }
        mediaFilters << QStringLiteral("setsar=1")
                     << QStringLiteral("fps=%1").arg(task.fps)
                     << QStringLiteral("format=rgba");
        if (!mediaIsImage) {
            if (timelineOriginSecond > kTimelineEpsilonSeconds) {
                mediaFilters << QStringLiteral("trim=start=%1:end=%2")
                                    .arg(timelineOriginText)
                                    .arg(QString::number(timelineOriginSecond + alignedTotalSeconds, 'f', 6))
                             << QStringLiteral("setpts=PTS-STARTPTS");
            } else if (timelineOriginSecond < -kTimelineEpsilonSeconds) {
                mediaFilters << QStringLiteral("trim=start=0:end=%1")
                                    .arg(QString::number(alignedTotalSeconds + timelineOriginSecond, 'f', 6))
                             << QStringLiteral("setpts=PTS-STARTPTS+%1/TB")
                                    .arg(QString::number(-timelineOriginSecond, 'f', 6));
            }
            mediaFilters << QStringLiteral("tpad=stop_mode=clone:stop_duration=%1").arg(totalSecondsText);
        }
        mediaChain += mediaFilters.join(QLatin1Char(','));
        mediaChain += QStringLiteral("[media_src]");
        filterParts << mediaChain;
        filterParts << QStringLiteral("[base_fill][media_src]overlay=%1:%2:format=rgb:alpha=straight[base_media]")
                           .arg((task.backgroundScaleMode == PreviewBackgroundScaleMode::SquareFitContain
                                    && !(mediaIsImage && mediaUsesPreprocessedImage))
                                    ? squareOffsetX
                                    : 0)
                           .arg((task.backgroundScaleMode == PreviewBackgroundScaleMode::SquareFitContain
                                    && !(mediaIsImage && mediaUsesPreprocessedImage))
                                    ? squareOffsetY
                                    : 0);
    } else {
        filterParts << QStringLiteral("[base_fill]null[base_media]");
    }

    if (hasDimMask) {
        filterParts << QStringLiteral("[%1:v]fps=%2,format=rgba[dim_mask]")
                           .arg(dimMaskInputIndex)
                           .arg(task.fps);
        filterParts << QStringLiteral("[base_media][dim_mask]overlay=0:0:format=rgb:alpha=straight[base_src]");
    } else {
        filterParts << QStringLiteral("[base_media]null[base_src]");
    }

    // Intro background fade-from-black: black the BACKGROUND base, then fade the
    // "曲绘" in so it reaches full brightness EXACTLY at chart 0 (the song +
    // clock_count start). chart 0 in output time is -timelineOriginSecond; the fade
    // runs for kBgFadeDurationSeconds and ENDS there, so it plays out across the tail
    // of the full-range lead-in preview (which runs on black before it). This only
    // touches [base] (the bg); the playfield outline + HUD are composited later as
    // [overlay_src], so they read normally over the black/fading background.
    // fade=t=in holds black before start_time, so the bg is black throughout the
    // covered intro and the early lead-in.
    //
    // CRITICAL: `fade` fades ALL planes of the RGBA base, INCLUDING alpha — so
    // before start_time the base is transparent-black (a=0), not opaque-black. The
    // final `[base][overlay_src]overlay=...:alpha=straight` step then composites the
    // playfield over a transparent base, which blows every PARTIAL-alpha overlay
    // pixel out to full opacity (a 12%-opacity antialiased / judge-area pixel renders
    // as solid white over the black opening). The trailing `,format=rgb24` re-flattens
    // the base to fully opaque after the fade — the RGB fade-from-black is preserved,
    // only the (now-bogus) alpha plane is dropped — restoring correct straight-alpha
    // compositing. Most visible with the heavy "判定区" outline (~30% partial-alpha
    // pixels) over the still-black opening, before the bg has faded in.
    if (introAudioEnabled) {
        const double chartZeroOutputSecond = -timelineOriginSecond;
        const double bgFadeStartSecond =
            qMax(0.0, chartZeroOutputSecond - miacode::intro::kBgFadeDurationSeconds);
        filterParts << QStringLiteral("[base_src]fade=t=in:st=%1:d=%2:color=black,format=rgb24[base]")
                           .arg(QString::number(bgFadeStartSecond, 'f', 6))
                           .arg(QString::number(miacode::intro::kBgFadeDurationSeconds, 'f', 6));
    } else {
        filterParts << QStringLiteral("[base_src]null[base]");
    }

    // Quick export frames are already read back in top-left raster order.
    filterParts << QStringLiteral("[0:v]format=rgba[overlay_src]");
    if (introAudioInputIndex >= 0) {
        // Mix the opening SFX over the front-pad. normalize=0 keeps the chart
        // mix at full level (the two don't overlap in time anyway); duration
        // follows the main chart audio. The SFX plays from output t=0.
        filterParts << QStringLiteral("[%1:a]atrim=0:%2,asetpts=PTS-STARTPTS,aresample=%3,aformat=channel_layouts=stereo[mainaud]")
                           .arg(audioInputIndex)
                           .arg(totalSecondsText)
                           .arg(kMixSampleRate);
        filterParts << QStringLiteral("[%1:a]aresample=%2,aformat=channel_layouts=stereo[introaud]")
                           .arg(introAudioInputIndex)
                           .arg(kMixSampleRate);
        filterParts << QStringLiteral("[mainaud][introaud]amix=inputs=2:normalize=0:duration=first[aout]");
    } else {
        filterParts << QStringLiteral("[%1:a]atrim=0:%2,asetpts=PTS-STARTPTS,aresample=%3,aformat=channel_layouts=stereo[aout]")
                           .arg(audioInputIndex)
                           .arg(totalSecondsText)
                           .arg(kMixSampleRate);
    }

    const SystemMemoryInfo memoryInfo = querySystemMemoryInfo();
    appendVideoExportLog(QStringLiteral("memory_snapshot"), memoryInfoToLog(memoryInfo));
    QString encoderProbeLog;
    // ffmpeg/encoder parameters are pinned to the HighQuality preset for both
    // export-quality modes. task.preset now selects only the readback path
    // (PBO vs synchronous), never the encode bitrate/x264 tuning — so Fast
    // trades readback speed without ever lowering output encode quality.
    const VideoEncoderConfig encoderConfig = chooseVideoEncoder(
        ffmpegPath,
        frameWidth,
        frameHeight,
        task.fps,
        VideoExportPreset::HighQuality,
        memoryInfo,
        exportConfig,
        &encoderProbeLog
    );
    appendVideoExportLog(QStringLiteral("encoder_select"), encoderProbeLog);
    const int idealThreadCount = qMax(1, QThread::idealThreadCount());
    const qint64 availMiB = bytesToMiB(memoryInfo.availablePhysicalBytes);
    const int encoderThreads = qBound(
        1,
        exportConfig.encoderThreadsOverride > 0 ? exportConfig.encoderThreadsOverride : idealThreadCount,
        32
    );
    const int defaultFilterThreads = encoderConfig.isHardware
        ? qBound(2, qMax(1, idealThreadCount / 2), 4)
        : qBound(1, qMax(1, idealThreadCount / 2), 8);
    const int filterThreads = qBound(
        1,
        exportConfig.filterThreadsOverride > 0 ? exportConfig.filterThreadsOverride : defaultFilterThreads,
        encoderConfig.isHardware ? 4 : 16
    );
    const int effectiveEncoderThreads = encoderThreads;
    appendVideoExportLog(
        QStringLiteral("thread_plan"),
        QStringLiteral("ideal=%1 encoderThreads=%2 effectiveEncoderThreads=%3 filterThreads=%4 encoder=%5 hw=%6 availMiB=%7 preset=%8")
            .arg(idealThreadCount)
            .arg(encoderThreads)
            .arg(effectiveEncoderThreads)
            .arg(filterThreads)
            .arg(encoderConfig.codec)
            .arg(encoderConfig.isHardware ? 1 : 0)
            .arg(memoryInfo.valid ? QString::number(availMiB) : QStringLiteral("na"))
            .arg(videoExportPresetToken(task.preset))
    );

    VideoExportQuickRenderBackend exportCanvas;
    QString quickBootstrapError;
    if (!exportCanvas.bootstrap(
            task,
            hasMedia,
            exportMarkers,
            exportMuriAnalysisReport,
            frameSize,
            &quickBootstrapError)) {
        result.message = QStringLiteral("Failed to load export skin assets.");
        result.details = withExportLogPath(
            quickBootstrapError.isEmpty()
                ? QStringLiteral("skin_dir=%1").arg(task.skinDirectory)
                : quickBootstrapError);
        appendVideoExportLog(
            QStringLiteral("fail_skin_load"),
            quickBootstrapError.isEmpty() ? result.message : quickBootstrapError);
        return result;
    }
    appendVideoExportLog(
        QStringLiteral("skin_bootstrap"),
        QStringLiteral("quick=1 loaded=%1 dir=%2")
            .arg(exportCanvas.hasCoreSkinAssetsLoadedForDebug() ? 1 : 0)
            .arg(task.skinDirectory));
    const QSurfaceFormat requestedFormat = QSurfaceFormat::defaultFormat();
    QOpenGLContext* shareContext = nullptr;
    QString offscreenInitError;
    bool useOffscreenGpu = exportCanvas.initializeOffscreenRenderer(
        requestedFormat,
        shareContext,
        &offscreenInitError
    );
    if (!useOffscreenGpu) {
        result.message = QStringLiteral("Failed to initialize Quick export renderer.");
        result.details = withExportLogPath(
            offscreenInitError.isEmpty()
                ? QStringLiteral("quick_export_session_init_failed")
                : offscreenInitError);
        appendVideoExportLog(
            QStringLiteral("fail_export_backend_init"),
            offscreenInitError.isEmpty() ? result.message : offscreenInitError);
        return result;
    }

    // Pre-roll maimai track-start intro (full-range exports only). The audio
    // plan already front-padded the timeline by introLeadSeconds, so frames
    // [0, introFrameCount) are the intro window. Mount the overlay scene + push
    // the banner data once here; the per-frame `frame` advance happens in the
    // loop. A QML load failure degrades gracefully — the padded frames just
    // render the chart at negative time without the overlay.
    bool introEnabled = task.intro.enabled
        && task.fullRangeExport
        && audioRenderPlan.introFrameCount > 0;
    if (introEnabled) {
        QString introError;
        if (!exportCanvas.setupIntro(task.intro, &introError)) {
            introEnabled = false;
            appendVideoExportLog(
                QStringLiteral("intro_setup_failed"),
                introError.isEmpty() ? QStringLiteral("unknown") : introError);
        } else {
            appendVideoExportLog(
                QStringLiteral("intro_setup"),
                QStringLiteral("frames=%1 leadSeconds=%2 difficulty=%3")
                    .arg(audioRenderPlan.introFrameCount)
                    .arg(audioRenderPlan.introLeadSeconds, 0, 'f', 3)
                    .arg(task.intro.difficulty));
        }
    }
    // The "导出质量 / Export Quality" toggle (task.preset) selects the
    // readback path:
    //   HighQuality (default) -> synchronous non-PBO readback
    //       (renderOverlayFrameOffscreen): glReadPixels through a CPU pointer
    //       the driver must serialize, so it cannot tear. ~20-30% slower at
    //       1080p60.
    //   Fast -> pipelined async glReadPixels(FBO->PBO): faster, but on some
    //       GL drivers prone to per-frame horizontal-band tearing that the
    //       in-engine fence could not fully prevent (observed on a GL 4.6
    //       context whose fence wait never failed). Speed-over-fidelity.
    // The ffmpeg/encoder parameters are HighQuality in BOTH modes (the toggle
    // only changes the readback path), so Fast never lowers encode quality.
    //
    // MIACODE_EXPORT_DISABLE_PBO_READBACK=1 is a hard diagnostic override that
    // forces the synchronous path regardless of the quality choice.
    const bool highQualityForcesSyncReadback =
        task.preset == VideoExportPreset::HighQuality;
    const bool disablePboReadbackViaEnv =
        qEnvironmentVariableIntValue("MIACODE_EXPORT_DISABLE_PBO_READBACK") == 1;
    const bool requestOffscreenPboReadback =
        useOffscreenGpu
        && exportConfig.renderBackend.requestOffscreenPboReadback
        && !disablePboReadbackViaEnv
        && !highQualityForcesSyncReadback;
    QString offscreenPboError;
    bool useOffscreenPboReadback = false;
    if (requestOffscreenPboReadback) {
        useOffscreenPboReadback = exportCanvas.supportsOffscreenPboReadback(&offscreenPboError);
    }
    if (disablePboReadbackViaEnv) {
        appendVideoExportLog(
            QStringLiteral("pbo_readback_disabled_via_env"),
            QStringLiteral("MIACODE_EXPORT_DISABLE_PBO_READBACK=1"));
    }
    if (highQualityForcesSyncReadback) {
        appendVideoExportLog(
            QStringLiteral("pbo_readback_disabled_by_quality"),
            QStringLiteral("exportQuality=high_quality readback=synchronous"));
    }
    appendVideoExportLog(
        QStringLiteral("render_backend"),
        QStringLiteral("quickRequired=1 envGpuRequested=%1 sourceCtx=%2 offscreenInit=%3 exportGpuReady=%4 pboRequested=%5 pboEnabled=%6 initError=%7 pboError=%8")
            .arg(exportConfig.renderBackend.requestGpuRender ? 1 : 0)
            .arg(shareContext != nullptr ? 1 : 0)
            .arg(useOffscreenGpu ? 1 : 0)
            .arg(exportCanvas.isGpuRendererReadyForDebug() ? 1 : 0)
            .arg(requestOffscreenPboReadback ? 1 : 0)
            .arg(useOffscreenPboReadback ? 1 : 0)
            .arg(offscreenInitError.isEmpty() ? QStringLiteral("ok") : offscreenInitError)
            .arg(offscreenPboError.isEmpty() ? QStringLiteral("ok") : offscreenPboError)
    );

    // Raw RGBA frames are packed after conversion to non-premultiplied RGBA8888.
    const QString overlayAlphaMode = QStringLiteral("straight");
    filterParts << QStringLiteral("[base][overlay_src]overlay=0:0:format=rgb:alpha=%1[vout]")
                       .arg(overlayAlphaMode);

    args << QStringLiteral("-filter_threads") << QString::number(filterThreads);
    args << QStringLiteral("-filter_complex_threads") << QString::number(filterThreads);
    args << QStringLiteral("-filter_complex") << filterParts.join(';');
    args << QStringLiteral("-map")
         << QStringLiteral("[vout]")
         << QStringLiteral("-map")
         << QStringLiteral("[aout]")
         << QStringLiteral("-fps_mode")
         << QStringLiteral("cfr")
         << QStringLiteral("-r")
         << QString::number(task.fps)
         << QStringLiteral("-frames:v")
         << QString::number(frameCount)
         << QStringLiteral("-g")
         << QString::number(qMax(1, task.fps * 2))
         << QStringLiteral("-c:v")
         << encoderConfig.codec
         << QStringLiteral("-pix_fmt")
         << QStringLiteral("yuv420p")
         << QStringLiteral("-c:a")
         << QStringLiteral("aac")
         << QStringLiteral("-b:a")
         // Clamp into the dropdown's accepted range so any out-of-band
         // value (e.g. an old preferences file with a stale int) still
         // produces a valid AAC encoder argument. 320k is the AAC LC
         // ceiling for stereo at 44.1/48 kHz; below 96k AAC quality
         // collapses, so 96k is our floor.
         << QStringLiteral("%1k")
                .arg(qBound(96, task.audioBitrateKbps, 320));
    if (previewCapped) {
        // -frames:v already stops the video at the capped count; -shortest trims the
        // (still full-length) audio so the preview output ends with the video.
        args << QStringLiteral("-shortest");
    }
    if (encoderConfig.explicitBframes >= 0) {
        args << QStringLiteral("-bf") << QString::number(encoderConfig.explicitBframes);
    }
    if (!encoderConfig.isHardware) {
        args << QStringLiteral("-threads") << QString::number(effectiveEncoderThreads);
    }
    args << encoderConfig.extraArgs;
    args << encodedTempPath;
    appendVideoExportLog(
        QStringLiteral("ffmpeg_encode_args"),
        truncateForLog(ffmpegBaseArgsLog(ffmpegPath, args), 8000)
    );

    QProcess ffmpeg;
    ffmpeg.setProcessChannelMode(QProcess::MergedChannels);
    ffmpeg.start(ffmpegPath, args, QIODevice::ReadOnly);
    if (!ffmpeg.waitForStarted(5000)) {
        result.message = QStringLiteral("Failed to start ffmpeg.");
        result.details = withExportLogPath(ffmpeg.errorString());
        appendVideoExportLog(
            QStringLiteral("fail_ffmpeg_start"),
            QStringLiteral("error=%1").arg(ffmpeg.errorString())
        );
        return result;
    }
    appendVideoExportLog(QStringLiteral("ffmpeg_encode_started"));
    appendVideoExportLog(QStringLiteral("ffmpeg_process_started"), describeProcessForLog(ffmpeg));
    if (!startRawVideoPipePumpThread(&rawVideoPipePump, &rawVideoPipeFailure)) {
        ffmpeg.kill();
        ffmpeg.waitForFinished(2000);
        result.message = QStringLiteral("Failed to start raw pipe writer.");
        result.details = withExportLogPath(rawVideoPipeFailure);
        appendVideoExportLog(
            QStringLiteral("fail_raw_pipe_writer_start"),
            QStringLiteral("failure=%1").arg(truncateForLog(rawVideoPipeFailure, 400))
        );
        return result;
    }
    appendVideoExportLog(
        QStringLiteral("raw_pipe_started"),
        QStringLiteral("transport=%1 input=%2")
            .arg(rawVideoPipeTransportName(rawVideoPipePump.pipe.transport))
            .arg(rawVideoPipePump.pipe.inputPath)
    );

    if (setProgressPercent(8, QStringLiteral("Rendering frames and encoding..."))) {
        ffmpeg.kill();
        ffmpeg.waitForFinished(2000);
        result.message = QStringLiteral("canceled");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("canceled"), QStringLiteral("stage=render_begin_progress"));
        return result;
    }

    const qint64 frameBudgetNs = static_cast<qint64>(1000000000.0 / qMax(1, task.fps));
    static constexpr qint64 kFrameStallLogNs = 80000000;  // 80ms
    static constexpr int kFrameProgressStride = 300;
    FrameTimingStats frameStats;

    const bool diagRepeatEnabled = exportConfig.diag.repeatEnabled;
    const int diagCropBottom = exportConfig.diag.cropBottom;
    const int diagMaxLogLines = exportConfig.diag.maxLines;
    const bool diagLogAllRepeatPairs = exportConfig.diag.logAllRepeatPairs;
    const bool diagObjectHashEnabled = diagRepeatEnabled && exportConfig.diag.objectHashEnabled;
    const bool diagObjectTraceEnabled = diagRepeatEnabled && exportConfig.diag.objectTraceEnabled;
    const int diagObjectTraceMaxLines = exportConfig.diag.objectTraceMaxLines;
    const int diagObjectDiffThreshold = exportConfig.diag.objectDiffThreshold;
    int diagRawRepeatedAdjacent = 0;
    int diagRawRepeatedRuns = 0;
    int diagRawLongestRun = 1;
    int diagRawLongestRunStartFrame = -1;
    int diagRawRepeatedWithObjects = 0;
    int diagRawRepeatedWithEffects = 0;
    int diagLoggedLines = 0;
    quint64 previousRawSignature = 0;
    bool hasPreviousRawSignature = false;
    int rawRepeatRunStartFrame = 0;
    int rawRepeatRunLength = 1;
    int diagObjectRepeatedAdjacent = 0;
    int diagObjectRepeatedRuns = 0;
    int diagObjectLongestRun = 1;
    int diagObjectLongestRunStartFrame = -1;
    int diagObjectRepeatedWithObjects = 0;
    int diagObjectRepeatedWithEffects = 0;
    int diagObjectActiveFrames = 0;
    int diagObjectLoggedLines = 0;
    int diagObjectTraceLoggedLines = 0;
    quint64 previousObjectSignature = 0;
    bool hasPreviousObjectSignature = false;
    int objectRepeatRunStartFrame = 0;
    int objectRepeatRunLength = 1;
    const bool diagCompareRenderPathsRequested =
        diagRepeatEnabled && exportConfig.diag.compareRenderPathsEnabled;
    const bool diagPipeHashEnabled = diagRepeatEnabled && exportConfig.diag.pipeHashEnabled;
    const int diagPipeHashMaxLines = exportConfig.diag.pipeHashMaxLines;
    int diagPipeHashLoggedLines = 0;
    int diagPipeHashObjectFrames = 0;
    int diagPipeHashObjectRepeatedAdj = 0;
    quint64 previousObjectPackedHash = 0;
    bool hasPreviousObjectPackedHash = false;
    const QString diagRawDumpPath = exportConfig.diag.rawDumpPath;
    QFile diagRawDumpFile;
    bool diagRawDumpEnabled = false;
    qint64 diagRawDumpBytes = 0;
    int diagRawDumpFrames = 0;
    if (!diagRawDumpPath.isEmpty()) {
        const QString normalizedRawDumpPath = normalizePath(diagRawDumpPath);
        const QFileInfo rawDumpInfo(normalizedRawDumpPath);
        QDir().mkpath(rawDumpInfo.absolutePath());
        diagRawDumpFile.setFileName(normalizedRawDumpPath);
        if (diagRawDumpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            diagRawDumpEnabled = true;
            appendVideoExportLog(
                QStringLiteral("raw_dump_open"),
                QStringLiteral("path=%1").arg(normalizedRawDumpPath)
            );
        } else {
            appendVideoExportLog(
                QStringLiteral("raw_dump_open_failed"),
                QStringLiteral("path=%1 error=%2").arg(normalizedRawDumpPath, diagRawDumpFile.errorString())
            );
        }
    }

    quint64 previousSignature = 0;
    bool hasPreviousSignature = false;
    int repeatRunStartFrame = 0;
    int repeatRunLength = 1;
    QElapsedTimer frameTimer;
    VideoExportQuickRenderBackend diagReferenceCanvas;
    bool diagReferenceUseOffscreen = false;
    bool diagReferenceReady = false;

    if (useOffscreenGpu) {
        frameTimer.start();
        // The warmup primes the Quick scene graph (texture upload, shader
        // compile, vertex buffer build) at `qMax(0.0, timelineOriginSecond)`
        // — chart time 0 for full-range exports, `segmentStartSecond` for
        // partial exports. This is deliberately a chart time that has active
        // notes so the warmup compiles the sprite/effect shaders; it is no
        // longer "the playhead the first real frame will see" (full-range
        // now renders the negative lead-in window, whose opening frames can
        // be empty and would prime nothing — leaving the first real frames
        // to pay the compile cost and risk coming back as a transparent,
        // black-compositing overlay).
        const double warmupPlayheadSeconds = qMax(0.0, timelineOriginSecond);
        const QImage warmupFrame = exportCanvas.renderOverlayFrameOffscreen(
            frameSize,
            warmupPlayheadSeconds,
            task.showTimestamp,
            task.showObjectStatsHud
        );
        const qint64 warmupNs = frameTimer.nsecsElapsed();
        appendVideoExportLog(
            QStringLiteral("offscreen_warmup"),
            QStringLiteral("ok=%1 renderMs=%2 drawMs=%3 readMs=%4")
                .arg(warmupFrame.isNull() ? 0 : 1)
                .arg(warmupNs / 1000000.0, 0, 'f', 3)
                .arg(exportCanvas.offscreenDrawNsLastFrameForDebug() / 1000000.0, 0, 'f', 3)
                .arg(exportCanvas.offscreenReadbackNsLastFrameForDebug() / 1000000.0, 0, 'f', 3)
        );
        if (useOffscreenPboReadback) {
            exportCanvas.resetOffscreenPboReadback();
        }
    }
    if (diagObjectHashEnabled) {
        diagReferenceCanvas.copyRenderStateFrom(exportCanvas);
        diagReferenceCanvas.setBackgroundBrightnessOuter(task.backgroundBrightnessOuter);
        diagReferenceCanvas.setBackgroundBrightnessInner(task.backgroundBrightnessInner);
        diagReferenceCanvas.setLayoutSquareScale(task.layoutSquareScale);
        diagReferenceCanvas.setSmoothBrightness(task.smoothBrightness);
        diagReferenceCanvas.setBackgroundScaleMode(task.backgroundScaleMode);
        diagReferenceCanvas.setTapFlowSpeed(task.tapFlowSpeed);
        diagReferenceCanvas.setTouchFlowSpeed(task.touchFlowSpeed);
        diagReferenceCanvas.setShowDebugInfo(false);
        diagReferenceCanvas.setNoteMarkers({});
        QString diagInitError;
        if (useOffscreenGpu) {
            diagReferenceUseOffscreen = diagReferenceCanvas.initializeOffscreenRenderer(
                requestedFormat,
                shareContext,
                &diagInitError
            );
            if (!diagReferenceUseOffscreen) {
                appendVideoExportLog(
                    QStringLiteral("object_hash_ref_backend"),
                    QStringLiteral("offscreenInit=0 initError=%1 fallback=cpu").arg(diagInitError)
                );
            }
        }
        diagReferenceReady = true;
        if (diagReferenceUseOffscreen) {
            appendVideoExportLog(
                QStringLiteral("object_hash_ref_backend"),
                QStringLiteral("offscreenInit=1 gpuReady=%1")
                    .arg(diagReferenceCanvas.isGpuRendererReadyForDebug() ? 1 : 0)
            );
        }
    }
    if (diagCompareRenderPathsRequested) {
        appendVideoExportLog(
            QStringLiteral("render_path_compare_backend"),
            QStringLiteral("ignored=1 reason=legacy_path_removed")
        );
    }

    // Two pending slots are needed in steady state: one frame queued in
    // a PBO awaiting GPU readback, and one frame submitted to the
    // convert worker. The for-loop below pushes a new pending entry per
    // iteration and pops the oldest one as renderFramePboStep returns
    // it; the post-loop drain steps walk the deque to zero.
    std::deque<PendingPboFrame> pendingPboFrames;
    QImage convertedRgbaFrame;
    QByteArray packedFrameScratch;

    auto processReadyFrame = [&](const ReadyFramePayload& readyFrame) -> bool {
        const int frameIndex = readyFrame.frameIndex;
        const double exportSecond = readyFrame.exportSecond;
        const QVector<ObjectTraceItem>& traceItems = readyFrame.traceItems;
        // Local mutable copy: we may paint the lead-in pause overlay on top
        // before the frame is packed for FFmpeg (see further down). All the
        // diagnostics above the pack step still observe the un-overlaid
        // frame because the overlay is the very last mutation we perform.
        QImage frame = readyFrame.frame;
        const qint64 renderNs = readyFrame.renderNs;
        const qint64 offscreenDrawNs = readyFrame.offscreenDrawNs;
        const qint64 offscreenReadbackNs = readyFrame.offscreenReadbackNs;
        const bool usedOffscreenPath = readyFrame.usedOffscreenPath;
        const int fallbackCount = readyFrame.fallbackCount;
        const bool usedGpuRenderer = readyFrame.usedGpuRenderer;

        // Diagnostic: sample the first few rendered frames' RGBA so we can
        // tell whether the renderer is emitting opaque pixels (disc/HUD)
        // or a fully-transparent overlay (which composites to black on
        // FFmpeg's solid base). Sampled only for frames 0..4 + every
        // ~30 frames after, to bound log volume.
        if (!frame.isNull() && (frameIndex < 5 || frameIndex % 30 == 0)) {
            const QSize sampleSize = frame.size();
            const int sampleWidth = sampleSize.width();
            const int sampleHeight = sampleSize.height();
            if (sampleWidth > 0 && sampleHeight > 0) {
                qint64 alphaSum = 0;
                qint64 alphaMax = 0;
                qint64 sampleCount = 0;
                const int step = qMax(1, qMin(sampleWidth, sampleHeight) / 32);
                for (int y = 0; y < sampleHeight; y += step) {
                    for (int x = 0; x < sampleWidth; x += step) {
                        const QRgb pixel = frame.pixel(x, y);
                        const int alpha = qAlpha(pixel);
                        alphaSum += alpha;
                        alphaMax = qMax<qint64>(alphaMax, alpha);
                        ++sampleCount;
                    }
                }
                const double meanAlpha = sampleCount > 0
                    ? static_cast<double>(alphaSum) / static_cast<double>(sampleCount)
                    : 0.0;
                appendVideoExportLog(
                    QStringLiteral("frame_alpha_sample"),
                    QStringLiteral("frame=%1 t=%2 size=%3x%4 samples=%5 meanA=%6 maxA=%7")
                        .arg(frameIndex)
                        .arg(exportSecond, 0, 'f', 6)
                        .arg(sampleWidth)
                        .arg(sampleHeight)
                        .arg(sampleCount)
                        .arg(meanAlpha, 0, 'f', 2)
                        .arg(alphaMax));
            }
        }

        if (frame.isNull()) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("Render frame failed.");
            result.details = withExportLogPath(QStringLiteral("frame image is null"));
            appendVideoExportLog(
                QStringLiteral("fail_render_frame"),
                QStringLiteral("frame=%1 offscreen=%2").arg(frameIndex).arg(usedOffscreenPath ? 1 : 0)
            );
            return false;
        }
        if (usedGpuRenderer) {
            ++frameStats.gpuRenderedFrames;
        }
        frameStats.cpuFallbackTotal += qMax(0, fallbackCount);
        if (fallbackCount > frameStats.cpuFallbackMax) {
            frameStats.cpuFallbackMax = fallbackCount;
            frameStats.cpuFallbackMaxFrame = frameIndex;
        }

        if (diagObjectHashEnabled && diagReferenceReady) {
            QImage referenceFrame;
            if (diagReferenceUseOffscreen) {
                referenceFrame = diagReferenceCanvas.renderOverlayFrameOffscreen(
                    frameSize,
                    exportSecond,
                    task.showTimestamp,
                    task.showObjectStatsHud
                );
            }
            if (referenceFrame.isNull()) {
                referenceFrame = diagReferenceCanvas.renderOverlayFrame(
                    frameSize,
                    exportSecond,
                    task.showTimestamp,
                    task.showObjectStatsHud
                );
            }

            int objectPixels = 0;
            const quint64 objectSignature = objectOnlyFrameSignature(
                frame,
                referenceFrame,
                diagObjectDiffThreshold,
                &objectPixels
            );
            if (!traceItems.isEmpty()) {
                ++diagObjectActiveFrames;
            }

            if (hasPreviousObjectSignature) {
                if (objectSignature != 0 && objectSignature == previousObjectSignature) {
                    ++diagObjectRepeatedAdjacent;
                    if (objectRepeatRunLength == 1) {
                        objectRepeatRunStartFrame = frameIndex - 1;
                    }
                    ++objectRepeatRunLength;
                    if (objectRepeatRunLength == 2) {
                        ++diagObjectRepeatedRuns;
                    }
                    if (objectRepeatRunLength > diagObjectLongestRun) {
                        diagObjectLongestRun = objectRepeatRunLength;
                        diagObjectLongestRunStartFrame = objectRepeatRunStartFrame;
                    }

                    const FrameLayerActivityStats layerStats =
                        estimateFrameLayerActivity(exportMarkers, exportSecond);
                    const bool hasObjectActivity = layerStats.activeCoreObjects() > 0;
                    const bool hasEffectActivity = layerStats.activeEffects() > 0;
                    if (hasObjectActivity) {
                        ++diagObjectRepeatedWithObjects;
                    }
                    if (hasEffectActivity) {
                        ++diagObjectRepeatedWithEffects;
                    }
                    if ((diagLogAllRepeatPairs || hasObjectActivity || hasEffectActivity)
                        && diagObjectLoggedLines < diagMaxLogLines) {
                        appendVideoExportLog(
                            QStringLiteral("object_repeat_detail"),
                            QStringLiteral(
                                "frame=%1 t=%2 sig=0x%3 pixels=%4 core=%5 fx=%6 %7")
                                .arg(frameIndex)
                                .arg(exportSecond, 0, 'f', 6)
                                .arg(QString::number(objectSignature, 16))
                                .arg(objectPixels)
                                .arg(layerStats.activeCoreObjects())
                                .arg(layerStats.activeEffects())
                                .arg(layerStats.toCompactString())
                        );
                        ++diagObjectLoggedLines;
                    }
                } else {
                    objectRepeatRunLength = 1;
                }
            } else {
                objectRepeatRunStartFrame = frameIndex;
                objectRepeatRunLength = 1;
            }
            previousObjectSignature = objectSignature;
            hasPreviousObjectSignature = true;
        }

        // Lead-in / pre-range overlay: while the playfield is held
        // stationary at segmentStart, paint a semi-transparent pause
        // glyph on top so the viewer can tell at a glance that playback
        // hasn't started yet. Once outputSecond crosses leadInSeconds
        // the condition flips false and the overlay vanishes.
        //
        // The overlay only makes sense for partial-range exports — a
        // full-range export already begins at chart 0, and its 2 s
        // count-down is part of the deliverable (the user expects the
        // chart's start moment, including the visual count-in if any,
        // to be in the rendered video). Drawing the pause glyph on the
        // full-range lead-in was reported as a regression where the
        // exported `.mp4` looked like it was stuck on a frozen frame
        // for the first 2 s. partialRangeExport gates the draw. (Ranges
        // that start at chart 0 are classified full-range upstream even
        // when they end early, so they never reach this overlay either.)
        const double outputSecondForOverlay =
            static_cast<double>(frameIndex) / static_cast<double>(qMax(1, task.fps));
        const bool inLeadInOverlay =
            outputSecondForOverlay + kTimelineEpsilonSeconds < audioRenderPlan.leadInSeconds;
        if (inLeadInOverlay && partialRangeExport) {
            drawLeadInPauseOverlay(&frame);
            if (frameIndex < 6 || frameIndex % 30 == 0) {
                appendVideoExportLog(
                    QStringLiteral("lead_in_pause_overlay"),
                    QStringLiteral("frame=%1 output_s=%2 lead_in_s=%3 size=%4x%5")
                        .arg(frameIndex)
                        .arg(outputSecondForOverlay, 0, 'f', 4)
                        .arg(audioRenderPlan.leadInSeconds, 0, 'f', 4)
                        .arg(frame.width())
                        .arg(frame.height())
                );
            }
        }

        const char* packedFrameData = nullptr;
        qint64 packedFrameSize = 0;
        if (!preparePackedRgbaFrame(
                frame,
                &convertedRgbaFrame,
                &packedFrameScratch,
                &packedFrameData,
                &packedFrameSize)) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("Failed to pack RGBA frame.");
            result.details = withExportLogPath(result.details);
            appendVideoExportLog(
                QStringLiteral("fail_pack_frame"),
                QStringLiteral("frame=%1").arg(frameIndex)
            );
            return false;
        }
        const quint64 packedHash = diagPipeHashEnabled
            ? fnv1a64Bytes(packedFrameData, packedFrameSize)
            : 0;

        // Alpha-encoding sanity probe. We are claiming the bytes about to
        // hit the FFmpeg pipe are straight-alpha RGBA8888 (declared via
        // `-pix_fmt rgba` and consumed under `overlay=...:alpha=straight`).
        // Premultiplied data masquerading as straight would manifest as
        // every partial-alpha pixel having max(R,G,B) <= A. Conversely a
        // single sampled pixel with max(R,G,B) > A is definitive proof
        // the data really is straight.
        //
        // Sampling is gated to a tiny fixed set of frames (0, 1, 30, 60,
        // 120) and a handful of fixed positions to keep the log to
        // <= 5 short lines per export.
        if (packedFrameData != nullptr
            && packedFrameSize >= static_cast<qint64>(frameWidth) * frameHeight * 4
            && frameWidth > 0
            && frameHeight > 0
            && (frameIndex == 0 || frameIndex == 1 || frameIndex == 30
                || frameIndex == 60 || frameIndex == 120)) {
            static const double kSampleNorm[][2] = {
                {0.50, 0.50}, {0.50, 0.20}, {0.50, 0.80},
                {0.20, 0.50}, {0.80, 0.50}, {0.70, 0.30},
                {0.55, 0.70}, {0.05, 0.05}, {0.95, 0.95},
            };
            const int sampleCount = static_cast<int>(sizeof(kSampleNorm) / sizeof(kSampleNorm[0]));
            QStringList samplePieces;
            samplePieces.reserve(sampleCount);
            int partialAlphaCount = 0;
            int strictlyStraightCount = 0;
            for (int i = 0; i < sampleCount; ++i) {
                const int sx = qBound(0, static_cast<int>(kSampleNorm[i][0] * frameWidth), frameWidth - 1);
                const int sy = qBound(0, static_cast<int>(kSampleNorm[i][1] * frameHeight), frameHeight - 1);
                const qint64 off = (static_cast<qint64>(sy) * frameWidth + sx) * 4;
                const uchar r = static_cast<uchar>(packedFrameData[off + 0]);
                const uchar g = static_cast<uchar>(packedFrameData[off + 1]);
                const uchar b = static_cast<uchar>(packedFrameData[off + 2]);
                const uchar a = static_cast<uchar>(packedFrameData[off + 3]);
                const int rgbMax = qMax(qMax(r, g), b);
                const bool partialAlpha = (a > 0 && a < 255);
                const bool strictlyStraight = partialAlpha && (rgbMax > a);
                if (partialAlpha) ++partialAlphaCount;
                if (strictlyStraight) ++strictlyStraightCount;
                samplePieces.append(QStringLiteral("(%1,%2)%3/%4/%5/%6%7")
                    .arg(sx).arg(sy)
                    .arg(static_cast<int>(r)).arg(static_cast<int>(g))
                    .arg(static_cast<int>(b)).arg(static_cast<int>(a))
                    .arg(strictlyStraight ? QStringLiteral("!") : QString()));
            }
            const QString alphaVerdict = strictlyStraightCount > 0
                ? QStringLiteral("straight_confirmed")
                : (partialAlphaCount == 0
                    ? QStringLiteral("no_partial_alpha_sampled")
                    : QStringLiteral("ambiguous_no_strict_straight_pixel"));
            appendVideoExportLog(
                QStringLiteral("alpha_sample"),
                QStringLiteral("frame=%1 size=%2x%3 verdict=%4 partial=%5 strict_straight=%6 samples=%7")
                    .arg(frameIndex)
                    .arg(frameWidth).arg(frameHeight)
                    .arg(alphaVerdict)
                    .arg(partialAlphaCount)
                    .arg(strictlyStraightCount)
                    .arg(samplePieces.join(QLatin1Char(' ')))
            );
        }

        if (diagPipeHashEnabled && !traceItems.isEmpty()) {
            ++diagPipeHashObjectFrames;
            if (hasPreviousObjectPackedHash && packedHash == previousObjectPackedHash) {
                ++diagPipeHashObjectRepeatedAdj;
            }
            previousObjectPackedHash = packedHash;
            hasPreviousObjectPackedHash = true;

            if (diagPipeHashLoggedLines < diagPipeHashMaxLines) {
                appendVideoExportLog(
                    QStringLiteral("pipe_frame_hash"),
                    QStringLiteral("frame=%1 t=%2 bytes=%3 hash=0x%4 objects=%5")
                        .arg(frameIndex)
                        .arg(exportSecond, 0, 'f', 6)
                        .arg(packedFrameSize)
                        .arg(QString::number(packedHash, 16))
                        .arg(traceItems.size())
                );
                ++diagPipeHashLoggedLines;
            }
        }

        if (diagRawDumpEnabled && packedFrameSize > 0) {
            if (!writeAllToFile(&diagRawDumpFile, packedFrameData, packedFrameSize)) {
                ffmpeg.kill();
                ffmpeg.waitForFinished(2000);
                result.message = QStringLiteral("Failed to write raw dump frame.");
                result.details = withExportLogPath(diagRawDumpFile.errorString());
                appendVideoExportLog(
                    QStringLiteral("raw_dump_write_failed"),
                    QStringLiteral("frame=%1 path=%2 error=%3")
                        .arg(frameIndex)
                        .arg(diagRawDumpFile.fileName())
                        .arg(diagRawDumpFile.errorString())
                );
                return false;
            }
            diagRawDumpBytes += packedFrameSize;
            ++diagRawDumpFrames;
        }

        frameTimer.restart();
        QString ffmpegWriteFailure;
        qint64 producerWaitNs = 0;
        int queuedFramesAfterEnqueue = 0;
        // Zero-copy enqueue path with a frame-0 carve-out.
        //
        // Steady state: when `preparePackedRgbaFrame` returned a pointer
        // into one of the readback QImage buffers (the dominant case —
        // the readback frame is already RGBA8888 with packed stride), we
        // move a refcount-bumped copy of that QImage into the pump
        // packet via QByteArray::fromRawData. That eliminates a per-frame
        // deep copy of ~width*height*4 bytes (~3.5 MB at 720p, ~8 MB at
        // 1080p) into a fresh heap-owned QByteArray. Beta24 measurements
        // (ECHO,_MAS @ 1280×720 60 fps): avgWriteMs collapsed from
        // 1.55 ms → 0.018 ms and total export time fell ~22.7%.
        //
        // Frame-0 carve-out: a not-yet-identified mechanism corrupts a
        // 250×26 px region at the top of frame 0 only when zero-copy is
        // used. Diff with the deep-copy path showed the corrupted bytes
        // follow the SHAPE of the playfield outline's top arc + the two
        // top-most lane-marker dots — i.e. the outline texture's
        // contribution to those pixels is missing in the readback bytes
        // by the time the worker thread reads them. Frames 1+ are
        // pixel-identical to the deep-copy path, so the regression is
        // genuinely confined to frame 0. Working hypotheses (none
        // confirmed): (a) Qt's first-frame texture-atlas upload races
        // with the readback buffer reuse, (b) the first staging-ring slot
        // (PreviewQuickExportSession readbackRing_) doesn't trigger COW
        // detach the same way on the very first PBO-pipeline convert,
        // (c) heap memory aliasing between the outline QImage and the
        // readback QImage on the first frame only.
        //
        // Workaround: deep-copy frame 0, zero-copy frames 1+. The cost
        // is one ~width*height*4-byte memcpy on a single frame per
        // export, which is dominated by the readback synchronisation on
        // that frame anyway. Remove the carve-out and the fallback
        // branch once the root cause is found.
        bool enqueueOk = true;
        if (packedFrameSize > 0) {
            const bool forceDeepCopyForFrame0 = (frameIndex == 0);
            if (!forceDeepCopyForFrame0 && packedFrameScratch.isEmpty()) {
                QImage zeroCopyOwner = convertedRgbaFrame.isNull() ? frame : convertedRgbaFrame;
                enqueueOk = enqueueRawVideoFrame(
                    &rawVideoPipePump,
                    std::move(zeroCopyOwner),
                    frameIndex,
                    &producerWaitNs,
                    &queuedFramesAfterEnqueue,
                    &ffmpegWriteFailure);
            } else if (!packedFrameScratch.isEmpty()) {
                enqueueOk = enqueueRawVideoFrame(
                    &rawVideoPipePump,
                    std::move(packedFrameScratch),
                    frameIndex,
                    &producerWaitNs,
                    &queuedFramesAfterEnqueue,
                    &ffmpegWriteFailure);
            } else {
                // Frame-0 carve-out: deep-copy the bytes at enqueue time
                // so that any later mutation of the readback QImage's
                // buffer cannot influence what the worker thread writes
                // to the ffmpeg pipe. See the comment block above.
                enqueueOk = enqueueRawVideoFrame(
                    &rawVideoPipePump,
                    QByteArray(packedFrameData, static_cast<qsizetype>(packedFrameSize)),
                    frameIndex,
                    &producerWaitNs,
                    &queuedFramesAfterEnqueue,
                    &ffmpegWriteFailure);
            }
        }
        if (!enqueueOk) {
            const QString processSnapshot = describeProcessForLog(ffmpeg);
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            const QString ffmpegOutput = processOutputAndErrorForLog(ffmpeg, 2000);
            result.message = QStringLiteral("Failed to queue frame data for ffmpeg.");
            QStringList detailLines;
            if (!ffmpegWriteFailure.trimmed().isEmpty()) {
                detailLines.append(ffmpegWriteFailure.trimmed());
            }
            detailLines.append(processSnapshot);
            if (!ffmpegOutput.isEmpty()) {
                detailLines.append(ffmpegOutput);
            }
            result.details = detailLines.join(QStringLiteral("\n"));
            result.details = withExportLogPath(result.details);
            appendVideoExportLog(
                QStringLiteral("fail_raw_pipe_enqueue"),
                QStringLiteral("frame=%1 queuedFrames=%2 bytes=%3 hash=0x%4 failure=%5 %6 output=%7")
                    .arg(frameIndex)
                    .arg(queuedFramesAfterEnqueue)
                    .arg(packedFrameSize)
                    .arg(QString::number(packedHash, 16))
                    .arg(truncateForLog(ffmpegWriteFailure, 400))
                    .arg(processSnapshot)
                    .arg(truncateForLog(ffmpegOutput, 1000))
            );
            return false;
        }
        const qint64 writeNs = frameTimer.nsecsElapsed();

        frameStats.renderTotalNs += renderNs;
        frameStats.writeTotalNs += writeNs;
        frameStats.offscreenDrawTotalNs += qMax<qint64>(0, offscreenDrawNs);
        frameStats.offscreenReadbackTotalNs += qMax<qint64>(0, offscreenReadbackNs);
        if (renderNs > frameStats.renderMaxNs) {
            frameStats.renderMaxNs = renderNs;
            frameStats.renderMaxFrame = frameIndex;
        }
        if (writeNs > frameStats.writeMaxNs) {
            frameStats.writeMaxNs = writeNs;
            frameStats.writeMaxFrame = frameIndex;
        }
        if (offscreenDrawNs > frameStats.offscreenDrawMaxNs) {
            frameStats.offscreenDrawMaxNs = offscreenDrawNs;
            frameStats.offscreenDrawMaxFrame = frameIndex;
        }
        if (offscreenReadbackNs > frameStats.offscreenReadbackMaxNs) {
            frameStats.offscreenReadbackMaxNs = offscreenReadbackNs;
            frameStats.offscreenReadbackMaxFrame = frameIndex;
        }
        if (renderNs > frameBudgetNs) {
            ++frameStats.overBudgetRenderFrames;
        }
        if (writeNs > frameBudgetNs) {
            ++frameStats.overBudgetWriteFrames;
        }

        const quint64 signature = sampledFrameSignature(frame);
        if (hasPreviousSignature) {
            if (signature == previousSignature) {
                ++frameStats.repeatedAdjacentFrames;
                if (repeatRunLength == 1) {
                    repeatRunStartFrame = frameIndex - 1;
                }
                ++repeatRunLength;
                if (repeatRunLength == 2) {
                    ++frameStats.repeatedRuns;
                }
                if (repeatRunLength > frameStats.longestRepeatedRun) {
                    frameStats.longestRepeatedRun = repeatRunLength;
                    frameStats.longestRepeatedRunStartFrame = repeatRunStartFrame;
                }
            } else {
                repeatRunLength = 1;
            }
        } else {
            repeatRunStartFrame = frameIndex;
            repeatRunLength = 1;
        }
        previousSignature = signature;
        hasPreviousSignature = true;

        if (diagRepeatEnabled) {
            const quint64 rawSignature = fullFrameSignature(frame, diagCropBottom);
            if (hasPreviousRawSignature) {
                if (rawSignature == previousRawSignature) {
                    ++diagRawRepeatedAdjacent;
                    if (rawRepeatRunLength == 1) {
                        rawRepeatRunStartFrame = frameIndex - 1;
                    }
                    ++rawRepeatRunLength;
                    if (rawRepeatRunLength == 2) {
                        ++diagRawRepeatedRuns;
                    }
                    if (rawRepeatRunLength > diagRawLongestRun) {
                        diagRawLongestRun = rawRepeatRunLength;
                        diagRawLongestRunStartFrame = rawRepeatRunStartFrame;
                    }

                    const FrameLayerActivityStats layerStats =
                        estimateFrameLayerActivity(exportMarkers, exportSecond);
                    const bool hasObjectActivity = layerStats.activeCoreObjects() > 0;
                    const bool hasEffectActivity = layerStats.activeEffects() > 0;
                    if (hasObjectActivity) {
                        ++diagRawRepeatedWithObjects;
                    }
                    if (hasEffectActivity) {
                        ++diagRawRepeatedWithEffects;
                    }
                    if ((diagLogAllRepeatPairs || hasObjectActivity || hasEffectActivity)
                        && diagLoggedLines < diagMaxLogLines) {
                        appendVideoExportLog(
                            QStringLiteral("raw_repeat_detail"),
                            QStringLiteral(
                                "frame=%1 t=%2 sig=0x%3 core=%4 fx=%5 %6")
                                .arg(frameIndex)
                                .arg(exportSecond, 0, 'f', 6)
                                .arg(QString::number(rawSignature, 16))
                                .arg(layerStats.activeCoreObjects())
                                .arg(layerStats.activeEffects())
                                .arg(layerStats.toCompactString())
                        );
                        ++diagLoggedLines;
                    }
                } else {
                    rawRepeatRunLength = 1;
                }
            } else {
                rawRepeatRunStartFrame = frameIndex;
                rawRepeatRunLength = 1;
            }
            previousRawSignature = rawSignature;
            hasPreviousRawSignature = true;
        }

        const bool shouldLogProgress = (frameIndex == 0)
            || (((frameIndex + 1) % kFrameProgressStride) == 0)
            || (frameIndex + 1 == frameCount)
            || (renderNs >= kFrameStallLogNs)
            || (writeNs >= kFrameStallLogNs);
        if (producerWaitNs > 0 && (shouldLogProgress || producerWaitNs >= kFrameStallLogNs)) {
            appendVideoExportLog(
                QStringLiteral("raw_pipe_backpressure"),
                QStringLiteral("frame=%1/%2 waitMs=%3 queuedFrames=%4 peakQueuedFrames=%5")
                    .arg(frameIndex + 1)
                    .arg(frameCount)
                    .arg(producerWaitNs / 1000000.0, 0, 'f', 3)
                    .arg(queuedFramesAfterEnqueue)
                    .arg(rawVideoPipePump.stats.maxQueuedFrames)
            );
        }
        if (shouldLogProgress) {
            appendVideoExportLog(
                QStringLiteral("frame_timing"),
                QStringLiteral("frame=%1/%2 t=%3 renderMs=%4 writeMs=%5 overBudgetR=%6 overBudgetW=%7 sameAdj=%8 longestRun=%9@%10 gpuFrame=%11 fallback=%12 offscreen=%13 offDrawMs=%14 offReadMs=%15 sig=0x%16")
                    .arg(frameIndex + 1)
                    .arg(frameCount)
                    .arg(exportSecond, 0, 'f', 6)
                    .arg(renderNs / 1000000.0, 0, 'f', 3)
                    .arg(writeNs / 1000000.0, 0, 'f', 3)
                    .arg(frameStats.overBudgetRenderFrames)
                    .arg(frameStats.overBudgetWriteFrames)
                    .arg(frameStats.repeatedAdjacentFrames)
                    .arg(frameStats.longestRepeatedRun)
                    .arg(frameStats.longestRepeatedRunStartFrame)
                    .arg(usedGpuRenderer ? 1 : 0)
                    .arg(fallbackCount)
                    .arg(usedOffscreenPath ? 1 : 0)
                    .arg(offscreenDrawNs / 1000000.0, 0, 'f', 3)
                    .arg(offscreenReadbackNs / 1000000.0, 0, 'f', 3)
                    .arg(QString::number(signature, 16))
            );
        }
        const int framePercent = qBound(
            8,
            8 + qRound(static_cast<double>(frameIndex + 1) * 82.0 / static_cast<double>(qMax(1, frameCount))),
            90
        );
        if (setProgressPercent(framePercent, QStringLiteral("Rendering frames and encoding... %1/%2").arg(frameIndex + 1).arg(frameCount))) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("canceled");
            result.details = withExportLogPath(result.details);
            appendVideoExportLog(QStringLiteral("canceled"), QStringLiteral("stage=frame_progress frame=%1").arg(frameIndex));
            return false;
        }
        if (frameIndex == 0 || ((frameIndex + 1) % 300) == 0 || frameIndex + 1 == frameCount) {
            appendVideoExportLog(
                QStringLiteral("frame_progress"),
                QStringLiteral("written=%1/%2").arg(frameIndex + 1).arg(frameCount)
            );
        }
        return true;
    };

    for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        if (ffmpeg.state() != QProcess::Running) {
            const QString processSnapshot = describeProcessForLog(ffmpeg);
            ffmpeg.waitForFinished(2000);
            const QString ffmpegOutput = processOutputAndErrorForLog(ffmpeg, 2000);
            result.message = QStringLiteral("ffmpeg exited unexpectedly during frame piping.");
            QStringList detailLines;
            detailLines.append(processSnapshot);
            if (!ffmpegOutput.isEmpty()) {
                detailLines.append(ffmpegOutput);
            }
            result.details = detailLines.join(QStringLiteral("\n"));
            result.details = withExportLogPath(result.details);
            appendVideoExportLog(
                QStringLiteral("fail_ffmpeg_early_exit"),
                QStringLiteral("frame=%1 %2 output=%3")
                    .arg(frameIndex)
                    .arg(processSnapshot)
                    .arg(truncateForLog(ffmpegOutput, 1000))
            );
            return result;
        }
        if (setProgressPercent(-1, QString())) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("canceled");
            result.details = withExportLogPath(result.details);
            appendVideoExportLog(QStringLiteral("canceled"), QStringLiteral("stage=frame_loop frame=%1").arg(frameIndex));
            return result;
        }
        const double outputSecond = static_cast<double>(frameIndex) / task.fps;
        const bool inLeadInOrPreload =
            outputSecond + kTimelineEpsilonSeconds < audioRenderPlan.leadInSeconds;
        // Render the chart playfield + HUD throughout the lead-in / preload
        // instead of emitting a transparent overlay (which composites to
        // black on FFmpeg's solid base).
        //   * Full-range exports actually play back the negative chart-time
        //     window (-leadIn → 0) so notes/effects scheduled before chart 0
        //     (e.g. approach animations for notes near second 0, or markers
        //     whose `second` is itself negative) render naturally instead of
        //     popping into existence at a frozen chart-zero frame.
        //   * Partial-range exports keep the chart frozen at segmentStart
        //     throughout the freeze window (drawLeadInPauseOverlay also
        //     paints a pause glyph on top), and the HUD is held on
        //     segmentStart too — the user expectation is "nothing animates
        //     during the freeze"; a ramping HUD would contradict that.
        const double rawChartSecond = timelineOriginSecond + outputSecond;
        const double exportSecond = partialRangeExport
            ? audioRenderPlan.segmentStartSecond + qMax(0.0, outputSecond - audioRenderPlan.leadInSeconds)
            : rawChartSecond;
        // HUD override during the pre-roll window:
        //   * Full-range exports show the count-down (chart-time ramps from
        //     -leadIn → 0). Since exportSecond now carries that negative
        //     value through to the scene, this override is effectively the
        //     same as state.playheadSeconds, but we keep it explicit so the
        //     HUD path is independent of any future scene-time clamping.
        //   * Partial-range exports want the HUD frozen on segmentStart
        //     (matches the frozen chart playfield + the pause overlay).
        //     The user expectation is "nothing animates during the freeze";
        //     a ramping HUD here would contradict that.
        const double hudPlayheadSecondsOverride =
            inLeadInOrPreload
                ? (partialRangeExport
                       ? audioRenderPlan.segmentStartSecond
                       : rawChartSecond)
                : std::numeric_limits<double>::quiet_NaN();
        // Pre-roll intro: advance the overlay `frame` and suppress the HUD while
        // it plays. The overlay sits above the HUD, so during cycle 2 (its
        // transparent centre) the count-down timestamp would otherwise show
        // through; hiding it keeps the intro clean.
        const int introAuthoringFrame =
            miacode::intro::authoringFrameForOutputFrame(frameIndex, task.fps);
        const bool inIntroWindow =
            introEnabled && frameIndex < audioRenderPlan.introFrameCount;
        // HUD / timestamp hide only while the maimai wipe still covers the chart;
        // once it retracts (kHudRevealFrame) they show over the black/fading
        // background, unaffected by the bg fade.
        const bool inIntroCover =
            introEnabled && introAuthoringFrame < miacode::intro::kHudRevealFrame;
        if (introEnabled) {
            exportCanvas.setIntroFrame(introAuthoringFrame, inIntroWindow);
        }
        const bool showTimestampThisFrame = task.showTimestamp && !inIntroCover;
        const bool showObjectStatsThisFrame = task.showObjectStatsHud && !inIntroCover;
        QVector<ObjectTraceItem> traceItems;
        if (diagObjectTraceEnabled || diagObjectHashEnabled) {
            traceItems = collectVisibleObjectTrace(
                exportMarkers,
                exportSecond,
                frameSize.width(),
                frameSize.height(),
                task.layoutSquareScale
            );
        }
        if (diagObjectTraceEnabled
            && !traceItems.isEmpty()
            && diagObjectTraceLoggedLines < diagObjectTraceMaxLines) {
            QStringList encodedItems;
            encodedItems.reserve(traceItems.size());
            for (const ObjectTraceItem& item : traceItems) {
                encodedItems.append(item.compact());
            }
            appendVideoExportLog(
                QStringLiteral("object_frame_trace"),
                QStringLiteral("frame=%1 t=%2 count=%3 objects=%4")
                    .arg(frameIndex)
                    .arg(exportSecond, 0, 'f', 6)
                    .arg(traceItems.size())
                    .arg(truncateForLog(encodedItems.join(';'), 16000))
            );
            ++diagObjectTraceLoggedLines;
        }

        ReadyFramePayload readyFrame;
        QString renderBackendFallbackDetail;
        const ExportFrameRenderStatus renderStatus = renderExportFrameWithConfiguredBackend(
            &exportCanvas,
            &useOffscreenGpu,
            &useOffscreenPboReadback,
            &pendingPboFrames,
            frameSize,
            frameIndex,
            exportSecond,
            showTimestampThisFrame,
            showObjectStatsThisFrame,
            std::move(traceItems),
            &readyFrame,
            &renderBackendFallbackDetail,
            hudPlayheadSecondsOverride
        );
        if (!renderBackendFallbackDetail.isEmpty()) {
            appendVideoExportLog(QStringLiteral("render_backend_fallback"), renderBackendFallbackDetail);
        }
        if (renderStatus == ExportFrameRenderStatus::Failed) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("Render frame failed.");
            result.details = withExportLogPath(QStringLiteral("render backend failed unexpectedly"));
            appendVideoExportLog(
                QStringLiteral("fail_render_frame"),
                QStringLiteral("frame=%1 offscreen=%2").arg(frameIndex).arg(useOffscreenGpu ? 1 : 0)
            );
            return result;
        }
        if (renderStatus == ExportFrameRenderStatus::Deferred) {
            continue;
        }
        if (!processReadyFrame(readyFrame)) {
            return result;
        }
    }

    while (useOffscreenPboReadback && !pendingPboFrames.empty()) {
        const int drainFrameIndex = pendingPboFrames.front().frameIndex;
        ReadyFramePayload readyFrame;
        QString drainError;
        if (!drainPendingExportFrame(
                &exportCanvas,
                &pendingPboFrames,
                frameSize,
                task.showTimestamp,
                task.showObjectStatsHud,
                &readyFrame,
                &drainError)) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("Render frame failed.");
            result.details = withExportLogPath(
                drainError.isEmpty() ? QStringLiteral("failed to drain PBO readback") : drainError);
            appendVideoExportLog(
                QStringLiteral("fail_render_frame"),
                QStringLiteral("frame=%1 offscreen=1 drain=1 error=%2")
                    .arg(drainFrameIndex)
                    .arg(drainError.isEmpty() ? QStringLiteral("unknown") : drainError)
            );
            return result;
        }
        if (!processReadyFrame(readyFrame)) {
            return result;
        }
    }

    appendVideoExportLog(
        QStringLiteral("frame_timing_summary"),
        QStringLiteral(
            "frames=%1 avgRenderMs=%2 avgWriteMs=%3 maxRenderMs=%4@%5 maxWriteMs=%6@%7 "
            "overBudgetR=%8 overBudgetW=%9 repeatedAdj=%10 repeatedRuns=%11 longestRun=%12@%13 "
            "gpuFrames=%14 avgFallback=%15 maxFallback=%16@%17 "
            "avgOffDrawMs=%18 avgOffReadMs=%19 maxOffDrawMs=%20@%21 maxOffReadMs=%22@%23")
            .arg(frameCount)
            .arg((frameStats.renderTotalNs / 1000000.0) / qMax(1, frameCount), 0, 'f', 3)
            .arg((frameStats.writeTotalNs / 1000000.0) / qMax(1, frameCount), 0, 'f', 3)
            .arg(frameStats.renderMaxNs / 1000000.0, 0, 'f', 3)
            .arg(frameStats.renderMaxFrame)
            .arg(frameStats.writeMaxNs / 1000000.0, 0, 'f', 3)
            .arg(frameStats.writeMaxFrame)
            .arg(frameStats.overBudgetRenderFrames)
            .arg(frameStats.overBudgetWriteFrames)
            .arg(frameStats.repeatedAdjacentFrames)
            .arg(frameStats.repeatedRuns)
            .arg(frameStats.longestRepeatedRun)
            .arg(frameStats.longestRepeatedRunStartFrame)
            .arg(frameStats.gpuRenderedFrames)
            .arg(static_cast<double>(frameStats.cpuFallbackTotal) / qMax(1, frameCount), 0, 'f', 3)
            .arg(frameStats.cpuFallbackMax)
            .arg(frameStats.cpuFallbackMaxFrame)
            .arg((frameStats.offscreenDrawTotalNs / 1000000.0) / qMax(1, frameCount), 0, 'f', 3)
            .arg((frameStats.offscreenReadbackTotalNs / 1000000.0) / qMax(1, frameCount), 0, 'f', 3)
            .arg(frameStats.offscreenDrawMaxNs / 1000000.0, 0, 'f', 3)
            .arg(frameStats.offscreenDrawMaxFrame)
            .arg(frameStats.offscreenReadbackMaxNs / 1000000.0, 0, 'f', 3)
            .arg(frameStats.offscreenReadbackMaxFrame)
    );
    appendVideoExportLog(
        QStringLiteral("raw_pipe_summary"),
        QStringLiteral(
            "transport=%1 frameMiB=%2 bufferMiB=%3 maxBufferedFrames=%4 peakQueuedFrames=%5 "
            "connectMs=%6 totalProducerWaitMs=%7 avgProducerWaitMs=%8 maxProducerWaitMs=%9@%10 "
            "totalPipeWriteMs=%11 avgPipeWriteMs=%12 maxPipeWriteMs=%13@%14")
            .arg(rawVideoPipeTransportName(rawVideoPipePump.pipe.transport))
            .arg(rawVideoPipePlan.frameBytes / (1024.0 * 1024.0), 0, 'f', 2)
            .arg((rawVideoPipePump.pipe.configuredBufferBytes > 0
                      ? rawVideoPipePump.pipe.configuredBufferBytes
                      : rawVideoPipePlan.requestedBufferBytes)
                     / (1024.0 * 1024.0),
                0,
                'f',
                2)
            .arg(rawVideoPipePlan.maxBufferedFrames)
            .arg(rawVideoPipePump.stats.maxQueuedFrames)
            .arg(rawVideoPipePump.stats.connectElapsedMs)
            .arg(rawVideoPipePump.stats.totalProducerWaitNs / 1000000.0, 0, 'f', 3)
            .arg(frameCount > 0
                    ? (rawVideoPipePump.stats.totalProducerWaitNs / 1000000.0)
                        / static_cast<double>(frameCount)
                    : 0.0,
                0,
                'f',
                3)
            .arg(rawVideoPipePump.stats.maxProducerWaitNs / 1000000.0, 0, 'f', 3)
            .arg(rawVideoPipePump.stats.maxProducerWaitFrame)
            .arg(rawVideoPipePump.stats.totalPipeWriteNs / 1000000.0, 0, 'f', 3)
            .arg(frameCount > 0
                    ? (rawVideoPipePump.stats.totalPipeWriteNs / 1000000.0)
                        / static_cast<double>(frameCount)
                    : 0.0,
                0,
                'f',
                3)
            .arg(rawVideoPipePump.stats.maxPipeWriteNs / 1000000.0, 0, 'f', 3)
            .arg(rawVideoPipePump.stats.maxPipeWriteFrame)
    );
    if (diagRepeatEnabled) {
        appendVideoExportLog(
            QStringLiteral("raw_repeat_summary"),
            QStringLiteral(
                "cropBottom=%1 repeatedAdj=%2 repeatedRuns=%3 longestRun=%4@%5 "
                "repeatWithObjects=%6 repeatWithEffects=%7 logged=%8")
                .arg(diagCropBottom)
                .arg(diagRawRepeatedAdjacent)
                .arg(diagRawRepeatedRuns)
                .arg(diagRawLongestRun)
                .arg(diagRawLongestRunStartFrame)
                .arg(diagRawRepeatedWithObjects)
                .arg(diagRawRepeatedWithEffects)
                .arg(diagLoggedLines)
        );
        if (diagObjectHashEnabled) {
            appendVideoExportLog(
                QStringLiteral("object_repeat_summary"),
                QStringLiteral(
                    "diffThreshold=%1 repeatedAdj=%2 repeatedRuns=%3 longestRun=%4@%5 "
                    "repeatWithObjects=%6 repeatWithEffects=%7 activeFrames=%8 logged=%9")
                    .arg(diagObjectDiffThreshold)
                    .arg(diagObjectRepeatedAdjacent)
                    .arg(diagObjectRepeatedRuns)
                    .arg(diagObjectLongestRun)
                    .arg(diagObjectLongestRunStartFrame)
                    .arg(diagObjectRepeatedWithObjects)
                    .arg(diagObjectRepeatedWithEffects)
                    .arg(diagObjectActiveFrames)
                    .arg(diagObjectLoggedLines)
            );
        }
        if (diagObjectTraceEnabled) {
            appendVideoExportLog(
                QStringLiteral("object_trace_summary"),
                QStringLiteral("logged=%1 max=%2").arg(diagObjectTraceLoggedLines).arg(diagObjectTraceMaxLines)
            );
        }
    }
    if (diagCompareRenderPathsRequested) {
        appendVideoExportLog(
            QStringLiteral("render_path_compare_summary"),
            QStringLiteral("ignored=1 reason=legacy_path_removed")
        );
    }
    if (diagPipeHashEnabled) {
        appendVideoExportLog(
            QStringLiteral("pipe_hash_summary"),
            QStringLiteral("objectFrames=%1 repeatedAdj=%2 logged=%3")
                .arg(diagPipeHashObjectFrames)
                .arg(diagPipeHashObjectRepeatedAdj)
                .arg(diagPipeHashLoggedLines)
        );
    }
    if (diagRawDumpEnabled) {
        diagRawDumpFile.flush();
        diagRawDumpFile.close();
        appendVideoExportLog(
            QStringLiteral("raw_dump_summary"),
            QStringLiteral("path=%1 frames=%2 bytes=%3")
                .arg(diagRawDumpFile.fileName())
                .arg(diagRawDumpFrames)
                .arg(diagRawDumpBytes)
        );
    }

    if (!finishRawVideoPipePump(&rawVideoPipePump, &rawVideoPipeFailure)) {
        const QString processSnapshot = describeProcessForLog(ffmpeg);
        ffmpeg.kill();
        ffmpeg.waitForFinished(2000);
        const QString ffmpegOutput = processOutputAndErrorForLog(ffmpeg, 2000);
        result.message = QStringLiteral("Failed to finalize raw pipe stream.");
        QStringList detailLines;
        if (!rawVideoPipeFailure.trimmed().isEmpty()) {
            detailLines.append(rawVideoPipeFailure.trimmed());
        }
        detailLines.append(processSnapshot);
        if (!ffmpegOutput.isEmpty()) {
            detailLines.append(ffmpegOutput);
        }
        result.details = withExportLogPath(detailLines.join(QStringLiteral("\n")));
        appendVideoExportLog(
            QStringLiteral("fail_raw_pipe_finalize"),
            QStringLiteral("failure=%1 %2 output=%3")
                .arg(truncateForLog(rawVideoPipeFailure, 400))
                .arg(processSnapshot)
                .arg(truncateForLog(ffmpegOutput, 1000))
        );
        return result;
    }
    if (!waitForProcessWithProgress(
            ffmpeg,
            QStringLiteral("ffmpeg_encode_finalize_wait_begin"),
            QStringLiteral("ffmpeg_encode_finalize_wait_done"),
            QStringLiteral("Finalizing encoded video stream..."),
            90,
            setProgressPercent,
            QStringLiteral("stage=ffmpeg_encode_finalize_wait"),
            &result)) {
        return result;
    }
    if (ffmpeg.exitStatus() != QProcess::NormalExit) {
        result.message = QStringLiteral("ffmpeg process failed.");
        const QString processSnapshot = describeProcessForLog(ffmpeg);
        const QString ffmpegOutput = processOutputAndErrorForLog(ffmpeg, 2000);
        QStringList detailLines;
        detailLines.append(processSnapshot);
        if (!ffmpegOutput.isEmpty()) {
            detailLines.append(ffmpegOutput);
        }
        result.details = detailLines.join(QStringLiteral("\n"));
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(
            QStringLiteral("fail_ffmpeg_wait"),
            QStringLiteral("%1 output=%2").arg(processSnapshot, truncateForLog(ffmpegOutput, 1000))
        );
        return result;
    }

    const QString ffmpegOutput = processOutputAndErrorForLog(ffmpeg, 2000);
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        result.message = QStringLiteral("ffmpeg encode failed.");
        const QString processSnapshot = describeProcessForLog(ffmpeg);
        QStringList detailLines;
        detailLines.append(processSnapshot);
        if (!ffmpegOutput.isEmpty()) {
            detailLines.append(ffmpegOutput);
        } else {
            detailLines.append(ffmpegBaseArgsLog(ffmpegPath, args));
        }
        result.details = detailLines.join(QStringLiteral("\n"));
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(
            QStringLiteral("fail_ffmpeg_exit"),
            QStringLiteral("%1 output=%2")
                .arg(processSnapshot)
                .arg(truncateForLog(ffmpegOutput, 1000))
        );
        return result;
    }

    const QFileInfo encodedTempInfo(encodedTempPath);
    appendVideoExportLog(
        QStringLiteral("encode_output_file"),
        QStringLiteral("path=%1 sizeBytes=%2")
            .arg(encodedTempPath)
            .arg(encodedTempInfo.exists() ? encodedTempInfo.size() : -1)
    );

    if (setProgressPercent(94, QStringLiteral("Repacking MP4 for fast start..."))) {
        result.message = QStringLiteral("canceled");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("canceled"), QStringLiteral("stage=remux_prepare"));
        return result;
    }

    QStringList remuxArgs{
        QStringLiteral("-y"),
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-i"),
        encodedTempPath,
        QStringLiteral("-c"),
        QStringLiteral("copy"),
        QStringLiteral("-movflags"),
        QStringLiteral("+faststart"),
        remuxStagePath
    };
    appendVideoExportLog(
        QStringLiteral("ffmpeg_remux_args"),
        truncateForLog(ffmpegBaseArgsLog(ffmpegPath, remuxArgs), 8000)
    );

    QProcess remuxProcess;
    remuxProcess.setProcessChannelMode(QProcess::MergedChannels);
    remuxProcess.start(ffmpegPath, remuxArgs, QIODevice::ReadOnly);
    if (!remuxProcess.waitForStarted(5000)) {
        result.message = QStringLiteral("Failed to start ffmpeg remux stage.");
        result.details = withExportLogPath(remuxProcess.errorString());
        appendVideoExportLog(
            QStringLiteral("fail_ffmpeg_remux_start"),
            QStringLiteral("error=%1").arg(remuxProcess.errorString())
        );
        return result;
    }
    appendVideoExportLog(QStringLiteral("ffmpeg_remux_started"));
    appendVideoExportLog(QStringLiteral("ffmpeg_remux_process_started"), describeProcessForLog(remuxProcess));
    if (!waitForProcessWithProgress(
            remuxProcess,
            QStringLiteral("ffmpeg_remux_wait_begin"),
            QStringLiteral("ffmpeg_remux_wait_done"),
            QStringLiteral("Repacking MP4 for fast start..."),
            96,
            setProgressPercent,
            QStringLiteral("stage=ffmpeg_remux_wait"),
            &result)) {
        QFile::remove(remuxStagePath);
        return result;
    }
    const QString remuxOutput = processOutputAndErrorForLog(remuxProcess, 2000);
    if (remuxProcess.exitStatus() != QProcess::NormalExit || remuxProcess.exitCode() != 0) {
        result.message = QStringLiteral("ffmpeg remux failed.");
        const QString processSnapshot = describeProcessForLog(remuxProcess);
        QStringList detailLines;
        detailLines.append(processSnapshot);
        if (!remuxOutput.isEmpty()) {
            detailLines.append(remuxOutput);
        }
        result.details = withExportLogPath(detailLines.join(QStringLiteral("\n")));
        appendVideoExportLog(
            QStringLiteral("fail_ffmpeg_remux_exit"),
            QStringLiteral("%1 output=%2")
                .arg(processSnapshot)
                .arg(truncateForLog(remuxOutput, 1000))
        );
        QFile::remove(remuxStagePath);
        return result;
    }

    QString promoteError;
    if (!replaceOutputFileAtomicallyBestEffort(remuxStagePath, task.outputPath, &promoteError)) {
        result.message = QStringLiteral("Failed to finalize output file.");
        result.details = withExportLogPath(
            QStringLiteral("%1\nStaged file: %2").arg(promoteError, remuxStagePath)
        );
        appendVideoExportLog(
            QStringLiteral("fail_output_promote"),
            QStringLiteral("error=%1 staged=%2 final=%3")
                .arg(promoteError, remuxStagePath, task.outputPath)
        );
        return result;
    }

    const QFileInfo outputInfo(task.outputPath);
    appendVideoExportLog(
        QStringLiteral("export_file"),
        QStringLiteral("path=%1 sizeBytes=%2").arg(task.outputPath).arg(outputInfo.exists() ? outputInfo.size() : -1)
    );
    setProgressPercent(95, QStringLiteral("Collecting export summary..."));
    appendVideoExportLog(
        QStringLiteral("ffprobe_summary"),
        probeExportedVideoSummary(ffprobePath, task.outputPath)
    );

    setProgressPercent(100, QStringLiteral("Export completed."));
    result.success = true;
    result.message = QStringLiteral("ok");
    appendVideoExportLog(
        QStringLiteral("export_success"),
        QStringLiteral("output=%1 elapsedMs=%2").arg(task.outputPath).arg(exportTimer.elapsed())
    );
    return result;
}
