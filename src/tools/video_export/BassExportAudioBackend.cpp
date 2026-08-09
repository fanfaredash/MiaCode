#include "BassExportAudioBackend.h"

#include "common/DebugLog.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewSfxAssets.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtMath>

#include <memory>
#include <vector>

#ifdef MIACODE_HAS_BASS_AUDIO
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "bass.h"
#include "bassmix.h"
#endif

namespace {

using miacode::preview_audio::kMixChannels;
using miacode::preview_audio::kMixSampleRate;

void appendExportLog(const QString& stage, const QString& detail = QString())
{
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Export, stage, detail);
}

QString runtimeFilePath(const QString& fileName)
{
#ifdef Q_OS_MACOS
    return QDir::cleanPath(
        QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("../Frameworks/%1").arg(fileName)));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
#endif
}

bool runtimeLibraryExists(const QString& fileName)
{
    return QFileInfo::exists(runtimeFilePath(fileName));
}

class Pcm16WavWriter
{
public:
    bool open(const QString& path, int sampleRate, int channels, qint64 totalFrames)
    {
        if (path.isEmpty() || sampleRate <= 0 || channels <= 0 || totalFrames <= 0) {
            return false;
        }

        file_.setFileName(path);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return false;
        }

        stream_.setDevice(&file_);
        stream_.setByteOrder(QDataStream::LittleEndian);

        const quint64 totalSamples = static_cast<quint64>(totalFrames) * static_cast<quint64>(channels);
        const quint32 dataBytes = static_cast<quint32>(totalSamples * sizeof(qint16));
        const quint32 riffChunkSize = 36u + dataBytes;
        const quint16 bitsPerSample = 16;
        const quint16 blockAlign = static_cast<quint16>(channels * (bitsPerSample / 8));
        const quint32 byteRate = static_cast<quint32>(sampleRate * blockAlign);

        stream_.writeRawData("RIFF", 4);
        stream_ << riffChunkSize;
        stream_.writeRawData("WAVE", 4);
        stream_.writeRawData("fmt ", 4);
        stream_ << quint32(16);
        stream_ << quint16(1);
        stream_ << quint16(channels);
        stream_ << quint32(sampleRate);
        stream_ << byteRate;
        stream_ << blockAlign;
        stream_ << bitsPerSample;
        stream_.writeRawData("data", 4);
        stream_ << dataBytes;
        return true;
    }

    bool append(const float* samples, int sampleCount)
    {
        if (samples == nullptr || sampleCount <= 0 || !file_.isOpen()) {
            return false;
        }

        constexpr int kPcmChunkSamples = 16384;
        int offset = 0;
        QByteArray pcmBytes;
        pcmBytes.resize(kPcmChunkSamples * static_cast<int>(sizeof(qint16)));
        while (offset < sampleCount) {
            const int chunkSamples = qMin(kPcmChunkSamples, sampleCount - offset);
            const int chunkBytes = chunkSamples * static_cast<int>(sizeof(qint16));
            qint16* out = reinterpret_cast<qint16*>(pcmBytes.data());
            for (int i = 0; i < chunkSamples; ++i) {
                const float clamped = qBound(-1.0f, samples[offset + i], 1.0f);
                out[i] = static_cast<qint16>(qRound(clamped * 32767.0f));
            }
            if (file_.write(pcmBytes.constData(), chunkBytes) != chunkBytes) {
                return false;
            }
            offset += chunkSamples;
        }
        return true;
    }

private:
    QFile file_;
    QDataStream stream_;
};

#ifdef MIACODE_HAS_BASS_AUDIO
struct ScheduledSource {
    DWORD stream = 0;

    ~ScheduledSource()
    {
        if (stream != 0) {
            BASS_StreamFree(stream);
        }
    }
};
#endif

}  // namespace

namespace miacode::video_export {

BassExportAudioBackend::BassExportAudioBackend() = default;

BassExportAudioBackend::~BassExportAudioBackend()
{
    shutdownBass();
}

QString BassExportAudioBackend::backendId() const
{
    return QStringLiteral("bass_offline_mixer");
}

bool BassExportAudioBackend::runtimeLibrariesPresent() const
{
#if defined(Q_OS_WIN)
    return runtimeLibraryExists(QStringLiteral("bass.dll"))
        && runtimeLibraryExists(QStringLiteral("bassmix.dll"));
#elif defined(Q_OS_MACOS) && defined(MIACODE_HAS_BASS_AUDIO)
    return runtimeLibraryExists(QStringLiteral("libbass.dylib"))
        && runtimeLibraryExists(QStringLiteral("libbassmix.dylib"))
        && runtimeLibraryExists(QStringLiteral("libbassopus.dylib"));
#else
    return false;
#endif
}

bool BassExportAudioBackend::isSupported(QString* reason) const
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!runtimeLibrariesPresent()) {
        if (reason != nullptr) {
            *reason = QStringLiteral("BASS runtime libraries are missing");
        }
        return false;
    }
    if (reason != nullptr) {
        *reason = QStringLiteral("BASS export backend is available");
    }
    return true;
