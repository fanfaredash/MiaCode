#pragma once

#include <functional>

class QWidget;

namespace miacode::video_export {

// Modal HUD-font settings dialog (current-font readout + font-library combo +
// import / reset), shared by the video-export dialog's "Font" tab and the main
// window's 视频设置 dialog. The font library lives next to the portable
// preferences file (<preferences dir>/fonts); the selection applies
// process-wide via miacode::preview::scene::setPreviewHudCustomFontPath and is
// persisted by that setter. `onFontChanged` (optional) fires after EVERY font
// change (pick / import / reset) so the host can refresh a live preview that
// is currently showing the HUD — pass {} when the next natural repaint is
// soon enough.
void openHudFontSettingsDialog(QWidget* parent,
                               const std::function<void()>& onFontChanged = {});

}  // namespace miacode::video_export
