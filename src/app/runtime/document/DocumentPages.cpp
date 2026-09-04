#include "runtime/document/DocumentSessionHost.h"
#include "runtime/Shared.h"
#include "runtime/editor/EditorHost.h"

#include "app/v2/PlaybackStateAuthority.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
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

using namespace miacode::runtime::shared;

void miacode::runtime::DocumentSessionHost::updateEditorHeader()
{
    updateDifficultyScopedActionStates();
}


void miacode::runtime::DocumentSessionHost::updateDifficultyScopedActionStates()
{
}


void miacode::runtime::DocumentSessionHost::updateEditorHeaderLayoutMode()
{
}


void miacode::runtime::DocumentSessionHost::syncEditorHeaderMinimumWidth()
{
}


void miacode::runtime::DocumentSessionHost::updateEditorStatus()
{
}


void miacode::runtime::DocumentSessionHost::updateEditorEmptyState()
{
}


void miacode::runtime::DocumentSessionHost::updateMetadataPageMode()
{
}


bool miacode::runtime::DocumentSessionHost::deleteDifficultyField(int difficultyId, bool alreadyConfirmed)
{
    const SimaiDifficultyData* difficultyData = session_.applicationServices_.workspace().document().difficulty(difficultyId);
    if (!SimaiDocument::isDifficultyId(difficultyId) || difficultyData == nullptr) {
        return false;
    }

    const bool deletingActiveDifficulty = (difficultyId == state_.activeDifficultyId_);
    const QString difficultyName = SimaiDocument::difficultyName(difficultyId);
    const QString currentLevel = difficultyData->level;
    const QString currentDesigner = difficultyData->designer;
    const QString currentChart = deletingActiveDifficulty ? session_.editorText() : difficultyData->chart;
    const bool emptyDifficulty = currentLevel.trimmed().isEmpty()
        && currentDesigner.trimmed().isEmpty()
        && currentChart.trimmed().isEmpty();

    if (!emptyDifficulty && !alreadyConfirmed) {
        const QMessageBox::StandardButton choice = UiDialogs::showMessageBox(
            QMessageBox::Question,
            nullptr,
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

    session_.stopQtPreviewPlayback(true);
    if (!session_.applicationServices_.workspace().removeDifficulty(difficultyId)) {
        return false;
    }
    state_.validationCacheByDifficulty_.remove(difficultyId);
    if (deletingActiveDifficulty) {
        session_.invalidateDocumentValidationRevision();
    } else {
        emit session_.documentValidationChanged();
    }
    state_.documentDirty_ = true;

    if (deletingActiveDifficulty) {
        session_.cacheWorkspaceLayoutSizes();
        state_.currentFieldDirty_ = false;
        const QVector<int> remainingIds = session_.applicationServices_.workspace().document().difficultyIds();
        if (remainingIds.isEmpty()) {
            state_.activeDifficultyId_ = 0;
            state_.activeOutlineKey_ = "welcome";
            populateMetadataPage();
            setChartBottomTabsMode(false);
            clearTimelineAndPreview();
            session_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &session_, [this]() { session_.refreshLayoutAfterPageSwitch(); });
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
        session_.noteStatus(UiText::text(QStringLiteral("document.deleted_1")).arg(difficultyName));
        return true;
    }
    if (!saveToPath(state_.currentFilePath_)) {
        session_.noteStatus(UiText::text(QStringLiteral("document.deleted_1_changes_are_still")).arg(difficultyName));
    }
    return true;
}

bool miacode::runtime::DocumentSessionHost::isBookmarkGroupExpanded(int difficultyId) const
{
    const auto it = state_.outlineBookmarkGroupExpanded_.constFind(difficultyId);
    if (it != state_.outlineBookmarkGroupExpanded_.cend()) {
        return it.value();
    }
    // Untouched groups: the active difficulty starts expanded, others folded.
    return difficultyId == state_.activeDifficultyId_;
}

void miacode::runtime::DocumentSessionHost::setBookmarkGroupExpanded(int difficultyId, bool expanded)
{
    state_.outlineBookmarkGroupExpanded_.insert(difficultyId, expanded);
    rebuildFieldSidebar();
}

QListWidgetItem* miacode::runtime::DocumentSessionHost::findBookmarkSidebarItem(int difficultyId, int line) const
{
    Q_UNUSED(difficultyId);
    Q_UNUSED(line);
    return nullptr;
}


void miacode::runtime::DocumentSessionHost::revealBookmarkInSidebar(int difficultyId, int line, bool beginRename)
{
    Q_UNUSED(difficultyId);
    Q_UNUSED(line);
    Q_UNUSED(beginRename);
}


void miacode::runtime::DocumentSessionHost::rebuildFieldSidebar()
{
}


void miacode::runtime::DocumentSessionHost::populateMetadataPage()
{
}


void miacode::runtime::DocumentSessionHost::populateDifficultyPage(int difficultyId)
{
    Q_UNUSED(difficultyId);
}


void miacode::runtime::DocumentSessionHost::syncHeaderDesignerEditFromModel()
{
}


void miacode::runtime::DocumentSessionHost::setChartBottomTabsMode(bool enabled)
{
    session_.setBottomTabsTabVisible(Session::BottomTabsTabId::Timeline, enabled);
    session_.setValidationTabVisible(enabled);
    session_.setBottomTabsTabVisible(Session::BottomTabsTabId::Muri, enabled);

    if (enabled) {
        session_.setCurrentBottomTabsTabId(Session::BottomTabsTabId::Timeline);
    }
}

bool miacode::runtime::DocumentSessionHost::switchToLatencyField()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    if (ui_.qmlExportSession_ != nullptr) {
        ui_.qmlExportSession_->leave();
    }
    session_.cacheWorkspaceLayoutSizes();
    // Preserve the current preview position across the switch, just like
    // switchToDifficultyField does, so entering the latency page keeps the
    // playhead instead of snapping to 0. installSandboxScene() consumes
    // pauseSecond_ (clamped to the test chart duration).
    const double restorePreviewSecond = qMax(0.0, state_.playing_
        ? session_.currentPreviewAuthoritativeAudioClockSecond()
        : state_.pauseSecond_);
    session_.stopQtPreviewPlayback(true);
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    // Non-command write: a page switch relocating the paused playhead, not a
    // seek — see PlaybackStateAuthority.h.
    if (auto* authority = session_.applicationServices_.playbackStateAuthority(); authority != nullptr) {
        authority->repositionSilently(restorePreviewSecond, "switch_to_latency_field");
    }
    state_.activeDifficultyId_ = 0;
    state_.activeOutlineKey_ = "latency";
    populateMetadataPage();
    setChartBottomTabsMode(true);
    session_.clearValidationDecorations();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    session_.updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    session_.refreshLayoutAfterPageSwitch();
    QTimer::singleShot(0, &session_, [this]() { session_.refreshLayoutAfterPageSwitch(); });
    return true;
}

bool miacode::runtime::DocumentSessionHost::switchToExportField()
{
    if (!maybeSaveCurrentFieldChanges()) {
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
    return state_.activeOutlineKey_ == QLatin1String("export");
}

void miacode::runtime::DocumentSessionHost::performSwitchToExportField()
{
    const int previousActiveDifficultyId = state_.activeDifficultyId_;
    // Carry the current preview position INTO the export audition so it doesn't
    // snap to 0 — matching the difficulty-tab switch (which preserves progress
    // when a difficulty / the latency page was active before the switch). Read
    // the authoritative clock while it is still live (before stopQtPreviewPlayback
    // below); installExportPreviewAuditionScene consumes this one-shot seed.
    // The metadata (谱面信息) page keeps no audition, but leaving a difficulty for
    // it stopped playback with keepPosition=true, so pauseSecond_ still
    // holds the last position — carry it into the export page too. Source detected
    // from the stack (currentWidget is still the page we're LEAVING; the switch to
    // the export field happens later), because activeOutlineKey_ was already overwritten
    // with the destination by the sidebar handler. A stale cross-file value is
    // guarded by loadDocument resetting pauseSecond_ to 0.
    const bool leavingMetadataPage = state_.activeOutlineKey_ == QLatin1String("metadata");
    const bool restoreEntryPreview = session_.hasActiveDifficulty()
        || state_.latencySandboxAuditionActive_
        || state_.exportPreviewAuditionActive_   // re-entering export from export (sidebar re-click)
        || leavingMetadataPage;
    state_.exportPreviewEntrySeedSecond_ = restoreEntryPreview
        ? qMax(0.0, state_.playing_
              ? session_.currentPreviewAuthoritativeAudioClockSecond()
              : state_.pauseSecond_)
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
    session_.cacheWorkspaceLayoutSizes();
    session_.stopQtPreviewPlayback(true);
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.activeDifficultyId_ = 0;
    state_.activeOutlineKey_ = "export";
    populateMetadataPage();
    setChartBottomTabsMode(false);
    session_.clearValidationDecorations();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    session_.updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    // The expensive part — building the embedded video panel — happens inside
    // onPageEntered.
    if (ui_.qmlExportSession_ != nullptr) {
        ui_.qmlExportSession_->enter(previousActiveDifficultyId);
    }
    session_.refreshLayoutAfterPageSwitch();
    QTimer::singleShot(0, &session_, [this]() { session_.refreshLayoutAfterPageSwitch(); });
}

bool miacode::runtime::DocumentSessionHost::switchToMetadataField()
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
    session_.cacheWorkspaceLayoutSizes();
    session_.stopQtPreviewPlayback(true);
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.activeDifficultyId_ = 0;
    state_.activeOutlineKey_ = "metadata";
    populateMetadataPage();
    setChartBottomTabsMode(false);
    session_.clearValidationDecorations();
    updateMetadataPageMode();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    session_.updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    session_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &session_, [this]() { session_.refreshLayoutAfterPageSwitch(); });
    return true;
}

