#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "PreviewAudioSettings.h"
#include "TimelineView.h"

class QtPreviewSfxRuntime : public QObject
{
    Q_OBJECT

public:
    explicit QtPreviewSfxRuntime(QObject* parent = nullptr);
    ~QtPreviewSfxRuntime() override;

    void reloadAssets(const PreviewAudioSettings& settings);
    void setChartPath(const QString& chartPath);
    void applyLevels(const PreviewAudioSettings& settings);
    void configureTimeline(const QVector<TimelineNoteMarker>& noteMarkers);
    void clearTimeline();
    void resetCursor(double second, bool includeCurrentSecond);
    void drainEvents(double second);
    void syncTouchholdVoices(double second);
    bool hasBackgroundTrack() const;
    void startBackgroundTrack(double second);
    void pauseBackgroundTrack();
    double backgroundPlaybackSecond() const;
    bool audition(const QString& kind);
    void stopAll();

private:
    struct Event {
        double second = 0.0;
        int priority = 0;
        QString kind;
        int spanIndex = -1;
    };

    struct TouchholdSpan {
        double startSecond = 0.0;
        double endSecond = 0.0;
    };

    struct EngineState;
    struct Voice;

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
    void resetBanks();
    bool initializeAudioEngine();
    void initializeAssets();
    void initializeBackgroundTrack();
    void applyVolumes();
    bool playKindInternal(const QString& kind);
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
    SfxBank answerSfx_;
    SfxBank slideSfx_;
    SfxBank breakSfx_;
    SfxBank exSfx_;
    SfxBank touchSfx_;
    QVector<TouchholdVoice> touchholdVoices_;
    EngineState* engineState_ = nullptr;
};
