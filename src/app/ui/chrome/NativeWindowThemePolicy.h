#pragma once

#include "app/ui/preferences/PreferenceDocument.h"

namespace NativeWindowThemePolicy {

enum class Appearance {
    System,
    Light,
    Dark,
};

constexpr Appearance appearanceFor(PreferenceDocument::ThemePreference preference)
{
    switch (preference) {
    case PreferenceDocument::ThemePreference::Light:
        return Appearance::Light;
    case PreferenceDocument::ThemePreference::Dark:
        return Appearance::Dark;
    case PreferenceDocument::ThemePreference::System:
    default:
        return Appearance::System;
    }
}

}  // namespace NativeWindowThemePolicy
