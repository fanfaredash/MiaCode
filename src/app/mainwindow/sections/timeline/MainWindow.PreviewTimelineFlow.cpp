namespace {

constexpr double kTimelineZeroSecondTolerance = 1e-6;

QString workspaceSwapPreviewPanelStyleSheet(bool swapped)
{
    const QString borderRule = swapped
        ? QStringLiteral(" border-right: 1px solid #DEE4EC;")
        : QStringLiteral(" border-left: 1px solid #DEE4EC;");
    return QStringLiteral(
        "QWidget#PreviewPanel {"
        " background: #F5F7FA;%1"
        "}"
        "QFrame#PreviewCanvasFrame {"
        " background: #000000;"
        " border: 1px solid #D8E0EA;"
        "}"
        "QFrame#PreviewControlCard, QFrame#PreviewStatsCard {"
        " background: #EDF2F8;"
        " border: 1px solid #D5E0EC;"
        " border-radius: 10px;"
        "}"
        "QFrame#PreviewControls {"
        " background: transparent;"
        " border: none;"
        "}"
        "QFrame#PreviewStats {"
        " background: transparent;"
        " border: none;"
        "}"
        "QLabel#PreviewStatChip {"
        " color: #213246;"
        " background: #F6F9FD;"
        " border: 1px solid #D3DEEA;"
        " border-radius: 9px;"
        " padding: 2px 8px;"
        " font-weight: 600;"
        "}"
        "QLabel#PreviewStatChipTotal {"
        " color: #213246;"
        " background: #F0F4FA;"
        " border: 1px solid #CBD8E6;"
        " border-radius: 9px;"
        " padding: 2px 8px;"
        " font-weight: 700;"
        "}"
        "QToolButton#PreviewControlButton {"
        " color: #223042;"
        " padding: 5px 8px;"
        " min-height: 28px;"
        " border: 1px solid #D8E0EA;"
        " border-radius: 6px;"
        " background: transparent;"
        " font-weight: 600;"
        "}"
        "QToolButton#PreviewControlButton:hover { background: #F5F8FC; border-color: #BCD0E5; }"
        "QToolButton#PreviewControlButton:pressed { background: #E8F1FB; }"
        "QSlider::groove:horizontal {"
        " height: 6px;"
        " background: #D8E0EA;"
        " border-radius: 3px;"
        "}"
        "QSlider::sub-page:horizontal {"
        " background: #2E77D0;"
        " border-radius: 3px;"
        "}"
        "QSlider::handle:horizontal {"
        " width: 12px;"
        " margin: -4px 0;"
        " border-radius: 6px;"
        " background: #FFFFFF;"
        " border: 1px solid #AFC0D6;"
        "}"
    ).arg(borderRule);
}

double shiftedTimelineSecond(double second, double offsetSeconds)
{
    if (!qIsFinite(second) || !qIsFinite(offsetSeconds)) {
        return second;
    }
    return second + offsetSeconds;
}

QVector<TimelineBeatMarker> shiftedBeatMarkers(
    const QVector<TimelineBeatMarker>& beatMarkers,
    double offsetSeconds
)
{
    QVector<TimelineBeatMarker> shifted = beatMarkers;
    for (TimelineBeatMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds);
    }
    return shifted;
}

QVector<TimelineNoteMarker> shiftedNoteMarkers(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double offsetSeconds
)
{
    QVector<TimelineNoteMarker> shifted = noteMarkers;
    for (TimelineNoteMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds);
        if (marker.endSecond >= 0.0) {
            marker.endSecond = shiftedTimelineSecond(marker.endSecond, offsetSeconds);
        }
        if (marker.slideTraceSecond >= 0.0) {
            marker.slideTraceSecond = shiftedTimelineSecond(marker.slideTraceSecond, offsetSeconds);
        }
        if (marker.availableSecond >= 0.0) {
            marker.availableSecond = shiftedTimelineSecond(marker.availableSecond, offsetSeconds);
        }
        for (double& shootSecond : marker.slideSegmentShootSeconds) {
            shootSecond = shiftedTimelineSecond(shootSecond, offsetSeconds);
        }
    }
    return shifted;
}

