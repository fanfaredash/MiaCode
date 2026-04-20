void LatencyDetectorDialog::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setSizeConstraint(QLayout::SetMinimumSize);
    rootLayout->setContentsMargins(14, 14, 14, 14);
    rootLayout->setSpacing(10);

    auto* bpmRow = new QHBoxLayout();
    bpmRow->setSpacing(8);
    auto* meterLabel = new QLabel(localizedText("拍号：", "Meter:"), this);
    meterCombo_ = new QComboBox(this);
    meterCombo_->setFixedWidth(88);
    for (const MeterPattern& pattern : meterPatterns()) {
        const QString meterId = QString::fromLatin1(pattern.id);
        QString label = QString::fromLatin1(pattern.label);
        if (meterId == QLatin1String("auto")) {
            label = localizedText("自动检测", "Auto Detect");
        }
        meterCombo_->addItem(label, meterId);
    }
    meterCombo_->setCurrentIndex(0);
    auto* bpmLabel = new QLabel(localizedText("BPM：", "BPM:"), this);
    bpmEdit_ = new QLineEdit(this);
    bpmEdit_->setValidator(new QDoubleValidator(1.0, 400.0, 3, bpmEdit_));
    bpmEdit_->setPlaceholderText("180.000");
    bpmEdit_->setFixedWidth(74);
    detectBpmButton_ = new QPushButton(localizedText("检测BPM", "Detect BPM"), this);
    bpmHelpButton_ = new QPushButton(localizedText(QStringLiteral("\u6D4B\u5F97\u4E0D\u51C6?"), "Inaccurate?"), this);
    bpmHelpButton_->setMinimumWidth(120);
    bpmRow->addWidget(bpmLabel);
    bpmRow->addWidget(bpmEdit_);
    bpmRow->addWidget(detectBpmButton_);
    bpmRow->addSpacing(16);
    bpmRow->addWidget(bpmHelpButton_);
    bpmRow->addStretch(1);
    rootLayout->addLayout(bpmRow);

    auto* offsetRow = new QHBoxLayout();
    offsetRow->setSpacing(6);
    auto* offsetLabel = new QLabel(localizedText("偏移：", "Offset:"), this);
    offsetEdit_ = new QLineEdit(this);
    offsetEdit_->setValidator(new QDoubleValidator(-9999.0, 9999.0, 3, offsetEdit_));
    offsetEdit_->setAlignment(Qt::AlignCenter);
    offsetEdit_->setFixedWidth(74);
    restoreDetectedOffsetButton_ = new QPushButton(localizedText(QStringLiteral("\u8FD8\u539F"), "Restore"), this);
    restoreDetectedOffsetButton_->setEnabled(false);
    detectOffsetButton_ = new QPushButton(localizedText("检测偏移", "Detect Offset"), this);

    const auto addAdjustButton = [this, offsetRow](const QString& text, double delta) {
        auto* button = new QPushButton(text, this);
        button->setFixedWidth(24);
        connect(button, &QPushButton::clicked, this, [this, delta]() {
            const double nextOffset = parsedOffset() + delta;
            updateOffsetEdit(nextOffset, true);
            restartPlaybackAfterOffsetChange();
        });
        offsetRow->addWidget(button);
    };

    offsetRow->addWidget(offsetLabel);
    addAdjustButton("<<", -0.010);
    addAdjustButton("<", -0.001);
    offsetRow->addWidget(offsetEdit_);
    addAdjustButton(">", 0.001);
    addAdjustButton(">>", 0.010);
    offsetRow->addWidget(detectOffsetButton_);
    offsetRow->addWidget(restoreDetectedOffsetButton_);
    offsetRow->addStretch(1);
    rootLayout->addLayout(offsetRow);

    auto* timingRow = new QHBoxLayout();
    timingRow->setSpacing(8);
    auto* snapLabel = new QLabel(localizedText(QStringLiteral("\u504F\u79FB\u5438\u9644\u8BBE\u7F6E"), "Offset Snap:"), this);
    offsetSnapCombo_ = new QComboBox(this);
    offsetSnapCombo_->setFixedWidth(96);
    offsetSnapCombo_->addItem(localizedText(QStringLiteral("\u5C0F\u8282"), "Bar"), QStringLiteral("bar"));
    offsetSnapCombo_->addItem(localizedText(QStringLiteral("4\u5206\u97F3\u7B26"), "Quarter"), QStringLiteral("quarter"));
    offsetSnapCombo_->addItem(localizedText(QStringLiteral("8\u5206\u97F3\u7B26"), "Eighth"), QStringLiteral("eighth"));
    offsetSnapCombo_->setCurrentIndex(1);
    zoomButton_ = new QToolButton(this);
    zoomButton_->setAutoRaise(false);
    zoomButton_->setCursor(Qt::PointingHandCursor);
    zoomButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    zoomButton_->setStyleSheet(UiTheme::timelineZoomButtonStyleSheet());
    zoomButton_->setFixedHeight(24);
    const int zoomButtonWidth = qMax(
        88,
        QFontMetrics(zoomButton_->font()).horizontalAdvance(UiTheme::timelineZoomButtonText(2.0)) + 32);
    zoomButton_->setFixedWidth(zoomButtonWidth);
    timingRow->addWidget(meterLabel);
    timingRow->addWidget(meterCombo_);
    timingRow->addSpacing(10);
    timingRow->addWidget(snapLabel);
    timingRow->addWidget(offsetSnapCombo_);
    timingRow->addStretch(1);
    timingRow->addWidget(zoomButton_, 0, Qt::AlignRight | Qt::AlignVCenter);
    rootLayout->addLayout(timingRow);

    waveformView_ = new TimelineView(this);
    waveformView_->setPresentationMode(TimelineView::PresentationMode::WaveformOnly);
    waveformView_->setFollowPreviewEnabled(false);
    connect(waveformView_, &TimelineView::previewPlayPauseRequested, this, &LatencyDetectorDialog::togglePlayback);
    connect(waveformView_, &TimelineView::centerNavigateRequested, this, [this](double second) {
        schedulePausedSeek(second, false, false);
    });
    connect(waveformView_, &TimelineView::timelineDragFinished, this, [this](double second) {
        schedulePausedSeek(second, false, true);
    });
    connect(waveformView_, &TimelineView::renderStateChanged, this, &LatencyDetectorDialog::updateZoomButtonUi);
    rootLayout->addWidget(waveformView_, 1);
    playbackSlider_ = new QSlider(Qt::Horizontal, this);
    playbackSlider_->setRange(0, 0);
    playbackSlider_->setSingleStep(10);
    playbackSlider_->setPageStep(300);
    rootLayout->addWidget(playbackSlider_);

    auto* controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(8);
    playPauseButton_ = new QToolButton(this);
    playPauseButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    playPauseButton_->setIconSize(QSize(16, 16));
    playPauseButton_->setFixedSize(QSize(32, 26));
    playPauseButton_->setAutoRaise(false);
    playPauseButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
    stopButton_ = new QToolButton(this);
    stopButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stopButton_->setIconSize(QSize(16, 16));
    stopButton_->setFixedSize(QSize(32, 26));
    stopButton_->setAutoRaise(false);
    stopButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
    playbackTimeLabel_ = new QLabel(this);
    playbackTimeLabel_->setMinimumWidth(70);
    playbackTimeLabel_->setFixedHeight(26);
    playbackTimeLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    speedButton_ = new QToolButton(this);
    speedButton_->setPopupMode(QToolButton::InstantPopup);
    speedButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    speedButton_->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
    speedButton_->setFixedSize(QSize(64, 26));
    speedButton_->setText("1x");
    auto* speedMenu = new QMenu(speedButton_);
    const QList<QPair<double, QString>>& speedOptions = latencyPlaybackSpeedOptions();
    for (const auto& speedOption : speedOptions) {
        QAction* action = speedMenu->addAction(speedOption.second);
        action->setCheckable(true);
        action->setChecked(qFuzzyCompare(speedOption.first, 1.0));
        connect(action, &QAction::triggered, this, [this, speedMenu, speed = speedOption.first]() {
            for (QAction* entry : speedMenu->actions()) {
                entry->setChecked(false);
            }
            if (QAction* action = qobject_cast<QAction*>(sender()); action != nullptr) {
                action->setChecked(true);
            }
            applyPlaybackRate(speed);
        });
    }
    speedButton_->setMenu(speedMenu);
    sfxVolumeSlider_ = new QSlider(Qt::Horizontal, this);
    sfxVolumeSlider_->setRange(0, 100);
    sfxVolumeSlider_->setValue(25);
    sfxVolumeSlider_->setFixedWidth(110);
    sfxVolumeValueLabel_ = new QLabel("25%", this);
    sfxVolumeValueLabel_->setMinimumWidth(42);
    auto* sfxLabel = new QLabel(localizedText("效果音", "SFX"), this);

    controlsRow->addWidget(playPauseButton_, 0, Qt::AlignVCenter);
    controlsRow->addWidget(stopButton_, 0, Qt::AlignVCenter);
    controlsRow->addWidget(playbackTimeLabel_, 0, Qt::AlignVCenter);
    controlsRow->addSpacing(4);
    controlsRow->addWidget(speedButton_, 0, Qt::AlignVCenter);
    controlsRow->addSpacing(48);
    controlsRow->addStretch(1);
    controlsRow->addWidget(sfxLabel, 0, Qt::AlignVCenter);
    controlsRow->addWidget(sfxVolumeSlider_, 0, Qt::AlignVCenter);
    controlsRow->addWidget(sfxVolumeValueLabel_, 0, Qt::AlignVCenter);
    rootLayout->addLayout(controlsRow);

    sfxRuntime_ = new QtPreviewSfxRuntime(this);
    sfxRuntime_->setChartPath(chartPath_);
    sfxRuntime_->setBackgroundTrackOffsetSeconds(0.0);
    sfxRuntime_->setBackgroundTrackPlaybackRate(playbackRate_);
    sfxRuntime_->reloadAssets(audioSettings_);

    playbackTimer_ = new QTimer(this);
    playbackTimer_->setTimerType(Qt::PreciseTimer);
    playbackTimer_->setInterval(8);
    connect(playbackTimer_, &QTimer::timeout, this, &LatencyDetectorDialog::onPlaybackTick);

    pausedSeekTimer_ = new QTimer(this);
    pausedSeekTimer_->setSingleShot(true);
    pausedSeekTimer_->setTimerType(Qt::PreciseTimer);
    pausedSeekTimer_->setInterval(33);
    connect(pausedSeekTimer_, &QTimer::timeout, this, &LatencyDetectorDialog::commitPendingPausedSeek);

    offsetReplayTimer_ = new QTimer(this);
    offsetReplayTimer_->setSingleShot(true);
    connect(offsetReplayTimer_, &QTimer::timeout, this, [this]() {
        startPlayback();
    });

    connect(playPauseButton_, &QToolButton::clicked, this, &LatencyDetectorDialog::togglePlayback);
    connect(stopButton_, &QToolButton::clicked, this, [this]() {
        beginManualSeekInteraction();
        seekToSecond(0.0, true);
    });
    connect(detectBpmButton_, &QPushButton::clicked, this, [this]() {
        const double bpm = detectBpm();
        if (bpm > 0.0) {
            updateBpmEdit(bpm, true);
        }
    });
    connect(bpmHelpButton_, &QPushButton::clicked, this, &LatencyDetectorDialog::showBpmHelpDialog);
    connect(detectOffsetButton_, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const double bpm = parsedBpm(&ok);
        if (!ok || bpm <= 0.0) {
            return;
        }
        detectedOffsetRestoreSeconds_ = hasLastAppliedOffset_ ? lastAppliedOffsetSeconds_ : parsedOffset();
        hasDetectedOffsetRestore_ = true;
        if (restoreDetectedOffsetButton_ != nullptr) {
            restoreDetectedOffsetButton_->setEnabled(true);
        }
        const double offset = detectOffset(bpm);
        updateOffsetEdit(offset, true);
        restartPlaybackAfterOffsetChange();
    });
    connect(restoreDetectedOffsetButton_, &QPushButton::clicked, this, &LatencyDetectorDialog::restoreDetectedOffset);
    connect(offsetEdit_, &QLineEdit::textEdited, this, [this]() {
        previewOffsetEdit();
    });
    connect(offsetEdit_, &QLineEdit::editingFinished, this, [this]() {
        applyOffsetEdit();
    });
    connect(bpmEdit_, &QLineEdit::editingFinished, this, [this]() {
        applyBpmEdit();
    });
    connect(meterCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        updateBeatOverlay();
        if (meterCombo_ != nullptr) {
            emit meterIdChanged(meterCombo_->currentData().toString());
        }
    });
    connect(offsetSnapCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        updateBeatOverlay();
    });
    connect(waveformView_, &TimelineView::timelineUserInteractionStarted, this, &LatencyDetectorDialog::beginManualSeekInteraction);
    connect(waveformView_, &TimelineView::timelineDragStarted, this, &LatencyDetectorDialog::beginManualSeekInteraction);
    connect(zoomButton_, &QToolButton::clicked, this, [this]() {
        if (waveformView_ != nullptr && zoomPresetIndex_ + 1 < waveformView_->zoomPresetCount()) {
            ++zoomPresetIndex_;
            applyZoomLevel(true);
            return;
        }
        zoomPresetIndex_ = 0;
        applyZoomLevel(true);
    });
    connect(playbackSlider_, &QSlider::sliderPressed, this, [this]() {
        playbackSliderDragging_ = true;
        beginManualSeekInteraction();
    });
    connect(playbackSlider_, &QSlider::sliderReleased, this, [this]() {
        playbackSliderDragging_ = false;
        seekToSecond(static_cast<double>(playbackSlider_->value()) / 1000.0, true);
    });
    connect(playbackSlider_, &QSlider::sliderMoved, this, [this](int value) {
        seekToSecond(static_cast<double>(value) / 1000.0, true);
    });
    connect(sfxVolumeSlider_, &QSlider::valueChanged, this, [this](int value) {
        beatSfxVolume_ = qBound(0.0, static_cast<double>(value) * 0.04, 4.0);
        sfxVolumeValueLabel_->setText(QString("%1%").arg(value));
    });

    if (waveformView_ != nullptr) {
        zoomPresetIndex_ = qMin(zoomPresetIndex_, waveformView_->zoomPresetCount() - 1);
    }
    updateZoomButtonUi();
    updateOffsetEdit(0.0, false);
}

