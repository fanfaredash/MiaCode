#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"

#include "BracketScopeHighlighter.h"
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
        // The chart body's dirty state is the workspace's save point, not a
        // widget undo-step count; only the header fields are asked here.
        return levelDirty || offsetDirty || designerDirty;
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
