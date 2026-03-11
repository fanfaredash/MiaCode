void MainWindow::refreshWaveformCache()
{
    const double firstSeconds = parsedFirstSeconds();
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackOffsetSeconds(firstSeconds);
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setTimelineOffsetSeconds(firstSeconds);
    }
    if (timelineView_ == nullptr) {
        return;
    }
    double audioDurationSeconds = 0.0;
    const QVector<float> waveform = buildWaveformPeaks(lastTrackPath_, &audioDurationSeconds);
    previewTrackDurationSeconds_ = qMax(0.0, audioDurationSeconds);
    timelineView_->setWaveformData(waveform, -firstSeconds, audioDurationSeconds);
    updatePreviewSliderRange();
}

bool MainWindow::hasActiveDifficulty() const
{
    return activeDifficultyId_ > 0 && document_.difficulty(activeDifficultyId_) != nullptr;
}

int MainWindow::activeDifficultyId() const
{
    return activeDifficultyId_;
}

QString MainWindow::activeChartText() const
{
    if (!hasActiveDifficulty()) {
        return QString();
    }
    if (editorStack_ != nullptr && editorStack_->currentWidget() == chartPage_) {
        return editorText();
    }
    const SimaiDifficultyData* difficultyData = document_.difficulty(activeDifficultyId_);
    return difficultyData != nullptr ? difficultyData->chart : QString();
}

double MainWindow::parsedFirstSeconds(bool* ok) const
{
    QString rawValue = document_.first;
    if (editorStack_ != nullptr && editorStack_->currentWidget() == metadataPage_ && firstEdit_ != nullptr) {
        rawValue = firstEdit_->text();
    }
    bool localOk = false;
    const double value = rawValue.trimmed().isEmpty() ? 0.0 : rawValue.trimmed().toDouble(&localOk);
    if (ok != nullptr) {
        *ok = rawValue.trimmed().isEmpty() ? true : localOk;
    }
    if (rawValue.trimmed().isEmpty()) {
        return 0.0;
    }
    return localOk ? value : 0.0;
}

double MainWindow::parsedWholeBpm(bool* ok) const
{
    const QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool localOk = false;
        const double value = field.value.trimmed().toDouble(&localOk);
        if (ok != nullptr) {
            *ok = localOk && value > 0.0;
        }
        return (localOk && value > 0.0) ? value : 0.0;
    }
    if (ok != nullptr) {
        *ok = false;
    }
    return 0.0;
}

QString MainWindow::parsedLatencyMeterId() const
{
    const QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("meter"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QString value = field.value.trimmed();
        if (value == QLatin1String("4/4")
            || value == QLatin1String("3/4")
            || value == QLatin1String("6/8")
            || value == QLatin1String("7/4")
            || value == QLatin1String("auto")) {
            return value;
        }
        return QStringLiteral("auto");
    }
    return QStringLiteral("auto");
}

void MainWindow::applyLatencyDetectorOffset(double seconds)
{
    const double normalized = qIsFinite(seconds) ? seconds : 0.0;
    const QString serialized = QString::number(normalized, 'f', 3);
    document_.first = serialized;
    if (firstEdit_ != nullptr) {
        QSignalBlocker blocker(firstEdit_);
        firstEdit_->setText(serialized);
    }
    documentDirty_ = true;
    updateDirtyState();
    refreshWaveformCache();
}

void MainWindow::applyLatencyDetectorBpm(double bpm)
{
    if (!qIsFinite(bpm) || bpm <= 0.0) {
        return;
    }
    QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    const QString serializedBpm = QString::number(bpm, 'f', 3);
    bool foundWholeBpm = false;
    for (SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        field.value = serializedBpm;
        foundWholeBpm = true;
        break;
    }
    if (!foundWholeBpm) {
        fields.append(SimaiRawField{QStringLiteral("wholebpm"), serializedBpm});
    }
    document_.extraFields = fields;
    setMetadataExtraText(SimaiDocument::serializeRawFields(fields));
    documentDirty_ = true;
    updateDirtyState();
}

void MainWindow::applyLatencyDetectorMeter(const QString& meterId)
{
    QString normalized = meterId.trimmed();
    if (normalized != QLatin1String("4/4")
        && normalized != QLatin1String("3/4")
        && normalized != QLatin1String("6/8")
        && normalized != QLatin1String("7/4")
        && normalized != QLatin1String("auto")) {
        normalized = QStringLiteral("auto");
    }

    QVector<SimaiRawField> fields = SimaiDocument::parseRawFields(
        metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
        true
    );
    bool found = false;
    for (SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("meter"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        field.value = normalized;
        found = true;
        break;
    }
    if (!found) {
        fields.append(SimaiRawField{QStringLiteral("meter"), normalized});
    }
    document_.extraFields = fields;
    setMetadataExtraText(SimaiDocument::serializeRawFields(fields));
    documentDirty_ = true;
    updateDirtyState();
}

void MainWindow::setCurrentFilePath(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const bool pathChanged = normalizedPath != currentFilePath_;
    if (pathChanged) {
        stopQtPreviewPlayback(false);
        if (latencyDetectorDialog_ != nullptr) {
            latencyDetectorDialog_->close();
            latencyDetectorDialog_.clear();
        }
    }
    currentFilePath_ = normalizedPath;
    if (!currentFilePath_.isEmpty()) {
        setLastOpenDirectory(currentFilePath_);

        const QString siblingTrack = QDir(QFileInfo(currentFilePath_).absolutePath()).filePath("track.mp3");
        if (QFileInfo::exists(siblingTrack)) {
            // Keep preview audio in sync with the currently opened chart directory.
            lastTrackPath_ = QDir::cleanPath(siblingTrack);
        } else {
            lastTrackPath_.clear();
        }
    } else {
        lastTrackPath_.clear();
    }
    updateWindowTitle();
    updateCurrentFileLabel();
    updateLatencyDetectorAvailability();
    if (pathChanged) {
        loadProjectRenderState();
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setChartPath(currentFilePath_);
        previewMediaController_->setBackgroundTrackPath(lastTrackPath_);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setChartPath(currentFilePath_);
    }
    applyPreviewAudioSettingsToRuntime();
    refreshWaveformCache();
}

void MainWindow::updateWindowTitle()
{
    QString titleText = document_.title;
    if (editorStack_ != nullptr && editorStack_->currentWidget() == metadataPage_ && titleEdit_ != nullptr) {
        titleText = titleEdit_->text();
    }
    if (titleText.trimmed().isEmpty()) {
        titleText = currentFilePath_.isEmpty()
            ? QString("Untitled.simai")
            : QFileInfo(currentFilePath_).fileName();
    }
    const QFontMetrics metrics(font());
    const QString elided = metrics.elidedText(titleText, Qt::ElideRight, 420);
    setWindowTitle(QString("MiaCode - %1[*]").arg(elided));
}

void MainWindow::updateCurrentFileLabel()
{
    if (currentFileLabel_ == nullptr) {
        return;
    }
    if (currentFilePath_.isEmpty()) {
        currentFileLabel_->setText("(unsaved)");
    } else {
        currentFileLabel_->setText(QDir::toNativeSeparators(currentFilePath_));
    }
}

QString MainWindow::editorText() const
{
    return qobject_cast<PlainCodeEditor*>(editorWidget_)->toPlainText();
}

void MainWindow::clearValidationErrors()
{
    errorList_->clear();
}

void MainWindow::clearValidationDecorations()
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    editor->setExtraSelections({});
}

