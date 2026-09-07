#pragma once

#include "runtime/Session.h"

#include "app/v2/PlaybackValidationPort.h"

namespace miacode::runtime {

class ValidationHost final : public miacode::v2::PlaybackValidationPort {
public:
    ValidationHost(Session& session, RuntimeContext::Ui& ui, RuntimeContext::State& state);

    void loadProjectValidationPreferences();
    void saveProjectValidationPreferences(const QString& chartFilePath = QString()) const;
    void applyIgnoreMuriIssuePrompts(bool enabled, bool persistPreference);
    const MuriAnalysisReport& alignedMuriAnalysisReportForPreview() const;
    void applyAlignedMuriAnalysisReportToViews() override;
    void clearValidationDecorations() override;
    void addValidationDecoration(int line, int col, const QString& message, int endCol = -1);
    void clearValidationCache() override;
    void applyDeferredAnalysisUiUpdates() override;
    // Stage 4.9d-4c: lets PlaybackCoordinator's async analysis-apply callback
    // reach documentValidationChanged through the validation port instead of
    // a Session reference — see PlaybackValidationPort.h.
    void notifyDocumentValidationChanged() override;
    void refreshValidationPanelForActiveField();
    void applyMuriRenderOptions();
    void setMuriRenderMode(RenderMode mode, bool persistState = true) override;
    void onToggleJudgeMarkers(bool checked);
    void onToggleTouchTrail(bool checked);
    bool runValidateSimaiSilently();
    Session::DocumentValidationSnapshot documentValidationSnapshot() const;
    Session::QmlAnalysisSnapshot qmlAnalysisSnapshot() const;

private:
    Session& session_;
    RuntimeContext::Ui& ui_;
    RuntimeContext::State& state_;
};

}  // namespace miacode::runtime
