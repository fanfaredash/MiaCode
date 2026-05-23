#include "VideoExportController.h"

#include "BassExportAudioBackend.h"
#include "LegacyExportAudioBackend.h"
#include "RawVideoPipeTransport.h"
#include "VideoExportAudioRenderPlan.h"
#include "VideoExportQuickRenderBackend.h"
#include "VideoExportRuntimePolicy.h"
#include "common/AssetPaths.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/DebugOptions.h"
#include "common/LayoutRingConfig.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewGameplayConfig.h"
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

namespace {
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

qint64 bytesToMiB(quint64 bytes)
{
    return static_cast<qint64>(bytes / (1024ULL * 1024ULL));
}

SystemMemoryInfo querySystemMemoryInfo()
{
    SystemMemoryInfo info;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX statex{};
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex) != 0) {
        info.valid = true;
        info.totalPhysicalBytes = statex.ullTotalPhys;
        info.availablePhysicalBytes = statex.ullAvailPhys;
    }
#endif
    return info;
}

QString memoryInfoToLog(const SystemMemoryInfo& info)
{
    if (!info.valid) {
        return QStringLiteral("memory_snapshot unavailable");
    }
    return QStringLiteral("memory_snapshot totalMiB=%1 availMiB=%2")
        .arg(bytesToMiB(info.totalPhysicalBytes))
        .arg(bytesToMiB(info.availablePhysicalBytes));
}

QString fasterX264Preset(const QString& preset)
{
    if (preset == QLatin1String("medium")) {
        return QStringLiteral("fast");
    }
    if (preset == QLatin1String("fast")) {
        return QStringLiteral("faster");
    }
    if (preset == QLatin1String("faster")) {
        return QStringLiteral("veryfast");
    }
    if (preset == QLatin1String("veryfast")) {
        return QStringLiteral("superfast");
    }
    return preset;
}

QString slowerX264Preset(const QString& preset)
{
    if (preset == QLatin1String("faster")) {
        return QStringLiteral("fast");
    }
    if (preset == QLatin1String("fast")) {
        return QStringLiteral("medium");
    }
    if (preset == QLatin1String("medium")) {
        return QStringLiteral("slow");
    }
    if (preset == QLatin1String("slow")) {
        return QStringLiteral("slower");
    }
    if (preset == QLatin1String("slower")) {
        return QStringLiteral("veryslow");
    }
    if (preset == QLatin1String("veryfast")) {
        return QStringLiteral("faster");
    }
    if (preset == QLatin1String("superfast")) {
        return QStringLiteral("veryfast");
    }
    if (preset == QLatin1String("ultrafast")) {
        return QStringLiteral("superfast");
    }
    return preset;
}

QString videoExportPresetToken(VideoExportPreset preset)
{
    switch (preset) {
    case VideoExportPreset::HighQuality:
        return QStringLiteral("high_quality");
    case VideoExportPreset::Fast:
    default:
        return QStringLiteral("fast");
    }
}

QString encoderAutoModeToken(EncoderAutoMode mode)
{
    switch (mode) {
    case EncoderAutoMode::Compatibility:
        return QStringLiteral("compatibility");
    case EncoderAutoMode::Hardware:
        return QStringLiteral("hardware");
    case EncoderAutoMode::Balanced:
    default:
        return QStringLiteral("balanced");
    }
}

EncoderAutoMode resolveEncoderAutoMode()
{
    const QString mode = qEnvironmentVariable("MIACODE_EXPORT_ENCODER_MODE").trimmed().toLower();
    if (mode == QLatin1String("compatibility")
        || mode == QLatin1String("software")
        || mode == QLatin1String("safe")) {
        return EncoderAutoMode::Compatibility;
    }
    if (mode == QLatin1String("hardware")
        || mode == QLatin1String("hardware_preferred")
        || mode == QLatin1String("fast")) {
        return EncoderAutoMode::Hardware;
    }
    return EncoderAutoMode::Balanced;
}

ExportRuntimeConfig loadExportRuntimeConfig()
{
    ExportRuntimeConfig config;
    config.encoderMode = resolveEncoderAutoMode();
    config.forcedEncoder = qEnvironmentVariable("MIACODE_EXPORT_FORCE_ENCODER").trimmed();
    config.skipEncoderRuntimeProbe =
        miacode::debug_options::envFlagEnabled("MIACODE_EXPORT_SKIP_ENCODER_RUNTIME_PROBE");
    config.encoderThreadsOverride =
        miacode::debug_options::envIntValue("MIACODE_EXPORT_ENCODER_THREADS", -1);
    config.filterThreadsOverride =
        miacode::debug_options::envIntValue("MIACODE_EXPORT_FILTER_THREADS", -1);
    config.x264PresetOverride = qEnvironmentVariable("MIACODE_EXPORT_X264_PRESET").trimmed();
    config.x264CrfOverride = miacode::debug_options::envIntValue("MIACODE_EXPORT_X264_CRF", -1);
    config.x264BframesOverride = miacode::debug_options::envIntValue("MIACODE_EXPORT_X264_BFRAMES", -1);

    const std::optional<bool> gpuRenderOverride =
        miacode::debug_options::envOptionalFlagValue("MIACODE_EXPORT_ENABLE_GPU_RENDER");
    const std::optional<bool> enablePboOverride =
        miacode::debug_options::envOptionalFlagValue("MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO");
    const std::optional<bool> disablePboOverride =
        miacode::debug_options::envOptionalFlagValue("MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO");
    config.renderBackend.requestGpuRender =
        enablePboOverride.value_or(false) ? true : gpuRenderOverride.value_or(true);
    config.renderBackend.requestOffscreenPboReadback =
        miacode::video_export::shouldRequestOffscreenPboReadback(enablePboOverride, disablePboOverride);

    config.diag.repeatEnabled = miacode::debug_options::envFlagEnabled("MIACODE_EXPORT_DIAG_REPEAT");
    config.diag.cropBottom = qMax(0, miacode::debug_options::envIntValue("MIACODE_EXPORT_DIAG_CROP_BOTTOM", 0));
    config.diag.maxLines = qMax(0, miacode::debug_options::envIntValue("MIACODE_EXPORT_DIAG_MAX_LINES", 400));
    config.diag.logAllRepeatPairs =
        miacode::debug_options::envFlagEnabled("MIACODE_EXPORT_DIAG_LOG_ALL_REPEATS");
    const std::optional<bool> objectHashOverride =
        miacode::debug_options::envOptionalFlagValue("MIACODE_EXPORT_DIAG_OBJECT_HASH");
    config.diag.objectHashEnabled = objectHashOverride.value_or(true);
    config.diag.objectTraceEnabled =
        miacode::debug_options::envFlagEnabled("MIACODE_EXPORT_DIAG_OBJECT_TRACE");
    config.diag.objectTraceMaxLines = qMax(
        0,
        miacode::debug_options::envIntValue(
            "MIACODE_EXPORT_DIAG_OBJECT_TRACE_MAX_LINES",
            qMax(config.diag.maxLines, 5000)
        )
    );
    config.diag.objectDiffThreshold = qBound(
        0,
        miacode::debug_options::envIntValue("MIACODE_EXPORT_DIAG_OBJECT_DIFF_THRESHOLD", 8),
        4 * 255
    );
    config.diag.compareRenderPathsEnabled =
        miacode::debug_options::envFlagEnabled("MIACODE_EXPORT_DIAG_COMPARE_RENDER_PATHS");
    config.diag.compareRadius = qBound(
        2,
        miacode::debug_options::envIntValue("MIACODE_EXPORT_DIAG_COMPARE_RADIUS", 24),
        512
    );
    config.diag.compareMaxLines = qMax(
        0,
        miacode::debug_options::envIntValue("MIACODE_EXPORT_DIAG_COMPARE_MAX_LINES", config.diag.maxLines)
    );
    config.diag.compareLogThreshold = qBound(
        0.0,
        miacode::debug_options::envDoubleValue("MIACODE_EXPORT_DIAG_COMPARE_LOG_THRESHOLD", 0.0010),
        1.0
    );
    config.diag.pipeHashEnabled =
        miacode::debug_options::envFlagEnabled("MIACODE_EXPORT_DIAG_PIPE_HASH");
    config.diag.pipeHashMaxLines = qMax(
        0,
        miacode::debug_options::envIntValue("MIACODE_EXPORT_DIAG_PIPE_HASH_MAX_LINES", config.diag.maxLines)
    );
    config.diag.rawDumpPath = qEnvironmentVariable("MIACODE_EXPORT_DIAG_RAW_DUMP_PATH").trimmed();
    return config;
}

bool shouldPreferHardwareEncoderInAutoMode(
    EncoderAutoMode mode,
    int outputWidth,
    int outputHeight,
    int fps,
    int idealThreadCount,
    const SystemMemoryInfo& memoryInfo,
    QString* reason
)
{
    if (mode == EncoderAutoMode::Compatibility) {
        if (reason != nullptr) {
            *reason = QStringLiteral("mode=compatibility");
        }
        return false;
    }
    if (mode == EncoderAutoMode::Hardware) {
        if (reason != nullptr) {
            *reason = QStringLiteral("mode=hardware");
        }
        return true;
    }

    const qint64 pixelsPerSecond =
        static_cast<qint64>(qMax(1, outputWidth)) * qMax(1, outputHeight) * qMax(1, fps);
    const qint64 availMiB = bytesToMiB(memoryInfo.availablePhysicalBytes);

    if (pixelsPerSecond >= 180000000LL) {
        if (reason != nullptr) {
            *reason = QStringLiteral("high_pixels_per_second");
        }
        return true;
    }
    if (pixelsPerSecond >= 120000000LL && idealThreadCount <= 8) {
        if (reason != nullptr) {
            *reason = QStringLiteral("heavy_export_with_limited_cpu");
        }
        return true;
    }
    if (memoryInfo.valid && availMiB > 0 && availMiB <= 4096 && pixelsPerSecond >= 100000000LL) {
        if (reason != nullptr) {
            *reason = QStringLiteral("memory_pressure");
        }
        return true;
    }

    if (reason != nullptr) {
        *reason = QStringLiteral("balanced_prefers_software");
    }
    return false;
}

VideoBitratePlan chooseVideoBitratePlan(
    VideoExportPreset preset,
    int outputWidth,
    int outputHeight,
    int fps
)
{
    const int safeWidth = qMax(1, outputWidth);
    const int safeHeight = qMax(1, outputHeight);
    const int safeFps = qMax(1, fps);

    VideoBitratePlan plan;
    if (preset == VideoExportPreset::HighQuality) {
        plan.bitrateKbps = qBound<qint64>(
            2600LL,
            qRound64(static_cast<double>(safeWidth) * safeHeight * safeFps * 0.090 / 1000.0),
            10500LL
        );
        plan.maxRateKbps = qBound<qint64>(
            plan.bitrateKbps,
            qRound64(static_cast<double>(plan.bitrateKbps) * 1.35),
            14000LL
        );
        plan.bufSizeKbps = qBound<qint64>(
            plan.maxRateKbps,
            qRound64(static_cast<double>(plan.maxRateKbps) * 1.75),
            20000LL
        );
        return plan;
    }

    plan.bitrateKbps = qBound<qint64>(
        2200LL,
        qRound64(static_cast<double>(safeWidth) * safeHeight * safeFps * 0.075 / 1000.0),
        8500LL
    );
    plan.maxRateKbps = qBound<qint64>(
        plan.bitrateKbps,
        qRound64(static_cast<double>(plan.bitrateKbps) * 1.40),
        10500LL
    );
    plan.bufSizeKbps = qBound<qint64>(
        plan.maxRateKbps,
        qRound64(static_cast<double>(plan.maxRateKbps) * 2.0),
        16000LL
    );
    return plan;
}

