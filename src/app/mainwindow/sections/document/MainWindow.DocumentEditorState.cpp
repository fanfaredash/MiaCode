#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"

#include "BracketScopeHighlighter.h"
#include "PlainCodeEditor.h"
#include "TimelineView.h"
#include "UiText.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "preview/runtime/PreviewRuntime.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

QString cursorSummary(const QTextCursor& cursor)
{
    return QStringLiteral(
        "anchor=%1 pos=%2 has_selection=%3 sel_start=%4 sel_end=%5 sel_len=%6")
        .arg(cursor.anchor())
        .arg(cursor.position())
        .arg(cursor.hasSelection() ? 1 : 0)
        .arg(cursor.selectionStart())
        .arg(cursor.selectionEnd())
        .arg(qAbs(cursor.position() - cursor.anchor()));
}

QString widgetSummary(QWidget* widget)
{
    if (widget == nullptr) {
        return QStringLiteral("null");
    }
    return QStringLiteral("%1(name=%2 ptr=0x%3)")
        .arg(widget->metaObject() != nullptr ? widget->metaObject()->className() : QStringLiteral("unknown"))
        .arg(widget->objectName().isEmpty() ? QStringLiteral("(none)") : widget->objectName())
        .arg(reinterpret_cast<quintptr>(widget), 0, 16);
}

void logSelectionRestore(const QString& scope, const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("selection_restore/%1").arg(scope),
        payload,
        true
    );
}

}  // namespace

std::pair<int, int> MainWindow::DocumentSection::currentCursorLineCol() const
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

void MainWindow::DocumentSection::pruneChartSelectionTransformUndoEntriesFromStep(int undoStepThreshold)
{
    const qsizetype beforeSize = state_.chartSelectionTransformUndoEntries_.size();
    for (qsizetype index = state_.chartSelectionTransformUndoEntries_.size() - 1; index >= 0; --index) {
        if (state_.chartSelectionTransformUndoEntries_.at(index).undoStepAfterApply >= undoStepThreshold) {
            state_.chartSelectionTransformUndoEntries_.removeAt(index);
        }
    }
    const qsizetype removedCount = beforeSize - state_.chartSelectionTransformUndoEntries_.size();
    if (removedCount > 0) {
        logSelectionRestore(
            QStringLiteral("prune"),
            QStringLiteral("threshold=%1 removed=%2 remaining=%3")
                .arg(undoStepThreshold)
                .arg(removedCount)
                .arg(state_.chartSelectionTransformUndoEntries_.size())
        );
    }
}

void MainWindow::DocumentSection::updateLastObservedChartEditorUndoRedoSteps()
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr) {
        state_.lastObservedEditorUndoSteps_ = 0;
        state_.lastObservedEditorRedoSteps_ = 0;
        return;
    }
    state_.lastObservedEditorUndoSteps_ = editor->document()->availableUndoSteps();
    state_.lastObservedEditorRedoSteps_ = editor->document()->availableRedoSteps();
}

void MainWindow::DocumentSection::clearChartSelectionTransformUndoEntries()
{
    state_.chartSelectionTransformUndoEntries_.clear();
    updateLastObservedChartEditorUndoRedoSteps();
}

void MainWindow::DocumentSection::syncChartSelectionTransformUndoState()
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr) {
        state_.chartSelectionTransformUndoEntries_.clear();
        state_.lastObservedEditorUndoSteps_ = 0;
        state_.lastObservedEditorRedoSteps_ = 0;
        return;
    }

    const int currentUndoSteps = editor->document()->availableUndoSteps();
    const int currentRedoSteps = editor->document()->availableRedoSteps();
    if (state_.editorSelectionUndoRedoRestoreInProgress_) {
        state_.lastObservedEditorUndoSteps_ = currentUndoSteps;
        state_.lastObservedEditorRedoSteps_ = currentRedoSteps;
        return;
    }
    if (state_.lastObservedEditorRedoSteps_ > 0 && currentRedoSteps == 0) {
        pruneChartSelectionTransformUndoEntriesFromStep(currentUndoSteps);
        logSelectionRestore(
            QStringLiteral("sync_steps"),
            QStringLiteral("redo_branch_cleared current_undo=%1 current_redo=%2")
                .arg(currentUndoSteps)
                .arg(currentRedoSteps)
        );
    }
    state_.lastObservedEditorUndoSteps_ = currentUndoSteps;
    state_.lastObservedEditorRedoSteps_ = currentRedoSteps;
}

