#include "BassPreviewAudioBackend.h"

#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewSfxTimeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtMath>

#ifdef Q_OS_WIN
#include <windows.h>

#include "bass.h"
#include "bassmix.h"
#endif

namespace {

constexpr double kBassPreviewEpsilonSeconds = miacode::preview_sfx_timeline::kTimelineEpsilonSeconds;
constexpr double kBassPreviewMinRate = 0.25;
constexpr double kBassPreviewMaxRate = 2.0;
constexpr double kBassPreviewStatusLogIntervalSeconds = 0.5;
constexpr DWORD kBassPreviewTempoFlags = 0x10000 | BASS_STREAM_DECODE; // BASS_FX_FREESOURCE | BASS_STREAM_DECODE
constexpr DWORD kBassPreviewTempoAttribute = 0x10000; // BASS_ATTRIB_TEMPO

bool runtimeAudioDebugEnabled()
{
    return miacode::debug_options::audioDebugOutputEnabled();
}

void appendAudioDebugLog(const QString& message)
{
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Audio, QString(), message);
}

QString runtimeFilePath(const QString& fileName)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
}

bool runtimeLibraryExists(const QString& fileName)
{
    return QFileInfo::exists(runtimeFilePath(fileName));
}

double clampTimelineSecond(double second)
{
    return qIsFinite(second) ? qMax(0.0, second) : 0.0;
}

float clampSampleVolume(double value)
{
    if (!qIsFinite(value)) {
        return 1.0f;
    }
    return static_cast<float>(qBound(0.0, value, 2.0));
}

#ifdef Q_OS_WIN
typedef DWORD (WINAPI* BassFxTempoCreateProc)(DWORD handle, DWORD flags);
int gBassDeviceRefCount = 0;
#endif

}  // namespace

#ifdef Q_OS_WIN

struct BassPreviewAudioBackend::Sample {
    QString name;
    QString kind;
    QString path;
    QByteArray bytes;
    DWORD source = 0;
    DWORD resampler = 0;
    double gain = 1.0;
    double lengthSeconds = 0.0;
    bool speedChangeSupported = false;
    float baseVolume = 1.0f;

    bool valid() const
    {
        return source != 0 && resampler != 0;
    }

