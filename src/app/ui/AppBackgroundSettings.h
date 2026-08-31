#pragma once

#include <QJsonObject>
#include <QString>

namespace miacode::ui {

struct AppBackgroundOverlaySettings {
    int toolbarAlphaDark = 200;
    int toolbarAlphaLight = 210;
    int statusAlphaDark = 210;
    int statusAlphaLight = 220;
    int panelAlphaDark = 200;
    int panelAlphaLight = 200;
    int cardAlphaDark = 255;
    int cardAlphaLight = 255;
    int editorHeaderAlphaDark = 190;
    int editorHeaderAlphaLight = 200;
    int inputAlphaDark = 200;
    int inputAlphaLight = 200;
    int codeEditorAlphaDark = 200;
    int codeEditorAlphaLight = 200;
};

enum class AppBackgroundSizeMode { Cover, Contain, Stretch, Center, Repeat };
enum class AppBackgroundPosition {
    Center, Left, Right, Top, Bottom, LeftTop, RightTop, LeftBottom, RightBottom
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

} // namespace miacode::ui
