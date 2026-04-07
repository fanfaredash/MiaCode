#pragma once

#include <QPointer>
#include <QVariantMap>

#include <QObject>

class QEvent;
class QTimer;

class MainWindow;

class QuickShellStyleBridge : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantMap palette READ palette NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY metricsChanged)

public:
    explicit QuickShellStyleBridge(MainWindow* backend, QObject* parent = nullptr);

    QVariantMap palette() const;
    QVariantMap metrics() const;

    Q_INVOKABLE void syncWindowSize(int width, int height);
    Q_INVOKABLE void refreshNow();

signals:
    void appearanceChanged();
    void metricsChanged();

private:
    void refreshFromBackend();

    QPointer<MainWindow> backend_;
    QTimer* refreshTimer_ = nullptr;
    QVariantMap palette_;
    QVariantMap metrics_;
};
