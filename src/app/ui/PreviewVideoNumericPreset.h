#pragma once

#include "UiText.h"
#include "common/PreviewVideoGeometryConfig.h"

#include <QJsonObject>

namespace miacode::ui {

struct PreviewVideoNumericPreset {
    double backgroundBrightnessOuter = miacode::preview_video::kBackgroundBrightnessDefault;
    double backgroundBrightnessInner = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    double layoutSquareScale = miacode::preview_video::kLayoutSquareScaleDefault;

    static PreviewVideoNumericPreset fromJson(const QJsonObject& json)
    {
        PreviewVideoNumericPreset preset;
        preset.backgroundBrightnessOuter = qBound(
            0.0,
            json.value(QStringLiteral("background_brightness_outer"))
                .toDouble(preset.backgroundBrightnessOuter),
            1.0);
        preset.backgroundBrightnessInner = qBound(
            0.0,
            json.value(QStringLiteral("background_brightness_inner"))
                .toDouble(preset.backgroundBrightnessInner),
            1.0);
        preset.layoutSquareScale = miacode::preview_video::normalizedLayoutSquareScale(
            json.value(QStringLiteral("layout_square_scale")).toDouble(preset.layoutSquareScale));
        return preset;
    }

    QJsonObject toJson() const
    {
        return {
            {QStringLiteral("background_brightness_outer"), backgroundBrightnessOuter},
            {QStringLiteral("background_brightness_inner"), backgroundBrightnessInner},
            {QStringLiteral("layout_square_scale"), layoutSquareScale},
        };
    }
};

inline PreviewVideoNumericPreset loadPreviewVideoNumericPreset()
{
    const QJsonObject preset = UiText::loadPreferencesObject()
        .value(QStringLiteral("app")).toObject()
        .value(QStringLiteral("preview")).toObject()
        .value(QStringLiteral("numeric_preset")).toObject();
    return PreviewVideoNumericPreset::fromJson(preset);
}

inline bool savePreviewVideoNumericPreset(const PreviewVideoNumericPreset& preset)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QStringLiteral("app")).toObject();
    QJsonObject preview = app.value(QStringLiteral("preview")).toObject();
    preview.insert(QStringLiteral("numeric_preset"), preset.toJson());
    app.insert(QStringLiteral("preview"), preview);
    root.insert(QStringLiteral("app"), app);
    return UiText::savePreferencesObject(root);
}

}  // namespace miacode::ui
