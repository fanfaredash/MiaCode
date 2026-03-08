#include "QtPreviewSfxRuntime.h"

#include "common/AssetPaths.h"

#include <algorithm>
#include <cstring>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>
#include <QtMath>

#include <SoundTouch.h>

#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio/miniaudio.h"

namespace {
constexpr double kQtPreviewSfxEpsilonSeconds = 1e-6;

bool runtimeAudioDebugEnabled()
{
    const QByteArray value = qgetenv("MIACODE_ENABLE_RUNTIME_DEBUG_OUTPUT").trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

QString audioDebugLogPath()
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty()) {
        return QDir::temp().filePath("miacode_audio_debug.log");
    }
    QDir dir(baseDir);
    dir.mkpath(".");
    return dir.filePath("miacode_audio_debug.log");
}

void appendAudioDebugLog(const QString& message)
{
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    static QMutex mutex;
    QMutexLocker locker(&mutex);
    QFile file(audioDebugLogPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
        return;
    }
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << " [audio] "
           << message
           << "\n";
}
}

struct QtPreviewSfxRuntime::EngineState {
    ma_engine engine{};
};

struct QtPreviewSfxRuntime::Voice {
    ma_sound sound{};
    bool initialized = false;
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
    ma_uint64 outputCursorFrame = 0;
    bool decoderAtEnd = false;
    bool stretcherFlushed = false;

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
                    state->outputCursorFrame += framesToCopy;
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
        state->decoderAtEnd = false;
        state->stretcherFlushed = false;
        state->outputCursorFrame = frameIndex;
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
        *cursor = state->outputCursorFrame;
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

QtPreviewSfxRuntime::QtPreviewSfxRuntime(QObject* parent)
    : QObject(parent)
{
    appendAudioDebugLog("QtPreviewSfxRuntime created");
}

QtPreviewSfxRuntime::~QtPreviewSfxRuntime()
{
    appendAudioDebugLog("QtPreviewSfxRuntime destroying");
    resetBanks();
}

void QtPreviewSfxRuntime::reloadAssets(const PreviewAudioSettings& settings)
{
    settings_ = settings;
    settings_.normalize();
    resetBanks();
    sfxDir_ = resolveSfxDir();
    if (!initializeAudioEngine()) {
        return;
    }
    initializeAssets();
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        initializeBackgroundTrack();
    } else {
        prepareStretchedBackgroundTrack(backgroundTrackLastTimelineSecond_);
    }
}

void QtPreviewSfxRuntime::setChartPath(const QString& chartPath)
{
    const QString normalizedChartPath = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    if (normalizedChartPath == chartPath_) {
        return;
    }

    chartPath_ = normalizedChartPath;
    trackPath_ = resolveTrackPath(chartPath_);
    const QString resolvedSfxDir = resolveSfxDir();
    if (resolvedSfxDir != sfxDir_) {
        sfxDir_ = resolvedSfxDir;
        if (engineInitialized_ && engineState_ != nullptr) {
            initializeAssets();
        }
    }
    appendAudioDebugLog(QString("setChartPath chart=%1 track=%2").arg(chartPath_, trackPath_));
    resetBackgroundTrack();
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        initializeBackgroundTrack();
    } else {
        prepareStretchedBackgroundTrack(backgroundTrackLastTimelineSecond_);
    }
}

void QtPreviewSfxRuntime::setBackgroundTrackOffsetSeconds(double seconds)
{
    const double clamped = qIsFinite(seconds) ? seconds : 0.0;
    if (qAbs(backgroundTrackOffsetSeconds_ - clamped) <= kQtPreviewSfxEpsilonSeconds) {
        return;
    }
    backgroundTrackOffsetSeconds_ = clamped;
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
    backgroundTrackLastTimelineSecond_ = 0.0;
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
        ma_sound_seek_to_second(&backgroundTrackVoice_->sound, 0.0f);
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_stop(&stretchedBackgroundState_->sound);
        ma_sound_seek_to_second(&stretchedBackgroundState_->sound, 0.0f);
    }
}

void QtPreviewSfxRuntime::setBackgroundTrackPlaybackRate(double rate)
{
    const double clamped = qBound(0.25, qIsFinite(rate) ? rate : 1.0, 2.0);
    if (qAbs(backgroundTrackPlaybackRate_ - clamped) <= kQtPreviewSfxEpsilonSeconds) {
        return;
    }
    backgroundTrackPlaybackRate_ = clamped;
    appendAudioDebugLog(QString("setBackgroundTrackPlaybackRate rate=%1").arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
    backgroundTrackLastTimelineSecond_ = qMax(0.0, backgroundTrackLastTimelineSecond_);
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_stop(&stretchedBackgroundState_->sound);
    }
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        resetStretchedBackgroundTrack();
        if (backgroundTrackVoice_ == nullptr && !trackPath_.isEmpty()) {
            initializeBackgroundTrack();
        }
    } else if (!trackPath_.isEmpty() && engineInitialized_ && engineState_ != nullptr) {
        if (backgroundTrackVoice_ != nullptr) {
            if (backgroundTrackVoice_->initialized) {
                ma_sound_uninit(&backgroundTrackVoice_->sound);
            }
            delete backgroundTrackVoice_;
            backgroundTrackVoice_ = nullptr;
            backgroundTrackConfigured_ = false;
        }
        prepareStretchedBackgroundTrack(backgroundTrackLastTimelineSecond_);
    }
}

