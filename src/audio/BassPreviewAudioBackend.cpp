#include "BassPreviewAudioBackend.h"

#include "BassPreviewDebugLogRouting.h"
#include "BassPreviewRetainedState.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/FileContentStamp.h"
#include "common/OperationLog.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewSfxTimeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QtMath>

#include <cstdio>   // G1 Commit 8 followup: std::snprintf for startup-beacon lines

#ifdef Q_OS_WIN
#include <windows.h>

#include "bass.h"
#include "bassmix.h"
#endif

namespace {

constexpr double kBassPreviewEpsilonSeconds = miacode::preview_sfx_timeline::kTimelineEpsilonSeconds;
constexpr double kBassPreviewMinRate = 0.25;
constexpr double kBassPreviewMaxRate = 2.0;
constexpr double kBassPreviewStatusLogIntervalSeconds = 1.0;
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

// G1 Commit 1: unified BASS error-code reporting. Call immediately after
// any BASS_* / BASS_Mixer_* / BASS_FX_* invocation. If BASS_ErrorGetCode()
// is non-zero, emits a single `bass_err ctx=<ctx> code=<n>` line to the
// audio channel. Reads-and-clears the per-thread error, matching BASS's
// own contract: only the most recent failure is preserved, so callers must
// query before the next BASS call or risk losing the code.
void noteBassErr(const char* ctx)
{
#ifdef Q_OS_WIN
    const int code = static_cast<int>(BASS_ErrorGetCode());
    if (code == 0) {
        return;
    }
    appendAudioDebugLog(QString("bass_err ctx=%1 code=%2").arg(QLatin1String(ctx)).arg(code));
#else
    Q_UNUSED(ctx);
#endif
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

QString retainedPlaybackModeLabel(miacode::preview_audio::RetainedPlaybackMode mode)
{
    using RetainedPlaybackMode = miacode::preview_audio::RetainedPlaybackMode;
    switch (mode) {
    case RetainedPlaybackMode::PausedExact:
        return QStringLiteral("paused_exact");
    case RetainedPlaybackMode::PausedAnchored:
        return QStringLiteral("paused_anchored");
    case RetainedPlaybackMode::Invalidated:
        return QStringLiteral("invalidated");
    case RetainedPlaybackMode::None:
    default:
        return QStringLiteral("none");
    }
}

QString retainedSeekActionLabel(miacode::preview_audio::bass::RetainedSeekAction action)
{
    using RetainedSeekAction = miacode::preview_audio::bass::RetainedSeekAction;
    switch (action) {
    case RetainedSeekAction::KeepPaused:
        return QStringLiteral("keep_paused");
    case RetainedSeekAction::ResumeExact:
        return QStringLiteral("resume_exact");
    case RetainedSeekAction::ResumeAnchored:
        return QStringLiteral("resume_anchored");
    case RetainedSeekAction::RepositionPaused:
        return QStringLiteral("reposition_paused");
    case RetainedSeekAction::RepositionAndResume:
        return QStringLiteral("reposition_and_resume");
    case RetainedSeekAction::AnchorPaused:
        return QStringLiteral("anchor_paused");
    case RetainedSeekAction::AnchorAndResume:
    default:
        return QStringLiteral("anchor_and_resume");
    }
}

QString bassDebugOperationLabel(miacode::preview_audio::bass::BassDebugOperation operation)
{
    using BassDebugOperation = miacode::preview_audio::bass::BassDebugOperation;
    switch (operation) {
    case BassDebugOperation::InitializeAudioEngine:
        return QStringLiteral("initialize_audio_engine");
    case BassDebugOperation::ResetAssets:
        return QStringLiteral("reset_assets");
    case BassDebugOperation::InitializeAssets:
        return QStringLiteral("initialize_assets");
    case BassDebugOperation::InvalidateRetainedState:
        return QStringLiteral("invalidate_retained_state");
    case BassDebugOperation::RebuildTimeline:
        return QStringLiteral("rebuild_timeline");
    case BassDebugOperation::AnchorTransport:
        return QStringLiteral("anchor_transport");
    case BassDebugOperation::PreparePreviewPlayback:
        return QStringLiteral("prepare_preview_playback");
    case BassDebugOperation::ConfigureBackgroundTrack:
        return QStringLiteral("configure_background_track");
    case BassDebugOperation::PauseExact:
        return QStringLiteral("pause_exact");
    case BassDebugOperation::ResumeTransport:
        return QStringLiteral("resume_transport");
    case BassDebugOperation::RetainedSeek:
        return QStringLiteral("retained_seek");
    case BassDebugOperation::RetainedReset:
        return QStringLiteral("retained_reset");
    case BassDebugOperation::StartBackgroundTrack:
        return QStringLiteral("start_background_track");
    case BassDebugOperation::SeekBackgroundTrack:
        return QStringLiteral("seek_background_track");
    case BassDebugOperation::TransportReady:
    default:
        return QStringLiteral("transport_ready");
    }
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
    // G1 Commit 8: per-sample pause-cycle counter for the §7.2 `bass_sample_pause`
    // line. Bumped each time the sample's MIXER_CHAN_PAUSE flag is re-set;
    // surfacing the cycle count is how multi-cycle audio tearing (the bug from
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.2) becomes visible in the
    // log instead of needing the user to remember how many pause-resume rounds
    // they did before the artifact showed up.
    int pauseCycleCount = 0;

    bool valid() const
    {
        return source != 0 && resampler != 0;
    }

    void free()
    {
        // G1 Commit 8 followup: per-sample destroy beacon per §7.3. Writing to
        // the startup beacon (not the audio debug channel) because the most
        // valuable use of this line is post-crash forensics — if the next
        // process startup reads a stale beacon file we want the *last sample
        // freed before the crash* visible there with its BASS handle values.
        if (source != 0 || resampler != 0) {
            char buf[160];
            const QByteArray kindUtf8 = kind.toUtf8();
            std::snprintf(buf, sizeof(buf),
                "audio/sample/destroy kind=%s source=0x%08x resampler=0x%08x",
                kindUtf8.constData(),
                static_cast<unsigned>(source),
                static_cast<unsigned>(resampler));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        if (source != 0) {
            BASS_Mixer_ChannelFlags(source, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
            noteBassErr("sample_free/pause_flag");
        }
        if (resampler != 0) {
            BASS_Mixer_ChannelRemove(resampler);
            noteBassErr("sample_free/mixer_remove");
            BASS_StreamFree(resampler);
            noteBassErr("sample_free/stream_free_resampler");
            resampler = 0;
        }
        if (source != 0) {
            BASS_StreamFree(source);
            noteBassErr("sample_free/stream_free_source");
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
        const float effective = clampSampleVolume(baseVolume * gain * qMax(0.0, eventGain));
        BASS_ChannelSetAttribute(source, BASS_ATTRIB_VOL, effective);
        noteBassErr("sample_apply_volume");
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
        free();

        path = samplePath;
        name = sampleName;
        kind = sampleKind;
        speedChangeSupported = speedChange;

        // G1 Commit 8 followup: per-sample create beacon per §7.3. The beacon is
        // heap-free / pure Win32, so if BASS_StreamCreateFile or BASS_FX_TempoCreate
        // crashes mid-call (the (a)-scenario fault locus per
        // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.1), this line still lands
        // and lets the next startup pin which sample was in flight. Filename only,
        // not full path, to fit the beacon line budget.
        {
            char buf[160];
            const QByteArray kindUtf8 = sampleKind.toUtf8();
            const QByteArray nameUtf8 = QFileInfo(samplePath).fileName().toUtf8();
            std::snprintf(buf, sizeof(buf),
                "audio/sample/create kind=%s file=%s speed_change=%d normalize=%d",
                kindUtf8.constData(),
                nameUtf8.constData(),
                speedChange ? 1 : 0,
                normalize ? 1 : 0);
            miacode::oplog::appendStartupBeaconLine(buf);
        }

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

        // G1 Commit 8 followup: keep the decode and tempo handles in separate
        // locals so the create_ok beacon line below can record both even when
        // `stream` is overwritten by the tempo wrapper. decode_handle stays
        // valid for as long as the tempo stream owns it via BASS_FX_FREESOURCE.
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
        const DWORD decodeHandle = stream;
        DWORD tempoHandle = 0;

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
            tempoHandle = tempoStream;
        }

        if (normalize) {
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
            gain = qMin(kBgmNormalizeMaxBoost, 1.0 / qMax(peak, kBgmNormalizePeakFloor));
            appendAudioDebugLog(
                QString("bgm_normalize_peak kind=%1 peak=%2 gain=%3 path=%4")
                    .arg(sampleKind)
                    .arg(peak, 0, 'f', 6)
                    .arg(gain, 0, 'f', 4)
                    .arg(samplePath));
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

        // BASS_ATTRIB_BUFFER is not settable on a decode-only Mixer stream — BASS
        // returns BASS_ERROR_NOTAVAIL (code 37). The call has always been a silent
        // no-op here; G1 Commit 1's blanket noteBassErr surfaced it as 14×N noise
        // per test session. Swallow the post-error code explicitly so the
        // diagnostic channel stays focused on real failures.
        BASS_ChannelSetAttribute(resamplerStream, BASS_ATTRIB_BUFFER, 0.0f);
        (void) BASS_ErrorGetCode();  // discard expected NOTAVAIL on decode stream
        BASS_Mixer_StreamAddChannel(resamplerStream, stream, 0);
        noteBassErr("sample_create/add_source_to_resampler");
        BASS_Mixer_ChannelFlags(stream, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        noteBassErr("sample_create/initial_pause_flag");
        BASS_Mixer_StreamAddChannel(backend->masterMixer_, resamplerStream, 0);
        noteBassErr("sample_create/add_resampler_to_master");
        BASS_ChannelSetPosition(stream, 0, BASS_POS_BYTE);
        noteBassErr("sample_create/initial_seek_zero");

        source = stream;
        resampler = resamplerStream;

        const QWORD lengthBytes = BASS_ChannelGetLength(source, BASS_POS_BYTE);
        if (lengthBytes != static_cast<QWORD>(-1)) {
            lengthSeconds = BASS_ChannelBytes2Seconds(source, lengthBytes);
        }
        applyVolume();
        // G1 Commit 8 followup: per-sample create_ok beacon per §7.3. Records the
        // three handles ((decode → tempo → resampler) needed to map a VEH fault
        // address back to the right BASS_FX SoundTouch SEQUENCE_MS / SEEKWINDOW_MS
        // ring slot when a crash drops into the plug-in.
        {
            char buf[200];
            const QByteArray kindUtf8 = kind.toUtf8();
            std::snprintf(buf, sizeof(buf),
                "audio/sample/create_ok kind=%s decode=0x%08x tempo=0x%08x resampler=0x%08x length_sec=%.3f",
                kindUtf8.constData(),
                static_cast<unsigned>(decodeHandle),
                static_cast<unsigned>(tempoHandle),
                static_cast<unsigned>(resamplerStream),
                lengthSeconds);
            miacode::oplog::appendStartupBeaconLine(buf);
        }
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
        noteBassErr("sample_set_current_sec");
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
        noteBassErr("sample_set_loop");
    }

    void setSpeed(double rate)
    {
        if (!valid() || !speedChangeSupported) {
            return;
        }
        // G1 Commit 6: BASS_ATTRIB_TEMPO must only be written while the sample is paused
        // (BASS_MIXER_CHAN_PAUSE flag set on the source). Writing it while the tempo stream
        // is being actively pulled by the master mixer triggers a SoundTouch race that
        // crashes BASS_FX at 0.5x. Per PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.1
        // scenario (b), this guard is the actual fix for the playback-time rate change
        // crash; the new G1 invariant ("speed set once" — at create / reload / paused
        // user rate change) means a caller hitting this skip path is a deferred rate
        // change waiting for the next pause cycle, not lost behavior.
        //
        // G2 Diag: 0.5x crash recurrence — async DebugLog drops the last few queued
        // lines when the process fast-fails, so the existing appendAudioDebugLog rows
        // never make it to disk. Mirror every step around the tempo write into the
        // synchronous beacon (heap-free, fsync per line) so the breadcrumb survives.
        const QByteArray kindUtf8 = kind.toUtf8();
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "audio/rate/sample_read_flags kind=%s source=0x%08x rate=%.3f",
                kindUtf8.constData(),
                static_cast<unsigned>(source),
                rate);
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        const DWORD flags = BASS_Mixer_ChannelFlags(source, 0, 0);
        noteBassErr("sample_set_speed/read_flags");
        if (flags != static_cast<DWORD>(-1) && (flags & BASS_MIXER_CHAN_PAUSE) == 0) {
            {
                char buf[200];
                std::snprintf(buf, sizeof(buf),
                    "audio/rate/sample_skip_playing kind=%s source=0x%08x rate=%.3f flags=0x%08lx",
                    kindUtf8.constData(),
                    static_cast<unsigned>(source),
                    rate,
                    static_cast<unsigned long>(flags));
                miacode::oplog::appendStartupBeaconLine(buf);
            }
            appendAudioDebugLog(
                QString("sample_set_speed_skipped_playing kind=%1 rate=%2")
                    .arg(kind)
                    .arg(rate, 0, 'f', 3));
            return;
        }
        const float tempo = static_cast<float>((qBound(kBassPreviewMinRate, rate, kBassPreviewMaxRate) - 1.0) * 100.0);
        {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "audio/rate/sample_about_to_setattr_tempo kind=%s source=0x%08x rate=%.3f tempo=%.3f flags=0x%08lx",
                kindUtf8.constData(),
                static_cast<unsigned>(source),
                rate,
                static_cast<double>(tempo),
                static_cast<unsigned long>(flags));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        BASS_ChannelSetAttribute(source, kBassPreviewTempoAttribute, tempo);
        noteBassErr("sample_set_speed");
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                "audio/rate/sample_setattr_tempo_done kind=%s source=0x%08x rate=%.3f",
                kindUtf8.constData(),
                static_cast<unsigned>(source),
                rate);
            miacode::oplog::appendStartupBeaconLine(buf);
        }
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
        noteBassErr("sample_play/clear_pause_flag");
    }

    void playOneShot(double eventGain)
    {
        if (!valid()) {
            return;
        }
        applyVolume(eventGain);
        BASS_Mixer_ChannelSetPosition(source, 0, BASS_POS_BYTE);
        noteBassErr("sample_one_shot/seek_zero");
        BASS_Mixer_ChannelFlags(source, 0, BASS_MIXER_CHAN_PAUSE);
        noteBassErr("sample_one_shot/clear_pause_flag");
    }

    void pause()
    {
        if (!valid()) {
            return;
        }
        BASS_Mixer_ChannelFlags(source, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        noteBassErr("sample_pause/set_pause_flag");
        // G1 Commit 8: bass_sample_pause diagnostic per §7.2. Only BGM is logged —
        // SFX pads pause every tick they're not retriggered and would flood the
        // channel. pause_bgm_raw is the current cursor in source-bytes-seconds,
        // which lets us spot tempo-stream drift cycle by cycle without needing
        // to cross-reference the bass_status row that follows.
        if (kind == QLatin1String("bgm")) {
            ++pauseCycleCount;
            appendAudioDebugLog(
                QString("bass_sample_pause kind=%1 cycle=%2 pause_bgm_raw=%3")
                    .arg(kind)
                    .arg(pauseCycleCount)
                    .arg(currentSec(), 0, 'f', 6));
        }
    }

    void stop()
    {
        if (!valid()) {
            return;
        }
        BASS_Mixer_ChannelFlags(source, BASS_MIXER_CHAN_PAUSE, BASS_MIXER_CHAN_PAUSE);
        noteBassErr("sample_stop/set_pause_flag");
        BASS_Mixer_ChannelSetPosition(source, 0, BASS_POS_BYTE);
        noteBassErr("sample_stop/seek_zero");
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
    shuttingDown_.store(true, std::memory_order_release);
#ifdef Q_OS_WIN
    stopPlaybackSession();
    resetAssets();
    unloadOptionalPlugins();
    if (masterMixer_ != 0) {
        BASS_StreamFree(masterMixer_);
        noteBassErr("dtor/master_stream_free");
        masterMixer_ = 0;
    }
    unloadBassFx();
    if (registeredBassDeviceRef_ && gBassDeviceRefCount > 0) {
        --gBassDeviceRefCount;
        registeredBassDeviceRef_ = false;
    }
    if (engineInitialized_ && gBassDeviceRefCount == 0) {
        BASS_Stop();
        noteBassErr("dtor/bass_stop");
        BASS_Free();
        noteBassErr("dtor/bass_free");
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
    // Resolved-path warmup cache removed (2026-06-03). It keyed on the chart-path
    // string only and never re-validated the track file, so a same-named track
    // with new content — or a track that appeared/changed after warmup — was
    // shadowed by a stale (or empty) cached path, silently dropping the BGM.
    // The live resolver is cheap and exists-checked; the warmup worker still
    // byte-prefetches the file into the OS cache, which was warmup's real value.
    return miacode::chart_assets::resolveTrackPath(chartPath);
}

QString BassPreviewAudioBackend::resolveSfxDir() const
{
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
    MC_OP("BassPreviewAudioBackend::setWarmupResolvedPaths");
    // No-op: the resolved-path cache was removed (see resolveTrackPath). Path
    // resolution is now always live so a same-named track with new content is
    // picked up. Kept as a no-op to preserve the backend interface; the warmup
    // worker still byte-prefetches the files into the OS cache.
    Q_UNUSED(chartPath);
    Q_UNUSED(trackPath);
    Q_UNUSED(sfxDir);
}

bool BassPreviewAudioBackend::ensureBassFxLoaded()
{
    MC_OP("BassPreviewAudioBackend::ensureBassFxLoaded");
#ifdef Q_OS_WIN
    if (bassFxTempoCreate_ != nullptr) {
        return true;
    }
    const QString libraryPath = runtimeFilePath(QStringLiteral("bass_fx.dll"));
    _mc_op_.note(QStringLiteral("path=%1").arg(libraryPath));
    const HMODULE module = LoadLibraryW(reinterpret_cast<LPCWSTR>(libraryPath.utf16()));
    if (module == nullptr) {
        _mc_op_.fail(QStringLiteral("LoadLibraryW failed err=%1").arg(::GetLastError()));
        appendAudioDebugLog(QString("bass_fx_load_failed path=%1").arg(libraryPath));
        return false;
    }
    FARPROC proc = GetProcAddress(module, "BASS_FX_TempoCreate");
    if (proc == nullptr) {
        FreeLibrary(module);
        _mc_op_.fail(QStringLiteral("GetProcAddress BASS_FX_TempoCreate missing"));
        appendAudioDebugLog(QString("bass_fx_symbol_missing path=%1").arg(libraryPath));
        return false;
    }
    bassFxModule_ = module;
    bassFxTempoCreate_ = reinterpret_cast<void*>(proc);
    return true;
#else
    _mc_op_.fail(QStringLiteral("non-Windows platform"));
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
        noteBassErr("plugin_free_aac");
        pluginAac_ = 0;
    }
    if (pluginOpus_ != 0) {
        BASS_PluginFree(pluginOpus_);
        noteBassErr("plugin_free_opus");
        pluginOpus_ = 0;
    }
#endif
}

bool BassPreviewAudioBackend::initializeAudioEngine()
{
    MC_OP("BassPreviewAudioBackend::initializeAudioEngine");
#ifndef Q_OS_WIN
    _mc_op_.fail(QStringLiteral("non-Windows platform"));
    return false;
#else
    if (engineInitialized_ && masterMixer_ != 0) {
        return true;
    }
    QElapsedTimer timer;
    timer.start();
    _mc_op_.note(QStringLiteral("device_sr=%1").arg(deviceSampleRate_));
    if (!ensureBassFxLoaded()) {
        _mc_op_.fail(QStringLiteral("bass_fx load failed"));
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
            QString("reused=0 elapsed_ms=%1 ok=0 reason=bass_fx").arg(timer.elapsed()),
            true);
        return false;
    }
    if (!registeredBassDeviceRef_) {
        if (gBassDeviceRefCount == 0) {
            if (!BASS_Init(-1, static_cast<int>(deviceSampleRate_), 0, nullptr, nullptr)) {
                _mc_op_.fail(QStringLiteral("BASS_Init err=%1").arg(static_cast<int>(BASS_ErrorGetCode())));
                appendAudioDebugLog(QString("bass_init_failed err=%1").arg(static_cast<int>(BASS_ErrorGetCode())));
                appendBassDebugLog(
                    miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
                    QString("reused=0 elapsed_ms=%1 ok=0 reason=bass_init").arg(timer.elapsed()),
                    true);
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
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
            QString("reused=0 elapsed_ms=%1 ok=0 reason=master_mixer").arg(timer.elapsed()),
            true);
        return false;
    }
    BASS_ChannelSetAttribute(masterMixer_, BASS_ATTRIB_BUFFER, 0.0f);
    noteBassErr("engine_init/master_buffer_attr");
    BASS_ChannelSetAttribute(masterMixer_, BASS_ATTRIB_MIXER_THREADS, 8.0f);
    noteBassErr("engine_init/master_mixer_threads_attr");
    // G1 Commit 6: master mixer stays ACTIVE_PLAYING for the lifetime of the engine.
    // Pre-G1 we cycled it via BASS_ChannelPause / BASS_ChannelPlay / BASS_ChannelStop
    // at every session-state change; each cold-start churned BASS_FX SoundTouch
    // buffers and was the root cause of the multi-cycle audio-tearing bug per
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.2. Sample audibility is now
    // gated entirely by the BASS_MIXER_CHAN_PAUSE flag on each per-sample source.
    BASS_ChannelPlay(masterMixer_, FALSE);
    noteBassErr("engine_init/master_play_once");
    engineInitialized_ = true;
    appendAudioDebugLog(QString("bass_engine_ready sample_rate=%1").arg(deviceSampleRate_));
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
        QString("reused=0 elapsed_ms=%1 ok=1 sample_rate=%2")
            .arg(timer.elapsed())
            .arg(deviceSampleRate_),
        true);
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
    int releasedSampleCount = 0;
    samplesByKind_.clear();
    backgroundTrackSample_ = nullptr;
    touchholdSample_ = nullptr;
    retainedBgmState_ = RetainedBgmState::NoneLoaded;
    trackMissingAfterLoadLogged_ = false;

    auto resetSample = [&releasedSampleCount](std::unique_ptr<Sample>& sample) {
        if (sample) {
            sample->free();
            sample.reset();
            ++releasedSampleCount;
        }
    };

    resetSample(answerSample_);
    resetSample(judgeSample_);
    resetSample(judgeBreakSample_);
    resetSample(slideSample_);
    resetSample(breakSample_);
    resetSample(breakSlideStartSample_);
    resetSample(breakSlideFinishSample_);
    resetSample(breakSlideTailBreakSample_);
    resetSample(judgeBreakSlideSample_);
    resetSample(exSample_);
    resetSample(touchSample_);
    resetSample(touchholdSampleOwner_);
    resetSample(fireworkSample_);
    resetSample(clockSample_);
    resetSample(trackStartSample_);
    resetSample(backgroundTrackSampleOwner_);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::ResetAssets,
        QString("released=%1").arg(releasedSampleCount),
        true);
#endif
}

void BassPreviewAudioBackend::initializeAssets()
{
    MC_OP("BassPreviewAudioBackend::initializeAssets");
#ifdef Q_OS_WIN
    QElapsedTimer timer;
    timer.start();
    resetAssets();
    if (!engineInitialized_ || masterMixer_ == 0) {
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAssets,
            QString("elapsed_ms=%1 loaded=0 skipped=1").arg(timer.elapsed()),
            true);
        return;
    }

    int loadedSampleCount = 0;
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
    loadSample(breakSlideTailBreakSample_, QStringLiteral("break_slide_tail_break"), false, true);
    loadSample(judgeBreakSlideSample_, QStringLiteral("judge_break_slide"), false, true);
    loadSample(exSample_, QStringLiteral("ex"), false, true);
    loadSample(touchSample_, QStringLiteral("touch"), false, true);
    loadSample(touchholdSampleOwner_, QStringLiteral("touchhold"), false, false);
    loadSample(fireworkSample_, QStringLiteral("firework"), false, true);
    loadSample(clockSample_, QStringLiteral("clock"), false, true);
    loadSample(trackStartSample_, QStringLiteral("track_start"), false, true);

    samplesByKind_.insert(QStringLiteral("break_touch"), judgeBreakSample_.get());
    samplesByKind_.insert(QStringLiteral("break_slide"), breakSlideStartSample_.get());

    if (!preparedAssets_.trackPath.isEmpty() && QFileInfo::exists(preparedAssets_.trackPath)) {
        backgroundTrackSampleOwner_ = std::make_unique<Sample>();
        if (backgroundTrackSampleOwner_->create(
                this,
                preparedAssets_.trackPath,
                QStringLiteral("bgm"),
                QStringLiteral("bgm"),
                true,
                true)) {
            backgroundTrackSample_ = backgroundTrackSampleOwner_.get();
            backgroundTrackSample_->setLoop(false);
            backgroundTrackSample_->setSpeed(playbackSession_.backgroundTrackPlaybackRate);
            ++loadedSampleCount;
        } else {
            backgroundTrackSampleOwner_.reset();
            backgroundTrackSample_ = nullptr;
        }
    }

    touchholdSample_ = touchholdSampleOwner_.get();
    const Sample* countedSamples[] = {
        answerSample_.get(),
        judgeSample_.get(),
        judgeBreakSample_.get(),
        slideSample_.get(),
        breakSample_.get(),
        breakSlideStartSample_.get(),
        breakSlideFinishSample_.get(),
        breakSlideTailBreakSample_.get(),
        judgeBreakSlideSample_.get(),
        exSample_.get(),
        touchSample_.get(),
        touchholdSampleOwner_.get(),
        fireworkSample_.get()
    };
    for (const Sample* sample : countedSamples) {
        if (sample != nullptr) {
            ++loadedSampleCount;
        }
    }
    applySampleLevels();
    updateRetainedBgmState();
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::InitializeAssets,
        QString("elapsed_ms=%1 loaded=%2 has_bgm=%3")
            .arg(timer.elapsed())
            .arg(loadedSampleCount)
            .arg(backgroundTrackSample_ != nullptr ? 1 : 0),
        true);
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
    apply(breakSlideTailBreakSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("break_slide_tail_break")));
    apply(judgeBreakSlideSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("judge_break_slide")));
    apply(exSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("ex")));
    apply(touchSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("touch")));
    apply(touchholdSample_, previewSfxVolumeForKind(settings_, QStringLiteral("touchhold")));
    apply(fireworkSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("firework")));
    apply(clockSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("clock")));
    apply(trackStartSample_.get(), previewSfxVolumeForKind(settings_, QStringLiteral("track_start")));
    apply(backgroundTrackSample_, previewTrackVolume(settings_));