std::pair<int, int> lineColForTextOffset(const QString& text, int offset)
{
    const int boundedOffset = qBound(0, offset, text.size());
    int line = 1;
    int col = 1;
    for (int index = 0; index < boundedOffset; ++index) {
        if (text.at(index) == QChar('\n')) {
            ++line;
            col = 1;
            continue;
        }
        ++col;
    }
    return {line, col};
}

}  // namespace
void MainWindow::refreshWaveformCache()
{
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackOffsetSeconds(0.0);
    }
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setTimelineOffsetSeconds(0.0);
    }
    if (timelineView_ == nullptr) {
        return;
    }
    double audioDurationSeconds = 0.0;
    const QVector<float> waveform = buildWaveformPeaks(lastTrackPath_, &audioDurationSeconds);
    previewTrackDurationSeconds_ = qMax(0.0, audioDurationSeconds);
    timelineView_->setWaveformData(waveform, 0.0, audioDurationSeconds);
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
    refreshTimelineMetadata();
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
        clearValidationCache();
        clearValidationErrors();
        clearValidationDecorations();
        stopQtPreviewPlayback(false);
        if (latencyDetectorDialog_ != nullptr) {
            latencyDetectorDialog_->close();
            latencyDetectorDialog_.clear();
        }
    }
    currentFilePath_ = normalizedPath;
    lastSessionFilePath_ = currentFilePath_;
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
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setStageMediaAvailable(probeStageMediaAvailable(currentFilePath_));
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
    refreshTimelineMetadata();
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
    if (muriRefreshTimer_ != nullptr) {
        muriRefreshTimer_->stop();
    }
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

    QElapsedTimer totalTimer;
    totalTimer.start();
    qint64 stageStartNs = totalTimer.nsecsElapsed();
    const auto finishStageMs = [&totalTimer, &stageStartNs]() -> double {
        const qint64 nowNs = totalTimer.nsecsElapsed();
        const double elapsedMs = static_cast<double>(nowNs - stageStartNs) / 1000000.0;
        stageStartNs = nowNs;
        return elapsedMs;
    };

    const QString chartText = activeChartText();
    SimaiNativeParseResult nativeResult = SimaiNativeParser::parseForTimeline(chartText);
    const double parseMs = finishStageMs();
    const double firstSeconds = parsedFirstSeconds();
    QVector<TimelineBeatMarker> beatMarkers = shiftedBeatMarkers(nativeResult.beatMarkers, firstSeconds);
    QVector<TimelineNoteMarker> noteMarkers = shiftedNoteMarkers(nativeResult.noteMarkers, firstSeconds);
    const auto appendCursorNote = [](QVector<TimelineCursorNote>* target, int line, int col, int lane, double second) {
        if (target == nullptr) {
            return;
        }
        TimelineCursorNote cursorNote;
        cursorNote.line = qMax(1, line);
        cursorNote.col = qMax(1, col);
        cursorNote.lane = lane;
        cursorNote.second = second;
        target->append(cursorNote);
    };
    const auto sortCursorNotes = [](QVector<TimelineCursorNote>* notes) {
        if (notes == nullptr) {
            return;
        }
        std::sort(notes->begin(), notes->end(), [](const TimelineCursorNote& a, const TimelineCursorNote& b) {
            if (a.line != b.line) {
                return a.line < b.line;
            }
            if (a.col != b.col) {
                return a.col < b.col;
            }
            return a.second < b.second;
        });
    };

    timelineCursorNotes_.clear();
    timelineCursorNotes_.reserve(noteMarkers.size() + beatMarkers.size() + 1);
    for (const TimelineNoteMarker& marker : noteMarkers) {
        appendCursorNote(&timelineCursorNotes_, marker.sourceLine, marker.sourceCol, marker.lane, marker.second);
    }
    for (const TimelineBeatMarker& marker : beatMarkers) {
        appendCursorNote(&timelineCursorNotes_, marker.sourceLine, marker.sourceCol, -1, marker.second);
    }

    bool hasRawZeroAnchor = false;
    for (const TimelineNoteMarker& marker : nativeResult.noteMarkers) {
        if (qAbs(marker.second) <= kTimelineZeroSecondTolerance) {
            hasRawZeroAnchor = true;
            break;
        }
    }
    if (!hasRawZeroAnchor) {
        for (const TimelineBeatMarker& marker : nativeResult.beatMarkers) {
            if (qAbs(marker.second) <= kTimelineZeroSecondTolerance) {
                hasRawZeroAnchor = true;
                break;
            }
        }
    }
    if (!hasRawZeroAnchor) {
        for (const TimelineCursorNote& note : timelineCursorNotes_) {
            if (qAbs(note.second - shiftedTimelineSecond(0.0, firstSeconds)) <= kTimelineZeroSecondTolerance) {
                hasRawZeroAnchor = true;
                break;
            }
        }
    }
    if (!hasRawZeroAnchor) {
        const int firstCommaOffset = chartText.indexOf(QLatin1Char(','));
        if (firstCommaOffset >= 0) {
            const auto [line, col] = lineColForTextOffset(chartText, firstCommaOffset);
            appendCursorNote(&timelineCursorNotes_, line, col, -1, shiftedTimelineSecond(0.0, firstSeconds));
        }
    }
    sortCursorNotes(&timelineCursorNotes_);

    previewFollowCursorNotes_.clear();
    switch (previewFollowMode_) {
    case PreviewFollowMode::EveryComma:
        previewFollowCursorNotes_ = timelineCursorNotes_;
        break;
    case PreviewFollowMode::NonEmptyComma: {
        previewFollowCursorNotes_.reserve(noteMarkers.size() + beatMarkers.size());
        QHash<qint64, bool> noteSecondKeys;
        noteSecondKeys.reserve(noteMarkers.size());
        for (const TimelineNoteMarker& marker : noteMarkers) {
            appendCursorNote(&previewFollowCursorNotes_, marker.sourceLine, marker.sourceCol, marker.lane, marker.second);
            noteSecondKeys.insert(qRound64(marker.second * 1000000.0), true);
        }
        for (const TimelineBeatMarker& marker : beatMarkers) {
            const qint64 secondKey = qRound64(marker.second * 1000000.0);
            if (!noteSecondKeys.contains(secondKey)) {
                continue;
            }
            appendCursorNote(&previewFollowCursorNotes_, marker.sourceLine, marker.sourceCol, -1, marker.second);
        }
        sortCursorNotes(&previewFollowCursorNotes_);
        break;
    }
    case PreviewFollowMode::LineOnly: {
        previewFollowCursorNotes_.reserve(timelineCursorNotes_.size());
        QHash<int, int> lineToIndex;
        for (const TimelineCursorNote& note : timelineCursorNotes_) {
            const int existingIndex = lineToIndex.value(note.line, -1);
            if (existingIndex < 0) {
                lineToIndex.insert(note.line, previewFollowCursorNotes_.size());
                previewFollowCursorNotes_.append(note);
                continue;
            }

            TimelineCursorNote& existing = previewFollowCursorNotes_[existingIndex];
            if (note.second + kTimelineZeroSecondTolerance < existing.second
                || (qAbs(note.second - existing.second) <= kTimelineZeroSecondTolerance && note.col < existing.col)) {
                existing = note;
            }
        }
        sortCursorNotes(&previewFollowCursorNotes_);
        break;
    }
    }
    const double cursorModelMs = finishStageMs();

    double durationSeconds = qMax(0.0, nativeResult.durationSeconds + firstSeconds);
    {
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
    lastTimelineParseDifficultyId_ = activeDifficultyId();
    lastTimelineParseChartText_ = chartText;
    lastTimelineParseResult_ = nativeResult;

    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->configureTimeline(noteMarkers);
        if (qtPreviewPlaying_) {
            double currentSecond = qMax(0.0, qtPreviewPauseSecond_);
            if (previewSfxRuntime_->hasBackgroundTrack()) {
                currentSecond = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
            } else if (previewMediaController_ != nullptr && previewMediaController_->hasVideoMedia()) {
                currentSecond = qMax(0.0, previewMediaController_->currentPlaybackSecond());
            }
            qtPreviewPauseSecond_ = currentSecond;
            previewSfxRuntime_->resetCursor(currentSecond, false);
            previewSfxRuntime_->syncTouchholdVoices(currentSecond);
        }
    }
    const double audioTimelineMs = finishStageMs();
    refreshPreviewObjectStatsTotals(noteMarkers);

    timelineView_->setTimelineData(beatMarkers, noteMarkers, durationSeconds);
    updatePreviewSliderRange();
    const QByteArray newSignature = noteMarkerSignature(noteMarkers);
    if (previewCanvas_ != nullptr) {
        if (newSignature != lastPreviewNoteMarkerSignature_) {
            previewCanvas_->setNoteMarkers(noteMarkers);
            lastPreviewNoteMarkerSignature_ = newSignature;
        }
    }
    const double fastUiMs = finishStageMs();
    scheduleDeferredMuriRefresh(noteMarkers, newSignature);

    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "edit/metadata_perf",
            QStringLiteral(
                "total=%1ms parse=%2ms cursor_model=%3ms audio_timeline=%4ms fast_ui=%5ms notes=%6 beats=%7"
            )
                .arg(QString::number(static_cast<double>(totalTimer.nsecsElapsed()) / 1000000.0, 'f', 2))
                .arg(QString::number(parseMs, 'f', 2))
                .arg(QString::number(cursorModelMs, 'f', 2))
                .arg(QString::number(audioTimelineMs, 'f', 2))
                .arg(QString::number(fastUiMs, 'f', 2))
                .arg(noteMarkers.size())
                .arg(beatMarkers.size())
        );
    }
}

