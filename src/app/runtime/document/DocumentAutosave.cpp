#include "app/v2/UiRequestService.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/Shared.h"
#include "runtime/editor/EditorHost.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/ProjectPreferences.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <algorithm>

#include <QtCore>
#include <QtGui>

using namespace miacode::runtime::shared;
#include "runtime/document/DocumentFlow.Internal.h"

using namespace miacode::runtime::document_detail;

namespace {

constexpr qint64 kAutosaveUnsetTimestampMs = -1;
constexpr char kAutosaveMetadataSchema[] = "miacode_autosave_v2";

QString autosaveMetadataFilePath(const QString& autosaveDirectoryPath)
{
    return QDir(autosaveDirectoryPath).filePath(QStringLiteral("autosave.json"));
}

QString autosaveTimestampStringUtc(qint64 msecsSinceEpoch)
{
    return QDateTime::fromMSecsSinceEpoch(msecsSinceEpoch, Qt::UTC)
        .toString(QStringLiteral("yyyy-MM-dd-HH-mm-ss"));
}

QString autosaveTimestampStringUtcNow()
{
    return autosaveTimestampStringUtc(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
}

QString backupRestoreEntryLabel(const BackupRestoreEntry& entry)
{
    if (!entry.modifiedAt.isValid()) {
        return QFileInfo(entry.filePath).fileName();
    }
    return entry.modifiedAt.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

bool ensureDirectoryExists(const QString& directoryPath)
{
    if (directoryPath.isEmpty()) {
        return false;
    }
    QDir dir(directoryPath);
    return dir.exists() || QDir().mkpath(directoryPath);
}

// Decode chart/backup text bytes: UTF-8 BOM → UTF-8 → system-encoding
// fallback. Shared by the backup-restore menu and the crash/autosave
// recovery prompt so every restore path interprets bytes identically.
QString decodeChartBackupText(const QByteArray& bytes)
{
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(bytes.mid(3));
    }
    QStringDecoder utf8Decoder(QStringConverter::Utf8);
    QString text = utf8Decoder.decode(bytes);
    if (utf8Decoder.hasError()) {
        QStringDecoder systemDecoder(QStringConverter::System);
        text = systemDecoder.decode(bytes);
    }
    return text;
}

}  // namespace

void miacode::runtime::DocumentSessionHost::resetAutosaveState(const QString& referenceText)
{
    state_.autosaveReferenceContentSignature_ = autosaveContentSignature(referenceText);
    state_.autosaveLastLatestContentSignature_.clear();
    state_.autosaveLastHistoryContentSignature_.clear();
    state_.autosaveLastHistorySnapshotMs_ = kAutosaveUnsetTimestampMs;
    state_.autosaveDirtySinceMs_ = kAutosaveUnsetTimestampMs;

    // Crash-recovery: a fresh document state means the on-disk content
    // is now the truth — clear any stale crash-recovery file and the
    // in-memory snapshot. If the user was working on a different chart
    // or just saved, we don't want a future crash to write a recovery
    // file with the now-stale content. The next markCurrentFieldDirty
    // refills the snapshot.
    cleanupCrashRecoveryForCleanExit();
}

void miacode::runtime::DocumentSessionHost::cleanupCrashRecoveryForCleanExit()
{
    if (!state_.currentFilePath_.isEmpty()) {
        const QString recoveryPath =
            miacode::crash_recovery::crashRecoveryFilePath(state_.currentFilePath_);
        const bool preservePendingRestore =
            !state_.pendingAbnormalExitBackupRestorePath_.isEmpty()
            && !recoveryPath.isEmpty()
            && QDir::cleanPath(state_.pendingAbnormalExitBackupRestorePath_)
                == QDir::cleanPath(recoveryPath);
        if (!preservePendingRestore) {
            miacode::crash_recovery::deleteRecoveryFile(state_.currentFilePath_);
        }
    }
    miacode::crash_recovery::clearSnapshot();
}

QString miacode::runtime::DocumentSessionHost::resolveAutosaveDirectoryPath() const
{
    return autosaveEntryDirectoryPathForFile(state_.currentFilePath_);
}

QVariantList miacode::runtime::DocumentSessionHost::backupDocumentEntries()
{
    QVariantList rows;
    const QList<BackupRestoreEntry> entries =
        backupRestoreEntriesForAutosaveDirectory(resolveAutosaveDirectoryPath());
    QSet<QString> usedLabels;
    for (const BackupRestoreEntry& entry : entries) {
        QString label = backupRestoreEntryLabel(entry);
        // Two snapshots can share a timestamp label; the file name breaks the
        // tie, exactly as the Widgets menu did.
        if (usedLabels.contains(label)) {
            label = QStringLiteral("%1  %2").arg(label, QFileInfo(entry.filePath).fileName());
        }
        usedLabels.insert(label);
        rows.append(QVariantMap{
            {QStringLiteral("path"), entry.filePath},
            {QStringLiteral("label"), label},
        });
    }
    return rows;
}

void miacode::runtime::DocumentSessionHost::restoreBackupFilePath(const QString& path, bool mentionAbnormalExit)
{
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        return;
    }
    const QString title = UiText::text(QStringLiteral("dialog.restore_backup.title"));
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const QFileInfo backupInfo(normalizedPath);
    if (normalizedPath.isEmpty() || !backupInfo.exists() || !backupInfo.isFile()) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Warning, title,
            UiText::text(QStringLiteral("dialog.restore_backup.missing"))
                .arg(QDir::toNativeSeparators(normalizedPath)));
        return;
    }

    const QString backupTimestampLabel = backupInfo.lastModified().isValid()
        ? backupInfo.lastModified().toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        : QFileInfo(normalizedPath).fileName();
    requests->requestConfirmation(
        title,
        mentionAbnormalExit
            ? UiText::text(QStringLiteral("dialog.restore_backup.abnormal_exit_confirm"))
                  .arg(backupTimestampLabel)
            : UiText::text(QStringLiteral("dialog.restore_backup.confirm")).arg(backupTimestampLabel),
        title,
        [this, normalizedPath, title](bool accepted) {
            if (accepted) {
                applyBackupFile(normalizedPath, title);
            }
        });
}

