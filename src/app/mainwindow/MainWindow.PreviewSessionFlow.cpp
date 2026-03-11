bool MainWindow::ensurePreviewSessionStarted()
{
    if (previewProcess_ != nullptr && previewProcess_->state() == QProcess::Running) {
        return true;
    }

    const QString scriptPath = resolvePreviewSessionScriptPath();
    if (scriptPath.isEmpty()) {
        appendOutput("preview/session-start", "script path not found");
        QMessageBox::warning(
            this,
            "Preview Session",
            "Preview session script is not configured.\n"
            "Set MIACODE_PREVIEW_SESSION_SCRIPT to enable legacy preview."
        );
        return false;
    }

    if (previewProcess_ != nullptr) {
        previewProcess_->deleteLater();
        previewProcess_ = nullptr;
    }

    previewProcess_ = new QProcess(this);
    previewProcess_->setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
    previewProcess_->setProcessEnvironment(pythonProcessEnvironment());
    previewProcess_->setProgram("python");
    previewProcess_->setArguments(QStringList{scriptPath});
    appendOutput(
        "preview/session-start",
        QString("program=python script=%1 cwd=%2")
            .arg(scriptPath, previewProcess_->workingDirectory())
    );
#ifdef Q_OS_WIN
    previewProcess_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    previewStdoutBuffer_.clear();
    previewStderrBuffer_.clear();
    connect(previewProcess_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
        onPreviewProcessFinished(exitCode);
    });
    connect(previewProcess_, &QProcess::readyReadStandardOutput, this, [this]() {
        if (previewProcess_ == nullptr) {
            return;
        }
        previewStdoutBuffer_ += decodeProcessText(previewProcess_->readAllStandardOutput());
        int lineBreak = previewStdoutBuffer_.indexOf('\n');
        while (lineBreak >= 0) {
            const QString line = previewStdoutBuffer_.left(lineBreak).trimmed();
            previewStdoutBuffer_.remove(0, lineBreak + 1);
            if (!line.isEmpty()) {
                if (line.contains("[session] ready")) {
                    previewArrangeRetryCount_ = 0;
                    schedulePreviewArrange(40);
                    sendPreviewConfigCommand();
                }
                if (!handlePreviewSessionLine(line)) {
                    appendOutput("preview/session", line);
                }
            }
            lineBreak = previewStdoutBuffer_.indexOf('\n');
        }
    });
    connect(previewProcess_, &QProcess::readyReadStandardError, this, [this]() {
        if (previewProcess_ == nullptr) {
            return;
        }
        previewStderrBuffer_ += decodeProcessText(previewProcess_->readAllStandardError());
        int lineBreak = previewStderrBuffer_.indexOf('\n');
        while (lineBreak >= 0) {
            const QString line = previewStderrBuffer_.left(lineBreak).trimmed();
            previewStderrBuffer_.remove(0, lineBreak + 1);
            if (!line.isEmpty()) {
                appendOutput("preview/session-stderr", line);
            }
            lineBreak = previewStderrBuffer_.indexOf('\n');
        }
    });
    connect(previewProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        appendOutput(
            "preview/session-error",
            QString("process error code=%1 message=%2")
                .arg(static_cast<int>(error))
                .arg(previewProcess_ != nullptr ? previewProcess_->errorString() : QString("n/a"))
        );
    });

    previewProcess_->start();
    if (!previewProcess_->waitForStarted(3000)) {
        appendOutput(
            "preview/session-start-failed",
            QString("failed to start python preview session. error=%1")
                .arg(previewProcess_->errorString())
        );
        previewProcess_->deleteLater();
        previewProcess_ = nullptr;
        return false;
    }

    appendOutput("preview/session-start", QString("started pid=%1").arg(previewProcess_->processId()));
    statusBar()->showMessage("Preview session started.");
    return true;
}

void MainWindow::stopPreviewSession()
{
    if (previewProcess_ == nullptr) {
        return;
    }
    QProcess* process = previewProcess_;
    previewProcess_ = nullptr;
    disconnect(process, nullptr, this, nullptr);
    if (process->state() == QProcess::Running) {
        const QJsonObject quitCmd{{"cmd", "quit"}};
        QByteArray payload = QJsonDocument(quitCmd).toJson(QJsonDocument::Compact);
        payload.append('\n');
        process->write(payload);
        process->waitForBytesWritten(500);
        process->waitForFinished(1000);
    }
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        process->waitForFinished(1000);
    }
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
}