void MainWindow::scheduleDeferredMuriRefresh(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QByteArray& noteMarkerSignature)
{
    pendingMuriNoteMarkers_ = noteMarkers;
    pendingMuriNoteMarkerSignature_ = noteMarkerSignature;
    if (muriRefreshTimer_ == nullptr) {
        refreshDeferredMuriDiagnostics();
        return;
    }
    muriRefreshTimer_->stop();
    muriRefreshTimer_->start();
}

void MainWindow::refreshDeferredMuriDiagnostics()
{
    if (!hasActiveDifficulty()) {
        return;
    }

    const QVector<TimelineNoteMarker> noteMarkers = pendingMuriNoteMarkers_;
    const QByteArray noteMarkerSignature = pendingMuriNoteMarkerSignature_;
    if (previewCanvas_ != nullptr
        && !noteMarkerSignature.isEmpty()
        && noteMarkerSignature != lastPreviewNoteMarkerSignature_) {
        return;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    qint64 stageStartNs = totalTimer.nsecsElapsed();
    const auto finishStageMs = [&totalTimer, &stageStartNs]() -> double {
        const qint64 nowNs = totalTimer.nsecsElapsed();
        const double elapsedMs = static_cast<double>(nowNs - stageStartNs) / 1000000.0;
        stageStartNs = nowNs;
        return elapsedMs;
    };

    muriAnalysisReport_ = MuriAnalyzer::analyze(noteMarkers);
    const double analyzeMs = finishStageMs();
    rebuildStaticMuriReferences(noteMarkers);
    const double staticRefMs = finishStageMs();

    if (timelineView_ != nullptr) {
        timelineView_->setMuriAnalysisReport(muriAnalysisReport_);
    }
    refreshMuriDiagnosticsPanel();
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setMuriAnalysisReport(muriAnalysisReport_);
        previewCanvas_->setMuriRenderOptions(muriRenderOptions_);
    }
    const double uiApplyMs = finishStageMs();

    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "edit/muri_perf",
            QStringLiteral("total=%1ms analyze=%2ms static_refs=%3ms ui_apply=%4ms diagnostics=%5 static=%6")
                .arg(QString::number(static_cast<double>(totalTimer.nsecsElapsed()) / 1000000.0, 'f', 2))
                .arg(QString::number(analyzeMs, 'f', 2))
                .arg(QString::number(staticRefMs, 'f', 2))
                .arg(QString::number(uiApplyMs, 'f', 2))
                .arg(muriAnalysisReport_.diagnostics.size())
                .arg(muriStaticReferences_.size())
        );
    }
}

void MainWindow::rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers)
{
    muriStaticReferences_ = miacode::muri::buildStaticMuriReferences(
        noteMarkers,
        static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0);
}

bool MainWindow::findCursorNoteForTextPosition(
    const QVector<TimelineCursorNote>& notes,
    int line,
    int col,
    int* indexOut
) const
{
    if (notes.isEmpty()) {
        return false;
    }

    for (int index = 0; index < notes.size(); ++index) {
        const TimelineCursorNote& note = notes.at(index);
        if (note.line > line || (note.line == line && note.col >= col)) {
            if (indexOut != nullptr) {
                *indexOut = index;
            }
            return true;
        }
    }

    if (indexOut != nullptr) {
        *indexOut = notes.size() - 1;
    }
    return true;
}

bool MainWindow::findTimelineCursorNoteForTextPosition(int line, int col, int* indexOut) const
{
    return findCursorNoteForTextPosition(timelineCursorNotes_, line, col, indexOut);
}

double MainWindow::timelineSecondForCursor(int line, int col) const
{
    int noteIndex = -1;
    if (!findTimelineCursorNoteForTextPosition(line, col, &noteIndex) || noteIndex < 0) {
        return -1.0;
    }
    return timelineCursorNotes_.at(noteIndex).second;
}

void MainWindow::seekTimelineToCursor(int line, int col)
{
    if (timelineView_ == nullptr) {
        return;
    }
    const double second = timelineSecondForCursor(line, col);
    if (second < -1e-6) {
        timelineView_->setCursorSeconds(second, true);
        return;
    }
    if (second < 0.0) {
        return;
    }
    timelineView_->setCursorSeconds(second, true);
}

void MainWindow::syncTimelineToEditorCursor(bool centerView)
{
    if (suppressTimelineCursorSync_ || !hasActiveDifficulty() || timelineView_ == nullptr) {
        return;
    }
    const auto [line, col] = currentCursorLineCol();
    const double second = timelineSecondForCursor(line, col);
    if (second < -1e-6) {
        timelineView_->setCursorSeconds(second, centerView);
        return;
    }
    if (second < 0.0) {
        return;
    }
    timelineView_->setCursorSeconds(second, centerView);
}

