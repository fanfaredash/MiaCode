#include "MiniaudioPreviewAudioBackend.h"

#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/MiniaudioFileAccess.h"
#include "common/OperationLog.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewSfxTimeline.h"

#include <algorithm>
#include <cstring>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QtMath>

#include <SoundTouch.h>

#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio/miniaudio.h"

#define QtPreviewSfxRuntime MiniaudioPreviewAudioBackend

namespace {
constexpr double kQtPreviewSfxEpsilonSeconds = miacode::preview_sfx_timeline::kTimelineEpsilonSeconds;

bool runtimeAudioDebugEnabled()
{
    return miacode::debug_options::audioDebugOutputEnabled();
}

QString audioDebugLogPath()
{
    return miacode::debug_log::audioLogPath();
}

void appendAudioDebugLog(const QString& message)
{
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Audio, QString(), message);
}
}

struct QtPreviewSfxRuntime::EngineState {
    ma_engine engine{};
};

struct QtPreviewSfxRuntime::Voice {
    ma_decoder decoder{};
    bool decoderInitialized = false;
    ma_sound sound{};
    bool initialized = false;
    ma_uint32 sampleRate = 0;
    ma_uint64 frameCount = 0;
};

struct QtPreviewSfxRuntime::StretchedBackgroundState {
    ma_data_source_base dataSource{};
    QString trackPath;
    double playbackRate = 1.0;
    ma_decoder decoder{};
    bool decoderInitialized = false;
    soundtouch::SoundTouch stretcher;
    ma_sound sound{};
    bool soundInitialized = false;
    QMutex mutex;
    QVector<float> decodeChunk;
    QVector<float> stretchChunk;
    ma_uint32 channels = 0;
    ma_uint32 sampleRate = 0;
    ma_uint64 sourceFrameCount = 0;
    ma_uint64 stretchedFrameCount = 0;
    ma_uint64 renderedOutputCursorFrame = 0;
    quint64 seekSerial = 0;
    bool decoderAtEnd = false;
    bool stretcherFlushed = false;
    bool authoritativeClockReady = false;
    bool loggedFirstPutAfterSeek = false;
    bool loggedFirstReceiveAfterSeek = false;

    static StretchedBackgroundState* from(ma_data_source* dataSourcePtr)
    {
        return reinterpret_cast<StretchedBackgroundState*>(dataSourcePtr);
    }

    static const ma_data_source_vtable* dataSourceVTable()
    {
        static const ma_data_source_vtable sVTable{
            &StretchedBackgroundState::readCallback,
            &StretchedBackgroundState::seekCallback,
            &StretchedBackgroundState::getDataFormatCallback,
            &StretchedBackgroundState::getCursorCallback,
            &StretchedBackgroundState::getLengthCallback,
            &StretchedBackgroundState::setLoopingCallback,
            0,
        };
        return &sVTable;
    }

