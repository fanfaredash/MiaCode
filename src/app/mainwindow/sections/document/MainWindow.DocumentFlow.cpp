bool MainWindow::maybeSaveBeforeContinue()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    if (!documentDirty_) {
        return true;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        "Unsaved Changes",
        "Current document has unsaved changes. Save before continue?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
    );
    if (choice == QMessageBox::Save) {
        return onSaveFile();
    }
    return choice == QMessageBox::Discard;
}

bool MainWindow::maybeSaveCurrentFieldChanges()
{
    if (!currentFieldDirty_) {
        return true;
    }

    const QString fieldName = hasActiveDifficulty()
        ? SimaiDocument::difficultyName(activeDifficultyId_)
        : QString("Metadata");
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this,
        "Unsaved Field Changes",
        QString("%1 has unsaved changes. Save before switch?").arg(fieldName),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
    );
    if (choice == QMessageBox::Save) {
        return applyCurrentFieldToDocument();
    }
    if (choice == QMessageBox::Discard) {
        currentFieldDirty_ = false;
        updateDirtyState();
        return true;
    }
    return false;
}

bool MainWindow::applyCurrentFieldToDocument()
{
    bool changed = false;
    bool metadataTimingChanged = false;
    if (hasActiveDifficulty()) {
        SimaiDifficultyData& difficultyData = document_.ensureDifficulty(activeDifficultyId_);
        const QString newLevel = difficultyLevelEdit_ != nullptr ? difficultyLevelEdit_->text() : QString();
        const QString newDesigner = difficultyDesignerEdit_ != nullptr ? difficultyDesignerEdit_->text() : QString();
        const QString newChart = editorText();
        if (difficultyData.level != newLevel || difficultyData.designer != newDesigner || difficultyData.chart != newChart) {
            difficultyData.level = newLevel;
            difficultyData.designer = newDesigner;
            difficultyData.chart = newChart;
            changed = true;
        }
    } else {
        const QString newTitle = titleEdit_ != nullptr ? titleEdit_->text() : QString();
        const QString newArtist = artistEdit_ != nullptr ? artistEdit_->text() : QString();
        const QString newFirst = firstEdit_ != nullptr ? firstEdit_->text() : QString();
        const QString newDesigner = designerEdit_ != nullptr ? designerEdit_->text() : QString();
        const QVector<SimaiRawField> newExtraFields = SimaiDocument::parseRawFields(
            metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
            true
        );
        if (document_.title != newTitle
            || document_.artist != newArtist
            || document_.first != newFirst
            || document_.designer != newDesigner
            || document_.extraFields != newExtraFields) {
            metadataTimingChanged = (document_.first != newFirst);
            document_.title = newTitle;
            document_.artist = newArtist;
            document_.first = newFirst;
            document_.designer = newDesigner;
            document_.extraFields = newExtraFields;
            changed = true;
        }
    }

    currentFieldDirty_ = false;
    if (changed) {
        documentDirty_ = true;
    }
    updateDirtyState();
    updateWindowTitle();
    rebuildFieldSidebar();
    if (metadataTimingChanged) {
        refreshWaveformCache();
        refreshTimelineMetadata();
    }
    return true;
}