void MainWindow::navigateTimelineToSecond(double second, bool focusEditor)
{
    if (timelineView_ == nullptr) {
        return;
    }

    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    int line = 1;
    int col = 1;
    double noteSecond = -1.0;
    const bool hasNearestNote = resolveNearestTimelineNote(clampedSecond, -1, &line, &col, &noteSecond);
    const bool previousSuppressState = suppressTimelineCursorSync_;
    suppressTimelineCursorSync_ = true;

    previewPendingSeekSecond_ = clampedSecond;
    previewPendingSeekCenterView_ = true;
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->stop();
    }
    seekPreviewToSecond(clampedSecond, true);
    timelineView_->setCursorSeconds(noteSecond >= 0.0 ? noteSecond : clampedSecond);

    if (hasNearestNote) {
        moveEditorCursorToTimelineLocation(line, col, false, focusEditor, true, true);
    }

    suppressTimelineCursorSync_ = previousSuppressState;

    if (hasNearestNote) {
        statusBar()->showMessage(
            QString("Timeline jump: %1s -> L%2 C%3")
                .arg(clampedSecond, 0, 'f', 3)
                .arg(line)
                .arg(col)
        );
    } else {
        statusBar()->showMessage(
            QString("Timeline centered at %1s (source location unavailable).")
                .arg(clampedSecond, 0, 'f', 3)
        );
    }
}

bool MainWindow::resolveNearestCursorNote(
    const QVector<TimelineCursorNote>& notes,
    double second,
    int lane,
    int* line,
    int* col,
    double* noteSecond
) const
{
    if (notes.isEmpty()) {
        return false;
    }
    const TimelineCursorNote* best = nullptr;
    bool foundSameLane = false;
    const bool preferLane = lane >= 1;
    const double target = qMax(0.0, second);

    for (const TimelineCursorNote& note : notes) {
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
        return false;
    }
    if (line != nullptr) {
        *line = best->line;
    }
    if (col != nullptr) {
        *col = best->col;
    }
    if (noteSecond != nullptr) {
        *noteSecond = best->second;
    }
    return true;
}

bool MainWindow::resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const
{
    return resolveNearestCursorNote(timelineCursorNotes_, second, lane, line, col, noteSecond);
}

bool MainWindow::resolveCursorNoteFromAnchor(
    const QVector<TimelineCursorNote>& notes,
    double second,
    int anchorLine,
    int anchorCol,
    int lane,
    int* line,
    int* col,
    double* noteSecond
) const
{
    if (notes.isEmpty()) {
        return false;
    }

    const TimelineCursorNote* best = nullptr;
    const TimelineCursorNote* fallbackLast = nullptr;
    const TimelineCursorNote* fallbackLastSameLane = nullptr;
    bool foundSameLane = false;
    const bool preferLane = lane >= 1;
    const double target = qMax(0.0, second);

    for (const TimelineCursorNote& note : notes) {
        fallbackLast = &note;
        if (preferLane && note.lane == lane) {
            fallbackLastSameLane = &note;
        }
        if (note.line < anchorLine || (note.line == anchorLine && note.col < anchorCol)) {
            continue;
        }

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
            if (!sameLane && !foundSameLane && best == nullptr) {
                best = &note;
                continue;
            }
        } else if (best == nullptr) {
            best = &note;
            continue;
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
        best = (preferLane && fallbackLastSameLane != nullptr) ? fallbackLastSameLane : fallbackLast;
    }
    if (best == nullptr) {
        return false;
    }
    if (line != nullptr) {
        *line = best->line;
    }
    if (col != nullptr) {
        *col = best->col;
    }
    if (noteSecond != nullptr) {
        *noteSecond = best->second;
    }
    return true;
}

bool MainWindow::moveEditorCursorToTimelineLocation(
    int line,
    int col,
    bool selectToken,
    bool focusEditor,
    bool centerView,
    bool suppressSignals
)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    if (editor == nullptr || editor->document() == nullptr) {
        return false;
    }

    QTextBlock block = editor->document()->findBlockByNumber(line - 1);
    if (!block.isValid()) {
        jumpToLocation(line, col);
        return true;
    }

    const QString blockText = block.text();
    const int lineLength = blockText.size();
    int localIndex = qBound(0, col - 1, qMax(0, lineLength));

    QTextCursor cursor(editor->document());
    if (selectToken) {
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

        cursor.setPosition(block.position() + tokenStart);
        cursor.setPosition(block.position() + tokenEnd, QTextCursor::KeepAnchor);
    } else {
        cursor.setPosition(block.position() + localIndex);
    }

    if (suppressSignals) {
        QSignalBlocker blocker(editor);
        editor->setTextCursor(cursor);
    } else {
        editor->setTextCursor(cursor);
    }

    if (centerView) {
        if (QScrollBar* vbar = editor->verticalScrollBar()) {
            const QRect caretRect = editor->cursorRect();
            const int centeredValue = vbar->value() + caretRect.center().y() - (editor->viewport()->height() / 2);
            vbar->setValue(qBound(vbar->minimum(), centeredValue, vbar->maximum()));
        }
    }
    if (focusEditor) {
        editor->setFocus();
        clearPreviewFollowDecoration();
    } else {
        setPreviewFollowDecoration(line, col);
    }
    return true;
}

bool MainWindow::resolveTimelineNoteFromCursorAnchor(
    double second,
    int anchorLine,
    int anchorCol,
    int lane,
    int* line,
    int* col,
    double* noteSecond
) const
{
    return resolveCursorNoteFromAnchor(
        timelineCursorNotes_,
        second,
        anchorLine,
        anchorCol,
        lane,
        line,
        col,
        noteSecond
    );
}

void MainWindow::syncEditorCursorToPreviewSecond(double second, bool centerView)
{
    if (suppressTimelineCursorSync_ || timelineView_ == nullptr || !timelineView_->followPreviewEnabled() || !hasActiveDifficulty()) {
        if (timelineView_ == nullptr || !hasActiveDifficulty() || !timelineView_->followPreviewEnabled()) {
            clearPreviewFollowDecoration();
        }
        return;
    }

    const auto [currentLine, currentCol] = currentCursorLineCol();
    int line = 1;
    int col = 1;
    double noteSecond = -1.0;
    const bool useAbsoluteSeekAnchor = !qtPreviewPlaying_;
    const bool resolved = useAbsoluteSeekAnchor
        ? resolveNearestCursorNote(previewFollowCursorNotes_, second, -1, &line, &col, &noteSecond)
        : resolveCursorNoteFromAnchor(
            previewFollowCursorNotes_,
            second,
            currentLine,
            currentCol,
            -1,
            &line,
            &col,
            &noteSecond);
    if (!resolved) {
        return;
    }

    const bool lineOnlyMode = previewFollowMode_ == PreviewFollowMode::LineOnly;
    const bool alreadyAtAnchor = lineOnlyMode
        ? (currentLine == line)
        : (currentLine == line && currentCol == col);

    if (alreadyAtAnchor) {
        if (lineOnlyMode) {
            clearPreviewFollowDecoration();
        } else {
            setPreviewFollowDecoration(line, col);
        }
        timelineView_->setCursorSeconds(noteSecond >= 0.0 ? noteSecond : qMax(0.0, second));
        return;
    }

    if (moveEditorCursorToTimelineLocation(line, col, false, false, centerView, false)) {
        if (lineOnlyMode) {
            clearPreviewFollowDecoration();
        }
        timelineView_->setCursorSeconds(noteSecond >= 0.0 ? noteSecond : qMax(0.0, second));
    }
}

