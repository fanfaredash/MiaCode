#include "QmlDocumentModel.h"

#include "app/mainwindow/MainWindow.h"
#include "core/chart/document/SimaiDocument.h"

#include <QFileInfo>
#include <QVariantMap>

QmlDocumentModel::QmlDocumentModel(MainWindow& backend, QObject* parent)
    : QObject(parent), backend_(&backend)
{
}

QString QmlDocumentModel::chartText() const
{
    return backend_->activeChartText();
}

void QmlDocumentModel::setChartText(const QString& value)
{
    if (!backend_->hasActiveDifficulty() || chartText() == value) return;
    if (SimaiDifficultyData* difficulty = backend_->document_.difficulty(backend_->activeDifficultyId_)) {
        difficulty->chart = value;
    }
    backend_->setEditorText(value);
    backend_->scheduleTimelineRefresh();
    markDocumentChanged();
    emit chartTextChanged();
}

QString QmlDocumentModel::metadataTitle() const { return backend_->document_.title; }
QString QmlDocumentModel::metadataArtist() const { return backend_->document_.artist; }
QString QmlDocumentModel::metadataFirst() const { return backend_->document_.first; }
QString QmlDocumentModel::metadataDesigner() const { return backend_->document_.designer; }
QString QmlDocumentModel::metadataVideoPath() const { return backend_->document_.videoPath; }
QString QmlDocumentModel::metadataExtraText() const
{
    return SimaiDocument::serializeRawFields(backend_->document_.extraFields);
}
QString QmlDocumentModel::metadataSourceText() const { return backend_->document_.toText(); }
QString QmlDocumentModel::metadataSourceError() const { return metadataSourceError_; }
bool QmlDocumentModel::metadataSourceValid() const { return metadataSourceError_.isEmpty(); }
bool QmlDocumentModel::unifiedDesignerEnabled() const { return backend_->unifiedDesignerEnabled_; }

QStringList QmlDocumentModel::designerCandidates() const
{
    QStringList values;
    const auto append = [&values](const QString& value) {
        const QString normalized = value.trimmed();
        if (!normalized.isEmpty() && !values.contains(normalized)) values.append(normalized);
    };
    append(backend_->document_.designer);
    for (int id : backend_->document_.difficultyIds()) {
        if (const SimaiDifficultyData* difficulty = backend_->document_.difficulty(id)) append(difficulty->designer);
    }
    return values;
}

