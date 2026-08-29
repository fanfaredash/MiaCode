#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"
#include "../editor/MainWindow.EditorSection.h"

#include "BracketScopeHighlighter.h"
#include "BusySpinner.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "app/qml_ui/export/QmlExportSession.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <initializer_list>

using namespace miacode::mainwindow::shared;

void MainWindow::DocumentSection::updateEditorHeader()
{
    updateDifficultyScopedActionStates();
    if (ui_.editorContextLabel_ == nullptr) {
        return;
    }
    if (!owner_.hasActiveDifficulty()) {
        if (state_.activeOutlineKey_ == QLatin1String("latency")) {
            ui_.editorContextLabel_->setText(UiText::text(QStringLiteral("document.latency_settings")));
            ui_.editorContextLabel_->setFont(uiAccentFont(12, QFont::DemiBold));
        } else if (state_.activeOutlineKey_ == QLatin1String("export")) {
            ui_.editorContextLabel_->setText(UiText::text(QStringLiteral("editor.export")));
            ui_.editorContextLabel_->setFont(uiAccentFont(12, QFont::DemiBold));
        } else if (state_.document_.difficultyIds().isEmpty() && state_.activeOutlineKey_ == QLatin1String("welcome")) {
            ui_.editorContextLabel_->setText(UiText::text(QStringLiteral("editor.welcome")));
            ui_.editorContextLabel_->setFont(uiAccentFont(15, QFont::DemiBold));
        } else {
            ui_.editorContextLabel_->setText(UiText::text(QStringLiteral("editor.metadata")));
            ui_.editorContextLabel_->setFont(uiAccentFont(12, QFont::DemiBold));
        }
        ui_.editorContextLabel_->setStyleSheet(QString());
        if (ui_.editorDifficultyControls_ != nullptr) {
            ui_.editorDifficultyControls_->hide();
        }
        if (ui_.editorBatchTransformControls_ != nullptr) {
            ui_.editorBatchTransformControls_->hide();
        }
        ui_.editorContextLabel_->setMinimumWidth(0);
        updateEditorHeaderLayoutMode();
        owner_.updateEditorValidationSummary();
        return;
    }
    ui_.editorContextLabel_->setText(SimaiDocument::difficultyShortName(state_.activeDifficultyId_));
    ui_.editorContextLabel_->setFont(uiAccentFont(12, QFont::DemiBold));
    ui_.editorContextLabel_->setStyleSheet(QString());
    ui_.editorContextLabel_->setMinimumWidth(QFontMetrics(ui_.editorContextLabel_->font()).horizontalAdvance(ui_.editorContextLabel_->text()) + 8);
    if (ui_.editorDifficultyControls_ != nullptr) {
        ui_.editorDifficultyControls_->show();
    }
    if (ui_.editorBatchTransformControls_ != nullptr) {
        ui_.editorBatchTransformControls_->show();
    }
    updateEditorHeaderLayoutMode();
    owner_.updateEditorValidationSummary();
}

void MainWindow::DocumentSection::updateDifficultyScopedActionStates()
{
    const bool enabled = owner_.hasActiveDifficulty();
    // Playback controls (play/pause/stop + their shortcuts) additionally light
    // up for the export-page preview audition, which plays the badge-selected
    // difficulty even though activeDifficultyId_ == 0 (D4), so hasActiveDifficulty
    // is false. (The latency page uses its own audition button, not these.) The
    // edit/transform actions below stay strictly difficulty-scoped.
    const bool playbackEnabled = enabled || state_.exportPreviewAuditionActive_;

    if (ui_.pausePreviewAction_ != nullptr) {
        ui_.pausePreviewAction_->setEnabled(playbackEnabled);
    }
    if (ui_.stopOrPlayPreviewShortcutAction_ != nullptr) {
        ui_.stopOrPlayPreviewShortcutAction_->setEnabled(playbackEnabled);
    }
    if (ui_.playPausePreviewShortcutAction_ != nullptr) {
        ui_.playPausePreviewShortcutAction_->setEnabled(playbackEnabled);
    }
    // exportVideoAction_ is deliberately NOT difficulty-scoped: since
    // 2026-06-12 it jumps to the Export hub page (reachable with no active
    // difficulty; the page greys its own panes).
    if (ui_.stopPreviewAction_ != nullptr) {
        ui_.stopPreviewAction_->setEnabled(playbackEnabled);
    }
    if (ui_.transformMirrorLeftRightAction_ != nullptr) {
        ui_.transformMirrorLeftRightAction_->setEnabled(enabled);
    }
    if (ui_.transformMirrorUpDownAction_ != nullptr) {
        ui_.transformMirrorUpDownAction_->setEnabled(enabled);
    }
    if (ui_.transformRotate180Action_ != nullptr) {
        ui_.transformRotate180Action_->setEnabled(enabled);
    }
    if (ui_.transformRotate45CounterClockwiseAction_ != nullptr) {
        ui_.transformRotate45CounterClockwiseAction_->setEnabled(enabled);
    }
    if (ui_.transformRotate45ClockwiseAction_ != nullptr) {
        ui_.transformRotate45ClockwiseAction_->setEnabled(enabled);
    }
    if (ui_.normalizeWholeChartAction_ != nullptr) {
        ui_.normalizeWholeChartAction_->setEnabled(enabled);
    }
    if (ui_.transformToggleBreakAction_ != nullptr) {
        ui_.transformToggleBreakAction_->setEnabled(enabled);
    }
    if (ui_.transformToggleExAction_ != nullptr) {
        ui_.transformToggleExAction_->setEnabled(enabled);
    }
    if (ui_.transformToggleFireworkAction_ != nullptr) {
        ui_.transformToggleFireworkAction_->setEnabled(enabled);
    }
    if (ui_.transformRandomRotateAction_ != nullptr) {
        ui_.transformRandomRotateAction_->setEnabled(enabled);
    }
    if (ui_.transformRaiseSubdivisionAction_ != nullptr) {
        ui_.transformRaiseSubdivisionAction_->setEnabled(enabled);
    }
    if (ui_.transformLowerSubdivisionAction_ != nullptr) {
        ui_.transformLowerSubdivisionAction_->setEnabled(enabled);
    }
    if (ui_.transformRaiseSubdivisionHalfStepAction_ != nullptr) {
        ui_.transformRaiseSubdivisionHalfStepAction_->setEnabled(enabled);
    }
    if (ui_.transformLowerSubdivisionHalfStepAction_ != nullptr) {
        ui_.transformLowerSubdivisionHalfStepAction_->setEnabled(enabled);
    }
    if (ui_.stopPreviewButton_ != nullptr) {
        ui_.stopPreviewButton_->setEnabled(playbackEnabled);
    }
    if (ui_.pausePreviewButton_ != nullptr) {
        ui_.pausePreviewButton_->setEnabled(playbackEnabled);
    }
    if (ui_.syntaxCheckButton_ != nullptr) {
        ui_.syntaxCheckButton_->setEnabled(enabled);
    }
    // The toolbar Export button is deliberately NOT difficulty-scoped: it
    // jumps to the Export hub page, which is reachable with no active
    // difficulty (the page greys its own panes). Same for the Tools-menu
    // 「导出谱面」action since 2026-06-12.
    if (ui_.transformMirrorLeftRightButton_ != nullptr) {
        ui_.transformMirrorLeftRightButton_->setEnabled(enabled);
    }
    if (ui_.transformMirrorUpDownButton_ != nullptr) {
        ui_.transformMirrorUpDownButton_->setEnabled(enabled);
    }
    if (ui_.transformRotate180Button_ != nullptr) {
        ui_.transformRotate180Button_->setEnabled(enabled);
    }
    if (ui_.transformRotate45CounterClockwiseButton_ != nullptr) {
        ui_.transformRotate45CounterClockwiseButton_->setEnabled(enabled);
    }
    if (ui_.transformRotate45ClockwiseButton_ != nullptr) {
        ui_.transformRotate45ClockwiseButton_->setEnabled(enabled);
    }
}

