namespace {

constexpr double kTimelineZeroSecondTolerance = 1e-6;
constexpr int kTimelineAnalysisIdleDelayMs = 180;

QString workspaceSwapPreviewPanelStyleSheet(bool swapped)
{
    QString style = UiTheme::previewPanelStyleSheet();
    if (swapped) {
        style.replace(QStringLiteral("border-left: 1px solid"), QStringLiteral("border-right: 1px solid"));
    }
    return style;
}

void updatePreviewControlsLayout(
    QHBoxLayout* previewControlsLayout,
    QToolButton* stopPreviewButton,
    QToolButton* pausePreviewButton,
    QSlider* previewSlider,
    QToolButton* previewSpeedButton,
    QToolButton* previewFullscreenButton,
    bool swapped
)
{
    if (previewControlsLayout == nullptr
        || stopPreviewButton == nullptr
        || pausePreviewButton == nullptr
        || previewSlider == nullptr
        || previewSpeedButton == nullptr
        || previewFullscreenButton == nullptr) {
        return;
    }

    previewControlsLayout->removeWidget(stopPreviewButton);
    previewControlsLayout->removeWidget(pausePreviewButton);
    previewControlsLayout->removeWidget(previewSlider);
    previewControlsLayout->removeWidget(previewSpeedButton);
    previewControlsLayout->removeWidget(previewFullscreenButton);

    if (swapped) {
        previewControlsLayout->addWidget(previewSpeedButton, 0);
        previewControlsLayout->addWidget(previewFullscreenButton, 0);
        previewControlsLayout->addWidget(previewSlider, 1);
        previewControlsLayout->addWidget(stopPreviewButton, 0);
        previewControlsLayout->addWidget(pausePreviewButton, 0);
    } else {
        previewControlsLayout->addWidget(stopPreviewButton, 0);
        previewControlsLayout->addWidget(pausePreviewButton, 0);
        previewControlsLayout->addWidget(previewSlider, 1);
        previewControlsLayout->addWidget(previewSpeedButton, 0);
        previewControlsLayout->addWidget(previewFullscreenButton, 0);
    }
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

void MainWindow::resetPreviewTrackTimelineOffsets()
{
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackOffsetSeconds(0.0);
    }
    resetPreviewStageMediaRouteTimelineOffset();
}

void MainWindow::applyWaveformData(const QVector<float>& peaks, double durationSeconds)
{
    previewTrackDurationSeconds_ = qMax(0.0, durationSeconds);
    if (timelineView_ != nullptr) {
        timelineView_->setWaveformData(peaks, 0.0, previewTrackDurationSeconds_);
    }
    updatePreviewSliderRange();
}

void MainWindow::refreshWaveformCache()
{
    refreshWaveformCache(-1.0);
}

void MainWindow::refreshWaveformCache(double knownDurationSeconds)
{
    resetPreviewTrackTimelineOffsets();
    if (timelineView_ == nullptr) {
        return;
    }

    ++waveformRefreshGeneration_;
    const quint64 generation = waveformRefreshGeneration_;
    const QString trackPath = lastTrackPath_;
    if (trackPath.isEmpty()) {
        applyWaveformData(QVector<float>(), 0.0);
        return;
    }

    const QFileInfo trackInfo(trackPath);
    if (!trackInfo.exists() || !trackInfo.isFile()) {
        applyWaveformData(QVector<float>(), 0.0);
        return;
    }

    const qint64 fileSize = trackInfo.size();
    const qint64 lastModifiedMs = fileLastModifiedMs(trackInfo);
    const bool cacheMatches =
        waveformCacheEntry_.trackPath == trackPath
        && waveformCacheEntry_.fileSize == fileSize
        && waveformCacheEntry_.lastModifiedMs == lastModifiedMs;
    const bool cacheHasPeaks = cacheMatches && !waveformCacheEntry_.peaks.isEmpty();
    if (cacheHasPeaks) {
        applyWaveformData(
            waveformCacheEntry_.peaks,
            knownDurationSeconds > 0.0 ? knownDurationSeconds : waveformCacheEntry_.durationSeconds
        );
        return;
    }

    if (knownDurationSeconds > 0.0) {
        applyWaveformData(QVector<float>(), knownDurationSeconds);
    } else if (cacheMatches && waveformCacheEntry_.durationSeconds > 0.0) {
        applyWaveformData(QVector<float>(), waveformCacheEntry_.durationSeconds);
    } else {
        applyWaveformData(QVector<float>(), 0.0);
    }

    QPointer<MainWindow> guard(this);
    QThreadPool* const pool = previewWarmupPool_ != nullptr
        ? previewWarmupPool_
        : QThreadPool::globalInstance();
    pool->start([guard, generation, trackPath, fileSize, lastModifiedMs]() {
        double audioDurationSeconds = 0.0;
        QElapsedTimer timer;
        timer.start();
        const QVector<float> peaks = buildWaveformPeaks(trackPath, &audioDurationSeconds, kWaveformPeakCount);
        const qint64 buildElapsedMs = timer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, trackPath, fileSize, lastModifiedMs, audioDurationSeconds, peaks, buildElapsedMs]() {
                if (guard.isNull()) {
                    return;
                }
                guard->applyWaveformCacheEntry(
                    generation,
                    trackPath,
                    fileSize,
                    lastModifiedMs,
                    audioDurationSeconds,
                    peaks,
                    buildElapsedMs
                );
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::applyWaveformCacheEntry(
    quint64 generation,
    const QString& trackPath,
    qint64 fileSize,
    qint64 lastModifiedMs,
    double durationSeconds,
    const QVector<float>& peaks,
    qint64 buildElapsedMs)
{
    Q_UNUSED(buildElapsedMs);

    if (generation != waveformRefreshGeneration_ || lastTrackPath_ != trackPath) {
        return;
    }

    const QFileInfo currentTrackInfo(trackPath);
    if (!currentTrackInfo.exists()
        || currentTrackInfo.size() != fileSize
        || fileLastModifiedMs(currentTrackInfo) != lastModifiedMs) {
        return;
    }

    waveformCacheEntry_.trackPath = trackPath;
    waveformCacheEntry_.fileSize = fileSize;
    waveformCacheEntry_.lastModifiedMs = lastModifiedMs;
    waveformCacheEntry_.durationSeconds = qMax(0.0, durationSeconds);
    waveformCacheEntry_.peaks = peaks;
    applyWaveformData(peaks, waveformCacheEntry_.durationSeconds);
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

miacode::simai::SimaiTimingMetadata MainWindow::currentTimingMetadata() const
{
    if (metadataExtraEdit_ != nullptr) {
        return miacode::simai::buildTimingMetadataFromRawText(metadataExtraEdit_->toPlainText(), true);
    }
    return miacode::simai::buildTimingMetadata(document_);
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
    return miacode::simai::latencyMeterIdForTimingMetadata(currentTimingMetadata());
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
    resetPreviewTrackTimelineOffsets();
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

void MainWindow::setCurrentFilePath(const QString& path, bool suppressImmediateRefresh)
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

        const QString siblingTrack = miacode::chart_assets::resolveTrackPath(currentFilePath_);
        if (!siblingTrack.isEmpty()) {
            // Keep preview audio in sync with the currently opened chart directory.
            lastTrackPath_ = siblingTrack;
        } else {
            lastTrackPath_.clear();
        }
    } else {
        lastTrackPath_.clear();
    }
    if (previewCanvas_ != nullptr) {
#ifdef HAVE_QT_MULTIMEDIA
        previewCanvas_->setStageMediaAvailable(miacode::chart_assets::hasBackgroundMedia(currentFilePath_));
#else
        previewCanvas_->setStageMediaAvailable(miacode::chart_assets::hasBackgroundMedia(currentFilePath_, false));
#endif
    }
    updateWindowTitle();
    updateCurrentFileLabel();
    updateLatencyDetectorAvailability();
    if (pathChanged) {
        loadProjectRenderState();
    }
    syncPreviewStageMediaRouteChartPath(currentFilePath_, lastTrackPath_, qtPreviewPauseSecond_);
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(qMax(0.0, qtPreviewPauseSecond_), false);
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setChartPath(currentFilePath_);
    }
    applyPreviewAudioSettingsToRuntime();
    if (!suppressImmediateRefresh) {
        refreshWaveformCache();
        refreshTimelineMetadata();
    }
    if (pathChanged && previewWarmupGeneration_ > 0) {
        schedulePreviewSubsystemWarmup();
    }
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

void MainWindow::scheduleTimelineRefresh()
{
    if (!hasActiveDifficulty() || timelineView_ == nullptr) {
        return;
    }
    ++timelineRevision_;
    refreshTimelineQuickModelFromCurrentText();
    requestTimelineSlowRefresh();
}

void MainWindow::refreshTimelineMetadata()
{
    scheduleTimelineRefresh();
}

void MainWindow::applyTimelineQuickChange(int position, int charsRemoved, int charsAdded)
{
    if (timelineView_ == nullptr || !hasActiveDifficulty()) {
        return;
    }

    const double firstSeconds = parsedFirstSeconds();
    const miacode::simai::SimaiTimingMetadata timingMetadata = currentTimingMetadata();
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextDocument* document = editor != nullptr ? editor->document() : nullptr;
    if (document != nullptr) {
        timelineQuickModel_.applyContentsChange(
            document,
            position,
            charsRemoved,
            charsAdded,
            firstSeconds,
            timingMetadata);
    } else {
        timelineQuickModel_.rebuildFromText(activeChartText(), firstSeconds, timingMetadata);
    }
    timelineView_->setTimelineData(timelineQuickModel_.snapshot());
    updatePreviewSliderRange();
}

void MainWindow::refreshTimelineQuickModelFromCurrentText()
{
    if (timelineView_ == nullptr || !hasActiveDifficulty()) {
        return;
    }
    timelineQuickModel_.rebuildFromText(activeChartText(), parsedFirstSeconds(), currentTimingMetadata());
    timelineView_->setTimelineData(timelineQuickModel_.snapshot());
    updatePreviewSliderRange();
}

void MainWindow::applyLatestTimelinePreviewStateToPausedPreview()
{
    if (qtPreviewPlaying_) {
        return;
    }

    const bool noteMarkersChanged = latestTimelineNoteMarkerSignature_ != lastPreviewNoteMarkerSignature_;
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->applyPausedPreviewState(
            latestTimelineNoteMarkers_,
            noteMarkersChanged,
            qtPreviewPauseSecond_);
    }

    refreshPreviewObjectStatsTotals(latestTimelineNoteMarkers_);
    if (previewCanvas_ != nullptr && noteMarkersChanged) {
        previewCanvas_->setNoteMarkers(latestTimelineNoteMarkers_);
    }
    applyAlignedMuriAnalysisReportToViews();
    lastPreviewNoteMarkerSignature_ = latestTimelineNoteMarkerSignature_;
}

void MainWindow::requestTimelineSlowRefresh()
{
    if (!hasActiveDifficulty()) {
        return;
    }

    pendingTimelineSlowRefresh_.revision = timelineRevision_;
    pendingTimelineSlowRefresh_.difficultyId = activeDifficultyId();
    pendingTimelineSlowRefresh_.chartText = activeChartText();
    pendingTimelineSlowRefresh_.firstSeconds = parsedFirstSeconds();
    pendingTimelineSlowRefresh_.timingMetadata = currentTimingMetadata();
    pendingTimelineSlowRefresh_.chineseUi = UiText::isChineseUi();
    timelineSlowRequestedRevision_ = pendingTimelineSlowRefresh_.revision;
    if (pendingPreviewPlaybackStart_) {
        pendingPreviewPlaybackRevision_ = timelineRevision_;
        pendingPreviewPlaybackDifficultyId_ = activeDifficultyId();
    }
    dispatchTimelineSlowRefresh();
}

void MainWindow::dispatchTimelineSlowRefresh()
{
    if (timelineSlowWorkerRunning_ || pendingTimelineSlowRefresh_.revision == 0) {
        return;
    }

    const TimelineSlowRefreshRequest request = pendingTimelineSlowRefresh_;
    pendingTimelineSlowRefresh_ = TimelineSlowRefreshRequest();
    timelineSlowWorkerRunning_ = true;
    timelineSlowRunningRevision_ = request.revision;
    QPointer<MainWindow> guard(this);
    QThreadPool* const pool = timelineSlowRefreshPool_ != nullptr
        ? timelineSlowRefreshPool_
        : QThreadPool::globalInstance();
    pool->start([guard, request]() {
        const SimaiNativeParseResult parseResult = SimaiNativeParser::parseForTimeline(
            request.chartText,
            request.timingMetadata);
        const TimelinePreviewRefreshState previewState =
            buildTimelinePreviewRefreshState(parseResult, request.firstSeconds);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, request, parseResult, previewState]() mutable {
                if (guard.isNull()) {
                    return;
                }

                guard->timelineSlowWorkerRunning_ = false;
                if (request.revision != guard->timelineSlowRequestedRevision_
                    || !guard->hasActiveDifficulty()
                    || request.difficultyId != guard->activeDifficultyId()
                    || request.chartText != guard->activeChartText()) {
                    guard->dispatchTimelineSlowRefresh();
                    return;
                }

                guard->lastTimelineParseDifficultyId_ = request.difficultyId;
                guard->lastTimelineParseChartText_ = request.chartText;
                guard->lastTimelineParseTimingMetadata_ = request.timingMetadata;
                guard->lastTimelineParseResult_ = parseResult;
                guard->latestTimelineNoteMarkers_ = previewState.shiftedNoteMarkers;
                guard->latestTimelineNoteMarkerSignature_ = previewState.noteMarkerSignature;
                guard->latestTimelinePreviewRevision_ = request.revision;
                guard->latestTimelinePreviewSnapshotReady_ = true;
                if (!guard->qtPreviewPlaying_) {
                    guard->applyLatestTimelinePreviewStateToPausedPreview();
                }
                guard->scheduleTimelineAnalysisRefresh(request, parseResult, previewState);
                if (guard->pendingPreviewPlaybackStart_
                    && !guard->qtPreviewPlaying_
                    && guard->pendingPreviewPlaybackRevision_ == request.revision
                    && guard->pendingPreviewPlaybackDifficultyId_ == request.difficultyId) {
                    const double pendingSecond = guard->pendingPreviewPlaybackSecond_;
                    const bool resumeFromPause = guard->pendingPreviewPlaybackResumeFromPause_;
                    guard->pendingPreviewPlaybackStart_ = false;
                    guard->startQtPreviewPlayback(pendingSecond, resumeFromPause);
                }
                guard->dispatchTimelineSlowRefresh();
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::scheduleTimelineAnalysisRefresh(
    const TimelineSlowRefreshRequest& request,
    const SimaiNativeParseResult& parseResult,
    const TimelinePreviewRefreshState& previewState)
{
    pendingTimelineAnalysisRefresh_.revision = request.revision;
    pendingTimelineAnalysisRefresh_.difficultyId = request.difficultyId;
    pendingTimelineAnalysisRefresh_.chartText = request.chartText;
    pendingTimelineAnalysisRefresh_.chineseUi = request.chineseUi;
    pendingTimelineAnalysisRefresh_.timingMetadata = request.timingMetadata;
    pendingTimelineAnalysisRefresh_.parseResult = parseResult;
    pendingTimelineAnalysisRefresh_.noteMarkerSignature = previewState.noteMarkerSignature;
    pendingTimelineAnalysisRefresh_.noteMarkers = previewState.shiftedNoteMarkers;
    pendingTimelineAnalysisRefresh_.renderOptions = muriRenderOptions_;
    pendingTimelineAnalysisRefresh_.staticTapOnSlideThresholdSeconds =
        static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0;
    timelineAnalysisRequestedRevision_ = request.revision;
    requestTimelineAnalysisDispatch();
}

bool MainWindow::scheduleTimelineAnalysisRefreshFromLatestPreviewState(int delayMs)
{
    if (!hasActiveDifficulty()
        || !latestTimelinePreviewSnapshotReady_
        || lastTimelineParseDifficultyId_ != activeDifficultyId()
        || lastTimelineParseChartText_ != activeChartText()
        || lastTimelineParseTimingMetadata_ != currentTimingMetadata()) {
        return false;
    }

    TimelineSlowRefreshRequest request;
    request.revision = latestTimelinePreviewRevision_;
    request.difficultyId = activeDifficultyId();
    request.chartText = lastTimelineParseChartText_;
    request.timingMetadata = lastTimelineParseTimingMetadata_;
    request.chineseUi = UiText::isChineseUi();

    pendingTimelineAnalysisRefresh_.revision = request.revision;
    pendingTimelineAnalysisRefresh_.difficultyId = request.difficultyId;
    pendingTimelineAnalysisRefresh_.chartText = request.chartText;
    pendingTimelineAnalysisRefresh_.chineseUi = request.chineseUi;
    pendingTimelineAnalysisRefresh_.timingMetadata = request.timingMetadata;
    pendingTimelineAnalysisRefresh_.parseResult = lastTimelineParseResult_;
    pendingTimelineAnalysisRefresh_.noteMarkerSignature = latestTimelineNoteMarkerSignature_;
    pendingTimelineAnalysisRefresh_.noteMarkers = latestTimelineNoteMarkers_;
    pendingTimelineAnalysisRefresh_.renderOptions = muriRenderOptions_;
    pendingTimelineAnalysisRefresh_.staticTapOnSlideThresholdSeconds =
        static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0;
    timelineAnalysisRequestedRevision_ = request.revision;
    requestTimelineAnalysisDispatch(delayMs);
    return true;
}

void MainWindow::requestTimelineAnalysisDispatch(int delayMs)
{
    if (pendingTimelineAnalysisRefresh_.revision == 0) {
        return;
    }
    if (qtPreviewPlaying_) {
        if (timelineAnalysisIdleTimer_ != nullptr) {
            timelineAnalysisIdleTimer_->stop();
        }
        return;
    }
    if (timelineAnalysisIdleTimer_ != nullptr) {
        const int effectiveDelayMs = delayMs >= 0 ? delayMs : kTimelineAnalysisIdleDelayMs;
        timelineAnalysisIdleTimer_->start(effectiveDelayMs);
        return;
    }
    dispatchTimelineAnalysisRefresh();
}

void MainWindow::dispatchTimelineAnalysisRefresh()
{
    if (!hasActiveDifficulty() || qtPreviewPlaying_ || timelineAnalysisWorkerRunning_ || pendingTimelineAnalysisRefresh_.revision == 0) {
        return;
    }

    const TimelineAnalysisRefreshRequest request = pendingTimelineAnalysisRefresh_;
    pendingTimelineAnalysisRefresh_ = TimelineAnalysisRefreshRequest();
    timelineAnalysisWorkerRunning_ = true;
    timelineAnalysisRunningRevision_ = request.revision;
    QPointer<MainWindow> guard(this);
    QThreadPool* const pool = timelineAnalysisPool_ != nullptr
        ? timelineAnalysisPool_
        : QThreadPool::globalInstance();
    pool->start([guard, request]() {
        TimelineAnalysisRefreshResult result = buildTimelineAnalysisRefreshResult(request);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                if (guard.isNull()) {
                    return;
                }

                guard->timelineAnalysisWorkerRunning_ = false;
                if (result.revision != guard->timelineAnalysisRequestedRevision_
                    || !guard->hasActiveDifficulty()
                    || result.difficultyId != guard->activeDifficultyId()
                    || result.chartText != guard->activeChartText()) {
                    guard->requestTimelineAnalysisDispatch();
                    return;
                }

                ValidationCacheEntry entry;
                entry.chartText = result.chartText;
                entry.chineseUi = result.validationReport.issues.isEmpty() ? UiText::isChineseUi() : result.chineseUi;
                entry.timingMetadata = result.timingMetadata;
                entry.ok = result.validationReport.ok;
                entry.errorCount = result.validationReport.errorCount;
                entry.warningCount = result.validationReport.warningCount;
                entry.lenientNoteCount = result.validationReport.lenientNoteCount;
                entry.lenientErrorCount = result.validationReport.lenientErrorCount;
                entry.strictNoteCount = result.validationReport.strictNoteCount;
                entry.strictErrorCount = result.validationReport.strictErrorCount;
                entry.issues.reserve(result.validationReport.issues.size());
                for (const SimaiNativeValidationIssue& issue : result.validationReport.issues) {
                    ValidationCachedIssue cachedIssue;
                    cachedIssue.line = issue.line;
                    cachedIssue.col = issue.col;
                    cachedIssue.endCol = issue.endCol;
                    cachedIssue.rawMessage = issue.rawMessage;
                    cachedIssue.displayMessage = issue.displayMessage;
                    entry.issues.append(cachedIssue);
                }
                guard->validationCacheByDifficulty_.insert(result.difficultyId, entry);
                guard->pendingDeferredValidationUiRefresh_ = true;
                guard->muriAnalysisReport_ = result.analysisReport;
                guard->muriAnalysisReportNoteMarkerSignature_ = result.noteMarkerSignature;
                guard->muriStaticReferences_ = result.staticReferences;
                guard->pendingDeferredMuriUiRefresh_ = true;
                if (!guard->qtPreviewPlaying_) {
                    guard->applyDeferredAnalysisUiUpdates();
                }
                guard->requestTimelineAnalysisDispatch();
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::rebuildStaticMuriReferences(const QVector<TimelineNoteMarker>& noteMarkers)
{
    muriStaticReferences_ = miacode::muri::buildStaticMuriReferences(
        noteMarkers,
        static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0);
}

double MainWindow::timelineSecondForCursor(int line, int col) const
{
    return timelineQuickModel_.timelineSecondForCursor(line, col);
}

void MainWindow::seekTimelineToCursor(int line, int col)
{
    if (timelineView_ == nullptr) {
        return;
    }
    const double second = timelineSecondForCursor(line, col);
    timelineView_->setCursorSeconds(second, false);
    timelineView_->focusCursor(true);
}

void MainWindow::syncTimelineToEditorCursor(bool centerView)
{
    if (suppressTimelineCursorSync_ || !hasActiveDifficulty() || timelineView_ == nullptr) {
        return;
    }
    const auto [line, col] = currentCursorLineCol();
    const double second = timelineSecondForCursor(line, col);
    timelineView_->setCursorSeconds(second, false);
    if (!qtPreviewPlaying_) {
        timelineView_->focusCursor(centerView);
    }
}

void MainWindow::navigateTimelineToSecond(double second, bool focusEditor)
{
    if (timelineView_ == nullptr) {
        return;
    }

    const double clampedSecond = qBound(0.0, second, previewDurationSeconds());
    int line = 1;
    int col = 1;
    double cursorSecond = 0.0;
    timelineQuickModel_.resolveTimelineNavigateCursor(clampedSecond, &line, &col, &cursorSecond);
    const bool previousSuppressState = suppressTimelineCursorSync_;
    suppressTimelineCursorSync_ = true;

    previewPendingSeekSecond_ = clampedSecond;
    previewPendingSeekCenterView_ = true;
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->stop();
    }
    seekPreviewToSecond(clampedSecond, true);
    timelineView_->setCursorSeconds(cursorSecond, false);
    timelineView_->focusPlayhead(true);

    moveEditorCursorToTimelineLocation(line, col, false, focusEditor, true, true);

    suppressTimelineCursorSync_ = previousSuppressState;

    statusBar()->showMessage(
        QString("Timeline jump: %1s -> L%2 C%3")
            .arg(clampedSecond, 0, 'f', 3)
            .arg(line)
            .arg(col)
    );
}

bool MainWindow::resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const
{
    return timelineQuickModel_.resolveNearestTimelineNote(second, lane, line, col, noteSecond);
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

void MainWindow::syncEditorCursorToPreviewSecond(double second, bool centerView)
{
    if (suppressTimelineCursorSync_ || timelineView_ == nullptr || !hasActiveDifficulty()) {
        clearPreviewFollowDecoration();
        return;
    }
    if (!timelineView_->followPreviewEnabled()) {
        clearPreviewFollowDecoration();
        return;
    }
    if (!qtPreviewPlaying_) {
        return;
    }

    const auto [currentLine, currentCol] = currentCursorLineCol();
    int line = 1;
    int col = 1;
    double cursorSecond = 0.0;
    const bool resolved = timelineQuickModel_.resolvePreviewFollowCursor(second, &line, &col, &cursorSecond);
    const bool alreadyAtAnchor = (currentLine == line && currentCol == col);

    if (alreadyAtAnchor) {
        if (resolved) {
            setPreviewFollowDecoration(line, col);
        } else {
            clearPreviewFollowDecoration();
        }
        timelineView_->setCursorSeconds(cursorSecond, false);
        return;
    }

    if (moveEditorCursorToTimelineLocation(line, col, false, false, centerView, false)) {
        if (!resolved) {
            clearPreviewFollowDecoration();
        }
        timelineView_->setCursorSeconds(cursorSecond, false);
    }
}

double MainWindow::previewDurationSeconds() const
{
    double duration = 0.0;
    if (qtPreviewPlaying_ && qtPreviewPlaybackEndSecond_ > 0.0) {
        duration = qMax(duration, qtPreviewPlaybackEndSecond_);
    }
    if (timelineView_ != nullptr) {
        duration = qMax(duration, timelineView_->durationSeconds());
        duration = qMax(duration, timelineView_->playheadSeconds());
        duration = qMax(duration, timelineView_->playbackEntrySeconds());
    }
    duration = qMax(duration, qMax(0.0, qtPreviewPauseSecond_));
    if (previewTrackDurationSeconds_ > 0.0) {
        duration = qMax(duration, previewTrackDurationSeconds_ + 3.0);
    }
    return qMax(0.0, duration);
}

double MainWindow::previewPlaybackEndSeconds() const
{
    if (qtPreviewPlaying_ && qtPreviewPlaybackEndSecond_ > 0.0) {
        return qMax(0.0, qtPreviewPlaybackEndSecond_);
    }
    double duration = 0.0;
    if (timelineView_ != nullptr) {
        duration = qMax(duration, timelineView_->durationSeconds());
    }
    if (previewTrackDurationSeconds_ > 0.0) {
        duration = qMax(duration, previewTrackDurationSeconds_);
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
    auto cache = std::make_shared<miacode::preview::scene::PreviewProgressStatsCache>();
    cache->rebuild(noteMarkers);
    previewProgressStatsCache_ = cache;
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setProgressStatsCache(previewProgressStatsCache_);
    }
    updatePreviewObjectStats(qtPreviewPauseSecond_);
}

void MainWindow::clearPreviewObjectStats()
{
    previewProgressStatsCache_.reset();
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setProgressStatsCache(previewProgressStatsCache_);
    }
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
    const int chipHeight = qMax(
        miacode::window_parity::kPreviewStatsChipHeight,
        !previewStatsChips_.isEmpty() && previewStatsChips_.constFirst() != nullptr
            ? previewStatsChips_.constFirst()->sizeHint().height()
            : miacode::window_parity::kPreviewStatsChipHeight
    );
    const miacode::window_parity::PreviewStatsLayout layout = miacode::window_parity::computePreviewStatsLayout(
        resolvedHostWidth,
        itemCount,
        horizontalSpacing,
        verticalSpacing,
        chipHeight,
        gridMargins.top(),
        gridMargins.bottom()
    );
    const int cols = layout.columns;
    const int rows = layout.rows;
    const bool structureChanged = (rows != previewStatsLayoutRows_) || (cols != previewStatsLayoutCols_);
    previewStatsLayoutRows_ = rows;
    previewStatsLayoutCols_ = cols;

    const int cardHeight = layout.minCardHeight;
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
        || previewCanvas_ == nullptr
        || !previewCanvasUsesFrameSwappedPacing()
        || qtPreviewAwaitingFrameSwap_) {
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
        if (previewCanvas_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
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
void MainWindow::togglePreviewFullscreen()
{
    if (previewFullscreenActive_) {
        exitPreviewFullscreen();
        return;
    }
    enterPreviewFullscreen();
}

void MainWindow::enterPreviewFullscreen()
{
    if (isQuickShellBackendMode()) {
        if (previewFullscreenActive_) {
            return;
        }
        previewFullscreenActive_ = true;
        updatePauseButtonAppearance();
        updatePreviewFullscreenButtonAppearance();
        return;
    }
    if (previewFullscreenActive_ || previewCanvasContainer_ == nullptr || previewCanvasFrame_ == nullptr) {
        return;
    }
    if (previewFullscreenWindow_ == nullptr) {
        previewFullscreenWindow_ = new QWidget(this, Qt::Window);
        previewFullscreenWindow_->setWindowTitle(uiText(
            "preview.fullscreen.window_title",
            QStringLiteral("Fullscreen Preview")
        ));
        previewFullscreenWindow_->setStyleSheet(QStringLiteral("background-color: #000000;"));
        previewFullscreenWindow_->setAttribute(Qt::WA_DeleteOnClose, false);
        previewFullscreenWindow_->setFocusPolicy(Qt::StrongFocus);
        previewFullscreenWindow_->setMouseTracking(true);
        previewFullscreenWindow_->installEventFilter(this);
        auto* fullscreenLayout = new QVBoxLayout(previewFullscreenWindow_);
        fullscreenLayout->setContentsMargins(0, 0, 0, 0);
        fullscreenLayout->setSpacing(0);
        previewFullscreenHost_ = new QWidget(previewFullscreenWindow_);
        previewFullscreenHost_->setStyleSheet(QStringLiteral("background-color: #000000;"));
        previewFullscreenHost_->setFocusPolicy(Qt::StrongFocus);
        previewFullscreenHost_->setMouseTracking(true);
        previewFullscreenHost_->installEventFilter(this);
        auto* hostLayout = new QVBoxLayout(previewFullscreenHost_);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        hostLayout->setSpacing(0);
        fullscreenLayout->addWidget(previewFullscreenHost_);
    }
    if (previewFullscreenControlsWindow_ == nullptr && previewFullscreenWindow_ != nullptr) {
        previewFullscreenControlsWindow_ = new QWidget(
            previewFullscreenWindow_,
            Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint
        );
        previewFullscreenControlsWindow_->setAttribute(Qt::WA_ShowWithoutActivating);
        previewFullscreenControlsWindow_->setAttribute(Qt::WA_TranslucentBackground);
        previewFullscreenControlsWindow_->setFocusPolicy(Qt::StrongFocus);
        previewFullscreenControlsWindow_->setMouseTracking(true);
        previewFullscreenControlsWindow_->installEventFilter(this);
        auto* controlsLayout = new QVBoxLayout(previewFullscreenControlsWindow_);
        controlsLayout->setContentsMargins(0, 0, 0, 0);
        controlsLayout->setSpacing(0);
        previewFullscreenControlsWindow_->hide();
    }
    if (previewFullscreenHintWindow_ == nullptr && previewFullscreenWindow_ != nullptr) {
        previewFullscreenHintWindow_ = new QWidget(
            previewFullscreenWindow_,
            Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint
        );
        previewFullscreenHintWindow_->setAttribute(Qt::WA_ShowWithoutActivating);
        previewFullscreenHintWindow_->setAttribute(Qt::WA_TranslucentBackground);
        previewFullscreenHintWindow_->setAttribute(Qt::WA_TransparentForMouseEvents);
        previewFullscreenHintWindow_->installEventFilter(this);
        auto* hintLayout = new QVBoxLayout(previewFullscreenHintWindow_);
        hintLayout->setContentsMargins(0, 0, 0, 0);
        hintLayout->setSpacing(0);
        previewFullscreenHintWindow_->hide();
    }
    if (previewFullscreenHintLabel_ == nullptr && previewFullscreenHintWindow_ != nullptr) {
        previewFullscreenHintLabel_ = new QLabel(previewFullscreenHintWindow_);
        previewFullscreenHintLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        previewFullscreenHintLabel_->setAttribute(Qt::WA_StyledBackground, true);
        previewFullscreenHintLabel_->setAlignment(Qt::AlignCenter);
        previewFullscreenHintLabel_->setStyleSheet(previewFullscreenHintStyleSheet());
        if (previewFullscreenHintWindow_->layout() != nullptr) {
            previewFullscreenHintWindow_->layout()->addWidget(previewFullscreenHintLabel_);
        }
        previewFullscreenHintLabel_->hide();
    }
    if (previewFullscreenHintTimer_ == nullptr) {
        previewFullscreenHintTimer_ = new QTimer(this);
        previewFullscreenHintTimer_->setSingleShot(true);
        connect(previewFullscreenHintTimer_, &QTimer::timeout, this, [this]() {
            if (previewFullscreenHintWindow_ != nullptr) {
                previewFullscreenHintWindow_->hide();
            }
        });
    }
    if (previewFullscreenControlsTimer_ == nullptr) {
        previewFullscreenControlsTimer_ = new QTimer(this);
        previewFullscreenControlsTimer_->setSingleShot(true);
        connect(previewFullscreenControlsTimer_, &QTimer::timeout, this, [this]() {
            hidePreviewFullscreenControls(true);
        });
    }
    if (previewFullscreenCursorPollTimer_ == nullptr) {
        previewFullscreenCursorPollTimer_ = new QTimer(this);
        previewFullscreenCursorPollTimer_->setInterval(120);
        connect(previewFullscreenCursorPollTimer_, &QTimer::timeout, this, &MainWindow::pollPreviewFullscreenCursor);
    }
    if (previewFullscreenControlsWindow_ != nullptr
        && (previewFullscreenControlsAnimation_ == nullptr
            || previewFullscreenControlsAnimation_->targetObject() != previewFullscreenControlsWindow_)) {
        delete previewFullscreenControlsAnimation_;
        previewFullscreenControlsAnimation_ = new QPropertyAnimation(
            previewFullscreenControlsWindow_,
            "geometry",
            this
        );
        previewFullscreenControlsAnimation_->setDuration(kPreviewFullscreenControlsAnimationDurationMs);
        previewFullscreenControlsAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    }
    if (previewFullscreenControlsWindow_ != nullptr
        && (previewFullscreenControlsOpacityAnimation_ == nullptr
            || previewFullscreenControlsOpacityAnimation_->targetObject() != previewFullscreenControlsWindow_)) {
        delete previewFullscreenControlsOpacityAnimation_;
        previewFullscreenControlsOpacityAnimation_ = new QPropertyAnimation(
            previewFullscreenControlsWindow_,
            "windowOpacity",
            this
        );
        previewFullscreenControlsOpacityAnimation_->setDuration(kPreviewFullscreenControlsOpacityAnimationDurationMs);
        previewFullscreenControlsOpacityAnimation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(previewFullscreenControlsOpacityAnimation_, &QPropertyAnimation::finished, this, [this]() {
            if (!previewFullscreenControlsVisible_ && previewFullscreenControlsWindow_ != nullptr) {
                previewFullscreenControlsWindow_->hide();
                previewFullscreenControlsWindow_->setWindowOpacity(0.0);
            }
        });
    }
    if (previewFullscreenHost_ == nullptr || previewFullscreenHost_->layout() == nullptr) {
        return;
    }
    previewCanvasContainer_->setParent(previewFullscreenHost_);
    previewFullscreenHost_->layout()->addWidget(previewCanvasContainer_);
    if (previewControlCard_ != nullptr && previewFullscreenControlsWindow_ != nullptr) {
        previewControlCard_->setParent(previewFullscreenControlsWindow_);
        previewControlCard_->setAttribute(Qt::WA_StyledBackground, true);
        previewControlCard_->setStyleSheet(previewFullscreenControlCardStyleSheet());
        if (previewFullscreenControlsWindow_->layout() != nullptr) {
            previewFullscreenControlsWindow_->layout()->addWidget(previewControlCard_);
        }
        previewControlCard_->show();
    }
    previewCanvasContainer_->show();
    previewFullscreenActive_ = true;
    previewFullscreenControlsVisible_ = false;
    previewFullscreenCursorTrackingInitialized_ = false;
    if (stopPreviewAction_ != nullptr) {
        stopPreviewAction_->setIcon(makePreviewStopIcon(previewFullscreenOverlayIconColor()));
    }
    updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
    previewFullscreenWindow_->showFullScreen();
    previewFullscreenWindow_->raise();
    previewFullscreenWindow_->activateWindow();
    if (previewFullscreenHintLabel_ != nullptr
        && previewFullscreenHintWindow_ != nullptr
        && previewFullscreenWindow_ != nullptr) {
        const QString exitHint = uiText(
            "preview.fullscreen.exit_hint",
            QStringLiteral("Press Esc to exit fullscreen")
        );
        QTimer::singleShot(0, this, [this, exitHint]() {
            if (!previewFullscreenActive_
                || previewFullscreenWindow_ == nullptr
                || previewFullscreenHintWindow_ == nullptr
                || previewFullscreenHintLabel_ == nullptr) {
                return;
            }
            previewFullscreenHintLabel_->setText(exitHint);
            previewFullscreenHintLabel_->setStyleSheet(previewFullscreenHintStyleSheet());
            previewFullscreenHintLabel_->adjustSize();
            previewFullscreenHintWindow_->show();
            previewFullscreenHintWindow_->raise();
            previewFullscreenHintLabel_->show();
            updatePreviewFullscreenOverlayGeometry();
            if (previewFullscreenHintTimer_ != nullptr) {
                previewFullscreenHintTimer_->start(2200);
            }
        });
    }
    if (previewFullscreenControlsWindow_ != nullptr) {
        previewFullscreenControlsWindow_->setWindowOpacity(0.0);
        previewFullscreenControlsWindow_->hide();
    }
    updatePreviewFullscreenOverlayGeometry();
    if (previewFullscreenCursorPollTimer_ != nullptr) {
        previewFullscreenCursorPollTimer_->start();
    }
    previewCanvasContainer_->setFocus();
    if (previewCanvas_ != nullptr) {
        previewCanvas_->requestActivate();
    }
}

void MainWindow::exitPreviewFullscreen()
{
    if (isQuickShellBackendMode()) {
        if (!previewFullscreenActive_) {
            return;
        }
        previewFullscreenActive_ = false;
        previewFullscreenControlsVisible_ = false;
        previewFullscreenCursorTrackingInitialized_ = false;
        updatePauseButtonAppearance();
        updatePreviewFullscreenButtonAppearance();
        return;
    }
    if (!previewFullscreenActive_ || previewCanvasContainer_ == nullptr || previewCanvasFrame_ == nullptr) {
        return;
    }
    if (previewFullscreenControlsTimer_ != nullptr) {
        previewFullscreenControlsTimer_->stop();
    }
    if (previewFullscreenCursorPollTimer_ != nullptr) {
        previewFullscreenCursorPollTimer_->stop();
    }
    if (previewFullscreenControlsAnimation_ != nullptr) {
        previewFullscreenControlsAnimation_->stop();
    }
    if (previewFullscreenControlsOpacityAnimation_ != nullptr) {
        previewFullscreenControlsOpacityAnimation_->stop();
    }
    if (previewFullscreenHost_ != nullptr && previewFullscreenHost_->layout() != nullptr) {
        previewFullscreenHost_->layout()->removeWidget(previewCanvasContainer_);
    }
    previewCanvasContainer_->setParent(previewCanvasFrame_);
    if (previewControlCard_ != nullptr && previewControlCard_->parentWidget() == previewFullscreenControlsWindow_) {
        if (previewFullscreenControlsWindow_ != nullptr && previewFullscreenControlsWindow_->layout() != nullptr) {
            previewFullscreenControlsWindow_->layout()->removeWidget(previewControlCard_);
        }
        previewControlCard_->hide();
        previewControlCard_->setParent(previewPanel_);
        previewControlCard_->setStyleSheet(QString());
    }
    previewFullscreenActive_ = false;
    previewFullscreenControlsVisible_ = false;
    previewFullscreenCursorTrackingInitialized_ = false;
    if (previewFullscreenHintTimer_ != nullptr) {
        previewFullscreenHintTimer_->stop();
    }
    if (previewFullscreenHintLabel_ != nullptr) {
        previewFullscreenHintLabel_->hide();
    }
    if (previewFullscreenHintWindow_ != nullptr) {
        previewFullscreenHintWindow_->hide();
    }
    if (previewFullscreenControlsWindow_ != nullptr) {
        previewFullscreenControlsWindow_->setWindowOpacity(0.0);
        previewFullscreenControlsWindow_->hide();
    }
    if (previewFullscreenWindow_ != nullptr) {
        previewFullscreenWindow_->hide();
    }
    if (stopPreviewAction_ != nullptr) {
        stopPreviewAction_->setIcon(makePreviewStopIcon(UiTheme::colors().iconPrimary));
    }
    updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
    updatePreviewPanelLayout();
    if (previewControlCard_ != nullptr) {
        previewControlCard_->show();
    }
    previewCanvasContainer_->show();
    previewCanvasContainer_->setFocus();
    if (previewCanvas_ != nullptr) {
        previewCanvas_->requestActivate();
    }
}

void MainWindow::updatePreviewFullscreenButtonAppearance()
{
    if (previewFullscreenButton_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(previewFullscreenButton_);
    const QColor iconColor =
        previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : UiTheme::colors().iconPrimary;
    previewFullscreenButton_->setChecked(previewFullscreenActive_);
    previewFullscreenButton_->setText(QString());
    previewFullscreenButton_->setIcon(
        previewFullscreenActive_ ? makePreviewExitFullscreenIcon(iconColor) : makePreviewEnterFullscreenIcon(iconColor)
    );
    previewFullscreenButton_->setToolTip(QString());
}

bool MainWindow::shouldRevealPreviewFullscreenControls(const QPoint& globalCursorPos) const
{
    if (!previewFullscreenActive_ || previewFullscreenWindow_ == nullptr) {
        return false;
    }

    const QRect windowGlobalRect(
        previewFullscreenWindow_->mapToGlobal(QPoint(0, 0)),
        previewFullscreenWindow_->size()
    );
    if (!windowGlobalRect.contains(globalCursorPos)) {
        return false;
    }

    if (previewFullscreenControlsWindow_ != nullptr
        && previewFullscreenControlsWindow_->isVisible()
        && previewFullscreenControlsWindow_->geometry().contains(globalCursorPos)) {
        return true;
    }

    const int controlsHeight =
        previewControlCard_ != nullptr
            ? qMax(previewControlCard_->minimumSizeHint().height(), previewControlCard_->sizeHint().height())
            : 0;
    const int revealHotzoneHeight = qMin(
        windowGlobalRect.height(),
        qMax(kPreviewFullscreenControlsRevealHotzoneHeight, controlsHeight + kPreviewFullscreenOverlayBottomMargin)
    );
    return globalCursorPos.y() >= windowGlobalRect.bottom() - revealHotzoneHeight;
}

QRect MainWindow::previewFullscreenControlCardRect(bool visible) const
{
    if (previewFullscreenWindow_ == nullptr || previewControlCard_ == nullptr) {
        return QRect();
    }
    const QRect windowRect = previewFullscreenWindow_->contentsRect();
    if (windowRect.width() <= 0 || windowRect.height() <= 0) {
        return QRect();
    }
    const QPoint globalTopLeft = previewFullscreenWindow_->mapToGlobal(windowRect.topLeft());

    const int horizontalMargin = qMin(kPreviewFullscreenOverlaySideMargin, qMax(12, windowRect.width() / 20));
    const int availableWidth = qMax(0, windowRect.width() - horizontalMargin * 2);
    if (availableWidth <= 0) {
        return QRect();
    }

    const QSize preferredSize = previewControlCard_->sizeHint().expandedTo(previewControlCard_->minimumSizeHint());
    const int cardWidth = qMax(
        previewControlCard_->minimumSizeHint().width(),
        qMin(availableWidth, kPreviewFullscreenOverlayMaxWidth)
    );
    const int cardHeight = qMax(preferredSize.height(), previewControlCard_->minimumSizeHint().height());
    const int cardX = globalTopLeft.x() + qMax(0, (windowRect.width() - cardWidth) / 2);
    const int visibleY = globalTopLeft.y() + windowRect.height() - cardHeight - kPreviewFullscreenOverlayBottomMargin;
    const int hiddenY = globalTopLeft.y() + windowRect.height() + kPreviewFullscreenOverlayHideOffset;
    return QRect(cardX, visible ? visibleY : hiddenY, cardWidth, cardHeight);
}

void MainWindow::showPreviewFullscreenControls(bool animate)
{
    if (!previewFullscreenActive_
        || previewFullscreenWindow_ == nullptr
        || previewFullscreenControlsWindow_ == nullptr
        || previewControlCard_ == nullptr
        || previewControlCard_->parentWidget() != previewFullscreenControlsWindow_) {
        return;
    }

    const QRect targetRect = previewFullscreenControlCardRect(true);
    if (!targetRect.isValid()) {
        return;
    }

    if (previewFullscreenControlsAnimation_ != nullptr) {
        previewFullscreenControlsAnimation_->stop();
    }
    if (previewFullscreenControlsOpacityAnimation_ != nullptr) {
        previewFullscreenControlsOpacityAnimation_->stop();
    }

    QRect currentRect = previewFullscreenControlsWindow_->geometry();
    if (!currentRect.isValid()) {
        currentRect = previewFullscreenControlCardRect(false);
    }
    if (!previewFullscreenControlsWindow_->isVisible()) {
        previewFullscreenControlsWindow_->setGeometry(currentRect);
        previewFullscreenControlsWindow_->setWindowOpacity(0.0);
    }

    previewFullscreenControlsWindow_->show();
    previewFullscreenControlsWindow_->raise();
    previewControlCard_->show();

    const qreal currentOpacity = previewFullscreenControlsWindow_->windowOpacity();
    if (!animate || !currentRect.isValid() || currentRect == targetRect) {
        previewFullscreenControlsWindow_->setGeometry(targetRect);
        previewFullscreenControlsWindow_->setWindowOpacity(1.0);
    } else {
        if (previewFullscreenControlsAnimation_ != nullptr) {
            previewFullscreenControlsAnimation_->setStartValue(currentRect);
            previewFullscreenControlsAnimation_->setEndValue(targetRect);
            previewFullscreenControlsAnimation_->start();
        } else {
            previewFullscreenControlsWindow_->setGeometry(targetRect);
        }
        if (previewFullscreenControlsOpacityAnimation_ != nullptr) {
            previewFullscreenControlsOpacityAnimation_->setStartValue(currentOpacity);
            previewFullscreenControlsOpacityAnimation_->setEndValue(1.0);
            previewFullscreenControlsOpacityAnimation_->start();
        } else {
            previewFullscreenControlsWindow_->setWindowOpacity(1.0);
        }
    }

    previewFullscreenControlsVisible_ = true;
    schedulePreviewFullscreenControlsAutoHide();
}

void MainWindow::hidePreviewFullscreenControls(bool animate)
{
    if (!previewFullscreenActive_
        || previewFullscreenWindow_ == nullptr
        || previewFullscreenControlsWindow_ == nullptr
        || previewControlCard_ == nullptr
        || previewControlCard_->parentWidget() != previewFullscreenControlsWindow_) {
        return;
    }

    const bool pointerOverControls =
        previewControlCard_->underMouse()
        || (previewSlider_ != nullptr && previewSlider_->underMouse())
        || (stopPreviewButton_ != nullptr && stopPreviewButton_->underMouse())
        || (pausePreviewButton_ != nullptr && pausePreviewButton_->underMouse())
        || (previewSpeedButton_ != nullptr && previewSpeedButton_->underMouse())
        || (previewFullscreenButton_ != nullptr && previewFullscreenButton_->underMouse());
    const bool speedMenuVisible =
        previewSpeedButton_ != nullptr
        && previewSpeedButton_->menu() != nullptr
        && previewSpeedButton_->menu()->isVisible();
    if (previewSliderDragging_ || pointerOverControls || speedMenuVisible) {
        schedulePreviewFullscreenControlsAutoHide();
        return;
    }

    const QRect targetRect = previewFullscreenControlCardRect(false);
    if (!targetRect.isValid()) {
        return;
    }

    if (previewFullscreenControlsAnimation_ != nullptr) {
        previewFullscreenControlsAnimation_->stop();
    }
    if (previewFullscreenControlsOpacityAnimation_ != nullptr) {
        previewFullscreenControlsOpacityAnimation_->stop();
    }

    const QRect currentRect = previewFullscreenControlsWindow_->geometry();
    if (!animate || !currentRect.isValid() || currentRect == targetRect) {
        previewFullscreenControlsWindow_->setGeometry(targetRect);
        previewFullscreenControlsWindow_->setWindowOpacity(0.0);
        previewFullscreenControlsWindow_->hide();
    } else {
        if (previewFullscreenControlsAnimation_ != nullptr) {
            previewFullscreenControlsAnimation_->setStartValue(currentRect);
            previewFullscreenControlsAnimation_->setEndValue(targetRect);
            previewFullscreenControlsAnimation_->start();
        } else {
            previewFullscreenControlsWindow_->setGeometry(targetRect);
        }
        if (previewFullscreenControlsOpacityAnimation_ != nullptr) {
            previewFullscreenControlsOpacityAnimation_->setStartValue(previewFullscreenControlsWindow_->windowOpacity());
            previewFullscreenControlsOpacityAnimation_->setEndValue(0.0);
            previewFullscreenControlsOpacityAnimation_->start();
        } else {
            previewFullscreenControlsWindow_->setWindowOpacity(0.0);
            previewFullscreenControlsWindow_->hide();
        }
    }

    previewFullscreenControlsVisible_ = false;
}

void MainWindow::schedulePreviewFullscreenControlsAutoHide()
{
    if (!previewFullscreenActive_ || previewFullscreenControlsTimer_ == nullptr) {
        return;
    }
    previewFullscreenControlsTimer_->start(kPreviewFullscreenControlsAutoHideDelayMs);
}

void MainWindow::pollPreviewFullscreenCursor()
{
    if (!previewFullscreenActive_ || previewFullscreenWindow_ == nullptr) {
        return;
    }

    const QPoint globalCursorPos = QCursor::pos();
    const QRect windowGlobalRect(
        previewFullscreenWindow_->mapToGlobal(QPoint(0, 0)),
        previewFullscreenWindow_->size()
    );
    const bool insideWindow = windowGlobalRect.contains(globalCursorPos);
    if (!insideWindow) {
        if (previewFullscreenControlsVisible_) {
            schedulePreviewFullscreenControlsAutoHide();
        }
        previewFullscreenCursorTrackingInitialized_ = false;
        return;
    }

    if (!previewFullscreenCursorTrackingInitialized_) {
        previewFullscreenLastCursorPos_ = globalCursorPos;
        previewFullscreenCursorTrackingInitialized_ = true;
        return;
    }

    if (previewFullscreenLastCursorPos_ != globalCursorPos) {
        previewFullscreenLastCursorPos_ = globalCursorPos;
        if (shouldRevealPreviewFullscreenControls(globalCursorPos)) {
            showPreviewFullscreenControls(true);
        } else if (previewFullscreenControlsVisible_) {
            schedulePreviewFullscreenControlsAutoHide();
        }
    }
}

void MainWindow::updatePreviewFullscreenOverlayGeometry()
{
    if (!previewFullscreenActive_ || previewFullscreenWindow_ == nullptr) {
        return;
    }

    const QRect windowRect = previewFullscreenWindow_->contentsRect();
    const QPoint globalTopLeft = previewFullscreenWindow_->mapToGlobal(windowRect.topLeft());

    if (previewFullscreenHintWindow_ != nullptr
        && previewFullscreenHintLabel_ != nullptr
        && previewFullscreenHintLabel_->isVisible()) {
        const QSize hintSize = previewFullscreenHintLabel_->sizeHint().expandedTo(QSize(220, 42));
        previewFullscreenHintWindow_->resize(hintSize);
        previewFullscreenHintWindow_->move(
            globalTopLeft.x() + qMax(16, (windowRect.width() - hintSize.width()) / 2),
            globalTopLeft.y() + kPreviewFullscreenHintTopMargin
        );
        previewFullscreenHintLabel_->resize(hintSize);
        previewFullscreenHintLabel_->raise();
        previewFullscreenHintWindow_->raise();
    }

    if (previewFullscreenControlsWindow_ != nullptr
        && previewControlCard_ != nullptr
        && previewControlCard_->parentWidget() == previewFullscreenControlsWindow_) {
        const QRect targetRect = previewFullscreenControlCardRect(previewFullscreenControlsVisible_);
        if (targetRect.isValid()) {
            if (previewFullscreenControlsAnimation_ != nullptr
                && previewFullscreenControlsAnimation_->state() == QAbstractAnimation::Running) {
                previewFullscreenControlsAnimation_->stop();
            }
            previewFullscreenControlsWindow_->setGeometry(targetRect);
            previewFullscreenControlsWindow_->setWindowOpacity(previewFullscreenControlsVisible_ ? 1.0 : 0.0);
            if (previewFullscreenControlsVisible_) {
                previewFullscreenControlsWindow_->show();
            }
            previewFullscreenControlsWindow_->raise();
        }
    }
}

void MainWindow::updatePreviewWorkspaceLayout()
{
    if (isQuickShellBackendMode()) {
        if (quickShellWorkspaceSurfaceWidget_ != nullptr) {
            quickShellWorkspaceSurfaceWidget_->updateGeometry();
            if (QLayout* layout = quickShellWorkspaceSurfaceWidget_->layout(); layout != nullptr) {
                layout->activate();
            }
        }
        if (quickShellPreviewControlsSurfaceWidget_ != nullptr) {
            quickShellPreviewControlsSurfaceWidget_->updateGeometry();
            if (QLayout* layout = quickShellPreviewControlsSurfaceWidget_->layout(); layout != nullptr) {
                layout->activate();
            }
        }
        updateEditorFindBarGeometry();
        applyFindOverlayInset();
        return;
    }
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
    const int controlHeight = qMax(previewControlCard_->minimumSizeHint().height(), previewControlCard_->sizeHint().height());
    const double aspectRatio = normalizedPreviewCanvasAspectRatio(previewCanvasAspectRatio_);
    const int statsHostWidth = qMax(
        0,
        qMax(minimumRightWidth, qMin(kEmbeddedPreviewPanelWidthMax, availableWidth)) - kPreviewPanelMarginX * 2 - 16
    );
    const int minimumStatsHeight = updatePreviewStatsLayoutMode(statsHostWidth);
    int targetRightWidth = miacode::window_parity::computePreviewPanelTargetWidth(
        availableWidth,
        availableHeight,
        leftMinWidth,
        controlHeight,
        minimumStatsHeight,
        aspectRatio
    );
    targetRightWidth = qBound(minimumRightWidth, targetRightWidth, rightMaxWidth);
    const int targetLeftWidth =
        (availableWidth >= leftMinWidth + targetRightWidth)
        ? qMax(leftMinWidth, availableWidth - targetRightWidth)
        : qMax(0, availableWidth - targetRightWidth);
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
    if (isQuickShellBackendMode()) {
        return;
    }
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
    if (isQuickShellBackendMode()) {
        return;
    }
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
    if (isQuickShellBackendMode()) {
        if (swapWorkspaceSidesAction_ != nullptr) {
            swapWorkspaceSidesAction_->blockSignals(true);
            swapWorkspaceSidesAction_->setChecked(workspacePanelsSwapped_);
            swapWorkspaceSidesAction_->setIcon(
                makeMenuSelectionCheckIcon(UiTheme::colors().accent, workspacePanelsSwapped_)
            );
            swapWorkspaceSidesAction_->blockSignals(false);
        }
        refreshLayoutAfterPageSwitch();
        return;
    }
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
    updatePreviewControlsLayout(
        previewControlsLayout_,
        stopPreviewButton_,
        pausePreviewButton_,
        previewSlider_,
        previewSpeedButton_,
        previewFullscreenButton_,
        workspacePanelsSwapped_
    );
    if (workspaceSplitter_ != nullptr) {
        workspaceSplitter_->setLayoutDirection(
            workspacePanelsSwapped_ ? Qt::RightToLeft : Qt::LeftToRight
        );
    }
    if (outlineDock_ != nullptr) {
        addDockWidget(
            Qt::LeftDockWidgetArea,
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
    if (isQuickShellBackendMode()) {
        if (quickShellWorkspaceSurfaceWidget_ != nullptr) {
            quickShellWorkspaceSurfaceWidget_->updateGeometry();
            if (QLayout* layout = quickShellWorkspaceSurfaceWidget_->layout(); layout != nullptr) {
                layout->activate();
            }
        }
        if (quickShellPreviewControlsSurfaceWidget_ != nullptr) {
            quickShellPreviewControlsSurfaceWidget_->updateGeometry();
            if (QLayout* layout = quickShellPreviewControlsSurfaceWidget_->layout(); layout != nullptr) {
                layout->activate();
            }
        }
        updateEditorHeaderLayoutMode();
        if (timelineView_ != nullptr) {
            timelineView_->updateGeometry();
            timelineView_->viewport()->update();
        }
        return;
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
    if (isQuickShellBackendMode()) {
        if (quickShellPreviewControlsSurfaceWidget_ != nullptr) {
            quickShellPreviewControlsSurfaceWidget_->updateGeometry();
            if (QLayout* layout = quickShellPreviewControlsSurfaceWidget_->layout(); layout != nullptr) {
                layout->activate();
            }
        }
        return;
    }
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

    const int controlHeight = qMax(previewControlCard_->minimumSizeHint().height(), previewControlCard_->sizeHint().height());
    const double aspectRatio = normalizedPreviewCanvasAspectRatio(previewCanvasAspectRatio_);
    const miacode::window_parity::PreviewPanelLayout layout = miacode::window_parity::computePreviewPanelLayout(
        panelRect.width(),
        panelRect.height(),
        controlHeight,
        aspectRatio
    );
    updatePreviewStatsLayoutMode(layout.statsHostWidth);
    previewCanvasFrame_->setGeometry(layout.previewX, layout.previewY, layout.previewWidth, layout.previewHeight);
    if (!previewFullscreenActive_) {
        previewCanvasContainer_->setGeometry(previewCanvasFrame_->rect().adjusted(1, 1, -1, -1));
    }
    if (!previewFullscreenActive_) {
        previewControlCard_->setGeometry(layout.controlX, layout.controlY, layout.controlWidth, controlHeight);
    } else {
        updatePreviewFullscreenOverlayGeometry();
    }
    previewStatsCard_->setGeometry(layout.statsX, layout.statsY, layout.statsWidth, layout.statsHeight);
    updatePreviewStatsLayoutMode(layout.statsHostWidth);

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

    const miacode::preview::scene::PreviewObjectStatsSnapshot stats =
        previewProgressStatsCache_ != nullptr
        ? previewProgressStatsCache_->snapshotAt(second)
        : miacode::preview::scene::PreviewObjectStatsSnapshot();

    const auto fmt = [](const QString& name, int played, int total) {
        return QString("%1  %2/%3")
            .arg(name.leftJustified(5, QChar(' '), true))
            .arg(played)
            .arg(total);
    };
    previewTapStatsLabel_->setText(fmt("Tap", stats.tapPlayed, stats.tapTotal));
    previewHoldStatsLabel_->setText(fmt("Hold", stats.holdPlayed, stats.holdTotal));
    previewSlideStatsLabel_->setText(fmt("Slide", stats.slidePlayed, stats.slideTotal));
    previewTouchStatsLabel_->setText(fmt("Touch", stats.touchPlayed, stats.touchTotal));
    previewBreakStatsLabel_->setText(fmt("Break", stats.breakPlayed, stats.breakTotal));
    previewTotalStatsLabel_->setText(fmt("Total", stats.totalPlayed, stats.totalCount));
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

bool MainWindow::stepPreviewSliderBySeconds(double deltaSeconds, bool centerView)
{
    if (previewSlider_ == nullptr || !qIsFinite(deltaSeconds)) {
        return false;
    }
    const int deltaMs = qRound(deltaSeconds * 1000.0);
    if (deltaMs == 0) {
        return false;
    }
    const int value = qBound(
        previewSlider_->minimum(),
        previewSlider_->value() + deltaMs,
        previewSlider_->maximum()
    );
    if (value == previewSlider_->value()) {
        showPreviewSliderTimeHint(value);
        return true;
    }
    previewSlider_->setValue(value);
    showPreviewSliderTimeHint(value);
    seekPreviewToSecond(static_cast<double>(value) / 1000.0, centerView);
    return true;
}

bool MainWindow::handlePreviewSliderWheel(QWheelEvent* event)
{
    if (previewSlider_ == nullptr || event == nullptr) {
        return false;
    }
    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->angleDelta().x();
    }
    if (delta == 0) {
        delta = event->pixelDelta().y();
    }
    if (delta == 0) {
        delta = event->pixelDelta().x();
    }
    if (delta == 0) {
        return false;
    }
    const int steps = delta > 0 ? qMax(1, qRound(static_cast<double>(delta) / 120.0))
                                : qMin(-1, qRound(static_cast<double>(delta) / 120.0));
    previewSlider_->setFocus(Qt::MouseFocusReason);
    const bool handled = stepPreviewSliderBySeconds(
        static_cast<double>(steps) * miacode::preview_interaction::kSeekSingleStepSeconds,
        true
    );
    if (handled) {
        event->accept();
    }
    return handled;
}

void MainWindow::beginPreviewHeldSeek(int direction, int key)
{
    if (direction == 0 || previewSlider_ == nullptr) {
        return;
    }
    previewHeldSeekDirection_ = direction > 0 ? 1 : -1;
    previewSeekHeldArrowKey_ = key;
    previewSeekHeldArrowLastElapsedMs_ = 0;
    previewSeekHeldArrowElapsed_.restart();
    if (previewHeldSeekTimer_ != nullptr && !previewHeldSeekTimer_->isActive()) {
        previewHeldSeekTimer_->start();
    }
}

void MainWindow::stopPreviewHeldSeek(int key)
{
    if (key != 0 && previewSeekHeldArrowKey_ != key) {
        return;
    }
    previewHeldSeekDirection_ = 0;
    previewSeekHeldArrowKey_ = 0;
    previewSeekHeldArrowLastElapsedMs_ = 0;
    previewSeekHeldArrowElapsed_.invalidate();
    if (previewHeldSeekTimer_ != nullptr) {
        previewHeldSeekTimer_->stop();
    }
}

void MainWindow::applyPreviewHeldSeekTick()
{
    if (previewHeldSeekDirection_ == 0
        || previewSeekHeldArrowKey_ == 0
        || !previewSeekHeldArrowElapsed_.isValid()) {
        return;
    }
    const int elapsedMs = static_cast<int>(previewSeekHeldArrowElapsed_.elapsed());
    const int deltaMs = previewSeekHeldArrowLastElapsedMs_ > 0
        ? (elapsedMs - previewSeekHeldArrowLastElapsedMs_)
        : miacode::preview_interaction::kSeekHoldTickIntervalMs;
    previewSeekHeldArrowLastElapsedMs_ = elapsedMs;
    const double heldSeconds = static_cast<double>(elapsedMs) / 1000.0;
    stepPreviewSliderBySeconds(
        static_cast<double>(previewHeldSeekDirection_)
            * miacode::preview_interaction::heldSeekStepSecondsForDeltaMs(deltaMs, heldSeconds),
        true
    );
}

void MainWindow::seekPreviewToSecond(double second, bool centerView)
{
    ensurePreviewStageMediaRouteInitialized();
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
    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    syncPausedPreviewMediaTimestamps(clampedSecond);
    applyQtPreviewPosition(clampedSecond, centerView);
    if (timelineView_ != nullptr) {
        timelineView_->focusPlayhead(centerView);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->update();
    }
    updatePreviewSliderPosition(clampedSecond);
}

void MainWindow::applyPreviewPlaybackRate(double rate)
{
    ensurePreviewStageMediaRouteInitialized();
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
    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
    }
    if (qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
        startQtPreviewPlayback(qtPreviewPauseSecond_, true);
    }
}

bool MainWindow::startQtPreviewPlayback(double second, bool resumeFromPause)
{
    if (!preparePreviewStartState()) {
        pendingPreviewPlaybackStart_ = hasActiveDifficulty();
        pendingPreviewPlaybackResumeFromPause_ = resumeFromPause;
        pendingPreviewPlaybackRevision_ = timelineRevision_;
        pendingPreviewPlaybackDifficultyId_ = activeDifficultyId();
        pendingPreviewPlaybackSecond_ = qBound(0.0, second, previewDurationSeconds());
        return false;
    }

    pendingPreviewPlaybackStart_ = false;

    ensurePreviewStageMediaRouteInitialized();
    ensurePreviewSfxRuntimePrepared();
    applyLatestTimelinePreviewStateToPausedPreview();
    const double startSecond = qBound(0.0, second, previewDurationSeconds());
    const bool hasVideoMedia = previewStageMediaRouteHasVideo();
    const auto applyPlaybackClockState = [this](double initialSecond) {
        qtPreviewStartSecond_ = initialSecond;
        qtPreviewPauseSecond_ = initialSecond;
        qtPreviewLastTimelineSecond_ = initialSecond;
        qtPreviewPendingTimelineSecond_ = initialSecond;
        qtPreviewPendingTimelineCenterView_ = true;
        qtPreviewTimelineDirty_ = false;
        qtPreviewTimelineStartSecond_ = initialSecond;
    };

    qtPreviewPlaybackReturnSecond_ = startSecond;
    qtPreviewPlaybackEndSecond_ = qMax(0.0, previewPlaybackEndSeconds());
    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);

    double effectiveStartSecond = startSecond;
    if (previewSfxRuntime_ != nullptr) {
        effectiveStartSecond = previewSfxRuntime_->startPreviewPlaybackTransaction(
            startSecond,
            resumeFromPause,
            previewPlaybackRate_);
    }

    applyPlaybackClockState(effectiveStartSecond);
    pausedPreviewMediaSeekPending_ = false;
    qtPreviewElapsed_.restart();
    qtPreviewTimelineElapsed_.restart();
    if (timelineView_ != nullptr) {
        timelineView_->setPlaybackEntrySeconds(qtPreviewPlaybackReturnSecond_);
        timelineView_->setPlayheadUpperLimitSeconds(qtPreviewPlaybackEndSecond_);
        if (qFuzzyCompare(timelineView_->playheadSeconds() + 1.0, effectiveStartSecond + 1.0)) {
            timelineView_->focusPlayhead(true);
        } else {
            timelineView_->setPlayheadSeconds(effectiveStartSecond, true);
            timelineView_->focusPlayhead(false);
        }
    }
    if (previewCanvas_ != nullptr) {
        if (!resumeFromPause) {
            previewCanvas_->resetProfilingSession();
        }
        previewCanvas_->setPlayheadSeconds(effectiveStartSecond, false);
    }
    if (hasVideoMedia) {
        startPreviewStageMediaRoutePlayback(effectiveStartSecond);
    }

    qtPreviewPlaying_ = true;
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    resetQtPreviewFixedFramePacing();
    if (previewCanvas_ != nullptr && !previewCanvasUsesFrameSwappedPacing()) {
        previewCanvas_->update();
    }
    if (previewCanvas_ != nullptr && previewCanvasUsesFrameSwappedPacing()) {
        requestNextDisplayRefreshPreviewFrame();
    } else {
        scheduleNextQtPreviewTick();
    }
    if (qtPreviewTimelineTimer_ != nullptr && !qtPreviewTimelineTimer_->isActive()) {
        qtPreviewTimelineTimer_->start();
    }
    if (previewStatsUiTimer_ != nullptr && !previewStatsUiTimer_->isActive()) {
        previewStatsUiTimer_->start();
    }
    syncEditorCursorToPreviewSecond(effectiveStartSecond, false);
    updatePreviewSliderPosition(effectiveStartSecond);
    updatePauseButtonAppearance();
    return true;
}

void MainWindow::finishQtPreviewPlaybackAndReturnToEntry(const QString& statusMessage)
{
    stopQtPreviewPlayback(true);
    if (statusBar() != nullptr && !statusMessage.isEmpty()) {
        statusBar()->showMessage(statusMessage);
    }
}

void MainWindow::stopQtPreviewPlayback(bool keepPosition)
{
    const bool wasPlaying = qtPreviewPlaying_;
    bool pauseSecondCaptured = false;
    if (previewSfxRuntime_ != nullptr) {
        const QtPreviewSfxRuntime::PausePreviewResult pauseResult =
            previewSfxRuntime_->capturePausedPreviewTransaction();
        if (pauseResult.usedBackgroundTrack) {
            qtPreviewPauseSecond_ = pauseResult.pauseSecond;
            pauseSecondCaptured = true;
        }
    }
    if (!pauseSecondCaptured) {
        if (previewStageMediaRouteHasVideo()) {
            qtPreviewPauseSecond_ = previewStageMediaRouteCurrentPlaybackSecond();
        }
    }
    pausePreviewStageMediaRoutePlayback();
    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->stop();
    }
    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->stop();
    }
    if (qtPreviewTimelineTimer_ != nullptr) {
        qtPreviewTimelineTimer_->stop();
    }
    if (previewStatsUiTimer_ != nullptr) {
        previewStatsUiTimer_->stop();
    }
    if (!keepPosition) {
        qtPreviewPauseSecond_ = 0.0;
    }
    pausedPreviewMediaSeekPending_ = false;
    if (wasPlaying) {
        qtPreviewPendingTimelineSecond_ = qtPreviewPauseSecond_;
        qtPreviewPendingTimelineCenterView_ = false;
        qtPreviewTimelineDirty_ = true;
    }
    qtPreviewPlaying_ = false;
    qtPreviewAwaitingFrameSwap_ = false;
    qtPreviewAwaitingFrameSwapSinceMs_ = -1;
    qtPreviewNextFixedTickDueNs_ = -1;
    flushQtPreviewTimelinePosition();
    if (timelineView_ != nullptr) {
        timelineView_->focusPlayhead(false);
        timelineView_->setPlayheadUpperLimitSeconds(previewDurationSeconds());
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->stopAll();
    }
    applyLatestTimelinePreviewStateToPausedPreview();
    applyDeferredAnalysisUiUpdates();
    if (pendingTimelineAnalysisRefresh_.revision != 0) {
        requestTimelineAnalysisDispatch(0);
    }
    if (runtimeDebugOutputEnabled_ && wasPlaying && previewCanvas_ != nullptr) {
        previewCanvas_->writeProfilingSummaryToFile();
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
    setPreviewStageMediaRouteObservedPlayheadSecond(second);
    refreshPreviewStageMediaRouteDebugState(!qtPreviewPlaying_);
    updatePreviewSliderPosition(second);
    if (!qtPreviewPlaying_) {
        updatePreviewObjectStats(second);
    }
    if (qtPreviewPlaying_) {
        syncEditorCursorToPreviewSecond(second, centerView);
    }
}

void MainWindow::syncPausedPreviewMediaTimestamps(double second)
{
    seekPreviewStageMediaRouteWhilePaused(second);
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
        timelineView_->setPlayheadSeconds(second, true);
        timelineView_->focusPlayhead(false);
        qtPreviewLastTimelineSecond_ = second;
        return;
    }
    if (!qtPreviewTimelineDirty_) {
        return;
    }
    timelineView_->setPlayheadSeconds(qtPreviewPendingTimelineSecond_, qtPreviewPendingTimelineCenterView_);
    timelineView_->focusPlayhead(qtPreviewPendingTimelineCenterView_);
    qtPreviewLastTimelineSecond_ = qtPreviewPendingTimelineSecond_;
    qtPreviewPendingTimelineCenterView_ = false;
    qtPreviewTimelineDirty_ = false;
}

void MainWindow::onQtPreviewTick()
{
    if (!qtPreviewPlaying_) {
        return;
    }
    double second = 0.0;
    if (previewSfxRuntime_ == nullptr) {
        const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        second = qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
    } else {
        const double elapsedSeconds = static_cast<double>(qtPreviewElapsed_.nsecsElapsed()) / 1000000000.0;
        const double fallbackSecond = qtPreviewStartSecond_ + (elapsedSeconds * previewPlaybackRate_);
        second = previewSfxRuntime_->syncPreviewPlaybackClockTransaction(fallbackSecond);
    }
    syncPreviewStageMediaRoutePlayback(second);
    const double playbackEndSecond = previewPlaybackEndSeconds();
    if (playbackEndSecond > 0.0
        && second + kTimelineZeroSecondTolerance >= playbackEndSecond) {
        second = playbackEndSecond;
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
