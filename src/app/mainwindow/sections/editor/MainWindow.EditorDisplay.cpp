#include "MainWindow.EditorSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/scene/PreviewProgressStatsCache.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

MainWindow::EditorSection::EditorSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::EditorSection::loadPortableState()
{
    state_.lastSessionFilePath_.clear();
    state_.lastOpenDir_.clear();
    state_.lastTrackPath_.clear();
    state_.autoRestoreLastSessionFile_ = true;
    resetPortablePreviewSettingsToDefaults();
    state_.editorLineSpacingFactor_ = kEditorLineSpacingFactorDefault;
    state_.editorTextFontPointSize_ = qBound(
        kEditorTextFontSizeMin,
        state_.editorTextFontPointSize_ > 0 ? state_.editorTextFontPointSize_ : editorFont().pointSize(),
        kEditorTextFontSizeMax
    );

    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject ui = root.value("ui").toObject();
    const QJsonObject app = root.value("app").toObject();
    const QJsonObject preview = app.value("preview").toObject();

    if (ui.value("editor_text_font_size").isDouble()) {
        state_.editorTextFontPointSize_ = qBound(
            kEditorTextFontSizeMin,
            qRound(ui.value("editor_text_font_size").toDouble(state_.editorTextFontPointSize_)),
            kEditorTextFontSizeMax
        );
    }
    if (ui.value("editor_line_spacing_factor").isDouble()) {
        state_.editorLineSpacingFactor_ = normalizeEditorLineSpacingFactor(
            ui.value("editor_line_spacing_factor").toDouble(state_.editorLineSpacingFactor_)
        );
    }
    applyEditorTextFontSize(state_.editorTextFontPointSize_, false);

    const QString dir = app.value("last_open_dir").toString();
    if (!dir.isEmpty() && QDir(dir).exists()) {
        state_.lastOpenDir_ = QDir::cleanPath(dir);
    }
    const QString lastOpenFile = app.value("last_open_file").toString();
    if (!lastOpenFile.isEmpty()) {
        state_.lastSessionFilePath_ = QDir::cleanPath(lastOpenFile);
    }
    if (app.value("auto_restore_last_open_file").isBool()) {
        state_.autoRestoreLastSessionFile_ = app.value("auto_restore_last_open_file").toBool(true);
    }
    const QString trackPath = app.value("last_track_path").toString();
    if (!trackPath.isEmpty() && QFileInfo::exists(trackPath)) {
        state_.lastTrackPath_ = QDir::cleanPath(trackPath);
    }
    applyPortablePreviewSettings(preview);
    owner_.refreshPreviewFrameRateTimers();
}

void MainWindow::EditorSection::resetPortablePreviewSettingsToDefaults()
{
    state_.softwarePreviewAudioSettings_ = PreviewAudioSettings();
    state_.previewAudioSettings_ = state_.softwarePreviewAudioSettings_;
    state_.showSlideTracks_ = true;
    state_.showJudgeMarkers_ = false;
    state_.showTouchTrail_ = false;
    state_.muriRenderOptions_ = MuriRenderOptions();
    state_.staticTapOnSlideThresholdMs_ = miacode::muri::kStaticTapOnSlideThresholdDefaultMs;
    state_.previewBackgroundBrightnessOuter_ = miacode::preview_video::kBackgroundBrightnessDefault;
    state_.previewBackgroundBrightnessInner_ = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    state_.previewLayoutSquareScale_ = miacode::preview_video::kLayoutSquareScaleDefault;
    state_.previewSmoothBrightness_ = miacode::preview_video::kSmoothBrightnessDefault;
    state_.previewOutlineVariant_ = PreviewOutlineVariant::Line;
    state_.previewOutlineVariantUsesAutoSelection_ = true;
    state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    state_.previewNoteFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    state_.previewSkinVariant_ = PreviewSkinVariant::Standard;
    state_.previewCanvasFrameRateMode_ = PreviewCanvasFrameRateMode::DisplayRefresh;
    state_.previewCanvasAspectRatio_ = 1.0;
    state_.previewAutoRestoreSquareAfterExport_ = false;
    state_.previewForceLabeledJudgeLineWhenPaused_ = true;
    state_.previewShowDebugInfo_ = false;
    state_.previewShowTimestamp_ = true;
    state_.previewShowObjectStatsHud_ = false;
    state_.exportShowObjectStatsHud_ = false;
    state_.previewShowValidationSummary_ = true;
    state_.workspacePanelsSwapped_ = false;
}

