#pragma once

// Internal shared definitions for the VideoExportController translation units.
//
// This header was extracted verbatim from VideoExportController.cpp during the
// god-file split. It holds the file-local using-declarations, constants, and the
// 17 helper struct/enum definitions, plus prototypes for every free helper
// function whose definition now lives in one of the VideoExport*.cpp group TUs.
//
// All entities live in namespace miacode::video_export::detail (formerly an
// anonymous namespace). Each group TU opens that same namespace for its
// definitions and adds `using namespace miacode::video_export::detail;` at file
// scope so the VideoExportController member methods call the helpers unqualified.

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
#include <memory>
#include <optional>

namespace miacode::video_export::detail {

// ---- file-local constants + using-declarations (from anon namespace) ----
constexpr double kTimelineEpsilonSeconds = miacode::preview_sfx_timeline::kTimelineEpsilonSeconds;

using miacode::preview_audio::kMixChannels;
using miacode::preview_audio::kMixSampleRate;
using miacode::video_export::raw_pipe::enqueueRawVideoFrame;
using miacode::video_export::raw_pipe::finishRawVideoPipePump;
using miacode::video_export::raw_pipe::RawVideoPipePlan;
using miacode::video_export::raw_pipe::RawVideoPipePump;
using miacode::video_export::raw_pipe::rawVideoPipeTransportName;
using miacode::video_export::raw_pipe::shutdownRawVideoPipePump;
using miacode::video_export::raw_pipe::startRawVideoPipe;
using miacode::video_export::raw_pipe::startRawVideoPipePumpThread;
using miacode::video_export::raw_pipe::chooseRawVideoPipePlan;

// ---- helper structs / enums (verbatim) ----
struct VideoEncoderConfig {
    QString codec;
    QStringList extraArgs;
    bool isHardware = false;
    int explicitBframes = -1;
};

struct SystemMemoryInfo {
    bool valid = false;
    quint64 totalPhysicalBytes = 0;
    quint64 availablePhysicalBytes = 0;
};

struct VideoBitratePlan {
    qint64 bitrateKbps = 0;
    qint64 maxRateKbps = 0;
    qint64 bufSizeKbps = 0;

    QStringList toArgs() const
    {
        return QStringList{
            QStringLiteral("-b:v"), QString::number(bitrateKbps) + QLatin1String("k"),
            QStringLiteral("-maxrate"), QString::number(maxRateKbps) + QLatin1String("k"),
            QStringLiteral("-bufsize"), QString::number(bufSizeKbps) + QLatin1String("k")
        };
    }
};

struct X264TuningPlan {
    // beta25 — defaults retuned for compactness. The previous
    // {preset=fast, crf=21, bf=0} chased fast wallclock encode at the
    // cost of ~25-35% larger files than the x264 stock defaults at the
    // same visual quality. An interim {crf=23, bf=3} pass leaned toward
    // stock x264 behaviour for size, but bf=3 reorders chroma references
    // and produced visible dark "ringing" halos around high-contrast
    // playfield overlays on yuv420p exports (see izanai_MAS export logs
    // 2026-05-12). Current defaults {preset=fast, crf=22, bf=0,
    // tune=animation} pull back from bf=3 to eliminate that artifact
    // while keeping crf one notch tighter than the bf=3 era so file
    // size doesn't balloon: dropping bf=3 typically costs ~10% size, and
    // crf=22 vs crf=23 recovers most of that. The animation tune
    // adjusts deblocking + aq-strength to favour the sharp-edge graphic
    // content that dominates the chart layer. The autotuner below adds
    // another one-step preset bump (faster→fast, fast→medium) so the
    // assembly defaults emit roughly stock x264 behaviour.
    QString preset = QStringLiteral("fast");
    int crf = 22;
    int bframes = 0;
    QString tune;

