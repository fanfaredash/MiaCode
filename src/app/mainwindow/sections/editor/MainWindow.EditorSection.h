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
    // Creates a bookmark on `line` of the active difficulty with a default
    // name (line comment → first token, else a line-number fallback). No-op
    // when the line already has a bookmark (it is revealed instead). When
    // beginRenameInSidebar is set the sidebar starts an inline rename so the
    // user can type the final name immediately.
    void createBookmarkAtLine(int line, bool beginRenameInSidebar);
    // Explicit user rename: trims the name, refuses an empty result, sets
    // nameLocked and marks the document dirty. Returns false when nothing
    // changed (missing bookmark / empty name).
    bool renameBookmark(int difficultyId, int line, const QString& name);
    void replaceBookmarkLine(int fromLine, int toLine);
    void refreshEditorBookmarkLines();
    void syncBookmarksFromEditorText(int changePosition = -1, int charsRemoved = 0, int charsAdded = 0);
    void reanchorActiveBookmarksAfterChartTransform();
    void exportBookmarksJson();
    void importBookmarksJson();
    // Adopt bookmarks for the freshly assigned state_.document_: the simai
    // &miacode_bookmarks= payload wins; the legacy project-JSON staging filled
    // by loadProjectRenderState() is the migration fallback. Called from
    // DocumentSection::loadDocument().
    void adoptBookmarksForLoadedDocument();
    // Push the in-memory bookmarks into state_.document_.bookmarks so the next
    // toText() serializes them. Called on the save path (and autosave snapshot).
    void syncBookmarksIntoDocument(SimaiDocument* document) const;
    // Marks the document dirty after a user-initiated bookmark mutation so the
    // change reaches the simai file on the next save.
    void markBookmarksMutatedByUser();
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
