#include "ui/UiText.h"
#include "ChartTransformCommands.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "core/chart/selection/ChartSelectionBeatSummary.h"
#include "QmlDocumentModel.h"

#include "editor/BookmarkCommentSyntax.h"

#include "app/v2/UiRequestService.h"
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
    miacode::v2::DocumentBridge*& bridgeSlot,
    miacode::v2::PreviewSurface*& previewSlot, QObject* parent)
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

QString QmlDocumentModel::metadataTitle() const
{
    return documentField(miacode::v2::ChartWorkspaceDocumentField::Title);
}
QString QmlDocumentModel::metadataArtist() const
{
    return documentField(miacode::v2::ChartWorkspaceDocumentField::Artist);
}
QString QmlDocumentModel::metadataFirst() const
{
    return documentField(miacode::v2::ChartWorkspaceDocumentField::First);
}
QString QmlDocumentModel::metadataDesigner() const
{
    return documentField(miacode::v2::ChartWorkspaceDocumentField::Designer);
}
QString QmlDocumentModel::metadataVideoPath() const
{
    return documentField(miacode::v2::ChartWorkspaceDocumentField::VideoPath);
}
bool QmlDocumentModel::metadataHasVideo() const
{
    return !miacode::chart_assets::resolveChartVideoPath(
        currentFilePath(), metadataVideoPath()).isEmpty();
}
QString QmlDocumentModel::metadataClockCount() const
{
    if (workspace_ == nullptr) return {};
    for (const SimaiRawField& field : workspace_->document().extraFields) {
        if (field.key.compare(QStringLiteral("clock_count"), Qt::CaseInsensitive) == 0)
            return field.value;
    }
    return {};
}
QString QmlDocumentModel::metadataExtraText() const
{
    if (workspace_ == nullptr) return {};
    QVector<SimaiRawField> fields;
    for (const SimaiRawField& field : workspace_->document().extraFields) {
        if (field.key.compare(QStringLiteral("clock_count"), Qt::CaseInsensitive) != 0)
            fields.append(field);
    }
    return SimaiDocument::serializeRawFields(fields);
}
bool QmlDocumentModel::metadataNeedsAttention() const
{
    return !metadataAttentionItems().isEmpty();
}
QString QmlDocumentModel::metadataAttentionText() const
{
    const QStringList items = metadataAttentionItems();
    return items.isEmpty() ? QString()
        : UiText::text(QStringLiteral("metadata.needs_attention")).arg(items.join(QStringLiteral("、")));
}
QStringList QmlDocumentModel::metadataAttentionItems() const
{
    QStringList items;
    if (metadataTitle().trimmed().isEmpty())
        items.append(UiText::text(QStringLiteral("metadata.field.title")));
    if (metadataArtist().trimmed().isEmpty())
        items.append(UiText::text(QStringLiteral("metadata.field.artist")));
    if (metadataDesigner().trimmed().isEmpty())
        items.append(UiText::text(QStringLiteral("metadata.field.des")));
    if (!miacode::chart_assets::hasChartBackgroundMedia(currentFilePath(), metadataVideoPath()))
        items.append(UiText::text(QStringLiteral("metadata.field.cover")));
    return items;
}
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
bool QmlDocumentModel::unifiedDesignerEnabled() const { return unifiedDesignerEnabled_; }

QVariantList QmlDocumentModel::designerSlots() const
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