void MainWindow::EditorSection::applyPortablePreviewSettings(const QJsonObject& preview)
{
    if (preview.value("static_tap_on_slide_threshold_ms").isDouble()) {
        state_.staticTapOnSlideThresholdMs_ = qBound(
            miacode::muri::kStaticTapOnSlideThresholdMinMs,
            qRound(preview.value("static_tap_on_slide_threshold_ms")
                       .toDouble(state_.staticTapOnSlideThresholdMs_)),
            miacode::muri::kStaticTapOnSlideThresholdMaxMs
        );
    }
    const QString muriRenderMode = preview.value("muri_render_mode").toString().trimmed().toLower();
    state_.muriRenderOptions_.renderMode =
        muriRenderMode == QLatin1String("maimuri_dx_style")
        ? RenderMode::MaimuriDxStyle
        : RenderMode::Native;
    if (preview.value("show_chart_review_slide_judge_overlay").isBool()) {
        state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay =
            preview.value("show_chart_review_slide_judge_overlay")
                .toBool(state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    }
    if (preview.value("show_chart_review_simple_judge_overlay").isBool()) {
        state_.muriRenderOptions_.showChartReviewSimpleJudgeOverlay =
            preview.value("show_chart_review_simple_judge_overlay")
                .toBool(state_.muriRenderOptions_.showChartReviewSimpleJudgeOverlay);
    }
    if (preview.value("wifi_need_c").isBool()) {
        state_.muriRenderOptions_.wifiNeedC = preview.value("wifi_need_c").toBool(state_.muriRenderOptions_.wifiNeedC);
    }
    state_.muriRenderOptions_.excludeTouchFromMultiTouch = true;
    if (preview.value("outline_variant").isString()) {
        const QString outlineVariant = preview.value("outline_variant").toString().trimmed();
        if (!outlineVariant.isEmpty()) {
            state_.previewOutlineVariant_ = owner_.previewOutlineVariantFromStorageValue(outlineVariant);
            state_.previewOutlineVariantUsesAutoSelection_ = false;
        }
    }
    const double legacyBrightness = qBound(
        0.0,
        preview.value("background_brightness").toDouble(miacode::preview_video::kBackgroundBrightnessDefault),
        1.0
    );
    if (preview.value("background_brightness_outer").isDouble()) {
        state_.previewBackgroundBrightnessOuter_ =
            qBound(0.0, preview.value("background_brightness_outer").toDouble(legacyBrightness), 1.0);
    } else {
        state_.previewBackgroundBrightnessOuter_ = legacyBrightness;
    }
    if (preview.value("background_brightness_inner").isDouble()) {
        state_.previewBackgroundBrightnessInner_ =
            qBound(0.0, preview.value("background_brightness_inner").toDouble(state_.previewBackgroundBrightnessOuter_), 1.0);
    } else {
        state_.previewBackgroundBrightnessInner_ = state_.previewBackgroundBrightnessOuter_;
    }
    if (preview.value("layout_square_scale").isDouble()) {
        state_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(
            preview.value("layout_square_scale").toDouble(state_.previewLayoutSquareScale_)
        );
    }
    if (preview.value("smooth_brightness").isBool()) {
        state_.previewSmoothBrightness_ = preview.value("smooth_brightness").toBool(state_.previewSmoothBrightness_);
    }
    const QString scaleMode = preview.value("background_scale_mode").toString().trimmed().toLower();
    if (scaleMode == QLatin1String("fit") || scaleMode == QLatin1String("contain")) {
        state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
    } else if (!scaleMode.isEmpty()) {
        state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    }
    if (preview.value("note_flow_speed").isDouble()) {
        state_.previewNoteFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(
            preview.value("note_flow_speed").toDouble(state_.previewNoteFlowSpeed_)
        );
    }
    if (preview.value("skin_variant").isString()) {
        state_.previewSkinVariant_ = owner_.previewSkinVariantFromStorageValue(preview.value("skin_variant").toString());
    }
    if (preview.value("canvas_frame_rate_mode").isString()) {
        state_.previewCanvasFrameRateMode_ =
            owner_.previewCanvasFrameRateModeFromStorageValue(preview.value("canvas_frame_rate_mode").toString());
    }
    if (preview.value("force_labeled_judge_line_when_paused").isBool()) {
        state_.previewForceLabeledJudgeLineWhenPaused_ =
            preview.value("force_labeled_judge_line_when_paused")
                .toBool(state_.previewForceLabeledJudgeLineWhenPaused_);
    } else if (preview.value("hide_stage_media_when_paused").isBool()) {
        state_.previewForceLabeledJudgeLineWhenPaused_ =
            preview.value("hide_stage_media_when_paused")
                .toBool(state_.previewForceLabeledJudgeLineWhenPaused_);
    }
    if (preview.value("show_debug_info").isBool()) {
        state_.previewShowDebugInfo_ = preview.value("show_debug_info").toBool(false);
    }
    if (preview.value("show_timestamp").isBool()) {
        state_.previewShowTimestamp_ = preview.value("show_timestamp").toBool(true);
    }
    if (preview.value("show_object_stats_preview").isBool()) {
        state_.previewShowObjectStatsHud_ = preview.value("show_object_stats_preview").toBool(false);
    }
    if (preview.value("show_object_stats_export").isBool()) {
        state_.exportShowObjectStatsHud_ = preview.value("show_object_stats_export").toBool(false);
    }
    const bool unifiedObjectStatsHud = state_.previewShowObjectStatsHud_ || state_.exportShowObjectStatsHud_;
    state_.previewShowObjectStatsHud_ = unifiedObjectStatsHud;
    state_.exportShowObjectStatsHud_ = unifiedObjectStatsHud;
    if (preview.value("show_validation_summary").isBool()) {
        state_.previewShowValidationSummary_ = preview.value("show_validation_summary").toBool(true);
    }
    if (preview.value("swap_side_panels").isBool()) {
        state_.workspacePanelsSwapped_ = preview.value("swap_side_panels").toBool(false);
    }
    state_.previewCanvasAspectRatio_ = 1.0;
    state_.previewAutoRestoreSquareAfterExport_ = false;
    if (preview.value("audio").isObject()) {
        state_.softwarePreviewAudioSettings_ = PreviewAudioSettings::fromJson(preview.value("audio").toObject());
    } else {
        state_.softwarePreviewAudioSettings_ = PreviewAudioSettings::fromJson(preview);
    }
    state_.softwarePreviewAudioSettings_.normalize();
    state_.previewAudioSettings_ = state_.softwarePreviewAudioSettings_;
}

void MainWindow::EditorSection::savePortableState() const
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value("ui").toObject();
    QJsonObject app = root.value("app").toObject();
    QJsonObject preview = app.value("preview").toObject();
    preview.remove("show_judge_markers");
    preview.remove("show_touch_trail");

    ui.insert("editor_text_font_size", state_.editorTextFontPointSize_);
    ui.insert("editor_line_spacing_factor", state_.editorLineSpacingFactor_);
    root.insert("ui", ui);

    app.insert("last_open_dir", state_.lastOpenDir_);
    app.insert("last_open_file", state_.lastSessionFilePath_);
    app.insert("auto_restore_last_open_file", state_.autoRestoreLastSessionFile_);
    app.insert("last_track_path", state_.lastTrackPath_);
    app.insert("show_slide_tracks", true);

    preview.insert("static_tap_on_slide_threshold_ms", state_.staticTapOnSlideThresholdMs_);
    preview.insert(
        "muri_render_mode",
        state_.muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle
            ? QStringLiteral("maimuri_dx_style")
            : QStringLiteral("native")
    );
    preview.insert("show_chart_review_slide_judge_overlay", state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    preview.insert("show_chart_review_simple_judge_overlay", state_.muriRenderOptions_.showChartReviewSimpleJudgeOverlay);
    preview.insert("wifi_need_c", state_.muriRenderOptions_.wifiNeedC);
    preview.insert("background_brightness", state_.previewBackgroundBrightnessOuter_);
    preview.insert("background_brightness_outer", state_.previewBackgroundBrightnessOuter_);
    preview.insert("background_brightness_inner", state_.previewBackgroundBrightnessInner_);
    preview.insert("layout_square_scale", state_.previewLayoutSquareScale_);
    preview.insert("smooth_brightness", state_.previewSmoothBrightness_);
    if (state_.previewOutlineVariantUsesAutoSelection_) {
        preview.remove("outline_variant");
    } else {
        preview.insert("outline_variant", owner_.previewOutlineVariantStorageValue());
    }
    preview.insert(
        "background_scale_mode",
        state_.previewBackgroundScaleMode_ == PreviewBackgroundScaleMode::FitContain
            ? QStringLiteral("fit")
            : QStringLiteral("fill")
    );
    preview.insert("note_flow_speed", state_.previewNoteFlowSpeed_);
    preview.insert("skin_variant", owner_.previewSkinVariantStorageValue());
    preview.insert("canvas_frame_rate_mode", owner_.previewCanvasFrameRateModeStorageValue());
    preview.insert(
        "force_labeled_judge_line_when_paused",
        state_.previewForceLabeledJudgeLineWhenPaused_
    );
    preview.insert("show_debug_info", state_.previewShowDebugInfo_);
    preview.insert("show_timestamp", state_.previewShowTimestamp_);
    preview.insert("show_object_stats_preview", state_.previewShowObjectStatsHud_);
    preview.insert("show_object_stats_export", state_.exportShowObjectStatsHud_);
    preview.insert("show_validation_summary", state_.previewShowValidationSummary_);
    preview.insert("swap_side_panels", state_.workspacePanelsSwapped_);
    preview.insert("canvas_aspect_ratio", 1.0);
    preview.insert("auto_restore_square_after_export", false);
    preview.insert("audio", state_.softwarePreviewAudioSettings_.toJson());

    app.insert("preview", preview);
    root.insert("app", app);
    UiText::savePreferencesObject(root);
}

void MainWindow::EditorSection::persistEditorTextFontPreference() const
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value("ui").toObject();
    ui.insert("editor_text_font_size", state_.editorTextFontPointSize_);
    ui.insert("editor_line_spacing_factor", state_.editorLineSpacingFactor_);
    root.insert("ui", ui);
    UiText::savePreferencesObject(root);
}