void miacode::runtime::DocumentSessionHost::applyBackupFile(const QString& normalizedPath, const QString& title)
{
    miacode::v2::UiRequestService* const requests = session_.uiRequestService();
    if (requests == nullptr) {
        return;
    }
    // Read the on-disk chart as the undo baseline BEFORE overwriting the
    // document, exactly as the confirm-and-apply version did inline.
    QString diskReferenceText = session_.applicationServices_.workspace().document().toText();
    if (!state_.currentFilePath_.isEmpty()) {
        QFile currentFile(state_.currentFilePath_);
        if (currentFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            diskReferenceText = decodeChartBackupText(currentFile.readAll());
        }
    }

    QFile file(normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        requests->postNotice(
            miacode::v2::NoticeSeverity::Error, title,
            UiText::text(QStringLiteral("dialog.restore_backup.read_failed"))
                .arg(QDir::toNativeSeparators(normalizedPath)));
        return;
    }

    const QString backupText = decodeChartBackupText(file.readAll());

    miacode::v2::ChartWorkspace& workspace = session_.applicationServices_.workspace();
    const miacode::v2::ChartWorkspaceResult replaced = workspace.replaceSource(backupText);
    if (!replaced.accepted) {
        workspace.openSource(backupText, state_.currentFilePath_);
        workspace.rebindSavePoint(diskReferenceText);
    }
    loadDocument();
    state_.autosaveReferenceContentSignature_ = autosaveContentSignature(diskReferenceText);
    state_.autosaveLastLatestContentSignature_.clear();
    state_.autosaveLastHistoryContentSignature_.clear();
    state_.autosaveLastHistorySnapshotMs_ = kAutosaveUnsetTimestampMs;
    state_.documentDirty_ = workspace.snapshot().dirty;
    state_.currentFieldDirty_ = true;
    updateDirtyState();
    session_.scheduleTimelineRefresh();
    // The status bar this used to write to is hidden under the QML shell, so
    // the confirmation of a successful restore goes to the shared notice
    // surface instead of vanishing.
    requests->postNotice(
        miacode::v2::NoticeSeverity::Information, title,
        UiText::text(QStringLiteral("status.restore_backup.loaded")));
}

void miacode::runtime::DocumentSessionHost::schedulePendingAbnormalExitBackupRestore()
{
    if (state_.pendingAbnormalExitBackupRestorePath_.isEmpty()
        || state_.pendingAbnormalExitBackupRestoreScheduled_) {
        return;
    }
    state_.pendingAbnormalExitBackupRestoreScheduled_ = true;
    QTimer::singleShot(0, &session_, [this]() {
        QTimer::singleShot(0, &session_, [this]() {
            runPendingAbnormalExitBackupRestore();
        });
    });
}

