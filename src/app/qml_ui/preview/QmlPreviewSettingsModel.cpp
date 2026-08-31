#include "QmlPreviewSettingsModel.h"

#include "common/PreviewGameplayConfig.h"
#include "core/scene/PreviewHudState.h"
#include "core/video/PreviewRenderSettings.h"
#include "mainwindow/MainWindow.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/video_export/FontLibrary.h"
#include "ui/UiText.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

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

QmlPreviewSettingsModel::QmlPreviewSettingsModel(MainWindow& backend,
                                                 miacode::v2::UiRequestService& uiRequests,
                                                 QObject* parent)
    : QObject(parent)
    // From the application assembly, not from the hidden window.
    , uiRequests_(&uiRequests)
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

QString QmlPreviewSettingsModel::skinGroupLabel() const
{
    return text("dialog.render_settings.video.skin");
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

QVariantList QmlPreviewSettingsModel::skinOptions() const
{
    QVariantList list;
    if (backend_ == nullptr) {
        return list;
    }
    for (const QString& name : backend_->availablePreviewSkinDirectoryNames()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), name},
            {QStringLiteral("label"), backend_->previewSkinDisplayName(name)},
        });
    }
    return list;
}

int QmlPreviewSettingsModel::skinIndex() const
{
    if (backend_ == nullptr) {
        return -1;
    }
    const QStringList names = backend_->availablePreviewSkinDirectoryNames();
    for (int i = 0; i < names.size(); ++i) {
        if (names.at(i).compare(backend_->previewSkinDirectoryName_, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return names.isEmpty() ? -1 : 0;
}

QVariantList QmlPreviewSettingsModel::skinJudgeEffectOptions() const
{
    return QVariantList{
        text("dialog.skin_settings.chart_effect.standard"),
        text("dialog.skin_settings.chart_effect.starry"),
    };
}

int QmlPreviewSettingsModel::skinJudgeEffectIndex() const
{
    return backend_ != nullptr && backend_->previewJudgeEffectStyle_ == PreviewJudgeEffectStyle::Starry
        ? 1
        : 0;
}

QVariantList QmlPreviewSettingsModel::outlineOptions() const
{
    return QVariantList{
        text("dialog.render_settings.gameplay.judge_line.point"),
        text("dialog.render_settings.gameplay.judge_line.line"),
        text("dialog.render_settings.gameplay.judge_line.area"),
        text("dialog.render_settings.gameplay.judge_line.area_labeled"),
    };
}

int QmlPreviewSettingsModel::outlineIndex() const
{
    if (backend_ == nullptr) {
        return 1;
    }
    switch (backend_->previewOutlineVariant_) {
    case PreviewOutlineVariant::Point:
        return 0;
    case PreviewOutlineVariant::JudgeArea:
        return 2;
    case PreviewOutlineVariant::JudgeAreaLabeled:
        return 3;
    case PreviewOutlineVariant::Line:
    default:
        return 1;
    }
}

QVariantList QmlPreviewSettingsModel::fontLibraryOptions() const
{
    QVariantList list;
    const QVector<miacode::video_export::FontLibraryEntry> entries =
        miacode::video_export::fontLibraryEntries(
            true, text("card_font.default"));
    for (const miacode::video_export::FontLibraryEntry& entry : entries) {
        list.append(QVariantMap{
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("path"), entry.path},
            {QStringLiteral("family"), entry.family},
        });
    }
    return list;
}

QVariantList QmlPreviewSettingsModel::hudFontAreaOptions() const
{
    return QVariantList{
        QVariantMap{
            {QStringLiteral("label"), text("dialog.video_export.option.hud_font_area.chart_info")},
            {QStringLiteral("sample"), QStringLiteral("Title / Artist / MASTER 13+ / Designer")},
        },
        QVariantMap{
            {QStringLiteral("label"), text("dialog.video_export.option.hud_font_area.timestamp")},
            {QStringLiteral("sample"), QStringLiteral("12:34:567")},
        },
        QVariantMap{
            {QStringLiteral("label"), text("dialog.video_export.option.hud_font_area.object_stats")},
            {QStringLiteral("sample"), QStringLiteral("DELUXE Rate: 101.0000%  TAP: 128/128")},
        },
        QVariantMap{
            {QStringLiteral("label"), text("dialog.video_export.option.hud_font_area.debug")},
            {QStringLiteral("sample"), QStringLiteral("Present: 60.0 FPS  max=17ms")},
        },
    };
}

QString QmlPreviewSettingsModel::hudFontPath() const
{
    return miacode::preview::scene::previewHudCustomFontPath(
        static_cast<miacode::preview::scene::PreviewHudFontArea>(hudFontAreaIndex_));
}

QString QmlPreviewSettingsModel::hudFontSample() const
{
    const QVariantList areas = hudFontAreaOptions();
    return areas.at(qBound(0, hudFontAreaIndex_, static_cast<int>(areas.size()) - 1))
        .toMap().value(QStringLiteral("sample")).toString();
}

void QmlPreviewSettingsModel::setSkinIndex(int index)
{
    if (backend_ == nullptr) {
        return;
    }
    const QStringList names = backend_->availablePreviewSkinDirectoryNames();
    if (index < 0 || index >= names.size()) {
        return;
    }
    const QString skinDirectoryName = names.at(index);
    if (backend_->previewSkinDirectoryName_.compare(skinDirectoryName, Qt::CaseInsensitive) == 0) {
        return;
    }
    backend_->previewSkinDirectoryName_ = skinDirectoryName;
    backend_->previewSkinVariant_ =
        skinDirectoryName.compare(QStringLiteral("skinDX"), Qt::CaseInsensitive) == 0
            ? MainWindow::PreviewSkinVariant::Dx
            : MainWindow::PreviewSkinVariant::Standard;
    backend_->applyPreviewSkinDirectoryToSurfaces();
    backend_->savePortableState();
    emit skinChanged();
}

void QmlPreviewSettingsModel::setSkinJudgeEffectIndex(int index)
{
    if (backend_ == nullptr) {
        return;
    }
    const auto style = index == 1 ? PreviewJudgeEffectStyle::Starry : PreviewJudgeEffectStyle::Standard;
    if (backend_->previewJudgeEffectStyle_ == style) {
        return;
    }
    backend_->previewJudgeEffectStyle_ = style;
    if (backend_->previewCanvas_ != nullptr) {
        backend_->previewCanvas_->setJudgeEffectStyle(style);
    }
    backend_->savePortableState();
    emit skinChanged();
}

void QmlPreviewSettingsModel::setOutlineIndex(int index)
{
    if (backend_ == nullptr) {
        return;
    }
    PreviewOutlineVariant variant = PreviewOutlineVariant::Line;
    switch (index) {
    case 0:
        variant = PreviewOutlineVariant::Point;
        break;
    case 2:
        variant = PreviewOutlineVariant::JudgeArea;
        break;
    case 3:
        variant = PreviewOutlineVariant::JudgeAreaLabeled;
        break;
    case 1:
    default:
        break;
    }
    backend_->applyPreviewOutlineVariant(variant, /*useAutoSelection=*/false, /*persistState=*/true);
    emit skinChanged();
}

void QmlPreviewSettingsModel::setHudFontAreaIndex(int index)
{
    const int normalized = qBound(0, index, 3);
    if (hudFontAreaIndex_ == normalized) {
        return;
    }
    hudFontAreaIndex_ = normalized;
    emit hudFontChanged();
}

void QmlPreviewSettingsModel::setHudFontPath(const QString& path)
{
    const auto area = static_cast<miacode::preview::scene::PreviewHudFontArea>(hudFontAreaIndex_);
    if (miacode::preview::scene::previewHudCustomFontPath(area) == path) {
        return;
    }
    miacode::preview::scene::setPreviewHudCustomFontPath(area, path);
    if (backend_ != nullptr && backend_->previewCanvas_ != nullptr) {
        backend_->previewCanvas_->update();
    }
    emit hudFontChanged();
}

void QmlPreviewSettingsModel::openSkinDirectory()
{
    if (backend_ == nullptr) {
        return;
    }
    const QString skinRoot = backend_->resolvePreviewSkinRootDir();
    if (!skinRoot.isEmpty()) {
        QDir().mkpath(skinRoot);
        QDesktopServices::openUrl(QUrl::fromLocalFile(skinRoot));
    }
}

void QmlPreviewSettingsModel::openJudgeLineDirectory()
{
    if (backend_ == nullptr) {
        return;
    }
    const QString outlineDir = backend_->resolvePreviewCustomOutlineDir();
    if (!outlineDir.isEmpty()) {
        QDir().mkpath(outlineDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(outlineDir));
    }
}

void QmlPreviewSettingsModel::importHudFont()
{
    if (uiRequests_ == nullptr) {
        return;
    }
    miacode::v2::FileRequest request;
    request.title = text("dialog.video_export.option.import_hud_font");
    request.nameFilters = QStringList{QStringLiteral("Font Files (*.ttf *.otf)")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        applyHudFontImport(path);
    });
}

void QmlPreviewSettingsModel::applyHudFontImport(const QString& selectedPath)
{
    if (selectedPath.isEmpty()) {
        return;
    }
    const miacode::video_export::FontImportResult result =
        miacode::video_export::importFontFileIntoLibrary(selectedPath);
    if (result.path.isEmpty()) {
        if (uiRequests_ != nullptr) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Warning,
                text("dialog.video_export.option.import_hud_font"),
                result.failure == miacode::video_export::FontImportFailure::CopyFailed
                    ? text("card_font.copy_failed")
                    : text("card_font.invalid_font"));
        }
        return;
    }
    emit fontLibraryChanged();
    setHudFontPath(result.path);
}

void QmlPreviewSettingsModel::resetHudFont()
{
    setHudFontPath(QString());
}

void QmlPreviewSettingsModel::refreshFontLibrary()
{
    emit fontLibraryChanged();
    emit hudFontChanged();
}

}  // namespace miacode::qml_ui