#endif
}

BassPreviewAudioBackend::Sample* BassPreviewAudioBackend::sampleForKind(const QString& kind) const
{
    return samplesByKind_.value(previewSfxNormalizedKind(kind), nullptr);
}

double BassPreviewAudioBackend::retainedTransportSecond() const
{
    return clampTimelineSecond(playbackSession_.lastAuthoritativeSecond);
}

bool BassPreviewAudioBackend::retainedSecondMatches(double targetSecond) const
{
    return miacode::preview_audio::bass::retainedSecondsMatch(
        retainedTransportSecond(),
        clampTimelineSecond(targetSecond));
}

void BassPreviewAudioBackend::noteInitWindowOpened(const QString& reason)
{
    Q_UNUSED(reason);
    initWindowActive_ = true;
}

void BassPreviewAudioBackend::noteTransportReady(const QString& reason)
{
    if (!initWindowActive_) {
        return;
    }
    initWindowActive_ = false;
    ++transportReadyGeneration_;
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::TransportReady,
        QString("reason=%1 generation=%2")
            .arg(reason)
            .arg(transportReadyGeneration_));
}

void BassPreviewAudioBackend::appendBassDebugLog(
    miacode::preview_audio::bass::BassDebugOperation operation,
    const QString& payload,
    bool initWindowContext) const
{
    const auto route = miacode::preview_audio::bass::bassDebugRouteForOperation(operation, initWindowContext);
    QString text = QStringLiteral("%1 op=%2")
        .arg(miacode::preview_audio::bass::bassDebugRouteName(route))
        .arg(bassDebugOperationLabel(operation));
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    appendAudioDebugLog(text);
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

void BassPreviewAudioBackend::invalidateRetainedPlaybackState(const QString& reason)
{
    const RetainedPlaybackMode previousMode = retainedPlaybackMode_;
    noteInitWindowOpened(reason);
    retainedPlaybackMode_ = RetainedPlaybackMode::Invalidated;
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::InvalidateRetainedState,
        QString("reason=%1 previous=%2")
            .arg(reason)
            .arg(retainedPlaybackModeLabel(previousMode)),
        true);
}

