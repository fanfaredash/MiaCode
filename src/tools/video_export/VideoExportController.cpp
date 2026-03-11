#include "VideoExportController.h"

#include "PreviewCanvas.h"
#include "common/AssetPaths.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtMath>

#include "../../../third_party/miniaudio/miniaudio.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr double kExportLeadInSeconds = 3.0;
constexpr int kMixSampleRate = 48000;
constexpr int kMixChannels = 2;
constexpr double kTimelineEpsilonSeconds = 1e-6;

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
        QStringLiteral("(?m)^\\s*[VAS\\.]{6}\\s+%1(?:\\s|$)").arg(QRegularExpression::escape(encoderName))
    );
    return pattern.match(encodersOutput).hasMatch();
}

VideoEncoderConfig chooseVideoEncoder(
    const QString& ffmpegPath,
    int resolution,
    int fps,
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
    const bool hasLibx265 = hasEncoderToken(output, QStringLiteral("libx265"));
    const bool hasH264Nvenc = hasEncoderToken(output, QStringLiteral("h264_nvenc"));
    const bool hasH264Qsv = hasEncoderToken(output, QStringLiteral("h264_qsv"));
    const bool hasH264Amf = hasEncoderToken(output, QStringLiteral("h264_amf"));
    const bool hasLibx264 = hasEncoderToken(output, QStringLiteral("libx264"));
    const bool hasOpenH264 = hasEncoderToken(output, QStringLiteral("libopenh264"));
    const bool hasMpeg4 = hasEncoderToken(output, QStringLiteral("mpeg4"));
    const int safeResolution = qMax(1, resolution);
    const int safeFps = qMax(1, fps);
    // Keep export artifacts low at 1024x1024@60 while avoiding oversized files.
    const qint64 estimatedBitrateKbps = qBound<qint64>(
        2200LL,
        qRound64(static_cast<double>(safeResolution) * safeResolution * safeFps * 0.075 / 1000.0),
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

    if (hasHevcNvenc) {
        config.codec = QStringLiteral("hevc_nvenc");
        config.extraArgs = variableBitrateArgs();
        config.isHardware = true;
    } else if (hasHevcQsv) {
        config.codec = QStringLiteral("hevc_qsv");
        config.extraArgs = variableBitrateArgs();
        config.isHardware = true;
    } else if (hasHevcAmf) {
        config.codec = QStringLiteral("hevc_amf");
        config.extraArgs = variableBitrateArgs();
        config.isHardware = true;
    } else if (hasLibx265) {
        config.codec = QStringLiteral("libx265");
        config.extraArgs = {
            QStringLiteral("-preset"), QStringLiteral("medium"),
            QStringLiteral("-crf"), QStringLiteral("26")
        };
    } else if (hasH264Nvenc) {
        config.codec = QStringLiteral("h264_nvenc");
        config.extraArgs = variableBitrateArgs();
        config.isHardware = true;
    } else if (hasH264Qsv) {
        config.codec = QStringLiteral("h264_qsv");
        config.extraArgs = variableBitrateArgs();
        config.isHardware = true;
    } else if (hasH264Amf) {
        config.codec = QStringLiteral("h264_amf");
        config.extraArgs = variableBitrateArgs();
        config.isHardware = true;
    } else if (hasLibx264) {
        config.codec = QStringLiteral("libx264");
        config.extraArgs = {
            QStringLiteral("-preset"), QStringLiteral("medium"),
            QStringLiteral("-crf"), QStringLiteral("21")
        };
    } else if (hasOpenH264) {
        config.codec = QStringLiteral("libopenh264");
        config.extraArgs = {
            QStringLiteral("-b:v"), QString::number(estimatedBitrateKbps) + QLatin1String("k")
        };
    } else if (hasMpeg4) {
        config.codec = QStringLiteral("mpeg4");
        config.extraArgs = {
            QStringLiteral("-q:v"), QStringLiteral("4")
        };
    } else {
        config.codec = QStringLiteral("mpeg4");
        config.extraArgs = {
            QStringLiteral("-q:v"), QStringLiteral("4")
        };
    }
    if (probeLog != nullptr) {
        *probeLog = QStringLiteral(
            "encoder_probe hevc_nvenc=%1 hevc_qsv=%2 hevc_amf=%3 libx265=%4 "
            "h264_nvenc=%5 h264_qsv=%6 h264_amf=%7 libx264=%8 libopenh264=%9 mpeg4=%10 "
            "selected=%11 hw=%12 bitrateK=%13 maxrateK=%14")
            .arg(hasHevcNvenc ? 1 : 0)
            .arg(hasHevcQsv ? 1 : 0)
            .arg(hasHevcAmf ? 1 : 0)
            .arg(hasLibx265 ? 1 : 0)
            .arg(hasH264Nvenc ? 1 : 0)
            .arg(hasH264Qsv ? 1 : 0)
            .arg(hasH264Amf ? 1 : 0)
            .arg(hasLibx264 ? 1 : 0)
            .arg(hasOpenH264 ? 1 : 0)
            .arg(hasMpeg4 ? 1 : 0)
            .arg(config.codec)
            .arg(config.isHardware ? 1 : 0)
            .arg(estimatedBitrateKbps)
            .arg(maxRateKbps);
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
    const QString envPath = qEnvironmentVariable(
        "MIACODE_PREVIEW_SFX_DIR",
        qEnvironmentVariable("MAIMURI_PREVIEW_SFX_DIR")
    ).trimmed();
    if (!envPath.isEmpty() && QFileInfo::exists(QDir(envPath).filePath("answer.wav"))) {
        return normalizePath(envPath);
    }

    const QString assetSfxUpper = miacode::assets::assetPath("SFX");
    if (QFileInfo::exists(QDir(assetSfxUpper).filePath("answer.wav"))) {
        return normalizePath(assetSfxUpper);
    }
    const QString assetSfxLower = miacode::assets::assetPath("assets/SFX");
    if (QFileInfo::exists(QDir(assetSfxLower).filePath("answer.wav"))) {
        return normalizePath(assetSfxLower);
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates{
        appDir.filePath("assets/SFX"),
        appDir.filePath("SFX"),
        appDir.filePath("../Resources/assets/SFX"),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(QDir(candidate).filePath("answer.wav"))) {
            return normalizePath(candidate);
        }
    }
    return QString();
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
    if (ma_decoder_init_file(path.toUtf8().constData(), &config, &decoder) != MA_SUCCESS) {
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
    QVector<float>* mix
)
{
    if (mix == nullptr || !clip.isValid() || gain <= 0.0 || startFrame < 0) {
        return;
    }
    const qint64 totalMixFrames = mix->size() / kMixChannels;
    if (startFrame >= totalMixFrames) {
        return;
    }
    qint64 framesToMix = qMin(clip.frameCount(), totalMixFrames - startFrame);
    if (maxFrames >= 0) {
        framesToMix = qMin(framesToMix, maxFrames);
    }
    if (framesToMix <= 0) {
        return;
    }

    const float gainF = static_cast<float>(gain);
    for (qint64 frame = 0; frame < framesToMix; ++frame) {
        const qint64 mixIndex = (startFrame + frame) * kMixChannels;
        const qint64 clipIndex = frame * clip.channels;
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
    return marker.second >= startSecond - kTimelineEpsilonSeconds
        && marker.second <= endSecond + kTimelineEpsilonSeconds;
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
    events->reserve(noteMarkers.size() * 3);
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
            if (marker.isBreak) {
                addEvent(marker.second, QStringLiteral("break"));
            }
            if (marker.endSecond > marker.second) {
                addEvent(marker.endSecond, QStringLiteral("answer"), 1, -1, 0.5);
            }
            if (marker.isEx) {
                addEvent(marker.second, QStringLiteral("ex"));
                if (marker.endSecond > marker.second) {
                    addEvent(marker.endSecond, QStringLiteral("ex"));
                }
            }
            continue;
        }
        if (type == QLatin1String("touch")) {
            addEvent(marker.second, QStringLiteral("touch"));
            if (marker.isFirework) {
                addEvent(marker.second + 0.05, QStringLiteral("firework"));
            }
            continue;
        }
        if (type == QLatin1String("touch_hold")) {
            addEvent(marker.second, QStringLiteral("touch"));
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
            if (marker.hasHeadStar && !marker.sameHeadSlide) {
                addEvent(marker.second, QStringLiteral("answer"));
                if (marker.headBreak) {
                    addEvent(marker.second, QStringLiteral("break"));
                }
            }
            const double traceSecond = marker.slideTraceSecond >= 0.0 ? marker.slideTraceSecond : marker.second;
            addEvent(traceSecond, QStringLiteral("slide"));
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
    double timelineOriginSecond,
    double segmentStartSecond
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
    const auto loadClip = [&sfxDir, &clips](const QString& key, const QString& fileName) {
        DecodedClip clip;
        const QString path = QDir(sfxDir).filePath(fileName);
        if (decodeAudioClip(path, &clip)) {
            clips.insert(key, clip);
        }
    };
    loadClip(QStringLiteral("answer"), QStringLiteral("answer.wav"));
    loadClip(QStringLiteral("slide"), QStringLiteral("slide.wav"));
    loadClip(QStringLiteral("break"), QStringLiteral("break.wav"));
    loadClip(QStringLiteral("ex"), QStringLiteral("judge_ex.wav"));
    loadClip(QStringLiteral("touch"), QStringLiteral("touch.wav"));
    loadClip(QStringLiteral("touchhold"), QStringLiteral("touchHold_riser.wav"));
    loadClip(QStringLiteral("firework"), QStringLiteral("firework.wav"));

    QVector<ExportEvent> events;
    QVector<ExportTouchholdSpan> spans;
    buildSfxTimeline(noteMarkers, &events, &spans);

    const auto kindVolume = [&settings](const QString& kind) -> double {
        if (kind == QLatin1String("answer")) {
            return settings.answerVolume;
        }
        if (kind == QLatin1String("slide")) {
            return settings.slideVolume;
        }
        if (kind == QLatin1String("break")) {
            return settings.breakVolume;
        }
        if (kind == QLatin1String("ex")) {
            return settings.exVolume;
        }
        if (kind == QLatin1String("touch")) {
            return settings.touchVolume;
        }
        if (kind == QLatin1String("touchhold")) {
            return settings.touchholdVolume;
        }
        if (kind == QLatin1String("firework")) {
            return settings.fireworkVolume;
        }
        return 0.0;
    };

    const auto mixEvent = [&clips, &mix, &kindVolume, timelineOriginSecond, segmentStartSecond](const QString& kind, double gain, double second) {
        const auto it = clips.constFind(kind);
        if (it == clips.constEnd()) {
            return;
        }
        if (second + kTimelineEpsilonSeconds < segmentStartSecond) {
            return;
        }
        const double volume = kindVolume(kind);
        const double mixedGain = qMax(0.0, gain) * qMax(0.0, volume);
        if (mixedGain <= 0.0) {
            return;
        }
        const double shiftedSecond = second - timelineOriginSecond;
        if (shiftedSecond < 0.0) {
            return;
        }
        const qint64 startFrame = qRound64(shiftedSecond * kMixSampleRate);
        addClipToMix(it.value(), mixedGain, startFrame, -1, &mix);
    };

    int index = 0;
    while (index < events.size()) {
        const int groupStart = index;
        const double groupSecond = events[groupStart].second;
        int groupEnd = groupStart + 1;
        while (groupEnd < events.size()
               && qAbs(events[groupEnd].second - groupSecond) <= kTimelineEpsilonSeconds) {
            ++groupEnd;
        }

        bool hasJudge = false;
        double judgeGain = 0.0;
        for (int i = groupStart; i < groupEnd; ++i) {
            const ExportEvent& event = events.at(i);
            if (event.kind == QLatin1String("touchhold_start")
                || event.kind == QLatin1String("touchhold_stop")) {
                continue;
            }
            if (event.kind == QLatin1String("answer")) {
                hasJudge = true;
                judgeGain = qMax(judgeGain, event.gain);
                continue;
            }
            mixEvent(event.kind, event.gain, event.second);
        }
        if (hasJudge) {
            mixEvent(QStringLiteral("answer"), judgeGain, groupSecond);
        }
        index = groupEnd;
    }

    const auto touchholdIt = clips.constFind(QStringLiteral("touchhold"));
    if (touchholdIt != clips.constEnd() && settings.touchholdVolume > 0.0) {
        const DecodedClip& touchholdClip = touchholdIt.value();
        for (const ExportTouchholdSpan& span : spans) {
            if (span.endSecond <= span.startSecond) {
                continue;
            }
            if (span.startSecond + kTimelineEpsilonSeconds < segmentStartSecond) {
                continue;
            }
            const double shiftedSecond = span.startSecond - timelineOriginSecond;
            if (shiftedSecond < 0.0) {
                continue;
            }
            const qint64 startFrame = qRound64(shiftedSecond * kMixSampleRate);
            const qint64 spanFrames = qRound64((span.endSecond - span.startSecond) * kMixSampleRate);
            addClipToMix(touchholdClip, settings.touchholdVolume, startFrame, spanFrames, &mix);
        }
    }

    return writeWav16(outputPath, mix, kMixSampleRate, kMixChannels);
}

bool writePackedRgbaFrame(QProcess* process, const QImage& frame)
{
    if (process == nullptr) {
        return false;
    }
    QImage rgba = frame;
    if (rgba.format() != QImage::Format_RGBA8888) {
        rgba = frame.convertToFormat(QImage::Format_RGBA8888);
    }

    const int width = rgba.width();
    const int height = rgba.height();
    const int packedStride = width * 4;
    const auto writeAll = [process](const char* data, qint64 size) -> bool {
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
    };
    if (rgba.bytesPerLine() == packedStride) {
        const qint64 expectedBytes = static_cast<qint64>(packedStride) * height;
        return writeAll(reinterpret_cast<const char*>(rgba.constBits()), expectedBytes);
    }

    QByteArray packed;
    packed.resize(packedStride * height);
    for (int y = 0; y < height; ++y) {
        std::memcpy(
            packed.data() + y * packedStride,
            rgba.constScanLine(y),
            static_cast<size_t>(packedStride)
        );
    }
    return writeAll(packed.constData(), packed.size());
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
    VideoExportResult result;
    QElapsedTimer exportTimer;
    exportTimer.start();
    appendVideoExportLog(
        QStringLiteral("export_begin"),
        QStringLiteral("output=%1 chart=%2 track=%3 notes=%4 start=%5 duration=%6 resolution=%7 fps=%8")
            .arg(task.outputPath, task.chartPath, task.trackPath)
            .arg(task.noteMarkers.size())
            .arg(task.exportStartSeconds, 0, 'f', 6)
            .arg(task.contentDurationSeconds, 0, 'f', 6)
            .arg(task.resolution)
            .arg(task.fps)
    );
    if (sourceCanvas == nullptr) {
        result.message = QStringLiteral("Preview canvas is not available.");
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
    if (task.resolution <= 0 || task.fps <= 0) {
        result.message = QStringLiteral("Export parameters are invalid.");
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

    const auto setProgressPercent = [progress](int percent, const QString& text) {
        if (progress == nullptr) {
            return false;
        }
        progress->setMaximum(100);
        progress->setValue(qBound(0, percent, 100));
        progress->setLabelText(text);
        QCoreApplication::processEvents();
        return progress->wasCanceled();
    };

    const double segmentStartSecond = qMax(0.0, task.exportStartSeconds);
    const double segmentDurationSeconds = task.contentDurationSeconds;
    const double segmentEndSecond = segmentStartSecond + segmentDurationSeconds;
    const double timelineOriginSecond = segmentStartSecond - kExportLeadInSeconds;
    const double totalSeconds = kExportLeadInSeconds + segmentDurationSeconds;
    const int frameCount = qMax(1, qCeil(totalSeconds * task.fps));
    const int resolution = task.resolution;
    const QSize frameSize(resolution, resolution);
    const QString mediaPath = resolveBackgroundMediaPath(task.chartPath);
    const bool hasMedia = !mediaPath.isEmpty();
    const bool mediaIsImage = hasMedia && isImageMediaPath(mediaPath);
    const QString trackPath = (task.trackPath.isEmpty() || !QFileInfo::exists(task.trackPath))
        ? QString()
        : normalizePath(task.trackPath);
    const bool hasTrack = !trackPath.isEmpty();
    const QVector<TimelineNoteMarker> exportMarkers =
        filteredMarkersForRange(task.noteMarkers, segmentStartSecond, segmentEndSecond);
    appendVideoExportLog(
        QStringLiteral("input_probe"),
        QStringLiteral("media=%1 hasMedia=%2 mediaIsImage=%3 track=%4 hasTrack=%5 segmentStart=%6 segmentEnd=%7 timelineOrigin=%8 totalSeconds=%9 frameCount=%10")
            .arg(mediaPath)
            .arg(hasMedia ? 1 : 0)
            .arg(mediaIsImage ? 1 : 0)
            .arg(trackPath)
            .arg(hasTrack ? 1 : 0)
            .arg(segmentStartSecond, 0, 'f', 6)
            .arg(segmentEndSecond, 0, 'f', 6)
            .arg(timelineOriginSecond, 0, 'f', 6)
            .arg(totalSeconds, 0, 'f', 6)
            .arg(frameCount)
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

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        result.message = QStringLiteral("Unable to create temporary directory.");
        result.details = withExportLogPath(result.details);
        appendVideoExportLog(QStringLiteral("fail_temp_dir"), result.message);
        return result;
    }

    const QString sfxWavPath = QDir(tempDir.path()).filePath(QStringLiteral("export_sfx.wav"));
    if (!mixSfxTrackToWav(
            sfxWavPath,
            exportMarkers,
            task.audioSettings,
            totalSeconds,
            timelineOriginSecond,
            segmentStartSecond)) {
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
         << QStringLiteral("%1x%2").arg(resolution).arg(resolution)
         << QStringLiteral("-framerate")
         << QString::number(task.fps)
         << QStringLiteral("-i")
         << QStringLiteral("pipe:0");

    int mediaInputIndex = -1;
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
    if (hasTrack) {
        bgmInputIndex = currentInputIndex++;
        args << QStringLiteral("-i") << trackPath;
    }
    sfxInputIndex = currentInputIndex++;
    args << QStringLiteral("-i") << sfxWavPath;

    const QString totalSecondsText = QString::number(totalSeconds, 'f', 6);
    const QString timelineOriginText = QString::number(timelineOriginSecond, 'f', 6);
    QStringList filterParts;
    filterParts << QStringLiteral("color=c=#1F2833:s=%1x%1:d=%2[base_fill]")
                       .arg(resolution)
                       .arg(totalSecondsText);
    if (hasMedia) {
        QString mediaChain =
            QStringLiteral("[%1:v]scale=%2:%2:force_original_aspect_ratio=increase,crop=%2:%2,setsar=1,fps=%3,format=rgba")
                .arg(mediaInputIndex)
                .arg(resolution)
                .arg(task.fps);
        if (!mediaIsImage) {
            if (timelineOriginSecond > kTimelineEpsilonSeconds) {
                mediaChain += QStringLiteral(",trim=start=%1:end=%2,setpts=PTS-STARTPTS")
                    .arg(timelineOriginText)
                    .arg(QString::number(timelineOriginSecond + totalSeconds, 'f', 6));
            } else if (timelineOriginSecond < -kTimelineEpsilonSeconds) {
                mediaChain += QStringLiteral(",trim=start=0:end=%1,setpts=PTS-STARTPTS+%2/TB")
                    .arg(QString::number(totalSeconds + timelineOriginSecond, 'f', 6))
                    .arg(QString::number(-timelineOriginSecond, 'f', 6));
            }
            mediaChain += QStringLiteral(",tpad=stop_mode=clone:stop_duration=%1").arg(totalSecondsText);
        }
        mediaChain += QStringLiteral("[media_src]");
        filterParts << mediaChain;
        filterParts << QStringLiteral("[base_fill][media_src]overlay=0:0:format=auto[base_media]");
    } else {
        filterParts << QStringLiteral("[base_fill]null[base_media]");
    }

    const double dimAlpha = qBound(0.0, 1.0 - task.backgroundBrightness, 1.0);
    if (dimAlpha > 1e-6) {
        filterParts << QStringLiteral("color=c=black@%1:s=%2x%2:d=%3[dim]")
                           .arg(QString::number(dimAlpha, 'f', 6))
                           .arg(resolution)
                           .arg(totalSecondsText);
        filterParts << QStringLiteral("[base_media][dim]overlay=0:0[base]");
    } else {
        filterParts << QStringLiteral("[base_media]null[base]");
    }

    filterParts << QStringLiteral("[0:v]vflip[overlay_src]");
    filterParts << QStringLiteral("[base][overlay_src]overlay=0:0:format=auto[vout]");
    filterParts << QStringLiteral("[%1:a]atrim=0:%2,asetpts=PTS-STARTPTS,aresample=%3[sfx]")
                       .arg(sfxInputIndex)
                       .arg(totalSecondsText)
                       .arg(kMixSampleRate);
    if (hasTrack) {
        if (timelineOriginSecond > kTimelineEpsilonSeconds) {
            filterParts << QStringLiteral("[%1:a]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS,aresample=%4,volume=%5[bgm]")
                               .arg(bgmInputIndex)
                               .arg(timelineOriginText)
                               .arg(QString::number(timelineOriginSecond + totalSeconds, 'f', 6))
                               .arg(kMixSampleRate)
                               .arg(QString::number(task.audioSettings.bgmVolume, 'f', 6));
        } else if (timelineOriginSecond < -kTimelineEpsilonSeconds) {
            const int delayMs = qMax(0, qRound(-timelineOriginSecond * 1000.0));
            filterParts << QStringLiteral("[%1:a]atrim=start=0:end=%2,asetpts=PTS-STARTPTS,adelay=%3|%3,aresample=%4,volume=%5[bgm]")
                               .arg(bgmInputIndex)
                               .arg(QString::number(totalSeconds + timelineOriginSecond, 'f', 6))
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

    QString encoderProbeLog;
    const VideoEncoderConfig encoderConfig = chooseVideoEncoder(ffmpegPath, resolution, task.fps, &encoderProbeLog);
    appendVideoExportLog(QStringLiteral("encoder_select"), encoderProbeLog);

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
    args << encoderConfig.extraArgs;
    args << QStringLiteral("-movflags")
         << QStringLiteral("+faststart")
         << task.outputPath;
    appendVideoExportLog(
        QStringLiteral("ffmpeg_args"),
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
    appendVideoExportLog(QStringLiteral("ffmpeg_started"));

    PreviewCanvas exportCanvas;
    exportCanvas.copyRenderStateFrom(*sourceCanvas);
    exportCanvas.setBackgroundBrightness(task.backgroundBrightness);
    exportCanvas.setShowDebugInfo(false);
    exportCanvas.setNoteMarkers(exportMarkers);
    QOpenGLContext* shareContext = sourceCanvas->context();
    QString offscreenInitError;
    bool useOffscreenGpu = exportCanvas.initializeOffscreenRenderer(
        sourceCanvas->format(),
        shareContext,
        &offscreenInitError
    );
    appendVideoExportLog(
        QStringLiteral("render_backend"),
        QStringLiteral("sourceGpuReady=%1 sourceCtx=%2 offscreenInit=%3 exportGpuReady=%4 initError=%5")
            .arg(sourceCanvas->isGpuRendererReadyForDebug() ? 1 : 0)
            .arg(shareContext != nullptr ? 1 : 0)
            .arg(useOffscreenGpu ? 1 : 0)
            .arg(exportCanvas.isGpuRendererReadyForDebug() ? 1 : 0)
            .arg(offscreenInitError.isEmpty() ? QStringLiteral("ok") : offscreenInitError)
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
    static constexpr int kFrameProgressStride = 120;
    FrameTimingStats frameStats;
    quint64 previousSignature = 0;
    bool hasPreviousSignature = false;
    int repeatRunStartFrame = 0;
    int repeatRunLength = 1;
    QElapsedTimer frameTimer;

    if (useOffscreenGpu) {
        frameTimer.start();
        const QImage warmupFrame = exportCanvas.renderOverlayFrameOffscreen(frameSize, timelineOriginSecond, task.showTimestamp);
        const qint64 warmupNs = frameTimer.nsecsElapsed();
        appendVideoExportLog(
            QStringLiteral("offscreen_warmup"),
            QStringLiteral("ok=%1 renderMs=%2 drawMs=%3 readMs=%4")
                .arg(warmupFrame.isNull() ? 0 : 1)
                .arg(warmupNs / 1000000.0, 0, 'f', 3)
                .arg(exportCanvas.offscreenDrawNsLastFrameForDebug() / 1000000.0, 0, 'f', 3)
                .arg(exportCanvas.offscreenReadbackNsLastFrameForDebug() / 1000000.0, 0, 'f', 3)
        );
    }

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
        if (progress != nullptr && progress->wasCanceled()) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("canceled");
            result.details = withExportLogPath(result.details);
            appendVideoExportLog(QStringLiteral("canceled"), QStringLiteral("stage=frame_loop frame=%1").arg(frameIndex));
            return result;
        }
        const double exportSecond = timelineOriginSecond + static_cast<double>(frameIndex) / task.fps;
        frameTimer.start();
        bool usedOffscreenPath = false;
        QImage frame;
        if (useOffscreenGpu) {
            frame = exportCanvas.renderOverlayFrameOffscreen(frameSize, exportSecond, task.showTimestamp);
            if (!frame.isNull()) {
                usedOffscreenPath = true;
            } else {
                appendVideoExportLog(
                    QStringLiteral("render_backend_fallback"),
                    QStringLiteral("frame=%1 reason=offscreen_render_failed").arg(frameIndex)
                );
                exportCanvas.shutdownOffscreenRenderer();
                useOffscreenGpu = false;
            }
        }
        if (frame.isNull()) {
            frame = exportCanvas.renderOverlayFrame(frameSize, exportSecond, task.showTimestamp);
        }
        const qint64 renderNs = frameTimer.nsecsElapsed();
        const qint64 offscreenDrawNs = exportCanvas.offscreenDrawNsLastFrameForDebug();
        const qint64 offscreenReadbackNs = exportCanvas.offscreenReadbackNsLastFrameForDebug();
        if (frame.isNull()) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("Render frame failed.");
            result.details = withExportLogPath(QStringLiteral("frame image is null"));
            appendVideoExportLog(
                QStringLiteral("fail_render_frame"),
                QStringLiteral("frame=%1 offscreen=%2").arg(frameIndex).arg(usedOffscreenPath ? 1 : 0)
            );
            return result;
        }
        if (exportCanvas.usedGpuRendererLastFrameForDebug()) {
            ++frameStats.gpuRenderedFrames;
        }
        const int fallbackCount = exportCanvas.cpuFallbackCountLastFrameForDebug();
        frameStats.cpuFallbackTotal += qMax(0, fallbackCount);
        if (fallbackCount > frameStats.cpuFallbackMax) {
            frameStats.cpuFallbackMax = fallbackCount;
            frameStats.cpuFallbackMaxFrame = frameIndex;
        }

        frameTimer.restart();
        if (!writePackedRgbaFrame(&ffmpeg, frame)) {
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
                QStringLiteral("frame=%1 error=%2 output=%3")
                    .arg(frameIndex)
                    .arg(ffmpeg.errorString())
                    .arg(truncateForLog(ffmpegOutput, 1000))
            );
            return result;
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
                    .arg(exportCanvas.usedGpuRendererLastFrameForDebug() ? 1 : 0)
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
            return result;
        }
        if (frameIndex == 0 || ((frameIndex + 1) % 300) == 0 || frameIndex + 1 == frameCount) {
            appendVideoExportLog(
                QStringLiteral("frame_progress"),
                QStringLiteral("written=%1/%2").arg(frameIndex + 1).arg(frameCount)
            );
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

    ffmpeg.closeWriteChannel();
    appendVideoExportLog(QStringLiteral("ffmpeg_finalize_wait_begin"));
    while (ffmpeg.state() != QProcess::NotRunning) {
        if (setProgressPercent(90, QStringLiteral("Finalizing encoded video stream..."))) {
            ffmpeg.kill();
            ffmpeg.waitForFinished(2000);
            result.message = QStringLiteral("canceled");
            result.details = withExportLogPath(result.details);
            appendVideoExportLog(QStringLiteral("canceled"), QStringLiteral("stage=ffmpeg_finalize_wait"));
            return result;
        }
        ffmpeg.waitForFinished(80);
    }
    appendVideoExportLog(
        QStringLiteral("ffmpeg_finalize_wait_done"),
        QStringLiteral("exitStatus=%1 exitCode=%2").arg(static_cast<int>(ffmpeg.exitStatus())).arg(ffmpeg.exitCode())
    );
    if (ffmpeg.exitStatus() != QProcess::NormalExit) {
        result.message = QStringLiteral("ffmpeg process failed.");
        const QString ffmpegOutput = truncateForLog(QString::fromUtf8(ffmpeg.readAllStandardOutput()).trimmed());
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

    const QString ffmpegOutput = truncateForLog(QString::fromUtf8(ffmpeg.readAllStandardOutput()).trimmed());
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
