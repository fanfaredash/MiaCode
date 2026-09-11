#include "document/ChartTransformCommands.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "core/chart/selection/ChartSelectionBeatSummary.h"
#include "document/DocumentModel.h"

#include "editor/BookmarkCommentSyntax.h"

#include "app/services/UiRequestService.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/Id3TagReader.h"
#include "common/ProjectPreferences.h"
#include "core/chart/document/SimaiDocument.h"

#include <algorithm>
#include <functional>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QVariantMap>
#include <QCoreApplication>


namespace miacode::ui {
namespace {

constexpr const char* kUnifiedDesignerPrefKey = "unified_designer_enabled";

void writeUnifiedDesignerPreference(const QString& chartPath, bool enabled)
{
    if (chartPath.isEmpty()) return;
    QJsonObject preferences = miacode::project_preferences::load(chartPath);
    preferences[QLatin1String(kUnifiedDesignerPrefKey)] = enabled;
    miacode::project_preferences::save(chartPath, preferences);
}

// 保存 / 放弃 / 取消, in button order. Ids match what the prompts branch on.
QVariantList unsavedSectionChoices()
{
    const auto choice = [](const char* id, const char* labelKey, const char* role) {
        return QVariantMap{
            {QStringLiteral("id"), QLatin1String(id)},
            {QStringLiteral("label"), qtTrId(labelKey)},
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


DocumentModel::DocumentModel(
    miacode::ShellNotifications& notifications, miacode::ChartWorkspace& workspace,
    miacode::ChartWorkspaceFileService& fileService,
    miacode::AnalysisService& analysisService,
    miacode::UiRequestService& uiRequests,
    miacode::DocumentBridge*& bridgeSlot,
    miacode::PreviewSurface*& previewSlot, QObject* parent)
    : QObject(parent)
    , notifications_(&notifications)
    , workspace_(&workspace)
    , fileService_(&fileService)
    , analysisService_(&analysisService)
    , uiRequests_(&uiRequests)
    , bridgeSlot_(&bridgeSlot)
    , previewSlot_(&previewSlot)
{
    metadataSaveTimer_.setSingleShot(true);
    connect(&metadataSaveTimer_, &QTimer::timeout, this, [this] {
        saveMetadataImmediately();
    });
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
    refreshUnifiedDesignerState();
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
    refreshDocumentState();
    connect(workspace_, &miacode::ChartWorkspace::changed, this, [this](quint64) {
        if (suppressWorkspaceChanged_) return;
        publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
    });
    connect(analysisService_, &miacode::AnalysisService::snapshotChanged,
            this, [this](int, quint64) {
                refreshDocumentState();
                emit syntaxIssuesChanged();
                emit documentStateChanged();
            });
    connect(notifications_, &miacode::ShellNotifications::documentReplaced, this, [this] {
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

DocumentModel::~DocumentModel()
{
    if (bridge() != nullptr) {
        bridge()->setDocumentSaveHandler({});
        bridge()->setChartTextHandler({});
        bridge()->setLeaveDocumentHandler({});
    }
}

QString DocumentModel::chartText() const
{
    if (workspace_ == nullptr) return {};
    const SimaiDifficultyData* difficulty =
        workspace_->document().difficulty(currentDifficultyId());
    return difficulty != nullptr ? difficulty->chart : QString();
}

void DocumentModel::setChartText(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->replaceActiveDifficultyChart(value).accepted;
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}

QString DocumentModel::metadataTitle() const
{
    return documentField(miacode::ChartWorkspaceDocumentField::Title);
}
QString DocumentModel::metadataArtist() const
{
    return documentField(miacode::ChartWorkspaceDocumentField::Artist);
}
QString DocumentModel::metadataFirst() const
{
    return documentField(miacode::ChartWorkspaceDocumentField::First);
}
QString DocumentModel::metadataDesigner() const
{
    return documentField(miacode::ChartWorkspaceDocumentField::Designer);
}
QString DocumentModel::metadataVideoPath() const
{
    return documentField(miacode::ChartWorkspaceDocumentField::VideoPath);
}
bool DocumentModel::metadataHasVideo() const
{
    return !miacode::chart_assets::resolveChartVideoPath(
        currentFilePath(), metadataVideoPath()).isEmpty();
}
QString DocumentModel::metadataClockCount() const
{
    if (workspace_ == nullptr) return {};
    for (const SimaiRawField& field : workspace_->document().extraFields) {
        if (field.key.compare(QStringLiteral("clock_count"), Qt::CaseInsensitive) == 0)
            return field.value;
    }
    return {};
}
QString DocumentModel::metadataExtraText() const
{
    if (workspace_ == nullptr) return {};
    QVector<SimaiRawField> fields;
    for (const SimaiRawField& field : workspace_->document().extraFields) {
        if (field.key.compare(QStringLiteral("clock_count"), Qt::CaseInsensitive) != 0)
            fields.append(field);
    }
    return SimaiDocument::serializeRawFields(fields);
}
bool DocumentModel::metadataNeedsAttention() const
{
    return !metadataAttentionItems().isEmpty();
}
QString DocumentModel::metadataAttentionText() const
{
    const QStringList items = metadataAttentionItems();
    return items.isEmpty() ? QString()
        : qtTrId("metadata.needs_attention").arg(items.join(QStringLiteral("、")));
}
QStringList DocumentModel::metadataAttentionItems() const
{
    QStringList items;
    if (metadataTitle().trimmed().isEmpty())
        items.append(qtTrId("metadata.field.title"));
    if (metadataArtist().trimmed().isEmpty())
        items.append(qtTrId("metadata.field.artist"));
    if (metadataDesigner().trimmed().isEmpty())
        items.append(qtTrId("metadata.field.des"));
    if (!miacode::chart_assets::hasChartBackgroundMedia(currentFilePath(), metadataVideoPath()))
        items.append(qtTrId("metadata.field.cover"));
    return items;
}
QString DocumentModel::wholeBpm() const
{
    if (workspace_ == nullptr) return {};
    for (const SimaiRawField& field : workspace_->document().extraFields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) == 0) {
            return field.value.trimmed();
        }
    }
    return {};
}
bool DocumentModel::unifiedDesignerEnabled() const { return unifiedDesignerEnabled_; }

QVariantList DocumentModel::designerSlots() const
{
    QVariantList result;
    if (workspace_ == nullptr) return result;
    for (int id = 1; id <= 7; ++id) {
        result.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), SimaiDocument::difficultyName(id)},
            {QStringLiteral("designer"), workspace_->document().designerForSlot(id)},
            {QStringLiteral("hasChart"), workspace_->document().difficulty(id) != nullptr},
        });
    }
    return result;
}

void DocumentModel::setMetadataTitle(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::ChartWorkspaceDocumentField::Title, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void DocumentModel::setMetadataArtist(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::ChartWorkspaceDocumentField::Artist, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void DocumentModel::setMetadataFirst(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::ChartWorkspaceDocumentField::First, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void DocumentModel::setMetadataDesigner(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::ChartWorkspaceDocumentField::Designer, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void DocumentModel::setMetadataVideoPath(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::ChartWorkspaceDocumentField::VideoPath, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void DocumentModel::setMetadataClockCount(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->upsertExtraField(QStringLiteral("clock_count"), value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void DocumentModel::setMetadataExtraText(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->replaceExtraFields(value, metadataClockCount()).accepted;
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}

void DocumentModel::requestMetadataAudio(std::function<void(const QString&)> onSelected)
{
    if (uiRequests_ == nullptr) return;
    miacode::FileRequest request;
    request.title = qtTrId("track_metadata.read_from_audio");
    request.startPath = currentFilePath();
    request.nameFilters = QStringList{
        qtTrId("track_metadata.metadata_audio_file_filter"),
        qtTrId("track_metadata.all_files"),
    };
    uiRequests_->requestFile(request, [callback = std::move(onSelected)](const QString& path) {
        if (callback && !path.trimmed().isEmpty()) callback(QDir::cleanPath(path));
    });
}

void DocumentModel::readTitleFromAudioFile()
{
    requestMetadataAudio([this](const QString& audioPath) {
        const miacode::id3::Tag tag = miacode::id3::readTagFromFile(audioPath);
        if (uiRequests_ == nullptr) return;
        if (!tag.valid) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Information,
                qtTrId("track_metadata.read_title_from_mp3"),
                qtTrId("track_metadata.no_id3v2_tag_was_found"));
            return;
        }
        const QString value = tag.title.trimmed();
        if (value.isEmpty()) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Information,
                qtTrId("track_metadata.read_title_from_mp3"),
                qtTrId("track_metadata.the_selected_mp3_s_id3")
                    .arg(qtTrId("track_metadata.title")));
            return;
        }
        setMetadataTitle(value);
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Information,
            qtTrId("track_metadata.read_title_from_mp3"),
            qtTrId("track_metadata.loaded_title_from_mp3"));
    });
}

void DocumentModel::readArtistFromAudioFile()
{
    requestMetadataAudio([this](const QString& audioPath) {
        const miacode::id3::Tag tag = miacode::id3::readTagFromFile(audioPath);
        if (uiRequests_ == nullptr) return;
        if (!tag.valid) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Information,
                qtTrId("track_metadata.read_artist_from_mp3"),
                qtTrId("track_metadata.no_id3v2_tag_was_found"));
            return;
        }
        const QString value = tag.artist.trimmed();
        if (value.isEmpty()) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Information,
                qtTrId("track_metadata.read_artist_from_mp3"),
                qtTrId("track_metadata.the_selected_mp3_s_id3")
                    .arg(qtTrId("track_metadata.artist")));
            return;
        }
        setMetadataArtist(value);
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Information,
            qtTrId("track_metadata.read_artist_from_mp3"),
            qtTrId("track_metadata.loaded_artist_from_mp3"));
    });
}

