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

MainWindow::DocumentSection::DocumentSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

bool MainWindow::DocumentSection::maybeSaveCurrentFieldChanges()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    if (!state_.currentFieldDirty_) {
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("maybe_save_current_field_changes"),
            totalTimer.elapsed(),
            QStringLiteral("result=clean_field")
        );
        return true;
    }

    const QString fieldName = owner_.hasActiveDifficulty()
        ? SimaiDocument::difficultyName(state_.activeDifficultyId_)
        : UiText::text(QStringLiteral("dialog.unsaved_field_changes.field.metadata"));
    QElapsedTimer dialogTimer;
    dialogTimer.start();
    const UnsavedChangesChoice choice = showUnsavedChangesDialog(
        &owner_,
        UiText::text(QStringLiteral("dialog.unsaved_field_changes.title")),
        UiText::text(QStringLiteral("dialog.unsaved_field_changes.message")).arg(fieldName)
    );
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("unsaved_field_changes_dialog"),
        dialogTimer.elapsed(),
        QStringLiteral("choice=%1 field=%2")
            .arg(unsavedChangesChoiceName(choice), fieldName)
    );
    if (choice == UnsavedChangesChoice::Save) {
        QElapsedTimer saveTimer;
        saveTimer.start();
        // "Save" must have the same durable meaning everywhere. Merely
        // applying the active editor field to SimaiDocument and restoring the
        // previous documentDirty flag made the UI look clean without writing
        // the file, so a later close silently lost the edit. onSaveFile()
        // commits the current field first and then atomically writes it.
        const bool saved = onSaveFile();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("on_save_file"),
            saveTimer.elapsed(),
            QStringLiteral("trigger=unsaved_field_changes result=%1 field=%2")
                .arg(saved ? QStringLiteral("saved") : QStringLiteral("failed"), fieldName)
        );
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("maybe_save_current_field_changes"),
            totalTimer.elapsed(),
            QStringLiteral("result=%1 field=%2")
                .arg(saved ? QStringLiteral("saved") : QStringLiteral("save_failed"), fieldName)
        );
        return saved;
    }
    if (choice == UnsavedChangesChoice::Discard) {
        if (owner_.hasActiveDifficulty()) {
            populateDifficultyPage(state_.activeDifficultyId_);
        } else if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
            populateMetadataPage();
        }
        state_.currentFieldDirty_ = false;
        updateDirtyState();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("maybe_save_current_field_changes"),
            totalTimer.elapsed(),
            QStringLiteral("result=discard field=%1").arg(fieldName)
        );
        return true;
    }
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/document"),
        QStringLiteral("maybe_save_current_field_changes"),
        totalTimer.elapsed(),
        QStringLiteral("result=cancel field=%1").arg(fieldName)
    );
    return false;
}

bool MainWindow::DocumentSection::applyCurrentFieldToDocument()
{
    // QML already commits into ChartWorkspace. Flushing hidden widget fields
    // would overwrite that. This path only clears the widget dirty latch.
    anchorCurrentFieldCleanState();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    return true;
}

void MainWindow::DocumentSection::cancelPendingStartupRestore()
{
    if (!state_.startupRestorePending_) {
        return;
    }
    state_.startupRestorePending_ = false;
    ++state_.startupRestoreGeneration_;
}

bool MainWindow::maybeSaveBeforeContinue()
{
    return documentSection_->maybeSaveBeforeContinue();
}

bool MainWindow::maybeSaveCurrentFieldChanges()
{
    return documentSection_->maybeSaveCurrentFieldChanges();
}

bool MainWindow::applyCurrentFieldToDocument()
{
    return documentSection_->applyCurrentFieldToDocument();
}

void MainWindow::onManagePerDifficultyDesigners()
{
    documentSection_->openPerDifficultyDesignerDialog();
}

void MainWindow::onNewFile()
{
    documentSection_->onNewFile();
}

void MainWindow::onOpenFile()
{
    documentSection_->onOpenFile();
}

void MainWindow::onOpenCurrentFolder()
{
    const QFileInfo fileInfo(currentFilePath_);
    const QString folderPath = currentFilePath_.isEmpty()
        ? QString()
        : fileInfo.absoluteDir().absolutePath();
    if (!folderPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
    }
}

