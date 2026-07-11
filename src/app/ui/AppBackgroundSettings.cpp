#include "app/ui/AppBackgroundSettings.h"

#include <QDir>
#include <QtGlobal>

namespace miacode::ui {

namespace {

AppBackgroundOverlaySettings normalizedAppBackgroundOverlaySettings(const AppBackgroundOverlaySettings& settings)
{
    AppBackgroundOverlaySettings normalized = settings;
    normalized.toolbarAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.toolbarAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.toolbarAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.toolbarAlphaLight, kAppBackgroundOverlayAlphaMax);
    normalized.statusAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.statusAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.statusAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.statusAlphaLight, kAppBackgroundOverlayAlphaMax);
    normalized.panelAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.panelAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.panelAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.panelAlphaLight, kAppBackgroundOverlayAlphaMax);
    normalized.cardAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.cardAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.cardAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.cardAlphaLight, kAppBackgroundOverlayAlphaMax);
    normalized.editorHeaderAlphaDark = qBound(
        kAppBackgroundOverlayAlphaMin,
        normalized.editorHeaderAlphaDark,
        kAppBackgroundOverlayAlphaMax);
    normalized.editorHeaderAlphaLight = qBound(
        kAppBackgroundOverlayAlphaMin,
        normalized.editorHeaderAlphaLight,
        kAppBackgroundOverlayAlphaMax);
    normalized.inputAlphaDark = qBound(kAppBackgroundOverlayAlphaMin, normalized.inputAlphaDark, kAppBackgroundOverlayAlphaMax);
    normalized.inputAlphaLight = qBound(kAppBackgroundOverlayAlphaMin, normalized.inputAlphaLight, kAppBackgroundOverlayAlphaMax);
    return normalized;
}

AppBackgroundOverlaySettings appBackgroundOverlaySettingsFromJson(const QJsonObject& object)
{
    AppBackgroundOverlaySettings settings;
    settings.toolbarAlphaDark = object.value(QStringLiteral("toolbar_dark")).toInt(settings.toolbarAlphaDark);
    settings.toolbarAlphaLight = object.value(QStringLiteral("toolbar_light")).toInt(settings.toolbarAlphaLight);
    settings.statusAlphaDark = object.value(QStringLiteral("status_dark")).toInt(settings.statusAlphaDark);
    settings.statusAlphaLight = object.value(QStringLiteral("status_light")).toInt(settings.statusAlphaLight);
    settings.panelAlphaDark = object.value(QStringLiteral("panel_dark")).toInt(settings.panelAlphaDark);
    settings.panelAlphaLight = object.value(QStringLiteral("panel_light")).toInt(settings.panelAlphaLight);
    settings.cardAlphaDark = object.value(QStringLiteral("card_dark")).toInt(settings.cardAlphaDark);
    settings.cardAlphaLight = object.value(QStringLiteral("card_light")).toInt(settings.cardAlphaLight);
    settings.editorHeaderAlphaDark =
        object.value(QStringLiteral("editor_header_dark")).toInt(settings.editorHeaderAlphaDark);
    settings.editorHeaderAlphaLight =
        object.value(QStringLiteral("editor_header_light")).toInt(settings.editorHeaderAlphaLight);
    settings.inputAlphaDark = object.value(QStringLiteral("input_dark")).toInt(settings.inputAlphaDark);
    settings.inputAlphaLight = object.value(QStringLiteral("input_light")).toInt(settings.inputAlphaLight);
    return normalizedAppBackgroundOverlaySettings(settings);
}

QJsonObject appBackgroundOverlaySettingsToJson(const AppBackgroundOverlaySettings& settings)
{
    const AppBackgroundOverlaySettings normalized = normalizedAppBackgroundOverlaySettings(settings);
    QJsonObject object;
    object.insert(QStringLiteral("toolbar_dark"), normalized.toolbarAlphaDark);
    object.insert(QStringLiteral("toolbar_light"), normalized.toolbarAlphaLight);
    object.insert(QStringLiteral("status_dark"), normalized.statusAlphaDark);
    object.insert(QStringLiteral("status_light"), normalized.statusAlphaLight);
    object.insert(QStringLiteral("panel_dark"), normalized.panelAlphaDark);
    object.insert(QStringLiteral("panel_light"), normalized.panelAlphaLight);
    object.insert(QStringLiteral("card_dark"), normalized.cardAlphaDark);
    object.insert(QStringLiteral("card_light"), normalized.cardAlphaLight);
    object.insert(QStringLiteral("editor_header_dark"), normalized.editorHeaderAlphaDark);
    object.insert(QStringLiteral("editor_header_light"), normalized.editorHeaderAlphaLight);
    object.insert(QStringLiteral("input_dark"), normalized.inputAlphaDark);
    object.insert(QStringLiteral("input_light"), normalized.inputAlphaLight);
    return object;
}

}  // namespace

AppBackgroundSizeMode appBackgroundSizeModeFromToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower().replace(QLatin1Char('-'), QLatin1Char('_'));
    if (normalized == QLatin1String("contain")) {
        return AppBackgroundSizeMode::Contain;
    }
    if (normalized == QLatin1String("stretch")) {
        return AppBackgroundSizeMode::Stretch;
    }
    if (normalized == QLatin1String("center")) {
        return AppBackgroundSizeMode::Center;
    }
    if (normalized == QLatin1String("repeat")) {
        return AppBackgroundSizeMode::Repeat;
    }
    return AppBackgroundSizeMode::Cover;
}