void BassPreviewAudioBackend::updateRetainedBgmState()
{
    if (backgroundTrackSample_ != nullptr) {
        if (retainedBgmState_ != RetainedBgmState::MissingOnDiskIgnored) {
            retainedBgmState_ = RetainedBgmState::LoadedUsable;
        }
        return;
    }
    retainedBgmState_ = RetainedBgmState::NoneLoaded;
}

void BassPreviewAudioBackend::logTrackFileMissingAfterLoadIfNeeded()
{
    if (backgroundTrackSample_ == nullptr
        || preparedAssets_.trackPath.isEmpty()
        || trackMissingAfterLoadLogged_
        || QFileInfo::exists(preparedAssets_.trackPath)) {
        return;
    }
    trackMissingAfterLoadLogged_ = true;
    retainedBgmState_ = RetainedBgmState::MissingOnDiskIgnored;
    appendAudioDebugLog(
        QString("bgm_disk_missing_after_load path=%1 txn=%2")
            .arg(preparedAssets_.trackPath)
            .arg(playbackTransactionId_));
}

void BassPreviewAudioBackend::suspendPlaybackTransport()
{
#ifdef Q_OS_WIN
    if (masterMixer_ == 0) {
        return;
    }
    logTrackFileMissingAfterLoadIfNeeded();
    const double pauseSecond = authoritativeSecond();
    playbackSession_.lastAuthoritativeSecond = pauseSecond;
    // G1 Commit 6 (corrected post-test): master mixer stays ACTIVE_PLAYING for the
    // engine's lifetime — what makes the BGM go silent is *this* call, setting
    // BASS_MIXER_CHAN_PAUSE on the BGM source. Pre-G1, BASS_ChannelPause on the
    // master silenced every channel implicitly; the original Commit 6 dropped
    // the master pause but left this site empty, which let BGM keep playing
    // through the pause UI state.
    //
    // beta50 followup: same blind spot applied to the touchhold voice. If a
    // touchhold span was active at the moment the user pressed ⏸, its sample
    // kept looping because the master-pause shortcut was gone and no per-
    // sample flag was being set on the touchhold source either. The earlier
    // comment claiming "touchhold is handled by separate pauseTouchholdVoices()
    // callers in the pause flow" was wrong — pauseTouchholdVoices() was only
    // invoked from the anchor / reposition / prepare paths, not from
    // suspendPlaybackTransport itself. Calling it here closes that hole.
    // SFX one-shots still self-terminate so they don't need explicit silencing.
    if (backgroundTrackSample_ != nullptr) {
        backgroundTrackSample_->pause();
    }
    pauseTouchholdVoices();
    playbackSession_.masterRunning = false;
    playbackSession_.backgroundTrackRunning = false;
    retainedPlaybackMode_ = RetainedPlaybackMode::PausedExact;
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::PauseExact,
        QString("second=%1 mode=%2")
            .arg(pauseSecond, 0, 'f', 6)
            .arg(retainedPlaybackModeLabel(retainedPlaybackMode_)));