void LatencyDetectorDialog::loadAudioAnalysis()
{
    const DecodedAudio decoded = decodeMonoTrack(trackPath_, kAnalysisSampleRate);
    decodedSamples_ = decoded.samples;
    trackDurationSeconds_ = decoded.durationSeconds;
    onsetEnvelope_ = buildOnsetEnvelope(decodedSamples_, kAnalysisSampleRate, &onsetStepSeconds_);
    offsetEnvelope_ = buildTransientEnvelope(decodedSamples_, kAnalysisSampleRate, &offsetStepSeconds_);
    if (waveformView_ != nullptr) {
        waveformView_->setWaveformData(miacode::waveform::makeWaveformPlaceholder(trackDurationSeconds_));
        waveformView_->setPlayheadUpperLimitSeconds(trackDurationSeconds_);
    }
    if (waveformCacheService_ != nullptr && !trackPath_.isEmpty()) {
        const QString cacheDirectoryPath = miacode::waveform::waveformCacheDirectoryPath(
            miacode::waveform::projectDataDirectoryPathForFile(chartPath_));
        QPointer<LatencyDetectorDialog> guard(this);
        waveformCacheService_->requestWaveform(
            trackPath_,
            cacheDirectoryPath,
            [guard, trackPath = trackPath_](miacode::waveform::WaveformDataPtr waveformData) {
                if (guard.isNull() || guard->waveformView_ == nullptr) {
                    return;
                }
                const QFileInfo currentTrackInfo(trackPath);
                if (!currentTrackInfo.exists() || !currentTrackInfo.isFile()) {
                    return;
                }
                if (waveformData && waveformData->fileSize >= 0) {
                    const qint64 lastModifiedMs = currentTrackInfo.lastModified().toMSecsSinceEpoch();
                    if (currentTrackInfo.size() != waveformData->fileSize
                        || lastModifiedMs != waveformData->lastModifiedMs) {
                        return;
                    }
                }
                guard->waveformView_->setWaveformData(waveformData);
            });
    }
    detectBpmButton_->setEnabled(!onsetEnvelope_.isEmpty());
    applyZoomLevel(true);
}

