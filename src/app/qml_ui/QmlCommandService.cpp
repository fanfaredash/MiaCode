#include "QmlCommandService.h"

#include "QmlDocumentModel.h"

QmlCommandService::QmlCommandService(QmlDocumentModel& document, QObject* parent)
    : QObject(parent), document_(&document)
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