#endif
}

void BassPreviewAudioBackend::anchorTransportToSecond(double targetSecond, const QString& reason)
{
    QElapsedTimer timer;
    timer.start();
    const double anchoredSecond = clampTimelineSecond(targetSecond);
    preparedPlayback_ = PreparedPlaybackState();
    stopAllSamples();
    resetMasterMixerClock(anchoredSecond);
    configureBackgroundTrackForSecond(
        anchoredSecond,
        reason,
        miacode::preview_audio::bass::BassDebugRoute::Init);
    playbackSession_.lastAuthoritativeSecond = anchoredSecond;
    resetCursor(anchoredSecond, false);
    pauseTouchholdVoices();
    retainedPlaybackMode_ = RetainedPlaybackMode::PausedAnchored;
    logTrackFileMissingAfterLoadIfNeeded();
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::AnchorTransport,
        QString("reason=%1 target=%2 elapsed_ms=%3 next_group_idx=%4")
            .arg(reason)
            .arg(anchoredSecond, 0, 'f', 6)
            .arg(timer.elapsed())
            .arg(playbackSession_.eventGroupIndex),
        true);
}

void BassPreviewAudioBackend::clearResidualVoicesForPausedReposition()
{
#ifdef Q_OS_WIN
    Sample* residualSamples[] = {
        answerSample_.get(),
        judgeSample_.get(),
        judgeBreakSample_.get(),
        slideSample_.get(),
        breakSample_.get(),
        breakSlideStartSample_.get(),
        breakSlideFinishSample_.get(),
        breakSlideTailBreakSample_.get(),
        judgeBreakSlideSample_.get(),
        exSample_.get(),
        touchSample_.get(),
        touchholdSample_,
        fireworkSample_.get()
    };
    for (Sample* sample : residualSamples) {
        if (sample != nullptr) {
            sample->stop();
        }
    }
#endif
}

void BassPreviewAudioBackend::repositionMasterTransportClock(double targetSecond)
{
#ifdef Q_OS_WIN
    if (masterMixer_ == 0) {
        return;
    }
    // G1 Commit 6: master mixer position is no longer used to derive chart-second
    // (authoritativeSecond returns lastAuthoritativeSecond unconditionally). Resetting
    // the master here pre-G1 was about restoring a 0-based offset for that math, so
    // dropping the BASS calls is now purely a cleanup of dead I/O — and avoids one
    // more SoundTouch cold-start in BASS_FX. The session state below still gets
    // recomputed; that part remains load-bearing for downstream callers reading
    // sessionStartSecond / sessionPlaybackRate.
#else
    Q_UNUSED(targetSecond);
#endif
    playbackSession_.sessionStartSecond = clampTimelineSecond(targetSecond);
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
}

void BassPreviewAudioBackend::repositionPausedTransportToSecond(double targetSecond, const QString& reason)
{
    QElapsedTimer timer;
    timer.start();
    const double repositionedSecond = clampTimelineSecond(targetSecond);
    preparedPlayback_ = PreparedPlaybackState();
    clearResidualVoicesForPausedReposition();
    repositionMasterTransportClock(repositionedSecond);
    configureBackgroundTrackForSecond(
        repositionedSecond,
        reason,
        miacode::preview_audio::bass::BassDebugRoute::Transport);
    resetCursor(repositionedSecond, false);
    pauseTouchholdVoices();
    retainedPlaybackMode_ = RetainedPlaybackMode::PausedAnchored;
    logTrackFileMissingAfterLoadIfNeeded();
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::RetainedReset,
        QString("reason=%1 action=reposition target=%2 elapsed_ms=%3 next_group_idx=%4")
            .arg(reason)
            .arg(repositionedSecond, 0, 'f', 6)
            .arg(timer.elapsed())
            .arg(playbackSession_.eventGroupIndex));
    noteTransportReady(reason);
}

void BassPreviewAudioBackend::startTransportFromCurrentAnchor()
{
#ifdef Q_OS_WIN
    if (masterMixer_ == 0) {
        return;
    }
    const RetainedPlaybackMode retainedMode = retainedPlaybackMode_;
    logTrackFileMissingAfterLoadIfNeeded();
    // G1 Commit 6: master mixer was started at engine init and stays running.
    // Resume is now purely the act of unsetting BASS_MIXER_CHAN_PAUSE on each
    // sample (done below via backgroundTrackSample_->play() and similar).
    playbackSession_.masterRunning = true;
    playbackSession_.lastAuthoritativeSecond = authoritativeSecond();
    // G1 followup: bass_play also fires on the retained-resume path (the
    // common case for ▶ after ⏸). The cold-start path emits it from
    // commitPreparedPreviewPlayback; without this line, the row never
    // appears for any session after the first prepare.
    appendAudioDebugLog(
        QString("bass_play txn=%1 start=%2 rate=%3 resume=1 reason=resume_anchor mode=%4")
            .arg(playbackTransactionId_)
            .arg(playbackSession_.lastAuthoritativeSecond, 0, 'f', 6)
            .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
            .arg(retainedPlaybackModeLabel(retainedMode)));
    if (backgroundTrackSample_ != nullptr) {
        if (!playbackSession_.backgroundTrackPendingStart) {
            backgroundTrackSample_->play();
            playbackSession_.backgroundTrackRunning = true;
            // G1 Commit 8: bass_sample_play per §7.2 — resume-from-anchor path.
            appendAudioDebugLog(
                QString("bass_sample_play kind=bgm rate_at_play=%1 offset_sec=%2 reason=resume_anchor")
                    .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
                    .arg(backgroundTrackSample_->currentSec(), 0, 'f', 6));
        } else {
            playbackSession_.backgroundTrackRunning = false;
        }
    } else {
        playbackSession_.backgroundTrackRunning = false;
    }
    if (retainedMode == RetainedPlaybackMode::PausedAnchored) {
        restoreTouchholdVoices(playbackSession_.lastAuthoritativeSecond);
    }
#endif
    preparedPlayback_ = PreparedPlaybackState();
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::ResumeTransport,
        QString("from=%1 second=%2 bg_pending=%3")
            .arg(retainedPlaybackModeLabel(retainedMode))
            .arg(playbackSession_.lastAuthoritativeSecond, 0, 'f', 6)
            .arg(playbackSession_.backgroundTrackPendingStart ? 1 : 0));
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
    noteTransportReady(QStringLiteral("resume_transport"));
}

void BassPreviewAudioBackend::reloadAssets(const PreviewAudioSettings& settings)
{
    MC_OP("BassPreviewAudioBackend::reloadAssets");
    settings_ = settings;
    settings_.normalize();
    invalidateRetainedPlaybackState(QStringLiteral("reload_assets"));
    trackMissingAfterLoadLogged_ = false;
    refreshPreparedAssets();
    if (!initializeAudioEngine()) {
        _mc_op_.fail(QStringLiteral("initializeAudioEngine failed"));
        updateRetainedBgmState();
        return;
    }
    initializeAssets();
    updateRetainedBgmState();
}

