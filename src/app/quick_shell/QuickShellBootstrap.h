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

    QIcon appIcon_;
    std::unique_ptr<MainWindow> backend_;
    std::unique_ptr<QuickShellNativeSurfaceHost> surfaceHost_;
    std::unique_ptr<QuickShellController> controller_;
    std::unique_ptr<QuickShellStyleBridge> styleBridge_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    bool previewSeekArmed_ = false;
};
