#include "MainWindow.EditorSection.h"
#include "../../MainWindowShared.h"

#include "QtPreviewSfxRuntime.h"
#include "UiText.h"
#include "preview/runtime/PreviewRuntime.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace {

QString renderModeToken(RenderMode mode)
{
    return mode == RenderMode::MaimuriDxStyle
        ? QStringLiteral("maimuri_dx_style")
        : QStringLiteral("native");
}

RenderMode renderModeFromToken(const QString& token)
{
    return token.trimmed().compare(QStringLiteral("maimuri_dx_style"), Qt::CaseInsensitive) == 0
        ? RenderMode::MaimuriDxStyle
        : RenderMode::Native;
}

QString legacyProjectRenderStateFilePath(const QString& currentFilePath)
{
    if (currentFilePath.isEmpty()) {
        return QString();
    }
    const QDir projectDir(QFileInfo(currentFilePath).absolutePath());
    return projectDir.filePath(QStringLiteral(".miacode_render_settings.json"));
}

}  // namespace

QString MainWindow::EditorSection::resolveProjectRenderStateFilePath() const
{
    const QString projectDataDirectoryPath =
        miacode::mainwindow::shared::resolveProjectDataDirectoryPath(state_.currentFilePath_);
    if (projectDataDirectoryPath.isEmpty()) {
        return QString();
    }
    return QDir(projectDataDirectoryPath).filePath(QStringLiteral("miacode_settings.json"));
}

