#include "runtime/document/DocumentSessionHost.h"
#include "app/services/UiRequestService.h"
#include "runtime/Shared.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/ProjectPreferences.h"
#include "common/WaveformCache.h"
#include "app/services/PlaybackStateAuthority.h"
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

struct PreparedDocumentOpenPayload {
    bool success = false;
    bool usedSystemEncoding = false;
    QString normalizedPath;
    SimaiDocument document;
    QString resolvedTrackPath;
    double trackDurationSeconds = 0.0;
    bool hasTrackDuration = false;
    qint64 readElapsedMs = 0;
    qint64 decodeElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 trackProbeElapsedMs = 0;
    qint64 totalElapsedMs = 0;
};

PreparedDocumentOpenPayload prepareDocumentOpenPayload(const QString& path, bool probeTrackDuration)
{
    PreparedDocumentOpenPayload payload;
    payload.normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (payload.normalizedPath.isEmpty()) {
        return payload;
    }

    QFile file(payload.normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return payload;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer phaseTimer;
    phaseTimer.start();
    const QByteArray bytes = file.readAll();
    payload.readElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    QString text;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(bytes.mid(3));
    } else {
        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        text = utf8Decoder.decode(bytes);
        if (utf8Decoder.hasError()) {
            QStringDecoder systemDecoder(QStringConverter::System);
            text = systemDecoder.decode(bytes);
            payload.usedSystemEncoding = true;
        }
    }
    payload.decodeElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    payload.document = SimaiDocument::fromText(text);
    payload.parseElapsedMs = phaseTimer.elapsed();

    if (probeTrackDuration) {
        payload.resolvedTrackPath = miacode::chart_assets::resolveTrackPath(payload.normalizedPath);
        phaseTimer.restart();
        if (!payload.resolvedTrackPath.isEmpty()) {
            payload.trackDurationSeconds = probeAudioDurationSeconds(payload.resolvedTrackPath);
            payload.hasTrackDuration = payload.trackDurationSeconds > 0.0;
        }
        payload.trackProbeElapsedMs = phaseTimer.elapsed();
    }

    payload.totalElapsedMs = totalTimer.elapsed();
    payload.success = true;
    return payload;
}

}  // namespace

void miacode::runtime::DocumentSessionHost::requestLeaveDocument(std::function<void(bool)> onDecided)
{
    const auto decide = [onDecided = std::move(onDecided)](bool leave) {
        if (onDecided) {
            onDecided(leave);
        }
    };

    runAutosaveCheck(false);
    // 由工作区提交当前输入并判断整文修改，运行时脏标记可能尚未同步。
    if (session_.qmlLeaveDocumentHandler_) {
        session_.qmlLeaveDocumentHandler_([decide](bool mayLeave) { decide(mayLeave); });
        return;
    }

    // 缺少处理器时，未保存的文档保留在窗口内。
    decide(!state_.documentDirty_ && !state_.currentFieldDirty_);
}

bool miacode::runtime::DocumentSessionHost::openFileAtPath(const QString& path, bool showErrors)
{
    MC_OP("miacode::runtime::DocumentSessionHost::openFileAtPath");
    _mc_op_.note(QStringLiteral("path=%1").arg(path));
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        _mc_op_.fail(QStringLiteral("empty path"));
        return false;
    }

    cancelPendingStartupRestore();
    const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
    if (!payload.success) {
        if (showErrors) {
            if (miacode::UiRequestService* const requests = session_.uiRequestService()) {
                requests->postNotice(
                    miacode::NoticeSeverity::Error,
                    QStringLiteral("Open Failed"),
                    QStringLiteral("Cannot open file:\n") + normalizedPath
                );
            }
        }
        _mc_op_.fail(QStringLiteral("prepareDocumentOpenPayload failed"));
        return false;
    }

    applyOpenedDocumentState(
        payload.normalizedPath,
        payload.usedSystemEncoding ? Session::TextEncoding::System : Session::TextEncoding::Utf8,
        payload.document,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
    return true;
}