void MainWindow::onNewFile()
{
    if (!maybeSaveBeforeContinue()) {
        return;
    }

    const QString targetDirectory = QFileDialog::getExistingDirectory(
        this,
        UiText::isChineseUi() ? QStringLiteral("选择谱面文件夹") : QStringLiteral("Select Chart Folder"),
        resolveInitialOpenDirectory(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (targetDirectory.isEmpty()) {
        return;
    }

    const QString normalizedDirectory = QDir::cleanPath(targetDirectory);
    const QString targetPath = QDir(normalizedDirectory).filePath(QStringLiteral("maidata.txt"));
    if (QFileInfo::exists(targetPath)) {
        const QMessageBox::StandardButton choice = QMessageBox::warning(
            this,
            UiText::isChineseUi() ? QStringLiteral("文件已存在") : QStringLiteral("File Already Exists"),
            UiText::isChineseUi()
                ? QStringLiteral("所选文件夹下已存在 maidata.txt，是否覆盖？")
                : QStringLiteral("maidata.txt already exists in the selected folder. Overwrite it?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (choice != QMessageBox::Yes) {
            return;
        }
    }

    const SimaiDocument newDocument = SimaiDocument::createEmpty();
    QStringEncoder encoder(QStringConverter::Utf8);
    const QByteArray payload = encoder.encode(newDocument.toText());
    QSaveFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Create Failed", "Cannot write file:\n" + targetPath);
        return;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        QMessageBox::critical(this, "Create Failed", "Write failed:\n" + targetPath);
        return;
    }

    loadDocument(newDocument);
    clearValidationCache();
    currentEncoding_ = TextEncoding::Utf8;
    setCurrentFilePath(targetPath);
    refreshValidationPanelForActiveField();
    statusBar()->showMessage(QString("Created: %1").arg(targetPath));
}

void MainWindow::onOpenFile()
{
    logTopLevelWindowSnapshot("open_file_flow/begin");
    logNativeWindowDebug("open_file_flow/begin");
    const bool canContinue = maybeSaveBeforeContinue();
    if (!canContinue) {
        logTopLevelWindowSnapshot("open_file_flow/cancelled_before_dialog");
        logNativeWindowDebug("open_file_flow/cancelled_before_dialog");
        return;
    }

    logWindowGeometryDebug("open_file_before_dialog");
    logTopLevelWindowSnapshot("open_file_before_dialog");
    logNativeWindowDebug("open_file_before_dialog");
    int sampleCount = 0;
    int restoreCount = 0;
    QTimer sampleTimer;
    sampleTimer.setInterval(120);
    sampleTimer.setSingleShot(false);
    connect(&sampleTimer, &QTimer::timeout, this, [this, &sampleCount, &restoreCount]() {
        if (sampleCount >= 80) {
            if (sampleCount == 80 && runtimeDebugOutputEnabled_) {
                appendOutput("window/dialog_watch", "open_file_dialog sample_limit_reached");
            }
            ++sampleCount;
            return;
        }
        ++sampleCount;
#ifdef Q_OS_WIN
        QString restoreDetail;
        if (tryRestoreOwnedNativeFileDialog(reinterpret_cast<HWND>(winId()), &restoreDetail)) {
            ++restoreCount;
            appendOutput("window/dialog_watch", QString("open_file_dialog sample=%1 %2").arg(sampleCount).arg(restoreDetail));
        }
#endif
        if (runtimeDebugOutputEnabled_ && (sampleCount <= 12 || (sampleCount % 5) == 0)) {
            logWindowGeometryDebug("open_file_dialog_poll");
            logNativeWindowDebug(QString("open_file_dialog_poll sample=%1").arg(sampleCount));
        }
    });

    logTopLevelWindowSnapshot("open_file_dialog_exec_begin");
    logNativeWindowDebug("open_file_dialog_exec_begin");
    sampleTimer.start();
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open simai file",
        resolveInitialOpenDirectory(),
        "Simai (*.txt *.simai);;All Files (*.*)",
        nullptr
    );
    sampleTimer.stop();
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "window/dialog_watch",
            QString("open_file_dialog finished selected_empty=%1 samples=%2 restores=%3")
                .arg(path.isEmpty() ? 1 : 0)
                .arg(qMin(sampleCount, 80))
                .arg(restoreCount)
        );
    }
    logTopLevelWindowSnapshot("open_file_dialog_exec_end");
    logNativeWindowDebug("open_file_dialog_exec_end");
    logWindowGeometryDebug("open_file_after_dialog", QString("selected_empty=%1").arg(path.isEmpty() ? 1 : 0));
    logTopLevelWindowSnapshot("open_file_after_dialog");
    logNativeWindowDebug("open_file_after_dialog");
    if (path.isEmpty()) {
        return;
    }
    openFileAtPath(path, true, true);
}

bool MainWindow::openFileAtPath(const QString& path, bool showStatusMessage, bool showErrors)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    setLastOpenDirectory(normalizedPath);

    QFile file(normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (showErrors) {
            QMessageBox::critical(this, "Open Failed", "Cannot open file:\n" + normalizedPath);
        }
        return false;
    }

    const QByteArray bytes = file.readAll();
    QString text;
    TextEncoding encodingUsed = TextEncoding::Utf8;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(bytes.mid(3));
    } else {
        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        text = utf8Decoder.decode(bytes);
        if (utf8Decoder.hasError()) {
            QStringDecoder systemDecoder(QStringConverter::System);
            text = systemDecoder.decode(bytes);
            encodingUsed = TextEncoding::System;
        }
    }

    currentEncoding_ = encodingUsed;
    setCurrentFilePath(normalizedPath);
    loadDocument(SimaiDocument::fromText(text));
    refreshValidationPanelForActiveField();
    refreshWaveformCache();
    if (showStatusMessage) {
        statusBar()->showMessage(
            QString("Opened: %1 (%2)")
                .arg(QFileInfo(normalizedPath).fileName())
                .arg(encodingUsed == TextEncoding::Utf8 ? "UTF-8" : "System encoding")
        );
    }
    return true;
}

bool MainWindow::restoreLastSessionFile()
{
    if (lastSessionFilePath_.isEmpty()) {
        return false;
    }
    const QFileInfo fileInfo(lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        lastSessionFilePath_.clear();
        return false;
    }
    return openFileAtPath(fileInfo.absoluteFilePath(), false, false);
}

bool MainWindow::onSaveFile()
{
    if (currentFilePath_.isEmpty()) {
        return onSaveFileAs();
    }
    return saveToPath(currentFilePath_);
}