void LatencyDetectorDialog::updatePlaybackUi()
{
    const QColor iconColor = UiTheme::colors().iconPrimary;
    playPauseButton_->setIcon(
        playing_
            ? UiTheme::dialogTransportPauseIcon(iconColor)
            : UiTheme::dialogTransportPlayIcon(iconColor));
    playPauseButton_->setToolTip(playing_
        ? localizedText("暂停", "Pause")
        : localizedText("播放", "Play"));
    playPauseButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet(playing_));
    stopButton_->setIcon(UiTheme::dialogTransportStopIcon(iconColor));
    stopButton_->setToolTip(localizedText("停止", "Stop"));
    if (stopButton_ != nullptr) {
        stopButton_->setEnabled(playing_ || playheadSecond_ > 0.001);
    }
    if (playbackSlider_ != nullptr) {
        const int maxMs = qMax(0, qRound(trackDurationSeconds_ * 1000.0));
        if (playbackSlider_->maximum() != maxMs) {
            playbackSlider_->setRange(0, maxMs);
        }
        if (!playbackSliderDragging_) {
            const bool blocked = playbackSlider_->blockSignals(true);
            playbackSlider_->setValue(qBound(0, qRound(playheadSecond_ * 1000.0), maxMs));
            playbackSlider_->blockSignals(blocked);
        }
    }
    playbackTimeLabel_->setText(QString("%1 / %2").arg(formatTimestamp(playheadSecond_), formatTimestamp(trackDurationSeconds_)));
    detectOffsetButton_->setEnabled(parsedBpm() > 0.0);
    updateZoomButtonUi();
}