double MainWindow::previewDurationSeconds() const
{
    double duration = 0.0;
    if (timelineView_ != nullptr) {
        duration = qMax(duration, timelineView_->durationSeconds());
    }
    if (previewTrackDurationSeconds_ > 0.0) {
        duration = qMax(duration, previewTrackDurationSeconds_ + 3.0);
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

double MainWindow::normalizedPreviewCanvasAspectRatio(double ratio) const
{
    if (!qIsFinite(ratio)) {
        return 1.0;
    }
    return qBound(1.0, ratio, 3.0);
}

MainWindow::PreviewCanvasFrameRateMode MainWindow::previewCanvasFrameRateModeFromStorageValue(const QString& value) const
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QLatin1String("120") || normalized == QLatin1String("120fps")) {
        return PreviewCanvasFrameRateMode::Fps120;
    }
    if (normalized == QLatin1String("display")
        || normalized == QLatin1String("display_max")
        || normalized == QLatin1String("screen")
        || normalized == QLatin1String("unlimited")) {
        return PreviewCanvasFrameRateMode::DisplayRefresh;
    }
    return PreviewCanvasFrameRateMode::Fps60;
}

QString MainWindow::previewCanvasFrameRateModeStorageValue() const
{
    switch (previewCanvasFrameRateMode_) {
    case PreviewCanvasFrameRateMode::Fps120:
        return QStringLiteral("120");
    case PreviewCanvasFrameRateMode::DisplayRefresh:
        return QStringLiteral("display_max");
    case PreviewCanvasFrameRateMode::Fps60:
    default:
        return QStringLiteral("60");
    }
}

MainWindow::PreviewFollowMode MainWindow::previewFollowModeFromStorageValue(const QString& value) const
{
    Q_UNUSED(value);
    return PreviewFollowMode::NonEmptyComma;
}

QString MainWindow::previewFollowModeStorageValue() const
{
    return QStringLiteral("non_empty");
}

void MainWindow::setPreviewFollowMode(PreviewFollowMode mode, bool persistState)
{
    mode = PreviewFollowMode::NonEmptyComma;
    const bool changed = previewFollowMode_ != mode;
    previewFollowMode_ = mode;
    if (hasActiveDifficulty()) {
        refreshTimelineMetadata();
    } else {
        previewFollowCursorNotes_.clear();
    }

    if (timelineView_ != nullptr && timelineView_->followPreviewEnabled() && hasActiveDifficulty()) {
        double second = qMax(0.0, qtPreviewPauseSecond_);
        if (qtPreviewPlaying_) {
            if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
                second = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
            } else if (previewMediaController_ != nullptr) {
                second = qMax(0.0, previewMediaController_->currentPlaybackSecond());
            }
        }
        syncEditorCursorToPreviewSecond(second, false);
    } else {
        clearPreviewFollowDecoration();
    }

    if ((changed || persistState) && persistState) {
        saveProjectRenderState();
        savePortableState();
    }
}

double MainWindow::currentPreviewCanvasRefreshRate() const
{
    QScreen* targetScreen = screen();
    if (windowHandle() != nullptr && windowHandle()->screen() != nullptr) {
        targetScreen = windowHandle()->screen();
    }
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    const double refreshRate = targetScreen != nullptr ? targetScreen->refreshRate() : 0.0;
    if (!qIsFinite(refreshRate) || refreshRate < 1.0) {
        return 60.0;
    }
    return refreshRate;
}

bool MainWindow::previewCanvasUsesFrameSwappedPacing() const
{
    return previewCanvasFrameRateMode_ == PreviewCanvasFrameRateMode::DisplayRefresh;
}

qint64 MainWindow::previewCanvasTargetFrameIntervalNs() const
{
    switch (previewCanvasFrameRateMode_) {
    case PreviewCanvasFrameRateMode::Fps120:
        return 1000000000LL / 120LL;
    case PreviewCanvasFrameRateMode::DisplayRefresh:
        return qMax<qint64>(1LL, qRound64(1000000000.0 / currentPreviewCanvasRefreshRate()));
    case PreviewCanvasFrameRateMode::Fps60:
    default:
        return 1000000000LL / 60LL;
    }
}

void MainWindow::resetQtPreviewFixedFramePacing()
{
    qtPreviewNextFixedTickDueNs_ = -1;
    if (previewCanvasUsesFrameSwappedPacing()) {
        return;
    }
    qtPreviewNextFixedTickDueNs_ = qtPreviewWatchdogElapsed_.nsecsElapsed() + previewCanvasTargetFrameIntervalNs();
}

void MainWindow::scheduleNextQtPreviewTick()
{
    if (qtPreviewTimer_ == nullptr || !qtPreviewPlaying_) {
        return;
    }
    if (previewCanvasUsesFrameSwappedPacing()) {
        qtPreviewTimer_->start(qMax(1, qtPreviewTimer_->interval()));
        return;
    }
    if (qtPreviewNextFixedTickDueNs_ < 0) {
        resetQtPreviewFixedFramePacing();
    }
    const qint64 nowNs = qtPreviewWatchdogElapsed_.nsecsElapsed();
    const qint64 delayNs = qMax<qint64>(0, qtPreviewNextFixedTickDueNs_ - nowNs);
    const int delayMs = delayNs <= 0 ? 0 : qMax(1, static_cast<int>((delayNs + 999999LL) / 1000000LL));
    qtPreviewTimer_->start(delayMs);
}