void MainWindow::EditorSection::loadProjectRenderState()
{
    const double previousCanvasAspectRatio = state_.previewCanvasAspectRatio_;
    const QJsonObject portableRoot = UiText::loadPreferencesObject();
    const QJsonObject portablePreview = portableRoot.value("app").toObject().value("preview").toObject();
    resetPortablePreviewSettingsToDefaults();
    applyPortablePreviewSettings(portablePreview);
    state_.projectLastOpenedDifficultyId_ = 0;
    bool projectTimelineZoomScaleLoaded = false;
    double projectTimelineZoomScale = 0.5;

    const QString path = resolveProjectRenderStateFilePath();
    const QString legacyPath = legacyProjectRenderStateFilePath(state_.currentFilePath_);
    const QString loadPath = QFileInfo::exists(path) ? path : legacyPath;
    if (!loadPath.isEmpty()) {
        QFile file(loadPath);
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject root = doc.object();
                if (root.value("audio").isObject()) {
                    state_.previewAudioSettings_ = PreviewAudioSettings::fromJson(root.value("audio").toObject());
                } else if (root.value("preview_audio").isObject()) {
                    state_.previewAudioSettings_ = PreviewAudioSettings::fromJson(root.value("preview_audio").toObject());
                }
                if (root.value("timing").isObject()) {
                    state_.previewTimingSettings_ = PreviewTimingSettings::fromJson(root.value("timing").toObject());
                } else if (root.value("preview_timing").isObject()) {
                    state_.previewTimingSettings_ = PreviewTimingSettings::fromJson(root.value("preview_timing").toObject());
                }
                const QJsonObject render = root.value("render").toObject();
                if (!render.isEmpty()) {
                    if (render.value("preview_playback_rate").isDouble()) {
                        state_.previewPlaybackRate_ = qMax(
                            0.25,
                            render.value("preview_playback_rate").toDouble(state_.previewPlaybackRate_)
                        );
                    }
                    const double legacyBrightness = qBound(
                        0.0,
                        render.value("background_brightness").toDouble(state_.previewBackgroundBrightnessOuter_),
                        1.0
                    );
                    if (render.value("background_brightness_outer").isDouble()) {
                        state_.previewBackgroundBrightnessOuter_ =
                            qBound(0.0, render.value("background_brightness_outer").toDouble(legacyBrightness), 1.0);
                    } else {
                        state_.previewBackgroundBrightnessOuter_ = legacyBrightness;
                    }
                    if (render.value("background_brightness_inner").isDouble()) {
                        state_.previewBackgroundBrightnessInner_ = qBound(
                            0.0,
                            render.value("background_brightness_inner").toDouble(state_.previewBackgroundBrightnessOuter_),
                            1.0
                        );
                    } else {
                        state_.previewBackgroundBrightnessInner_ = state_.previewBackgroundBrightnessOuter_;
                    }
                    if (render.value("layout_square_scale").isDouble()) {
                        state_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(
                            render.value("layout_square_scale").toDouble(state_.previewLayoutSquareScale_)
                        );
                    }
                    if (render.value("smooth_brightness").isBool()) {
                        state_.previewSmoothBrightness_ = render.value("smooth_brightness").toBool(state_.previewSmoothBrightness_);
                    }
                    const QString scaleMode = render.value("background_scale_mode").toString().trimmed().toLower();
                    if (scaleMode == QLatin1String("fit") || scaleMode == QLatin1String("contain")) {
                        state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
                    } else if (!scaleMode.isEmpty()) {
                        state_.previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
                    }
                    const double legacyFlowSpeed = render.value("note_flow_speed").toDouble(
                        miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed
                    );
                    if (render.value("tap_flow_speed").isDouble()) {
                        state_.previewTapFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(
                            render.value("tap_flow_speed").toDouble(state_.previewTapFlowSpeed_)
                        );
                    } else if (render.value("note_flow_speed").isDouble()) {
                        state_.previewTapFlowSpeed_ =
                            miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(legacyFlowSpeed);
                    }
                    if (render.value("touch_flow_speed").isDouble()) {
                        state_.previewTouchFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(
                            render.value("touch_flow_speed").toDouble(state_.previewTouchFlowSpeed_)
                        );
                    } else if (render.value("note_flow_speed").isDouble()) {
                        state_.previewTouchFlowSpeed_ =
                            miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(legacyFlowSpeed);
                    }
                    if (render.value("slide_earlier_second_and_text_on_top").isBool()) {
                        state_.previewSlideEarlierSecondAndTextOnTop_ =
                            render.value("slide_earlier_second_and_text_on_top")
                                .toBool(state_.previewSlideEarlierSecondAndTextOnTop_);
                    }
                    if (render.value("skin_variant").isString()) {
                        const QString skinValue = render.value("skin_variant").toString().trimmed();
                        state_.previewSkinVariant_ = owner_.previewSkinVariantFromStorageValue(skinValue);
                        state_.previewSkinDirectoryName_ =
                            state_.previewSkinVariant_ == PreviewSkinVariant::Dx ? QStringLiteral("skinDX") : QStringLiteral("skinSTD");
                        const QString normalizedSkinValue = skinValue.toLower();
                        if (!skinValue.isEmpty()
                            && normalizedSkinValue != QLatin1String("standard")
                            && normalizedSkinValue != QLatin1String("std")
                            && normalizedSkinValue != QLatin1String("skin")
                            && normalizedSkinValue != QLatin1String("skinstd")
                            && normalizedSkinValue != QLatin1String("dx")
                            && normalizedSkinValue != QLatin1String("skin_dx")
                            && normalizedSkinValue != QLatin1String("skindx")) {
                            state_.previewSkinDirectoryName_ = skinValue;
                        }
                    }
                    if (render.value("outline_variant").isString()) {
                        const QString outlineVariant = render.value("outline_variant").toString().trimmed();
                        if (!outlineVariant.isEmpty()) {
                            state_.previewOutlineVariant_ = owner_.previewOutlineVariantFromStorageValue(outlineVariant);
                            state_.previewOutlineVariantUsesAutoSelection_ = false;
                        }
                    }
                    if (render.value("custom_outline_file").isString()) {
                        state_.previewCustomOutlineFileName_ =
                            QFileInfo(render.value("custom_outline_file").toString().trimmed()).fileName();
                        if (!state_.previewCustomOutlineFileName_.isEmpty()) {
                            state_.previewOutlineVariantUsesAutoSelection_ = false;
                        }
                    }
                    if (render.value("render_mode").isString()) {
                        state_.muriRenderOptions_.renderMode = renderModeFromToken(render.value("render_mode").toString());
                    }
                    if (render.value("show_chart_review_slide_judge_overlay").isBool()) {
                        state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay =
                            render.value("show_chart_review_slide_judge_overlay")
                                .toBool(state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay);
                    }
                    if (render.value("show_chart_review_tap_judge_overlay").isBool()) {
                        state_.muriRenderOptions_.showChartReviewTapJudgeOverlay =
                            render.value("show_chart_review_tap_judge_overlay")
                                .toBool(state_.muriRenderOptions_.showChartReviewTapJudgeOverlay);
                    }
                    if (render.value("show_chart_review_touch_judge_overlay").isBool()) {
                        state_.muriRenderOptions_.showChartReviewTouchJudgeOverlay =
                            render.value("show_chart_review_touch_judge_overlay")
                                .toBool(state_.muriRenderOptions_.showChartReviewTouchJudgeOverlay);
                    }
                    if (render.value("wifi_need_c").isBool()) {
                        state_.muriRenderOptions_.wifiNeedC =
                            render.value("wifi_need_c").toBool(state_.muriRenderOptions_.wifiNeedC);
                    }
                    state_.muriRenderOptions_.excludeTouchFromMultiTouch = true;
                    if (render.value("show_slide_tracks").isBool()) {
                        state_.showSlideTracks_ = render.value("show_slide_tracks").toBool(state_.showSlideTracks_);
                    }
                    if (render.value("show_judge_markers").isBool()) {
                        state_.showJudgeMarkers_ =
                            render.value("show_judge_markers").toBool(state_.showJudgeMarkers_);
                    }
                    if (render.value("show_touch_trail").isBool()) {
                        state_.showTouchTrail_ = render.value("show_touch_trail").toBool(state_.showTouchTrail_);
                    }
                    if (render.value("canvas_frame_rate_mode").isString()) {
                        state_.previewCanvasFrameRateMode_ =
                            owner_.previewCanvasFrameRateModeFromStorageValue(render.value("canvas_frame_rate_mode").toString());
                    }
                    if (render.value("force_labeled_judge_line_when_paused").isBool()) {
                        state_.previewForceLabeledJudgeLineWhenPaused_ =
                            render.value("force_labeled_judge_line_when_paused")
                                .toBool(state_.previewForceLabeledJudgeLineWhenPaused_);
                    } else if (render.value("hide_stage_media_when_paused").isBool()) {
                        state_.previewForceLabeledJudgeLineWhenPaused_ =
                            render.value("hide_stage_media_when_paused")
                                .toBool(state_.previewForceLabeledJudgeLineWhenPaused_);
                    }
                    if (render.value("show_debug_info").isBool()) {
                        state_.previewShowDebugInfo_ = render.value("show_debug_info").toBool(state_.previewShowDebugInfo_);
                    }
                    if (render.value("show_timestamp").isBool()) {
                        state_.previewShowTimestamp_ = render.value("show_timestamp").toBool(state_.previewShowTimestamp_);
                    }
                    if (render.value("show_object_stats_preview").isBool()) {
                        state_.previewShowObjectStatsHud_ =
                            render.value("show_object_stats_preview").toBool(state_.previewShowObjectStatsHud_);
                    }
                    if (render.value("show_object_stats_export").isBool()) {
                        state_.exportShowObjectStatsHud_ =
                            render.value("show_object_stats_export").toBool(state_.exportShowObjectStatsHud_);
                    }
                    if (render.value("show_validation_summary").isBool()) {
                        state_.previewShowValidationSummary_ =
                            render.value("show_validation_summary").toBool(state_.previewShowValidationSummary_);
                    }
                    if (render.value("follow_preview").isBool()) {
                        state_.previewFollowEnabled_ =
                            render.value("follow_preview").toBool(state_.previewFollowEnabled_);
                    }
                    if (render.value("follow_progress").isBool()) {
                        state_.previewProgressFollowEnabled_ =
                            render.value("follow_progress").toBool(state_.previewProgressFollowEnabled_);
                    }
                    if (render.value("timeline_zoom_scale").isDouble()) {
                        projectTimelineZoomScaleLoaded = true;
                        projectTimelineZoomScale = render.value("timeline_zoom_scale").toDouble(projectTimelineZoomScale);
                    }
                    const miacode::chart_transform::ChartNormalizationOptions normalizationOptions =
                        miacode::chart_transform::chartNormalizationOptionsFromPreferences(
                            render,
                            miacode::chart_transform::ChartNormalizationOptions{
                                state_.chartNormalizeStartAtNewMeasure_,
                                state_.chartNormalizeReduceTo384Grid_});
                    state_.chartNormalizeStartAtNewMeasure_ = normalizationOptions.startAtNewMeasure;
                    state_.chartNormalizeReduceTo384Grid_ = normalizationOptions.reduceTo384Grid;
                    if (render.value("swap_side_panels").isBool()) {
                        state_.workspacePanelsSwapped_ = render.value("swap_side_panels").toBool(state_.workspacePanelsSwapped_);
                    }
                    const bool unifiedObjectStatsHud = state_.previewShowObjectStatsHud_ || state_.exportShowObjectStatsHud_;
                    state_.previewShowObjectStatsHud_ = unifiedObjectStatsHud;
                    state_.exportShowObjectStatsHud_ = unifiedObjectStatsHud;
                    state_.previewAutoRestoreSquareAfterExport_ = false;
                    owner_.setPreviewCanvasAspectRatio(1.0, false);
                }
                const int savedDifficultyId = root.value("last_opened_difficulty").toInt(0);
                if (SimaiDocument::isDifficultyId(savedDifficultyId)) {
                    state_.projectLastOpenedDifficultyId_ = savedDifficultyId;
                }
            }
        }
    }
    if (state_.previewOutlineVariantUsesAutoSelection_) {
        state_.previewOutlineVariant_ = owner_.autoPreviewOutlineVariantForChart(state_.currentFilePath_);
    }
    if (qAbs(state_.previewCanvasAspectRatio_ - previousCanvasAspectRatio) > 1e-6) {
        if (state_.previewCanvasAspectRatio_ + 1e-6 < previousCanvasAspectRatio) {
            owner_.updatePreviewWorkspaceLayout();
        } else {
            owner_.updatePreviewPanelLayout();
        }
    }
    state_.previewAudioSettings_.normalize();
    state_.previewTimingSettings_.normalize();
    if (state_.timelineQuickStateBridge_ != nullptr) {
        if (projectTimelineZoomScaleLoaded) {
            state_.timelineQuickStateBridge_->setZoomScale(projectTimelineZoomScale);
        }
        state_.timelineQuickStateBridge_->setFollowPreviewEnabled(state_.previewFollowEnabled_);
        state_.timelineQuickStateBridge_->setFollowProgressEnabled(state_.previewProgressFollowEnabled_);
    }
    owner_.refreshPreviewFrameRateTimers();
    owner_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_);
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->setBackgroundTrackPlaybackRate(state_.previewPlaybackRate_);
    }
    owner_.applyPreviewStageMediaRouteVisualSettings();
    if (state_.previewCanvas_ != nullptr) {
        owner_.applyEffectivePreviewOutlineVariantToCanvas();
        state_.previewCanvas_->setSkinDirectory(owner_.resolvePreviewSkinDir());
        state_.previewCanvas_->setBackgroundBrightnessOuter(state_.previewBackgroundBrightnessOuter_);
        state_.previewCanvas_->setBackgroundBrightnessInner(state_.previewBackgroundBrightnessInner_);
        state_.previewCanvas_->setLayoutSquareScale(state_.previewLayoutSquareScale_);
        state_.previewCanvas_->setSmoothBrightness(state_.previewSmoothBrightness_);
        state_.previewCanvas_->setBackgroundScaleMode(state_.previewBackgroundScaleMode_);
        state_.previewCanvas_->setTapFlowSpeed(state_.previewTapFlowSpeed_);
        state_.previewCanvas_->setTouchFlowSpeed(state_.previewTouchFlowSpeed_);
        state_.previewCanvas_->setSlideEarlierSecondAndTextOnTop(state_.previewSlideEarlierSecondAndTextOnTop_);
        state_.previewCanvas_->setShowDebugInfo(state_.previewShowDebugInfo_);
        state_.previewCanvas_->setShowTimestamp(state_.previewShowTimestamp_);
        state_.previewCanvas_->setShowObjectStatsHud(state_.previewShowObjectStatsHud_);
    }
    owner_.applyMuriRenderOptions();
}

