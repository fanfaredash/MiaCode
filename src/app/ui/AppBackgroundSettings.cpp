#include "app/ui/AppBackgroundSettings.h"

#include <QDir>
#include <QtGlobal>

namespace miacode::ui {

namespace {

AppBackgroundOverlaySettings normalizedOverlay(const AppBackgroundOverlaySettings& settings)
{
    AppBackgroundOverlaySettings normalized = settings;
    normalized.toolbarAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.toolbarAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.toolbarAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.toolbarAlphaLight, kAppBackgroundOverlayAlphaMax);
    normalized.statusAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.statusAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.statusAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.statusAlphaLight, kAppBackgroundOverlayAlphaMax);
    normalized.panelAlpha = qBound(kAppBackgroundOverlayAlphaMin, normalized.panelAlpha, kAppBackgroundOverlayAlphaMax);
    normalized.cardAlphaDark = kAppBackgroundOverlayAlphaMax;
    normalized.cardAlphaLight = kAppBackgroundOverlayAlphaMax;
    normalized.editorHeaderAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.editorHeaderAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.editorHeaderAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.editorHeaderAlphaLight, kAppBackgroundOverlayAlphaMax);
    normalized.inputAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.inputAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.inputAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.inputAlphaLight, kAppBackgroundOverlayAlphaMax);
    normalized.codeEditorAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.codeEditorAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.codeEditorAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.codeEditorAlphaLight, kAppBackgroundOverlayAlphaMax);
    return normalized;
}

AppBackgroundOverlaySettings overlayFromJson(const QJsonObject& object)
{
    AppBackgroundOverlaySettings settings;
    settings.toolbarAlphaDark = object.value(QStringLiteral("toolbar_dark")).toInt(settings.toolbarAlphaDark);
    settings.toolbarAlphaLight = object.value(QStringLiteral("toolbar_light")).toInt(settings.toolbarAlphaLight);
    settings.statusAlphaDark = object.value(QStringLiteral("status_dark")).toInt(settings.statusAlphaDark);
    settings.statusAlphaLight = object.value(QStringLiteral("status_light")).toInt(settings.statusAlphaLight);
    if (object.contains(QStringLiteral("panel"))) {
        settings.panelAlpha = object.value(QStringLiteral("panel")).toInt(settings.panelAlpha);
    } else if (object.contains(QStringLiteral("panel_dark"))) {
        settings.panelAlpha = object.value(QStringLiteral("panel_dark")).toInt(settings.panelAlpha);
    } else {
        settings.panelAlpha = object.value(QStringLiteral("panel_light")).toInt(settings.panelAlpha);
    }
    settings.cardAlphaDark = object.value(QStringLiteral("card_dark")).toInt(settings.cardAlphaDark);
    settings.cardAlphaLight = object.value(QStringLiteral("card_light")).toInt(settings.cardAlphaLight);
    settings.editorHeaderAlphaDark = object.value(QStringLiteral("editor_header_dark")).toInt(settings.editorHeaderAlphaDark);
    settings.editorHeaderAlphaLight = object.value(QStringLiteral("editor_header_light")).toInt(settings.editorHeaderAlphaLight);
    settings.inputAlphaDark = object.value(QStringLiteral("input_dark")).toInt(settings.inputAlphaDark);
    settings.inputAlphaLight = object.value(QStringLiteral("input_light")).toInt(settings.inputAlphaLight);
    settings.codeEditorAlphaDark = object.value(QStringLiteral("code_editor_dark")).toInt(settings.codeEditorAlphaDark);
    settings.codeEditorAlphaLight = object.value(QStringLiteral("code_editor_light")).toInt(settings.codeEditorAlphaLight);
    return normalizedOverlay(settings);
}

QJsonObject overlayToJson(const AppBackgroundOverlaySettings& settings)
{
    const auto normalized = normalizedOverlay(settings);
    return QJsonObject{
        {QStringLiteral("toolbar_dark"), normalized.toolbarAlphaDark},
        {QStringLiteral("toolbar_light"), normalized.toolbarAlphaLight},
        {QStringLiteral("status_dark"), normalized.statusAlphaDark},
        {QStringLiteral("status_light"), normalized.statusAlphaLight},
        {QStringLiteral("panel"), normalized.panelAlpha},
        {QStringLiteral("card_dark"), normalized.cardAlphaDark},
        {QStringLiteral("card_light"), normalized.cardAlphaLight},
        {QStringLiteral("editor_header_dark"), normalized.editorHeaderAlphaDark},
        {QStringLiteral("editor_header_light"), normalized.editorHeaderAlphaLight},
        {QStringLiteral("input_dark"), normalized.inputAlphaDark},
        {QStringLiteral("input_light"), normalized.inputAlphaLight},
        {QStringLiteral("code_editor_dark"), normalized.codeEditorAlphaDark},
        {QStringLiteral("code_editor_light"), normalized.codeEditorAlphaLight},
    };
}

} // namespace

