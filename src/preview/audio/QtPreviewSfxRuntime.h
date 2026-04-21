#pragma once

#include <memory>

#include <QObject>

#include "PreviewAudioBackend.h"

class QtPreviewSfxRuntime : public QObject
{
    Q_OBJECT

public:
    using PausePreviewResult = miacode::preview_audio::PausePreviewResult;
    using RetainedPlaybackMode = miacode::preview_audio::RetainedPlaybackMode;
    using RetainedBgmState = miacode::preview_audio::RetainedBgmState;

    explicit QtPreviewSfxRuntime(QObject* parent = nullptr);
    ~QtPreviewSfxRuntime() override;

    void setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir);
    void reloadAssets(const PreviewAudioSettings& settings);
    bool audioEngineInitialized() const;
    void setChartPath(const QString& chartPath);
    void setBackgroundTrackOffsetSeconds(double seconds);
    void setBackgroundTrackPlaybackRate(double rate);
    void applyLevels(const PreviewAudioSettings& settings);
    void configureTimeline(
        const QVector<TimelineNoteMarker>& noteMarkers,
        double playbackRate,
        const PreviewTimingSettings& timingSettings);
    void clearTimeline();
    void setPlaybackTransactionId(quint64 transactionId);
    double preparePreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate);
    void commitPreparedPreviewPlayback();
    void cancelPreparedPreviewPlayback();
    double preparedStartSecond() const;
    void applyPausedPreviewState(
        const QVector<TimelineNoteMarker>& noteMarkers,
        bool noteMarkersChanged,
        double pauseSecond,
        double playbackRate,
        const PreviewTimingSettings& timingSettings);
    double startPreviewPlaybackTransaction(double startSecond, bool resumeFromPause, double playbackRate);
    PausePreviewResult capturePausedPreviewTransaction();
    PausePreviewResult pausePreviewPlaybackTransaction();
    double resumeRetainedPreviewPlaybackTransaction();
    double seekRetainedPreviewPlaybackTransaction(double targetSecond, bool continuePlaying);
    void resetRetainedPreviewPlaybackTransaction(double targetSecond);
    void clearRetainedPreviewPlaybackTransaction();
    RetainedPlaybackMode retainedPlaybackMode() const;
    RetainedBgmState retainedBgmState() const;
    double authoritativePlaybackSecond() const;
    double syncPreviewPlaybackClockTransaction(double fallbackSecond);
    void resetCursor(double second, bool includeCurrentSecond);
    void drainEvents(double second);
    void pauseTouchholdVoices();
    void restoreTouchholdVoices(double second);
    void syncBackgroundTrack(double timelineSecond);
    bool hasBackgroundTrack() const;
    bool isBackgroundTrackRunning() const;
    void startBackgroundTrack(double second);
    void seekBackgroundTrack(double second);
    void pauseBackgroundTrack();
    double backgroundPlaybackSecond() const;
    bool audition(const QString& kind, double gain = 1.0);
    void stopAll();

private:
    std::unique_ptr<miacode::preview_audio::PreviewAudioBackend> createBackend() const;

    std::unique_ptr<miacode::preview_audio::PreviewAudioBackend> backend_;
};
