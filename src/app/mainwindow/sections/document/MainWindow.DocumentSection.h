#pragma once

#include "../../MainWindow.h"

class QTextCursor;

class MainWindow::DocumentSection {
public:
    DocumentSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    bool maybeSaveBeforeContinue();
    bool maybeSaveCurrentFieldChanges();
    bool applyCurrentFieldToDocument();
    void onNewFile();
    void onOpenFile();
    bool openFileAtPath(const QString& path, bool showStatusMessage = true, bool showErrors = true);
    void refreshRestoreBackupMenu(QMenu* restoreBackupMenu);
    void restoreBackupFilePath(const QString& path);
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
    void onRaiseSubdivisionSelection();
    void onLowerSubdivisionSelection();
    void updateEditorHeader();
    void updateDifficultyScopedActionStates();
    void updateEditorHeaderLayoutMode();
    void syncEditorHeaderMinimumWidth();
    void updateEditorStatus();
    void updateEditorEmptyState();
    void updateMetadataPageMode();
    bool deleteDifficultyField(int difficultyId);
    void updateDifficultyDeleteButton(bool visible);
    void rebuildFieldSidebar();
    void populateMetadataPage();
    void populateDifficultyPage(int difficultyId);
    bool switchToMetadataField();
    bool switchToWelcomePage();
    bool switchToDifficultyField(int difficultyId);
    void activateInitialField();
    void loadDocument(const SimaiDocument& document);
    void clearTimelineAndPreview();
    void applyDifficultySwitchEditorScrollRestore(int verticalScrollValue, int horizontalScrollValue);
    void rebuildAutosaveMetadata(const QString& autosaveDirectoryPath) const;

private:
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