bool MainWindow::onSaveFileAs()
{
    logTopLevelWindowSnapshot("save_file_as_dialog/begin");
    logWindowGeometryDebug("save_file_as_before_dialog");
    logNativeWindowDebug("save_file_as_before_dialog");
    int sampleCount = 0;
    int restoreCount = 0;
    QTimer sampleTimer;
    sampleTimer.setInterval(120);
    connect(&sampleTimer, &QTimer::timeout, this, [this, &sampleCount, &restoreCount]() {
        if (sampleCount >= 80) {
            if (sampleCount == 80 && runtimeDebugOutputEnabled_) {
                appendOutput("window/dialog_watch", "save_file_dialog sample_limit_reached");
            }
            ++sampleCount;
            return;
        }
        ++sampleCount;
#ifdef Q_OS_WIN
        QString restoreDetail;
        if (tryRestoreOwnedNativeFileDialog(reinterpret_cast<HWND>(winId()), &restoreDetail)) {
            ++restoreCount;
            appendOutput("window/dialog_watch", QString("save_file_dialog sample=%1 %2").arg(sampleCount).arg(restoreDetail));
        }
#endif
        if (runtimeDebugOutputEnabled_ && (sampleCount <= 12 || (sampleCount % 5) == 0)) {
            logWindowGeometryDebug("save_file_dialog_poll");
            logNativeWindowDebug(QString("save_file_dialog_poll sample=%1").arg(sampleCount));
        }
    });

    logTopLevelWindowSnapshot("save_file_dialog_exec_begin");
    logNativeWindowDebug("save_file_dialog_exec_begin");
    sampleTimer.start();
    const QString path = QFileDialog::getSaveFileName(
        this,
        "Save simai file",
        currentFilePath_.isEmpty() ? QString("chart.txt") : currentFilePath_,
        "Simai (*.txt *.simai);;All Files (*.*)",
        nullptr
    );
    sampleTimer.stop();
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "window/dialog_watch",
            QString("save_file_dialog finished selected_empty=%1 samples=%2 restores=%3")
                .arg(path.isEmpty() ? 1 : 0)
                .arg(qMin(sampleCount, 80))
                .arg(restoreCount)
        );
    }
    logTopLevelWindowSnapshot("save_file_dialog_exec_end");
    logNativeWindowDebug("save_file_dialog_exec_end");
    logWindowGeometryDebug("save_file_as_after_dialog", QString("selected_empty=%1").arg(path.isEmpty() ? 1 : 0));
    logTopLevelWindowSnapshot("save_file_as_dialog/after_dialog");
    logNativeWindowDebug("save_file_as_after_dialog");
    if (path.isEmpty()) {
        return false;
    }
    setLastOpenDirectory(path);
    return saveToPath(path);
}

bool MainWindow::saveToPath(const QString& path)
{
    if (!applyCurrentFieldToDocument()) {
        return false;
    }
    bool firstOk = false;
    (void)parsedFirstSeconds(&firstOk);
    if (!firstOk) {
        QMessageBox::critical(this, "Save Failed", "&first must be a valid number of seconds.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Save Failed", "Cannot write file:\n" + path);
        return false;
    }
    QByteArray data;
    const QString serialized = document_.toText();
    if (currentEncoding_ == TextEncoding::System) {
        QStringEncoder encoder(QStringConverter::System);
        data = encoder.encode(serialized);
    } else {
        QStringEncoder encoder(QStringConverter::Utf8);
        data = encoder.encode(serialized);
    }
    if (file.write(data) != data.size() || !file.commit()) {
        QMessageBox::critical(this, "Save Failed", "Write failed:\n" + path);
        return false;
    }
    setCurrentFilePath(path);
    documentDirty_ = false;
    currentFieldDirty_ = false;
    updateDirtyState();
    statusBar()->showMessage("Saved: " + QFileInfo(path).fileName());
    return true;
}