    void free()
    {
        if (source != 0) {
            BASS_Mixer_ChannelFlags(source, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        }
        if (resampler != 0) {
            BASS_Mixer_ChannelRemove(resampler);
            BASS_StreamFree(resampler);
            resampler = 0;
        }
        if (source != 0) {
            BASS_StreamFree(source);
            source = 0;
        }
        bytes.clear();
        lengthSeconds = 0.0;
    }

    void applyVolume(double eventGain = 1.0)
    {
        if (!valid()) {
            return;
        }
        const float effective = clampSampleVolume(baseVolume * qMax(0.0, eventGain));
        BASS_ChannelSetAttribute(source, BASS_ATTRIB_VOL, effective);
    }

    bool create(
        BassPreviewAudioBackend* backend,
        const QString& samplePath,
        const QString& sampleName,
        const QString& sampleKind,
        bool normalize,
        bool speedChange
    )
    {
        Q_UNUSED(normalize);
        free();

        path = samplePath;
        name = sampleName;
        kind = sampleKind;
        speedChangeSupported = speedChange;

        QFile file(samplePath);
        if (!file.open(QIODevice::ReadOnly)) {
            appendAudioDebugLog(QString("bass_sample_open_failed kind=%1 path=%2").arg(kind, samplePath));
            return false;
        }
        bytes = file.readAll();
        file.close();
        if (bytes.isEmpty()) {
            appendAudioDebugLog(QString("bass_sample_empty kind=%1 path=%2").arg(kind, samplePath));
            return false;
        }

        DWORD stream = BASS_StreamCreateFile(
            TRUE,
            bytes.constData(),
            0,
            static_cast<QWORD>(bytes.size()),
            BASS_STREAM_DECODE | BASS_STREAM_PRESCAN | BASS_ASYNCFILE
        );
        if (stream == 0) {
            appendAudioDebugLog(
                QString("bass_sample_stream_create_failed kind=%1 path=%2 err=%3")
                    .arg(kind, samplePath)
                    .arg(static_cast<int>(BASS_ErrorGetCode())));
            return false;
        }

        if (speedChangeSupported) {
            const auto tempoCreate = reinterpret_cast<BassFxTempoCreateProc>(backend->bassFxTempoCreate_);
            if (tempoCreate == nullptr) {
                BASS_StreamFree(stream);
                appendAudioDebugLog(QString("bass_fx_tempo_missing kind=%1 path=%2").arg(kind, samplePath));
                return false;
            }
            const DWORD tempoStream = tempoCreate(stream, kBassPreviewTempoFlags);
            if (tempoStream == 0) {
                const int errorCode = static_cast<int>(BASS_ErrorGetCode());
                BASS_StreamFree(stream);
                appendAudioDebugLog(
                    QString("bass_tempo_create_failed kind=%1 path=%2 err=%3")
                        .arg(kind, samplePath)
                        .arg(errorCode));
                return false;
            }
            stream = tempoStream;
        }

        float channelFrequency = static_cast<float>(backend->deviceSampleRate_);
        BASS_ChannelGetAttribute(stream, BASS_ATTRIB_FREQ, &channelFrequency);
        const DWORD resamplerFlags = BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_MIXER_NONSTOP;
        const DWORD resamplerStream =
            BASS_Mixer_StreamCreate(
                static_cast<DWORD>(qMax(1.0f, channelFrequency)),
                miacode::preview_audio::kMixChannels,
                resamplerFlags);
        if (resamplerStream == 0) {
            const int errorCode = static_cast<int>(BASS_ErrorGetCode());
            BASS_StreamFree(stream);
            appendAudioDebugLog(
                QString("bass_resampler_create_failed kind=%1 path=%2 err=%3")
                    .arg(kind, samplePath)
                    .arg(errorCode));
            return false;
        }

        BASS_ChannelSetAttribute(resamplerStream, BASS_ATTRIB_BUFFER, 0.0f);
        BASS_Mixer_StreamAddChannel(resamplerStream, stream, 0);
        BASS_Mixer_ChannelFlags(stream, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        BASS_Mixer_StreamAddChannel(backend->masterMixer_, resamplerStream, 0);
        BASS_ChannelSetPosition(stream, 0, BASS_POS_BYTE);

        source = stream;
        resampler = resamplerStream;

        const QWORD lengthBytes = BASS_ChannelGetLength(source, BASS_POS_BYTE);
        if (lengthBytes != static_cast<QWORD>(-1)) {
            lengthSeconds = BASS_ChannelBytes2Seconds(source, lengthBytes);
        }
        applyVolume();
        return true;
    }

    void setCurrentSec(double seconds)
    {
        if (!valid()) {
            return;
        }
        const double clamped = qBound(0.0, qIsFinite(seconds) ? seconds : 0.0, lengthSeconds);
        const QWORD position = BASS_ChannelSeconds2Bytes(source, clamped);
        BASS_ChannelSetPosition(source, position, BASS_POS_BYTE);
    }

    double currentSec() const
    {
        if (!valid()) {
            return 0.0;
        }
        const QWORD position = BASS_Mixer_ChannelGetPosition(source, BASS_POS_BYTE);
        return BASS_ChannelBytes2Seconds(source, position);
    }

    void setLoop(bool loop)
    {
        if (!valid()) {
            return;
        }
        BASS_ChannelFlags(source, loop ? BASS_SAMPLE_LOOP : 0, BASS_SAMPLE_LOOP);
    }

    void setSpeed(double rate)
    {
        if (!valid() || !speedChangeSupported) {
            return;
        }
        const float tempo = static_cast<float>((qBound(kBassPreviewMinRate, rate, kBassPreviewMaxRate) - 1.0) * 100.0);
        BASS_ChannelSetAttribute(source, kBassPreviewTempoAttribute, tempo);
    }

    bool isPlaying() const
    {
        if (!valid()) {
            return false;
        }
        if (BASS_Mixer_ChannelIsActive(source) != BASS_ACTIVE_PLAYING) {
            return false;
        }
        const DWORD flags = BASS_Mixer_ChannelFlags(source, 0, 0);
        return (flags & BASS_MIXER_CHAN_PAUSE) == 0;
    }

    void play()
    {
        if (!valid()) {
            return;
        }
        BASS_Mixer_ChannelFlags(source, 0, BASS_MIXER_CHAN_PAUSE);
    }

    void playOneShot(double eventGain)
    {
        if (!valid()) {
            return;
        }
        applyVolume(eventGain);
        BASS_Mixer_ChannelSetPosition(source, 0, BASS_POS_BYTE);
        BASS_Mixer_ChannelFlags(source, 0, BASS_MIXER_CHAN_PAUSE);
    }

    void pause()
    {
        if (!valid()) {
            return;
        }
        BASS_Mixer_ChannelFlags(source, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
    }

    void stop()
    {
        if (!valid()) {
            return;
        }
        BASS_Mixer_ChannelFlags(source, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        BASS_Mixer_ChannelSetPosition(source, 0, BASS_POS_BYTE);
    }
};

#endif

BassPreviewAudioBackend::BassPreviewAudioBackend(QObject* parent)
    : QObject(parent)
{
    appendAudioDebugLog("BassPreviewAudioBackend created");
}

BassPreviewAudioBackend::~BassPreviewAudioBackend()
{
    appendAudioDebugLog("BassPreviewAudioBackend destroying");
#ifdef Q_OS_WIN
    stopPlaybackSession();
    resetAssets();
    unloadOptionalPlugins();
    if (masterMixer_ != 0) {
        BASS_StreamFree(masterMixer_);
        masterMixer_ = 0;
    }
    unloadBassFx();
    if (registeredBassDeviceRef_ && gBassDeviceRefCount > 0) {
        --gBassDeviceRefCount;
        registeredBassDeviceRef_ = false;
    }
    if (engineInitialized_ && gBassDeviceRefCount == 0) {
        BASS_Stop();
        BASS_Free();
        engineInitialized_ = false;
    } else if (engineInitialized_) {
        engineInitialized_ = false;
    }
#endif
}

QString BassPreviewAudioBackend::backendId() const
{
    return QStringLiteral("bass");
}

QString BassPreviewAudioBackend::resolveTrackPath(const QString& chartPath) const
{
    const QString normalizedChartPath = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    if (!warmupPaths_.chartPath.isEmpty() && normalizedChartPath == warmupPaths_.chartPath) {
        return warmupPaths_.trackPath;
    }
    return miacode::chart_assets::resolveTrackPath(chartPath);
}

QString BassPreviewAudioBackend::resolveSfxDir() const
{
    if (!warmupPaths_.sfxDir.isEmpty()) {
        return warmupPaths_.sfxDir;
    }
    return miacode::preview_sfx::resolveSfxDirectory();
}

bool BassPreviewAudioBackend::runtimeLibrariesPresent() const
{
#ifdef Q_OS_WIN
    return runtimeLibraryExists(QStringLiteral("bass.dll"))
        && runtimeLibraryExists(QStringLiteral("bassmix.dll"))
        && runtimeLibraryExists(QStringLiteral("bass_fx.dll"));
#else
    return false;
#endif
}

bool BassPreviewAudioBackend::canBePrimary(QString* reason) const
{
#ifdef Q_OS_WIN
    if (!runtimeLibrariesPresent()) {
        if (reason != nullptr) {
            *reason = QStringLiteral("missing repo-local BASS runtime libraries");
        }
        return false;
    }
    if (reason != nullptr) {
        *reason = QStringLiteral("repo-local BASS runtime libraries are available");
    }
    return true;
#else
    if (reason != nullptr) {
        *reason = QStringLiteral("Bass preview backend is Windows-first");
    }
    return false;
#endif
}

void BassPreviewAudioBackend::setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir)
{
    warmupPaths_.chartPath = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    warmupPaths_.trackPath = trackPath.isEmpty() ? QString() : QDir::cleanPath(trackPath);
    warmupPaths_.sfxDir = sfxDir.isEmpty() ? QString() : QDir::cleanPath(sfxDir);
}

bool BassPreviewAudioBackend::ensureBassFxLoaded()
{
#ifdef Q_OS_WIN
    if (bassFxTempoCreate_ != nullptr) {
        return true;
    }
    const QString libraryPath = runtimeFilePath(QStringLiteral("bass_fx.dll"));
    const HMODULE module = LoadLibraryW(reinterpret_cast<LPCWSTR>(libraryPath.utf16()));
    if (module == nullptr) {
        appendAudioDebugLog(QString("bass_fx_load_failed path=%1").arg(libraryPath));
        return false;
    }
    FARPROC proc = GetProcAddress(module, "BASS_FX_TempoCreate");
    if (proc == nullptr) {
        FreeLibrary(module);
        appendAudioDebugLog(QString("bass_fx_symbol_missing path=%1").arg(libraryPath));
        return false;
    }
    bassFxModule_ = module;
    bassFxTempoCreate_ = reinterpret_cast<void*>(proc);
    return true;
#else
    return false;
#endif
}

void BassPreviewAudioBackend::unloadBassFx()
{
#ifdef Q_OS_WIN
    if (bassFxModule_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(bassFxModule_));
    }
#endif
    bassFxModule_ = nullptr;
    bassFxTempoCreate_ = nullptr;
}

void BassPreviewAudioBackend::loadOptionalPlugins()
{
#ifdef Q_OS_WIN
    if (pluginAac_ == 0) {
        const QString aacPath = runtimeFilePath(QStringLiteral("bass_aac.dll"));
        if (QFileInfo::exists(aacPath)) {
            pluginAac_ = BASS_PluginLoad(reinterpret_cast<const WCHAR*>(aacPath.utf16()), 0);
        }
    }
    if (pluginOpus_ == 0) {
        const QString opusPath = runtimeFilePath(QStringLiteral("bassopus.dll"));
        if (QFileInfo::exists(opusPath)) {
            pluginOpus_ = BASS_PluginLoad(reinterpret_cast<const WCHAR*>(opusPath.utf16()), 0);
        }
    }
#endif
}

void BassPreviewAudioBackend::unloadOptionalPlugins()
{
#ifdef Q_OS_WIN
    if (pluginAac_ != 0) {
        BASS_PluginFree(pluginAac_);
        pluginAac_ = 0;
    }
    if (pluginOpus_ != 0) {
        BASS_PluginFree(pluginOpus_);
        pluginOpus_ = 0;
    }
#endif
}

bool BassPreviewAudioBackend::initializeAudioEngine()
{
#ifndef Q_OS_WIN
    return false;
#else
    if (engineInitialized_ && masterMixer_ != 0) {
        return true;
    }
    if (!ensureBassFxLoaded()) {
        return false;
    }
    if (!registeredBassDeviceRef_) {
        if (gBassDeviceRefCount == 0) {
            if (!BASS_Init(-1, static_cast<int>(deviceSampleRate_), 0, nullptr, nullptr)) {
                appendAudioDebugLog(QString("bass_init_failed err=%1").arg(static_cast<int>(BASS_ErrorGetCode())));
                return false;
            }
        }
        ++gBassDeviceRefCount;
        registeredBassDeviceRef_ = true;
    }
    loadOptionalPlugins();
    masterMixer_ = BASS_Mixer_StreamCreate(
        deviceSampleRate_,
        miacode::preview_audio::kMixChannels,
        BASS_SAMPLE_FLOAT | BASS_MIXER_NONSTOP | BASS_MIXER_POSEX);
    if (masterMixer_ == 0) {
        appendAudioDebugLog(QString("bass_master_mixer_failed err=%1").arg(static_cast<int>(BASS_ErrorGetCode())));
        if (registeredBassDeviceRef_ && gBassDeviceRefCount > 0) {
            --gBassDeviceRefCount;
            registeredBassDeviceRef_ = false;
        }
        if (gBassDeviceRefCount == 0) {
            BASS_Free();
        }
        return false;
    }
    BASS_ChannelSetAttribute(masterMixer_, BASS_ATTRIB_BUFFER, 0.0f);
    BASS_ChannelSetAttribute(masterMixer_, BASS_ATTRIB_MIXER_THREADS, 8.0f);
    engineInitialized_ = true;
    appendAudioDebugLog(QString("bass_engine_ready sample_rate=%1").arg(deviceSampleRate_));
    return true;
#endif
}

void BassPreviewAudioBackend::refreshPreparedAssets()
{
    preparedAssets_.trackPath = resolveTrackPath(preparedAssets_.chartPath);
    preparedAssets_.sfxDir = resolveSfxDir();
}

void BassPreviewAudioBackend::resetAssets()
{
#ifdef Q_OS_WIN
    samplesByKind_.clear();
    backgroundTrackSample_ = nullptr;
    touchholdSample_ = nullptr;

    auto resetSample = [](std::unique_ptr<Sample>& sample) {
        if (sample) {
            sample->free();
            sample.reset();
        }
    };

    resetSample(answerSample_);
    resetSample(judgeSample_);
    resetSample(judgeBreakSample_);
    resetSample(slideSample_);
    resetSample(breakSample_);
    resetSample(breakSlideStartSample_);
    resetSample(breakSlideFinishSample_);
    resetSample(judgeBreakSlideSample_);
    resetSample(exSample_);
    resetSample(touchSample_);
    resetSample(touchholdSampleOwner_);
    resetSample(fireworkSample_);
    resetSample(backgroundTrackSampleOwner_);
#endif
}

void BassPreviewAudioBackend::initializeAssets()
{
#ifdef Q_OS_WIN
    resetAssets();
    if (!engineInitialized_ || masterMixer_ == 0) {
        return;
    }

    const auto loadSample = [this](std::unique_ptr<Sample>& slot, const QString& kind, bool speedChange, bool requiredForMap) {
        const QString path = miacode::preview_sfx::assetFilePathForKind(preparedAssets_.sfxDir, kind);
        if (!QFileInfo::exists(path)) {
            return;
        }
        slot = std::make_unique<Sample>();
        if (!slot->create(this, path, kind, kind, false, speedChange)) {
            slot.reset();
            return;
        }
        if (requiredForMap) {
            samplesByKind_.insert(kind, slot.get());
        }
    };

    loadSample(answerSample_, QStringLiteral("answer"), false, true);
    loadSample(judgeSample_, QStringLiteral("judge"), false, true);
    loadSample(judgeBreakSample_, QStringLiteral("judge_break"), false, true);
    loadSample(slideSample_, QStringLiteral("slide"), false, true);
    loadSample(breakSample_, QStringLiteral("break"), false, true);
    loadSample(breakSlideStartSample_, QStringLiteral("break_slide_start"), false, true);
    loadSample(breakSlideFinishSample_, QStringLiteral("break_slide_finish"), false, true);
    loadSample(judgeBreakSlideSample_, QStringLiteral("judge_break_slide"), false, true);
    loadSample(exSample_, QStringLiteral("ex"), false, true);
    loadSample(touchSample_, QStringLiteral("touch"), false, true);
    loadSample(touchholdSampleOwner_, QStringLiteral("touchhold"), false, false);
    loadSample(fireworkSample_, QStringLiteral("firework"), false, true);

    samplesByKind_.insert(QStringLiteral("break_touch"), judgeBreakSample_.get());
    samplesByKind_.insert(QStringLiteral("break_slide"), breakSlideStartSample_.get());

    if (!preparedAssets_.trackPath.isEmpty() && QFileInfo::exists(preparedAssets_.trackPath)) {
        backgroundTrackSampleOwner_ = std::make_unique<Sample>();
        if (backgroundTrackSampleOwner_->create(
                this,
                preparedAssets_.trackPath,
                QStringLiteral("bgm"),
                QStringLiteral("bgm"),
                false,
                true)) {
            backgroundTrackSample_ = backgroundTrackSampleOwner_.get();
            backgroundTrackSample_->setLoop(false);
            backgroundTrackSample_->setSpeed(playbackSession_.backgroundTrackPlaybackRate);
        } else {
            backgroundTrackSampleOwner_.reset();
            backgroundTrackSample_ = nullptr;
        }
    }

    touchholdSample_ = touchholdSampleOwner_.get();
    applySampleLevels();
#endif
}

void BassPreviewAudioBackend::applySampleLevels()
{
#ifdef Q_OS_WIN
    const auto apply = [](Sample* sample, double volume) {
        if (sample != nullptr) {
            sample->baseVolume = clampSampleVolume(volume);
            sample->applyVolume();
        }
    };

    apply(answerSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("answer")));
    apply(judgeSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("judge")));
    apply(judgeBreakSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("judge_break")));
    apply(slideSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("slide")));
    apply(breakSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("break")));
    apply(breakSlideStartSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("break_slide_start")));
    apply(breakSlideFinishSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("break_slide_finish")));
    apply(judgeBreakSlideSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("judge_break_slide")));
    apply(exSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("ex")));
    apply(touchSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("touch")));
    apply(touchholdSample_, previewSfxVolumeForKind(settings_, QStringLiteral("touchhold")));
    apply(fireworkSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("firework")));
    apply(backgroundTrackSample_, previewTrackVolume(settings_));
