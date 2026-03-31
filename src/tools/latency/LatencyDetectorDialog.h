#pragma once

#include <QDialog>
#include <QPair>
#include <QString>
#include <QVector>

#include "PreviewAudioSettings.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QSlider;
class QTimer;
class QToolButton;
class QtPreviewSfxRuntime;
class QWidget;

class LatencyDetectorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LatencyDetectorDialog(
        const QString& trackPath,
        const QString& chartPath,
        const PreviewAudioSettings& audioSettings,
        QWidget* parent = nullptr
    );
    ~LatencyDetectorDialog() override;

    QString trackPath() const;
    void setBpm(double bpm);
    void setMeterId(const QString& meterId);
    void setOffsetSeconds(double seconds);

signals:
    void bpmChanged(double bpm);
    void meterIdChanged(const QString& meterId);
    void offsetChanged(double offsetSeconds);

private:
    void buildUi();
    void loadAudioAnalysis();
    void updatePlaybackUi();
    void updateBeatOverlay();
    void smoothFollowPlayhead(bool forceCenter);
    void applyZoomLevel(bool centerOnPlayhead);
    void updateVisibleRange(bool centerOnPlayhead = false);
    void updateBpmEdit(double bpm, bool notify);
    void updateOffsetEdit(double seconds, bool notify);
    bool applyOffsetValue(double seconds, bool notify, bool updateText);
    void previewOffsetEdit();
    void applyOffsetEdit();
    void applyBpmEdit();
    void restoreDetectedOffset();
    void showBpmHelpDialog();
    void seekToSecond(double second, bool centerView);
    void togglePlayback();
    void startPlayback();
    void pausePlayback();
    void restartPlaybackAfterOffsetChange();
    void onPlaybackTick();
    void triggerBeatAudition(double fromSecond, double toSecond);
    QString selectedOffsetSnapModeId() const;
    double parsedBpm(bool* ok = nullptr) const;
    double parsedOffset(bool* ok = nullptr) const;
    double detectBpm();
    double detectOffset(double bpm) const;
    QString formatTimestamp(double second) const;
    QString localizedText(const QString& zh, const QString& en) const;

    QString trackPath_;
    QString chartPath_;
    PreviewAudioSettings audioSettings_;
    QWidget* waveformView_ = nullptr;
    QLineEdit* bpmEdit_ = nullptr;
    QPushButton* detectBpmButton_ = nullptr;
    QPushButton* bpmHelpButton_ = nullptr;
    QLineEdit* offsetEdit_ = nullptr;
    QPushButton* detectOffsetButton_ = nullptr;
    QPushButton* restoreDetectedOffsetButton_ = nullptr;
    QPushButton* playPauseButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QSlider* playbackSlider_ = nullptr;
    QSlider* sfxVolumeSlider_ = nullptr;
    QLabel* sfxVolumeValueLabel_ = nullptr;
    QToolButton* speedButton_ = nullptr;
    QToolButton* zoomOutButton_ = nullptr;
    QToolButton* zoomInButton_ = nullptr;
    QLabel* playbackTimeLabel_ = nullptr;
    QComboBox* meterCombo_ = nullptr;
    QComboBox* offsetSnapCombo_ = nullptr;
    QtPreviewSfxRuntime* sfxRuntime_ = nullptr;
    QTimer* playbackTimer_ = nullptr;
    QTimer* beatAuditionTimer_ = nullptr;
    QTimer* offsetReplayTimer_ = nullptr;
    QVector<float> waveformPeaks_;
    QVector<float> onsetEnvelope_;
    QVector<float> offsetEnvelope_;
    QVector<float> decodedSamples_;
    double trackDurationSeconds_ = 0.0;
    double onsetStepSeconds_ = 0.0;
    double offsetStepSeconds_ = 0.0;
    double playheadSecond_ = 0.0;
    double playbackRate_ = 1.0;
    double visibleStartSecond_ = 0.0;
    double visibleDurationSeconds_ = 12.0;
    double baseVisibleDurationSeconds_ = 12.0;
    int zoomLevel_ = 0;
    double pendingBeatBpm_ = 0.0;
    double pendingBeatOffset_ = 0.0;
    int pendingBeatBarPulseCount_ = 4;
    double pendingBeatAuditionPeriodSeconds_ = 0.0;
    bool pendingBeatForceUniformGain_ = false;
    QVector<double> pendingBeatAccentWeights_{1.0};
    bool pendingBeatUseUniformAccent_ = true;
    int pendingBeatAccentAnchorIndex_ = 0;
    double detectedMeterPhaseSecond_ = 0.0;
    bool detectedMeterPhaseValid_ = false;
    QString lastDetectedMeterId_ = "4/4";
    double detectedOffsetRestoreSeconds_ = 0.0;
    bool hasDetectedOffsetRestore_ = false;
    double beatSfxVolume_ = 1.0;
    bool playbackSliderDragging_ = false;
    double lastDetectedBpm_ = 0.0;
    QVector<QPair<double, double>> lastDetectedBpmCandidates_;
    double lastBeatAuditionSecond_ = -1.0;
    double lastAppliedOffsetSeconds_ = 0.0;
    bool hasLastAppliedOffset_ = false;
    bool playing_ = false;
};