void LatencyDetectorDialog::updateZoomButtonUi()
{
    if (zoomButton_ == nullptr || waveformView_ == nullptr) {
        return;
    }

    const int waveformZoomIndex = waveformView_->zoomPresetIndex();
    const double currentScale = waveformView_->zoomScale();
    const QVector<double>& zoomPresets = latencyTimelineZoomPresets();
    const int nextIndex = (waveformZoomIndex + 1 < waveformView_->zoomPresetCount()) ? (waveformZoomIndex + 1) : 0;
    const double nextScale = zoomPresets.value(nextIndex, currentScale);
    const QString sign = nextScale < currentScale ? QStringLiteral("-") : QStringLiteral("+");
    const QString zoomText = UiTheme::timelineZoomButtonText(currentScale);
    if (zoomPresetIndex_ == waveformZoomIndex
        && zoomButton_->text() == zoomText
        && zoomButton_->isEnabled() == (waveformView_->zoomPresetCount() > 0)) {
        return;
    }

    zoomPresetIndex_ = waveformZoomIndex;
    zoomButton_->setIcon(UiTheme::timelineZoomButtonIcon(UiTheme::colors().timelineLabel, sign));
    zoomButton_->setIconSize(QSize(18, 18));
    zoomButton_->setText(zoomText);
    zoomButton_->setToolTip(QStringLiteral("Timeline zoom: %1").arg(zoomText));
    zoomButton_->setEnabled(waveformView_->zoomPresetCount() > 0);
}

