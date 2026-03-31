#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "common/PreviewSfxTimeline.h"
#include "PreviewAudioSettings.h"

class QtPreviewSfxRuntime : public QObject
{
    Q_OBJECT

public:
    explicit QtPreviewSfxRuntime(QObject* parent = nullptr);
    ~QtPreviewSfxRuntime() override;

    void reloadAssets(const PreviewAudioSettings& settings);
    void setChartPath(const QString& chartPath);
    void setBackgroundTrackOffsetSeconds(double seconds);
    void setBackgroundTrackPlaybackRate(double rate);
    void applyLevels(const PreviewAudioSettings& settings);
    void configureTimeline(const QVector<TimelineNoteMarker>& noteMarkers);
    void clearTimeline();
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
    using Event = miacode::preview_sfx_timeline::Event;
    using TouchholdSpan = miacode::preview_sfx_timeline::TouchholdSpan;

    struct EngineState;
    struct Voice;
    struct StretchedBackgroundState;

    struct SfxBank {
        QVector<Voice*> voices;
        int nextVoice = 0;
        bool configured = false;
    };

    struct TouchholdVoice {
        struct Voice* voice = nullptr;
        int activeSpanIndex = -1;
    };

    QString resolveTrackPath(const QString& chartPath) const;
    QString resolveSfxDir() const;
    void resetBackgroundTrack();
    void resetStretchedBackgroundTrack();
    void resetBanks();
    bool initializeAudioEngine();
    void initializeAssets();
    void initializeBackgroundTrack();
    bool prepareStretchedBackgroundTrack(double timelineSecond);
    double stretchedBackgroundPlaybackSecond() const;
    void applyVolumes();
    bool playKindInternal(const QString& kind, double gain = 1.0);
    void updateTouchholdVoiceVolumes();
    void startTouchholdSpan(int spanIndex, double offsetSeconds);
    void stopTouchholdSpan(int spanIndex);
    bool playTouchholdAudition();

    PreviewAudioSettings settings_;
    QVector<Event> events_;
    int eventIndex_ = 0;
    QString chartPath_;
    QString sfxDir_;
    QString trackPath_;
    QVector<TouchholdSpan> touchholdSpans_;
    quint64 touchholdSoundLengthFrames_ = 0;
    quint32 deviceSampleRate_ = 48000;
    bool engineInitialized_ = false;
    Voice* backgroundTrackVoice_ = nullptr;
    bool backgroundTrackConfigured_ = false;
    bool backgroundTrackRunning_ = false;
    bool backgroundTrackPendingStart_ = false;
    double backgroundTrackOffsetSeconds_ = 0.0;
    double backgroundTrackPlaybackRate_ = 1.0;
    double backgroundTrackLastTimelineSecond_ = 0.0;
    StretchedBackgroundState* stretchedBackgroundState_ = nullptr;
    SfxBank answerSfx_;
    SfxBank judgeSfx_;
    SfxBank judgeBreakSfx_;
    SfxBank slideSfx_;
    SfxBank breakSfx_;
    SfxBank breakSlideStartSfx_;
    SfxBank breakSlideSfx_;
    SfxBank judgeBreakSlideSfx_;
    SfxBank exSfx_;
    SfxBank touchSfx_;
    SfxBank fireworkSfx_;
    QVector<TouchholdVoice> touchholdVoices_;
    EngineState* engineState_ = nullptr;
};
