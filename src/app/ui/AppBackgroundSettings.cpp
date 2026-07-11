#include "app/ui/AppBackgroundSettings.h"

#include <QDir>
#include <QtGlobal>

namespace miacode::ui {

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
    return normalized;
}

AppBackgroundSettings appBackgroundSettingsFromJson(const QJsonObject& object)
{
    AppBackgroundSettings settings;
    settings.enabled = object.value(QStringLiteral("enabled")).toBool(false);
    settings.imagePath = object.value(QStringLiteral("image_path")).toString();
    settings.opacity = object.value(QStringLiteral("opacity")).toDouble(kAppBackgroundOpacityDefault);
    settings.blur = object.value(QStringLiteral("blur")).toInt(kAppBackgroundBlurDefault);
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
    object.insert(QStringLiteral("size_mode"), appBackgroundSizeModeToken(normalized.sizeMode));
    object.insert(QStringLiteral("position"), appBackgroundPositionToken(normalized.position));
    return object;
}

}  // namespace miacode::ui
