void MainWindow::loadPortableState()
{
    lastSessionFilePath_.clear();
    lastOpenDir_.clear();
    lastTrackPath_.clear();
    autoRestoreLastSessionFile_ = true;
    resetPortablePreviewSettingsToDefaults();
    editorLineSpacingFactor_ = kEditorLineSpacingFactorDefault;
    editorTextFontPointSize_ = qBound(
        kEditorTextFontSizeMin,
        editorTextFontPointSize_ > 0 ? editorTextFontPointSize_ : editorFont().pointSize(),
        kEditorTextFontSizeMax
    );

    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject ui = root.value("ui").toObject();
    const QJsonObject app = root.value("app").toObject();
    const QJsonObject preview = app.value("preview").toObject();

    if (ui.value("editor_text_font_size").isDouble()) {
        editorTextFontPointSize_ = qBound(
            kEditorTextFontSizeMin,
            qRound(ui.value("editor_text_font_size").toDouble(editorTextFontPointSize_)),
            kEditorTextFontSizeMax
        );
    }
    if (ui.value("editor_line_spacing_factor").isDouble()) {
        editorLineSpacingFactor_ = normalizeEditorLineSpacingFactor(
            ui.value("editor_line_spacing_factor").toDouble(editorLineSpacingFactor_)
        );
    }
    applyEditorTextFontSize(editorTextFontPointSize_, false);

    const QString dir = app.value("last_open_dir").toString();
    if (!dir.isEmpty() && QDir(dir).exists()) {
        lastOpenDir_ = QDir::cleanPath(dir);
    }
    const QString lastOpenFile = app.value("last_open_file").toString();
    if (!lastOpenFile.isEmpty()) {
        lastSessionFilePath_ = QDir::cleanPath(lastOpenFile);
    }
    const QString trackPath = app.value("last_track_path").toString();
    if (!trackPath.isEmpty() && QFileInfo::exists(trackPath)) {
        lastTrackPath_ = QDir::cleanPath(trackPath);
    }
    applyPortablePreviewSettings(preview);
    refreshPreviewFrameRateTimers();
}

void MainWindow::resetPortablePreviewSettingsToDefaults()
{
    softwarePreviewAudioSettings_ = PreviewAudioSettings();
    previewAudioSettings_ = softwarePreviewAudioSettings_;
    showSlideTracks_ = true;
    showJudgeMarkers_ = false;
    showTouchTrail_ = false;
    muriRenderOptions_ = MuriRenderOptions();
    staticTapOnSlideThresholdMs_ = miacode::muri::kStaticTapOnSlideThresholdDefaultMs;
    previewBackgroundBrightnessOuter_ = miacode::preview_video::kBackgroundBrightnessDefault;
    previewBackgroundBrightnessInner_ = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    previewLayoutSquareScale_ = miacode::preview_video::kLayoutSquareScaleDefault;
    previewSmoothBrightness_ = miacode::preview_video::kSmoothBrightnessDefault;
    previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    previewNoteFlowSpeed_ = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    previewCanvasFrameRateMode_ = PreviewCanvasFrameRateMode::DisplayRefresh;
    previewCanvasAspectRatio_ = 1.0;
    previewAutoRestoreSquareAfterExport_ = false;
    previewShowDebugInfo_ = false;
    previewShowTimestamp_ = true;
    previewShowObjectStatsHud_ = false;
    exportShowObjectStatsHud_ = false;
    previewShowValidationSummary_ = true;
    workspacePanelsSwapped_ = false;
}