bool BassPreviewAudioBackend::audioEngineInitialized() const
{
    return engineInitialized_ && masterMixer_ != 0;
}

void BassPreviewAudioBackend::setChartPath(const QString& chartPath)
{
    MC_OP("BassPreviewAudioBackend::setChartPath");
    const QString normalized = chartPath.isEmpty() ? QString() : QDir::cleanPath(chartPath);
    // Content-aware skip: the same chart path can point at a track whose bytes
    // were rewritten in place (the in-app audio tools rewrite track.mp3 at the
    // same path, and users swap same-named files in Explorer). A path-only
    // equality check would wrongly skip the reload and keep playing the old
    // audio, recoverable only by switching files. Stamp the resolved track by
    // (size, mtime) and skip ONLY when both the path and the stamp are unchanged.
    const QString trackStamp = miacode::fs::fileContentStamp(resolveTrackPath(normalized));
    if (preparedAssets_.chartPath == normalized && preparedAssets_.trackStamp == trackStamp) {
        return;
    }
    invalidateRetainedPlaybackState(QStringLiteral("chart_path_changed"));
    trackMissingAfterLoadLogged_ = false;
    preparedAssets_.chartPath = normalized;
    preparedAssets_.trackStamp = trackStamp;
    refreshPreparedAssets();
    if (engineInitialized_) {
        initializeAssets();
    }
    updateRetainedBgmState();
}

void BassPreviewAudioBackend::setBackgroundTrackOffsetSeconds(double seconds)
{
    const double normalized = qIsFinite(seconds) ? seconds : 0.0;
    if (qAbs(playbackSession_.backgroundTrackOffsetSeconds - normalized) > kBassPreviewEpsilonSeconds) {
        invalidateRetainedPlaybackState(QStringLiteral("background_track_offset_changed"));
    }
    playbackSession_.backgroundTrackOffsetSeconds = normalized;
}

void BassPreviewAudioBackend::setBackgroundTrackPlaybackRate(double rate)
{
    MC_OP("BassPreviewAudioBackend::setBackgroundTrackPlaybackRate");
    const double normalizedRate = qBound(kBassPreviewMinRate, qIsFinite(rate) ? rate : 1.0, kBassPreviewMaxRate);
    // G2 Diag: paused-rate-change entry — paired with the live-rate-change path
    // (applyPlaybackRateAtChartSecond). Same justification: sync beacon
    // survives a fast-fail in setBackgroundTrackSampleSpeed → Sample::setSpeed
    // even when the async DebugLog queue does not.
    {
        char buf[180];
        std::snprintf(buf, sizeof(buf),
            "audio/rate/bass_paused_enter from=%.3f to=%.3f has_bgm=%d",
            playbackSession_.backgroundTrackPlaybackRate,
            normalizedRate,
            backgroundTrackSample_ != nullptr ? 1 : 0);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (qAbs(playbackSession_.backgroundTrackPlaybackRate - normalizedRate) > kBassPreviewEpsilonSeconds) {
        invalidateRetainedPlaybackState(QStringLiteral("background_track_rate_changed"));
    }
    playbackSession_.backgroundTrackPlaybackRate = normalizedRate;
    setBackgroundTrackSampleSpeed(playbackSession_.backgroundTrackPlaybackRate);
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_paused_exit");
}

void BassPreviewAudioBackend::applyPlaybackRateAtChartSecond(double rate, double chartSecond)
{
    MC_OP("BassPreviewAudioBackend::applyPlaybackRateAtChartSecond");
#ifdef Q_OS_WIN
    const double normalizedRate = qBound(kBassPreviewMinRate, qIsFinite(rate) ? rate : 1.0, kBassPreviewMaxRate);
    const double sanitizedChart = qIsFinite(chartSecond) ? chartSecond : 0.0;
    // G2 Diag: every leg of the pause-modify-resume sequence below maps to one
    // synchronous beacon line. Async DebugLog drops queue tail on fast-fail —
    // the user reported 0.5x crash where NO bass_live_rate_change row landed —
    // so the only reliable trail is the sync-fsync beacon. Each line is a
    // strncpy'd stack buffer to keep heap allocation off the critical path.
    {
        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "audio/rate/bass_enter from=%.3f to=%.3f chart=%.6f has_bgm=%d",
            playbackSession_.backgroundTrackPlaybackRate,
            normalizedRate,
            sanitizedChart,
            backgroundTrackSample_ != nullptr ? 1 : 0);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
    if (backgroundTrackSample_ == nullptr) {
        // No BGM loaded — just record the rate so the next sample creation
        // picks it up. invalidateRetainedPlaybackState is intentionally NOT
        // called here; this entry is a live UI tweak, not a structural
        // change to the prepared transport.
        playbackSession_.backgroundTrackPlaybackRate = normalizedRate;
        playbackSession_.sessionPlaybackRate = normalizedRate;
        miacode::oplog::appendStartupBeaconLine(
            "audio/rate/bass_exit reason=no_bgm");
        return;
    }
    // G2 Commit 2: the pause-modify-resume sequence from
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §6.2. Pause first so the
    // BASS_FX tempo stream stops being pulled by the master mixer, then
    // the Sample::setSpeed guard introduced in G1 Commit 6 passes its
    // MIXER_CHAN_PAUSE check and the TEMPO write lands. Re-anchor the
    // cursor to the wall-clock chart-second to keep audio/visual aligned
    // across the rate change, then resume. The whole atomic step happens
    // while the master mixer continues to play silence — the only audible
    // event is the brief BGM gap (~50-100ms typically) covered by the
    // pause flag flip.
    const double oldRate = playbackSession_.backgroundTrackPlaybackRate;
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_about_to_pause");
    backgroundTrackSample_->pause();
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_about_to_setspeed");
    backgroundTrackSample_->setSpeed(normalizedRate);
    const double rawSecond = sanitizedChart + playbackSession_.backgroundTrackOffsetSeconds;
    if (rawSecond >= 0.0) {
        {
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                "audio/rate/bass_about_to_seek raw=%.6f",
                rawSecond);
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        backgroundTrackSample_->setCurrentSec(rawSecond);
    }
    playbackSession_.backgroundTrackPlaybackRate = normalizedRate;
    playbackSession_.sessionPlaybackRate = normalizedRate;
    playbackSession_.sessionStartSecond = sanitizedChart;
    playbackSession_.lastAuthoritativeSecond = sanitizedChart;
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_about_to_resume");
    backgroundTrackSample_->play();
    miacode::oplog::appendStartupBeaconLine("audio/rate/bass_exit reason=ok");
    appendAudioDebugLog(
        QString("bass_live_rate_change from=%1 to=%2 chart=%3 raw=%4")
            .arg(oldRate, 0, 'f', 3)
            .arg(normalizedRate, 0, 'f', 3)
            .arg(sanitizedChart, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6));
    // G2 followup A2 (§7.2 consistency): bass_sample_play also gets a row
    // here so the per-session log has a clean "play flag flipped" record
    // for every flag-flip event (cold play, retained resume, live rate
    // change). reason=live_rate_resume distinguishes this from the other
    // two paths.
    appendAudioDebugLog(
        QString("bass_sample_play kind=bgm rate_at_play=%1 offset_sec=%2 reason=live_rate_resume")
            .arg(normalizedRate, 0, 'f', 3)
            .arg(backgroundTrackSample_->currentSec(), 0, 'f', 6));
#else
    Q_UNUSED(rate);
    Q_UNUSED(chartSecond);
#endif
}

void BassPreviewAudioBackend::applyLevels(const PreviewAudioSettings& settings)
{
    MC_OP("BassPreviewAudioBackend::applyLevels");
    settings_ = settings;
    settings_.normalize();
    applySampleLevels();
    // rebuildPreparedGroups() re-collapses the event groups (breakSlideTailCheer
    // muting changes grouping), which can shift indices, so the cursor must be
    // re-anchored afterward. Anchor to the chart-second of the last group we
    // ALREADY fired — NOT authoritativeSecond(). During active playback that
    // clock is frozen at the last start/seek snapshot (MainWindow's wall-clock
    // is the live master and is never written back here); re-seeking to it would
    // rewind the cursor to the playback start, so the next drainEvents() would
    // replay every tap from there in a single burst. That burst — heard when a
    // volume slider is dragged mid-playback (both the audio-settings dialog and
    // the latency page funnel through applyLevels) — is the long-suspected
    // "stray play/audition event". Anchoring to the last-fired group preserves
    // the fired/unfired boundary regardless of whether we're playing or paused.
    const double anchorSecond =
        (playbackSession_.eventGroupIndex > 0
         && playbackSession_.eventGroupIndex - 1 < preparedGroups_.size())
            ? preparedGroups_[playbackSession_.eventGroupIndex - 1].second
            : -1.0;
    rebuildPreparedGroups();
    resetCursor(anchorSecond, false);
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
            miacode::preview_sfx_timeline::collapseEventGroup(
                preparedTimeline_.events,
                index,
                groupEnd,
                settings_.breakSlideTailCheerMuted));
        index = groupEnd;
    }
}

