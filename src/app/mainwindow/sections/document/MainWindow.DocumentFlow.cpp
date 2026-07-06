#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
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
        : uiText("dialog.unsaved_field_changes.field.metadata", "Metadata");
    QElapsedTimer dialogTimer;
    dialogTimer.start();
    const UnsavedChangesChoice choice = showUnsavedChangesDialog(
        &owner_,
        uiText("dialog.unsaved_field_changes.title", "Unsaved Field Changes"),
        uiText("dialog.unsaved_field_changes.message", "%1 has unsaved changes. Save before switch?").arg(fieldName)
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
        QElapsedTimer applyTimer;
        applyTimer.start();
        const bool wasDocumentDirty = state_.documentDirty_;
        const bool applied = applyCurrentFieldToDocument();
        if (applied) {
            state_.documentDirty_ = wasDocumentDirty;
            updateDirtyState();
            owner_.updateWindowTitle();
        }
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("apply_current_field_to_document"),
            applyTimer.elapsed(),
            QStringLiteral("trigger=unsaved_field_changes result=%1 field=%2")
                .arg(applied ? QStringLiteral("applied") : QStringLiteral("failed"), fieldName)
        );
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/document"),
            QStringLiteral("maybe_save_current_field_changes"),
            totalTimer.elapsed(),
            QStringLiteral("result=%1 field=%2")
                .arg(applied ? QStringLiteral("saved") : QStringLiteral("save_failed"), fieldName)
        );
        return applied;
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
    bool changed = false;
    bool metadataTimingChanged = false;
    bool designerBroadcastNeeded = false;
    QString broadcastDesignerValue;
    if (owner_.hasActiveDifficulty()) {
        SimaiDifficultyData& difficultyData = state_.document_.ensureDifficulty(state_.activeDifficultyId_);
        const QString newLevel = ui_.difficultyLevelEdit_ != nullptr ? ui_.difficultyLevelEdit_->text() : difficultyData.level;
        const QString newChart = owner_.editorText();
        if (difficultyData.level != newLevel || difficultyData.chart != newChart) {
            difficultyData.level = newLevel;
            difficultyData.chart = newChart;
            changed = true;
        }
        // Header designer edit (visible in 顶部显示=谱师 mode; while hidden it
        // mirrors the model, so this block is a no-op). Editing it under
        // unified mode broadcasts the new name, same as editing the top &des.
        const QString newDesigner = ui_.difficultyDesignerEdit_ != nullptr
            ? ui_.difficultyDesignerEdit_->text()
            : difficultyData.designer;
        if (difficultyData.designer != newDesigner) {
            difficultyData.designer = newDesigner;
            changed = true;
            if (state_.unifiedDesignerEnabled_) {
                designerBroadcastNeeded = true;
                broadcastDesignerValue = newDesigner;
            }
        }
        const QString newFirst = ui_.firstEdit_ != nullptr ? ui_.firstEdit_->text() : state_.document_.first;
        if (state_.document_.first != newFirst) {
            state_.document_.first = newFirst;
            metadataTimingChanged = true;
            changed = true;
        }
    } else {
        const QString newTitle = ui_.titleEdit_ != nullptr ? ui_.titleEdit_->text() : QString();
        const QString newArtist = ui_.artistEdit_ != nullptr ? ui_.artistEdit_->text() : QString();
        const QString newDesigner = ui_.designerEdit_ != nullptr ? ui_.designerEdit_->text() : QString();
        QVector<SimaiRawField> newExtraFields = SimaiDocument::parseUnmanagedFields(
            ui_.metadataExtraEdit_ != nullptr ? ui_.metadataExtraEdit_->toPlainText() : QString(),
            true
        );
        SimaiDocument::ensureDefaultClockCount(&newExtraFields);
        const bool designerEdited = (state_.document_.designer != newDesigner);
        if (state_.document_.title != newTitle
            || state_.document_.artist != newArtist
            || designerEdited
            || state_.document_.extraFields != newExtraFields) {
            state_.document_.title = newTitle;
            state_.document_.artist = newArtist;
            state_.document_.designer = newDesigner;
            state_.document_.extraFields = newExtraFields;
            changed = true;
        }
        if (state_.unifiedDesignerEnabled_ && designerEdited) {
            designerBroadcastNeeded = true;
            broadcastDesignerValue = newDesigner;
        }
    }

    // Broadcast designer name to every other slot when the "unified" option
    // is enabled. We do this *after* the just-edited field has been applied
    // so the value being broadcast is always the new one. The edit can
    // originate from the top &des (metadata page) or from the header's
    // per-difficulty field (顶部显示=谱师 mode) — mirror whichever line edit
    // was NOT the source so both read the canonical name afterwards.
    if (designerBroadcastNeeded) {
        if (state_.document_.designer != broadcastDesignerValue) {
            state_.document_.designer = broadcastDesignerValue;
            changed = true;
        }
        // Charted difficulties AND chart-less standalone &des_N both follow the
        // unified name. setDesignerForSlot("") clears a standalone entry, which
        // is the correct "clear all" behaviour when broadcasting an empty name.
        const QVector<QPair<int, QString>> designerSlots = state_.document_.perDifficultyDesigners();
        for (const QPair<int, QString>& slot : designerSlots) {
            if (slot.second != broadcastDesignerValue) {
                state_.document_.setDesignerForSlot(slot.first, broadcastDesignerValue);
                changed = true;
            }
        }
        if (ui_.designerEdit_ != nullptr && ui_.designerEdit_->text() != broadcastDesignerValue) {
            QSignalBlocker block(ui_.designerEdit_);
            ui_.designerEdit_->setText(broadcastDesignerValue);
        }
        syncHeaderDesignerEditFromModel();
    }

    anchorCurrentFieldCleanState();
    state_.currentFieldDirty_ = false;
    if (changed) {
        state_.documentDirty_ = true;
    }
    updateDirtyState();
    owner_.updateWindowTitle();
    rebuildFieldSidebar();
    if (metadataTimingChanged) {
        owner_.refreshWaveformCache();
        owner_.refreshTimelineMetadata();
    }
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

