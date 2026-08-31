#pragma once

#include "app/ui/UiText.h"

#include <QStyleHints>

namespace miacode::ui {

enum class ThemeVariant {
    Light,
    Dark,
};

class ThemeVariantResolver final
{
public:
    static ThemeVariant resolve(UiText::ThemePreference preference,
                                Qt::ColorScheme systemScheme);
    static ThemeVariant resolve(UiText::ThemePreference preference);
};

} // namespace miacode::ui