#endif
}

BassPreviewAudioBackend::Sample* BassPreviewAudioBackend::sampleForKind(const QString& kind) const
{
    return samplesByKind_.value(previewSfxNormalizedKind(kind), nullptr);
}

void BassPreviewAudioBackend::setBackgroundTrackSampleSpeed(double rate)
{
#ifdef Q_OS_WIN
    if (backgroundTrackSample_ != nullptr) {
        backgroundTrackSample_->setSpeed(rate);
    }
#else
    Q_UNUSED(rate);
#endif
}

void BassPreviewAudioBackend::reloadAssets(const PreviewAudioSettings& settings)
{
    settings_ = settings;
    settings_.normalize();
    refreshPreparedAssets();
    if (!initializeAudioEngine()) {
        return;
    }
    initializeAssets();
}

bool BassPreviewAudioBackend::audioEngineInitialized() const
{
    return engineInitialized_ && masterMixer_ != 0;
}

void BassPreviewAudioBackend::setChartPath(const QString& chartPath)
{
    const QString normalized = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    if (preparedAssets_.chartPath == normalized) {
        return;
    }
    preparedAssets_.chartPath = normalized;
    refreshPreparedAssets();
    if (engineInitialized_) {
        initializeAssets();
    }
}

void BassPreviewAudioBackend::setBackgroundTrackOffsetSeconds(double seconds)
{
    playbackSession_.backgroundTrackOffsetSeconds = qIsFinite(seconds) ? seconds : 0.0;
}