void DocumentModel::extractCoverFromAudioFile()
{
    if (uiRequests_ == nullptr || workspace_ == nullptr) return;
    const QString chartPath = currentFilePath();
    if (chartPath.isEmpty()) {
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Warning,
            qtTrId("metadata.field.cover"),
            qtTrId("media_tools.open_or_save_a_chart"));
        return;
    }
    requestMetadataAudio([this, chartPath](const QString& audioPath) {
        if (uiRequests_ == nullptr) return;
        const QString title = qtTrId("track_metadata.extract_cover_to_bg_jpg");
        const miacode::id3::Tag tag = miacode::id3::readTagFromFile(audioPath);
        if (!tag.valid || tag.pictureBytes.isEmpty()) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Information,
                title,
                qtTrId("track_metadata.the_selected_mp3_has_no"));
            return;
        }
        QImage cover;
        if (!cover.loadFromData(tag.pictureBytes)) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Warning,
                title,
                qtTrId("track_metadata.failed_to_decode_embedded_cover")
                    .arg(tag.pictureMimeType));
            return;
        }

        const QString bgPath = QDir(QFileInfo(chartPath).absolutePath()).filePath(
            QStringLiteral("bg.jpg"));
        const QStringList existingCovers = mediaService_.existingCandidates(
            chartPath, miacode::ChartMediaService::Kind::Image);
        const QString existingBgPath = existingCovers.isEmpty()
            ? QString() : existingCovers.constFirst();
        const bool hasExistingCover = !existingCovers.isEmpty();
        if (hasExistingCover) {
            uiRequests_->requestConfirmation(
                title,
                qtTrId("track_metadata.background_image_exists_overwrite"),
                qtTrId("action.yes"),
                [this, cover, bgPath, existingBgPath, title](bool accepted) {
                    if (accepted) writeExtractedCover(cover, bgPath, existingBgPath, title);
                });
            return;
        }
        writeExtractedCover(cover, bgPath, existingBgPath, title);
    });
}

