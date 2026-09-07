#pragma once

#include "QmlCommandService.h"
#include "QmlDocumentModel.h"
#include "QmlAnalysisModel.h"
#include "QmlEditorPageHost.h"
#include "QmlEditorController.h"
#include "QmlShortcutModel.h"
#include "QmlPreviewModel.h"
#include "QmlShellLifecycle.h"
#include "QmlTimelineModel.h"
#include "QmlUiPlatformChrome.h"
#include "QmlUiSettings.h"
#include "media/QmlMediaToolsModel.h"
#include "preferences/QmlPreferencesModel.h"
#include "preferences/QmlAppBackgroundModel.h"
#include "preview/QmlAudioSettingsModel.h"
#include "preview/QmlPreviewSettingsModel.h"
#include "latency/QmlLatencyModel.h"
#include "app/v2/ApplicationServices.h"

#include <QObject>

// Root contract injected into MiaCode.UI. Every visual component reaches the
// application through these cohesive service objects.
class QmlApplicationContext final : public QObject
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
    explicit QmlApplicationContext(miacode::v2::ApplicationServices& services,
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
    miacode::v2::ApplicationServices& services_;
    QmlUiSettings preferences_;
    miacode::qml_ui::QmlAppBackgroundModel appBackground_;
    QmlDocumentModel document_;
    QmlAnalysisModel analysis_;
    QmlPreviewModel preview_;
    miacode::qml_ui::QmlTimelineModel timeline_;
    QmlCommandService commands_;
    QmlEditorPageHost pages_;
    miacode::qml_ui::QmlEditorController editor_;
    miacode::qml_ui::QmlShortcutModel shortcuts_;
    QmlUiPlatformChrome platform_;
    miacode::qml_ui::QmlMediaToolsModel mediaTools_;
    miacode::qml_ui::QmlPreferencesModel preferencesModel_;
    miacode::qml_ui::QmlAudioSettingsModel audioSettings_;
    miacode::qml_ui::QmlPreviewSettingsModel previewSettings_;
    miacode::qml_ui::QmlLatencyModel latency_;
    miacode::qml_ui::QmlShellLifecycle lifecycle_;
    QObject* windowChrome_ = nullptr;
    QObject* chartDropBridge_ = nullptr;
};