void BassPreviewAudioBackend::setBackgroundTrackPlaybackRate(double rate)
{
    playbackSession_.backgroundTrackPlaybackRate =
        qBound(kBassPreviewMinRate, qIsFinite(rate) ? rate : 1.0, kBassPreviewMaxRate);
    setBackgroundTrackSampleSpeed(playbackSession_.backgroundTrackPlaybackRate);
}

void BassPreviewAudioBackend::applyLevels(const PreviewAudioSettings& settings)
{
    settings_ = settings;
    settings_.normalize();
    applySampleLevels();
}

void BassPreviewAudioBackend::clearPreparedTimeline()
{
    preparedTimeline_ = TimelineProgramState();
    preparedGroups_.clear();
}

void BassPreviewAudioBackend::rebuildPreparedGroups()
{
    preparedGroups_.clear();
    int index = 0;
    while (index < preparedTimeline_.events.size()) {
        const int groupEnd = miacode::preview_sfx_timeline::eventGroupEndIndex(preparedTimeline_.events, index);
        preparedGroups_.append(
            miacode::preview_sfx_timeline::collapseEventGroup(preparedTimeline_.events, index, groupEnd));
        index = groupEnd;
    }
}

void BassPreviewAudioBackend::rebuildPreparedTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    timingSettings_ = timingSettings;
    timingSettings_.normalize();
    preparedTimeline_.sourceNoteMarkers = noteMarkers;
    preparedTimelinePlaybackRate_ = playbackRate;
    miacode::preview_sfx_timeline::buildTimeline(
        noteMarkers,
        playbackRate,
        timingSettings_,
        &preparedTimeline_.events,
        &preparedTimeline_.touchholdSpans);
    rebuildPreparedGroups();
}