void LatencyDetectorDialog::beginManualSeekInteraction()
{
    if (offsetReplayTimer_ != nullptr) {
        offsetReplayTimer_->stop();
    }
    if (pausedSeekTimer_ != nullptr) {
        pausedSeekTimer_->stop();
    }
    if (!playing_) {
        return;
    }
    if (sfxRuntime_ != nullptr) {
        if (sfxRuntime_->isBackgroundTrackRunning()) {
            playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
        } else {
            playheadSecond_ = currentTransportSecond();
        }
    }
    pausePlayback();
}

void LatencyDetectorDialog::applyPlaybackRate(double rate)
{
    const QList<QPair<double, QString>>& speedOptions = latencyPlaybackSpeedOptions();
    if (speedOptions.isEmpty()) {
        return;
    }
    const int optionIndex = closestLatencyPlaybackSpeedIndex(rate);
    playbackRate_ = speedOptions.at(optionIndex).first;
    if (speedButton_ != nullptr) {
        speedButton_->setText(speedOptions.at(optionIndex).second);
        if (QMenu* speedMenu = speedButton_->menu(); speedMenu != nullptr) {
            const QList<QAction*> actions = speedMenu->actions();
            for (int index = 0; index < actions.size(); ++index) {
                actions.at(index)->setChecked(index == optionIndex);
            }
        }
    }
    if (sfxRuntime_ != nullptr) {
        sfxRuntime_->setBackgroundTrackPlaybackRate(playbackRate_);
    }
    if (playing_) {
        if (sfxRuntime_ != nullptr) {
            if (sfxRuntime_->isBackgroundTrackRunning()) {
                playheadSecond_ = qBound(0.0, sfxRuntime_->backgroundPlaybackSecond(), trackDurationSeconds_);
            } else {
                playheadSecond_ = currentTransportSecond();
            }
        }
        startPlayback();
    }
}