void MainWindow::addValidationError(int line, int col, const QString& message)
{
    auto* item = new QListWidgetItem(QString("L%1 C%2  %3").arg(line).arg(col).arg(message), errorList_);
    item->setData(Qt::UserRole, line);
    item->setData(Qt::UserRole + 1, col);
}

void MainWindow::addValidationDecoration(int line, int col, const QString& message)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        return;
    }

    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + qMax(0, col - 1));
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);

    QTextEdit::ExtraSelection sel;
    sel.cursor = cursor;
    sel.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    sel.format.setUnderlineColor(QColor("#E74C3C"));
    sel.format.setToolTip(message);

    auto selections = editor->extraSelections();
    selections.append(sel);
    editor->setExtraSelections(selections);
}

void MainWindow::jumpToLocation(int line, int col)
{
    if (line < 1) {
        line = 1;
    }
    if (col < 1) {
        col = 1;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        return;
    }
    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + col - 1);
    cursor.clearSelection();
    editor->setTextCursor(cursor);
    editor->ensureCursorVisible();
    editor->setFocus();
}

QString MainWindow::resolvePreviewSessionScriptPath() const
{
    const QString envPath = qEnvironmentVariable("MIACODE_PREVIEW_SESSION_SCRIPT", qEnvironmentVariable("MAIMURI_PREVIEW_SESSION_SCRIPT"));
    if (!envPath.isEmpty() && QFileInfo::exists(envPath)) {
        return envPath;
    }
    return QString();
}

void MainWindow::scheduleTimelineRefresh()
{
    if (metadataRefreshTimer_ == nullptr) {
        return;
    }
    if (!hasActiveDifficulty()) {
        return;
    }
    metadataRefreshTimer_->stop();
    metadataRefreshTimer_->start();
}

void MainWindow::refreshTimelineMetadata()
{
    if (timelineView_ == nullptr || !hasActiveDifficulty()) {
        return;
    }
    const SimaiNativeParseResult nativeResult = SimaiNativeParser::parseForTimeline(activeChartText());
    QVector<TimelineBeatMarker> beatMarkers = nativeResult.beatMarkers;
    QVector<TimelineNoteMarker> noteMarkers = nativeResult.noteMarkers;
    timelineCursorNotes_.clear();
    timelineCursorNotes_.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        TimelineCursorNote cursorNote;
        cursorNote.line = qMax(1, marker.sourceLine);
        cursorNote.col = qMax(1, marker.sourceCol);
        cursorNote.lane = marker.lane;
        cursorNote.second = marker.second;
        timelineCursorNotes_.append(cursorNote);
    }

    std::sort(timelineCursorNotes_.begin(), timelineCursorNotes_.end(), [](const TimelineCursorNote& a, const TimelineCursorNote& b) {
        if (a.line != b.line) {
            return a.line < b.line;
        }
        if (a.col != b.col) {
            return a.col < b.col;
        }
        return a.second < b.second;
    });

    double durationSeconds = nativeResult.durationSeconds;
    if (durationSeconds <= 0.0) {
        for (const TimelineNoteMarker& marker : noteMarkers) {
            durationSeconds = qMax(durationSeconds, marker.second);
            if (marker.endSecond > marker.second) {
                durationSeconds = qMax(durationSeconds, marker.endSecond);
            }
        }
        for (const TimelineBeatMarker& marker : beatMarkers) {
            durationSeconds = qMax(durationSeconds, marker.second);
        }
    }

    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->configureTimeline(noteMarkers);
    }
    refreshPreviewObjectStatsTotals(noteMarkers);

    timelineView_->setTimelineData(beatMarkers, noteMarkers, durationSeconds);
    updatePreviewSliderRange();
    if (previewCanvas_ != nullptr) {
        const QByteArray newSignature = noteMarkerSignature(noteMarkers);
        if (newSignature != lastPreviewNoteMarkerSignature_) {
            previewCanvas_->setNoteMarkers(noteMarkers);
            lastPreviewNoteMarkerSignature_ = newSignature;
        }
    }
}

double MainWindow::timelineSecondForCursor(int line, int col) const
{
    if (timelineCursorNotes_.isEmpty()) {
        return -1.0;
    }

    for (const TimelineCursorNote& note : timelineCursorNotes_) {
        if (note.line > line || (note.line == line && note.col >= col)) {
            return note.second;
        }
    }
    return timelineCursorNotes_.constLast().second;
}

