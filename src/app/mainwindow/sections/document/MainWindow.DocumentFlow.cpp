namespace {

enum class UnsavedChangesChoice {
    Save,
    Discard,
    Cancel,
};

UnsavedChangesChoice showUnsavedChangesDialog(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox dialog(QMessageBox::Warning, title, text, QMessageBox::NoButton, parent);
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    QPushButton* saveButton = dialog.addButton(uiText("action.save", "Save"), QMessageBox::AcceptRole);
    QPushButton* discardButton = dialog.addButton(uiText("action.discard", "Discard"), QMessageBox::DestructiveRole);
    QPushButton* cancelButton = dialog.addButton(uiText("action.cancel", "Cancel"), QMessageBox::RejectRole);
    dialog.setDefaultButton(saveButton);
    dialog.setEscapeButton(cancelButton);
    UiDialogs::localizeMessageBox(&dialog);
    centerDialogOnAnchor(&dialog, parent);
    dialog.exec();
    if (dialog.clickedButton() == saveButton) {
        return UnsavedChangesChoice::Save;
    }
    if (dialog.clickedButton() == discardButton) {
        return UnsavedChangesChoice::Discard;
    }
    return UnsavedChangesChoice::Cancel;
}

#ifdef Q_OS_WIN
QString nativeDialogWinEventName(DWORD event)
{
    switch (event) {
    case EVENT_SYSTEM_FOREGROUND:
        return QStringLiteral("EVENT_SYSTEM_FOREGROUND");
    case EVENT_SYSTEM_DIALOGSTART:
        return QStringLiteral("EVENT_SYSTEM_DIALOGSTART");
    case EVENT_SYSTEM_DIALOGEND:
        return QStringLiteral("EVENT_SYSTEM_DIALOGEND");
    case EVENT_SYSTEM_CAPTURESTART:
        return QStringLiteral("EVENT_SYSTEM_CAPTURESTART");
    case EVENT_SYSTEM_CAPTUREEND:
        return QStringLiteral("EVENT_SYSTEM_CAPTUREEND");
    case EVENT_OBJECT_CREATE:
        return QStringLiteral("EVENT_OBJECT_CREATE");
    case EVENT_OBJECT_DESTROY:
        return QStringLiteral("EVENT_OBJECT_DESTROY");
    case EVENT_OBJECT_SHOW:
        return QStringLiteral("EVENT_OBJECT_SHOW");
    case EVENT_OBJECT_HIDE:
        return QStringLiteral("EVENT_OBJECT_HIDE");
    case EVENT_OBJECT_FOCUS:
        return QStringLiteral("EVENT_OBJECT_FOCUS");
    case EVENT_OBJECT_LOCATIONCHANGE:
        return QStringLiteral("EVENT_OBJECT_LOCATIONCHANGE");
    case EVENT_OBJECT_NAMECHANGE:
        return QStringLiteral("EVENT_OBJECT_NAMECHANGE");
    default:
        return QStringLiteral("EVENT(0x%1)").arg(static_cast<qulonglong>(event), 0, 16);
    }
}

QString nativeDialogObjectIdName(LONG objectId)
{
    switch (objectId) {
    case OBJID_WINDOW:
        return QStringLiteral("OBJID_WINDOW");
    case OBJID_CLIENT:
        return QStringLiteral("OBJID_CLIENT");
    case OBJID_TITLEBAR:
        return QStringLiteral("OBJID_TITLEBAR");
    case OBJID_SYSMENU:
        return QStringLiteral("OBJID_SYSMENU");
    case OBJID_MENU:
        return QStringLiteral("OBJID_MENU");
    case OBJID_CARET:
        return QStringLiteral("OBJID_CARET");
    case OBJID_CURSOR:
        return QStringLiteral("OBJID_CURSOR");
    default:
        return QStringLiteral("OBJID(%1)").arg(objectId);
    }
}

struct NativeDialogWinEventTraceState {
    QString scope;
    HWND ownerHwnd = nullptr;
    DWORD ownerPid = 0;
};

NativeDialogWinEventTraceState* g_nativeDialogWinEventTraceState = nullptr;

bool shouldTraceNativeDialogEvent(
    const NativeDialogWinEventTraceState* state,
    HWND hwnd,
    LONG objectId
)
{
    if (state == nullptr) {
        return false;
    }

    if (objectId != OBJID_WINDOW
        && objectId != OBJID_CLIENT
        && objectId != OBJID_CARET
        && objectId != OBJID_CURSOR
        && objectId != OBJID_SYSMENU
        && objectId != 0) {
        return false;
    }

    if (hwnd == nullptr) {
        return true;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    const HWND owner = GetWindow(hwnd, GW_OWNER);
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    const HWND rootOwner = GetAncestor(hwnd, GA_ROOTOWNER);
    const HWND foreground = GetForegroundWindow();

    return hwnd == state->ownerHwnd
        || owner == state->ownerHwnd
        || root == state->ownerHwnd
        || rootOwner == state->ownerHwnd
        || hwnd == foreground
        || rootOwner == foreground
        || (state->ownerPid != 0 && pid == state->ownerPid);
}

void appendNativeDialogHookLog(
    const QString& scope,
    const QString& payload
)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("window/native_hook"),
        QStringLiteral("scope=%1 %2").arg(scope, payload)
    );
}