void MainWindow::DocumentSection::updateEditorHeaderLayoutMode()
{
    if (ui_.editorHeaderWidget_ == nullptr || ui_.editorCursorLabel_ == nullptr || ui_.editorContextLabel_ == nullptr) {
        return;
    }

    if (!owner_.hasActiveDifficulty()) {
        ui_.editorCursorLabel_->setVisible(false);
        if (ui_.editorValidationSummaryWidget_ != nullptr) {
            ui_.editorValidationSummaryWidget_->setVisible(false);
        }
        syncEditorHeaderMinimumWidth();
        return;
    }

    if (ui_.difficultyLevelLabel_ != nullptr) {
        ui_.difficultyLevelLabel_->setVisible(true);
    }
    if (ui_.difficultyLevelEdit_ != nullptr) {
        ui_.difficultyLevelEdit_->setFixedWidth(48);
        ui_.difficultyLevelEdit_->setVisible(true);
    }
    // The 顶部显示 preference decides which field pair sits next to Lv: the
    // chart-wide offset (default) or the active difficulty's designer.
    const bool headerShowsDesigner =
        state_.editorHeaderTopDisplay_ == MainWindow::EditorHeaderTopDisplay::Designer;
    if (ui_.difficultyFirstLabel_ != nullptr) {
        ui_.difficultyFirstLabel_->setVisible(!headerShowsDesigner);
    }
    if (ui_.firstEdit_ != nullptr) {
        ui_.firstEdit_->setFixedWidth(64);
        ui_.firstEdit_->setVisible(!headerShowsDesigner);
    }
    if (ui_.difficultyDesignerLabel_ != nullptr) {
        ui_.difficultyDesignerLabel_->setVisible(headerShowsDesigner);
    }
    if (ui_.difficultyDesignerEdit_ != nullptr) {
        ui_.difficultyDesignerEdit_->setFixedWidth(96);
        ui_.difficultyDesignerEdit_->setVisible(headerShowsDesigner);
    }
    if (ui_.editorDifficultyControls_ != nullptr) {
        ui_.editorDifficultyControls_->setVisible(true);
    }

    if (ui_.editorValidationSummaryWidget_ != nullptr) {
        const auto applySummaryVisibility = [](QLabel* icon, QLabel* count) {
            const bool hasContent = (icon != nullptr && icon->property("hasContent").toBool())
                || (count != nullptr && count->property("hasContent").toBool());
            if (icon != nullptr) {
                icon->setVisible(hasContent);
            }
            if (count != nullptr) {
                count->setVisible(hasContent);
            }
        };
        applySummaryVisibility(ui_.editorValidationErrorIconLabel_, ui_.editorValidationErrorCountLabel_);
        applySummaryVisibility(ui_.editorValidationWarningIconLabel_, ui_.editorValidationWarningCountLabel_);
        applySummaryVisibility(ui_.editorValidationMuriIconLabel_, ui_.editorValidationMuriCountLabel_);
        const int reservedSummaryWidth = qMax(
            ui_.editorValidationSummaryWidget_->minimumSizeHint().width(),
            ui_.editorValidationSummaryWidget_->sizeHint().width()
        );
        ui_.editorValidationSummaryWidget_->setVisible(true);
        ui_.editorValidationSummaryWidget_->setMinimumWidth(reservedSummaryWidth);
        ui_.editorValidationSummaryWidget_->setMaximumWidth(reservedSummaryWidth);
        ui_.editorValidationSummaryWidget_->adjustSize();
    }

    const auto [line, col] = currentCursorLineCol();
    const QString cursorText = UiText::text(QStringLiteral("document.ln_1_col_2")).arg(line).arg(col);
    ui_.editorCursorLabel_->setText(cursorText);
    ui_.editorCursorLabel_->setFixedWidth(QFontMetrics(ui_.editorCursorLabel_->font()).horizontalAdvance(cursorText) + 10);
    ui_.editorCursorLabel_->setVisible(true);
    const QString correctedCursorText = UiText::text(QStringLiteral("document.ln_1_col_2")).arg(line).arg(col);
    const QString correctedCursorWidthTemplate = UiText::text(QStringLiteral("document.ln_9999_col_9999"));
    ui_.editorCursorLabel_->setText(correctedCursorText);
    ui_.editorCursorLabel_->setFixedWidth(
        QFontMetrics(ui_.editorCursorLabel_->font()).horizontalAdvance(correctedCursorWidthTemplate) + 10);

    if (QLayout* headerLayout = ui_.editorHeaderWidget_->layout(); headerLayout != nullptr) {
        headerLayout->activate();
    }
    syncEditorHeaderMinimumWidth();
}

void MainWindow::DocumentSection::syncEditorHeaderMinimumWidth()
{
    if (ui_.editorHeaderWidget_ == nullptr) {
        return;
    }

    int headerMinimumWidth = 0;
    if (QLayout* headerLayout = ui_.editorHeaderWidget_->layout(); headerLayout != nullptr) {
        headerLayout->activate();
        const auto widgetMinimumWidth = [](const QWidget* widget, bool includeHidden = false) {
            if (widget == nullptr || (!includeHidden && widget->isHidden())) {
                return 0;
            }
            return qMax(widget->minimumWidth(), qMax(widget->minimumSizeHint().width(), widget->sizeHint().width()));
        };
        const QMargins margins = headerLayout->contentsMargins();
        const auto composedHeaderMinimumWidth = [&](std::initializer_list<int> sectionWidths) {
            int width = margins.left() + margins.right();
            int visibleSectionCount = 0;
            for (int sectionWidth : sectionWidths) {
                if (sectionWidth <= 0) {
                    continue;
                }
                if (visibleSectionCount > 0) {
                    width += qMax(0, headerLayout->spacing());
                }
                width += sectionWidth;
                ++visibleSectionCount;
            }
            return qMax(width, margins.left() + margins.right());
        };
        const auto difficultyContextMinimumWidth = [&]() {
            if (ui_.editorContextLabel_ == nullptr) {
                return 0;
            }
            int difficultyId = state_.activeDifficultyId_;
            if (!SimaiDocument::isDifficultyId(difficultyId) && ui_.qmlExportSession_ != nullptr) {
                difficultyId = ui_.qmlExportSession_->selectedDifficultyId();
            }
            const QString labelText = SimaiDocument::isDifficultyId(difficultyId)
                ? SimaiDocument::difficultyShortName(difficultyId)
                : ui_.editorContextLabel_->text();
            return QFontMetrics(ui_.editorContextLabel_->font()).horizontalAdvance(labelText) + 8;
        };
        const auto cursorMinimumWidth = [&]() {
            return widgetMinimumWidth(ui_.editorCursorLabel_, true);
        };
        headerMinimumWidth = composedHeaderMinimumWidth({
            widgetMinimumWidth(ui_.editorContextLabel_),
            widgetMinimumWidth(ui_.editorDifficultyControls_),
            widgetMinimumWidth(ui_.editorValidationSummaryWidget_),
            widgetMinimumWidth(ui_.editorCursorLabel_ != nullptr ? ui_.editorCursorLabel_->parentWidget() : nullptr),
        });
        if (state_.activeOutlineKey_ == QLatin1String("export")) {
            const int difficultyHeaderMinimumWidth = composedHeaderMinimumWidth({
                difficultyContextMinimumWidth(),
                widgetMinimumWidth(ui_.editorDifficultyControls_, true),
                widgetMinimumWidth(ui_.editorValidationSummaryWidget_, true),
                cursorMinimumWidth(),
            });
            headerMinimumWidth = qMax(headerMinimumWidth, difficultyHeaderMinimumWidth);
        }
        ui_.editorHeaderWidget_->setMinimumWidth(headerMinimumWidth);
    }
    ui_.editorHeaderWidget_->updateGeometry();

    if (ui_.previewLeftColumn_ != nullptr) {
        const int baseMinimumWidth = qMax(0, ui_.previewLeftColumn_->property("baseMinimumWidth").toInt());
        const int previousMinimumWidth = ui_.previewLeftColumn_->minimumWidth();
        if (QLayout* leftColumnLayout = ui_.previewLeftColumn_->layout(); leftColumnLayout != nullptr) {
            leftColumnLayout->activate();
        }
        const int nextMinimumWidth = qMax(
            baseMinimumWidth,
            headerMinimumWidth
        );
        ui_.previewLeftColumn_->setMinimumWidth(nextMinimumWidth);
        ui_.previewLeftColumn_->updateGeometry();
        if (nextMinimumWidth != previousMinimumWidth && ui_.workspaceSplitter_ != nullptr) {
            owner_.updatePreviewWorkspaceLayout();
        }
    }
    if (ui_.workspaceContentWidget_ != nullptr) {
        const int baseMinimumWidth = qMax(0, ui_.workspaceContentWidget_->property("baseMinimumWidth").toInt());
        ui_.workspaceContentWidget_->setMinimumWidth(qMax(baseMinimumWidth, headerMinimumWidth));
        ui_.workspaceContentWidget_->updateGeometry();
    }

    owner_.refreshQuickShellRehostedWidgetParent(ui_.outlineDock_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.workspaceContentWidget_);
    owner_.refreshQuickShellRehostedWidgetParent(ui_.bottomTabs_);

    if (ui_.workspaceSplitter_ != nullptr) {
        ui_.workspaceSplitter_->updateGeometry();
    }
}