void BassPreviewAudioBackend::rebuildPreparedTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    QElapsedTimer timer;
    timer.start();
    noteInitWindowOpened(QStringLiteral("rebuild_timeline"));
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
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::RebuildTimeline,
        QString("elapsed_ms=%1 markers=%2 events=%3 groups=%4 touchhold_spans=%5 rate=%6")
            .arg(timer.elapsed())
            .arg(noteMarkers.size())
            .arg(preparedTimeline_.events.size())
            .arg(preparedGroups_.size())
            .arg(preparedTimeline_.touchholdSpans.size())
            .arg(playbackRate, 0, 'f', 3),
        true);
    // G1 Commit 8: §7.2 / §7.4 alias for the rebuild_timeline row, with just the
    // two fields a human reading the validation log cares about. The verbose
    // `bass_init op=rebuild_timeline ...` line above keeps its existing format
    // so log scrapers can still index it; this `bass_timeline_built` row is the
    // one ECHO step 1 reads.
    appendAudioDebugLog(
        QString("bass_timeline_built groups=%1 rate=%2")
            .arg(preparedGroups_.size())
            .arg(playbackRate, 0, 'f', 3));
}

void BassPreviewAudioBackend::configureTimeline(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double playbackRate,
    const PreviewTimingSettings& timingSettings)
{
    MC_OP("BassPreviewAudioBackend::configureTimeline");
    rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
}

void BassPreviewAudioBackend::clearTimeline()
{
    MC_OP("BassPreviewAudioBackend::clearTimeline");
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
    // G1 Commit 6: see comment in repositionMasterTransportClock. Same rationale here:
    // the master keeps running, and authoritativeSecond no longer reads its position.
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
        breakSlideTailBreakSample_.get(),
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
    stopAllSamples();
    resetMasterMixerClock(playbackSession_.lastAuthoritativeSecond);
    playbackSession_.backgroundTrackRunning = false;
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackPendingStartSecond = playbackSession_.lastAuthoritativeSecond;
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
}

// G2 followup A2: armNextGroupSync / clearScheduledGroupSync deleted outright.
// They were empty no-op stubs left behind by G1 Commit 7's BASS_SYNC_POS
// removal; with every call site in this file now also gone, the surface area
// has nothing left to support.

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
    // G1 Commit 7: scheduledGroupIndex_ deleted with the BASS_SYNC_POS scheduler.
    // The next group to trigger is simply the current event-group cursor.
    const int nextGroupIndex = playbackSession_.eventGroupIndex;
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

// G1 Commit 7: onMixerGroupSync / handleMixerGroupSync deleted. The BASS_SYNC_POS
// callback chain was the BASS-cursor-driven SFX trigger path; it's been fully
// replaced by wall-clock drainEvents in MainWindow's per-tick handler.

double BassPreviewAudioBackend::authoritativeSecond() const
{
    // G1 Commit 6: BASS_ChannelGetPosition(masterMixer_) is no longer queried.
    // Wall-clock is the chart-second master (MainWindow's qtPreviewElapsed_), and
    // the master mixer keeps running indefinitely with a position that bears no
    // relation to session time. Return the last externally-recorded snapshot
    // (set at prepare / pause / seek / start transitions). MainWindow has
    // stopped reading this through currentPreviewAuthoritativeAudioClockSecond
    // (see Commit 4); the only remaining consumers are internal — seek-action
    // selection in seekRetainedPreviewPlaybackTransaction and the
    // PausePreviewResult.pauseSecond round-trip, both of which now compare
    // against the wall-clock pause-second the caller already supplied at the
    // last suspendPlaybackTransport.
    return playbackSession_.lastAuthoritativeSecond;
}

void BassPreviewAudioBackend::configureBackgroundTrackForSecond(
    double second,
    const QString& reason,
    miacode::preview_audio::bass::BassDebugRoute route)
{
#ifdef Q_OS_WIN
    QElapsedTimer timer;
    timer.start();
    if (backgroundTrackSample_ == nullptr) {
        playbackSession_.backgroundTrackPendingStart = false;
        playbackSession_.backgroundTrackRunning = false;
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::ConfigureBackgroundTrack,
            QString("reason=%1 second=%2 elapsed_ms=%3 has_bgm=0")
                .arg(reason)
                .arg(second, 0, 'f', 6)
                .arg(timer.elapsed()),
            route == miacode::preview_audio::bass::BassDebugRoute::Init);
        return;
    }

    backgroundTrackSample_->setLoop(false);
    // G1 Commit 6: setSpeed is no longer called from this per-seek / per-pause helper.
    // BASS_ATTRIB_TEMPO is written exactly twice in a sample's lifetime: at
    // initializeAssets() right after creation, and from
    // setBackgroundTrackPlaybackRate() when the user moves the rate slider — and the
    // latter is now guarded by Sample::setSpeed so it only takes effect when the
    // sample is in MIXER_CHAN_PAUSE state. Calling it here on every configure step
    // was the proximate trigger for the 0.5x rate-change crash (scenario (b) in
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.1).
    const double rawSecond = second + playbackSession_.backgroundTrackOffsetSeconds;
    if (rawSecond < 0.0) {
        backgroundTrackSample_->setCurrentSec(0.0);
        backgroundTrackSample_->pause();
        playbackSession_.backgroundTrackPendingStart = true;
        playbackSession_.backgroundTrackPendingStartSecond = second - rawSecond;
        playbackSession_.backgroundTrackRunning = false;
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::ConfigureBackgroundTrack,
            QString("reason=%1 second=%2 raw=%3 elapsed_ms=%4 pending_start=1 pending_second=%5")
                .arg(reason)
                .arg(second, 0, 'f', 6)
                .arg(rawSecond, 0, 'f', 6)
                .arg(timer.elapsed())
                .arg(playbackSession_.backgroundTrackPendingStartSecond, 0, 'f', 6),
            route == miacode::preview_audio::bass::BassDebugRoute::Init);
        return;
    }

    backgroundTrackSample_->setCurrentSec(rawSecond);
    backgroundTrackSample_->pause();
    playbackSession_.backgroundTrackPendingStart = false;
    playbackSession_.backgroundTrackPendingStartSecond = second;
    playbackSession_.backgroundTrackRunning = false;
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::ConfigureBackgroundTrack,
        QString("reason=%1 second=%2 raw=%3 elapsed_ms=%4 pending_start=0")
            .arg(reason)
            .arg(second, 0, 'f', 6)
            .arg(rawSecond, 0, 'f', 6)
            .arg(timer.elapsed()),
        route == miacode::preview_audio::bass::BassDebugRoute::Init);
#else
    Q_UNUSED(second);
    Q_UNUSED(reason);
    Q_UNUSED(route);
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
    MC_OP("BassPreviewAudioBackend::preparePreviewPlaybackTransaction");
    QElapsedTimer timer;
    timer.start();
    noteInitWindowOpened(QStringLiteral("prepare_preview_playback"));
    if (!initializeAudioEngine()) {
        return startSecond;
    }
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
    setBackgroundTrackPlaybackRate(playbackRate);
    stopPlaybackSession();
    // G1 Commit 6: re-apply rate now that stopPlaybackSession has put every Sample
    // back into BASS_MIXER_CHAN_PAUSE state. setBackgroundTrackPlaybackRate above
    // may have skipped its internal setSpeed if the previous session was still
    // playing when this prepare came in (the new Sample::setSpeed guard short-
    // circuits writes to BASS_FX TEMPO whenever the source is being actively
    // pulled — see PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.1 / §6.1).
    // The rate value is already stored in playbackSession_; this call just
    // pushes it onto the now-paused tempo stream.
    setBackgroundTrackSampleSpeed(playbackSession_.backgroundTrackPlaybackRate);
    resetMasterMixerClock(startSecond);
    configureBackgroundTrackForSecond(
        startSecond,
        QStringLiteral("prepare_preview_playback"),
        miacode::preview_audio::bass::BassDebugRoute::Init);
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
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::PreparePreviewPlayback,
        QString("elapsed_ms=%1 start=%2 resume=%3 rate=%4 groups=%5")
            .arg(timer.elapsed())
            .arg(preparedPlayback_.startSecond, 0, 'f', 6)
            .arg(resumeFromPause ? 1 : 0)
            .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
            .arg(preparedGroups_.size()),
        true);
    noteTransportReady(QStringLiteral("prepare_preview_playback"));
    return preparedPlayback_.startSecond;
}

void BassPreviewAudioBackend::commitPreparedPreviewPlayback()
{
    MC_OP("BassPreviewAudioBackend::commitPreparedPreviewPlayback");
#ifdef Q_OS_WIN
    if (!preparedPlayback_.pending || masterMixer_ == 0) {
        return;
    }
    // G1 Commit 6: master mixer was started at engine init. The commit step is now
    // purely about unsetting BASS_MIXER_CHAN_PAUSE on the BGM sample below.
    playbackSession_.masterRunning = true;
    playbackSession_.lastAuthoritativeSecond = preparedPlayback_.startSecond;
    if (backgroundTrackSample_ != nullptr && !playbackSession_.backgroundTrackPendingStart) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackRunning = true;
        // G1 Commit 8: bass_sample_play per §7.2 — confirms the BGM flag flipped
        // *after* TEMPO was set (Commit 6 invariant) and records where in the
        // source we resumed reading.
        appendAudioDebugLog(
            QString("bass_sample_play kind=bgm rate_at_play=%1 offset_sec=%2 reason=commit")
                .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
                .arg(backgroundTrackSample_->currentSec(), 0, 'f', 6));
    }
    if (!preparedPlayback_.resumeFromPause) {
        drainEvents(preparedPlayback_.startSecond);
    }
    restoreTouchholdVoices(preparedPlayback_.startSecond);
    logTrackFileMissingAfterLoadIfNeeded();
    // G1 Commit 8: rename `bass_commit` → `bass_play` per §7.2 of
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md, and add the rate field so
    // ECHO validation can read out the speed each play session started with
    // without cross-referencing other lines.
    appendAudioDebugLog(
        QString("bass_play txn=%1 start=%2 rate=%3 resume=%4 bg_pending=%5")
            .arg(playbackTransactionId_)
            .arg(preparedPlayback_.startSecond, 0, 'f', 6)
            .arg(playbackSession_.backgroundTrackPlaybackRate, 0, 'f', 3)
            .arg(preparedPlayback_.resumeFromPause ? 1 : 0)
            .arg(playbackSession_.backgroundTrackPendingStart ? 1 : 0));
