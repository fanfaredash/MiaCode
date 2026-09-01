#pragma once

#include "runtime/Session.h"

namespace miacode::runtime {

class EditorHost {
public:
    EditorHost(Session& session, Session::HostUi& ui, Session::HostState& state);

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
    // Jump to the bookmark's line without leaving a persistent sidebar marker.
    // Creates a bookmark by inserting a visible `|| [label]` comment on `line`.
    // No-op when the line already has a bookmark (it is revealed instead).
    // When beginRenameInSidebar is set, the sidebar starts inline rename.
    // Explicit user rename: rewrites the line's `|| [label]` prefix in the
    // editor text. An empty name removes an existing explicit label and falls
    // back to automatic naming.
    void syncBookmarksFromEditorText(int changePosition = -1, int charsRemoved = 0, int charsAdded = 0);
    // Rebuilds the derived sidebar index after a document is assigned.
    void adoptBookmarksForLoadedDocument();
    QString resolveProjectRenderStateFilePath() const;
    void loadProjectRenderState();
    void saveProjectRenderState() const;
    void removeProjectRenderState() const;

private:
    Session& session_;
    Session::HostUi& ui_;
    Session::HostState& state_;
    // The preview appearance settings are owned by the application assembly,
    // not by the window; this is the same single copy Session binds to.
    miacode::v2::PreviewAppearanceState::Values& previewAppearanceValues_;
};

}  // namespace miacode::runtime
