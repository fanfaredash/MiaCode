#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"
#include "../editor/MainWindow.EditorSection.h"

#include "BracketScopeHighlighter.h"
#include "BusySpinner.h"
#include "DialogLocalization.h"
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
}


void MainWindow::DocumentSection::updateDifficultyScopedActionStates()
{
}


void MainWindow::DocumentSection::updateEditorHeaderLayoutMode()
{
}


void MainWindow::DocumentSection::syncEditorHeaderMinimumWidth()
{
}


void MainWindow::DocumentSection::updateEditorStatus()
{
}


void MainWindow::DocumentSection::updateEditorEmptyState()
{
}


void MainWindow::DocumentSection::updateMetadataPageMode()
{
}


bool MainWindow::DocumentSection::deleteDifficultyField(int difficultyId, bool alreadyConfirmed)
{
    const SimaiDifficultyData* difficultyData = owner_.applicationServices_.workspace().document().difficulty(difficultyId);
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

    if (!emptyDifficulty && !alreadyConfirmed) {
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
    if (!owner_.applicationServices_.workspace().removeDifficulty(difficultyId)) {
        return false;
    }
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
        const QVector<int> remainingIds = owner_.applicationServices_.workspace().document().difficultyIds();
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
    Q_UNUSED(difficultyId);
    Q_UNUSED(line);
    return nullptr;
}


void MainWindow::DocumentSection::revealBookmarkInSidebar(int difficultyId, int line, bool beginRename)
{
    Q_UNUSED(difficultyId);
    Q_UNUSED(line);
    Q_UNUSED(beginRename);
}


void MainWindow::DocumentSection::rebuildFieldSidebar()
{
}


void MainWindow::DocumentSection::populateMetadataPage()
{
}


void MainWindow::DocumentSection::populateDifficultyPage(int difficultyId)
{
    Q_UNUSED(difficultyId);
}


void MainWindow::DocumentSection::syncHeaderDesignerEditFromModel()
{
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
    // This used to defer the switch one event-loop tick behind a busy spinner
    // drawn over the "Export" sidebar row, because building the embedded video
    // panel blocked the UI thread. Both halves of that are gone: the embedded
    // panel was deleted with the Widgets export dialog, and the sidebar is QML —
    // the spinner lived on the hidden widget list's viewport, so it could never
    // reach a screen. What the deferral did keep doing was return true BEFORE
    // the switch ran, which told the QML page host to show the export page even
    // on a refused switch. Running it inline reports the real answer.
    performSwitchToExportField();
    return ui_.editorStack_->currentWidget() == ui_.exportPlaceholderPage_;
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
    if (!SimaiDocument::isDifficultyId(difficultyId) || owner_.applicationServices_.workspace().document().difficulty(difficultyId) == nullptr) {
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
    // it to chartPage_ later). Widgets outline writes the destination key before
    // calling us; QML leaveOverlayPage does not, so activeOutlineKey_ can still be
    // "export" or "latency" here and cannot be used as the source. A stale
    // cross-file value is guarded against by loadDocument resetting
    // qtPreviewPauseSecond_ to 0.
    const bool leavingMetadataPage = ui_.editorStack_ != nullptr
        && ui_.metadataPage_ != nullptr
        && ui_.editorStack_->currentWidget() == ui_.metadataPage_;
    const bool leavingOverlayField = state_.activeOutlineKey_ == QLatin1String("export")
        || state_.activeOutlineKey_ == QLatin1String("latency");
    const bool restoreSwitchView = owner_.hasActiveDifficulty()
        || state_.latencySandboxAuditionActive_
        || state_.exportPreviewAuditionActive_
        || leavingMetadataPage
        || leavingOverlayField;
    const double restorePreviewSecond = restoreSwitchView
        ? qMax(0.0, state_.qtPreviewPlaying_
              ? owner_.currentPreviewAuthoritativeAudioClockSecond()
              : state_.qtPreviewPauseSecond_)
        : 0.0;
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
    // Widgets outline already wrote "chart". QML leaveOverlayPage resumes through
    // this function with the overlay key still set, and shellExportPageActive()
    // is that key — leftover "export" keeps the fullscreen button hidden.
    if (state_.activeOutlineKey_.isEmpty()
        || state_.activeOutlineKey_ == QLatin1String("metadata")
        || state_.activeOutlineKey_ == QLatin1String("welcome")
        || state_.activeOutlineKey_ == QLatin1String("export")
        || state_.activeOutlineKey_ == QLatin1String("latency")) {
        state_.activeOutlineKey_ = QStringLiteral("chart");
    }
    populateDifficultyPage(difficultyId);
    if (owner_.editorSection_ != nullptr) {
        owner_.editorSection_->syncBookmarksFromEditorText();
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
            owner_.applicationServices_.workspace().document().videoPath);  // Phase 4c — &video= override
    }
    if (ui_.editorStack_ != nullptr && ui_.chartPage_ != nullptr) {
        ui_.editorStack_->setCurrentWidget(ui_.chartPage_);
    }
    setChartBottomTabsMode(true);
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setFollowPreviewEnabled(state_.previewFollowEnabled_);
    }
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
    const QVector<int> ids = owner_.applicationServices_.workspace().document().difficultyIds();
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

void MainWindow::DocumentSection::loadDocument()
{
    clearDeletedDifficultyUndoState();
    const miacode::v2::ChartWorkspaceSnapshot snapshot =
        owner_.applicationServices_.workspace().snapshot();
    resetAutosaveState(snapshot.sourceText);
    state_.documentDirty_ = snapshot.dirty;
    state_.currentFieldDirty_ = false;
    miacode::mainwindow::shared::writePreviewPauseSecond(
        state_.qtPreviewPauseSecond_, 0.0, state_.qtPreviewPlaying_, "load_document");
    state_.lastExportAuditionDifficultyId_ = 0;
    state_.activeDifficultyId_ = snapshot.activeDifficultyId;
    if (SimaiDocument::isDifficultyId(state_.activeDifficultyId_)) {
        state_.projectLastOpenedDifficultyId_ = state_.activeDifficultyId_;
    }
    owner_.loadProjectValidationPreferences();
    updateDirtyState();
    owner_.scheduleTimelineRefresh();
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
