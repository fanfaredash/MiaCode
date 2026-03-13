void MainWindow::loadPortableState()
{
    lastOpenDir_.clear();
    softwarePreviewAudioSettings_ = PreviewAudioSettings();
    previewAudioSettings_ = softwarePreviewAudioSettings_;
    showSlideTracks_ = true;
    showJudgeMarkers_ = false;
    showTouchTrail_ = false;
    previewBackgroundBrightnessOuter_ = 0.2;
    previewBackgroundBrightnessInner_ = 0.2;
    previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    previewShowDebugInfo_ = false;
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
    const QString trackPath = app.value("last_track_path").toString();
    if (!trackPath.isEmpty() && QFileInfo::exists(trackPath)) {
        lastTrackPath_ = QDir::cleanPath(trackPath);
    }
    showSlideTracks_ = true;
    if (preview.value("show_judge_markers").isBool()) {
        showJudgeMarkers_ = preview.value("show_judge_markers").toBool(false);
    }
    if (preview.value("show_touch_trail").isBool()) {
        showTouchTrail_ = preview.value("show_touch_trail").toBool(false);
    }
    const double legacyBrightness = qBound(0.0, preview.value("background_brightness").toDouble(0.2), 1.0);
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
    const QString scaleMode = preview.value("background_scale_mode").toString().trimmed().toLower();
    if (scaleMode == QLatin1String("fit") || scaleMode == QLatin1String("contain")) {
        previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
    } else {
        previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
    }
    if (preview.value("show_debug_info").isBool()) {
        previewShowDebugInfo_ = preview.value("show_debug_info").toBool(false);
    }
    if (preview.value("audio").isObject()) {
        softwarePreviewAudioSettings_ = PreviewAudioSettings::fromJson(preview.value("audio").toObject());
    } else {
        softwarePreviewAudioSettings_.bgmVolume = preview.value("bgm_volume").toDouble(softwarePreviewAudioSettings_.bgmVolume);
        const double legacyAnswer = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.answerVolume);
        const double legacySlide = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.slideVolume);
        const double legacyBreak = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.breakVolume);
        const double legacyEx = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.exVolume);
        const double legacyTouch = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.touchVolume);
        const double legacyTouchhold = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.touchholdVolume);
        const double legacyFirework = preview.value("sfx_volume").toDouble(softwarePreviewAudioSettings_.fireworkVolume);
        softwarePreviewAudioSettings_.answerVolume = preview.value("answer_volume").toDouble(legacyAnswer);
        softwarePreviewAudioSettings_.slideVolume = preview.value("slide_volume").toDouble(legacySlide);
        softwarePreviewAudioSettings_.breakVolume = preview.value("break_volume").toDouble(legacyBreak);
        softwarePreviewAudioSettings_.exVolume = preview.value("ex_volume").toDouble(legacyEx);
        softwarePreviewAudioSettings_.touchVolume = preview.value("touch_volume").toDouble(legacyTouch);
        softwarePreviewAudioSettings_.touchholdVolume = preview.value("touchhold_volume").toDouble(legacyTouchhold);
        softwarePreviewAudioSettings_.fireworkVolume = preview.value("firework_volume").toDouble(legacyFirework);
        softwarePreviewAudioSettings_.normalize();
    }
    previewAudioSettings_ = softwarePreviewAudioSettings_;
}

void MainWindow::savePortableState() const
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value("ui").toObject();
    QJsonObject app = root.value("app").toObject();
    QJsonObject preview = app.value("preview").toObject();

    ui.insert("editor_text_font_size", editorTextFontPointSize_);
    ui.insert("editor_line_spacing_factor", editorLineSpacingFactor_);
    root.insert("ui", ui);

    app.insert("last_open_dir", lastOpenDir_);
    app.insert("last_track_path", lastTrackPath_);
    app.insert("show_slide_tracks", true);

    preview.insert("show_judge_markers", showJudgeMarkers_);
    preview.insert("show_touch_trail", showTouchTrail_);
    preview.insert("background_brightness", previewBackgroundBrightnessOuter_);
    preview.insert("background_brightness_outer", previewBackgroundBrightnessOuter_);
    preview.insert("background_brightness_inner", previewBackgroundBrightnessInner_);
    preview.insert(
        "background_scale_mode",
        previewBackgroundScaleMode_ == PreviewBackgroundScaleMode::FitContain
            ? QStringLiteral("fit")
            : QStringLiteral("fill")
    );
    preview.insert("show_debug_info", previewShowDebugInfo_);
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
    editorTextFontPointSize_ = normalized;
    if (auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_); editor != nullptr) {
        QSignalBlocker blocker(editor);
        editor->setFont(editorFont(normalized));
        editor->setBlockSpacingPixels(blockSpacingPixels);
        editor->refreshLineNumberAreaLayout();
    }
    if (metadataExtraEdit_ != nullptr) {
        metadataExtraEdit_->setFont(editorFont(normalized));
        applyBlockSpacingToTextEdit(metadataExtraEdit_, blockSpacingPixels);
    }
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

void MainWindow::applyEditorLineSpacingFactor(double factor, bool persistPreference)
{
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
    if (persistPreference) {
        persistEditorTextFontPreference();
    }
}

