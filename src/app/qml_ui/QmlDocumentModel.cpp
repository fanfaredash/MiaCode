#include "ui/UiText.h"
#include "ChartTransformCommands.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "QmlDocumentModel.h"

#include "editor/BookmarkCommentSyntax.h"

#include "app/v2/UiRequestService.h"
#include "common/ChartAssetPaths.h"
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
    miacode::v2::ShellNotifications& notifications, miacode::v2::ChartWorkspace& workspace,
    miacode::v2::ChartWorkspaceFileService& fileService,
    miacode::v2::AnalysisService& analysisService,
    miacode::v2::UiRequestService& uiRequests,
    miacode::v2::DocumentBridge*& bridgeSlot, QObject* parent)
    : QObject(parent)
    , notifications_(&notifications)
    , workspace_(&workspace)
    , fileService_(&fileService)
    , analysisService_(&analysisService)
    , uiRequests_(&uiRequests)
    , bridgeSlot_(&bridgeSlot)
{
    if (!workspace_->snapshot().hasDocument) {
        workspace_->openSource(SimaiDocument::createEmpty().toText());
    }
    bridge()->setDocumentSaveHandler([this](const QString& path) {
        return saveToPath(path);
    });
    bridge()->setLeaveDocumentHandler(
        [this](std::function<void(bool)> onDecided) { requestLeaveDocument(std::move(onDecided)); });
    bridge()->setChartTextHandler([this](const QString& text) {
        if (workspace_ == nullptr) return false;
        setChartText(text);
        return chartText() == text;
    });
    // The workspace already has a document from window startup, or a fresh empty
    // chart opened above. Publish that first committed identity so later
    // navigation values are stamped with the workspace revision.
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
    refreshDocumentState();
    connect(workspace_, &miacode::v2::ChartWorkspace::changed, this, [this](quint64) {
        if (suppressWorkspaceChanged_) return;
        publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
    });
    connect(analysisService_, &miacode::v2::AnalysisService::snapshotChanged,
            this, [this](int, quint64) {
                refreshDocumentState();
                emit syntaxIssuesChanged();
                emit documentStateChanged();
            });
    connect(notifications_, &miacode::v2::ShellNotifications::documentReplaced, this, [this] {
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
                    .arg(bridge() != nullptr ? bridge()->filePath() : QString())
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
    if (bridge() != nullptr) {
        bridge()->setDocumentSaveHandler({});
        bridge()->setChartTextHandler({});
        bridge()->setLeaveDocumentHandler({});
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
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->replaceActiveDifficultyChart(value).accepted;
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}

QString QmlDocumentModel::metadataTitle() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::Title); }
QString QmlDocumentModel::metadataArtist() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::Artist); }
QString QmlDocumentModel::metadataFirst() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::First); }
QString QmlDocumentModel::metadataDesigner() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::Designer); }
QString QmlDocumentModel::metadataVideoPath() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::VideoPath); }
// clock_count has no dedicated SimaiDocument member (unlike title/artist/first/
// designer/videoPath above): it lives in the raw &field=value sequence and is
// defaulted to 4 by SimaiDocument::ensureDefaultClockCount on load, so this
// mirrors wholeBpm() below rather than routing through documentField().
QString QmlDocumentModel::metadataClockCount() const
{
    if (workspace_ == nullptr) return {};
    for (const SimaiRawField& field : workspace_->document().extraFields) {
        if (field.key.compare(QStringLiteral("clock_count"), Qt::CaseInsensitive) == 0) {
            return field.value.trimmed();
        }
    }
    return {};
}
QString QmlDocumentModel::metadataExtraText() const { return documentField(miacode::v2::ChartWorkspaceDocumentField::ExtraText); }
QString QmlDocumentModel::wholeBpm() const
{
    if (workspace_ == nullptr) return {};
    for (const SimaiRawField& field : workspace_->document().extraFields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) == 0) {
            return field.value.trimmed();
        }
    }
    return {};
}
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
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::Title, value);
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataArtist(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::Artist, value);
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataFirst(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::First, value);
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataDesigner(const QString& value)
{
    if (workspace_ == nullptr) return;
    const bool changed = runWorkspaceMutation([&] {
        return unifiedDesignerEnabled_
            ? workspace_->unifyDesigners(value)
            : workspace_->updateDocumentField(
                  miacode::v2::ChartWorkspaceDocumentField::Designer, value);
    });
    if (!changed) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataVideoPath(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::VideoPath, value);
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataClockCount(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->upsertExtraField(QStringLiteral("clock_count"), value);
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataExtraText(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::ExtraText, value);
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataSourceText(const QString& value)
{
    if (workspace_ == nullptr) return;
    miacode::v2::ChartWorkspaceResult result;
    if (!runWorkspaceMutation([&] {
            result = workspace_->replaceSource(value);
            return true;
        })) {
        return;
    }
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
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDifficultyField(
                currentDifficultyId(), miacode::v2::ChartWorkspaceDifficultyField::Level,
                value);
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setCurrentDifficultyDesigner(const QString& value)
{
    if (workspace_ == nullptr) return;
    const bool changed = runWorkspaceMutation([&] {
        return unifiedDesignerEnabled_
            ? workspace_->unifyDesigners(value)
            : workspace_->updateDifficultyField(
                  currentDifficultyId(), miacode::v2::ChartWorkspaceDifficultyField::Designer,
                  value);
    });
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

QVariantList QmlDocumentModel::recentDocuments()
{
    return bridge() != nullptr ? bridge()->recentDocumentEntries() : QVariantList{};
}

QVariantList QmlDocumentModel::backupDocuments()
{
    return bridge() != nullptr ? bridge()->backupDocumentEntries() : QVariantList{};
}

void QmlDocumentModel::restoreBackup(const QString& path)
{
    if (bridge() != nullptr) {
        bridge()->restoreBackupDocument(path);
    }
}

void QmlDocumentModel::createDocumentFromPickedAudio()
{
    miacode::v2::UiRequestService* const requests = uiRequests_;
    if (requests == nullptr || fileService_ == nullptr) {
        return;
    }
    QStringList patterns;
    for (const QString& extension : miacode::chart_assets::supportedTrackFileExtensions()) {
        patterns << QStringLiteral("*.%1").arg(extension);
    }
    miacode::v2::FileRequest request;
    request.title = tr("选择音频");
    request.nameFilters = QStringList{
        tr("音频文件 (%1)").arg(patterns.join(QLatin1Char(' '))),
        tr("所有文件 (*)"),
    };
    requests->requestFile(request, [this](const QString& audioPath) {
        if (!audioPath.trimmed().isEmpty()) {
            createChartBesideAudio(QDir::cleanPath(audioPath));
        }
    });
}

void QmlDocumentModel::createChartBesideAudio(const QString& audioPath)
{
    miacode::v2::UiRequestService* const requests = uiRequests_;
    if (requests == nullptr) {
        return;
    }
    // The chart is created where the audio already lives — beside it, not in a
    // folder made for it. A chart IS its folder, and the audio's folder is
    // already that folder as far as the user is concerned.
    const QString targetPath =
        QFileInfo(audioPath).absoluteDir().filePath(QStringLiteral("maidata.txt"));
    if (!QFileInfo::exists(targetPath)) {
        ensureTrackCopyThenCreate(audioPath, targetPath);
        return;
    }
    requests->requestConfirmation(
        UiText::text(QStringLiteral("document.file_already_exists")),
        UiText::text(QStringLiteral("document.maidata_txt_already_exists_in")),
        UiText::text(QStringLiteral("action.yes")),
        [this, audioPath, targetPath](bool accepted) {
            if (accepted) {
                ensureTrackCopyThenCreate(audioPath, targetPath);
            }
        });
}

void QmlDocumentModel::ensureTrackCopyThenCreate(
    const QString& audioPath, const QString& targetPath)
{
    miacode::v2::UiRequestService* const requests = uiRequests_;
    const QFileInfo audioInfo(audioPath);
    const QString extension = audioInfo.suffix().toLower();
    const QString trackName = QStringLiteral("track.%1").arg(extension);

    // Already named track.<ext>: nothing to copy, and copying would mean
    // copying a file onto itself.
    if (audioInfo.fileName().compare(trackName, Qt::CaseInsensitive) == 0) {
        createEmptyDocumentAt(targetPath);
        return;
    }

    const QString trackPath = audioInfo.absoluteDir().filePath(trackName);
    const auto copyThenCreate = [this, audioPath, trackPath, targetPath, requests]() {
        if (QFileInfo::exists(trackPath) && !QFile::remove(trackPath)) {
            if (requests != nullptr) {
                requests->postNotice(
                    miacode::v2::NoticeSeverity::Error, tr("新建失败"),
                    tr("无法替换：\n%1").arg(QDir::toNativeSeparators(trackPath)));
            }
            return;
        }
        if (!QFile::copy(audioPath, trackPath)) {
            if (requests != nullptr) {
                requests->postNotice(
                    miacode::v2::NoticeSeverity::Error, tr("新建失败"),
                    tr("无法写入：\n%1").arg(QDir::toNativeSeparators(trackPath)));
            }
            return;
        }
        // The engine resolves a track by trying track.mp3, .wav, .flac, .ogg in
        // that order, so a copy landing on a later extension while an earlier
        // one exists would leave the chart playing the other file. Say so
        // rather than let it be discovered during playback.
        const QString resolved = miacode::chart_assets::resolveTrackPathForDirectory(
            QFileInfo(trackPath).absolutePath());
        if (requests != nullptr && !resolved.isEmpty()
            && QFileInfo(resolved) != QFileInfo(trackPath)) {
            requests->postNotice(
                miacode::v2::NoticeSeverity::Warning, tr("音轨可能不是刚选的那个"),
                tr("文件夹里已有 %1，谱面会优先使用它，而不是 %2。")
                    .arg(QFileInfo(resolved).fileName(), QFileInfo(trackPath).fileName()));
        }
        createEmptyDocumentAt(targetPath);
    };

    if (!QFileInfo::exists(trackPath)) {
        copyThenCreate();
        return;
    }
    if (requests == nullptr) {
        return;
    }
    requests->requestConfirmation(
        UiText::text(QStringLiteral("document.file_already_exists")),
        tr("%1 已存在。用「%2」替换它吗？")
            .arg(trackName, audioInfo.fileName()),
        UiText::text(QStringLiteral("action.yes")),
        [copyThenCreate](bool accepted) {
            if (accepted) {
                copyThenCreate();
            }
        });
}

void QmlDocumentModel::createEmptyDocumentAt(const QString& targetPath)
{
    miacode::v2::UiRequestService* const requests = uiRequests_;
    if (fileService_ == nullptr) {
        return;
    }
    if (!fileService_->createEmptyDocument(targetPath).accepted) {
        if (requests != nullptr) {
            requests->postNotice(
                miacode::v2::NoticeSeverity::Error,
                UiText::text(QStringLiteral("document.file_already_exists")),
                tr("无法写入文件：\n%1").arg(QDir::toNativeSeparators(targetPath)));
        }
        return;
    }
    // Written, then opened the same way any chart is: the workspace parses it
    // and takes the fresh file as its save point, so a new document starts
    // clean rather than dirty-on-arrival.
    if (!openFile(QUrl::fromLocalFile(targetPath))) {
        return;
    }
    if (bridge() != nullptr) {
        bridge()->noteRecentDocument(targetPath);
    }
}

void QmlDocumentModel::closeDocument()
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] { return workspace_->closeDocument().accepted; })) return;
    unifiedDesignerEnabled_ = false;
    clearMetadataSourceRejection();
    publishWorkspaceCommit(WorkspaceCommitKind::Open, true);
}