    QStringList toArgs() const
    {
        QStringList args{
            QStringLiteral("-preset"), preset,
            QStringLiteral("-crf"), QString::number(crf),
            QStringLiteral("-bf"), QString::number(bframes)
        };
        if (!tune.isEmpty()) {
            args << QStringLiteral("-tune") << tune;
        }
        return args;
    }
};

enum class EncoderAutoMode {
    Balanced,
    Compatibility,
    Hardware,
};

struct ExportRenderBackendOptions {
    bool requestGpuRender = true;
    bool requestOffscreenPboReadback = true;
};

struct ExportDiagOptions {
    bool repeatEnabled = false;
    int cropBottom = 0;
    int maxLines = 400;
    bool logAllRepeatPairs = false;
    bool objectHashEnabled = true;
    bool objectTraceEnabled = false;
    int objectTraceMaxLines = 5000;
    int objectDiffThreshold = 8;
    bool compareRenderPathsEnabled = false;
    int compareRadius = 24;
    int compareMaxLines = 400;
    double compareLogThreshold = 0.0010;
    bool pipeHashEnabled = false;
    int pipeHashMaxLines = 400;
    QString rawDumpPath;
};

struct ExportRuntimeConfig {
    EncoderAutoMode encoderMode = EncoderAutoMode::Balanced;
    QString forcedEncoder;
    bool skipEncoderRuntimeProbe = false;
    int encoderThreadsOverride = -1;
    int filterThreadsOverride = -1;
    QString x264PresetOverride;
    int x264CrfOverride = -1;
    int x264BframesOverride = -1;
    ExportRenderBackendOptions renderBackend;
    ExportDiagOptions diag;
};

struct FrameTimingStats {
    qint64 renderTotalNs = 0;
    qint64 writeTotalNs = 0;
    qint64 renderMaxNs = 0;
    qint64 writeMaxNs = 0;
    int renderMaxFrame = -1;
    int writeMaxFrame = -1;
    int overBudgetRenderFrames = 0;
    int overBudgetWriteFrames = 0;
    int repeatedAdjacentFrames = 0;
    int repeatedRuns = 0;
    int longestRepeatedRun = 1;
    int longestRepeatedRunStartFrame = -1;
    int gpuRenderedFrames = 0;
    qint64 cpuFallbackTotal = 0;
    int cpuFallbackMax = 0;
    int cpuFallbackMaxFrame = -1;
    qint64 offscreenDrawTotalNs = 0;
    qint64 offscreenReadbackTotalNs = 0;
    qint64 offscreenDrawMaxNs = 0;
    qint64 offscreenReadbackMaxNs = 0;
    int offscreenDrawMaxFrame = -1;
    int offscreenReadbackMaxFrame = -1;
};

struct ExportPipeBackpressurePlan {
    qint64 frameBytes = 0;
    qint64 highWaterBytes = 0;
    qint64 lowWaterBytes = 0;
    int waitSliceMs = 50;
    int waitTimeoutMs = 30000;
};

struct ExportPipeBackpressureStats {
    int hitCount = 0;
    qint64 totalWaitNs = 0;
    qint64 maxWaitNs = 0;
    int maxWaitFrame = -1;
    qint64 maxQueuedBytes = 0;
    int maxQueuedFrame = -1;
};

struct FrameLayerActivityStats {
    int tapVisible = 0;
    int tapParked = 0;
    int holdVisible = 0;
    int slideMotionVisible = 0;
    int wifiMotionVisible = 0;
    int slideTrackVisible = 0;
    int wifiTrackVisible = 0;
    int touchVisible = 0;
    int touchHoldVisible = 0;
    int holdSustainVisible = 0;
    int judgeTapVisible = 0;
    int judgeTouchVisible = 0;
    int judgeFireworkVisible = 0;

    int activeCoreObjects() const
    {
        return tapVisible + holdVisible + slideMotionVisible + wifiMotionVisible + touchVisible + touchHoldVisible;
    }

    int activeEffects() const
    {
        return holdSustainVisible + judgeTapVisible + judgeTouchVisible + judgeFireworkVisible;
    }

    QString toCompactString() const
    {
        return QStringLiteral(
            "tap=%1 parked=%2 hold=%3 slide=%4 wifi=%5 trackS=%6 trackW=%7 "
            "touch=%8 touchHold=%9 sustain=%10 judgeTap=%11 judgeTouch=%12 judgeFirework=%13")
            .arg(tapVisible)
            .arg(tapParked)
            .arg(holdVisible)
            .arg(slideMotionVisible)
            .arg(wifiMotionVisible)
            .arg(slideTrackVisible)
            .arg(wifiTrackVisible)
            .arg(touchVisible)
            .arg(touchHoldVisible)
            .arg(holdSustainVisible)
            .arg(judgeTapVisible)
            .arg(judgeTouchVisible)
            .arg(judgeFireworkVisible);
    }
};

struct ObjectTraceItem {
    QString key;
    QString type;
    QPointF posPx;
    QString extra;