X264TuningPlan chooseX264TuningPlan(
    VideoExportPreset preset,
    const ExportRuntimeConfig& exportConfig,
    const SystemMemoryInfo& memoryInfo,
    int outputWidth,
    int outputHeight,
    int fps,
    int idealThreadCount
)
{
    const qint64 availMiB = bytesToMiB(memoryInfo.availablePhysicalBytes);
    const qint64 totalMiB = bytesToMiB(memoryInfo.totalPhysicalBytes);
    const qint64 pixelsPerSecond =
        static_cast<qint64>(qMax(1, outputWidth)) * qMax(1, outputHeight) * qMax(1, fps);

    X264TuningPlan plan;
    if (memoryInfo.valid) {
        if (availMiB >= 24576 && totalMiB >= 32768) {
            plan.preset = QStringLiteral("medium");
        } else if (availMiB >= 12288) {
            plan.preset = QStringLiteral("fast");
        } else {
            plan.preset = QStringLiteral("faster");
        }
    }

    if (idealThreadCount <= 4) {
        plan.preset = QStringLiteral("faster");
    } else if (idealThreadCount <= 8 && plan.preset == QLatin1String("medium")) {
        plan.preset = QStringLiteral("fast");
    } else if (idealThreadCount <= 8 && plan.preset == QLatin1String("fast") && memoryInfo.valid && availMiB < 16384) {
        plan.preset = QStringLiteral("faster");
    }

    if (memoryInfo.valid && availMiB < 8192) {
        plan.preset = QStringLiteral("faster");
    } else if (memoryInfo.valid && availMiB < 12288 && plan.preset == QLatin1String("medium")) {
        plan.preset = QStringLiteral("fast");
    }

    if (memoryInfo.valid && totalMiB < 16384 && plan.preset == QLatin1String("medium")) {
        plan.preset = QStringLiteral("fast");
    }

    if (pixelsPerSecond >= 120000000LL) {
        plan.preset = fasterX264Preset(plan.preset);
    }
    if (pixelsPerSecond >= 180000000LL) {
        plan.preset = fasterX264Preset(plan.preset);
    }

    // Compactness bump. After the memory/thread/pixel-rate logic above
    // settles on a preset, advance the two cheap ones by a notch:
    //   faster → fast  (~10-15% smaller, ~30% slower encode)
    //   fast   → medium (~10-12% smaller, ~50-100% slower encode)
    // Higher presets (medium, slow, slower, veryslow) and the
    // ultrafast/superfast/veryfast tail are left alone — those slots
    // are picked by other branches deliberately and bumping them costs
    // disproportionate encode time per byte saved.
    if (plan.preset == QLatin1String("faster")) {
        plan.preset = QStringLiteral("fast");
    } else if (plan.preset == QLatin1String("fast")) {
        plan.preset = QStringLiteral("medium");
    }
    plan.tune = QStringLiteral("animation");

    if (preset == VideoExportPreset::HighQuality) {
        // Pull two CRF notches below the Fast default. With Fast at
        // crf=22 this lands HighQuality at crf=20 — perceptually
        // transparent on the high-contrast UI overlay and still well
        // above the qMax(18, …) floor, which keeps the file size from
        // exploding if some future tweak lowers the Fast default.
        plan.crf = qMax(18, plan.crf - 2);
        plan.bframes = 0;
    }

    if (!exportConfig.x264PresetOverride.isEmpty()) {
        plan.preset = exportConfig.x264PresetOverride;
    }
    plan.crf = qBound(
        16,
        exportConfig.x264CrfOverride >= 0 ? exportConfig.x264CrfOverride : plan.crf,
        28
    );
    // beta25 — bframes upper bound raised from 2 to 8 to honour the new
    // x264 stock default of 3 (and let advanced users push higher via
    // MIACODE_EXPORT_X264_BFRAMES). 8 is x264's recommended ceiling for
    // typical content; values above that hit diminishing returns and
    // can slow decode on weaker playback hardware.
    plan.bframes = qBound(
        0,
        exportConfig.x264BframesOverride >= 0 ? exportConfig.x264BframesOverride : plan.bframes,
        8
    );
    return plan;
}

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

void appendRenderBackendFallbackDetail(QString* detail, const QString& entry)
{
    if (detail == nullptr || entry.isEmpty()) {
        return;
    }
    if (detail->isEmpty()) {
        *detail = entry;
        return;
    }
    *detail += QStringLiteral("; ") + entry;
}

// Paint a semi-transparent two-bar pause glyph centred over the export
// frame. Called once per frame for the lead-in / pre-range window so the
// viewer immediately sees that the playfield is held in a stationary state
// (chart frozen at segmentStart, no playback yet). The glyph disappears
// the moment the lead-in ends and the chart starts to advance.
//
// Layered so the pause read clearly against both dark and bright chart
// backgrounds:
//   1) A faint full-frame black wash (alpha 70/255 ≈ 27%) that mutes the
//      whole playfield so it visibly "feels" paused.
//   2) A rounded translucent backdrop panel under the bars for contrast
//      on bright frames.
//   3) Two opaque white bars with a thin black outline so the bars
//      stay legible regardless of what shows through.
//
// `Format_RGBA8888` (straight alpha) is what the export pipeline feeds
// FFmpeg — QPainter on this format composes correctly through Qt's
// internal premultiplied path; the final straight-alpha output is what
// FFmpeg's `overlay=…:alpha=straight` filter expects.
void drawLeadInPauseOverlay(QImage* frame)
{
    if (frame == nullptr || frame->isNull()) {
        return;
    }
    const QSize sz = frame->size();
    const int shortSide = qMin(sz.width(), sz.height());
    if (shortSide <= 0) {
        return;
    }

    QPainter painter(frame);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    // Layer 1 — full-frame dim. SourceOver onto an opaque chart frame
    // pulls its perceived brightness down ~27%; onto a transparent base
    // it leaves a dark wash that FFmpeg's overlay filter then composites
    // over the chart background. Either way the playfield reads paused.
    painter.fillRect(frame->rect(), QColor(0, 0, 0, 70));

    // 2/3 of the original sizing — the previous version read as overly
    // dominant during the lead-in; scaling barHeight here cascades to
    // every other dimension because they are all derived from it.
    const int barHeight = qMax(8, qRound(shortSide * 0.22 * 2.0 / 3.0));
    const int barWidth = qMax(3, qRound(barHeight * 0.32));
    const int gap = qMax(2, qRound(barWidth * 0.65));
    const int totalWidth = barWidth * 2 + gap;
    const qreal x0 = (sz.width() - totalWidth) / 2.0;
    const qreal y0 = (sz.height() - barHeight) / 2.0;
    const qreal radius = qMax<qreal>(3.0, barWidth * 0.25);

    // Layer 2 — translucent backdrop panel behind the bars.
    const qreal panelPadX = barWidth * 0.7;
    const qreal panelPadY = barHeight * 0.18;
    const QRectF panelRect(
        x0 - panelPadX,
        y0 - panelPadY,
        totalWidth + panelPadX * 2.0,
        barHeight + panelPadY * 2.0
    );
    painter.setBrush(QColor(0, 0, 0, 110));
    painter.drawRoundedRect(panelRect, radius * 1.6, radius * 1.6);

    // Layer 3 — bars themselves: opaque white with a thin dark outline.
    painter.setPen(QPen(QColor(0, 0, 0, 160), 1.5));
    painter.setBrush(QColor(255, 255, 255, 230));
    painter.drawRoundedRect(QRectF(x0, y0, barWidth, barHeight), radius, radius);
    painter.drawRoundedRect(QRectF(x0 + barWidth + gap, y0, barWidth, barHeight), radius, radius);
}

ReadyFramePayload buildReadyFramePayload(
    VideoExportQuickRenderBackend* exportBackend,
    int frameIndex,
    double exportSecond,
    QVector<ObjectTraceItem>&& traceItems,
    QImage frame,
    qint64 renderNs,
    bool usedOffscreenPath
)
{
    ReadyFramePayload readyFrame;
    readyFrame.frameIndex = frameIndex;
    readyFrame.exportSecond = exportSecond;
    readyFrame.traceItems = std::move(traceItems);
    readyFrame.frame = std::move(frame);
    readyFrame.renderNs = renderNs;
    readyFrame.usedOffscreenPath = usedOffscreenPath;
    if (exportBackend != nullptr) {
        readyFrame.offscreenDrawNs = exportBackend->offscreenDrawNsLastFrameForDebug();
        readyFrame.offscreenReadbackNs = exportBackend->offscreenReadbackNsLastFrameForDebug();
        readyFrame.fallbackCount = exportBackend->cpuFallbackCountLastFrameForDebug();
        readyFrame.usedGpuRenderer = exportBackend->usedGpuRendererLastFrameForDebug();
    }
    return readyFrame;
}

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
)
{
    if (exportBackend == nullptr
        || useOffscreenGpu == nullptr
        || useOffscreenPboReadback == nullptr
        || pendingPboFrames == nullptr
        || readyFrame == nullptr) {
        return ExportFrameRenderStatus::Failed;
    }

    *readyFrame = ReadyFramePayload{};
    QElapsedTimer frameTimer;
    frameTimer.start();
    bool usedOffscreenPath = false;
    QImage frame;

    if (*useOffscreenGpu && *useOffscreenPboReadback) {
        QImage completedFrame;
        bool completedFrameReady = false;
        QString pboStepError;
        const bool pboStepOk = exportBackend->renderOverlayFrameOffscreenPboStep(
            frameSize,
            exportSecond,
            showTimestamp,
            showObjectStatsHud,
            &completedFrame,
            &completedFrameReady,
            false,
            &pboStepError,
            hudPlayheadSecondsOverride
        );
        const qint64 renderNs = frameTimer.nsecsElapsed();
        if (pboStepOk) {
            usedOffscreenPath = true;
            // Two pending slots in steady state since the convert worker
            // adds an extra frame of pipeline depth on top of the PBO
            // ping-pong: oldest entry = frame whose worker job just
            // finished and is being returned to us in completedFrame;
            // newer entry = frame currently in a PBO awaiting either
            // worker submission or readback finalisation.
            const bool producedReadyFrame = completedFrameReady && !pendingPboFrames->empty();
            if (producedReadyFrame) {
                PendingPboFrame oldest = std::move(pendingPboFrames->front());
                pendingPboFrames->pop_front();
                *readyFrame = buildReadyFramePayload(
                    exportBackend,
                    oldest.frameIndex,
                    oldest.exportSecond,
                    std::move(oldest.traceItems),
                    std::move(completedFrame),
                    renderNs,
                    true
                );
            }
            PendingPboFrame newPending;
            newPending.valid = true;
            newPending.frameIndex = frameIndex;
            newPending.exportSecond = exportSecond;
            newPending.traceItems = std::move(traceItems);
            pendingPboFrames->push_back(std::move(newPending));
            return producedReadyFrame ? ExportFrameRenderStatus::Ready : ExportFrameRenderStatus::Deferred;
        }

        appendRenderBackendFallbackDetail(
            fallbackDetail,
            QStringLiteral("frame=%1 reason=offscreen_pbo_failed error=%2")
                .arg(frameIndex)
                .arg(pboStepError)
        );
        exportBackend->resetOffscreenPboReadback();
        *useOffscreenPboReadback = false;
    }

    frame = *useOffscreenGpu
        ? exportBackend->renderOverlayFrameOffscreen(
              frameSize,
              exportSecond,
              showTimestamp,
              showObjectStatsHud,
              hudPlayheadSecondsOverride)
        : exportBackend->renderOverlayFrame(
              frameSize,
              exportSecond,
              showTimestamp,
              showObjectStatsHud,
              hudPlayheadSecondsOverride);
    if (frame.isNull()) {
        appendRenderBackendFallbackDetail(
            fallbackDetail,
            QStringLiteral("frame=%1 reason=quick_render_failed").arg(frameIndex));
        return ExportFrameRenderStatus::Failed;
    }

    *readyFrame = buildReadyFramePayload(
        exportBackend,
        frameIndex,
        exportSecond,
        std::move(traceItems),
        std::move(frame),
        frameTimer.nsecsElapsed(),
        usedOffscreenPath || *useOffscreenGpu
    );
    return ExportFrameRenderStatus::Ready;
}

bool drainPendingExportFrame(
    VideoExportQuickRenderBackend* exportBackend,
    std::deque<PendingPboFrame>* pendingPboFrames,
    const QSize& frameSize,
    bool showTimestamp,
    bool showObjectStatsHud,
    ReadyFramePayload* readyFrame,
    QString* errorMessage
)
{
    if (exportBackend == nullptr || pendingPboFrames == nullptr || readyFrame == nullptr
        || pendingPboFrames->empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no pending PBO frame to drain");
        }
        return false;
    }

    *readyFrame = ReadyFramePayload{};
    QElapsedTimer frameTimer;
    frameTimer.start();
    QImage drainedFrame;
    bool drainedFrameReady = false;
    QString drainError;
    const bool drainOk = exportBackend->renderOverlayFrameOffscreenPboStep(
        frameSize,
        pendingPboFrames->front().exportSecond,
        showTimestamp,
        showObjectStatsHud,
        &drainedFrame,
        &drainedFrameReady,
        true,
        &drainError
    );
    if (!drainOk || !drainedFrameReady) {
        if (errorMessage != nullptr) {
            *errorMessage = drainError.isEmpty() ? QStringLiteral("failed to drain PBO readback") : drainError;
        }
        return false;
    }

    PendingPboFrame oldest = std::move(pendingPboFrames->front());
    pendingPboFrames->pop_front();
    *readyFrame = buildReadyFramePayload(
        exportBackend,
        oldest.frameIndex,
        oldest.exportSecond,
        std::move(oldest.traceItems),
        std::move(drainedFrame),
        frameTimer.nsecsElapsed(),
        true
    );
    return true;
}

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