void MainWindow::EditorSection::applyEditorTextFontSize(int pointSize, bool persistPreference)
{
    const int normalized = qBound(kEditorTextFontSizeMin, pointSize, kEditorTextFontSizeMax);
    const int blockSpacingPixels = blockSpacingPixelsForPointSize(normalized, state_.editorLineSpacingFactor_);
    const bool previousSuppress = state_.suppressTextDirtyTracking_;
    state_.suppressTextDirtyTracking_ = true;
    state_.editorTextFontPointSize_ = normalized;
    if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
        QSignalBlocker blocker(editor);
        editor->setFont(editorFont(normalized));
        editor->setBlockSpacingPixels(blockSpacingPixels);
        editor->refreshLineNumberAreaLayout();
    }
    if (ui_.timelineView_ != nullptr) {
        QFont timelineHeaderLineNumberFont = editorFont(normalized);
        timelineHeaderLineNumberFont.setPointSize(qMax(timelineHeaderLineNumberFont.pointSize() + 1, 12));
        ui_.timelineView_->setHeaderLineNumberFont(timelineHeaderLineNumberFont);
        owner_.windowSection_->updateBottomTabsDeviceHeight();
    }
    if (ui_.metadataExtraEdit_ != nullptr) {
        ui_.metadataExtraEdit_->setFont(editorFont(normalized));
        applyBlockSpacingToTextEdit(ui_.metadataExtraEdit_, blockSpacingPixels);
    }
    state_.suppressTextDirtyTracking_ = previousSuppress;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::EditorSection::applyEditorLineSpacingFactor(double factor, bool persistPreference)
{
    const bool previousSuppress = state_.suppressTextDirtyTracking_;
    state_.suppressTextDirtyTracking_ = true;
    state_.editorLineSpacingFactor_ = normalizeEditorLineSpacingFactor(factor);
    const int blockSpacingPixels =
        blockSpacingPixelsForPointSize(state_.editorTextFontPointSize_, state_.editorLineSpacingFactor_);
    if (auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_); editor != nullptr) {
        QSignalBlocker blocker(editor);
        editor->setBlockSpacingPixels(blockSpacingPixels);
        editor->refreshLineNumberAreaLayout();
    }
    if (ui_.metadataExtraEdit_ != nullptr) {
        applyBlockSpacingToTextEdit(ui_.metadataExtraEdit_, blockSpacingPixels);
    }
    state_.suppressTextDirtyTracking_ = previousSuppress;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::loadPortableState()
{
    editorSection_->loadPortableState();
}

void MainWindow::resetPortablePreviewSettingsToDefaults()
{
    editorSection_->resetPortablePreviewSettingsToDefaults();
}

void MainWindow::applyPortablePreviewSettings(const QJsonObject& preview)
{
    editorSection_->applyPortablePreviewSettings(preview);
}

void MainWindow::savePortableState() const
{
    editorSection_->savePortableState();
}

void MainWindow::persistEditorTextFontPreference() const
{
    editorSection_->persistEditorTextFontPreference();
}

void MainWindow::applyEditorTextFontSize(int pointSize, bool persistPreference)
{
    editorSection_->applyEditorTextFontSize(pointSize, persistPreference);
}

void MainWindow::applyEditorLineSpacingFactor(double factor, bool persistPreference)
{
    editorSection_->applyEditorLineSpacingFactor(factor, persistPreference);
}
