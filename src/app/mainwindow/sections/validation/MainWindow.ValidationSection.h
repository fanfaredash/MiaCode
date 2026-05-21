#pragma once

#include "../../MainWindow.h"

class MainWindow::ValidationSection {
public:
    ValidationSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    QListWidgetItem* addWrappedListEntry(
        QListWidget* list,
        const QString& html,
        const QString& plainText,
        int line = 1,
        int col = 1,
        double second = -1.0,
        bool enabled = true
    );
    void relayoutWrappedListRows(QListWidget* list);
    void scheduleWrappedListRelayout(QListWidget* list);
    QString currentValidationIgnoreScopeKey() const;
    bool isIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey) const;
    void setIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey, bool ignored);
    void loadProjectValidationPreferences();
    void saveProjectValidationPreferences(const QString& chartFilePath = QString()) const;
    void applyIgnoreMuriIssuePrompts(bool enabled, bool persistPreference);
    void refreshEditorExtraSelections();
    const MuriAnalysisReport& alignedMuriAnalysisReportForPreview() const;
    void applyAlignedMuriAnalysisReportToViews();
    void updateEditorValidationSummary();
    void setPreviewFollowDecoration(
        int startLine,
        int startCol,
        int endLine = -1,
        int endCol = -1,
        int cursorLine = -1,
        int cursorCol = -1,
        bool ensureVisible = false);
    void clearPreviewFollowDecoration();
    void clearValidationErrors();
    void clearMuriDiagnostics();
    void clearValidationDecorations();
    void addValidationError(
        int line,
        int col,
        const QString& rawMessage,
        const QString& displayMessage,
        const QString& issueTypeKey = QString(),
        const QString& issueTypeLabel = QString(),
        bool ignoredInHeader = false
    );
    void addValidationDecoration(int line, int col, const QString& message, int endCol = -1);
    void jumpToLocation(int line, int col);
    void onErrorItemActivated(QListWidgetItem* item);
    void onMuriItemActivated(QListWidgetItem* item);
    void showIssueListContextMenu(QListWidget* list, const QPoint& pos, bool muriList);
    void refreshMuriDiagnosticsPanel();
    void flushPendingMuriDiagnosticsPanelRefresh();
    void clearValidationCache();
    void applyDeferredAnalysisUiUpdates();
    void setValidationTabVisible(bool visible);
    void refreshValidationPanelForActiveField();
    void applyMuriRenderOptions();
    void setMuriRenderMode(RenderMode mode, bool persistState = true);
    void onToggleJudgeMarkers(bool checked);
    void onToggleTouchTrail(bool checked);
    void onEditStaticTapOnSlideThreshold();
    bool runValidateSimaiSilently(bool focusFirstIssue = false);

private:
    bool isMuriDiagnosticsTabActive() const;
    void rebuildMuriDiagnosticsPanel();
    void refreshEditorExtraSelectionsForReason(const QString& reason);
    void rebuildValidationExtraSelectionsCache(const QString& reason);
    void applyEditorExtraSelectionsForReason(const QString& reason);
    QByteArray buildValidationExtraSelectionsSignature() const;

    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
