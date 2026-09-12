#include "theme/UiTheme.h"

#include "theme/ThemeVariantResolver.h"

// UiTheme 只保留 QML 与原生窗口仍共享的明暗配色源。

namespace {

QString css(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

const UiTheme::Colors& lightColors()
{
    static const UiTheme::Colors theme{
        false,                              // dark
        QColor("#F4F6F8"),                  // windowBg
        QColor("#EBEFF3"),                  // windowAltBg
        QColor("#D9E7F8"),                  // toolbarBg
        QColor("#526F98"),                  // statusBg
        QColor("#EBEFF3"),                  // panelBg
        QColor("#FFFFFF"),                  // cardBg
        QColor("#D9E6F5"),                  // cardAltBg
        QColor("#FFFFFF"),                  // inputBg
        QColor("#E5EAF0"),                  // inputDisabledBg
        QColor("#000000"),                  // canvasBg
        QColor("#3D4856"),                  // textPrimary
        QColor("#5D6B7C"),                  // textSecondary
        QColor("#9AA6B4"),                  // textMuted
        QColor("#FFFFFF"),                  // textInverse
        QColor("#C8D2DE"),                  // border
        QColor("#C8D1C2"),                  // borderSoft
        QColor("#AAB8C8"),                  // borderStrong
        QColor("#526F98"),                  // accent
        QColor("#627FA8"),                  // accentHover
        QColor("#405B80"),                  // accentPressed
        QColor("#FFFFFF"),                  // accentText
        QColor("#D3DBCD"),                  // selection
        QColor("#2F3B4A"),                  // selectionText
        QColor("#F4F6F8"),                  // menuBg
        QColor("#C8D2DE"),                  // menuBorder
        QColor("#E5E9E1"),                  // menuHoverBg
        QColor("#9AA6B4"),                  // menuDisabledText
        QColor("#EBEFF3"),                  // scrollTrack
        QColor("#A3AFBD"),                  // scrollHandle
        QColor("#7E8DA0"),                  // scrollHandleHover
        QColor("#2F3B4A"),                  // iconPrimary
        QColor("#5D6B7C"),                  // iconSecondary
    };
    return theme;
}

const UiTheme::Colors& darkColors()
{
    static const UiTheme::Colors theme{
        true,                               // dark
        QColor("#151A20"),                  // windowBg
        QColor("#1B2129"),                  // windowAltBg
        QColor("#171C23"),                  // toolbarBg
        QColor("#171C23"),                  // statusBg
        QColor("#1B2129"),                  // panelBg
        QColor("#232B35"),                  // cardBg
        QColor("#1F2630"),                  // cardAltBg
        QColor("#171D24"),                  // inputBg
        QColor("#202833"),                  // inputDisabledBg
        QColor("#050607"),                  // canvasBg
        QColor("#E6EEF8"),                  // textPrimary
        QColor("#A9B6C6"),                  // textSecondary
        QColor("#7B8798"),                  // textMuted
        QColor("#0F141A"),                  // textInverse
        QColor("#384656"),                  // border
        QColor("#455466"),                  // borderSoft
        QColor("#546679"),                  // borderStrong
        QColor("#4F8FEC"),                  // accent
        QColor("#67A1F1"),                  // accentHover
        QColor("#3E79D0"),                  // accentPressed
        QColor("#F7FBFF"),                  // accentText
        QColor("#315D9E"),                  // selection
        QColor("#F7FBFF"),                  // selectionText
        QColor("#1F2630"),                  // menuBg
        QColor("#455466"),                  // menuBorder
        QColor("#2C3846"),                  // menuHoverBg
        QColor("#748091"),                  // menuDisabledText
        QColor("#1C232C"),                  // scrollTrack
        QColor("#5A6A7B"),                  // scrollHandle
        QColor("#70849A"),                  // scrollHandleHover
        QColor("#D8E2EE"),                  // iconPrimary
        QColor("#95A4B7"),                  // iconSecondary
    };
    return theme;
}

const UiTheme::Colors& legacyLightColors()
{
    static const UiTheme::Colors theme{
        false,                              // dark
        QColor("#F8FAFD"),                  // windowBg
        QColor("#F5F7FA"),                  // windowAltBg
        QColor("#F7F9FC"),                  // toolbarBg
        QColor("#F7F9FC"),                  // statusBg
        QColor("#F5F7FA"),                  // panelBg
        QColor("#FFFFFF"),                  // cardBg
        QColor("#EDF2F8"),                  // cardAltBg
        QColor("#FFFFFF"),                  // inputBg
        QColor("#F2F5F9"),                  // inputDisabledBg
        QColor("#000000"),                  // canvasBg
        QColor("#203040"),                  // textPrimary
        QColor("#5F6B7A"),                  // textSecondary
        QColor("#7A8796"),                  // textMuted
        QColor("#FFFFFF"),                  // textInverse
        QColor("#D5E0EC"),                  // border
        QColor("#CCD6E2"),                  // borderSoft
        QColor("#B8C7DA"),                  // borderStrong
        QColor("#2E77D0"),                  // accent
        QColor("#3A86E8"),                  // accentHover
        QColor("#2668B9"),                  // accentPressed
        QColor("#FFFFFF"),                  // accentText
        QColor("#B8CCE5"),                  // selection
        QColor("#1F1F1F"),                  // selectionText
        QColor("#FFFFFF"),                  // menuBg
        QColor("#D7E0EB"),                  // menuBorder
        QColor("#EEF5FF"),                  // menuHoverBg
        QColor("#9AA5B4"),                  // menuDisabledText
        QColor("#F4F7FB"),                  // scrollTrack
        QColor("#9CB5CE"),                  // scrollHandle
        QColor("#81A2C3"),                  // scrollHandleHover
        QColor("#2B3C4E"),                  // iconPrimary
        QColor("#5D6E83"),                  // iconSecondary
    };
    return theme;
}

const UiTheme::Colors& legacyDarkColors()
{
    static const UiTheme::Colors theme{
        true,                               // dark
        QColor("#151A20"),                  // windowBg
        QColor("#1B2129"),                  // windowAltBg
        QColor("#171C23"),                  // toolbarBg
        QColor("#171C23"),                  // statusBg
        QColor("#1B2129"),                  // panelBg
        QColor("#232B35"),                  // cardBg
        QColor("#1F2630"),                  // cardAltBg
        QColor("#171D24"),                  // inputBg
        QColor("#202833"),                  // inputDisabledBg
        QColor("#050607"),                  // canvasBg
        QColor("#E6EEF8"),                  // textPrimary
        QColor("#A9B6C6"),                  // textSecondary
        QColor("#7B8798"),                  // textMuted
        QColor("#0F141A"),                  // textInverse
        QColor("#384656"),                  // border
        QColor("#455466"),                  // borderSoft
        QColor("#546679"),                  // borderStrong
        QColor("#4F8FEC"),                  // accent
        QColor("#67A1F1"),                  // accentHover
        QColor("#3E79D0"),                  // accentPressed
        QColor("#F7FBFF"),                  // accentText
        QColor("#315D9E"),                  // selection
        QColor("#F7FBFF"),                  // selectionText
        QColor("#1F2630"),                  // menuBg
        QColor("#455466"),                  // menuBorder
        QColor("#2C3846"),                  // menuHoverBg
        QColor("#748091"),                  // menuDisabledText
        QColor("#1C232C"),                  // scrollTrack
        QColor("#5A6A7B"),                  // scrollHandle
        QColor("#70849A"),                  // scrollHandleHover
        QColor("#D8E2EE"),                  // iconPrimary
        QColor("#95A4B7"),                  // iconSecondary
    };
    return theme;
}

}  // namespace

namespace UiTheme {

namespace {

PreferenceDocument::ThemePalette activeThemePalette()
{
    if (PreferenceDocument::preferredTheme() == PreferenceDocument::ThemePreference::Legacy) {
        return PreferenceDocument::ThemePalette::Legacy;
    }
    const bool darkAppearance = miacode::ui::ThemeVariantResolver::resolve(
        PreferenceDocument::preferredTheme()) == miacode::ui::ThemeVariant::Dark;
    return darkAppearance
        ? PreferenceDocument::preferredDarkTheme()
        : PreferenceDocument::preferredLightTheme();
}

}  // namespace

ResolvedTheme resolvedTheme()
{
    return PreferenceDocument::themePaletteIsDark(activeThemePalette())
        ? ResolvedTheme::Dark
        : ResolvedTheme::Light;
}

bool isDarkTheme()
{
    return resolvedTheme() == ResolvedTheme::Dark;
}

const Colors& colors()
{
    switch (activeThemePalette()) {
    case PreferenceDocument::ThemePalette::Light:
        return lightColors();
    case PreferenceDocument::ThemePalette::LegacyLight:
        return legacyLightColors();
    case PreferenceDocument::ThemePalette::Legacy:
        return legacyDarkColors();
    case PreferenceDocument::ThemePalette::Dark:
    default:
        return darkColors();
    }
}

}  // namespace UiTheme
