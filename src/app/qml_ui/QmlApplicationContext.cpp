#include "QmlApplicationContext.h"

QmlApplicationContext::QmlApplicationContext(miacode::v2::ApplicationServices& services,
                                             QObject* parent)
    : QObject(parent)
    , services_(services)
    , preferences_(this)
    , appBackground_(&services.uiRequests(), {}, {}, this)
    , document_(services.shellNotifications(), services.workspace(), services.files(), services.analysis(),
                services.uiRequests(), services.documentBridgeSlot(), this)
    , analysis_(services.workspace(), services.analysis(),
                services.timelineSurfaceSlot(), this)
    , preview_(services.shellNotifications(), services.previewSurfaceSlot(), services.playbackControlSlot(), this)
    , timeline_(services.shellNotifications(), services.timelineSurfaceSlot(), this)
    , commands_(document_, services.editorPageRouterSlot(),
                services.documentBridgeSlot(), this)
    , pages_(services.shellNotifications(), services.editorPageRouterSlot(), services.exportPageSessionSlot(), this)
    , editor_(this)
    , shortcuts_(this)
    , platform_(this)
    , mediaTools_(services.uiRequests(), services.jobProgress(),
                  services.mediaToolsEngineSlot(), this)
    , preferencesModel_(services.preferencesStoreSlot(), preferences_, this)
    , audioSettings_(services.previewSurfaceSlot(), this)
    , previewSettings_(services.uiRequests(), services.previewAppearance(),
                       services.previewSurfaceSlot(), this)
    , latency_(services.latencyEngineSlot(), this)
    , lifecycle_(services.editorPageRouterSlot(), this)
{
    // Keep the QML text controller in lockstep with the persisted settings.
    // Settings are the v2 boundary; MainWindow only owns their durable values.
    const auto syncEditorAppearance = [this]() {
        miacode::v2::PreferencesStore* const store = services_.preferencesStore();
        if (store == nullptr) {
            return;
        }
        preferences_.setEditorAppearance(store->editorTextFontSize(),
                                         store->editorLineSpacingFactor());
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
    connect(&services.shellNotifications(), &miacode::v2::ShellNotifications::editorPreferencesChanged,
            &preferences_, &QmlUiSettings::reloadEditorSettings);
    connect(&services.shellNotifications(), &miacode::v2::ShellNotifications::muriPromptPreferenceChanged,
            &analysis_, &QmlAnalysisModel::refreshPreferences);
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