void MainWindow::seekTimelineToCursor(int line, int col)
{
    if (timelineView_ == nullptr) {
        return;
    }
    const double second = timelineSecondForCursor(line, col);
    if (second < 0.0) {
        return;
    }
    timelineView_->setCursorSeconds(second);
    timelineView_->setPlayheadSeconds(second, true);
}

void MainWindow::syncTimelineToEditorCursor(bool centerView)
{
    if (qtPreviewPlaying_ || !hasActiveDifficulty() || timelineView_ == nullptr) {
        return;
    }
    const auto [line, col] = currentCursorLineCol();
    const double second = timelineSecondForCursor(line, col);
    if (second < 0.0) {
        return;
    }
    timelineView_->setCursorSeconds(second);
    timelineView_->setPlayheadSeconds(second, centerView);
}

double MainWindow::previewDurationSeconds() const
{
    double duration = 0.0;
    if (timelineView_ != nullptr) {
        duration = qMax(duration, timelineView_->durationSeconds());
    }
    if (previewTrackDurationSeconds_ > 0.0) {
        const double firstSeconds = parsedFirstSeconds();
        const double audioStartSecond = qMax(0.0, -firstSeconds);
        duration = qMax(duration, audioStartSecond + previewTrackDurationSeconds_ + 3.0);
    }
    return qMax(0.0, duration);
}

void MainWindow::updatePreviewSliderRange()
{
    if (previewSlider_ == nullptr) {
        return;
    }
    const int maximum = qMax(1, qRound(previewDurationSeconds() * 1000.0));
    QSignalBlocker blocker(previewSlider_);
    previewSlider_->setMaximum(maximum);
}

void MainWindow::updatePreviewSliderPosition(double second)
{
    if (previewSlider_ == nullptr || previewSliderDragging_) {
        return;
    }
    const int value = qBound(0, qRound(second * 1000.0), previewSlider_->maximum());
    QSignalBlocker blocker(previewSlider_);
    previewSlider_->setValue(value);
}

void MainWindow::refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers)
{
    previewStatsNoteMarkers_ = noteMarkers;
    updatePreviewObjectStats(qtPreviewPauseSecond_);
}

void MainWindow::clearPreviewObjectStats()
{
    previewStatsNoteMarkers_.clear();
    updatePreviewObjectStats(0.0);
}

int MainWindow::updatePreviewStatsLayoutMode(int hostWidth)
{
    if (previewStatsCard_ == nullptr || previewStatsGridLayout_ == nullptr || previewStatsChips_.isEmpty()) {
        return 0;
    }

    const int itemCount = previewStatsChips_.size();
    const QWidget* gridHost = previewStatsGridLayout_->parentWidget();
    const int horizontalSpacing = qMax(0, previewStatsGridLayout_->horizontalSpacing());
    const int verticalSpacing = qMax(0, previewStatsGridLayout_->verticalSpacing());
    const QMargins gridMargins = previewStatsGridLayout_->contentsMargins();
    const int resolvedHostWidth =
        (hostWidth >= 0)
        ? hostWidth
        : ((gridHost != nullptr) ? gridHost->contentsRect().width() : previewStatsCard_->contentsRect().width());
    constexpr int kWideLayoutCols = 3;
    constexpr int kNarrowLayoutCols = 2;
    constexpr int kWideLayoutMinChipWidth = 118;
    const int wideLayoutThreshold = kWideLayoutCols * kWideLayoutMinChipWidth + qMax(0, kWideLayoutCols - 1) * horizontalSpacing;
    const bool useWideLayout = resolvedHostWidth >= wideLayoutThreshold;
    const int cols = qMin(itemCount, useWideLayout ? kWideLayoutCols : kNarrowLayoutCols);
    const int rows = qMax(1, (itemCount + cols - 1) / cols);
    const bool structureChanged = (rows != previewStatsLayoutRows_) || (cols != previewStatsLayoutCols_);
    previewStatsLayoutRows_ = rows;
    previewStatsLayoutCols_ = cols;

    const int chipHeight = qMax(
        30,
        !previewStatsChips_.isEmpty() && previewStatsChips_.constFirst() != nullptr
            ? previewStatsChips_.constFirst()->sizeHint().height()
            : 30
    );
    const int cardHeight = 16 + gridMargins.top() + gridMargins.bottom() + rows * chipHeight + qMax(0, rows - 1) * verticalSpacing;
    previewStatsCard_->setMinimumHeight(cardHeight);

    if (structureChanged) {
        while (QLayoutItem* item = previewStatsGridLayout_->takeAt(0)) {
            delete item;
        }
        for (int col = 0; col < 6; ++col) {
            previewStatsGridLayout_->setColumnStretch(col, 0);
            previewStatsGridLayout_->setColumnMinimumWidth(col, 0);
        }
        for (int row = 0; row < 6; ++row) {
            previewStatsGridLayout_->setRowStretch(row, 0);
        }

        for (int i = 0; i < itemCount; ++i) {
            const int row = i / cols;
            const int col = i % cols;
            previewStatsGridLayout_->addWidget(previewStatsChips_.at(i), row, col);
        }
        for (int col = 0; col < cols; ++col) {
            previewStatsGridLayout_->setColumnStretch(col, 1);
        }
        for (int row = 0; row < rows; ++row) {
            previewStatsGridLayout_->setRowStretch(row, 1);
        }
    }

    // Keep chip widths column-driven and independent from text metrics.
    const int totalSpacing = horizontalSpacing * qMax(0, cols - 1);
    const int availableWidth = qMax(0, resolvedHostWidth - gridMargins.left() - gridMargins.right() - totalSpacing);
    const int columnWidth = (cols > 0) ? (availableWidth / cols) : 0;
    for (QLabel* chip : previewStatsChips_) {
        if (chip == nullptr) {
            continue;
        }
        chip->setFixedWidth(qMax(0, columnWidth));
    }

    return cardHeight;
}

