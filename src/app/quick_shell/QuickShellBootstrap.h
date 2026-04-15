#pragma once

#include <memory>

#include <QObject>
#include <QIcon>
#include <QElapsedTimer>

class QQmlApplicationEngine;
class QTimer;

class MainWindow;
class QuickShellController;
class QuickShellNativeSurfaceHost;
class QuickShellStyleBridge;

class QuickShellBootstrap : public QObject
{
    Q_OBJECT

public:
    explicit QuickShellBootstrap(const QIcon& appIcon, QObject* parent = nullptr);
    ~QuickShellBootstrap() override;

    bool start();
    QuickShellController* controller() const;
    QuickShellStyleBridge* styleBridge() const;

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool previewSeekHotRectContainsGlobalPoint(const QPoint& globalPoint) const;
    bool shouldTraceFocusObject(QObject* watched) const;
    QString describeFocusObject(QObject* object) const;
    QString focusReasonName(Qt::FocusReason reason) const;
    void logFocusEvent(const QString& action, QObject* watched = nullptr, QEvent* event = nullptr, const QString& detail = QString()) const;

    QIcon appIcon_;
    std::unique_ptr<MainWindow> backend_;
    std::unique_ptr<QuickShellNativeSurfaceHost> surfaceHost_;
    std::unique_ptr<QuickShellController> controller_;
    std::unique_ptr<QuickShellStyleBridge> styleBridge_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    bool previewSeekArmed_ = false;
};
