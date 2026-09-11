#pragma once

#include "app/ui/preferences/PreferenceDocument.h"

#include <QStyleHints>

namespace miacode::ui {

enum class ThemeVariant {
    Light,
    Dark,
};

class ThemeVariantResolver final
{
public:
    static ThemeVariant resolve(PreferenceDocument::ThemePreference preference,
                                Qt::ColorScheme systemScheme);
    static ThemeVariant resolve(PreferenceDocument::ThemePreference preference);
};

} // namespace miacode::ui
