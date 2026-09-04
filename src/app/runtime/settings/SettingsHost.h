#pragma once

#include "runtime/Session.h"

#include "app/v2/PreferencesStore.h"

namespace miacode::runtime {

class SettingsHost final : public miacode::v2::PreferencesStore {
public:
    SettingsHost(Session& session, RuntimeContext::Ui& ui, RuntimeContext::State& state);

    void applyConfiguredShortcuts();

    int editorTextFontSize() const override;
    double editorLineSpacingFactor() const override;
    bool editorHalfWidthInputEnabled() const override;
    bool editorAutoCompletionEnabled() const override;
    bool editorImeInputDisabled() const override;
    void applyEditorTextFontSize(int pointSize, bool persist) override;
    void applyEditorLineSpacingFactor(double factor, bool persist) override;
    void applyEditorHalfWidthInputEnabled(bool enabled, bool persist) override;
    void applyEditorAutoCompletionEnabled(bool enabled, bool persist) override;
    void applyEditorImeInputDisabled(bool disabled, bool persist) override;
    PreviewCanvasFrameRateMode previewCanvasFrameRateMode() const override;
    PreviewCanvasFrameRateMode previewStageMediaFrameRateMode() const override;
    PreviewCanvasFrameRateMode timelineFrameRateMode() const override;
    double previewCanvasRefreshRate() const override;
    void setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persist) override;
    void setPreviewStageMediaFrameRateMode(PreviewCanvasFrameRateMode mode, bool persist) override;
    void setTimelineFrameRateMode(PreviewCanvasFrameRateMode mode, bool persist) override;
    bool videoDecodePrefersSoftware() const override;
    void setVideoDecodePrefersSoftware(bool preferSoftware, bool persist) override;
    bool workspacePanelsSwapped() const override;
    void setWorkspacePanelsSwapped(bool swapped, bool persist) override;

private:
    Session& session_;
    RuntimeContext::Ui& ui_;
    RuntimeContext::State& state_;
};

}  // namespace miacode::runtime