AppBackgroundSizeMode appBackgroundSizeModeFromToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower().replace(QLatin1Char('-'), QLatin1Char('_'));
    if (normalized == QLatin1String("contain")) return AppBackgroundSizeMode::Contain;
    if (normalized == QLatin1String("stretch")) return AppBackgroundSizeMode::Stretch;
    if (normalized == QLatin1String("center")) return AppBackgroundSizeMode::Center;
    if (normalized == QLatin1String("repeat")) return AppBackgroundSizeMode::Repeat;
    return AppBackgroundSizeMode::Cover;
}

QString appBackgroundSizeModeToken(AppBackgroundSizeMode mode)
{
    switch (mode) {
    case AppBackgroundSizeMode::Contain: return QStringLiteral("contain");
    case AppBackgroundSizeMode::Stretch: return QStringLiteral("stretch");
    case AppBackgroundSizeMode::Center: return QStringLiteral("center");
    case AppBackgroundSizeMode::Repeat: return QStringLiteral("repeat");
    case AppBackgroundSizeMode::Cover: default: return QStringLiteral("cover");
    }
}

AppBackgroundPosition appBackgroundPositionFromToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower().replace(QLatin1Char('-'), QLatin1Char('_'));
    if (normalized == QLatin1String("left")) return AppBackgroundPosition::Left;
    if (normalized == QLatin1String("right")) return AppBackgroundPosition::Right;
    if (normalized == QLatin1String("top")) return AppBackgroundPosition::Top;
    if (normalized == QLatin1String("bottom")) return AppBackgroundPosition::Bottom;
    if (normalized == QLatin1String("left_top")) return AppBackgroundPosition::LeftTop;
    if (normalized == QLatin1String("right_top")) return AppBackgroundPosition::RightTop;
    if (normalized == QLatin1String("left_bottom")) return AppBackgroundPosition::LeftBottom;
    if (normalized == QLatin1String("right_bottom")) return AppBackgroundPosition::RightBottom;
    return AppBackgroundPosition::Center;
}

QString appBackgroundPositionToken(AppBackgroundPosition position)
{
    switch (position) {
    case AppBackgroundPosition::Left: return QStringLiteral("left");
    case AppBackgroundPosition::Right: return QStringLiteral("right");
    case AppBackgroundPosition::Top: return QStringLiteral("top");
    case AppBackgroundPosition::Bottom: return QStringLiteral("bottom");
    case AppBackgroundPosition::LeftTop: return QStringLiteral("left_top");
    case AppBackgroundPosition::RightTop: return QStringLiteral("right_top");
    case AppBackgroundPosition::LeftBottom: return QStringLiteral("left_bottom");
    case AppBackgroundPosition::RightBottom: return QStringLiteral("right_bottom");
    case AppBackgroundPosition::Center: default: return QStringLiteral("center");
    }
}

AppBackgroundSettings normalizedAppBackgroundSettings(const AppBackgroundSettings& settings)
{
    AppBackgroundSettings normalized = settings;
    normalized.imagePath = QDir::cleanPath(settings.imagePath.trimmed());
    if (normalized.imagePath == QLatin1String(".")) normalized.imagePath.clear();
    normalized.opacity = qBound(kAppBackgroundOpacityMin, normalized.opacity, kAppBackgroundOpacityMax);
    normalized.blur = qBound(kAppBackgroundBlurMin, normalized.blur, kAppBackgroundBlurMax);
    normalized.overlays = normalizedOverlay(normalized.overlays);
    return normalized;
}

AppBackgroundSettings appBackgroundSettingsFromJson(const QJsonObject& object)
{
    AppBackgroundSettings settings;
    settings.enabled = object.value(QStringLiteral("enabled")).toBool(false);
    settings.imagePath = object.value(QStringLiteral("image_path")).toString();
    settings.opacity = object.value(QStringLiteral("opacity")).toDouble(kAppBackgroundOpacityDefault);
    settings.blur = object.value(QStringLiteral("blur")).toInt(kAppBackgroundBlurDefault);
    settings.overlays = overlayFromJson(object.value(QStringLiteral("overlay_alpha")).toObject());
    settings.sizeMode = appBackgroundSizeModeFromToken(object.value(QStringLiteral("size_mode")).toString());
    settings.position = appBackgroundPositionFromToken(object.value(QStringLiteral("position")).toString());
    return normalizedAppBackgroundSettings(settings);
}

QJsonObject appBackgroundSettingsToJson(const AppBackgroundSettings& settings)
{
    const auto normalized = normalizedAppBackgroundSettings(settings);
    return QJsonObject{
        {QStringLiteral("enabled"), normalized.enabled},
        {QStringLiteral("image_path"), normalized.imagePath},
        {QStringLiteral("opacity"), normalized.opacity},
        {QStringLiteral("blur"), normalized.blur},
        {QStringLiteral("overlay_alpha"), overlayToJson(normalized.overlays)},
        {QStringLiteral("size_mode"), appBackgroundSizeModeToken(normalized.sizeMode)},
        {QStringLiteral("position"), appBackgroundPositionToken(normalized.position)},
    };
}

} // namespace miacode::ui