void MainWindow::updatePreviewWorkspaceLayout()
{
    if (workspaceSplitter_ == nullptr || previewPanel_ == nullptr || previewControlCard_ == nullptr) {
        return;
    }

    const QRect splitterRect = workspaceSplitter_->contentsRect();
    if (splitterRect.width() <= 0 || splitterRect.height() <= 0) {
        updatePreviewPanelLayout();
        return;
    }

    const int handleWidth = qMax(0, workspaceSplitter_->handleWidth());
    const int availableWidth = qMax(0, splitterRect.width() - handleWidth);
    const int availableHeight = qMax(0, splitterRect.height());
    const int leftMinWidth = (previewLeftColumn_ != nullptr) ? previewLeftColumn_->minimumWidth() : 320;
    const int minimumRightWidth =
        (availableWidth >= leftMinWidth + kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2)
        ? (kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2)
        : qMin(availableWidth, kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2);
    const int rightMaxWidth =
        (availableWidth >= leftMinWidth + minimumRightWidth)
        ? qMin(kEmbeddedPreviewPanelWidthMax, availableWidth - leftMinWidth)
        : availableWidth;
    const int preferredRightMaxWidth = qMax(minimumRightWidth, rightMaxWidth);
    int preferredRightWidth = qRound(availableWidth * kEmbeddedPreviewPanelWidthRatio);
    preferredRightWidth = qMin(preferredRightWidth, preferredRightMaxWidth);
    preferredRightWidth = qMax(preferredRightWidth, qMin(kEmbeddedPreviewPanelMinWidth, preferredRightMaxWidth));

    const int controlHeight = qMax(previewControlCard_->minimumSizeHint().height(), previewControlCard_->sizeHint().height());
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/layout-calc/workspace-base",
            QString("splitter_rect=%1x%2 handle=%3 available=%4x%5 left_min=%6 right_min=%7 right_max=%8 preferred_ratio=%.2f preferred_right=%9 control_h=%10")
                .arg(splitterRect.width())
                .arg(splitterRect.height())
                .arg(handleWidth)
                .arg(availableWidth)
                .arg(availableHeight)
                .arg(leftMinWidth)
                .arg(minimumRightWidth)
                .arg(rightMaxWidth)
                .arg(preferredRightWidth)
                .arg(controlHeight)
                .replace("%.2f", QString::number(kEmbeddedPreviewPanelWidthRatio, 'f', 2))
        );
    }
    int targetRightWidth = preferredRightWidth;
    for (int i = 0; i < 3; ++i) {
        const int panelContentWidth = qMax(0, targetRightWidth - kPreviewPanelMarginX * 2);
        const int statsHostWidth = qMax(0, panelContentWidth - 16);
        const int minimumStatsHeight = updatePreviewStatsLayoutMode(statsHostWidth);
        const int availablePreviewHeight = qMax(
            0,
            availableHeight
                - kPreviewPanelMarginTop
                - kPreviewCanvasControlGap
                - controlHeight
                - kPreviewControlStatsGap
                - minimumStatsHeight
                - kPreviewStatsBottomGap
        );
        const int heightLimitedWidth = qMax(0, availablePreviewHeight + kPreviewPanelMarginX * 2);
        const int nextRightWidth = qMin(targetRightWidth, qMax(minimumRightWidth, heightLimitedWidth));
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "preview/layout-calc/workspace-iter",
                QString("iter=%1 target_right=%2 panel_content_w=%3 stats_host_w=%4 stats_min_h=%5 available_preview_h=%6 height_limited_w=%7 next_right=%8")
                    .arg(i)
                    .arg(targetRightWidth)
                    .arg(panelContentWidth)
                    .arg(statsHostWidth)
                    .arg(minimumStatsHeight)
                    .arg(availablePreviewHeight)
                    .arg(heightLimitedWidth)
                    .arg(nextRightWidth)
            );
        }
        if (nextRightWidth == targetRightWidth) {
            break;
        }
        targetRightWidth = nextRightWidth;
    }

    targetRightWidth = qBound(minimumRightWidth, targetRightWidth, rightMaxWidth);
    const int targetLeftWidth =
        (availableWidth >= leftMinWidth + targetRightWidth)
        ? qMax(leftMinWidth, availableWidth - targetRightWidth)
        : qMax(0, availableWidth - targetRightWidth);
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/layout-calc/workspace-final",
            QString("target_left=%1 target_right=%2 total_available_w=%3")
                .arg(targetLeftWidth)
                .arg(targetRightWidth)
                .arg(availableWidth)
        );
    }
    const QList<int> currentSizes = workspaceSplitter_->sizes();
    if (currentSizes.size() == 2
        && (qAbs(currentSizes.at(0) - targetLeftWidth) > 1 || qAbs(currentSizes.at(1) - targetRightWidth) > 1)) {
        workspaceSplitter_->setSizes({targetLeftWidth, targetRightWidth});
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "preview/layout-calc/workspace-apply",
                QString("applied_sizes=[%1,%2] previous=[%3,%4]")
                    .arg(targetLeftWidth)
                    .arg(targetRightWidth)
                    .arg(currentSizes.value(0))
                    .arg(currentSizes.value(1))
            );
        }
    }

    updatePreviewPanelLayout();
}

