#include "VideoExportController.h"

#include "PreviewCanvas.h"
#include "common/AssetPaths.h"
#include "common/LayoutRingConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewSfxAssets.h"
#include "common/VideoExportConfig.h"

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
#include <QMutex>
#include <QMutexLocker>
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

#include "../../../third_party/miniaudio/miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr int kMixSampleRate = 48000;
constexpr int kMixChannels = 2;
constexpr double kTimelineEpsilonSeconds = 1e-6;

QString performanceProfileToken(VideoExportPerformanceProfile profile)
{
    switch (profile) {
    case VideoExportPerformanceProfile::Speed:
        return QStringLiteral("speed");
    case VideoExportPerformanceProfile::Balanced:
    default:
        return QStringLiteral("balanced");
    }
}

struct ExportEvent {
    double second = 0.0;
    int priority = 0;
    QString kind;
    int spanIndex = -1;
    double gain = 1.0;
};

struct ExportTouchholdSpan {
    double startSecond = 0.0;
    double endSecond = 0.0;
};

struct AggregatedExportPlayback {
    QString kind;
    int count = 0;
    double maxGain = 0.0;
};

struct ScheduledExportPlayback {
    double second = 0.0;
    QString kind;
    double gain = 1.0;
    double nextSameKindSecond = -1.0;
};

struct DecodedClip {
    QVector<float> samples;
    int sampleRate = kMixSampleRate;
    int channels = kMixChannels;

    qint64 frameCount() const
    {
        if (channels <= 0) {
            return 0;
        }
        return samples.size() / channels;
    }

    bool isValid() const
    {
        return !samples.isEmpty() && channels > 0;
    }
};

struct VideoEncoderConfig {
    QString codec;
    QStringList extraArgs;
    bool isHardware = false;
};

struct SystemMemoryInfo {
    bool valid = false;
    quint64 totalPhysicalBytes = 0;
    quint64 availablePhysicalBytes = 0;
};

struct X265TuningPlan {
    int pools = 4;
    int frameThreads = 1;
    int lookahead = 6;
    int bframes = 1;
    int lookaheadSlices = 1;
    int wpp = 1;
    int pmode = 0;
    int pme = 0;

    QString toParams() const
    {
        return QStringLiteral(
                   "pools=%1:frame-threads=%2:rc-lookahead=%3:lookahead-slices=%4:bframes=%5:wpp=%6:pmode=%7:pme=%8")
            .arg(pools)
            .arg(frameThreads)
            .arg(lookahead)
            .arg(lookaheadSlices)
            .arg(bframes)
            .arg(wpp)
            .arg(pmode)
            .arg(pme);
    }
};

struct X264TuningPlan {
    QString preset = QStringLiteral("fast");
    int crf = 21;
    int bframes = 0;

    QStringList toArgs() const
    {
        return QStringList{
            QStringLiteral("-preset"), preset,
            QStringLiteral("-crf"), QString::number(crf),
            QStringLiteral("-bf"), QString::number(bframes)
        };
    }
};