void MainWindow::DocumentSection::updateEditorStatus()
{
    if (ui_.editorCursorLabel_ == nullptr) {
        return;
    }
    if (!owner_.hasActiveDifficulty()) {
        ui_.editorCursorLabel_->clear();
        updateEditorHeaderLayoutMode();
        return;
    }
    updateEditorHeaderLayoutMode();
}

void MainWindow::DocumentSection::updateEditorEmptyState()
{
    if (ui_.editorEmptyStateLabel_ != nullptr) {
        ui_.editorEmptyStateLabel_->hide();
    }
}

void MainWindow::DocumentSection::updateMetadataPageMode()
{
    if (ui_.metadataCard_ == nullptr || ui_.metadataEmptyHintLabel_ == nullptr) {
        return;
    }
    ui_.metadataCard_->setVisible(true);
    ui_.metadataEmptyHintLabel_->hide();
}

bool MainWindow::DocumentSection::deleteDifficultyField(int difficultyId)
{
    const SimaiDifficultyData* difficultyData = state_.document_.difficulty(difficultyId);
    if (!SimaiDocument::isDifficultyId(difficultyId) || difficultyData == nullptr) {
        return false;
    }

    const bool deletingActiveDifficulty = (difficultyId == state_.activeDifficultyId_);
    const QString difficultyName = SimaiDocument::difficultyName(difficultyId);
    const QString currentLevel =
        deletingActiveDifficulty && ui_.difficultyLevelEdit_ != nullptr ? ui_.difficultyLevelEdit_->text() : difficultyData->level;
    // The header designer edit (顶部显示=谱师 mode) mirrors the model whenever it
    // isn't being typed in, so reading the live edit for the active difficulty
    // captures any uncommitted designer text for undo; other difficulties (and
    // a missing widget) fall back to the saved model value.
    const QString currentDesigner =
        deletingActiveDifficulty && ui_.difficultyDesignerEdit_ != nullptr
            ? ui_.difficultyDesignerEdit_->text()
            : difficultyData->designer;
    const QString currentChart = deletingActiveDifficulty ? owner_.editorText() : difficultyData->chart;
    const bool emptyDifficulty = currentLevel.trimmed().isEmpty()
        && currentDesigner.trimmed().isEmpty()
        && currentChart.trimmed().isEmpty();

    if (!emptyDifficulty) {
        const QMessageBox::StandardButton choice = UiDialogs::showMessageBox(
            QMessageBox::Question,
            &owner_,
            UiText::text(QStringLiteral("document.delete_difficulty")),
            UiText::text(QStringLiteral("document.delete_1")).arg(difficultyName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (choice != QMessageBox::Yes) {
            return false;
        }
    }

    clearDeletedDifficultyUndoState();
    state_.deletedDifficultyUndoState_.valid = true;
    state_.deletedDifficultyUndoState_.wasActive = deletingActiveDifficulty;
    state_.deletedDifficultyUndoState_.difficultyId = difficultyId;
    state_.deletedDifficultyUndoState_.difficultyData.id = difficultyId;
    state_.deletedDifficultyUndoState_.difficultyData.level = currentLevel;
    state_.deletedDifficultyUndoState_.difficultyData.designer = currentDesigner;
    state_.deletedDifficultyUndoState_.difficultyData.chart = currentChart;

    owner_.stopQtPreviewPlayback(true);
    state_.document_.removeDifficulty(difficultyId);
    state_.validationCacheByDifficulty_.remove(difficultyId);
    if (deletingActiveDifficulty) {
        owner_.invalidateDocumentValidationRevision();
    } else {
        emit owner_.documentValidationChanged();
    }
    state_.documentDirty_ = true;

    if (deletingActiveDifficulty) {
        owner_.cacheWorkspaceLayoutSizes();
        state_.currentFieldDirty_ = false;
        const QVector<int> remainingIds = state_.document_.difficultyIds();
        if (remainingIds.isEmpty()) {
            state_.activeDifficultyId_ = 0;
            state_.activeOutlineKey_ = "welcome";
            populateMetadataPage();
            if (ui_.editorStack_ != nullptr && ui_.welcomePage_ != nullptr) {
                ui_.editorStack_->setCurrentWidget(ui_.welcomePage_);
            }
            setChartBottomTabsMode(false);
            clearTimelineAndPreview();
            if (ui_.outlineList_ != nullptr) {
                ui_.outlineList_->setFocus();
            }
            owner_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
        } else {
            int fallbackId = remainingIds.constFirst();
            int bestDistance = qAbs(fallbackId - difficultyId);
            for (int id : remainingIds) {
                const int distance = qAbs(id - difficultyId);
                if (distance < bestDistance || (distance == bestDistance && id < fallbackId)) {
                    fallbackId = id;
                    bestDistance = distance;
                }
            }
            state_.activeOutlineKey_ = "chart";
            switchToDifficultyField(fallbackId);
        }
    }

    rebuildFieldSidebar();
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
    updateDirtyState();
    if (state_.currentFilePath_.isEmpty()) {
        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("document.deleted_1")).arg(difficultyName));
        return true;
    }
    if (!saveToPath(state_.currentFilePath_)) {
        owner_.statusBar()->showMessage(UiText::text(QStringLiteral("document.deleted_1_changes_are_still")).arg(difficultyName));
    }
    return true;
}

bool MainWindow::DocumentSection::isBookmarkGroupExpanded(int difficultyId) const
{
    const auto it = state_.outlineBookmarkGroupExpanded_.constFind(difficultyId);
    if (it != state_.outlineBookmarkGroupExpanded_.cend()) {
        return it.value();
    }
    // Untouched groups: the active difficulty starts expanded, others folded.
    return difficultyId == state_.activeDifficultyId_;
}

void MainWindow::DocumentSection::setBookmarkGroupExpanded(int difficultyId, bool expanded)
{
    state_.outlineBookmarkGroupExpanded_.insert(difficultyId, expanded);
    rebuildFieldSidebar();
}

QListWidgetItem* MainWindow::DocumentSection::findBookmarkSidebarItem(int difficultyId, int line) const
{
    if (ui_.outlineList_ == nullptr) {
        return nullptr;
    }
    for (int i = 0; i < ui_.outlineList_->count(); ++i) {
        QListWidgetItem* item = ui_.outlineList_->item(i);
        if (item != nullptr
            && item->data(kOutlineItemKindRole).toString() == QLatin1String("bookmark")
            && item->data(kOutlineItemDifficultyRole).toInt() == difficultyId
            && item->data(kOutlineItemLineRole).toInt() == line) {
            return item;
        }
    }
    return nullptr;
}

void MainWindow::DocumentSection::revealBookmarkInSidebar(int difficultyId, int line, bool beginRename)
{
    if (ui_.outlineList_ == nullptr) {
        return;
    }
    state_.outlineBookmarkGroupExpanded_.insert(difficultyId, true);
    rebuildFieldSidebar();
    QListWidgetItem* item = findBookmarkSidebarItem(difficultyId, line);
    if (item == nullptr) {
        return;
    }
    {
        QSignalBlocker blocker(ui_.outlineList_);
        ui_.outlineList_->setCurrentItem(item);
    }
    ui_.outlineList_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    if (beginRename) {
        ui_.outlineList_->editItem(item);
    }
}