void MainWindow::updatePreviewPanelLayout()
{
    if (previewPanel_ == nullptr
        || previewCanvasFrame_ == nullptr
        || previewCanvasContainer_ == nullptr
        || previewControlCard_ == nullptr
        || previewStatsCard_ == nullptr) {
        return;
    }

    const QRect panelRect = previewPanel_->contentsRect();
    if (panelRect.width() <= 0 || panelRect.height() <= 0) {
        return;
    }

    const int contentX = panelRect.x() + kPreviewPanelMarginX;
    const int contentY = panelRect.y() + kPreviewPanelMarginTop;
    const int contentWidth = qMax(0, panelRect.width() - kPreviewPanelMarginX * 2);
    const int controlHeight = qMax(previewControlCard_->minimumSizeHint().height(), previewControlCard_->sizeHint().height());
    const int statsHostWidth = qMax(0, contentWidth - 16);
    const int minimumStatsHeight = updatePreviewStatsLayoutMode(statsHostWidth);
    const int availablePreviewHeight = qMax(
        0,
        panelRect.height()
            - kPreviewPanelMarginTop
            - kPreviewCanvasControlGap
            - controlHeight
            - kPreviewControlStatsGap
            - minimumStatsHeight
            - kPreviewStatsBottomGap
    );
    const int previewSide = qMax(1, qMin(contentWidth, availablePreviewHeight));
    const int controlY = contentY + previewSide + kPreviewCanvasControlGap;
    const int statsAreaY = controlY + controlHeight + kPreviewControlStatsGap;
    const int statsAreaHeight = qMax(0, panelRect.height() - (statsAreaY - panelRect.y()) - kPreviewStatsBottomGap);
    const int statsHeight = qMin(minimumStatsHeight, statsAreaHeight);
    const int statsY = statsAreaY + qMax(0, (statsAreaHeight - statsHeight) / 2);
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/layout-calc/panel",
            QString("panel=%1x%2 content=(x=%3,y=%4,w=%5) control_h=%6 stats_host_w=%7 stats_min_h=%8 available_preview_h=%9 preview_side=%10 control_y=%11 stats_area_y=%12 stats_area_h=%13 stats_y=%14 stats_h=%15")
                .arg(panelRect.width())
                .arg(panelRect.height())
                .arg(contentX)
                .arg(contentY)
                .arg(contentWidth)
                .arg(controlHeight)
                .arg(statsHostWidth)
                .arg(minimumStatsHeight)
                .arg(availablePreviewHeight)
                .arg(previewSide)
                .arg(controlY)
                .arg(statsAreaY)
                .arg(statsAreaHeight)
                .arg(statsY)
                .arg(statsHeight)
        );
    }

    previewCanvasFrame_->setGeometry(contentX, contentY, previewSide, previewSide);
    previewCanvasContainer_->setGeometry(previewCanvasFrame_->rect().adjusted(1, 1, -1, -1));
    previewControlCard_->setGeometry(contentX, controlY, contentWidth, controlHeight);
    previewStatsCard_->setGeometry(contentX, statsY, contentWidth, statsHeight);
    updatePreviewStatsLayoutMode(statsHostWidth);

    if (!previewLayoutInitialized_) {
        previewCanvasContainer_->show();
        previewLayoutInitialized_ = true;
    }
}

void MainWindow::updatePreviewObjectStats(double second)
{
    if (previewTapStatsLabel_ == nullptr
        || previewHoldStatsLabel_ == nullptr
        || previewSlideStatsLabel_ == nullptr
        || previewTouchStatsLabel_ == nullptr
        || previewBreakStatsLabel_ == nullptr
        || previewTotalStatsLabel_ == nullptr) {
        return;
    }

    int tapTotal = 0;
    int tapPlayed = 0;
    int holdTotal = 0;
    int holdPlayed = 0;
    int slideTotal = 0;
    int slidePlayed = 0;
    int touchTotal = 0;
    int touchPlayed = 0;
    int breakTotal = 0;
    int breakPlayed = 0;
    int helperTapNonBreakTotal = 0;
    int helperTapNonBreakPlayed = 0;
    int helperTapBreakTotal = 0;
    int helperTapBreakPlayed = 0;
    int baseTotalCount = 0;
    int baseTotalPlayed = 0;
    int totalCount = 0;
    int totalPlayed = 0;

    for (const TimelineNoteMarker& marker : previewStatsNoteMarkers_) {
        const QString type = marker.type.toLower();
        const bool played = marker.second <= (second + 1e-6);
        const bool isTap = (type == "tap");
        const bool isHold = (type == "hold" || type == "touch_hold");
        const bool isSlide = (type == "slide" || type == "wifi");
        const bool isTouch = (type == "touch");
        const bool isBreak = marker.isBreak || marker.headBreak || marker.trackBreak;
        if (played) {
            ++baseTotalPlayed;
        }
        ++baseTotalCount;
        if (isTap) {
            // Legacy display semantics: "Tap" excludes break taps, and break
            // is shown as a dedicated bucket.
            if (!marker.isBreak) {
                ++tapTotal;
                if (played) {
                    ++tapPlayed;
                }
            }
        }
        if (isHold) {
            ++holdTotal;
            if (played) {
                ++holdPlayed;
            }
        }
        if (isSlide) {
            ++slideTotal;
            if (played) {
                ++slidePlayed;
            }
        }
        if (isTouch) {
            ++touchTotal;
            if (played) {
                ++touchPlayed;
            }
        }
        if (isBreak) {
            ++breakTotal;
            if (played) {
                ++breakPlayed;
            }
        }

        // Legacy parser internally has helper slide-head taps ("*_") that are
        // filtered from timeline lanes. Re-add them for preview counters so
        // Tap/Total matches legacy display numbers.
        if (isSlide && marker.hasHeadStar) {
            const double helperMoment = marker.slideTraceSecond > marker.second
                ? marker.slideTraceSecond
                : marker.second;
            const bool helperPlayed = helperMoment <= (second + 1e-6);
            if (marker.headBreak) {
                ++helperTapBreakTotal;
                if (helperPlayed) {
                    ++helperTapBreakPlayed;
                }
            } else {
                ++helperTapNonBreakTotal;
                if (helperPlayed) {
                    ++helperTapNonBreakPlayed;
                }
            }
        }
    }

    tapTotal += helperTapNonBreakTotal;
    tapPlayed += helperTapNonBreakPlayed;
    breakTotal += helperTapBreakTotal;
    breakPlayed += helperTapBreakPlayed;

    totalCount = baseTotalCount + helperTapNonBreakTotal + helperTapBreakTotal;
    totalPlayed = baseTotalPlayed + helperTapNonBreakPlayed + helperTapBreakPlayed;

    const auto fmt = [](const QString& name, int played, int total) {
        return QString("%1  %2/%3")
            .arg(name.leftJustified(5, QChar(' '), true))
            .arg(played)
            .arg(total);
    };
    previewTapStatsLabel_->setText(fmt("Tap", tapPlayed, tapTotal));
    previewHoldStatsLabel_->setText(fmt("Hold", holdPlayed, holdTotal));
    previewSlideStatsLabel_->setText(fmt("Slide", slidePlayed, slideTotal));
    previewTouchStatsLabel_->setText(fmt("Touch", touchPlayed, touchTotal));
    previewBreakStatsLabel_->setText(fmt("Break", breakPlayed, breakTotal));
    previewTotalStatsLabel_->setText(fmt("Total", totalPlayed, totalCount));
}

