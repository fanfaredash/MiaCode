#include "QmlDocumentModel.h"
#include "QmlTouchPadAuthoringBridge.h"

#include "app/mainwindow/MainWindow.h"
#include "core/chart/document/SimaiDocument.h"

#include <QFileInfo>
#include <QVariantMap>

QmlDocumentModel::QmlDocumentModel(MainWindow& backend, QObject* parent)
    : QObject(parent), backend_(&backend)
{
    backend_->setQmlTouchPadAuthoringHandler([this](const QString& pad, bool backtick) {
        const miacode::qml_ui::QmlTouchPadAuthoringContext context{
            currentDifficultyId(), qmlCaretDifficultyId_, documentRevision_, qmlCaretRevision_,
            qmlEditorFocused_, qmlImeComposing_};
        if (!context.accepts()) return false;
        QString updatedText = chartText();
        int updatedPosition = qmlCaretPosition_;
        if (!miacode::qml_ui::applyQmlTouchPadAuthoringEdit(
                &updatedText, &updatedPosition, pad, backtick)) return false;
        setChartText(updatedText);
        qmlCaretAnchor_ = qmlCaretPosition_ = updatedPosition;
        return true;
    });
    backend_->setQmlTouchPadAuthoringContextHandler([this] {
        return miacode::qml_ui::QmlTouchPadAuthoringContext{
            currentDifficultyId(), qmlCaretDifficultyId_, documentRevision_, qmlCaretRevision_,
            qmlEditorFocused_, qmlImeComposing_}.accepts();
    });
    refreshDocumentState();
    connect(backend_, &MainWindow::documentValidationChanged,
            this, [this] {
                // Timeline refreshes may emit while a source transaction is
                // still installing its document.  Deferring the projection
                // makes QML observe the completed transaction, never its
                // previous chart paired with a new pending revision.
                QMetaObject::invokeMethod(this, [this] {
                    refreshDocumentState();
                    emit syntaxIssuesChanged();
                    emit documentStateChanged();
                }, Qt::QueuedConnection);
            });
    connect(backend_, &MainWindow::documentReplaced, this, [this] {
        // A chart may be replaced by the backend directly (startup, root
        // ChartDrop, native File/Open, recovery), not only through this QML
        // facade.  Some replacement routes finalize their dirty/revision
        // state after loadDocument() returns, so defer the projection until
        // that transaction has fully committed.  QML then receives one
        // coherent snapshot for the title, source editor, difficulty tabs,
        // and derived bookmark list rather than a new chart with old state.
        QMetaObject::invokeMethod(this, [this] {
            clearMetadataSourceRejection();
            emitDocumentStateChanged();
            emit documentReplaced();
        }, Qt::QueuedConnection);
    });
}

QmlDocumentModel::~QmlDocumentModel()
{
    if (backend_ != nullptr) {
        backend_->setQmlTouchPadAuthoringHandler({});
        backend_->setQmlTouchPadAuthoringContextHandler({});
    }
}

QString QmlDocumentModel::chartText() const
{
    return backend_->activeDocumentChartText();
}

void QmlDocumentModel::setChartText(const QString& value)
{
    if (!backend_->updateActiveChartText(value)) return;
    markDocumentChanged();
    emit chartTextChanged();
}

QString QmlDocumentModel::metadataTitle() const { return backend_->documentField(MainWindow::DocumentField::Title); }
QString QmlDocumentModel::metadataArtist() const { return backend_->documentField(MainWindow::DocumentField::Artist); }
QString QmlDocumentModel::metadataFirst() const { return backend_->documentField(MainWindow::DocumentField::First); }
QString QmlDocumentModel::metadataDesigner() const { return backend_->documentField(MainWindow::DocumentField::Designer); }
QString QmlDocumentModel::metadataVideoPath() const { return backend_->documentField(MainWindow::DocumentField::VideoPath); }
QString QmlDocumentModel::metadataExtraText() const { return backend_->documentField(MainWindow::DocumentField::ExtraText); }
QString QmlDocumentModel::metadataSourceText() const
{
    return metadataSourceError_.isEmpty()
        ? backend_->documentSourceText()
        : metadataSourceAttemptText_;
}
QString QmlDocumentModel::metadataSourceError() const { return metadataSourceError_; }
QVariantList QmlDocumentModel::metadataSourceIssues() const { return sourceIssuesToVariantList(); }
bool QmlDocumentModel::metadataSourceValid() const { return metadataSourceError_.isEmpty(); }
bool QmlDocumentModel::unifiedDesignerEnabled() const { return backend_->documentUnifiedDesignerEnabled(); }

