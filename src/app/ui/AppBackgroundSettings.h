#pragma once

#include <QJsonObject>
#include <QString>

namespace miacode::ui {

struct AppBackgroundOverlaySettings {
    int toolbarAlphaDark = 198;
    int toolbarAlphaLight = 206;
    int statusAlphaDark = 208;
    int statusAlphaLight = 216;
    int panelAlphaDark = 176;
    int panelAlphaLight = 190;
    int cardAlphaDark = 184;
    int cardAlphaLight = 196;
    int editorHeaderAlphaDark = 188;
    int editorHeaderAlphaLight = 196;
    int inputAlphaDark = 196;
    int inputAlphaLight = 204;
};

enum class AppBackgroundSizeMode {
    Cover,
    Contain,
    Stretch,
    Center,
    Repeat,
};

enum class AppBackgroundPosition {
    Center,
    Left,
    Right,
    Top,
    Bottom,
    LeftTop,
    RightTop,
    LeftBottom,
    RightBottom,
};

struct AppBackgroundSettings {
    bool enabled = false;
    QString imagePath;
    double opacity = 0.2;
    int blur = 0;
    AppBackgroundOverlaySettings overlays;
    AppBackgroundSizeMode sizeMode = AppBackgroundSizeMode::Cover;
    AppBackgroundPosition position = AppBackgroundPosition::Center;
};

constexpr double kAppBackgroundOpacityMin = 0.0;
constexpr double kAppBackgroundOpacityMax = 0.8;
constexpr double kAppBackgroundOpacityDefault = 0.2;
constexpr int kAppBackgroundBlurMin = 0;
constexpr int kAppBackgroundBlurMax = 0;
constexpr int kAppBackgroundBlurDefault = 0;
constexpr int kAppBackgroundOverlayAlphaMin = 0;
constexpr int kAppBackgroundOverlayAlphaMax = 255;

AppBackgroundSettings normalizedAppBackgroundSettings(const AppBackgroundSettings& settings);
AppBackgroundSettings appBackgroundSettingsFromJson(const QJsonObject& object);
QJsonObject appBackgroundSettingsToJson(const AppBackgroundSettings& settings);

AppBackgroundSizeMode appBackgroundSizeModeFromToken(const QString& token);
QString appBackgroundSizeModeToken(AppBackgroundSizeMode mode);

AppBackgroundPosition appBackgroundPositionFromToken(const QString& token);
QString appBackgroundPositionToken(AppBackgroundPosition position);

}  // namespace miacode::ui