bool miacode::runtime::DocumentSessionHost::restoreLastSessionFile()
{
    MC_OP("miacode::runtime::DocumentSessionHost::restoreLastSessionFile");
    _mc_op_.note(QStringLiteral("path=%1").arg(state_.lastSessionFilePath_));
    if (state_.lastSessionFilePath_.isEmpty()) {
        return false;  // not a failure — first run or cleared session
    }
    const QFileInfo fileInfo(state_.lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        _mc_op_.fail(QStringLiteral("session_file_missing"));
        state_.lastSessionFilePath_.clear();
        return false;
    }
    const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(fileInfo.absoluteFilePath(), true);
    if (!payload.success) {
        _mc_op_.fail(QStringLiteral("prepareDocumentOpenPayload failed"));
        return false;
    }
    applyOpenedDocumentState(
        payload.normalizedPath,
        payload.usedSystemEncoding ? Session::TextEncoding::System : Session::TextEncoding::Utf8,
        payload.document,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
    return true;
}

void miacode::runtime::DocumentSessionHost::scheduleStartupRestoreLastSessionFile()
{
    if (!state_.autoRestoreLastSessionFile_ || state_.lastSessionFilePath_.isEmpty()) {
        state_.startupRestorePending_ = false;
        return;
    }

    const QFileInfo fileInfo(state_.lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        state_.lastSessionFilePath_.clear();
        state_.startupRestorePending_ = false;
        return;
    }

    state_.startupRestorePending_ = true;
    const quint64 generation = ++state_.startupRestoreGeneration_;
    const QString normalizedPath = fileInfo.absoluteFilePath();
    QPointer<Session> guard(&session_);
    QThreadPool* const pool = state_.previewWarmupPool_ != nullptr
        ? state_.previewWarmupPool_
        : QThreadPool::globalInstance();
    pool->start([guard, generation, normalizedPath]() {
        const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, payload]() {
                if (guard.isNull() || generation != guard->state_.startupRestoreGeneration_ || !guard->state_.startupRestorePending_) {
                    return;
                }

                guard->state_.startupRestorePending_ = false;
                if (!payload.success) {
                    appendStartupTimingStage("mainwindow/startup_restore_prepare_failed", 0, 0);
                    guard->scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
                    return;
                }

                Session::PreparedStartupRestoreDocument prepared;
                prepared.generation = generation;
                prepared.normalizedPath = payload.normalizedPath;
                prepared.document = payload.document;
                prepared.encodingUsed = payload.usedSystemEncoding ? Session::TextEncoding::System : Session::TextEncoding::Utf8;
                prepared.resolvedTrackPath = payload.resolvedTrackPath;
                prepared.trackDurationSeconds = payload.trackDurationSeconds;
                prepared.hasTrackDuration = payload.hasTrackDuration;
                prepared.readElapsedMs = payload.readElapsedMs;
                prepared.decodeElapsedMs = payload.decodeElapsedMs;
                prepared.parseElapsedMs = payload.parseElapsedMs;
                prepared.trackProbeElapsedMs = payload.trackProbeElapsedMs;
                prepared.totalElapsedMs = payload.totalElapsedMs;
                guard->applyPreparedStartupRestoreDocument(prepared);
            },
            Qt::QueuedConnection
        );
    });
}