void DocumentModel::writeExtractedCover(
    const QImage& cover, const QString& bgPath, const QString& existingBgPath,
    const QString& title)
{
    if (uiRequests_ == nullptr) return;
    if (preview() != nullptr) preview()->prepareForMediaFileOperation();
    QString backupPath;
    if (!existingBgPath.isEmpty()) {
        backupPath = miacode::chart_media_import::nextBackupPath(existingBgPath);
        if (!QFile::rename(existingBgPath, backupPath)) {
            if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Error, title,
                qtTrId("track_metadata.failed_to_write_bg_jpg"));
            return;
        }
    }

    QSaveFile output(bgPath);
    output.setDirectWriteFallback(false);
    const bool written = output.open(QIODevice::WriteOnly)
        && cover.save(&output, "JPG", 92)
        && output.commit();
    if (!written) {
        output.cancelWriting();
        QFile::remove(bgPath);
        if (!backupPath.isEmpty()) QFile::rename(backupPath, existingBgPath);
        if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Error, title,
            qtTrId("track_metadata.failed_to_write_bg_jpg"));
        return;
    }
    if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
    uiRequests_->postNotice(
        miacode::NoticeSeverity::Information,
        title,
        qtTrId(existingBgPath.isEmpty() ? "track_metadata.wrote_bg_jpg_from_the" : "track_metadata.overwrote_bg_jpg_with_embedded"));
}

void DocumentModel::importChartBackgroundImage()
{
    requestChartMediaImport(miacode::ChartMediaService::Kind::Image);
}

void DocumentModel::importChartBackgroundVideo()
{
    requestChartMediaImport(miacode::ChartMediaService::Kind::Video);
}

void DocumentModel::removeChartPv()
{
    if (uiRequests_ == nullptr || workspace_ == nullptr) return;
    const QString chartPath = currentFilePath();
    if (chartPath.isEmpty()) {
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Warning,
            qtTrId("metadata.field.background_video"),
            qtTrId("media_tools.open_or_save_a_chart"));
        return;
    }
    uiRequests_->requestConfirmation(
        qtTrId("track_metadata.delete_pv"),
        qtTrId("track_metadata.delete_pv_confirm"),
        qtTrId("action.yes"),
        [this, chartPath](bool accepted) {
            if (!accepted || workspace_ == nullptr || uiRequests_ == nullptr) return;
            if (preview() != nullptr) preview()->prepareForMediaFileOperation();
            const auto result = mediaService_.removePv(chartPath, metadataVideoPath());
            if (!result.ok) {
                if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
                uiRequests_->postNotice(
                    miacode::NoticeSeverity::Error,
                    qtTrId("track_metadata.delete_pv"),
                    qtTrId("track_metadata.failed_to_remove_media")
                        .arg(result.errorCode));
                return;
            }
            setMetadataVideoPath(QString());
            if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Information,
                qtTrId("track_metadata.delete_pv"),
                qtTrId("track_metadata.deleted_pv"));
        });
}

void DocumentModel::requestChartMediaImport(miacode::ChartMediaService::Kind kind)
{
    if (uiRequests_ == nullptr || workspace_ == nullptr) return;
    const QString chartPath = currentFilePath();
    if (chartPath.isEmpty()) {
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Warning,
            qtTrId("metadata.field.background_video"),
            qtTrId("media_tools.open_or_save_a_chart"));
        return;
    }

    const bool video = kind == miacode::ChartMediaService::Kind::Video;
    miacode::FileRequest request;
    request.title = qtTrId(video ? "track_metadata.import_background_video" : "track_metadata.import_file");
    request.startPath = chartPath;
    request.nameFilters = QStringList{
        qtTrId(video ? "track_metadata.video_file_filter" : "track_metadata.image_file_filter"),
        qtTrId("track_metadata.all_files"),
    };
    uiRequests_->requestFile(request, [this, kind](const QString& sourcePath) {
        if (sourcePath.trimmed().isEmpty() || uiRequests_ == nullptr) return;
        if (!miacode::ChartMediaService::sourceIsSupported(sourcePath, kind)) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Warning,
                qtTrId("track_metadata.unsupported_media_file"),
                kind == miacode::ChartMediaService::Kind::Image
                    ? qtTrId("track_metadata.failed_to_read_image")
                    : qtTrId("track_metadata.unsupported_media_file"));
            return;
        }
        const QString target = miacode::ChartMediaService::targetPath(
            currentFilePath(), sourcePath, kind);
        bool replacesExisting = false;
        for (const QString& candidate : miacode::ChartMediaService::existingCandidates(
                 currentFilePath(), kind)) {
            if (miacode::ChartMediaService::isConflictingCandidate(
                    candidate, sourcePath, target)) {
                replacesExisting = true;
                break;
            }
        }
        if (!replacesExisting) {
            applyChartMediaImport(sourcePath, kind);
            return;
        }
        const bool replacingVideo = kind == miacode::ChartMediaService::Kind::Video;
        uiRequests_->requestConfirmation(
            qtTrId(replacingVideo ? "track_metadata.import_background_video" : "track_metadata.import_file"),
            qtTrId(replacingVideo ? "track_metadata.background_video_exists_overwrite" : "track_metadata.background_image_exists_overwrite"),
            qtTrId("action.yes"),
            [this, sourcePath, kind](bool accepted) {
                if (accepted) applyChartMediaImport(sourcePath, kind);
            });
    });
}

void DocumentModel::applyChartMediaImport(
    const QString& sourcePath, miacode::ChartMediaService::Kind kind)
{
    if (workspace_ == nullptr || uiRequests_ == nullptr) return;
    const QString chartPath = currentFilePath();
    if (preview() != nullptr) preview()->prepareForMediaFileOperation();
    const auto result = mediaService_.importMedia(chartPath, sourcePath, kind);
    if (!result.ok) {
        if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Error,
            qtTrId("track_metadata.failed_to_import_media"),
            qtTrId("track_metadata.failed_to_import_media")
                .arg(result.errorCode));
        return;
    }
    if (kind == miacode::ChartMediaService::Kind::Video) {
        setMetadataVideoPath(QStringLiteral("pv.mp4"));
    }
    if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
    const bool video = kind == miacode::ChartMediaService::Kind::Video;
    if (!result.warnings.isEmpty()) {
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Warning,
            qtTrId("track_metadata.import_file"),
            result.warnings.join(QLatin1Char('\n')));
    }
    uiRequests_->postNotice(
        miacode::NoticeSeverity::Information,
        qtTrId(video ? "track_metadata.import_background_video" : "track_metadata.import_file"),
        qtTrId(video ? "track_metadata.imported_background_video" : "track_metadata.imported_background_image")
            .arg(result.targetPath));
}