void QtPreviewSfxRuntime::applyLevels(const PreviewAudioSettings& settings)
{
    settings_ = settings;
    settings_.normalize();
    applyVolumes();
}

void QtPreviewSfxRuntime::configureTimeline(const QVector<TimelineNoteMarker>& noteMarkers)
{
    events_.clear();
    eventIndex_ = 0;
    touchholdSpans_.clear();
    touchholdSpans_.reserve(noteMarkers.size());
    events_.reserve(noteMarkers.size() * 3);

    const auto addEvent = [this](double second, const QString& kind, int priority = 1, int spanIndex = -1) {
        if (second < 0.0 || kind.isEmpty()) {
            return;
        }
        Event event;
        event.second = second;
        event.priority = priority;
        event.kind = kind;
        event.spanIndex = spanIndex;
        events_.append(event);
    };

    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (marker.type == "tap") {
            addEvent(marker.second, "answer");
            if (marker.isBreak) {
                addEvent(marker.second, "break");
            }
            if (marker.isEx) {
                addEvent(marker.second, "ex");
            }
            continue;
        }
        if (marker.type == "hold") {
            addEvent(marker.second, "answer");
            if (marker.endSecond > marker.second) {
                addEvent(marker.endSecond, "answer");
            }
            if (marker.isEx) {
                addEvent(marker.second, "ex");
                if (marker.endSecond > marker.second) {
                    addEvent(marker.endSecond, "ex");
                }
            }
            continue;
        }
        if (marker.type == "touch") {
            addEvent(marker.second, "touch");
            continue;
        }
        if (marker.type == "touch_hold") {
            addEvent(marker.second, "touch");
            if (marker.endSecond > marker.second) {
                TouchholdSpan span;
                span.startSecond = marker.second;
                span.endSecond = marker.endSecond;
                const int spanIndex = touchholdSpans_.size();
                touchholdSpans_.append(span);
                addEvent(span.startSecond, "touchhold_start", 0, spanIndex);
                addEvent(span.endSecond, "touchhold_stop", 2, spanIndex);
            }
            continue;
        }
        if (marker.type == "slide" || marker.type == "wifi") {
            if (marker.hasHeadStar && !marker.sameHeadSlide) {
                addEvent(marker.second, "answer");
            }
            addEvent(marker.slideTraceSecond >= 0.0 ? marker.slideTraceSecond : marker.second, "slide");
            continue;
        }
    }

    std::sort(events_.begin(), events_.end(), [](const Event& a, const Event& b) {
        if (qAbs(a.second - b.second) > kQtPreviewSfxEpsilonSeconds) {
            return a.second < b.second;
        }
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.kind < b.kind;
    });
}

void QtPreviewSfxRuntime::clearTimeline()
{
    events_.clear();
    eventIndex_ = 0;
    touchholdSpans_.clear();
    stopAll();
}

void QtPreviewSfxRuntime::resetCursor(double second, bool includeCurrentSecond)
{
    eventIndex_ = 0;
    while (eventIndex_ < events_.size()) {
        const double eventSecond = events_[eventIndex_].second;
        const bool beforeStart = includeCurrentSecond
            ? (eventSecond + kQtPreviewSfxEpsilonSeconds < second)
            : (eventSecond <= second + kQtPreviewSfxEpsilonSeconds);
        if (!beforeStart) {
            break;
        }
        ++eventIndex_;
    }
}

void QtPreviewSfxRuntime::drainEvents(double second)
{
    while (eventIndex_ < events_.size()) {
        const Event& event = events_[eventIndex_];
        if (event.second > second + kQtPreviewSfxEpsilonSeconds) {
            break;
        }
        if (event.kind == "touchhold_start") {
            startTouchholdSpan(event.spanIndex, 0.0);
        } else if (event.kind == "touchhold_stop") {
            stopTouchholdSpan(event.spanIndex);
        } else {
            playKindInternal(event.kind);
        }
        ++eventIndex_;
    }
}