void LatencyDetectorDialog::stepPlaybackRate(int deltaSteps)
{
    const QList<QPair<double, QString>>& speedOptions = latencyPlaybackSpeedOptions();
    if (speedOptions.isEmpty() || deltaSteps == 0) {
        return;
    }
    const int currentIndex = closestLatencyPlaybackSpeedIndex(playbackRate_);
    const int nextIndex = qBound(0, currentIndex + deltaSteps, speedOptions.size() - 1);
    applyPlaybackRate(speedOptions.at(nextIndex).first);
}

void LatencyDetectorDialog::handleStopOrPlayShortcut()
{
    if (playing_) {
        beginManualSeekInteraction();
        seekToSecond(0.0, true);
        return;
    }
    togglePlayback();
}

void LatencyDetectorDialog::schedulePausedSeek(double second, bool centerView, bool immediate)
{
    beginManualSeekInteraction();
    playheadSecond_ = qBound(0.0, second, trackDurationSeconds_);
    transportAnchorSecond_ = playheadSecond_;
    transportElapsed_.invalidate();
    lastBeatAuditionSecond_ = playheadSecond_;
    updateVisibleRange(centerView);

    pendingPausedSeekSecond_ = playheadSecond_;
    pendingPausedSeekCenterView_ = centerView;
    pendingPausedSeekActive_ = true;
    if (pausedSeekTimer_ != nullptr) {
        if (immediate) {
            pausedSeekTimer_->stop();
            commitPendingPausedSeek();
        } else {
            pausedSeekTimer_->start();
        }
    } else if (immediate) {
        commitPendingPausedSeek();
    }
}

void LatencyDetectorDialog::commitPendingPausedSeek()
{
    if (!pendingPausedSeekActive_ || playing_) {
        return;
    }
    pendingPausedSeekActive_ = false;
    transportAnchorSecond_ = pendingPausedSeekSecond_;
    transportElapsed_.invalidate();
    if (sfxRuntime_ != nullptr) {
        sfxRuntime_->pauseBackgroundTrack();
        sfxRuntime_->seekBackgroundTrack(pendingPausedSeekSecond_);
        sfxRuntime_->pauseBackgroundTrack();
    }
}