void MainWindow::requestNextDisplayRefreshPreviewFrame()
{
    if (!qtPreviewPlaying_
        || legacyPygamePreviewEnabled_
        || previewCanvas_ == nullptr
        || !previewCanvasUsesFrameSwappedPacing()) {
        return;
    }
    qtPreviewAwaitingFrameSwap_ = true;
    qtPreviewAwaitingFrameSwapSinceMs_ = qtPreviewWatchdogElapsed_.elapsed();
    previewCanvas_->update();
    scheduleNextQtPreviewTick();
}

void MainWindow::refreshPreviewFrameRateTimers()
{
    const int intervalMs = qMax(1, qRound(static_cast<double>(previewCanvasTargetFrameIntervalNs()) / 1000000.0));

    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->setInterval(intervalMs);
    }
    if (qtPreviewTimelineTimer_ != nullptr) {
        qtPreviewTimelineTimer_->setInterval(intervalMs);
    }
}

void MainWindow::setPreviewCanvasFrameRateMode(PreviewCanvasFrameRateMode mode, bool persistState)
{
    if (previewCanvasFrameRateMode_ == mode) {
        refreshPreviewFrameRateTimers();
        return;
    }
    previewCanvasFrameRateMode_ = mode;
    refreshPreviewFrameRateTimers();
    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->stop();
    }
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    resetQtPreviewFixedFramePacing();
    if (qtPreviewPlaying_) {
        if (previewCanvas_ != nullptr && !legacyPygamePreviewEnabled_ && previewCanvasUsesFrameSwappedPacing()) {
            requestNextDisplayRefreshPreviewFrame();
        } else {
            scheduleNextQtPreviewTick();
        }
    }
    if (persistState) {
        saveProjectRenderState();
        savePortableState();
    }
}

void MainWindow::setPreviewCanvasAspectRatio(double ratio, bool persistState)
{
    const double normalized = normalizedPreviewCanvasAspectRatio(ratio);
    if (qAbs(previewCanvasAspectRatio_ - normalized) <= 1e-6) {
        return;
    }
    const double previousRatio = previewCanvasAspectRatio_;
    previewCanvasAspectRatio_ = normalized;
    if (normalized + 1e-6 < previousRatio) {
        updatePreviewWorkspaceLayout();
    } else {
        updatePreviewPanelLayout();
    }
    if (persistState) {
        saveProjectRenderState();
        savePortableState();
    }
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
    const double aspectRatio = normalizedPreviewCanvasAspectRatio(previewCanvasAspectRatio_);
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/layout-calc/workspace-base",
            QString("splitter_rect=%1x%2 handle=%3 available=%4x%5 left_min=%6 right_min=%7 right_max=%8 preferred_ratio=%.2f preferred_right=%9 control_h=%10 aspect=%11")
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
                .arg(aspectRatio, 0, 'f', 3)
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
        const int heightLimitedWidth = qMax(
            0,
            qRound(static_cast<double>(availablePreviewHeight) * aspectRatio) + kPreviewPanelMarginX * 2
        );
        const int nextRightWidth = qMin(targetRightWidth, qMax(minimumRightWidth, heightLimitedWidth));
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "preview/layout-calc/workspace-iter",
                QString("iter=%1 target_right=%2 panel_content_w=%3 stats_host_w=%4 stats_min_h=%5 available_preview_h=%6 height_limited_w=%7 next_right=%8 aspect=%9")
                    .arg(i)
                    .arg(targetRightWidth)
                    .arg(panelContentWidth)
                    .arg(statsHostWidth)
                    .arg(minimumStatsHeight)
                    .arg(availablePreviewHeight)
                    .arg(heightLimitedWidth)
                    .arg(nextRightWidth)
                    .arg(aspectRatio, 0, 'f', 3)
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
    workspaceCachedLeftWidth_ = targetLeftWidth;
    workspaceCachedRightWidth_ = targetRightWidth;

    updatePreviewPanelLayout();
    updateEditorFindBarGeometry();
    applyFindOverlayInset();
}

void MainWindow::cacheWorkspaceLayoutSizes()
{
    if (workspaceSplitter_ == nullptr) {
        return;
    }
    const QList<int> sizes = workspaceSplitter_->sizes();
    if (sizes.size() != 2) {
        return;
    }
    workspaceCachedLeftWidth_ = qMax(0, sizes.at(0));
    workspaceCachedRightWidth_ = qMax(0, sizes.at(1));
}

void MainWindow::restoreWorkspaceLayoutSizes()
{
    if (workspaceSplitter_ == nullptr || workspaceCachedLeftWidth_ <= 0 || workspaceCachedRightWidth_ <= 0) {
        return;
    }
    const QList<int> currentSizes = workspaceSplitter_->sizes();
    if (currentSizes.size() == 2
        && qAbs(currentSizes.at(0) - workspaceCachedLeftWidth_) <= 1
        && qAbs(currentSizes.at(1) - workspaceCachedRightWidth_) <= 1) {
        return;
    }
    workspaceSplitter_->setSizes({workspaceCachedLeftWidth_, workspaceCachedRightWidth_});
    updatePreviewPanelLayout();
    updateEditorFindBarGeometry();
    applyFindOverlayInset();
}

void MainWindow::setWorkspacePanelsSwapped(bool swapped, bool persistState)
{
    if (workspacePanelsSwapped_ == swapped) {
        if (swapWorkspaceSidesAction_ != nullptr) {
            swapWorkspaceSidesAction_->blockSignals(true);
            swapWorkspaceSidesAction_->setChecked(workspacePanelsSwapped_);
            swapWorkspaceSidesAction_->blockSignals(false);
        }
        return;
    }

    cacheWorkspaceLayoutSizes();
    workspacePanelsSwapped_ = swapped;
    applyWorkspacePanelArrangement();
    if (persistState) {
        savePortableState();
    }
}