void MainWindow::addRecentFilePath(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return;
    }
    recentFilePaths_.removeAll(normalizedPath);
    recentFilePaths_.prepend(normalizedPath);
    while (recentFilePaths_.size() > 10) {
        recentFilePaths_.removeLast();
    }
    savePortableState();
}

QVariantList MainWindow::recentDocumentEntries()
{
    QStringList existing;
    QSet<QString> seen;
    QVariantList entries;
    for (const QString& path : recentFilePaths_) {
        const QString normalized = path.isEmpty() ? QString() : QDir::cleanPath(path);
        if (normalized.isEmpty() || seen.contains(normalized)) {
            continue;
        }
        const QFileInfo info(normalized);
        if (!info.exists() || !info.isFile()) {
            continue;
        }
        seen.insert(normalized);
        existing.append(normalized);
        // The folder is the song; the file inside it is always maidata.txt, so
        // the folder name is the only part that tells entries apart.
        const QString folderName = info.absoluteDir().dirName().trimmed();
        entries.append(QVariantMap{
            {QStringLiteral("path"), normalized},
            {QStringLiteral("label"), folderName.isEmpty()
                 ? QDir::toNativeSeparators(info.absoluteFilePath())
                 : folderName},
        });
    }
    if (existing != recentFilePaths_) {
        recentFilePaths_ = existing;
        savePortableState();
    }
    return entries;
}

void MainWindow::restoreBackupDocument(const QString& path)
{
    restoreBackupFilePath(path);
}

void MainWindow::noteRecentDocument(const QString& path)
{
    addRecentFilePath(path);
}

QVariantList MainWindow::backupDocumentEntries()
{
    return documentSection_->backupDocumentEntries();
}

void MainWindow::openRecentFilePath(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return;
    }
    const QFileInfo fileInfo(normalizedPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        recentFilePaths_.removeAll(normalizedPath);
        savePortableState();
        UiDialogs::showMessageBox(QMessageBox::Warning, this, UiText::text(QStringLiteral("action.open_recent")), "File no longer exists:\n" + normalizedPath);
        return;
    }
    if (!maybeSaveBeforeContinue()) {
        return;
    }
    openFileAtPath(normalizedPath, true, true);
}

void MainWindow::restoreBackupFilePath(const QString& path)
{
    documentSection_->restoreBackupFilePath(path);
}

void MainWindow::refreshRecentFilesMenu(QMenu* recentFilesMenu)
{
    if (recentFilesMenu == nullptr) {
        return;
    }
    recentFilesMenu->clear();
    QStringList existingPaths;
    QSet<QString> seenPaths;
    for (const QString& path : recentFilePaths_) {
        const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
        if (normalizedPath.isEmpty() || seenPaths.contains(normalizedPath)) {
            continue;
        }
        const QFileInfo fileInfo(normalizedPath);
        if (!fileInfo.exists() || !fileInfo.isFile()) {
            continue;
        }
        seenPaths.insert(normalizedPath);
        existingPaths.append(normalizedPath);
    }
    if (existingPaths != recentFilePaths_) {
        recentFilePaths_ = existingPaths;
        savePortableState();
    }
    if (existingPaths.isEmpty()) {
        QAction* emptyAction = recentFilesMenu->addAction(UiText::text(QStringLiteral("action.open_recent.empty")));
        emptyAction->setEnabled(false);
        return;
    }
    for (const QString& path : existingPaths) {
        const QFileInfo fileInfo(path);
        const QString folderName = fileInfo.absoluteDir().dirName().trimmed();
        const QString displayName = folderName.isEmpty()
            ? QDir::toNativeSeparators(fileInfo.absoluteFilePath())
            : folderName;
        QAction* action = recentFilesMenu->addAction(displayName);
        action->setToolTip(QDir::toNativeSeparators(path));
        action->setStatusTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path]() {
            openRecentFilePath(path);
        });
    }
}

void MainWindow::refreshRestoreBackupMenu(QMenu* restoreBackupMenu)
{
    documentSection_->refreshRestoreBackupMenu(restoreBackupMenu);
}

