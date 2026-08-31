#include "app/v2/JobProgressService.h"
#include "app/v2/UiRequestService.h"
#include "QmlApplicationContext.h"

#include "mainwindow/MainWindow.h"
#include "mainwindow/MainWindowShared.h"

QmlApplicationContext::QmlApplicationContext(MainWindow& backend, QObject* parent)
    : QObject(parent)
    , backend_(backend)
    , workspace_()
    , fileService_(workspace_)
    , analysisService_(workspace_, miacode::mainwindow::shared::uiValidationLocale())
    , preferences_(this)
    , document_(backend, workspace_, fileService_, analysisService_, this)
    , analysis_(backend, workspace_, analysisService_, this)
    , preview_(backend, this)
    , timeline_(backend, this)
    , commands_(backend, document_, this)
    , pages_(backend, this)
    , coverExport_(*backend.qmlExportSession(), *backend.uiRequestService(), this)
    , editor_(this)
    , shortcuts_(this)
    , platform_(this)
    , mediaTools_(backend, this)
    , preferencesModel_(backend, this)
    , audioSettings_(backend, this)
    , previewSettings_(backend, this)
    , latency_(backend, this)
    , lifecycle_(backend, this)
{
    // Keep the QML text controller in lockstep with the persisted settings.
    // Settings are the v2 boundary; MainWindow only owns their durable values.
    const auto syncEditorAppearance = [this]() {
        preferences_.setEditorAppearance(backend_.currentEditorTextFontSize(),
                                         backend_.currentEditorLineSpacingFactor());
    };
    syncEditorAppearance();
    connect(&preferencesModel_, &miacode::qml_ui::QmlPreferencesModel::editorChanged,
            this, syncEditorAppearance);

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
    connect(&pages_, &QmlEditorPageHost::coverPageRequested,
            &coverExport_, &QmlCoverExportSession::enter);
    connect(&pages_, &QmlEditorPageHost::activePageIdChanged, this, [this] {
        if (pages_.activePageId() != QLatin1String("cover")) {
            coverExport_.leave();
        }
    });
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
QObject* QmlApplicationContext::shell() { return &lifecycle_; }
QObject* QmlApplicationContext::timeline() { return &timeline_; }
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
QObject* QmlApplicationContext::audioSettings() { return &audioSettings_; }
QObject* QmlApplicationContext::previewSettings() { return &previewSettings_; }

QObject* QmlApplicationContext::latency() { return &latency_; }
QObject* QmlApplicationContext::coverExport() { return &coverExport_; }

void QmlApplicationContext::setWindowChrome(QObject* chrome)
{
    windowChrome_ = chrome;
}