void MainWindow::DocumentSection::recordChartSelectionTransformUndoEntry(
    int originalAnchor,
    int originalPosition,
    const QTextCursor& transformedCursor)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr) {
        return;
    }

    const int undoStepAfterApply = editor->document()->availableUndoSteps();
    if (undoStepAfterApply <= 0) {
        return;
    }

    pruneChartSelectionTransformUndoEntriesFromStep(undoStepAfterApply);
    SelectionTransformUndoEntry entry;
    entry.undoStepAfterApply = undoStepAfterApply;
    entry.originalAnchor = originalAnchor;
    entry.originalPosition = originalPosition;
    entry.transformedAnchor = transformedCursor.anchor();
    entry.transformedPosition = transformedCursor.position();
    state_.chartSelectionTransformUndoEntries_.append(entry);
    logSelectionRestore(
        QStringLiteral("record"),
        QStringLiteral("undo_step=%1 original={%2} transformed={%3} entries=%4")
            .arg(undoStepAfterApply)
            .arg(QStringLiteral(
                     "anchor=%1 pos=%2 has_selection=%3 sel_start=%4 sel_end=%5 sel_len=%6")
                     .arg(originalAnchor)
                     .arg(originalPosition)
                     .arg(originalAnchor != originalPosition ? 1 : 0)
                     .arg(qMin(originalAnchor, originalPosition))
                     .arg(qMax(originalAnchor, originalPosition))
                     .arg(qAbs(originalPosition - originalAnchor)))
            .arg(cursorSummary(transformedCursor))
            .arg(state_.chartSelectionTransformUndoEntries_.size())
    );
    updateLastObservedChartEditorUndoRedoSteps();
}

void MainWindow::DocumentSection::recordChartSelectionUndoRestoreAfterNextEdit(
    int originalAnchor,
    int originalPosition)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr) {
        return;
    }
    if (originalAnchor == originalPosition || state_.editorSelectionUndoRedoRestoreInProgress_) {
        return;
    }

    const int undoStepsBefore = editor->document()->availableUndoSteps();
    const QPointer<PlainCodeEditor> guard(editor);
    QTimer::singleShot(0, editor, [this, guard, originalAnchor, originalPosition, undoStepsBefore]() {
        if (guard.isNull() || guard->document() == nullptr) {
            return;
        }
        if (guard->document()->availableUndoSteps() <= undoStepsBefore) {
            logSelectionRestore(
                QStringLiteral("record_deferred_skip"),
                QStringLiteral("reason=no_new_undo_step before=%1 after=%2 original_anchor=%3 original_pos=%4")
                    .arg(undoStepsBefore)
                    .arg(guard->document()->availableUndoSteps())
                    .arg(originalAnchor)
                    .arg(originalPosition)
            );
            return;
        }
        recordChartSelectionTransformUndoEntry(originalAnchor, originalPosition, guard->textCursor());
    });
}

const MainWindow::SelectionTransformUndoEntry* MainWindow::DocumentSection::findChartSelectionTransformUndoEntry(
    int undoStepAfterApply) const
{
    for (const SelectionTransformUndoEntry& entry : state_.chartSelectionTransformUndoEntries_) {
        if (entry.undoStepAfterApply == undoStepAfterApply) {
            return &entry;
        }
    }
    return nullptr;
}