void QmlDocumentModel::setMetadataTitle(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::Title, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataArtist(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::Artist, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataFirst(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::First, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataDesigner(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::Designer, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataVideoPath(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->updateDocumentField(
                miacode::v2::ChartWorkspaceDocumentField::VideoPath, value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataClockCount(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->upsertExtraField(QStringLiteral("clock_count"), value);
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setMetadataExtraText(const QString& value)
{
    if (workspace_ == nullptr) return;
    if (!runWorkspaceMutation([&] {
            return workspace_->replaceExtraFields(value, metadataClockCount()).accepted;
        })) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}

void QmlDocumentModel::requestMetadataAudio(std::function<void(const QString&)> onSelected)
{
    if (uiRequests_ == nullptr) return;
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("track_metadata.read_from_audio"));
    request.startPath = currentFilePath();
    request.nameFilters = QStringList{
        UiText::text(QStringLiteral("track_metadata.metadata_audio_file_filter")),
        UiText::text(QStringLiteral("track_metadata.all_files")),
    };
    uiRequests_->requestFile(request, [callback = std::move(onSelected)](const QString& path) {
        if (callback && !path.trimmed().isEmpty()) callback(QDir::cleanPath(path));
    });
}

void QmlDocumentModel::readTitleFromAudioFile()
{
    requestMetadataAudio([this](const QString& audioPath) {
        const miacode::id3::Tag tag = miacode::id3::readTagFromFile(audioPath);
        if (uiRequests_ == nullptr) return;
        if (!tag.valid) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Information,
                UiText::text(QStringLiteral("track_metadata.read_title_from_mp3")),
                UiText::text(QStringLiteral("track_metadata.no_id3v2_tag_was_found")));
            return;
        }
        const QString value = tag.title.trimmed();
        if (value.isEmpty()) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Information,
                UiText::text(QStringLiteral("track_metadata.read_title_from_mp3")),
                UiText::text(QStringLiteral("track_metadata.the_selected_mp3_s_id3"))
                    .arg(UiText::text(QStringLiteral("track_metadata.title"))));
            return;
        }
        setMetadataTitle(value);
        uiRequests_->postNotice(
            miacode::v2::NoticeSeverity::Information,
            UiText::text(QStringLiteral("track_metadata.read_title_from_mp3")),
            UiText::text(QStringLiteral("track_metadata.loaded_title_from_mp3")));
    });
}

void QmlDocumentModel::readArtistFromAudioFile()
{
    requestMetadataAudio([this](const QString& audioPath) {
        const miacode::id3::Tag tag = miacode::id3::readTagFromFile(audioPath);
        if (uiRequests_ == nullptr) return;
        if (!tag.valid) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Information,
                UiText::text(QStringLiteral("track_metadata.read_artist_from_mp3")),
                UiText::text(QStringLiteral("track_metadata.no_id3v2_tag_was_found")));
            return;
        }
        const QString value = tag.artist.trimmed();
        if (value.isEmpty()) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Information,
                UiText::text(QStringLiteral("track_metadata.read_artist_from_mp3")),
                UiText::text(QStringLiteral("track_metadata.the_selected_mp3_s_id3"))
                    .arg(UiText::text(QStringLiteral("track_metadata.artist"))));
            return;
        }
        setMetadataArtist(value);
        uiRequests_->postNotice(
            miacode::v2::NoticeSeverity::Information,
            UiText::text(QStringLiteral("track_metadata.read_artist_from_mp3")),
            UiText::text(QStringLiteral("track_metadata.loaded_artist_from_mp3")));
    });
}

void QmlDocumentModel::extractCoverFromAudioFile()
{
    if (uiRequests_ == nullptr || workspace_ == nullptr) return;
    const QString chartPath = currentFilePath();
    if (chartPath.isEmpty()) {
        uiRequests_->postNotice(
            miacode::v2::NoticeSeverity::Warning,
            UiText::text(QStringLiteral("metadata.field.cover")),
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }
    requestMetadataAudio([this, chartPath](const QString& audioPath) {
        if (uiRequests_ == nullptr) return;
        const QString title = UiText::text(QStringLiteral("track_metadata.extract_cover_to_bg_jpg"));
        const miacode::id3::Tag tag = miacode::id3::readTagFromFile(audioPath);
        if (!tag.valid || tag.pictureBytes.isEmpty()) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Information,
                title,
                UiText::text(QStringLiteral("track_metadata.the_selected_mp3_has_no")));
            return;
        }
        QImage cover;
        if (!cover.loadFromData(tag.pictureBytes)) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Warning,
                title,
                UiText::text(QStringLiteral("track_metadata.failed_to_decode_embedded_cover"))
                    .arg(tag.pictureMimeType));
            return;
        }

        const QString bgPath = QDir(QFileInfo(chartPath).absolutePath()).filePath(
            QStringLiteral("bg.jpg"));
        const QStringList existingCovers = mediaService_.existingCandidates(
            chartPath, miacode::v2::ChartMediaService::Kind::Image);
        const QString existingBgPath = existingCovers.isEmpty()
            ? QString() : existingCovers.constFirst();
        const bool hasExistingCover = !existingCovers.isEmpty();
        if (hasExistingCover) {
            uiRequests_->requestConfirmation(
                title,
                UiText::text(QStringLiteral("track_metadata.background_image_exists_overwrite")),
                UiText::text(QStringLiteral("action.yes")),
                [this, cover, bgPath, existingBgPath, title](bool accepted) {
                    if (accepted) writeExtractedCover(cover, bgPath, existingBgPath, title);
                });
            return;
        }
        writeExtractedCover(cover, bgPath, existingBgPath, title);
    });
}