void QtPreviewSfxRuntime::syncTouchholdVoices(double second)
{
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice != nullptr && voice.voice->initialized) {
            ma_sound_stop(&voice.voice->sound);
        }
        voice.activeSpanIndex = -1;
    }

    for (int spanIndex = 0; spanIndex < touchholdSpans_.size(); ++spanIndex) {
        const TouchholdSpan& span = touchholdSpans_[spanIndex];
        if (second <= span.startSecond + kQtPreviewSfxEpsilonSeconds) {
            continue;
        }
        if (second >= span.endSecond - kQtPreviewSfxEpsilonSeconds) {
            continue;
        }
        startTouchholdSpan(spanIndex, second - span.startSecond);
    }
}

bool QtPreviewSfxRuntime::hasBackgroundTrack() const
{
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        return backgroundTrackConfigured_ && backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized;
    }
    if (!trackPath_.isEmpty() && engineInitialized_ && engineState_ != nullptr) {
        return true;
    }
    return stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized;
}

bool QtPreviewSfxRuntime::isBackgroundTrackRunning() const
{
    return backgroundTrackRunning_;
}

void QtPreviewSfxRuntime::startBackgroundTrack(double second)
{
    if (!hasBackgroundTrack()) {
        return;
    }
    const bool useStretched = !qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0);
    if (useStretched && !prepareStretchedBackgroundTrack(second)) {
        backgroundTrackLastTimelineSecond_ = qMax(0.0, second);
        backgroundTrackPendingStart_ = true;
        backgroundTrackRunning_ = false;
        appendAudioDebugLog(QString("startBackgroundTrack deferred second=%1 rate=%2")
                                .arg(second, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
        return;
    }

    ma_sound* activeSound = nullptr;
    StretchedBackgroundState* activeStretchedState = nullptr;
    if (useStretched) {
        if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
            return;
        }
        activeSound = &stretchedBackgroundState_->sound;
        activeStretchedState = stretchedBackgroundState_;
    } else {
        if (backgroundTrackVoice_ == nullptr || !backgroundTrackVoice_->initialized) {
            return;
        }
        activeSound = &backgroundTrackVoice_->sound;
    }

    backgroundTrackLastTimelineSecond_ = qMax(0.0, second);
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
    ma_sound_stop(activeSound);
    const double rawSecond = second + backgroundTrackOffsetSeconds_;
    const double mappedSecond = useStretched ? (rawSecond / backgroundTrackPlaybackRate_) : rawSecond;
    if (rawSecond < 0.0) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
        backgroundTrackPendingStart_ = true;
        return;
    }
    if (useStretched && activeStretchedState != nullptr) {
        const double clampedMappedSecond = qMax(0.0, mappedSecond);
        ma_uint64 targetFrame = static_cast<ma_uint64>(clampedMappedSecond * activeStretchedState->sampleRate);
        if (activeStretchedState->stretchedFrameCount > 0) {
            targetFrame = qMin(targetFrame, activeStretchedState->stretchedFrameCount - 1);
        }
        ma_sound_seek_to_pcm_frame(activeSound, targetFrame);
    } else if (ma_sound_seek_to_second(activeSound, static_cast<float>(qMax(0.0, mappedSecond))) != MA_SUCCESS) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
    }
    const ma_result startResult = ma_sound_start(activeSound);
    if (startResult == MA_SUCCESS) {
        backgroundTrackRunning_ = true;
        appendAudioDebugLog(QString("startBackgroundTrack started second=%1 raw=%2 mapped=%3 rate=%4")
                                .arg(second, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    } else {
        backgroundTrackRunning_ = false;
        backgroundTrackPendingStart_ = true;
        appendAudioDebugLog(QString("startBackgroundTrack start failed rc=%1 second=%2 mapped=%3 rate=%4")
                                .arg(static_cast<int>(startResult))
                                .arg(second, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    }
}

void QtPreviewSfxRuntime::pauseBackgroundTrack()
{
    if (!hasBackgroundTrack()) {
        return;
    }
    if (!qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
            ma_sound_stop(&stretchedBackgroundState_->sound);
        }
    } else if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
    }
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
}

double QtPreviewSfxRuntime::backgroundPlaybackSecond() const
{
    if (!hasBackgroundTrack()) {
        return 0.0;
    }
    if (!qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        return stretchedBackgroundPlaybackSecond();
    }
    if (!backgroundTrackRunning_) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }
    float cursorSeconds = 0.0f;
    if (ma_sound_get_cursor_in_seconds(&backgroundTrackVoice_->sound, &cursorSeconds) != MA_SUCCESS) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }
    return qMax(0.0, static_cast<double>(cursorSeconds) - backgroundTrackOffsetSeconds_);
}

