#pragma once

#include <QString>
#include <QVector>

#include "common/PreviewSfxTimeline.h"
#include "common/PreviewTimingSettings.h"
#include "PreviewAudioSettings.h"

namespace miacode::preview_audio {

struct PausePreviewResult {
    bool usedBackgroundTrack = false;
    double pauseSecond = 0.0;
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
};

}  // namespace miacode::preview_audio