void MainWindow::applyWorkspacePanelArrangement()
{
    if (previewPanel_ != nullptr) {
        previewPanel_->setStyleSheet(workspaceSwapPreviewPanelStyleSheet(workspacePanelsSwapped_));
        previewPanel_->setLayoutDirection(Qt::LeftToRight);
    }
    if (previewLeftColumn_ != nullptr) {
        previewLeftColumn_->setLayoutDirection(Qt::LeftToRight);
    }
    if (previewCanvasContainer_ != nullptr) {
        previewCanvasContainer_->setLayoutDirection(Qt::LeftToRight);
    }
    if (previewControlCard_ != nullptr) {
        previewControlCard_->setLayoutDirection(Qt::LeftToRight);
    }
    if (previewStatsCard_ != nullptr) {
        previewStatsCard_->setLayoutDirection(Qt::LeftToRight);
    }
    if (bottomTabs_ != nullptr) {
        bottomTabs_->setLayoutDirection(Qt::LeftToRight);
    }
    if (workspaceSplitter_ != nullptr) {
        workspaceSplitter_->setLayoutDirection(
            workspacePanelsSwapped_ ? Qt::RightToLeft : Qt::LeftToRight
        );
    }
    if (outlineDock_ != nullptr) {
        addDockWidget(
            workspacePanelsSwapped_ ? Qt::RightDockWidgetArea : Qt::LeftDockWidgetArea,
            outlineDock_
        );
    }
    if (swapWorkspaceSidesAction_ != nullptr) {
        swapWorkspaceSidesAction_->blockSignals(true);
        swapWorkspaceSidesAction_->setChecked(workspacePanelsSwapped_);
        swapWorkspaceSidesAction_->setIcon(
            makeMenuSelectionCheckIcon(UiTheme::colors().accent, workspacePanelsSwapped_)
        );
        swapWorkspaceSidesAction_->blockSignals(false);
    }
    refreshLayoutAfterPageSwitch();
}

void MainWindow::refreshLayoutAfterPageSwitch()
{
    if (previewLeftColumn_ != nullptr) {
        previewLeftColumn_->updateGeometry();
        if (QLayout* layout = previewLeftColumn_->layout(); layout != nullptr) {
            layout->activate();
        }
    }
    if (editorStack_ != nullptr) {
        editorStack_->updateGeometry();
    }
    if (bottomTabs_ != nullptr) {
        bottomTabs_->updateGeometry();
    }
    if (workspaceSplitter_ != nullptr) {
        workspaceSplitter_->updateGeometry();
        if (QLayout* layout = workspaceSplitter_->layout(); layout != nullptr) {
            layout->activate();
        }
    }
    restoreWorkspaceLayoutSizes();
    updatePreviewWorkspaceLayout();
    updateEditorHeaderLayoutMode();
    if (timelineView_ != nullptr) {
        timelineView_->updateGeometry();
        timelineView_->viewport()->update();
    }
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
    const double aspectRatio = normalizedPreviewCanvasAspectRatio(previewCanvasAspectRatio_);
    const int previewWidth = qMax(1, qMin(contentWidth, qRound(static_cast<double>(availablePreviewHeight) * aspectRatio)));
    const int previewHeight = qMax(1, qRound(static_cast<double>(previewWidth) / aspectRatio));
    const int previewX = contentX + qMax(0, (contentWidth - previewWidth) / 2);
    const int controlY = contentY + previewHeight + kPreviewCanvasControlGap;
    const int statsAreaY = controlY + controlHeight + kPreviewControlStatsGap;
    const int statsAreaHeight = qMax(0, panelRect.height() - (statsAreaY - panelRect.y()) - kPreviewStatsBottomGap);
    const int statsHeight = qMin(minimumStatsHeight, statsAreaHeight);
    const int statsY = statsAreaY + qMax(0, (statsAreaHeight - statsHeight) / 2);
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/layout-calc/panel",
            QString("panel=%1x%2 content=(x=%3,y=%4,w=%5) control_h=%6 stats_host_w=%7 stats_min_h=%8 available_preview_h=%9 preview=%10x%11 aspect=%12 control_y=%13 stats_area_y=%14 stats_area_h=%15 stats_y=%16 stats_h=%17")
                .arg(panelRect.width())
                .arg(panelRect.height())
                .arg(contentX)
                .arg(contentY)
                .arg(contentWidth)
                .arg(controlHeight)
                .arg(statsHostWidth)
                .arg(minimumStatsHeight)
                .arg(availablePreviewHeight)
                .arg(previewWidth)
                .arg(previewHeight)
                .arg(aspectRatio, 0, 'f', 3)
                .arg(controlY)
                .arg(statsAreaY)
                .arg(statsAreaHeight)
                .arg(statsY)
                .arg(statsHeight)
        );
    }

    previewCanvasFrame_->setGeometry(previewX, contentY, previewWidth, previewHeight);
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
    int totalCount = 0;
    int totalPlayed = 0;
    QHash<QString, bool> helperHeadSeen;
    const auto slideJudgeSecondFor = [](const TimelineNoteMarker& marker) {
        if (marker.endSecond > marker.second) {
            return marker.endSecond;
        }
        if (marker.slideTraceSecond > marker.second) {
            return marker.slideTraceSecond;
        }
        return marker.second;
    };
    const auto slideHeadEventKey = [](const TimelineNoteMarker& marker) {
        return QStringLiteral("slide_head_star|%1|%2|%3|%4|%5")
            .arg(marker.second, 0, 'f', 6)
            .arg(marker.lane)
            .arg(marker.sourceLine)
            .arg(marker.sourceCol)
            .arg(marker.eachGroupId);
    };
    const auto addCount = [&totalCount, &totalPlayed](bool played, int* total, int* playedCount) {
        ++(*total);
        ++totalCount;
        if (played) {
            ++(*playedCount);
            ++totalPlayed;
        }
    };

    for (const TimelineNoteMarker& marker : previewStatsNoteMarkers_) {
        const QString type = marker.type.toLower();
        const bool isTap = (type == "tap");
        const bool isHold = (type == "hold" || type == "touch_hold");
        const bool isSlide = (type == "slide" || type == "wifi");
        const bool isTouch = (type == "touch");
        if (isTap) {
            const bool played = marker.second <= (second + 1e-6);
            if (marker.isBreak) {
                addCount(played, &breakTotal, &breakPlayed);
            } else {
                addCount(played, &tapTotal, &tapPlayed);
            }
            continue;
        }

        if (isTouch) {
            const bool played = marker.second <= (second + 1e-6);
            if (marker.isBreak) {
                addCount(played, &breakTotal, &breakPlayed);
            } else {
                addCount(played, &touchTotal, &touchPlayed);
            }
            continue;
        }

        if (isHold) {
            const bool played = marker.second <= (second + 1e-6);
            if (marker.isBreak) {
                addCount(played, &breakTotal, &breakPlayed);
            } else {
                addCount(played, &holdTotal, &holdPlayed);
            }
            continue;
        }

        if (!isSlide) {
            continue;
        }

        if (marker.hasHeadStar) {
            const QString helperKey = slideHeadEventKey(marker);
            if (!helperHeadSeen.contains(helperKey)) {
                helperHeadSeen.insert(helperKey, true);
                const bool helperPlayed = marker.second <= (second + 1e-6);
                if (marker.headBreak) {
                    addCount(helperPlayed, &breakTotal, &breakPlayed);
                } else {
                    addCount(helperPlayed, &tapTotal, &tapPlayed);
                }
            }
        }

        const double slideJudgeSecond = slideJudgeSecondFor(marker);
        const bool slideEventPlayed = slideJudgeSecond <= (second + 1e-6);
        if (marker.trackBreak) {
            addCount(slideEventPlayed, &breakTotal, &breakPlayed);
        } else {
            addCount(slideEventPlayed, &slideTotal, &slidePlayed);
        }
    }

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
    ensurePreviewSfxRuntimePrepared();
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
    qtPreviewPlaybackReturnSecond_ = clampedSecond;
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    syncPausedPreviewMediaTimestamps(clampedSecond);
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
        if (QMenu* speedMenu = previewSpeedButton_->menu(); speedMenu != nullptr) {
            const int targetIndex = nearestPreviewPlaybackRateIndex(previewPlaybackRate_);
            const QList<QAction*> actions = speedMenu->actions();
            for (int index = 0; index < actions.size(); ++index) {
                QAction* action = actions[index];
                const QVariant data = action != nullptr ? action->data() : QVariant();
                const bool checked = data.isValid()
                    ? qFuzzyCompare(data.toDouble() + 1.0, previewPlaybackRate_ + 1.0)
                    : (index == targetIndex);
                if (action != nullptr) {
                    action->setChecked(checked);
                }
            }
        }
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
        if (resumeFromPause && previewSfxRuntime_->hasBackgroundTrack()) {
            previewSfxRuntime_->seekBackgroundTrack(startSecond);
        }
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
    resetQtPreviewFixedFramePacing();
    if (previewCanvas_ != nullptr
        && (!previewCanvasUsesFrameSwappedPacing() || legacyPygamePreviewEnabled_)) {
        previewCanvas_->update();
    }
    if (previewCanvas_ != nullptr && !legacyPygamePreviewEnabled_ && previewCanvasUsesFrameSwappedPacing()) {
        requestNextDisplayRefreshPreviewFrame();
    } else {
        scheduleNextQtPreviewTick();
    }
    if (qtPreviewTimelineTimer_ != nullptr && !qtPreviewTimelineTimer_->isActive()) {
        qtPreviewTimelineTimer_->start();
    }
    updatePreviewSliderPosition(startSecond);
    updatePauseButtonAppearance();
}

