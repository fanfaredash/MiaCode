#pragma once

#include "../../MainWindow.h"

class MainWindow::EditorSection {
public:
    EditorSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void loadPortableState();
    void resetPortablePreviewSettingsToDefaults();
    void applyPortablePreviewSettings(const QJsonObject& preview);
    void savePortableState() const;
    void persistEditorTextFontPreference() const;
    void applyEditorTextFontSize(int pointSize, bool persistPreference);
    void applyEditorLineSpacingFactor(double factor, bool persistPreference);
    void applyEditorHalfWidthInputEnabled(bool enabled, bool persistPreference);
    void applyEditorOverwriteModeEnabled(bool enabled, bool persistPreference);
    void applyEditorAutoCompletionEnabled(bool enabled, bool persistPreference);
    void applyEditorImeInputDisabled(bool disabled, bool persistPreference);
    void applyEditorHeaderTopDisplay(EditorHeaderTopDisplay mode, bool persistPreference);
    // Jump to the bookmark's line and highlight it in the sidebar (no dialog).
    void activateBookmarkAtLine(int line);
    void deleteBookmarkAtLineWithConfirmation(int line);
    // Creates a bookmark by inserting a visible `|| [label]` comment on `line`.
    // No-op when the line already has a bookmark (it is revealed instead).
    // When beginRenameInSidebar is set, the sidebar starts inline rename.
    void createBookmarkAtLine(int line, bool beginRenameInSidebar);
    // Explicit user rename: rewrites the line's `|| [label]` prefix in the
    // editor text. An empty name removes an existing explicit label and falls
    // back to automatic naming.
    bool renameBookmark(int difficultyId, int line, const QString& name);
    void replaceBookmarkLine(int fromLine, int toLine);
    void refreshEditorBookmarkLines();
    void syncBookmarksFromEditorText(int changePosition = -1, int charsRemoved = 0, int charsAdded = 0);
    // Rebuilds the derived sidebar index after a document is assigned.
    void adoptBookmarksForLoadedDocument();
    void setFullCopyAreaVisible(bool visible);
    void syncCopyAreaEditorAppearance();
    void syncCopyAreaLineCount();
    QString resolveProjectRenderStateFilePath() const;
    void loadProjectRenderState();
    void saveProjectRenderState() const;
    void removeProjectRenderState() const;

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