void CALLBACK nativeDialogWinEventProc(
    HWINEVENTHOOK hook,
    DWORD event,
    HWND hwnd,
    LONG idObject,
    LONG idChild,
    DWORD eventThread,
    DWORD eventTime
)
{
    Q_UNUSED(hook);

    const NativeDialogWinEventTraceState* state = g_nativeDialogWinEventTraceState;
    if (!shouldTraceNativeDialogEvent(state, hwnd, idObject)) {
        return;
    }

    appendNativeDialogHookLog(
        state != nullptr ? state->scope : QStringLiteral("unknown"),
        QString("event=%1 object=%2 child=%3 event_thread=%4 event_time=%5 hwnd={%6}")
            .arg(nativeDialogWinEventName(event))
            .arg(nativeDialogObjectIdName(idObject))
            .arg(idChild)
            .arg(eventThread)
            .arg(eventTime)
            .arg(describeNativeWindowHandle(hwnd))
    );
}

class ScopedNativeDialogWinEventTrace {
public:
    ScopedNativeDialogWinEventTrace(const QString& scope, HWND ownerHwnd)
    {
        state_.scope = scope;
        state_.ownerHwnd = ownerHwnd;
        if (ownerHwnd != nullptr) {
            GetWindowThreadProcessId(ownerHwnd, &state_.ownerPid);
        }

        g_nativeDialogWinEventTraceState = &state_;
        appendNativeDialogHookLog(
            state_.scope,
            QString("trace_start owner={%1} owner_pid=%2")
                .arg(describeNativeWindowHandle(ownerHwnd))
                .arg(state_.ownerPid)
        );

        hooks_.append(SetWinEventHook(
            EVENT_SYSTEM_FOREGROUND,
            EVENT_SYSTEM_DIALOGEND,
            nullptr,
            &nativeDialogWinEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT
        ));
        hooks_.append(SetWinEventHook(
            EVENT_OBJECT_CREATE,
            EVENT_OBJECT_HIDE,
            nullptr,
            &nativeDialogWinEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT
        ));
        hooks_.append(SetWinEventHook(
            EVENT_OBJECT_FOCUS,
            EVENT_OBJECT_NAMECHANGE,
            nullptr,
            &nativeDialogWinEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT
        ));
        hooks_.append(SetWinEventHook(
            EVENT_SYSTEM_CAPTURESTART,
            EVENT_SYSTEM_CAPTUREEND,
            nullptr,
            &nativeDialogWinEventProc,
            0,
            0,
            WINEVENT_OUTOFCONTEXT
        ));
    }

    ~ScopedNativeDialogWinEventTrace()
    {
        for (HWINEVENTHOOK hook : hooks_) {
            if (hook != nullptr) {
                UnhookWinEvent(hook);
            }
        }
        appendNativeDialogHookLog(
            state_.scope,
            QStringLiteral("trace_end")
        );
        if (g_nativeDialogWinEventTraceState == &state_) {
            g_nativeDialogWinEventTraceState = nullptr;
        }
    }

private:
    NativeDialogWinEventTraceState state_;
    QVector<HWINEVENTHOOK> hooks_;
};
#endif

}  // namespace

