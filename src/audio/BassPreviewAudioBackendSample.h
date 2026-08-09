#pragma once

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

#ifdef MIACODE_HAS_BASS_AUDIO
#include "bass.h"
#include "bassmix.h"
#endif

#include "BassPreviewAudioBackendImpl.h"

using namespace miacode::audio::bass_detail;

#ifdef MIACODE_HAS_BASS_AUDIO

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
    SampleSpeedMode speedMode = SampleSpeedMode::None;
    float nativeFrequency = 0.0f;
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
        speedChangeSupported = false;
        speedMode = SampleSpeedMode::None;
        nativeFrequency = 0.0f;
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

    void applyTempoWindowConfig(DWORD tempoStream, const BassTempoWindowConfig& config)
    {
        if (tempoStream == 0 || kind != QLatin1String("bgm")) {
            return;
        }
        if (!config.valid) {
            appendAudioDebugLog(
                QString("bgm_tempo_window label=%1 valid=0 apply=0 error=%2 preset=%3 params=%4")
                    .arg(config.label)
                    .arg(config.error)
                    .arg(config.rawPreset)
                    .arg(config.rawParams));
            return;
        }
        if (!config.apply) {
            appendAudioDebugLog(
                QString("bgm_tempo_window label=%1 valid=1 apply=0 preset=%2 params=%3")
                    .arg(config.label)
                    .arg(config.rawPreset)
                    .arg(config.rawParams));
            return;
        }

        BASS_ChannelSetAttribute(tempoStream, kBassPreviewTempoOptionSequenceMs, config.sequenceMs);
        noteBassErr("sample_tempo_window/set_sequence");
        BASS_ChannelSetAttribute(tempoStream, kBassPreviewTempoOptionSeekWindowMs, config.seekWindowMs);
        noteBassErr("sample_tempo_window/set_seek");
        BASS_ChannelSetAttribute(tempoStream, kBassPreviewTempoOptionOverlapMs, config.overlapMs);
        noteBassErr("sample_tempo_window/set_overlap");

        float actualSequenceMs = -1.0f;
        float actualSeekWindowMs = -1.0f;
        float actualOverlapMs = -1.0f;
        const bool gotSequence =
            BASS_ChannelGetAttribute(tempoStream, kBassPreviewTempoOptionSequenceMs, &actualSequenceMs);
        noteBassErr("sample_tempo_window/get_sequence");
        const bool gotSeek =
            BASS_ChannelGetAttribute(tempoStream, kBassPreviewTempoOptionSeekWindowMs, &actualSeekWindowMs);
        noteBassErr("sample_tempo_window/get_seek");
        const bool gotOverlap =
            BASS_ChannelGetAttribute(tempoStream, kBassPreviewTempoOptionOverlapMs, &actualOverlapMs);
        noteBassErr("sample_tempo_window/get_overlap");

        appendAudioDebugLog(
            QString("bgm_tempo_window label=%1 valid=1 apply=1 sequence_ms=%2 seek_ms=%3 overlap_ms=%4 actual_sequence_ms=%5 actual_seek_ms=%6 actual_overlap_ms=%7 got=%8%9%10 preset=%11 params=%12")
                .arg(config.label)
                .arg(static_cast<double>(config.sequenceMs), 0, 'f', 3)
                .arg(static_cast<double>(config.seekWindowMs), 0, 'f', 3)
                .arg(static_cast<double>(config.overlapMs), 0, 'f', 3)
                .arg(static_cast<double>(actualSequenceMs), 0, 'f', 3)
                .arg(static_cast<double>(actualSeekWindowMs), 0, 'f', 3)
                .arg(static_cast<double>(actualOverlapMs), 0, 'f', 3)
                .arg(gotSequence ? 1 : 0)
                .arg(gotSeek ? 1 : 0)
                .arg(gotOverlap ? 1 : 0)
                .arg(config.rawPreset)
                .arg(config.rawParams));
    }

    bool create(
        BassPreviewAudioBackend* backend,
        const QString& samplePath,
        const QString& sampleName,
        const QString& sampleKind,
        bool normalize,
        SampleSpeedMode requestedSpeedMode
    )
    {
        free();

        path = samplePath;
        name = sampleName;
        kind = sampleKind;
        speedMode = requestedSpeedMode;
        speedChangeSupported = speedMode != SampleSpeedMode::None;

        // G1 Commit 8 followup: per-sample create beacon per §7.3. The beacon is
        // heap-free and platform-neutral, so if BASS_StreamCreateFile or BASS_FX_TempoCreate
        // crashes mid-call (the (a)-scenario fault locus per
        // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.1), this line still lands
        // and lets the next startup pin which sample was in flight. Filename only,
        // not full path, to fit the beacon line budget.
        {
            char buf[160];
            const QByteArray kindUtf8 = sampleKind.toUtf8();
            const QByteArray nameUtf8 = QFileInfo(samplePath).fileName().toUtf8();
            const QByteArray modeUtf8 = sampleSpeedModeLabel(speedMode).toUtf8();
            std::snprintf(buf, sizeof(buf),
                "audio/sample/create kind=%s file=%s speed_change=%d mode=%s normalize=%d",
                kindUtf8.constData(),
                nameUtf8.constData(),
                speedChangeSupported ? 1 : 0,
                modeUtf8.constData(),
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

        if (speedMode == SampleSpeedMode::Tempo) {
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
            applyTempoWindowConfig(tempoStream, backgroundTrackTempoWindowConfig());
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
        nativeFrequency = qMax(1.0f, channelFrequency);
        const DWORD resamplerFlags = BASS_STREAM_DECODE | BASS_SAMPLE_FLOAT | BASS_MIXER_NONSTOP;
        const DWORD resamplerStream =
            BASS_Mixer_StreamCreate(
                static_cast<DWORD>(nativeFrequency),
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
            const QByteArray modeUtf8 = sampleSpeedModeLabel(speedMode).toUtf8();
            std::snprintf(buf, sizeof(buf),
                "audio/sample/create_ok kind=%s mode=%s decode=0x%08x tempo=0x%08x resampler=0x%08x length_sec=%.3f",
                kindUtf8.constData(),
                modeUtf8.constData(),
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

    bool canWriteSpeedAttribute(double rate)
    {
        const QByteArray kindUtf8 = kind.toUtf8();
        const QByteArray modeUtf8 = sampleSpeedModeLabel(speedMode).toUtf8();
        {
            char buf[190];
            std::snprintf(buf, sizeof(buf),
                "audio/rate/sample_read_flags kind=%s mode=%s source=0x%08x rate=%.3f",
                kindUtf8.constData(),
                modeUtf8.constData(),
                static_cast<unsigned>(source),
                rate);
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        const DWORD flags = BASS_Mixer_ChannelFlags(source, 0, 0);
        noteBassErr("sample_set_speed/read_flags");
        if (flags == static_cast<DWORD>(-1) || (flags & BASS_MIXER_CHAN_PAUSE) != 0) {
            return true;
        }
        {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "audio/rate/sample_skip_playing kind=%s mode=%s source=0x%08x rate=%.3f flags=0x%08lx",
                kindUtf8.constData(),
                modeUtf8.constData(),
                static_cast<unsigned>(source),
                rate,
                static_cast<unsigned long>(flags));
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        appendAudioDebugLog(
            QString("sample_set_speed_skipped_playing kind=%1 mode=%2 rate=%3")
                .arg(kind)
                .arg(sampleSpeedModeLabel(speedMode))
                .arg(rate, 0, 'f', 3));
        return false;
    }

    void setSpeed(double rate)
    {
        if (!valid() || !speedChangeSupported) {
            return;
        }
        const double normalizedRate = qBound(kBassPreviewMinRate, rate, kBassPreviewMaxRate);
        if (!canWriteSpeedAttribute(normalizedRate)) {
            return;
        }
        if (speedMode == SampleSpeedMode::RateTranspose) {
            float baseFrequency = nativeFrequency;
            if (baseFrequency <= 0.0f) {
                BASS_ChannelGetAttribute(source, BASS_ATTRIB_FREQ, &baseFrequency);
                noteBassErr("sample_set_speed/read_freq");
                nativeFrequency = qMax(1.0f, baseFrequency);
                baseFrequency = nativeFrequency;
            }
            const float targetFrequency = static_cast<float>(baseFrequency * normalizedRate);
            BASS_ChannelSetAttribute(source, BASS_ATTRIB_FREQ, targetFrequency);
            noteBassErr("sample_set_speed/rate_transpose");
            if (kind == QLatin1String("bgm")) {
                appendAudioDebugLog(
                    QString("bgm_rate_mode mode=%1 rate=%2 native_freq=%3 target_freq=%4")
                        .arg(sampleSpeedModeLabel(speedMode))
                        .arg(normalizedRate, 0, 'f', 3)
                        .arg(static_cast<double>(baseFrequency), 0, 'f', 3)
                        .arg(static_cast<double>(targetFrequency), 0, 'f', 3));
            }
            return;
        }
        if (speedMode != SampleSpeedMode::Tempo) {
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
        const float tempo = static_cast<float>((normalizedRate - 1.0) * 100.0);
        {
            char buf[220];
            std::snprintf(buf, sizeof(buf),
                "audio/rate/sample_about_to_setattr_tempo kind=%s source=0x%08x rate=%.3f tempo=%.3f flags=0x%08lx",
                kindUtf8.constData(),
                static_cast<unsigned>(source),
                normalizedRate,
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
                normalizedRate);
            miacode::oplog::appendStartupBeaconLine(buf);
        }
        if (kind == QLatin1String("bgm")) {
            appendAudioDebugLog(
                QString("bgm_rate_mode mode=%1 rate=%2 tempo=%3")
                    .arg(sampleSpeedModeLabel(speedMode))
                    .arg(normalizedRate, 0, 'f', 3)
                    .arg(static_cast<double>(tempo), 0, 'f', 3));
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

    bool playOneShot(double eventGain, int* nativeErrorCode = nullptr)
    {
        if (nativeErrorCode != nullptr) {
            *nativeErrorCode = 0;
        }
        if (!valid()) {
            return false;
        }
        applyVolume(eventGain);
        const bool seeked = BASS_Mixer_ChannelSetPosition(source, 0, BASS_POS_BYTE);
        const int seekError = noteBassErr("sample_one_shot/seek_zero");
        if (!seeked) {
            if (nativeErrorCode != nullptr) {
                *nativeErrorCode = seekError;
            }
            return false;
        }
        const DWORD flags = BASS_Mixer_ChannelFlags(source, 0, BASS_MIXER_CHAN_PAUSE);
        const int flagsError = noteBassErr("sample_one_shot/clear_pause_flag");
        if (flags == static_cast<DWORD>(-1)) {
            if (nativeErrorCode != nullptr) {
                *nativeErrorCode = flagsError;
            }
            return false;
        }
        return true;
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