QStringList QmlDocumentModel::designerCandidates() const
{
    QStringList values;
    const auto append = [&values](const QString& value) {
        const QString normalized = value.trimmed();
        if (!normalized.isEmpty() && !values.contains(normalized)) values.append(normalized);
    };
    append(metadataDesigner());
    for (int id : backend_->documentDifficultyIds()) {
        append(backend_->difficultyField(id, MainWindow::DifficultyField::Designer));
    }
    return values;
}

void QmlDocumentModel::setMetadataTitle(const QString& value)
{
    if (!backend_->updateDocumentField(MainWindow::DocumentField::Title, value)) return;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataArtist(const QString& value)
{
    if (!backend_->updateDocumentField(MainWindow::DocumentField::Artist, value)) return;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataFirst(const QString& value)
{
    if (!backend_->updateDocumentField(MainWindow::DocumentField::First, value)) return;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataDesigner(const QString& value)
{
    if (!backend_->updateDocumentField(MainWindow::DocumentField::Designer, value)) return;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataVideoPath(const QString& value)
{
    if (!backend_->updateDocumentField(MainWindow::DocumentField::VideoPath, value)) return;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataExtraText(const QString& value)
{
    if (!backend_->updateDocumentField(MainWindow::DocumentField::ExtraText, value)) return;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataSourceText(const QString& value)
{
    const MainWindow::DocumentSourceReplaceResult result = backend_->replaceDocumentSourceText(value);
    metadataSourceIssues_ = result.issues;
    if (!result.accepted) {
        metadataSourceAttemptText_ = value;
        QStringList messages;
        for (const auto& issue : metadataSourceIssues_) {
            messages.append(QStringLiteral("%1:%2 %3")
                .arg(issue.line).arg(issue.column).arg(issue.message));
        }
        metadataSourceError_ = messages.join(QLatin1Char('\n'));
        emit metadataSourceChanged();
        return;
    }
    // loadDocument() emits MainWindow::documentReplaced synchronously.  Its
    // connection above clears any rejected-source draft and publishes the
    // complete replacement snapshot exactly once.
}

QString QmlDocumentModel::documentTitle() const
{
    return metadataTitle().trimmed().isEmpty() ? currentFileName() : metadataTitle();
}
QString QmlDocumentModel::currentFilePath() const { return backend_->documentFilePath(); }
QString QmlDocumentModel::currentFileName() const
{
    return currentFilePath().isEmpty()
        ? QStringLiteral("未命名")
        : QFileInfo(currentFilePath()).fileName();
}
QString QmlDocumentModel::currentDifficultyName() const
{
    return SimaiDocument::difficultyName(currentDifficultyId());
}
QString QmlDocumentModel::currentDifficultyLabel() const
{
    const QString name = currentDifficultyName();
    const QString level = currentDifficultyLevel().trimmed();
    return level.isEmpty() ? name : QStringLiteral("%1 %2").arg(name, level);
}
int QmlDocumentModel::currentDifficultyId() const { return presentationState_.activeDifficultyId; }

QVariantList QmlDocumentModel::difficulties() const
{
    QVariantList result;
    for (int id : backend_->documentDifficultyIds()) {
        const QString name = SimaiDocument::difficultyName(id);
        const QString level = backend_->difficultyField(id, MainWindow::DifficultyField::Level);
        const QString designer = backend_->difficultyField(id, MainWindow::DifficultyField::Designer);
        result.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("level"), level},
            {QStringLiteral("designer"), designer},
            {QStringLiteral("label"), level.trimmed().isEmpty()
                ? name : QStringLiteral("%1 %2").arg(name, level)},
        });
    }
    return result;
}

QVariantList QmlDocumentModel::availableDifficulties() const
{
    QVariantList result;
    const QVector<int> existingIds = backend_->documentDifficultyIds();
    for (int id = 1; id <= 7; ++id) {
        if (!existingIds.contains(id)) {
            result.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("label"), SimaiDocument::difficultyName(id)},
            });
        }
    }
    return result;
}