void QmlDocumentModel::writeExtractedCover(
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
                miacode::v2::NoticeSeverity::Error, title,
                UiText::text(QStringLiteral("track_metadata.failed_to_write_bg_jpg")));
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
            miacode::v2::NoticeSeverity::Error, title,
            UiText::text(QStringLiteral("track_metadata.failed_to_write_bg_jpg")));
        return;
    }
    if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
    uiRequests_->postNotice(
        miacode::v2::NoticeSeverity::Information,
        title,
        UiText::text(existingBgPath.isEmpty()
            ? QStringLiteral("track_metadata.wrote_bg_jpg_from_the")
            : QStringLiteral("track_metadata.overwrote_bg_jpg_with_embedded")));
}

void QmlDocumentModel::importChartBackgroundImage()
{
    requestChartMediaImport(miacode::v2::ChartMediaService::Kind::Image);
}

void QmlDocumentModel::importChartBackgroundVideo()
{
    requestChartMediaImport(miacode::v2::ChartMediaService::Kind::Video);
}

void QmlDocumentModel::removeChartPv()
{
    if (uiRequests_ == nullptr || workspace_ == nullptr) return;
    const QString chartPath = currentFilePath();
    if (chartPath.isEmpty()) {
        uiRequests_->postNotice(
            miacode::v2::NoticeSeverity::Warning,
            UiText::text(QStringLiteral("metadata.field.background_video")),
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }
    uiRequests_->requestConfirmation(
        UiText::text(QStringLiteral("track_metadata.delete_pv")),
        UiText::text(QStringLiteral("track_metadata.delete_pv_confirm")),
        UiText::text(QStringLiteral("action.yes")),
        [this, chartPath](bool accepted) {
            if (!accepted || workspace_ == nullptr || uiRequests_ == nullptr) return;
            if (preview() != nullptr) preview()->prepareForMediaFileOperation();
            const auto result = mediaService_.removePv(chartPath, metadataVideoPath());
            if (!result.ok) {
                if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
                uiRequests_->postNotice(
                    miacode::v2::NoticeSeverity::Error,
                    UiText::text(QStringLiteral("track_metadata.delete_pv")),
                    UiText::text(QStringLiteral("track_metadata.failed_to_remove_media"))
                        .arg(result.errorCode));
                return;
            }
            setMetadataVideoPath(QString());
            if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Information,
                UiText::text(QStringLiteral("track_metadata.delete_pv")),
                UiText::text(QStringLiteral("track_metadata.deleted_pv")));
        });
}