void MainWindow::DocumentSection::rebuildFieldSidebar()
{
    if (ui_.outlineList_ == nullptr) {
        return;
    }
    QSignalBlocker blocker(ui_.outlineList_);
    // Keep the viewport where the user left it across rebuilds (the list is
    // torn down and rebuilt on most document/sidebar state changes).
    const int restoreScrollValue = ui_.outlineList_->verticalScrollBar() != nullptr
        ? ui_.outlineList_->verticalScrollBar()->value()
        : 0;
    // Preserve a selected bookmark row across the rebuild (difficulty rows are
    // re-selected via activeDifficultyId_ / activeOutlineKey_ as before).
    int restoreBookmarkDifficultyId = 0;
    int restoreBookmarkLine = -1;
    if (QListWidgetItem* currentItem = ui_.outlineList_->currentItem();
        currentItem != nullptr && currentItem->data(kOutlineItemKindRole).toString() == QLatin1String("bookmark")) {
        restoreBookmarkDifficultyId = currentItem->data(kOutlineItemDifficultyRole).toInt();
        restoreBookmarkLine = currentItem->data(kOutlineItemLineRole).toInt();
    }
    ui_.outlineList_->clear();
    const QString metadataLabel = UiText::text(QStringLiteral("sidebar.metadata"));
    auto* metadataItem = new QListWidgetItem(
        owner_.style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        metadataLabel,
        ui_.outlineList_
    );
    metadataItem->setData(Qt::UserRole, "metadata");
    metadataItem->setToolTip(metadataLabel);
    // The latency page is informationally a sub-page of the metadata page —
    // keep the metadata row marked "you are here" while it is showing.
    metadataItem->setData(kOutlineItemActiveRole,
                          state_.activeOutlineKey_ == QLatin1String("metadata")
                              || state_.activeOutlineKey_ == QLatin1String("latency"));

    // The latency-settings sidebar item is gone (L-A migration): the page is
    // now reached from the metadata page's "延迟与偏移校准" entry card (and
    // the Tools menu's BPM && 延迟检测 direct action).

    QListWidgetItem* selectedItem = nullptr;
    bool hasMissingDifficulty = false;
    const QVector<int> ids = state_.document_.difficultyIds();
    for (int id = 1; id <= 7; ++id) {
        if (state_.document_.difficulty(id) == nullptr) {
            hasMissingDifficulty = true;
            break;
        }
    }
    if (hasMissingDifficulty) {
        // Abbreviated to "+ Add Diff." in English so the label fits the
        // narrow sidebar list-item column without truncation. Chinese
        // version "添加难度" already fits.
        const QString addDifficultyLabel = UiText::text(QStringLiteral("sidebar.add_difficulty"));
        auto* addItem = new QListWidgetItem(
            owner_.style()->standardIcon(QStyle::SP_FileDialogNewFolder),
            addDifficultyLabel,
            ui_.outlineList_
        );
        addItem->setData(Qt::UserRole, "add");
        addItem->setToolTip(addDifficultyLabel);
    }
    auto bookmarksForDifficulty = [this](int difficultyId) {
        QVector<MainWindow::EditorBookmark> bookmarks;
        for (const MainWindow::EditorBookmark& bookmark : std::as_const(state_.editorBookmarks_)) {
            if (bookmark.difficultyId == difficultyId) {
                bookmarks.append(bookmark);
            }
        }
        std::sort(bookmarks.begin(), bookmarks.end(), [](const MainWindow::EditorBookmark& left, const MainWindow::EditorBookmark& right) {
            if (left.line != right.line) {
                return left.line < right.line;
            }
            return left.title.localeAwareCompare(right.title) < 0;
        });
        return bookmarks;
    };

    // Non-interactive 4px spacer rows (plus the list's 2px item spacing on
    // both sides) separate the sidebar's sections. When "+ Add Diff." is
    // visible, it stays attached to the first difficulty row so that whole
    // authoring block reads as one tight group.
    auto addSectionSpacer = [this]() {
        auto* spacer = new QListWidgetItem(ui_.outlineList_);
        spacer->setFlags(Qt::NoItemFlags);
        spacer->setData(kOutlineItemKindRole, "spacer");
        spacer->setSizeHint(QSize(1, 4));
    };

    if (!ids.isEmpty() && !hasMissingDifficulty) {
        addSectionSpacer();
    }
    for (int id : ids) {
        const QVector<MainWindow::EditorBookmark> bookmarks = bookmarksForDifficulty(id);
        const QString difficultyLabel = SimaiDocument::difficultyName(id);
        auto* difficultyItem = new QListWidgetItem(difficultyLabel, ui_.outlineList_);
        difficultyItem->setIcon(makeDifficultyBadgeIcon(id));
        difficultyItem->setData(kOutlineItemKindRole, "difficulty_chart");
        difficultyItem->setData(kOutlineItemDifficultyRole, id);
        difficultyItem->setData(kOutlineItemBookmarkCountRole, bookmarks.size());
        difficultyItem->setData(kOutlineItemExpandedRole, isBookmarkGroupExpanded(id));
        // Persistent "you are here" marker — held by the difficulty row even
        // while the list selection sits on one of its bookmark rows.
        difficultyItem->setData(kOutlineItemActiveRole,
                                id == state_.activeDifficultyId_
                                    && state_.activeOutlineKey_ == QLatin1String("chart"));
        difficultyItem->setSizeHint(QSize(1, 30));
        difficultyItem->setToolTip(bookmarks.isEmpty()
            ? difficultyLabel
            : UiText::text(QStringLiteral("document.1_comment_bookmarks")).arg(difficultyLabel));
        if (id == state_.activeDifficultyId_) {
            selectedItem = difficultyItem;
        }

        if (bookmarks.isEmpty()) {
            continue;
        }
        const bool expanded = isBookmarkGroupExpanded(id);
        if (!expanded) {
            continue;
        }

        for (const MainWindow::EditorBookmark& bookmark : bookmarks) {
            const QString title = bookmark.title.trimmed().isEmpty()
                ? UiText::text(QStringLiteral("document.untitled_bookmark"))
                : bookmark.title.trimmed();
            const QString tooltip = UiText::text(QStringLiteral("document.1_line_2_double_click")).arg(title).arg(bookmark.line);
            // Item text = the bare name (QListWidgetItem aliases EditRole to
            // DisplayRole, so the inline editor edits exactly the name); the
            // delegate paints the indent + line badge from the data roles.
            auto* bookmarkItem = new QListWidgetItem(title, ui_.outlineList_);
            bookmarkItem->setFlags(bookmarkItem->flags() | Qt::ItemIsEditable);
            bookmarkItem->setData(kOutlineItemKindRole, "bookmark");
            bookmarkItem->setData(kOutlineItemDifficultyRole, id);
            bookmarkItem->setData(kOutlineItemLineRole, bookmark.line);
            bookmarkItem->setData(kOutlineItemActiveRole, false);
            // Group-wide max line (list is sorted ascending) — fixed badge
            // width so the name column aligns vertically.
            bookmarkItem->setData(kOutlineItemMaxLineRole, bookmarks.last().line);
            bookmarkItem->setSizeHint(QSize(1, 24));
            bookmarkItem->setToolTip(tooltip);
            if (id == restoreBookmarkDifficultyId && bookmark.line == restoreBookmarkLine) {
                selectedItem = bookmarkItem;
            }
        }
    }
    if (!ids.isEmpty()) {
        addSectionSpacer();
    }
    const QString exportLabel = UiText::text(QStringLiteral("sidebar.export"));
    auto* exportItem = new QListWidgetItem(
        makeExportAccessIcon(UiTheme::colors().iconPrimary),
        exportLabel,
        ui_.outlineList_
    );
    exportItem->setData(Qt::UserRole, "export");
    exportItem->setData(kOutlineItemActiveRole, state_.activeOutlineKey_ == QLatin1String("export"));
    exportItem->setToolTip(
        UiText::text(QStringLiteral("document.open_the_export_page_video"))
    );
    auto* toolboxItem = new QListWidgetItem(
        makeToolboxAccessIcon(UiTheme::colors().iconPrimary, QColor(QStringLiteral("#E6B84A"))),
        UiText::text(QStringLiteral("document.toolbox")),
        ui_.outlineList_
    );
    toolboxItem->setData(Qt::UserRole, "toolbox");
    toolboxItem->setToolTip(
        UiText::text(QStringLiteral("document.open_toolbox_muri_check_format"))
    );
    if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
        selectedItem = metadataItem;
    } else if (state_.activeOutlineKey_ == QLatin1String("latency")) {
        // The latency page is informationally a sub-page of the metadata page
        // now — keep the metadata item highlighted while it is showing.
        selectedItem = metadataItem;
    } else if (state_.activeOutlineKey_ == QLatin1String("export")) {
        selectedItem = exportItem;
    }
    if (selectedItem != nullptr) {
        ui_.outlineList_->setCurrentItem(selectedItem);
    } else {
        ui_.outlineList_->setCurrentItem(nullptr);
        ui_.outlineList_->setCurrentRow(-1);
        ui_.outlineList_->clearSelection();
        if (ui_.outlineList_->selectionModel() != nullptr) {
            ui_.outlineList_->selectionModel()->clearCurrentIndex();
            ui_.outlineList_->selectionModel()->clearSelection();
        }
    }
    if (ui_.outlineList_->verticalScrollBar() != nullptr) {
        ui_.outlineList_->verticalScrollBar()->setValue(restoreScrollValue);
    }
    // The rebuild can move the Export row (e.g. the previously active
    // difficulty's auto-expanded bookmark group collapses when the export
    // page takes over) — re-anchor the busy spinner to the row's new rect.
    if (ui_.outlineBusySpinner_ != nullptr && ui_.outlineBusySpinner_->isActive()) {
        positionOutlineExportBusySpinner();
    }
    // The export page derives its difficulty badges + card availability from
    // the same document state this sidebar reflects — refresh it on the same
    // triggers (document load, difficulty add/delete, page switches). Cheap.
}

