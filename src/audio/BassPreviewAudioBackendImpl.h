#pragma once

// Internal implementation header for the BassPreviewAudioBackend translation
// units. Extracted verbatim from the original BassPreviewAudioBackend.cpp
// anonymous namespace during the god-file split; the only changes are the move
// into the named namespace miacode::audio::bass_detail and the addition of
// `inline` / `inline constexpr` linkage so the helpers can be shared across TUs
// without ODR violations. The mutable file-static reference counter
// gBassDeviceRefCount is declared `extern` here and DEFINED exactly once in
// BassPreviewAudioBackend_EngineInit.cpp.
//
// NOTE: this header is included by each TU *after* that TU's #include block
// (Qt headers + common headers + the Windows/BASS headers guarded by
// Q_OS_WIN), exactly mirroring the original file's ordering where the
// anonymous namespace followed the includes. The DWORD / BASS_* references in
// the tempo constants and noteBassErr therefore resolve against the including
// TU's windows.h / bass.h, just as they did before the split.

#include "BassPreviewDebugLogRouting.h"
#include "PreviewAudioBackend.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewSfxTimeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QtMath>

namespace miacode {
namespace audio {
namespace bass_detail {

inline constexpr double kBassPreviewEpsilonSeconds = miacode::preview_sfx_timeline::kTimelineEpsilonSeconds;
inline constexpr double kBassPreviewMinRate = 0.25;
inline constexpr double kBassPreviewMaxRate = 2.0;
inline constexpr double kBassPreviewStatusLogIntervalSeconds = 1.0;
inline constexpr DWORD kBassPreviewTempoFlags = 0x10000 | BASS_STREAM_DECODE; // BASS_FX_FREESOURCE | BASS_STREAM_DECODE
inline constexpr DWORD kBassPreviewTempoAttribute = 0x10000; // BASS_ATTRIB_TEMPO
inline constexpr DWORD kBassPreviewTempoOptionSequenceMs = 0x10013; // BASS_ATTRIB_TEMPO_OPTION_SEQUENCE_MS
inline constexpr DWORD kBassPreviewTempoOptionSeekWindowMs = 0x10014; // BASS_ATTRIB_TEMPO_OPTION_SEEKWINDOW_MS
inline constexpr DWORD kBassPreviewTempoOptionOverlapMs = 0x10015; // BASS_ATTRIB_TEMPO_OPTION_OVERLAP_MS

enum class SampleSpeedMode {
    None,
    Tempo,
    RateTranspose,
};

inline QString sampleSpeedModeLabel(SampleSpeedMode mode)
{
    switch (mode) {
    case SampleSpeedMode::Tempo:
        return QStringLiteral("tempo");
    case SampleSpeedMode::RateTranspose:
        return QStringLiteral("rate_transpose");
    case SampleSpeedMode::None:
    default:
        return QStringLiteral("none");
    }
}

inline SampleSpeedMode backgroundTrackSpeedMode()
{
    const QString value = miacode::debug_options::envValue("MIACODE_BASS_BGM_RATE_MODE").trimmed().toLower();
    if (value == QStringLiteral("rate_transpose")
            || value == QStringLiteral("transpose")
            || value == QStringLiteral("source_time")
            || value == QStringLiteral("accurate")) {
        return SampleSpeedMode::RateTranspose;
    }
    return SampleSpeedMode::Tempo;
}

struct BassTempoWindowConfig {
    QString label = QStringLiteral("stock");
    QString rawPreset;
    QString rawParams;
    QString error;
    float sequenceMs = 0.0f;
    float seekWindowMs = 0.0f;
    float overlapMs = 0.0f;
    bool apply = false;
    bool custom = false;
    bool valid = true;
};

inline QString normalizedBassTempoToken(QString value)
{
    value = value.trimmed().toLower();
    value.replace(QLatin1Char('-'), QLatin1Char('_'));
    return value;
}

inline bool parseBassTempoWindowTriple(const QString& raw, float* sequenceMs, float* seekWindowMs, float* overlapMs)
{
    QString normalized = raw.trimmed();
    normalized.replace(QLatin1Char(';'), QLatin1Char(','));
    normalized.replace(QLatin1Char('/'), QLatin1Char(','));
    normalized.replace(QLatin1Char('|'), QLatin1Char(','));
    normalized.replace(QLatin1Char('x'), QLatin1Char(','));
    normalized.replace(QLatin1Char('X'), QLatin1Char(','));
    normalized.replace(QLatin1Char(' '), QLatin1Char(','));
    const QStringList parts = normalized.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() != 3) {
        return false;
    }