#else
    if (reason != nullptr) {
        *reason = QStringLiteral("BASS export backend is unavailable in this build");
    }
    return false;
#endif
}

bool BassExportAudioBackend::initializeBass(QString* errorMessage)
{
#ifndef MIACODE_HAS_BASS_AUDIO
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("BASS export backend is unavailable");
    }
    return false;
#else
    if (bassDeviceLease_.acquired()) {
        return true;
    }
    bassDeviceLease_ = miacode::preview_audio::PreviewBassDeviceLease::acquire({
        [] { return static_cast<miacode::preview_audio::BassDeviceLeaseApi::DeviceId>(BASS_GetDevice()); },
        [] { return BASS_Init(0, kMixSampleRate, BASS_DEVICE_NOSPEAKER, nullptr, nullptr) != FALSE; },
        [] { BASS_Free(); },
    });
    if (!bassDeviceLease_.acquired()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("BASS_Init failed err=%1").arg(static_cast<int>(BASS_ErrorGetCode()));
        }
        return false;
    }
    return true;
#endif
}

void BassExportAudioBackend::shutdownBass()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    bassDeviceLease_.release();
#endif
}

bool BassExportAudioBackend::renderMixedTrackToWav(
    const VideoExportAudioRenderPlan& plan,
    const QString& outputPath,
    QString* errorMessage
)
{
#ifndef MIACODE_HAS_BASS_AUDIO
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("BASS export backend is unavailable");
    }
    return false;
#else
    QString supportReason;
    if (!isSupported(&supportReason)) {
        if (errorMessage != nullptr) {
            *errorMessage = supportReason;
        }
        return false;
    }
    if (!initializeBass(errorMessage)) {
        return false;
    }

    HPLUGIN pluginAac = 0;
    HPLUGIN pluginOpus = 0;
#ifdef Q_OS_WIN
    const QString aacPath = runtimeFilePath(QStringLiteral("bass_aac.dll"));
    if (QFileInfo::exists(aacPath)) {
        pluginAac = BASS_PluginLoad(reinterpret_cast<const WCHAR*>(aacPath.utf16()), 0);
    }
    const QString opusPath = runtimeFilePath(QStringLiteral("bassopus.dll"));
    if (QFileInfo::exists(opusPath)) {
        pluginOpus = BASS_PluginLoad(reinterpret_cast<const WCHAR*>(opusPath.utf16()), 0);
    }
#elif defined(Q_OS_MACOS)
    const QString opusPath = runtimeFilePath(QStringLiteral("libbassopus.dylib"));
    if (QFileInfo::exists(opusPath)) {
        const QByteArray encodedOpusPath = QFile::encodeName(opusPath);
        pluginOpus = BASS_PluginLoad(encodedOpusPath.constData(), 0);
    }
