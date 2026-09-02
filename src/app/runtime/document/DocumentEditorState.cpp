#include "runtime/document/DocumentSessionHost.h"
#include "runtime/Shared.h"

#include "BracketScopeHighlighter.h"
#include "UiText.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "preview/runtime/PreviewRuntime.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::runtime::shared;

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

void miacode::runtime::DocumentSessionHost::setMetadataExtraText(const QString& text)
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

void miacode::runtime::DocumentSessionHost::updatePauseButtonAppearance()
{
    // The play/pause presentation changed, and the QML transport's button reads
    // the same condition this one does — playing_ OR the export
    // intro's lead-in. setPreviewPlayingFlag announces the first; nothing
    // announced the second, so the intro played with the transport still
    // showing a play button. Announced before the widget guard, because a v1
    // action that no longer exists is not a reason to leave the shell stale,
    // and queued for the same reason that writer is: callers reach here from
    // the middle of a transition.
    QMetaObject::invokeMethod(
        &session_, [this]() { emit session_.presentationChanged(); }, Qt::QueuedConnection);
    if (ui_.pausePreviewAction_ == nullptr) {
        return;
    }
    const QColor iconColor =
        state_.previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : UiTheme::colors().iconPrimary;
    const bool previewPlaying = state_.playing_ || state_.exportIntroLeadInActive_;
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

void miacode::runtime::DocumentSessionHost::updateDirtyState()
{
    const bool dirty = state_.documentDirty_ || state_.currentFieldDirty_;
    if (dirty) {
        if (state_.autosaveDirtySinceMs_ < 0) {
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
    session_.updateWindowTitle();
}

// See PlaybackDocumentPort.h: this is a read-only query, not a relocation of
// appliedQmlWorkspaceRevision_ — the field stays on Session, whose write site
// is DocumentFileFlow.cpp:748.
quint64 miacode::runtime::DocumentSessionHost::appliedWorkspaceRevision() const
{
    return session_.appliedQmlWorkspaceRevision_;
}

bool miacode::runtime::DocumentSessionHost::currentFieldHasUndoChanges() const
{
    if (session_.hasActiveDifficulty()) {
        const SimaiDifficultyData* difficultyData = session_.applicationServices_.workspace().document().difficulty(state_.activeDifficultyId_);
        const QString savedLevel = difficultyData != nullptr ? difficultyData->level : QString();
        const bool levelDirty = ui_.difficultyLevelEdit_ != nullptr && ui_.difficultyLevelEdit_->text() != savedLevel;
        // The header's offset field edits the chart-wide &first, not a
        // per-difficulty value.
        const bool offsetDirty = ui_.firstEdit_ != nullptr && ui_.firstEdit_->text() != session_.applicationServices_.workspace().document().first;
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
        const bool titleDirty = ui_.titleEdit_ != nullptr && ui_.titleEdit_->text() != session_.applicationServices_.workspace().document().title;
        const bool artistDirty = ui_.artistEdit_ != nullptr && ui_.artistEdit_->text() != session_.applicationServices_.workspace().document().artist;
        const bool designerDirty = ui_.designerEdit_ != nullptr && ui_.designerEdit_->text() != session_.applicationServices_.workspace().document().designer;
        bool extraDirty = false;
        if (ui_.metadataExtraEdit_ != nullptr && ui_.metadataExtraEdit_->document() != nullptr) {
            extraDirty = ui_.metadataExtraEdit_->document()->availableUndoSteps() != state_.metadataExtraUndoSaveAnchor_;
        }
        return titleDirty || artistDirty || designerDirty || extraDirty;
    }

    return false;
}

void miacode::runtime::DocumentSessionHost::anchorCurrentFieldCleanState()
{
    if (session_.hasActiveDifficulty()) {
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

void miacode::runtime::DocumentSessionHost::refreshCurrentFieldDirtyState()
{
    state_.currentFieldDirty_ = currentFieldHasUndoChanges();
    updateDirtyState();
}

void miacode::runtime::DocumentSessionHost::noteDocumentEditedForAutosave()
{
    if (ui_.autosaveIdleTimer_ != nullptr) {
        ui_.autosaveIdleTimer_->start();
    }
    // Crash-time autosave — push the current document text into the
    // crash-handler's snapshot mailbox so an abnormal exit (segfault,
    // abort, std::terminate) within the next ~milliseconds still
    // produces a recovery file. Cheap: bounded memcpy + atomic store,
    // no disk I/O. The 2-second debounced .bak write above is for
    // routine autosave; this is the per-edit safety net.
    if (!state_.currentFilePath_.isEmpty()) {
        miacode::crash_recovery::updateSnapshot(
            state_.currentFilePath_,
            currentDocumentTextForAutosave());
    }
}

void miacode::runtime::DocumentSessionHost::markCurrentFieldDirty()
{
    noteDocumentEditedForAutosave();
    refreshCurrentFieldDirtyState();
}

void miacode::runtime::DocumentSessionHost::clearDeletedDifficultyUndoState()
{
    state_.deletedDifficultyUndoState_ = Session::DeletedDifficultyUndoState{};
}

bool miacode::runtime::DocumentSessionHost::undoDeletedDifficultyField()
{
    if (!state_.deletedDifficultyUndoState_.valid) {
        return false;
    }
    if (!applyCurrentFieldToDocument()) {
        return false;
    }

    const Session::DeletedDifficultyUndoState deletedState = state_.deletedDifficultyUndoState_;
    if (!SimaiDocument::isDifficultyId(deletedState.difficultyId)) {
        clearDeletedDifficultyUndoState();
        return false;
    }
    if (session_.applicationServices_.workspace().document().difficulty(deletedState.difficultyId) != nullptr) {
        session_.noteStatus(
            QString("Cannot restore %1 because that difficulty already exists.")
                .arg(SimaiDocument::difficultyName(deletedState.difficultyId))
        );
        return false;
    }

    session_.stopQtPreviewPlayback(true);
    miacode::v2::ChartWorkspace& workspace = session_.applicationServices_.workspace();
    if (!workspace.addDifficulty(deletedState.difficultyId)) {
        return false;
    }
    workspace.replaceDifficultyChart(
        deletedState.difficultyId, deletedState.difficultyData.chart);
    workspace.updateDifficultyField(
        deletedState.difficultyId,
        miacode::v2::ChartWorkspaceDifficultyField::Level,
        deletedState.difficultyData.level);
    const QString designer = state_.unifiedDesignerEnabled_
        ? workspace.document().designer
        : deletedState.difficultyData.designer;
    workspace.updateDifficultyField(
        deletedState.difficultyId,
        miacode::v2::ChartWorkspaceDifficultyField::Designer,
        designer);
    state_.validationCacheByDifficulty_.remove(deletedState.difficultyId);
    state_.documentDirty_ = true;
    state_.currentFieldDirty_ = false;
    updateDirtyState();

    const bool shouldActivateRestoredDifficulty = deletedState.wasActive || !session_.hasActiveDifficulty();
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
        session_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &session_, [this]() { session_.refreshLayoutAfterPageSwitch(); });
    }

    clearDeletedDifficultyUndoState();
    const QString difficultyName = SimaiDocument::difficultyName(deletedState.difficultyId);
    if (state_.currentFilePath_.isEmpty()) {
        session_.noteStatus(QString("Restored %1.").arg(difficultyName));
        return true;
    }
    if (!saveToPath(state_.currentFilePath_)) {
        session_.noteStatus(QString("Restored %1. Changes are still unsaved.").arg(difficultyName));
        return true;
    }
    session_.noteStatus(QString("Restored %1.").arg(difficultyName));
    return true;
}
