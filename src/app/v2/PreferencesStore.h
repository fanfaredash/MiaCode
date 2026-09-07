#pragma once

#include "core/video/PreviewRenderSettings.h"

namespace miacode::v2 {

// 偏好设置's durable values.
//
// Stage 3.5 item 2. These are not plain settings: every setter also applies the
// change — editor font size and line spacing re-lay out the text view, the
// frame-rate modes re-pace the live surfaces, and swapping the workspace panels
// rearranges the layout. That is why they stay behind an interface the window
// implements rather than becoming a value object.
//
// Every setter takes `persist` because the same entry points serve both a user
// edit (persist) and a restore from disk (do not). Dropping the flag would make
// loading settings rewrite them.
//
// Deliberately Qt-GUI-free.
class PreferencesStore
{
public:
    virtual ~PreferencesStore() = default;

    // ---- editor ----
    virtual int editorTextFontSize() const = 0;
    virtual double editorLineSpacingFactor() const = 0;
    virtual bool editorHalfWidthInputEnabled() const = 0;
    virtual bool editorAutoCompletionEnabled() const = 0;
    virtual bool editorImeInputDisabled() const = 0;

    virtual void applyEditorTextFontSize(int pointSize, bool persist) = 0;
    virtual void applyEditorLineSpacingFactor(double factor, bool persist) = 0;
    virtual void applyEditorHalfWidthInputEnabled(bool enabled, bool persist) = 0;
    virtual void applyEditorAutoCompletionEnabled(bool enabled, bool persist) = 0;
    virtual void applyEditorImeInputDisabled(bool disabled, bool persist) = 0;

    // ---- frame pacing ----
    virtual PreviewCanvasFrameRateMode previewCanvasFrameRateMode() const = 0;
    virtual PreviewCanvasFrameRateMode previewStageMediaFrameRateMode() const = 0;
    virtual PreviewCanvasFrameRateMode timelineFrameRateMode() const = 0;
    // The screen's actual refresh rate, which is what decides whether the
    // 120 FPS option is offered at all.
    virtual double previewCanvasRefreshRate() const = 0;

    virtual void setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persist) = 0;
    virtual void setPreviewStageMediaFrameRateMode(PreviewCanvasFrameRateMode mode,
                                                   bool persist) = 0;
    virtual void setTimelineFrameRateMode(PreviewCanvasFrameRateMode mode, bool persist) = 0;

    // ---- decode + layout ----
    virtual bool videoDecodePrefersSoftware() const = 0;
    virtual void setVideoDecodePrefersSoftware(bool preferSoftware, bool persist) = 0;
    virtual bool workspacePanelsSwapped() const = 0;
    virtual void setWorkspacePanelsSwapped(bool swapped, bool persist) = 0;

protected:
    PreferencesStore() = default;
    PreferencesStore(const PreferencesStore&) = default;
    PreferencesStore& operator=(const PreferencesStore&) = default;
};

}  // namespace miacode::v2
