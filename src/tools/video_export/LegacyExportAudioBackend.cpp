#include "LegacyExportAudioBackend.h"

#include "common/DebugLog.h"
#include "common/MiniaudioFileAccess.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewSfxAssets.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QtMath>

#include <algorithm>

namespace {

using miacode::preview_audio::kMixChannels;
using miacode::preview_audio::kMixSampleRate;

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

void appendExportLog(const QString& stage, const QString& detail = QString())
{
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Export, stage, detail);
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
    if (miacode::audio_io::decoderInitFile(path, &config, &decoder) != MA_SUCCESS) {
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
    if (startFrame >= totalMixFrames || clipStartFrame >= clip.frameCount()) {
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
        const float left = clip.samples[clipIndex];
        const float right = clip.channels >= 2 ? clip.samples[clipIndex + 1] : left;
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

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    const quint32 dataBytes =
        static_cast<quint32>(samples.size() * static_cast<int>(sizeof(qint16)));
    const quint32 riffChunkSize = 36u + dataBytes;
    const quint16 bitsPerSample = 16;
    const quint16 blockAlign = static_cast<quint16>(channels * (bitsPerSample / 8));
    const quint32 byteRate = static_cast<quint32>(sampleRate * blockAlign);

    stream.writeRawData("RIFF", 4);
    stream << riffChunkSize;
    stream.writeRawData("WAVE", 4);
    stream.writeRawData("fmt ", 4);
    stream << quint32(16);
    stream << quint16(1);
    stream << quint16(channels);
    stream << quint32(sampleRate);
    stream << byteRate;
    stream << blockAlign;
    stream << bitsPerSample;
    stream.writeRawData("data", 4);
    stream << dataBytes;

    constexpr int kPcmChunkSamples = 16384;
    QByteArray pcmBytes;
    pcmBytes.resize(kPcmChunkSamples * static_cast<int>(sizeof(qint16)));
    int sampleIndex = 0;
    while (sampleIndex < samples.size()) {
        const int chunkSamples = qMin(kPcmChunkSamples, samples.size() - sampleIndex);
        const int chunkBytes = chunkSamples * static_cast<int>(sizeof(qint16));
        qint16* out = reinterpret_cast<qint16*>(pcmBytes.data());
        for (int i = 0; i < chunkSamples; ++i) {
            const float clamped = qBound(-1.0f, samples.at(sampleIndex + i), 1.0f);
            out[i] = static_cast<qint16>(qRound(clamped * 32767.0f));
        }
        if (file.write(pcmBytes.constData(), chunkBytes) != chunkBytes) {
            return false;
        }
        sampleIndex += chunkSamples;
    }
    return true;
}

}  // namespace

namespace miacode::video_export {

QString LegacyExportAudioBackend::backendId() const
{
    return QStringLiteral("legacy_miniaudio");
}

bool LegacyExportAudioBackend::isSupported(QString* reason) const
{
    if (reason != nullptr) {
        *reason = QStringLiteral("legacy export audio backend");
    }
    return true;
}

bool LegacyExportAudioBackend::renderMixedTrackToWav(
    const VideoExportAudioRenderPlan& plan,
    const QString& outputPath,
    QString* errorMessage
)
{
    const qint64 totalFrames = qMax<qint64>(1, qCeil(plan.alignedTotalSeconds * kMixSampleRate));
    QVector<float> mix;
    mix.fill(0.0f, static_cast<int>(totalFrames * kMixChannels));

    QHash<QString, DecodedClip> clips;
    const auto loadClip = [&clips](const QString& key, const QString& path) -> const DecodedClip* {
        const auto it = clips.constFind(key);
        if (it != clips.constEnd()) {
            return &it.value();
        }
        DecodedClip clip;
        if (!decodeAudioClip(path, &clip)) {
            return nullptr;
        }
        return &clips.insert(key, clip).value();
    };

    if (plan.backgroundTrack.enabled) {
        if (const DecodedClip* clip = loadClip(QStringLiteral("bgm"), plan.backgroundTrack.path)) {
            const qint64 startFrame = qRound64(plan.backgroundTrack.mixStartSecond * kMixSampleRate);
            const qint64 maxFrames = qMax<qint64>(0, qRound64(plan.backgroundTrack.durationSeconds * kMixSampleRate));
            const qint64 clipStartFrame =
                qMax<qint64>(0, qRound64(plan.backgroundTrack.sourceStartSecond * kMixSampleRate));
            addClipToMix(*clip, plan.backgroundTrack.gain, startFrame, maxFrames, clipStartFrame, &mix);
        }
    }

    for (const auto& playback : plan.scheduledSfxPlaybacks) {
        const QString path = miacode::preview_sfx::assetFilePathForKind(plan.sfxDirectory, playback.assetKind);
        const DecodedClip* clip = loadClip(playback.assetKind, path);
        if (clip == nullptr) {
            continue;
        }
        const qint64 startFrame = qRound64(playback.mixSecond * kMixSampleRate);
        const qint64 maxFrames = playback.maxDurationSeconds >= 0.0
            ? qMax<qint64>(0, qRound64(playback.maxDurationSeconds * kMixSampleRate))
            : -1;
        addClipToMix(*clip, playback.gain, startFrame, maxFrames, 0, &mix);
    }

    for (const auto& span : plan.mergedTouchholdSpans) {
        const QString path = miacode::preview_sfx::assetFilePathForKind(plan.sfxDirectory, span.assetKind);
        const DecodedClip* clip = loadClip(span.assetKind, path);
        if (clip == nullptr) {
            continue;
        }
        const qint64 startFrame = qRound64(span.mixSecond * kMixSampleRate);
        const qint64 maxFrames = qMax<qint64>(0, qRound64(span.durationSeconds * kMixSampleRate));
        addClipToMix(*clip, span.gain, startFrame, maxFrames, 0, &mix);
    }

    if (!writeWav16(outputPath, mix, kMixSampleRate, kMixChannels)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to write mixed wav");
        }
        return false;
    }

    appendExportLog(
        QStringLiteral("audio_backend_render_complete"),
        QStringLiteral("backend=%1 mode=legacy output=%2 bgm=%3 sfx=%4 touchhold=%5 seconds=%6")
            .arg(backendId())
            .arg(outputPath)
            .arg(plan.backgroundTrack.enabled ? 1 : 0)
            .arg(plan.scheduledSfxPlaybacks.size())
            .arg(plan.mergedTouchholdSpans.size())
            .arg(plan.alignedTotalSeconds, 0, 'f', 6));
    return true;
}

}  // namespace miacode::video_export
