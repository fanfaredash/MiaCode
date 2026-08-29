#include "QmlPreviewSettingsModel.h"

#include "common/PreviewGameplayConfig.h"
#include "core/video/PreviewRenderSettings.h"
#include "mainwindow/MainWindow.h"
#include "ui/UiText.h"

namespace miacode::qml_ui {

namespace {

QString text(const char* key)
{
    return UiText::text(QLatin1String(key));
}

QVariantMap option(const QVariant& value, const char* labelKey)
{
    return QVariantMap{
        {QStringLiteral("value"), value},
        {QStringLiteral("label"), text(labelKey)},
    };
}

}  // namespace

QmlPreviewSettingsModel::QmlPreviewSettingsModel(MainWindow& backend, QObject* parent)
    : QObject(parent)
    , backend_(&backend)
{
}

QVariantMap QmlPreviewSettingsModel::values() const
{
    return backend_ != nullptr ? backend_->previewRenderSettings() : QVariantMap{};
}

void QmlPreviewSettingsModel::setValue(const QString& key, const QVariant& value)
{
    if (backend_ == nullptr) {
        return;
    }
    backend_->setPreviewRenderSetting(key, value);
    emit changed();
}

QVariantMap QmlPreviewSettingsModel::labels() const
{
    return QVariantMap{
        {QStringLiteral("brightnessOuter"), text("dialog.render_settings.video.brightness_outer")},
        {QStringLiteral("brightnessInner"), text("dialog.render_settings.video.brightness_inner")},
        {QStringLiteral("layoutSquareScale"), text("dialog.render_settings.video.layout_square_scale")},
        {QStringLiteral("scaleMode"), text("dialog.render_settings.video.scale_mode")},
        {QStringLiteral("smoothBrightness"), text("dialog.render_settings.video.smooth_brightness")},
        {QStringLiteral("showTimestamp"), text("dialog.video_export.option.show_timestamp")},
        {QStringLiteral("touchPadAuthoringShortcut"),
         text("dialog.render_settings.video.touch_pad_authoring_shortcut")},
        {QStringLiteral("showDebugInfo"), text("dialog.render_settings.preview.debug")},
        {QStringLiteral("forceLabeledJudgeLineWhenPaused"),
         text("dialog.render_settings.gameplay.force_labeled_judge_line_when_paused")},
        {QStringLiteral("tapFlowSpeed"), text("dialog.render_settings.video.tap_flow_speed")},
        {QStringLiteral("touchFlowSpeed"), text("dialog.render_settings.video.touch_flow_speed")},
        {QStringLiteral("judgeEffect"), text("dialog.render_settings.gameplay.judge_effect")},
        {QStringLiteral("slideEarlierOnTop"), text("dialog.render_settings.gameplay.slide_stack_order")},
        {QStringLiteral("centerDisplay"), text("dialog.render_settings.gameplay.center_display")},
        {QStringLiteral("tapJudgeTextDistance"),
         text("dialog.render_settings.gameplay.tap_judge_text_distance")},
    };
}

QString QmlPreviewSettingsModel::videoGroupLabel() const
{
    return text("dialog.render_settings.video_group");
}

QString QmlPreviewSettingsModel::gameplayGroupLabel() const
{
    return text("dialog.render_settings.gameplay_group");
}

QVariantList QmlPreviewSettingsModel::scaleModeOptions() const
{
    return QVariantList{
        option(static_cast<int>(PreviewBackgroundScaleMode::FillCrop),
               "dialog.render_settings.video.scale.fill"),
        option(static_cast<int>(PreviewBackgroundScaleMode::FitContain),
               "dialog.render_settings.video.scale.fit"),
        option(static_cast<int>(PreviewBackgroundScaleMode::SquareFitContain),
               "dialog.render_settings.video.scale.square_fit"),
        option(static_cast<int>(PreviewBackgroundScaleMode::InnerCircleFitOuterFill),
               "dialog.render_settings.video.scale.inner_circle_fit_outer_fill"),
    };
}

QVariantList QmlPreviewSettingsModel::slideStackOrderOptions() const
{
    // true = the earlier second and its text sit on top, which is the DX order.
    return QVariantList{
        option(true, "dialog.render_settings.gameplay.slide_stack_order.dx_style"),
        option(false, "dialog.render_settings.gameplay.slide_stack_order.finale_style"),
    };
}

QVariantList QmlPreviewSettingsModel::centerDisplayOptions() const
{
    using miacode::preview_gameplay::CenterDisplayMode;
    return QVariantList{
        option(static_cast<int>(CenterDisplayMode::Off),
               "dialog.render_settings.gameplay.center_display.off"),
        option(static_cast<int>(CenterDisplayMode::Combo),
               "dialog.render_settings.gameplay.center_display.combo"),
        option(static_cast<int>(CenterDisplayMode::AchievementDxPlus),
               "dialog.render_settings.gameplay.center_display.achievement_dx_plus"),
        option(static_cast<int>(CenterDisplayMode::AchievementDxMinus100),
               "dialog.render_settings.gameplay.center_display.achievement_dx_minus_100"),
        option(static_cast<int>(CenterDisplayMode::AchievementDxMinus101),
               "dialog.render_settings.gameplay.center_display.achievement_dx_minus_101"),
        option(static_cast<int>(CenterDisplayMode::DxScorePlus),
               "dialog.render_settings.gameplay.center_display.dx_score_plus"),
        option(static_cast<int>(CenterDisplayMode::DxScoreMinus),
               "dialog.render_settings.gameplay.center_display.dx_score_minus"),
        option(static_cast<int>(CenterDisplayMode::AchievementFinalePlus),
               "dialog.render_settings.gameplay.center_display.achievement_finale_plus"),
    };
}

QVariantList QmlPreviewSettingsModel::tapJudgeTextDistanceOptions() const
{
    return QVariantList{
        option(static_cast<int>(PreviewTapJudgeTextDistance::Inner),
               "dialog.render_settings.gameplay.tap_judge_text_distance.inner"),
        option(static_cast<int>(PreviewTapJudgeTextDistance::Middle),
               "dialog.render_settings.gameplay.tap_judge_text_distance.middle"),
        option(static_cast<int>(PreviewTapJudgeTextDistance::Outer),
               "dialog.render_settings.gameplay.tap_judge_text_distance.outer"),
    };
}

QVariantList QmlPreviewSettingsModel::judgeEffectOptions() const
{
    // Keys into `values`, not enum members: each overlay is its own boolean.
    return QVariantList{
        option(QStringLiteral("judgeEffectSlide"),
               "dialog.render_settings.gameplay.judge_effect.slide"),
        option(QStringLiteral("judgeEffectTap"),
               "dialog.render_settings.gameplay.judge_effect.tap"),
        option(QStringLiteral("judgeEffectBreak"),
               "dialog.render_settings.gameplay.judge_effect.break"),
        option(QStringLiteral("judgeEffectTouch"),
               "dialog.render_settings.gameplay.judge_effect.touch"),
    };
}

}  // namespace miacode::qml_ui