#endif
    preparedPlayback_ = PreparedPlaybackState();
}

void BassPreviewAudioBackend::cancelPreparedPreviewPlayback()
{
    MC_OP("BassPreviewAudioBackend::cancelPreparedPreviewPlayback");
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
    MC_OP("BassPreviewAudioBackend::applyPausedPreviewState");
    preparedPlayback_ = PreparedPlaybackState();
    const bool transportInvalidated =
        noteMarkersChanged
        || preparedTimeline_.sourceNoteMarkers.size() != noteMarkers.size()
        || qAbs(preparedTimelinePlaybackRate_ - playbackRate) > kBassPreviewEpsilonSeconds
        || !previewTimingSettingsEqual(timingSettings_, timingSettings);
    if (transportInvalidated) {
        noteInitWindowOpened(QStringLiteral("apply_paused_state_invalidated"));
        rebuildPreparedTimeline(noteMarkers, playbackRate, timingSettings);
    }
    const double clampedPauseSecond = clampTimelineSecond(pauseSecond);
    // G1 fix (seek-into-片头 A/V desync): the reuse "same second" check must
    // compare against where the BGM ACTUALLY is, not retainedTransportSecond()
    // (= lastAuthoritativeSecond) — that field no longer tracks the live BGM
    // cursor (G1 Commit 6, see the KeepPaused note below). A drag into the
    // negative 片头 region moves it to match the target while the BGM stays parked
    // at its old position; trusting it then "reuses" the transport and resumes the
    // BGM from the wrong second. When a BGM is the live clock, use its real
    // chart-second so a mismatch falls through to repositionPausedTransportToSecond.
    const double reuseCompareSecond =
        (hasBackgroundTrack() && backgroundTrackSample_ != nullptr
         && !playbackSession_.backgroundTrackPendingStart)
            ? backgroundTrackSample_->currentSec() - playbackSession_.backgroundTrackOffsetSeconds
            : retainedTransportSecond();
    if (miacode::preview_audio::bass::canReusePausedTransport(
            retainedPlaybackMode_,
            transportInvalidated,
            reuseCompareSecond,
            clampedPauseSecond)) {
        playbackSession_.lastAuthoritativeSecond = clampedPauseSecond;
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::RetainedReset,
            QString("action=reuse mode=%1 second=%2")
                .arg(retainedPlaybackModeLabel(retainedPlaybackMode_))
                .arg(clampedPauseSecond, 0, 'f', 6));
        noteTransportReady(QStringLiteral("apply_paused_state_reuse"));
        return;
    }
    if (!transportInvalidated
        && (retainedPlaybackMode_ == RetainedPlaybackMode::PausedExact
            || retainedPlaybackMode_ == RetainedPlaybackMode::PausedAnchored)) {
        repositionPausedTransportToSecond(clampedPauseSecond, QStringLiteral("apply_paused_state"));
        return;
    }
    anchorTransportToSecond(clampedPauseSecond, QStringLiteral("apply_paused_state"));
    noteTransportReady(QStringLiteral("apply_paused_state_anchor"));
}

double BassPreviewAudioBackend::startPreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate)
{
    MC_OP("BassPreviewAudioBackend::startPreviewPlaybackTransaction");
    const double preparedSecond = preparePreviewPlaybackTransaction(startSecond, resumeFromPause, playbackRate);
    commitPreparedPreviewPlayback();
    return preparedSecond;
}

miacode::preview_audio::PausePreviewResult BassPreviewAudioBackend::capturePausedPreviewTransaction()
{
    return pausePreviewPlaybackTransaction();
}

miacode::preview_audio::PausePreviewResult BassPreviewAudioBackend::pausePreviewPlaybackTransaction()
{
    MC_OP("BassPreviewAudioBackend::pausePreviewPlaybackTransaction");
    PausePreviewResult result;
    result.usedBackgroundTrack = hasBackgroundTrack();
    result.pauseSecond = authoritativeSecond();
    // G1 fix (A/V desync on resume): authoritativeSecond() is the last externally
    // recorded snapshot, which on the export-page audition pause path can be stale
    // — reset toward the session start (0) — while the BGM sample kept advancing to
    // the true playhead. That desynced resume (transport from 0, BGM from its real
    // position). When a BGM is the live clock, take ITS position (the ground truth
    // of where the audio actually is) as the pause second so transport and BGM
    // resume in lockstep. No-op in the editor preview, where the BGM already tracks
    // the playhead, so authoritativeSecond() and the BGM position agree.
    if (playbackSession_.masterRunning
        && result.usedBackgroundTrack
        && backgroundTrackSample_ != nullptr
        && !playbackSession_.backgroundTrackPendingStart) {
        const double bgmChartSecond =
            backgroundTrackSample_->currentSec() - playbackSession_.backgroundTrackOffsetSeconds;
        if (qIsFinite(bgmChartSecond) && bgmChartSecond >= 0.0) {
            result.pauseSecond = bgmChartSecond;
        }
    }
    playbackSession_.lastAuthoritativeSecond = result.pauseSecond;
    if (playbackSession_.masterRunning) {
        suspendPlaybackTransport();
    }
    result.retainedMode = retainedPlaybackMode_;
    result.retainedBgmState = retainedBgmState_;
    return result;
}

double BassPreviewAudioBackend::resumeRetainedPreviewPlaybackTransaction()
{
    MC_OP("BassPreviewAudioBackend::resumeRetainedPreviewPlaybackTransaction");
    if (retainedPlaybackMode_ != RetainedPlaybackMode::PausedExact
        && retainedPlaybackMode_ != RetainedPlaybackMode::PausedAnchored) {
        return authoritativeSecond();
    }
    startTransportFromCurrentAnchor();
    return authoritativeSecond();
}

double BassPreviewAudioBackend::seekRetainedPreviewPlaybackTransaction(double targetSecond, bool continuePlaying)
{
    MC_OP("BassPreviewAudioBackend::seekRetainedPreviewPlaybackTransaction");
    const double clampedSecond = clampTimelineSecond(targetSecond);
    const bool sameSecond = retainedSecondMatches(clampedSecond);
    const auto action = miacode::preview_audio::bass::retainedSeekAction(
        retainedPlaybackMode_,
        sameSecond,
        continuePlaying);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::RetainedSeek,
        QString("mode=%1 action=%2 target=%3 continue=%4 same_second=%5")
            .arg(retainedPlaybackModeLabel(retainedPlaybackMode_))
            .arg(retainedSeekActionLabel(action))
            .arg(clampedSecond, 0, 'f', 6)
            .arg(continuePlaying ? 1 : 0)
            .arg(sameSecond ? 1 : 0));
    switch (action) {
    case miacode::preview_audio::bass::RetainedSeekAction::KeepPaused:
        // beta50 followup — same root cause as resetRetainedPreviewPlaybackTransaction's
        // matching block. Post-G1 lastAuthoritativeSecond doesn't track the
        // live BGM cursor / SFX event cursor / touchhold, so sameSecond=true
        // (which got us here) doesn't mean those three are aligned at
        // clampedSecond. Route through repositionPausedTransportToSecond so
        // BGM, SFX-event-index, and touchhold all re-anchor in one shot.
        repositionPausedTransportToSecond(clampedSecond, QStringLiteral("retained_seek_paused"));
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::ResumeExact:
    case miacode::preview_audio::bass::RetainedSeekAction::ResumeAnchored:
        startTransportFromCurrentAnchor();
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::RepositionPaused:
        repositionPausedTransportToSecond(clampedSecond, QStringLiteral("retained_seek_paused"));
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::RepositionAndResume:
        repositionPausedTransportToSecond(clampedSecond, QStringLiteral("retained_seek_resume"));
        startTransportFromCurrentAnchor();
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::AnchorPaused:
        noteInitWindowOpened(QStringLiteral("retained_seek_paused"));
        anchorTransportToSecond(clampedSecond, QStringLiteral("retained_seek_paused"));
        noteTransportReady(QStringLiteral("retained_seek_paused_anchor"));
        break;
    case miacode::preview_audio::bass::RetainedSeekAction::AnchorAndResume:
    default:
        noteInitWindowOpened(QStringLiteral("retained_seek_resume"));
        anchorTransportToSecond(clampedSecond, QStringLiteral("retained_seek_resume"));
        startTransportFromCurrentAnchor();
        break;
    }
    return authoritativeSecond();
}