QString MainWindow::formatPreviewTimestamp(double second) const
{
    const int totalCentiseconds = qMax(0, qRound(second * 100.0));
    const int minutes = totalCentiseconds / 6000;
    const int secondsPart = (totalCentiseconds / 100) % 60;
    const int centiseconds = totalCentiseconds % 100;
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secondsPart, 2, 10, QChar('0'))
        .arg(centiseconds, 2, 10, QChar('0'));
}

void MainWindow::showPreviewSliderTimeHint(int sliderValue)
{
    if (previewSlider_ == nullptr) {
        return;
    }
    const double second = static_cast<double>(sliderValue) / 1000.0;
    QStyleOptionSlider option;
    option.initFrom(previewSlider_);
    option.subControls = QStyle::SC_SliderHandle;
    option.orientation = previewSlider_->orientation();
    option.minimum = previewSlider_->minimum();
    option.maximum = previewSlider_->maximum();
    option.sliderPosition = sliderValue;
    option.sliderValue = sliderValue;
    option.upsideDown = false;
    const QRect handleRect = previewSlider_->style()->subControlRect(
        QStyle::CC_Slider,
        &option,
        QStyle::SC_SliderHandle,
        previewSlider_
    );
    const QPoint global = previewSlider_->mapToGlobal(handleRect.center() + QPoint(0, -18));
    QToolTip::showText(global, formatPreviewTimestamp(second), previewSlider_, previewSlider_->rect(), 600);
}

void MainWindow::schedulePreviewSeek(double second, bool centerView)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    previewPendingSeekSecond_ = clampedSecond;
    previewPendingSeekCenterView_ = centerView;
    updatePreviewSliderPosition(clampedSecond);
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->start();
    } else {
        seekPreviewToSecond(clampedSecond, centerView);
    }
}

void MainWindow::seekPreviewToSecond(double second, bool centerView)
{
    ensurePreviewMediaControllerInitialized();
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
    }
    qtPreviewStartSecond_ = clampedSecond;
    qtPreviewPauseSecond_ = clampedSecond;
    qtPreviewTimelineStartSecond_ = clampedSecond;
    qtPreviewTimelineElapsed_.restart();
    qtPreviewPendingTimelineSecond_ = clampedSecond;
    qtPreviewPendingTimelineCenterView_ = centerView;
    qtPreviewTimelineDirty_ = true;
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setPlayheadSeconds(clampedSecond);
    }
    applyQtPreviewPosition(clampedSecond, centerView);
    if (previewCanvas_ != nullptr) {
        previewCanvas_->update();
    }
    updatePreviewSliderPosition(clampedSecond);
}

void MainWindow::applyPreviewPlaybackRate(double rate)
{
    ensurePreviewMediaControllerInitialized();
    const double clampedRate = qMax(0.25, rate);
    if (qFuzzyCompare(previewPlaybackRate_ + 1.0, clampedRate + 1.0)) {
        return;
    }
    previewPlaybackRate_ = clampedRate;
    if (previewSpeedButton_ != nullptr) {
        QString rateText = QString::number(previewPlaybackRate_, 'f', 2);
        while (rateText.endsWith('0')) {
            rateText.chop(1);
        }
        if (rateText.endsWith('.')) {
            rateText.chop(1);
        }
        previewSpeedButton_->setText(QString("%1x").arg(rateText));
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setPlaybackRate(previewPlaybackRate_);
        previewMediaController_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
    }
    if (qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
        startQtPreviewPlayback(qtPreviewPauseSecond_, true);
    }
}

