#pragma once

#include <functional>

class QWidget;

namespace miacode::video_export {

// Builds the HUD-font picker as an EMBEDDABLE widget (current-font readout +
// font-library combo + live sample + import / reset), for hosting inline in the
// 皮肤 popup / export 皮肤 tab. The font library lives next to the portable
// preferences file (<preferences dir>/fonts); the selection applies
// process-wide via miacode::preview::scene::setPreviewHudCustomFontPath and is
// persisted by that setter. `onFontChanged` (optional) fires after EVERY font
// change (pick / import / reset) so the host can refresh a live preview that
// is currently showing the HUD — pass {} when the next natural repaint is
// soon enough. The returned widget is parented to `parent`.
QWidget* createHudFontSettingsWidget(QWidget* parent,
                                     const std::function<void()>& onFontChanged = {});

}  // namespace miacode::video_export