void BassPreviewAudioBackend::configureTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
}

void BassPreviewAudioBackend::clearTimeline()
{
    clearPreparedTimeline();
    stopAll();
}

void BassPreviewAudioBackend::setPlaybackTransactionId(quint64 transactionId)
{
    playbackTransactionId_ = transactionId;
}

void BassPreviewAudioBackend::resetMasterMixerClock(double startSecond)
{
#ifdef Q_OS_WIN
    if (masterMixer_ == 0) {
        return;
    }
    clearScheduledGroupSync();
    BASS_ChannelStop(masterMixer_);
    BASS_ChannelSetPosition(masterMixer_, 0, BASS_POS_BYTE);
    playbackSession_.sessionStartSecond = clampTimelineSecond(startSecond);
    playbackSession_.sessionPlaybackRate = qBound(
        kBassPreviewMinRate,
        qIsFinite(playbackSession_.backgroundTrackPlaybackRate) ? playbackSession_.backgroundTrackPlaybackRate : 1.0,
        kBassPreviewMaxRate);
    playbackSession_.lastAuthoritativeSecond = playbackSession_.sessionStartSecond;
    playbackSession_.lastStatusLogSecond = -1.0;
    playbackSession_.lastTriggeredGroupSecond = -1.0;
    playbackSession_.lastTriggeredGroupIndex = -1;
    playbackSession_.triggeredGroupCount = 0;
    playbackSession_.masterRunning = false;
#else
    Q_UNUSED(startSecond);
#endif
}

void BassPreviewAudioBackend::stopAllSamples()
{
#ifdef Q_OS_WIN
    const Sample* uniqueSamples[] = {
        answerSample_.get(),
        judgeSample_.get(),
        judgeBreakSample_.get(),
        slideSample_.get(),
        breakSample_.get(),
        breakSlideStartSample_.get(),
        breakSlideFinishSample_.get(),
        judgeBreakSlideSample_.get(),
        exSample_.get(),
        touchSample_.get(),
        touchholdSample_,
        fireworkSample_.get(),
        backgroundTrackSample_
    };
    for (const Sample* samplePtr : uniqueSamples) {
        Sample* sample = const_cast<Sample*>(samplePtr);
        if (sample != nullptr) {
            sample->stop();
        }
    }
#endif
}

void BassPreviewAudioBackend::stopPlaybackSession()
{
    clearScheduledGroupSync();
    stopAllSamples();
    resetMasterMixerClock(playbackSession_.lastAuthoritativeSecond);
    playbackSession_.backgroundTrackRunning = false;
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackPendingStartSecond = playbackSession_.lastAuthoritativeSecond;
}

void BassPreviewAudioBackend::clearScheduledGroupSync()
{
#ifdef Q_OS_WIN
    QMutexLocker locker(&schedulerMutex_);
    if (scheduledGroupSync_ != 0 && masterMixer_ != 0) {
        BASS_ChannelRemoveSync(masterMixer_, scheduledGroupSync_);
    }
    scheduledGroupSync_ = 0;
    scheduledGroupIndex_ = -1;
#endif
}

void BassPreviewAudioBackend::armNextGroupSync(double currentSecond)
{
#ifdef Q_OS_WIN
    if (masterMixer_ == 0 || !playbackSession_.masterRunning) {
        return;
    }

    QMutexLocker locker(&schedulerMutex_);
    if (scheduledGroupSync_ != 0) {
        return;
    }

    for (int index = playbackSession_.eventGroupIndex; index < preparedGroups_.size(); ++index) {
        const double groupSecond = preparedGroups_[index].second;
        if (groupSecond <= currentSecond + kBassPreviewEpsilonSeconds) {
            continue;
        }
        const double relativeSecond = qMax(
            0.0,
            (groupSecond - playbackSession_.sessionStartSecond)
                / qMax(kBassPreviewMinRate, playbackSession_.sessionPlaybackRate));
        const QWORD position = BASS_ChannelSeconds2Bytes(masterMixer_, relativeSecond);
        const quint32 syncHandle = BASS_ChannelSetSync(
            masterMixer_,
            BASS_SYNC_POS | BASS_SYNC_MIXTIME,
            position,
            reinterpret_cast<SYNCPROC*>(BassPreviewAudioBackend::onMixerGroupSync),
            this);
        if (syncHandle != 0) {
            scheduledGroupSync_ = syncHandle;
            scheduledGroupIndex_ = index;
            appendAudioDebugLog(
                QString("bass_schedule_arm txn=%1 idx=%2 second=%3")
                    .arg(playbackTransactionId_)
                    .arg(index)
                    .arg(groupSecond, 0, 'f', 6));
        }
        return;
    }
#else
    Q_UNUSED(currentSecond);
#endif
}