void MainWindow::DocumentSection::populateMetadataPage()
{
    if (ui_.titleEdit_ == nullptr || ui_.artistEdit_ == nullptr || ui_.designerEdit_ == nullptr) {
        return;
    }
    QSignalBlocker blockerTitle(ui_.titleEdit_);
    QSignalBlocker blockerArtist(ui_.artistEdit_);
    QSignalBlocker blockerDesigner(ui_.designerEdit_);
    ui_.titleEdit_->setText(state_.document_.title);
    ui_.artistEdit_->setText(state_.document_.artist);
    ui_.designerEdit_->setText(state_.document_.designer);
    ui_.titleEdit_->setModified(false);
    ui_.artistEdit_->setModified(false);
    ui_.designerEdit_->setModified(false);
    // The chart-wide offset (`&first`) now lives on the difficulty-page header,
    // not here — it is loaded in populateDifficultyPage().
    setMetadataExtraText(SimaiDocument::serializeRawFields(state_.document_.extraFields));
    // The "延迟与偏移校准" entry card's BPM/offset summary tracks the same
    // sources the latency page itself shows: parsedWholeBpm (the document's
    // &wholebpm resolution) and document_.first.
    if (ui_.latencyEntrySummaryLabel_ != nullptr) {
        bool bpmOk = false;
        const double bpm = owner_.parsedWholeBpm(&bpmOk);
        QString bpmText = QStringLiteral("—");
        if (bpmOk && bpm > 0.0) {
            bpmText = QString::number(bpm, 'f', 3);
            while (bpmText.endsWith(QLatin1Char('0'))) {
                bpmText.chop(1);
            }
            if (bpmText.endsWith(QLatin1Char('.'))) {
                bpmText.chop(1);
            }
        }
        const QString offsetRaw = state_.document_.first.trimmed();
        const QString offsetText = offsetRaw.isEmpty() ? QStringLiteral("0") : offsetRaw;
        ui_.latencyEntrySummaryLabel_->setText(UiText::text(QStringLiteral("document.bpm_1_offset_2_s")).arg(bpmText, offsetText));
    }
    updateMetadataPageMode();
    updateEditorHeader();
}

void MainWindow::DocumentSection::populateDifficultyPage(int difficultyId)
{
    const SimaiDifficultyData* difficultyData = state_.document_.difficulty(difficultyId);
    if (difficultyData == nullptr) {
        return;
    }
    if (ui_.difficultyLevelEdit_ != nullptr) {
        QSignalBlocker blocker(ui_.difficultyLevelEdit_);
        if (auto* placeholderEdit = dynamic_cast<LeftPlaceholderLineEdit*>(ui_.difficultyLevelEdit_)) {
            placeholderEdit->setLeftPlaceholderText(QString("&lv_%1=").arg(difficultyId));
        }
        ui_.difficultyLevelEdit_->setText(difficultyData->level);
        ui_.difficultyLevelEdit_->setModified(false);
    }
    // The header's offset field edits the chart-wide `&first` (shared with the
    // latency page), so it loads from the document, not the difficulty.
    if (ui_.firstEdit_ != nullptr) {
        QSignalBlocker blocker(ui_.firstEdit_);
        ui_.firstEdit_->setText(state_.document_.first);
        ui_.firstEdit_->setModified(false);
    }
    // The header's designer field (visible in 顶部显示=谱师 mode) edits this
    // difficulty's `&des_N`. Kept in sync even while hidden so a later commit
    // can read it unconditionally without picking up a stale difficulty's name.
    if (ui_.difficultyDesignerEdit_ != nullptr) {
        QSignalBlocker blocker(ui_.difficultyDesignerEdit_);
        if (auto* placeholderEdit = dynamic_cast<LeftPlaceholderLineEdit*>(ui_.difficultyDesignerEdit_)) {
            placeholderEdit->setLeftPlaceholderText(QString("&des_%1=").arg(difficultyId));
        }
        ui_.difficultyDesignerEdit_->setText(difficultyData->designer);
        ui_.difficultyDesignerEdit_->setModified(false);
    }
    setEditorText(difficultyData->chart);
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
}

void MainWindow::DocumentSection::syncHeaderDesignerEditFromModel()
{
    // Re-reads the active difficulty's designer into the header edit. Called
    // after anything that rewrites designers behind the header's back (the
    // designer-management dialog, the load-time unified reconcile, a 顶部显示
    // preference flip) so a later applyCurrentFieldToDocument never commits a
    // stale header value over the model.
    if (ui_.difficultyDesignerEdit_ == nullptr || !owner_.hasActiveDifficulty()) {
        return;
    }
    const SimaiDifficultyData* difficultyData = state_.document_.difficulty(state_.activeDifficultyId_);
    if (difficultyData == nullptr) {
        return;
    }
    QSignalBlocker blocker(ui_.difficultyDesignerEdit_);
    if (auto* placeholderEdit = dynamic_cast<LeftPlaceholderLineEdit*>(ui_.difficultyDesignerEdit_)) {
        placeholderEdit->setLeftPlaceholderText(QString("&des_%1=").arg(state_.activeDifficultyId_));
    }
    ui_.difficultyDesignerEdit_->setText(difficultyData->designer);
    ui_.difficultyDesignerEdit_->setModified(false);
}

void MainWindow::DocumentSection::applyDifficultySwitchEditorScrollRestore(
    int verticalScrollValue,
    int horizontalScrollValue)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    if (editor == nullptr) {
        return;
    }
    if (QScrollBar* vertical = editor->verticalScrollBar(); vertical != nullptr) {
        vertical->setValue(qBound(vertical->minimum(), verticalScrollValue, vertical->maximum()));
    }
    if (QScrollBar* horizontal = editor->horizontalScrollBar(); horizontal != nullptr) {
        horizontal->setValue(qBound(horizontal->minimum(), horizontalScrollValue, horizontal->maximum()));
    }
}

void MainWindow::DocumentSection::setChartBottomTabsMode(bool enabled)
{
    owner_.setBottomTabsTabVisible(MainWindow::BottomTabsTabId::Timeline, enabled);
    owner_.setValidationTabVisible(enabled);
    owner_.setBottomTabsTabVisible(MainWindow::BottomTabsTabId::Muri, enabled);

    if (ui_.bottomTabs_ != nullptr) {
        ui_.bottomTabs_->setVisible(enabled);
        owner_.refreshQuickShellRehostedWidgetParent(ui_.bottomTabs_);
    }
    if (owner_.quickShellBottomTabsProxy_ != nullptr) {
        owner_.quickShellBottomTabsProxy_->setVisible(enabled);
        owner_.refreshQuickShellRehostedWidgetParent(owner_.quickShellBottomTabsProxy_);
    }

    if (enabled) {
        owner_.setCurrentBottomTabsTabId(MainWindow::BottomTabsTabId::Timeline);
    }
}

bool MainWindow::DocumentSection::switchToLatencyField()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    if (ui_.latencyPlaceholderPage_ == nullptr || ui_.editorStack_ == nullptr) {
        return false;
    }
    // Leaving the export page (possibly) — tear down its embedded video
    // panel unconditionally (idempotent), same pattern as the latency
    // onPageLeft calls in the other switch functions.
    if (ui_.qmlExportSession_ != nullptr) {
        ui_.qmlExportSession_->leave();
    }
    owner_.cacheWorkspaceLayoutSizes();
    // Preserve the current preview position across the switch, just like
    // switchToDifficultyField does, so entering the latency page keeps the
    // playhead instead of snapping to 0. installSandboxScene() consumes
    // qtPreviewPauseSecond_ (clamped to the test chart duration).
    const double restorePreviewSecond = qMax(0.0, state_.qtPreviewPlaying_
        ? owner_.currentPreviewAuthoritativeAudioClockSecond()
        : state_.qtPreviewPauseSecond_);
    owner_.stopQtPreviewPlayback(true);
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    miacode::mainwindow::shared::writePreviewPauseSecond(
        state_.qtPreviewPauseSecond_, restorePreviewSecond, state_.qtPreviewPlaying_, "switch_to_latency_field");
    state_.activeDifficultyId_ = 0;
    state_.activeOutlineKey_ = "latency";
    populateMetadataPage();  // keeps document fields in sync for sidebar use
    ui_.editorStack_->setCurrentWidget(ui_.latencyPlaceholderPage_);
    // Bottom timeline + bottom tabs remain visible: the sandbox audition
    // drives them with the synthesized test chart, so the user can watch
    // the taps scroll past the judge line in sync with the song.
    setChartBottomTabsMode(true);
    owner_.clearValidationDecorations();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    owner_.updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    owner_.refreshLayoutAfterPageSwitch();
    QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
    return true;
}

