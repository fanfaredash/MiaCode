#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"

#include "BracketScopeHighlighter.h"
#include "PlainCodeEditor.h"
#include "TimelineView.h"
#include "UiText.h"
#include "preview/runtime/PreviewRuntime.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

std::pair<int, int> MainWindow::DocumentSection::currentCursorLineCol() const
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

bool MainWindow::DocumentSection::currentSelectionRange(int* startPos, int* endPos) const
{
    if (startPos == nullptr || endPos == nullptr) {
        return false;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        return false;
    }
    *startPos = cursor.selectionStart();
    *endPos = cursor.selectionEnd();
    return *endPos > *startPos;
}

std::pair<int, int> MainWindow::DocumentSection::currentSelectionOrCursorLineCol() const
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        return currentCursorLineCol();
    }
    cursor.setPosition(cursor.selectionStart());
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

void MainWindow::DocumentSection::setMetadataExtraText(const QString& text)
{
    if (ui_.metadataExtraEdit_ == nullptr) {
        return;
    }
    const bool previousSuppress = state_.suppressTextDirtyTracking_;
    state_.suppressTextDirtyTracking_ = true;
    QSignalBlocker blocker(ui_.metadataExtraEdit_);
    ui_.metadataExtraEdit_->setPlainText(text);
    ui_.metadataExtraEdit_->document()->clearUndoRedoStacks();
    ui_.metadataExtraEdit_->document()->setModified(false);
    state_.metadataExtraUndoSaveAnchor_ = ui_.metadataExtraEdit_->document()->availableUndoSteps();
    applyBlockSpacingToTextEdit(ui_.metadataExtraEdit_, blockSpacingPixelsForPointSize(state_.editorTextFontPointSize_, state_.editorLineSpacingFactor_));
    state_.suppressTextDirtyTracking_ = previousSuppress;
}

void MainWindow::DocumentSection::setEditorText(const QString& text)
{
    const bool previousSuppress = state_.suppressTextDirtyTracking_;
    state_.suppressTextDirtyTracking_ = true;
    QSignalBlocker blocker(ui_.editorWidget_);
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const int blockSpacingPixels = blockSpacingPixelsForPointSize(state_.editorTextFontPointSize_, state_.editorLineSpacingFactor_);
    editor->setPlainText(text);
    editor->setBlockSpacingPixels(blockSpacingPixels);
    editor->document()->clearUndoRedoStacks();
    editor->document()->setModified(false);
    state_.editorUndoSaveAnchor_ = editor->document()->availableUndoSteps();
    // QSignalBlocker suppresses blockCountChanged, so force line-number gutter recompute.
    editor->refreshLineNumberAreaLayout();
    state_.suppressTextDirtyTracking_ = previousSuppress;
}

void MainWindow::DocumentSection::updatePauseButtonAppearance()
{
    if (ui_.pausePreviewAction_ == nullptr) {
        return;
    }
    const QColor iconColor =
        state_.previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : UiTheme::colors().iconPrimary;
    if (state_.qtPreviewPlaying_) {
        ui_.pausePreviewAction_->setIcon(makePreviewPauseIcon(iconColor));
        ui_.pausePreviewAction_->setText(uiText("preview.pause", "Pause"));
    } else {
        ui_.pausePreviewAction_->setIcon(makePreviewPlayIcon(iconColor));
        ui_.pausePreviewAction_->setText(uiText("preview.play", "Play"));
    }
    if (ui_.pausePreviewButton_ != nullptr) {
        ui_.pausePreviewButton_->setText(
            state_.qtPreviewPlaying_
                ? uiText("preview.pause", "Pause")
                : uiText("preview.play", "Play")
        );
        ui_.pausePreviewButton_->setStyleSheet(
            state_.previewFullscreenActive_
                ? previewFullscreenPauseButtonStyleSheet(state_.qtPreviewPlaying_)
                : UiTheme::pausePreviewButtonStyleSheet(state_.qtPreviewPlaying_)
        );
    }
}

void MainWindow::DocumentSection::updateDirtyState()
{
    const bool dirty = state_.documentDirty_ || state_.currentFieldDirty_;
    const bool wasDirty = owner_.isWindowModified();
    if (dirty) {
        if (!wasDirty || state_.autosaveDirtySinceMs_ < 0) {
            state_.autosaveDirtySinceMs_ = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
        }
        if (ui_.autosaveTimer_ != nullptr && !ui_.autosaveTimer_->isActive()) {
            ui_.autosaveTimer_->start();
        }
    } else {
        state_.autosaveDirtySinceMs_ = -1;
        if (ui_.autosaveTimer_ != nullptr) {
            ui_.autosaveTimer_->stop();
        }
        if (ui_.autosaveIdleTimer_ != nullptr) {
            ui_.autosaveIdleTimer_->stop();
        }
    }
    owner_.setWindowModified(dirty);
    owner_.updateWindowTitle();
}