DiagTapApproachSample diagSampleTapApproach(double deltaSeconds)
{
    DiagTapApproachSample sample;
    if (deltaSeconds <= -kDiagTapLifecycleDurationSeconds) {
        return sample;
    }
    if (deltaSeconds < -kDiagTapFlyDurationSeconds) {
        const double progress = qBound(
            0.0,
            (deltaSeconds + kDiagTapLifecycleDurationSeconds) / qMax(0.001, kDiagTapSpawnDurationSeconds),
            1.0
        );
        sample.scale = progress;
        return sample;
    }
    if (deltaSeconds < 0.0) {
        const double flightProgress = qBound(
            0.0,
            (deltaSeconds + kDiagTapFlyDurationSeconds) / qMax(0.001, kDiagTapFlyDurationSeconds),
            1.0
        );
        sample.distance = kDiagLogicalDistanceTap + (kDiagLogicalDistanceEdge - kDiagLogicalDistanceTap) * flightProgress;
        sample.scale = 1.0;
        return sample;
    }
    sample.distance = kDiagLogicalDistanceEdge;
    sample.scale = 1.0;
    return sample;
}

bool diagTapSpriteVisible(double deltaSeconds)
{
    if (deltaSeconds > 0.0) {
        return false;
    }
    return diagSampleTapApproach(deltaSeconds).scale > 0.0;
}

bool diagSlideHeadVisible(double deltaSeconds)
{
    if (deltaSeconds >= 0.0) {
        return false;
    }
    return diagSampleTapApproach(deltaSeconds).scale > 0.0;
}

QPointF diagLaneUnitVector(int lane)
{
    if (lane < 1 || lane > 8) {
        return QPointF(0.0, 0.0);
    }
    const double angleDeg = -67.5 + (lane - 1) * 45.0;
    const double angleRad = qDegreesToRadians(angleDeg);
    return QPointF(qCos(angleRad), qSin(angleRad));
}

QRectF diagPlayfieldRect(int width, int height, double layoutSquareScale)
{
    const QRectF stageRect(0.0, 0.0, qMax(1, width), qMax(1, height));
    return miacode::preview_video::centeredLayoutRectForStage(stageRect, layoutSquareScale);
}

QPointF diagMapLogicalPointToPlayfield(const QPointF& logicalPoint, const QRectF& playfieldRect)
{
    const qreal scale = playfieldRect.width() / kDiagLogicalCanvasSize;
    return QPointF(
        playfieldRect.left() + logicalPoint.x() * scale,
        playfieldRect.top() + logicalPoint.y() * scale
    );
}

QPointF diagInterpolatePoint(const QVector<QPointF>& points, qreal proportion)
{
    if (points.isEmpty()) {
        return QPointF();
    }
    if (points.size() == 1) {
        return points.constFirst();
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    const qreal scaled = clamped * (points.size() - 1);
    const int index = qBound(0, static_cast<int>(qFloor(scaled)), points.size() - 2);
    const qreal t = scaled - index;
    const QPointF& a = points[index];
    const QPointF& b = points[index + 1];
    return QPointF(a.x() + (b.x() - a.x()) * t, a.y() + (b.y() - a.y()) * t);
}

qreal diagInterpolateAngle(const QVector<double>& angles, qreal proportion)
{
    if (angles.isEmpty()) {
        return 0.0;
    }
    if (angles.size() == 1) {
        return angles.constFirst();
    }
    const qreal clamped = qBound<qreal>(0.0, proportion, 1.0);
    const qreal scaled = clamped * (angles.size() - 1);
    const int index = qBound(0, static_cast<int>(qFloor(scaled)), angles.size() - 2);
    const qreal t = scaled - index;
    const qreal a = angles[index];
    const qreal b = angles[index + 1];
    qreal delta = std::fmod(b - a + 540.0, 360.0) - 180.0;
    return a + delta * t;
}

QString markerTraceKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("L%1C%2")
        .arg(qMax(1, marker.sourceLine))
        .arg(qMax(1, marker.sourceCol));
}

QVector<ObjectTraceItem> collectVisibleObjectTrace(
    const QVector<TimelineNoteMarker>& markers,
    double playheadSecond,
    int frameWidth,
    int frameHeight,
    double layoutSquareScale
)
{
    QVector<ObjectTraceItem> items;
    const QRectF playfield = diagPlayfieldRect(frameWidth, frameHeight, layoutSquareScale);
    for (const TimelineNoteMarker& marker : markers) {
        const QString keyBase = markerTraceKey(marker);

        if (marker.type == QLatin1String("tap")) {
            const double delta = playheadSecond - marker.second;
            if (delta > 0.0) {
                continue;
            }
            const DiagTapApproachSample approach = diagSampleTapApproach(delta);
            if (approach.scale <= 0.0) {
                continue;
            }
            const QPointF lane = diagLaneUnitVector(marker.lane);
            const QPointF logical(
                kDiagLogicalCanvasCenter + lane.x() * approach.distance,
                kDiagLogicalCanvasCenter + lane.y() * approach.distance
            );
            ObjectTraceItem item;
            item.key = keyBase;
            item.type = QStringLiteral("tap");
            item.posPx = diagMapLogicalPointToPlayfield(logical, playfield);
            item.extra = QStringLiteral("lane=%1").arg(marker.lane);
            items.append(item);
            continue;
        }

        if (marker.type == QLatin1String("hold")) {
            if (marker.endSecond < marker.second) {
                continue;
            }
            const double delta = playheadSecond - marker.second;
            const double deltaEnd = playheadSecond - marker.endSecond;
            if (deltaEnd > 0.0) {
                continue;
            }
            const DiagTapApproachSample headApproach = diagSampleTapApproach(delta);
            if (headApproach.scale <= 0.0) {
                continue;
            }
            const QPointF lane = diagLaneUnitVector(marker.lane);
            if (delta < -kDiagTapFlyDurationSeconds) {
                const QPointF logical(
                    kDiagLogicalCanvasCenter + lane.x() * kDiagLogicalDistanceTap,
                    kDiagLogicalCanvasCenter + lane.y() * kDiagLogicalDistanceTap
                );
                ObjectTraceItem item;
                item.key = keyBase + QStringLiteral(":spawn");
                item.type = QStringLiteral("hold");
                item.posPx = diagMapLogicalPointToPlayfield(logical, playfield);
                item.extra = QStringLiteral("lane=%1,phase=spawn").arg(marker.lane);
                items.append(item);
                continue;
            }
            const double distance = headApproach.distance;
            double distanceEnd = deltaEnd * kDiagTapUnitsPerSecond + kDiagLogicalDistanceEdge;
            distanceEnd = qBound(kDiagLogicalDistanceTap, distanceEnd, kDiagLogicalDistanceEdge);
            const QPointF logicalHead(
                kDiagLogicalCanvasCenter + lane.x() * distance,
                kDiagLogicalCanvasCenter + lane.y() * distance
            );
            const QPointF logicalTail(
                kDiagLogicalCanvasCenter + lane.x() * distanceEnd,
                kDiagLogicalCanvasCenter + lane.y() * distanceEnd
            );
            ObjectTraceItem head;
            head.key = keyBase + QStringLiteral(":head");
            head.type = QStringLiteral("hold");
            head.posPx = diagMapLogicalPointToPlayfield(logicalHead, playfield);
            head.extra = QStringLiteral("lane=%1,role=head").arg(marker.lane);
            items.append(head);

            ObjectTraceItem tail;
            tail.key = keyBase + QStringLiteral(":tail");
            tail.type = QStringLiteral("hold");
            tail.posPx = diagMapLogicalPointToPlayfield(logicalTail, playfield);
            tail.extra = QStringLiteral("lane=%1,role=tail").arg(marker.lane);
            items.append(tail);
            continue;
        }

        if (marker.type == QLatin1String("slide")) {
            if (marker.hasHeadStar) {
                const double delta = playheadSecond - marker.second;
                if (delta < 0.0) {
                    const DiagTapApproachSample approach = diagSampleTapApproach(delta);
                    if (approach.scale > 0.0) {
                        const QPointF lane = diagLaneUnitVector(marker.lane);
                        const QPointF logical(
                            kDiagLogicalCanvasCenter + lane.x() * approach.distance,
                            kDiagLogicalCanvasCenter + lane.y() * approach.distance
                        );
                        ObjectTraceItem headStar;
                        headStar.key = keyBase + QStringLiteral(":head_pre");
                        headStar.type = QStringLiteral("slide_head");
                        headStar.posPx = diagMapLogicalPointToPlayfield(logical, playfield);
                        headStar.extra = QStringLiteral("lane=%1").arg(marker.lane);
                        items.append(headStar);
                    }
                }
            }
            if (marker.slideTraceSecond <= marker.second || marker.slideSegmentPoints.isEmpty()) {
                continue;
            }
            if (playheadSecond < marker.second || playheadSecond > marker.endSecond) {
                continue;
            }
            QPointF logical;
            qreal angle = 0.0;
            if (playheadSecond < marker.slideTraceSecond) {
                if (!marker.hasHeadStar || marker.slideSegmentPoints.constFirst().isEmpty()) {
                    continue;
                }
                const QVector<QPointF>& points = marker.slideSegmentPoints.constFirst();
                const QVector<double>& angles = marker.slideSegmentAngles.constFirst();
                logical = points.constFirst();
                angle = angles.isEmpty() ? 0.0 : angles.constFirst();
            } else {
                int segmentIndex = 0;
                if (!marker.slideSegmentShootSeconds.isEmpty()
                    && marker.slideSegmentShootSeconds.size() == marker.slideSegmentDurations.size()) {
                    for (int i = marker.slideSegmentShootSeconds.size() - 1; i >= 0; --i) {
                        if (playheadSecond >= marker.slideSegmentShootSeconds[i]) {
                            segmentIndex = i;
                            break;
                        }
                    }
                }
                segmentIndex = qBound(0, segmentIndex, marker.slideSegmentPoints.size() - 1);
                const QVector<QPointF>& points = marker.slideSegmentPoints[segmentIndex];
                const QVector<double>& angles = marker.slideSegmentAngles.value(segmentIndex);
                if (points.isEmpty()) {
                    continue;
                }
                qreal proportion = 1.0;
                if (segmentIndex < marker.slideSegmentShootSeconds.size()
                    && segmentIndex < marker.slideSegmentDurations.size()) {
                    const qreal duration = qMax(0.001, marker.slideSegmentDurations[segmentIndex]);
                    proportion = qBound<qreal>(
                        0.0,
                        (playheadSecond - marker.slideSegmentShootSeconds[segmentIndex]) / duration,
                        1.0
                    );
                }
                logical = diagInterpolatePoint(points, proportion);
                angle = diagInterpolateAngle(angles, proportion);
            }
            ObjectTraceItem item;
            item.key = keyBase + QStringLiteral(":star");
            item.type = QStringLiteral("slide_star");
            item.posPx = diagMapLogicalPointToPlayfield(
                QPointF(kDiagLogicalCanvasCenter + logical.x(), kDiagLogicalCanvasCenter + logical.y()),
                playfield
            );
            item.extra = QStringLiteral("lane=%1,angle=%2").arg(marker.lane).arg(angle, 0, 'f', 2);
            items.append(item);
            continue;
        }

        if (marker.type == QLatin1String("wifi")) {
            if (marker.hasHeadStar) {
                const double delta = playheadSecond - marker.second;
                if (delta < 0.0) {
                    const DiagTapApproachSample approach = diagSampleTapApproach(delta);
                    if (approach.scale > 0.0) {
                        const QPointF lane = diagLaneUnitVector(marker.lane);
                        const QPointF logical(
                            kDiagLogicalCanvasCenter + lane.x() * approach.distance,
                            kDiagLogicalCanvasCenter + lane.y() * approach.distance
                        );
                        ObjectTraceItem headStar;
                        headStar.key = keyBase + QStringLiteral(":head_pre");
                        headStar.type = QStringLiteral("wifi_head");
                        headStar.posPx = diagMapLogicalPointToPlayfield(logical, playfield);
                        headStar.extra = QStringLiteral("lane=%1").arg(marker.lane);
                        items.append(headStar);
                    }
                }
            }
            if (marker.slideTraceSecond <= marker.second || marker.wifiLanePoints.isEmpty()) {
                continue;
            }
            if (playheadSecond < marker.second || playheadSecond > marker.endSecond) {
                continue;
            }
            bool waiting = playheadSecond < marker.slideTraceSecond;
            qreal proportion = 0.0;
            if (!waiting && !marker.slideSegmentDurations.isEmpty()) {
                const qreal duration = qMax(0.001, marker.slideSegmentDurations.constFirst());
                proportion = qBound<qreal>(0.0, (playheadSecond - marker.slideTraceSecond) / duration, 1.0);
            } else if (!waiting && marker.endSecond > marker.slideTraceSecond) {
                const qreal duration = qMax(0.001, marker.endSecond - marker.slideTraceSecond);
                proportion = qBound<qreal>(0.0, (playheadSecond - marker.slideTraceSecond) / duration, 1.0);
            }
            for (int laneIndex = 0; laneIndex < marker.wifiLanePoints.size(); ++laneIndex) {
                const QVector<QPointF>& points = marker.wifiLanePoints[laneIndex];
                const QVector<double>& angles = marker.wifiLaneAngles.value(laneIndex);
                if (points.isEmpty()) {
                    continue;
                }
                const QPointF logical = waiting ? points.constFirst() : diagInterpolatePoint(points, proportion);
                const qreal angle = waiting
                    ? (angles.isEmpty() ? 0.0 : angles.constFirst())
                    : diagInterpolateAngle(angles, proportion);
                ObjectTraceItem item;
                item.key = keyBase + QStringLiteral(":lane%1").arg(laneIndex);
                item.type = QStringLiteral("wifi_star");
                item.posPx = diagMapLogicalPointToPlayfield(
                    QPointF(kDiagLogicalCanvasCenter + logical.x(), kDiagLogicalCanvasCenter + logical.y()),
                    playfield
                );
                item.extra = QStringLiteral("lane=%1,angle=%2").arg(marker.lane).arg(angle, 0, 'f', 2);
                items.append(item);
            }
            continue;
        }

        if (marker.type == QLatin1String("touch")) {
            if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
                continue;
            }
            const qreal delta = playheadSecond - marker.second;
            if (delta <= -kDiagTouchDurationSeconds || delta >= 0.0) {
                continue;
            }
            ObjectTraceItem item;
            item.key = keyBase;
            item.type = QStringLiteral("touch");
            item.posPx = diagMapLogicalPointToPlayfield(marker.touchPoint, playfield);
            item.extra = marker.touchPad;
            items.append(item);
            continue;
        }

        if (marker.type == QLatin1String("touch_hold")) {
            if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
                continue;
            }
            if (marker.endSecond <= marker.second) {
                continue;
            }
            const qreal delta = playheadSecond - marker.second;
            const qreal duration = qMax<qreal>(0.001, marker.endSecond - marker.second);
            if (delta <= -kDiagTouchDurationSeconds || delta >= duration) {
                continue;
            }
            ObjectTraceItem item;
            item.key = keyBase;
            item.type = QStringLiteral("touch_hold");
            item.posPx = diagMapLogicalPointToPlayfield(marker.touchPoint, playfield);
            item.extra = marker.touchPad;
            items.append(item);
            continue;
        }
    }

    std::sort(items.begin(), items.end(), [](const ObjectTraceItem& a, const ObjectTraceItem& b) {
        if (a.type != b.type) {
            return a.type < b.type;
        }
        return a.key < b.key;
    });
    return items;
}

