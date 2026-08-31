#include "app/ui/AppBackgroundSettings.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QTextStream>

#include <cmath>

namespace {

bool require(bool condition, const QString& message, QTextStream& out)
{
    if (!condition) {
        out << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < 1e-9;
}

bool testDefaultsAndNormalization(QTextStream& out)
{
    const miacode::ui::AppBackgroundSettings defaults;
    if (!require(!defaults.enabled && near(defaults.opacity, 0.2)
                     && defaults.overlays.toolbarAlphaDark == 200
                     && defaults.overlays.toolbarAlphaLight == 210
                     && defaults.overlays.statusAlphaDark == 210
                     && defaults.overlays.statusAlphaLight == 220
                     && defaults.overlays.panelAlphaDark == 200
                     && defaults.overlays.panelAlphaLight == 200
                     && defaults.overlays.cardAlphaDark == 255
                     && defaults.overlays.cardAlphaLight == 255
                     && defaults.overlays.editorHeaderAlphaDark == 190
                     && defaults.overlays.editorHeaderAlphaLight == 200
                     && defaults.overlays.inputAlphaDark == 200
                     && defaults.overlays.inputAlphaLight == 200
                     && defaults.overlays.codeEditorAlphaDark == 200
                     && defaults.overlays.codeEditorAlphaLight == 200,
                 QStringLiteral("v1 background defaults"), out)) {
        return false;
    }

    miacode::ui::AppBackgroundSettings unnormalized = defaults;
    unnormalized.imagePath = QStringLiteral("  /tmp/example.png/..  ");
    unnormalized.opacity = 2.0;
    unnormalized.overlays.toolbarAlphaDark = -10;
    unnormalized.overlays.cardAlphaDark = 1;
    const auto normalized = miacode::ui::normalizedAppBackgroundSettings(unnormalized);
    return require(normalized.imagePath == QStringLiteral("/tmp"), QStringLiteral("image path is cleaned"), out)
        && require(near(normalized.opacity, 0.8), QStringLiteral("opacity is clamped"), out)
        && require(normalized.overlays.toolbarAlphaDark == 0, QStringLiteral("overlay alpha is clamped"), out)
        && require(normalized.overlays.cardAlphaDark == 255, QStringLiteral("card alpha stays opaque"), out);
}

bool testJsonContract(QTextStream& out)
{
    const QJsonObject object{
        {QStringLiteral("enabled"), true},
        {QStringLiteral("image_path"), QStringLiteral("/tmp/background.png")},
        {QStringLiteral("opacity"), -1.0},
        {QStringLiteral("blur"), 12},
        {QStringLiteral("size_mode"), QStringLiteral("right-bottom")},
        {QStringLiteral("position"), QStringLiteral("left_top")},
        {QStringLiteral("overlay_alpha"), QJsonObject{
            {QStringLiteral("toolbar_dark"), 42},
            {QStringLiteral("toolbar_light"), 43},
            {QStringLiteral("card_dark"), 12},
            {QStringLiteral("card_light"), 13},
        }},
    };
    const auto settings = miacode::ui::appBackgroundSettingsFromJson(object);
    if (!require(settings.enabled && settings.imagePath == QStringLiteral("/tmp/background.png")
                     && near(settings.opacity, 0.0)
                     && settings.blur == 0
                     && settings.sizeMode == miacode::ui::AppBackgroundSizeMode::Cover
                     && settings.position == miacode::ui::AppBackgroundPosition::LeftTop
                     && settings.overlays.toolbarAlphaDark == 42
                     && settings.overlays.toolbarAlphaLight == 43
                     && settings.overlays.cardAlphaDark == 255
                     && settings.overlays.cardAlphaLight == 255,
                 QStringLiteral("v1 JSON values and unknown tokens"), out)) {
        return false;
    }

    const QJsonObject serialized = miacode::ui::appBackgroundSettingsToJson(settings);
    const auto roundTrip = miacode::ui::appBackgroundSettingsFromJson(serialized);
    return require(roundTrip.enabled == settings.enabled
                       && roundTrip.imagePath == settings.imagePath
                       && near(roundTrip.opacity, settings.opacity)
                       && roundTrip.sizeMode == settings.sizeMode
                       && roundTrip.position == settings.position
                       && roundTrip.overlays.toolbarAlphaDark == settings.overlays.toolbarAlphaDark,
                   QStringLiteral("background JSON round trip"), out);
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const bool ok = testDefaultsAndNormalization(out) && testJsonContract(out);
    if (ok) {
        out << "app_background_settings_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
