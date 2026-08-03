#include "audio/OfflineAudioDecoder.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include <QFile>
#include <QFileInfo>
#include <QtMath>

#include "common/MiniaudioFileAccess.h"

#include "../../third_party/miniaudio/miniaudio.h"

#ifdef MIACODE_HAS_BASS_AUDIO
#include "bass.h"
#endif

namespace miacode::audio_decode {

namespace {

QVector<float> resampleLinear(
    const QVector<float>& source,
    int sourceSampleRate,
    int targetSampleRate)
{
    if (source.isEmpty() || sourceSampleRate <= 0 || targetSampleRate <= 0) {
        return {};
    }
    if (sourceSampleRate == targetSampleRate) {
        return source;
    }

    const qint64 targetCount64 = qRound64(
        static_cast<double>(source.size()) * targetSampleRate / sourceSampleRate);
    const int targetCount = static_cast<int>(qBound<qint64>(
        static_cast<qint64>(1), targetCount64, static_cast<qint64>(INT_MAX)));
    QVector<float> result(targetCount, 0.0f);
    const double sourceStep = static_cast<double>(sourceSampleRate) / targetSampleRate;
    for (int targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
        const double sourcePosition = qMin(
            static_cast<double>(source.size() - 1),
            static_cast<double>(targetIndex) * sourceStep);
        const int left = static_cast<int>(sourcePosition);
        const int right = qMin(left + 1, source.size() - 1);
        const double fraction = sourcePosition - left;
        result[targetIndex] = static_cast<float>(
            source.at(left) * (1.0 - fraction) + source.at(right) * fraction);
    }
    return result;
}

#ifdef MIACODE_HAS_BASS_AUDIO
DecodedMonoAudio decodeWithBass(const QString& path, int targetSampleRate)
{
    DecodedMonoAudio decoded;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return decoded;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        return decoded;
    }

    const HSTREAM stream = BASS_StreamCreateFile(
        TRUE,
        bytes.constData(),
        0,
        static_cast<QWORD>(bytes.size()),
        BASS_STREAM_DECODE | BASS_STREAM_PRESCAN | BASS_SAMPLE_FLOAT);
    if (stream == 0) {
        return decoded;
    }

    BASS_CHANNELINFO info{};
    if (!BASS_ChannelGetInfo(stream, &info) || info.chans == 0 || info.freq == 0) {
        BASS_StreamFree(stream);
        return decoded;
    }
    const int channels = qBound(1, static_cast<int>(info.chans), 8);
    const int sourceSampleRate = static_cast<int>(info.freq);

    constexpr int kFramesPerChunk = 4096;
    QVector<float> interleaved(kFramesPerChunk * channels, 0.0f);
    QVector<float> mono;
    while (true) {
        const DWORD requestedBytes = static_cast<DWORD>(interleaved.size() * sizeof(float));
        const DWORD bytesRead = BASS_ChannelGetData(
            stream, interleaved.data(), requestedBytes | BASS_DATA_FLOAT);
        if (bytesRead == static_cast<DWORD>(-1) || bytesRead == 0) {
            break;
        }
        const int framesRead = static_cast<int>(bytesRead / sizeof(float)) / channels;
        if (framesRead <= 0) {
            break;
        }
        const int oldSize = mono.size();
        mono.resize(oldSize + framesRead);
        for (int frame = 0; frame < framesRead; ++frame) {
            double sum = 0.0;
            for (int channel = 0; channel < channels; ++channel) {
                sum += interleaved.at(frame * channels + channel);
            }
            mono[oldSize + frame] = static_cast<float>(sum / channels);
        }
    }
    BASS_StreamFree(stream);
    if (mono.isEmpty()) {
        return decoded;
    }

    decoded.samples = resampleLinear(mono, sourceSampleRate, targetSampleRate);
    decoded.sampleRate = targetSampleRate;
    decoded.durationSeconds = static_cast<double>(decoded.samples.size()) / targetSampleRate;
    decoded.backend = Backend::Bass;
    return decoded;
}
#endif

DecodedMonoAudio decodeWithMiniaudio(const QString& path, int targetSampleRate)
{
    DecodedMonoAudio decoded;
    ma_decoder_config config = ma_decoder_config_init(
        ma_format_f32, 1, static_cast<ma_uint32>(targetSampleRate));
    ma_decoder decoder;
    if (miacode::audio_io::decoderInitFile(path, &config, &decoder) != MA_SUCCESS) {
        return decoded;
    }

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS && totalFrames > 0) {
        decoded.samples.reserve(static_cast<int>(
            qMin<ma_uint64>(totalFrames, static_cast<ma_uint64>(INT_MAX))));
    }
    QVector<float> buffer(4096, 0.0f);
    while (true) {
        ma_uint64 framesRead = 0;
        const ma_result result = ma_decoder_read_pcm_frames(
            &decoder, buffer.data(), static_cast<ma_uint64>(buffer.size()), &framesRead);
        if ((result != MA_SUCCESS && result != MA_AT_END) || framesRead == 0) {
            break;
        }
        const int oldSize = decoded.samples.size();
        decoded.samples.resize(oldSize + static_cast<int>(framesRead));
        std::copy_n(buffer.cbegin(), static_cast<int>(framesRead), decoded.samples.begin() + oldSize);
    }
    ma_decoder_uninit(&decoder);
    if (decoded.samples.isEmpty()) {
        return {};
    }
    decoded.sampleRate = targetSampleRate;
    decoded.durationSeconds = static_cast<double>(decoded.samples.size()) / targetSampleRate;
    decoded.backend = Backend::Miniaudio;
    return decoded;
}

}  // namespace

DecodedMonoAudio decodeFileToMono(
    const QString& path,
    int targetSampleRate,
    BackendPreference preference)
{
    if (path.isEmpty() || targetSampleRate <= 0 || !QFileInfo::exists(path)) {
        return {};
    }
#ifdef MIACODE_HAS_BASS_AUDIO
    if (preference == BackendPreference::Bass) {
        return decodeWithBass(path, targetSampleRate);
    }
#endif
    if (preference == BackendPreference::Miniaudio) {
        return decodeWithMiniaudio(path, targetSampleRate);
    }
    return {};
}

bool bassBackendAvailable()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    return true;
#else
    return false;
#endif
}

QString backendLabel(Backend backend)
{
    switch (backend) {
    case Backend::Bass:
        return QStringLiteral("bass");
    case Backend::Miniaudio:
        return QStringLiteral("miniaudio");
    case Backend::None:
    default:
        return QStringLiteral("none");
    }
}

}  // namespace miacode::audio_decode