bool miacode::runtime::DocumentSessionHost::switchToWelcomePage()
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
    session_.cacheWorkspaceLayoutSizes();
    session_.stopQtPreviewPlayback(true);
    state_.pendingPreviewPlaybackStart_ = false;
    state_.pendingPreviewPlaybackResumeFromPause_ = false;
    state_.pendingPreviewPlaybackRevision_ = 0;
    state_.pendingPreviewPlaybackDifficultyId_ = 0;
    state_.pendingPreviewPlaybackSecond_ = 0.0;
    state_.activeDifficultyId_ = 0;
    state_.activeOutlineKey_ = "welcome";
    setChartBottomTabsMode(false);
    session_.clearValidationDecorations();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    updateEditorHeader();
    session_.updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    session_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &session_, [this]() { session_.refreshLayoutAfterPageSwitch(); });
    return true;
}

bool miacode::runtime::DocumentSessionHost::switchToDifficultyField(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId) || session_.applicationServices_.workspace().document().difficulty(difficultyId) == nullptr) {
        return false;
    }
    // The user-facing toggle for this was removed in beta59 — behavior is
    // now always "preserve editor position + preview progress when an
    // active difficulty was selected before the switch".
    // Also preserve when coming FROM the latency page OR the export page: both set
    // activeDifficultyId_=0 (so hasActiveDifficulty() is false) but maintain a valid
    // playhead in pauseSecond_ (export audition mirrors the latency
    // sandbox), which we want to carry over to the difficulty. (Both audition flags
    // are still true here — onPageLeft() below tears them down only afterwards.)
    // The metadata (谱面信息) page has no audition either, but leaving a difficulty
    // for it stops playback with keepPosition=true, so pauseSecond_ still
    // holds the last position — carry it back too. Detect the source page from the
    // stack (currentWidget is still the page we're LEAVING — this function switches
    // it to chartPage_ later). Widgets outline writes the destination key before
    // calling us; QML leaveOverlayPage does not, so activeOutlineKey_ can still be
    // "export" or "latency" here and cannot be used as the source. A stale
    // cross-file value is guarded against by loadDocument resetting
    // pauseSecond_ to 0.
    const bool leavingMetadataPage = state_.activeOutlineKey_ == QLatin1String("metadata");
    const bool leavingOverlayField = state_.activeOutlineKey_ == QLatin1String("export")
        || state_.activeOutlineKey_ == QLatin1String("latency");
    const bool restoreSwitchView = session_.hasActiveDifficulty()
        || state_.latencySandboxAuditionActive_
        || state_.exportPreviewAuditionActive_
        || leavingMetadataPage
        || leavingOverlayField;
    const double restorePreviewSecond = restoreSwitchView
        ? qMax(0.0, state_.playing_
              ? session_.currentPreviewAuthoritativeAudioClockSecond()
              : state_.pauseSecond_)
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
    session_.cacheWorkspaceLayoutSizes();
    session_.stopQtPreviewPlayback(true);
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
    if (session_.editor_ != nullptr) {
        session_.editor_->syncBookmarksFromEditorText();
    }
    const double previousPreviewTrackDurationSeconds = state_.previewTrackDurationSeconds_;
    const std::shared_ptr<const miacode::waveform::WaveformData> previousWaveformData =
        state_.timelineQuickStateBridge_ != nullptr ? state_.timelineQuickStateBridge_->waveformData() : nullptr;
    clearTimelineAndPreview();
    if (restoreSwitchView) {
        // Non-command write: clearTimelineAndPreview() just cleared
        // pauseSecond_ implicitly to 0 via the timeline reset above; this
        // overwrites it with the position carried across the switch. Must
        // stay a plain, unconditional write — see PlaybackStateAuthority.h.
        if (auto* authority = session_.applicationServices_.playbackStateAuthority(); authority != nullptr) {
            authority->repositionSilently(restorePreviewSecond, "switch_to_difficulty_field");
        }
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
        if (state_.scene_ != nullptr) {
            state_.scene_->setPlayheadSeconds(restorePreviewSecond, false);
        }
    } else {
        state_.pendingDifficultySwitchPreviewRestore_ = false;
        state_.pendingDifficultySwitchPreviewRestoreRevision_ = 0;
        state_.pendingDifficultySwitchPreviewRestoreDifficultyId_ = 0;
        state_.pendingDifficultySwitchPreviewRestoreSecond_ = 0.0;
    }
    if (previousWaveformData) {
        session_.applyWaveformData(previousWaveformData);
    } else if (previousPreviewTrackDurationSeconds > 0.0) {
        state_.previewTrackDurationSeconds_ = previousPreviewTrackDurationSeconds;
        session_.updatePreviewSliderRange();
    }
    if (!state_.currentFilePath_.isEmpty()) {
        session_.syncPreviewStageMediaRouteChartPath(
            state_.currentFilePath_,
            state_.lastTrackPath_,
            state_.pauseSecond_,
            session_.applicationServices_.workspace().document().videoPath);  // Phase 4c — &video= override
    }
    setChartBottomTabsMode(true);
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->setFollowPreviewEnabled(state_.previewFollowEnabled_);
    }
    // Entering a difficulty re-asserts the correct preview levels. With the latency
    // audition torn down above (onPageLeft), the mode is Normal, so the single
    // mode-aware dispatch entry pushes the user's real mix (see
    // applyPreviewAudioSettingsToRuntime) — not a special-cased override.
    session_.applyPreviewAudioSettingsToRuntime();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    QTimer::singleShot(0, &session_, [this, difficultyId]() {
        if (state_.activeDifficultyId_ != difficultyId || !session_.hasActiveDifficulty()) {
            return;
        }
        session_.restoreBottomTabsCurrentTabAfterRefresh(Session::BottomTabsTabId::Timeline);
        session_.scheduleTimelineRefresh();
    });
    session_.saveProjectRenderState();
    // Chart-switch leak gauge. This is the single funnel for BOTH switch paths —
    // loadDocument() reaches a chart only via activateInitialField() -> here — so
    // one call covers difficulty switches and file opens alike. No-ops outside
    // --debug. See emitChartSwitchResourceGauge() for what the sample means.
    session_.emitChartSwitchResourceGauge();
    session_.refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, &session_, [this]() { session_.refreshLayoutAfterPageSwitch(); });
    return true;
}

