#pragma once

#include <memory>

#include <QObject>
#include <QIcon>

class QQmlApplicationEngine;

class MainWindow;
class QuickShellController;
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

    QIcon appIcon_;
    std::unique_ptr<MainWindow> backend_;
    std::unique_ptr<QuickShellController> controller_;
    std::unique_ptr<QuickShellStyleBridge> styleBridge_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
};