bool MainWindow::DocumentSection::restoreChartSelectionTransformCursor(
    const SelectionTransformUndoEntry& entry,
    bool transformedSelection)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr) {
        return false;
    }

    QTextDocument* document = editor->document();
    const int maxPosition = qMax(0, document->characterCount() - 1);
    const int anchor = transformedSelection ? entry.transformedAnchor : entry.originalAnchor;
    const int position = transformedSelection ? entry.transformedPosition : entry.originalPosition;
    if (anchor < 0 || position < 0) {
        return false;
    }

    const int boundedAnchor = qBound(0, anchor, maxPosition);
    const int boundedPosition = qBound(0, position, maxPosition);
    const auto applySelection = [boundedAnchor, boundedPosition](PlainCodeEditor* target) {
        if (target == nullptr || target->document() == nullptr) {
            return;
        }
        QTextDocument* targetDocument = target->document();
        const int targetMaxPosition = qMax(0, targetDocument->characterCount() - 1);
        QTextCursor cursor(targetDocument);
        cursor.setPosition(qBound(0, boundedAnchor, targetMaxPosition));
        cursor.setPosition(qBound(0, boundedPosition, targetMaxPosition), QTextCursor::KeepAnchor);
        target->setTextCursor(cursor);
    };

    logSelectionRestore(
        QStringLiteral("restore_begin"),
        QStringLiteral("mode=%1 focus_before=%2 target=%3 requested_anchor=%4 requested_pos=%5 current={%6}")
            .arg(transformedSelection ? QStringLiteral("redo") : QStringLiteral("undo"))
            .arg(widgetSummary(QApplication::focusWidget()))
            .arg(widgetSummary(editor))
            .arg(anchor)
            .arg(position)
            .arg(cursorSummary(editor->textCursor()))
    );
    editor->setFocus(Qt::ShortcutFocusReason);
    applySelection(editor);
    logSelectionRestore(
        QStringLiteral("restore_immediate"),
        QStringLiteral("mode=%1 focus_after=%2 current={%3}")
            .arg(transformedSelection ? QStringLiteral("redo") : QStringLiteral("undo"))
            .arg(widgetSummary(QApplication::focusWidget()))
            .arg(cursorSummary(editor->textCursor()))
    );
    const QPointer<PlainCodeEditor> guard(editor);
    QTimer::singleShot(0, editor, [guard, applySelection, transformedSelection]() {
        if (guard.isNull()) {
            return;
        }
        guard->setFocus(Qt::ShortcutFocusReason);
        applySelection(guard.data());
        logSelectionRestore(
            QStringLiteral("restore_deferred"),
            QStringLiteral("mode=%1 focus_after=%2 current={%3}")
                .arg(transformedSelection ? QStringLiteral("redo") : QStringLiteral("undo"))
                .arg(widgetSummary(QApplication::focusWidget()))
                .arg(cursorSummary(guard->textCursor()))
        );
    });
    return true;
}

bool MainWindow::DocumentSection::undoChartEditorWithSelectionRestore()
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr || !editor->document()->isUndoAvailable()) {
        return false;
    }

    const int undoStepBefore = editor->document()->availableUndoSteps();
    logSelectionRestore(
        QStringLiteral("undo_begin"),
        QStringLiteral("undo_step_before=%1 focus=%2 current={%3}")
            .arg(undoStepBefore)
            .arg(widgetSummary(QApplication::focusWidget()))
            .arg(cursorSummary(editor->textCursor()))
    );
    state_.editorSelectionUndoRedoRestoreInProgress_ = true;
    editor->undo();
    if (const SelectionTransformUndoEntry* entry = findChartSelectionTransformUndoEntry(undoStepBefore); entry != nullptr) {
        logSelectionRestore(
            QStringLiteral("undo_match"),
            QStringLiteral("undo_step=%1 matched=1 original_anchor=%2 original_pos=%3 transformed_anchor=%4 transformed_pos=%5")
                .arg(undoStepBefore)
                .arg(entry->originalAnchor)
                .arg(entry->originalPosition)
                .arg(entry->transformedAnchor)
                .arg(entry->transformedPosition)
        );
        (void)restoreChartSelectionTransformCursor(*entry, false);
    } else {
        logSelectionRestore(
            QStringLiteral("undo_match"),
            QStringLiteral("undo_step=%1 matched=0 post_undo_step=%2 current={%3}")
                .arg(undoStepBefore)
                .arg(editor->document()->availableUndoSteps())
                .arg(cursorSummary(editor->textCursor()))
        );
    }
    state_.editorSelectionUndoRedoRestoreInProgress_ = false;
    updateLastObservedChartEditorUndoRedoSteps();
    return true;
}