bool MainWindow::DocumentSection::applyBatchTransform(const QString& opName, const BatchTransform& transform)
{
    MC_OP("MainWindow::DocumentSection::applyBatchTransform");
    _mc_op_.note(QStringLiteral("op=%1").arg(opName));
    const QString original = owner_.editorText();
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    int changed = 0;
    const QString transformed = transform(original, &changed);
    if (transformed == original) {
        owner_.statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

    QTextCursor editCursor = oldCursor;
    editCursor.beginEditBlock();
    editCursor.select(QTextCursor::Document);
    editCursor.insertText(transformed);
    editCursor.endEditBlock();

    QTextCursor restoredCursor(editor->document());
    const int maxPos = editor->document()->characterCount() - 1;
    const int restoredAnchor = qBound(0, oldCursor.anchor(), maxPos);
    const int restoredPosition = qBound(0, oldCursor.position(), maxPos);
    restoredCursor.setPosition(restoredAnchor);
    restoredCursor.setPosition(restoredPosition, QTextCursor::KeepAnchor);
    editor->setTextCursor(restoredCursor);
    if (editor->verticalScrollBar() != nullptr) {
        editor->verticalScrollBar()->setValue(qBound(
            editor->verticalScrollBar()->minimum(),
            oldVScroll,
            editor->verticalScrollBar()->maximum()
        ));
    }
    if (editor->horizontalScrollBar() != nullptr) {
        editor->horizontalScrollBar()->setValue(qBound(
            editor->horizontalScrollBar()->minimum(),
            oldHScroll,
            editor->horizontalScrollBar()->maximum()
        ));
    }

    markCurrentFieldDirty();
    state_.lastPreviewNoteMarkerSignature_.clear();
    owner_.refreshTimelineMetadata();
    owner_.statusBar()->showMessage(QString("%1 applied: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

bool MainWindow::DocumentSection::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
{
    if (!transform) {
        return false;
    }
    return applySelectionBatchTransform(
        opName,
        [transform](const QString& selected, const QString&, int* changedCount) {
            return transform(selected, changedCount);
        });
}

bool MainWindow::DocumentSection::applySelectionBatchTransform(const QString& opName, const SelectionContextBatchTransform& transform)
{
    MC_OP("MainWindow::DocumentSection::applySelectionBatchTransform");
    _mc_op_.note(QStringLiteral("op=%1").arg(opName));
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    int startPos = -1;
    int endPos = -1;
    if (!currentSelectionRange(&startPos, &endPos)) {
        owner_.statusBar()->showMessage(QString("%1: no selection.").arg(opName));
        return false;
    }

    const QString original = owner_.editorText();
    const int begin = qMin(startPos, endPos);
    const int finish = qMax(startPos, endPos);
    if (begin < 0 || finish <= begin || finish > original.size()) {
        owner_.statusBar()->showMessage(QString("%1: invalid selection range.").arg(opName));
        return false;
    }

    const QString selected = original.mid(begin, finish - begin);
    const QString suffixContext = original.mid(finish);
    int changed = 0;
    const QString transformed = transform(selected, suffixContext, &changed);
    if (transformed == selected) {
        owner_.statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

    const bool forwardSelection = oldCursor.hasSelection()
        ? (oldCursor.position() >= oldCursor.anchor())
        : true;
    const int originalAnchor = forwardSelection ? begin : finish;
    const int originalPosition = forwardSelection ? finish : begin;

    QTextCursor editCursor = oldCursor;
    editCursor.beginEditBlock();
    editCursor.setPosition(begin);
    editCursor.setPosition(finish, QTextCursor::KeepAnchor);
    editCursor.insertText(transformed);
    editCursor.endEditBlock();

    QTextCursor restoredCursor(editor->document());
    const int maxPos = editor->document()->characterCount() - 1;
    const int transformedEnd = begin + transformed.size();
    const int restoredAnchor = qBound(0, forwardSelection ? begin : transformedEnd, maxPos);
    const int restoredPosition = qBound(0, forwardSelection ? transformedEnd : begin, maxPos);
    restoredCursor.setPosition(restoredAnchor);
    restoredCursor.setPosition(restoredPosition, QTextCursor::KeepAnchor);
    editor->setTextCursor(restoredCursor);
    recordChartSelectionTransformUndoEntry(originalAnchor, originalPosition, restoredCursor);
    if (editor->verticalScrollBar() != nullptr) {
        editor->verticalScrollBar()->setValue(qBound(
            editor->verticalScrollBar()->minimum(),
            oldVScroll,
            editor->verticalScrollBar()->maximum()
        ));
    }
    if (editor->horizontalScrollBar() != nullptr) {
        editor->horizontalScrollBar()->setValue(qBound(
            editor->horizontalScrollBar()->minimum(),
            oldHScroll,
            editor->horizontalScrollBar()->maximum()
        ));
    }

    markCurrentFieldDirty();
    state_.lastPreviewNoteMarkerSignature_.clear();
    owner_.refreshTimelineMetadata();
    owner_.statusBar()->showMessage(QString("%1 applied on selection: %2 replacement(s).").arg(opName).arg(changed));
    return true;
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
        UiDialogs::showMessageBox(QMessageBox::Warning, this, uiText("action.open_recent", "Open Recent"), "File no longer exists:\n" + normalizedPath);
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
        QAction* emptyAction = recentFilesMenu->addAction(uiText("action.open_recent.empty", "No Recent Files"));
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
        loadDocument(SimaiDocument::createEmpty());
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            uiText("dialog.open_startup_folder.missing_maidata.title", "maidata.txt Not Found"),
            uiText(
                "dialog.open_startup_folder.missing_maidata.message",
                QStringLiteral("No maidata.txt was found in the dropped folder:\n%1")
                    .arg(QDir::toNativeSeparators(info.absoluteFilePath()))
            )
        );
        return false;
    }

    if (info.exists() && info.isFile()) {
        return openFileAtPath(info.absoluteFilePath(), true, true);
    }

    UiDialogs::showMessageBox(
        QMessageBox::Warning,
        this,
        uiText("dialog.open_startup_target.missing.title", "Open Failed"),
        uiText(
            "dialog.open_startup_target.missing.message",
            QStringLiteral("The dropped file or folder does not exist:\n%1")
                .arg(QDir::toNativeSeparators(normalizedPath))
        )
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

bool MainWindow::applyBatchTransform(const QString& opName, const BatchTransform& transform)
{
    return documentSection_->applyBatchTransform(opName, transform);
}

bool MainWindow::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
{
    return documentSection_->applySelectionBatchTransform(opName, transform);
}

std::pair<int, int> MainWindow::currentCursorLineCol() const
{
    return documentSection_->currentCursorLineCol();
}

std::pair<int, int> MainWindow::currentSelectionOrCursorLineCol() const
{
    return documentSection_->currentSelectionOrCursorLineCol();
}

bool MainWindow::currentSelectionRange(int* startPos, int* endPos) const
{
    return documentSection_->currentSelectionRange(startPos, endPos);
}

void MainWindow::setMetadataExtraText(const QString& text)
{
    documentSection_->setMetadataExtraText(text);
}

void MainWindow::setEditorText(const QString& text)
{
    documentSection_->setEditorText(text);
}

void MainWindow::updatePauseButtonAppearance()
{
    documentSection_->updatePauseButtonAppearance();
}

void MainWindow::updateDirtyState()
{
    documentSection_->updateDirtyState();
}

bool MainWindow::currentFieldHasUndoChanges() const
{
    return documentSection_->currentFieldHasUndoChanges();
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

void MainWindow::activateInitialField()
{
    documentSection_->activateInitialField();
}

void MainWindow::loadDocument(const SimaiDocument& document)
{
    documentSection_->loadDocument(document);
}

void MainWindow::clearTimelineAndPreview()
{
    documentSection_->clearTimelineAndPreview();
}
