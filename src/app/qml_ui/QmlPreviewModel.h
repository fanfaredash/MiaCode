#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

class MainWindow;
class QuickShellController;

// Real-time preview state exposed to the pure-QML UI. Playback and
// rendering remain owned by MiaCode's preview runtime.
class QmlPreviewModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double positionSeconds READ positionSeconds WRITE setPositionSeconds NOTIFY positionChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY transportChanged)
    Q_PROPERTY(double lowerBoundSeconds READ lowerBoundSeconds NOTIFY transportChanged)
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY transportChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(QString renderMode READ renderMode NOTIFY renderModeChanged)
    Q_PROPERTY(QString renderModeLabel READ renderModeLabel NOTIFY renderModeChanged)
    Q_PROPERTY(QVariantList statistics READ statistics NOTIFY statisticsChanged)
    Q_PROPERTY(QObject* runtime READ runtime CONSTANT)
    Q_PROPERTY(QObject* mediaHost READ mediaHost CONSTANT)

public:
    QmlPreviewModel(MainWindow& backend, QuickShellController& controller, QObject* parent = nullptr);

    double positionSeconds() const;
    double durationSeconds() const;
    double lowerBoundSeconds() const;
    double rate() const;
    bool playing() const;
    QString renderMode() const;
    QString renderModeLabel() const;
    QVariantList statistics() const;
    QString currentSkinDirectory() const;
    QObject* runtime() const;
    QObject* mediaHost() const;

    void setPositionSeconds(double value);
    void setRate(double value);
    void setPlaying(bool value);
    Q_INVOKABLE void toggleRenderMode();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void beginScrub();
    Q_INVOKABLE void updateScrub(double second);
    Q_INVOKABLE void endScrub(double second);

signals:
    void positionChanged();
    void transportChanged();
    void playingChanged();
    void renderModeChanged();
    void statisticsChanged();

private:
    void refreshFromController(bool force = false);
    void rebuildStatistics();
    void refreshSkinDirectory();
    void updateV2UiProbePlaybackState();
    void resetV2UiProbe();
    void appendV2UiProbeSummary() const;

    MainWindow* backend_ = nullptr;
    QuickShellController* controller_ = nullptr;
    double positionSeconds_ = 0.0;
    double durationSeconds_ = 0.0;
    double lowerBoundSeconds_ = 0.0;
    double rate_ = 1.0;
    bool playing_ = false;
    QString renderMode_;
    QString renderModeLabel_;
    QStringList statisticsTexts_;
    QString skinDirectory_;
    QVariantList statistics_;
    bool v2UiProbeEnabled_ = false;
    bool v2UiProbePlaybackActive_ = false;
    mutable qint64 v2UiProbeStatisticsRebuildCount_ = 0;
    mutable qint64 v2UiProbeStatisticsBuildNs_ = 0;
    mutable qint64 v2UiProbeStatisticsBuildMaxNs_ = 0;
    mutable qint64 v2UiProbeSkinResolveNs_ = 0;
    mutable qint64 v2UiProbeSkinResolveMaxNs_ = 0;
    qint64 v2UiProbeShellStateChangeCount_ = 0;
};