QString QmlDocumentModel::currentDifficultyLevel() const
{
    return backend_->difficultyField(currentDifficultyId(), MainWindow::DifficultyField::Level);
}
QString QmlDocumentModel::currentDifficultyDesigner() const
{
    return backend_->difficultyField(currentDifficultyId(), MainWindow::DifficultyField::Designer);
}
void QmlDocumentModel::setCurrentDifficultyLevel(const QString& value)
{
    if (!backend_->updateDifficultyField(
            currentDifficultyId(), MainWindow::DifficultyField::Level, value)) return;
    markDocumentChanged();
    emit currentDifficultyFieldsChanged();
    emit currentDifficultyChanged();
    emit difficultiesChanged();
}
void QmlDocumentModel::setCurrentDifficultyDesigner(const QString& value)
{
    if (!backend_->updateDifficultyField(
            currentDifficultyId(), MainWindow::DifficultyField::Designer, value)) return;
    markDocumentChanged();
    emit currentDifficultyFieldsChanged();
    emit difficultiesChanged();
}

QVariantList QmlDocumentModel::syntaxIssues() const
{
    const miacode::qml_ui::DocumentValidationProjection& snapshot = validationSnapshot_;
    QVariantList result;
    result.reserve(snapshot.issues.size());
    for (const miacode::qml_ui::DocumentValidationProjectionIssue& issue : snapshot.issues) {
        result.append(QVariantMap{
            {QStringLiteral("line"), issue.line},
            {QStringLiteral("column"), issue.column},
            {QStringLiteral("endColumn"), issue.endColumn},
            {QStringLiteral("severity"),
             issue.severity == miacode::qml_ui::DocumentValidationIssueSeverity::Warning
                 ? QStringLiteral("warning")
                 : QStringLiteral("error")},
            {QStringLiteral("message"), issue.message},
            {QStringLiteral("difficultyId"), presentationState_.activeDifficultyId},
            {QStringLiteral("revision"), QVariant::fromValue<qulonglong>(presentationState_.validationRevision)},
        });
    }
    return result;
}
int QmlDocumentModel::syntaxIssueCount() const
{
    return validationSnapshot_.issues.size();
}
int QmlDocumentModel::syntaxErrorCount() const
{
    return validationSnapshot_.errorCount;
}
int QmlDocumentModel::syntaxWarningCount() const
{
    return validationSnapshot_.warningCount;
}
int QmlDocumentModel::parsedNoteCount() const
{
    return validationSnapshot_.parsedNoteCount;
}
qulonglong QmlDocumentModel::documentRevision() const { return presentationState_.documentRevision; }
qulonglong QmlDocumentModel::validationRevision() const { return presentationState_.validationRevision; }
bool QmlDocumentModel::validationPending() const { return presentationState_.validationPending; }
bool QmlDocumentModel::validationAvailable() const { return presentationState_.validationAvailable; }
bool QmlDocumentModel::dirty() const { return presentationState_.dirty; }
QStringList QmlDocumentModel::dirtyEditorKeys() const
{
    return presentationState_.dirtyEditorKeys;
}

