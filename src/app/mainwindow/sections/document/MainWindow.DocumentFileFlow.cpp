#include "MainWindow.DocumentSection.h"
#include "app/v2/UiRequestService.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/Id3TagReader.h"
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
#include <QtWidgets>

using namespace miacode::mainwindow::shared;
#include "MainWindow.DocumentFlow.Internal.h"

using namespace miacode::mainwindow::documentflow_detail;

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

bool writeDocumentFileAtomically(
    const QString& path,
    const SimaiDocument& document,
    QString* failedStage = nullptr)
{
    QStringEncoder encoder(QStringConverter::Utf8);
    const QByteArray payload = encoder.encode(document.toText());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (failedStage != nullptr) {
            *failedStage = QStringLiteral("open_maidata");
        }
        return false;
    }
    if (file.write(payload) != payload.size()) {
        if (failedStage != nullptr) {
            *failedStage = QStringLiteral("write_maidata");
        }
        return false;
    }
    if (!file.commit()) {
        if (failedStage != nullptr) {
            *failedStage = QStringLiteral("commit_maidata");
        }
        return false;
    }
    return true;
}

}  // namespace

bool MainWindow::DocumentSection::applyUnsavedChangesChoice(
    const QString& choiceId, const QString& logContext)
{
    if (choiceId == QLatin1String("save")) {
        QElapsedTimer saveTimer;
        saveTimer.start();
        // "Save" must have the same durable meaning everywhere: onSaveFile()
        // commits the current field first and then atomically writes it.
        const bool saved = onSaveFile();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("on_save_file"),
            saveTimer.elapsed(),
            QStringLiteral("trigger=%1 result=%2")
                .arg(logContext, saved ? QStringLiteral("saved") : QStringLiteral("failed"))
        );
        return saved;
    }
    const bool shouldContinue = choiceId == QLatin1String("discard");
    if (shouldContinue) {
        anchorCurrentFieldCleanState();
        state_.documentDirty_ = false;
        state_.currentFieldDirty_ = false;
        updateDirtyState();
        owner_.updateWindowTitle();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("%1 result=%2").arg(logContext, choiceId)
    );
    return shouldContinue;
}

bool MainWindow::DocumentSection::maybeSaveBeforeContinue()
{
    runAutosaveCheck(false);
    if (!state_.documentDirty_ && !state_.currentFieldDirty_) {
        return true;
    }
    // Still the Widgets prompt, and deliberately so: the callers left on this
    // overload are the hidden MainWindow's own File-menu actions, which the QML
    // shell has no way to reach. Everything the v2 shell can actually run goes
    // through requestLeaveDocument below. This overload dies with
    // src/app/mainwindow/ in stage 4.
    const UnsavedChangesChoice choice = showUnsavedChangesDialog(
        &owner_,
        UiText::text(QStringLiteral("dialog.unsaved_changes.title")),
        UiText::text(QStringLiteral("dialog.unsaved_changes.message"))
    );
    return applyUnsavedChangesChoice(
        unsavedChangesChoiceName(choice), QStringLiteral("maybe_save_before_continue"));
}

void MainWindow::DocumentSection::requestLeaveDocument(std::function<void(bool)> onDecided)
{
    const auto decide = [onDecided = std::move(onDecided)](bool leave) {
        if (onDecided) {
            onDecided(leave);
        }
    };

    runAutosaveCheck(false);
    if (!state_.documentDirty_ && !state_.currentFieldDirty_) {
        decide(true);
        return;
    }

    // The shell knows which difficulties changed; this object knows only that
    // the file did. Saving is per section now, so a single question here could
    // save at most one of them and would leave with the rest.
    if (owner_.qmlLeaveDocumentHandler_) {
        owner_.qmlLeaveDocumentHandler_([decide](bool mayLeave) { decide(mayLeave); });
        return;
    }

    miacode::v2::UiRequestService* const requests = owner_.uiRequestService();
    if (requests == nullptr) {
        // No shell to ask. Refusing to leave is the answer that cannot lose
        // work.
        decide(false);
        return;
    }

    requests->requestChoice(
        UiText::text(QStringLiteral("dialog.unsaved_changes.title")),
        UiText::text(QStringLiteral("dialog.unsaved_changes.message")),
        unsavedChangesChoices(),
        QStringLiteral("cancel"),
        [this, decide](const QString& choiceId) {
            decide(applyUnsavedChangesChoice(choiceId, QStringLiteral("request_leave_document")));
        });
}

