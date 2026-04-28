#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"

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
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

void MainWindow::DocumentSection::updateEditorHeader()
{
    updateDifficultyScopedActionStates();
    if (ui_.editorContextLabel_ == nullptr) {
        return;
    }
    if (!owner_.hasActiveDifficulty()) {
        if (state_.document_.difficultyIds().isEmpty() && state_.activeOutlineKey_ == QLatin1String("welcome")) {
            ui_.editorContextLabel_->setText(uiText("editor.welcome", "Welcome to MiaCode!"));
            ui_.editorContextLabel_->setFont(uiAccentFont(15, QFont::DemiBold));
        } else {
            ui_.editorContextLabel_->setText(uiText("editor.metadata", "Metadata"));
            ui_.editorContextLabel_->setFont(uiAccentFont(12, QFont::DemiBold));
        }
        ui_.editorContextLabel_->setStyleSheet(QString());
        if (ui_.editorDifficultyControls_ != nullptr) {
            ui_.editorDifficultyControls_->hide();
        }
        if (ui_.editorBatchTransformControls_ != nullptr) {
            ui_.editorBatchTransformControls_->hide();
        }
        updateDifficultyDeleteButton(false);
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
    updateDifficultyDeleteButton(false);
    updateEditorHeaderLayoutMode();
    owner_.updateEditorValidationSummary();
}

void MainWindow::DocumentSection::updateDifficultyScopedActionStates()
{
    const bool enabled = owner_.hasActiveDifficulty();

    if (ui_.validateAction_ != nullptr) {
        ui_.validateAction_->setEnabled(enabled);
    }
    if (ui_.pausePreviewAction_ != nullptr) {
        ui_.pausePreviewAction_->setEnabled(enabled);
    }
    if (ui_.stopOrPlayPreviewShortcutAction_ != nullptr) {
        ui_.stopOrPlayPreviewShortcutAction_->setEnabled(enabled);
    }
    if (ui_.playPausePreviewShortcutAction_ != nullptr) {
        ui_.playPausePreviewShortcutAction_->setEnabled(enabled);
    }
    if (ui_.exportVideoAction_ != nullptr) {
        ui_.exportVideoAction_->setEnabled(enabled);
    }
    if (ui_.stopPreviewAction_ != nullptr) {
        ui_.stopPreviewAction_->setEnabled(enabled);
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
    if (ui_.stopPreviewButton_ != nullptr) {
        ui_.stopPreviewButton_->setEnabled(enabled);
    }
    if (ui_.pausePreviewButton_ != nullptr) {
        ui_.pausePreviewButton_->setEnabled(enabled);
    }
    if (ui_.syntaxCheckButton_ != nullptr) {
        ui_.syntaxCheckButton_->setEnabled(enabled);
    }
    if (ui_.exportVideoButton_ != nullptr) {
        ui_.exportVideoButton_->setEnabled(enabled);
    }
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

    const int headerWidth = ui_.editorHeaderWidget_->contentsRect().width();
    const bool summaryHasContent =
        ui_.editorValidationSummaryWidget_ != nullptr
        && ui_.editorValidationSummaryWidget_->property("hasContent").toBool();
    const auto summaryGroupFullWidth = [](QLabel* icon, QLabel* count, int spacing) {
        const bool hasContent = (icon != nullptr && icon->property("hasContent").toBool())
            || (count != nullptr && count->property("hasContent").toBool());
        if (!hasContent) {
            return 0;
        }
        int width = 0;
        if (icon != nullptr) {
            width += qMax(icon->sizeHint().width(), icon->minimumWidth());
        }
        if (count != nullptr) {
            if (width > 0) {
                width += spacing;
            }
            width += qMax(count->sizeHint().width(), count->minimumWidth());
        }
        return width;
    };
    int summaryFullWidth = 0;
    int summaryVisibleGroups = 0;
    for (const int groupWidth : {
             summaryGroupFullWidth(ui_.editorValidationMuriIconLabel_, ui_.editorValidationMuriCountLabel_, 4),
             summaryGroupFullWidth(ui_.editorValidationWarningIconLabel_, ui_.editorValidationWarningCountLabel_, 3),
             summaryGroupFullWidth(ui_.editorValidationErrorIconLabel_, ui_.editorValidationErrorCountLabel_, 6)}) {
        if (groupWidth <= 0) {
            continue;
        }
        if (summaryVisibleGroups > 0) {
            summaryFullWidth += 8;
        }
        summaryFullWidth += groupWidth;
        ++summaryVisibleGroups;
    }

    if (ui_.difficultyLevelLabel_ != nullptr) {
        ui_.difficultyLevelLabel_->setVisible(true);
    }
    if (ui_.difficultyDesignerLabel_ != nullptr) {
        ui_.difficultyDesignerLabel_->setVisible(true);
    }
    if (ui_.difficultyLevelEdit_ != nullptr) {
        ui_.difficultyLevelEdit_->setFixedWidth(48);
        ui_.difficultyLevelEdit_->setVisible(true);
    }
    if (ui_.difficultyDesignerEdit_ != nullptr) {
        ui_.difficultyDesignerEdit_->setFixedWidth(96);
        ui_.difficultyDesignerEdit_->setVisible(true);
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
    const QString cursorText = UiText::isChineseUi()
        ? QStringLiteral("%1行 %2列").arg(line).arg(col)
        : QStringLiteral("Ln %1, Col %2").arg(line).arg(col);
    ui_.editorCursorLabel_->setText(cursorText);
    ui_.editorCursorLabel_->setFixedWidth(QFontMetrics(ui_.editorCursorLabel_->font()).horizontalAdvance(cursorText) + 10);
    ui_.editorCursorLabel_->setVisible(true);
    const QString correctedCursorText = UiText::isChineseUi()
        ? QStringLiteral("%1行 %2列").arg(line).arg(col)
        : QStringLiteral("Ln %1, Col %2").arg(line).arg(col);
    const QString correctedCursorWidthTemplate = UiText::isChineseUi()
        ? QStringLiteral("9999行 9999列")
        : QStringLiteral("Ln 9999, Col 9999");
    ui_.editorCursorLabel_->setText(correctedCursorText);
    ui_.editorCursorLabel_->setFixedWidth(
        QFontMetrics(ui_.editorCursorLabel_->font()).horizontalAdvance(correctedCursorWidthTemplate) + 10);

    if (QLayout* headerLayout = ui_.editorHeaderWidget_->layout(); headerLayout != nullptr) {
        headerLayout->activate();
    }
    syncEditorHeaderMinimumWidth();
    return;

    struct HeaderLayoutCandidate {
        bool showLevelControls = true;
        bool showDesignerControls = true;
        bool showSummary = false;
        bool showSummaryCounts = false;
        bool showCursor = false;
        bool compactCursor = false;
    };

    const auto applyCandidate = [this, summaryHasContent](const HeaderLayoutCandidate& candidate) {
        constexpr int kLevelWidth = 48;
        constexpr int kDesignerWidth = 96;

        if (ui_.difficultyLevelLabel_ != nullptr) {
            ui_.difficultyLevelLabel_->setVisible(candidate.showLevelControls);
        }
        if (ui_.difficultyDesignerLabel_ != nullptr) {
            ui_.difficultyDesignerLabel_->setVisible(candidate.showDesignerControls);
        }
        if (ui_.difficultyLevelEdit_ != nullptr) {
            ui_.difficultyLevelEdit_->setFixedWidth(kLevelWidth);
            ui_.difficultyLevelEdit_->setVisible(candidate.showLevelControls);
        }
        if (ui_.difficultyDesignerEdit_ != nullptr) {
            ui_.difficultyDesignerEdit_->setFixedWidth(kDesignerWidth);
            ui_.difficultyDesignerEdit_->setVisible(candidate.showDesignerControls);
        }
        if (ui_.editorDifficultyControls_ != nullptr) {
            ui_.editorDifficultyControls_->setVisible(candidate.showLevelControls || candidate.showDesignerControls);
        }

        if (ui_.editorValidationSummaryWidget_ != nullptr) {
            const bool showSummary = summaryHasContent && candidate.showSummary;
            ui_.editorValidationSummaryWidget_->setVisible(showSummary);
            const auto applySummaryVisibility = [showSummary, candidate](QLabel* icon, QLabel* count) {
                const bool hasContent = (icon != nullptr && icon->property("hasContent").toBool())
                    || (count != nullptr && count->property("hasContent").toBool());
                if (icon != nullptr) {
                    icon->setVisible(showSummary && hasContent);
                }
                if (count != nullptr) {
                    count->setVisible(showSummary && candidate.showSummaryCounts && hasContent);
                }
            };
            applySummaryVisibility(ui_.editorValidationErrorIconLabel_, ui_.editorValidationErrorCountLabel_);
            applySummaryVisibility(ui_.editorValidationWarningIconLabel_, ui_.editorValidationWarningCountLabel_);
            applySummaryVisibility(ui_.editorValidationMuriIconLabel_, ui_.editorValidationMuriCountLabel_);
            ui_.editorValidationSummaryWidget_->adjustSize();
        }
    };

    const QVector<HeaderLayoutCandidate> candidates = summaryHasContent
        ? QVector<HeaderLayoutCandidate>{
              HeaderLayoutCandidate{true, true, true, true, true, false},
              HeaderLayoutCandidate{true, true, true, false, true, false},
              HeaderLayoutCandidate{true, true, false, false, true, false},
              HeaderLayoutCandidate{true, true, false, false, true, true},
              HeaderLayoutCandidate{true, true, false, false, false, false},
              HeaderLayoutCandidate{true, false, false, false, false, false},
              HeaderLayoutCandidate{false, false, false, false, false, false},
          }
        : QVector<HeaderLayoutCandidate>{
              HeaderLayoutCandidate{true, true, false, false, true, false},
              HeaderLayoutCandidate{true, true, false, false, true, true},
              HeaderLayoutCandidate{true, true, false, false, false, false},
              HeaderLayoutCandidate{true, false, false, false, false, false},
              HeaderLayoutCandidate{false, false, false, false, false, false},
          };

    for (int index = 0; index < candidates.size(); ++index) {
        const HeaderLayoutCandidate& candidate = candidates.at(index);
        applyCandidate(candidate);

        if (candidate.showCursor) {
            const auto [line, col] = currentCursorLineCol();
            if (candidate.compactCursor) {
                ui_.editorCursorLabel_->setText(QStringLiteral("%1:%2").arg(line).arg(col));
            } else if (UiText::isChineseUi()) {
                ui_.editorCursorLabel_->setText(QStringLiteral("%1琛?%2鍒?").arg(line).arg(col));
            } else {
                ui_.editorCursorLabel_->setText(QStringLiteral("Ln %1, Col %2").arg(line).arg(col));
            }
            ui_.editorCursorLabel_->setFixedWidth(
                QFontMetrics(ui_.editorCursorLabel_->font()).horizontalAdvance(ui_.editorCursorLabel_->text()) + 10);
        }
        ui_.editorCursorLabel_->setVisible(candidate.showCursor);

        if (QLayout* headerLayout = ui_.editorHeaderWidget_->layout(); headerLayout != nullptr) {
            headerLayout->activate();
        }
        const int requiredWidth = ui_.editorHeaderWidget_->minimumSizeHint().width();
        if (headerWidth >= requiredWidth || index == candidates.size() - 1) {
            break;
        }
    }

    const bool showCursor = ui_.editorCursorLabel_->isVisible();
    const bool compactCursor = showCursor
        && ui_.editorCursorLabel_->text().contains(QLatin1Char(':'))
        && !ui_.editorCursorLabel_->text().contains(QLatin1Char(','));
    if (showCursor) {
        const auto [line, col] = currentCursorLineCol();
        if (compactCursor) {
            ui_.editorCursorLabel_->setText(QStringLiteral("%1:%2").arg(line).arg(col));
        } else if (UiText::isChineseUi()) {
            ui_.editorCursorLabel_->setText(QStringLiteral("%1行 %2列").arg(line).arg(col));
        } else {
            ui_.editorCursorLabel_->setText(QStringLiteral("Ln %1, Col %2").arg(line).arg(col));
        }
        ui_.editorCursorLabel_->setFixedWidth(
            QFontMetrics(ui_.editorCursorLabel_->font()).horizontalAdvance(ui_.editorCursorLabel_->text()) + 10);
    }
    ui_.editorCursorLabel_->setVisible(showCursor);
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
        const auto widgetMinimumWidth = [](const QWidget* widget) {
            if (widget == nullptr || widget->isHidden()) {
                return 0;
            }
            return qMax(widget->minimumSizeHint().width(), widget->sizeHint().width());
        };
        const QMargins margins = headerLayout->contentsMargins();
        headerMinimumWidth = margins.left() + margins.right();
        int visibleSectionCount = 0;
        const auto addSectionWidth = [&](int width) {
            if (width <= 0) {
                return;
            }
            if (visibleSectionCount > 0) {
                headerMinimumWidth += qMax(0, headerLayout->spacing());
            }
            headerMinimumWidth += width;
            ++visibleSectionCount;
        };
        addSectionWidth(widgetMinimumWidth(ui_.editorContextLabel_));
        addSectionWidth(widgetMinimumWidth(ui_.editorDifficultyControls_));
        addSectionWidth(widgetMinimumWidth(ui_.editorValidationSummaryWidget_));
        addSectionWidth(widgetMinimumWidth(ui_.editorCursorLabel_ != nullptr ? ui_.editorCursorLabel_->parentWidget() : nullptr));
        headerMinimumWidth = qMax(headerMinimumWidth, margins.left() + margins.right());
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
    const QString currentDesigner =
        deletingActiveDifficulty && ui_.difficultyDesignerEdit_ != nullptr ? ui_.difficultyDesignerEdit_->text() : difficultyData->designer;
    const QString currentChart = deletingActiveDifficulty ? owner_.editorText() : difficultyData->chart;
    const bool emptyDifficulty = currentLevel.trimmed().isEmpty()
        && currentDesigner.trimmed().isEmpty()
        && currentChart.trimmed().isEmpty();

    if (!emptyDifficulty) {
        const QMessageBox::StandardButton choice = UiDialogs::showMessageBox(
            QMessageBox::Question,
            &owner_,
            "Delete Difficulty",
            QString("Delete %1?").arg(difficultyName),
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
        owner_.statusBar()->showMessage(QString("Deleted %1.").arg(difficultyName));
        return true;
    }
    if (!saveToPath(state_.currentFilePath_)) {
        owner_.statusBar()->showMessage(QString("Deleted %1. Changes are still unsaved.").arg(difficultyName));
    }
    return true;
}

void MainWindow::DocumentSection::updateDifficultyDeleteButton(bool visible)
{
    if (ui_.deleteDifficultyButton_ == nullptr) {
        return;
    }
    constexpr int kOutlineIconOnlyThreshold = 120;
    const int outlineListWidth =
        ui_.outlineList_ != nullptr && ui_.outlineList_->viewport() != nullptr ? ui_.outlineList_->viewport()->width() : 0;
    if (!visible
        || !owner_.hasActiveDifficulty()
        || ui_.outlineList_ == nullptr
        || (outlineListWidth > 0 && outlineListWidth < kOutlineIconOnlyThreshold)) {
        ui_.deleteDifficultyButton_->hide();
        return;
    }
    QListWidgetItem* currentItem = ui_.outlineList_->currentItem();
    if (currentItem == nullptr || !SimaiDocument::isDifficultyId(currentItem->data(Qt::UserRole + 1).toInt())) {
        ui_.deleteDifficultyButton_->hide();
        return;
    }
    const QRect rowRect = ui_.outlineList_->visualItemRect(currentItem);
    if (!rowRect.isValid() || rowRect.isEmpty()) {
        ui_.deleteDifficultyButton_->hide();
        return;
    }
    const int x = rowRect.right() - ui_.deleteDifficultyButton_->width() - 8;
    const int y = rowRect.top() + (rowRect.height() - ui_.deleteDifficultyButton_->height()) / 2;
    ui_.deleteDifficultyButton_->move(x, y);
    ui_.deleteDifficultyButton_->raise();
    ui_.deleteDifficultyButton_->show();
}

void MainWindow::DocumentSection::rebuildFieldSidebar()
{
    if (ui_.outlineList_ == nullptr) {
        return;
    }
    updateDifficultyDeleteButton(false);
    QSignalBlocker blocker(ui_.outlineList_);
    ui_.outlineList_->clear();
    const QString metadataLabel = uiText("sidebar.metadata", "Metadata");
    auto* metadataItem = new QListWidgetItem(
        owner_.style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        metadataLabel,
        ui_.outlineList_
    );
    metadataItem->setData(Qt::UserRole, "metadata");
    metadataItem->setToolTip(metadataLabel);

    QListWidgetItem* selectedItem = nullptr;
    bool hasMissingDifficulty = false;
    const QVector<int> ids = state_.document_.difficultyIds();
    for (int id : ids) {
        const QString difficultyLabel = SimaiDocument::difficultyName(id);
        auto* difficultyItem = new QListWidgetItem(difficultyLabel, ui_.outlineList_);
        difficultyItem->setIcon(makeDifficultyBadgeIcon(id));
        difficultyItem->setData(Qt::UserRole, "difficulty_chart");
        difficultyItem->setData(Qt::UserRole + 1, id);
        difficultyItem->setToolTip(difficultyLabel);
        if (id == state_.activeDifficultyId_) {
            selectedItem = difficultyItem;
        }
    }
    for (int id = 1; id <= 7; ++id) {
        if (state_.document_.difficulty(id) == nullptr) {
            hasMissingDifficulty = true;
            break;
        }
    }
    if (hasMissingDifficulty) {
        const QString addDifficultyLabel = uiText("sidebar.add_difficulty", "+ Add Difficulty");
        auto* addItem = new QListWidgetItem(
            owner_.style()->standardIcon(QStyle::SP_FileDialogNewFolder),
            addDifficultyLabel,
            ui_.outlineList_
        );
        addItem->setData(Qt::UserRole, "add");
        addItem->setToolTip(addDifficultyLabel);
    }
    auto* toolboxItem = new QListWidgetItem(
        makeToolboxAccessIcon(UiTheme::colors().iconPrimary, QColor(QStringLiteral("#E6B84A"))),
        UiText::isChineseUi() ? QStringLiteral("工具箱") : QStringLiteral("Toolbox"),
        ui_.outlineList_
    );
    toolboxItem->setData(Qt::UserRole, "toolbox");
    toolboxItem->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("打开工具箱：无理检测 / 视频导出 / BPM检测与偏移")
            : QStringLiteral("Open toolbox: Muri Check / Video Export / BPM & Offset")
    );
    toolboxItem->setText(UiText::isChineseUi() ? QStringLiteral("工具箱") : QStringLiteral("Toolbox"));
    toolboxItem->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("打开工具箱：无理检测 / 谱面整理 / 官谱镜像站")
            : QStringLiteral("Open toolbox: Muri Check / Format Chart / Official Chart Mirror")
    );
    if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
        selectedItem = metadataItem;
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
}

void MainWindow::DocumentSection::populateMetadataPage()
{
    if (ui_.titleEdit_ == nullptr || ui_.artistEdit_ == nullptr || ui_.firstEdit_ == nullptr || ui_.designerEdit_ == nullptr) {
        return;
    }
    QSignalBlocker blockerTitle(ui_.titleEdit_);
    QSignalBlocker blockerArtist(ui_.artistEdit_);
    QSignalBlocker blockerFirst(ui_.firstEdit_);
    QSignalBlocker blockerDesigner(ui_.designerEdit_);
    ui_.titleEdit_->setText(state_.document_.title);
    ui_.artistEdit_->setText(state_.document_.artist);
    ui_.firstEdit_->setText(state_.document_.first);
    ui_.designerEdit_->setText(state_.document_.designer);
    ui_.titleEdit_->setModified(false);
    ui_.artistEdit_->setModified(false);
    ui_.firstEdit_->setModified(false);
    ui_.designerEdit_->setModified(false);
    setMetadataExtraText(SimaiDocument::serializeRawFields(state_.document_.extraFields));
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

bool MainWindow::DocumentSection::switchToMetadataField()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
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
    owner_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
    return true;
}

bool MainWindow::DocumentSection::switchToWelcomePage()
{
    if (!maybeSaveBeforeContinue()) {
        return false;
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
    owner_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
    return true;
}

bool MainWindow::DocumentSection::switchToDifficultyField(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId) || state_.document_.difficulty(difficultyId) == nullptr) {
        return false;
    }
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
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
    const double previousPreviewTrackDurationSeconds = state_.previewTrackDurationSeconds_;
    const std::shared_ptr<const miacode::waveform::WaveformData> previousWaveformData =
        state_.timelineQuickStateBridge_ != nullptr ? state_.timelineQuickStateBridge_->waveformData() : nullptr;
    clearTimelineAndPreview();
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
            state_.qtPreviewPauseSecond_);
    }
    if (ui_.editorStack_ != nullptr && ui_.chartPage_ != nullptr) {
        ui_.editorStack_->setCurrentWidget(ui_.chartPage_);
    }
    setChartBottomTabsMode(true);
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
    resetAutosaveState(state_.document_.toText());
    state_.documentDirty_ = false;
    state_.currentFieldDirty_ = false;
    state_.activeDifficultyId_ = 0;
    state_.activeOutlineKey_ = state_.document_.difficultyIds().isEmpty() ? QStringLiteral("welcome") : QStringLiteral("chart");
    activateInitialField();
    updateMetadataPageMode();
    updateDirtyState();
    owner_.updateWindowTitle();
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
    state_.muriAnalysisReportNoteMarkerSignature_.clear();
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