QString DocumentModel::documentTitle() const
{
    const QString title = documentField(miacode::ChartWorkspaceDocumentField::Title);
    const QString chartTitle = title.trimmed().isEmpty() ? currentFileName() : title;
    const QString difficulty = currentDifficultyId() > 0 ? currentDifficultyLabel() : QString();
    return difficulty.isEmpty() ? chartTitle
        : QStringLiteral("%1 — %2").arg(chartTitle, difficulty);
}
QString DocumentModel::currentFilePath() const
{
    return workspace_ != nullptr ? workspace_->snapshot().filePath : QString();
}
QString DocumentModel::currentFileName() const
{
    return currentFilePath().isEmpty()
        ? qtTrId("document.untitled")
        : QFileInfo(currentFilePath()).fileName();
}
QString DocumentModel::currentDifficultyName() const
{
    return SimaiDocument::difficultyName(currentDifficultyId());
}
QString DocumentModel::currentDifficultyLabel() const
{
    const QString name = currentDifficultyName();
    const QString level = currentDifficultyLevel().trimmed();
    return level.isEmpty() ? name : QStringLiteral("%1 %2").arg(name, level);
}
int DocumentModel::currentDifficultyId() const { return presentationState_.activeDifficultyId; }

QVariantList DocumentModel::difficulties() const
{
    QVariantList result;
    if (workspace_ == nullptr) return result;
    for (int id : workspace_->document().difficultyIds()) {
        const QString name = SimaiDocument::difficultyName(id);
        const QString level = difficultyField(
            id, miacode::ChartWorkspaceDifficultyField::Level);
        const QString designer = difficultyField(
            id, miacode::ChartWorkspaceDifficultyField::Designer);
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

QVariantList DocumentModel::availableDifficulties() const
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

QString DocumentModel::currentDifficultyLevel() const
{
    return difficultyField(
        currentDifficultyId(), miacode::ChartWorkspaceDifficultyField::Level);
}
QString DocumentModel::currentDifficultyDesigner() const
{
    return difficultyField(
        currentDifficultyId(), miacode::ChartWorkspaceDifficultyField::Designer);
}
QString DocumentModel::currentDifficultyOffset() const
{
    return metadataFirst();
}
bool DocumentModel::currentDifficultyLevelMissing() const
{
    return currentDifficultyId() > 0 && currentDifficultyLevel().trimmed().isEmpty();
}
void DocumentModel::setCurrentDifficultyLevel(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDifficultyField(
                currentDifficultyId(), miacode::ChartWorkspaceDifficultyField::Level,
                value);
        })) {
        return;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void DocumentModel::setCurrentDifficultyDesigner(const QString& value)
{
    if (workspace_ == nullptr) return;
    const bool changed = runWorkspaceMutation([&] {
        return workspace_->updateDifficultyField(
                  currentDifficultyId(), miacode::ChartWorkspaceDifficultyField::Designer,
                  value);
    });
    if (!changed) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void DocumentModel::setCurrentDifficultyOffset(const QString& value)
{
    setMetadataFirst(value);
}

QVariantList DocumentModel::syntaxIssues() const
{
    const miacode::ui::DocumentValidationProjection& snapshot = validationSnapshot_;
    QVariantList result;
    result.reserve(snapshot.issues.size() + (currentDifficultyLevelMissing() ? 1 : 0));
    for (const miacode::ui::DocumentValidationProjectionIssue& issue : snapshot.issues) {
        result.append(QVariantMap{
            {QStringLiteral("line"), issue.line},
            {QStringLiteral("column"), issue.column},
            {QStringLiteral("endColumn"), issue.endColumn},
            {QStringLiteral("severity"),
             issue.severity == miacode::ui::DocumentValidationIssueSeverity::Warning
                 ? QStringLiteral("warning")
                 : QStringLiteral("error")},
            {QStringLiteral("message"), issue.message},
            {QStringLiteral("code"), issue.code},
            {QStringLiteral("difficultyId"), presentationState_.activeDifficultyId},
            {QStringLiteral("revision"), QVariant::fromValue<qulonglong>(presentationState_.validationRevision)},
        });
    }
    if (currentDifficultyLevelMissing() && snapshot.available) {
        result.append(QVariantMap{
            {QStringLiteral("line"), 0},
            {QStringLiteral("column"), 0},
            {QStringLiteral("endColumn"), 0},
            {QStringLiteral("severity"), QStringLiteral("error")},
            {QStringLiteral("message"), qtTrId("validation.difficulty_level_missing")},
            {QStringLiteral("code"), QStringLiteral("missing_difficulty_level")},
            {QStringLiteral("difficultyId"), presentationState_.activeDifficultyId},
            {QStringLiteral("revision"), QVariant::fromValue<qulonglong>(presentationState_.validationRevision)},
        });
    }
    return result;
}
int DocumentModel::syntaxIssueCount() const
{
    return validationSnapshot_.issues.size() + (currentDifficultyLevelMissing() ? 1 : 0);
}
int DocumentModel::syntaxErrorCount() const
{
    return validationSnapshot_.errorCount + (currentDifficultyLevelMissing() ? 1 : 0);
}
int DocumentModel::syntaxWarningCount() const
{
    return validationSnapshot_.warningCount;
}
int DocumentModel::parsedNoteCount() const
{
    return validationSnapshot_.parsedNoteCount;
}
qulonglong DocumentModel::documentRevision() const { return presentationState_.documentRevision; }
qulonglong DocumentModel::validationRevision() const { return presentationState_.validationRevision; }
bool DocumentModel::validationPending() const { return presentationState_.validationPending; }
bool DocumentModel::validationAvailable() const { return presentationState_.validationAvailable; }
bool DocumentModel::dirty() const
{
    return presentationState_.dirty;
}
QStringList DocumentModel::dirtyEditorKeys() const
{
    return presentationState_.dirtyEditorKeys;
}
qulonglong DocumentModel::bookmarkGeneration() const { return bookmarkGeneration_; }

QVariantList DocumentModel::recentDocuments()
{
    return bridge() != nullptr ? bridge()->recentDocumentEntries() : QVariantList{};
}

QVariantList DocumentModel::backupDocuments()
{
    return bridge() != nullptr ? bridge()->backupDocumentEntries() : QVariantList{};
}

void DocumentModel::restoreBackup(const QString& path)
{
    if (bridge() != nullptr) {
        bridge()->restoreBackupDocument(path);
    }
}

void DocumentModel::createDocumentFromPickedAudio()
{
    miacode::UiRequestService* const requests = uiRequests_;
    if (requests == nullptr || fileService_ == nullptr) {
        return;
    }
    QStringList patterns;
    for (const QString& extension : miacode::chart_assets::supportedTrackFileExtensions()) {
        patterns << QStringLiteral("*.%1").arg(extension);
    }
    miacode::FileRequest request;
    request.title = qtTrId("document.choose_audio");
    request.nameFilters = QStringList{
        qtTrId("document.audio_files_filter").arg(patterns.join(QLatin1Char(' '))),
        qtTrId("qml.all_files"),
    };
    requests->requestFile(request, [this](const QString& audioPath) {
        if (!audioPath.trimmed().isEmpty()) {
            createChartBesideAudio(QDir::cleanPath(audioPath));
        }
    });
}

void DocumentModel::createChartBesideAudio(const QString& audioPath)
{
    miacode::UiRequestService* const requests = uiRequests_;
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
        qtTrId("document.file_already_exists"),
        qtTrId("document.maidata_txt_already_exists_in"),
        qtTrId("action.yes"),
        [this, audioPath, targetPath](bool accepted) {
            if (accepted) {
                ensureTrackCopyThenCreate(audioPath, targetPath);
            }
        });
}

void DocumentModel::ensureTrackCopyThenCreate(
    const QString& audioPath, const QString& targetPath)
{
    miacode::UiRequestService* const requests = uiRequests_;
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
                    miacode::NoticeSeverity::Error, qtTrId("document.new_failed"),
                    qtTrId("document.cannot_replace").arg(QDir::toNativeSeparators(trackPath)));
            }
            return;
        }
        if (!QFile::copy(audioPath, trackPath)) {
            if (requests != nullptr) {
                requests->postNotice(
                    miacode::NoticeSeverity::Error, qtTrId("document.new_failed"),
                    qtTrId("document.cannot_write").arg(QDir::toNativeSeparators(trackPath)));
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
                miacode::NoticeSeverity::Warning, qtTrId("document.track_may_differ"),
                qtTrId("document.existing_track_preferred")
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
        qtTrId("document.file_already_exists"),
        qtTrId("document.replace_existing_confirm")
            .arg(trackName, audioInfo.fileName()),
        qtTrId("action.yes"),
        [copyThenCreate](bool accepted) {
            if (accepted) {
                copyThenCreate();
            }
        });
}