FrameLayerActivityStats estimateFrameLayerActivity(
    const QVector<TimelineNoteMarker>& markers,
    double playheadSecond
)
{
    FrameLayerActivityStats stats;
    for (const TimelineNoteMarker& marker : markers) {
        if (marker.type == QLatin1String("tap")) {
            const double delta = playheadSecond - marker.second;
            const DiagTapApproachSample approach = diagSampleTapApproach(delta);
            if (delta <= 0.0 && approach.scale > 0.0) {
                ++stats.tapVisible;
                if (approach.distance <= kDiagLogicalDistanceTap) {
                    ++stats.tapParked;
                }
            }
            if (delta >= 0.0 && delta <= kDiagJudgeEffectDurationSeconds) {
                ++stats.judgeTapVisible;
            }
            continue;
        }

        if (marker.type == QLatin1String("hold")) {
            if (marker.endSecond >= marker.second) {
                const double delta = playheadSecond - marker.second;
                const double deltaEnd = playheadSecond - marker.endSecond;
                if (deltaEnd <= 0.0 && diagTapSpriteVisible(delta)) {
                    ++stats.holdVisible;
                }
                if (playheadSecond >= marker.second && playheadSecond < marker.endSecond) {
                    ++stats.holdSustainVisible;
                }
                if (deltaEnd >= 0.0 && deltaEnd <= kDiagJudgeEffectDurationSeconds) {
                    ++stats.judgeTapVisible;
                }
            }
            continue;
        }

        if (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) {
            const bool isWifi = marker.type == QLatin1String("wifi");
            const double delta = playheadSecond - marker.second;
            if (marker.hasHeadStar && diagSlideHeadVisible(delta)) {
                ++stats.tapVisible;
                if (delta < -kDiagTapFlyDurationSeconds) {
                    ++stats.tapParked;
                }
            }
            if (delta >= 0.0 && delta <= kDiagJudgeEffectDurationSeconds && marker.hasHeadStar) {
                ++stats.judgeTapVisible;
            }
            if (playheadSecond >= marker.second && playheadSecond <= marker.endSecond) {
                if (isWifi) {
                    ++stats.wifiMotionVisible;
                } else {
                    ++stats.slideMotionVisible;
                }
            }
            if (marker.availableSecond >= 0.0
                && playheadSecond >= marker.second - kDiagSlideTrackAppearLeadInSeconds
                && !(marker.endSecond > marker.slideTraceSecond && playheadSecond >= marker.endSecond)) {
                if (isWifi) {
                    ++stats.wifiTrackVisible;
                } else {
                    ++stats.slideTrackVisible;
                }
            }
            continue;
        }

        if (marker.type == QLatin1String("touch")) {
            const double delta = playheadSecond - marker.second;
            if (delta > -kDiagTouchDurationSeconds && delta < 0.0) {
                ++stats.touchVisible;
            }
            if (delta >= 0.0 && delta <= kDiagJudgeEffectTouchDurationSeconds) {
                ++stats.judgeTouchVisible;
            }
            if (marker.isFirework) {
                const double fireworkElapsed = delta - kDiagJudgeEffectFireworkTouchTriggerDelaySeconds;
                if (fireworkElapsed >= 0.0 && fireworkElapsed <= kDiagJudgeEffectFireworkDurationSeconds) {
                    ++stats.judgeFireworkVisible;
                }
            }
            continue;
        }

        if (marker.type == QLatin1String("touch_hold")) {
            if (marker.endSecond <= marker.second) {
                continue;
            }
            const double delta = playheadSecond - marker.second;
            const double holdDuration = marker.endSecond - marker.second;
            if (delta > -kDiagTouchDurationSeconds && delta < holdDuration) {
                ++stats.touchHoldVisible;
            }
            if (playheadSecond >= marker.second && playheadSecond < marker.endSecond) {
                ++stats.holdSustainVisible;
            }
            const double deltaEnd = playheadSecond - marker.endSecond;
            if (deltaEnd >= 0.0 && deltaEnd <= kDiagJudgeEffectDurationSeconds) {
                ++stats.judgeTapVisible;
            }
            continue;
        }
    }
    return stats;
}

QString normalizePath(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

QString truncateForLog(const QString& text, int maxChars = 4000)
{
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(maxChars) + QStringLiteral(" ...<truncated>");
}

QString videoExportLogPath()
{
    return miacode::debug_log::exportLogPath();
}

bool exportDetailedLoggingEnabled()
{
    return miacode::debug_options::exportDebugOutputEnabled();
}

bool shouldWriteSummaryExportStage(const QString& stage)
{
    if (stage.startsWith(QStringLiteral("fail_")) || stage == QStringLiteral("canceled")) {
        return true;
    }

    static const QStringList kSummaryStages{
        QStringLiteral("export_begin"),
        QStringLiteral("resolve_ffmpeg"),
        QStringLiteral("resolve_ffprobe"),
        QStringLiteral("output_staging"),
        QStringLiteral("stage_static_media"),
        QStringLiteral("stage_static_media_fallback"),
        QStringLiteral("audio_backend_select"),
        QStringLiteral("audio_mix_ok"),
        QStringLiteral("encoder_select"),
        QStringLiteral("skin_bootstrap"),
        QStringLiteral("render_backend"),
        QStringLiteral("offscreen_warmup"),
        QStringLiteral("render_backend_fallback"),
        QStringLiteral("frame_timing_summary"),
        QStringLiteral("raw_pipe_summary"),
        QStringLiteral("encode_output_file"),
        QStringLiteral("ffmpeg_encode_started"),
        QStringLiteral("ffmpeg_remux_started"),
        QStringLiteral("export_file"),
        QStringLiteral("ffprobe_summary"),
        QStringLiteral("export_success"),
    };
    return kSummaryStages.contains(stage);
}

QString summarizedExportLogDetail(const QString& detail)
{
    QString normalized = detail.trimmed();
    if (normalized.isEmpty()) {
        return normalized;
    }
    normalized.replace(QLatin1Char('\n'), QLatin1Char(' '));
    normalized = normalized.simplified();
    return truncateForLog(normalized, 1200);
}

void appendVideoExportLog(const QString& stage, const QString& detail = QString())
{
    if (exportDetailedLoggingEnabled()) {
        miacode::debug_log::appendLine(miacode::debug_log::Channel::Export, stage, detail);
    } else if (shouldWriteSummaryExportStage(stage)) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Export,
            stage,
            summarizedExportLogDetail(detail),
            true
        );
    }
    if (stage.startsWith(QStringLiteral("fail_"))) {
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("export/%1").arg(stage),
            detail.isEmpty() ? QStringLiteral("See export log for details.") : detail
        );
    }
}

bool fileIsExecutable(const QString& path)
{
    const QFileInfo info(path);
    return info.exists() && info.isFile() && info.isExecutable();
}

