#include "QtPreviewSfxRuntime.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtMath>

#define MINIAUDIO_IMPLEMENTATION
#include "../third_party/miniaudio/miniaudio.h"

namespace {
constexpr double kQtPreviewSfxEpsilonSeconds = 1e-6;
}

struct QtPreviewSfxRuntime::EngineState {
    ma_engine engine{};
};

struct QtPreviewSfxRuntime::Voice {
    ma_sound sound{};
    bool initialized = false;
};

QtPreviewSfxRuntime::QtPreviewSfxRuntime(QObject* parent)
    : QObject(parent)
{
}

QtPreviewSfxRuntime::~QtPreviewSfxRuntime()
{
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
    initializeBackgroundTrack();
}

void QtPreviewSfxRuntime::setChartPath(const QString& chartPath)
{
    const QString normalizedChartPath = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    if (normalizedChartPath == chartPath_) {
        return;
    }

    chartPath_ = normalizedChartPath;
    trackPath_ = resolveTrackPath(chartPath_);
    resetBackgroundTrack();
    initializeBackgroundTrack();
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
    return backgroundTrackConfigured_ && backgroundTrackVoice_ != nullptr && backgroundTrackVoice_->initialized;
}

void QtPreviewSfxRuntime::startBackgroundTrack(double second)
{
    if (!hasBackgroundTrack()) {
        return;
    }
    ma_sound_stop(&backgroundTrackVoice_->sound);
    ma_sound_seek_to_second(&backgroundTrackVoice_->sound, static_cast<float>(qMax(0.0, second)));
    ma_sound_start(&backgroundTrackVoice_->sound);
}

void QtPreviewSfxRuntime::pauseBackgroundTrack()
{
    if (!hasBackgroundTrack()) {
        return;
    }
    ma_sound_stop(&backgroundTrackVoice_->sound);
}

double QtPreviewSfxRuntime::backgroundPlaybackSecond() const
{
    if (!hasBackgroundTrack()) {
        return 0.0;
    }
    float cursorSeconds = 0.0f;
    if (ma_sound_get_cursor_in_seconds(&backgroundTrackVoice_->sound, &cursorSeconds) != MA_SUCCESS) {
        return 0.0;
    }
    return qMax(0.0, static_cast<double>(cursorSeconds));
}

bool QtPreviewSfxRuntime::audition(const QString& kind)
{
    return playKindInternal(kind);
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
        qEnvironmentVariable("MAICODE_PREVIEW_SFX_DIR", qEnvironmentVariable("MAIMURI_PREVIEW_SFX_DIR")).trimmed()
    );
    if (!envDir.isEmpty() && QFileInfo::exists(QDir(envDir).filePath("answer.wav"))) {
        return envDir;
    }

    QStringList candidates;
    const QDir currentDir = QDir::current();
    candidates << QDir::cleanPath(currentDir.filePath("test/SFX"));
    candidates << QDir::cleanPath(currentDir.filePath("../test/SFX"));
    candidates << QDir::cleanPath(currentDir.filePath("tmp/SFX"));
    candidates << QDir::cleanPath(currentDir.filePath("../tmp/SFX"));

    const QDir appDir(QCoreApplication::applicationDirPath());
    candidates << QDir::cleanPath(appDir.filePath("test/SFX"));
    candidates << QDir::cleanPath(appDir.filePath("..\\test\\SFX"));
    candidates << QDir::cleanPath(appDir.filePath("..\\..\\test\\SFX"));
    candidates << QDir::cleanPath(appDir.filePath("..\\..\\..\\test\\SFX"));
    candidates << QDir::cleanPath(appDir.filePath("tmp\\SFX"));
    candidates << QDir::cleanPath(appDir.filePath("..\\tmp\\SFX"));
    candidates << QDir::cleanPath(appDir.filePath("..\\..\\tmp\\SFX"));
    candidates << QDir::cleanPath(appDir.filePath("..\\..\\..\\tmp\\SFX"));

    for (const QString& path : candidates) {
        if (QFileInfo::exists(QDir(path).filePath("answer.wav"))) {
            return path;
        }
    }
    return QString();
}

void QtPreviewSfxRuntime::resetBackgroundTrack()
{
    if (backgroundTrackVoice_ != nullptr) {
        if (backgroundTrackVoice_->initialized) {
            ma_sound_uninit(&backgroundTrackVoice_->sound);
        }
        delete backgroundTrackVoice_;
        backgroundTrackVoice_ = nullptr;
    }
    backgroundTrackConfigured_ = false;
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
        return false;
    }
    deviceSampleRate_ = ma_engine_get_sample_rate(&engineState_->engine);
    engineInitialized_ = true;
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
        return;
    }
    voice->initialized = true;
    backgroundTrackVoice_ = voice;
    backgroundTrackConfigured_ = true;
    ma_sound_set_volume(&backgroundTrackVoice_->sound, static_cast<float>(settings_.bgmVolume));
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
    for (TouchholdVoice& voice : touchholdVoices_) {
        if (voice.voice != nullptr && voice.voice->initialized) {
            ma_sound_set_volume(&voice.voice->sound, static_cast<float>(settings_.touchholdVolume));
        }
    }
}

bool QtPreviewSfxRuntime::playKindInternal(const QString& kind)
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
    ma_sound_stop(&voice->sound);
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