void BassPreviewAudioBackend::resetRetainedPreviewPlaybackTransaction(double targetSecond)
{
    MC_OP("BassPreviewAudioBackend::resetRetainedPreviewPlaybackTransaction");
    const double clampedSecond = clampTimelineSecond(targetSecond);
    const bool sameSecond = retainedSecondMatches(clampedSecond);
    const auto action = miacode::preview_audio::bass::retainedSeekAction(
        retainedPlaybackMode_,
        sameSecond,
        false);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::RetainedReset,
        QString("mode=%1 action=%2 target=%3 same_second=%4")
            .arg(retainedPlaybackModeLabel(retainedPlaybackMode_))
            .arg(retainedSeekActionLabel(action))
            .arg(clampedSecond, 0, 'f', 6)
            .arg(sameSecond ? 1 : 0));
    if (action == miacode::preview_audio::bass::RetainedSeekAction::KeepPaused
        || action == miacode::preview_audio::bass::RetainedSeekAction::RepositionPaused) {
        // beta50 followup — Stop-button audio sync fix:
        //
        // sameSecond is computed against lastAuthoritativeSecond, but G1
        // Commit 6 stopped that field from tracking the live BGM cursor
        // (and the live SFX event cursor) — it now only carries whatever
        // value was last *recorded* at the previous transport state
        // change. So "sameSecond=true" no longer implies "BGM cursor +
        // SFX event cursor + touchhold are already at clampedSecond".
        //
        // Concretely: Play 0 → ⏸10 → ▶ resume → ⏸30 → ⏹ Stop back to 10
        // landed here with sameSecond=true → KeepPaused → only
        // lastAuthoritativeSecond got updated, while BGM cursor stayed
        // near ~30 and the SFX event-group cursor still pointed past
        // 10's groups. Result: next ▶ played the audio from chart-30
        // while visuals were on chart-10, and SFX skipped every group
        // between 10 and 30.
        //
        // Fix: collapse KeepPaused into the same repositionPausedTransportToSecond
        // path RepositionPaused uses. That routine seeks all three cursors
        // (BGM source via configureBackgroundTrackForSecond, SFX event
        // index via resetCursor, touchhold via pauseTouchholdVoices) plus
        // resets session-state housekeeping. Cost is one BASS_ChannelSetPosition
        // + one event-cursor scan; both are cheap.
        repositionPausedTransportToSecond(clampedSecond, QStringLiteral("retained_reset"));
        return;
    }
    noteInitWindowOpened(QStringLiteral("retained_reset"));
    anchorTransportToSecond(clampedSecond, QStringLiteral("retained_reset"));
    noteTransportReady(QStringLiteral("retained_reset_anchor"));
}

void BassPreviewAudioBackend::clearRetainedPreviewPlaybackTransaction()
{
    MC_OP("BassPreviewAudioBackend::clearRetainedPreviewPlaybackTransaction");
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
}

BassPreviewAudioBackend::RetainedPlaybackMode BassPreviewAudioBackend::retainedPlaybackMode() const
{
    return retainedPlaybackMode_;
}

BassPreviewAudioBackend::RetainedBgmState BassPreviewAudioBackend::retainedBgmState() const
{
    return retainedBgmState_;
}

double BassPreviewAudioBackend::authoritativePlaybackSecond() const
{
    return authoritativeSecond();
}

double BassPreviewAudioBackend::syncPreviewPlaybackClockTransaction(double fallbackSecond)
{
    if (shuttingDown_.load(std::memory_order_acquire)) {
        return playbackSession_.lastAuthoritativeSecond;
    }
    logTrackFileMissingAfterLoadIfNeeded();
    const double second = authoritativeSecond();
    playbackSession_.lastAuthoritativeSecond = second;
    maybeStartPendingBackgroundTrack(second);
    drainEvents(second);
    logPlaybackStatus(second, fallbackSecond);
    return second;
}

void BassPreviewAudioBackend::resetCursor(double second, bool includeCurrentSecond)
{
    MC_OP("BassPreviewAudioBackend::resetCursor");
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
        if (event.kind == QLatin1String("touchhold_start")
            || event.kind == QLatin1String("touchhold_stop")) {
            // Latest-wins ownership: rather than naively start/stop the single
            // shared touch-hold sample per event (which let a prior span's stop
            // clobber the next span's start at a seamless join, and let an older
            // span's stop kill a newer overlapping one), re-derive who should own
            // the voice at this instant and reconcile. Order-independent.
            reconcileTouchholdVoice(event.second);
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
    // G1 Commit 8: bass_sfx_drain per §7.2. Emit one line per tick that actually
    // triggered something, with the chart-second the tick was draining toward,
    // the count, and the first/last group indices. Quiet ticks (drained=0) stay
    // out of the log so the channel isn't dominated by no-ops. Track the range
    // around the loop so we can read it in the log line afterward.
    const int firstIdxBeforeDrain = playbackSession_.eventGroupIndex;
    int drainedCount = 0;
    int lastTriggeredIdx = -1;
    while (playbackSession_.eventGroupIndex < preparedGroups_.size()) {
        const CollapsedEventGroup& group = preparedGroups_[playbackSession_.eventGroupIndex];
        if (group.second > second + kBassPreviewEpsilonSeconds) {
            break;
        }
        // G1 Commit 7: pre-G1 each drain had to cancel a matching BASS_SYNC_POS arm
        // so the same group wasn't triggered twice. No arms exist anymore (the SYNC
        // scheduler is gone), so the cancellation block has been deleted.
        triggerGroup(group);
        playbackSession_.lastTriggeredGroupIndex = playbackSession_.eventGroupIndex;
        playbackSession_.lastTriggeredGroupSecond = group.second;
        playbackSession_.triggeredGroupCount += 1;
        lastTriggeredIdx = playbackSession_.eventGroupIndex;
        ++drainedCount;
        ++playbackSession_.eventGroupIndex;
    }
    if (drainedCount > 0) {
        appendAudioDebugLog(
            QString("bass_sfx_drain at_chart=%1 drained=%2 first_idx=%3 last_idx=%4")
                .arg(second, 0, 'f', 6)
                .arg(drainedCount)
                .arg(firstIdxBeforeDrain)
                .arg(lastTriggeredIdx));
    }
}

void BassPreviewAudioBackend::reconcileTouchholdVoice(double second)
{
#ifdef Q_OS_WIN
    if (touchholdSample_ == nullptr) {
        return;
    }
    const int owner = miacode::preview_sfx_timeline::touchholdOwnerSpanIndexAt(
        preparedTimeline_.touchholdSpans, second);
    if (owner == touchholdOwnerSpanIndex_) {
        return;  // voice already belongs to the right span — leave it playing
    }
    touchholdOwnerSpanIndex_ = owner;
    if (owner < 0) {
        touchholdSample_->stop();
        return;
    }
    const TouchholdSpan& span = preparedTimeline_.touchholdSpans[owner];
    touchholdSample_->setCurrentSec(qMax(0.0, second - span.startSecond));
    touchholdSample_->play();
#else
    Q_UNUSED(second);
#endif
}

void BassPreviewAudioBackend::pauseTouchholdVoices()
{
    MC_OP("BassPreviewAudioBackend::pauseTouchholdVoices");
#ifdef Q_OS_WIN
    if (touchholdSample_ != nullptr) {
        touchholdSample_->stop();
    }
#endif
    touchholdOwnerSpanIndex_ = -1;
}

void BassPreviewAudioBackend::restoreTouchholdVoices(double second)
{
    MC_OP("BassPreviewAudioBackend::restoreTouchholdVoices");
#ifdef Q_OS_WIN
    pauseTouchholdVoices();
    reconcileTouchholdVoice(second);
#else
    Q_UNUSED(second);
#endif
}

void BassPreviewAudioBackend::syncBackgroundTrack(double timelineSecond)
{
    maybeStartPendingBackgroundTrack(timelineSecond);
    // G1 followup: restore the per-second bass_status row. Pre-G1 it was driven
    // from syncPreviewPlaybackClockTransaction; that path is gone (Commit 5),
    // and MainWindow now calls syncBackgroundTrack on every tick as the BGM-
    // pending-start hook. Riding that schedule keeps bass_status emitting at
    // the same ~16ms cadence the old call had, then rate-limited to once per
    // second internally by logPlaybackStatus. authoritativeSecond returns the
    // last-recorded snapshot now, so we pass MainWindow's wall-clock second
    // directly into both arguments — `auth` will track wall-clock and the
    // row's drift_ms collapses to ~0 as long as the chart is on rate.
    logPlaybackStatus(timelineSecond, timelineSecond);
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
    MC_OP("BassPreviewAudioBackend::startBackgroundTrack");
#ifdef Q_OS_WIN
    if (masterMixer_ != 0 && !playbackSession_.masterRunning) {
        resetMasterMixerClock(second);
        // G1 Commit 6: master mixer was started at engine init and never stops.
        playbackSession_.masterRunning = true;
        playbackSession_.lastAuthoritativeSecond = clampTimelineSecond(second);
    }
    configureBackgroundTrackForSecond(
        second,
        QStringLiteral("start_background_track"),
        miacode::preview_audio::bass::BassDebugRoute::Transport);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::StartBackgroundTrack,
        QString("second=%1").arg(second, 0, 'f', 6));
    if (backgroundTrackSample_ != nullptr && !playbackSession_.backgroundTrackPendingStart) {
        backgroundTrackSample_->play();
        playbackSession_.backgroundTrackRunning = true;
    }
    noteTransportReady(QStringLiteral("start_background_track"));
#else
    Q_UNUSED(second);
#endif
}

void BassPreviewAudioBackend::seekBackgroundTrack(double second)
{
    MC_OP("BassPreviewAudioBackend::seekBackgroundTrack");
    configureBackgroundTrackForSecond(
        second,
        QStringLiteral("seek_background_track"),
        miacode::preview_audio::bass::BassDebugRoute::Transport);
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::SeekBackgroundTrack,
        QString("second=%1").arg(second, 0, 'f', 6));
    noteTransportReady(QStringLiteral("seek_background_track"));
}

void BassPreviewAudioBackend::pauseBackgroundTrack()
{
    MC_OP("BassPreviewAudioBackend::pauseBackgroundTrack");
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
    MC_OP("BassPreviewAudioBackend::audition");
#ifdef Q_OS_WIN
    if (!initializeAudioEngine() || masterMixer_ == 0) {
        return false;
    }
    if (!playbackSession_.masterRunning) {
        resetMasterMixerClock(0.0);
        // G1 Commit 6: master mixer was started at engine init and never stops.
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
    MC_OP("BassPreviewAudioBackend::stopAll");
    stopPlaybackSession();
    preparedPlayback_ = PreparedPlaybackState();
    retainedPlaybackMode_ = RetainedPlaybackMode::None;
}

void BassPreviewAudioBackend::prepareForShutdown()
{
    MC_OP("BassPreviewAudioBackend::prepareForShutdown");
    shuttingDown_.store(true, std::memory_order_release);
    stopAll();
}