void MainWindow::DocumentSection::onNewFile()
{
    MC_OP("MainWindow::DocumentSection::onNewFile");
    if (!maybeSaveBeforeContinue()) {
        return;
    }

    const QString targetDirectory = QFileDialog::getExistingDirectory(
        &owner_,
        UiText::text(QStringLiteral("document.select_chart_folder")),
        owner_.resolveInitialOpenDirectory(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (targetDirectory.isEmpty()) {
        return;
    }

    const QString normalizedDirectory = QDir::cleanPath(targetDirectory);
    const QString targetPath = QDir(normalizedDirectory).filePath(QStringLiteral("maidata.txt"));
    if (QFileInfo::exists(targetPath)) {
        const QMessageBox::StandardButton choice = UiDialogs::showMessageBox(
            QMessageBox::Warning,
            &owner_,
            UiText::text(QStringLiteral("document.file_already_exists")),
            UiText::text(QStringLiteral("document.maidata_txt_already_exists_in")),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (choice != QMessageBox::Yes) {
            return;
        }
    }

    const SimaiDocument newDocument = SimaiDocument::createEmpty();
    if (!writeDocumentFileAtomically(targetPath, newDocument)) {
        UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Create Failed", "Cannot write file:\n" + targetPath);
        return;
    }

    cancelPendingStartupRestore();
    // loadDocument publishes the authoritative replacement notification.
    // Install the new path first so its file/title consumers receive one
    // coherent snapshot rather than the outgoing chart path.
    owner_.setCurrentFilePath(targetPath);
    loadDocument(newDocument);
    owner_.clearValidationCache();
    state_.currentEncoding_ = TextEncoding::Utf8;
    owner_.statusBar()->showMessage(QString("Created: %1").arg(targetPath));
}

namespace {

QString cleanDropFolderName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        name.chop(1);
    }
    const QString device = name.section(QLatin1Char('.'), 0, 0).toUpper();
    const bool reservedDevice = QSet<QString>{QStringLiteral("CON"), QStringLiteral("PRN"),
                                              QStringLiteral("AUX"), QStringLiteral("NUL")}.contains(device);
    const bool numberedDevice = (device.startsWith(QStringLiteral("COM"))
                                 || device.startsWith(QStringLiteral("LPT")))
        && device.size() == 4 && device.at(3) >= QLatin1Char('1') && device.at(3) <= QLatin1Char('9');
    if (reservedDevice || numberedDevice) {
        name.prepend(QLatin1Char('_'));
    }
    name = name.left(80).trimmed();
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
        name.chop(1);
    }
    return name;
}

} // namespace

void MainWindow::DocumentSection::createChartsFromAudioDrop(const QStringList& audioPaths)
{
    if (audioPaths.isEmpty()) {
        return;
    }
    QElapsedTimer dropTimer;
    dropTimer.start();
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/chart_drop"),
        QStringLiteral("drop_received count=%1").arg(audioPaths.size()));
    const QStringList supported = miacode::chart_assets::supportedTrackFileExtensions();
    QList<DocumentSection::DroppedChartCandidate> candidates;
    for (const QString& path : audioPaths) {
        const QFileInfo info(path);
        const QString extension = info.suffix().toLower();
        if (!info.isFile() || !supported.contains(extension)) {
            miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
                QStringLiteral("ui/chart_drop"),
                QStringLiteral("unsupported_file_ignored format=%1").arg(extension));
            continue;
        }
        QString title = info.completeBaseName();
        if (extension == QStringLiteral("mp3")) {
            const auto tag = miacode::id3::readTagFromFile(path);
            if (!tag.title.trimmed().isEmpty()) {
                title = tag.title.trimmed();
            }
        }
        QString folder = cleanDropFolderName(title);
        if (folder.isEmpty()) {
            folder = QStringLiteral("Untitled");
        }
        candidates.append({info.absoluteFilePath(), info.absolutePath(), extension,
                           QDir(info.absolutePath()).filePath(folder)});
        miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
            QStringLiteral("ui/chart_drop"),
            QStringLiteral("supported_file_counted format=%1").arg(extension));
    }
    if (candidates.isEmpty()) {
        return;
    }

    QHash<QString, QSet<QString>> reserved;
    QStringList preview;
    for (DroppedChartCandidate& candidate : candidates) {
        const QString key = candidate.sourceDirectory.toCaseFolded();
        QString target = candidate.targetDirectory;
        int suffix = 2;
        while (QFileInfo::exists(target) || reserved[key].contains(target.toCaseFolded())) {
            target = QDir(candidate.sourceDirectory).filePath(
                QStringLiteral("%1 (%2)").arg(QFileInfo(candidate.targetDirectory).fileName()).arg(suffix++));
        }
        candidate.targetDirectory = target;
        reserved[key].insert(target.toCaseFolded());
        preview << QDir::toNativeSeparators(target);
    }

    miacode::v2::UiRequestService* const requests = owner_.uiRequestService();
    if (requests == nullptr) {
        return;
    }
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/chart_drop"),
        QStringLiteral("preview_shown count=%1").arg(candidates.size()));
    // Two questions, asked one after the other rather than in one nested loop:
    // first whether to create these charts, then — only if yes — whether the
    // current document may be left behind. The second half runs from the first
    // one's continuation, so nothing here blocks the shell.
    requests->requestConfirmation(
        UiText::text(QStringLiteral("drop_chart.preview.title")),
        UiText::text(QStringLiteral("drop_chart.preview.message"))
            .arg(candidates.size()) + preview.join(QLatin1Char('\n')),
        UiText::text(QStringLiteral("drop_chart.preview.create")).arg(candidates.size()),
        [this, candidates, dropTimer](bool accepted) mutable {
            if (!accepted) {
                miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
                    QStringLiteral("ui/chart_drop"), QStringLiteral("create_cancelled"));
                return;
            }
            requestLeaveDocument([this, candidates, dropTimer](bool mayLeave) mutable {
                if (!mayLeave) {
                    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
                        QStringLiteral("ui/chart_drop"), QStringLiteral("create_cancelled"));
                    return;
                }
                finishChartsFromAudioDrop(candidates, dropTimer);
            });
        });
}

