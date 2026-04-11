#pragma once

#include "QuickShellContracts.h"

#include <QPointer>
#include <QVariantMap>

#include <QObject>

class QEvent;
class QTimer;
class QuickShellNativeSurfaceHost;

class QuickShellStyleBridge : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantMap palette READ palette NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY metricsChanged)

public:
    QuickShellStyleBridge(
        QuickShellNativeContentProvider* contentProvider,
        QuickShellNativeSurfaceHost* surfaceHost,
        QObject* parent = nullptr
    );

    QVariantMap palette() const;
    QVariantMap metrics() const;

    Q_INVOKABLE void syncWindowSize(int width, int height);
    Q_INVOKABLE void refreshNow();

signals:
    void appearanceChanged();
    void metricsChanged();

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void scheduleRefresh();
    void refreshFromBackend();

    QuickShellNativeContentProvider* contentProvider_ = nullptr;
    QuickShellNativeSurfaceHost* surfaceHost_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    QVariantMap palette_;
    QVariantMap metrics_;
    bool refreshScheduled_ = false;
    bool refreshInProgress_ = false;
};