bool MainWindow::sendPreviewCommand(const QString& mode, int cursorLine, int cursorCol, const QString& trackPath)
{
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        return false;
    }
    previewAudioSettings_.normalize();
    QJsonObject cmd{
        {"cmd", "preview"},
        {"mode", mode},
        {"track", trackPath},
        {"chart", activeChartText()},
        {"chart_name", document_.title.trimmed().isEmpty()
                ? (currentFilePath_.isEmpty() ? QString("Untitled") : QFileInfo(currentFilePath_).fileName())
                : document_.title},
        {"chart_path", currentFilePath_},
        {"volume", previewAudioSettings_.bgmVolume},
        {"bgm_volume", previewAudioSettings_.bgmVolume},
        {"sfx_volume", qMax(qMax(qMax(qMax(qMax(previewAudioSettings_.answerVolume, previewAudioSettings_.slideVolume), previewAudioSettings_.breakVolume), previewAudioSettings_.exVolume), previewAudioSettings_.touchVolume), previewAudioSettings_.touchholdVolume)},
        {"answer_volume", previewAudioSettings_.answerVolume},
        {"slide_volume", previewAudioSettings_.slideVolume},
        {"break_volume", previewAudioSettings_.breakVolume},
        {"ex_volume", previewAudioSettings_.exVolume},
        {"touch_volume", previewAudioSettings_.touchVolume},
        {"touchhold_volume", previewAudioSettings_.touchholdVolume},
        {"show_slide_tracks", true},
        {"show_judge_markers", showJudgeMarkers_},
        {"show_touch_trail", showTouchTrail_},
        {"render_profile", "studio"},
    };
    const QString skinDir = resolvePreviewSkinDir();
    if (!skinDir.isEmpty()) {
        cmd.insert("skin_dir", skinDir);
    }
    if (mode == "cursor") {
        cmd.insert("cursor_line", cursorLine);
        cmd.insert("cursor_col", cursorCol);
    }

    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send preview command to session");
        return false;
    }
    previewProcess_->waitForBytesWritten(1000);
    return true;
}

bool MainWindow::sendPreviewPrepareCommand()
{
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        return false;
    }
    if (currentFilePath_.isEmpty()) {
        return false;
    }
    QJsonObject cmd{
        {"cmd", "prepare"},
        {"chart_path", currentFilePath_},
        {"render_profile", "studio"},
        {"background_brightness", previewBackgroundBrightness_},
    };
    const QString skinDir = resolvePreviewSkinDir();
    if (!skinDir.isEmpty()) {
        cmd.insert("skin_dir", skinDir);
    }
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send preview prepare command");
        return false;
    }
    previewProcess_->waitForBytesWritten(1000);
    return true;
}

bool MainWindow::sendPreviewConfigCommand(const QString& audition)
{
    if (previewProcess_ == nullptr || previewProcess_->state() != QProcess::Running) {
        return false;
    }
    previewAudioSettings_.normalize();
    QJsonObject cmd{
        {"cmd", "config"},
        {"bgm_volume", previewAudioSettings_.bgmVolume},
        {"sfx_volume", qMax(qMax(qMax(qMax(qMax(previewAudioSettings_.answerVolume, previewAudioSettings_.slideVolume), previewAudioSettings_.breakVolume), previewAudioSettings_.exVolume), previewAudioSettings_.touchVolume), previewAudioSettings_.touchholdVolume)},
        {"answer_volume", previewAudioSettings_.answerVolume},
        {"slide_volume", previewAudioSettings_.slideVolume},
        {"break_volume", previewAudioSettings_.breakVolume},
        {"ex_volume", previewAudioSettings_.exVolume},
        {"touch_volume", previewAudioSettings_.touchVolume},
        {"touchhold_volume", previewAudioSettings_.touchholdVolume},
        {"show_slide_tracks", true},
        {"show_judge_markers", showJudgeMarkers_},
        {"show_touch_trail", showTouchTrail_},
        {"background_brightness", previewBackgroundBrightness_},
    };
    if (!audition.isEmpty()) {
        cmd.insert("audition", audition);
    }
    QByteArray payload = QJsonDocument(cmd).toJson(QJsonDocument::Compact);
    payload.append('\n');
    if (previewProcess_->write(payload) < 0) {
        appendOutput("preview/session-write-failed", "failed to send preview config command");
        return false;
    }
    previewProcess_->waitForBytesWritten(1000);
    return true;
}