QString resolveFfmpegExecutable()
{
#ifdef Q_OS_WIN
    const QString ffmpegName = QStringLiteral("ffmpeg.exe");
#else
    const QString ffmpegName = QStringLiteral("ffmpeg");
#endif
    const QString envPath = qEnvironmentVariable("MIACODE_FFMPEG_PATH", qEnvironmentVariable("MIACODE_FFMPEG"));
    if (fileIsExecutable(envPath)) {
        return normalizePath(envPath);
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList appCandidates{
        appDir.filePath(ffmpegName),
        appDir.filePath(QStringLiteral("ffmpeg/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../Resources/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../third_party/ffmpeg/windows/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../third_party/ffmpeg/macos/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../third_party/ffmpeg/linux/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../../third_party/ffmpeg/windows/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../../third_party/ffmpeg/macos/%1").arg(ffmpegName)),
        appDir.filePath(QStringLiteral("../../../third_party/ffmpeg/linux/%1").arg(ffmpegName)),
    };
    for (const QString& candidate : appCandidates) {
        if (fileIsExecutable(candidate)) {
            return normalizePath(candidate);
        }
    }

    const QString fromPath = QStandardPaths::findExecutable(ffmpegName);
    if (fileIsExecutable(fromPath)) {
        return normalizePath(fromPath);
    }
    return QString();
}

QString resolveFfprobeExecutable(const QString& ffmpegPath)
{
#ifdef Q_OS_WIN
    const QString ffprobeName = QStringLiteral("ffprobe.exe");
#else
    const QString ffprobeName = QStringLiteral("ffprobe");
#endif
    if (!ffmpegPath.isEmpty()) {
        const QFileInfo ffmpegInfo(ffmpegPath);
        const QString sibling = ffmpegInfo.dir().filePath(ffprobeName);
        if (fileIsExecutable(sibling)) {
            return normalizePath(sibling);
        }
    }
    const QString fromPath = QStandardPaths::findExecutable(ffprobeName);
    if (fileIsExecutable(fromPath)) {
        return normalizePath(fromPath);
    }
    return QString();
}

bool hasEncoderToken(const QString& encodersOutput, const QString& encoderName)
{
    const QRegularExpression pattern(
        QStringLiteral("(?m)^\\s*\\S{6}\\s+%1(?:\\s|$)").arg(QRegularExpression::escape(encoderName))
    );
    return pattern.match(encodersOutput).hasMatch();
}

QImage buildCircularDimMaskImage(
    int frameWidth,
    int frameHeight,
    double outerDimAlpha,
    double innerDimAlpha,
    double layoutRingDiameterRatio,
    double layoutSquareScale,
    bool smoothBrightness
)
{
    const int width = qMax(1, frameWidth);
    const int height = qMax(1, frameHeight);
    QImage mask(width, height, QImage::Format_RGBA8888);
    mask.fill(Qt::transparent);

    const int outerAlpha = qBound(0, qRound(outerDimAlpha * 255.0), 255);
    const int innerAlpha = qBound(0, qRound(innerDimAlpha * 255.0), 255);
    if (outerAlpha == 0 && innerAlpha == 0) {
        return mask;
    }

    const double layoutSide = miacode::preview_video::layoutSquareSideForCanvasHeight(
        static_cast<double>(height),
        layoutSquareScale
    );
    const double centerX = (static_cast<double>(width) - 1.0) * 0.5;
    const double centerY = (static_cast<double>(height) - 1.0) * 0.5;

    for (int y = 0; y < height; ++y) {
        uchar* row = mask.scanLine(y);
        const double dy = static_cast<double>(y) - centerY;
        for (int x = 0; x < width; ++x) {
            const double dx = static_cast<double>(x) - centerX;
            const double radius = std::sqrt(dx * dx + dy * dy);
            const int alpha = qBound(
                0,
                qRound(
                    miacode::preview_video::dimAlphaForRadius(
                        radius,
                        outerDimAlpha,
                        innerDimAlpha,
                        layoutSide,
                        layoutRingDiameterRatio,
                        smoothBrightness
                    ) * 255.0
                ),
                255
            );
            const int offset = x * 4;
            row[offset + 0] = 0;
            row[offset + 1] = 0;
            row[offset + 2] = 0;
            row[offset + 3] = static_cast<uchar>(alpha);
        }
    }
    return mask;
}

QRectF staticMediaTargetRect(
    const QSize& mediaSize,
    const QSize& outputSize,
    PreviewBackgroundScaleMode scaleMode)
{
    if (mediaSize.isEmpty() || outputSize.isEmpty()) {
        return QRectF();
    }

    QSize fittedSize = mediaSize;
    fittedSize.scale(
        outputSize,
        scaleMode == PreviewBackgroundScaleMode::FitContain
            ? Qt::KeepAspectRatio
            : Qt::KeepAspectRatioByExpanding
    );
    if (fittedSize.isEmpty()) {
        return QRectF();
    }

    return QRectF(
        (outputSize.width() - fittedSize.width()) * 0.5,
        (outputSize.height() - fittedSize.height()) * 0.5,
        fittedSize.width(),
        fittedSize.height()
    );
}

bool stageStaticBackgroundImageForExport(
    const QString& sourcePath,
    const QSize& outputSize,
    PreviewBackgroundScaleMode scaleMode,
    const QString& stagedPath,
    QString* detail)
{
    if (detail != nullptr) {
        detail->clear();
    }

    if (sourcePath.isEmpty() || outputSize.isEmpty() || stagedPath.isEmpty()) {
        if (detail != nullptr) {
            *detail = QStringLiteral("invalid_input");
        }
        return false;
    }

    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    const QImage sourceImage = reader.read();
    if (sourceImage.isNull()) {
        if (detail != nullptr) {
            *detail = QStringLiteral("read_failed error=%1").arg(reader.errorString());
        }
        return false;
    }

    QImage stagedImage(outputSize, QImage::Format_RGBA8888);
    stagedImage.fill(Qt::black);

    QPainter painter(&stagedImage);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(staticMediaTargetRect(sourceImage.size(), outputSize, scaleMode), sourceImage);
    painter.end();

    if (!stagedImage.save(stagedPath)) {
        if (detail != nullptr) {
            *detail = QStringLiteral("save_failed path=%1").arg(stagedPath);
        }
        return false;
    }

    if (detail != nullptr) {
        *detail = QStringLiteral("source=%1x%2 staged=%3x%4 mode=%5 path=%6")
            .arg(sourceImage.width())
            .arg(sourceImage.height())
            .arg(stagedImage.width())
            .arg(stagedImage.height())
            .arg(scaleMode == PreviewBackgroundScaleMode::FitContain ? QStringLiteral("fit") : QStringLiteral("fill"))
            .arg(stagedPath);
    }
    return true;
}

bool probeEncoderRuntimeAvailability(
    const QString& ffmpegPath,
    const VideoEncoderConfig& candidate,
    int outputWidth,
    int outputHeight,
    int fps,
    QString* detail
)
{
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    const int safeFps = qBound(24, qMax(1, fps), 120);
    QSize probeSize(qMax(64, outputWidth), qMax(64, outputHeight));
    probeSize.scale(
        candidate.isHardware ? QSize(1920, 1080) : QSize(1280, 720),
        Qt::KeepAspectRatio
    );
    probeSize.setWidth(qMax(64, probeSize.width() & ~1));
    probeSize.setHeight(qMax(64, probeSize.height() & ~1));
    const int probeFrameCount = candidate.isHardware ? qBound(6, safeFps / 5, 12) : 1;
    const double probeDurationSeconds =
        qMax(0.10, static_cast<double>(probeFrameCount) / static_cast<double>(safeFps));
    QStringList args{
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"),
        QStringLiteral("color=c=black:s=%1x%2:r=%3:d=%4")
            .arg(probeSize.width())
            .arg(probeSize.height())
            .arg(safeFps)
            .arg(QString::number(probeDurationSeconds, 'f', 3)),
        QStringLiteral("-an"),
        QStringLiteral("-frames:v"), QString::number(probeFrameCount),
        QStringLiteral("-c:v"), candidate.codec,
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p")
    };
    if (candidate.explicitBframes >= 0) {
        args << QStringLiteral("-bf") << QString::number(candidate.explicitBframes);
    }
    args << candidate.extraArgs;
    args << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");

    probe.start(ffmpegPath, args, QIODevice::ReadOnly);
    if (!probe.waitForStarted(3000)) {
        if (detail != nullptr) {
            *detail = QStringLiteral("start_failed:%1").arg(probe.errorString());
        }
        return false;
    }

    constexpr int kProbeSliceMs = 50;
    const int kProbeTimeoutMs = candidate.isHardware ? 6000 : 4000;
    int elapsedMs = 0;
    while (probe.state() != QProcess::NotRunning && elapsedMs < kProbeTimeoutMs) {
        probe.waitForFinished(kProbeSliceMs);
        elapsedMs += kProbeSliceMs;
        QCoreApplication::processEvents();
    }
    if (probe.state() != QProcess::NotRunning) {
        probe.kill();
        probe.waitForFinished(1000);
        if (detail != nullptr) {
            *detail = QStringLiteral("probe=%1x%2 frames=%3 timeout")
                .arg(probeSize.width())
                .arg(probeSize.height())
                .arg(probeFrameCount);
        }
        return false;
    }

    const QString output = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    const bool ok = probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
    if (detail != nullptr) {
        *detail = QStringLiteral("probe=%1x%2 frames=%3")
            .arg(probeSize.width())
            .arg(probeSize.height())
            .arg(probeFrameCount);
    }
    if (!ok && detail != nullptr) {
        *detail += QStringLiteral(" status=%1 code=%2 output=%3")
                       .arg(static_cast<int>(probe.exitStatus()))
                       .arg(probe.exitCode())
                       .arg(truncateForLog(output, 280));
    }
    return ok;
}

VideoEncoderConfig chooseVideoEncoder(
    const QString& ffmpegPath,
    int outputWidth,
    int outputHeight,
    int fps,
    VideoExportPreset preset,
    const SystemMemoryInfo& memoryInfo,
    const ExportRuntimeConfig& exportConfig,
    QString* probeLog
)
{
    VideoEncoderConfig config;
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(
        ffmpegPath,
        QStringList{QStringLiteral("-hide_banner"), QStringLiteral("-encoders")},
        QIODevice::ReadOnly
    );
    if (!probe.waitForStarted(5000)) {
        config.codec = QStringLiteral("mpeg4");
        config.extraArgs = {
            QStringLiteral("-q:v"),
            preset == VideoExportPreset::HighQuality
                ? QStringLiteral("3")
                : QStringLiteral("4")
        };
        if (probeLog != nullptr) {
            *probeLog = QStringLiteral("encoder_probe_start_failed error=%1 fallback=%2")
                .arg(probe.errorString(), config.codec);
        }
        return config;
    }

    probe.waitForFinished(10000);
    const QString output = QString::fromUtf8(probe.readAllStandardOutput());
    const bool hasHevcNvenc = hasEncoderToken(output, QStringLiteral("hevc_nvenc"));
    const bool hasHevcQsv = hasEncoderToken(output, QStringLiteral("hevc_qsv"));
    const bool hasHevcAmf = hasEncoderToken(output, QStringLiteral("hevc_amf"));
    const bool hasHevcMf = hasEncoderToken(output, QStringLiteral("hevc_mf"));
    const bool hasH264Nvenc = hasEncoderToken(output, QStringLiteral("h264_nvenc"));
    const bool hasH264Qsv = hasEncoderToken(output, QStringLiteral("h264_qsv"));
    const bool hasH264Amf = hasEncoderToken(output, QStringLiteral("h264_amf"));
    const bool hasH264Mf = hasEncoderToken(output, QStringLiteral("h264_mf"));
    const bool hasLibx264 = hasEncoderToken(output, QStringLiteral("libx264"));
    const bool hasOpenH264 = hasEncoderToken(output, QStringLiteral("libopenh264"));
    const bool hasMpeg4 = hasEncoderToken(output, QStringLiteral("mpeg4"));
    const int safeWidth = qMax(1, outputWidth);
    const int safeHeight = qMax(1, outputHeight);
    const int safeFps = qMax(1, fps);
    const int idealThreadCount = qMax(1, QThread::idealThreadCount());
    const VideoBitratePlan bitratePlan = chooseVideoBitratePlan(preset, safeWidth, safeHeight, safeFps);
    const X264TuningPlan x264Plan =
        chooseX264TuningPlan(preset, exportConfig, memoryInfo, safeWidth, safeHeight, safeFps, idealThreadCount);
    const QStringList bitrateArgs = bitratePlan.toArgs();
    const QStringList x264Args = x264Plan.toArgs();
    const auto mpeg4Args = [preset]() {
        switch (preset) {
        case VideoExportPreset::HighQuality:
            return QStringList{QStringLiteral("-q:v"), QStringLiteral("3")};
        case VideoExportPreset::Fast:
        default:
            return QStringList{QStringLiteral("-q:v"), QStringLiteral("4")};
        }
    };
    const auto openH264Args = [preset, bitratePlan]() {
        qint64 targetBitrateKbps = bitratePlan.bitrateKbps;
        if (preset == VideoExportPreset::HighQuality) {
            targetBitrateKbps = qRound64(static_cast<double>(targetBitrateKbps) * 1.10);
        }
        return QStringList{
            QStringLiteral("-b:v"), QString::number(qMax<qint64>(600LL, targetBitrateKbps)) + QLatin1String("k")
        };
    };
    const auto makeEncoderConfig = [&](const QString& codec, bool isHardware) {
        VideoEncoderConfig item;
        item.codec = codec;
        item.isHardware = isHardware;

        const bool isH264Codec = codec.startsWith(QStringLiteral("h264"));
        if (codec == QLatin1String("libx264")) {
            item.extraArgs = x264Args;
            return item;
        }
        if (codec == QLatin1String("libopenh264")) {
            item.extraArgs = openH264Args();
            return item;
        }
        if (codec == QLatin1String("mpeg4")) {
            item.extraArgs = mpeg4Args();
            return item;
        }

        if (preset == VideoExportPreset::Fast) {
            item.extraArgs = bitrateArgs;
            item.explicitBframes = isH264Codec ? 0 : -1;
            return item;
        }

        // Keep the high-quality preset on the same conservative hardware path
        // as fast mode, and raise quality through bitrate/CRF instead.
        item.extraArgs = bitrateArgs;
        item.explicitBframes = isH264Codec ? 0 : -1;
        return item;
    };
    const QString forcedEncoder = exportConfig.forcedEncoder;
    const EncoderAutoMode encoderAutoMode = exportConfig.encoderMode;
    QString encoderAutoModeReason;
    const bool preferHardwareFirst = shouldPreferHardwareEncoderInAutoMode(
        encoderAutoMode,
        safeWidth,
        safeHeight,
        safeFps,
        idealThreadCount,
        memoryInfo,
        &encoderAutoModeReason
    );

    QVector<VideoEncoderConfig> candidates;
    candidates.reserve(14);
    const auto pushCandidate = [&candidates, &makeEncoderConfig](const QString& codec, bool isHardware) {
        candidates.push_back(makeEncoderConfig(codec, isHardware));
    };
    const bool forceSpecificEncoder = !forcedEncoder.isEmpty();
    const auto autoModeEnabled = [&forceSpecificEncoder]() {
        return !forceSpecificEncoder;
    };
    const auto forcedMatches = [&forcedEncoder](const QString& codec) {
        return !forcedEncoder.isEmpty() && codec.compare(forcedEncoder, Qt::CaseInsensitive) == 0;
    };
    const auto appendAutoHardwareCandidates = [&]() {
        if (hasH264Nvenc) {
            pushCandidate(QStringLiteral("h264_nvenc"), true);
        }
        if (hasH264Qsv) {
            pushCandidate(QStringLiteral("h264_qsv"), true);
        }
        if (hasH264Amf) {
            pushCandidate(QStringLiteral("h264_amf"), true);
        }
        if (hasH264Mf) {
            pushCandidate(QStringLiteral("h264_mf"), true);
        }
        if (encoderAutoMode == EncoderAutoMode::Hardware) {
            if (hasHevcNvenc) {
                pushCandidate(QStringLiteral("hevc_nvenc"), true);
            }
            if (hasHevcQsv) {
                pushCandidate(QStringLiteral("hevc_qsv"), true);
            }
            if (hasHevcAmf) {
                pushCandidate(QStringLiteral("hevc_amf"), true);
            }
            if (hasHevcMf) {
                pushCandidate(QStringLiteral("hevc_mf"), true);
            }
        }
    };
    const auto appendAutoSoftwareCandidates = [&]() {
        if (hasLibx264) {
            pushCandidate(QStringLiteral("libx264"), false);
        }
        if (hasOpenH264) {
            pushCandidate(QStringLiteral("libopenh264"), false);
        }
    };

    // Automatic mode now prefers conservative H.264 export paths for better compatibility and speed.
    if (autoModeEnabled()) {
        if (preferHardwareFirst) {
            appendAutoHardwareCandidates();
            appendAutoSoftwareCandidates();
        } else {
            appendAutoSoftwareCandidates();
            appendAutoHardwareCandidates();
        }
    } else {
        if (forcedMatches(QStringLiteral("hevc_nvenc")) && hasHevcNvenc) {
            pushCandidate(QStringLiteral("hevc_nvenc"), true);
        }
        if (forcedMatches(QStringLiteral("hevc_qsv")) && hasHevcQsv) {
            pushCandidate(QStringLiteral("hevc_qsv"), true);
        }
        if (forcedMatches(QStringLiteral("hevc_amf")) && hasHevcAmf) {
            pushCandidate(QStringLiteral("hevc_amf"), true);
        }
        if (forcedMatches(QStringLiteral("hevc_mf")) && hasHevcMf) {
            pushCandidate(QStringLiteral("hevc_mf"), true);
        }
        if (forcedMatches(QStringLiteral("h264_nvenc")) && hasH264Nvenc) {
            pushCandidate(QStringLiteral("h264_nvenc"), true);
        }
        if (forcedMatches(QStringLiteral("h264_qsv")) && hasH264Qsv) {
            pushCandidate(QStringLiteral("h264_qsv"), true);
        }
        if (forcedMatches(QStringLiteral("h264_amf")) && hasH264Amf) {
            pushCandidate(QStringLiteral("h264_amf"), true);
        }
        if (forcedMatches(QStringLiteral("h264_mf")) && hasH264Mf) {
            pushCandidate(QStringLiteral("h264_mf"), true);
        }
        if (forcedMatches(QStringLiteral("libx264")) && hasLibx264) {
            pushCandidate(QStringLiteral("libx264"), false);
        }
        if (forcedMatches(QStringLiteral("libopenh264")) && hasOpenH264) {
            pushCandidate(QStringLiteral("libopenh264"), false);
        }
        if (forcedMatches(QStringLiteral("mpeg4")) && hasMpeg4) {
            pushCandidate(QStringLiteral("mpeg4"), false);
        }
    }
    if (hasMpeg4 || candidates.isEmpty()) {
        pushCandidate(QStringLiteral("mpeg4"), false);
    }

    if (!forcedEncoder.isEmpty()) {
        const auto forcedIt = std::find_if(
            candidates.cbegin(),
            candidates.cend(),
            [&forcedEncoder](const VideoEncoderConfig& candidate) {
                return candidate.codec.compare(forcedEncoder, Qt::CaseInsensitive) == 0;
            }
        );
        if (forcedIt != candidates.cend()) {
            config = *forcedIt;
            if (probeLog != nullptr) {
                QString detail = QStringLiteral(
                    "encoder_probe forced=%1 selected=%2 hw=%3 size=%4x%5 preset=%6")
                    .arg(forcedEncoder)
                    .arg(config.codec)
                    .arg(config.isHardware ? 1 : 0)
                    .arg(safeWidth)
                    .arg(safeHeight)
                    .arg(videoExportPresetToken(preset));
                if (config.codec == QLatin1String("libx264")) {
                    detail += QStringLiteral(" x264Preset=%1 x264Crf=%2 x264Bframes=%3")
                        .arg(x264Plan.preset)
                        .arg(x264Plan.crf)
                        .arg(x264Plan.bframes);
                }
                *probeLog = detail;
            }
            return config;
        }
        if (probeLog != nullptr) {
            *probeLog = QStringLiteral("encoder_probe forced=%1 unavailable").arg(forcedEncoder);
        }
        config.codec = QStringLiteral("mpeg4");
        config.extraArgs = mpeg4Args();
        config.isHardware = false;
        return config;
    }

    const bool skipRuntimeProbe = exportConfig.skipEncoderRuntimeProbe;
    QStringList runtimeProbeLines;
    runtimeProbeLines.reserve(candidates.size());
    bool selected = false;
    for (const VideoEncoderConfig& candidate : candidates) {
        if (skipRuntimeProbe) {
            config = candidate;
            runtimeProbeLines.append(QStringLiteral("%1:skip").arg(candidate.codec));
            selected = true;
            break;
        }
        if (!candidate.isHardware) {
            config = candidate;
            runtimeProbeLines.append(QStringLiteral("%1:software").arg(candidate.codec));
            selected = true;
            break;
        }
        QString probeDetail;
        if (probeEncoderRuntimeAvailability(ffmpegPath, candidate, safeWidth, safeHeight, fps, &probeDetail)) {
            config = candidate;
            runtimeProbeLines.append(QStringLiteral("%1:ok").arg(candidate.codec));
            selected = true;
            break;
        }
        runtimeProbeLines.append(
            QStringLiteral("%1:fail(%2)").arg(candidate.codec, truncateForLog(probeDetail, 180))
        );
    }
    if (!selected) {
        for (const VideoEncoderConfig& candidate : candidates) {
            if (!candidate.isHardware) {
                config = candidate;
                selected = true;
                runtimeProbeLines.append(QStringLiteral("fallback=software:%1").arg(candidate.codec));
                break;
            }
        }
    }
    if (!selected) {
        config.codec = QStringLiteral("mpeg4");
        config.extraArgs = mpeg4Args();
        config.isHardware = false;
        runtimeProbeLines.append(QStringLiteral("fallback=hardcoded:mpeg4"));
    }

    if (probeLog != nullptr) {
        QString detail = QStringLiteral(
            "encoder_probe hevc_nvenc=%1 hevc_qsv=%2 hevc_amf=%3 hevc_mf=%4 "
            "h264_nvenc=%5 h264_qsv=%6 h264_amf=%7 h264_mf=%8 libx264=%9 libopenh264=%10 mpeg4=%11 "
            "selected=%12 hw=%13 bitrateK=%14 maxrateK=%15 size=%16x%17 autoMode=%18 hwFirst=%19 modeReason=%20 preset=%21")
            .arg(hasHevcNvenc ? 1 : 0)
            .arg(hasHevcQsv ? 1 : 0)
            .arg(hasHevcAmf ? 1 : 0)
            .arg(hasHevcMf ? 1 : 0)
            .arg(hasH264Nvenc ? 1 : 0)
            .arg(hasH264Qsv ? 1 : 0)
            .arg(hasH264Amf ? 1 : 0)
            .arg(hasH264Mf ? 1 : 0)
            .arg(hasLibx264 ? 1 : 0)
            .arg(hasOpenH264 ? 1 : 0)
            .arg(hasMpeg4 ? 1 : 0)
            .arg(config.codec)
            .arg(config.isHardware ? 1 : 0)
            .arg(bitratePlan.bitrateKbps)
            .arg(bitratePlan.maxRateKbps)
            .arg(safeWidth)
            .arg(safeHeight)
            .arg(encoderAutoModeToken(encoderAutoMode))
            .arg(preferHardwareFirst ? 1 : 0)
            .arg(encoderAutoModeReason)
            .arg(videoExportPresetToken(preset));
        if (!runtimeProbeLines.isEmpty()) {
            detail += QStringLiteral(" runtime=%1")
                .arg(truncateForLog(runtimeProbeLines.join(QLatin1Char(';')), 2400));
        }
        if (config.codec == QLatin1String("libx264")) {
            detail += QStringLiteral(" x264Preset=%1 x264Crf=%2 x264Bframes=%3")
                .arg(x264Plan.preset)
                .arg(x264Plan.crf)
                .arg(x264Plan.bframes);
        }
        *probeLog = detail;
    }
    return config;
}

bool isImageMediaPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("jpg")
        || suffix == QLatin1String("jpeg")
        || suffix == QLatin1String("png")
        || suffix == QLatin1String("bmp")
        || suffix == QLatin1String("webp");
}

