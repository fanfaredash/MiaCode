#pragma once

#include "../../MainWindow.h"

class QTextCursor;

class MainWindow::DocumentSection {
public:
    DocumentSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    bool maybeSaveBeforeContinue();
    bool maybeSaveCurrentFieldChanges();
    bool applyCurrentFieldToDocument();
    // Opens the modal "manage per-difficulty designers" dialog: seven rows for
    // &des_1..7 plus the "all difficulties share one designer" toggle. Commits
    // on OK (chart-less names become standalone &des_N — no phantom
    // difficulty). See implementation in DocumentFlow.cpp.
    void openPerDifficultyDesignerDialog();
    // Apply the saved-or-inferred unified-designer preference to the
    // runtime flag when a chart is opened. Reads
    // <chartDir>/.miacode/preferences.json; falls back to
    // SimaiDocument::inferUnifiedDesignerDefault() when the key is absent.
    void refreshUnifiedDesignerStateForLoadedDocument();
    void onNewFile();
    void onOpenFile();
    bool openFileAtPath(const QString& path, bool showStatusMessage = true, bool showErrors = true);
    void refreshRestoreBackupMenu(QMenu* restoreBackupMenu);
    void restoreBackupFilePath(const QString& path, bool mentionAbnormalExit = false);
    bool restoreLastSessionFile();
    void scheduleStartupRestoreLastSessionFile();
    void cancelPendingStartupRestore();
    void applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared);
    void applyOpenedDocumentState(
        const QString& normalizedPath,
        TextEncoding encodingUsed,
        const SimaiDocument& document,
        bool showStatusMessage,
        double knownTrackDurationSeconds = -1.0
    );
    void resetAutosaveState(const QString& referenceText);
    // Drop the in-memory crash-recovery snapshot AND delete the
    // on-disk recovery file for the current chart. Called from the
    // close-event path so a clean exit doesn't leave a stale recovery
    // file that would prompt "recover unsaved changes?" on next open.
    void cleanupCrashRecoveryForCleanExit();
    QString resolveAutosaveDirectoryPath() const;
    QString currentDocumentTextForAutosave() const;
    void pruneAutosaveFiles(const QString& autosaveDirectoryPath) const;
    void runAutosaveCheck(bool allowHistory = true);
    bool onSaveFile();
    bool onSaveFileAs();
    bool saveToPath(const QString& path);
    bool applyBatchTransform(const QString& opName, const BatchTransform& transform);
    bool applySelectionBatchTransform(const QString& opName, const BatchTransform& transform);
    bool applySelectionBatchTransform(const QString& opName, const SelectionContextBatchTransform& transform);
    std::pair<int, int> currentCursorLineCol() const;
    std::pair<int, int> currentSelectionOrCursorLineCol() const;
    bool currentSelectionRange(int* startPos, int* endPos) const;
    void setMetadataExtraText(const QString& text);
    void setEditorText(const QString& text);
    void updatePauseButtonAppearance();
    void updateDirtyState();
    bool currentFieldHasUndoChanges() const;
    void anchorCurrentFieldCleanState();
    void refreshCurrentFieldDirtyState();
    void markCurrentFieldDirty();
    void clearDeletedDifficultyUndoState();
    bool undoDeletedDifficultyField();
    void clearChartSelectionTransformUndoEntries();
    void syncChartSelectionTransformUndoState();
    bool undoChartEditorWithSelectionRestore();
    bool redoChartEditorWithSelectionRestore();
    QString resolveInitialOpenDirectory() const;
    void setLastOpenDirectory(const QString& pathOrDir);
    QString transformChartText(const QString& input, ChartTransformOp op, int* changedCount = nullptr) const;
    void onMirrorLeftRight();
    void onMirrorUpDown();
    void onRotate180();
    void onRotate45CounterClockwise();
    void onRotate45Clockwise();
    void onNormalizeWholeChart();
    void onToggleBreakSelection();
    void onToggleExSelection();
    void onToggleFireworkSelection();
    void onRandomRotateSelection();
    void onClearCompleteElementsSelection();
    void onRaiseSubdivisionSelection();
    void onLowerSubdivisionSelection();
    void onRaiseSubdivisionHalfStepSelection();
    void onLowerSubdivisionHalfStepSelection();
    void updateEditorHeader();
    void updateDifficultyScopedActionStates();
    void updateEditorHeaderLayoutMode();
    void syncEditorHeaderMinimumWidth();
    void updateEditorStatus();
    void updateEditorEmptyState();
    void updateMetadataPageMode();
    bool deleteDifficultyField(int difficultyId);
    void rebuildFieldSidebar();
    // Sidebar bookmark-group fold state. Only explicit toggles are recorded;
    // an untouched difficulty defaults to expanded when active, collapsed
    // otherwise (see the bookmark redesign spec).
    bool isBookmarkGroupExpanded(int difficultyId) const;
    void setBookmarkGroupExpanded(int difficultyId, bool expanded);
    // Expands the difficulty's bookmark group, rebuilds the sidebar, selects
    // and centers the bookmark row; beginRename additionally starts the
    // inline name editor. Safe no-op when the bookmark does not exist.
    void revealBookmarkInSidebar(int difficultyId, int line, bool beginRename);
    // The bookmark list item for (difficultyId, line), or nullptr.
    QListWidgetItem* findBookmarkSidebarItem(int difficultyId, int line) const;
    void populateMetadataPage();
    void populateDifficultyPage(int difficultyId);
    void syncHeaderDesignerEditFromModel();
    bool switchToMetadataField();
    bool switchToWelcomePage();
    bool switchToDifficultyField(int difficultyId);
    bool switchToLatencyField();
    bool switchToExportField();
    // Floats the busy spinner over the "Export" sidebar row / hides it. Shown
    // while the (slow) export-page build runs after switchToExportField().
    // Positioning is separate so sidebar rebuilds can re-anchor an active
    // spinner after rows move.
    bool positionOutlineExportBusySpinner();
    void showOutlineExportBusySpinner();
    void hideOutlineExportBusySpinner();
    void activateInitialField();
    void loadDocument(const SimaiDocument& document);
    void clearTimelineAndPreview();
    void applyDifficultySwitchEditorScrollRestore(int verticalScrollValue, int horizontalScrollValue);
    void rebuildAutosaveMetadata(const QString& autosaveDirectoryPath) const;

