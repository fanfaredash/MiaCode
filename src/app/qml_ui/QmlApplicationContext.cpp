#include "QmlApplicationContext.h"

#include "mainwindow/MainWindow.h"

QmlApplicationContext::QmlApplicationContext(MainWindow& backend,
                                             miacode::v2::ApplicationServices& services,
                                             QObject* parent)
    : QObject(parent)
    , backend_(backend)
    , services_(services)
    , preferences_(this)
    , appBackground_(&services.uiRequests(), {}, {}, this)
    , document_(backend, services.workspace(), services.files(), services.analysis(), this)
    , analysis_(backend, services.workspace(), services.analysis(), this)
    , preview_(backend, this)
    , timeline_(backend, this)
    , commands_(backend, document_, this)
    , pages_(backend, this)
    , coverExport_(*backend.qmlExportSession(), services.uiRequests(), this)
    , editor_(this)
    , shortcuts_(this)
    , platform_(this)
    , mediaTools_(backend, this)
    , preferencesModel_(backend, preferences_, this)
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
QObject* QmlApplicationContext::appBackground() { return &appBackground_; }
QObject* QmlApplicationContext::preview() { return &preview_; }
QObject* QmlApplicationContext::commands() { return &commands_; }
QObject* QmlApplicationContext::shell() { return &lifecycle_; }
QObject* QmlApplicationContext::timeline() { return &timeline_; }
QObject* QmlApplicationContext::pages() { return &pages_; }
QObject* QmlApplicationContext::editor() { return &editor_; }
QObject* QmlApplicationContext::editorSync() { return &services_.editorSync(); }
QObject* QmlApplicationContext::shortcuts() { return &shortcuts_; }
QObject* QmlApplicationContext::windowChrome() const { return windowChrome_; }
QObject* QmlApplicationContext::chartDropBridge() const { return chartDropBridge_; }
QObject* QmlApplicationContext::platform() { return &platform_; }

// Owned by the application service assembly, which is built before the window,
// so the export session and the QML shell share one boundary; a second instance
// would mean a second dialog host and duplicated pickers.
QObject* QmlApplicationContext::uiRequests() { return &services_.uiRequests(); }

QObject* QmlApplicationContext::jobProgress() { return &services_.jobProgress(); }

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

void QmlApplicationContext::setChartDropBridge(QObject* bridge)
{
    if (chartDropBridge_ == bridge) {
        return;
    }
    chartDropBridge_ = bridge;
    emit chartDropBridgeChanged();
}
