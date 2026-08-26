#include "QmlDocumentModel.h"

#include "editor/BookmarkCommentSyntax.h"

#include "app/mainwindow/MainWindow.h"
#include "common/DebugLog.h"
#include "core/chart/document/SimaiDocument.h"

#include <QFileInfo>
#include <QVariantMap>

QmlDocumentModel::QmlDocumentModel(
    MainWindow& backend, miacode::v2::ChartWorkspace& workspace,
    miacode::v2::ChartWorkspaceFileService& fileService,
    miacode::v2::AnalysisService& analysisService, QObject* parent)
    : QObject(parent)
    , backend_(&backend)
    , workspace_(&workspace)
    , fileService_(&fileService)
    , analysisService_(&analysisService)
{
    if (!workspace_->snapshot().hasDocument) {
        workspace_->openSource(
            backend_->documentSourceText(), backend_->documentFilePath(),
            backend_->documentActiveDifficultyId());
    }
    backend_->setQmlDocumentSaveHandler([this](const QString& path) {
        return saveToPath(path);
    });
    // The workspace was seeded from the already-open backend document above.
    // Publish that first committed identity as well so every subsequent
    // backend navigation value is stamped with the workspace revision.
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
    refreshDocumentState();
    connect(analysisService_, &miacode::v2::AnalysisService::snapshotChanged,
            this, [this](int, quint64) {
                refreshDocumentState();
                emit syntaxIssuesChanged();
                emit documentStateChanged();
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
            adoptBackendDocumentReplacement();
            // A desktop report says the editor keeps showing the outgoing
            // chart after a switch while the timeline and preview media follow
            // the incoming one. Nothing in the log records document identity,
            // so this pins what the QML projection publishes at the moment of
            // replacement; SourceEditor logs what it ends up showing.
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("editor/document_replaced"),
                QStringLiteral("path=%1 difficulty=%2 revision=%3 chart_chars=%4 difficulties=%5")
                    .arg(backend_ != nullptr ? backend_->documentFilePath() : QString())
                    .arg(currentDifficultyId())
                    .arg(documentRevision_)
                    .arg(chartText().size())
                    .arg(workspace_ != nullptr
                             ? workspace_->document().difficultyIds().size() : -1));
        }, Qt::QueuedConnection);
    });
}

QmlDocumentModel::~QmlDocumentModel()
{
    if (backend_ != nullptr) {
        backend_->setQmlDocumentSaveHandler({});
    }
}

QString QmlDocumentModel::chartText() const
{
    if (workspace_ == nullptr) return {};
    const SimaiDifficultyData* difficulty =
        workspace_->document().difficulty(currentDifficultyId());
    return difficulty != nullptr ? difficulty->chart : QString();
}

void QmlDocumentModel::setChartText(const QString& value)
{
    if (workspace_ == nullptr
        || !workspace_->replaceActiveDifficultyChart(value).accepted) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}

QString QmlDocumentModel::metadataTitle() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::Title); }
QString QmlDocumentModel::metadataArtist() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::Artist); }
QString QmlDocumentModel::metadataFirst() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::First); }
QString QmlDocumentModel::metadataDesigner() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::Designer); }
QString QmlDocumentModel::metadataVideoPath() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::VideoPath); }
QString QmlDocumentModel::metadataExtraText() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::ExtraText); }
QString QmlDocumentModel::metadataSourceText() const
{
    return metadataSourceError_.isEmpty()
        ? (workspace_ != nullptr ? workspace_->snapshot().sourceText : QString())
        : metadataSourceAttemptText_;
}
QString QmlDocumentModel::metadataSourceError() const { return metadataSourceError_; }
QVariantList QmlDocumentModel::metadataSourceIssues() const { return sourceIssuesToVariantList(); }
bool QmlDocumentModel::metadataSourceValid() const { return metadataSourceError_.isEmpty(); }
bool QmlDocumentModel::unifiedDesignerEnabled() const { return unifiedDesignerEnabled_; }

QStringList QmlDocumentModel::designerCandidates() const
{
    QStringList values;
    const auto append = [&values](const QString& value) {
        const QString normalized = value.trimmed();
        if (!normalized.isEmpty() && !values.contains(normalized)) values.append(normalized);
    };
    append(metadataDesigner());
    if (workspace_ == nullptr) return values;
    for (int id : workspace_->document().difficultyIds()) {
        append(difficultyField(id, miacode::v2::ChartWorkspaceDifficultyField::Designer));
    }
    return values;
}

