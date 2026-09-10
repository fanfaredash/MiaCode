#pragma once

#include "document/CommandService.h"
#include "document/DocumentModel.h"
#include "document/AnalysisModel.h"
#include "layout/PageHost.h"
#include "editor/EditorController.h"
#include "chrome/ShortcutModel.h"
#include "preview/PreviewModel.h"
#include "shell/ShellLifecycle.h"
#include "timeline/TimelineModel.h"
#include "chrome/PlatformChrome.h"
#include "layout/WorkbenchSettings.h"
#include "media/MediaToolsModel.h"
#include "preferences/PreferencesModel.h"
#include "preferences/AppBackgroundModel.h"
#include "preview/AudioSettingsModel.h"
#include "preview/PreviewSettingsModel.h"
#include "latency/LatencyModel.h"
#include "app/services/ApplicationServices.h"

#include <QObject>

// Root contract injected into MiaCode.UI. Every visual component reaches the
// application through these cohesive service objects.
namespace miacode::ui {

class ApplicationContext final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* document READ document CONSTANT)
    Q_PROPERTY(QObject* analysis READ analysis CONSTANT)
    Q_PROPERTY(QObject* preferences READ preferences CONSTANT)
    Q_PROPERTY(QObject* appBackground READ appBackground CONSTANT)
    Q_PROPERTY(QObject* preview READ preview CONSTANT)
    Q_PROPERTY(QObject* commands READ commands CONSTANT)
    // Root-window close contract; the polling shell controller it replaced
    // is gone, so this is deliberately small.
    Q_PROPERTY(QObject* shell READ shell CONSTANT)
    Q_PROPERTY(QObject* timeline READ timeline CONSTANT)
    Q_PROPERTY(QObject* pages READ pages CONSTANT)
    Q_PROPERTY(QObject* editor READ editor CONSTANT)
    Q_PROPERTY(QObject* editorSync READ editorSync CONSTANT)
    Q_PROPERTY(QObject* shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(QObject* windowChrome READ windowChrome CONSTANT)
    Q_PROPERTY(QObject* chartDropBridge READ chartDropBridge NOTIFY chartDropBridgeChanged)
    Q_PROPERTY(QObject* platform READ platform CONSTANT)
    // Shared Widgets-free UI boundary, hosted once by MainView.qml.
    Q_PROPERTY(QObject* uiRequests READ uiRequests CONSTANT)
    Q_PROPERTY(QObject* jobProgress READ jobProgress CONSTANT)
    Q_PROPERTY(QObject* mediaTools READ mediaTools CONSTANT)
    Q_PROPERTY(QObject* preferencesModel READ preferencesModel CONSTANT)
    Q_PROPERTY(QObject* audioSettings READ audioSettings CONSTANT)
    Q_PROPERTY(QObject* previewSettings READ previewSettings CONSTANT)
    Q_PROPERTY(QObject* latency READ latency CONSTANT)

public:
    // Stage 3.5 item 2 is complete here: the context takes the application
    // service assembly and nothing else. No MainWindow — not a reference, not a
    // parameter, not an include. Every domain reaches the window, while it
    // still exists, through an interface the window implements.
    explicit ApplicationContext(miacode::ApplicationServices& services,
                                   QObject* parent = nullptr);

    QObject* document();
    QObject* analysis();
    QObject* preferences();
    QObject* appBackground();
    QObject* preview();
    QObject* commands();
    QObject* shell();
    QObject* timeline();
    QObject* pages();
    QObject* editor();
    QObject* editorSync();
    QObject* shortcuts();
    QObject* windowChrome() const;
    QObject* chartDropBridge() const;
    QObject* platform();
    QObject* uiRequests();
    QObject* jobProgress();
    QObject* mediaTools();
    QObject* preferencesModel();
    QObject* audioSettings();
    QObject* previewSettings();
    QObject* latency();
    void setWindowChrome(QObject* chrome);
    void setChartDropBridge(QObject* bridge);

signals:
    void chartDropBridgeChanged();

private:
    miacode::ApplicationServices& services_;
    WorkbenchSettings preferences_;
    miacode::ui::AppBackgroundModel appBackground_;
    DocumentModel document_;
    AnalysisModel analysis_;
    PreviewModel preview_;
    miacode::ui::TimelineModel timeline_;
    CommandService commands_;
    PageHost pages_;
    miacode::ui::EditorController editor_;
    miacode::ui::ShortcutModel shortcuts_;
    PlatformChrome platform_;
    miacode::ui::MediaToolsModel mediaTools_;
    miacode::ui::PreferencesModel preferencesModel_;
    miacode::ui::AudioSettingsModel audioSettings_;
    miacode::ui::PreviewSettingsModel previewSettings_;
    miacode::ui::LatencyModel latency_;
    miacode::ui::ShellLifecycle lifecycle_;
    QObject* windowChrome_ = nullptr;
    QObject* chartDropBridge_ = nullptr;
};
} // namespace miacode::ui