void QmlDocumentModel::requestChartMediaImport(miacode::v2::ChartMediaService::Kind kind)
{
    if (uiRequests_ == nullptr || workspace_ == nullptr) return;
    const QString chartPath = currentFilePath();
    if (chartPath.isEmpty()) {
        uiRequests_->postNotice(
            miacode::v2::NoticeSeverity::Warning,
            UiText::text(QStringLiteral("metadata.field.background_video")),
            UiText::text(QStringLiteral("media_tools.open_or_save_a_chart")));
        return;
    }

    const bool video = kind == miacode::v2::ChartMediaService::Kind::Video;
    miacode::v2::FileRequest request;
    request.title = UiText::text(video
        ? QStringLiteral("track_metadata.import_background_video")
        : QStringLiteral("track_metadata.import_file"));
    request.startPath = chartPath;
    request.nameFilters = QStringList{
        UiText::text(video ? QStringLiteral("track_metadata.video_file_filter")
                           : QStringLiteral("track_metadata.image_file_filter")),
        UiText::text(QStringLiteral("track_metadata.all_files")),
    };
    uiRequests_->requestFile(request, [this, kind](const QString& sourcePath) {
        if (sourcePath.trimmed().isEmpty() || uiRequests_ == nullptr) return;
        if (!miacode::v2::ChartMediaService::sourceIsSupported(sourcePath, kind)) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Warning,
                UiText::text(QStringLiteral("track_metadata.unsupported_media_file")),
                kind == miacode::v2::ChartMediaService::Kind::Image
                    ? UiText::text(QStringLiteral("track_metadata.failed_to_read_image"))
                    : UiText::text(QStringLiteral("track_metadata.unsupported_media_file")));
            return;
        }
        const QString target = miacode::v2::ChartMediaService::targetPath(
            currentFilePath(), sourcePath, kind);
        bool replacesExisting = false;
        for (const QString& candidate : miacode::v2::ChartMediaService::existingCandidates(
                 currentFilePath(), kind)) {
            if (miacode::v2::ChartMediaService::isConflictingCandidate(
                    candidate, sourcePath, target)) {
                replacesExisting = true;
                break;
            }
        }
        if (!replacesExisting) {
            applyChartMediaImport(sourcePath, kind);
            return;
        }
        const bool replacingVideo = kind == miacode::v2::ChartMediaService::Kind::Video;
        uiRequests_->requestConfirmation(
            UiText::text(replacingVideo ? QStringLiteral("track_metadata.import_background_video")
                               : QStringLiteral("track_metadata.import_file")),
            UiText::text(replacingVideo
                ? QStringLiteral("track_metadata.background_video_exists_overwrite")
                : QStringLiteral("track_metadata.background_image_exists_overwrite")),
            UiText::text(QStringLiteral("action.yes")),
            [this, sourcePath, kind](bool accepted) {
                if (accepted) applyChartMediaImport(sourcePath, kind);
            });
    });
}

void QmlDocumentModel::applyChartMediaImport(
    const QString& sourcePath, miacode::v2::ChartMediaService::Kind kind)
{
    if (workspace_ == nullptr || uiRequests_ == nullptr) return;
    const QString chartPath = currentFilePath();
    if (preview() != nullptr) preview()->prepareForMediaFileOperation();
    const auto result = mediaService_.importMedia(chartPath, sourcePath, kind);
    if (!result.ok) {
        if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
        uiRequests_->postNotice(
            miacode::v2::NoticeSeverity::Error,
            UiText::text(QStringLiteral("track_metadata.failed_to_import_media")),
            UiText::text(QStringLiteral("track_metadata.failed_to_import_media"))
                .arg(result.errorCode));
        return;
    }
    if (kind == miacode::v2::ChartMediaService::Kind::Video) {
        setMetadataVideoPath(QStringLiteral("pv.mp4"));
    }
    if (preview() != nullptr) preview()->refreshMediaAfterFileOperation();
    const bool video = kind == miacode::v2::ChartMediaService::Kind::Video;
    if (!result.warnings.isEmpty()) {
        uiRequests_->postNotice(
            miacode::v2::NoticeSeverity::Warning,
            UiText::text(QStringLiteral("track_metadata.import_file")),
            result.warnings.join(QLatin1Char('\n')));
    }
    uiRequests_->postNotice(
        miacode::v2::NoticeSeverity::Information,
        UiText::text(video ? QStringLiteral("track_metadata.import_background_video")
                           : QStringLiteral("track_metadata.import_file")),
        UiText::text(video ? QStringLiteral("track_metadata.imported_background_video")
                           : QStringLiteral("track_metadata.imported_background_image"))
            .arg(result.targetPath));
}

