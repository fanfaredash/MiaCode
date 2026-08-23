#pragma once

#include <QObject>
#include <QVariantList>

class MainWindow;
class QuickShellController;

// Real-time preview state exposed to the pure-QML UI. Playback and
// rendering remain owned by MiaCode's preview runtime.
class QmlPreviewModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double positionSeconds READ positionSeconds WRITE setPositionSeconds NOTIFY changed)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY changed)
    Q_PROPERTY(double lowerBoundSeconds READ lowerBoundSeconds NOTIFY changed)
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY changed)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY changed)
    Q_PROPERTY(QString renderMode READ renderMode NOTIFY changed)
    Q_PROPERTY(QString renderModeLabel READ renderModeLabel NOTIFY changed)
    Q_PROPERTY(QVariantList statistics READ statistics NOTIFY changed)
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
    void changed();

private:
    MainWindow* backend_ = nullptr;
    QuickShellController* controller_ = nullptr;
};