void QtPreviewSfxRuntime::syncBackgroundTrack(double timelineSecond)
{
    if (!hasBackgroundTrack()) {
        return;
    }
    const bool useStretched = !qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0);
    if (useStretched && !prepareStretchedBackgroundTrack(timelineSecond)) {
        backgroundTrackLastTimelineSecond_ = qMax(0.0, timelineSecond);
        backgroundTrackPendingStart_ = true;
        backgroundTrackRunning_ = false;
        appendAudioDebugLog(QString("syncBackgroundTrack deferred second=%1 rate=%2")
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
        return;
    }

    ma_sound* activeSound = nullptr;
    StretchedBackgroundState* activeStretchedState = nullptr;
    if (useStretched) {
        if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
            return;
        }
        activeSound = &stretchedBackgroundState_->sound;
        activeStretchedState = stretchedBackgroundState_;
    } else {
        if (backgroundTrackVoice_ == nullptr || !backgroundTrackVoice_->initialized) {
            return;
        }
        activeSound = &backgroundTrackVoice_->sound;
    }

    backgroundTrackLastTimelineSecond_ = qMax(0.0, timelineSecond);
    if (!backgroundTrackPendingStart_) {
        return;
    }
    const double rawSecond = timelineSecond + backgroundTrackOffsetSeconds_;
    const double mappedSecond = useStretched ? (rawSecond / backgroundTrackPlaybackRate_) : rawSecond;
    if (rawSecond < 0.0) {
        return;
    }
    if (useStretched && activeStretchedState != nullptr) {
        const double clampedMappedSecond = qMax(0.0, mappedSecond);
        ma_uint64 targetFrame = static_cast<ma_uint64>(clampedMappedSecond * activeStretchedState->sampleRate);
        if (activeStretchedState->stretchedFrameCount > 0) {
            targetFrame = qMin(targetFrame, activeStretchedState->stretchedFrameCount - 1);
        }
        ma_sound_seek_to_pcm_frame(activeSound, targetFrame);
    } else if (ma_sound_seek_to_second(activeSound, static_cast<float>(qMax(0.0, mappedSecond))) != MA_SUCCESS) {
        ma_sound_seek_to_pcm_frame(activeSound, 0);
    }
    const ma_result startResult = ma_sound_start(activeSound);
    if (startResult == MA_SUCCESS) {
        backgroundTrackPendingStart_ = false;
        backgroundTrackRunning_ = true;
        appendAudioDebugLog(QString("syncBackgroundTrack started second=%1 raw=%2 mapped=%3 rate=%4")
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(rawSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    } else {
        backgroundTrackPendingStart_ = true;
        backgroundTrackRunning_ = false;
        appendAudioDebugLog(QString("syncBackgroundTrack start failed rc=%1 second=%2 mapped=%3 rate=%4")
                                .arg(static_cast<int>(startResult))
                                .arg(timelineSecond, 0, 'f', 3)
                                .arg(mappedSecond, 0, 'f', 3)
                                .arg(backgroundTrackPlaybackRate_, 0, 'f', 3));
    }
}

bool QtPreviewSfxRuntime::audition(const QString& kind, double gain)
{
    return playKindInternal(kind, gain);
}

void QtPreviewSfxRuntime::stopAll()
{
    const auto stopBank = [](SfxBank& bank) {
        for (Voice* voice : bank.voices) {
            if (voice != nullptr && voice->initialized) {
                ma_sound_stop(&voice->sound);
            }
        }
    };

    stopBank(answerSfx_);
    stopBank(slideSfx_);
    stopBank(breakSfx_);
    stopBank(exSfx_);
    stopBank(touchSfx_);
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_stop(&backgroundTrackVoice_->sound);
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_stop(&stretchedBackgroundState_->sound);
    }
    backgroundTrackPendingStart_ = false;
    backgroundTrackRunning_ = false;
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice != nullptr && voice.voice->initialized) {
            ma_sound_stop(&voice.voice->sound);
        }
        voice.activeSpanIndex = -1;
    }
}

QString QtPreviewSfxRuntime::resolveTrackPath(const QString& chartPath) const
{
    if (chartPath.isEmpty()) {
        return QString();
    }
    const QFileInfo chartInfo(chartPath);
    const QString path = QDir(chartInfo.absolutePath()).filePath("track.mp3");
    if (QFileInfo::exists(path)) {
        return QDir::cleanPath(path);
    }
    return QString();
}