QString appBackgroundSizeModeToken(AppBackgroundSizeMode mode)
{
    switch (mode) {
    case AppBackgroundSizeMode::Contain:
        return QStringLiteral("contain");
    case AppBackgroundSizeMode::Stretch:
        return QStringLiteral("stretch");
    case AppBackgroundSizeMode::Center:
        return QStringLiteral("center");
    case AppBackgroundSizeMode::Repeat:
        return QStringLiteral("repeat");
    case AppBackgroundSizeMode::Cover:
    default:
        return QStringLiteral("cover");
    }
}

AppBackgroundPosition appBackgroundPositionFromToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower().replace(QLatin1Char('-'), QLatin1Char('_'));
    if (normalized == QLatin1String("left")) {
        return AppBackgroundPosition::Left;
    }
    if (normalized == QLatin1String("right")) {
        return AppBackgroundPosition::Right;
    }
    if (normalized == QLatin1String("top")) {
        return AppBackgroundPosition::Top;
    }
    if (normalized == QLatin1String("bottom")) {
        return AppBackgroundPosition::Bottom;
    }
    if (normalized == QLatin1String("left_top")) {
        return AppBackgroundPosition::LeftTop;
    }
    if (normalized == QLatin1String("right_top")) {
        return AppBackgroundPosition::RightTop;
    }
    if (normalized == QLatin1String("left_bottom")) {
        return AppBackgroundPosition::LeftBottom;
    }
    if (normalized == QLatin1String("right_bottom")) {
        return AppBackgroundPosition::RightBottom;
    }
    return AppBackgroundPosition::Center;
}

QString appBackgroundPositionToken(AppBackgroundPosition position)
{
    switch (position) {
    case AppBackgroundPosition::Left:
        return QStringLiteral("left");
    case AppBackgroundPosition::Right:
        return QStringLiteral("right");
    case AppBackgroundPosition::Top:
        return QStringLiteral("top");
    case AppBackgroundPosition::Bottom:
        return QStringLiteral("bottom");
    case AppBackgroundPosition::LeftTop:
        return QStringLiteral("left_top");
    case AppBackgroundPosition::RightTop:
        return QStringLiteral("right_top");
    case AppBackgroundPosition::LeftBottom:
        return QStringLiteral("left_bottom");
    case AppBackgroundPosition::RightBottom:
        return QStringLiteral("right_bottom");
    case AppBackgroundPosition::Center:
    default:
        return QStringLiteral("center");
    }
}

AppBackgroundSettings normalizedAppBackgroundSettings(const AppBackgroundSettings& settings)
{
    AppBackgroundSettings normalized = settings;
    normalized.imagePath = QDir::cleanPath(settings.imagePath.trimmed());
    if (normalized.imagePath == QLatin1String(".")) {
        normalized.imagePath.clear();
    }
    normalized.opacity = qBound(kAppBackgroundOpacityMin, normalized.opacity, kAppBackgroundOpacityMax);
    normalized.blur = qBound(kAppBackgroundBlurMin, normalized.blur, kAppBackgroundBlurMax);
    normalized.overlays = normalizedAppBackgroundOverlaySettings(normalized.overlays);
    return normalized;
}

AppBackgroundSettings appBackgroundSettingsFromJson(const QJsonObject& object)
{
    AppBackgroundSettings settings;
    settings.enabled = object.value(QStringLiteral("enabled")).toBool(false);
    settings.imagePath = object.value(QStringLiteral("image_path")).toString();
    settings.opacity = object.value(QStringLiteral("opacity")).toDouble(kAppBackgroundOpacityDefault);
    settings.blur = object.value(QStringLiteral("blur")).toInt(kAppBackgroundBlurDefault);
    settings.overlays = appBackgroundOverlaySettingsFromJson(object.value(QStringLiteral("overlay_alpha")).toObject());
    settings.sizeMode = appBackgroundSizeModeFromToken(object.value(QStringLiteral("size_mode")).toString());
    settings.position = appBackgroundPositionFromToken(object.value(QStringLiteral("position")).toString());
    return normalizedAppBackgroundSettings(settings);
}

QJsonObject appBackgroundSettingsToJson(const AppBackgroundSettings& settings)
{
    const AppBackgroundSettings normalized = normalizedAppBackgroundSettings(settings);
    QJsonObject object;
    object.insert(QStringLiteral("enabled"), normalized.enabled);
    object.insert(QStringLiteral("image_path"), normalized.imagePath);
    object.insert(QStringLiteral("opacity"), normalized.opacity);
    object.insert(QStringLiteral("blur"), normalized.blur);
    object.insert(QStringLiteral("overlay_alpha"), appBackgroundOverlaySettingsToJson(normalized.overlays));
    object.insert(QStringLiteral("size_mode"), appBackgroundSizeModeToken(normalized.sizeMode));
    object.insert(QStringLiteral("position"), appBackgroundPositionToken(normalized.position));
    return object;
}

}  // namespace miacode::ui
