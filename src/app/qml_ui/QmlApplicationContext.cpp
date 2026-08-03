#include "QmlApplicationContext.h"

#include "app/mainwindow/MainWindow.h"
#include "app/quick_shell/QuickShellController.h"

QmlApplicationContext::QmlApplicationContext(
    MainWindow& backend,
    QuickShellController& shell,
    QObject* parent)
    : QObject(parent)
    , preferences_(this)
    , document_(backend, this)
    , preview_(shell, this)
    , commands_(document_, this)
    , shell_(&shell)
{
}

QObject* QmlApplicationContext::document() { return &document_; }
QObject* QmlApplicationContext::preferences() { return &preferences_; }
QObject* QmlApplicationContext::preview() { return &preview_; }
QObject* QmlApplicationContext::commands() { return &commands_; }
QObject* QmlApplicationContext::shell() { return shell_; }