bool QmlDocumentModel::saveDifficultySection(int difficultyId)
{
    if (fileService_ == nullptr) return false;
    if (!runWorkspaceMutation([&] { return fileService_->save(difficultyId).accepted; })) {
        emit operationFailed(tr("保存失败"), tr("无法写入谱面文件。"));
        return false;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

bool QmlDocumentModel::revertDifficultyChart(int difficultyId)
{
    if (workspace_ == nullptr) return false;
    if (!runWorkspaceMutation([&] {
            return workspace_->revertDifficultyChart(difficultyId).accepted;
        })) {
        return false;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::SourceReplacement, true);
    return true;
}

bool QmlDocumentModel::openFile(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (fileService_ == nullptr) return false;
    miacode::v2::ChartWorkspaceFileResult result;
    if (!runWorkspaceMutation([&] {
            result = fileService_->open(path);
            return result.accepted;
        })) {
        emit operationFailed(tr("打开失败"), tr("无法打开谱面文件。"));
        return false;
    }
    if (!result.issues.isEmpty()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("editor/document_open"),
            QStringLiteral("reason=open_file issues=%1 path=%2 error=%3")
                .arg(result.issues.size())
                .arg(path)
                .arg(result.error));
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

void QmlDocumentModel::requestLeaveCurrentField(std::function<void(bool)> onDecided)
{
    const auto finish = [onDecided = std::move(onDecided)](bool mayLeave) {
        if (onDecided) {
            onDecided(mayLeave);
        }
    };
    if (workspace_ == nullptr) {
        finish(false);
        return;
    }

    const miacode::v2::ChartWorkspaceSnapshot snapshot = workspace_->snapshot();
    const int difficultyId = snapshot.activeDifficultyId;
    const bool dirtyCurrentDifficulty = difficultyId > 0
        && snapshot.dirtyDifficultyIds.contains(difficultyId);
    // The metadata page represents the whole source. When a different
    // difficulty is dirty, that is not a metadata edit and must not trigger a
    // second prompt merely because an overlay is being left.
    const bool dirtyCurrentDocument = difficultyId <= 0
        && snapshot.dirty
        && snapshot.dirtyDifficultyIds.isEmpty();
    if (!dirtyCurrentDifficulty && !dirtyCurrentDocument) {
        finish(true);
        return;
    }

    if (uiRequests_ == nullptr) {
        // Refuse rather than allowing a page switch to hide edits when the QML
        // request host is unavailable.
        finish(false);
        return;
    }

    const QString fieldName = difficultyId > 0
        ? SimaiDocument::difficultyName(difficultyId)
        : UiText::text(QStringLiteral("dialog.unsaved_field_changes.field.metadata"));
    uiRequests_->requestChoice(
        UiText::text(QStringLiteral("dialog.unsaved_field_changes.title")),
        UiText::text(QStringLiteral("dialog.unsaved_field_changes.message")).arg(fieldName),
        unsavedSectionChoices(),
        QStringLiteral("cancel"),
        [this, difficultyId, finish](const QString& choiceId) mutable {
            if (choiceId == QLatin1String("cancel")) {
                finish(false);
                return;
            }
            if (choiceId == QLatin1String("save")) {
                saveSectionOrAskForPath(difficultyId, std::move(finish));
                return;
            }

            if (difficultyId > 0) {
                if (!runWorkspaceMutation([&] {
                        return workspace_->revertDifficultyChart(difficultyId).accepted;
                    })) {
                    finish(false);
                    return;
                }
                publishWorkspaceCommit(WorkspaceCommitKind::SourceReplacement, true);
                finish(true);
                return;
            }

            // There is no durable save point to reload for a never-saved
            // document. Refusing the discard is the only safe answer there.
            if (workspace_->snapshot().filePath.isEmpty()) {
                finish(false);
                return;
            }
            discardChanges();
            finish(!workspace_->snapshot().dirty);
        });
}

void QmlDocumentModel::saveSectionOrAskForPath(
    int difficultyId, std::function<void(bool)> onSaved)
{
    const auto finish = [onSaved = std::move(onSaved)](bool saved) {
        if (onSaved) onSaved(saved);
    };
    if (fileService_ == nullptr || workspace_ == nullptr) {
        finish(false);
        return;
    }
    if (!workspace_->snapshot().filePath.isEmpty()) {
        const bool saved = runWorkspaceMutation(
            [&] { return fileService_->save(difficultyId).accepted; });
        if (saved) {
            publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
        } else {
            emit operationFailed(tr("保存失败"), tr("无法写入谱面文件。"));
        }
        finish(saved);
        return;
    }

    miacode::v2::UiRequestService* const requests = uiRequests_;
    if (requests == nullptr) {
        finish(false);
        return;
    }
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("action.save_as"));
    request.saveMode = true;
    request.nameFilters = QStringList{tr("Simai 文件 (*.txt *.simai)"), tr("所有文件 (*.*)")};
    requests->requestFile(request, [this, finish](const QString& path) {
        if (path.trimmed().isEmpty()) {
            // Cancelling the pick cancels the save, which cancels whatever the
            // save was a step of. Nothing was written.
            finish(false);
            return;
        }
        // A file that does not exist yet has no earlier content for the other
        // difficulties to be left at, so the first write is the whole document.
        const bool saved = runWorkspaceMutation(
            [&] { return fileService_->saveAs(path, 0).accepted; });
        if (saved) {
            publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
        } else {
            emit operationFailed(tr("保存失败"), tr("无法写入谱面文件。"));
        }
        finish(saved);
    });
}

void QmlDocumentModel::requestSaveDifficultySection(int difficultyId)
{
    saveSectionOrAskForPath(difficultyId, [this, difficultyId](bool saved) {
        emit sectionSaveFinished(difficultyId, saved);
    });
}

void QmlDocumentModel::askNextDirtySection(std::function<void(bool)> onDecided)
{
    miacode::v2::UiRequestService* const requests = uiRequests_;
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
                saveSectionOrAskForPath(
                    difficultyId, [this, onDecided = std::move(onDecided)](bool saved) mutable {
                        if (!saved) {
                            // Nothing was written, so leaving would lose it.
                            if (onDecided) onDecided(false);
                            return;
                        }
                        askNextDirtySection(std::move(onDecided));
                    });
                return;
            }
            if (!runWorkspaceMutation([&] {
                    return workspace_->revertDifficultyChart(difficultyId).accepted;
                })) {
                if (onDecided) onDecided(false);
                return;
            }
            publishWorkspaceCommit(WorkspaceCommitKind::SourceReplacement, true);
            // That difficulty is no longer among the dirty ones, so this walks
            // the list down rather than around it.
            askNextDirtySection(std::move(onDecided));
        });
}