std::unique_ptr<miacode::video_export::VideoExportAudioBackend> createExportAudioBackend(QString* errorMessage)
{
#ifdef Q_OS_WIN
    auto backend = std::make_unique<miacode::video_export::BassExportAudioBackend>();
    QString reason;
    if (!backend->isSupported(&reason)) {
        if (errorMessage != nullptr) {
            *errorMessage = reason;
        }
        return {};
    }
    return backend;
#else
    auto backend = std::make_unique<miacode::video_export::LegacyExportAudioBackend>();
    QString reason;
    backend->isSupported(&reason);
    Q_UNUSED(reason);
    return backend;
#endif
}

quint64 fnv1a64Bytes(const char* data, qint64 size)
{
    if (data == nullptr || size <= 0) {
        return 0;
    }
    quint64 hash = 1469598103934665603ULL;
    for (qint64 i = 0; i < size; ++i) {
        hash ^= static_cast<quint64>(static_cast<unsigned char>(data[i]));
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool writeAllToProcess(QProcess* process, const char* data, qint64 size, QString* failureDetail = nullptr)
{
    constexpr qint64 kWriteChunkBytes = 1LL * 1024LL * 1024LL;
    constexpr qint64 kQueueHighWaterBytes = 4LL * 1024LL * 1024LL;
    constexpr qint64 kQueueLowWaterBytes = 1LL * 1024LL * 1024LL;

    if (process == nullptr || data == nullptr || size < 0) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid process write input");
        }
        return false;
    }
    qint64 writtenTotal = 0;
    while (writtenTotal < size) {
        while (process->bytesToWrite() > kQueueHighWaterBytes) {
            if (process->state() != QProcess::Running) {
                if (failureDetail != nullptr) {
                    *failureDetail = QStringLiteral("process exited while draining chunk queue after %1/%2 bytes")
                        .arg(writtenTotal)
                        .arg(size);
                }
                return false;
            }
            if (!process->waitForBytesWritten(30000)) {
                if (failureDetail != nullptr) {
                    *failureDetail = QStringLiteral("chunk queue drain timeout after %1/%2 bytes queued=%3")
                        .arg(writtenTotal)
                        .arg(size)
                        .arg(process->bytesToWrite());
                }
                return false;
            }
            if (process->bytesToWrite() <= kQueueLowWaterBytes) {
                break;
            }
        }

        const qint64 chunkBytes = qMin(kWriteChunkBytes, size - writtenTotal);
        const qint64 written = process->write(data + writtenTotal, chunkBytes);
        if (written < 0) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral("write returned %1 after %2/%3 bytes")
                    .arg(written)
                    .arg(writtenTotal)
                    .arg(size);
            }
            return false;
        }
        if (written == 0) {
            if (!process->waitForBytesWritten(30000)) {
                if (failureDetail != nullptr) {
                    *failureDetail = QStringLiteral("waitForBytesWritten failed after %1/%2 bytes")
                        .arg(writtenTotal)
                        .arg(size);
                }
                return false;
            }
            continue;
        }
        writtenTotal += written;
    }
    return true;
}

ExportPipeBackpressurePlan chooseExportPipeBackpressurePlan(const QSize& frameSize)
{
    constexpr qint64 kHighWaterFloorBytes = 24LL * 1024LL * 1024LL;
    constexpr qint64 kLowWaterFloorBytes = 8LL * 1024LL * 1024LL;

    ExportPipeBackpressurePlan plan;
    const qint64 width = qMax(1, frameSize.width());
    const qint64 height = qMax(1, frameSize.height());
    plan.frameBytes = width * height * 4LL;
    plan.lowWaterBytes = qMax(kLowWaterFloorBytes, plan.frameBytes);
    plan.highWaterBytes = qMax(kHighWaterFloorBytes, plan.frameBytes * 3LL);
    if (plan.highWaterBytes <= plan.lowWaterBytes) {
        plan.highWaterBytes = plan.lowWaterBytes + plan.frameBytes;
    }
    return plan;
}