QString QtPreviewSfxRuntime::resolveSfxDir() const
{
    const QString envDir = QDir::cleanPath(
        qEnvironmentVariable("MIACODE_PREVIEW_SFX_DIR", qEnvironmentVariable("MAIMURI_PREVIEW_SFX_DIR")).trimmed()
    );
    if (!envDir.isEmpty() && QFileInfo::exists(QDir(envDir).filePath("answer.wav"))) {
        return envDir;
    }

    QStringList candidates;
    const QString assetSfxUpper = miacode::assets::assetPath("SFX");
    if (!assetSfxUpper.isEmpty()) {
        candidates << assetSfxUpper;
    }
    const QString assetSfxLower = miacode::assets::assetPath("sfx");
    if (!assetSfxLower.isEmpty()) {
        candidates << assetSfxLower;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    candidates << QDir::cleanPath(appDir.filePath("SFX"));
    candidates << QDir::cleanPath(appDir.filePath("sfx"));

    for (const QString& path : candidates) {
        if (QFileInfo::exists(QDir(path).filePath("answer.wav"))) {
            return path;
        }
    }
    return QString();
}

void QtPreviewSfxRuntime::resetBackgroundTrack()
{
    resetStretchedBackgroundTrack();
    if (backgroundTrackVoice_ != nullptr) {
        if (backgroundTrackVoice_->initialized) {
            ma_sound_uninit(&backgroundTrackVoice_->sound);
        }
        delete backgroundTrackVoice_;
        backgroundTrackVoice_ = nullptr;
    }
    backgroundTrackConfigured_ = false;
    backgroundTrackRunning_ = false;
    backgroundTrackPendingStart_ = false;
    backgroundTrackLastTimelineSecond_ = 0.0;
}

void QtPreviewSfxRuntime::resetStretchedBackgroundTrack()
{
    if (stretchedBackgroundState_ == nullptr) {
        return;
    }
    if (stretchedBackgroundState_->soundInitialized) {
        ma_sound_uninit(&stretchedBackgroundState_->sound);
        stretchedBackgroundState_->soundInitialized = false;
    }
    ma_data_source_uninit(reinterpret_cast<ma_data_source*>(&stretchedBackgroundState_->dataSource));
    if (stretchedBackgroundState_->decoderInitialized) {
        ma_decoder_uninit(&stretchedBackgroundState_->decoder);
        stretchedBackgroundState_->decoderInitialized = false;
    }
    delete stretchedBackgroundState_;
    stretchedBackgroundState_ = nullptr;
}

void QtPreviewSfxRuntime::resetBanks()
{
    const auto resetBank = [](SfxBank& bank) {
        for (Voice* voice : bank.voices) {
            if (voice == nullptr) {
                continue;
            }
            if (voice->initialized) {
                ma_sound_uninit(&voice->sound);
            }
            delete voice;
        }
        bank.voices.clear();
        bank.nextVoice = 0;
        bank.configured = false;
    };

    resetBank(answerSfx_);
    resetBank(slideSfx_);
    resetBank(breakSfx_);
    resetBank(exSfx_);
    resetBank(touchSfx_);
    resetBackgroundTrack();
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice != nullptr) {
            if (voice.voice->initialized) {
                ma_sound_uninit(&voice.voice->sound);
            }
            delete voice.voice;
            voice.voice = nullptr;
        }
        voice.activeSpanIndex = -1;
    }
    touchholdVoices_.clear();
    touchholdSoundLengthFrames_ = 0;

    if (engineState_ != nullptr) {
        ma_engine_uninit(&engineState_->engine);
        delete engineState_;
        engineState_ = nullptr;
    }
    engineInitialized_ = false;
}

bool QtPreviewSfxRuntime::initializeAudioEngine()
{
    if (engineState_ != nullptr) {
        return true;
    }

    engineState_ = new EngineState();
    if (ma_engine_init(nullptr, &engineState_->engine) != MA_SUCCESS) {
        delete engineState_;
        engineState_ = nullptr;
        engineInitialized_ = false;
        appendAudioDebugLog("initializeAudioEngine failed");
        return false;
    }
    deviceSampleRate_ = ma_engine_get_sample_rate(&engineState_->engine);
    engineInitialized_ = true;
    appendAudioDebugLog(QString("initializeAudioEngine ok sampleRate=%1").arg(deviceSampleRate_));
    return true;
}