void QmlDocumentModel::askAboutRemainingDocument(std::function<void(bool)> onDecided)
{
    miacode::v2::UiRequestService* const requests = uiRequests_;
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
                saveSectionOrAskForPath(0, [onDecided](bool saved) {
                    if (onDecided) onDecided(saved);
                });
                return;
            }
            if (!workspace_->snapshot().filePath.isEmpty()) {
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
    if (!runWorkspaceMutation([&] {
            return fileService_->save(saveSectionDifficultyId()).accepted;
        })) {
        emit operationFailed(tr("保存失败"), tr("无法写入谱面文件。"));
        return false;
    }
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
    miacode::v2::ChartWorkspaceFileResult result;
    if (!runWorkspaceMutation([&] {
            result = fileService_->open(path);
            return result.accepted;
        })) {
        return;
    }
    clearMetadataSourceRejection();
    publishWorkspaceCommit(
        WorkspaceCommitKind::Open, true, result.usedSystemEncoding);
}
void QmlDocumentModel::selectDifficulty(int id)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] { return workspace_->selectDifficulty(id); })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::DifficultySelection);
}
bool QmlDocumentModel::addDifficulty(int id)
{
    if (workspace_ == nullptr) return false;
    if (!runWorkspaceMutation([&] { return workspace_->addDifficulty(id); })) return false;
    publishWorkspaceCommit(WorkspaceCommitKind::Structure);
    return true;
}
bool QmlDocumentModel::removeDifficulty(int id)
{
    if (workspace_ == nullptr) return false;
    if (!runWorkspaceMutation([&] { return workspace_->removeDifficulty(id); })) return false;
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
    runWorkspaceMutation([&] { return workspace_->unifyDesigners(canonicalName); });
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
        if (bridge() != nullptr && difficultyId == currentDifficultyId()) {
            bridge()->requestEditorNavigation(line, 1, line, 1, false, true, true);
        }
    }, Qt::QueuedConnection);
}

bool QmlDocumentModel::runWorkspaceMutation(const std::function<bool()>& mutate)
{
    suppressWorkspaceChanged_ = true;
    const bool ok = mutate();
    suppressWorkspaceChanged_ = false;
    return ok;
}

void QmlDocumentModel::publishWorkspaceCommit(
    WorkspaceCommitKind kind, bool replacement, bool usedSystemEncoding)
{
    Q_UNUSED(kind);
    Q_UNUSED(usedSystemEncoding);
    if (workspace_ == nullptr) return;
    if (replacement) {
        ++documentGeneration_;
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
    if (!runWorkspaceMutation([&] { return fileService_->saveAs(path, 0).accepted; })) {
        emit operationFailed(tr("保存失败"), tr("无法写入谱面文件。"));
        return false;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

void QmlDocumentModel::adoptBackendDocumentReplacement()
{
    ++documentGeneration_;
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
    if (bridge() == nullptr) {
        return map;
    }
    const auto options = bridge()->normalizationOptions();
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
    if (bridge() == nullptr) {
        return;
    }
    bridge()->setNormalizationOptions(normalizeOptionsFromVariant(options));
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