void miacode::runtime::DocumentSessionHost::applyPreparedStartupRestoreDocument(const Session::PreparedStartupRestoreDocument& prepared)
{
    if (prepared.generation != state_.startupRestoreGeneration_) {
        return;
    }

    appendStartupTimingStage("mainwindow/startup_restore_read_bytes", prepared.readElapsedMs, prepared.readElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_decode_text", prepared.decodeElapsedMs, prepared.decodeElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_parse_document", prepared.parseElapsedMs, prepared.parseElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_probe_track_duration", prepared.trackProbeElapsedMs, prepared.trackProbeElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_prepare_total", prepared.totalElapsedMs, prepared.totalElapsedMs);

    QElapsedTimer applyTimer;
    applyTimer.start();
    applyOpenedDocumentState(
        prepared.normalizedPath,
        prepared.encodingUsed,
        prepared.document,
        prepared.hasTrackDuration ? prepared.trackDurationSeconds : -1.0
    );
    const qint64 applyElapsedMs = applyTimer.elapsed();
    appendStartupTimingStage("mainwindow/startup_restore_apply_document_ui", applyElapsedMs, applyElapsedMs);
    appendStartupTimingStage(
        "mainwindow/restored_last_document_applied",
        prepared.totalElapsedMs + applyElapsedMs,
        prepared.totalElapsedMs + applyElapsedMs
    );
    session_.scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void miacode::runtime::DocumentSessionHost::applyOpenedDocumentState(
    const QString& normalizedPath,
    Session::TextEncoding encodingUsed,
    const SimaiDocument& document,
    double knownTrackDurationSeconds)
{
    MC_OP("miacode::runtime::DocumentSessionHost::applyOpenedDocumentState");
    _mc_op_.note(QStringLiteral("path=%1 dur=%2")
                     .arg(normalizedPath)
                     .arg(knownTrackDurationSeconds, 0, 'f', 3));
    state_.currentEncoding_ = encodingUsed;
    session_.applyWaveformData(
        miacode::waveform::makeWaveformPlaceholder(
            knownTrackDurationSeconds > 0.0 ? knownTrackDurationSeconds : 0.0));
    session_.setCurrentFilePath(normalizedPath, true);
    session_.addRecentFilePath(normalizedPath);

    // Eagerly create the crash-recovery directory BEFORE the user can
    // edit. Without this, a crash in the first ~1 ms after a keystroke
    // (before the lazy mkpath inside updateSnapshot has run) would find
    // the parent directory missing and fail CreateFileW. mkpath is
    // re-entrant and cheap on warm runs (one stat()).
    miacode::crash_recovery::prepareForChart(normalizedPath);

    // Abnormal-exit recovery intentionally reuses File -> Restore Backup.
    // Opening the chart must finish first so the restore prompt appears over
    // the fully loaded window and the old on-disk content remains the restore
    // baseline, exactly like a manual menu action.
    const bool previousSessionAbandoned =
        miacode::crash_recovery::consumeAbandonedSessionChartMatch(normalizedPath);
    const QString crashRecoveryPath = miacode::crash_recovery::crashRecoveryFilePath(normalizedPath);
    const bool crashRecoveryFileExists =
        !crashRecoveryPath.isEmpty() && QFileInfo(crashRecoveryPath).exists();
    if (previousSessionAbandoned || crashRecoveryFileExists) {
        state_.pendingAbnormalExitBackupRestorePath_ =
            latestBackupRestoreFilePathForChart(normalizedPath);
        state_.pendingAbnormalExitBackupRestoreChartPath_ =
            state_.pendingAbnormalExitBackupRestorePath_.isEmpty() ? QString() : normalizedPath;
        if (!state_.pendingAbnormalExitBackupRestorePath_.isEmpty()) {
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("crash_recovery"),
                QStringLiteral("action=defer_restore_backup path=%1 chart=%2")
                    .arg(state_.pendingAbnormalExitBackupRestorePath_, normalizedPath));
        }
    }

    miacode::ChartWorkspace& workspace = session_.applicationServices_.workspace();
    const QString source = document.toText();
    const miacode::ChartWorkspaceSnapshot snapshot = workspace.snapshot();
    if (!snapshot.hasDocument
        || snapshot.filePath != normalizedPath
        || workspace.document().toText() != source) {
        workspace.openSource(source, normalizedPath);
    }
    // Reads the project preference and reconciles it with what was just
    // loaded. Never writes a document field: the chart stays byte for byte
    // what is on disk, so this open cannot arrive dirty.
    reconcileUnifiedDocumentDesigner(
        miacode::DocumentBridge::UnifiedDesignerReconcileReason::DocumentOpened);
    loadDocument();
    session_.refreshWaveformCache(knownTrackDurationSeconds);
    if (!state_.pendingAbnormalExitBackupRestorePath_.isEmpty()) {
        schedulePendingAbnormalExitBackupRestore();
    }
}

void miacode::runtime::DocumentSessionHost::resetWorkingPosition()
{
    if (!state_.backendActive_) {
        return;
    }
    session_.resetWorkingPositionPending_ = true;
    state_.pendingDifficultySwitchPreviewRestore_ = false;
    state_.pendingDifficultySwitchPreviewRestoreRevision_ = 0;
    state_.pendingDifficultySwitchPreviewRestoreDifficultyId_ = 0;
    state_.pendingDifficultySwitchPreviewRestoreSecond_ = 0.0;
    session_.setTouchPadAuthoringAnchor(-1.0, -1.0);
    clearTimelineAndPreview();
    if (auto* authority = session_.applicationServices_.playbackStateAuthority(); authority != nullptr) {
        authority->repositionSilently(0.0, "reset_working_position");
    }
}