bool MainWindow::DocumentSection::redoChartEditorWithSelectionRestore()
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr || editor->document() == nullptr || !editor->document()->isRedoAvailable()) {
        return false;
    }

    logSelectionRestore(
        QStringLiteral("redo_begin"),
        QStringLiteral("undo_step_before=%1 redo_step_before=%2 focus=%3 current={%4}")
            .arg(editor->document()->availableUndoSteps())
            .arg(editor->document()->availableRedoSteps())
            .arg(widgetSummary(QApplication::focusWidget()))
            .arg(cursorSummary(editor->textCursor()))
    );
    state_.editorSelectionUndoRedoRestoreInProgress_ = true;
    editor->redo();
    const int undoStepAfterRedo = editor->document()->availableUndoSteps();
    if (const SelectionTransformUndoEntry* entry = findChartSelectionTransformUndoEntry(undoStepAfterRedo); entry != nullptr) {
        logSelectionRestore(
            QStringLiteral("redo_match"),
            QStringLiteral("undo_step_after=%1 matched=1 original_anchor=%2 original_pos=%3 transformed_anchor=%4 transformed_pos=%5")
                .arg(undoStepAfterRedo)
                .arg(entry->originalAnchor)
                .arg(entry->originalPosition)
                .arg(entry->transformedAnchor)
                .arg(entry->transformedPosition)
        );
        (void)restoreChartSelectionTransformCursor(*entry, true);
    } else {
        logSelectionRestore(
            QStringLiteral("redo_match"),
            QStringLiteral("undo_step_after=%1 matched=0 redo_step_after=%2 current={%3}")
                .arg(undoStepAfterRedo)
                .arg(editor->document()->availableRedoSteps())
                .arg(cursorSummary(editor->textCursor()))
        );
    }
    state_.editorSelectionUndoRedoRestoreInProgress_ = false;
    updateLastObservedChartEditorUndoRedoSteps();
    return true;
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
    // The chart body never carries the &wholebpm metadata line, so feed it to the
    // editor here for the '(' BPM completion list. extraFields is the canonical
    // store; it's refreshed on load / difficulty switch, both of which route
    // through setEditorText().
    QString wholeBpm;
    for (const SimaiRawField& field : std::as_const(state_.document_.extraFields)) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) == 0) {
            wholeBpm = field.value.trimmed();
            break;
        }
    }
    editor->setWholeBpmCandidate(wholeBpm);
    editor->document()->clearUndoRedoStacks();
    editor->document()->setModified(false);
    state_.editorUndoSaveAnchor_ = editor->document()->availableUndoSteps();
    clearChartSelectionTransformUndoEntries();
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
    const bool previewPlaying = state_.qtPreviewPlaying_ || state_.exportIntroLeadInActive_;
    if (previewPlaying) {
        ui_.pausePreviewAction_->setIcon(makePreviewPauseIcon(iconColor));
        ui_.pausePreviewAction_->setText(UiText::text(QStringLiteral("preview.pause")));
    } else {
        ui_.pausePreviewAction_->setIcon(makePreviewPlayIcon(iconColor));
        ui_.pausePreviewAction_->setText(UiText::text(QStringLiteral("preview.play")));
    }
    if (ui_.pausePreviewButton_ != nullptr) {
        ui_.pausePreviewButton_->setText(
            previewPlaying
                ? UiText::text(QStringLiteral("preview.pause"))
                : UiText::text(QStringLiteral("preview.play"))
        );
        ui_.pausePreviewButton_->setStyleSheet(
            state_.previewFullscreenActive_
                ? previewFullscreenPauseButtonStyleSheet(previewPlaying)
                : UiTheme::pausePreviewButtonStyleSheet(previewPlaying)
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
        const bool levelDirty = ui_.difficultyLevelEdit_ != nullptr && ui_.difficultyLevelEdit_->text() != savedLevel;
        // The header's offset field edits the chart-wide &first, not a
        // per-difficulty value.
        const bool offsetDirty = ui_.firstEdit_ != nullptr && ui_.firstEdit_->text() != state_.document_.first;
        // Header designer edit (顶部显示=谱师 mode). While hidden it mirrors the
        // model, so this stays false outside that mode.
        const QString savedDesigner = difficultyData != nullptr ? difficultyData->designer : QString();
        const bool designerDirty =
            ui_.difficultyDesignerEdit_ != nullptr && ui_.difficultyDesignerEdit_->text() != savedDesigner;
        bool chartDirty = false;
        if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
            editor != nullptr && editor->document() != nullptr) {
            chartDirty = editor->document()->availableUndoSteps() != state_.editorUndoSaveAnchor_;
        }
        return levelDirty || offsetDirty || designerDirty || chartDirty;
    }

    if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
        const bool titleDirty = ui_.titleEdit_ != nullptr && ui_.titleEdit_->text() != state_.document_.title;
        const bool artistDirty = ui_.artistEdit_ != nullptr && ui_.artistEdit_->text() != state_.document_.artist;
        const bool designerDirty = ui_.designerEdit_ != nullptr && ui_.designerEdit_->text() != state_.document_.designer;
        bool extraDirty = false;
        if (ui_.metadataExtraEdit_ != nullptr && ui_.metadataExtraEdit_->document() != nullptr) {
            extraDirty = ui_.metadataExtraEdit_->document()->availableUndoSteps() != state_.metadataExtraUndoSaveAnchor_;
        }
        return titleDirty || artistDirty || designerDirty || extraDirty;
    }

    return false;
}