void QtPreviewSfxRuntime::initializeAssets()
{
    if (!engineInitialized_ || engineState_ == nullptr || sfxDir_.isEmpty()) {
        return;
    }

    const auto configureBank = [this](SfxBank& bank, const QString& filename, int voiceCount) {
        const QString path = QDir(sfxDir_).filePath(filename);
        if (!QFileInfo::exists(path)) {
            return;
        }
        const QByteArray pathBytes = QFile::encodeName(path);
        bank.voices.reserve(voiceCount);
        for (int i = 0; i < voiceCount; ++i) {
            Voice* voice = new Voice();
            if (ma_sound_init_from_file(
                    &engineState_->engine,
                    pathBytes.constData(),
                    0,
                    nullptr,
                    nullptr,
                    &voice->sound
                ) == MA_SUCCESS) {
                voice->initialized = true;
                bank.voices.append(voice);
            } else {
                delete voice;
            }
        }
        bank.configured = !bank.voices.isEmpty();
    };

    configureBank(answerSfx_, "answer.wav", 12);
    configureBank(slideSfx_, "slide.wav", 8);
    configureBank(breakSfx_, "break.wav", 8);
    configureBank(exSfx_, "judge_ex.wav", 8);
    configureBank(touchSfx_, "touch.wav", 12);

    const QString touchholdPath = QDir(sfxDir_).filePath("touchHold_riser.wav");
    if (QFileInfo::exists(touchholdPath)) {
        const QByteArray pathBytes = QFile::encodeName(touchholdPath);
        touchholdVoices_.reserve(8);
        for (int i = 0; i < 8; ++i) {
            TouchholdVoice touchholdVoice;
            touchholdVoice.voice = new Voice();
            if (ma_sound_init_from_file(
                    &engineState_->engine,
                    pathBytes.constData(),
                    0,
                    nullptr,
                    nullptr,
                    &touchholdVoice.voice->sound
                ) == MA_SUCCESS) {
                touchholdVoice.voice->initialized = true;
                ma_uint64 lengthFrames = 0;
                if (ma_sound_get_length_in_pcm_frames(&touchholdVoice.voice->sound, &lengthFrames) == MA_SUCCESS) {
                    touchholdSoundLengthFrames_ = qMax<quint64>(touchholdSoundLengthFrames_, lengthFrames);
                }
                touchholdVoices_.append(touchholdVoice);
            } else {
                delete touchholdVoice.voice;
            }
        }
    }

    applyVolumes();
}

void QtPreviewSfxRuntime::initializeBackgroundTrack()
{
    if (!engineInitialized_ || engineState_ == nullptr || trackPath_.isEmpty()) {
        return;
    }

    const QByteArray pathBytes = QFile::encodeName(trackPath_);
    Voice* voice = new Voice();
    if (ma_sound_init_from_file(
            &engineState_->engine,
            pathBytes.constData(),
            0,
            nullptr,
            nullptr,
            &voice->sound
        ) != MA_SUCCESS) {
        delete voice;
        appendAudioDebugLog(QString("initializeBackgroundTrack failed path=%1").arg(trackPath_));
        return;
    }
    voice->initialized = true;
    backgroundTrackVoice_ = voice;
    backgroundTrackConfigured_ = true;
    backgroundTrackRunning_ = false;
    backgroundTrackPendingStart_ = false;
    backgroundTrackLastTimelineSecond_ = 0.0;
    ma_sound_set_volume(&backgroundTrackVoice_->sound, static_cast<float>(settings_.bgmVolume));
    appendAudioDebugLog(QString("initializeBackgroundTrack ok path=%1 volume=%2")
                            .arg(trackPath_)
                            .arg(settings_.bgmVolume, 0, 'f', 3));
}

void QtPreviewSfxRuntime::applyVolumes()
{
    const auto applyVolume = [](SfxBank& bank, double volume) {
        for (Voice* voice : bank.voices) {
            if (voice != nullptr && voice->initialized) {
                ma_sound_set_volume(&voice->sound, static_cast<float>(volume));
            }
        }
    };

    applyVolume(answerSfx_, settings_.answerVolume);
    applyVolume(slideSfx_, settings_.slideVolume);
    applyVolume(breakSfx_, settings_.breakVolume);
    applyVolume(exSfx_, settings_.exVolume);
    applyVolume(touchSfx_, settings_.touchVolume);
    if (backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized) {
        ma_sound_set_volume(&backgroundTrackVoice_->sound, static_cast<float>(settings_.bgmVolume));
    }
    if (stretchedBackgroundState_ != nullptr && stretchedBackgroundState_->soundInitialized) {
        ma_sound_set_volume(&stretchedBackgroundState_->sound, static_cast<float>(settings_.bgmVolume));
    }
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice != nullptr && voice.voice->initialized) {
            ma_sound_set_volume(&voice.voice->sound, static_cast<float>(settings_.touchholdVolume));
        }
    }
}

