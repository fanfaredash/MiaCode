#include "QmlDocumentModel.h"

#include "app/mainwindow/MainWindow.h"
#include "core/chart/document/SimaiDocument.h"

#include <QFileInfo>
#include <QVariantMap>

QmlDocumentModel::QmlDocumentModel(MainWindow& backend, QObject* parent)
    : QObject(parent), backend_(&backend)
{
    connect(backend_, &MainWindow::documentValidationChanged,
            this, &QmlDocumentModel::syntaxIssuesChanged);
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
QString QmlDocumentModel::metadataSourceText() const { return backend_->documentSourceText(); }
QString QmlDocumentModel::metadataSourceError() const { return metadataSourceError_; }
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
    metadataSourceError_.clear();
    if (!backend_->replaceDocumentSourceText(value)) return;
    markDocumentChanged();
    emit metadataSourceChanged();
    emitDocumentStateChanged();
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
int QmlDocumentModel::currentDifficultyId() const { return backend_->documentActiveDifficultyId(); }

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
    const MainWindow::DocumentValidationSnapshot snapshot = backend_->documentValidationSnapshot();
    QVariantList result;
    result.reserve(snapshot.issues.size());
    for (const SimaiNativeValidationIssue& issue : snapshot.issues) {
        result.append(QVariantMap{
            {QStringLiteral("line"), issue.line},
            {QStringLiteral("column"), issue.col},
            {QStringLiteral("endColumn"), issue.endCol},
            {QStringLiteral("severity"),
             issue.severity == SimaiNativeValidationSeverity::Warning
                 ? QStringLiteral("warning")
                 : QStringLiteral("error")},
            {QStringLiteral("message"), issue.displayMessage},
        });
    }
    return result;
}
int QmlDocumentModel::syntaxIssueCount() const
{
    return backend_->documentValidationSnapshot().issues.size();
}
int QmlDocumentModel::syntaxErrorCount() const
{
    return backend_->documentValidationSnapshot().errorCount;
}
int QmlDocumentModel::syntaxWarningCount() const
{
    return backend_->documentValidationSnapshot().warningCount;
}
int QmlDocumentModel::parsedNoteCount() const
{
    return backend_->documentValidationSnapshot().parsedNoteCount;
}
bool QmlDocumentModel::dirty() const { return backend_->isWindowModified(); }
QStringList QmlDocumentModel::dirtyEditorKeys() const
{
    if (!dirty()) return {};
    return currentDifficultyId() > 0
        ? QStringList{QStringLiteral("difficulty:%1").arg(currentDifficultyId())}
        : QStringList{QStringLiteral("metadata")};
}

bool QmlDocumentModel::openFile(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (!backend_->openStartupTarget(path)) {
        emit operationFailed(tr("打开失败"), tr("无法打开谱面文件。"));
        return false;
    }
    emitDocumentStateChanged();
    emit documentReplaced();
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
    emitDocumentStateChanged();
    emit documentReplaced();
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

void QmlDocumentModel::markDocumentChanged()
{
    emit dirtyChanged();
    emit dirtyEditorKeysChanged();
    emit metadataSourceChanged();
    emit documentTitleChanged();
    emit syntaxIssuesChanged();
}

void QmlDocumentModel::emitDocumentStateChanged()
{
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
}