    static ma_result readCallback(
        ma_data_source* dataSourcePtr,
        void* framesOut,
        ma_uint64 frameCount,
        ma_uint64* framesRead
    )
    {
        StretchedBackgroundState* state = from(dataSourcePtr);
        if (state == nullptr || !state->decoderInitialized || state->channels == 0) {
            if (framesRead != nullptr) {
                *framesRead = 0;
            }
            return MA_INVALID_OPERATION;
        }
        if (framesRead != nullptr) {
            *framesRead = 0;
        }
        if (frameCount == 0) {
            return MA_SUCCESS;
        }

        QMutexLocker locker(&state->mutex);
        const ma_uint32 channels = state->channels;
        const ma_uint64 requestedFrames = frameCount;
        ma_uint64 producedFrames = 0;
        float* output = static_cast<float*>(framesOut);
        if (state->stretchChunk.isEmpty()) {
            state->stretchChunk.resize(static_cast<int>(4096 * channels));
        }
        if (state->decodeChunk.isEmpty()) {
            state->decodeChunk.resize(static_cast<int>(4096 * channels));
        }

        while (producedFrames < requestedFrames) {
            const ma_uint64 remainingFrames = requestedFrames - producedFrames;
            const unsigned int availableFrames = state->stretcher.numSamples();
            if (availableFrames > 0) {
                const unsigned int receiveFrames =
                    static_cast<unsigned int>(qMin<ma_uint64>(remainingFrames, availableFrames));
                const qsizetype receiveSamples =
                    static_cast<qsizetype>(receiveFrames * static_cast<ma_uint64>(channels));
                if (state->stretchChunk.size() < receiveSamples) {
                    state->stretchChunk.resize(static_cast<int>(receiveSamples));
                }
                const unsigned int framesOutNow = state->stretcher.receiveSamples(
                    state->stretchChunk.data(),
                    receiveFrames
                );
                if (framesOutNow > 0) {
                    state->authoritativeClockReady = true;
                    if (!state->loggedFirstReceiveAfterSeek) {
                        state->loggedFirstReceiveAfterSeek = true;
                        appendAudioDebugLog(
                            QString("stretched_read_first_output seek=%1 rendered_cursor_frame=%2 frames_out=%3 available_before=%4 requested_frames=%5 rate=%6")
                                .arg(state->seekSerial)
                                .arg(state->renderedOutputCursorFrame)
                                .arg(framesOutNow)
                                .arg(availableFrames)
                                .arg(requestedFrames)
                                .arg(state->playbackRate, 0, 'f', 3));
                    }
                    const ma_uint64 framesToCopy = static_cast<ma_uint64>(framesOutNow);
                    const qsizetype sampleCount =
                        static_cast<qsizetype>(framesToCopy * static_cast<ma_uint64>(channels));
                    if (output != nullptr) {
                        std::memcpy(
                            output + (producedFrames * channels),
                            state->stretchChunk.constData(),
                            static_cast<size_t>(sampleCount) * sizeof(float)
                        );
                    }
                    producedFrames += framesToCopy;
                    state->renderedOutputCursorFrame += framesToCopy;
                    continue;
                }
            }

            if (state->decoderAtEnd) {
                if (!state->stretcherFlushed) {
                    state->stretcher.flush();
                    state->stretcherFlushed = true;
                    continue;
                }
                break;
            }

            constexpr ma_uint64 kDecodeFrames = 4096;
            ma_uint64 decodedFrames = 0;
            const ma_result decodeResult = ma_decoder_read_pcm_frames(
                &state->decoder,
                state->decodeChunk.data(),
                kDecodeFrames,
                &decodedFrames
            );
            if (decodedFrames > 0) {
                if (!state->loggedFirstPutAfterSeek) {
                    state->loggedFirstPutAfterSeek = true;
                    appendAudioDebugLog(
                        QString("stretched_read_first_input seek=%1 rendered_cursor_frame=%2 decoded_frames=%3 rate=%4")
                            .arg(state->seekSerial)
                            .arg(state->renderedOutputCursorFrame)
                            .arg(decodedFrames)
                            .arg(state->playbackRate, 0, 'f', 3));
                }
                state->stretcher.putSamples(
                    state->decodeChunk.constData(),
                    static_cast<unsigned int>(decodedFrames)
                );
            }
            if (decodeResult != MA_SUCCESS || decodedFrames == 0) {
                state->decoderAtEnd = true;
            }
        }

        if (framesRead != nullptr) {
            *framesRead = producedFrames;
        }
        return producedFrames == requestedFrames ? MA_SUCCESS : MA_AT_END;
    }

