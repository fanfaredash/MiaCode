#pragma once

#include "runtime/Session.h"

#include "app/v2/PlaybackValidationPort.h"

namespace miacode::runtime {

class ValidationHost final : public miacode::v2::PlaybackValidationPort {
public:
    ValidationHost(Session& session, RuntimeContext::Ui& ui, RuntimeContext::State& state);

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
    const MuriAnalysisReport& alignedMuriAnalysisReportForPreview() const;
    void applyAlignedMuriAnalysisReportToViews() override;
    void updateEditorValidationSummary();
    void clearValidationErrors();
    void clearMuriDiagnostics();
    void clearValidationDecorations() override;
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
    void flushPendingMuriDiagnosticsPanelRefresh() override;
    void clearValidationCache() override;
    void applyDeferredAnalysisUiUpdates() override;
    // Stage 4.9d-4c: lets PlaybackCoordinator's async analysis-apply callback
    // reach documentValidationChanged through the validation port instead of
    // a Session reference — see PlaybackValidationPort.h.
    void notifyDocumentValidationChanged() override;
    // Stage 4.9d-4b-2e: lets PlaybackCoordinator::setCurrentBottomTabsTabId
    // re-relayout the issue lists after a bottom-tab switch without naming
    // QListWidget on the port — see scheduleWrappedListRelayout below.
    void scheduleBottomTabsIssueListRelayout() override;
    void setValidationTabVisible(bool visible);
    void refreshValidationPanelForActiveField();
    void applyMuriRenderOptions();
    void setMuriRenderMode(RenderMode mode, bool persistState = true) override;
    void onToggleJudgeMarkers(bool checked);
    void onToggleTouchTrail(bool checked);
    void onEditStaticTapOnSlideThreshold();
    bool runValidateSimaiSilently(bool focusFirstIssue = false);
    Session::DocumentValidationSnapshot documentValidationSnapshot() const;
    Session::QmlAnalysisSnapshot qmlAnalysisSnapshot() const;

private:
    bool isMuriDiagnosticsTabActive() const;
    void rebuildMuriDiagnosticsPanel();

    Session& session_;
    RuntimeContext::Ui& ui_;
    RuntimeContext::State& state_;
};

}  // namespace miacode::runtime