void DocumentModel::createEmptyDocumentAt(const QString& targetPath)
{
    miacode::UiRequestService* const requests = uiRequests_;
    if (fileService_ == nullptr) {
        return;
    }
    if (!fileService_->createEmptyDocument(targetPath).accepted) {
        if (requests != nullptr) {
            requests->postNotice(
                miacode::NoticeSeverity::Error,
                qtTrId("document.file_already_exists"),
                qtTrId("document.cannot_write_file").arg(QDir::toNativeSeparators(targetPath)));
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

void DocumentModel::closeDocument()
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] { return workspace_->closeDocument().accepted; })) return;
    const bool wasUnified = unifiedDesignerEnabled_;
    unifiedDesignerEnabled_ = false;
    publishWorkspaceCommit(WorkspaceCommitKind::Open, true);
    if (wasUnified) emit unifiedDesignerEnabledChanged();
}

bool DocumentModel::saveDifficultySection(int difficultyId)
{
    if (fileService_ == nullptr) return false;
    if (!runWorkspaceMutation([&] { return fileService_->save(difficultyId).accepted; })) {
        emit operationFailed(qtTrId("document.save_failed"), qtTrId("document.cannot_write_chart"));
        return false;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

bool DocumentModel::revertDifficultyChart(int difficultyId)
{
    if (workspace_ == nullptr || !SimaiDocument::isDifficultyId(difficultyId)) return false;
    if (!runWorkspaceMutation([&] {
            return workspace_->revertDifficultyChart(difficultyId).accepted;
        })) {
        return false;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::SourceReplacement);
    return true;
}

bool DocumentModel::openFile(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (fileService_ == nullptr) return false;
    miacode::ChartWorkspaceFileResult result;
    if (!runWorkspaceMutation([&] {
            result = fileService_->open(path);
            return result.accepted;
        })) {
        emit operationFailed(qtTrId("dialog.open_startup_target.missing.title"), qtTrId("document.cannot_open_chart"));
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
    if (bridge() != nullptr) {
        runWorkspaceMutation([&] {
            bridge()->reconcileUnifiedDocumentDesigner(
                miacode::DocumentBridge::UnifiedDesignerReconcileReason::DocumentOpened);
            return true;
        });
    }
    refreshUnifiedDesignerState();
    publishWorkspaceCommit(
        WorkspaceCommitKind::Open, true, result.usedSystemEncoding);
    return true;
}

void DocumentModel::requestLeaveDocument(std::function<void(bool)> onDecided)
{
    requestLeaveSection(0, std::move(onDecided));
}

void DocumentModel::requestLeaveCurrentField(std::function<void(bool)> onDecided)
{
    // 页面导航保留工作区中的修改，提交当前输入后切换视图。
    if (!closeDecisionPending_) emit editingFinishedRequested();
    if (onDecided) onDecided(workspace_ != nullptr && !closeDecisionPending_);
}

void DocumentModel::saveSectionOrAskForPath(
    int difficultyId, std::function<void(bool)> onSaved)
{
    emit editingFinishedRequested();
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
            writeUnifiedDesignerPreference(currentFilePath(), unifiedDesignerEnabled_);
            publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
        } else {
            emit operationFailed(qtTrId("document.save_failed"), qtTrId("document.cannot_write_chart"));
        }
        finish(saved);
        return;
    }

    miacode::UiRequestService* const requests = uiRequests_;
    if (requests == nullptr) {
        finish(false);
        return;
    }
    miacode::FileRequest request;
    request.title = qtTrId("action.save_as");
    request.saveMode = true;
    request.nameFilters = QStringList{qtTrId("qml.simai_files_txt_simai"), qtTrId("qml.all_files_2")};
    const qulonglong generation = documentGeneration_;
    requests->requestFile(request, [this, difficultyId, generation, finish](const QString& path) {
        if (generation != documentGeneration_ || path.trimmed().isEmpty()) {
            // Cancelling the pick cancels the save, which cancels whatever the
            // save was a step of. Nothing was written.
            finish(false);
            return;
        }
        // 首次保存遵守请求范围，其他难度的修改保留在工作区。
        const bool saved = runWorkspaceMutation(
            [&] { return fileService_->saveAs(path, difficultyId).accepted; });
        if (saved) {
            writeUnifiedDesignerPreference(currentFilePath(), unifiedDesignerEnabled_);
            publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
        } else {
            emit operationFailed(qtTrId("document.save_failed"), qtTrId("document.cannot_write_chart"));
        }
        finish(saved);
    });
}

bool DocumentModel::saveMetadataImmediately()
{
    metadataSaveTimer_.stop();
    if (closeDecisionPending_) return true;
    if (workspace_ == nullptr || !workspace_->metadataDirty()
        || currentFilePath().isEmpty()) return true;
    if (fileService_ == nullptr || !runWorkspaceMutation([&] {
            return fileService_->save(miacode::ChartWorkspace::MetadataSection).accepted;
        })) {
        emit operationFailed(qtTrId("document.save_failed"), qtTrId("document.cannot_write_chart"));
        return false;
    }
    writeUnifiedDesignerPreference(currentFilePath(), workspace_->unifiedDesignerEnabled());
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

void DocumentModel::requestCloseDifficulty(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId)) return;
    requestLeaveSection(difficultyId, [this, difficultyId](bool mayClose) {
        if (mayClose) emit difficultyCloseAccepted(difficultyId);
    });
}

