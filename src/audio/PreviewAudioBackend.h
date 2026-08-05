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
    // Chart-second read from the AUDIO device's own clock — where the BGM stream
    // has actually been consumed to — as opposed to the wall clock that drives
    // chart-second everywhere else.
    //
    // SFX scheduling uses this so SFX and BGM sit in ONE clock domain. They are
    // already mixed and output together; only the decision of WHEN to fire an SFX
    // was made against a separate clock, and the two crystals drift. The visual
    // chart-second deliberately stays on the wall clock (a BASS-cursor-driven
    // visual clock was the "smear" that G1 Commit 4 removed), so this narrows the
    // audio seam without reopening the visual one.
    //
    // Returns false when there is no usable audio clock: no BGM loaded, BGM not
    // running yet (a positive `&first` delays its start), or a backend that has
    // none. Callers keep their wall-clock second in that case — this refines the
    // trigger instant, it is never the only source.
    //
    // Defaults to "no audio clock" so the miniaudio backend (Linux) keeps the
    // wall-clock behaviour unchanged; only the BASS backend overrides it.
    virtual bool audioClockChartSecond(double* outSecond) const
    {
        (void) outSecond;
        return false;
    }
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