void miacode::runtime::DocumentSessionHost::activateInitialField()
{
    const QVector<int> ids = session_.applicationServices_.workspace().document().difficultyIds();
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

void miacode::runtime::DocumentSessionHost::loadDocument()
{
    clearDeletedDifficultyUndoState();
    const miacode::v2::ChartWorkspaceSnapshot snapshot =
        session_.applicationServices_.workspace().snapshot();
    resetAutosaveState(snapshot.sourceText);
    state_.documentDirty_ = snapshot.dirty;
    state_.currentFieldDirty_ = false;
    // Non-command write: a fresh document starts the playhead at 0 — see
    // PlaybackStateAuthority.h.
    if (auto* authority = session_.applicationServices_.playbackStateAuthority(); authority != nullptr) {
        authority->repositionSilently(0.0, "load_document");
    }
    state_.lastExportAuditionDifficultyId_ = 0;
    state_.activeDifficultyId_ = snapshot.activeDifficultyId;
    if (SimaiDocument::isDifficultyId(state_.activeDifficultyId_)) {
        state_.projectLastOpenedDifficultyId_ = state_.activeDifficultyId_;
    }
    session_.loadProjectValidationPreferences();
    updateDirtyState();
    session_.scheduleTimelineRefresh();
    emit session_.documentReplaced();
}

void miacode::runtime::DocumentSessionHost::clearTimelineAndPreview()
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
    session_.clearPreviewFollowDecoration();
    session_.clearPreviewObjectStats();
    session_.clearMuriDiagnostics();
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
    session_.stopQtPreviewPlayback(false);
    if (state_.timelineQuickStateBridge_ != nullptr) {
        state_.timelineQuickStateBridge_->clear();
        state_.timelineQuickStateBridge_->setMuriAnalysisReport(state_.muriAnalysisReport_);
    }
    if (state_.scene_ != nullptr) {
        state_.scene_->reset();
        state_.scene_->setMuriAnalysisReport(state_.muriAnalysisReport_);
    }
    session_.clearPreviewStageMediaRoute();
    session_.updatePreviewSliderRange();
    session_.updatePreviewSliderPosition(0.0);
}