bool waitForProcessBackpressureDrain(
    QProcess* process,
    const ExportPipeBackpressurePlan& plan,
    ExportPipeBackpressureStats* stats,
    int frameIndex,
    qint64* waitNs,
    qint64* peakQueuedBytes,
    QString* failureDetail)
{
    if (waitNs != nullptr) {
        *waitNs = 0;
    }
    if (peakQueuedBytes != nullptr) {
        *peakQueuedBytes = 0;
    }
    if (process == nullptr) {
        if (failureDetail != nullptr) {
            *failureDetail = QStringLiteral("invalid process for backpressure wait");
        }
        return false;
    }

    const qint64 initialQueuedBytes = process->bytesToWrite();
    if (initialQueuedBytes <= plan.highWaterBytes) {
        return true;
    }

    if (stats != nullptr) {
        ++stats->hitCount;
    }

    qint64 peakBytes = initialQueuedBytes;
    QElapsedTimer timer;
    timer.start();

    while (true) {
        if (process->state() != QProcess::Running) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral(
                                     "process exited during backpressure wait queued=%1 high=%2 low=%3")
                                     .arg(process->bytesToWrite())
                                     .arg(plan.highWaterBytes)
                                     .arg(plan.lowWaterBytes);
            }
            return false;
        }

        const qint64 queuedBytes = process->bytesToWrite();
        peakBytes = qMax(peakBytes, queuedBytes);
        if (queuedBytes <= plan.lowWaterBytes) {
            break;
        }

        if (timer.elapsed() >= plan.waitTimeoutMs) {
            if (failureDetail != nullptr) {
                *failureDetail = QStringLiteral(
                                     "backpressure drain timeout queued=%1 high=%2 low=%3 timeoutMs=%4")
                                     .arg(queuedBytes)
                                     .arg(plan.highWaterBytes)
                                     .arg(plan.lowWaterBytes)
                                     .arg(plan.waitTimeoutMs);
            }
            return false;
        }

        process->waitForBytesWritten(plan.waitSliceMs);
    }

    const qint64 elapsedNs = timer.nsecsElapsed();
    if (waitNs != nullptr) {
        *waitNs = elapsedNs;
    }
    if (peakQueuedBytes != nullptr) {
        *peakQueuedBytes = peakBytes;
    }
    if (stats != nullptr) {
        stats->totalWaitNs += elapsedNs;
        if (elapsedNs > stats->maxWaitNs) {
            stats->maxWaitNs = elapsedNs;
            stats->maxWaitFrame = frameIndex;
        }
        if (peakBytes > stats->maxQueuedBytes) {
            stats->maxQueuedBytes = peakBytes;
            stats->maxQueuedFrame = frameIndex;
        }
    }
    return true;
}

bool writeAllToFile(QFile* file, const char* data, qint64 size)
{
    if (file == nullptr || data == nullptr || size < 0) {
        return false;
    }
    qint64 writtenTotal = 0;
    while (writtenTotal < size) {
        const qint64 written = file->write(data + writtenTotal, size - writtenTotal);
        if (written <= 0) {
            return false;
        }
        writtenTotal += written;
    }
    return true;
}

bool preparePackedRgbaFrame(
    const QImage& frame,
    QImage* convertedFrame,
    QByteArray* packedScratch,
    const char** data,
    qint64* size
)
{
    if (convertedFrame == nullptr || packedScratch == nullptr || data == nullptr || size == nullptr) {
        return false;
    }
    *convertedFrame = QImage();
    packedScratch->clear();
    *data = nullptr;
    *size = 0;

    const QImage* rgba = &frame;
    if (rgba->format() != QImage::Format_RGBA8888) {
        *convertedFrame = frame.convertToFormat(QImage::Format_RGBA8888);
        if (!frame.isNull() && convertedFrame->isNull()) {
            return false;
        }
        rgba = convertedFrame;
    }
    if (!frame.isNull() && rgba->isNull()) {
        return false;
    }

    const int width = rgba->width();
    const int height = rgba->height();
    if (width <= 0 || height <= 0) {
        return true;
    }

    const qint64 packedStride = static_cast<qint64>(width) * 4;
    const qint64 packedSize = packedStride * height;
    if (rgba->bytesPerLine() == packedStride) {
        if (packedSize > 0 && rgba->constBits() == nullptr) {
            return false;
        }
        *data = reinterpret_cast<const char*>(rgba->constBits());
        *size = packedSize;
        return true;
    }

    packedScratch->resize(static_cast<qsizetype>(packedSize));
    if (packedScratch->size() != packedSize || (packedSize > 0 && packedScratch->data() == nullptr)) {
        packedScratch->clear();
        return false;
    }
    for (int y = 0; y < height; ++y) {
        std::memcpy(
            packedScratch->data() + y * packedStride,
            rgba->constScanLine(y),
            static_cast<size_t>(packedStride)
        );
    }
    *data = packedScratch->constData();
    *size = packedScratch->size();
    return true;
}

double meanAbsDiffNormalized(const QImage& lhs, const QImage& rhs)
{
    QImage a = lhs;
    if (a.format() != QImage::Format_RGBA8888) {
        a = lhs.convertToFormat(QImage::Format_RGBA8888);
    }
    QImage b = rhs;
    if (b.format() != QImage::Format_RGBA8888) {
        b = rhs.convertToFormat(QImage::Format_RGBA8888);
    }
    if (a.size() != b.size() || a.width() <= 0 || a.height() <= 0) {
        return -1.0;
    }

    qint64 totalAbs = 0;
    const int width = a.width();
    const int height = a.height();
    const int rowBytes = width * 4;
    for (int y = 0; y < height; ++y) {
        const uchar* rowA = a.constScanLine(y);
        const uchar* rowB = b.constScanLine(y);
        for (int x = 0; x < rowBytes; ++x) {
            totalAbs += qAbs(static_cast<int>(rowA[x]) - static_cast<int>(rowB[x]));
        }
    }
    const double denom = static_cast<double>(width) * height * 4.0 * 255.0;
    return denom > 0.0 ? static_cast<double>(totalAbs) / denom : -1.0;
}

double meanAbsDiffNormalizedRect(const QImage& lhs, const QImage& rhs, const QRect& rect)
{
    QImage a = lhs;
    if (a.format() != QImage::Format_RGBA8888) {
        a = lhs.convertToFormat(QImage::Format_RGBA8888);
    }
    QImage b = rhs;
    if (b.format() != QImage::Format_RGBA8888) {
        b = rhs.convertToFormat(QImage::Format_RGBA8888);
    }
    if (a.size() != b.size() || a.width() <= 0 || a.height() <= 0) {
        return -1.0;
    }

    const int width = a.width();
    const int height = a.height();
    const int x0 = qBound(0, rect.left(), width);
    const int y0 = qBound(0, rect.top(), height);
    const int x1 = qBound(0, rect.right() + 1, width);
    const int y1 = qBound(0, rect.bottom() + 1, height);
    if (x1 <= x0 || y1 <= y0) {
        return -1.0;
    }

    qint64 totalAbs = 0;
    for (int y = y0; y < y1; ++y) {
        const uchar* rowA = a.constScanLine(y);
        const uchar* rowB = b.constScanLine(y);
        for (int x = x0; x < x1; ++x) {
            const int p = x * 4;
            totalAbs += qAbs(static_cast<int>(rowA[p + 0]) - static_cast<int>(rowB[p + 0]));
            totalAbs += qAbs(static_cast<int>(rowA[p + 1]) - static_cast<int>(rowB[p + 1]));
            totalAbs += qAbs(static_cast<int>(rowA[p + 2]) - static_cast<int>(rowB[p + 2]));
            totalAbs += qAbs(static_cast<int>(rowA[p + 3]) - static_cast<int>(rowB[p + 3]));
        }
    }
    const double denom = static_cast<double>(x1 - x0) * (y1 - y0) * 4.0 * 255.0;
    return denom > 0.0 ? static_cast<double>(totalAbs) / denom : -1.0;
}

double meanAbsDiffAroundTraceItems(
    const QImage& lhs,
    const QImage& rhs,
    const QVector<ObjectTraceItem>& traceItems,
    int radius,
    double* maxDiffOut
)
{
    if (maxDiffOut != nullptr) {
        *maxDiffOut = -1.0;
    }
    if (traceItems.isEmpty()) {
        return -1.0;
    }

    const int clampedRadius = qBound(2, radius, 512);
    double sumDiff = 0.0;
    double maxDiff = -1.0;
    int count = 0;
    for (const ObjectTraceItem& item : traceItems) {
        const int cx = qRound(item.posPx.x());
        const int cy = qRound(item.posPx.y());
        const QRect roi(
            cx - clampedRadius,
            cy - clampedRadius,
            clampedRadius * 2 + 1,
            clampedRadius * 2 + 1
        );
        const double diff = meanAbsDiffNormalizedRect(lhs, rhs, roi);
        if (diff < 0.0) {
            continue;
        }
        sumDiff += diff;
        maxDiff = qMax(maxDiff, diff);
        ++count;
    }
    if (maxDiffOut != nullptr) {
        *maxDiffOut = maxDiff;
    }
    if (count <= 0) {
        return -1.0;
    }
    return sumDiff / count;
}

quint64 sampledFrameSignature(const QImage& frame)
{
    QImage rgba = frame;
    if (rgba.format() != QImage::Format_RGBA8888) {
        rgba = frame.convertToFormat(QImage::Format_RGBA8888);
    }
    const int width = rgba.width();
    const int height = rgba.height();
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const int stepX = qMax(1, width / 16);
    const int stepY = qMax(1, height / 16);

    quint64 hash = 1469598103934665603ULL;
    for (int y = 0; y < height; y += stepY) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < width; x += stepX) {
            const uchar* px = row + (x * 4);
            hash ^= static_cast<quint64>(px[0]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(px[1]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(px[2]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(px[3]);
            hash *= 1099511628211ULL;
        }
    }
    hash ^= static_cast<quint64>(width);
    hash *= 1099511628211ULL;
    hash ^= static_cast<quint64>(height);
    return hash;
}

quint64 fullFrameSignature(const QImage& frame, int cropBottom)
{
    QImage rgba = frame;
    if (rgba.format() != QImage::Format_RGBA8888) {
        rgba = frame.convertToFormat(QImage::Format_RGBA8888);
    }
    const int width = rgba.width();
    const int height = rgba.height();
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const int clampedCrop = qBound(0, cropBottom, height - 1);
    const int hashHeight = height - clampedCrop;
    const int packedWidthBytes = width * 4;
    quint64 hash = 1469598103934665603ULL;
    for (int y = 0; y < hashHeight; ++y) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < packedWidthBytes; ++x) {
            hash ^= static_cast<quint64>(row[x]);
            hash *= 1099511628211ULL;
        }
    }
    hash ^= static_cast<quint64>(width);
    hash *= 1099511628211ULL;
    hash ^= static_cast<quint64>(hashHeight);
    return hash;
}