void MainWindow::DocumentSection::anchorCurrentFieldCleanState()
{
    if (owner_.hasActiveDifficulty()) {
        if (ui_.difficultyLevelEdit_ != nullptr) {
            ui_.difficultyLevelEdit_->setModified(false);
        }
        if (ui_.firstEdit_ != nullptr) {
            ui_.firstEdit_->setModified(false);
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
    // Crash-time autosave — push the current document text into the
    // crash-handler's snapshot mailbox so an abnormal exit (segfault,
    // abort, std::terminate) within the next ~milliseconds still
    // produces a recovery file. Cheap: bounded memcpy + atomic store,
    // no disk I/O. The 2-second debounced .bak write below is for
    // routine autosave; this is the per-edit safety net.
    if (!state_.currentFilePath_.isEmpty()) {
        miacode::crash_recovery::updateSnapshot(
            state_.currentFilePath_,
            currentDocumentTextForAutosave());
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
    // Keep the "all difficulties share one designer" invariant intact: the
    // captured snapshot holds whatever name was current at delete time, which
    // may now be stale if the shared name changed before the undo. Re-seed
    // from the canonical &des so the restored difficulty doesn't reintroduce a
    // divergent designer. Mirrors the seed applied when a difficulty is freshly
    // added (see MainWindow.FrameBootstrap.cpp).
    if (state_.unifiedDesignerEnabled_) {
        restoredDifficulty.designer = state_.document_.designer;
    }
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
