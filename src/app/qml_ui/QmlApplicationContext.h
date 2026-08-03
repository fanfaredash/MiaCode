#pragma once

#include "QmlCommandService.h"
#include "QmlDocumentModel.h"
#include "QmlPreviewModel.h"
#include "QmlWorkspaceSettings.h"

#include <QObject>

class MainWindow;
class QuickShellController;

// Root contract injected into MiaCode.UI. Every visual component reaches the
// application through these five cohesive service objects.
class QmlApplicationContext final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* document READ document CONSTANT)
    Q_PROPERTY(QObject* preferences READ preferences CONSTANT)
    Q_PROPERTY(QObject* preview READ preview CONSTANT)
    Q_PROPERTY(QObject* commands READ commands CONSTANT)
    Q_PROPERTY(QObject* shell READ shell CONSTANT)

public:
    QmlApplicationContext(MainWindow& backend, QuickShellController& shell, QObject* parent = nullptr);

    QObject* document();
    QObject* preferences();
    QObject* preview();
    QObject* commands();
    QObject* shell();

private:
    QmlWorkspaceSettings preferences_;
    QmlDocumentModel document_;
    QmlPreviewModel preview_;
    QmlCommandService commands_;
    QuickShellController* shell_ = nullptr;
};
