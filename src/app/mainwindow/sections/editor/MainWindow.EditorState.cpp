#include "MainWindow.EditorSection.h"
#include "../../MainWindowShared.h"

#include "UiText.h"
#include "preview/runtime/PreviewRuntime.h"

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
                const QJsonObject render = root.value("render").toObject();
                if (!render.isEmpty()) {
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
                    if (render.value("note_flow_speed").isDouble()) {
                        state_.previewNoteFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(
                            render.value("note_flow_speed").toDouble(state_.previewNoteFlowSpeed_)
                        );
                    }
                    if (render.value("skin_variant").isString()) {
                        state_.previewSkinVariant_ = owner_.previewSkinVariantFromStorageValue(
                            render.value("skin_variant").toString()
                        );
                    }
                    if (render.value("outline_variant").isString()) {
                        const QString outlineVariant = render.value("outline_variant").toString().trimmed();
                        if (!outlineVariant.isEmpty()) {
                            state_.previewOutlineVariant_ = owner_.previewOutlineVariantFromStorageValue(outlineVariant);
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
                    if (render.value("show_chart_review_simple_judge_overlay").isBool()) {
                        state_.muriRenderOptions_.showChartReviewSimpleJudgeOverlay =
                            render.value("show_chart_review_simple_judge_overlay")
                                .toBool(state_.muriRenderOptions_.showChartReviewSimpleJudgeOverlay);
                    }
                    if (render.value("wifi_need_c").isBool()) {
                        state_.muriRenderOptions_.wifiNeedC =
                            render.value("wifi_need_c").toBool(state_.muriRenderOptions_.wifiNeedC);
                    }
                    state_.muriRenderOptions_.excludeTouchFromMultiTouch = true;
                    state_.showJudgeMarkers_ = false;
                    state_.showTouchTrail_ = false;
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
    if (ui_.timelineView_ != nullptr) {
        ui_.timelineView_->setFollowPreviewEnabled(state_.previewFollowEnabled_);
    }
    owner_.refreshPreviewFrameRateTimers();
    owner_.applyPreviewStageMediaRouteVisualSettings();
    if (state_.previewCanvas_ != nullptr) {
        owner_.applyEffectivePreviewOutlineVariantToCanvas();
        state_.previewCanvas_->setSkinDirectory(owner_.resolvePreviewSkinDir());
        state_.previewCanvas_->setBackgroundBrightnessOuter(state_.previewBackgroundBrightnessOuter_);
        state_.previewCanvas_->setBackgroundBrightnessInner(state_.previewBackgroundBrightnessInner_);
        state_.previewCanvas_->setLayoutSquareScale(state_.previewLayoutSquareScale_);
        state_.previewCanvas_->setSmoothBrightness(state_.previewSmoothBrightness_);
        state_.previewCanvas_->setBackgroundScaleMode(state_.previewBackgroundScaleMode_);
        state_.previewCanvas_->setNoteFlowSpeed(state_.previewNoteFlowSpeed_);
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
    QJsonObject render;
    render.remove("show_judge_markers");
    render.remove("show_touch_trail");
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
    render.insert("note_flow_speed", state_.previewNoteFlowSpeed_);
    render.insert("skin_variant", owner_.previewSkinVariantStorageValue());
    if (state_.previewOutlineVariantUsesAutoSelection_) {
        render.remove("outline_variant");
    } else {
        render.insert("outline_variant", owner_.previewOutlineVariantStorageValue());
    }
    render.insert("render_mode", renderModeToken(state_.muriRenderOptions_.renderMode));
    render.insert("show_chart_review_slide_judge_overlay", state_.muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    render.insert("show_chart_review_simple_judge_overlay", state_.muriRenderOptions_.showChartReviewSimpleJudgeOverlay);
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