void DocumentModel::requestLeaveSection(int difficultyId, std::function<void(bool)> onDecided)
{
    if (closeDecisionPending_ || workspace_ == nullptr) {
        if (onDecided) onDecided(false);
        return;
    }
    closeDecisionPending_ = true;
    metadataSaveTimer_.stop();
    const auto finish = [this, onDecided = std::move(onDecided)](bool mayLeave) {
        closeDecisionPending_ = false;
        if (workspace_ != nullptr && workspace_->metadataDirty() && !currentFilePath().isEmpty()) {
            metadataSaveTimer_.start();
        }
        if (onDecided) onDecided(mayLeave);
    };
    emit editingFinishedRequested();
    const auto snapshot = workspace_->snapshot();
    const bool wholeDocument = difficultyId == 0;
    if (!wholeDocument && workspace_->document().difficulty(difficultyId) == nullptr) {
        finish(false);
        return;
    }
    const bool dirty = wholeDocument ? snapshot.dirty : snapshot.dirtyDifficultyIds.contains(difficultyId);
    if (!dirty) {
        finish(true);
        return;
    }
    if (uiRequests_ == nullptr) {
        finish(false);
        return;
    }
    const qulonglong generation = documentGeneration_;
    uiRequests_->requestChoice(
        qtTrId(wholeDocument ? "dialog.unsaved_changes.title" : "dialog.unsaved_tab_changes.title"),
        wholeDocument ? qtTrId("dialog.unsaved_changes.message")
                      : qtTrId("dialog.unsaved_tab_changes.message")
                            .arg(SimaiDocument::difficultyName(difficultyId)),
        unsavedSectionChoices(),
        QStringLiteral("cancel"),
        [this, difficultyId, wholeDocument, generation, finish](const QString& choiceId) {
            if (generation != documentGeneration_) {
                finish(false);
                return;
            }
            if (choiceId == QLatin1String("save")) {
                saveSectionOrAskForPath(difficultyId, finish);
                return;
            }
            if (choiceId == QLatin1String("discard")) {
                finish(wholeDocument ? discardChanges() : revertDifficultyChart(difficultyId));
                return;
            }
            finish(false);
        });
}

bool DocumentModel::wholeSourceEditorActive() const { return wholeSourceEditorActive_; }

void DocumentModel::setWholeSourceEditorActive(bool active)
{
    if (wholeSourceEditorActive_ == active) return;
    wholeSourceEditorActive_ = active;
    emit wholeSourceEditorActiveChanged();
}

int DocumentModel::saveSectionDifficultyId() const
{
    if (workspace_ == nullptr) return 0;
    if (wholeSourceEditorActive_) return miacode::ChartWorkspace::MetadataSection;
    const int active = workspace_->snapshot().activeDifficultyId;
    return active > 0 ? active : miacode::ChartWorkspace::MetadataSection;
}

bool DocumentModel::save()
{
    emit editingFinishedRequested();
    if (fileService_ == nullptr) return false;
    const int sectionId = saveSectionDifficultyId();
    if (!runWorkspaceMutation([&] {
            return fileService_->save(sectionId).accepted;
        })) {
        emit operationFailed(qtTrId("document.save_failed"), qtTrId("document.cannot_write_chart"));
        return false;
    }
    writeUnifiedDesignerPreference(currentFilePath(), unifiedDesignerEnabled_);
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

bool DocumentModel::saveWholeDocument()
{
    emit editingFinishedRequested();
    if (fileService_ == nullptr) return false;
    if (!runWorkspaceMutation([&] { return fileService_->save(0).accepted; })) {
        emit operationFailed(qtTrId("document.save_failed"), qtTrId("document.cannot_write_chart"));
        return false;
    }
    writeUnifiedDesignerPreference(currentFilePath(), unifiedDesignerEnabled_);
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}
bool DocumentModel::saveAs(const QUrl& fileUrl)
{
    emit editingFinishedRequested();
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const bool saved = saveToPath(path);
    if (saved) {
        writeUnifiedDesignerPreference(currentFilePath(), unifiedDesignerEnabled_);
        emitDocumentStateChanged();
    }
    return saved;
}
bool DocumentModel::discardChanges()
{
    if (workspace_ == nullptr) return false;
    // Discard must restore the in-memory save point, rather than reopening the
    // file. Reopening would create a new load transaction and would make the
    // close path depend on the still-unsettled v1 unified-designer semantics.
    if (!runWorkspaceMutation([&] {
            return workspace_->revertDifficultyChart(0).accepted;
        })) {
        return false;
    }
    reconcileUnifiedDesignerAfterSourceReplacement();
    refreshUnifiedDesignerState();
    publishWorkspaceCommit(WorkspaceCommitKind::SourceReplacement, true);
    return true;
}
void DocumentModel::selectDifficulty(int id)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] { return workspace_->selectDifficulty(id); })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::DifficultySelection);
}
bool DocumentModel::addDifficulty(int id)
{
    if (workspace_ == nullptr) return false;
    if (!runWorkspaceMutation([&] { return workspace_->addDifficulty(id); })) return false;
    publishWorkspaceCommit(WorkspaceCommitKind::Structure);
    return true;
}
bool DocumentModel::removeDifficulty(int id)
{
    if (workspace_ == nullptr) return false;
    if (!runWorkspaceMutation([&] { return workspace_->removeDifficulty(id); })) return false;
    publishWorkspaceCommit(WorkspaceCommitKind::Structure);
    return true;
}
int DocumentModel::chartPosition(int line, int column) const
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
bool DocumentModel::applyDesignerSlots(
    const QVariantList& slotValues, bool unified, const QString& canonicalName)
{
    if (workspace_ == nullptr) return false;
    QVector<QPair<int, QString>> designerValues;
    for (const QVariant& value : slotValues) {
        const QVariantMap entry = value.toMap();
        const int id = entry.value(QStringLiteral("id")).toInt();
        if (id < 1 || id > 7) continue;
        designerValues.append(qMakePair(id, entry.value(QStringLiteral("designer")).toString()));
    }
    const bool changed = runWorkspaceMutation([&] {
        return bridge() != nullptr
            ? bridge()->applyDocumentDesignerSlots(designerValues, unified, canonicalName)
            : applyDesignerSlotsWithoutBridge(designerValues, unified, canonicalName);
    });
    if (changed) {
        publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
        if (bridge() == nullptr)
            writeUnifiedDesignerPreference(currentFilePath(), unifiedDesignerEnabled_);
    }
    return changed;
}