void MainWindow::applyPortablePreviewSettings(const QJsonObject& preview)
{
    if (preview.value("static_tap_on_slide_threshold_ms").isDouble()) {
        staticTapOnSlideThresholdMs_ = qBound(
            miacode::muri::kStaticTapOnSlideThresholdMinMs,
            qRound(preview.value("static_tap_on_slide_threshold_ms")
                       .toDouble(staticTapOnSlideThresholdMs_)),
            miacode::muri::kStaticTapOnSlideThresholdMaxMs
        );
    }
    const QString muriRenderMode = preview.value("muri_render_mode").toString().trimmed().toLower();
    muriRenderOptions_.renderMode =
        muriRenderMode == QLatin1String("maimuri_dx_style")
        ? RenderMode::MaimuriDxStyle
        : RenderMode::Native;
    if (preview.value("show_chart_review_slide_judge_overlay").isBool()) {
        muriRenderOptions_.showChartReviewSlideJudgeOverlay =
            preview.value("show_chart_review_slide_judge_overlay")
                .toBool(muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    }
    if (preview.value("show_chart_review_simple_judge_overlay").isBool()) {
        muriRenderOptions_.showChartReviewSimpleJudgeOverlay =
            preview.value("show_chart_review_simple_judge_overlay")
                .toBool(muriRenderOptions_.showChartReviewSimpleJudgeOverlay);
    }
    if (preview.value("wifi_need_c").isBool()) {
        muriRenderOptions_.wifiNeedC = preview.value("wifi_need_c").toBool(muriRenderOptions_.wifiNeedC);
    }
    const double legacyBrightness = qBound(
        0.0,
        preview.value("background_brightness").toDouble(miacode::preview_video::kBackgroundBrightnessDefault),
        1.0
    );
    if (preview.value("background_brightness_outer").isDouble()) {
        previewBackgroundBrightnessOuter_ =
            qBound(0.0, preview.value("background_brightness_outer").toDouble(legacyBrightness), 1.0);
    } else {
        previewBackgroundBrightnessOuter_ = legacyBrightness;
    }
    if (preview.value("background_brightness_inner").isDouble()) {
        previewBackgroundBrightnessInner_ =
            qBound(0.0, preview.value("background_brightness_inner").toDouble(previewBackgroundBrightnessOuter_), 1.0);
    } else {
        previewBackgroundBrightnessInner_ = previewBackgroundBrightnessOuter_;
    }
    if (preview.value("layout_square_scale").isDouble()) {
        previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(
            preview.value("layout_square_scale").toDouble(previewLayoutSquareScale_)
        );
    }
    if (preview.value("smooth_brightness").isBool()) {
        previewSmoothBrightness_ = preview.value("smooth_brightness").toBool(previewSmoothBrightness_);
    }
    const QString scaleMode = preview.value("background_scale_mode").toString().trimmed().toLower();
    if (scaleMode == QLatin1String("fit") || scaleMode == QLatin1String("contain")) {
        previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
    } else if (!scaleMode.isEmpty()) {
        previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    }
    if (preview.value("note_flow_speed").isDouble()) {
        previewNoteFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(
            preview.value("note_flow_speed").toDouble(previewNoteFlowSpeed_)
        );
    }
    if (preview.value("canvas_frame_rate_mode").isString()) {
        previewCanvasFrameRateMode_ =
            previewCanvasFrameRateModeFromStorageValue(preview.value("canvas_frame_rate_mode").toString());
    }
    if (preview.value("show_debug_info").isBool()) {
        previewShowDebugInfo_ = preview.value("show_debug_info").toBool(false);
    }
    if (preview.value("show_timestamp").isBool()) {
        previewShowTimestamp_ = preview.value("show_timestamp").toBool(true);
    }
    if (preview.value("show_object_stats_preview").isBool()) {
        previewShowObjectStatsHud_ = preview.value("show_object_stats_preview").toBool(false);
    }
    if (preview.value("show_object_stats_export").isBool()) {
        exportShowObjectStatsHud_ = preview.value("show_object_stats_export").toBool(false);
    }
    const bool unifiedObjectStatsHud = previewShowObjectStatsHud_ || exportShowObjectStatsHud_;
    previewShowObjectStatsHud_ = unifiedObjectStatsHud;
    exportShowObjectStatsHud_ = unifiedObjectStatsHud;
    if (preview.value("show_validation_summary").isBool()) {
        previewShowValidationSummary_ = preview.value("show_validation_summary").toBool(true);
    }
    if (preview.value("swap_side_panels").isBool()) {
        workspacePanelsSwapped_ = preview.value("swap_side_panels").toBool(false);
    }
    previewCanvasAspectRatio_ = 1.0;
    previewAutoRestoreSquareAfterExport_ = false;
    if (preview.value("audio").isObject()) {
        softwarePreviewAudioSettings_ = PreviewAudioSettings::fromJson(preview.value("audio").toObject());
    } else {
        softwarePreviewAudioSettings_ = PreviewAudioSettings::fromJson(preview);
    }
    softwarePreviewAudioSettings_.normalize();
    previewAudioSettings_ = softwarePreviewAudioSettings_;
}

void MainWindow::savePortableState() const
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value("ui").toObject();
    QJsonObject app = root.value("app").toObject();
    QJsonObject preview = app.value("preview").toObject();
    preview.remove("show_judge_markers");
    preview.remove("show_touch_trail");

    ui.insert("editor_text_font_size", editorTextFontPointSize_);
    ui.insert("editor_line_spacing_factor", editorLineSpacingFactor_);
    root.insert("ui", ui);

    app.insert("last_open_dir", lastOpenDir_);
    app.insert("last_open_file", lastSessionFilePath_);
    app.insert("auto_restore_last_open_file", true);
    app.insert("last_track_path", lastTrackPath_);
    app.insert("show_slide_tracks", true);

    preview.insert("static_tap_on_slide_threshold_ms", staticTapOnSlideThresholdMs_);
    preview.insert(
        "muri_render_mode",
        muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle
            ? QStringLiteral("maimuri_dx_style")
            : QStringLiteral("native")
    );
    preview.insert("show_chart_review_slide_judge_overlay", muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    preview.insert("show_chart_review_simple_judge_overlay", muriRenderOptions_.showChartReviewSimpleJudgeOverlay);
    preview.insert("wifi_need_c", muriRenderOptions_.wifiNeedC);
    preview.insert("background_brightness", previewBackgroundBrightnessOuter_);
    preview.insert("background_brightness_outer", previewBackgroundBrightnessOuter_);
    preview.insert("background_brightness_inner", previewBackgroundBrightnessInner_);
    preview.insert("layout_square_scale", previewLayoutSquareScale_);
    preview.insert("smooth_brightness", previewSmoothBrightness_);
    preview.insert(
        "background_scale_mode",
        previewBackgroundScaleMode_ == PreviewBackgroundScaleMode::FitContain
            ? QStringLiteral("fit")
            : QStringLiteral("fill")
    );
    preview.insert("note_flow_speed", previewNoteFlowSpeed_);
    preview.insert("canvas_frame_rate_mode", previewCanvasFrameRateModeStorageValue());
    preview.insert("show_debug_info", previewShowDebugInfo_);
    preview.insert("show_timestamp", previewShowTimestamp_);
    preview.insert("show_object_stats_preview", previewShowObjectStatsHud_);
    preview.insert("show_object_stats_export", exportShowObjectStatsHud_);
    preview.insert("show_validation_summary", previewShowValidationSummary_);
    preview.insert("swap_side_panels", workspacePanelsSwapped_);
    preview.insert("canvas_aspect_ratio", 1.0);
    preview.insert("auto_restore_square_after_export", false);
    preview.insert("audio", softwarePreviewAudioSettings_.toJson());

    app.insert("preview", preview);
    root.insert("app", app);
    UiText::savePreferencesObject(root);
}

void MainWindow::persistEditorTextFontPreference() const
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value("ui").toObject();
    ui.insert("editor_text_font_size", editorTextFontPointSize_);
    ui.insert("editor_line_spacing_factor", editorLineSpacingFactor_);
    root.insert("ui", ui);
    UiText::savePreferencesObject(root);
}

