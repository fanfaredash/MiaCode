#pragma once

#include "../../MainWindow.h"

class MainWindow::DocumentSection {
public:
    DocumentSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    bool maybeSaveBeforeContinue();
    bool maybeSaveCurrentFieldChanges();
    bool applyCurrentFieldToDocument();
    void onNewFile();
    void onOpenFile();
    bool openFileAtPath(const QString& path, bool showStatusMessage = true, bool showErrors = true);
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
    QString resolveAutosaveDirectoryPath() const;
    QString currentDocumentTextForAutosave() const;
    void pruneAutosaveFiles(const QString& autosaveDirectoryPath) const;
    void runAutosaveCheck();
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
    void refreshCurrentFieldDirtyState();
    void markCurrentFieldDirty();
    void clearDeletedDifficultyUndoState();
    bool undoDeletedDifficultyField();
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

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
