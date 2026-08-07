#pragma once

#include <QObject>
#include <QVariantList>

class QuickShellController;

// Real-time preview state exposed to the pure-QML UI. Playback and
// rendering remain owned by MiaCode's preview runtime.
class QmlPreviewModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double positionSeconds READ positionSeconds WRITE setPositionSeconds NOTIFY changed)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY changed)
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY changed)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY changed)
    Q_PROPERTY(bool muriMode READ muriMode WRITE setMuriMode NOTIFY changed)
    Q_PROPERTY(QVariantList statistics READ statistics NOTIFY changed)
    Q_PROPERTY(QObject* runtime READ runtime CONSTANT)
    Q_PROPERTY(QObject* mediaHost READ mediaHost CONSTANT)

public:
    explicit QmlPreviewModel(QuickShellController& controller, QObject* parent = nullptr);

    double positionSeconds() const;
    double durationSeconds() const;
    double rate() const;
    bool playing() const;
    bool muriMode() const;
    QVariantList statistics() const;
    QObject* runtime() const;
    QObject* mediaHost() const;

    void setPositionSeconds(double value);
    void setRate(double value);
    void setPlaying(bool value);
    void setMuriMode(bool value);
    Q_INVOKABLE void stop();

signals:
    void changed();

private:
    QuickShellController* controller_ = nullptr;
};