void MainWindow::applyEditorTextFontSize(int pointSize, bool persistPreference)
{
    const int normalized = qBound(kEditorTextFontSizeMin, pointSize, kEditorTextFontSizeMax);
    const int blockSpacingPixels = blockSpacingPixelsForPointSize(normalized, editorLineSpacingFactor_);
    const bool previousSuppress = suppressTextDirtyTracking_;
    suppressTextDirtyTracking_ = true;
    editorTextFontPointSize_ = normalized;
    if (auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_); editor != nullptr) {
        QSignalBlocker blocker(editor);
        editor->setFont(editorFont(normalized));
        editor->setBlockSpacingPixels(blockSpacingPixels);
        editor->refreshLineNumberAreaLayout();
    }
    if (timelineView_ != nullptr) {
        QFont timelineHeaderLineNumberFont = editorFont(normalized);
        timelineHeaderLineNumberFont.setPointSize(qMax(timelineHeaderLineNumberFont.pointSize() + 1, 12));
        timelineView_->setHeaderLineNumberFont(timelineHeaderLineNumberFont);
        updateBottomTabsDeviceHeight();
    }
    if (metadataExtraEdit_ != nullptr) {
        metadataExtraEdit_->setFont(editorFont(normalized));
        applyBlockSpacingToTextEdit(metadataExtraEdit_, blockSpacingPixels);
    }
    suppressTextDirtyTracking_ = previousSuppress;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::applyEditorLineSpacingFactor(double factor, bool persistPreference)
{
    const bool previousSuppress = suppressTextDirtyTracking_;
    suppressTextDirtyTracking_ = true;
    editorLineSpacingFactor_ = normalizeEditorLineSpacingFactor(factor);
    const int blockSpacingPixels = blockSpacingPixelsForPointSize(editorTextFontPointSize_, editorLineSpacingFactor_);
    if (auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_); editor != nullptr) {
        QSignalBlocker blocker(editor);
        editor->setBlockSpacingPixels(blockSpacingPixels);
        editor->refreshLineNumberAreaLayout();
    }
    if (metadataExtraEdit_ != nullptr) {
        applyBlockSpacingToTextEdit(metadataExtraEdit_, blockSpacingPixels);
    }
    suppressTextDirtyTracking_ = previousSuppress;
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}