    QString compact() const
    {
        if (extra.isEmpty()) {
            return QStringLiteral("%1:%2@(%3,%4)")
                .arg(type)
                .arg(key)
                .arg(posPx.x(), 0, 'f', 2)
                .arg(posPx.y(), 0, 'f', 2);
        }
        return QStringLiteral("%1:%2@(%3,%4){%5}")
            .arg(type)
            .arg(key)
            .arg(posPx.x(), 0, 'f', 2)
            .arg(posPx.y(), 0, 'f', 2)
            .arg(extra);
    }
};

struct ReadyFramePayload {
    int frameIndex = -1;
    double exportSecond = 0.0;
    QVector<ObjectTraceItem> traceItems;
    QImage frame;
    qint64 renderNs = 0;
    qint64 offscreenDrawNs = 0;
    qint64 offscreenReadbackNs = 0;
    bool usedOffscreenPath = false;
    int fallbackCount = 0;
    bool usedGpuRenderer = false;
};

struct PendingPboFrame {
    bool valid = false;
    int frameIndex = -1;
    double exportSecond = 0.0;
    QVector<ObjectTraceItem> traceItems;
};

enum class ExportFrameRenderStatus {
    Ready,
    Deferred,
    Failed,
};

// ---- diagnostics constants + DiagTapApproachSample (verbatim) ----
constexpr double kDiagTapUnitsPerSecond = miacode::preview_gameplay::kTapUnitsPerSecond;
constexpr double kDiagLogicalDistanceEdge = miacode::preview_gameplay::kLogicalDistanceEdge;
constexpr double kDiagLogicalDistanceTap = miacode::preview_gameplay::kLogicalDistanceTap;
constexpr double kDiagTapLifecycleDurationSeconds = miacode::preview_gameplay::kTapLifecycleDurationSeconds;
constexpr double kDiagTapSpawnDurationSeconds = miacode::preview_gameplay::kTapSpawnDurationSeconds;
constexpr double kDiagTapFlyDurationSeconds = miacode::preview_gameplay::kTapFlyDurationSeconds;
constexpr double kDiagTouchDurationSeconds = miacode::preview_gameplay::kTouchDurationSeconds;
constexpr double kDiagSlideTrackAppearLeadInSeconds = miacode::preview_gameplay::kSlideTrackAppearLeadInSeconds;
constexpr double kDiagJudgeEffectDurationSeconds = miacode::preview_gameplay::kJudgeEffectDurationSeconds;
constexpr double kDiagJudgeEffectTouchDurationSeconds = miacode::preview_gameplay::kJudgeEffectTouchDurationSeconds;
constexpr double kDiagJudgeEffectFireworkTouchTriggerDelaySeconds =
    miacode::preview_gameplay::kJudgeEffectFireworkTouchTriggerDelaySeconds;
constexpr double kDiagJudgeEffectFireworkDurationSeconds = miacode::preview_gameplay::kJudgeEffectFireworkDurationSeconds;
constexpr double kDiagLogicalCanvasSize = miacode::preview_gameplay::kLogicalCanvasSize;
constexpr double kDiagLogicalCanvasCenter = kDiagLogicalCanvasSize / 2.0;

struct DiagTapApproachSample {
    double distance = kDiagLogicalDistanceTap;
    double scale = 0.0;
};

// ---- free-function prototypes (definitions live in the VideoExport*.cpp group TUs) ----
// NOTE: exportTempDirTemplate() and the ExportTempDirRegistry/ScopedExportTempDirTracker
// classes are single-TU (VideoExportPreparedTask.cpp) and are intentionally NOT declared here.

// VideoExportEncoder.cpp
qint64 bytesToMiB(quint64 bytes);
SystemMemoryInfo querySystemMemoryInfo();
QString memoryInfoToLog(const SystemMemoryInfo& info);
QString fasterX264Preset(const QString& preset);
QString slowerX264Preset(const QString& preset);
QString videoExportPresetToken(VideoExportPreset preset);
QString encoderAutoModeToken(EncoderAutoMode mode);
EncoderAutoMode resolveEncoderAutoMode();
ExportRuntimeConfig loadExportRuntimeConfig();
bool shouldPreferHardwareEncoderInAutoMode(
    EncoderAutoMode mode,
    int outputWidth,
    int outputHeight,
    int fps,
    int idealThreadCount,
    const SystemMemoryInfo& memoryInfo,
    QString* reason
);
VideoBitratePlan chooseVideoBitratePlan(
    VideoExportPreset preset,
    int outputWidth,
    int outputHeight,
    int fps
);
X264TuningPlan chooseX264TuningPlan(
    VideoExportPreset preset,
    const ExportRuntimeConfig& exportConfig,
    const SystemMemoryInfo& memoryInfo,
    int outputWidth,
    int outputHeight,
    int fps,
    int idealThreadCount
);
bool fileIsExecutable(const QString& path);
QString resolveFfmpegExecutable();
QString resolveFfprobeExecutable(const QString& ffmpegPath);
bool hasEncoderToken(const QString& encodersOutput, const QString& encoderName);
bool probeEncoderRuntimeAvailability(
    const QString& ffmpegPath,
    const VideoEncoderConfig& candidate,
    int outputWidth,
    int outputHeight,
    int fps,
    QString* detail
);
VideoEncoderConfig chooseVideoEncoder(
    const QString& ffmpegPath,
    int outputWidth,
    int outputHeight,
    int fps,
    VideoExportPreset preset,
    const SystemMemoryInfo& memoryInfo,
    const ExportRuntimeConfig& exportConfig,
    QString* probeLog
);

// VideoExportFrameRender.cpp
void appendRenderBackendFallbackDetail(QString* detail, const QString& entry);
void drawLeadInPauseOverlay(QImage* frame);
ReadyFramePayload buildReadyFramePayload(
    VideoExportQuickRenderBackend* exportBackend,
    int frameIndex,
    double exportSecond,
    QVector<ObjectTraceItem>&& traceItems,
    QImage frame,
    qint64 renderNs,
    bool usedOffscreenPath
);
ExportFrameRenderStatus renderExportFrameWithConfiguredBackend(
    VideoExportQuickRenderBackend* exportBackend,
    bool* useOffscreenGpu,
    bool* useOffscreenPboReadback,
    std::deque<PendingPboFrame>* pendingPboFrames,
    const QSize& frameSize,
    int frameIndex,
    double exportSecond,
    bool showTimestamp,
    bool showObjectStatsHud,
    QVector<ObjectTraceItem>&& traceItems,
    ReadyFramePayload* readyFrame,
    QString* fallbackDetail,
    double hudPlayheadSecondsOverride
);
bool drainPendingExportFrame(
    VideoExportQuickRenderBackend* exportBackend,
    std::deque<PendingPboFrame>* pendingPboFrames,
    const QSize& frameSize,
    bool showTimestamp,
    bool showObjectStatsHud,
    ReadyFramePayload* readyFrame,
    QString* errorMessage
);
QImage buildCircularDimMaskImage(
    int frameWidth,
    int frameHeight,
    double outerDimAlpha,
    double innerDimAlpha,
    double layoutRingDiameterRatio,
    double layoutSquareScale,
    bool smoothBrightness
);
QImage buildCircularMediaMaskImage(
    int frameWidth,
    int frameHeight,
    double layoutSquareScale
);
QRectF staticMediaTargetRect(
    const QSize& mediaSize,
    const QSize& outputSize,
    PreviewBackgroundScaleMode scaleMode);
bool stageStaticBackgroundImageForExport(
    const QString& sourcePath,
    const QSize& outputSize,
    PreviewBackgroundScaleMode scaleMode,
    double layoutSquareScale,
    const QString& stagedPath,
    QString* detail);
bool preparePackedRgbaFrame(
    const QImage& frame,
    QImage* convertedFrame,
    QByteArray* packedScratch,
    const char** data,
    qint64* size
);

// VideoExportDiagnostics.cpp
DiagTapApproachSample diagSampleTapApproach(double deltaSeconds);
bool diagTapSpriteVisible(double deltaSeconds);
bool diagSlideHeadVisible(double deltaSeconds);
QPointF diagLaneUnitVector(int lane);
QRectF diagPlayfieldRect(int width, int height, double layoutSquareScale);
QPointF diagMapLogicalPointToPlayfield(const QPointF& logicalPoint, const QRectF& playfieldRect);
QPointF diagInterpolatePoint(const QVector<QPointF>& points, qreal proportion);
qreal diagInterpolateAngle(const QVector<double>& angles, qreal proportion);
QString markerTraceKey(const TimelineNoteMarker& marker);
QVector<ObjectTraceItem> collectVisibleObjectTrace(
    const QVector<TimelineNoteMarker>& markers,
    double playheadSecond,
    int frameWidth,
    int frameHeight,
    double layoutSquareScale
);
FrameLayerActivityStats estimateFrameLayerActivity(
    const QVector<TimelineNoteMarker>& markers,
    double playheadSecond
);
quint64 fnv1a64Bytes(const char* data, qint64 size);
double meanAbsDiffNormalized(const QImage& lhs, const QImage& rhs);
double meanAbsDiffNormalizedRect(const QImage& lhs, const QImage& rhs, const QRect& rect);
double meanAbsDiffAroundTraceItems(
    const QImage& lhs,
    const QImage& rhs,
    const QVector<ObjectTraceItem>& traceItems,
    int radius,
    double* maxDiffOut
);
quint64 sampledFrameSignature(const QImage& frame);
quint64 fullFrameSignature(const QImage& frame, int cropBottom);
quint64 objectOnlyFrameSignature(
    const QImage& frameWithObjects,
    const QImage& frameWithoutObjects,
    int diffThreshold,
    int* activePixelCount
);

// VideoExportPipeline.cpp
QString normalizePath(const QString& path);
QString truncateForLog(const QString& text, int maxChars = 4000);
QString videoExportLogPath();
bool exportDetailedLoggingEnabled();
bool shouldWriteSummaryExportStage(const QString& stage);
QString summarizedExportLogDetail(const QString& detail);
void appendVideoExportLog(const QString& stage, const QString& detail = QString());
bool isImageMediaPath(const QString& path);
std::unique_ptr<miacode::video_export::VideoExportAudioBackend> createExportAudioBackend(QString* errorMessage);
bool writeAllToProcess(QProcess* process, const char* data, qint64 size, QString* failureDetail = nullptr);
ExportPipeBackpressurePlan chooseExportPipeBackpressurePlan(const QSize& frameSize);
bool waitForProcessBackpressureDrain(
    QProcess* process,
    const ExportPipeBackpressurePlan& plan,
    ExportPipeBackpressureStats* stats,
    int frameIndex,
    qint64* waitNs = nullptr,
    qint64* peakQueuedBytes = nullptr,
    QString* failureDetail = nullptr
);
bool writeAllToFile(QFile* file, const char* data, qint64 size);
QString probeExportedVideoSummary(const QString& ffprobePath, const QString& outputPath);
QString ffmpegBaseArgsLog(const QString& ffmpegPath, const QStringList& args);
QString qProcessStateForLog(QProcess::ProcessState state);
QString qProcessExitStatusForLog(QProcess::ExitStatus status);
QString describeProcessForLog(const QProcess& process);
QString makeRemuxStageOutputPath(const QString& finalOutputPath);
QString processOutputAndErrorForLog(QProcess& process, int maxChars = 2000);
QString withExportLogPath(const QString& details);
bool waitForProcessWithProgress(
    QProcess& process,
    const QString& beginStage,
    const QString& doneStage,
    const QString& progressText,
    int progressPercent,
    const std::function<bool(int, const QString&)>& setProgressPercent,
    const QString& cancelDetail,
    VideoExportResult* result
);
bool replaceOutputFileAtomicallyBestEffort(
    const QString& stagedPath,
    const QString& finalPath,
    QString* errorMessage
);

}  // namespace miacode::video_export::detail