void DocumentModel::reconcileUnifiedDesignerAfterSourceReplacement()
{
    if (bridge() == nullptr) {
        if (workspace_ != nullptr && workspace_->unifiedDesignerEnabled()
            && !workspace_->document().isUnifiedDesignerTriviallySafe()) {
            workspace_->setUnifiedDesignerEnabled(false);
        }
        return;
    }
    runWorkspaceMutation([&] {
        bridge()->reconcileUnifiedDocumentDesigner(
            miacode::DocumentBridge::UnifiedDesignerReconcileReason::SourceReplaced);
        return true;
    });
}

bool DocumentModel::applyDesignerSlotsWithoutBridge(
    const QVector<QPair<int, QString>>& slotValues, bool unified, const QString& canonicalName)
{
    const bool modeWas = workspace_->unifiedDesignerEnabled();
    if (modeWas) workspace_->setUnifiedDesignerEnabled(false);
    bool changed = false;
    for (const QPair<int, QString>& slot : slotValues) {
        changed = workspace_->setDesignerForSlot(slot.first, slot.second) || changed;
    }
    if (unified) {
        workspace_->setUnifiedDesignerEnabled(true);
        changed = workspace_->unifyDesigners(canonicalName) || changed;
    }
    return changed || modeWas != unified;
}

void DocumentModel::logEditorDocumentState(const QString& reason, int difficultyId,
                                              qulonglong revision, int shownChars)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("editor/document_shown"),
        QStringLiteral("reason=%1 difficulty=%2 revision=%3 shown_chars=%4 "
                       "projected_difficulty=%5 projected_revision=%6 projected_chars=%7")
            .arg(reason)
            .arg(difficultyId)
            .arg(revision)
            .arg(shownChars)
            .arg(currentDifficultyId())
            .arg(documentRevision_)
            .arg(chartText().size()));
}

QVariantList DocumentModel::bookmarksForDifficulty(int difficultyId) const
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

void DocumentModel::navigateToBookmark(int difficultyId, int line)
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

bool DocumentModel::runWorkspaceMutation(const std::function<bool()>& mutate)
{
    suppressWorkspaceChanged_ = true;
    const bool ok = mutate();
    suppressWorkspaceChanged_ = false;
    return ok;
}

void DocumentModel::publishWorkspaceCommit(
    WorkspaceCommitKind kind, bool replacement, bool usedSystemEncoding)
{
    Q_UNUSED(usedSystemEncoding);
    if (workspace_ == nullptr) return;
    if (!closeDecisionPending_ && kind != WorkspaceCommitKind::SavePoint && workspace_->metadataDirty()
        && !currentFilePath().isEmpty()) {
        // 同一事件中的字段修改合并为一次写入，覆盖页面与运行时入口。
        metadataSaveTimer_.start();
    }
    if (replacement) {
        ++documentGeneration_;
    }
    refreshUnifiedDesignerState();
    emitDocumentStateChanged();
    if (replacement) emit documentReplaced();
}