void QmlDocumentModel::setMetadataTitle(const QString& value)
{
    if (backend_->document_.title == value) return;
    backend_->document_.title = value;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataArtist(const QString& value)
{
    if (backend_->document_.artist == value) return;
    backend_->document_.artist = value;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataFirst(const QString& value)
{
    if (backend_->document_.first == value) return;
    backend_->document_.first = value;
    backend_->scheduleTimelineRefresh();
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataDesigner(const QString& value)
{
    if (backend_->document_.designer == value) return;
    backend_->document_.designer = value;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataVideoPath(const QString& value)
{
    if (backend_->document_.videoPath == value) return;
    backend_->document_.videoPath = value;
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataExtraText(const QString& value)
{
    const QVector<SimaiRawField> fields = SimaiDocument::parseUnmanagedFields(value, true);
    if (backend_->document_.extraFields == fields) return;
    backend_->document_.extraFields = fields;
    backend_->setMetadataExtraText(value);
    backend_->scheduleTimelineRefresh();
    markDocumentChanged();
    emit metadataChanged();
}
void QmlDocumentModel::setMetadataSourceText(const QString& value)
{
    const SimaiDocument parsed = SimaiDocument::fromText(value);
    metadataSourceError_.clear();
    backend_->loadDocument(parsed);
    emit metadataSourceChanged();
    emitDocumentStateChanged();
}

QString QmlDocumentModel::documentTitle() const
{
    return backend_->document_.title.trimmed().isEmpty() ? currentFileName() : backend_->document_.title;
}
QString QmlDocumentModel::currentFilePath() const { return backend_->currentFilePath_; }
QString QmlDocumentModel::currentFileName() const
{
    return backend_->currentFilePath_.isEmpty()
        ? QStringLiteral("未命名")
        : QFileInfo(backend_->currentFilePath_).fileName();
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
int QmlDocumentModel::currentDifficultyId() const { return backend_->activeDifficultyId_; }

QVariantList QmlDocumentModel::difficulties() const
{
    QVariantList result;
    for (int id : backend_->document_.difficultyIds()) {
        const SimaiDifficultyData* difficulty = backend_->document_.difficulty(id);
        if (difficulty == nullptr) continue;
        const QString name = SimaiDocument::difficultyName(id);
        result.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("level"), difficulty->level},
            {QStringLiteral("designer"), difficulty->designer},
            {QStringLiteral("label"), difficulty->level.trimmed().isEmpty()
                ? name : QStringLiteral("%1 %2").arg(name, difficulty->level)},
        });
    }
    return result;
}

QVariantList QmlDocumentModel::availableDifficulties() const
{
    QVariantList result;
    for (int id = 1; id <= 7; ++id) {
        if (backend_->document_.difficulty(id) == nullptr) {
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
    const SimaiDifficultyData* difficulty = backend_->document_.difficulty(currentDifficultyId());
    return difficulty != nullptr ? difficulty->level : QString();
}
QString QmlDocumentModel::currentDifficultyDesigner() const
{
    const SimaiDifficultyData* difficulty = backend_->document_.difficulty(currentDifficultyId());
    return difficulty != nullptr ? difficulty->designer : QString();
}
void QmlDocumentModel::setCurrentDifficultyLevel(const QString& value)
{
    SimaiDifficultyData* difficulty = backend_->document_.difficulty(currentDifficultyId());
    if (difficulty == nullptr || difficulty->level == value) return;
    difficulty->level = value;
    markDocumentChanged();
    emit currentDifficultyFieldsChanged();
    emit currentDifficultyChanged();
    emit difficultiesChanged();
}
void QmlDocumentModel::setCurrentDifficultyDesigner(const QString& value)
{
    SimaiDifficultyData* difficulty = backend_->document_.difficulty(currentDifficultyId());
    if (difficulty == nullptr || difficulty->designer == value) return;
    difficulty->designer = value;
    markDocumentChanged();
    emit currentDifficultyFieldsChanged();
    emit difficultiesChanged();
}

QVariantList QmlDocumentModel::syntaxIssues() const { return {}; }
int QmlDocumentModel::syntaxIssueCount() const { return 0; }
int QmlDocumentModel::syntaxErrorCount() const { return 0; }
int QmlDocumentModel::syntaxWarningCount() const { return 0; }
int QmlDocumentModel::parsedNoteCount() const { return 0; }
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
    if (!backend_->openFileAtPath(path, true, true)) {
        emit operationFailed(tr("打开失败"), tr("无法打开谱面文件。"));
        return false;
    }
    emitDocumentStateChanged();
    emit documentReplaced();
    return true;
}
bool QmlDocumentModel::save()
{
    if (backend_->currentFilePath_.isEmpty()) return false;
    const bool saved = backend_->saveToPath(backend_->currentFilePath_);
    if (saved) emitDocumentStateChanged();
    return saved;
}
bool QmlDocumentModel::saveAs(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const bool saved = backend_->saveToPath(path);
    if (saved) emitDocumentStateChanged();
    return saved;
}
void QmlDocumentModel::discardChanges()
{
    if (!backend_->currentFilePath_.isEmpty()) openFile(QUrl::fromLocalFile(backend_->currentFilePath_));
}
void QmlDocumentModel::selectDifficulty(int id)
{
    if (!backend_->switchToDifficultyField(id)) return;
    emitDocumentStateChanged();
}
bool QmlDocumentModel::addDifficulty(int id)
{
    if (!SimaiDocument::isDifficultyId(id) || backend_->document_.difficulty(id) != nullptr) return false;
    backend_->document_.ensureDifficulty(id);
    backend_->documentDirty_ = true;
    backend_->rebuildFieldSidebar();
    const bool selected = backend_->switchToDifficultyField(id);
    emitDocumentStateChanged();
    return selected;
}
bool QmlDocumentModel::removeDifficulty(int id)
{
    if (!backend_->deleteDifficultyField(id)) return false;
    emitDocumentStateChanged();
    return true;
}
void QmlDocumentModel::validateChart()
{
    // Reuse the existing silent validation path. Issue list bridging into
    // syntaxIssues() is still incomplete on the v2 shell.
    backend_->runValidateSimaiSilently(false);
    emit syntaxIssuesChanged();
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
    backend_->document_.designer = canonicalName;
    for (int id : backend_->document_.difficultyIds()) {
        if (SimaiDifficultyData* difficulty = backend_->document_.difficulty(id)) difficulty->designer = canonicalName;
    }
    backend_->unifiedDesignerEnabled_ = true;
    markDocumentChanged();
    emit unifiedDesignerEnabledChanged();
    emit metadataChanged();
    emit difficultiesChanged();
}
void QmlDocumentModel::disableUnifiedDesigner()
{
    if (!backend_->unifiedDesignerEnabled_) return;
    backend_->unifiedDesignerEnabled_ = false;
    markDocumentChanged();
    emit unifiedDesignerEnabledChanged();
}

void QmlDocumentModel::markDocumentChanged()
{
    backend_->documentDirty_ = true;
    backend_->updateDirtyState();
    emit dirtyChanged();
    emit dirtyEditorKeysChanged();
    emit metadataSourceChanged();
    emit documentTitleChanged();
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
}