bool MainWindow::DocumentSection::switchToExportField()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    if (ui_.exportPlaceholderPage_ == nullptr || ui_.editorStack_ == nullptr) {
        return false;
    }
    // The export page is noticeably slow to switch to — building its embedded
    // video panel blocks the UI thread for a while. Show a busy spinner over
    // the "Export" sidebar row and defer the heavy build one event-loop tick so
    // the spinner paints (and starts spinning) before the thread blocks. The
    // spinner's own active state guards against a double-trigger landing two
    // deferred builds in flight.
    if (ui_.outlineBusySpinner_ != nullptr && ui_.outlineBusySpinner_->isActive()) {
        return true;
    }
    showOutlineExportBusySpinner();
    QTimer::singleShot(0, &owner_, [this]() {
        performSwitchToExportField();
        hideOutlineExportBusySpinner();
    });
    return true;
}

bool MainWindow::DocumentSection::positionOutlineExportBusySpinner()
{
    if (ui_.outlineBusySpinner_ == nullptr || ui_.outlineList_ == nullptr) {
        return false;
    }
    QListWidgetItem* exportItem = nullptr;
    for (int i = 0; i < ui_.outlineList_->count(); ++i) {
        QListWidgetItem* item = ui_.outlineList_->item(i);
        if (item != nullptr && item->data(Qt::UserRole).toString() == QLatin1String("export")) {
            exportItem = item;
            break;
        }
    }
    if (exportItem == nullptr) {
        return false;
    }
    const QRect rowRect = ui_.outlineList_->visualItemRect(exportItem);
    if (!rowRect.isValid() || rowRect.isEmpty()) {
        return false;
    }
    auto* spinner = ui_.outlineBusySpinner_;
    const int x = rowRect.right() - spinner->width() - 8;
    const int y = rowRect.top() + (rowRect.height() - spinner->height()) / 2;
    spinner->move(x, y);
    return true;
}

void MainWindow::DocumentSection::showOutlineExportBusySpinner()
{
    if (!positionOutlineExportBusySpinner()) {
        return;
    }
    auto* spinner = ui_.outlineBusySpinner_;
    spinner->setColor(UiTheme::colors().iconPrimary);
    spinner->start();
    // Paint the first frame synchronously so the spinner is on screen before the
    // deferred (and UI-thread-blocking) page build begins.
    spinner->repaint();
}

void MainWindow::DocumentSection::hideOutlineExportBusySpinner()
{
    if (ui_.outlineBusySpinner_ != nullptr) {
        ui_.outlineBusySpinner_->stop();
    }
}

void MainWindow::tickOutlineBusySpinner()
{
    if (outlineBusySpinner_ != nullptr && outlineBusySpinner_->isActive()) {
        outlineBusySpinner_->advance();
    }
}

void MainWindow::DocumentSection::performSwitchToExportField()
{
    if (ui_.exportPlaceholderPage_ == nullptr || ui_.editorStack_ == nullptr) {
        return;
    }
    // Captured BEFORE the reset below: seeds the page's difficulty badge
    // default (decision D4 — "the difficulty that was active on entry").
    const int previousActiveDifficultyId = state_.activeDifficultyId_;
    // Carry the current preview position INTO the export audition so it doesn't
    // snap to 0 — matching the difficulty-tab switch (which preserves progress
    // when a difficulty / the latency page was active before the switch). Read
    // the authoritative clock while it is still live (before stopQtPreviewPlayback
    // below); installExportPreviewAuditionScene consumes this one-shot seed.
    // The metadata (谱面信息) page keeps no audition, but leaving a difficulty for
    // it stopped playback with keepPosition=true, so qtPreviewPauseSecond_ still
    // holds the last position — carry it into the export page too. Source detected
    // from the stack (currentWidget is still the page we're LEAVING; the switch to
    // the export field happens later), because activeOutlineKey_ was already overwritten
    // with the destination by the sidebar handler. A stale cross-file value is
    // guarded by loadDocument resetting qtPreviewPauseSecond_ to 0.
    const bool leavingMetadataPage = ui_.editorStack_ != nullptr
        && ui_.metadataPage_ != nullptr
        && ui_.editorStack_->currentWidget() == ui_.metadataPage_;
    const bool restoreEntryPreview = owner_.hasActiveDifficulty()
        || state_.latencySandboxAuditionActive_
        || state_.exportPreviewAuditionActive_   // re-entering export from export (sidebar re-click)
        || leavingMetadataPage;
    state_.exportPreviewEntrySeedSecond_ = restoreEntryPreview
        ? qMax(0.0, state_.qtPreviewPlaying_
              ? owner_.currentPreviewAuthoritativeAudioClockSecond()
              : state_.qtPreviewPauseSecond_)
        : -1.0;
    // Navigating away always tears down the latency audition. onPageLeft() is
    // idempotent (setOnPage(false) no-ops when not on the page), so it is NOT
    // gated on activeOutlineKey_ == "latency": the sidebar click handler overwrites
    // that key with the destination BEFORE calling this switch, so the old guard
    // was always false and teardown (audio-level restore + flag clear) was silently
    // skipped — the root cause of the SFX-volume leak into the normal preview.
    // Same contract for the export page: every leave path tears down its
    // embedded video panel (idempotent; a running export keeps rendering).
    if (ui_.qmlExportSession_ != nullptr) {
        ui_.qmlExportSession_->leave();
    }
    owner_.cacheWorkspaceLayoutSizes();
    owner_.stopQtPreviewPlayback(true);
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.activeDifficultyId_ = 0;
    state_.activeOutlineKey_ = "export";
    owner_.tickOutlineBusySpinner();
    populateMetadataPage();  // keeps document fields in sync for sidebar use
    ui_.editorStack_->setCurrentWidget(ui_.exportPlaceholderPage_);
    setChartBottomTabsMode(false);
    owner_.clearValidationDecorations();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    owner_.updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    owner_.tickOutlineBusySpinner();
    // The expensive part — building the embedded video panel — happens inside
    // onPageEntered. It ticks the spinner at its own sub-step boundaries so the
    // ring keeps rotating across the build (see createEmbeddedVideoExportPanel).
    if (ui_.qmlExportSession_ != nullptr) {
        ui_.qmlExportSession_->enter(previousActiveDifficultyId);
    }
    owner_.tickOutlineBusySpinner();
    // Entering the export page changes the preview aspect (square → export video
    // ratio) and collapses the bottom tabs; both drive the workspace surface to a
    // new size ASYNCHRONOUSLY from QML, after the two refreshes below have already
    // run. Arm the settle watch so that late resize re-runs the finalize and the
    // page doesn't stay composited at its stale, scrambled pre-resize geometry.
    owner_.armWorkspaceSurfaceSettleRelayout();
    owner_.refreshLayoutAfterPageSwitch();
    QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
}

bool MainWindow::DocumentSection::switchToMetadataField()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    // Navigating away always tears down the latency audition. onPageLeft() is
    // idempotent (setOnPage(false) no-ops when not on the page), so it is NOT
    // gated on activeOutlineKey_ == "latency": the sidebar click handler overwrites
    // that key with the destination BEFORE calling this switch, so the old guard
    // was always false and teardown (audio-level restore + flag clear) was silently
    // skipped — the root cause of the SFX-volume leak into the normal preview.
    // Same contract for the export page: every leave path tears down its
    // embedded video panel (idempotent; a running export keeps rendering).
    if (ui_.qmlExportSession_ != nullptr) {
        ui_.qmlExportSession_->leave();
    }
    owner_.cacheWorkspaceLayoutSizes();
    owner_.stopQtPreviewPlayback(true);
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.activeDifficultyId_ = 0;
    state_.activeOutlineKey_ = "metadata";
    populateMetadataPage();
    if (ui_.editorStack_ != nullptr && ui_.metadataPage_ != nullptr) {
        ui_.editorStack_->setCurrentWidget(ui_.metadataPage_);
    }
    setChartBottomTabsMode(false);
    owner_.clearValidationDecorations();
    updateMetadataPageMode();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    owner_.updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    // setChartBottomTabsMode(false) above collapses the bottom tabs, which drives
    // the rehosted workspace surface to a new size ASYNCHRONOUSLY from QML — after
    // the two refreshes below have already run. Arm the settle watch so that late
    // resize re-runs the finalize; without it the just-switched page can stay
    // composited at its stale pre-resize geometry until an input event forces a
    // repaint (same root cause as the export page, milder here: height-only change
    // on a static, top-anchored layout rather than a preview-aspect width change).
    owner_.armWorkspaceSurfaceSettleRelayout();
    owner_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
    return true;
}

