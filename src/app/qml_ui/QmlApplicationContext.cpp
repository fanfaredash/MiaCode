#include "QmlApplicationContext.h"

#include "mainwindow/MainWindow.h"
#include "mainwindow/MainWindowShared.h"
#include "QuickShellController.h"

QmlApplicationContext::QmlApplicationContext(
    MainWindow& backend,
    QuickShellController& shell,
    QObject* parent)
    : QObject(parent)
    , workspace_()
    , fileService_(workspace_)
    , analysisService_(workspace_, miacode::mainwindow::shared::uiValidationLocale())
    , preferences_(this)
    , document_(backend, workspace_, fileService_, analysisService_, this)
    , analysis_(backend, workspace_, analysisService_, this)
    , preview_(backend, shell, this)
    , commands_(backend, document_, this)
    , pages_(backend, this)
    , editor_(this)
    , shortcuts_(this)
    , platform_(this)
    , shell_(&shell)
{
    editor_.setHalfWidthInputEnabled(preferences_.editorHalfWidthInputEnabled());
    editor_.setOverwriteMode(preferences_.editorOverwriteModeEnabled());
    editor_.setAutoCompletionEnabled(preferences_.editorAutoCompletionEnabled());
}

QObject* QmlApplicationContext::document() { return &document_; }
QObject* QmlApplicationContext::analysis() { return &analysis_; }
QObject* QmlApplicationContext::preferences() { return &preferences_; }
QObject* QmlApplicationContext::preview() { return &preview_; }
QObject* QmlApplicationContext::commands() { return &commands_; }
QObject* QmlApplicationContext::shell() { return shell_; }
QObject* QmlApplicationContext::pages() { return &pages_; }
QObject* QmlApplicationContext::editor() { return &editor_; }
QObject* QmlApplicationContext::shortcuts() { return &shortcuts_; }
QObject* QmlApplicationContext::windowChrome() const { return windowChrome_; }
QObject* QmlApplicationContext::platform() { return &platform_; }

void QmlApplicationContext::setWindowChrome(QObject* chrome)
{
    windowChrome_ = chrome;
}
