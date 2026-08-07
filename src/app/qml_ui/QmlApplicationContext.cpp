#include "QmlApplicationContext.h"

#include "mainwindow/MainWindow.h"
#include "QuickShellController.h"

QmlApplicationContext::QmlApplicationContext(
    MainWindow& backend,
    QuickShellController& shell,
    QObject* parent)
    : QObject(parent)
    , preferences_(this)
    , document_(backend, this)
    , preview_(shell, this)
    , commands_(backend, document_, this)
    , pages_(backend, this)
    , platform_(this)
    , shell_(&shell)
{
}

QObject* QmlApplicationContext::document() { return &document_; }
QObject* QmlApplicationContext::preferences() { return &preferences_; }
QObject* QmlApplicationContext::preview() { return &preview_; }
QObject* QmlApplicationContext::commands() { return &commands_; }
QObject* QmlApplicationContext::shell() { return shell_; }
QObject* QmlApplicationContext::pages() { return &pages_; }
QObject* QmlApplicationContext::windowChrome() const { return windowChrome_; }
QObject* QmlApplicationContext::platform() { return &platform_; }

void QmlApplicationContext::setWindowChrome(QObject* chrome)
{
    windowChrome_ = chrome;
}