bool MainWindow::DocumentSection::switchToWelcomePage()
{
    if (!maybeSaveBeforeContinue()) {
        return false;
    }
    // Navigating away always tears down the latency audition. onPageLeft() is
    // idempotent (setOnPage(false) no-ops when not on the page), so it is NOT
    // gated on activeOutlineKey_ == "latency": the sidebar click handler overwrites
    // that key with the destination BEFORE calling this switch, so the old guard
    // was always false and teardown (audio-level restore + flag clear) was silently
    // skipped — the root cause of the SFX-volume leak into the normal preview.
    // Same contract for the export page: every leave path tears down its
    // embedded video panel (idempotent; a running export keeps rendering).
    if (ui_.qmlExportSession_ != nullptr) {
        ui_.qmlExportSession_->leave();
    }
    owner_.cacheWorkspaceLayoutSizes();
    owner_.stopQtPreviewPlayback(true);
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.activeDifficultyId_ = 0;
    state_.activeOutlineKey_ = "welcome";
    if (ui_.editorStack_ != nullptr && ui_.welcomePage_ != nullptr) {
        ui_.editorStack_->setCurrentWidget(ui_.welcomePage_);
    }
    setChartBottomTabsMode(false);
    owner_.clearValidationDecorations();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    updateEditorHeader();
    owner_.updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    // Same async-resize settle as switchToMetadataField: setChartBottomTabsMode(false)
    // above collapses the bottom tabs, resizing the rehosted workspace surface from
    // QML after the refreshes below run. Arm the watch so the page repaints at its
    // final geometry instead of a stale, pre-resize composite.
    owner_.armWorkspaceSurfaceSettleRelayout();
    owner_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
    return true;
}

bool MainWindow::DocumentSection::switchToDifficultyField(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId) || state_.document_.difficulty(difficultyId) == nullptr) {
        return false;
    }
    // The user-facing toggle for this was removed in beta59 — behavior is
    // now always "preserve editor position + preview progress when an
    // active difficulty was selected before the switch".
    // Also preserve when coming FROM the latency page OR the export page: both set
    // activeDifficultyId_=0 (so hasActiveDifficulty() is false) but maintain a valid
    // playhead in qtPreviewPauseSecond_ (export audition mirrors the latency
    // sandbox), which we want to carry over to the difficulty. (Both audition flags
    // are still true here — onPageLeft() below tears them down only afterwards.)
    // The metadata (谱面信息) page has no audition either, but leaving a difficulty
    // for it stops playback with keepPosition=true, so qtPreviewPauseSecond_ still
    // holds the last position — carry it back too. Detect the source page from the
    // stack (currentWidget is still the page we're LEAVING — this function switches
    // it to chartPage_ later): activeOutlineKey_ is useless here because the sidebar
    // click handler already overwrote it with the destination ("chart") before
    // calling us. A stale cross-file value is guarded against by loadDocument
    // resetting qtPreviewPauseSecond_ to 0.
    const bool leavingMetadataPage = ui_.editorStack_ != nullptr
        && ui_.metadataPage_ != nullptr
        && ui_.editorStack_->currentWidget() == ui_.metadataPage_;
    const bool restoreSwitchView = owner_.hasActiveDifficulty()
        || state_.latencySandboxAuditionActive_
        || state_.exportPreviewAuditionActive_
        || leavingMetadataPage;
    const double restorePreviewSecond = restoreSwitchView
        ? qMax(0.0, state_.qtPreviewPlaying_
              ? owner_.currentPreviewAuthoritativeAudioClockSecond()
              : state_.qtPreviewPauseSecond_)
        : 0.0;
    int restoreVerticalScrollValue = 0;
    int restoreHorizontalScrollValue = 0;
    if (restoreSwitchView) {
        if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
            if (QScrollBar* vertical = editor->verticalScrollBar(); vertical != nullptr) {
                restoreVerticalScrollValue = vertical->value();
            }
            if (QScrollBar* horizontal = editor->horizontalScrollBar(); horizontal != nullptr) {
                restoreHorizontalScrollValue = horizontal->value();
            }
        }
    }
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    // Navigating away always tears down the latency audition. onPageLeft() is
    // idempotent (setOnPage(false) no-ops when not on the page), so it is NOT
    // gated on activeOutlineKey_ == "latency": the sidebar click handler overwrites
    // that key with the destination BEFORE calling this switch, so the old guard
    // was always false and teardown (audio-level restore + flag clear) was silently
    // skipped — the root cause of the SFX-volume leak into the normal preview.
    // Same contract for the export page: every leave path tears down its
    // embedded video panel (idempotent; a running export keeps rendering).
    if (ui_.qmlExportSession_ != nullptr) {
        ui_.qmlExportSession_->leave();
    }
    owner_.cacheWorkspaceLayoutSizes();
    owner_.stopQtPreviewPlayback(true);
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.activeDifficultyId_ = difficultyId;
    state_.projectLastOpenedDifficultyId_ = difficultyId;
    if (state_.activeOutlineKey_.isEmpty() || state_.activeOutlineKey_ == "metadata" || state_.activeOutlineKey_ == "welcome") {
        state_.activeOutlineKey_ = "chart";
    }
    populateDifficultyPage(difficultyId);
    if (owner_.editorSection_ != nullptr) {
        owner_.editorSection_->syncBookmarksFromEditorText();
    }
    if (restoreSwitchView) {
        applyDifficultySwitchEditorScrollRestore(restoreVerticalScrollValue, restoreHorizontalScrollValue);
        QTimer::singleShot(0, &owner_, [this, difficultyId, restoreVerticalScrollValue, restoreHorizontalScrollValue]() {
            if (state_.activeDifficultyId_ != difficultyId || !owner_.hasActiveDifficulty()) {
                return;
            }
            applyDifficultySwitchEditorScrollRestore(restoreVerticalScrollValue, restoreHorizontalScrollValue);
        });
    }
    const double previousPreviewTrackDurationSeconds = state_.previewTrackDurationSeconds_;
    const std::shared_ptr<const miacode::waveform::WaveformData> previousWaveformData =
        state_.timelineQuickStateBridge_ != nullptr ? state_.timelineQuickStateBridge_->waveformData() : nullptr;
    clearTimelineAndPreview();
    if (restoreSwitchView) {
        miacode::mainwindow::shared::writePreviewPauseSecond(
            state_.qtPreviewPauseSecond_, restorePreviewSecond, state_.qtPreviewPlaying_, "switch_to_difficulty_field");
        state_.pendingDifficultySwitchPreviewRestore_ = true;
        state_.pendingDifficultySwitchPreviewRestoreRevision_ = state_.timelineRevision_ + 1;
        state_.pendingDifficultySwitchPreviewRestoreDifficultyId_ = difficultyId;
        state_.pendingDifficultySwitchPreviewRestoreSecond_ = restorePreviewSecond;
        if (ui_.previewSlider_ != nullptr) {
            QSignalBlocker blocker(ui_.previewSlider_);
            ui_.previewSlider_->setMaximum(qMax(ui_.previewSlider_->maximum(), qMax(1, qRound(restorePreviewSecond * 1000.0))));
        }
        if (ui_.previewSlider_ != nullptr && !state_.previewScrubDragging_) {
            const int value = qBound(0, qRound(restorePreviewSecond * 1000.0), ui_.previewSlider_->maximum());
            QSignalBlocker blocker(ui_.previewSlider_);
            ui_.previewSlider_->setValue(value);
        }
        if (state_.timelineQuickStateBridge_ != nullptr) {
            state_.timelineQuickStateBridge_->setPlayheadSeconds(restorePreviewSecond, false);
        }
        if (state_.previewCanvas_ != nullptr) {
            state_.previewCanvas_->setPlayheadSeconds(restorePreviewSecond, false);
        }
    } else {
        state_.pendingDifficultySwitchPreviewRestore_ = false;
        state_.pendingDifficultySwitchPreviewRestoreRevision_ = 0;
        state_.pendingDifficultySwitchPreviewRestoreDifficultyId_ = 0;
        state_.pendingDifficultySwitchPreviewRestoreSecond_ = 0.0;
    }
    if (previousWaveformData) {
        owner_.applyWaveformData(previousWaveformData);
    } else if (previousPreviewTrackDurationSeconds > 0.0) {
        state_.previewTrackDurationSeconds_ = previousPreviewTrackDurationSeconds;
        owner_.updatePreviewSliderRange();
    }
    if (!state_.currentFilePath_.isEmpty()) {
        owner_.syncPreviewStageMediaRouteChartPath(
            state_.currentFilePath_,
            state_.lastTrackPath_,
            state_.qtPreviewPauseSecond_,
            state_.document_.videoPath);  // Phase 4c — &video= override
    }
    if (ui_.editorStack_ != nullptr && ui_.chartPage_ != nullptr) {
        ui_.editorStack_->setCurrentWidget(ui_.chartPage_);
    }
    setChartBottomTabsMode(true);
    // Entering a difficulty re-asserts the correct preview levels. With the latency
    // audition torn down above (onPageLeft), the mode is Normal, so the single
    // mode-aware dispatch entry pushes the user's real mix (see
    // applyPreviewAudioSettingsToRuntime) — not a special-cased override.
    owner_.applyPreviewAudioSettingsToRuntime();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    QTimer::singleShot(0, &owner_, [this, difficultyId]() {
        if (state_.activeDifficultyId_ != difficultyId || !owner_.hasActiveDifficulty()) {
            return;
        }
        owner_.restoreBottomTabsCurrentTabAfterRefresh(MainWindow::BottomTabsTabId::Timeline);
        owner_.scheduleTimelineRefresh();
    });
    owner_.saveProjectRenderState();
    // Chart-switch leak gauge. This is the single funnel for BOTH switch paths —
    // loadDocument() reaches a chart only via activateInitialField() -> here — so
    // one call covers difficulty switches and file opens alike. No-ops outside
    // --debug. See emitChartSwitchResourceGauge() for what the sample means.
    owner_.emitChartSwitchResourceGauge();
    owner_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
    return true;
}

