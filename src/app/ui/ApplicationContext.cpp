#include "ApplicationContext.h"


namespace miacode::ui {
ApplicationContext::ApplicationContext(miacode::ApplicationServices& services,
                                             QObject* parent)
    : QObject(parent)
    , services_(services)
    , preferences_(this)
    , appBackground_(&services.uiRequests(), {}, {}, this)
    , document_(services.shellNotifications(), services.workspace(), services.files(), services.analysis(),
                services.uiRequests(), services.documentBridgeSlot(),
                services.previewSurfaceSlot(), this)
    , analysis_(services.workspace(), services.analysis(), this)
    , preview_(services.shellNotifications(), services.previewSurfaceSlot(), services.playbackControlSlot(), this)
    , timeline_(services.shellNotifications(), services.timelineSurfaceSlot(), this)
    , commands_(document_, services.documentBridgeSlot(), this)
    , pages_(services.shellNotifications(), document_, services.editorPageRouterSlot(),
             services.exportPageSessionSlot(), this)
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
        miacode::PreferencesStore* const store = services_.preferencesStore();
        if (store == nullptr) {
            return;
        }
        preferences_.setEditorAppearance(store->editorTextFontSize(),
                                         store->editorLineSpacingFactor());
    };
    syncEditorAppearance();
    connect(&preferencesModel_, &miacode::ui::PreferencesModel::editorChanged,
            this, syncEditorAppearance);

    const auto applyEditorSettings = [this] {
        const int inputHandlingMode = preferencesModel_.editorInputHandlingMode();
        editor_.setHalfWidthInputEnabled(
            inputHandlingMode != miacode::ui::PreferencesModel::LeaveInputUnchanged);
        editor_.setOverwriteMode(preferences_.editorOverwriteModeEnabled());
        editor_.setAutoCompletionEnabled(preferences_.editorAutoCompletionEnabled());
        editor_.setImeInputDisabled(
            inputHandlingMode
            == miacode::ui::PreferencesModel::BlockInputMethodsAndCorrectFullWidth);
    };
    connect(&preferences_, &WorkbenchSettings::editorSettingsChanged,
            this, applyEditorSettings);
    connect(&services.shellNotifications(), &miacode::ShellNotifications::editorPreferencesChanged,
            &preferences_, &WorkbenchSettings::reloadEditorSettings);
    connect(&services.shellNotifications(), &miacode::ShellNotifications::muriPromptPreferenceChanged,
            &analysis_, &AnalysisModel::refreshPreferences);
    connect(&document_, &DocumentModel::metadataChanged,
            this, [this] { editor_.setWholeBpm(document_.wholeBpm()); });
    applyEditorSettings();
    editor_.setWholeBpm(document_.wholeBpm());
}

QObject* ApplicationContext::document() { return &document_; }
QObject* ApplicationContext::analysis() { return &analysis_; }
QObject* ApplicationContext::preferences() { return &preferences_; }
QObject* ApplicationContext::appBackground() { return &appBackground_; }
QObject* ApplicationContext::preview() { return &preview_; }
QObject* ApplicationContext::commands() { return &commands_; }
QObject* ApplicationContext::shell() { return &lifecycle_; }
QObject* ApplicationContext::timeline() { return &timeline_; }
QObject* ApplicationContext::pages() { return &pages_; }
QObject* ApplicationContext::editor() { return &editor_; }
QObject* ApplicationContext::editorSync() { return &services_.editorSync(); }
QObject* ApplicationContext::shortcuts() { return &shortcuts_; }
QObject* ApplicationContext::windowChrome() const { return windowChrome_; }
QObject* ApplicationContext::chartDropBridge() const { return chartDropBridge_; }
QObject* ApplicationContext::platform() { return &platform_; }

// Owned by the application service assembly, which is built before the window,
// so the export session and the QML shell share one boundary; a second instance
// would mean a second dialog host and duplicated pickers.
QObject* ApplicationContext::uiRequests() { return &services_.uiRequests(); }

QObject* ApplicationContext::jobProgress() { return &services_.jobProgress(); }

QObject* ApplicationContext::mediaTools() { return &mediaTools_; }

QObject* ApplicationContext::preferencesModel() { return &preferencesModel_; }
QObject* ApplicationContext::audioSettings() { return &audioSettings_; }
QObject* ApplicationContext::previewSettings() { return &previewSettings_; }

QObject* ApplicationContext::latency() { return &latency_; }

void ApplicationContext::setWindowChrome(QObject* chrome)
{
    windowChrome_ = chrome;
}

void ApplicationContext::setChartDropBridge(QObject* bridge)
{
    if (chartDropBridge_ == bridge) {
        return;
    }
    chartDropBridge_ = bridge;
    emit chartDropBridgeChanged();
}

} // namespace miacode::ui
