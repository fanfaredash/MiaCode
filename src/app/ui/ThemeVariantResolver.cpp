#include "app/ui/ThemeVariantResolver.h"

#include <QCoreApplication>
#include <QGuiApplication>

namespace miacode::ui {

ThemeVariant ThemeVariantResolver::resolve(UiText::ThemePreference preference,
                                           Qt::ColorScheme systemScheme)
{
    switch (preference) {
    case UiText::ThemePreference::Light:
        return ThemeVariant::Light;
    case UiText::ThemePreference::Dark:
        return ThemeVariant::Dark;
    case UiText::ThemePreference::System:
        break;
    }
    return systemScheme == Qt::ColorScheme::Light ? ThemeVariant::Light : ThemeVariant::Dark;
}

ThemeVariant ThemeVariantResolver::resolve(UiText::ThemePreference preference)
{
    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()); app != nullptr
        && app->styleHints() != nullptr) {
        scheme = app->styleHints()->colorScheme();
    }
    return resolve(preference, scheme);
}

} // namespace miacode::ui
