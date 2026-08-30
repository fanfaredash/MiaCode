#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include "tools/latency/LatencyAnalysis.h"

class MainWindow;

namespace miacode::latency {
class LatencySandboxController;
}

namespace miacode::qml_ui {

// QML-facing surface for 延迟校准. LatencySandboxController already owns the
// audition lifecycle and the test-chart synthesis without a widget in sight, so
// this model only carries the values, the detection results, and the decoded
// audio cache the Widgets page used to hold.
class QmlLatencyModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY valuesChanged)
    Q_PROPERTY(double offsetSeconds READ offsetSeconds WRITE setOffsetSeconds NOTIFY valuesChanged)
    Q_PROPERTY(int clockCount READ clockCount WRITE setClockCount NOTIFY valuesChanged)
    Q_PROPERTY(int subdivision READ subdivision WRITE setSubdivision NOTIFY valuesChanged)
    Q_PROPERTY(int sfxVolumePercent READ sfxVolumePercent WRITE setSfxVolumePercent NOTIFY valuesChanged)
    Q_PROPERTY(bool auditionRunning READ auditionRunning NOTIFY auditionChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY playheadChanged)
    Q_PROPERTY(bool trackAvailable READ trackAvailable NOTIFY valuesChanged)
    Q_PROPERTY(QString bpmDetectResult READ bpmDetectResult NOTIFY detectionChanged)
    Q_PROPERTY(QString offsetDetectResult READ offsetDetectResult NOTIFY detectionChanged)
    Q_PROPERTY(QVariantList audioDecoderOptions READ audioDecoderOptions CONSTANT)
    Q_PROPERTY(QString audioDecoder READ audioDecoder WRITE setAudioDecoder NOTIFY valuesChanged)

public:
    explicit QmlLatencyModel(MainWindow& backend, QObject* parent = nullptr);

    double bpm() const { return bpm_; }
    void setBpm(double value);
    double offsetSeconds() const { return offsetSeconds_; }
    void setOffsetSeconds(double value);
    int clockCount() const { return clockCount_; }
    void setClockCount(int value);
    int subdivision() const;
    void setSubdivision(int value);
    int sfxVolumePercent() const;
    void setSfxVolumePercent(int value);

    bool auditionRunning() const;
    QString positionText() const;
    bool trackAvailable() const;
    QString bpmDetectResult() const { return bpmDetectResult_; }
    QString offsetDetectResult() const { return offsetDetectResult_; }
    QVariantList audioDecoderOptions() const;
    QString audioDecoder() const { return audioDecoder_; }
    void setAudioDecoder(const QString& token);

    // Page lifecycle: installs / restores the sandbox preview scene.
    Q_INVOKABLE void enter();
    Q_INVOKABLE void leave();
    Q_INVOKABLE void refreshFromDocument();
    Q_INVOKABLE void toggleAudition();
    Q_INVOKABLE void detectBpm();
    Q_INVOKABLE void detectOffset();

signals:
    void valuesChanged();
    void auditionChanged();
    void playheadChanged();
    void detectionChanged();

private:
    bool ensureAudioEnvelopeReady();
    void clearAudioEnvelopeCache();
    miacode::audio_decode::BackendPreference decodeBackend() const;
    miacode::latency::LatencySandboxController* sandbox() const;

    MainWindow* backend_ = nullptr;
    double bpm_ = 120.0;
    double offsetSeconds_ = 0.0;
    int clockCount_ = 4;
    double playheadSeconds_ = 0.0;
    QString audioDecoder_ = QStringLiteral("miniaudio");
    QString bpmDetectResult_;
    QString offsetDetectResult_;

    QString cachedAudioPath_;
    miacode::latency_analysis::Envelope cachedOnsetEnvelope_;
    miacode::latency_analysis::Envelope cachedTransientEnvelope_;
    double cachedAudioDurationSeconds_ = 0.0;

    // Carried from BPM detection into offset detection: it measurably improves
    // accuracy on non-4/4 songs, and there is no user-facing meter selector.
    QString lastDetectedMeterId_ = QStringLiteral("4/4");
    double lastDetectedMeterPhase_ = 0.0;
    bool hasLastDetectedMeterPhase_ = false;
};

}  // namespace miacode::qml_ui