bool MainWindow::handlePreviewSessionLine(const QString& line)
{
    QString jsonText = line.trimmed();
    const int jsonStart = jsonText.indexOf('{');
    const int jsonEnd = jsonText.lastIndexOf('}');
    if (jsonStart >= 0 && jsonEnd > jsonStart) {
        jsonText = jsonText.mid(jsonStart, jsonEnd - jsonStart + 1);
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject payload = doc.object();
            if (payload.value("event").toString() == "playhead_limit") {
                if (timelineView_ != nullptr) {
                    const QJsonValue secondValue = payload.value("second");
                    if (secondValue.isDouble()) {
                        timelineView_->setPlayheadUpperLimitSeconds(secondValue.toDouble());
                    } else {
                        timelineView_->setPlayheadUpperLimitSeconds(-1.0);
                    }
                }
                return true;
            }
        }
    }

    double second = 0.0;
    const PreviewIntegration::PlayheadParseResult parsed = PreviewIntegration::parsePlayheadEvent(line, &second);
    if (parsed == PreviewIntegration::PlayheadParseResult::NotPlayheadEvent) {
        return false;
    }
    if (parsed == PreviewIntegration::PlayheadParseResult::Parsed && timelineView_ != nullptr) {
        timelineView_->setPlayheadSeconds(second, true);
    }
    if (parsed == PreviewIntegration::PlayheadParseResult::Parsed && previewCanvas_ != nullptr) {
        previewCanvas_->setPlayheadSeconds(second);
    }
    if (parsed == PreviewIntegration::PlayheadParseResult::Parsed && previewMediaController_ != nullptr) {
        previewMediaController_->setPlayheadSeconds(second);
    }
    return true;
}

void MainWindow::startPreviewProcess(const QString& mode, int cursorLine, int cursorCol)
{
    if (!runValidateSimai()) {
        appendOutput("preview/blocked", "validation failed; preview canceled");
        statusBar()->showMessage("Preview canceled: fix validation errors first.");
        return;
    }

    if (timelineView_ != nullptr) {
        timelineView_->setPlayheadUpperLimitSeconds(-1.0);
    }
    QString trackPath = resolveDefaultTrackPath();
    if (trackPath.isEmpty()) {
        logTopLevelWindowSnapshot("preview_track_dialog/begin");
        logWindowGeometryDebug("preview_track_before_dialog");
        logNativeWindowDebug("preview_track_before_dialog");
        int sampleCount = 0;
        int restoreCount = 0;
        QTimer sampleTimer;
        sampleTimer.setInterval(120);
        connect(&sampleTimer, &QTimer::timeout, this, [this, &sampleCount, &restoreCount]() {
            if (sampleCount >= 80) {
                if (sampleCount == 80 && runtimeDebugOutputEnabled_) {
                    appendOutput("window/dialog_watch", "preview_track_dialog sample_limit_reached");
                }
                ++sampleCount;
                return;
            }
            ++sampleCount;
#ifdef Q_OS_WIN
            QString restoreDetail;
            if (tryRestoreOwnedNativeFileDialog(reinterpret_cast<HWND>(winId()), &restoreDetail)) {
                ++restoreCount;
                appendOutput(
                    "window/dialog_watch",
                    QString("preview_track_dialog sample=%1 %2").arg(sampleCount).arg(restoreDetail)
                );
            }
#endif
            if (runtimeDebugOutputEnabled_ && (sampleCount <= 12 || (sampleCount % 5) == 0)) {
                logWindowGeometryDebug("preview_track_dialog_poll");
                logNativeWindowDebug(QString("preview_track_dialog_poll sample=%1").arg(sampleCount));
            }
        });

        logTopLevelWindowSnapshot("preview_track_dialog_exec_begin");
        logNativeWindowDebug("preview_track_dialog_exec_begin");
        sampleTimer.start();
        trackPath = QFileDialog::getOpenFileName(
            this,
            "Select Preview Track",
            QString(),
            "Audio (*.mp3 *.wav *.ogg);;All Files (*.*)",
            nullptr
        );
        sampleTimer.stop();
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "window/dialog_watch",
                QString("preview_track_dialog finished selected_empty=%1 samples=%2 restores=%3")
                    .arg(trackPath.isEmpty() ? 1 : 0)
                    .arg(qMin(sampleCount, 80))
                    .arg(restoreCount)
            );
        }
        logTopLevelWindowSnapshot("preview_track_dialog_exec_end");
        logNativeWindowDebug("preview_track_dialog_exec_end");
        logWindowGeometryDebug("preview_track_after_dialog", QString("selected_empty=%1").arg(trackPath.isEmpty() ? 1 : 0));
        logTopLevelWindowSnapshot("preview_track_dialog/after_dialog");
        logNativeWindowDebug("preview_track_after_dialog");
    }
    if (trackPath.isEmpty()) {
        return;
    }
    setLastOpenDirectory(trackPath);
    lastTrackPath_ = trackPath;

    if (!ensurePreviewSessionStarted()) {
        return;
    }
    if (!sendPreviewCommand(mode, cursorLine, cursorCol, trackPath)) {
        appendOutput("preview/session", "retrying by restarting session process");
        stopPreviewSession();
        if (!ensurePreviewSessionStarted()) {
            return;
        }
        if (!sendPreviewCommand(mode, cursorLine, cursorCol, trackPath)) {
            appendOutput("preview/session", "failed to send preview command after restart");
            return;
        }
    }

    statusBar()->showMessage(
        QString("Preview(%1) sent to resident session: %2").arg(mode).arg(QFileInfo(trackPath).fileName())
    );
    schedulePreviewArrange(80);
}

