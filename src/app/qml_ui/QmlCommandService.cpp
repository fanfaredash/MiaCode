#include "QmlCommandService.h"
#include "QmlShortcutCommands.h"

#include "QmlDocumentModel.h"
#include "mainwindow/MainWindow.h"

QmlCommandService::QmlCommandService(
    MainWindow& backend,
    QmlDocumentModel& document,
    QObject* parent)
    : QObject(parent)
    , backend_(&backend)
    , document_(&document)
{
}

bool QmlCommandService::openDocument(const QUrl& fileUrl) { return document_->openFile(fileUrl); }
bool QmlCommandService::saveDocument() { return document_->save(); }
bool QmlCommandService::saveDocumentAs(const QUrl& fileUrl) { return document_->saveAs(fileUrl); }
void QmlCommandService::discardDocumentChanges() { document_->discardChanges(); }
void QmlCommandService::validateDocument() { document_->validateChart(); }
void QmlCommandService::selectDifficulty(int id) { document_->selectDifficulty(id); }
bool QmlCommandService::addDifficulty(int id) { return document_->addDifficulty(id); }
bool QmlCommandService::removeDifficulty(int id) { return document_->removeDifficulty(id); }
void QmlCommandService::enableUnifiedDesigner(const QString& name) { document_->enableUnifiedDesigner(name); }
void QmlCommandService::disableUnifiedDesigner() { document_->disableUnifiedDesigner(); }

void QmlCommandService::openPreferences()
{
    if (backend_ != nullptr) {
        backend_->onPreferences();
    }
}

QStringList QmlCommandService::shortcutCommandIds() const
{
    // Chart transforms only, and the shell binds them to the editor rather than
    // back to MainWindow: a transform acts on the editor's selection, which is
    // the one thing this side does not have. Preview commands bind straight to
    // the preview session instead of coming through here.
    return miacode::qml_ui::qmlShortcutCommandIds();
}