void MainWindow::finishQtPreviewPlaybackAndReturnToEntry(const QString& statusMessage)
{
    const double returnSecond = timelineView_ != nullptr
        ? qBound(0.0, timelineView_->cursorSeconds(), previewDurationSeconds())
        : qBound(0.0, qtPreviewPlaybackReturnSecond_, previewDurationSeconds());
    stopQtPreviewPlayback(true);
    seekPreviewToSecond(returnSecond, true);
    if (statusBar() != nullptr && !statusMessage.isEmpty()) {
        statusBar()->showMessage(statusMessage);
    }
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
    qtPreviewNextFixedTickDueNs_ = -1;
    qtPreviewPendingAudioCalibration_ = false;
    flushQtPreviewTimelinePosition();
    syncEditorCursorToPreviewSecond(qtPreviewPauseSecond_, false);
    if (timelineView_ == nullptr || !timelineView_->followPreviewEnabled()) {
        clearPreviewFollowDecoration();
    }
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
        previewCanvas_->setPlayheadSeconds(second, !qtPreviewPlaying_);
    }
    updatePreviewSliderPosition(second);
    updatePreviewObjectStats(second);
    syncEditorCursorToPreviewSecond(second, centerView);
}

void MainWindow::syncPausedPreviewMediaTimestamps(double second)
{
    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    if (previewMediaController_ != nullptr) {
        previewMediaController_->setPlayheadSeconds(clampedSecond);
    }
}

void MainWindow::flushQtPreviewTimelinePosition()
{
    if (timelineView_ == nullptr) {
        return;
    }
    if (qtPreviewPlaying_) {
        double second = qMax(
            0.0,
            qtPreviewTimelineStartSecond_ + ((qtPreviewTimelineElapsed_.elapsed() / 1000.0) * previewPlaybackRate_)
        );
        if (previewSfxRuntime_ != nullptr
            && previewSfxRuntime_->hasBackgroundTrack()
            && previewSfxRuntime_->isBackgroundTrackRunning()) {
            second = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
        }
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
        requestNextDisplayRefreshPreviewFrame();
        return;
    }
    double second = 0.0;
    if (previewSfxRuntime_ != nullptr
        && previewSfxRuntime_->hasBackgroundTrack()
        && previewSfxRuntime_->isBackgroundTrackRunning()) {
        second = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
    } else {
        const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        second = qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
    }
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
        finishQtPreviewPlaybackAndReturnToEntry("Qt preview reached the end of current timeline.");
        return;
    }

    if (previewCanvas_ != nullptr) {
        previewCanvas_->noteTickForProfiling();
    }
    applyQtPreviewPosition(second, true);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->drainEvents(second);
    }
    requestNextDisplayRefreshPreviewFrame();
}

void MainWindow::jumpToNearestTimelineNote(double second, int lane)
{
    int line = 1;
    int col = 1;
    if (!resolveNearestTimelineNote(second, lane, &line, &col, nullptr)) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }
    if (!moveEditorCursorToTimelineLocation(line, col, true, true, true, false)) {
        statusBar()->showMessage("Timeline metadata unavailable.");
        return;
    }
    statusBar()->showMessage(
        QString("Timeline jump: %1s -> L%2 C%3")
            .arg(qMax(0.0, second), 0, 'f', 3)
            .arg(line)
            .arg(col)
    );
}