bool MainWindow::applyBatchTransform(const QString& opName, const BatchTransform& transform)
{
    const QString original = editorText();
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    int changed = 0;
    const QString transformed = transform(original, &changed);
    if (transformed == original) {
        statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

    QTextCursor editCursor = oldCursor;
    editCursor.beginEditBlock();
    editCursor.select(QTextCursor::Document);
    editCursor.insertText(transformed);
    editCursor.endEditBlock();

    QTextCursor restoredCursor(editor->document());
    const int maxPos = editor->document()->characterCount() - 1;
    const int restoredAnchor = qBound(0, oldCursor.anchor(), maxPos);
    const int restoredPosition = qBound(0, oldCursor.position(), maxPos);
    restoredCursor.setPosition(restoredAnchor);
    restoredCursor.setPosition(restoredPosition, QTextCursor::KeepAnchor);
    editor->setTextCursor(restoredCursor);
    if (editor->verticalScrollBar() != nullptr) {
        editor->verticalScrollBar()->setValue(qBound(
            editor->verticalScrollBar()->minimum(),
            oldVScroll,
            editor->verticalScrollBar()->maximum()
        ));
    }
    if (editor->horizontalScrollBar() != nullptr) {
        editor->horizontalScrollBar()->setValue(qBound(
            editor->horizontalScrollBar()->minimum(),
            oldHScroll,
            editor->horizontalScrollBar()->maximum()
        ));
    }

    markCurrentFieldDirty();
    scheduleTimelineRefresh();
    statusBar()->showMessage(QString("%1 applied: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

bool MainWindow::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    int startPos = -1;
    int endPos = -1;
    if (!currentSelectionRange(&startPos, &endPos)) {
        statusBar()->showMessage(QString("%1: no selection.").arg(opName));
        return false;
    }

    const QString original = editorText();
    const int begin = qMin(startPos, endPos);
    const int finish = qMax(startPos, endPos);
    if (begin < 0 || finish <= begin || finish > original.size()) {
        statusBar()->showMessage(QString("%1: invalid selection range.").arg(opName));
        return false;
    }

    const QString selected = original.mid(begin, finish - begin);
    int changed = 0;
    const QString transformed = transform(selected, &changed);
    if (transformed == selected) {
        statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
        return false;
    }

    QTextCursor editCursor = oldCursor;
    editCursor.beginEditBlock();
    editCursor.setPosition(begin);
    editCursor.setPosition(finish, QTextCursor::KeepAnchor);
    editCursor.insertText(transformed);
    editCursor.endEditBlock();

    QTextCursor restoredCursor(editor->document());
    const int maxPos = editor->document()->characterCount() - 1;
    const int transformedEnd = begin + transformed.size();
    const bool forwardSelection = oldCursor.position() >= oldCursor.anchor();
    const int restoredAnchor = qBound(0, forwardSelection ? begin : transformedEnd, maxPos);
    const int restoredPosition = qBound(0, forwardSelection ? transformedEnd : begin, maxPos);
    restoredCursor.setPosition(restoredAnchor);
    restoredCursor.setPosition(restoredPosition, QTextCursor::KeepAnchor);
    editor->setTextCursor(restoredCursor);
    if (editor->verticalScrollBar() != nullptr) {
        editor->verticalScrollBar()->setValue(qBound(
            editor->verticalScrollBar()->minimum(),
            oldVScroll,
            editor->verticalScrollBar()->maximum()
        ));
    }
    if (editor->horizontalScrollBar() != nullptr) {
        editor->horizontalScrollBar()->setValue(qBound(
            editor->horizontalScrollBar()->minimum(),
            oldHScroll,
            editor->horizontalScrollBar()->maximum()
        ));
    }

    markCurrentFieldDirty();
    scheduleTimelineRefresh();
    statusBar()->showMessage(QString("%1 applied on selection: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

std::pair<int, int> MainWindow::currentCursorLineCol() const
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

bool MainWindow::currentSelectionRange(int* startPos, int* endPos) const
{
    if (startPos == nullptr || endPos == nullptr) {
        return false;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        return false;
    }
    *startPos = cursor.selectionStart();
    *endPos = cursor.selectionEnd();
    return *endPos > *startPos;
}

std::pair<int, int> MainWindow::currentSelectionOrCursorLineCol() const
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        return currentCursorLineCol();
    }
    cursor.setPosition(cursor.selectionStart());
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

void MainWindow::setMetadataExtraText(const QString& text)
{
    if (metadataExtraEdit_ == nullptr) {
        return;
    }
    const bool previousSuppress = suppressTextDirtyTracking_;
    suppressTextDirtyTracking_ = true;
    QSignalBlocker blocker(metadataExtraEdit_);
    metadataExtraEdit_->setPlainText(text);
    metadataExtraEdit_->document()->clearUndoRedoStacks();
    applyBlockSpacingToTextEdit(metadataExtraEdit_, blockSpacingPixelsForPointSize(editorTextFontPointSize_, editorLineSpacingFactor_));
    suppressTextDirtyTracking_ = previousSuppress;
}

void MainWindow::setEditorText(const QString& text)
{
    const bool previousSuppress = suppressTextDirtyTracking_;
    suppressTextDirtyTracking_ = true;
    QSignalBlocker blocker(editorWidget_);
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const int blockSpacingPixels = blockSpacingPixelsForPointSize(editorTextFontPointSize_, editorLineSpacingFactor_);
    editor->setPlainText(text);
    editor->setBlockSpacingPixels(blockSpacingPixels);
    editor->document()->clearUndoRedoStacks();
    // QSignalBlocker suppresses blockCountChanged, so force line-number gutter recompute.
    editor->refreshLineNumberAreaLayout();
    suppressTextDirtyTracking_ = previousSuppress;
}

void MainWindow::updatePauseButtonAppearance()
{
    if (pausePreviewAction_ == nullptr) {
        return;
    }
    const QColor iconColor = UiTheme::colors().iconPrimary;
    if (qtPreviewPlaying_) {
        pausePreviewAction_->setIcon(makePreviewPauseIcon(iconColor));
        pausePreviewAction_->setText(uiText("preview.pause", "Pause"));
    } else {
        pausePreviewAction_->setIcon(makePreviewPlayIcon(iconColor));
        pausePreviewAction_->setText(uiText("preview.play", "Play"));
    }
    if (pausePreviewButton_ != nullptr) {
        pausePreviewButton_->setText(
            qtPreviewPlaying_
                ? uiText("preview.pause", "Pause")
                : uiText("preview.play", "Play")
        );
        pausePreviewButton_->setStyleSheet(UiTheme::pausePreviewButtonStyleSheet(qtPreviewPlaying_));
    }
}

void MainWindow::updateDirtyState()
{
    setWindowModified(documentDirty_ || currentFieldDirty_);
}

void MainWindow::markCurrentFieldDirty()
{
    currentFieldDirty_ = true;
    updateDirtyState();
}

void MainWindow::updateEditorHeader()
{
    updateDifficultyScopedActionStates();
    if (editorContextLabel_ == nullptr) {
        return;
    }
    if (!hasActiveDifficulty()) {
        if (document_.difficultyIds().isEmpty() && activeOutlineKey_ == QLatin1String("welcome")) {
            editorContextLabel_->setText(uiText("editor.welcome", "Welcome to MiaCode!"));
            editorContextLabel_->setFont(uiAccentFont(15, QFont::DemiBold));
        } else {
            editorContextLabel_->setText(uiText("editor.metadata", "Metadata"));
            editorContextLabel_->setFont(uiAccentFont(12, QFont::DemiBold));
        }
        editorContextLabel_->setStyleSheet(QString());
        if (editorDifficultyControls_ != nullptr) {
            editorDifficultyControls_->hide();
        }
        if (editorBatchTransformControls_ != nullptr) {
            editorBatchTransformControls_->hide();
        }
        updateDifficultyDeleteButton(false);
        editorContextLabel_->setMinimumWidth(0);
        updateEditorHeaderLayoutMode();
        return;
    }
    editorContextLabel_->setText(SimaiDocument::difficultyShortName(activeDifficultyId_));
    editorContextLabel_->setFont(uiAccentFont(12, QFont::DemiBold));
    editorContextLabel_->setStyleSheet(QString());
    editorContextLabel_->setMinimumWidth(QFontMetrics(editorContextLabel_->font()).horizontalAdvance(editorContextLabel_->text()) + 8);
    if (editorDifficultyControls_ != nullptr) {
        editorDifficultyControls_->show();
    }
    if (editorBatchTransformControls_ != nullptr) {
        editorBatchTransformControls_->show();
    }
    updateDifficultyDeleteButton(false);
    updateEditorHeaderLayoutMode();
}

void MainWindow::updateDifficultyScopedActionStates()
{
    const bool enabled = hasActiveDifficulty();

    if (validateAction_ != nullptr) {
        validateAction_->setEnabled(enabled);
    }
    if (pausePreviewAction_ != nullptr) {
        pausePreviewAction_->setEnabled(enabled);
    }
    if (exportVideoAction_ != nullptr) {
        exportVideoAction_->setEnabled(enabled);
    }
    if (stopPreviewAction_ != nullptr) {
        stopPreviewAction_->setEnabled(enabled);
    }
    if (transformMirrorLeftRightAction_ != nullptr) {
        transformMirrorLeftRightAction_->setEnabled(enabled);
    }
    if (transformMirrorUpDownAction_ != nullptr) {
        transformMirrorUpDownAction_->setEnabled(enabled);
    }
    if (transformRotate180Action_ != nullptr) {
        transformRotate180Action_->setEnabled(enabled);
    }
    if (transformRotate45CounterClockwiseAction_ != nullptr) {
        transformRotate45CounterClockwiseAction_->setEnabled(enabled);
    }
    if (transformRotate45ClockwiseAction_ != nullptr) {
        transformRotate45ClockwiseAction_->setEnabled(enabled);
    }
    if (transformToggleBreakAction_ != nullptr) {
        transformToggleBreakAction_->setEnabled(enabled);
    }
    if (transformToggleExAction_ != nullptr) {
        transformToggleExAction_->setEnabled(enabled);
    }
    if (transformToggleFireworkAction_ != nullptr) {
        transformToggleFireworkAction_->setEnabled(enabled);
    }
    if (transformRandomRotateAction_ != nullptr) {
        transformRandomRotateAction_->setEnabled(enabled);
    }
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(enabled);
    }
    if (pausePreviewButton_ != nullptr) {
        pausePreviewButton_->setEnabled(enabled);
    }
    if (syntaxCheckButton_ != nullptr) {
        syntaxCheckButton_->setEnabled(enabled);
    }
    if (exportVideoButton_ != nullptr) {
        exportVideoButton_->setEnabled(enabled);
    }
    if (transformMirrorLeftRightButton_ != nullptr) {
        transformMirrorLeftRightButton_->setEnabled(enabled);
    }
    if (transformMirrorUpDownButton_ != nullptr) {
        transformMirrorUpDownButton_->setEnabled(enabled);
    }
    if (transformRotate180Button_ != nullptr) {
        transformRotate180Button_->setEnabled(enabled);
    }
    if (transformRotate45CounterClockwiseButton_ != nullptr) {
        transformRotate45CounterClockwiseButton_->setEnabled(enabled);
    }
    if (transformRotate45ClockwiseButton_ != nullptr) {
        transformRotate45ClockwiseButton_->setEnabled(enabled);
    }
}

void MainWindow::updateEditorHeaderLayoutMode()
{
    if (editorHeaderWidget_ == nullptr || editorCursorLabel_ == nullptr || editorContextLabel_ == nullptr) {
        return;
    }

    if (!hasActiveDifficulty()) {
        editorCursorLabel_->setVisible(false);
        return;
    }
    editorCursorLabel_->setVisible(true);
}

void MainWindow::updateEditorStatus()
{
    if (editorCursorLabel_ == nullptr) {
        return;
    }
    if (!hasActiveDifficulty()) {
        editorCursorLabel_->clear();
        updateEditorHeaderLayoutMode();
        return;
    }
    const auto [line, col] = currentCursorLineCol();
    editorCursorLabel_->setText(QString("Ln %1, Col %2").arg(line).arg(col));
    updateEditorHeaderLayoutMode();
}

void MainWindow::updateEditorEmptyState()
{
    if (editorEmptyStateLabel_ != nullptr) {
        editorEmptyStateLabel_->hide();
    }
}

void MainWindow::updateMetadataPageMode()
{
    if (metadataCard_ == nullptr || metadataEmptyHintLabel_ == nullptr) {
        return;
    }
    metadataCard_->setVisible(true);
    metadataEmptyHintLabel_->hide();
}

bool MainWindow::deleteDifficultyField(int difficultyId)
{
    const SimaiDifficultyData* difficultyData = document_.difficulty(difficultyId);
    if (!SimaiDocument::isDifficultyId(difficultyId) || difficultyData == nullptr) {
        return false;
    }

    const bool deletingActiveDifficulty = (difficultyId == activeDifficultyId_);
    const QString difficultyName = SimaiDocument::difficultyName(difficultyId);
    const QString currentLevel =
        deletingActiveDifficulty && difficultyLevelEdit_ != nullptr ? difficultyLevelEdit_->text() : difficultyData->level;
    const QString currentDesigner =
        deletingActiveDifficulty && difficultyDesignerEdit_ != nullptr ? difficultyDesignerEdit_->text() : difficultyData->designer;
    const QString currentChart = deletingActiveDifficulty ? editorText() : difficultyData->chart;
    const bool emptyDifficulty = currentLevel.trimmed().isEmpty()
        && currentDesigner.trimmed().isEmpty()
        && currentChart.trimmed().isEmpty();

    if (!emptyDifficulty) {
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this,
            "Delete Difficulty",
            QString("Delete %1?").arg(difficultyName),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (choice != QMessageBox::Yes) {
            return false;
        }
    }

    stopQtPreviewPlayback(true);
    document_.removeDifficulty(difficultyId);
    documentDirty_ = true;

    if (deletingActiveDifficulty) {
        cacheWorkspaceLayoutSizes();
        currentFieldDirty_ = false;
        const QVector<int> remainingIds = document_.difficultyIds();
        if (remainingIds.isEmpty()) {
            activeDifficultyId_ = 0;
            activeOutlineKey_ = "welcome";
            populateMetadataPage();
            if (editorStack_ != nullptr && welcomePage_ != nullptr) {
                editorStack_->setCurrentWidget(welcomePage_);
            }
            if (bottomTabs_ != nullptr) {
                bottomTabs_->setVisible(false);
            }
            setValidationTabVisible(false);
            clearTimelineAndPreview();
            if (outlineList_ != nullptr) {
                outlineList_->setFocus();
            }
            refreshLayoutAfterPageSwitch();
            QTimer::singleShot(0, this, [this]() { refreshLayoutAfterPageSwitch(); });
        } else {
            int fallbackId = remainingIds.constFirst();
            int bestDistance = qAbs(fallbackId - difficultyId);
            for (int id : remainingIds) {
                const int distance = qAbs(id - difficultyId);
                if (distance < bestDistance || (distance == bestDistance && id < fallbackId)) {
                    fallbackId = id;
                    bestDistance = distance;
                }
            }
            activeOutlineKey_ = "chart";
            switchToDifficultyField(fallbackId);
        }
    }

    rebuildFieldSidebar();
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
    updateDirtyState();
    if (currentFilePath_.isEmpty()) {
        statusBar()->showMessage(QString("Deleted %1.").arg(difficultyName));
        return true;
    }
    if (!saveToPath(currentFilePath_)) {
        statusBar()->showMessage(QString("Deleted %1. Changes are still unsaved.").arg(difficultyName));
    }
    return true;
}

void MainWindow::updateDifficultyDeleteButton(bool visible)
{
    if (deleteDifficultyButton_ == nullptr) {
        return;
    }
    if (!visible || !hasActiveDifficulty() || outlineList_ == nullptr) {
        deleteDifficultyButton_->hide();
        return;
    }
    QListWidgetItem* currentItem = outlineList_->currentItem();
    if (currentItem == nullptr || !SimaiDocument::isDifficultyId(currentItem->data(Qt::UserRole + 1).toInt())) {
        deleteDifficultyButton_->hide();
        return;
    }
    const QRect rowRect = outlineList_->visualItemRect(currentItem);
    if (!rowRect.isValid() || rowRect.isEmpty()) {
        deleteDifficultyButton_->hide();
        return;
    }
    const int x = rowRect.right() - deleteDifficultyButton_->width() - 8;
    const int y = rowRect.top() + (rowRect.height() - deleteDifficultyButton_->height()) / 2;
    deleteDifficultyButton_->move(x, y);
    deleteDifficultyButton_->raise();
    deleteDifficultyButton_->show();
}

void MainWindow::rebuildFieldSidebar()
{
    if (outlineList_ == nullptr) {
        return;
    }
    updateDifficultyDeleteButton(false);
    QSignalBlocker blocker(outlineList_);
    outlineList_->clear();
    auto* metadataItem = new QListWidgetItem(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        uiText("sidebar.metadata", "Metadata"),
        outlineList_
    );
    metadataItem->setData(Qt::UserRole, "metadata");

    QListWidgetItem* selectedItem = nullptr;
    bool hasMissingDifficulty = false;
    const QVector<int> ids = document_.difficultyIds();
    for (int id : ids) {
        auto* difficultyItem = new QListWidgetItem(SimaiDocument::difficultyName(id), outlineList_);
        difficultyItem->setIcon(makeDifficultyBadgeIcon(id));
        difficultyItem->setData(Qt::UserRole, "difficulty_chart");
        difficultyItem->setData(Qt::UserRole + 1, id);
        if (id == activeDifficultyId_) {
            selectedItem = difficultyItem;
        }
    }
    for (int id = 1; id <= 7; ++id) {
        if (document_.difficulty(id) == nullptr) {
            hasMissingDifficulty = true;
            break;
        }
    }
    if (hasMissingDifficulty) {
        auto* addItem = new QListWidgetItem(
            style()->standardIcon(QStyle::SP_FileDialogNewFolder),
            uiText("sidebar.add_difficulty", "+ Add Difficulty"),
            outlineList_
        );
        addItem->setData(Qt::UserRole, "add");
    }
    if (activeOutlineKey_ == QLatin1String("metadata")) {
        selectedItem = metadataItem;
    }
    if (selectedItem != nullptr) {
        outlineList_->setCurrentItem(selectedItem);
    } else {
        outlineList_->setCurrentItem(nullptr);
        outlineList_->setCurrentRow(-1);
        outlineList_->clearSelection();
        if (outlineList_->selectionModel() != nullptr) {
            outlineList_->selectionModel()->clearCurrentIndex();
            outlineList_->selectionModel()->clearSelection();
        }
    }
}

void MainWindow::populateMetadataPage()
{
    if (titleEdit_ == nullptr || artistEdit_ == nullptr || firstEdit_ == nullptr || designerEdit_ == nullptr) {
        return;
    }
    QSignalBlocker blockerTitle(titleEdit_);
    QSignalBlocker blockerArtist(artistEdit_);
    QSignalBlocker blockerFirst(firstEdit_);
    QSignalBlocker blockerDesigner(designerEdit_);
    titleEdit_->setText(document_.title);
    artistEdit_->setText(document_.artist);
    firstEdit_->setText(document_.first);
    designerEdit_->setText(document_.designer);
    setMetadataExtraText(SimaiDocument::serializeRawFields(document_.extraFields));
    updateMetadataPageMode();
    updateEditorHeader();
}

void MainWindow::populateDifficultyPage(int difficultyId)
{
    const SimaiDifficultyData* difficultyData = document_.difficulty(difficultyId);
    if (difficultyData == nullptr) {
        return;
    }
    if (difficultyLevelEdit_ != nullptr) {
        QSignalBlocker blocker(difficultyLevelEdit_);
        if (auto* placeholderEdit = dynamic_cast<LeftPlaceholderLineEdit*>(difficultyLevelEdit_)) {
            placeholderEdit->setLeftPlaceholderText(QString("&lv_%1=").arg(difficultyId));
        }
        difficultyLevelEdit_->setText(difficultyData->level);
    }
    if (difficultyDesignerEdit_ != nullptr) {
        QSignalBlocker blocker(difficultyDesignerEdit_);
        if (auto* placeholderEdit = dynamic_cast<LeftPlaceholderLineEdit*>(difficultyDesignerEdit_)) {
            placeholderEdit->setLeftPlaceholderText(QString("&des_%1=").arg(difficultyId));
        }
        difficultyDesignerEdit_->setText(difficultyData->designer);
    }
    setEditorText(difficultyData->chart);
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
}

bool MainWindow::switchToMetadataField()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    cacheWorkspaceLayoutSizes();
    stopQtPreviewPlayback(true);
    activeDifficultyId_ = 0;
    activeOutlineKey_ = "metadata";
    populateMetadataPage();
    if (editorStack_ != nullptr && metadataPage_ != nullptr) {
        editorStack_->setCurrentWidget(metadataPage_);
    }
    if (bottomTabs_ != nullptr && timelineView_ != nullptr) {
        const int timelineTabIndex = bottomTabs_->indexOf(timelineView_);
        if (timelineTabIndex >= 0) {
            bottomTabs_->setTabVisible(timelineTabIndex, false);
        }
    }
    if (bottomTabs_ != nullptr) {
        bottomTabs_->setVisible(false);
    }
    setValidationTabVisible(false);
    clearValidationDecorations();
    updateMetadataPageMode();
    currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    refreshLayoutAfterPageSwitch();
    QTimer::singleShot(0, this, [this]() { refreshLayoutAfterPageSwitch(); });
    return true;
}

bool MainWindow::switchToWelcomePage()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    cacheWorkspaceLayoutSizes();
    stopQtPreviewPlayback(true);
    activeDifficultyId_ = 0;
    activeOutlineKey_ = "welcome";
    if (editorStack_ != nullptr && welcomePage_ != nullptr) {
        editorStack_->setCurrentWidget(welcomePage_);
    }
    if (bottomTabs_ != nullptr && timelineView_ != nullptr) {
        const int timelineTabIndex = bottomTabs_->indexOf(timelineView_);
        if (timelineTabIndex >= 0) {
            bottomTabs_->setTabVisible(timelineTabIndex, false);
        }
    }
    if (bottomTabs_ != nullptr) {
        bottomTabs_->setVisible(false);
    }
    setValidationTabVisible(false);
    clearValidationDecorations();
    currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    updateWindowTitle();
    updateEditorEmptyState();
    updateEditorStatus();
    refreshLayoutAfterPageSwitch();
    QTimer::singleShot(0, this, [this]() { refreshLayoutAfterPageSwitch(); });
    return true;
}

bool MainWindow::switchToDifficultyField(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId) || document_.difficulty(difficultyId) == nullptr) {
        return false;
    }
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    cacheWorkspaceLayoutSizes();
    stopQtPreviewPlayback(true);
    activeDifficultyId_ = difficultyId;
    projectLastOpenedDifficultyId_ = difficultyId;
    if (activeOutlineKey_.isEmpty() || activeOutlineKey_ == "metadata" || activeOutlineKey_ == "welcome") {
        activeOutlineKey_ = "chart";
    }
    populateDifficultyPage(difficultyId);
    if (editorStack_ != nullptr && chartPage_ != nullptr) {
        editorStack_->setCurrentWidget(chartPage_);
    }
    if (bottomTabs_ != nullptr && timelineView_ != nullptr) {
        const int timelineTabIndex = bottomTabs_->indexOf(timelineView_);
        if (timelineTabIndex >= 0) {
            bottomTabs_->setTabVisible(timelineTabIndex, true);
            bottomTabs_->setCurrentIndex(timelineTabIndex);
        }
        bottomTabs_->setVisible(true);
    } else if (bottomTabs_ != nullptr) {
        bottomTabs_->setVisible(true);
    }
    setValidationTabVisible(true);
    refreshValidationPanelForActiveField();
    currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
    scheduleTimelineRefresh();
    saveProjectRenderState();
    refreshLayoutAfterPageSwitch();
    QTimer::singleShot(0, this, [this]() { refreshLayoutAfterPageSwitch(); });
    return true;
}