void MainWindow::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    ensurePreviewMediaControllerInitialized();
    ensurePreviewSfxRuntimePrepared();
    const double startSecond = qBound(0.0, second, previewDurationSeconds());
    qtPreviewStartSecond_ = startSecond;
    qtPreviewPauseSecond_ = startSecond;
    qtPreviewLastTimelineSecond_ = startSecond;
    qtPreviewPendingTimelineSecond_ = startSecond;
    qtPreviewPendingTimelineCenterView_ = true;
    qtPreviewTimelineDirty_ = false;
    qtPreviewTimelineStartSecond_ = startSecond;
    qtPreviewTimelineCenterNextTick_ = true;
    qtPreviewTimelineElapsed_.restart();
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
        timelineView_->setPlayheadSeconds(startSecond, true);
    }
    if (previewCanvas_ != nullptr) {
        if (!resumeFromPause) {
            previewCanvas_->resetProfilingSession();
        }
        previewCanvas_->setPlayheadSeconds(startSecond);
    }
    if (!resumeFromPause && previewMediaController_ != nullptr) {
        previewMediaController_->resetProfilingSession();
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setPlaybackRate(previewPlaybackRate_);
        previewMediaController_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        previewMediaController_->setBackgroundTrackVolume(previewAudioSettings_.bgmVolume);
        previewMediaController_->setPlayheadSeconds(startSecond);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        previewSfxRuntime_->startBackgroundTrack(startSecond);
        if (previewSfxRuntime_->hasBackgroundTrack()
            && previewSfxRuntime_->isBackgroundTrackRunning()) {
            qtPreviewStartSecond_ = previewSfxRuntime_->backgroundPlaybackSecond();
            qtPreviewPauseSecond_ = qtPreviewStartSecond_;
            qtPreviewLastTimelineSecond_ = qtPreviewStartSecond_;
            qtPreviewPendingTimelineSecond_ = qtPreviewStartSecond_;
            qtPreviewPendingTimelineCenterView_ = true;
            qtPreviewTimelineDirty_ = false;
            qtPreviewTimelineStartSecond_ = qtPreviewStartSecond_;
            qtPreviewTimelineCenterNextTick_ = true;
            qtPreviewTimelineElapsed_.restart();
            qtPreviewPendingAudioCalibration_ = true;
            if (timelineView_ != nullptr) {
                timelineView_->setPlayheadSeconds(qtPreviewStartSecond_, true);
            }
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setPlayheadSeconds(qtPreviewStartSecond_);
            }
            if (previewMediaController_ != nullptr) {
                previewMediaController_->setPlayheadSeconds(qtPreviewStartSecond_);
            }
        } else {
            qtPreviewPendingAudioCalibration_ = false;
        }
        previewSfxRuntime_->resetCursor(qtPreviewStartSecond_, !resumeFromPause);
        if (!resumeFromPause) {
            previewSfxRuntime_->drainEvents(qtPreviewStartSecond_);
        }
        previewSfxRuntime_->syncTouchholdVoices(qtPreviewStartSecond_);
    } else {
        qtPreviewPendingAudioCalibration_ = false;
    }
    if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
        previewMediaController_->startPlayback(qtPreviewStartSecond_);
    }

    qtPreviewElapsed_.restart();
    qtPreviewPlaying_ = true;
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    if (previewCanvas_ != nullptr) {
        qtPreviewAwaitingFrameSwap_ = true;
        qtPreviewAwaitingFrameSwapSinceMs_ = qtPreviewWatchdogElapsed_.elapsed();
        previewCanvas_->update();
    }
    if (qtPreviewTimer_ != nullptr && !qtPreviewTimer_->isActive()) {
        qtPreviewTimer_->start();
    }
    if (qtPreviewTimelineTimer_ != nullptr && !qtPreviewTimelineTimer_->isActive()) {
        qtPreviewTimelineTimer_->start();
    }
    updatePreviewSliderPosition(startSecond);
    updatePauseButtonAppearance();
}

void MainWindow::stopQtPreviewPlayback(bool keepPosition)
{
    const bool wasPlaying = qtPreviewPlaying_;
    if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
        qtPreviewPauseSecond_ = previewSfxRuntime_->backgroundPlaybackSecond();
        previewSfxRuntime_->pauseBackgroundTrack();
    } else if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
        qtPreviewPauseSecond_ = previewMediaController_->currentPlaybackSecond();
    }
    if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
        previewMediaController_->pausePlayback();
    }
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->stop();
    }
    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->stop();
    }
    if (qtPreviewTimelineTimer_ != nullptr) {
        qtPreviewTimelineTimer_->stop();
    }
    if (!keepPosition) {
        qtPreviewPauseSecond_ = 0.0;
    }
    if (wasPlaying) {
        qtPreviewPendingTimelineSecond_ = qtPreviewPauseSecond_;
        qtPreviewPendingTimelineCenterView_ = false;
        qtPreviewTimelineDirty_ = true;
    }
    qtPreviewPlaying_ = false;
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    qtPreviewPendingAudioCalibration_ = false;
    flushQtPreviewTimelinePosition();
    syncTimelineToEditorCursor(true);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->stopAll();
    }
    if (runtimeDebugOutputEnabled_ && wasPlaying && previewCanvas_ != nullptr) {
        const QString summaryPath = previewCanvas_->writeProfilingSummaryToFile();
        if (!summaryPath.isEmpty() && previewMediaController_ != nullptr) {
            QFile file(summaryPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
                QTextStream stream(&file);
                stream << previewMediaController_->profilingSummaryLines();
            }
        }
    }
    updatePreviewSliderPosition(qtPreviewPauseSecond_);
    updatePreviewObjectStats(qtPreviewPauseSecond_);
    updatePauseButtonAppearance();
}

void MainWindow::applyQtPreviewPosition(double second, bool centerView)
{
    qtPreviewPauseSecond_ = second;
    if (!qtPreviewPlaying_
        && timelineView_ != nullptr
        && (qtPreviewLastTimelineSecond_ < 0.0 || qAbs(second - qtPreviewLastTimelineSecond_) >= (1.0 / 30.0))) {
        qtPreviewPendingTimelineSecond_ = second;
        qtPreviewPendingTimelineCenterView_ = qtPreviewPendingTimelineCenterView_ || centerView;
        qtPreviewTimelineDirty_ = true;
        flushQtPreviewTimelinePosition();
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(second);
    }
    updatePreviewSliderPosition(second);
    updatePreviewObjectStats(second);
}

void MainWindow::flushQtPreviewTimelinePosition()
{
    if (timelineView_ == nullptr) {
        return;
    }
    if (qtPreviewPlaying_) {
        const double second = qMax(
            0.0,
            qtPreviewTimelineStartSecond_ + ((qtPreviewTimelineElapsed_.elapsed() / 1000.0) * previewPlaybackRate_)
        );
        timelineView_->setPlayheadSeconds(second, qtPreviewTimelineCenterNextTick_);
        qtPreviewLastTimelineSecond_ = second;
        qtPreviewTimelineCenterNextTick_ = false;
        return;
    }
    if (!qtPreviewTimelineDirty_) {
        return;
    }
    timelineView_->setPlayheadSeconds(qtPreviewPendingTimelineSecond_, qtPreviewPendingTimelineCenterView_);
    qtPreviewLastTimelineSecond_ = qtPreviewPendingTimelineSecond_;
    qtPreviewPendingTimelineCenterView_ = false;
    qtPreviewTimelineDirty_ = false;
}