bool QmlDocumentModel::openFile(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (!backend_->openStartupTarget(path)) {
        emit operationFailed(tr("打开失败"), tr("无法打开谱面文件。"));
        return false;
    }
    return true;
}
bool QmlDocumentModel::save()
{
    const bool saved = backend_->saveDocument();
    if (saved) emitDocumentStateChanged();
    return saved;
}
bool QmlDocumentModel::saveAs(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const bool saved = backend_->saveDocumentAs(path);
    if (saved) emitDocumentStateChanged();
    return saved;
}
void QmlDocumentModel::discardChanges()
{
    if (!backend_->discardDocumentChanges()) return;
}
void QmlDocumentModel::selectDifficulty(int id)
{
    if (!backend_->selectDocumentDifficulty(id)) return;
    emitDocumentStateChanged();
}
bool QmlDocumentModel::addDifficulty(int id)
{
    const bool selected = backend_->addDocumentDifficulty(id);
    if (!selected) return false;
    emitDocumentStateChanged();
    return selected;
}
bool QmlDocumentModel::removeDifficulty(int id)
{
    if (!backend_->removeDocumentDifficulty(id)) return false;
    emitDocumentStateChanged();
    return true;
}
void QmlDocumentModel::validateChart()
{
    backend_->validateActiveDocument();
}
int QmlDocumentModel::chartPosition(int line, int column) const
{
    const QString text = chartText();
    int position = 0;
    int currentLine = 1;
    while (currentLine < qMax(1, line) && position < text.size()) {
        const int newline = text.indexOf(QLatin1Char('\n'), position);
        if (newline < 0) return text.size();
        position = newline + 1;
        ++currentLine;
    }
    return qBound(position, position + qMax(0, column - 1), text.size());
}
void QmlDocumentModel::enableUnifiedDesigner(const QString& canonicalName)
{
    backend_->enableUnifiedDocumentDesigner(canonicalName);
    markDocumentChanged();
    emit unifiedDesignerEnabledChanged();
    emit metadataChanged();
    emit difficultiesChanged();
}
void QmlDocumentModel::disableUnifiedDesigner()
{
    if (!backend_->documentUnifiedDesignerEnabled()) return;
    backend_->disableUnifiedDocumentDesigner();
    emit unifiedDesignerEnabledChanged();
}

bool QmlDocumentModel::publishEditorCaret(int difficultyId, qulonglong revision, int line, int column)
{
    if (backend_ == nullptr || revision != documentRevision_
        || difficultyId != currentDifficultyId() || difficultyId <= 0) {
        return false;
    }
    backend_->publishQmlEditorCaret(difficultyId, line, column);
    return true;
}

void QmlDocumentModel::setQmlEditorInteraction(int difficultyId, qulonglong revision, int anchor,
                                               int position, bool focused, bool imeComposing)
{
    qmlCaretDifficultyId_ = difficultyId;
    qmlCaretRevision_ = revision;
    qmlCaretAnchor_ = anchor;
    qmlCaretPosition_ = position;
    qmlEditorFocused_ = focused;
    qmlImeComposing_ = imeComposing;
    if (backend_ != nullptr) backend_->refreshQmlTouchPadAuthoringContext();
}

void QmlDocumentModel::markDocumentChanged()
{
    refreshDocumentState();
    emit dirtyChanged();
    emit dirtyEditorKeysChanged();
    emit metadataSourceChanged();
    emit documentTitleChanged();
    emit syntaxIssuesChanged();
    emit documentStateChanged();
}

void QmlDocumentModel::emitDocumentStateChanged()
{
    refreshDocumentState();
    emit chartTextChanged();
    emit metadataChanged();
    emit metadataSourceChanged();
    emit documentTitleChanged();
    emit currentFilePathChanged();
    emit currentDifficultyChanged();
    emit difficultiesChanged();
    emit currentDifficultyFieldsChanged();
    emit dirtyChanged();
    emit dirtyEditorKeysChanged();
    emit syntaxIssuesChanged();
    emit documentStateChanged();
}

void QmlDocumentModel::refreshDocumentState()
{
    validationSnapshot_ = backend_->documentValidationSnapshot();
    documentRevision_ = validationSnapshot_.revision;
    miacode::qml_ui::DocumentPresentationInput input;
    input.activeDifficultyId = backend_->documentActiveDifficultyId();
    input.dirty = backend_->isWindowModified();
    input.documentRevision = documentRevision_;
    input.validation = validationSnapshot_;
    presentationState_ = miacode::qml_ui::projectDocumentPresentation(input);
}

void QmlDocumentModel::clearMetadataSourceRejection()
{
    metadataSourceError_.clear();
    metadataSourceAttemptText_.clear();
    metadataSourceIssues_.clear();
}

QVariantList QmlDocumentModel::sourceIssuesToVariantList() const
{
    QVariantList result;
    result.reserve(metadataSourceIssues_.size());
    for (const auto& issue : metadataSourceIssues_) {
        result.append(QVariantMap{
            {QStringLiteral("line"), issue.line},
            {QStringLiteral("column"), issue.column},
            {QStringLiteral("endColumn"), issue.endColumn},
            {QStringLiteral("severity"), issue.severity
                == miacode::qml_ui::DocumentValidationIssueSeverity::Warning
                ? QStringLiteral("warning") : QStringLiteral("error")},
            {QStringLiteral("message"), issue.message},
        });
    }
    return result;
}