quint64 objectOnlyFrameSignature(
    const QImage& frameWithObjects,
    const QImage& frameWithoutObjects,
    int diffThreshold,
    int* activePixelCount
)
{
    if (activePixelCount != nullptr) {
        *activePixelCount = 0;
    }

    QImage withRgba = frameWithObjects;
    if (withRgba.format() != QImage::Format_RGBA8888) {
        withRgba = frameWithObjects.convertToFormat(QImage::Format_RGBA8888);
    }
    QImage withoutRgba = frameWithoutObjects;
    if (withoutRgba.format() != QImage::Format_RGBA8888) {
        withoutRgba = frameWithoutObjects.convertToFormat(QImage::Format_RGBA8888);
    }
    if (withRgba.size() != withoutRgba.size()) {
        return 0;
    }

    const int width = withRgba.width();
    const int height = withRgba.height();
    if (width <= 0 || height <= 0) {
        return 0;
    }

    const int clampedThreshold = qBound(0, diffThreshold, 4 * 255);
    quint64 hash = 1469598103934665603ULL;
    int active = 0;
    for (int y = 0; y < height; ++y) {
        const uchar* rowWith = withRgba.constScanLine(y);
        const uchar* rowWithout = withoutRgba.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const int p = x * 4;
            const int d0 = qAbs(static_cast<int>(rowWith[p + 0]) - static_cast<int>(rowWithout[p + 0]));
            const int d1 = qAbs(static_cast<int>(rowWith[p + 1]) - static_cast<int>(rowWithout[p + 1]));
            const int d2 = qAbs(static_cast<int>(rowWith[p + 2]) - static_cast<int>(rowWithout[p + 2]));
            const int d3 = qAbs(static_cast<int>(rowWith[p + 3]) - static_cast<int>(rowWithout[p + 3]));
            const int score = d0 + d1 + d2 + d3;
            if (score <= clampedThreshold) {
                continue;
            }

            ++active;
            hash ^= static_cast<quint64>(x);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(y);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(rowWith[p + 0]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(rowWith[p + 1]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(rowWith[p + 2]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(rowWith[p + 3]);
            hash *= 1099511628211ULL;
        }
    }
    if (activePixelCount != nullptr) {
        *activePixelCount = active;
    }
    if (active == 0) {
        return 0;
    }
    hash ^= static_cast<quint64>(active);
    hash *= 1099511628211ULL;
    return hash;
}

QString probeExportedVideoSummary(const QString& ffprobePath, const QString& outputPath)
{
    if (ffprobePath.isEmpty()) {
        return QStringLiteral("ffprobe_missing");
    }
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    const QStringList args{
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("v:0"),
        QStringLiteral("-show_entries"),
        QStringLiteral("stream=codec_name,pix_fmt,width,height,r_frame_rate,avg_frame_rate,time_base,nb_frames,duration"),
        QStringLiteral("-show_entries"),
        QStringLiteral("format=duration,size,bit_rate"),
        QStringLiteral("-of"),
        QStringLiteral("default=noprint_wrappers=1"),
        outputPath
    };
    probe.start(ffprobePath, args, QIODevice::ReadOnly);
    if (!probe.waitForStarted(5000)) {
        return QStringLiteral("ffprobe_start_failed error=%1").arg(probe.errorString());
    }
    constexpr int kProbeSliceMs = 100;
    constexpr int kProbeTimeoutMs = 5000;
    int elapsedMs = 0;
    while (probe.state() != QProcess::NotRunning && elapsedMs < kProbeTimeoutMs) {
        probe.waitForFinished(kProbeSliceMs);
        elapsedMs += kProbeSliceMs;
        QCoreApplication::processEvents();
    }
    if (probe.state() != QProcess::NotRunning) {
        probe.kill();
        probe.waitForFinished(2000);
        return QStringLiteral("ffprobe_timeout_or_failed error=%1").arg(probe.errorString());
    }
    const QString output = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        const QString exitInfo = QStringLiteral("ffprobe_nonzero status=%1 code=%2")
            .arg(static_cast<int>(probe.exitStatus()))
            .arg(probe.exitCode());
        if (output.isEmpty()) {
            return exitInfo;
        }
        return exitInfo + QStringLiteral(" output=") + truncateForLog(output, 2000);
    }
    return truncateForLog(output, 4000);
}

QString ffmpegBaseArgsLog(const QString& ffmpegPath, const QStringList& args)
{
    return QString("%1 %2").arg(ffmpegPath, args.join(' '));
}

QString qProcessStateForLog(QProcess::ProcessState state)
{
    switch (state) {
    case QProcess::NotRunning:
        return QStringLiteral("NotRunning");
    case QProcess::Starting:
        return QStringLiteral("Starting");
    case QProcess::Running:
        return QStringLiteral("Running");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(state));
}

QString qProcessExitStatusForLog(QProcess::ExitStatus status)
{
    switch (status) {
    case QProcess::NormalExit:
        return QStringLiteral("NormalExit");
    case QProcess::CrashExit:
        return QStringLiteral("CrashExit");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(status));
}

QString describeProcessForLog(const QProcess& process)
{
    return QStringLiteral("state=%1 exitStatus=%2 exitCode=%3 processError=%4 pid=%5")
        .arg(qProcessStateForLog(process.state()))
        .arg(qProcessExitStatusForLog(process.exitStatus()))
        .arg(process.exitCode())
        .arg(truncateForLog(process.errorString().trimmed(), 400))
        .arg(QString::number(process.processId()));
}

QString makeRemuxStageOutputPath(const QString& finalOutputPath)
{
    const QFileInfo outputInfo(finalOutputPath);
    const QString baseName = outputInfo.completeBaseName().isEmpty()
        ? QStringLiteral("miacode_export")
        : outputInfo.completeBaseName();
    const QString suffix = outputInfo.completeSuffix().isEmpty()
        ? QStringLiteral("mp4")
        : outputInfo.completeSuffix();
    return QDir(outputInfo.absolutePath()).filePath(
        QStringLiteral("%1.miacode_remux_%2.%3")
            .arg(baseName)
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces))
            .arg(suffix)
    );
}

QString processOutputAndErrorForLog(QProcess& process, int maxChars = 2000)
{
    const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    const QString errorOutput = QString::fromUtf8(process.readAllStandardError()).trimmed();
    QStringList parts;
    if (!output.isEmpty()) {
        parts.append(QStringLiteral("stdout: %1").arg(truncateForLog(output, maxChars)));
    }
    if (!errorOutput.isEmpty()) {
        parts.append(QStringLiteral("stderr: %1").arg(truncateForLog(errorOutput, maxChars)));
    }
    if (parts.isEmpty()) {
        const QString processError = process.errorString().trimmed();
        if (!processError.isEmpty()) {
            parts.append(QStringLiteral("process_error: %1").arg(truncateForLog(processError, maxChars)));
        }
    }
    return parts.join(QStringLiteral("\n"));
}

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
)
{
    QElapsedTimer waitTimer;
    waitTimer.start();
    appendVideoExportLog(beginStage);
    while (process.state() != QProcess::NotRunning) {
        if (setProgressPercent(progressPercent, progressText)) {
            process.kill();
            process.waitForFinished(2000);
            if (result != nullptr) {
                result->message = QStringLiteral("canceled");
                result->details = withExportLogPath(result->details);
            }
            appendVideoExportLog(QStringLiteral("canceled"), cancelDetail);
            return false;
        }
        process.waitForFinished(80);
    }
    appendVideoExportLog(
        doneStage,
        QStringLiteral("exitStatus=%1 exitCode=%2 elapsedMs=%3")
            .arg(static_cast<int>(process.exitStatus()))
            .arg(process.exitCode())
            .arg(waitTimer.elapsed())
    );
    return true;
}

bool replaceOutputFileAtomicallyBestEffort(
    const QString& stagedPath,
    const QString& finalPath,
    QString* errorMessage
)
{
    if (stagedPath.isEmpty() || finalPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("staged or final output path is empty");
        }
        return false;
    }
    if (!QFileInfo::exists(stagedPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("staged remux output does not exist");
        }
        return false;
    }
    const QString backupPath = finalPath + QStringLiteral(".miacode_backup");
    QFile::remove(backupPath);
    const bool hadExistingFinal = QFileInfo::exists(finalPath);
    QFile finalFile(finalPath);
    QFile stagedFile(stagedPath);
    if (hadExistingFinal && !finalFile.rename(backupPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to move existing output aside");
        }
        return false;
    }
    if (stagedFile.rename(finalPath)) {
        QFile::remove(backupPath);
        return true;
    }
    const QString renameError = QStringLiteral("failed to promote staged remux output");
    if (hadExistingFinal) {
        QFile backupFile(backupPath);
        backupFile.rename(finalPath);
    }
    if (errorMessage != nullptr) {
        *errorMessage = renameError;
    }
    return false;
}

QString withExportLogPath(const QString& details)
{
    QStringList lines;
    if (!details.trimmed().isEmpty()) {
        lines.append(details);
    }
    lines.append(QStringLiteral("Export log: %1").arg(videoExportLogPath()));
    lines.append(QStringLiteral("Error log: %1").arg(miacode::debug_log::fatalLogPath()));
    return lines.join(QStringLiteral("\n"));
}

}  // namespace

VideoExportResult VideoExportController::exportFullPreview(
    const VideoExportTask& task,
    QProgressDialog* progress
)
{
    MC_OP("VideoExportController::exportFullPreview");
    _mc_op_.note(QStringLiteral("output=%1").arg(task.outputPath));
    const auto progressCallback = [progress](int percent, const QString& text) {
        if (progress == nullptr) {
            return false;
        }
        if (percent >= 0) {
            progress->setMaximum(100);
            progress->setValue(qBound(0, percent, 100));
        }
        if (!text.isEmpty()) {
            progress->setLabelText(text);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return progress->wasCanceled();
    };
    return exportPreparedTask(task, progressCallback);
}

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

    const int frameCount = audioRenderPlan.frameCount;
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
        if (!(mediaIsImage && mediaUsesPreprocessedImage)) {
            if (task.backgroundScaleMode == PreviewBackgroundScaleMode::FitContain) {
                mediaFilters << QStringLiteral(
                    "scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black")
                                    .arg(frameWidth)
                                    .arg(frameHeight);
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
        filterParts << QStringLiteral("[base_fill][media_src]overlay=0:0:format=rgb:alpha=straight[base_media]");
    } else {
        filterParts << QStringLiteral("[base_fill]null[base_media]");
    }

    if (hasDimMask) {
        filterParts << QStringLiteral("[%1:v]fps=%2,format=rgba[dim_mask]")
                           .arg(dimMaskInputIndex)
                           .arg(task.fps);
        filterParts << QStringLiteral("[base_media][dim_mask]overlay=0:0:format=rgb:alpha=straight[base]");
    } else {
        filterParts << QStringLiteral("[base_media]null[base]");
    }

    // Quick export frames are already read back in top-left raster order.
    filterParts << QStringLiteral("[0:v]format=rgba[overlay_src]");
    filterParts << QStringLiteral("[%1:a]atrim=0:%2,asetpts=PTS-STARTPTS,aresample=%3,aformat=channel_layouts=stereo[aout]")
                       .arg(audioInputIndex)
                       .arg(totalSecondsText)
                       .arg(kMixSampleRate);

    const SystemMemoryInfo memoryInfo = querySystemMemoryInfo();
    appendVideoExportLog(QStringLiteral("memory_snapshot"), memoryInfoToLog(memoryInfo));
    QString encoderProbeLog;
    const VideoEncoderConfig encoderConfig = chooseVideoEncoder(
        ffmpegPath,
        frameWidth,
        frameHeight,
        task.fps,
        task.preset,
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
    // Diagnostic gate: MIACODE_EXPORT_DISABLE_PBO_READBACK=1 forces the
    // synchronous non-PBO readback path (renderOverlayFrameOffscreen,
    // PreviewQuickExportSession.cpp:354-362). The PBO path schedules an
    // async glReadPixels(FBO→PBO) with no fence before the next frame
    // clears the FBO; if a driver mistracks that hazard, the captured
    // bytes are partly from the next frame's clear/draw — manifesting as
    // horizontal-band tearing on individual rendered frames. The non-PBO
    // path uses glReadPixels with a CPU pointer, which the driver must
    // serialize, so it cannot exhibit this race. Costs one full
    // GPU→CPU stall per frame (~20-30% slower export at 1080p60), but
    // is the safe ground truth for diagnosing PBO-race symptoms.
    const bool disablePboReadbackViaEnv =
        qEnvironmentVariableIntValue("MIACODE_EXPORT_DISABLE_PBO_READBACK") == 1;
    const bool requestOffscreenPboReadback =
        useOffscreenGpu
        && exportConfig.renderBackend.requestOffscreenPboReadback
        && !disablePboReadbackViaEnv;
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
        // compile, vertex buffer build) using the playhead the first real
        // frame will see — `qMax(0.0, timelineOriginSecond)` so the warmup
        // is at chart time 0 for full-range exports and at
        // `segmentStartSecond` for partial exports. Warming at a negative
        // chart time produced an empty / no-active-notes scene cache that
        // was reused for the first 120 lead-in frames, leaving them as a
        // transparent overlay (= black after FFmpeg's overlay-over-base
        // composite).
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
        // for the first 2 s. partialRangeExport gates the draw.
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
        // with the readback buffer reuse, (b) the warmup-allocated
        // `reusableReadbackFrame_` buffer doesn't trigger COW detach the
        // same way on the very first PBO-pipeline convert, (c) heap
        // memory aliasing between the outline QImage and the readback
        // QImage on the first frame only.
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
        // black on FFmpeg's solid base). The scene plays back the chart
        // frozen at its segment-start time (chart 0 for full-range,
        // segmentStart for partial-range exports), while the HUD reads
        // the unclamped raw chart time so the timestamp counts up through
        // the lead-in (e.g. -2s → 0s for full-range; segmentStart-1s →
        // segmentStart for partial).
        const double rawChartSecond = timelineOriginSecond + outputSecond;
        const double exportSecond = partialRangeExport
            ? audioRenderPlan.segmentStartSecond + qMax(0.0, outputSecond - audioRenderPlan.leadInSeconds)
            : qMax(0.0, rawChartSecond);
        // HUD override during the pre-roll window:
        //   * Full-range exports want the count-down displayed (chart-time
        //     ramps from -leadIn → 0), so use the unclamped rawChartSecond.
        //     Out of pre-roll it falls back to whatever exportSecond is.
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
            task.showTimestamp,
            task.showObjectStatsHud,
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
