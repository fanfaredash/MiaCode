#pragma once

#include "tools/video_export/FontLibrary.h"

#include <functional>

#include <QString>

class QWidget;

namespace miacode::video_export {

// Reusable difficulty-card font selector for BOTH the cover studio and the video
// export "片头" tab. Two dropdowns — 标题字体 (display) / 正文字体 (body) — over the
// shared portable font library (<preferences>/fonts), plus Import / Reset.
//
// By product request there is NO live sample box: the card itself IS the live
// preview in every host, so a separate swatch would only add clutter. An empty
// path means "use the bundled default font".
//
// The host owns persistence + preview refresh: `onChanged` fires after every
// pick / import / reset, and the host reads the current selection back through
// `displayPath()` / `bodyPath()`. Those accessors (and `setSelection`) touch the
// live combos, so call them only while `widget` is alive — during the cover
// studio's closeEvent-time save, never from a destructor (the widget is
// reparented into the inspector column; see CoverStudioPanel §8 lifecycle note).
struct CardFontSelector {
    QWidget* widget = nullptr;
    std::function<QString()> displayPath;   // "" = default
    std::function<QString()> bodyPath;      // "" = default
    // Set both combos (signals suppressed → does NOT invoke onChanged). Used to
    // restore a persisted selection after construction.
    std::function<void(const QString& displayPath, const QString& bodyPath)> setSelection;
};

CardFontSelector createCardFontSelector(QWidget* parent,
                                        const std::function<void()>& onChanged,
                                        FontComboWidthMode widthMode = FontComboWidthMode::StandardForm);

}  // namespace miacode::video_export