void MainWindow::DocumentSection::finishChartsFromAudioDrop(
    const QList<DroppedChartCandidate>& candidates, QElapsedTimer dropTimer)
{
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/chart_drop"),
        QStringLiteral("create_confirmed count=%1").arg(candidates.size()));

    const SimaiDocument emptyDocument = SimaiDocument::createEmpty();
    int created = 0;
    int failed = 0;
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/chart_drop"),
        QStringLiteral("batch_create_started count=%1").arg(candidates.size()));
    for (const DroppedChartCandidate& candidate : candidates) {
        const QString staging = QDir(candidate.sourceDirectory).filePath(
            QStringLiteral(".miacode-drop-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        const QString stagedChart = QDir(staging).filePath(QFileInfo(candidate.targetDirectory).fileName());
        const QString stagedTrack = QDir(stagedChart).filePath(QStringLiteral("track.%1").arg(candidate.extension));
        const QString stagedMaidata = QDir(stagedChart).filePath(QStringLiteral("maidata.txt"));
        QString failedStage = QStringLiteral("create_staging_directory");
        bool ok = QDir().mkpath(stagedChart);
        if (ok) {
            failedStage = QStringLiteral("copy_audio");
            ok = QFile::copy(candidate.sourcePath, stagedTrack);
        }
        if (ok) {
            failedStage.clear();
            ok = writeDocumentFileAtomically(stagedMaidata, emptyDocument, &failedStage);
        }
        if (ok) {
            failedStage = QStringLiteral("publish");
            ok = QDir().rename(stagedChart, candidate.targetDirectory);
        }
        QDir(staging).removeRecursively();
        if (!ok) {
            ++failed;
            miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
                QStringLiteral("ui/chart_drop"),
                QStringLiteral("chart_create_failed stage=%1 format=%2 created=%3 failed=%4 elapsed_ms=%5")
                    .arg(failedStage, candidate.extension)
                    .arg(created).arg(failed).arg(dropTimer.elapsed()));
            continue;
        }
        ++created;
        miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
            QStringLiteral("ui/chart_drop"),
            QStringLiteral("chart_create_succeeded format=%1").arg(candidate.extension));
    }

    miacode::v2::UiRequestService* const requests = owner_.uiRequestService();
    if (requests != nullptr) {
        if (created == 0) {
            requests->postNotice(miacode::v2::NoticeSeverity::Error,
                UiText::text(QStringLiteral("drop_chart.error.title")),
                UiText::text(QStringLiteral("drop_chart.create_failed")));
        } else if (candidates.size() == 1 && created == 1) {
            miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
                QStringLiteral("ui/chart_drop"), QStringLiteral("switch_prompt_shown"));
            const QString target = candidates.first().targetDirectory;
            requests->requestConfirmation(
                UiText::text(QStringLiteral("drop_chart.created_title")),
                UiText::text(QStringLiteral("drop_chart.confirm_switch")).arg(created),
                UiText::text(QStringLiteral("action.open")),
                [this, target](bool accepted) {
                    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
                        QStringLiteral("ui/chart_drop"),
                        accepted ? QStringLiteral("switch_confirmed")
                                 : QStringLiteral("switch_declined"));
                    if (accepted) {
                        owner_.openStartupTarget(target);
                    }
                });
        } else if (failed > 0) {
            requests->postNotice(miacode::v2::NoticeSeverity::Warning,
                UiText::text(QStringLiteral("drop_chart.created_title")),
                UiText::text(QStringLiteral("drop_chart.created_with_failures"))
                    .arg(created).arg(failed));
        }
    }
    owner_.statusBar()->showMessage(UiText::text(QStringLiteral("drop_chart.created_chart")).arg(created));
    miacode::debug_log::appendLine(miacode::debug_log::Channel::Runtime,
        QStringLiteral("ui/chart_drop"),
        QStringLiteral("batch_create_finished count=%1 elapsed_ms=%2")
            .arg(created).arg(dropTimer.elapsed()));
}