bool MainWindow::maybeSaveBeforeContinue()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    if (!documentDirty_) {
        return true;
    }

    const UnsavedChangesChoice choice = showUnsavedChangesDialog(
        this,
        uiText("dialog.unsaved_changes.title", "Unsaved Changes"),
        uiText("dialog.unsaved_changes.message", "Current document has unsaved changes. Save before continue?")
    );
    if (choice == UnsavedChangesChoice::Save) {
        return onSaveFile();
    }
    return choice == UnsavedChangesChoice::Discard;
}

bool MainWindow::maybeSaveCurrentFieldChanges()
{
    if (!currentFieldDirty_) {
        return true;
    }

    const QString fieldName = hasActiveDifficulty()
        ? SimaiDocument::difficultyName(activeDifficultyId_)
        : uiText("dialog.unsaved_field_changes.field.metadata", "Metadata");
    const UnsavedChangesChoice choice = showUnsavedChangesDialog(
        this,
        uiText("dialog.unsaved_field_changes.title", "Unsaved Field Changes"),
        uiText("dialog.unsaved_field_changes.message", "%1 has unsaved changes. Save before switch?").arg(fieldName)
    );
    if (choice == UnsavedChangesChoice::Save) {
        return applyCurrentFieldToDocument();
    }
    if (choice == UnsavedChangesChoice::Discard) {
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
        const QMessageBox::StandardButton choice = UiDialogs::showMessageBox(
            QMessageBox::Warning,
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
        UiDialogs::showMessageBox(QMessageBox::Critical, this, "Create Failed", "Cannot write file:\n" + targetPath);
        return;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        UiDialogs::showMessageBox(QMessageBox::Critical, this, "Create Failed", "Write failed:\n" + targetPath);
        return;
    }

    cancelPendingStartupRestore();
    loadDocument(newDocument);
    clearValidationCache();
    currentEncoding_ = TextEncoding::Utf8;
    setCurrentFilePath(targetPath);
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
    suspendEmbeddedPreviewForNativeDialog("open_file_dialog");
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("Open simai file"),
        resolveInitialOpenDirectory(),
        QStringLiteral("Simai (*.txt *.simai);;All Files (*.*)")
    );
    resumeEmbeddedPreviewForNativeDialog("open_file_dialog");
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

    cancelPendingStartupRestore();
    const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
    if (!payload.success) {
        if (showErrors) {
            UiDialogs::showMessageBox(QMessageBox::Critical, this, "Open Failed", "Cannot open file:\n" + normalizedPath);
        }
        return false;
    }

    applyOpenedDocumentState(
        payload.normalizedPath,
        payload.usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8,
        payload.document,
        showStatusMessage,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
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
    const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(fileInfo.absoluteFilePath(), true);
    if (!payload.success) {
        return false;
    }
    applyOpenedDocumentState(
        payload.normalizedPath,
        payload.usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8,
        payload.document,
        false,
        payload.hasTrackDuration ? payload.trackDurationSeconds : -1.0
    );
    return true;
}

void MainWindow::scheduleStartupRestoreLastSessionFile()
{
    if (!autoRestoreLastSessionFile_ || lastSessionFilePath_.isEmpty()) {
        startupRestorePending_ = false;
        return;
    }

    const QFileInfo fileInfo(lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        lastSessionFilePath_.clear();
        startupRestorePending_ = false;
        return;
    }

    startupRestorePending_ = true;
    const quint64 generation = ++startupRestoreGeneration_;
    const QString normalizedPath = fileInfo.absoluteFilePath();
    QPointer<MainWindow> guard(this);
    QThreadPool* const pool = previewWarmupPool_ != nullptr
        ? previewWarmupPool_
        : QThreadPool::globalInstance();
    pool->start([guard, generation, normalizedPath]() {
        const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, payload]() {
                if (guard.isNull() || generation != guard->startupRestoreGeneration_ || !guard->startupRestorePending_) {
                    return;
                }

                guard->startupRestorePending_ = false;
                if (!payload.success) {
                    appendStartupTimingStage("mainwindow/startup_restore_prepare_failed", 0, 0);
                    return;
                }

                PreparedStartupRestoreDocument prepared;
                prepared.generation = generation;
                prepared.normalizedPath = payload.normalizedPath;
                prepared.document = payload.document;
                prepared.encodingUsed = payload.usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8;
                prepared.resolvedTrackPath = payload.resolvedTrackPath;
                prepared.trackDurationSeconds = payload.trackDurationSeconds;
                prepared.hasTrackDuration = payload.hasTrackDuration;
                prepared.readElapsedMs = payload.readElapsedMs;
                prepared.decodeElapsedMs = payload.decodeElapsedMs;
                prepared.parseElapsedMs = payload.parseElapsedMs;
                prepared.trackProbeElapsedMs = payload.trackProbeElapsedMs;
                prepared.totalElapsedMs = payload.totalElapsedMs;
                guard->applyPreparedStartupRestoreDocument(prepared);
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::cancelPendingStartupRestore()
{
    if (!startupRestorePending_) {
        return;
    }
    startupRestorePending_ = false;
    ++startupRestoreGeneration_;
}

void MainWindow::applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared)
{
    if (prepared.generation != startupRestoreGeneration_) {
        return;
    }

    appendStartupTimingStage("mainwindow/startup_restore_read_bytes", prepared.readElapsedMs, prepared.readElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_decode_text", prepared.decodeElapsedMs, prepared.decodeElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_parse_document", prepared.parseElapsedMs, prepared.parseElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_probe_track_duration", prepared.trackProbeElapsedMs, prepared.trackProbeElapsedMs);
    appendStartupTimingStage("mainwindow/startup_restore_prepare_total", prepared.totalElapsedMs, prepared.totalElapsedMs);

    QElapsedTimer applyTimer;
    applyTimer.start();
    applyOpenedDocumentState(
        prepared.normalizedPath,
        prepared.encodingUsed,
        prepared.document,
        false,
        prepared.hasTrackDuration ? prepared.trackDurationSeconds : -1.0
    );
    const qint64 applyElapsedMs = applyTimer.elapsed();
    appendStartupTimingStage("mainwindow/startup_restore_apply_document_ui", applyElapsedMs, applyElapsedMs);
    appendStartupTimingStage(
        "mainwindow/restored_last_document_applied",
        prepared.totalElapsedMs + applyElapsedMs,
        prepared.totalElapsedMs + applyElapsedMs
    );
}

void MainWindow::applyOpenedDocumentState(
    const QString& normalizedPath,
    TextEncoding encodingUsed,
    const SimaiDocument& document,
    bool showStatusMessage,
    double knownTrackDurationSeconds)
{
    currentEncoding_ = encodingUsed;
    applyWaveformData(QVector<float>(), knownTrackDurationSeconds > 0.0 ? knownTrackDurationSeconds : 0.0);
    setCurrentFilePath(normalizedPath, true);
    loadDocument(document);
    refreshWaveformCache(knownTrackDurationSeconds);
    if (showStatusMessage) {
        statusBar()->showMessage(
            QString("Opened: %1 (%2)")
                .arg(QFileInfo(normalizedPath).fileName())
                .arg(encodingUsed == TextEncoding::Utf8 ? "UTF-8" : "System encoding")
        );
    }
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
    suspendEmbeddedPreviewForNativeDialog("save_file_dialog");
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("Save simai file"),
        currentFilePath_.isEmpty() ? QStringLiteral("chart.txt") : currentFilePath_,
        QStringLiteral("Simai (*.txt *.simai);;All Files (*.*)")
    );
    resumeEmbeddedPreviewForNativeDialog("save_file_dialog");
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
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    bool firstOk = false;
    (void)parsedFirstSeconds(&firstOk);
    if (!firstOk) {
        UiDialogs::showMessageBox(QMessageBox::Critical, this, "Save Failed", "&first must be a valid number of seconds.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        UiDialogs::showMessageBox(QMessageBox::Critical, this, "Save Failed", "Cannot write file:\n" + path);
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
        UiDialogs::showMessageBox(QMessageBox::Critical, this, "Save Failed", "Write failed:\n" + path);
        return false;
    }
    if (normalizedPath != currentFilePath_) {
        setCurrentFilePath(normalizedPath);
    }
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
    lastPreviewNoteMarkerSignature_.clear();
    refreshTimelineMetadata();
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
    lastPreviewNoteMarkerSignature_.clear();
    refreshTimelineMetadata();
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
    const QColor iconColor =
        previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : UiTheme::colors().iconPrimary;
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
        pausePreviewButton_->setStyleSheet(
            previewFullscreenActive_
                ? previewFullscreenPauseButtonStyleSheet(qtPreviewPlaying_)
                : UiTheme::pausePreviewButtonStyleSheet(qtPreviewPlaying_)
        );
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

void MainWindow::clearDeletedDifficultyUndoState()
{
    deletedDifficultyUndoState_ = DeletedDifficultyUndoState{};
}

bool MainWindow::undoDeletedDifficultyField()
{
    if (!deletedDifficultyUndoState_.valid) {
        return false;
    }
    if (!applyCurrentFieldToDocument()) {
        return false;
    }

    const DeletedDifficultyUndoState deletedState = deletedDifficultyUndoState_;
    if (!SimaiDocument::isDifficultyId(deletedState.difficultyId)) {
        clearDeletedDifficultyUndoState();
        return false;
    }
    if (document_.difficulty(deletedState.difficultyId) != nullptr) {
        statusBar()->showMessage(
            QString("Cannot restore %1 because that difficulty already exists.")
                .arg(SimaiDocument::difficultyName(deletedState.difficultyId))
        );
        return false;
    }

    stopQtPreviewPlayback(true);
    SimaiDifficultyData& restoredDifficulty = document_.ensureDifficulty(deletedState.difficultyId);
    restoredDifficulty = deletedState.difficultyData;
    restoredDifficulty.id = deletedState.difficultyId;
    validationCacheByDifficulty_.remove(deletedState.difficultyId);
    documentDirty_ = true;
    currentFieldDirty_ = false;
    updateDirtyState();

    const bool shouldActivateRestoredDifficulty = deletedState.wasActive || !hasActiveDifficulty();
    if (shouldActivateRestoredDifficulty) {
        activeOutlineKey_ = QStringLiteral("chart");
        if (!switchToDifficultyField(deletedState.difficultyId)) {
            return false;
        }
    } else {
        rebuildFieldSidebar();
        updateEditorHeader();
        updateEditorEmptyState();
        updateEditorStatus();
        saveProjectRenderState();
        refreshLayoutAfterPageSwitch();
        QTimer::singleShot(0, this, [this]() { refreshLayoutAfterPageSwitch(); });
    }

    clearDeletedDifficultyUndoState();
    const QString difficultyName = SimaiDocument::difficultyName(deletedState.difficultyId);
    if (currentFilePath_.isEmpty()) {
        statusBar()->showMessage(QString("Restored %1.").arg(difficultyName));
        return true;
    }
    if (!saveToPath(currentFilePath_)) {
        statusBar()->showMessage(QString("Restored %1. Changes are still unsaved.").arg(difficultyName));
        return true;
    }
    statusBar()->showMessage(QString("Restored %1.").arg(difficultyName));
    return true;
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
        updateEditorValidationSummary();
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
    updateEditorValidationSummary();
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
    if (normalizeWholeChartAction_ != nullptr) {
        normalizeWholeChartAction_->setEnabled(enabled);
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
        if (editorValidationSummaryWidget_ != nullptr) {
            editorValidationSummaryWidget_->setVisible(false);
        }
        return;
    }

    const int headerWidth = editorHeaderWidget_->contentsRect().width();
    const bool summaryHasContent =
        editorValidationSummaryWidget_ != nullptr
        && editorValidationSummaryWidget_->property("hasContent").toBool();
    const auto summaryGroupFullWidth = [](QLabel* icon, QLabel* count, int spacing) {
        const bool hasContent = (icon != nullptr && icon->property("hasContent").toBool())
            || (count != nullptr && count->property("hasContent").toBool());
        if (!hasContent) {
            return 0;
        }
        int width = 0;
        if (icon != nullptr) {
            width += icon->sizeHint().width();
        }
        if (count != nullptr) {
            if (width > 0) {
                width += spacing;
            }
            width += count->sizeHint().width();
        }
        return width;
    };
    int summaryFullWidth = 0;
    int summaryVisibleGroups = 0;
    for (const int groupWidth : {
             summaryGroupFullWidth(editorValidationErrorIconLabel_, editorValidationErrorCountLabel_, 6),
             summaryGroupFullWidth(editorValidationWarningIconLabel_, editorValidationWarningCountLabel_, 3),
             summaryGroupFullWidth(editorValidationMuriIconLabel_, editorValidationMuriCountLabel_, 4)}) {
        if (groupWidth <= 0) {
            continue;
        }
        if (summaryVisibleGroups > 0) {
            summaryFullWidth += 8;
        }
        summaryFullWidth += groupWidth;
        ++summaryVisibleGroups;
    }
    const int summaryExtraWidth = summaryHasContent ? qMax(0, summaryFullWidth - 52) : 0;
    const int summaryIconOnlyThreshold = 456 + qMin(summaryExtraWidth, 48);
    const int summaryHideThreshold = 374 + qMin(summaryExtraWidth / 2, 28);
    const int compactCursorThreshold = 400 + qMin(summaryExtraWidth / 2, 28);
    const int cursorHideThreshold = 360 + qMin(summaryExtraWidth / 2, 20);

    bool showLevelControls = true;
    bool showDesignerControls = true;
    bool showSummary = summaryHasContent;
    bool showSummaryCounts = summaryHasContent;
    bool showCursor = true;
    bool compactCursor = false;
    int levelWidth = 48;
    int designerWidth = 105;

    if (headerWidth < 700) {
        designerWidth = 96;
    }
    if (headerWidth < 640) {
        designerWidth = 84;
        levelWidth = 45;
    }
    if (headerWidth < summaryIconOnlyThreshold) {
        showSummaryCounts = false;
    }
    if (headerWidth < summaryHideThreshold) {
        showSummary = false;
        levelWidth = 39;
        designerWidth = 69;
    }
    if (headerWidth < compactCursorThreshold) {
        compactCursor = true;
        designerWidth = 75;
        levelWidth = 41;
    }
    if (headerWidth < cursorHideThreshold) {
        showCursor = false;
        levelWidth = 36;
        designerWidth = 63;
        showDesignerControls = false;
    }
    if (headerWidth < 310) {
        showLevelControls = false;
    }

    if (difficultyLevelLabel_ != nullptr) {
        difficultyLevelLabel_->setVisible(showLevelControls);
    }
    if (difficultyDesignerLabel_ != nullptr) {
        difficultyDesignerLabel_->setVisible(showDesignerControls);
    }
    if (difficultyLevelEdit_ != nullptr) {
        difficultyLevelEdit_->setFixedWidth(levelWidth);
        difficultyLevelEdit_->setVisible(showLevelControls);
    }
    if (difficultyDesignerEdit_ != nullptr) {
        difficultyDesignerEdit_->setFixedWidth(designerWidth);
        difficultyDesignerEdit_->setVisible(showDesignerControls);
    }
    if (editorValidationSummaryWidget_ != nullptr) {
        editorValidationSummaryWidget_->setVisible(summaryHasContent && showSummary);
        const auto applySummaryVisibility = [showSummary, showSummaryCounts](QLabel* icon, QLabel* count) {
            const bool hasContent = (icon != nullptr && icon->property("hasContent").toBool())
                || (count != nullptr && count->property("hasContent").toBool());
            if (icon != nullptr) {
                icon->setVisible(showSummary && hasContent);
            }
            if (count != nullptr) {
                count->setVisible(showSummary && showSummaryCounts && hasContent);
            }
        };
        applySummaryVisibility(editorValidationErrorIconLabel_, editorValidationErrorCountLabel_);
        applySummaryVisibility(editorValidationWarningIconLabel_, editorValidationWarningCountLabel_);
        applySummaryVisibility(editorValidationMuriIconLabel_, editorValidationMuriCountLabel_);
        editorValidationSummaryWidget_->adjustSize();
    }
    if (showCursor) {
        const auto [line, col] = currentCursorLineCol();
        if (compactCursor) {
            editorCursorLabel_->setText(QStringLiteral("%1:%2").arg(line).arg(col));
        } else if (UiText::isChineseUi()) {
            editorCursorLabel_->setText(QStringLiteral("%1行 %2列").arg(line).arg(col));
        } else {
            editorCursorLabel_->setText(QStringLiteral("Ln %1, Col %2").arg(line).arg(col));
        }
        editorCursorLabel_->setFixedWidth(
            QFontMetrics(editorCursorLabel_->font()).horizontalAdvance(editorCursorLabel_->text()) + 10);
    }
    editorCursorLabel_->setVisible(showCursor);
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
        const QMessageBox::StandardButton choice = UiDialogs::showMessageBox(
            QMessageBox::Question,
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

    clearDeletedDifficultyUndoState();
    deletedDifficultyUndoState_.valid = true;
    deletedDifficultyUndoState_.wasActive = deletingActiveDifficulty;
    deletedDifficultyUndoState_.difficultyId = difficultyId;
    deletedDifficultyUndoState_.difficultyData.id = difficultyId;
    deletedDifficultyUndoState_.difficultyData.level = currentLevel;
    deletedDifficultyUndoState_.difficultyData.designer = currentDesigner;
    deletedDifficultyUndoState_.difficultyData.chart = currentChart;

    stopQtPreviewPlayback(true);
    document_.removeDifficulty(difficultyId);
    validationCacheByDifficulty_.remove(difficultyId);
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
    constexpr int kOutlineIconOnlyThreshold = 120;
    const int outlineListWidth =
        outlineList_ != nullptr && outlineList_->viewport() != nullptr ? outlineList_->viewport()->width() : 0;
    if (!visible
        || !hasActiveDifficulty()
        || outlineList_ == nullptr
        || (outlineListWidth > 0 && outlineListWidth < kOutlineIconOnlyThreshold)) {
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
    const QString metadataLabel = uiText("sidebar.metadata", "Metadata");
    auto* metadataItem = new QListWidgetItem(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        metadataLabel,
        outlineList_
    );
    metadataItem->setData(Qt::UserRole, "metadata");
    metadataItem->setToolTip(metadataLabel);

    QListWidgetItem* selectedItem = nullptr;
    bool hasMissingDifficulty = false;
    const QVector<int> ids = document_.difficultyIds();
    for (int id : ids) {
        const QString difficultyLabel = SimaiDocument::difficultyName(id);
        auto* difficultyItem = new QListWidgetItem(difficultyLabel, outlineList_);
        difficultyItem->setIcon(makeDifficultyBadgeIcon(id));
        difficultyItem->setData(Qt::UserRole, "difficulty_chart");
        difficultyItem->setData(Qt::UserRole + 1, id);
        difficultyItem->setToolTip(difficultyLabel);
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
        const QString addDifficultyLabel = uiText("sidebar.add_difficulty", "+ Add Difficulty");
        auto* addItem = new QListWidgetItem(
            style()->standardIcon(QStyle::SP_FileDialogNewFolder),
            addDifficultyLabel,
            outlineList_
        );
        addItem->setData(Qt::UserRole, "add");
        addItem->setToolTip(addDifficultyLabel);
    }
    auto* toolboxItem = new QListWidgetItem(
        makeToolboxAccessIcon(UiTheme::colors().iconPrimary, QColor(QStringLiteral("#E6B84A"))),
        UiText::isChineseUi() ? QStringLiteral("工具箱") : QStringLiteral("Toolbox"),
        outlineList_
    );
    toolboxItem->setData(Qt::UserRole, "toolbox");
    toolboxItem->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("打开工具箱：无理检测 / 视频导出 / BPM检测与偏移")
            : QStringLiteral("Open toolbox: Muri Check / Video Export / BPM & Offset")
    );
    toolboxItem->setText(UiText::isChineseUi() ? QStringLiteral("工具箱") : QStringLiteral("Toolbox"));
    toolboxItem->setToolTip(
        UiText::isChineseUi()
            ? QStringLiteral("打开工具箱：无理检测 / 谱面格式整理 / 批量视频导出 / 官谱镜像站")
            : QStringLiteral("Open toolbox: Muri Check / Format Chart / Batch Video Export / Official Chart Mirror")
    );
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
    pendingPreviewPlaybackStart_ = false;
    pendingPreviewPlaybackResumeFromPause_ = false;
    pendingPreviewPlaybackRevision_ = 0;
    pendingPreviewPlaybackDifficultyId_ = 0;
    pendingPreviewPlaybackSecond_ = 0.0;
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
    pendingPreviewPlaybackStart_ = false;
    pendingPreviewPlaybackResumeFromPause_ = false;
    pendingPreviewPlaybackRevision_ = 0;
    pendingPreviewPlaybackDifficultyId_ = 0;
    pendingPreviewPlaybackSecond_ = 0.0;
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
    pendingPreviewPlaybackStart_ = false;
    pendingPreviewPlaybackResumeFromPause_ = false;
    pendingPreviewPlaybackRevision_ = 0;
    pendingPreviewPlaybackDifficultyId_ = 0;
    pendingPreviewPlaybackSecond_ = 0.0;
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
    currentFieldDirty_ = false;
    updateDirtyState();
    rebuildFieldSidebar();
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
    clearDeletedDifficultyUndoState();
    document_ = document;
    documentDirty_ = false;
    currentFieldDirty_ = false;
    activeDifficultyId_ = 0;
    activeOutlineKey_ = document_.difficultyIds().isEmpty() ? QStringLiteral("welcome") : QStringLiteral("chart");
    activateInitialField();
    updateMetadataPageMode();
    updateDirtyState();
    updateWindowTitle();
}

void MainWindow::clearTimelineAndPreview()
{
    timelineQuickModel_.clear();
    pendingTimelineSlowRefresh_ = TimelineSlowRefreshRequest();
    pendingTimelineAnalysisRefresh_ = TimelineAnalysisRefreshRequest();
    timelineSlowRequestedRevision_ = 0;
    timelineSlowRunningRevision_ = 0;
    timelineAnalysisRequestedRevision_ = 0;
    timelineAnalysisRunningRevision_ = 0;
    lastPreviewNoteMarkerSignature_.clear();
    latestTimelineNoteMarkers_.clear();
    latestTimelineNoteMarkerSignature_.clear();
    latestTimelinePreviewRevision_ = 0;
    latestTimelinePreviewSnapshotReady_ = false;
    lastTimelineParseDifficultyId_ = 0;
    lastTimelineParseChartText_.clear();
    lastTimelineParseTimingMetadata_ = miacode::simai::SimaiTimingMetadata();
    lastTimelineParseResult_ = SimaiNativeParseResult();
    muriAnalysisReport_ = MuriAnalysisReport();
    muriAnalysisReportNoteMarkerSignature_.clear();
    pendingDeferredValidationUiRefresh_ = false;
    pendingDeferredMuriUiRefresh_ = false;
    if (timelineAnalysisIdleTimer_ != nullptr) {
        timelineAnalysisIdleTimer_->stop();
    }
    clearPreviewFollowDecoration();
    clearPreviewObjectStats();
    clearMuriDiagnostics();
    previewTrackDurationSeconds_ = 0.0;
    qtPreviewTimelineDirty_ = false;
    qtPreviewPendingTimelineSecond_ = 0.0;
    qtPreviewPendingTimelineCenterView_ = true;
    pendingPreviewPlaybackStart_ = false;
    pendingPreviewPlaybackResumeFromPause_ = false;
    pendingPreviewPlaybackRevision_ = 0;
    pendingPreviewPlaybackDifficultyId_ = 0;
    pendingPreviewPlaybackSecond_ = 0.0;
    qtPreviewLastTimelineSecond_ = -1.0;
    qtPreviewTimelineStartSecond_ = 0.0;
    qtPreviewPlaybackReturnSecond_ = 0.0;
    qtPreviewPlaybackEndSecond_ = 0.0;
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->clearTimeline();
    }
    stopQtPreviewPlayback(false);
    if (timelineView_ != nullptr) {
        timelineView_->clear();
        timelineView_->setMuriAnalysisReport(muriAnalysisReport_);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->reset();
        previewCanvas_->setMuriAnalysisReport(muriAnalysisReport_);
    }
    if (previewMediaController_ != nullptr) {
        dispatchPreviewMediaControllerCall([](PreviewMediaController* controller) {
            controller->reset();
        });
    }
    updatePreviewSliderRange();
    updatePreviewSliderPosition(0.0);
}