#endif

    const DWORD mixerFlags = BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_MIXER_NONSTOP | BASS_MIXER_NOSPEAKER;
    const HSTREAM masterMixer = BASS_Mixer_StreamCreate(kMixSampleRate, kMixChannels, mixerFlags);
    if (masterMixer == 0) {
        if (pluginOpus != 0) {
            BASS_PluginFree(pluginOpus);
        }
        if (pluginAac != 0) {
            BASS_PluginFree(pluginAac);
        }
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("BASS_Mixer_StreamCreate failed err=%1")
                .arg(static_cast<int>(BASS_ErrorGetCode()));
        }
        return false;
    }

    auto cleanupPlugins = [&]() {
        if (pluginOpus != 0) {
            BASS_PluginFree(pluginOpus);
            pluginOpus = 0;
        }
        if (pluginAac != 0) {
            BASS_PluginFree(pluginAac);
            pluginAac = 0;
        }
    };

    std::vector<std::unique_ptr<ScheduledSource>> sources;
    auto addScheduledFile = [&](const QString& path,
                                double mixStartSecond,
                                double sourceStartSecond,
                                double durationSeconds,
                                double gain,
                                const QString& tag) -> bool {
        if (path.isEmpty() || !QFileInfo::exists(path) || gain <= 0.0 || durationSeconds <= 0.0) {
            return true;
        }

        const DWORD sourceFlags = BASS_STREAM_DECODE | BASS_STREAM_PRESCAN;
#ifdef Q_OS_WIN
        HSTREAM stream = BASS_StreamCreateFile(FALSE, reinterpret_cast<const WCHAR*>(path.utf16()), 0, 0, sourceFlags);
#else
        const QByteArray encodedPath = QFile::encodeName(path);
        HSTREAM stream = BASS_StreamCreateFile(FALSE, encodedPath.constData(), 0, 0, sourceFlags);
#endif
        if (stream == 0) {
            appendExportLog(
                QStringLiteral("audio_backend_source_skip"),
                QStringLiteral("backend=%1 tag=%2 path=%3 err=%4")
                    .arg(backendId())
                    .arg(tag)
                    .arg(path)
                    .arg(static_cast<int>(BASS_ErrorGetCode())));
            return true;
        }

        double effectiveGain = gain;
        if (tag == QStringLiteral("bgm")) {
            const QWORD scanLength = BASS_ChannelGetLength(stream, BASS_POS_BYTE);
            double peak = 0.0;
            if (scanLength != static_cast<QWORD>(-1) && scanLength > 0) {
                QWORD lastPos = 0;
                for (int iter = 0; iter < 200000; ++iter) {
                    const QWORD pos = BASS_ChannelGetPosition(stream, BASS_POS_BYTE);
                    if (pos == static_cast<QWORD>(-1) || pos >= scanLength) {
                        break;
                    }
                    if (iter > 0 && pos == lastPos) {
                        break;
                    }
                    lastPos = pos;
                    const DWORD level = BASS_ChannelGetLevel(stream);
                    if (level == static_cast<DWORD>(-1)) {
                        break;
                    }
                    const double leftNorm = static_cast<double>(LOWORD(level)) / 32768.0;
                    const double rightNorm = static_cast<double>(HIWORD(level)) / 32768.0;
                    const double maxNorm = qMax(leftNorm, rightNorm);
                    if (maxNorm > peak) {
                        peak = maxNorm;
                    }
                }
            }
            constexpr double kBgmNormalizeMaxBoost = 4.0;
            constexpr double kBgmNormalizePeakFloor = 1.0e-4;
            const double normalizationGain =
                qMin(kBgmNormalizeMaxBoost, 1.0 / qMax(peak, kBgmNormalizePeakFloor));
            effectiveGain = gain * normalizationGain;
            appendExportLog(
                QStringLiteral("bgm_normalize_peak"),
                QStringLiteral("peak=%1 norm_gain=%2 plan_gain=%3 effective=%4 path=%5")
                    .arg(peak, 0, 'f', 6)
                    .arg(normalizationGain, 0, 'f', 4)
                    .arg(gain, 0, 'f', 4)
                    .arg(effectiveGain, 0, 'f', 4)
                    .arg(path));
        }

        if (sourceStartSecond > 0.0) {
            const QWORD sourcePosition = BASS_ChannelSeconds2Bytes(stream, sourceStartSecond);
            if (!BASS_ChannelSetPosition(stream, sourcePosition, BASS_POS_BYTE)) {
                BASS_StreamFree(stream);
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("BASS_ChannelSetPosition failed tag=%1 err=%2")
                        .arg(tag)
                        .arg(static_cast<int>(BASS_ErrorGetCode()));
                }
                return false;
            }
        } else if (tag == QStringLiteral("bgm")) {
            BASS_ChannelSetPosition(stream, 0, BASS_POS_BYTE);
        }

        BASS_ChannelSetAttribute(stream, BASS_ATTRIB_VOL, static_cast<float>(qBound(0.0, effectiveGain, 2.0)));
        const QWORD mixStartBytes = BASS_ChannelSeconds2Bytes(masterMixer, mixStartSecond);
        const QWORD mixLengthBytes = BASS_ChannelSeconds2Bytes(masterMixer, durationSeconds);
        if (!BASS_Mixer_StreamAddChannelEx(
                masterMixer,
                stream,
                BASS_MIXER_CHAN_ABSOLUTE,
                mixStartBytes,
                mixLengthBytes)) {
            const int bassError = static_cast<int>(BASS_ErrorGetCode());
            BASS_StreamFree(stream);
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("BASS_Mixer_StreamAddChannelEx failed tag=%1 err=%2")
                    .arg(tag)
                    .arg(bassError);
            }
            return false;
        }

        auto source = std::make_unique<ScheduledSource>();
        source->stream = stream;
        sources.push_back(std::move(source));
        return true;
    };

    if (plan.backgroundTrack.enabled) {
        if (!addScheduledFile(
                plan.backgroundTrack.path,
                plan.backgroundTrack.mixStartSecond,
                plan.backgroundTrack.sourceStartSecond,
                plan.backgroundTrack.durationSeconds,
                plan.backgroundTrack.gain,
                QStringLiteral("bgm"))) {
            BASS_StreamFree(masterMixer);
            cleanupPlugins();
            return false;
        }
    }

    for (int index = 0; index < plan.scheduledSfxPlaybacks.size(); ++index) {
        const auto& playback = plan.scheduledSfxPlaybacks.at(index);
        const QString path = miacode::preview_sfx::assetFilePathForKind(plan.sfxDirectory, playback.assetKind);
        const double durationSeconds = playback.maxDurationSeconds >= 0.0
            ? playback.maxDurationSeconds
            : plan.alignedTotalSeconds;
        if (!addScheduledFile(
                path,
                playback.mixSecond,
                0.0,
                durationSeconds,
                playback.gain,
                QStringLiteral("sfx:%1:%2").arg(index).arg(playback.kind))) {
            BASS_StreamFree(masterMixer);
            cleanupPlugins();
            return false;
        }
    }

    for (int index = 0; index < plan.mergedTouchholdSpans.size(); ++index) {
        const auto& span = plan.mergedTouchholdSpans.at(index);
        const QString path = miacode::preview_sfx::assetFilePathForKind(plan.sfxDirectory, span.assetKind);
        if (!addScheduledFile(
                path,
                span.mixSecond,
                0.0,
                span.durationSeconds,
                span.gain,
                QStringLiteral("touchhold:%1").arg(index))) {
            BASS_StreamFree(masterMixer);
            cleanupPlugins();
            return false;
        }
    }

    const qint64 totalFrames = qMax<qint64>(1, qCeil(plan.alignedTotalSeconds * kMixSampleRate));
    Pcm16WavWriter writer;
    if (!writer.open(outputPath, kMixSampleRate, kMixChannels, totalFrames)) {
        BASS_StreamFree(masterMixer);
        cleanupPlugins();
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to open export wav for writing");
        }
        return false;
    }

    constexpr qint64 kRenderChunkFrames = 4096;
    QVector<float> buffer;
    buffer.resize(static_cast<int>(kRenderChunkFrames * kMixChannels));
    qint64 framesRendered = 0;
    while (framesRendered < totalFrames) {
        const qint64 framesToRead = qMin(kRenderChunkFrames, totalFrames - framesRendered);
        const DWORD bytesToRead = static_cast<DWORD>(framesToRead * kMixChannels * sizeof(float));
        const DWORD bytesRead = BASS_ChannelGetData(masterMixer, buffer.data(), bytesToRead | BASS_DATA_FLOAT);
        if (bytesRead == static_cast<DWORD>(-1)) {
            BASS_StreamFree(masterMixer);
            cleanupPlugins();
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("BASS_ChannelGetData failed err=%1")
                    .arg(static_cast<int>(BASS_ErrorGetCode()));
            }
            return false;
        }

        const int samplesRead = static_cast<int>(bytesRead / sizeof(float));
        const int expectedSamples = static_cast<int>(framesToRead * kMixChannels);
        if (samplesRead < expectedSamples) {
            std::fill(buffer.begin() + samplesRead, buffer.begin() + expectedSamples, 0.0f);
        }
        if (!writer.append(buffer.constData(), expectedSamples)) {
            BASS_StreamFree(masterMixer);
            cleanupPlugins();
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("failed to write rendered audio block");
            }
            return false;
        }
        framesRendered += framesToRead;
    }

    BASS_StreamFree(masterMixer);
    cleanupPlugins();
    appendExportLog(
        QStringLiteral("audio_backend_render_complete"),
        QStringLiteral("backend=%1 mode=bass output=%2 bgm=%3 sfx=%4 touchhold=%5 frames=%6 seconds=%7")
            .arg(backendId())
            .arg(outputPath)
            .arg(plan.backgroundTrack.enabled ? 1 : 0)
            .arg(plan.scheduledSfxPlaybacks.size())
            .arg(plan.mergedTouchholdSpans.size())
            .arg(totalFrames)
            .arg(plan.alignedTotalSeconds, 0, 'f', 6));
    return true;
#endif
}

}  // namespace miacode::video_export