void MainWindow::onPreviewProcessFinished(int exitCode)
{
    if (previewProcess_ == nullptr) {
        return;
    }
    const QString restOut = decodeProcessText(previewProcess_->readAllStandardOutput()).trimmed();
    const QString restErr = decodeProcessText(previewProcess_->readAllStandardError()).trimmed();
    if (!restOut.isEmpty()) {
        appendOutput("preview/session", restOut);
    }
    if (!restErr.isEmpty()) {
        appendOutput("preview/session-stderr", restErr);
    }
    appendOutput("preview/session-exit", QString("exit_code=%1").arg(exitCode));
    statusBar()->showMessage("Preview session exited.");
    previewProcess_->deleteLater();
    previewProcess_ = nullptr;
}

void MainWindow::appendOutput(const QString& title, const QString& payload)
{
    if (!runtimeDebugOutputEnabled_) {
        return;
    }
    QFile logFile(runtimeDebugLogPath());
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        out << timestampLine(title) << "\n";
        out << payload << "\n\n";
    }
    if (outputView_ == nullptr) {
        return;
    }
    outputView_->appendPlainText(timestampLine(title));
    outputView_->appendPlainText(payload);
    outputView_->appendPlainText(QString());
}

void MainWindow::onErrorItemActivated(QListWidgetItem* item)
{
    if (item == nullptr) {
        return;
    }
    const int line = item->data(Qt::UserRole).toInt();
    const int col = item->data(Qt::UserRole + 1).toInt();
    jumpToLocation(line, col);
}

bool MainWindow::runValidateSimai()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return false;
    }
    clearValidationErrors();
    clearValidationDecorations();

    const QString chartText = activeChartText();
    if (chartText.trimmed().isEmpty()) {
        addValidationError(1, 1, "Chart is empty.");
        addValidationDecoration(1, 1, "Chart is empty.");
        if (bottomTabs_ != nullptr && errorList_ != nullptr) {
            const int errorTabIndex = bottomTabs_->indexOf(errorList_);
            if (errorTabIndex >= 0) {
                bottomTabs_->setCurrentIndex(errorTabIndex);
            }
        }
        statusBar()->showMessage("Validate Simai failed: chart is empty.");
        return false;
    }

    const SimaiNativeParseResult nativeResult = SimaiNativeParser::validateSyntax(chartText);
    QString payload;
    payload += QString("note_count=%1\nsyntax_error_count=%2")
        .arg(nativeResult.noteMarkers.size())
        .arg(nativeResult.errors.size());
    appendOutput("validate", payload);

    for (const SimaiNativeMessage& err : nativeResult.errors) {
        addValidationError(err.line, err.col, err.message);
        addValidationDecoration(err.line, err.col, err.message);
    }
    if (!nativeResult.errors.isEmpty()) {
        if (bottomTabs_ != nullptr && errorList_ != nullptr) {
            const int errorTabIndex = bottomTabs_->indexOf(errorList_);
            if (errorTabIndex >= 0) {
                bottomTabs_->setCurrentIndex(errorTabIndex);
            }
        }
        onErrorItemActivated(errorList_->item(0));
    }

    if (nativeResult.errors.isEmpty()) {
        statusBar()->showMessage("Validate Simai passed.");
        return true;
    } else {
        statusBar()->showMessage(QString("Validate Simai failed: %1 syntax error(s).").arg(nativeResult.errors.size()));
    }
    return false;
}

bool MainWindow::saveBeforePreviewStart()
{
    if (documentDirty_ || currentFieldDirty_) {
        return onSaveFile();
    }
    return maybeSaveCurrentFieldChanges();
}

void MainWindow::onValidateSimai()
{
    (void)runValidateSimai();
}