void DocumentModel::emitDocumentStateChanged()
{
    refreshDocumentState();
    emit chartTextChanged();
    emit metadataChanged();
    emit unifiedDesignerEnabledChanged();
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

void DocumentModel::refreshDocumentState()
{
    const miacode::ChartWorkspaceSnapshot workspaceSnapshot =
        workspace_ != nullptr ? workspace_->snapshot()
                              : miacode::ChartWorkspaceSnapshot();
    validationSnapshot_ = analysisService_ != nullptr
        ? miacode::ui::projectDocumentValidation(
              analysisService_->snapshot(), workspaceSnapshot.activeDifficultyId,
              workspaceSnapshot.revision)
        : miacode::ui::DocumentValidationProjection();
    documentRevision_ = workspaceSnapshot.revision;
    miacode::ui::DocumentPresentationInput input;
    input.activeDifficultyId = workspaceSnapshot.activeDifficultyId;
    input.dirty = workspaceSnapshot.dirty;
    input.metadataDirty = workspace_ != nullptr && workspace_->metadataDirty();
    input.dirtyDifficultyIds = workspaceSnapshot.dirtyDifficultyIds;
    input.documentRevision = documentRevision_;
    input.validation = validationSnapshot_;
    presentationState_ = miacode::ui::projectDocumentPresentation(input);
}

bool DocumentModel::saveToPath(const QString& path)
{
    if (fileService_ == nullptr || path.trimmed().isEmpty()) return false;
    if (!runWorkspaceMutation([&] { return fileService_->saveAs(path, 0).accepted; })) {
        emit operationFailed(qtTrId("document.save_failed"), qtTrId("document.cannot_write_chart"));
        return false;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

void DocumentModel::adoptBackendDocumentReplacement()
{
    ++documentGeneration_;
    refreshUnifiedDesignerState();
    emitDocumentStateChanged();
    emit documentReplaced();
}

void DocumentModel::refreshUnifiedDesignerState()
{
    unifiedDesignerEnabled_ = workspace_ != nullptr && workspace_->unifiedDesignerEnabled();
}

QString DocumentModel::documentField(
    miacode::ChartWorkspaceDocumentField field) const
{
    if (workspace_ == nullptr) return {};
    const SimaiDocument& document = workspace_->document();
    switch (field) {
    case miacode::ChartWorkspaceDocumentField::Title:
        return document.title;
    case miacode::ChartWorkspaceDocumentField::Artist:
        return document.artist;
    case miacode::ChartWorkspaceDocumentField::First:
        return document.first;
    case miacode::ChartWorkspaceDocumentField::Designer:
        return document.designer;
    case miacode::ChartWorkspaceDocumentField::VideoPath:
        return document.videoPath;
    }
    return {};
}

QString DocumentModel::difficultyField(
    int difficultyId, miacode::ChartWorkspaceDifficultyField field) const
{
    if (workspace_ == nullptr) return {};
    const SimaiDifficultyData* difficulty = workspace_->document().difficulty(difficultyId);
    if (difficulty == nullptr) return {};
    switch (field) {
    case miacode::ChartWorkspaceDifficultyField::Level:
        return difficulty->level;
    case miacode::ChartWorkspaceDifficultyField::Designer:
        return difficulty->designer;
    }
    return {};
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
    const QString syntax = options.value(QStringLiteral("syntax")).toString().trimmed().toLower();
    parsed.syntax = syntax == QStringLiteral("hinata") || syntax == QStringLiteral("compact_single_line")
        ? miacode::chart_transform::ChartNormalizationSyntax::CompactSingleLine
        : miacode::chart_transform::ChartNormalizationSyntax::SegmentPreserving;
    return parsed;
}

}  // namespace

QStringList DocumentModel::chartTransformIds() const
{
    QStringList ids;
    for (const miacode::ui::ChartTransformSpec& spec : miacode::ui::chartTransformSpecs()) {
        ids.append(spec.id);
    }
    return ids;
}

QVariantList DocumentModel::chartTransformMenu() const
{
    QVariantList rows;
    for (const miacode::ui::ChartTransformSpec& spec : miacode::ui::chartTransformSpecs()) {
        rows.append(QVariantMap{
            {QStringLiteral("id"), spec.id},
            {QStringLiteral("label"), qtTrId(spec.labelKey.toUtf8().constData())},
            {QStringLiteral("section"), spec.section},
        });
    }
    return rows;
}

QString DocumentModel::chartTransformMoreLabel() const
{
    return qtTrId("action.transform.more");
}

QVariantMap DocumentModel::transformChartSelection(
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

    const auto specs = miacode::ui::chartTransformSpecs();
    const auto spec = std::find_if(specs.cbegin(), specs.cend(),
                                   [&opId](const miacode::ui::ChartTransformSpec& candidate) {
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
            opId == QStringLiteral("transform.reset_tap_notes")
                ? miacode::chart_transform::resetTapNotesInSelection(text, begin, end, &changed)
                : miacode::chart_transform::clearCompleteElementsInSelection(text, begin, end, &changed);
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

QVariantMap DocumentModel::selectionBeatSummary(
    const QString& text, int anchor, int position) const
{
    const miacode::chart_selection::ChartSelectionBeatSummary summary =
        miacode::chart_selection::summarizeChartSelectionBeats(text, anchor, position);
    QVariantList parts;
    for (const miacode::chart_selection::ChartSelectionBeatPart& part : summary.parts) {
        parts.append(QVariantMap{
            {QStringLiteral("count"), part.count},
            {QStringLiteral("denominator"), part.denominator},
        });
    }
    return QVariantMap{
        {QStringLiteral("totalCommaCount"), summary.totalCommaCount},
        {QStringLiteral("parts"), parts},
        {QStringLiteral("exact"), summary.exact},
    };
}

QVariantMap DocumentModel::normalizeChartSelection(
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

QVariantMap DocumentModel::normalizeOptions() const
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
        options.syntax == miacode::chart_transform::ChartNormalizationSyntax::CompactSingleLine
            ? QStringLiteral("compact_single_line")
            : QStringLiteral("segment_preserving"));
    return map;
}

void DocumentModel::setNormalizeOptions(const QVariantMap& options)
{
    if (bridge() == nullptr) {
        return;
    }
    bridge()->setNormalizationOptions(normalizeOptionsFromVariant(options));
}

QVariantList DocumentModel::normalizeGridOptions() const
{
    const auto row = [](bool on, const char* key) {
        QVariantMap option;
        option.insert(QStringLiteral("value"), on);
        option.insert(QStringLiteral("label"), qtTrId(key));
        return QVariant(option);
    };
    return QVariantList{
        row(true, "preferences.on"),
        row(false, "preferences.off"),
    };
}

QVariantList DocumentModel::normalizeSectionOptions() const
{
    const auto row = [](int measures, const char* key) {
        QVariantMap option;
        option.insert(QStringLiteral("value"), measures);
        option.insert(QStringLiteral("label"), qtTrId(key));
        return QVariant(option);
    };
    return QVariantList{
        row(4, "document.chart_section_every_4_measures"),
        row(2, "document.chart_section_every_2_measures"),
        row(0, "document.chart_section_none"),
    };
}

QVariantList DocumentModel::normalizeSyntaxOptions() const
{
    const auto row = [](const QString& token, const QString& label) {
        QVariantMap option;
        option.insert(QStringLiteral("value"), token);
        option.insert(QStringLiteral("label"), label);
        return QVariant(option);
    };
    return QVariantList{
        row(QStringLiteral("segment_preserving"),
            qtTrId("dialog.normalize.segment_preserving")),
        row(QStringLiteral("compact_single_line"),
            qtTrId("dialog.normalize.compact_single_line")),
    };
}

} // namespace miacode::ui
