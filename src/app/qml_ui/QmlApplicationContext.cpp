#include "app/v2/JobProgressService.h"
#include "app/v2/UiRequestService.h"
#include "QmlApplicationContext.h"

#include "mainwindow/MainWindow.h"
#include "mainwindow/MainWindowShared.h"
#include "QuickShellController.h"

QmlApplicationContext::QmlApplicationContext(
    MainWindow& backend,
    QuickShellController& shell,
    QObject* parent)
    : QObject(parent)
    , backend_(backend)
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
    , mediaTools_(backend, this)
    , preferencesModel_(backend, this)
    , latency_(backend, this)
    , shell_(&shell)
{
    editor_.setHalfWidthInputEnabled(preferences_.editorHalfWidthInputEnabled());
    editor_.setOverwriteMode(preferences_.editorOverwriteModeEnabled());
    editor_.setAutoCompletionEnabled(preferences_.editorAutoCompletionEnabled());
    // A shortcut edited in the QML page must reach the live QActions, otherwise
    // it would only take effect after a restart.
    connect(&shortcuts_, &miacode::qml_ui::QmlShortcutModel::revisionChanged, this, [this]() {
        backend_.applyConfiguredShortcuts();
    });
}

QObject* QmlApplicationContext::document() { return &document_; }
QObject* QmlApplicationContext::analysis() { return &analysis_; }
QObject* QmlApplicationContext::preferences() { return &preferences_; }
QObject* QmlApplicationContext::preview() { return &preview_; }
QObject* QmlApplicationContext::commands() { return &commands_; }
QObject* QmlApplicationContext::shell() { return shell_; }
QObject* QmlApplicationContext::pages() { return &pages_; }
QObject* QmlApplicationContext::editor() { return &editor_; }
QObject* QmlApplicationContext::editorSync() { return &backend_.editorSyncController(); }
QObject* QmlApplicationContext::shortcuts() { return &shortcuts_; }
QObject* QmlApplicationContext::windowChrome() const { return windowChrome_; }
QObject* QmlApplicationContext::platform() { return &platform_; }

// Owned by MainWindow so the export session (built before this context) and the
// QML shell share one boundary; a second instance would mean a second dialog
// host and duplicated pickers.
QObject* QmlApplicationContext::uiRequests() { return backend_.uiRequestService(); }

QObject* QmlApplicationContext::jobProgress() { return backend_.jobProgressService(); }

QObject* QmlApplicationContext::mediaTools() { return &mediaTools_; }

QObject* QmlApplicationContext::preferencesModel() { return &preferencesModel_; }

QObject* QmlApplicationContext::latency() { return &latency_; }

void QmlApplicationContext::setWindowChrome(QObject* chrome)
{
    windowChrome_ = chrome;
}
