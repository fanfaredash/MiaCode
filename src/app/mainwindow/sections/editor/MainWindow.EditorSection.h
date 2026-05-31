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
    void applyEditorAutoCloseBracketsEnabled(bool enabled, bool persistPreference);
    void applyEditorAutoInsertSquareAfterHEnabled(bool enabled, bool persistPreference);
    void applyEditorBracketCompletionEnabled(bool enabled, bool persistPreference);
    void applyEditorImeInputDisabled(bool disabled, bool persistPreference);
    void showCreateBookmarkDialog();
    void showBookmarkManager();
    void openBookmarkAtLine(int line);
    void addBookmark(int line, const QString& title, const QString& text);
    void replaceBookmarkLine(int fromLine, int toLine);
    void refreshEditorBookmarkLines();
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