bool QtPreviewSfxRuntime::prepareStretchedBackgroundTrack(double timelineSecond)
{
    Q_UNUSED(timelineSecond);
    if (qFuzzyCompare(backgroundTrackPlaybackRate_ + 1.0, 2.0)) {
        return false;
    }
    if (!engineInitialized_ || engineState_ == nullptr || trackPath_.isEmpty()) {
        return false;
    }
    if (stretchedBackgroundState_ != nullptr
        && stretchedBackgroundState_->soundInitialized
        && stretchedBackgroundState_->trackPath == trackPath_
        && qAbs(stretchedBackgroundState_->playbackRate - backgroundTrackPlaybackRate_) <= kQtPreviewSfxEpsilonSeconds) {
        return true;
    }
    resetStretchedBackgroundTrack();
    StretchedBackgroundState* state = new StretchedBackgroundState();
    state->trackPath = trackPath_;
    state->playbackRate = backgroundTrackPlaybackRate_;

    ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
    const QByteArray pathBytes = QFile::encodeName(trackPath_);
    const ma_result decoderInitResult = ma_decoder_init_file(pathBytes.constData(), &decoderConfig, &state->decoder);
    if (decoderInitResult != MA_SUCCESS) {
        appendAudioDebugLog(QString("prepareStretchedBackgroundTrack decoder_init_file failed rc=%1 track=%2")
                                .arg(static_cast<int>(decoderInitResult))
                                .arg(trackPath_));
        delete state;
        return false;
    }
    state->decoderInitialized = true;
    state->channels = state->decoder.outputChannels;
    state->sampleRate = state->decoder.outputSampleRate;
    if (state->channels == 0 || state->sampleRate == 0) {
        ma_result formatResult = ma_data_source_get_data_format(
            reinterpret_cast<ma_data_source*>(&state->decoder),
            nullptr,
            &state->channels,
            &state->sampleRate,
            nullptr,
            0
        );
        if (formatResult != MA_SUCCESS || state->channels == 0 || state->sampleRate == 0) {
            appendAudioDebugLog(QString("prepareStretchedBackgroundTrack get_data_format failed rc=%1 track=%2")
                                    .arg(static_cast<int>(formatResult))
                                    .arg(trackPath_));
            ma_decoder_uninit(&state->decoder);
            delete state;
            return false;
        }
    }
    state->stretcher.setSampleRate(state->sampleRate);
    state->stretcher.setChannels(state->channels);
    state->stretcher.setPitch(1.0f);
    state->stretcher.setTempo(static_cast<float>(state->playbackRate));
    state->decodeChunk.resize(static_cast<int>(4096 * state->channels));
    state->stretchChunk.resize(static_cast<int>(4096 * state->channels));

    ma_uint64 sourceLengthFrames = 0;
    if (ma_data_source_get_length_in_pcm_frames(
            reinterpret_cast<ma_data_source*>(&state->decoder),
            &sourceLengthFrames
        ) == MA_SUCCESS
        && sourceLengthFrames > 0) {
        state->sourceFrameCount = sourceLengthFrames;
        state->stretchedFrameCount = static_cast<ma_uint64>(
            qMax(1.0, static_cast<double>(qCeil(static_cast<double>(sourceLengthFrames) / state->playbackRate)))
        );
    }

    ma_data_source_config dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = StretchedBackgroundState::dataSourceVTable();
    if (ma_data_source_init(&dataSourceConfig, reinterpret_cast<ma_data_source*>(&state->dataSource)) != MA_SUCCESS) {
        appendAudioDebugLog("prepareStretchedBackgroundTrack data_source_init failed");
        ma_decoder_uninit(&state->decoder);
        delete state;
        return false;
    }

    const ma_result soundInitResult = ma_sound_init_from_data_source(
        &engineState_->engine,
        reinterpret_cast<ma_data_source*>(&state->dataSource),
        0,
        nullptr,
        &state->sound
    );
    if (soundInitResult != MA_SUCCESS) {
        appendAudioDebugLog(QString("prepareStretchedBackgroundTrack sound_init_from_data_source failed rc=%1")
                                .arg(static_cast<int>(soundInitResult)));
        ma_data_source_uninit(reinterpret_cast<ma_data_source*>(&state->dataSource));
        ma_decoder_uninit(&state->decoder);
        delete state;
        return false;
    }
    state->soundInitialized = true;
    ma_sound_set_volume(&state->sound, static_cast<float>(settings_.bgmVolume));
    stretchedBackgroundState_ = state;
    appendAudioDebugLog(QString("prepareStretchedBackgroundTrack ready track=%1 rate=%2 channels=%3 sampleRate=%4")
                            .arg(state->trackPath)
                            .arg(state->playbackRate, 0, 'f', 3)
                            .arg(state->channels)
                            .arg(state->sampleRate));
    return true;
}

double QtPreviewSfxRuntime::stretchedBackgroundPlaybackSecond() const
{
    if (stretchedBackgroundState_ == nullptr || !stretchedBackgroundState_->soundInitialized) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }
    if (!backgroundTrackRunning_) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }

    float cursorSeconds = 0.0f;
    if (ma_sound_get_cursor_in_seconds(&stretchedBackgroundState_->sound, &cursorSeconds) != MA_SUCCESS) {
        return qMax(0.0, backgroundTrackLastTimelineSecond_);
    }

    const double timelineSecond =
        (static_cast<double>(cursorSeconds) * backgroundTrackPlaybackRate_) - backgroundTrackOffsetSeconds_;
    return qMax(0.0, timelineSecond);
}