void MainWindow::DocumentSection::activateInitialField()
{
    const QVector<int> ids = state_.document_.difficultyIds();
    if (!ids.isEmpty()) {
        state_.activeOutlineKey_ = "chart";
        int targetId = 0;
        if (SimaiDocument::isDifficultyId(state_.projectLastOpenedDifficultyId_)
            && ids.contains(state_.projectLastOpenedDifficultyId_)) {
            targetId = state_.projectLastOpenedDifficultyId_;
        }
        if (targetId == 0) {
            const QVector<int> preferredOrder{5, 6, 4, 7, 3, 2, 1};
            targetId = ids.constFirst();
            for (int id : preferredOrder) {
                if (ids.contains(id)) {
                    targetId = id;
                    break;
                }
            }
        }
        switchToDifficultyField(targetId);
    } else {
        state_.activeOutlineKey_ = "welcome";
        switchToWelcomePage();
        clearTimelineAndPreview();
    }
}

void MainWindow::DocumentSection::loadDocument(const SimaiDocument& document)
{
    clearDeletedDifficultyUndoState();
    state_.document_ = document;
    // Build the comment-derived bookmark cache before activateInitialField();
    // the difficulty switch below refreshes it again for the live editor text.
    if (owner_.editorSection_ != nullptr) {
        owner_.editorSection_->adoptBookmarksForLoadedDocument();
    }
    resetAutosaveState(state_.document_.toText());
    state_.documentDirty_ = false;
    state_.currentFieldDirty_ = false;
    state_.activeDifficultyId_ = 0;
    // A freshly loaded document has no carried-over preview position; clear it so
    // the initial-field activation (and the metadata-page restore branch in
    // switchToDifficultyField) can never resurrect a stale second from the file
    // that was open before this one. Same reason for the export audition's
    // last-installed difficulty (its position-preserve gate).
    miacode::mainwindow::shared::writePreviewPauseSecond(
        state_.qtPreviewPauseSecond_, 0.0, state_.qtPreviewPlaying_, "load_document");
    state_.lastExportAuditionDifficultyId_ = 0;
    state_.activeOutlineKey_ = state_.document_.difficultyIds().isEmpty() ? QStringLiteral("welcome") : QStringLiteral("chart");
    activateInitialField();
    updateMetadataPageMode();
    // Restore the per-project "all difficulties share the same designer"
    // preference (or infer it on first open) AFTER metadata-page-mode is
    // refreshed, so the checkbox UI exists and is in its final layout.
    refreshUnifiedDesignerStateForLoadedDocument();
    owner_.loadProjectValidationPreferences();
    updateDirtyState();
    owner_.updateWindowTitle();
    if (owner_.extensionManager_ != nullptr) {
        owner_.extensionManager_->publishEvent(QStringLiteral("workspace.document.opened"), QJsonObject{
            {QStringLiteral("source"), QStringLiteral("workspace")},
            {QStringLiteral("data"), QJsonObject{
                {QStringLiteral("uri"), state_.currentFilePath_.isEmpty()
                    ? QStringLiteral("untitled:active")
                    : QUrl::fromLocalFile(state_.currentFilePath_).toString()},
                {QStringLiteral("difficultyCount"), state_.document_.difficultyIds().size()},
            }},
        });
    }
    // All replacement routes (startup target, chart drop, New, discard,
    // backup restore, and accepted full-source transactions) funnel through
    // this method.  Publish only after loadDocument has established the
    // active difficulty, title and document-owned bookmark cache so QML never
    // observes a mixture of the outgoing and incoming documents.
    emit owner_.documentReplaced();
}

void MainWindow::DocumentSection::clearTimelineAndPreview()
{
    state_.timelineQuickModel_.clear();
    state_.pendingTimelineSlowRefresh_ = TimelineSlowRefreshRequest();
    state_.pendingTimelineAnalysisRefresh_ = TimelineAnalysisRefreshRequest();
    state_.timelineSlowRequestedRevision_ = 0;
    state_.timelineSlowRunningRevision_ = 0;
    state_.timelineAnalysisRequestedRevision_ = 0;
    state_.timelineAnalysisRunningRevision_ = 0;
    state_.lastPreviewNoteMarkerSignature_.clear();
    state_.latestTimelineNoteMarkers_.clear();
    state_.latestTimelineNoteMarkerSignature_.clear();
    state_.latestTimelinePreviewRevision_ = 0;
    state_.latestTimelinePreviewSnapshotReady_ = false;
    state_.lastTimelineParseDifficultyId_ = 0;
    state_.lastTimelineParseChartText_.clear();
    state_.lastTimelineParseTimingMetadata_ = miacode::simai::SimaiTimingMetadata();
    state_.lastTimelineParseResult_ = SimaiNativeParseResult();
    state_.muriAnalysisReport_ = MuriAnalysisReport();
    state_.muriAnalysisReport_.revision = ++state_.muriAnalysisReportRevisionCounter_;
    state_.muriAnalysisReportNoteMarkerSignature_.clear();
    state_.muriAnalysisReportDifficultyId_ = 0;
    state_.muriAnalysisReportTimelineRevision_ = 0;
    state_.muriAnalysisResultAvailable_ = false;
    state_.muriStaticReferencesNoteMarkerSignature_.clear();
    state_.muriStaticReferencesDifficultyId_ = 0;
    state_.muriStaticReferencesTimelineRevision_ = 0;
    state_.muriStaticReferencesAvailable_ = false;
    state_.pendingDeferredValidationUiRefresh_ = false;
    state_.pendingDeferredMuriUiRefresh_ = false;
    if (ui_.timelineAnalysisIdleTimer_ != nullptr) {
        ui_.timelineAnalysisIdleTimer_->stop();
    }
    owner_.clearPreviewFollowDecoration();
    owner_.clearPreviewObjectStats();
    owner_.clearMuriDiagnostics();
    state_.previewTrackDurationSeconds_ = 0.0;
    state_.qtPreviewTimelineDirty_ = false;
    state_.qtPreviewPendingTimelineSecond_ = 0.0;
    state_.qtPreviewPendingTimelineCenterView_ = true;
    state_.previewFollowBindingCacheValid_ = false;
    state_.previewFollowBindingCache_ = TimelineQuickModel::PreviewFollowBinding();
    state_.pendingQuickTimelineCursorSync_ = false;
    state_.pendingQuickTimelineCursorSecond_ = 0.0;
    state_.pendingQuickTimelineCursorCenterView_ = false;
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.qtPreviewLastTimelineSecond_ = -1.0;
    state_.qtPreviewTimelineStartSecond_ = 0.0;
    state_.qtPreviewPlaybackReturnSecond_ = 0.0;
    state_.qtPreviewPlaybackEndSecond_ = 0.0;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->clearTimeline();
    }
    owner_.stopQtPreviewPlayback(false);
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->clear();
        state_.timelineQuickStateBridge_->setMuriAnalysisReport(state_.muriAnalysisReport_);
    }
    if (state_.previewCanvas_ != nullptr) {
        state_.previewCanvas_->reset();
        state_.previewCanvas_->setMuriAnalysisReport(state_.muriAnalysisReport_);
    }
    owner_.clearPreviewStageMediaRoute();
    owner_.updatePreviewSliderRange();
    owner_.updatePreviewSliderPosition(0.0);
}