void LatencyDetectorDialog::updateBeatOverlay()
{
    bool bpmOk = false;
    const double bpm = parsedBpm(&bpmOk);
    bool offsetOk = false;
    const double offset = parsedOffset(&offsetOk);
    const QString selectedMeterId = meterCombo_ != nullptr ? meterCombo_->currentData().toString() : QStringLiteral("4/4");
    const QString snapModeId = selectedOffsetSnapModeId();
    QString meterId = selectedMeterId;
    if (meterId == QLatin1String("auto")) {
        meterId = lastDetectedMeterId_;
    }
    const MeterPattern* pattern = meterPatternById(meterId);
    pendingBeatBpm_ = bpmOk ? bpm : 0.0;
    pendingBeatOffset_ = offsetOk ? offset : 0.0;
    pendingBeatBarPulseCount_ = pattern != nullptr ? qMax(1, pattern->accentWeights.size()) : 4;
    pendingBeatForceUniformGain_ = snapModeId != QLatin1String("bar");
    pendingBeatUseUniformAccent_ = pendingBeatForceUniformGain_ || selectedMeterId == QLatin1String("auto");
    pendingBeatAuditionPeriodSeconds_ = 0.0;
    pendingBeatAccentAnchorIndex_ = 0;
    pendingBeatAccentWeights_.clear();
    const double beatPeriod = pendingBeatBpm_ > 0.0 ? (60.0 / pendingBeatBpm_) : 0.0;
    if (beatPeriod > 0.0) {
        if (snapModeId == QLatin1String("eighth")) {
            pendingBeatAuditionPeriodSeconds_ = beatPeriod * 0.5;
        } else {
            pendingBeatAuditionPeriodSeconds_ = beatPeriod;
        }
    }
    if (pendingBeatUseUniformAccent_) {
        pendingBeatAccentWeights_.append(1.0);
    } else if (pattern != nullptr && !pattern->accentWeights.isEmpty()) {
        const double maxWeight = *std::max_element(pattern->accentWeights.constBegin(), pattern->accentWeights.constEnd());
        const double safeMaxWeight = maxWeight > 1e-6 ? maxWeight : 1.0;
        for (double weight : pattern->accentWeights) {
            pendingBeatAccentWeights_.append(qBound(0.0, weight / safeMaxWeight, 1.0));
        }
    } else {
        pendingBeatAccentWeights_.append(1.0);
    }
    if (!pendingBeatUseUniformAccent_
        && pendingBeatBpm_ > 0.0
        && !pendingBeatAccentWeights_.isEmpty()
        && detectedMeterPhaseValid_
        && meterId == lastDetectedMeterId_) {
        if (beatPeriod > 0.0) {
            const double anchorBeats = (detectedMeterPhaseSecond_ - pendingBeatOffset_) / beatPeriod;
            pendingBeatAccentAnchorIndex_ = qRound(anchorBeats);
        }
    }
    rebuildTimelineSnapshot();
    updatePlaybackUi();
}

void LatencyDetectorDialog::smoothFollowPlayhead(bool forceCenter)
{
    if (waveformView_ != nullptr) {
        waveformView_->setPlayheadSeconds(playheadSecond_, forceCenter);
    }
}

void LatencyDetectorDialog::applyZoomLevel(bool centerOnPlayhead)
{
    if (waveformView_ == nullptr) {
        return;
    }
    const int boundedIndex = qBound(0, zoomPresetIndex_, waveformView_->zoomPresetCount() - 1);
    const int deltaSteps = boundedIndex - waveformView_->zoomPresetIndex();
    zoomPresetIndex_ = boundedIndex;
    if (deltaSteps != 0) {
        waveformView_->stepZoomPresetForQuickSurface(deltaSteps, playheadSecond_);
    }
    updateVisibleRange(centerOnPlayhead);
}

void LatencyDetectorDialog::updateVisibleRange(bool centerOnPlayhead)
{
    if (waveformView_ != nullptr) {
        waveformView_->setPlayheadSeconds(playheadSecond_, centerOnPlayhead);
    }
    updatePlaybackUi();
}

void LatencyDetectorDialog::rebuildTimelineSnapshot()
{
    if (waveformView_ == nullptr) {
        return;
    }
    waveformView_->setTimelineData(buildLatencyTimelineSnapshot(
        trackDurationSeconds_,
        pendingBeatBpm_,
        pendingBeatOffset_,
        pendingBeatBarPulseCount_));
    waveformView_->setPlayheadUpperLimitSeconds(trackDurationSeconds_);
    waveformView_->setPlayheadSeconds(playheadSecond_, false);
}

void LatencyDetectorDialog::updateBpmEdit(double bpm, bool notify)
{
    const double normalized = qIsFinite(bpm) && bpm > 0.0 ? bpm : 0.0;
    if (normalized <= 0.0) {
        bpmEdit_->clear();
    } else {
        bpmEdit_->setText(QString::number(normalized, 'f', 3));
    }
    updateBeatOverlay();
    if (notify && normalized > 0.0) {
        emit bpmChanged(normalized);
    }
}

void LatencyDetectorDialog::updateOffsetEdit(double seconds, bool notify)
{
    (void)applyOffsetValue(seconds, notify, true);
}

bool LatencyDetectorDialog::applyOffsetValue(double seconds, bool notify, bool updateText)
{
    const double normalized = qIsFinite(seconds) ? seconds : 0.0;
    const bool changed = !hasLastAppliedOffset_ || qAbs(lastAppliedOffsetSeconds_ - normalized) > 0.0000001;
    if (updateText && offsetEdit_ != nullptr) {
        const QSignalBlocker blocker(offsetEdit_);
        offsetEdit_->setText(QString::number(normalized, 'f', 3));
    }
    updateBeatOverlay();
    lastAppliedOffsetSeconds_ = normalized;
    hasLastAppliedOffset_ = true;
    if (notify && changed) {
        emit offsetChanged(normalized);
    }
    return changed;
}