void MainWindow::activateInitialField()
{
    const QVector<int> ids = document_.difficultyIds();
    if (!ids.isEmpty()) {
        activeOutlineKey_ = "chart";
        int targetId = 0;
        if (SimaiDocument::isDifficultyId(projectLastOpenedDifficultyId_)
            && ids.contains(projectLastOpenedDifficultyId_)) {
            targetId = projectLastOpenedDifficultyId_;
        }
        if (targetId == 0) {
            const QVector<int> preferredOrder{5, 6, 4, 7, 3, 2, 1};
            targetId = ids.constFirst();
            for (int id : preferredOrder) {
                if (ids.contains(id)) {
                    targetId = id;
                    break;
                }
            }
        }
        switchToDifficultyField(targetId);
    } else {
        activeOutlineKey_ = "welcome";
        switchToWelcomePage();
        clearTimelineAndPreview();
    }
}

void MainWindow::loadDocument(const SimaiDocument& document)
{
    document_ = document;
    documentDirty_ = false;
    currentFieldDirty_ = false;
    activeDifficultyId_ = 0;
    activeOutlineKey_ = document_.difficultyIds().isEmpty() ? QStringLiteral("welcome") : QStringLiteral("chart");
    rebuildFieldSidebar();
    activateInitialField();
    updateMetadataPageMode();
    updateDirtyState();
    updateWindowTitle();
    updateEditorHeader();
    updateEditorEmptyState();
    updateEditorStatus();
}

void MainWindow::clearTimelineAndPreview()
{
    timelineCursorNotes_.clear();
    previewFollowCursorNotes_.clear();
    lastPreviewNoteMarkerSignature_.clear();
    clearPreviewFollowDecoration();
    clearPreviewObjectStats();
    previewTrackDurationSeconds_ = 0.0;
    qtPreviewTimelineDirty_ = false;
    qtPreviewPendingTimelineSecond_ = 0.0;
    qtPreviewPendingTimelineCenterView_ = true;
    qtPreviewLastTimelineSecond_ = -1.0;
    qtPreviewTimelineStartSecond_ = 0.0;
    qtPreviewTimelineCenterNextTick_ = true;
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->clearTimeline();
    }
    stopQtPreviewPlayback(false);
    if (timelineView_ != nullptr) {
        timelineView_->clear();
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->reset();
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->reset();
    }
    updatePreviewSliderRange();
    updatePreviewSliderPosition(0.0);
}