void miacode::runtime::DocumentSessionHost::runPendingAbnormalExitBackupRestore()
{
    state_.pendingAbnormalExitBackupRestoreScheduled_ = false;
    const QString path = state_.pendingAbnormalExitBackupRestorePath_;
    const QString chartPath = state_.pendingAbnormalExitBackupRestoreChartPath_;
    state_.pendingAbnormalExitBackupRestorePath_.clear();
    state_.pendingAbnormalExitBackupRestoreChartPath_.clear();
    if (path.isEmpty()
        || chartPath.isEmpty()
        || state_.currentFilePath_.isEmpty()
        || QDir::cleanPath(chartPath) != QDir::cleanPath(state_.currentFilePath_)) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("crash_recovery"),
        QStringLiteral("action=run_deferred_restore_backup path=%1 chart=%2")
            .arg(path, state_.currentFilePath_));
    restoreBackupFilePath(path, true);
}

QString miacode::runtime::DocumentSessionHost::currentDocumentTextForAutosave() const
{
    SimaiDocument snapshot = session_.applicationServices_.workspace().document();
    // Tracks where a live (possibly uncommitted) designer edit was captured
    // from, so the unified-mode mirror below broadcasts what the user is
    // actually typing rather than a stale committed value.
    QString liveCanonicalDesigner = snapshot.designer;
    if (session_.hasActiveDifficulty()) {
        SimaiDifficultyData& difficultyData = snapshot.ensureDifficulty(state_.activeDifficultyId_);
        difficultyData.chart = session_.editorText();
    }
    // The metadata page used to be scraped from hidden QLineEdits here. Those
    // were never constructed once QML took the page over, so each ternary fell
    // to QString() and every autosave taken with metadata open overwrote the
    // snapshot's title, artist, designer and extra fields with empty ones —
    // losing them from the backup. QML commits metadata edits straight into
    // ChartWorkspace, so the values copied into `snapshot` above are already
    // the live ones and need no second capture.

    // Under unified mode the metadata page (top &des) or the difficulty header
    // (&des_N) may hold an uncommitted designer edit that hasn't broadcast yet
    // (broadcast only happens on field commit in applyCurrentFieldToDocument).
    // Mirror it across &des and every per-difficulty name — charted and
    // standalone alike — so the autosaved backup is never internally
    // out-of-sync with what the user is typing.
    const bool capturedDesignerFromUi = session_.hasActiveDifficulty()
        || state_.activeOutlineKey_ == QLatin1String("metadata");
    if (state_.unifiedDesignerEnabled_ && capturedDesignerFromUi) {
        const QString canonical = liveCanonicalDesigner;
        snapshot.designer = canonical;
        const QVector<QPair<int, QString>> designerSlots = snapshot.perDifficultyDesigners();
        for (const QPair<int, QString>& slot : designerSlots) {
            if (slot.second != canonical) {
                snapshot.setDesignerForSlot(slot.first, canonical);
            }
        }
    }
    return snapshot.toText();
}

void miacode::runtime::DocumentSessionHost::pruneAutosaveFiles(const QString& autosaveDirectoryPath) const
{
    QDir historyDir(autosaveHistoryDirectoryPath(autosaveDirectoryPath));
    QFileInfoList autosaveFiles = historyDir.entryInfoList(
        QStringList{QStringLiteral("*.bak")},
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name
    );
    if (autosaveFiles.size() <= kAutosaveHistoryMaxVersions) {
        return;
    }

    const int removeCount = autosaveFiles.size() - kAutosaveHistoryMaxVersions;
    for (int index = 0; index < removeCount; ++index) {
        historyDir.remove(autosaveFiles.at(index).fileName());
    }
}