void MainWindow::EditorSection::saveProjectRenderState() const
{
    const QString path = resolveProjectRenderStateFilePath();
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo pathInfo(path);
    if (!QDir().mkpath(pathInfo.absolutePath())) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QJsonObject root;
    root.insert("audio", state_.previewAudioSettings_.toJson());
    root.insert("timing", state_.previewTimingSettings_.toJson());
    QJsonObject render;
    render.insert("preview_playback_rate", state_.previewPlaybackRate_);
    render.insert("show_slide_tracks", state_.showSlideTracks_);
    render.insert("show_judge_markers", state_.showJudgeMarkers_);
    render.insert("show_touch_trail", state_.showTouchTrail_);
    render.insert("background_brightness", state_.previewBackgroundBrightnessOuter_);
    render.insert("background_brightness_outer", state_.previewBackgroundBrightnessOuter_);
    render.insert("background_brightness_inner", state_.previewBackgroundBrightnessInner_);
    render.insert("layout_square_scale", state_.previewLayoutSquareScale_);
    render.insert("smooth_brightness", state_.previewSmoothBrightness_);
    render.insert(
        "background_scale_mode",
        state_.previewBackgroundScaleMode_ == PreviewBackgroundScaleMode::FitContain
            ? QStringLiteral("fit")
            : QStringLiteral("fill")
    );
    render.insert("tap_flow_speed", state_.previewTapFlowSpeed_);
    render.insert("touch_flow_speed", state_.previewTouchFlowSpeed_);
    render.insert("slide_earlier_second_and_text_on_top", state_.previewSlideEarlierSecondAndTextOnTop_);
    render.insert("skin_variant", owner_.previewSkinVariantStorageValue());
    if (state_.previewOutlineVariantUsesAutoSelection_) {
        render.remove("outline_variant");
        render.remove("custom_outline_file");
    } else {
        render.insert("outline_variant", owner_.previewOutlineVariantStorageValue());
        if (state_.previewCustomOutlineFileName_.isEmpty()) {
            render.remove("custom_outline_file");
        } else {
            render.insert("custom_outline_file", state_.previewCustomOutlineFileName_);
        }
    }
    render.insert("render_mode", renderModeToken(state_.muriRenderOptions_.renderMode));
    render.insert("show_chart_review_slide_judge_overlay", state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    render.insert("show_chart_review_tap_judge_overlay", state_.muriRenderOptions_.showChartReviewTapJudgeOverlay);
    render.insert("show_chart_review_touch_judge_overlay", state_.muriRenderOptions_.showChartReviewTouchJudgeOverlay);
    render.insert("wifi_need_c", state_.muriRenderOptions_.wifiNeedC);
    render.insert("canvas_frame_rate_mode", owner_.previewCanvasFrameRateModeStorageValue());
    render.insert(
        "force_labeled_judge_line_when_paused",
        state_.previewForceLabeledJudgeLineWhenPaused_
    );
    render.insert("show_debug_info", state_.previewShowDebugInfo_);
    render.insert("show_timestamp", state_.previewShowTimestamp_);
    render.insert("show_object_stats_preview", state_.previewShowObjectStatsHud_);
    render.insert("show_object_stats_export", state_.exportShowObjectStatsHud_);
    render.insert("show_validation_summary", state_.previewShowValidationSummary_);
    render.insert("follow_preview", state_.previewFollowEnabled_);
    render.insert("follow_progress", state_.previewProgressFollowEnabled_);
    render.insert(
        "timeline_zoom_scale",
        state_.timelineQuickStateBridge_ != nullptr ? state_.timelineQuickStateBridge_->zoomScale() : 0.5
    );
    miacode::chart_transform::saveChartNormalizationOptionsToPreferences(
        &render,
        miacode::chart_transform::ChartNormalizationOptions{
            state_.chartNormalizeStartAtNewMeasure_,
            state_.chartNormalizeReduceTo384Grid_});
    render.insert("swap_side_panels", state_.workspacePanelsSwapped_);
    render.insert("canvas_aspect_ratio", 1.0);
    render.insert("auto_restore_square_after_export", false);
    root.insert("render", render);
    root.insert("last_opened_difficulty", state_.projectLastOpenedDifficultyId_);
    root.insert("schema", "miacode_settings_v1");
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        return;
    }
    if (!file.commit()) {
        return;
    }

    const QString legacyPath = legacyProjectRenderStateFilePath(state_.currentFilePath_);
    if (!legacyPath.isEmpty() && legacyPath != path) {
        QFile::remove(legacyPath);
    }
}

void MainWindow::EditorSection::removeProjectRenderState() const
{
    const QString path = resolveProjectRenderStateFilePath();
    if (path.isEmpty()) {
        return;
    }
    QFile::remove(path);
    const QString legacyPath = legacyProjectRenderStateFilePath(state_.currentFilePath_);
    if (!legacyPath.isEmpty() && legacyPath != path) {
        QFile::remove(legacyPath);
    }
}

QString MainWindow::resolveProjectRenderStateFilePath() const
{
    return editorSection_->resolveProjectRenderStateFilePath();
}

void MainWindow::loadProjectRenderState()
{
    editorSection_->loadProjectRenderState();
}

void MainWindow::saveProjectRenderState() const
{
    editorSection_->saveProjectRenderState();
}

void MainWindow::removeProjectRenderState() const
{
    editorSection_->removeProjectRenderState();
}