bool QtPreviewSfxRuntime::playKindInternal(const QString& kind, double gain)
{
    const QString lowered = kind.trimmed().toLower();
    if (lowered.isEmpty()) {
        return false;
    }

    SfxBank* bank = nullptr;
    double volume = 0.0;
    if (lowered == "answer") {
        bank = &answerSfx_;
        volume = settings_.answerVolume;
    } else if (lowered == "slide") {
        bank = &slideSfx_;
        volume = settings_.slideVolume;
    } else if (lowered == "break") {
        bank = &breakSfx_;
        volume = settings_.breakVolume;
    } else if (lowered == "ex") {
        bank = &exSfx_;
        volume = settings_.exVolume;
    } else if (lowered == "touch") {
        bank = &touchSfx_;
        volume = settings_.touchVolume;
    } else if (lowered == "touchhold") {
        return playTouchholdAudition();
    }

    if (bank == nullptr || !bank->configured || bank->voices.isEmpty()) {
        return false;
    }
    if (volume <= 0.0) {
        return true;
    }

    Voice* voice = bank->voices[bank->nextVoice % bank->voices.size()];
    bank->nextVoice = (bank->nextVoice + 1) % bank->voices.size();
    if (voice == nullptr || !voice->initialized) {
        return true;
    }
    const double effectiveGain = qMax(0.0, gain);
    const double effectiveVolume = qBound(0.0, volume * effectiveGain, 1.0);
    ma_sound_stop(&voice->sound);
    ma_sound_set_volume(&voice->sound, static_cast<float>(effectiveVolume));
    ma_sound_seek_to_pcm_frame(&voice->sound, 0);
    ma_sound_start(&voice->sound);
    return true;
}

void QtPreviewSfxRuntime::startTouchholdSpan(int spanIndex, double offsetSeconds)
{
    if (settings_.touchholdVolume <= 0.0) {
        return;
    }
    if (spanIndex < 0 || spanIndex >= touchholdSpans_.size()) {
        return;
    }
    if (touchholdVoices_.isEmpty()) {
        return;
    }

    const TouchholdSpan& span = touchholdSpans_[spanIndex];
    if (span.endSecond <= span.startSecond) {
        return;
    }

    const ma_uint64 offsetFrames = static_cast<ma_uint64>(qMax(0.0, offsetSeconds) * deviceSampleRate_);
    if (touchholdSoundLengthFrames_ > 0 && offsetFrames >= touchholdSoundLengthFrames_) {
        return;
    }

    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.activeSpanIndex == spanIndex) {
            if (voice.voice == nullptr || !voice.voice->initialized) {
                return;
            }
            ma_sound_stop(&voice.voice->sound);
            ma_sound_seek_to_pcm_frame(&voice.voice->sound, offsetFrames);
            ma_sound_start(&voice.voice->sound);
            return;
        }
    }

    TouchholdVoice* freeVoice = nullptr;
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.activeSpanIndex < 0) {
            freeVoice = &voice;
            break;
        }
    }
    if (freeVoice == nullptr) {
        freeVoice = &touchholdVoices_.first();
    }
    if (freeVoice == nullptr || freeVoice->voice == nullptr || !freeVoice->voice->initialized) {
        return;
    }

    ma_sound_stop(&freeVoice->voice->sound);
    ma_sound_seek_to_pcm_frame(&freeVoice->voice->sound, offsetFrames);
    ma_sound_start(&freeVoice->voice->sound);
    freeVoice->activeSpanIndex = spanIndex;
}

void QtPreviewSfxRuntime::stopTouchholdSpan(int spanIndex)
{
    if (spanIndex < 0) {
        return;
    }
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.activeSpanIndex != spanIndex) {
            continue;
        }
        if (voice.voice != nullptr && voice.voice->initialized) {
            ma_sound_stop(&voice.voice->sound);
        }
        voice.activeSpanIndex = -1;
        return;
    }
}

bool QtPreviewSfxRuntime::playTouchholdAudition()
{
    if (settings_.touchholdVolume <= 0.0 || touchholdVoices_.isEmpty()) {
        return !touchholdVoices_.isEmpty();
    }

    TouchholdVoice* voiceToUse = nullptr;
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.activeSpanIndex < 0) {
            voiceToUse = &voice;
            break;
        }
    }
    if (voiceToUse == nullptr) {
        voiceToUse = &touchholdVoices_.first();
    }
    if (voiceToUse == nullptr || voiceToUse->voice == nullptr || !voiceToUse->voice->initialized) {
        return false;
    }

    voiceToUse->activeSpanIndex = -1;
    ma_sound_stop(&voiceToUse->voice->sound);
    ma_sound_seek_to_pcm_frame(&voiceToUse->voice->sound, 0);
    ma_sound_start(&voiceToUse->voice->sound);
    return true;
}