void MainWindow::onQtPreviewTick()
{
    if (!qtPreviewPlaying_) {
        return;
    }
    if (qtPreviewPendingAudioCalibration_ && previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
        const double calibratedSecond = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
        qtPreviewStartSecond_ = calibratedSecond;
        qtPreviewPauseSecond_ = calibratedSecond;
        qtPreviewLastTimelineSecond_ = -1.0;
        qtPreviewElapsed_.restart();
        qtPreviewTimelineStartSecond_ = calibratedSecond;
        qtPreviewTimelineCenterNextTick_ = true;
        qtPreviewTimelineElapsed_.restart();
        if (previewCanvas_ != nullptr) {
            previewCanvas_->noteTickForProfiling();
        }
        applyQtPreviewPosition(calibratedSecond, true);
        if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
            previewMediaController_->setPlayheadSeconds(calibratedSecond);
        }
        previewSfxRuntime_->drainEvents(calibratedSecond);
        previewSfxRuntime_->syncTouchholdVoices(calibratedSecond);
        qtPreviewPendingAudioCalibration_ = false;
        if (previewCanvas_ != nullptr) {
            qtPreviewAwaitingFrameSwap_ = true;
            qtPreviewAwaitingFrameSwapSinceMs_ = qtPreviewWatchdogElapsed_.elapsed();
        }
        return;
    }
    const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
    double second = qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
    if (previewMediaController_ != nullptr) {
        previewMediaController_->syncPlayback(second);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->syncBackgroundTrack(second);
    }
    const double duration = previewDurationSeconds();
    if (duration > 0.0 && second > duration) {
        second = duration;
        applyQtPreviewPosition(second, true);
        if (previewSfxRuntime_ != nullptr) {
            previewSfxRuntime_->drainEvents(second);
        }
        stopQtPreviewPlayback(true);
        statusBar()->showMessage("Qt preview reached the end of current timeline.");
        return;
    }

    if (previewCanvas_ != nullptr) {
        previewCanvas_->noteTickForProfiling();
    }
    applyQtPreviewPosition(second, true);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->drainEvents(second);
    }
    if (previewCanvas_ != nullptr) {
        qtPreviewAwaitingFrameSwap_ = true;
        qtPreviewAwaitingFrameSwapSinceMs_ = qtPreviewWatchdogElapsed_.elapsed();
    }
}

void MainWindow::jumpToNearestTimelineNote(double second, int lane)
{
    if (timelineCursorNotes_.isEmpty()) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }

    const TimelineCursorNote* best = nullptr;
    bool foundSameLane = false;
    const bool preferLane = lane >= 1;
    const double target = qMax(0.0, second);

    for (const TimelineCursorNote& note : timelineCursorNotes_) {
        const bool sameLane = preferLane && note.lane == lane;
        if (preferLane) {
            if (sameLane && !foundSameLane) {
                best = &note;
                foundSameLane = true;
                continue;
            }
            if (!sameLane && foundSameLane) {
                continue;
            }
            if (!sameLane && !foundSameLane && best != nullptr) {
                // Still in all-note fallback mode.
            }
        }

        if (best == nullptr) {
            best = &note;
            continue;
        }

        const double bestDelta = qAbs(best->second - target);
        const double noteDelta = qAbs(note.second - target);
        if (noteDelta + 1e-9 < bestDelta) {
            best = &note;
            continue;
        }
        if (qAbs(noteDelta - bestDelta) <= 1e-9) {
            if (note.line < best->line || (note.line == best->line && note.col < best->col)) {
                best = &note;
            }
        }
    }

    if (best == nullptr) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }

    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextBlock block = editor->document()->findBlockByNumber(best->line - 1);
    if (!block.isValid()) {
        jumpToLocation(best->line, best->col);
    } else {
        const QString blockText = block.text();
        const int lineLength = blockText.size();
        int localIndex = qBound(0, best->col - 1, qMax(0, lineLength));
        const int commentIndex = blockText.indexOf(QStringLiteral("||"));
        const int scanEnd = (commentIndex >= 0) ? commentIndex : lineLength;
        if (localIndex > scanEnd) {
            localIndex = scanEnd;
        }
        auto isDelimiter = [](QChar ch) {
            return ch.isSpace() || ch == QChar('/') || ch == QChar(',') || ch == QChar('`');
        };

        int tokenStart = localIndex;
        while (tokenStart > 0 && !isDelimiter(blockText.at(tokenStart - 1))) {
            --tokenStart;
        }
        int tokenEnd = localIndex;
        while (tokenEnd < scanEnd && !isDelimiter(blockText.at(tokenEnd))) {
            ++tokenEnd;
        }
        if (tokenEnd <= tokenStart) {
            tokenStart = qBound(0, localIndex, lineLength);
            tokenEnd = qMin(lineLength, tokenStart + 1);
        }

        QTextCursor cursor(editor->document());
        cursor.setPosition(block.position() + tokenStart);
        cursor.setPosition(block.position() + tokenEnd, QTextCursor::KeepAnchor);
        editor->setTextCursor(cursor);
        if (QScrollBar* vbar = editor->verticalScrollBar()) {
            const QRect caretRect = editor->cursorRect();
            const int centeredValue = vbar->value() + caretRect.center().y() - (editor->viewport()->height() / 2);
            vbar->setValue(qBound(vbar->minimum(), centeredValue, vbar->maximum()));
        }
        editor->setFocus();
    }
    statusBar()->showMessage(
        QString("Timeline jump: %1s -> L%2 C%3")
            .arg(target, 0, 'f', 3)
            .arg(best->line)
            .arg(best->col)
    );
}

