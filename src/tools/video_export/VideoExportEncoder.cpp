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
#include <QSettings>
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
#include <iterator>
#include <limits>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "VideoExportControllerInternal.h"

// VideoExportEncoder.cpp — memory probe, x264/bitrate/runtime-config selection, ffmpeg/ffprobe resolution, encoder runtime probe, and encoder selection.
//
// Definitions extracted verbatim from the original VideoExportController.cpp
// during the god-file split. All helpers live in the shared
// miacode::video_export::detail namespace (declared in
// VideoExportControllerInternal.h).
using namespace miacode::video_export::detail;

namespace miacode::video_export::detail {

namespace {

constexpr auto kPreferredHardwareEncoderSettingsKey =
    "video_export/runtime_probe/preferred_hardware_encoder";

QSettings encoderRuntimeSettings()
{
    return QSettings(
        QSettings::IniFormat,
        QSettings::UserScope,
        QStringLiteral("MiaCode"),
        QStringLiteral("VideoExportRuntime"));
}

QString preferredHardwareEncoder()
{
    QSettings settings = encoderRuntimeSettings();
    return settings.value(QLatin1String(kPreferredHardwareEncoderSettingsKey)).toString().trimmed();
}

void rememberPreferredHardwareEncoder(const QString& codec)
{
    if (codec.isEmpty()) {
        return;
    }
    QSettings settings = encoderRuntimeSettings();
    settings.setValue(QLatin1String(kPreferredHardwareEncoderSettingsKey), codec);
}

}  // namespace

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
    config.premultipliedPipeOverride =
        miacode::debug_options::exportPremultipliedPipeOverride();

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
    VideoExportPreset preset,
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