void miacode::runtime::DocumentSessionHost::syncRuntimeFromWorkspace()
{
    const miacode::ChartWorkspaceSnapshot snapshot =
        session_.applicationServices_.workspace().snapshot();
    if (snapshot.revision <= session_.appliedQmlWorkspaceRevision_) {
        return;
    }
    session_.appliedQmlWorkspaceRevision_ = snapshot.revision;

    const quint64 previousOpenGeneration = session_.appliedDocumentOpenGeneration_;
    const bool documentIdentityChanged =
        snapshot.documentOpenGeneration != previousOpenGeneration;
    session_.appliedDocumentOpenGeneration_ = snapshot.documentOpenGeneration;
    const bool resetInheritedWorkingPosition =
        documentIdentityChanged && previousOpenGeneration != 0 && state_.backendActive_;

    if (!snapshot.hasDocument) {
        state_.documentDirty_ = false;
        state_.currentFieldDirty_ = false;
        state_.activeDifficultyId_ = 0;
        session_.setCurrentFilePath(QString(), true);
        if (resetInheritedWorkingPosition) {
            resetWorkingPosition();
        }
        session_.resetWorkingPositionPending_ = false;
        return;
    }

    if (resetInheritedWorkingPosition) {
        resetWorkingPosition();
        state_.activeDifficultyId_ = 0;
    }

    const bool pathChanged = snapshot.filePath != state_.currentFilePath_;
    if (pathChanged) {
        session_.setCurrentFilePath(snapshot.filePath, true);
        if (!snapshot.filePath.isEmpty()) {
            // An untitled document had nowhere to record a shared-designer
            // choice; the save that just gave it a path also gives it one.
            flushPendingUnifiedDesignerPreference();
            session_.addRecentFilePath(snapshot.filePath);
            miacode::crash_recovery::prepareForChart(snapshot.filePath);
            const bool previousSessionAbandoned =
                miacode::crash_recovery::consumeAbandonedSessionChartMatch(snapshot.filePath);
            const QString crashRecoveryPath =
                miacode::crash_recovery::crashRecoveryFilePath(snapshot.filePath);
            const bool crashRecoveryFileExists =
                !crashRecoveryPath.isEmpty() && QFileInfo(crashRecoveryPath).exists();
            if (previousSessionAbandoned || crashRecoveryFileExists) {
                state_.pendingAbnormalExitBackupRestorePath_ =
                    latestBackupRestoreFilePathForChart(snapshot.filePath);
                state_.pendingAbnormalExitBackupRestoreChartPath_ =
                    state_.pendingAbnormalExitBackupRestorePath_.isEmpty()
                        ? QString()
                        : snapshot.filePath;
                if (!state_.pendingAbnormalExitBackupRestorePath_.isEmpty()) {
                    schedulePendingAbnormalExitBackupRestore();
                }
            }
        }
        // The workspace path is authoritative for the document, while the
        // preview duration still needs the media length before the user can
        // start playback. Keep the same eager duration handoff as the native
        // open path; waveform extraction remains asynchronous below it.
        const double knownTrackDurationSeconds =
            probeAudioDurationSeconds(state_.lastTrackPath_);
        session_.refreshWaveformCache(knownTrackDurationSeconds);
        resetAutosaveState(snapshot.sourceText);
    }

    state_.documentDirty_ = snapshot.dirty;
    state_.currentFieldDirty_ = false;
    if (snapshot.dirty) {
        noteDocumentEditedForAutosave();
    }
    // Overlay audition pages deliberately keep their source outside the
    // workspace's active difficulty. A metadata-only workspace write must not
    // turn that intentional mismatch into a real difficulty switch.
    const bool auditionPageOwnsSource =
        state_.latencySandboxAuditionActive_ || state_.exportPreviewAuditionActive_;
    const bool difficultyChanged =
        !auditionPageOwnsSource
        && SimaiDocument::isDifficultyId(snapshot.activeDifficultyId)
        && snapshot.activeDifficultyId != state_.activeDifficultyId_;
    if (difficultyChanged) {
        switchToDifficultyField(snapshot.activeDifficultyId);
    } else {
        session_.resetWorkingPositionPending_ = false;
        session_.scheduleTimelineRefresh();
    }
}
