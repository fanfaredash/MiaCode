#pragma once

#include "UiText.h"

namespace UiNativeWindowThemePolicy {

enum class Appearance {
    System,
    Light,
    Dark,
};

constexpr Appearance appearanceFor(UiText::ThemePreference preference)
{
    switch (preference) {
    case UiText::ThemePreference::Light:
        return Appearance::Light;
    case UiText::ThemePreference::Dark:
        return Appearance::Dark;
    case UiText::ThemePreference::System:
    default:
        return Appearance::System;
    }
}

}  // namespace UiNativeWindowThemePolicy