    if (preset == VideoExportPreset::Fast) {
        if (reason != nullptr) {
            *reason = QStringLiteral("fast_preset_prefers_hardware");
        }
        return true;
    }

#ifdef Q_OS_MACOS
    // Balanced mode on macOS: VideoToolbox is high quality and far more
    // power-efficient than libx264, so prefer it whenever available instead
    // of the Windows-tuned pixel-rate heuristics below. libx264 remains the
    // runtime-probe fallback.
    if (reason != nullptr) {
        *reason = QStringLiteral("macos_prefers_videotoolbox");
    }
    return true;
#endif

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
    VideoExportSizePreset sizePreset,
    int outputWidth,
    int outputHeight,
    int fps
)
{
    const int safeWidth = qMax(1, outputWidth);
    const int safeHeight = qMax(1, outputHeight);
    const int safeFps = qMax(1, fps);

    VideoBitratePlan plan;
    const miacode::video_export::VideoExportSizePolicy sizePolicy =
        miacode::video_export::videoExportSizePolicy(sizePreset);
    if (sizePreset != VideoExportSizePreset::Standard) {
        plan.bitrateKbps = qBound<qint64>(
            sizePolicy.minBitrateKbps,
            qRound64(static_cast<double>(safeWidth) * safeHeight * safeFps
                     * sizePolicy.bitrateCoefficient / 1000.0),
            sizePolicy.maxBitrateKbps
        );
        plan.maxRateKbps = qMax<qint64>(
            plan.bitrateKbps,
            qRound64(static_cast<double>(plan.bitrateKbps) * sizePolicy.maxRateMultiplier)
        );
        plan.bufSizeKbps = qMax<qint64>(
            plan.maxRateKbps,
            qRound64(static_cast<double>(plan.maxRateKbps) * sizePolicy.bufferMultiplier)
        );
        return plan;
    }
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
    VideoExportSizePreset sizePreset,
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
    const miacode::video_export::VideoExportSizePolicy sizePolicy =
        miacode::video_export::videoExportSizePolicy(sizePreset);
    if (preset == VideoExportPreset::Fast) {
        plan.preset = QStringLiteral("veryfast");
        plan.crf = 22;
        plan.bframes = 0;
        plan.tune = QStringLiteral("animation");
        if (!exportConfig.x264PresetOverride.isEmpty()) {
            plan.preset = exportConfig.x264PresetOverride;
        }
        plan.crf = qBound(
            16,
            exportConfig.x264CrfOverride >= 0
                ? exportConfig.x264CrfOverride
                : (sizePolicy.x264Crf >= 0 ? sizePolicy.x264Crf : plan.crf),
            28);
        plan.bframes = qBound(
            0,
            exportConfig.x264BframesOverride >= 0 ? exportConfig.x264BframesOverride : plan.bframes,
            8);
        return plan;
    }
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
        exportConfig.x264CrfOverride >= 0
            ? exportConfig.x264CrfOverride
            : (sizePolicy.x264Crf >= 0 ? sizePolicy.x264Crf : plan.crf),
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
    VideoExportSizePreset sizePreset,
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
#ifdef Q_OS_MACOS
    const bool hasH264Videotoolbox = hasEncoderToken(output, QStringLiteral("h264_videotoolbox"));
    const bool hasHevcVideotoolbox = hasEncoderToken(output, QStringLiteral("hevc_videotoolbox"));
#endif
    const int safeWidth = qMax(1, outputWidth);
    const int safeHeight = qMax(1, outputHeight);
    const int safeFps = qMax(1, fps);
    const int idealThreadCount = qMax(1, QThread::idealThreadCount());
    const VideoBitratePlan bitratePlan =
        chooseVideoBitratePlan(preset, sizePreset, safeWidth, safeHeight, safeFps);
    const X264TuningPlan x264Plan =
        chooseX264TuningPlan(
            preset, sizePreset, exportConfig, memoryInfo,
            safeWidth, safeHeight, safeFps, idealThreadCount);
    const miacode::video_export::VideoExportSizePolicy sizePolicy =
        miacode::video_export::videoExportSizePolicy(sizePreset);
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
        if (codec == QLatin1String("h264_mf") && sizePolicy.usePeakConstrainedVbr) {
            item.extraArgs = bitrateArgs;
            item.extraArgs << QStringLiteral("-rate_control") << QStringLiteral("pc_vbr")
                           << QStringLiteral("-scenario") << QStringLiteral("archive");
            item.explicitBframes = 0;
            return item;
        }

#ifdef Q_OS_MACOS
        if (codec.endsWith(QLatin1String("_videotoolbox"))) {
            // VideoToolbox has no CRF mode; rate control comes from the shared
            // bitrate plan. -allow_sw 0 makes the runtime probe fail (and fall
            // back to libx264) instead of silently software-encoding inside
            // the VideoToolbox session.
            item.extraArgs = bitrateArgs;
            item.extraArgs << QStringLiteral("-allow_sw") << QStringLiteral("0");
            item.explicitBframes = isH264Codec ? 0 : -1;
            return item;
        }
#endif

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
        preset,
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
#ifdef Q_OS_MACOS
        if (hasH264Videotoolbox) {
            pushCandidate(QStringLiteral("h264_videotoolbox"), true);
        }
#endif
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
#ifdef Q_OS_MACOS
            if (hasHevcVideotoolbox) {
                pushCandidate(QStringLiteral("hevc_videotoolbox"), true);
            }
#endif
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
#ifdef Q_OS_MACOS
        if (forcedMatches(QStringLiteral("h264_videotoolbox")) && hasH264Videotoolbox) {
            pushCandidate(QStringLiteral("h264_videotoolbox"), true);
        }
        if (forcedMatches(QStringLiteral("hevc_videotoolbox")) && hasHevcVideotoolbox) {
            pushCandidate(QStringLiteral("hevc_videotoolbox"), true);
        }
#endif
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

    QString preferredHardwareCodec;
    if (autoModeEnabled() && preferHardwareFirst) {
        preferredHardwareCodec = preferredHardwareEncoder();
        const auto preferredIt = std::find_if(
            candidates.begin(),
            candidates.end(),
            [&preferredHardwareCodec](const VideoEncoderConfig& candidate) {
                return candidate.isHardware
                    && candidate.codec.compare(preferredHardwareCodec, Qt::CaseInsensitive) == 0;
            });
        if (preferredIt != candidates.end() && preferredIt != candidates.begin()) {
            std::rotate(candidates.begin(), preferredIt, std::next(preferredIt));
        }
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
            if (candidate.isHardware) {
                rememberPreferredHardwareEncoder(candidate.codec);
            }
            runtimeProbeLines.append(
                candidate.codec.compare(preferredHardwareCodec, Qt::CaseInsensitive) == 0
                    ? QStringLiteral("%1:ok(preferred_first)").arg(candidate.codec)
                    : QStringLiteral("%1:ok").arg(candidate.codec));
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
#ifdef Q_OS_MACOS
        detail += QStringLiteral(" h264_videotoolbox=%1 hevc_videotoolbox=%2")
            .arg(hasH264Videotoolbox ? 1 : 0)
            .arg(hasHevcVideotoolbox ? 1 : 0);
#endif
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

}  // namespace miacode::video_export::detail