private:
    void schedulePendingAbnormalExitBackupRestore();
    void runPendingAbnormalExitBackupRestore();
    // Broadcast the chosen designer name into the top &des, every
    // per-difficulty designer (charted and standalone), and the metadata-page
    // designer line edit. Used by the load-time reconcile.
    void applyUnifiedDesignerName(const QString& canonicalName);
    // Modal picker shown when the user enables the option but no single
    // canonical name exists yet. Lists every distinct non-empty designer
    // found in the document plus a "Clear all" option, and writes the
    // user's selection (or an empty string for "Clear all") to *out.
    // Returns false if the user cancels — callers should revert the
    // checkbox to OFF in that case.
    bool promptCanonicalDesignerName(const QStringList& candidates, QString* out);
    // The heavy body of switchToExportField(), run one event-loop tick later so
    // the busy spinner can paint before the build blocks the UI thread.
    void performSwitchToExportField();
    void setChartBottomTabsMode(bool enabled);
    void pruneChartSelectionTransformUndoEntriesFromStep(int undoStepThreshold);
    void updateLastObservedChartEditorUndoRedoSteps();
    void recordChartSelectionTransformUndoEntry(int originalAnchor, int originalPosition, const QTextCursor& transformedCursor);
    const SelectionTransformUndoEntry* findChartSelectionTransformUndoEntry(int undoStepAfterApply) const;
    bool restoreChartSelectionTransformCursor(const SelectionTransformUndoEntry& entry, bool transformedSelection);

    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
