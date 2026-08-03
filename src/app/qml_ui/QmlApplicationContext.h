#pragma once

#include "QmlCommandService.h"
#include "QmlDocumentModel.h"
#include "QmlEditorPageHost.h"
#include "QmlPreviewModel.h"
#include "QmlWorkspaceSettings.h"

#include <QObject>

class MainWindow;
class QuickShellController;

// Root contract injected into MiaCode.UI. Every visual component reaches the
// application through these cohesive service objects.
class QmlApplicationContext final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* document READ document CONSTANT)
    Q_PROPERTY(QObject* preferences READ preferences CONSTANT)
    Q_PROPERTY(QObject* preview READ preview CONSTANT)
    Q_PROPERTY(QObject* commands READ commands CONSTANT)
    Q_PROPERTY(QObject* shell READ shell CONSTANT)
    Q_PROPERTY(QObject* pages READ pages CONSTANT)

public:
    QmlApplicationContext(MainWindow& backend, QuickShellController& shell, QObject* parent = nullptr);

    QObject* document();
    QObject* preferences();
    QObject* preview();
    QObject* commands();
    QObject* shell();
    QObject* pages();

private:
    QmlWorkspaceSettings preferences_;
    QmlDocumentModel document_;
    QmlPreviewModel preview_;
    QmlCommandService commands_;
    QmlEditorPageHost pages_;
    QuickShellController* shell_ = nullptr;
};