enum class EncoderAutoMode {
    Balanced,
    Compatibility,
    Hardware,
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

bool envFlagEnabled(const QString& key);
int envIntValue(const QString& key, int defaultValue);

bool shouldAggregateExportPlaybackKind(const QString& kind)
{
    return previewSfxShouldAggregateKind(kind);
}

void accumulateExportPlayback(QVector<AggregatedExportPlayback>* playbacks, const QString& kind, double gain)
{
    if (playbacks == nullptr || kind.isEmpty()) {
        return;
    }
    for (AggregatedExportPlayback& playback : *playbacks) {
        if (playback.kind != kind) {
            continue;
        }
        ++playback.count;
        playback.maxGain = qMax(playback.maxGain, qMax(0.0, gain));
        return;
    }
    AggregatedExportPlayback playback;
    playback.kind = kind;
    playback.count = 1;
    playback.maxGain = qMax(0.0, gain);
    playbacks->append(playback);
}

double exportPlaybackGain(const AggregatedExportPlayback& playback)
{
    return previewSfxPlaybackGainForAggregate(playback.kind, playback.count, playback.maxGain);
}

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
        QStringList paths;
        {
            QMutexLocker locker(&mutex_);
            paths = activeDirs_.values();
            activeDirs_.clear();
        }
        for (const QString& path : paths) {
            QDir(path).removeRecursively();
        }
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

X265TuningPlan chooseX265TuningPlan(
    const SystemMemoryInfo& memoryInfo,
    int outputWidth,
    int outputHeight,
    int idealThreadCount
)
{
    const qint64 totalMiB = bytesToMiB(memoryInfo.totalPhysicalBytes);
    const qint64 availMiB = bytesToMiB(memoryInfo.availablePhysicalBytes);

    X265TuningPlan plan;
    Q_UNUSED(outputWidth);
    Q_UNUSED(outputHeight);
    plan.bframes = 0;
    if (memoryInfo.valid) {
        if (availMiB >= 24576 && totalMiB >= 32768) {
            plan.pools = 8;
            plan.lookahead = 8;
        } else if (availMiB >= 16384) {
            plan.pools = 6;
            plan.lookahead = 8;
        } else if (availMiB >= 8192) {
            plan.pools = 4;
            plan.lookahead = 6;
        } else if (availMiB >= 4096) {
            plan.pools = 3;
            plan.lookahead = 5;
        } else {
            plan.pools = 2;
            plan.lookahead = 4;
        }
    }

    plan.pools = qBound(
        1,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X265_POOLS"), qMin(plan.pools, qMax(1, idealThreadCount))),
        8
    );
    plan.frameThreads = qBound(
        1,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X265_FRAME_THREADS"), plan.frameThreads),
        4
    );
    plan.lookahead = qBound(
        4,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X265_LOOKAHEAD"), plan.lookahead),
        20
    );
    plan.bframes = qBound(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X265_BFRAMES"), plan.bframes),
        8
    );
    plan.lookaheadSlices = qBound(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X265_LOOKAHEAD_SLICES"), plan.lookaheadSlices),
        16
    );
    plan.wpp = qBound(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X265_WPP"), plan.wpp),
        1
    );
    plan.pmode = qBound(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X265_PMODE"), plan.pmode),
        1
    );
    plan.pme = qBound(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X265_PME"), plan.pme),
        1
    );
    return plan;
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
    return EncoderAutoMode::Hardware;
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

X264TuningPlan chooseX264TuningPlan(
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

    const QString presetOverride =
        qEnvironmentVariable("MIACODE_EXPORT_X264_PRESET").trimmed();
    if (!presetOverride.isEmpty()) {
        plan.preset = presetOverride;
    }
    plan.crf = qBound(
        16,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X264_CRF"), plan.crf),
        28
    );
    plan.bframes = qBound(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_X264_BFRAMES"), plan.bframes),
        2
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

ReadyFramePayload buildReadyFramePayload(
    PreviewCanvas* exportCanvas,
    int frameIndex,
    double exportSecond,
    QVector<ObjectTraceItem>&& traceItems,
    QImage&& frame,
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
    if (exportCanvas != nullptr) {
        readyFrame.offscreenDrawNs = exportCanvas->offscreenDrawNsLastFrameForDebug();
        readyFrame.offscreenReadbackNs = exportCanvas->offscreenReadbackNsLastFrameForDebug();
        readyFrame.fallbackCount = exportCanvas->cpuFallbackCountLastFrameForDebug();
        readyFrame.usedGpuRenderer = exportCanvas->usedGpuRendererLastFrameForDebug();
    }
    return readyFrame;
}

ExportFrameRenderStatus renderExportFrameWithConfiguredBackend(
    PreviewCanvas* exportCanvas,
    bool* useOffscreenGpu,
    bool* useOffscreenPboReadback,
    PendingPboFrame* pendingPboFrame,
    const QSize& frameSize,
    int frameIndex,
    double exportSecond,
    bool showTimestamp,
    bool showObjectStatsHud,
    QVector<ObjectTraceItem>&& traceItems,
    ReadyFramePayload* readyFrame,
    QString* fallbackDetail
)
{
    if (exportCanvas == nullptr
        || useOffscreenGpu == nullptr
        || useOffscreenPboReadback == nullptr
        || pendingPboFrame == nullptr
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
        const bool pboStepOk = exportCanvas->renderOverlayFrameOffscreenPboStep(
            frameSize,
            exportSecond,
            showTimestamp,
            showObjectStatsHud,
            &completedFrame,
            &completedFrameReady,
            false,
            &pboStepError
        );
        const qint64 renderNs = frameTimer.nsecsElapsed();
        if (pboStepOk) {
            usedOffscreenPath = true;
            const bool producedReadyFrame = completedFrameReady && pendingPboFrame->valid;
            if (producedReadyFrame) {
                *readyFrame = buildReadyFramePayload(
                    exportCanvas,
                    pendingPboFrame->frameIndex,
                    pendingPboFrame->exportSecond,
                    std::move(pendingPboFrame->traceItems),
                    std::move(completedFrame),
                    renderNs,
                    true
                );
            }
            pendingPboFrame->valid = true;
            pendingPboFrame->frameIndex = frameIndex;
            pendingPboFrame->exportSecond = exportSecond;
            pendingPboFrame->traceItems = std::move(traceItems);
            return producedReadyFrame ? ExportFrameRenderStatus::Ready : ExportFrameRenderStatus::Deferred;
        }

        appendRenderBackendFallbackDetail(
            fallbackDetail,
            QStringLiteral("frame=%1 reason=offscreen_pbo_failed error=%2")
                .arg(frameIndex)
                .arg(pboStepError)
        );
        exportCanvas->resetOffscreenPboReadback();
        *useOffscreenPboReadback = false;
    }

    if (*useOffscreenGpu) {
        frame = exportCanvas->renderOverlayFrameOffscreen(frameSize, exportSecond, showTimestamp, showObjectStatsHud);
        if (!frame.isNull()) {
            usedOffscreenPath = true;
        } else {
            appendRenderBackendFallbackDetail(
                fallbackDetail,
                QStringLiteral("frame=%1 reason=offscreen_render_failed").arg(frameIndex)
            );
            exportCanvas->shutdownOffscreenRenderer();
            *useOffscreenGpu = false;
        }
    }

    if (frame.isNull()) {
        frame = exportCanvas->renderOverlayFrame(frameSize, exportSecond, showTimestamp, showObjectStatsHud);
    }

    *readyFrame = buildReadyFramePayload(
        exportCanvas,
        frameIndex,
        exportSecond,
        std::move(traceItems),
        std::move(frame),
        frameTimer.nsecsElapsed(),
        usedOffscreenPath
    );
    return ExportFrameRenderStatus::Ready;
}

bool drainPendingExportFrame(
    PreviewCanvas* exportCanvas,
    PendingPboFrame* pendingPboFrame,
    const QSize& frameSize,
    bool showTimestamp,
    bool showObjectStatsHud,
    ReadyFramePayload* readyFrame,
    QString* errorMessage
)
{
    if (exportCanvas == nullptr || pendingPboFrame == nullptr || readyFrame == nullptr || !pendingPboFrame->valid) {
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
    const bool drainOk = exportCanvas->renderOverlayFrameOffscreenPboStep(
        frameSize,
        pendingPboFrame->exportSecond,
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

    *readyFrame = buildReadyFramePayload(
        exportCanvas,
        pendingPboFrame->frameIndex,
        pendingPboFrame->exportSecond,
        std::move(pendingPboFrame->traceItems),
        std::move(drainedFrame),
        frameTimer.nsecsElapsed(),
        true
    );
    pendingPboFrame->valid = false;
    pendingPboFrame->frameIndex = -1;
    pendingPboFrame->exportSecond = 0.0;
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
constexpr double kOutlineTargetToPlayfieldRatio =
    (kDiagLogicalCanvasSize - miacode::layout_ring::kOutlineInsetLogical * 2.0) / kDiagLogicalCanvasSize;

bool envFlagEnabled(const QString& key)
{
    const QByteArray keyBytes = key.toUtf8();
    const QString raw = qEnvironmentVariable(keyBytes.constData()).trimmed();
    return raw == QLatin1String("1")
        || raw.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
        || raw.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0
        || raw.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0;
}

int envIntValue(const QString& key, int defaultValue)
{
    const QByteArray keyBytes = key.toUtf8();
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(keyBytes.constData(), &ok);
    return ok ? value : defaultValue;
}

double envDoubleValue(const QString& key, double defaultValue)
{
    const QByteArray keyBytes = key.toUtf8();
    bool ok = false;
    const double value = qEnvironmentVariable(keyBytes.constData()).toDouble(&ok);
    return ok ? value : defaultValue;
}

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

QString videoExportDebugLogPath()
{
    const QString envPath = qEnvironmentVariable("MIACODE_EXPORT_LOG_PATH").trimmed();
    if (!envPath.isEmpty()) {
        return normalizePath(envPath);
    }
    return QDir::temp().filePath(QStringLiteral("miacode_video_export.log"));
}

void appendVideoExportLog(const QString& stage, const QString& detail = QString())
{
    QFile logFile(videoExportDebugLogPath());
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream out(&logFile);
    out << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        << " [video_export] " << stage;
    if (!detail.isEmpty()) {
        out << " | " << detail;
    }
    out << "\n";
}

QString truncateForLog(const QString& text, int maxChars = 4000)
{
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(maxChars) + QStringLiteral(" ...<truncated>");
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

double detectLayoutRingDiameterRatio(const QImage& source)
{
    if (source.isNull() || source.width() <= 2 || source.height() <= 2) {
        return miacode::layout_ring::kFallbackTextureDiameterRatio;
    }
    QImage image = source;
    if (image.format() != QImage::Format_RGBA8888) {
        image = source.convertToFormat(QImage::Format_RGBA8888);
    }
    if (image.isNull()) {
        return miacode::layout_ring::kFallbackTextureDiameterRatio;
    }

    const int width = image.width();
    const int height = image.height();
    const int maxRadius = qMax(1, qMin(width, height) / 2);
    QVector<double> histogram(maxRadius + 1, 0.0);
    const double cx = (static_cast<double>(width) - 1.0) * 0.5;
    const double cy = (static_cast<double>(height) - 1.0) * 0.5;

    for (int y = 0; y < height; ++y) {
        const uchar* row = image.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const int offset = x * 4;
            const int r = row[offset + 0];
            const int g = row[offset + 1];
            const int b = row[offset + 2];
            const int a = row[offset + 3];
            if (a < miacode::layout_ring::kDetectMinAlpha) {
                continue;
            }
            const int luminance = (r * 3 + g * 4 + b) / 8;
            if (luminance < miacode::layout_ring::kDetectMinLuminance) {
                continue;
            }
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            const int radius = qBound(0, qRound(std::sqrt(dx * dx + dy * dy)), maxRadius);
            histogram[radius] += static_cast<double>(a) * static_cast<double>(luminance);
        }
    }

    QVector<double> smooth(histogram.size(), 0.0);
    for (int i = 0; i < histogram.size(); ++i) {
        double sum = histogram[i] * 2.0;
        double weight = 2.0;
        if (i > 0) {
            sum += histogram[i - 1];
            weight += 1.0;
        }
        if (i + 1 < histogram.size()) {
            sum += histogram[i + 1];
            weight += 1.0;
        }
        smooth[i] = sum / qMax(1.0, weight);
    }

    const int searchStart = qBound(
        0,
        qRound(maxRadius * miacode::layout_ring::kDetectSearchStartRadiusRatio),
        maxRadius
    );
    const int searchEnd = qBound(
        searchStart,
        qRound(maxRadius * miacode::layout_ring::kDetectSearchEndRadiusRatio),
        maxRadius
    );
    int peakIndex = -1;
    double peakValue = 0.0;
    for (int i = searchStart; i <= searchEnd; ++i) {
        if (smooth[i] > peakValue) {
            peakValue = smooth[i];
            peakIndex = i;
        }
    }
    if (peakIndex < 0 || peakValue <= 1.0) {
        return miacode::layout_ring::kFallbackTextureDiameterRatio;
    }

    const double edgeThreshold = peakValue * miacode::layout_ring::kDetectEdgeThresholdRatio;
    int innerRadius = peakIndex;
    while (innerRadius > searchStart && smooth[innerRadius - 1] >= edgeThreshold) {
        --innerRadius;
    }
    int outerRadius = peakIndex;
    while (outerRadius < searchEnd && smooth[outerRadius + 1] >= edgeThreshold) {
        ++outerRadius;
    }

    const double averageRadius = (static_cast<double>(innerRadius) + static_cast<double>(outerRadius)) * 0.5;
    const double diameterRatio = (averageRadius * 2.0) / static_cast<double>(qMax(1, qMin(width, height)));
    return qBound(
        miacode::layout_ring::kDetectDiameterRatioMin,
        diameterRatio,
        miacode::layout_ring::kDetectDiameterRatioMax
    );
}

QString primaryOutlinePath()
{
    return miacode::assets::assetPath("background/outline.png");
}

QString legacyOutlinePath()
{
    return miacode::assets::assetPath("background/outline_2.png");
}

QString outlinePathForStageMedia(bool hasMedia)
{
    const QString preferredPath = hasMedia ? primaryOutlinePath() : legacyOutlinePath();
    if (QFileInfo::exists(preferredPath)) {
        return preferredPath;
    }

    const QString fallbackPath = hasMedia ? legacyOutlinePath() : primaryOutlinePath();
    if (QFileInfo::exists(fallbackPath)) {
        return fallbackPath;
    }

    return QString();
}

double resolvedLayoutRingDiameterRatio(bool hasMedia)
{
    const QString outlinePath = outlinePathForStageMedia(hasMedia);
    const QImage outline(outlinePath);
    const double textureRatio = detectLayoutRingDiameterRatio(outline);
    return qBound(
        miacode::layout_ring::kPlayfieldRatioMin,
        textureRatio * kOutlineTargetToPlayfieldRatio,
        miacode::layout_ring::kPlayfieldRatioMax
    );
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
    if (candidate.codec.startsWith(QStringLiteral("h264"))) {
        args << QStringLiteral("-bf") << QStringLiteral("0");
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
    const SystemMemoryInfo& memoryInfo,
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
        config.extraArgs = {QStringLiteral("-q:v"), QStringLiteral("4")};
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
    const bool hasLibx265 = hasEncoderToken(output, QStringLiteral("libx265"));
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
    const X265TuningPlan x265Plan = chooseX265TuningPlan(memoryInfo, safeWidth, safeHeight, idealThreadCount);
    const X264TuningPlan x264Plan = chooseX264TuningPlan(memoryInfo, safeWidth, safeHeight, safeFps, idealThreadCount);
    // Keep export artifacts low while avoiding oversized files.
    const qint64 estimatedBitrateKbps = qBound<qint64>(
        2200LL,
        qRound64(static_cast<double>(safeWidth) * safeHeight * safeFps * 0.075 / 1000.0),
        8500LL
    );
    const qint64 maxRateKbps = qBound<qint64>(
        estimatedBitrateKbps,
        qRound64(static_cast<double>(estimatedBitrateKbps) * 1.40),
        10500LL
    );
    const qint64 bufSizeKbps = qBound<qint64>(
        maxRateKbps,
        qRound64(static_cast<double>(maxRateKbps) * 2.0),
        16000LL
    );
    const auto variableBitrateArgs = [estimatedBitrateKbps, maxRateKbps, bufSizeKbps]() {
        return QStringList{
            QStringLiteral("-b:v"), QString::number(estimatedBitrateKbps) + QLatin1String("k"),
            QStringLiteral("-maxrate"), QString::number(maxRateKbps) + QLatin1String("k"),
            QStringLiteral("-bufsize"), QString::number(bufSizeKbps) + QLatin1String("k")
        };
    };
    const QString x265Params = x265Plan.toParams();
    const QStringList x264Args = x264Plan.toArgs();
    const QString forcedEncoder = qEnvironmentVariable("MIACODE_EXPORT_FORCE_ENCODER").trimmed();
    const EncoderAutoMode encoderAutoMode = resolveEncoderAutoMode();
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
    const auto pushCandidate = [&candidates](const QString& codec, const QStringList& extraArgs, bool isHardware) {
        VideoEncoderConfig item;
        item.codec = codec;
        item.extraArgs = extraArgs;
        item.isHardware = isHardware;
        candidates.push_back(item);
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
            pushCandidate(QStringLiteral("h264_nvenc"), variableBitrateArgs(), true);
        }
        if (hasH264Qsv) {
            pushCandidate(QStringLiteral("h264_qsv"), variableBitrateArgs(), true);
        }
        if (hasH264Amf) {
            pushCandidate(QStringLiteral("h264_amf"), variableBitrateArgs(), true);
        }
        if (hasH264Mf) {
            pushCandidate(QStringLiteral("h264_mf"), variableBitrateArgs(), true);
        }
        if (encoderAutoMode == EncoderAutoMode::Hardware) {
            if (hasHevcNvenc) {
                pushCandidate(QStringLiteral("hevc_nvenc"), variableBitrateArgs(), true);
            }
            if (hasHevcQsv) {
                pushCandidate(QStringLiteral("hevc_qsv"), variableBitrateArgs(), true);
            }
            if (hasHevcAmf) {
                pushCandidate(QStringLiteral("hevc_amf"), variableBitrateArgs(), true);
            }
            if (hasHevcMf) {
                pushCandidate(QStringLiteral("hevc_mf"), variableBitrateArgs(), true);
            }
        }
    };
    const auto appendAutoSoftwareCandidates = [&]() {
        if (hasLibx264) {
            pushCandidate(QStringLiteral("libx264"), x264Args, false);
        }
        if (hasOpenH264) {
            pushCandidate(
                QStringLiteral("libopenh264"),
                QStringList{
                    QStringLiteral("-b:v"), QString::number(estimatedBitrateKbps) + QLatin1String("k")
                },
                false
            );
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
            pushCandidate(QStringLiteral("hevc_nvenc"), variableBitrateArgs(), true);
        }
        if (forcedMatches(QStringLiteral("hevc_qsv")) && hasHevcQsv) {
            pushCandidate(QStringLiteral("hevc_qsv"), variableBitrateArgs(), true);
        }
        if (forcedMatches(QStringLiteral("hevc_amf")) && hasHevcAmf) {
            pushCandidate(QStringLiteral("hevc_amf"), variableBitrateArgs(), true);
        }
        if (forcedMatches(QStringLiteral("hevc_mf")) && hasHevcMf) {
            pushCandidate(QStringLiteral("hevc_mf"), variableBitrateArgs(), true);
        }
        if (forcedMatches(QStringLiteral("libx265")) && hasLibx265) {
            pushCandidate(
                QStringLiteral("libx265"),
                QStringList{
                    QStringLiteral("-preset"), QStringLiteral("medium"),
                    QStringLiteral("-crf"), QStringLiteral("26"),
                    QStringLiteral("-x265-params"), x265Params
                },
                false
            );
        }
        if (forcedMatches(QStringLiteral("h264_nvenc")) && hasH264Nvenc) {
            pushCandidate(QStringLiteral("h264_nvenc"), variableBitrateArgs(), true);
        }
        if (forcedMatches(QStringLiteral("h264_qsv")) && hasH264Qsv) {
            pushCandidate(QStringLiteral("h264_qsv"), variableBitrateArgs(), true);
        }
        if (forcedMatches(QStringLiteral("h264_amf")) && hasH264Amf) {
            pushCandidate(QStringLiteral("h264_amf"), variableBitrateArgs(), true);
        }
        if (forcedMatches(QStringLiteral("h264_mf")) && hasH264Mf) {
            pushCandidate(QStringLiteral("h264_mf"), variableBitrateArgs(), true);
        }
        if (forcedMatches(QStringLiteral("libx264")) && hasLibx264) {
            pushCandidate(QStringLiteral("libx264"), x264Args, false);
        }
        if (forcedMatches(QStringLiteral("libopenh264")) && hasOpenH264) {
            pushCandidate(
                QStringLiteral("libopenh264"),
                QStringList{
                    QStringLiteral("-b:v"), QString::number(estimatedBitrateKbps) + QLatin1String("k")
                },
                false
            );
        }
        if (forcedMatches(QStringLiteral("mpeg4")) && hasMpeg4) {
            pushCandidate(
                QStringLiteral("mpeg4"),
                QStringList{
                    QStringLiteral("-q:v"), QStringLiteral("4")
                },
                false
            );
        }
    }
    if (hasMpeg4 || candidates.isEmpty()) {
        pushCandidate(
            QStringLiteral("mpeg4"),
            QStringList{
                QStringLiteral("-q:v"), QStringLiteral("4")
            },
            false
        );
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
                    "encoder_probe forced=%1 selected=%2 hw=%3 size=%4x%5")
                    .arg(forcedEncoder)
                    .arg(config.codec)
                    .arg(config.isHardware ? 1 : 0)
                    .arg(safeWidth)
                    .arg(safeHeight);
                if (config.codec == QLatin1String("libx265")) {
                    detail += QStringLiteral(" x265Params=%1").arg(x265Params);
                } else if (config.codec == QLatin1String("libx264")) {
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
        config.extraArgs = {QStringLiteral("-q:v"), QStringLiteral("4")};
        config.isHardware = false;
        return config;
    }

    const bool skipRuntimeProbe = envFlagEnabled(QStringLiteral("MIACODE_EXPORT_SKIP_ENCODER_RUNTIME_PROBE"));
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
        config.extraArgs = {QStringLiteral("-q:v"), QStringLiteral("4")};
        config.isHardware = false;
        runtimeProbeLines.append(QStringLiteral("fallback=hardcoded:mpeg4"));
    }

    if (probeLog != nullptr) {
        QString detail = QStringLiteral(
            "encoder_probe hevc_nvenc=%1 hevc_qsv=%2 hevc_amf=%3 hevc_mf=%4 libx265=%5 "
            "h264_nvenc=%6 h264_qsv=%7 h264_amf=%8 h264_mf=%9 libx264=%10 libopenh264=%11 mpeg4=%12 "
            "selected=%13 hw=%14 bitrateK=%15 maxrateK=%16 size=%17x%18 autoMode=%19 hwFirst=%20 modeReason=%21")
            .arg(hasHevcNvenc ? 1 : 0)
            .arg(hasHevcQsv ? 1 : 0)
            .arg(hasHevcAmf ? 1 : 0)
            .arg(hasHevcMf ? 1 : 0)
            .arg(hasLibx265 ? 1 : 0)
            .arg(hasH264Nvenc ? 1 : 0)
            .arg(hasH264Qsv ? 1 : 0)
            .arg(hasH264Amf ? 1 : 0)
            .arg(hasH264Mf ? 1 : 0)
            .arg(hasLibx264 ? 1 : 0)
            .arg(hasOpenH264 ? 1 : 0)
            .arg(hasMpeg4 ? 1 : 0)
            .arg(config.codec)
            .arg(config.isHardware ? 1 : 0)
            .arg(estimatedBitrateKbps)
            .arg(maxRateKbps)
            .arg(safeWidth)
            .arg(safeHeight)
            .arg(encoderAutoModeToken(encoderAutoMode))
            .arg(preferHardwareFirst ? 1 : 0)
            .arg(encoderAutoModeReason);
        if (!runtimeProbeLines.isEmpty()) {
            detail += QStringLiteral(" runtime=%1")
                .arg(truncateForLog(runtimeProbeLines.join(QLatin1Char(';')), 2400));
        }
        if (config.codec == QLatin1String("libx265")) {
            detail += QStringLiteral(" x265Params=%1").arg(x265Params);
            if (memoryInfo.valid) {
                detail += QStringLiteral(" availMiB=%1 totalMiB=%2")
                    .arg(bytesToMiB(memoryInfo.availablePhysicalBytes))
                    .arg(bytesToMiB(memoryInfo.totalPhysicalBytes));
            }
        } else if (config.codec == QLatin1String("libx264")) {
            detail += QStringLiteral(" x264Preset=%1 x264Crf=%2 x264Bframes=%3")
                .arg(x264Plan.preset)
                .arg(x264Plan.crf)
                .arg(x264Plan.bframes);
        }
        *probeLog = detail;
    }
    return config;
}

QString resolveBackgroundMediaPath(const QString& chartPath)
{
    if (chartPath.isEmpty()) {
        return QString();
    }
    const QDir chartDir(QFileInfo(chartPath).absolutePath());
    const QStringList candidates{
        QStringLiteral("bg.mp4"),
        QStringLiteral("pv.mp4"),
        QStringLiteral("bg.jpg"),
        QStringLiteral("bg.png"),
        QStringLiteral("bg.jpeg"),
    };
    for (const QString& name : candidates) {
        const QString path = chartDir.filePath(name);
        if (QFileInfo::exists(path)) {
            return normalizePath(path);
        }
    }
    return QString();
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

QString resolveSfxDirectory()
{
    return normalizePath(miacode::preview_sfx::resolveSfxDirectory());
}

bool decodeAudioClip(const QString& path, DecodedClip* clip)
{
    if (clip == nullptr) {
        return false;
    }
    clip->samples.clear();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return false;
    }

    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kMixChannels, kMixSampleRate);
    const QByteArray encodedPath = QFile::encodeName(path);
    if (ma_decoder_init_file(encodedPath.constData(), &config, &decoder) != MA_SUCCESS) {
        return false;
    }

    constexpr ma_uint64 kChunkFrames = 4096;
    QVector<float> chunk;
    chunk.resize(static_cast<int>(kChunkFrames * kMixChannels));
    for (;;) {
        ma_uint64 framesRead = 0;
        const ma_result rc = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &framesRead);
        if (framesRead > 0) {
            const int sampleCount = static_cast<int>(framesRead * kMixChannels);
            const int previousSize = clip->samples.size();
            clip->samples.resize(previousSize + sampleCount);
            std::copy_n(chunk.constData(), sampleCount, clip->samples.begin() + previousSize);
        }
        if (rc != MA_SUCCESS || framesRead == 0) {
            break;
        }
    }

    ma_decoder_uninit(&decoder);
    clip->sampleRate = kMixSampleRate;
    clip->channels = kMixChannels;
    return !clip->samples.isEmpty();
}

void addClipToMix(
    const DecodedClip& clip,
    double gain,
    qint64 startFrame,
    qint64 maxFrames,
    qint64 clipStartFrame,
    QVector<float>* mix
)
{
    if (mix == nullptr || !clip.isValid() || gain <= 0.0 || startFrame < 0 || clipStartFrame < 0) {
        return;
    }
    const qint64 totalMixFrames = mix->size() / kMixChannels;
    if (startFrame >= totalMixFrames) {
        return;
    }
    if (clipStartFrame >= clip.frameCount()) {
        return;
    }
    qint64 framesToMix = qMin(clip.frameCount() - clipStartFrame, totalMixFrames - startFrame);
    if (maxFrames >= 0) {
        framesToMix = qMin(framesToMix, maxFrames);
    }
    if (framesToMix <= 0) {
        return;
    }

    const float gainF = static_cast<float>(gain);
    for (qint64 frame = 0; frame < framesToMix; ++frame) {
        const qint64 mixIndex = (startFrame + frame) * kMixChannels;
        const qint64 clipIndex = (clipStartFrame + frame) * clip.channels;
        float left = clip.samples[clipIndex];
        float right = clip.channels >= 2 ? clip.samples[clipIndex + 1] : left;
        (*mix)[mixIndex] += left * gainF;
        (*mix)[mixIndex + 1] += right * gainF;
    }
}

bool writeWav16(const QString& path, const QVector<float>& samples, int sampleRate, int channels)
{
    if (path.isEmpty() || sampleRate <= 0 || channels <= 0) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    QByteArray pcmBytes;
    pcmBytes.resize(samples.size() * static_cast<int>(sizeof(qint16)));
    qint16* out = reinterpret_cast<qint16*>(pcmBytes.data());
    for (int i = 0; i < samples.size(); ++i) {
        const float clamped = qBound(-1.0f, samples.at(i), 1.0f);
        out[i] = static_cast<qint16>(qRound(clamped * 32767.0f));
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    const quint32 dataBytes = static_cast<quint32>(pcmBytes.size());
    const quint32 riffChunkSize = 36u + dataBytes;
    const quint16 bitsPerSample = 16;
    const quint16 blockAlign = static_cast<quint16>(channels * (bitsPerSample / 8));
    const quint32 byteRate = static_cast<quint32>(sampleRate * blockAlign);

    stream.writeRawData("RIFF", 4);
    stream << riffChunkSize;
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16);        // PCM fmt chunk size
    stream << quint16(1);         // Audio format PCM
    stream << quint16(channels);
    stream << quint32(sampleRate);
    stream << byteRate;
    stream << blockAlign;
    stream << bitsPerSample;
    stream.writeRawData("data", 4);
    stream << dataBytes;
    if (file.write(pcmBytes) != pcmBytes.size()) {
        return false;
    }
    return true;
}

bool noteMarkerIntersectsRange(const TimelineNoteMarker& marker, double startSecond, double endSecond)
{
    if (endSecond <= startSecond) {
        return true;
    }
    const double markerStart = marker.second;
    const double markerEnd = qMax(marker.second, marker.endSecond);
    return markerEnd >= startSecond - kTimelineEpsilonSeconds
        && markerStart <= endSecond + kTimelineEpsilonSeconds;
}

QVector<TimelineNoteMarker> filteredMarkersForRange(
    const QVector<TimelineNoteMarker>& markers,
    double startSecond,
    double endSecond
)
{
    QVector<TimelineNoteMarker> filtered;
    filtered.reserve(markers.size());
    for (const TimelineNoteMarker& marker : markers) {
        if (noteMarkerIntersectsRange(marker, startSecond, endSecond)) {
            filtered.append(marker);
        }
    }
    return filtered;
}

void buildSfxTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    QVector<ExportEvent>* events,
    QVector<ExportTouchholdSpan>* touchholdSpans
)
{
    if (events == nullptr || touchholdSpans == nullptr) {
        return;
    }
    events->clear();
    touchholdSpans->clear();
    events->reserve(noteMarkers.size() * 5);
    touchholdSpans->reserve(noteMarkers.size());

    const auto addEvent = [events](double second, const QString& kind, int priority = 1, int spanIndex = -1, double gain = 1.0) {
        if (second < 0.0 || kind.isEmpty()) {
            return;
        }
        ExportEvent event;
        event.second = second;
        event.priority = priority;
        event.kind = kind;
        event.spanIndex = spanIndex;
        event.gain = qMax(0.0, gain);
        events->append(event);
    };

    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString type = marker.type.toLower();
        if (type == QLatin1String("tap")) {
            addEvent(marker.second, QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("judge"));
            if (marker.isBreak) {
                addEvent(marker.second, QStringLiteral("break"));
            }
            if (marker.isEx) {
                addEvent(marker.second, QStringLiteral("ex"));
            }
            continue;
        }
        if (type == QLatin1String("hold")) {
            addEvent(marker.second, QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("judge"));
            if (marker.isBreak) {
                addEvent(marker.second, QStringLiteral("break"));
            }
            if (marker.endSecond > marker.second) {
                addEvent(marker.endSecond, QStringLiteral("answer"));
            }
            if (marker.isEx) {
                addEvent(marker.second, QStringLiteral("ex"));
            }
            continue;
        }
        if (type == QLatin1String("touch")) {
            addEvent(marker.second, QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("touch"));
            if (marker.isFirework) {
                addEvent(marker.second + 0.05, QStringLiteral("firework"));
            }
            continue;
        }
        if (type == QLatin1String("touch_hold")) {
            addEvent(marker.second, QStringLiteral("answer"));
            addEvent(marker.second, marker.isBreak ? QStringLiteral("judge_break") : QStringLiteral("touch"));
            if (marker.isFirework && marker.endSecond >= 0.0) {
                addEvent(marker.endSecond, QStringLiteral("firework"));
            }
            if (marker.endSecond > marker.second) {
                ExportTouchholdSpan span;
                span.startSecond = marker.second;
                span.endSecond = marker.endSecond;
                const int spanIndex = touchholdSpans->size();
                touchholdSpans->append(span);
                addEvent(span.startSecond, QStringLiteral("touchhold_start"), 0, spanIndex);
                addEvent(span.endSecond, QStringLiteral("touchhold_stop"), 2, spanIndex);
            }
            continue;
        }
        if (type == QLatin1String("slide") || type == QLatin1String("wifi")) {
            if (marker.hasHeadStar) {
                addEvent(marker.second, QStringLiteral("answer"));
                addEvent(marker.second, marker.headBreak ? QStringLiteral("judge_break") : QStringLiteral("judge"));
                if (marker.headBreak) {
                    if (!marker.trackBreak) {
                        addEvent(marker.second, QStringLiteral("break"));
                    }
                }
                if (marker.headEx) {
                    addEvent(marker.second, QStringLiteral("ex"));
                }
            }
            const double traceSecond = marker.slideTraceSecond >= 0.0 ? marker.slideTraceSecond : marker.second;
            addEvent(traceSecond, marker.trackBreak ? QStringLiteral("break_slide_start") : QStringLiteral("slide"));
            if (marker.trackBreak && marker.endSecond > traceSecond) {
                addEvent(marker.endSecond, QStringLiteral("break_slide_finish"), 1, -1, 0.5);
                addEvent(marker.endSecond, QStringLiteral("judge_break_slide"), 1, -1, 0.5);
            }
            continue;
        }
    }

    std::sort(events->begin(), events->end(), [](const ExportEvent& a, const ExportEvent& b) {
        if (qAbs(a.second - b.second) > kTimelineEpsilonSeconds) {
            return a.second < b.second;
        }
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.kind < b.kind;
    });
}

bool mixSfxTrackToWav(
    const QString& outputPath,
    const QVector<TimelineNoteMarker>& noteMarkers,
    const PreviewAudioSettings& settings,
    double totalSeconds,
    double timelineOriginSecond
)
{
    const qint64 totalFrames = qMax<qint64>(1, qCeil(totalSeconds * kMixSampleRate));
    QVector<float> mix;
    mix.fill(0.0f, static_cast<int>(totalFrames * kMixChannels));

    const QString sfxDir = resolveSfxDirectory();
    if (sfxDir.isEmpty()) {
        return false;
    }

    QHash<QString, DecodedClip> clips;
    const auto loadClip = [&sfxDir, &clips](const QString& key) {
        DecodedClip clip;
        const QString path = miacode::preview_sfx::assetFilePathForKind(sfxDir, key);
        if (decodeAudioClip(path, &clip)) {
            clips.insert(key, clip);
        }
    };
    loadClip(QStringLiteral("answer"));
    loadClip(QStringLiteral("judge"));
    loadClip(QStringLiteral("judge_break"));
    loadClip(QStringLiteral("slide"));
    loadClip(QStringLiteral("break"));
    loadClip(QStringLiteral("break_slide_start"));
    loadClip(QStringLiteral("break_slide"));
    loadClip(QStringLiteral("judge_break_slide"));
    loadClip(QStringLiteral("ex"));
    loadClip(QStringLiteral("touch"));
    loadClip(QStringLiteral("touchhold"));
    loadClip(QStringLiteral("firework"));

    QVector<ExportEvent> events;
    QVector<ExportTouchholdSpan> spans;
    buildSfxTimeline(noteMarkers, &events, &spans);

    const auto kindVolume = [&settings](const QString& kind) -> double {
        return previewSfxVolumeForKind(settings, kind);
    };

    QVector<ScheduledExportPlayback> scheduledPlaybacks;
    int index = 0;
    while (index < events.size()) {
        const int groupStart = index;
        const double groupSecond = events[groupStart].second;
        int groupEnd = groupStart + 1;
        while (groupEnd < events.size()
               && qAbs(events[groupEnd].second - groupSecond) <= kTimelineEpsilonSeconds) {
            ++groupEnd;
        }

        QVector<AggregatedExportPlayback> playbacks;
        for (int i = groupStart; i < groupEnd; ++i) {
            const ExportEvent& event = events.at(i);
            if (event.kind == QLatin1String("touchhold_start")
                || event.kind == QLatin1String("touchhold_stop")) {
                continue;
            }
            if (shouldAggregateExportPlaybackKind(event.kind)) {
                accumulateExportPlayback(&playbacks, event.kind, event.gain);
                continue;
            }
            ScheduledExportPlayback playback;
            playback.second = event.second;
            playback.kind = event.kind;
            playback.gain = event.gain;
            scheduledPlaybacks.append(playback);
        }
        for (const AggregatedExportPlayback& playback : playbacks) {
            ScheduledExportPlayback scheduled;
            scheduled.second = groupSecond;
            scheduled.kind = playback.kind;
            scheduled.gain = exportPlaybackGain(playback);
            scheduledPlaybacks.append(scheduled);
        }
        index = groupEnd;
    }

    QHash<QString, double> nextPlaybackSecondByKind;
    for (int i = scheduledPlaybacks.size() - 1; i >= 0; --i) {
        ScheduledExportPlayback& playback = scheduledPlaybacks[i];
        const QString normalizedKind = previewSfxNormalizedKind(playback.kind);
        if (previewSfxShouldInterruptPreviousKind(normalizedKind)
            && nextPlaybackSecondByKind.contains(normalizedKind)) {
            playback.nextSameKindSecond = nextPlaybackSecondByKind.value(normalizedKind);
        }
        nextPlaybackSecondByKind.insert(normalizedKind, playback.second);
    }

    for (const ScheduledExportPlayback& playback : std::as_const(scheduledPlaybacks)) {
        qint64 maxFrames = -1;
        if (playback.nextSameKindSecond >= 0.0) {
            const double availableSeconds = playback.nextSameKindSecond - playback.second;
            maxFrames = qMax<qint64>(0, qRound64(availableSeconds * kMixSampleRate));
        }
        const QString clipKind = playback.kind == QLatin1String("break_slide_finish")
            ? QStringLiteral("break_slide")
            : playback.kind;
        const auto it = clips.constFind(clipKind);
        if (it == clips.constEnd()) {
            continue;
        }
        if (playback.second + kTimelineEpsilonSeconds < timelineOriginSecond) {
            continue;
        }
        const double volume = kindVolume(playback.kind);
        const double mixedGain = qMax(0.0, playback.gain) * qMax(0.0, volume);
        if (mixedGain <= 0.0) {
            continue;
        }
        const double shiftedSecond = playback.second - timelineOriginSecond;
        if (shiftedSecond < 0.0) {
            continue;
        }
        const qint64 startFrame = qRound64(shiftedSecond * kMixSampleRate);
        addClipToMix(it.value(), mixedGain, startFrame, maxFrames, 0, &mix);
    }

    const auto touchholdIt = clips.constFind(QStringLiteral("touchhold"));
    if (touchholdIt != clips.constEnd() && settings.touchVolume > 0.0) {
        const DecodedClip& touchholdClip = touchholdIt.value();
        struct MixTouchholdSpan {
            qint64 startFrame = 0;
            qint64 endFrame = 0;
        };
        QVector<MixTouchholdSpan> activeSpans;
        activeSpans.reserve(spans.size());
        QVector<qint64> boundaries;
        boundaries.reserve(spans.size() * 2);
        const qint64 totalMixFrames = mix.size() / kMixChannels;
        for (const ExportTouchholdSpan& span : spans) {
            if (span.endSecond <= span.startSecond) {
                continue;
            }
            if (span.startSecond + kTimelineEpsilonSeconds < timelineOriginSecond) {
                continue;
            }
            const double shiftedSecond = span.startSecond - timelineOriginSecond;
            if (shiftedSecond < 0.0) {
                continue;
            }
            const qint64 startFrame = qRound64(shiftedSecond * kMixSampleRate);
            const qint64 spanFrames = qRound64((span.endSecond - span.startSecond) * kMixSampleRate);
            const qint64 audibleEndFrame = qMin(startFrame + spanFrames, startFrame + touchholdClip.frameCount());
            if (startFrame >= totalMixFrames || audibleEndFrame <= startFrame) {
                continue;
            }
            MixTouchholdSpan mixSpan;
            mixSpan.startFrame = startFrame;
            mixSpan.endFrame = qMin(audibleEndFrame, totalMixFrames);
            activeSpans.append(mixSpan);
            boundaries.append(mixSpan.startFrame);
            boundaries.append(mixSpan.endFrame);
        }
        if (!activeSpans.isEmpty()) {
            std::sort(boundaries.begin(), boundaries.end());
            boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
            for (int boundaryIndex = 0; boundaryIndex + 1 < boundaries.size(); ++boundaryIndex) {
                const qint64 intervalStart = boundaries.at(boundaryIndex);
                const qint64 intervalEnd = boundaries.at(boundaryIndex + 1);
                if (intervalEnd <= intervalStart) {
                    continue;
                }

                QVector<const MixTouchholdSpan*> overlappingSpans;
                overlappingSpans.reserve(activeSpans.size());
                for (const MixTouchholdSpan& span : activeSpans) {
                    if (span.startFrame < intervalEnd && span.endFrame > intervalStart) {
                        overlappingSpans.append(&span);
                    }
                }
                if (overlappingSpans.isEmpty()) {
                    continue;
                }

                const double totalCopies = previewTouchholdAggregatePlaybackCopies(overlappingSpans.size());
                const double perSpanGain =
                    qBound(0.0, settings.touchVolume * (totalCopies / static_cast<double>(overlappingSpans.size())), 1.5);
                for (const MixTouchholdSpan* span : overlappingSpans) {
                    const qint64 clipStartFrame = intervalStart - span->startFrame;
                    addClipToMix(
                        touchholdClip,
                        perSpanGain,
                        intervalStart,
                        intervalEnd - intervalStart,
                        clipStartFrame,
                        &mix
                    );
                }
            }
        }
    }

    return writeWav16(outputPath, mix, kMixSampleRate, kMixChannels);
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

bool writeAllToProcess(QProcess* process, const char* data, qint64 size)
{
    if (process == nullptr || data == nullptr || size < 0) {
        return false;
    }
    qint64 writtenTotal = 0;
    while (writtenTotal < size) {
        const qint64 written = process->write(data + writtenTotal, size - writtenTotal);
        if (written < 0) {
            return false;
        }
        if (written == 0) {
            if (!process->waitForBytesWritten(30000)) {
                return false;
            }
            continue;
        }
        writtenTotal += written;
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
        rgba = convertedFrame;
    }

    const int width = rgba->width();
    const int height = rgba->height();
    if (width <= 0 || height <= 0) {
        return true;
    }

    const qint64 packedStride = static_cast<qint64>(width) * 4;
    const qint64 packedSize = packedStride * height;
    if (rgba->bytesPerLine() == packedStride) {
        *data = reinterpret_cast<const char*>(rgba->constBits());
        *size = packedSize;
        return true;
    }

    packedScratch->resize(static_cast<qsizetype>(packedSize));
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
    if (!output.isEmpty()) {
        return truncateForLog(output, maxChars);
    }
    return truncateForLog(process.errorString(), maxChars);
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
    const QString logPathLine = QStringLiteral("Log: %1").arg(videoExportDebugLogPath());
    if (details.trimmed().isEmpty()) {
        return logPathLine;
    }
    return details + QStringLiteral("\n") + logPathLine;
}

}  // namespace

VideoExportResult VideoExportController::exportFullPreview(
    const VideoExportTask& task,
    const PreviewCanvas* sourceCanvas,
    QProgressDialog* progress
)
{
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
    return exportPreparedTask(task, sourceCanvas, progressCallback);
}

VideoExportResult VideoExportController::exportPreparedTask(
    const VideoExportTask& task,
    const PreviewCanvas* sourceCanvas,
    const VideoExportProgressCallback& progressCallback
)
{
    VideoExportResult result;
    QElapsedTimer exportTimer;
    exportTimer.start();
    appendVideoExportLog(
        QStringLiteral("export_begin"),
        QStringLiteral("output=%1 chart=%2 media=%3 track=%4 skin=%5 notes=%6 start=%7 duration=%8 size=%9x%10 fps=%11 profile=%12 sourceCanvas=%13")
            .arg(task.outputPath, task.chartPath, task.backgroundMediaPath, task.trackPath, task.skinDirectory)
            .arg(task.noteMarkers.size())
            .arg(task.exportStartSeconds, 0, 'f', 6)
            .arg(task.contentDurationSeconds, 0, 'f', 6)
            .arg(task.outputWidth)
            .arg(task.outputHeight)
            .arg(task.fps)
            .arg(performanceProfileToken(task.performanceProfile))
            .arg(sourceCanvas != nullptr ? 1 : 0)
    );
    if (sourceCanvas == nullptr && task.skinDirectory.trimmed().isEmpty()) {
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

    const double segmentStartSecond = qMax(0.0, task.exportStartSeconds);
    const double segmentDurationSeconds = task.contentDurationSeconds;
    const double segmentEndSecond = segmentStartSecond + segmentDurationSeconds;
    const double timelineOriginSecond = segmentStartSecond - miacode::video_export::kLeadInSeconds;
    const double totalSeconds = miacode::video_export::kLeadInSeconds + segmentDurationSeconds;
    const int frameCount = qMax(1, qRound(totalSeconds * task.fps));
    const double alignedTotalSeconds = static_cast<double>(frameCount) / qMax(1, task.fps);
    const int frameWidth = qMax(1, task.outputWidth);
    const int frameHeight = qMax(1, task.outputHeight);
    const QSize frameSize(frameWidth, frameHeight);
    const QString explicitMediaPath = normalizePath(task.backgroundMediaPath);
    const QString mediaPath = (!explicitMediaPath.isEmpty() && QFileInfo::exists(explicitMediaPath))
        ? explicitMediaPath
        : resolveBackgroundMediaPath(task.chartPath);
    const bool hasMedia = !mediaPath.isEmpty();
    const bool mediaIsImage = hasMedia && isImageMediaPath(mediaPath);
    const QString trackPath = (task.trackPath.isEmpty() || !QFileInfo::exists(task.trackPath))
        ? QString()
        : normalizePath(task.trackPath);
    const bool hasTrack = !trackPath.isEmpty();
    const QVector<TimelineNoteMarker> exportMarkers =
        filteredMarkersForRange(task.noteMarkers, timelineOriginSecond, segmentEndSecond);
    appendVideoExportLog(
        QStringLiteral("input_probe"),
        QStringLiteral("media=%1 hasMedia=%2 mediaIsImage=%3 track=%4 hasTrack=%5 segmentStart=%6 segmentEnd=%7 timelineOrigin=%8 totalSeconds=%9 alignedSeconds=%10 frameCount=%11 size=%12x%13")
            .arg(mediaPath)
            .arg(hasMedia ? 1 : 0)
            .arg(mediaIsImage ? 1 : 0)
            .arg(trackPath)
            .arg(hasTrack ? 1 : 0)
            .arg(segmentStartSecond, 0, 'f', 6)
            .arg(segmentEndSecond, 0, 'f', 6)
            .arg(timelineOriginSecond, 0, 'f', 6)
            .arg(totalSeconds, 0, 'f', 6)
            .arg(alignedTotalSeconds, 0, 'f', 6)
            .arg(frameCount)
            .arg(frameWidth)
            .arg(frameHeight)
    );
    appendVideoExportLog(
        QStringLiteral("render_plan"),
        QStringLiteral("fps=%1 frameBudgetMs=%2 frameCount=%3")
            .arg(task.fps)
            .arg(1000.0 / static_cast<double>(qMax(1, task.fps)), 0, 'f', 4)
            .arg(frameCount)
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

    const QString sfxWavPath = QDir(tempDir.path()).filePath(QStringLiteral("export_sfx.wav"));
    if (!mixSfxTrackToWav(
            sfxWavPath,
            exportMarkers,
            task.audioSettings,
            alignedTotalSeconds,
            timelineOriginSecond)) {
        result.message = QStringLiteral("Unable to generate SFX mix track.");
        result.details = QStringLiteral("Check whether assets/SFX files are complete.");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("fail_mix_sfx"), QStringLiteral("sfxWavPath=%1").arg(sfxWavPath));
        return result;
    }
    appendVideoExportLog(QStringLiteral("mix_sfx_ok"), QStringLiteral("sfxWavPath=%1").arg(sfxWavPath));

    if (setProgressPercent(5, QStringLiteral("Starting ffmpeg..."))) {
        result.message = QStringLiteral("canceled");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("canceled"), QStringLiteral("stage=start_ffmpeg_progress"));
        return result;
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
         << QStringLiteral("pipe:0");

    const double outerDimAlpha = qBound(0.0, 1.0 - task.backgroundBrightnessOuter, 1.0);
    const double innerDimAlpha = qBound(0.0, 1.0 - task.backgroundBrightnessInner, 1.0);
    const bool hasDimMask = outerDimAlpha > 1e-6 || innerDimAlpha > 1e-6;

    int mediaInputIndex = -1;
    int dimMaskInputIndex = -1;
    int bgmInputIndex = -1;
    int sfxInputIndex = -1;
    int currentInputIndex = 1;
    if (hasMedia) {
        mediaInputIndex = currentInputIndex++;
        if (mediaIsImage) {
            args << QStringLiteral("-loop")
                 << QStringLiteral("1")
                 << QStringLiteral("-framerate")
                 << QString::number(task.fps);
        }
        args << QStringLiteral("-i") << mediaPath;
    }
    if (hasDimMask) {
        const QString dimMaskPath = QDir(tempDir.path()).filePath(QStringLiteral("dim_mask.png"));
        const double ringRatio = resolvedLayoutRingDiameterRatio(hasMedia);
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
    if (hasTrack) {
        bgmInputIndex = currentInputIndex++;
        args << QStringLiteral("-i") << trackPath;
    }
    sfxInputIndex = currentInputIndex++;
    args << QStringLiteral("-i") << sfxWavPath;

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
        if (task.backgroundScaleMode == PreviewBackgroundScaleMode::FitContain) {
            mediaChain += QStringLiteral(
                "scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black")
                .arg(frameWidth)
                .arg(frameHeight);
        } else {
            mediaChain += QStringLiteral(
                "scale=%1:%2:force_original_aspect_ratio=increase,crop=%1:%2")
                .arg(frameWidth)
                .arg(frameHeight);
        }
        mediaChain += QStringLiteral(",setsar=1,fps=%1,format=rgba").arg(task.fps);
        if (!mediaIsImage) {
            if (timelineOriginSecond > kTimelineEpsilonSeconds) {
                mediaChain += QStringLiteral(",trim=start=%1:end=%2,setpts=PTS-STARTPTS")
                    .arg(timelineOriginText)
                    .arg(QString::number(timelineOriginSecond + alignedTotalSeconds, 'f', 6));
            } else if (timelineOriginSecond < -kTimelineEpsilonSeconds) {
                mediaChain += QStringLiteral(",trim=start=0:end=%1,setpts=PTS-STARTPTS+%2/TB")
                    .arg(QString::number(alignedTotalSeconds + timelineOriginSecond, 'f', 6))
                    .arg(QString::number(-timelineOriginSecond, 'f', 6));
            }
            mediaChain += QStringLiteral(",tpad=stop_mode=clone:stop_duration=%1").arg(totalSecondsText);
        }
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

    filterParts << QStringLiteral("[0:v]vflip,format=rgba[overlay_src]");
    filterParts << QStringLiteral("[%1:a]atrim=0:%2,asetpts=PTS-STARTPTS,aresample=%3[sfx]")
                       .arg(sfxInputIndex)
                       .arg(totalSecondsText)
                       .arg(kMixSampleRate);
    if (hasTrack) {
        if (timelineOriginSecond > kTimelineEpsilonSeconds) {
            filterParts << QStringLiteral("[%1:a]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS,aresample=%4,volume=%5[bgm]")
                               .arg(bgmInputIndex)
                               .arg(timelineOriginText)
                               .arg(QString::number(timelineOriginSecond + alignedTotalSeconds, 'f', 6))
                               .arg(kMixSampleRate)
                               .arg(QString::number(task.audioSettings.bgmVolume, 'f', 6));
        } else if (timelineOriginSecond < -kTimelineEpsilonSeconds) {
            const int delayMs = qMax(0, qRound(-timelineOriginSecond * 1000.0));
            filterParts << QStringLiteral("[%1:a]atrim=start=0:end=%2,asetpts=PTS-STARTPTS,adelay=%3|%3,aresample=%4,volume=%5[bgm]")
                               .arg(bgmInputIndex)
                               .arg(QString::number(alignedTotalSeconds + timelineOriginSecond, 'f', 6))
                               .arg(delayMs)
                               .arg(kMixSampleRate)
                               .arg(QString::number(task.audioSettings.bgmVolume, 'f', 6));
        } else {
            filterParts << QStringLiteral("[%1:a]atrim=0:%2,asetpts=PTS-STARTPTS,aresample=%3,volume=%4[bgm]")
                               .arg(bgmInputIndex)
                               .arg(totalSecondsText)
                               .arg(kMixSampleRate)
                               .arg(QString::number(task.audioSettings.bgmVolume, 'f', 6));
        }
        filterParts << QStringLiteral("[bgm][sfx]amix=inputs=2:normalize=0[aout]");
    } else {
        filterParts << QStringLiteral("[sfx]anull[aout]");
    }

    const SystemMemoryInfo memoryInfo = querySystemMemoryInfo();
    appendVideoExportLog(QStringLiteral("memory_snapshot"), memoryInfoToLog(memoryInfo));
    QString encoderProbeLog;
    const VideoEncoderConfig encoderConfig = chooseVideoEncoder(
        ffmpegPath,
        frameWidth,
        frameHeight,
        task.fps,
        memoryInfo,
        &encoderProbeLog
    );
    appendVideoExportLog(QStringLiteral("encoder_select"), encoderProbeLog);
    const int idealThreadCount = qMax(1, QThread::idealThreadCount());
    const bool speedProfile = task.performanceProfile == VideoExportPerformanceProfile::Speed;
    const bool softwareX265 = !encoderConfig.isHardware && encoderConfig.codec == QLatin1String("libx265");
    const qint64 availMiB = bytesToMiB(memoryInfo.availablePhysicalBytes);
    const int encoderThreads = qBound(
        1,
        envIntValue(QStringLiteral("MIACODE_EXPORT_ENCODER_THREADS"), idealThreadCount),
        32
    );
    const int defaultFilterThreads = softwareX265
        ? ((memoryInfo.valid && availMiB >= 16384) ? 2 : 1)
        : (speedProfile
            ? qBound(1, idealThreadCount, 16)
            : qBound(1, qMax(1, idealThreadCount / 2), 8));
    const int filterThreads = qBound(
        1,
        envIntValue(QStringLiteral("MIACODE_EXPORT_FILTER_THREADS"), defaultFilterThreads),
        softwareX265 ? 2 : 16
    );
    const int effectiveEncoderThreads = softwareX265 ? 0 : encoderThreads;
    appendVideoExportLog(
        QStringLiteral("thread_plan"),
        QStringLiteral("ideal=%1 encoderThreads=%2 effectiveEncoderThreads=%3 filterThreads=%4 encoder=%5 hw=%6 x265Managed=%7 availMiB=%8 profile=%9")
            .arg(idealThreadCount)
            .arg(encoderThreads)
            .arg(effectiveEncoderThreads)
            .arg(filterThreads)
            .arg(encoderConfig.codec)
            .arg(encoderConfig.isHardware ? 1 : 0)
            .arg(softwareX265 ? 1 : 0)
            .arg(memoryInfo.valid ? QString::number(availMiB) : QStringLiteral("na"))
            .arg(performanceProfileToken(task.performanceProfile))
    );

    PreviewCanvas exportCanvas;
    if (sourceCanvas != nullptr) {
        exportCanvas.copyRenderStateFrom(*sourceCanvas);
    } else {
        exportCanvas.setSkinDirectory(task.skinDirectory);
        const int skinWaitMs = qBound(0, task.skinLoadWaitMs, 20000);
        bool skinLoaded = exportCanvas.hasCoreSkinAssetsLoadedForDebug();
        if (!skinLoaded && skinWaitMs > 0) {
            QElapsedTimer skinWaitTimer;
            skinWaitTimer.start();
            while (!skinLoaded && skinWaitTimer.elapsed() < skinWaitMs) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                skinLoaded = exportCanvas.hasCoreSkinAssetsLoadedForDebug();
            }
        }
        appendVideoExportLog(
            QStringLiteral("skin_bootstrap"),
            QStringLiteral("standalone=1 loaded=%1 waitMs=%2 dir=%3")
                .arg(skinLoaded ? 1 : 0)
                .arg(skinWaitMs)
                .arg(task.skinDirectory)
        );
        if (!skinLoaded) {
            result.message = QStringLiteral("Failed to load export skin assets.");
            result.details = withExportLogPath(QStringLiteral("skin_dir=%1").arg(task.skinDirectory));
            appendVideoExportLog(QStringLiteral("fail_skin_load"), result.message);
            return result;
        }
    }
    exportCanvas.setStageMediaAvailable(hasMedia);
    exportCanvas.setBackgroundBrightnessOuter(task.backgroundBrightnessOuter);
    exportCanvas.setBackgroundBrightnessInner(task.backgroundBrightnessInner);
    exportCanvas.setLayoutSquareScale(task.layoutSquareScale);
    exportCanvas.setSmoothBrightness(task.smoothBrightness);
    exportCanvas.setBackgroundScaleMode(task.backgroundScaleMode);
    exportCanvas.setNoteFlowSpeed(task.noteFlowSpeed);
    exportCanvas.setShowDebugInfo(false);
    exportCanvas.setExportWifiTrackBrightnessCompensationEnabled(true);
    exportCanvas.setNoteMarkers(exportMarkers);
    exportCanvas.setMuriRenderOptions(task.muriRenderOptions);
    exportCanvas.setMuriAnalysisReport(task.muriAnalysisReport);
    exportCanvas.setCpuTrackAreaCachingEnabled(speedProfile);
    const QSurfaceFormat requestedFormat = sourceCanvas != nullptr
        ? sourceCanvas->format()
        : QSurfaceFormat::defaultFormat();
    QOpenGLContext* shareContext = sourceCanvas != nullptr ? sourceCanvas->context() : nullptr;
    const bool hasOffscreenPboReadbackOverride =
        !qEnvironmentVariableIsEmpty("MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO");
    const bool enableOffscreenPboReadback =
        envFlagEnabled(QStringLiteral("MIACODE_EXPORT_ENABLE_OFFSCREEN_PBO"));
    const bool hasGpuRenderOverride =
        !qEnvironmentVariableIsEmpty("MIACODE_EXPORT_ENABLE_GPU_RENDER");
    const bool requestOffscreenGpu =
        enableOffscreenPboReadback
        || (hasGpuRenderOverride
                ? envFlagEnabled(QStringLiteral("MIACODE_EXPORT_ENABLE_GPU_RENDER"))
                : true);
    QString offscreenInitError;
    bool useOffscreenGpu = false;
    if (requestOffscreenGpu) {
        useOffscreenGpu = exportCanvas.initializeOffscreenRenderer(
            requestedFormat,
            shareContext,
            &offscreenInitError
        );
    } else {
        offscreenInitError = QStringLiteral("disabled_by_env");
    }
    const bool disableOffscreenPboReadback =
        envFlagEnabled(QStringLiteral("MIACODE_EXPORT_DISABLE_OFFSCREEN_PBO"));
    const bool requestOffscreenPboReadback =
        useOffscreenGpu
        && !disableOffscreenPboReadback
        && (hasOffscreenPboReadbackOverride ? enableOffscreenPboReadback : true);
    QString offscreenPboError;
    bool useOffscreenPboReadback = false;
    if (requestOffscreenPboReadback) {
        useOffscreenPboReadback = exportCanvas.supportsOffscreenPboReadback(&offscreenPboError);
    }
    appendVideoExportLog(
        QStringLiteral("render_backend"),
        QStringLiteral("gpuRequested=%1 sourceGpuReady=%2 sourceCtx=%3 offscreenInit=%4 exportGpuReady=%5 pboRequested=%6 pboEnabled=%7 initError=%8 pboError=%9")
            .arg(requestOffscreenGpu ? 1 : 0)
            .arg(sourceCanvas != nullptr && sourceCanvas->isGpuRendererReadyForDebug() ? 1 : 0)
            .arg(shareContext != nullptr ? 1 : 0)
            .arg(useOffscreenGpu ? 1 : 0)
            .arg(exportCanvas.isGpuRendererReadyForDebug() ? 1 : 0)
            .arg(requestOffscreenPboReadback ? 1 : 0)
            .arg(useOffscreenPboReadback ? 1 : 0)
            .arg(offscreenInitError.isEmpty() ? QStringLiteral("ok") : offscreenInitError)
            .arg(offscreenPboError.isEmpty() ? QStringLiteral("ok") : offscreenPboError)
    );

    const QString overlayAlphaMode = useOffscreenGpu
        ? QStringLiteral("premultiplied")
        : QStringLiteral("straight");
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
         << QStringLiteral("160k");
    if (encoderConfig.codec.startsWith(QStringLiteral("h264"))) {
        args << QStringLiteral("-bf") << QStringLiteral("0");
    }
    if (!encoderConfig.isHardware && !softwareX265) {
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
    ffmpeg.start(ffmpegPath, args, QIODevice::ReadWrite);
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
    static constexpr int kFrameProgressStride = 120;
    FrameTimingStats frameStats;

    const bool diagRepeatEnabled = envFlagEnabled(QStringLiteral("MIACODE_EXPORT_DIAG_REPEAT"));
    const int diagCropBottom = qMax(0, envIntValue(QStringLiteral("MIACODE_EXPORT_DIAG_CROP_BOTTOM"), 0));
    const int diagMaxLogLines = qMax(0, envIntValue(QStringLiteral("MIACODE_EXPORT_DIAG_MAX_LINES"), 400));
    const bool diagLogAllRepeatPairs = envFlagEnabled(QStringLiteral("MIACODE_EXPORT_DIAG_LOG_ALL_REPEATS"));
    const bool hasObjectHashOverride = !qEnvironmentVariableIsEmpty("MIACODE_EXPORT_DIAG_OBJECT_HASH");
    const bool diagObjectHashEnabled = diagRepeatEnabled
        && (hasObjectHashOverride
                ? envFlagEnabled(QStringLiteral("MIACODE_EXPORT_DIAG_OBJECT_HASH"))
                : true);
    const bool diagObjectTraceEnabled = diagRepeatEnabled
        && envFlagEnabled(QStringLiteral("MIACODE_EXPORT_DIAG_OBJECT_TRACE"));
    const int diagObjectTraceMaxLines = qMax(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_DIAG_OBJECT_TRACE_MAX_LINES"), 5000)
    );
    const int diagObjectDiffThreshold = qBound(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_DIAG_OBJECT_DIFF_THRESHOLD"), 8),
        4 * 255
    );
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
    const bool diagCompareRenderPathsEnabled = diagRepeatEnabled
        && envFlagEnabled(QStringLiteral("MIACODE_EXPORT_DIAG_COMPARE_RENDER_PATHS"));
    const int diagCompareRadius = qBound(
        2,
        envIntValue(QStringLiteral("MIACODE_EXPORT_DIAG_COMPARE_RADIUS"), 24),
        512
    );
    const int diagCompareMaxLines = qMax(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_DIAG_COMPARE_MAX_LINES"), 400)
    );
    const double diagCompareLogThreshold = qBound(
        0.0,
        envDoubleValue(QStringLiteral("MIACODE_EXPORT_DIAG_COMPARE_LOG_THRESHOLD"), 0.0010),
        1.0
    );
    int diagCompareLoggedLines = 0;
    int diagCompareFrames = 0;
    int diagCompareObjectFrames = 0;
    double diagCompareFullDiffSum = 0.0;
    double diagCompareFullDiffMax = 0.0;
    int diagCompareFullDiffMaxFrame = -1;
    double diagCompareObjectDiffSum = 0.0;
    double diagCompareObjectDiffMax = 0.0;
    int diagCompareObjectDiffMaxFrame = -1;
    const bool diagPipeHashEnabled = diagRepeatEnabled
        && envFlagEnabled(QStringLiteral("MIACODE_EXPORT_DIAG_PIPE_HASH"));
    const int diagPipeHashMaxLines = qMax(
        0,
        envIntValue(QStringLiteral("MIACODE_EXPORT_DIAG_PIPE_HASH_MAX_LINES"), 400)
    );
    int diagPipeHashLoggedLines = 0;
    int diagPipeHashObjectFrames = 0;
    int diagPipeHashObjectRepeatedAdj = 0;
    quint64 previousObjectPackedHash = 0;
    bool hasPreviousObjectPackedHash = false;
    const QString diagRawDumpPath =
        qEnvironmentVariable("MIACODE_EXPORT_DIAG_RAW_DUMP_PATH").trimmed();
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
    PreviewCanvas diagReferenceCanvas;
    PreviewCanvas diagCpuCompareCanvas;
    bool diagReferenceUseOffscreen = false;
    bool diagReferenceReady = false;
    bool diagCpuCompareReady = false;

    if (useOffscreenGpu) {
        frameTimer.start();
        const QImage warmupFrame = exportCanvas.renderOverlayFrameOffscreen(
            frameSize,
            timelineOriginSecond,
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
        diagReferenceCanvas.setNoteFlowSpeed(task.noteFlowSpeed);
        diagReferenceCanvas.setShowDebugInfo(false);
        diagReferenceCanvas.setNoteMarkers({});
        diagReferenceCanvas.setCpuTrackAreaCachingEnabled(false);
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
    if (diagCompareRenderPathsEnabled) {
        diagCpuCompareCanvas.copyRenderStateFrom(exportCanvas);
        diagCpuCompareCanvas.setBackgroundBrightnessOuter(task.backgroundBrightnessOuter);
        diagCpuCompareCanvas.setBackgroundBrightnessInner(task.backgroundBrightnessInner);
        diagCpuCompareCanvas.setLayoutSquareScale(task.layoutSquareScale);
        diagCpuCompareCanvas.setSmoothBrightness(task.smoothBrightness);
        diagCpuCompareCanvas.setBackgroundScaleMode(task.backgroundScaleMode);
        diagCpuCompareCanvas.setNoteFlowSpeed(task.noteFlowSpeed);
        diagCpuCompareCanvas.setShowDebugInfo(false);
        diagCpuCompareCanvas.setNoteMarkers(exportMarkers);
        diagCpuCompareCanvas.setCpuTrackAreaCachingEnabled(false);
        diagCpuCompareReady = true;
        appendVideoExportLog(
            QStringLiteral("render_path_compare_backend"),
            QStringLiteral("cpuCompareReady=1")
        );
    }

    PendingPboFrame pendingPboFrame;
    QImage convertedRgbaFrame;
    QByteArray packedFrameScratch;

    auto processReadyFrame = [&](const ReadyFramePayload& readyFrame) -> bool {
        const int frameIndex = readyFrame.frameIndex;
        const double exportSecond = readyFrame.exportSecond;
        const QVector<ObjectTraceItem>& traceItems = readyFrame.traceItems;
        const QImage& frame = readyFrame.frame;
        const qint64 renderNs = readyFrame.renderNs;
        const qint64 offscreenDrawNs = readyFrame.offscreenDrawNs;
        const qint64 offscreenReadbackNs = readyFrame.offscreenReadbackNs;
        const bool usedOffscreenPath = readyFrame.usedOffscreenPath;
        const int fallbackCount = readyFrame.fallbackCount;
        const bool usedGpuRenderer = readyFrame.usedGpuRenderer;

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

        if (diagCompareRenderPathsEnabled && diagCpuCompareReady) {
            const QImage cpuFrame = diagCpuCompareCanvas.renderOverlayFrame(
                frameSize,
                exportSecond,
                task.showTimestamp,
                task.showObjectStatsHud
            );
            const double fullDiff = meanAbsDiffNormalized(frame, cpuFrame);
            if (fullDiff >= 0.0) {
                ++diagCompareFrames;
                diagCompareFullDiffSum += fullDiff;
                if (fullDiff > diagCompareFullDiffMax) {
                    diagCompareFullDiffMax = fullDiff;
                    diagCompareFullDiffMaxFrame = frameIndex;
                }
            }

            double objectDiffMax = -1.0;
            const double objectDiff = meanAbsDiffAroundTraceItems(
                frame,
                cpuFrame,
                traceItems,
                diagCompareRadius,
                &objectDiffMax
            );
            if (objectDiff >= 0.0) {
                ++diagCompareObjectFrames;
                diagCompareObjectDiffSum += objectDiff;
                if (objectDiff > diagCompareObjectDiffMax) {
                    diagCompareObjectDiffMax = objectDiff;
                    diagCompareObjectDiffMaxFrame = frameIndex;
                }
            }

            if (diagCompareLoggedLines < diagCompareMaxLines) {
                const bool shouldLog = !traceItems.isEmpty()
                    || (fullDiff >= diagCompareLogThreshold)
                    || (objectDiff >= diagCompareLogThreshold);
                if (shouldLog) {
                    appendVideoExportLog(
                        QStringLiteral("render_path_compare"),
                        QStringLiteral(
                            "frame=%1 t=%2 hasObjects=%3 fullDiff=%4 objDiff=%5 objMax=%6 radius=%7 offSig=0x%8 cpuSig=0x%9")
                            .arg(frameIndex)
                            .arg(exportSecond, 0, 'f', 6)
                            .arg(traceItems.isEmpty() ? 0 : 1)
                            .arg(fullDiff, 0, 'f', 8)
                            .arg(objectDiff, 0, 'f', 8)
                            .arg(objectDiffMax, 0, 'f', 8)
                            .arg(diagCompareRadius)
                            .arg(QString::number(sampledFrameSignature(frame), 16))
                            .arg(QString::number(sampledFrameSignature(cpuFrame), 16))
                    );
                    ++diagCompareLoggedLines;
                }
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
        if (packedFrameSize > 0 && !writeAllToProcess(&ffmpeg, packedFrameData, packedFrameSize)) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            const QString ffmpegOutput = truncateForLog(QString::fromUtf8(ffmpeg.readAllStandardOutput()).trimmed());
            result.message = QStringLiteral("Failed to write frame data to ffmpeg.");
            result.details = ffmpeg.errorString();
            if (!ffmpegOutput.isEmpty()) {
                result.details = ffmpegOutput + QStringLiteral("\n") + result.details;
            }
            result.details = withExportLogPath(result.details);
            appendVideoExportLog(
                QStringLiteral("fail_ffmpeg_write"),
                QStringLiteral("frame=%1 bytes=%2 hash=0x%3 error=%4 output=%5")
                    .arg(frameIndex)
                    .arg(packedFrameSize)
                    .arg(QString::number(packedHash, 16))
                    .arg(ffmpeg.errorString())
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
            ffmpeg.waitForFinished(2000);
            const QString ffmpegOutput = truncateForLog(QString::fromUtf8(ffmpeg.readAllStandardOutput()).trimmed());
            result.message = QStringLiteral("ffmpeg exited unexpectedly during frame piping.");
            result.details = ffmpegOutput;
            if (result.details.isEmpty()) {
                result.details = ffmpeg.errorString();
            } else if (!ffmpeg.errorString().isEmpty()) {
                result.details += QStringLiteral("\n") + ffmpeg.errorString();
            }
            result.details = withExportLogPath(result.details);
            appendVideoExportLog(
                QStringLiteral("fail_ffmpeg_early_exit"),
                QStringLiteral("frame=%1 state=%2 exitCode=%3 output=%4")
                    .arg(frameIndex)
                    .arg(static_cast<int>(ffmpeg.state()))
                    .arg(ffmpeg.exitCode())
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
        const double exportSecond = timelineOriginSecond + static_cast<double>(frameIndex) / task.fps;
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
            &pendingPboFrame,
            frameSize,
            frameIndex,
            exportSecond,
            task.showTimestamp,
            task.showObjectStatsHud,
            std::move(traceItems),
            &readyFrame,
            &renderBackendFallbackDetail
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

    if (useOffscreenPboReadback && pendingPboFrame.valid) {
        ReadyFramePayload readyFrame;
        QString drainError;
        if (!drainPendingExportFrame(
                &exportCanvas,
                &pendingPboFrame,
                frameSize,
                task.showTimestamp,
                task.showObjectStatsHud,
                &readyFrame,
                &drainError)) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("Render frame failed.");
            result.details = withExportLogPath(drainError.isEmpty() ? QStringLiteral("failed to drain PBO readback") : drainError);
            appendVideoExportLog(
                QStringLiteral("fail_render_frame"),
                QStringLiteral("frame=%1 offscreen=1 drain=1 error=%2")
                    .arg(pendingPboFrame.frameIndex)
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
    if (diagCompareRenderPathsEnabled) {
        appendVideoExportLog(
            QStringLiteral("render_path_compare_summary"),
            QStringLiteral(
                "frames=%1 objectFrames=%2 avgFullDiff=%3 maxFullDiff=%4@%5 "
                "avgObjDiff=%6 maxObjDiff=%7@%8 logged=%9 radius=%10 threshold=%11")
                .arg(diagCompareFrames)
                .arg(diagCompareObjectFrames)
                .arg(diagCompareFrames > 0
                        ? (diagCompareFullDiffSum / static_cast<double>(diagCompareFrames))
                        : 0.0,
                    0,
                    'f',
                    8)
                .arg(diagCompareFullDiffMax, 0, 'f', 8)
                .arg(diagCompareFullDiffMaxFrame)
                .arg(diagCompareObjectFrames > 0
                        ? (diagCompareObjectDiffSum / static_cast<double>(diagCompareObjectFrames))
                        : 0.0,
                    0,
                    'f',
                    8)
                .arg(diagCompareObjectDiffMax, 0, 'f', 8)
                .arg(diagCompareObjectDiffMaxFrame)
                .arg(diagCompareLoggedLines)
                .arg(diagCompareRadius)
                .arg(diagCompareLogThreshold, 0, 'f', 8)
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

    ffmpeg.closeWriteChannel();
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
        const QString ffmpegOutput = processOutputAndErrorForLog(ffmpeg, 2000);
        result.details = ffmpeg.errorString();
        if (!ffmpegOutput.isEmpty()) {
            result.details = ffmpegOutput + QStringLiteral("\n") + result.details;
        }
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(
            QStringLiteral("fail_ffmpeg_wait"),
            QStringLiteral("error=%1 output=%2").arg(ffmpeg.errorString(), truncateForLog(ffmpegOutput, 1000))
        );
        return result;
    }

    const QString ffmpegOutput = processOutputAndErrorForLog(ffmpeg, 2000);
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        result.message = QStringLiteral("ffmpeg encode failed.");
        result.details = ffmpegOutput;
        if (result.details.isEmpty()) {
            result.details = ffmpegBaseArgsLog(ffmpegPath, args);
        }
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(
            QStringLiteral("fail_ffmpeg_exit"),
            QStringLiteral("status=%1 code=%2 output=%3")
                .arg(static_cast<int>(ffmpeg.exitStatus()))
                .arg(ffmpeg.exitCode())
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
        result.details = withExportLogPath(remuxOutput);
        appendVideoExportLog(
            QStringLiteral("fail_ffmpeg_remux_exit"),
            QStringLiteral("status=%1 code=%2 output=%3")
                .arg(static_cast<int>(remuxProcess.exitStatus()))
                .arg(remuxProcess.exitCode())
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