void BassPreviewAudioBackend::logPlaybackStatus(double authoritativeSecond, double fallbackSecond)
{
#ifdef Q_OS_WIN
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    if (playbackSession_.lastStatusLogSecond >= 0.0
        && authoritativeSecond - playbackSession_.lastStatusLogSecond < kBassPreviewStatusLogIntervalSeconds) {
        return;
    }
    playbackSession_.lastStatusLogSecond = authoritativeSecond;

    const double mixerSecond = (authoritativeSecond - playbackSession_.sessionStartSecond)
        / qMax(kBassPreviewMinRate, playbackSession_.sessionPlaybackRate);
    const double bgmRawSecond = backgroundTrackSample_ != nullptr ? backgroundTrackSample_->currentSec() : -1.0;
    const double bgmChartSecond = backgroundTrackSample_ != nullptr
        ? (bgmRawSecond - playbackSession_.backgroundTrackOffsetSeconds)
        : -1.0;
    const double driftMs = (authoritativeSecond - fallbackSecond) * 1000.0;
    const int nextGroupIndex = scheduledGroupIndex_ >= 0 ? scheduledGroupIndex_ : playbackSession_.eventGroupIndex;
    const double nextGroupSecond =
        (nextGroupIndex >= 0 && nextGroupIndex < preparedGroups_.size()) ? preparedGroups_[nextGroupIndex].second : -1.0;
    appendAudioDebugLog(
        QString("bass_status txn=%1 auth=%2 mixer=%3 bgm_raw=%4 bgm_chart=%5 fallback=%6 drift_ms=%7 next_group_idx=%8 next_group_second=%9 last_trigger_idx=%10 last_trigger_second=%11 triggered_count=%12")
            .arg(playbackTransactionId_)
            .arg(authoritativeSecond, 0, 'f', 6)
            .arg(mixerSecond, 0, 'f', 6)
            .arg(bgmRawSecond, 0, 'f', 6)
            .arg(bgmChartSecond, 0, 'f', 6)
            .arg(fallbackSecond, 0, 'f', 6)
            .arg(driftMs, 0, 'f', 3)
            .arg(nextGroupIndex)
            .arg(nextGroupSecond, 0, 'f', 6)
            .arg(playbackSession_.lastTriggeredGroupIndex)
            .arg(playbackSession_.lastTriggeredGroupSecond, 0, 'f', 6)
            .arg(playbackSession_.triggeredGroupCount));
#else
    Q_UNUSED(authoritativeSecond);
    Q_UNUSED(fallbackSecond);
#endif
}

QString BassPreviewAudioBackend::groupSignature(const CollapsedEventGroup& group) const
{
    QStringList parts;
    parts.reserve(group.orderedEvents.size() + group.aggregatedPlaybacks.size());
    for (const Event& event : group.orderedEvents) {
        parts.append(QStringLiteral("e:%1").arg(event.kind));
    }
    for (const miacode::preview_sfx_timeline::AggregatedPlayback& playback : group.aggregatedPlaybacks) {
        parts.append(QStringLiteral("a:%1#%2").arg(playback.kind).arg(playback.count));
    }
    return parts.join(QLatin1Char('|'));
}

void BassPreviewAudioBackend::logPreparedEventWindow(double startSecond) const
{
    if (!runtimeAudioDebugEnabled()) {
        return;
    }
    const int firstIndex = qBound(0, playbackSession_.eventGroupIndex, preparedGroups_.size());
    const int lastIndex = qMin(preparedGroups_.size(), firstIndex + 8);
    for (int index = firstIndex; index < lastIndex; ++index) {
        const CollapsedEventGroup& group = preparedGroups_[index];
        appendAudioDebugLog(
            QString("bass_prepare_event txn=%1 idx=%2 second=%3 lead_ms=%4 signature=%5")
                .arg(playbackTransactionId_)
                .arg(index)
                .arg(group.second, 0, 'f', 6)
                .arg((group.second - startSecond) * 1000.0, 0, 'f', 3)
                .arg(groupSignature(group)));
    }
}

#ifdef Q_OS_WIN
void BassPreviewAudioBackend::onMixerGroupSync(quint32 handle, quint32 channel, quint32 data, void* user)
{
    Q_UNUSED(channel);
    Q_UNUSED(data);
    BassPreviewAudioBackend* backend = static_cast<BassPreviewAudioBackend*>(user);
    if (backend != nullptr) {
        backend->handleMixerGroupSync(handle);
    }
}

void BassPreviewAudioBackend::handleMixerGroupSync(quint32 handle)
{
    CollapsedEventGroup group;
    int groupIndex = -1;
    bool shouldTrigger = false;
    {
        QMutexLocker locker(&schedulerMutex_);
        if (handle == 0 || handle != scheduledGroupSync_) {
            return;
        }
        groupIndex = scheduledGroupIndex_;
        scheduledGroupSync_ = 0;
        scheduledGroupIndex_ = -1;
        if (groupIndex >= 0 && groupIndex < preparedGroups_.size()) {
            group = preparedGroups_[groupIndex];
            if (playbackSession_.eventGroupIndex <= groupIndex) {
                playbackSession_.eventGroupIndex = groupIndex + 1;
            }
            playbackSession_.lastTriggeredGroupIndex = groupIndex;
            playbackSession_.lastTriggeredGroupSecond = group.second;
            playbackSession_.triggeredGroupCount += 1;
            shouldTrigger = true;
        }
    }
    if (!shouldTrigger) {
        return;
    }
    triggerGroup(group);
    armNextGroupSync(group.second);
}
#endif

double BassPreviewAudioBackend::authoritativeSecond() const
{
#ifdef Q_OS_WIN
    if (!engineInitialized_ || masterMixer_ == 0 || !playbackSession_.masterRunning) {
        return playbackSession_.lastAuthoritativeSecond;
    }
    const QWORD position = BASS_ChannelGetPosition(masterMixer_, BASS_POS_BYTE);
    if (position == static_cast<QWORD>(-1)) {
        return playbackSession_.lastAuthoritativeSecond;
    }
    const double mixerElapsedSecond = BASS_ChannelBytes2Seconds(masterMixer_, position);
    return playbackSession_.sessionStartSecond + (mixerElapsedSecond * playbackSession_.sessionPlaybackRate);
#else
    return playbackSession_.lastAuthoritativeSecond;
#endif
}