void LatencyDetectorDialog::previewOffsetEdit()
{
    bool ok = false;
    const double offset = parsedOffset(&ok);
    if (!ok) {
        return;
    }
    if (applyOffsetValue(offset, true, false)) {
        restartPlaybackAfterOffsetChange();
    }
}

void LatencyDetectorDialog::applyOffsetEdit()
{
    if (applyOffsetValue(parsedOffset(), true, true)) {
        restartPlaybackAfterOffsetChange();
    }
}

void LatencyDetectorDialog::restoreDetectedOffset()
{
    if (!hasDetectedOffsetRestore_) {
        return;
    }
    if (restoreDetectedOffsetButton_ != nullptr) {
        restoreDetectedOffsetButton_->setEnabled(false);
    }
    hasDetectedOffsetRestore_ = false;
    if (applyOffsetValue(detectedOffsetRestoreSeconds_, true, true)) {
        restartPlaybackAfterOffsetChange();
    }
}

void LatencyDetectorDialog::applyBpmEdit()
{
    bool ok = false;
    const double bpm = parsedBpm(&ok);
    if (!ok || bpm <= 0.0) {
        updateBpmEdit(0.0, false);
    } else {
        updateBpmEdit(bpm, true);
    }
}

void LatencyDetectorDialog::showBpmHelpDialog()
{
    QDialog dialog(UiDialogs::effectiveParentWidget(this));
    dialog.setWindowTitle(localizedText("BPM检测说明", "BPM Detection Notes"));
    dialog.resize(460, 210);
    UiDialogs::prepareDialogWindow(&dialog, this);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(6);

    auto* infoEdit = new QPlainTextEdit(&dialog);
    infoEdit->setReadOnly(true);
    infoEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    infoEdit->setMinimumHeight(120);

    QPair<double, double> alt1{0.0, 0.0};
    QPair<double, double> alt2{0.0, 0.0};
    int altCount = 0;
    for (const auto& entry : lastDetectedBpmCandidates_) {
        if (lastDetectedBpm_ > 0.0 && qAbs(entry.first - lastDetectedBpm_) <= 0.05) {
            continue;
        }
        if (altCount == 0) {
            alt1 = entry;
        } else if (altCount == 1) {
            alt2 = entry;
        }
        ++altCount;
        if (altCount >= 2) {
            break;
        }
    }

    const QString latest = lastDetectedBpm_ > 0.0
        ? QString::number(lastDetectedBpm_, 'f', 3)
        : QStringLiteral("--");
    const QString altBpm1 = alt1.first > 0.0 ? QString::number(alt1.first, 'f', 3) : QStringLiteral("--");
    const QString altBpm2 = alt2.first > 0.0 ? QString::number(alt2.first, 'f', 3) : QStringLiteral("--");
    const double altScore1 = alt1.first > 0.0 ? alt1.second : 0.0;
    const double altScore2 = alt2.first > 0.0 ? alt2.second : 0.0;

    QString text = localizedText(
        "在测试前请先设置拍号。\n"
        "如果拍号复杂或未知，请选择自动检测。\n"
        "因乐曲本身存在变速或音频瞬态不明显，可能出现误差。\n"
        "\n"
        "最近一次检测结果：" + latest + " BPM\n"
        + QString("备选1：%1 BPM\n").arg(altBpm1)
        + QString("备选2：%1 BPM\n").arg(altBpm2),
        "Set meter before testing. \n"
        "Choose Auto when meter is complex or unknown.\n"
        "BPM detection may be inaccurate due to tempo changes or weak transients.\n"
        "\n"
        "Latest result: " + latest + " BPM\n"
        + QString("Alternative 1: %1 BPM\n").arg(altBpm1)
        + QString("Alternative 2: %1 BPM\n").arg(altBpm2)
    );

    infoEdit->setPlainText(text);
    rootLayout->addWidget(infoEdit, 1);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);
    dialog.exec();
}