QString QmlDocumentModel::documentTitle() const
{
    const QString title = documentField(miacode::v2::ChartWorkspaceDocumentField::Title);
    const QString chartTitle = title.trimmed().isEmpty() ? currentFileName() : title;
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
QString QmlDocumentModel::currentDifficultyOffset() const
{
    return metadataFirst();
}
bool QmlDocumentModel::currentDifficultyLevelMissing() const
{
    return currentDifficultyId() > 0 && currentDifficultyLevel().trimmed().isEmpty();
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
        return workspace_->updateDifficultyField(
                  currentDifficultyId(), miacode::v2::ChartWorkspaceDifficultyField::Designer,
                  value);
    });
    if (!changed) return;
    publishWorkspaceCommit(WorkspaceCommitKind::Incremental);
}
void QmlDocumentModel::setCurrentDifficultyOffset(const QString& value)
{
    setMetadataFirst(value);
}

QVariantList QmlDocumentModel::syntaxIssues() const
{
    const miacode::qml_ui::DocumentValidationProjection& snapshot = validationSnapshot_;
    QVariantList result;
    result.reserve(snapshot.issues.size() + (currentDifficultyLevelMissing() ? 1 : 0));
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
            {QStringLiteral("message"), UiText::text(QStringLiteral("validation.difficulty_level_missing"))},
            {QStringLiteral("code"), QStringLiteral("missing_difficulty_level")},
            {QStringLiteral("difficultyId"), presentationState_.activeDifficultyId},
            {QStringLiteral("revision"), QVariant::fromValue<qulonglong>(presentationState_.validationRevision)},
        });
    }
    return result;
}
int QmlDocumentModel::syntaxIssueCount() const
{
    return validationSnapshot_.issues.size() + (currentDifficultyLevelMissing() ? 1 : 0);
}
int QmlDocumentModel::syntaxErrorCount() const
{
    return validationSnapshot_.errorCount + (currentDifficultyLevelMissing() ? 1 : 0);
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
bool QmlDocumentModel::dirty() const
{
    return presentationState_.dirty;
}
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
    const bool wasUnified = unifiedDesignerEnabled_;
    unifiedDesignerEnabled_ = false;
    publishWorkspaceCommit(WorkspaceCommitKind::Open, true);
    if (wasUnified) emit unifiedDesignerEnabledChanged();
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
    if (workspace_ == nullptr || !SimaiDocument::isDifficultyId(difficultyId)) return false;
    if (!runWorkspaceMutation([&] {
            return workspace_->revertDifficultyChart(difficultyId).accepted;
        })) {
        return false;
    }
    publishWorkspaceCommit(WorkspaceCommitKind::SourceReplacement);
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
    if (bridge() != nullptr) {
        runWorkspaceMutation([&] {
            bridge()->reconcileUnifiedDocumentDesigner(
                miacode::v2::DocumentBridge::UnifiedDesignerReconcileReason::DocumentOpened);
            return true;
        });
    }
    refreshUnifiedDesignerState();
    publishWorkspaceCommit(
        WorkspaceCommitKind::Open, true, result.usedSystemEncoding);
    return true;
}

void QmlDocumentModel::requestLeaveDocument(std::function<void(bool)> onDecided)
{
    requestLeaveSection(0, std::move(onDecided));
}

void QmlDocumentModel::requestLeaveCurrentField(std::function<void(bool)> onDecided)
{
    // 页面导航保留工作区中的修改，提交当前输入后切换视图。
    if (!closeDecisionPending_) emit editingFinishedRequested();
    if (onDecided) onDecided(workspace_ != nullptr && !closeDecisionPending_);
}

void QmlDocumentModel::saveSectionOrAskForPath(
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
            emit operationFailed(tr("保存失败"), tr("无法写入谱面文件。"));
        }
        finish(saved);
    });
}

bool QmlDocumentModel::saveMetadataImmediately()
{
    metadataSaveTimer_.stop();
    if (closeDecisionPending_) return true;
    if (workspace_ == nullptr || !workspace_->metadataDirty()
        || currentFilePath().isEmpty()) return true;
    if (fileService_ == nullptr || !runWorkspaceMutation([&] {
            return fileService_->save(miacode::v2::ChartWorkspace::MetadataSection).accepted;
        })) {
        emit operationFailed(tr("保存失败"), tr("无法写入谱面文件。"));
        return false;
    }
    writeUnifiedDesignerPreference(currentFilePath(), workspace_->unifiedDesignerEnabled());
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

void QmlDocumentModel::requestCloseDifficulty(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId)) return;
    requestLeaveSection(difficultyId, [this, difficultyId](bool mayClose) {
        if (mayClose) emit difficultyCloseAccepted(difficultyId);
    });
}