void BassPreviewAudioBackend::configureBackgroundTrackForSecond(double second)
{
#ifdef Q_OS_WIN
    if (backgroundTrackSample_ == nullptr) {
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = false;
        return;
    }

    backgroundTrackSample_->setLoop(false);
    backgroundTrackSample_->setSpeed(playbackSession_.backgroundTrackPlaybackRate);

    const double rawSecond = second + playbackSession_.backgroundTrackOffsetSeconds;
    if (rawSecond < 0.0) {
        backgroundTrackSample_->setCurrentSec(0.0);
        backgroundTrackSample_->pause();
        playbackSession_.backgroundTrackPendingStart = true;
        playbackSession_.backgroundTrackPendingStartSecond = second - rawSecond;
        playbackSession_.backgroundTrackRunning = false;
        return;
    }

    backgroundTrackSample_->setCurrentSec(rawSecond);
    backgroundTrackSample_->pause();
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackPendingStartSecond = second;
    playbackSession_.backgroundTrackRunning = false;
#else
    Q_UNUSED(second);
#endif
}

bool BassPreviewAudioBackend::maybeStartPendingBackgroundTrack(double second)
{
#ifdef Q_OS_WIN
    if (backgroundTrackSample_ == nullptr) {
        return false;
    }
    if (playbackSession_.backgroundTrackPendingStart
        && second + kBassPreviewEpsilonSeconds >= playbackSession_.backgroundTrackPendingStartSecond) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = true;
        return true;
    }
    return false;
#else
    Q_UNUSED(second);
    return false;
#endif
}

double BassPreviewAudioBackend::preparePreviewPlaybackTransaction(
    double startSecond,
    bool resumeFromPause,
    double playbackRate)
{
    Q_UNUSED(resumeFromPause);
    if (!initializeAudioEngine()) {
        return startSecond;
    }
    setBackgroundTrackPlaybackRate(playbackRate);
    stopPlaybackSession();
    resetMasterMixerClock(startSecond);
    configureBackgroundTrackForSecond(startSecond);
    resetCursor(startSecond, !resumeFromPause);
    pauseTouchholdVoices();
    preparedPlayback_.pending = true;
    preparedPlayback_.resumeFromPause = resumeFromPause;
    preparedPlayback_.startSecond = clampTimelineSecond(startSecond);
    logPreparedEventWindow(preparedPlayback_.startSecond);
    appendAudioDebugLog(
        QString("bass_prepare txn=%1 start=%2 resume=%3 rate=%4 groups=%5")
            .arg(playbackTransactionId_)
            .arg(preparedPlayback_.startSecond, 0, 'f', 6)
            .arg(resumeFromPause ? 1 : 0)
            .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
            .arg(preparedGroups_.size()));
    return preparedPlayback_.startSecond;
}

void BassPreviewAudioBackend::commitPreparedPreviewPlayback()
{
#ifdef Q_OS_WIN
    if (!preparedPlayback_.pending || masterMixer_ == 0) {
        return;
    }
    BASS_ChannelPlay(masterMixer_, FALSE);
    playbackSession_.masterRunning = true;
    playbackSession_.lastAuthoritativeSecond = preparedPlayback_.startSecond;
    if (backgroundTrackSample_ != nullptr && !playbackSession_.backgroundTrackPendingStart) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackRunning = true;
    }
    if (!preparedPlayback_.resumeFromPause) {
        drainEvents(preparedPlayback_.startSecond);
    }
    restoreTouchholdVoices(preparedPlayback_.startSecond);
    armNextGroupSync(preparedPlayback_.startSecond);
    appendAudioDebugLog(
        QString("bass_commit txn=%1 start=%2 resume=%3 bg_pending=%4")
            .arg(playbackTransactionId_)
            .arg(preparedPlayback_.startSecond, 0, 'f', 6)
            .arg(preparedPlayback_.resumeFromPause ? 1 : 0)
            .arg(playbackSession_.backgroundTrackPendingStart ? 1 : 0));
#endif
    preparedPlayback_ = PreparedPlaybackState();
}

void BassPreviewAudioBackend::cancelPreparedPreviewPlayback()
{
    if (!preparedPlayback_.pending) {
        return;
    }
    stopPlaybackSession();
    preparedPlayback_ = PreparedPlaybackState();
}

double BassPreviewAudioBackend::preparedStartSecond() const
{
    return preparedPlayback_.pending ? preparedPlayback_.startSecond : playbackSession_.lastAuthoritativeSecond;
}

void BassPreviewAudioBackend::applyPausedPreviewState(
    const QVector<TimelineNoteMarker>& noteMarkers,
    bool noteMarkersChanged,
    double pauseSecond,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    preparedPlayback_ = PreparedPlaybackState();
    if (noteMarkersChanged
        || preparedTimeline_.sourceNoteMarkers.size() != noteMarkers.size()
        || qAbs(preparedTimelinePlaybackRate_ - playbackRate) > kBassPreviewEpsilonSeconds
        || !previewTimingSettingsEqual(timingSettings_, timingSettings)) {
        rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
    }
    stopPlaybackSession();
    playbackSession_.lastAuthoritativeSecond = clampTimelineSecond(pauseSecond);
    resetCursor(pauseSecond, false);
    pauseTouchholdVoices();
}

double BassPreviewAudioBackend::startPreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate)
{
    const double preparedSecond = preparePreviewPlaybackTransaction(startSecond, resumeFromPause, playbackRate);
    commitPreparedPreviewPlayback();
    return preparedSecond;
}

miacode::preview_audio::PausePreviewResult BassPreviewAudioBackend::capturePausedPreviewTransaction()
{
    PausePreviewResult result;
    result.usedBackgroundTrack = hasBackgroundTrack();
    result.pauseSecond = authoritativeSecond();
    stopPlaybackSession();
    preparedPlayback_ = PreparedPlaybackState();
    playbackSession_.lastAuthoritativeSecond = result.pauseSecond;
    return result;
}

double BassPreviewAudioBackend::syncPreviewPlaybackClockTransaction(double fallbackSecond)
{
    const double second = authoritativeSecond();
    playbackSession_.lastAuthoritativeSecond = second;
    maybeStartPendingBackgroundTrack(second);
    drainEvents(second);
    armNextGroupSync(second);
    logPlaybackStatus(second, fallbackSecond);
    return second;
}