void QmlDocumentModel::setMetadataTitle(const QString& value)
{
    if (workspace_ == nullptr || !workspace_->updateDocumentField(
            miacode::v2::ChartWorkspaceDocumentField::Title, value)) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataArtist(const QString& value)
{
    if (workspace_ == nullptr || !workspace_->updateDocumentField(
            miacode::v2::ChartWorkspaceDocumentField::Artist, value)) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataFirst(const QString& value)
{
    if (workspace_ == nullptr || !workspace_->updateDocumentField(
            miacode::v2::ChartWorkspaceDocumentField::First, value)) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataDesigner(const QString& value)
{
    if (workspace_ == nullptr) return;
    const bool changed = unifiedDesignerEnabled_
        ? workspace_->unifyDesigners(value)
        : workspace_->updateDocumentField(
              miacode::v2::ChartWorkspaceDocumentField::Designer, value);
    if (!changed) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataVideoPath(const QString& value)
{
    if (workspace_ == nullptr || !workspace_->updateDocumentField(
            miacode::v2::ChartWorkspaceDocumentField::VideoPath, value)) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataExtraText(const QString& value)
{
    if (workspace_ == nullptr || !workspace_->updateDocumentField(
            miacode::v2::ChartWorkspaceDocumentField::ExtraText, value)) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataSourceText(const QString& value)
{
    if (workspace_ == nullptr) return;
    const miacode::v2::ChartWorkspaceResult result = workspace_->replaceSource(value);
    metadataSourceIssues_.clear();
    metadataSourceIssues_.reserve(result.issues.size());
    for (const miacode::v2::ChartWorkspaceIssue& issue : result.issues) {
        metadataSourceIssues_.append({
            issue.line, issue.column, issue.endColumn,
            issue.severity == miacode::v2::ChartWorkspaceIssueSeverity::Warning
                ? miacode::qml_ui::DocumentValidationIssueSeverity::Warning
                : miacode::qml_ui::DocumentValidationIssueSeverity::Error,
            issue.message});
    }
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
    clearMetadataSourceRejection();
    publishWorkspaceCommit(WorkspaceCommitKind::SourceReplacement, true);
}

QString QmlDocumentModel::documentTitle() const
{
    const QString chartTitle = metadataTitle().trimmed().isEmpty()
        ? currentFileName() : metadataTitle();
    const QString difficulty = currentDifficultyId() > 0 ? currentDifficultyLabel() : QString();
    return difficulty.isEmpty() ? chartTitle
        : QStringLiteral("%1 — %2").arg(chartTitle, difficulty);
}
QString QmlDocumentModel::currentFilePath() const
{
    return workspace_ != nullptr ? workspace_->snapshot().filePath : QString();
}
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
    if (workspace_ == nullptr) return result;
    for (int id : workspace_->document().difficultyIds()) {
        const QString name = SimaiDocument::difficultyName(id);
        const QString level = difficultyField(
            id, miacode::v2::ChartWorkspaceDifficultyField::Level);
        const QString designer = difficultyField(
            id, miacode::v2::ChartWorkspaceDifficultyField::Designer);
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
    const QVector<int> existingIds = workspace_ != nullptr
        ? workspace_->document().difficultyIds() : QVector<int>();
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
    return difficultyField(
        currentDifficultyId(), miacode::v2::ChartWorkspaceDifficultyField::Level);
}
QString QmlDocumentModel::currentDifficultyDesigner() const
{
    return difficultyField(
        currentDifficultyId(), miacode::v2::ChartWorkspaceDifficultyField::Designer);
}
void QmlDocumentModel::setCurrentDifficultyLevel(const QString& value)
{
    if (workspace_ == nullptr || !workspace_->updateDifficultyField(
            currentDifficultyId(), miacode::v2::ChartWorkspaceDifficultyField::Level,
            value)) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setCurrentDifficultyDesigner(const QString& value)
{
    if (workspace_ == nullptr) return;
    const bool changed = unifiedDesignerEnabled_
        ? workspace_->unifyDesigners(value)
        : workspace_->updateDifficultyField(
              currentDifficultyId(), miacode::v2::ChartWorkspaceDifficultyField::Designer,
              value);
    if (!changed) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
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
qulonglong QmlDocumentModel::bookmarkGeneration() const { return bookmarkGeneration_; }

bool QmlDocumentModel::openFile(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (fileService_ == nullptr) return false;
    const miacode::v2::ChartWorkspaceFileResult result = fileService_->open(path);
    if (!result.accepted) {
        emit operationFailed(tr("打开失败"), tr("无法打开谱面文件。"));
        return false;
    }
    unifiedDesignerEnabled_ = false;
    clearMetadataSourceRejection();
    publishWorkspaceCommit(
        WorkspaceCommitKind::Open, true, result.usedSystemEncoding);
    return true;
}
bool QmlDocumentModel::save()
{
    if (fileService_ == nullptr) return false;
    const miacode::v2::ChartWorkspaceFileResult result = fileService_->save();
    if (!result.accepted) return false;
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}
bool QmlDocumentModel::saveAs(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    return saveToPath(path);
}
void QmlDocumentModel::discardChanges()
{
    if (fileService_ == nullptr || workspace_ == nullptr) return;
    const QString path = workspace_->snapshot().filePath;
    if (path.isEmpty()) return;
    const miacode::v2::ChartWorkspaceFileResult result = fileService_->open(path);
    if (!result.accepted) return;
    clearMetadataSourceRejection();
    publishWorkspaceCommit(
        WorkspaceCommitKind::Open, true, result.usedSystemEncoding);
}
void QmlDocumentModel::selectDifficulty(int id)
{
    if (workspace_ == nullptr || !workspace_->selectDifficulty(id)) return;
    publishWorkspaceCommit(WorkspaceCommitKind::DifficultySelection);
}
bool QmlDocumentModel::addDifficulty(int id)
{
    if (workspace_ == nullptr || !workspace_->addDifficulty(id)) return false;
    publishWorkspaceCommit(WorkspaceCommitKind::Structure);
    return true;
}
bool QmlDocumentModel::removeDifficulty(int id)
{
    if (workspace_ == nullptr || !workspace_->removeDifficulty(id)) return false;
    publishWorkspaceCommit(WorkspaceCommitKind::Structure);
    return true;
}
void QmlDocumentModel::validateChart()
{
    if (analysisService_ != nullptr) analysisService_->requestAnalysis();
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
    if (workspace_ == nullptr) return;
    unifiedDesignerEnabled_ = true;
    workspace_->unifyDesigners(canonicalName);
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
    emit unifiedDesignerEnabledChanged();
}
void QmlDocumentModel::disableUnifiedDesigner()
{
    if (!unifiedDesignerEnabled_) return;
    unifiedDesignerEnabled_ = false;
    emit unifiedDesignerEnabledChanged();
}

void QmlDocumentModel::logEditorDocumentState(const QString& reason, int difficultyId,
                                              qulonglong revision, int shownChars,
                                              bool metadataMode)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("editor/document_shown"),
        QStringLiteral("reason=%1 difficulty=%2 revision=%3 shown_chars=%4 metadata=%5 "
                       "projected_difficulty=%6 projected_revision=%7 projected_chars=%8")
            .arg(reason)
            .arg(difficultyId)
            .arg(revision)
            .arg(shownChars)
            .arg(metadataMode ? 1 : 0)
            .arg(currentDifficultyId())
            .arg(documentRevision_)
            .arg(metadataMode ? metadataSourceText().size() : chartText().size()));
}

QVariantList QmlDocumentModel::bookmarksForDifficulty(int difficultyId) const
{
    QVariantList bookmarks;
    if (workspace_ == nullptr) return bookmarks;
    const SimaiDifficultyData* difficulty = workspace_->document().difficulty(difficultyId);
    if (difficulty == nullptr) return bookmarks;
    const QStringList lines = difficulty->chart.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        const auto bookmark = miacode::editor::parseBookmarkComment(lines.at(index));
        if (!bookmark.has_value()) {
            continue;
        }
        bookmarks.append(QVariantMap{
            {QStringLiteral("line"), index + 1},
            {QStringLiteral("title"), bookmark->title},
        });
    }
    return bookmarks;
}

void QmlDocumentModel::navigateToBookmark(int difficultyId, int line)
{
    if (difficultyId <= 0 || line <= 0) {
        return;
    }
    if (difficultyId != currentDifficultyId()) {
        selectDifficulty(difficultyId);
    }
    if (difficultyId != currentDifficultyId()) {
        return;
    }
    QMetaObject::invokeMethod(this, [this, difficultyId, line] {
        if (backend_ != nullptr && difficultyId == currentDifficultyId()) {
            backend_->requestEditorNavigation(line, 1, line, 1, false, true, true);
        }
    }, Qt::QueuedConnection);
}

void QmlDocumentModel::publishWorkspaceCommit(
    WorkspaceCommitKind kind, bool replacement, bool usedSystemEncoding)
{
    if (workspace_ == nullptr) return;
    const miacode::v2::ChartWorkspaceSnapshot snapshot = workspace_->snapshot();
    MainWindow::QmlDocumentCommitKind backendKind =
        MainWindow::QmlDocumentCommitKind::Incremental;
    switch (kind) {
    case WorkspaceCommitKind::Incremental:
        backendKind = MainWindow::QmlDocumentCommitKind::Incremental;
        break;
    case WorkspaceCommitKind::DifficultySelection:
        backendKind = MainWindow::QmlDocumentCommitKind::DifficultySelection;
        break;
    case WorkspaceCommitKind::Structure:
        backendKind = MainWindow::QmlDocumentCommitKind::Structure;
        break;
    case WorkspaceCommitKind::SourceReplacement:
        backendKind = MainWindow::QmlDocumentCommitKind::SourceReplacement;
        break;
    case WorkspaceCommitKind::Open:
        backendKind = MainWindow::QmlDocumentCommitKind::Open;
        break;
    case WorkspaceCommitKind::SavePoint:
        backendKind = MainWindow::QmlDocumentCommitKind::SavePoint;
        break;
    }
    if (backend_ != nullptr) {
        backend_->applyCommittedQmlDocument(
            snapshot.sourceText, snapshot.filePath, snapshot.activeDifficultyId,
            snapshot.dirty, snapshot.revision, backendKind, usedSystemEncoding);
    }
    emitDocumentStateChanged();
    if (replacement) emit documentReplaced();
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
    ++bookmarkGeneration_;
    emit bookmarksChanged();
    emit documentStateChanged();
}

void QmlDocumentModel::refreshDocumentState()
{
    const miacode::v2::ChartWorkspaceSnapshot workspaceSnapshot =
        workspace_ != nullptr ? workspace_->snapshot()
                              : miacode::v2::ChartWorkspaceSnapshot();
    validationSnapshot_ = analysisService_ != nullptr
        ? miacode::qml_ui::projectDocumentValidation(
              analysisService_->snapshot(), workspaceSnapshot.activeDifficultyId,
              workspaceSnapshot.revision)
        : miacode::qml_ui::DocumentValidationProjection();
    documentRevision_ = workspaceSnapshot.revision;
    miacode::qml_ui::DocumentPresentationInput input;
    input.activeDifficultyId = workspaceSnapshot.activeDifficultyId;
    input.dirty = workspaceSnapshot.dirty;
    input.documentRevision = documentRevision_;
    input.validation = validationSnapshot_;
    presentationState_ = miacode::qml_ui::projectDocumentPresentation(input);
}

bool QmlDocumentModel::saveToPath(const QString& path)
{
    if (fileService_ == nullptr || path.trimmed().isEmpty()) return false;
    const miacode::v2::ChartWorkspaceFileResult result = fileService_->saveAs(path);
    if (!result.accepted) return false;
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

void QmlDocumentModel::adoptBackendDocumentReplacement()
{
    if (backend_ == nullptr || workspace_ == nullptr) return;
    const miacode::v2::ChartWorkspaceSnapshot current = workspace_->snapshot();
    const QString backendSource = backend_->documentSourceText();
    const QString backendPath = backend_->documentFilePath();
    const int backendDifficultyId = backend_->documentActiveDifficultyId();
    if (current.hasDocument && current.sourceText == backendSource
        && current.filePath == backendPath
        && current.activeDifficultyId == backendDifficultyId) {
        return;
    }

    const miacode::v2::ChartWorkspaceResult adopted = workspace_->openSource(
        backendSource, backendPath, backendDifficultyId);
    if (!adopted.accepted) return;
    unifiedDesignerEnabled_ = false;
    clearMetadataSourceRejection();
    emitDocumentStateChanged();
    emit unifiedDesignerEnabledChanged();
    emit documentReplaced();
}

QString QmlDocumentModel::documentField(
    miacode::v2::ChartWorkspaceDocumentField field) const
{
    if (workspace_ == nullptr) return {};
    const SimaiDocument& document = workspace_->document();
    switch (field) {
    case miacode::v2::ChartWorkspaceDocumentField::Title:
        return document.title;
    case miacode::v2::ChartWorkspaceDocumentField::Artist:
        return document.artist;
    case miacode::v2::ChartWorkspaceDocumentField::First:
        return document.first;
    case miacode::v2::ChartWorkspaceDocumentField::Designer:
        return document.designer;
    case miacode::v2::ChartWorkspaceDocumentField::VideoPath:
        return document.videoPath;
    case miacode::v2::ChartWorkspaceDocumentField::ExtraText:
        return SimaiDocument::serializeRawFields(document.extraFields);
    }
    return {};
}

QString QmlDocumentModel::difficultyField(
    int difficultyId, miacode::v2::ChartWorkspaceDifficultyField field) const
{
    if (workspace_ == nullptr) return {};
    const SimaiDifficultyData* difficulty = workspace_->document().difficulty(difficultyId);
    if (difficulty == nullptr) return {};
    switch (field) {
    case miacode::v2::ChartWorkspaceDifficultyField::Level:
        return difficulty->level;
    case miacode::v2::ChartWorkspaceDifficultyField::Designer:
        return difficulty->designer;
    }
    return {};
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
