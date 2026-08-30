#include "ui/UiText.h"
#include "ChartTransformCommands.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "QmlDocumentModel.h"

#include "editor/BookmarkCommentSyntax.h"

#include "app/mainwindow/MainWindow.h"
#include "app/v2/UiRequestService.h"
#include "common/DebugLog.h"
#include "core/chart/document/SimaiDocument.h"

#include <algorithm>
#include <functional>

#include <QFileInfo>
#include <QVariantMap>

namespace {

// 保存 / 放弃 / 取消, in button order. Ids match what the prompts branch on.
QVariantList unsavedSectionChoices()
{
    const auto choice = [](const char* id, const char* labelKey, const char* role) {
        return QVariantMap{
            {QStringLiteral("id"), QLatin1String(id)},
            {QStringLiteral("label"), UiText::text(QLatin1String(labelKey))},
            {QStringLiteral("role"), QLatin1String(role)},
        };
    };
    return QVariantList{
        choice("save", "action.save", "accept"),
        choice("discard", "action.discard", "destructive"),
        choice("cancel", "action.cancel", "reject"),
    };
}

}  // namespace


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
    backend_->setQmlLeaveDocumentHandler(
        [this](std::function<void(bool)> onDecided) { requestLeaveDocument(std::move(onDecided)); });
    backend_->setQmlChartTextHandler([this](const QString& text) {
        if (workspace_ == nullptr) return false;
        setChartText(text);
        return chartText() == text;
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
        backend_->setQmlChartTextHandler({});
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

QStringList QmlDocumentModel::recentDocuments()
{
    return backend_ != nullptr ? backend_->recentDocumentPaths() : QStringList{};
}

void QmlDocumentModel::closeDocument()
{
    if (workspace_ == nullptr) return;
    if (!workspace_->closeDocument().accepted) return;
    unifiedDesignerEnabled_ = false;
    clearMetadataSourceRejection();
    publishWorkspaceCommit(WorkspaceCommitKind::Open, true);
}

bool QmlDocumentModel::saveDifficultySection(int difficultyId)
{
    if (fileService_ == nullptr) return false;
    if (!fileService_->save(difficultyId).accepted) return false;
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

bool QmlDocumentModel::revertDifficultyChart(int difficultyId)
{
    if (workspace_ == nullptr) return false;
    const miacode::v2::ChartWorkspaceResult result =
        workspace_->revertDifficultyChart(difficultyId);
    if (!result.accepted) return false;
    // A section going back to its saved text is a source replacement as far as
    // every consumer is concerned: the text they hold is no longer current.
    publishWorkspaceCommit(WorkspaceCommitKind::SourceReplacement, true);
    return true;
}

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
void QmlDocumentModel::requestLeaveDocument(std::function<void(bool)> onDecided)
{
    if (workspace_ == nullptr || !workspace_->snapshot().dirty) {
        if (onDecided) onDecided(true);
        return;
    }
    askNextDirtySection(std::move(onDecided));
}

void QmlDocumentModel::askNextDirtySection(std::function<void(bool)> onDecided)
{
    miacode::v2::UiRequestService* const requests =
        backend_ != nullptr ? backend_->uiRequestService() : nullptr;
    const miacode::v2::ChartWorkspaceSnapshot snapshot = workspace_->snapshot();
    if (snapshot.dirtyDifficultyIds.isEmpty() || requests == nullptr) {
        askAboutRemainingDocument(std::move(onDecided));
        return;
    }

    const int difficultyId = snapshot.dirtyDifficultyIds.constFirst();
    // Show it before asking about it. A prompt naming a difficulty the user
    // cannot see is a prompt they have to answer from memory.
    selectDifficulty(difficultyId);

    const QString label = SimaiDocument::difficultyName(difficultyId);
    requests->requestChoice(
        UiText::text(QStringLiteral("dialog.unsaved_changes.title")),
        tr("「%1」有未保存的更改。").arg(label),
        unsavedSectionChoices(),
        QStringLiteral("cancel"),
        [this, difficultyId, onDecided = std::move(onDecided)](const QString& choiceId) mutable {
            if (choiceId == QLatin1String("cancel")) {
                if (onDecided) onDecided(false);
                return;
            }
            if (choiceId == QLatin1String("save")) {
                if (fileService_ == nullptr
                    || !fileService_->save(difficultyId).accepted) {
                    // Nothing was written, so leaving would lose it.
                    if (onDecided) onDecided(false);
                    return;
                }
                publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
            } else {
                workspace_->revertDifficultyChart(difficultyId);
                publishWorkspaceCommit(WorkspaceCommitKind::SourceReplacement, true);
            }
            // Whatever happened, that difficulty is no longer among the dirty
            // ones, so this walks the list down rather than around it.
            askNextDirtySection(std::move(onDecided));
        });
}

void QmlDocumentModel::askAboutRemainingDocument(std::function<void(bool)> onDecided)
{
    miacode::v2::UiRequestService* const requests =
        backend_ != nullptr ? backend_->uiRequestService() : nullptr;
    if (!workspace_->snapshot().dirty || requests == nullptr) {
        if (onDecided) onDecided(true);
        return;
    }
    // What is left is not any one difficulty: metadata, or a difficulty added
    // or removed. That is a change to the file, so the file is what it asks
    // about.
    requests->requestChoice(
        UiText::text(QStringLiteral("dialog.unsaved_changes.title")),
        UiText::text(QStringLiteral("dialog.unsaved_changes.message")),
        unsavedSectionChoices(),
        QStringLiteral("cancel"),
        [this, onDecided = std::move(onDecided)](const QString& choiceId) {
            if (choiceId == QLatin1String("cancel")) {
                if (onDecided) onDecided(false);
                return;
            }
            if (choiceId == QLatin1String("save")) {
                if (fileService_ == nullptr || !fileService_->save(0).accepted) {
                    if (onDecided) onDecided(false);
                    return;
                }
                publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
            } else if (!workspace_->snapshot().filePath.isEmpty()) {
                discardChanges();
            }
            if (onDecided) onDecided(true);
        });
}

bool QmlDocumentModel::wholeSourceEditorActive() const { return wholeSourceEditorActive_; }

void QmlDocumentModel::setWholeSourceEditorActive(bool active)
{
    if (wholeSourceEditorActive_ == active) return;
    wholeSourceEditorActive_ = active;
    emit wholeSourceEditorActiveChanged();
}

int QmlDocumentModel::saveSectionDifficultyId() const
{
    if (wholeSourceEditorActive_ || workspace_ == nullptr) return 0;
    return workspace_->snapshot().activeDifficultyId;
}

bool QmlDocumentModel::save()
{
    if (fileService_ == nullptr) return false;
    const miacode::v2::ChartWorkspaceFileResult result =
        fileService_->save(saveSectionDifficultyId());
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
        // Control comments (a bare 拍号, or an empty `||`) are chart data, not
        // sections, so the outline skips them the way the Widgets one did.
        if (!bookmark.has_value() || bookmark->control) {
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
    input.dirtyDifficultyIds = workspaceSnapshot.dirtyDifficultyIds;
    input.documentRevision = documentRevision_;
    input.validation = validationSnapshot_;
    presentationState_ = miacode::qml_ui::projectDocumentPresentation(input);
}

bool QmlDocumentModel::saveToPath(const QString& path)
{
    if (fileService_ == nullptr || path.trimmed().isEmpty()) return false;
    // 另存为 writes a whole document deliberately: the new file has no earlier
    // content for the other difficulties to be left at, so saving one section
    // there would drop the rest of the chart on the floor.
    const miacode::v2::ChartWorkspaceFileResult result = fileService_->saveAs(path, 0);
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

namespace {

miacode::chart_transform::ChartNormalizationOptions normalizeOptionsFromVariant(
    const QVariantMap& options)
{
    miacode::chart_transform::ChartNormalizationOptions parsed;
    parsed.startAtNewMeasure = true;
    parsed.reduceTo384Grid = options.value(QStringLiteral("reduceTo384Grid"), true).toBool();
    parsed.sectionMeasureCount = options.value(QStringLiteral("sectionMeasureCount"), 4).toInt();
    // The Widgets dialog derived this from the sectioning choice rather than
    // carrying it separately; keeping it derived stops QML producing a
    // combination the engine never saw from that path.
    parsed.splitEveryFourMeasures = parsed.sectionMeasureCount == 4;
    parsed.syntax = options.value(QStringLiteral("syntax")).toString() == QStringLiteral("hinata")
        ? miacode::chart_transform::ChartNormalizationSyntax::Hinata
        : miacode::chart_transform::ChartNormalizationSyntax::Fpd;
    return parsed;
}

}  // namespace

QStringList QmlDocumentModel::chartTransformIds() const
{
    QStringList ids;
    for (const miacode::qml_ui::ChartTransformSpec& spec : miacode::qml_ui::chartTransformSpecs()) {
        ids.append(spec.id);
    }
    return ids;
}

QVariantList QmlDocumentModel::chartTransformMenu() const
{
    QVariantList rows;
    for (const miacode::qml_ui::ChartTransformSpec& spec : miacode::qml_ui::chartTransformSpecs()) {
        rows.append(QVariantMap{
            {QStringLiteral("id"), spec.id},
            {QStringLiteral("label"), UiText::text(spec.labelKey)},
            {QStringLiteral("section"), spec.section},
        });
    }
    return rows;
}

QString QmlDocumentModel::chartTransformMoreLabel() const
{
    return UiText::text(QStringLiteral("action.transform.more"));
}

QVariantMap QmlDocumentModel::transformChartSelection(
    const QString& text, int anchor, int position, const QString& opId) const
{
    QVariantMap transaction;
    transaction.insert(QStringLiteral("consumed"), false);
    transaction.insert(QStringLiteral("hasEdit"), false);
    transaction.insert(QStringLiteral("undoGroup"), true);
    transaction.insert(QStringLiteral("changed"), 0);

    const int begin = qBound(0, qMin(anchor, position), text.size());
    const int end = qBound(begin, qMax(anchor, position), text.size());
    if (begin >= end) {
        // Every one of these edits a range, so an empty selection is a
        // no-target, not a whole-chart shortcut.
        transaction.insert(QStringLiteral("error"), QStringLiteral("no_selection"));
        return transaction;
    }

    const auto specs = miacode::qml_ui::chartTransformSpecs();
    const auto spec = std::find_if(specs.cbegin(), specs.cend(),
                                   [&opId](const miacode::qml_ui::ChartTransformSpec& candidate) {
                                       return candidate.id == opId;
                                   });
    if (spec == specs.cend()) {
        transaction.insert(QStringLiteral("error"), QStringLiteral("unknown_transform"));
        return transaction;
    }

    const QString selected = text.mid(begin, end - begin);
    int changed = 0;
    QString replacement;
    if (spec->apply) {
        replacement = spec->apply(selected, text.mid(end), &changed);
    } else {
        const QString transformedFull =
            miacode::chart_transform::clearCompleteElementsInSelection(text, begin, end, &changed);
        // The transform rewrites the whole text; the selection's new extent is
        // whatever is left once the untouched tail is accounted for.
        const int untouchedSuffix = text.size() - end;
        replacement = transformedFull.mid(begin, transformedFull.size() - untouchedSuffix - begin);
    }

    transaction.insert(QStringLiteral("consumed"), true);
    transaction.insert(QStringLiteral("changed"), changed);
    if (replacement == selected) {
        return transaction;
    }

    const int transformedEnd = begin + replacement.size();
    const bool forward = position >= anchor;
    transaction.insert(QStringLiteral("hasEdit"), true);
    transaction.insert(QStringLiteral("replacementStart"), begin);
    transaction.insert(QStringLiteral("replacementEnd"), end);
    transaction.insert(QStringLiteral("replacementText"), replacement);
    transaction.insert(QStringLiteral("anchor"), forward ? begin : transformedEnd);
    transaction.insert(QStringLiteral("position"), forward ? transformedEnd : begin);
    return transaction;
}

QVariantMap QmlDocumentModel::normalizeChartSelection(
    const QString& text, int anchor, int position, const QVariantMap& options) const
{
    // Shaped as one of SourceEditor's editor transactions so the existing apply
    // path records it on the undo stack like any other edit.
    QVariantMap transaction;
    transaction.insert(QStringLiteral("consumed"), false);
    transaction.insert(QStringLiteral("hasEdit"), false);
    transaction.insert(QStringLiteral("undoGroup"), true);
    if (workspace_ == nullptr) {
        transaction.insert(QStringLiteral("error"), QStringLiteral("workspace_unavailable"));
        return transaction;
    }

    const int begin = qBound(0, qMin(anchor, position), text.size());
    const int end = qBound(begin, qMax(anchor, position), text.size());
    // No selection means the whole chart, matching the Widgets entry.
    const int selectionStart = begin == end ? 0 : begin;
    const int selectionEnd = begin == end ? text.size() : end;

    const auto normalized = miacode::chart_transform::normalizeChartSelectionText(
        text,
        selectionStart,
        selectionEnd,
        miacode::simai::buildTimingMetadata(workspace_->document()),
        normalizeOptionsFromVariant(options));
    if (!normalized.ok) {
        transaction.insert(QStringLiteral("error"), normalized.errorMessage);
        return transaction;
    }

    const QString replacement = miacode::chart_transform::composeNormalizedSelectionReplacement(
        text, selectionStart, selectionEnd, normalized.text);
    transaction.insert(QStringLiteral("consumed"), true);
    if (replacement == text.mid(selectionStart, selectionEnd - selectionStart)) {
        // Already normalized: consumed but with no edit, so the caller can say
        // so instead of recording an undo step that changes nothing.
        return transaction;
    }

    const int transformedEnd = selectionStart + replacement.size();
    const bool forward = position >= anchor;
    transaction.insert(QStringLiteral("hasEdit"), true);
    transaction.insert(QStringLiteral("replacementStart"), selectionStart);
    transaction.insert(QStringLiteral("replacementEnd"), selectionEnd);
    transaction.insert(QStringLiteral("replacementText"), replacement);
    transaction.insert(QStringLiteral("anchor"), forward ? selectionStart : transformedEnd);
    transaction.insert(QStringLiteral("position"), forward ? transformedEnd : selectionStart);
    return transaction;
}

QVariantMap QmlDocumentModel::normalizeOptions() const
{
    QVariantMap map;
    if (backend_ == nullptr) {
        return map;
    }
    const auto options = backend_->chartNormalizeOptions();
    map.insert(QStringLiteral("reduceTo384Grid"), options.reduceTo384Grid);
    map.insert(QStringLiteral("sectionMeasureCount"), options.sectionMeasureCount);
    map.insert(
        QStringLiteral("syntax"),
        options.syntax == miacode::chart_transform::ChartNormalizationSyntax::Hinata
            ? QStringLiteral("hinata")
            : QStringLiteral("fpd"));
    return map;
}

void QmlDocumentModel::setNormalizeOptions(const QVariantMap& options)
{
    if (backend_ == nullptr) {
        return;
    }
    backend_->setChartNormalizeOptions(normalizeOptionsFromVariant(options));
}

QVariantList QmlDocumentModel::normalizeGridOptions() const
{
    const auto row = [](bool on, const char* key) {
        QVariantMap option;
        option.insert(QStringLiteral("value"), on);
        option.insert(QStringLiteral("label"), UiText::text(QString::fromLatin1(key)));
        return QVariant(option);
    };
    return QVariantList{
        row(true, "preferences.on"),
        row(false, "preferences.off"),
    };
}

QVariantList QmlDocumentModel::normalizeSectionOptions() const
{
    const auto row = [](int measures, const char* key) {
        QVariantMap option;
        option.insert(QStringLiteral("value"), measures);
        option.insert(QStringLiteral("label"), UiText::text(QString::fromLatin1(key)));
        return QVariant(option);
    };
    return QVariantList{
        row(4, "document.chart_section_every_4_measures"),
        row(2, "document.chart_section_every_2_measures"),
        row(0, "document.chart_section_none"),
    };
}

QVariantList QmlDocumentModel::normalizeSyntaxOptions() const
{
    const auto row = [](const QString& token, const QString& label) {
        QVariantMap option;
        option.insert(QStringLiteral("value"), token);
        option.insert(QStringLiteral("label"), label);
        return QVariant(option);
    };
    return QVariantList{
        row(QStringLiteral("fpd"), QStringLiteral("v1")),
        row(QStringLiteral("hinata"), QStringLiteral("v2")),
    };
}
