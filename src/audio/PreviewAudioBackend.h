#pragma once

#include <QString>
#include <QVector>

#include "common/PreviewSfxTimeline.h"
#include "common/PreviewTimingSettings.h"
#include "PreviewAudioSettings.h"

namespace miacode::preview_audio {

enum class RetainedPlaybackMode {
    None,
    PausedExact,
    PausedAnchored,
    Invalidated,
};

enum class RetainedBgmState {
    NoneLoaded,
    LoadedUsable,
    MissingOnDiskIgnored,
};

struct PausePreviewResult {
    bool usedBackgroundTrack = false;
    double pauseSecond = 0.0;
    RetainedPlaybackMode retainedMode = RetainedPlaybackMode::None;
    RetainedBgmState retainedBgmState = RetainedBgmState::NoneLoaded;
};

class PreviewAudioBackend
{
public:
    virtual ~PreviewAudioBackend() = default;

    virtual QString backendId() const = 0;
    virtual bool canBePrimary(QString* reason = nullptr) const = 0;
    virtual int nativeErrorCode() const noexcept { return 0; }
    virtual void clearNativeErrorCode() noexcept {}

    virtual void setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir) = 0;
    virtual void reloadAssets(const PreviewAudioSettings& settings) = 0;
    virtual bool audioEngineInitialized() const = 0;
    virtual void setChartPath(const QString& chartPath) = 0;
    virtual void setBackgroundTrackOffsetSeconds(double seconds) = 0;
    virtual void setBackgroundTrackPlaybackRate(double rate) = 0;
    // G2 Commit 2: live rate change during playback. Implements the
    // pause-modify-resume sequence per
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §6.2 so the BGM tempo
    // source is paused before its rate attribute is written and the cursor is
    // re-anchored to the wall-clock chart-second the caller supplies.
    // The base default is a delegate to setBackgroundTrackPlaybackRate —
    // legacy / non-BASS backends keep the G1 "rate-change only when
    // already paused" behavior unless they override this slot.
    virtual void applyPlaybackRateAtChartSecond(double rate, double chartSecond)
    {
        Q_UNUSED(chartSecond);
        setBackgroundTrackPlaybackRate(rate);
    }
    virtual void applyLevels(const PreviewAudioSettings& settings) = 0;
    virtual void configureTimeline(
        const QVector<TimelineNoteMarker>& noteMarkers,
        double playbackRate,
        const PreviewTimingSettings& timingSettings) = 0;
    virtual void clearTimeline() = 0;
    virtual void setPlaybackTransactionId(quint64 transactionId) = 0;
    virtual double preparePreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate) = 0;
    virtual void commitPreparedPreviewPlayback() = 0;
    virtual void cancelPreparedPreviewPlayback() = 0;
    virtual double preparedStartSecond() const = 0;
    virtual void applyPausedPreviewState(
        const QVector<TimelineNoteMarker>& noteMarkers,
        bool noteMarkersChanged,
        double pauseSecond,
        double playbackRate,
        const PreviewTimingSettings& timingSettings) = 0;
    virtual double startPreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate) = 0;
    virtual PausePreviewResult capturePausedPreviewTransaction() = 0;
    virtual PausePreviewResult pausePreviewPlaybackTransaction() = 0;
    virtual double resumeRetainedPreviewPlaybackTransaction() = 0;
    virtual double seekRetainedPreviewPlaybackTransaction(double targetSecond, bool continuePlaying) = 0;
    virtual void resetRetainedPreviewPlaybackTransaction(double targetSecond) = 0;
    virtual void clearRetainedPreviewPlaybackTransaction() = 0;
    virtual RetainedPlaybackMode retainedPlaybackMode() const = 0;
    virtual RetainedBgmState retainedBgmState() const = 0;
    virtual double authoritativePlaybackSecond() const = 0;
    // Hard-stops the note-SFX one-shot voices, leaving BGM and the touch-hold voice
    // alone. Only the audio-device auto-pause calls this. A manual pause deliberately
    // lets one-shots ring out (see suspendPlaybackTransport), but on a device change
    // that tail outlives the route switch and is heard as a stray note AFTER the
    // preview has visibly stopped. Default no-op, so the miniaudio backend is unchanged.
    virtual void stopSfxVoices() {}
    virtual double syncPreviewPlaybackClockTransaction(double fallbackSecond) = 0;
    virtual void resetCursor(double second, bool includeCurrentSecond) = 0;
    virtual void drainEvents(double second) = 0;
    virtual void pauseTouchholdVoices() = 0;
    virtual void restoreTouchholdVoices(double second) = 0;
    virtual void syncBackgroundTrack(double timelineSecond) = 0;
    virtual bool hasBackgroundTrack() const = 0;
    virtual bool isBackgroundTrackRunning() const = 0;
    virtual void startBackgroundTrack(double second) = 0;
    virtual void seekBackgroundTrack(double second) = 0;
    virtual void pauseBackgroundTrack() = 0;
    virtual double backgroundPlaybackSecond() const = 0;
    virtual bool audition(const QString& kind, double gain = 1.0) = 0;
    virtual void stopAll() = 0;
    virtual void prepareForShutdown()
    {
        stopAll();
    }
};

}  // namespace miacode::preview_audio
