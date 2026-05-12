#include "BgmPeakNormalize.h"

#include "common/MiniaudioFileAccess.h"
#include "common/PreviewAudioMixConfig.h"

#include <QFileInfo>
#include <QVector>
#include <QtGlobal>
#include <QtMath>

namespace miacode::audio {

namespace {

constexpr double kBgmMaxBoost = 4.0;
constexpr double kBgmPeakFloor = 1.0e-4;

}  // namespace

BgmPeakNormalizationResult computeBgmPeakNormalization(const QString& filePath)
{
    BgmPeakNormalizationResult result;

    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        return result;
    }

    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(
        ma_format_f32,
        miacode::preview_audio::kMixChannels,
        miacode::preview_audio::kMixSampleRate);
    if (miacode::audio_io::decoderInitFile(filePath, &config, &decoder) != MA_SUCCESS) {
        return result;
    }

    constexpr int kChannels = miacode::preview_audio::kMixChannels;
    constexpr ma_uint64 kChunkFrames = 4096;
    QVector<float> chunk(static_cast<int>(kChunkFrames) * kChannels);
    double peak = 0.0;
    for (;;) {
        ma_uint64 framesRead = 0;
        const ma_result rc = ma_decoder_read_pcm_frames(
            &decoder, chunk.data(), kChunkFrames, &framesRead);
        const int sampleCount = static_cast<int>(framesRead) * kChannels;
        for (int i = 0; i < sampleCount; ++i) {
            const double absValue = qAbs(static_cast<double>(chunk.at(i)));
            if (absValue > peak) {
                peak = absValue;
            }
        }
        if (rc != MA_SUCCESS || framesRead == 0) {
            break;
        }
    }
    ma_decoder_uninit(&decoder);

    result.peak = peak;
    result.decoded = true;
    result.gain = qMin(kBgmMaxBoost, 1.0 / qMax(peak, kBgmPeakFloor));
    return result;
}

}  // namespace miacode::audio
