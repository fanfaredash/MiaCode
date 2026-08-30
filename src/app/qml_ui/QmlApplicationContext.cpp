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
    , shell_(&shell)
{
    const auto applyEditorSettings = [this] {
        editor_.setHalfWidthInputEnabled(preferences_.editorHalfWidthInputEnabled());
        editor_.setOverwriteMode(preferences_.editorOverwriteModeEnabled());
        editor_.setAutoCompletionEnabled(preferences_.editorAutoCompletionEnabled());
        editor_.setImeInputDisabled(preferences_.editorImeInputDisabled());
    };
    connect(&preferences_, &QmlUiSettings::editorSettingsChanged,
            this, applyEditorSettings);
    connect(&backend_, &MainWindow::editorPreferencesChanged,
            &preferences_, &QmlUiSettings::reloadEditorSettings);
    connect(&backend_, &MainWindow::muriPromptPreferenceChanged,
            &analysis_, &QmlAnalysisModel::refreshPreferences);
    connect(&document_, &QmlDocumentModel::metadataChanged,
            this, [this] { editor_.setWholeBpm(document_.wholeBpm()); });
    applyEditorSettings();
    editor_.setWholeBpm(document_.wholeBpm());
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

void QmlApplicationContext::setWindowChrome(QObject* chrome)
{
    windowChrome_ = chrome;
}
