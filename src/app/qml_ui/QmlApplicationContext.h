#pragma once

#include "QmlCommandService.h"
#include "QmlDocumentModel.h"
#include "QmlAnalysisModel.h"
#include "QmlEditorPageHost.h"
#include "QmlEditorController.h"
#include "QmlShortcutModel.h"
#include "QmlPreviewModel.h"
#include "QmlUiPlatformChrome.h"
#include "QmlUiSettings.h"
#include "app/v2/ChartWorkspace.h"
#include "app/v2/ChartWorkspaceFileService.h"

#include <QObject>

class MainWindow;
class QuickShellController;

// Root contract injected into MiaCode.UI. Every visual component reaches the
// application through these cohesive service objects.
class QmlApplicationContext final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* document READ document CONSTANT)
    Q_PROPERTY(QObject* analysis READ analysis CONSTANT)
    Q_PROPERTY(QObject* preferences READ preferences CONSTANT)
    Q_PROPERTY(QObject* preview READ preview CONSTANT)
    Q_PROPERTY(QObject* commands READ commands CONSTANT)
    Q_PROPERTY(QObject* shell READ shell CONSTANT)
    Q_PROPERTY(QObject* pages READ pages CONSTANT)
    Q_PROPERTY(QObject* editor READ editor CONSTANT)
    Q_PROPERTY(QObject* shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(QObject* windowChrome READ windowChrome CONSTANT)
    Q_PROPERTY(QObject* platform READ platform CONSTANT)

public:
    QmlApplicationContext(MainWindow& backend, QuickShellController& shell, QObject* parent = nullptr);

    QObject* document();
    QObject* analysis();
    QObject* preferences();
    QObject* preview();
    QObject* commands();
    QObject* shell();
    QObject* pages();
    QObject* editor();
    QObject* shortcuts();
    QObject* windowChrome() const;
    QObject* platform();
    void setWindowChrome(QObject* chrome);

private:
    miacode::v2::ChartWorkspace workspace_;
    miacode::v2::ChartWorkspaceFileService fileService_;
    QmlUiSettings preferences_;
    QmlDocumentModel document_;
    QmlAnalysisModel analysis_;
    QmlPreviewModel preview_;
    QmlCommandService commands_;
    QmlEditorPageHost pages_;
    miacode::qml_ui::QmlEditorController editor_;
    miacode::qml_ui::QmlShortcutModel shortcuts_;
    QmlUiPlatformChrome platform_;
    QuickShellController* shell_ = nullptr;
    QObject* windowChrome_ = nullptr;
};