bool MainWindow::openFileAtPath(const QString& path, bool showStatusMessage, bool showErrors)
{
    return documentSection_->openFileAtPath(path, showStatusMessage, showErrors);
}

// openStartupTarget runs at launch and from the chart-drop switch prompt, both
// of which the v2 shell reaches, so what it has to say goes to the shell.
void MainWindow::postShellNotice(const QString& title, const QString& text)
{
    if (miacode::v2::UiRequestService* const requests = uiRequestService()) {
        requests->postNotice(miacode::v2::NoticeSeverity::Warning, title, text);
    }
}

bool MainWindow::openStartupTarget(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    const QFileInfo info(normalizedPath);
    if (info.isDir()) {
        const QString maidataPath = QDir(info.absoluteFilePath()).filePath(QStringLiteral("maidata.txt"));
        if (QFileInfo::exists(maidataPath) && QFileInfo(maidataPath).isFile()) {
            return openFileAtPath(maidataPath, true, true);
        }

        setCurrentFilePath(QString(), true);
        applicationServices_.workspace().openSource(SimaiDocument::createEmpty().toText());
        loadDocument();
        postShellNotice(
            UiText::text(QStringLiteral("dialog.open_startup_folder.missing_maidata.title")),
            UiText::text(QStringLiteral("dialog.open_startup_folder.missing_maidata.message"))
                .arg(QDir::toNativeSeparators(info.absoluteFilePath()))
        );
        return false;
    }

    if (info.exists() && info.isFile()) {
        return openFileAtPath(info.absoluteFilePath(), true, true);
    }

    postShellNotice(
        UiText::text(QStringLiteral("dialog.open_startup_target.missing.title")),
        UiText::text(QStringLiteral("dialog.open_startup_target.missing.message"))
            .arg(QDir::toNativeSeparators(normalizedPath))
    );
    return false;
}

bool MainWindow::restoreLastSessionFile()
{
    return documentSection_->restoreLastSessionFile();
}

void MainWindow::scheduleStartupRestoreLastSessionFile()
{
    documentSection_->scheduleStartupRestoreLastSessionFile();
}

void MainWindow::cancelPendingStartupRestore()
{
    documentSection_->cancelPendingStartupRestore();
}

void MainWindow::applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared)
{
    documentSection_->applyPreparedStartupRestoreDocument(prepared);
}

void MainWindow::applyOpenedDocumentState(
    const QString& normalizedPath,
    TextEncoding encodingUsed,
    const SimaiDocument& document,
    bool showStatusMessage,
    double knownTrackDurationSeconds)
{
    documentSection_->applyOpenedDocumentState(
        normalizedPath,
        encodingUsed,
        document,
        showStatusMessage,
        knownTrackDurationSeconds
    );
}

void MainWindow::resetAutosaveState(const QString& referenceText)
{
    documentSection_->resetAutosaveState(referenceText);
}

QString MainWindow::resolveAutosaveDirectoryPath() const
{
    return documentSection_->resolveAutosaveDirectoryPath();
}

QString MainWindow::currentDocumentTextForAutosave() const
{
    return documentSection_->currentDocumentTextForAutosave();
}

void MainWindow::pruneAutosaveFiles(const QString& autosaveDirectoryPath) const
{
    documentSection_->pruneAutosaveFiles(autosaveDirectoryPath);
}

void MainWindow::runAutosaveCheck(bool allowHistory)
{
    documentSection_->runAutosaveCheck(allowHistory);
}

bool MainWindow::onSaveFile()
{
    return documentSection_->onSaveFile();
}

bool MainWindow::onSaveFileAs()
{
    return documentSection_->onSaveFileAs();
}

bool MainWindow::saveToPath(const QString& path)
{
    return documentSection_->saveToPath(path);
}

