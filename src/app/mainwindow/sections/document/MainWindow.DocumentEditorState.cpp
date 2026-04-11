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
    owner_.setWindowModified(state_.documentDirty_ || state_.currentFieldDirty_);
    owner_.updateWindowTitle();
}

bool MainWindow::DocumentSection::currentFieldHasUndoChanges() const
{
    if (owner_.hasActiveDifficulty()) {
        const bool levelDirty = ui_.difficultyLevelEdit_ != nullptr && ui_.difficultyLevelEdit_->isUndoAvailable();
        const bool designerDirty = ui_.difficultyDesignerEdit_ != nullptr && ui_.difficultyDesignerEdit_->isUndoAvailable();
        bool chartDirty = false;
        if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
            editor != nullptr && editor->document() != nullptr) {
            chartDirty = editor->document()->isUndoAvailable();
        }
        return levelDirty || designerDirty || chartDirty;
    }

    if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
        const bool titleDirty = ui_.titleEdit_ != nullptr && ui_.titleEdit_->isUndoAvailable();
        const bool artistDirty = ui_.artistEdit_ != nullptr && ui_.artistEdit_->isUndoAvailable();
        const bool firstDirty = ui_.firstEdit_ != nullptr && ui_.firstEdit_->isUndoAvailable();
        const bool designerDirty = ui_.designerEdit_ != nullptr && ui_.designerEdit_->isUndoAvailable();
        bool extraDirty = false;
        if (ui_.metadataExtraEdit_ != nullptr && ui_.metadataExtraEdit_->document() != nullptr) {
            extraDirty = ui_.metadataExtraEdit_->document()->isUndoAvailable();
        }
        return titleDirty || artistDirty || firstDirty || designerDirty || extraDirty;
    }

    return false;
}

void MainWindow::DocumentSection::refreshCurrentFieldDirtyState()
{
    state_.currentFieldDirty_ = currentFieldHasUndoChanges();
    updateDirtyState();
}

void MainWindow::DocumentSection::markCurrentFieldDirty()
{
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