    static ma_result seekCallback(ma_data_source* dataSourcePtr, ma_uint64 frameIndex)
    {
        StretchedBackgroundState* state = from(dataSourcePtr);
        if (state == nullptr || !state->decoderInitialized) {
            return MA_INVALID_OPERATION;
        }

        QMutexLocker locker(&state->mutex);
        const double sourceFrameAsDouble = static_cast<double>(frameIndex) * state->playbackRate;
        ma_uint64 sourceFrameIndex = sourceFrameAsDouble <= 0.0
            ? 0
            : static_cast<ma_uint64>(sourceFrameAsDouble);
        if (state->sourceFrameCount > 0) {
            sourceFrameIndex = qMin(sourceFrameIndex, state->sourceFrameCount - 1);
        }
        const ma_result seekResult = ma_data_source_seek_to_pcm_frame(
            reinterpret_cast<ma_data_source*>(&state->decoder),
            sourceFrameIndex
        );
        if (seekResult != MA_SUCCESS) {
            return seekResult;
        }
        state->stretcher.clear();
        state->stretcher.setTempo(static_cast<float>(state->playbackRate));
        state->seekSerial += 1;
        state->decoderAtEnd = false;
        state->stretcherFlushed = false;
        state->authoritativeClockReady = false;
        state->loggedFirstPutAfterSeek = false;
        state->loggedFirstReceiveAfterSeek = false;
        state->renderedOutputCursorFrame = frameIndex;
        appendAudioDebugLog(
            QString("stretched_seek seek=%1 output_frame=%2 source_frame=%3 rate=%4")
                .arg(state->seekSerial)
                .arg(frameIndex)
                .arg(sourceFrameIndex)
                .arg(state->playbackRate, 0, 'f', 3));
        return MA_SUCCESS;
    }

    static ma_result getDataFormatCallback(
        ma_data_source* dataSourcePtr,
        ma_format* format,
        ma_uint32* channels,
        ma_uint32* sampleRate,
        ma_channel* channelMap,
        size_t channelMapCap
    )
    {
        Q_UNUSED(channelMap);
        Q_UNUSED(channelMapCap);
        StretchedBackgroundState* state = from(dataSourcePtr);
        if (state == nullptr || !state->decoderInitialized) {
            return MA_INVALID_OPERATION;
        }
        if (format != nullptr) {
            *format = ma_format_f32;
        }
        if (channels != nullptr) {
            *channels = state->channels;
        }
        if (sampleRate != nullptr) {
            *sampleRate = state->sampleRate;
        }
        return MA_SUCCESS;
    }

    static ma_result getCursorCallback(ma_data_source* dataSourcePtr, ma_uint64* cursor)
    {
        if (cursor == nullptr) {
            return MA_INVALID_ARGS;
        }
        StretchedBackgroundState* state = from(dataSourcePtr);
        if (state == nullptr || !state->decoderInitialized) {
            *cursor = 0;
            return MA_INVALID_OPERATION;
        }
        QMutexLocker locker(&state->mutex);
        *cursor = state->renderedOutputCursorFrame;
        return MA_SUCCESS;
    }

    static ma_result getLengthCallback(ma_data_source* dataSourcePtr, ma_uint64* length)
    {
        if (length == nullptr) {
            return MA_INVALID_ARGS;
        }
        StretchedBackgroundState* state = from(dataSourcePtr);
        if (state == nullptr || !state->decoderInitialized) {
            *length = 0;
            return MA_INVALID_OPERATION;
        }
        if (state->stretchedFrameCount == 0) {
            *length = 0;
            return MA_NOT_IMPLEMENTED;
        }
        *length = state->stretchedFrameCount;
        return MA_SUCCESS;
    }

    static ma_result setLoopingCallback(ma_data_source* dataSourcePtr, ma_bool32 isLooping)
    {
        Q_UNUSED(dataSourcePtr);
        Q_UNUSED(isLooping);
        return MA_NOT_IMPLEMENTED;
    }
};

QString QtPreviewSfxRuntime::backendId() const
{
    return QStringLiteral("miniaudio");
}

bool QtPreviewSfxRuntime::canBePrimary(QString* reason) const
{
    if (reason != nullptr) {
        *reason = QStringLiteral("miniaudio compatibility backend");
    }
    return true;
}


#include "QtPreviewSfxRuntime.Timeline.cpp"
#include "QtPreviewSfxRuntime.Background.cpp"
#include "QtPreviewSfxRuntime.Assets.cpp"
#include "QtPreviewSfxRuntime.Engine.cpp"
#include "QtPreviewSfxRuntime.Voices.cpp"

#undef QtPreviewSfxRuntime