    bool okSequence = false;
    bool okSeek = false;
    bool okOverlap = false;
    const float parsedSequence = parts.at(0).toFloat(&okSequence);
    const float parsedSeek = parts.at(1).toFloat(&okSeek);
    const float parsedOverlap = parts.at(2).toFloat(&okOverlap);
    if (!okSequence || !okSeek || !okOverlap) {
        return false;
    }
    if (!qIsFinite(parsedSequence) || !qIsFinite(parsedSeek) || !qIsFinite(parsedOverlap)) {
        return false;
    }
    if (parsedSequence < 0.0f || parsedSeek < 0.0f || parsedOverlap < 0.0f) {
        return false;
    }
    *sequenceMs = parsedSequence;
    *seekWindowMs = parsedSeek;
    *overlapMs = parsedOverlap;
    return true;
}

inline BassTempoWindowConfig makeBassTempoWindowConfig(
    const QString& label,
    float sequenceMs,
    float seekWindowMs,
    float overlapMs)
{
    BassTempoWindowConfig config;
    config.label = label;
    config.sequenceMs = sequenceMs;
    config.seekWindowMs = seekWindowMs;
    config.overlapMs = overlapMs;
    config.apply = true;
    return config;
}

inline BassTempoWindowConfig backgroundTrackTempoWindowConfig()
{
    const QString rawParams = miacode::debug_options::envValue("MIACODE_BASS_BGM_TEMPO_PARAMS").trimmed();
    const QString rawPreset = miacode::debug_options::envValue("MIACODE_BASS_BGM_TEMPO_PRESET").trimmed();
    if (!rawParams.isEmpty()) {
        BassTempoWindowConfig config;
        config.label = QStringLiteral("custom");
        config.rawParams = rawParams;
        config.rawPreset = rawPreset;
        config.apply = true;
        config.custom = true;
        if (!parseBassTempoWindowTriple(rawParams, &config.sequenceMs, &config.seekWindowMs, &config.overlapMs)) {
            config.apply = false;
            config.valid = false;
            config.error = QStringLiteral("invalid_params_expected_sequence_seek_overlap_ms");
        }
        return config;
    }

    const QString preset = normalizedBassTempoToken(rawPreset);
    if (preset.isEmpty()) {
        BassTempoWindowConfig config = makeBassTempoWindowConfig(QStringLiteral("compact40"), 40.0f, 15.0f, 8.0f);
        config.rawPreset = rawPreset;
        return config;
    }
    if (preset == QStringLiteral("stock")
            || preset == QStringLiteral("default")
            || preset == QStringLiteral("bass_default")
            || preset == QStringLiteral("none")) {
        BassTempoWindowConfig config;
        config.rawPreset = rawPreset;
        return config;
    }
    if (preset == QStringLiteral("auto")) {
        BassTempoWindowConfig config = makeBassTempoWindowConfig(QStringLiteral("auto"), 0.0f, 0.0f, 8.0f);
        config.rawPreset = rawPreset;
        return config;
    }
    if (preset == QStringLiteral("tight") || preset == QStringLiteral("tight20")) {
        BassTempoWindowConfig config = makeBassTempoWindowConfig(QStringLiteral("tight20"), 20.0f, 8.0f, 4.0f);
        config.rawPreset = rawPreset;
        return config;
    }
    if (preset == QStringLiteral("balanced") || preset == QStringLiteral("balanced30")) {
        BassTempoWindowConfig config = makeBassTempoWindowConfig(QStringLiteral("balanced30"), 30.0f, 10.0f, 6.0f);
        config.rawPreset = rawPreset;
        return config;
    }
    if (preset == QStringLiteral("compact") || preset == QStringLiteral("compact40")) {
        BassTempoWindowConfig config = makeBassTempoWindowConfig(QStringLiteral("compact40"), 40.0f, 15.0f, 8.0f);
        config.rawPreset = rawPreset;
        return config;
    }
    if (preset == QStringLiteral("smooth") || preset == QStringLiteral("smooth60")) {
        BassTempoWindowConfig config = makeBassTempoWindowConfig(QStringLiteral("smooth60"), 60.0f, 20.0f, 8.0f);
        config.rawPreset = rawPreset;
        return config;
    }
    if (preset == QStringLiteral("wide") || preset == QStringLiteral("wide82")) {
        BassTempoWindowConfig config = makeBassTempoWindowConfig(QStringLiteral("wide82"), 82.0f, 28.0f, 8.0f);
        config.rawPreset = rawPreset;
        return config;
    }

    BassTempoWindowConfig config;
    config.rawPreset = rawPreset;
    config.valid = false;
    config.error = QStringLiteral("unknown_preset");
    return config;
}

inline bool runtimeAudioDebugEnabled()
{
    return miacode::debug_options::audioDebugOutputEnabled();
}

inline void appendAudioDebugLog(const QString& message)
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
inline void noteBassErr(const char* ctx)
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

inline QString runtimeFilePath(const QString& fileName)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(fileName);
}

inline bool runtimeLibraryExists(const QString& fileName)
{
    return QFileInfo::exists(runtimeFilePath(fileName));
}

inline double clampTimelineSecond(double second)
{
    return qIsFinite(second) ? qMax(0.0, second) : 0.0;
}

inline float clampSampleVolume(double value)
{
    if (!qIsFinite(value)) {
        return 1.0f;
    }
    return static_cast<float>(qBound(0.0, value, 2.0));
}

inline QString retainedPlaybackModeLabel(miacode::preview_audio::RetainedPlaybackMode mode)
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

inline QString retainedSeekActionLabel(miacode::preview_audio::bass::RetainedSeekAction action)
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

inline QString bassDebugOperationLabel(miacode::preview_audio::bass::BassDebugOperation operation)
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
extern int gBassDeviceRefCount;
#endif

}  // namespace bass_detail
}  // namespace audio
}  // namespace miacode