void MainWindow::handleAudioDrop(const QStringList& audioPaths,
                                 quint64 requestId,
                                 quint64 generation,
                                 miacode::v2::ChartDropImportService::Completion completion)
{
    if (documentSection_ == nullptr || chartDropImportService_ == nullptr) {
        if (completion) {
            completion({requestId, generation, true, true, true, 0,
                        static_cast<int>(audioPaths.size()), {}});
        }
        return;
    }
    chartDropImportService_->submit(
        audioPaths,
        requestId,
        generation,
        documentSection_->chartDropImportAdapter(),
        std::move(completion));
}

void MainWindow::releaseChartDropImportService()
{
    if (chartDropImportService_ != nullptr) {
        chartDropImportService_->release();
    }
}

void MainWindow::setMetadataExtraText(const QString& text)
{
    documentSection_->setMetadataExtraText(text);
}


void MainWindow::updatePauseButtonAppearance()
{
    documentSection_->updatePauseButtonAppearance();
}

void MainWindow::updateDirtyState()
{
    documentSection_->updateDirtyState();
}

void MainWindow::refreshCurrentFieldDirtyState()
{
    documentSection_->refreshCurrentFieldDirtyState();
}

void MainWindow::markCurrentFieldDirty()
{
    documentSection_->markCurrentFieldDirty();
}

void MainWindow::clearDeletedDifficultyUndoState()
{
    documentSection_->clearDeletedDifficultyUndoState();
}

bool MainWindow::undoDeletedDifficultyField()
{
    return documentSection_->undoDeletedDifficultyField();
}

void MainWindow::updateEditorHeader()
{
    documentSection_->updateEditorHeader();
}

void MainWindow::updateDifficultyScopedActionStates()
{
    documentSection_->updateDifficultyScopedActionStates();
}

void MainWindow::updateEditorHeaderLayoutMode()
{
    documentSection_->updateEditorHeaderLayoutMode();
}

void MainWindow::syncEditorHeaderMinimumWidth()
{
    documentSection_->syncEditorHeaderMinimumWidth();
}

void MainWindow::updateEditorStatus()
{
    documentSection_->updateEditorStatus();
}

void MainWindow::updateEditorEmptyState()
{
    documentSection_->updateEditorEmptyState();
}

void MainWindow::updateMetadataPageMode()
{
    documentSection_->updateMetadataPageMode();
}

bool MainWindow::deleteDifficultyField(int difficultyId)
{
    return documentSection_->deleteDifficultyField(difficultyId);
}

void MainWindow::rebuildFieldSidebar()
{
    documentSection_->rebuildFieldSidebar();
}

void MainWindow::populateMetadataPage()
{
    documentSection_->populateMetadataPage();
}

void MainWindow::populateDifficultyPage(int difficultyId)
{
    documentSection_->populateDifficultyPage(difficultyId);
}

bool MainWindow::switchToMetadataField()
{
    return documentSection_->switchToMetadataField();
}

bool MainWindow::switchToWelcomePage()
{
    return documentSection_->switchToWelcomePage();
}

bool MainWindow::switchToDifficultyField(int difficultyId)
{
    return documentSection_->switchToDifficultyField(difficultyId);
}

bool MainWindow::switchToLatencyField()
{
    return documentSection_->switchToLatencyField();
}

bool MainWindow::switchToExportField()
{
    return documentSection_->switchToExportField();
}

// ---- miacode::v2::EditorPageRouter ----
//
// The QML page host names the router, never these switchTo*Field entry points:
// they still drive the hidden QStackedWidget, and that half of the work leaves
// with the window in stage 4.

bool MainWindow::enterDifficultyPage(int difficultyId)
{
    return switchToDifficultyField(difficultyId);
}

bool MainWindow::enterMetadataPage()
{
    return switchToMetadataField();
}

bool MainWindow::enterLatencyPage()
{
    return switchToLatencyField();
}

bool MainWindow::enterExportPage()
{
    return switchToExportField();
}

void MainWindow::packChartAsZip()
{
    onPackAsZip();
}

void MainWindow::openPreferences()
{
    onPreferences();
}

void MainWindow::activateInitialField()
{
    documentSection_->activateInitialField();
}

void MainWindow::loadDocument()
{
    documentSection_->loadDocument();
}

void MainWindow::clearTimelineAndPreview()
{
    documentSection_->clearTimelineAndPreview();
}