bool MainWindow::DocumentSection::currentFieldHasUndoChanges() const
{
    if (owner_.hasActiveDifficulty()) {
        const SimaiDifficultyData* difficultyData = state_.document_.difficulty(state_.activeDifficultyId_);
        const QString savedLevel = difficultyData != nullptr ? difficultyData->level : QString();
        const QString savedDesigner = difficultyData != nullptr ? difficultyData->designer : QString();
        const bool levelDirty = ui_.difficultyLevelEdit_ != nullptr && ui_.difficultyLevelEdit_->text() != savedLevel;
        const bool designerDirty = ui_.difficultyDesignerEdit_ != nullptr && ui_.difficultyDesignerEdit_->text() != savedDesigner;
        bool chartDirty = false;
        if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
            editor != nullptr && editor->document() != nullptr) {
            chartDirty = editor->document()->availableUndoSteps() != state_.editorUndoSaveAnchor_;
        }
        return levelDirty || designerDirty || chartDirty;
    }

    if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
        const bool titleDirty = ui_.titleEdit_ != nullptr && ui_.titleEdit_->text() != state_.document_.title;
        const bool artistDirty = ui_.artistEdit_ != nullptr && ui_.artistEdit_->text() != state_.document_.artist;
        const bool firstDirty = ui_.firstEdit_ != nullptr && ui_.firstEdit_->text() != state_.document_.first;
        const bool designerDirty = ui_.designerEdit_ != nullptr && ui_.designerEdit_->text() != state_.document_.designer;
        bool extraDirty = false;
        if (ui_.metadataExtraEdit_ != nullptr && ui_.metadataExtraEdit_->document() != nullptr) {
            extraDirty = ui_.metadataExtraEdit_->document()->availableUndoSteps() != state_.metadataExtraUndoSaveAnchor_;
        }
        return titleDirty || artistDirty || firstDirty || designerDirty || extraDirty;
    }

    return false;
}

void MainWindow::DocumentSection::anchorCurrentFieldCleanState()
{
    if (owner_.hasActiveDifficulty()) {
        if (ui_.difficultyLevelEdit_ != nullptr) {
            ui_.difficultyLevelEdit_->setModified(false);
        }
        if (ui_.difficultyDesignerEdit_ != nullptr) {
            ui_.difficultyDesignerEdit_->setModified(false);
        }
        if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
            editor != nullptr && editor->document() != nullptr) {
            editor->document()->setModified(false);
            state_.editorUndoSaveAnchor_ = editor->document()->availableUndoSteps();
        }
        return;
    }

    if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
        if (ui_.titleEdit_ != nullptr) {
            ui_.titleEdit_->setModified(false);
        }
        if (ui_.artistEdit_ != nullptr) {
            ui_.artistEdit_->setModified(false);
        }
        if (ui_.firstEdit_ != nullptr) {
            ui_.firstEdit_->setModified(false);
        }
        if (ui_.designerEdit_ != nullptr) {
            ui_.designerEdit_->setModified(false);
        }
        if (ui_.metadataExtraEdit_ != nullptr && ui_.metadataExtraEdit_->document() != nullptr) {
            ui_.metadataExtraEdit_->document()->setModified(false);
            state_.metadataExtraUndoSaveAnchor_ = ui_.metadataExtraEdit_->document()->availableUndoSteps();
        }
    }
}

void MainWindow::DocumentSection::refreshCurrentFieldDirtyState()
{
    state_.currentFieldDirty_ = currentFieldHasUndoChanges();
    updateDirtyState();
}

void MainWindow::DocumentSection::markCurrentFieldDirty()
{
    if (ui_.autosaveIdleTimer_ != nullptr) {
        ui_.autosaveIdleTimer_->start();
    }
    refreshCurrentFieldDirtyState();
}

void MainWindow::DocumentSection::clearDeletedDifficultyUndoState()
{
    state_.deletedDifficultyUndoState_ = DeletedDifficultyUndoState{};
}

bool MainWindow::DocumentSection::undoDeletedDifficultyField()
{
    if (!state_.deletedDifficultyUndoState_.valid) {
        return false;
    }
    if (!applyCurrentFieldToDocument()) {
        return false;
    }

    const DeletedDifficultyUndoState deletedState = state_.deletedDifficultyUndoState_;
    if (!SimaiDocument::isDifficultyId(deletedState.difficultyId)) {
        clearDeletedDifficultyUndoState();
        return false;
    }
    if (state_.document_.difficulty(deletedState.difficultyId) != nullptr) {
        owner_.statusBar()->showMessage(
            QString("Cannot restore %1 because that difficulty already exists.")
                .arg(SimaiDocument::difficultyName(deletedState.difficultyId))
        );
        return false;
    }

    owner_.stopQtPreviewPlayback(true);
    SimaiDifficultyData& restoredDifficulty = state_.document_.ensureDifficulty(deletedState.difficultyId);
    restoredDifficulty = deletedState.difficultyData;
    restoredDifficulty.id = deletedState.difficultyId;
    state_.validationCacheByDifficulty_.remove(deletedState.difficultyId);
    state_.documentDirty_ = true;
    state_.currentFieldDirty_ = false;
    updateDirtyState();

    const bool shouldActivateRestoredDifficulty = deletedState.wasActive || !owner_.hasActiveDifficulty();
    if (shouldActivateRestoredDifficulty) {
        state_.activeOutlineKey_ = QStringLiteral("chart");
        if (!switchToDifficultyField(deletedState.difficultyId)) {
            return false;
        }
    } else {
        rebuildFieldSidebar();
        updateEditorHeader();
        updateEditorEmptyState();
        updateEditorStatus();
        owner_.saveProjectRenderState();
        owner_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
    }

    clearDeletedDifficultyUndoState();
    const QString difficultyName = SimaiDocument::difficultyName(deletedState.difficultyId);
    if (state_.currentFilePath_.isEmpty()) {
        owner_.statusBar()->showMessage(QString("Restored %1.").arg(difficultyName));
        return true;
    }
    if (!saveToPath(state_.currentFilePath_)) {
        owner_.statusBar()->showMessage(QString("Restored %1. Changes are still unsaved.").arg(difficultyName));
        return true;
    }
    owner_.statusBar()->showMessage(QString("Restored %1.").arg(difficultyName));
    return true;
}