void MainWindow::DocumentSection::onOpenFile()
{
    MC_OP("MainWindow::DocumentSection::onOpenFile");
    owner_.windowSection_->logTopLevelWindowSnapshot("open_file_flow/begin");
    const bool canContinue = maybeSaveBeforeContinue();
    if (!canContinue) {
        owner_.windowSection_->logTopLevelWindowSnapshot("open_file_flow/cancelled_before_dialog");
        return;
    }

    owner_.windowSection_->logWindowGeometryDebug("open_file_before_dialog");
    owner_.windowSection_->logTopLevelWindowSnapshot("open_file_before_dialog");
    const QString path = QFileDialog::getOpenFileName(
        &owner_,
        QStringLiteral("Open simai file"),
        owner_.resolveInitialOpenDirectory(),
        QStringLiteral("Simai (*.txt *.simai);;All Files (*.*)")
    );
    owner_.windowSection_->logWindowGeometryDebug("open_file_after_dialog", QString("selected_empty=%1").arg(path.isEmpty() ? 1 : 0));
    owner_.windowSection_->logTopLevelWindowSnapshot("open_file_after_dialog");
    if (path.isEmpty()) {
        return;
    }
    openFileAtPath(path, true, true);
}

bool MainWindow::DocumentSection::openFileAtPath(const QString& path, bool showStatusMessage, bool showErrors)
{
    MC_OP("MainWindow::DocumentSection::openFileAtPath");
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
            UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Open Failed", "Cannot open file:\n" + normalizedPath);
        }
        _mc_op_.fail(QStringLiteral("prepareDocumentOpenPayload failed"));
        return false;
    }

    applyOpenedDocumentState(
        payload.normalizedPath,
        payload.usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8,
        payload.document,
        showStatusMessage,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
    return true;
}

bool MainWindow::DocumentSection::restoreLastSessionFile()
{
    MC_OP("MainWindow::DocumentSection::restoreLastSessionFile");
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
        payload.usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8,
        payload.document,
        false,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
    return true;
}

void MainWindow::DocumentSection::scheduleStartupRestoreLastSessionFile()
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
    QPointer<MainWindow> guard(&owner_);
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

                PreparedStartupRestoreDocument prepared;
                prepared.generation = generation;
                prepared.normalizedPath = payload.normalizedPath;
                prepared.document = payload.document;
                prepared.encodingUsed = payload.usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8;
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

void MainWindow::DocumentSection::applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared)
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
        false,
        prepared.hasTrackDuration ? prepared.trackDurationSeconds : -1.0
    );
    const qint64 applyElapsedMs = applyTimer.elapsed();
    appendStartupTimingStage("mainwindow/startup_restore_apply_document_ui", applyElapsedMs, applyElapsedMs);
    appendStartupTimingStage(
        "mainwindow/restored_last_document_applied",
        prepared.totalElapsedMs + applyElapsedMs,
        prepared.totalElapsedMs + applyElapsedMs
    );
    owner_.scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void MainWindow::DocumentSection::applyOpenedDocumentState(
    const QString& normalizedPath,
    TextEncoding encodingUsed,
    const SimaiDocument& document,
    bool showStatusMessage,
    double knownTrackDurationSeconds)
{
    MC_OP("MainWindow::DocumentSection::applyOpenedDocumentState");
    _mc_op_.note(QStringLiteral("path=%1 dur=%2")
                     .arg(normalizedPath)
                     .arg(knownTrackDurationSeconds, 0, 'f', 3));
    state_.currentEncoding_ = encodingUsed;
    owner_.applyWaveformData(
        miacode::waveform::makeWaveformPlaceholder(
            knownTrackDurationSeconds > 0.0 ? knownTrackDurationSeconds : 0.0));
    owner_.setCurrentFilePath(normalizedPath, true);
    owner_.addRecentFilePath(normalizedPath);

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

    loadDocument(document);
    owner_.refreshWaveformCache(knownTrackDurationSeconds);
    if (!state_.pendingAbnormalExitBackupRestorePath_.isEmpty()) {
        schedulePendingAbnormalExitBackupRestore();
    }
    if (showStatusMessage) {
        owner_.statusBar()->showMessage(
            QString("Opened: %1 (%2)")
                .arg(QFileInfo(normalizedPath).fileName())
                .arg(encodingUsed == TextEncoding::Utf8 ? "UTF-8" : "System encoding")
        );
    }
}