void QmlDocumentModel::requestLeaveSection(int difficultyId, std::function<void(bool)> onDecided)
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
        UiText::text(wholeDocument ? QStringLiteral("dialog.unsaved_changes.title")
                                  : QStringLiteral("dialog.unsaved_tab_changes.title")),
        wholeDocument ? UiText::text(QStringLiteral("dialog.unsaved_changes.message"))
                      : UiText::text(QStringLiteral("dialog.unsaved_tab_changes.message"))
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

bool QmlDocumentModel::wholeSourceEditorActive() const { return wholeSourceEditorActive_; }

void QmlDocumentModel::setWholeSourceEditorActive(bool active)
{
    if (wholeSourceEditorActive_ == active) return;
    wholeSourceEditorActive_ = active;
    emit wholeSourceEditorActiveChanged();
}

int QmlDocumentModel::saveSectionDifficultyId() const
{
    if (workspace_ == nullptr) return 0;
    if (wholeSourceEditorActive_) return miacode::v2::ChartWorkspace::MetadataSection;
    const int active = workspace_->snapshot().activeDifficultyId;
    return active > 0 ? active : miacode::v2::ChartWorkspace::MetadataSection;
}

bool QmlDocumentModel::save()
{
    emit editingFinishedRequested();
    if (fileService_ == nullptr) return false;
    const int sectionId = saveSectionDifficultyId();
    if (!runWorkspaceMutation([&] {
            return fileService_->save(sectionId).accepted;
        })) {
        emit operationFailed(tr("保存失败"), tr("无法写入谱面文件。"));
        return false;
    }
    writeUnifiedDesignerPreference(currentFilePath(), unifiedDesignerEnabled_);
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}

bool QmlDocumentModel::saveWholeDocument()
{
    emit editingFinishedRequested();
    if (fileService_ == nullptr) return false;
    if (!runWorkspaceMutation([&] { return fileService_->save(0).accepted; })) {
        emit operationFailed(tr("保存失败"), tr("无法写入谱面文件。"));
        return false;
    }
    writeUnifiedDesignerPreference(currentFilePath(), unifiedDesignerEnabled_);
    publishWorkspaceCommit(WorkspaceCommitKind::SavePoint);
    return true;
}
bool QmlDocumentModel::saveAs(const QUrl& fileUrl)
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
bool QmlDocumentModel::discardChanges()
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
bool QmlDocumentModel::applyDesignerSlots(
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

void QmlDocumentModel::reconcileUnifiedDesignerAfterSourceReplacement()
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
            miacode::v2::DocumentBridge::UnifiedDesignerReconcileReason::SourceReplaced);
        return true;
    });
}

bool QmlDocumentModel::applyDesignerSlotsWithoutBridge(
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

void QmlDocumentModel::logEditorDocumentState(const QString& reason, int difficultyId,
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

void QmlDocumentModel::emitDocumentStateChanged()
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
    input.metadataDirty = workspace_ != nullptr && workspace_->metadataDirty();
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
    refreshUnifiedDesignerState();
    emitDocumentStateChanged();
    emit documentReplaced();
}

void QmlDocumentModel::refreshUnifiedDesignerState()
{
    unifiedDesignerEnabled_ = workspace_ != nullptr && workspace_->unifiedDesignerEnabled();
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

QVariantMap QmlDocumentModel::selectionBeatSummary(
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
        options.syntax == miacode::chart_transform::ChartNormalizationSyntax::CompactSingleLine
            ? QStringLiteral("compact_single_line")
            : QStringLiteral("segment_preserving"));
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
        row(QStringLiteral("segment_preserving"), QStringLiteral("分段保留")),
        row(QStringLiteral("compact_single_line"), QStringLiteral("单行紧凑")),
    };
}
