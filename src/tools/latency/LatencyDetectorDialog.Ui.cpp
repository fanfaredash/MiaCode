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
    timingRow->addWidget(meterLabel);
    timingRow->addWidget(meterCombo_);
    timingRow->addSpacing(10);
    timingRow->addWidget(snapLabel);
    timingRow->addWidget(offsetSnapCombo_);
    timingRow->addStretch(1);
    rootLayout->addLayout(timingRow);

    waveformView_ = new WaveformOverviewWidget(this);
    static_cast<WaveformOverviewWidget*>(waveformView_)->setSeekCallback([this](double second) {
        seekToSecond(second, true);
    });
    rootLayout->addWidget(waveformView_, 1);
    playbackSlider_ = new QSlider(Qt::Horizontal, this);
    playbackSlider_->setRange(0, 0);
    playbackSlider_->setSingleStep(10);
    playbackSlider_->setPageStep(300);
    rootLayout->addWidget(playbackSlider_);

    auto* controlsRow = new QHBoxLayout();
    controlsRow->setSpacing(8);
    playPauseButton_ = new QPushButton(this);
    stopButton_ = new QPushButton(localizedText("停止", "Stop"), this);
    speedButton_ = new QToolButton(this);
    speedButton_->setPopupMode(QToolButton::InstantPopup);
    speedButton_->setText("1x");
    auto* speedMenu = new QMenu(speedButton_);
    const QList<QPair<double, QString>> speedOptions{
        {0.25, "0.25x"},
        {0.50, "0.5x"},
        {0.75, "0.75x"},
        {1.00, "1x"},
        {1.25, "1.25x"},
        {1.50, "1.5x"},
        {2.00, "2x"},
    };
    for (const auto& speedOption : speedOptions) {
        QAction* action = speedMenu->addAction(speedOption.second);
        action->setCheckable(true);
        action->setChecked(qFuzzyCompare(speedOption.first, 1.0));
        connect(action, &QAction::triggered, this, [this, speedMenu, speed = speedOption.first, label = speedOption.second]() {
            for (QAction* entry : speedMenu->actions()) {
                entry->setChecked(false);
            }
            if (QAction* action = qobject_cast<QAction*>(sender()); action != nullptr) {
                action->setChecked(true);
            }
            playbackRate_ = speed;
            speedButton_->setText(label);
            if (sfxRuntime_ != nullptr) {
                sfxRuntime_->setBackgroundTrackPlaybackRate(playbackRate_);
            }
            if (playing_) {
                startPlayback();
            }
        });
    }
    speedButton_->setMenu(speedMenu);

    zoomOutButton_ = new QToolButton(this);
    zoomOutButton_->setText("-");
    zoomInButton_ = new QToolButton(this);
    zoomInButton_->setText("+");
    sfxVolumeSlider_ = new QSlider(Qt::Horizontal, this);
    sfxVolumeSlider_->setRange(0, 100);
    sfxVolumeSlider_->setValue(25);
    sfxVolumeSlider_->setFixedWidth(110);
    sfxVolumeValueLabel_ = new QLabel("25%", this);
    sfxVolumeValueLabel_->setMinimumWidth(42);
    playbackTimeLabel_ = new QLabel(this);
    playbackTimeLabel_->setMinimumWidth(170);
    playbackTimeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    controlsRow->addWidget(playPauseButton_);
    controlsRow->addWidget(stopButton_);
    controlsRow->addWidget(speedButton_);
    controlsRow->addSpacing(10);
    controlsRow->addWidget(new QLabel(localizedText("缩放", "Zoom"), this));
    controlsRow->addWidget(zoomOutButton_);
    controlsRow->addWidget(zoomInButton_);
    controlsRow->addSpacing(10);
    controlsRow->addWidget(new QLabel(localizedText("效果音", "SFX"), this));
    controlsRow->addWidget(sfxVolumeSlider_);
    controlsRow->addWidget(sfxVolumeValueLabel_);
    controlsRow->addStretch(1);
    controlsRow->addWidget(playbackTimeLabel_);
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

    offsetReplayTimer_ = new QTimer(this);
    offsetReplayTimer_->setSingleShot(true);
    connect(offsetReplayTimer_, &QTimer::timeout, this, [this]() {
        startPlayback();
    });

    connect(playPauseButton_, &QPushButton::clicked, this, &LatencyDetectorDialog::togglePlayback);
    connect(stopButton_, &QPushButton::clicked, this, [this]() {
        pausePlayback();
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
    connect(zoomOutButton_, &QToolButton::clicked, this, [this]() {
        if (zoomLevel_ < 1) {
            ++zoomLevel_;
            applyZoomLevel(true);
        }
    });
    connect(zoomInButton_, &QToolButton::clicked, this, [this]() {
        if (zoomLevel_ > -1) {
            --zoomLevel_;
            applyZoomLevel(true);
        }
    });
    connect(playbackSlider_, &QSlider::sliderPressed, this, [this]() {
        playbackSliderDragging_ = true;
    });
    connect(playbackSlider_, &QSlider::sliderReleased, this, [this]() {
        playbackSliderDragging_ = false;
        seekToSecond(static_cast<double>(playbackSlider_->value()) / 1000.0, false);
    });
    connect(playbackSlider_, &QSlider::sliderMoved, this, [this](int value) {
        seekToSecond(static_cast<double>(value) / 1000.0, false);
    });
    connect(sfxVolumeSlider_, &QSlider::valueChanged, this, [this](int value) {
        beatSfxVolume_ = qBound(0.0, static_cast<double>(value) * 0.04, 4.0);
        sfxVolumeValueLabel_->setText(QString("%1%").arg(value));
    });

    updateOffsetEdit(0.0, false);
}

void LatencyDetectorDialog::loadAudioAnalysis()
{
    const DecodedAudio decoded = decodeMonoTrack(trackPath_, kAnalysisSampleRate);
    decodedSamples_ = decoded.samples;
    trackDurationSeconds_ = decoded.durationSeconds;
    waveformPeaks_ = buildWaveformPeaks(decodedSamples_, kWaveformPeakCount);
    onsetEnvelope_ = buildOnsetEnvelope(decodedSamples_, kAnalysisSampleRate, &onsetStepSeconds_);
    offsetEnvelope_ = buildTransientEnvelope(decodedSamples_, kAnalysisSampleRate, &offsetStepSeconds_);
    static_cast<WaveformOverviewWidget*>(waveformView_)->setWaveformData(waveformPeaks_, trackDurationSeconds_);
    detectBpmButton_->setEnabled(!onsetEnvelope_.isEmpty());
    visibleDurationSeconds_ = qMin(qMax(trackDurationSeconds_, kMinimumVisibleSeconds), 12.0);
    baseVisibleDurationSeconds_ = visibleDurationSeconds_;
    zoomLevel_ = 0;
}

void LatencyDetectorDialog::updatePlaybackUi()
{
    playPauseButton_->setText(playing_
        ? localizedText("暂停", "Pause")
        : localizedText("播放", "Play"));
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
    zoomOutButton_->setEnabled(zoomLevel_ < 1);
    zoomInButton_->setEnabled(zoomLevel_ > -1);
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
    if (pendingBeatBpm_ > 0.0) {
        static_cast<WaveformOverviewWidget*>(waveformView_)->setBeatGrid(
            pendingBeatBpm_,
            pendingBeatOffset_,
            pendingBeatBarPulseCount_
        );
    } else {
        static_cast<WaveformOverviewWidget*>(waveformView_)->clearBeatGrid();
    }
    updatePlaybackUi();
}

void LatencyDetectorDialog::smoothFollowPlayhead(bool forceCenter)
{
    if (trackDurationSeconds_ <= 0.0) {
        visibleStartSecond_ = 0.0;
        return;
    }
    const double maxStart = qMax(0.0, trackDurationSeconds_ - visibleDurationSeconds_);
    const double targetStart = qBound(0.0, playheadSecond_ - visibleDurationSeconds_ * 0.35, maxStart);
    if (forceCenter) {
        visibleStartSecond_ = targetStart;
        return;
    }

    const double leftBoundary = visibleStartSecond_ + visibleDurationSeconds_ * 0.15;
    const double rightBoundary = visibleStartSecond_ + visibleDurationSeconds_ * 0.85;
    if (playheadSecond_ < leftBoundary || playheadSecond_ > rightBoundary) {
        const double center = visibleStartSecond_ + visibleDurationSeconds_ * 0.5;
        const double normalizedDistance = qBound(
            0.0,
            qAbs(playheadSecond_ - center) / qMax(0.001, visibleDurationSeconds_ * 0.5),
            1.0
        );
        const double blend = 0.18 + 0.42 * normalizedDistance;
        visibleStartSecond_ = visibleStartSecond_ + (targetStart - visibleStartSecond_) * blend;
    }
    visibleStartSecond_ = qBound(0.0, visibleStartSecond_, maxStart);
}

void LatencyDetectorDialog::applyZoomLevel(bool centerOnPlayhead)
{
    const double base = qMax(kMinimumVisibleSeconds, baseVisibleDurationSeconds_);
    double factor = 1.0;
    if (zoomLevel_ > 0) {
        factor = 1.5;
    } else if (zoomLevel_ < 0) {
        factor = 1.0 / 1.5;
    }
    visibleDurationSeconds_ = base * factor;
    updateVisibleRange(centerOnPlayhead);
}

void LatencyDetectorDialog::updateVisibleRange(bool centerOnPlayhead)
{
    if (trackDurationSeconds_ <= 0.0) {
        visibleStartSecond_ = 0.0;
        visibleDurationSeconds_ = kMinimumVisibleSeconds;
    } else {
        visibleDurationSeconds_ = qBound(kMinimumVisibleSeconds, visibleDurationSeconds_, qMax(trackDurationSeconds_, kMinimumVisibleSeconds));
        if (centerOnPlayhead) {
            visibleStartSecond_ = playheadSecond_ - visibleDurationSeconds_ * 0.35;
        }
        const double maxStart = qMax(0.0, trackDurationSeconds_ - visibleDurationSeconds_);
        visibleStartSecond_ = qBound(0.0, visibleStartSecond_, maxStart);
    }
    static_cast<WaveformOverviewWidget*>(waveformView_)->setVisibleRange(visibleStartSecond_, visibleDurationSeconds_);
    static_cast<WaveformOverviewWidget*>(waveformView_)->setPlayheadSecond(playheadSecond_);
    updatePlaybackUi();
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