void BassPreviewAudioBackend::resetCursor(double second, bool includeCurrentSecond)
{
    playbackSession_.eventGroupIndex = 0;
    while (playbackSession_.eventGroupIndex < preparedGroups_.size()) {
        const double groupSecond = preparedGroups_[playbackSession_.eventGroupIndex].second;
        const bool beforeStart = includeCurrentSecond
            ? (groupSecond + kBassPreviewEpsilonSeconds < second)
            : (groupSecond <= second + kBassPreviewEpsilonSeconds);
        if (!beforeStart) {
            break;
        }
        ++playbackSession_.eventGroupIndex;
    }
}

void BassPreviewAudioBackend::triggerGroup(const CollapsedEventGroup& group)
{
    for (const Event& event : group.orderedEvents) {
        if (event.kind == QLatin1String("touchhold_start")) {
            if (touchholdSample_ != nullptr) {
                touchholdSample_->setCurrentSec(0.0);
                touchholdSample_->play();
            }
            continue;
        }
        if (event.kind == QLatin1String("touchhold_stop")) {
            if (touchholdSample_ != nullptr) {
                touchholdSample_->stop();
            }
            continue;
        }
        playKindInternal(event.kind, event.gain);
    }

    for (const miacode::preview_sfx_timeline::AggregatedPlayback& playback : group.aggregatedPlaybacks) {
        playKindInternal(playback.kind, miacode::preview_sfx_timeline::aggregatedPlaybackGain(playback));
    }
}

void BassPreviewAudioBackend::drainEvents(double second)
{
    while (playbackSession_.eventGroupIndex < preparedGroups_.size()) {
        const CollapsedEventGroup& group = preparedGroups_[playbackSession_.eventGroupIndex];
        if (group.second > second + kBassPreviewEpsilonSeconds) {
            break;
        }
#ifdef Q_OS_WIN
        {
            QMutexLocker locker(&schedulerMutex_);
            if (scheduledGroupSync_ != 0 && scheduledGroupIndex_ == playbackSession_.eventGroupIndex && masterMixer_ != 0) {
                BASS_ChannelRemoveSync(masterMixer_, scheduledGroupSync_);
                scheduledGroupSync_ = 0;
                scheduledGroupIndex_ = -1;
            }
        }
#endif
        triggerGroup(group);
        playbackSession_.lastTriggeredGroupIndex = playbackSession_.eventGroupIndex;
        playbackSession_.lastTriggeredGroupSecond = group.second;
        playbackSession_.triggeredGroupCount += 1;
        ++playbackSession_.eventGroupIndex;
    }
}

void BassPreviewAudioBackend::pauseTouchholdVoices()
{
#ifdef Q_OS_WIN
    if (touchholdSample_ != nullptr) {
        touchholdSample_->stop();
    }
#endif
}

void BassPreviewAudioBackend::restoreTouchholdVoices(double second)
{
#ifdef Q_OS_WIN
    pauseTouchholdVoices();
    if (touchholdSample_ == nullptr) {
        return;
    }
    for (const TouchholdSpan& span : preparedTimeline_.touchholdSpans) {
        if (second + kBassPreviewEpsilonSeconds < span.startSecond) {
            continue;
        }
        if (second >= span.endSecond - kBassPreviewEpsilonSeconds) {
            continue;
        }
        touchholdSample_->setCurrentSec(second - span.startSecond);
        touchholdSample_->play();
        return;
    }
#else
    Q_UNUSED(second);
#endif
}

void BassPreviewAudioBackend::syncBackgroundTrack(double timelineSecond)
{
    maybeStartPendingBackgroundTrack(timelineSecond);
}

bool BassPreviewAudioBackend::hasBackgroundTrack() const
{
    return backgroundTrackSample_ != nullptr;
}

bool BassPreviewAudioBackend::isBackgroundTrackRunning() const
{
    return playbackSession_.backgroundTrackRunning;
}

void BassPreviewAudioBackend::startBackgroundTrack(double second)
{
#ifdef Q_OS_WIN
    if (masterMixer_ != 0 && !playbackSession_.masterRunning) {
        resetMasterMixerClock(second);
        BASS_ChannelPlay(masterMixer_, FALSE);
        playbackSession_.masterRunning = true;
        playbackSession_.lastAuthoritativeSecond = clampTimelineSecond(second);
    }
    configureBackgroundTrackForSecond(second);
    if (backgroundTrackSample_ != nullptr && !playbackSession_.backgroundTrackPendingStart) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackRunning = true;
    }
#else
    Q_UNUSED(second);
#endif
}

void BassPreviewAudioBackend::seekBackgroundTrack(double second)
{
    configureBackgroundTrackForSecond(second);
}

void BassPreviewAudioBackend::pauseBackgroundTrack()
{
#ifdef Q_OS_WIN
    if (backgroundTrackSample_ != nullptr) {
        backgroundTrackSample_->pause();
    }
    playbackSession_.backgroundTrackRunning = false;
#endif
}

double BassPreviewAudioBackend::backgroundPlaybackSecond() const
{
    return authoritativeSecond();
}

bool BassPreviewAudioBackend::playKindInternal(const QString& kind, double gain)
{
#ifdef Q_OS_WIN
    Sample* sample = sampleForKind(kind);
    if (sample == nullptr) {
        return false;
    }
    sample->playOneShot(gain);
    return true;
#else
    Q_UNUSED(kind);
    Q_UNUSED(gain);
    return false;
#endif
}

bool BassPreviewAudioBackend::audition(const QString& kind, double gain)
{
#ifdef Q_OS_WIN
    if (!initializeAudioEngine() || masterMixer_ == 0) {
        return false;
    }
    if (!playbackSession_.masterRunning) {
        resetMasterMixerClock(0.0);
        BASS_ChannelPlay(masterMixer_, FALSE);
        playbackSession_.masterRunning = true;
    }
    return playKindInternal(kind, gain);
#else
    Q_UNUSED(kind);
    Q_UNUSED(gain);
    return false;
#endif
}

void BassPreviewAudioBackend::stopAll()
{
    stopPlaybackSession();
    preparedPlayback_ = PreparedPlaybackState();
}