void miacode::runtime::DocumentSessionHost::rebuildAutosaveMetadata(const QString& autosaveDirectoryPath) const
{
    if (autosaveDirectoryPath.isEmpty() || state_.currentFilePath_.isEmpty() || !ensureDirectoryExists(autosaveDirectoryPath)) {
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(kAutosaveMetadataSchema));
    root.insert(QStringLiteral("source_path"), QDir::cleanPath(state_.currentFilePath_));
    root.insert(
        QStringLiteral("source_relative_path"),
        QFileInfo(state_.currentFilePath_).fileName()
    );
    root.insert(QStringLiteral("latest_file"), QFileInfo(autosaveLatestFilePath(autosaveDirectoryPath)).fileName());
    root.insert(QStringLiteral("history_dir"), QStringLiteral("history"));
    root.insert(QStringLiteral("history_limit"), kAutosaveHistoryMaxVersions);
    root.insert(QStringLiteral("history_interval_ms"), kAutosaveIntervalMs);
    root.insert(
        QStringLiteral("saved_reference_signature"),
        QString::fromLatin1(state_.autosaveReferenceContentSignature_.toHex())
    );

    const QString latestFilePath = autosaveLatestFilePath(autosaveDirectoryPath);
    const QFileInfo latestInfo(latestFilePath);
    if (latestInfo.exists() && latestInfo.isFile()) {
        QJsonObject latest;
        latest.insert(QStringLiteral("file"), latestInfo.fileName());
        latest.insert(QStringLiteral("size_bytes"), static_cast<qint64>(latestInfo.size()));
        latest.insert(QStringLiteral("modified_at"), latestInfo.lastModified().toUTC().toString(Qt::ISODateWithMs));
        root.insert(QStringLiteral("latest"), latest);
    }

    const QString historyDirectoryPath = autosaveHistoryDirectoryPath(autosaveDirectoryPath);
    QDir historyDir(historyDirectoryPath);
    QFileInfoList historyFiles = historyDir.entryInfoList(
        QStringList{QStringLiteral("*.bak")},
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name
    );
    QJsonArray historyArray;
    for (const QFileInfo& historyInfo : historyFiles) {
        QJsonObject entry;
        entry.insert(QStringLiteral("file"), QStringLiteral("history/%1").arg(historyInfo.fileName()));
        entry.insert(QStringLiteral("size_bytes"), static_cast<qint64>(historyInfo.size()));
        entry.insert(QStringLiteral("modified_at"), historyInfo.lastModified().toUTC().toString(Qt::ISODateWithMs));
        historyArray.append(entry);
    }
    root.insert(QStringLiteral("history"), historyArray);
    root.insert(QStringLiteral("history_count"), historyArray.size());
    root.insert(QStringLiteral("updated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    QSaveFile metadataFile(autosaveMetadataFilePath(autosaveDirectoryPath));
    if (!metadataFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (metadataFile.write(payload) != payload.size()) {
        return;
    }
    metadataFile.commit();
}

void miacode::runtime::DocumentSessionHost::runAutosaveCheck(bool allowHistory)
{
    if (state_.currentFilePath_.isEmpty()) {
        return;
    }

    const QString autosaveDirectoryPath = resolveAutosaveDirectoryPath();
    if (autosaveDirectoryPath.isEmpty()) {
        return;
    }

    const QFileInfo autosaveDirectoryInfo(autosaveDirectoryPath);
    const QString metadataFilePath = autosaveMetadataFilePath(autosaveDirectoryPath);
    if (!QFileInfo::exists(metadataFilePath) && autosaveDirectoryInfo.exists() && autosaveDirectoryInfo.isDir()) {
        rebuildAutosaveMetadata(autosaveDirectoryPath);
    }

    const QString snapshotText = currentDocumentTextForAutosave();
    const QByteArray snapshotSignature = autosaveContentSignature(snapshotText);
    if (snapshotSignature == state_.autosaveReferenceContentSignature_) {
        state_.autosaveDirtySinceMs_ = kAutosaveUnsetTimestampMs;
        return;
    }

    if (!ensureDirectoryExists(autosaveDirectoryPath)) {
        return;
    }

    const qint64 nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
    if (state_.autosaveDirtySinceMs_ < 0) {
        state_.autosaveDirtySinceMs_ = nowMs;
    }

    const QString latestFilePath = autosaveLatestFilePath(autosaveDirectoryPath);
    const bool latestExists = QFileInfo::exists(latestFilePath);
    if (!latestExists || snapshotSignature != state_.autosaveLastLatestContentSignature_) {
        QSaveFile latestFile(latestFilePath);
        if (!latestFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }

        const QByteArray payload = snapshotText.toUtf8();
        if (latestFile.write(payload) != payload.size() || !latestFile.commit()) {
            return;
        }
        state_.autosaveLastLatestContentSignature_ = snapshotSignature;
    }

    const qint64 historyReferenceMs = state_.autosaveLastHistorySnapshotMs_ >= 0
        ? state_.autosaveLastHistorySnapshotMs_
        : state_.autosaveDirtySinceMs_;
    const bool historyDue = historyReferenceMs >= 0 && nowMs - historyReferenceMs >= kAutosaveIntervalMs;
    if (allowHistory && historyDue && snapshotSignature != state_.autosaveLastHistoryContentSignature_) {
        const QString historyDirectoryPath = autosaveHistoryDirectoryPath(autosaveDirectoryPath);
        if (!ensureDirectoryExists(historyDirectoryPath)) {
            return;
        }

        const QString historyFileName = autosaveTimestampStringUtcNow() + QStringLiteral(".bak");
        QSaveFile historyFile(QDir(historyDirectoryPath).filePath(historyFileName));
        if (!historyFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return;
        }

        const QByteArray payload = snapshotText.toUtf8();
        if (historyFile.write(payload) != payload.size() || !historyFile.commit()) {
            return;
        }
        state_.autosaveLastHistoryContentSignature_ = snapshotSignature;
        state_.autosaveLastHistorySnapshotMs_ = nowMs;
        pruneAutosaveFiles(autosaveDirectoryPath);
    }

    rebuildAutosaveMetadata(autosaveDirectoryPath);
}

namespace {

// Saving is on the v2 path (Session::saveDocument / saveDocumentAs), so its
// failures have to reach the QML shell rather than a Widgets box.
void postSaveFailureThrough(miacode::v2::UiRequestService* requests, const QString& text)
{
    if (requests != nullptr) {
        requests->postNotice(miacode::v2::NoticeSeverity::Error,
                             QStringLiteral("Save Failed"), text);
    }
}

}  // namespace

bool miacode::runtime::DocumentSessionHost::saveToPath(const QString& path)
{
    if (session_.qmlDocumentSaveHandler_) {
        return session_.qmlDocumentSaveHandler_(path);
    }
    MC_OP("miacode::runtime::DocumentSessionHost::saveToPath");
    _mc_op_.note(QStringLiteral("path=%1").arg(path));
    QElapsedTimer totalTimer;
    totalTimer.start();
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const QString targetName = QFileInfo(normalizedPath.isEmpty() ? path : normalizedPath).fileName();

    QElapsedTimer applyTimer;
    applyTimer.start();
    const bool applied = applyCurrentFieldToDocument();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("apply_current_field_to_document"),
        applyTimer.elapsed(),
        QStringLiteral("trigger=save_to_path result=%1 target=%2")
            .arg(applied ? QStringLiteral("applied") : QStringLiteral("failed"), targetName)
    );
    if (!applied) {
        _mc_op_.fail(QStringLiteral("apply_current_field_to_document failed"));
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("save_to_path"),
            totalTimer.elapsed(),
            QStringLiteral("result=apply_failed target=%1").arg(targetName)
        );
        return false;
    }
    bool firstOk = false;
    (void)session_.parsedFirstSeconds(&firstOk);
    if (!firstOk) {
        _mc_op_.fail(QStringLiteral("invalid_first"));
        postSaveFailureThrough(session_.uiRequestService(), QStringLiteral("&first must be a valid number of seconds."));
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("save_to_path"),
            totalTimer.elapsed(),
            QStringLiteral("result=invalid_first target=%1").arg(targetName)
        );
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        _mc_op_.fail(QStringLiteral("QSaveFile::open: %1").arg(file.errorString()));
        postSaveFailureThrough(session_.uiRequestService(), QStringLiteral("Cannot write file:\n") + path);
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("save_to_path"),
            totalTimer.elapsed(),
            QStringLiteral("result=open_failed target=%1").arg(targetName)
        );
        return false;
    }
    QByteArray data;
    const QString serialized = session_.applicationServices_.workspace().document().toText();
    if (state_.currentEncoding_ == Session::TextEncoding::System) {
        QStringEncoder encoder(QStringConverter::System);
        data = encoder.encode(serialized);
    } else {
        QStringEncoder encoder(QStringConverter::Utf8);
        data = encoder.encode(serialized);
    }
    if (file.write(data) != data.size() || !file.commit()) {
        _mc_op_.fail(QStringLiteral("write_or_commit_failed err=%1").arg(file.errorString()));
        postSaveFailureThrough(session_.uiRequestService(), QStringLiteral("Write failed:\n") + path);
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("save_to_path"),
            totalTimer.elapsed(),
            QStringLiteral("result=write_failed target=%1").arg(targetName)
        );
        return false;
    }
    if (normalizedPath != state_.currentFilePath_) {
        session_.setCurrentFilePath(normalizedPath);
    }
    session_.saveProjectValidationPreferences(normalizedPath);
    session_.addRecentFilePath(normalizedPath);
    session_.saveProjectRenderState();
    resetAutosaveState(serialized);
    session_.applicationServices_.workspace().markSaved(normalizedPath);
    const QString autosaveDirectoryPath = resolveAutosaveDirectoryPath();
    if (!autosaveDirectoryPath.isEmpty()) {
        rebuildAutosaveMetadata(autosaveDirectoryPath);
    }
    state_.documentDirty_ = false;
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    session_.updateWindowTitle();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("save_to_path"),
        totalTimer.elapsed(),
        QStringLiteral("result=saved target=%1").arg(targetName)
    );
    return true;
}
