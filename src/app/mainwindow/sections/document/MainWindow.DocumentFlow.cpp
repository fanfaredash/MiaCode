#include "MainWindow.DocumentSection.h"
#include "../../MainWindowShared.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/scene/PreviewProgressStatsCache.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

MainWindow::DocumentSection::DocumentSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

namespace {

enum class UnsavedChangesChoice {
    Save,
    Discard,
    Cancel,
};

struct PreparedDocumentOpenPayload {
    bool success = false;
    bool usedSystemEncoding = false;
    QString normalizedPath;
    SimaiDocument document;
    QString resolvedTrackPath;
    double trackDurationSeconds = 0.0;
    bool hasTrackDuration = false;
    qint64 readElapsedMs = 0;
    qint64 decodeElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 trackProbeElapsedMs = 0;
    qint64 totalElapsedMs = 0;
};

PreparedDocumentOpenPayload prepareDocumentOpenPayload(const QString& path, bool probeTrackDuration)
{
    PreparedDocumentOpenPayload payload;
    payload.normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (payload.normalizedPath.isEmpty()) {
        return payload;
    }

    QFile file(payload.normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return payload;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer phaseTimer;
    phaseTimer.start();
    const QByteArray bytes = file.readAll();
    payload.readElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    QString text;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(bytes.mid(3));
    } else {
        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        text = utf8Decoder.decode(bytes);
        if (utf8Decoder.hasError()) {
            QStringDecoder systemDecoder(QStringConverter::System);
            text = systemDecoder.decode(bytes);
            payload.usedSystemEncoding = true;
        }
    }
    payload.decodeElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    payload.document = SimaiDocument::fromText(text);
    payload.parseElapsedMs = phaseTimer.elapsed();

    if (probeTrackDuration) {
        payload.resolvedTrackPath = miacode::chart_assets::resolveTrackPath(payload.normalizedPath);
        phaseTimer.restart();
        if (!payload.resolvedTrackPath.isEmpty()) {
            payload.trackDurationSeconds = probeAudioDurationSeconds(payload.resolvedTrackPath);
            payload.hasTrackDuration = payload.trackDurationSeconds > 0.0;
        }
        payload.trackProbeElapsedMs = phaseTimer.elapsed();
    }

    payload.totalElapsedMs = totalTimer.elapsed();
    payload.success = true;
    return payload;
}

UnsavedChangesChoice showUnsavedChangesDialog(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox dialog(
        QMessageBox::Warning,
        title,
        text,
        QMessageBox::NoButton,
        UiDialogs::effectiveParentWidget(parent)
    );
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    UiDialogs::applyDetachedParentBehavior(&dialog, parent);
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

}  // namespace

bool MainWindow::DocumentSection::maybeSaveBeforeContinue()
{
    if (!maybeSaveCurrentFieldChanges()) {
        return false;
    }
    if (!state_.documentDirty_) {
        return true;
    }

    const UnsavedChangesChoice choice = showUnsavedChangesDialog(
        &owner_,
        uiText("dialog.unsaved_changes.title", "Unsaved Changes"),
        uiText("dialog.unsaved_changes.message", "Current document has unsaved changes. Save before continue?")
    );
    if (choice == UnsavedChangesChoice::Save) {
        return onSaveFile();
    }
    return choice == UnsavedChangesChoice::Discard;
}

bool MainWindow::DocumentSection::maybeSaveCurrentFieldChanges()
{
    if (!state_.currentFieldDirty_) {
        return true;
    }

    const QString fieldName = owner_.hasActiveDifficulty()
        ? SimaiDocument::difficultyName(state_.activeDifficultyId_)
        : uiText("dialog.unsaved_field_changes.field.metadata", "Metadata");
    const UnsavedChangesChoice choice = showUnsavedChangesDialog(
        &owner_,
        uiText("dialog.unsaved_field_changes.title", "Unsaved Field Changes"),
        uiText("dialog.unsaved_field_changes.message", "%1 has unsaved changes. Save before switch?").arg(fieldName)
    );
    if (choice == UnsavedChangesChoice::Save) {
        return applyCurrentFieldToDocument();
    }
    if (choice == UnsavedChangesChoice::Discard) {
        state_.currentFieldDirty_ = false;
        updateDirtyState();
        return true;
    }
    return false;
}

bool MainWindow::DocumentSection::applyCurrentFieldToDocument()
{
    bool changed = false;
    bool metadataTimingChanged = false;
    if (owner_.hasActiveDifficulty()) {
        SimaiDifficultyData& difficultyData = state_.document_.ensureDifficulty(state_.activeDifficultyId_);
        const QString newLevel = ui_.difficultyLevelEdit_ != nullptr ? ui_.difficultyLevelEdit_->text() : QString();
        const QString newDesigner = ui_.difficultyDesignerEdit_ != nullptr ? ui_.difficultyDesignerEdit_->text() : QString();
        const QString newChart = owner_.editorText();
        if (difficultyData.level != newLevel || difficultyData.designer != newDesigner || difficultyData.chart != newChart) {
            difficultyData.level = newLevel;
            difficultyData.designer = newDesigner;
            difficultyData.chart = newChart;
            changed = true;
        }
    } else {
        const QString newTitle = ui_.titleEdit_ != nullptr ? ui_.titleEdit_->text() : QString();
        const QString newArtist = ui_.artistEdit_ != nullptr ? ui_.artistEdit_->text() : QString();
        const QString newFirst = ui_.firstEdit_ != nullptr ? ui_.firstEdit_->text() : QString();
        const QString newDesigner = ui_.designerEdit_ != nullptr ? ui_.designerEdit_->text() : QString();
        const QVector<SimaiRawField> newExtraFields = SimaiDocument::parseRawFields(
            ui_.metadataExtraEdit_ != nullptr ? ui_.metadataExtraEdit_->toPlainText() : QString(),
            true
        );
        if (state_.document_.title != newTitle
            || state_.document_.artist != newArtist
            || state_.document_.first != newFirst
            || state_.document_.designer != newDesigner
            || state_.document_.extraFields != newExtraFields) {
            metadataTimingChanged = (state_.document_.first != newFirst);
            state_.document_.title = newTitle;
            state_.document_.artist = newArtist;
            state_.document_.first = newFirst;
            state_.document_.designer = newDesigner;
            state_.document_.extraFields = newExtraFields;
            changed = true;
        }
    }

    state_.currentFieldDirty_ = false;
    if (changed) {
        state_.documentDirty_ = true;
    }
    updateDirtyState();
    owner_.updateWindowTitle();
    rebuildFieldSidebar();
    if (metadataTimingChanged) {
        owner_.refreshWaveformCache();
        owner_.refreshTimelineMetadata();
    }
    return true;
}

void MainWindow::DocumentSection::onNewFile()
{
    if (!maybeSaveBeforeContinue()) {
        return;
    }

    const QString targetDirectory = QFileDialog::getExistingDirectory(
        &owner_,
        UiText::isChineseUi() ? QStringLiteral("选择谱面文件夹") : QStringLiteral("Select Chart Folder"),
        owner_.resolveInitialOpenDirectory(),
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
            &owner_,
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
            UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Create Failed", "Cannot write file:\n" + targetPath);
        return;
    }
    if (file.write(payload) != payload.size() || !file.commit()) {
        UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Create Failed", "Write failed:\n" + targetPath);
        return;
    }

    cancelPendingStartupRestore();
    loadDocument(newDocument);
    owner_.clearValidationCache();
    state_.currentEncoding_ = TextEncoding::Utf8;
    owner_.setCurrentFilePath(targetPath);
    owner_.statusBar()->showMessage(QString("Created: %1").arg(targetPath));
}

void MainWindow::DocumentSection::onOpenFile()
{
    owner_.logTopLevelWindowSnapshot("open_file_flow/begin");
    const bool canContinue = maybeSaveBeforeContinue();
    if (!canContinue) {
        owner_.logTopLevelWindowSnapshot("open_file_flow/cancelled_before_dialog");
        return;
    }

    owner_.logWindowGeometryDebug("open_file_before_dialog");
    owner_.logTopLevelWindowSnapshot("open_file_before_dialog");
    const QString path = QFileDialog::getOpenFileName(
        &owner_,
        QStringLiteral("Open simai file"),
        owner_.resolveInitialOpenDirectory(),
        QStringLiteral("Simai (*.txt *.simai);;All Files (*.*)")
    );
    owner_.logWindowGeometryDebug("open_file_after_dialog", QString("selected_empty=%1").arg(path.isEmpty() ? 1 : 0));
    owner_.logTopLevelWindowSnapshot("open_file_after_dialog");
    if (path.isEmpty()) {
        return;
    }
    openFileAtPath(path, true, true);
}

bool MainWindow::DocumentSection::openFileAtPath(const QString& path, bool showStatusMessage, bool showErrors)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    cancelPendingStartupRestore();
    const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
    if (!payload.success) {
        if (showErrors) {
            UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Open Failed", "Cannot open file:\n" + normalizedPath);
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

bool MainWindow::DocumentSection::restoreLastSessionFile()
{
    if (state_.lastSessionFilePath_.isEmpty()) {
        return false;
    }
    const QFileInfo fileInfo(state_.lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        state_.lastSessionFilePath_.clear();
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

void MainWindow::DocumentSection::scheduleStartupRestoreLastSessionFile()
{
    if (!state_.autoRestoreLastSessionFile_ || state_.lastSessionFilePath_.isEmpty()) {
        state_.startupRestorePending_ = false;
        return;
    }

    const QFileInfo fileInfo(state_.lastSessionFilePath_);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        state_.lastSessionFilePath_.clear();
        state_.startupRestorePending_ = false;
        return;
    }

    state_.startupRestorePending_ = true;
    const quint64 generation = ++state_.startupRestoreGeneration_;
    const QString normalizedPath = fileInfo.absoluteFilePath();
    QPointer<MainWindow> guard(&owner_);
    QThreadPool* const pool = state_.previewWarmupPool_ != nullptr
        ? state_.previewWarmupPool_
        : QThreadPool::globalInstance();
    pool->start([guard, generation, normalizedPath]() {
        const PreparedDocumentOpenPayload payload = prepareDocumentOpenPayload(normalizedPath, true);
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, payload]() {
                if (guard.isNull() || generation != guard->state_.startupRestoreGeneration_ || !guard->state_.startupRestorePending_) {
                    return;
                }

                guard->state_.startupRestorePending_ = false;
                if (!payload.success) {
                    appendStartupTimingStage("mainwindow/startup_restore_prepare_failed", 0, 0);
                    guard->scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
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

void MainWindow::DocumentSection::cancelPendingStartupRestore()
{
    if (!state_.startupRestorePending_) {
        return;
    }
    state_.startupRestorePending_ = false;
    ++state_.startupRestoreGeneration_;
}

void MainWindow::DocumentSection::applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared)
{
    if (prepared.generation != state_.startupRestoreGeneration_) {
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
    owner_.scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void MainWindow::DocumentSection::applyOpenedDocumentState(
    const QString& normalizedPath,
    TextEncoding encodingUsed,
    const SimaiDocument& document,
    bool showStatusMessage,
    double knownTrackDurationSeconds)
{
    state_.currentEncoding_ = encodingUsed;
    owner_.applyWaveformData(QVector<float>(), knownTrackDurationSeconds > 0.0 ? knownTrackDurationSeconds : 0.0);
    owner_.setCurrentFilePath(normalizedPath, true);
    loadDocument(document);
    owner_.refreshWaveformCache(knownTrackDurationSeconds);
    if (showStatusMessage) {
        owner_.statusBar()->showMessage(
            QString("Opened: %1 (%2)")
                .arg(QFileInfo(normalizedPath).fileName())
                .arg(encodingUsed == TextEncoding::Utf8 ? "UTF-8" : "System encoding")
        );
    }
}

void MainWindow::DocumentSection::resetAutosaveState(const QString& referenceText)
{
    state_.autosaveReferenceContentSignature_ = autosaveContentSignature(referenceText);
    state_.autosaveLastContentSignature_.clear();
}

QString MainWindow::DocumentSection::resolveAutosaveDirectoryPath() const
{
    if (state_.currentFilePath_.isEmpty()) {
        return QString();
    }

    const QFileInfo currentFileInfo(state_.currentFilePath_);
    const QString projectDirectoryPath = currentFileInfo.absolutePath();
    if (projectDirectoryPath.isEmpty()) {
        return QString();
    }

    return QDir(projectDirectoryPath).filePath(QStringLiteral(".autosave"));
}

QString MainWindow::DocumentSection::currentDocumentTextForAutosave() const
{
    SimaiDocument snapshot = state_.document_;
    if (owner_.hasActiveDifficulty()) {
        SimaiDifficultyData& difficultyData = snapshot.ensureDifficulty(state_.activeDifficultyId_);
        difficultyData.level = ui_.difficultyLevelEdit_ != nullptr ? ui_.difficultyLevelEdit_->text() : QString();
        difficultyData.designer = ui_.difficultyDesignerEdit_ != nullptr ? ui_.difficultyDesignerEdit_->text() : QString();
        difficultyData.chart = owner_.editorText();
    } else if (state_.activeOutlineKey_ == QLatin1String("metadata")) {
        snapshot.title = ui_.titleEdit_ != nullptr ? ui_.titleEdit_->text() : QString();
        snapshot.artist = ui_.artistEdit_ != nullptr ? ui_.artistEdit_->text() : QString();
        snapshot.first = ui_.firstEdit_ != nullptr ? ui_.firstEdit_->text() : QString();
        snapshot.designer = ui_.designerEdit_ != nullptr ? ui_.designerEdit_->text() : QString();
        snapshot.extraFields = SimaiDocument::parseRawFields(
            ui_.metadataExtraEdit_ != nullptr ? ui_.metadataExtraEdit_->toPlainText() : QString(),
            true
        );
    }
    return snapshot.toText();
}

void MainWindow::DocumentSection::pruneAutosaveFiles(const QString& autosaveDirectoryPath) const
{
    QDir autosaveDir(autosaveDirectoryPath);
    QFileInfoList autosaveFiles = autosaveDir.entryInfoList(
        QStringList{QStringLiteral("autosave_*.txt")},
        QDir::Files | QDir::NoDotAndDotDot
    );
    if (autosaveFiles.size() <= kAutosaveMaxVersions) {
        return;
    }

    std::sort(
        autosaveFiles.begin(),
        autosaveFiles.end(),
        [](const QFileInfo& lhs, const QFileInfo& rhs) {
            if (lhs.lastModified() == rhs.lastModified()) {
                return lhs.fileName() < rhs.fileName();
            }
            return lhs.lastModified() < rhs.lastModified();
        }
    );

    const int removeCount = autosaveFiles.size() - kAutosaveMaxVersions;
    for (int index = 0; index < removeCount; ++index) {
        autosaveDir.remove(autosaveFiles.at(index).fileName());
    }
}

void MainWindow::DocumentSection::runAutosaveCheck()
{
    if (state_.currentFilePath_.isEmpty()) {
        return;
    }

    const QString snapshotText = currentDocumentTextForAutosave();
    const QByteArray snapshotSignature = autosaveContentSignature(snapshotText);
    if (snapshotSignature == state_.autosaveReferenceContentSignature_
        || snapshotSignature == state_.autosaveLastContentSignature_) {
        return;
    }

    const QString autosaveDirectoryPath = resolveAutosaveDirectoryPath();
    if (autosaveDirectoryPath.isEmpty()) {
        return;
    }

    QDir autosaveDir(autosaveDirectoryPath);
    if (!autosaveDir.exists() && !autosaveDir.mkpath(QStringLiteral("."))) {
        return;
    }

    QString autosaveBaseName = QFileInfo(state_.currentFilePath_).completeBaseName();
    if (autosaveBaseName.trimmed().isEmpty()) {
        autosaveBaseName = QStringLiteral("maidata");
    }
    const QString autosaveFileName = QStringLiteral("autosave_%1_%2.txt")
        .arg(
            autosaveBaseName,
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))
        );
    QSaveFile autosaveFile(autosaveDir.filePath(autosaveFileName));
    if (!autosaveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    const QByteArray payload = snapshotText.toUtf8();
    if (autosaveFile.write(payload) != payload.size() || !autosaveFile.commit()) {
        return;
    }

    state_.autosaveLastContentSignature_ = snapshotSignature;
    pruneAutosaveFiles(autosaveDirectoryPath);
}

bool MainWindow::DocumentSection::onSaveFile()
{
    if (state_.currentFilePath_.isEmpty()) {
        return onSaveFileAs();
    }
    return saveToPath(state_.currentFilePath_);
}

bool MainWindow::DocumentSection::onSaveFileAs()
{
    owner_.logTopLevelWindowSnapshot("save_file_as_dialog/begin");
    owner_.logWindowGeometryDebug("save_file_as_before_dialog");
    const QString path = QFileDialog::getSaveFileName(
        &owner_,
        QStringLiteral("Save simai file"),
        state_.currentFilePath_.isEmpty() ? QStringLiteral("chart.txt") : state_.currentFilePath_,
        QStringLiteral("Simai (*.txt *.simai);;All Files (*.*)")
    );
    owner_.logWindowGeometryDebug("save_file_as_after_dialog", QString("selected_empty=%1").arg(path.isEmpty() ? 1 : 0));
    owner_.logTopLevelWindowSnapshot("save_file_as_dialog/after_dialog");
    if (path.isEmpty()) {
        return false;
    }
    owner_.setLastOpenDirectory(path);
    return saveToPath(path);
}

bool MainWindow::DocumentSection::saveToPath(const QString& path)
{
    if (!applyCurrentFieldToDocument()) {
        return false;
    }
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    bool firstOk = false;
    (void)owner_.parsedFirstSeconds(&firstOk);
    if (!firstOk) {
        UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Save Failed", "&first must be a valid number of seconds.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Save Failed", "Cannot write file:\n" + path);
        return false;
    }
    QByteArray data;
    const QString serialized = state_.document_.toText();
    if (state_.currentEncoding_ == TextEncoding::System) {
        QStringEncoder encoder(QStringConverter::System);
        data = encoder.encode(serialized);
    } else {
        QStringEncoder encoder(QStringConverter::Utf8);
        data = encoder.encode(serialized);
    }
    if (file.write(data) != data.size() || !file.commit()) {
        UiDialogs::showMessageBox(QMessageBox::Critical, &owner_, "Save Failed", "Write failed:\n" + path);
        return false;
    }
    if (normalizedPath != state_.currentFilePath_) {
        owner_.setCurrentFilePath(normalizedPath);
    }
    resetAutosaveState(serialized);
    state_.documentDirty_ = false;
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    owner_.updateWindowTitle();
    owner_.statusBar()->showMessage("Saved: " + QFileInfo(path).fileName());
    return true;
}

bool MainWindow::DocumentSection::applyBatchTransform(const QString& opName, const BatchTransform& transform)
{
    const QString original = owner_.editorText();
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    int changed = 0;
    const QString transformed = transform(original, &changed);
    if (transformed == original) {
        owner_.statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
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
    state_.lastPreviewNoteMarkerSignature_.clear();
    owner_.refreshTimelineMetadata();
    owner_.statusBar()->showMessage(QString("%1 applied: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

bool MainWindow::DocumentSection::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
{
    auto* editor = qobject_cast<PlainCodeEditor*>(ui_.editorWidget_);
    const QTextCursor oldCursor = editor->textCursor();
    const int oldVScroll = editor->verticalScrollBar() != nullptr ? editor->verticalScrollBar()->value() : 0;
    const int oldHScroll = editor->horizontalScrollBar() != nullptr ? editor->horizontalScrollBar()->value() : 0;
    int startPos = -1;
    int endPos = -1;
    if (!currentSelectionRange(&startPos, &endPos)) {
        owner_.statusBar()->showMessage(QString("%1: no selection.").arg(opName));
        return false;
    }

    const QString original = owner_.editorText();
    const int begin = qMin(startPos, endPos);
    const int finish = qMax(startPos, endPos);
    if (begin < 0 || finish <= begin || finish > original.size()) {
        owner_.statusBar()->showMessage(QString("%1: invalid selection range.").arg(opName));
        return false;
    }

    const QString selected = original.mid(begin, finish - begin);
    int changed = 0;
    const QString transformed = transform(selected, &changed);
    if (transformed == selected) {
        owner_.statusBar()->showMessage(QString("%1: no note index changed.").arg(opName));
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
    state_.lastPreviewNoteMarkerSignature_.clear();
    owner_.refreshTimelineMetadata();
    owner_.statusBar()->showMessage(QString("%1 applied on selection: %2 replacement(s).").arg(opName).arg(changed));
    return true;
}

bool MainWindow::maybeSaveBeforeContinue()
{
    return documentSection_->maybeSaveBeforeContinue();
}

bool MainWindow::maybeSaveCurrentFieldChanges()
{
    return documentSection_->maybeSaveCurrentFieldChanges();
}

bool MainWindow::applyCurrentFieldToDocument()
{
    return documentSection_->applyCurrentFieldToDocument();
}

void MainWindow::onNewFile()
{
    documentSection_->onNewFile();
}

void MainWindow::onOpenFile()
{
    documentSection_->onOpenFile();
}

bool MainWindow::openFileAtPath(const QString& path, bool showStatusMessage, bool showErrors)
{
    return documentSection_->openFileAtPath(path, showStatusMessage, showErrors);
}

bool MainWindow::restoreLastSessionFile()
{
    return documentSection_->restoreLastSessionFile();
}

void MainWindow::scheduleStartupRestoreLastSessionFile()
{
    documentSection_->scheduleStartupRestoreLastSessionFile();
}

void MainWindow::cancelPendingStartupRestore()
{
    documentSection_->cancelPendingStartupRestore();
}

void MainWindow::applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared)
{
    documentSection_->applyPreparedStartupRestoreDocument(prepared);
}

void MainWindow::applyOpenedDocumentState(
    const QString& normalizedPath,
    TextEncoding encodingUsed,
    const SimaiDocument& document,
    bool showStatusMessage,
    double knownTrackDurationSeconds)
{
    documentSection_->applyOpenedDocumentState(
        normalizedPath,
        encodingUsed,
        document,
        showStatusMessage,
        knownTrackDurationSeconds
    );
}

void MainWindow::resetAutosaveState(const QString& referenceText)
{
    documentSection_->resetAutosaveState(referenceText);
}

QString MainWindow::resolveAutosaveDirectoryPath() const
{
    return documentSection_->resolveAutosaveDirectoryPath();
}

QString MainWindow::currentDocumentTextForAutosave() const
{
    return documentSection_->currentDocumentTextForAutosave();
}

void MainWindow::pruneAutosaveFiles(const QString& autosaveDirectoryPath) const
{
    documentSection_->pruneAutosaveFiles(autosaveDirectoryPath);
}

void MainWindow::runAutosaveCheck()
{
    documentSection_->runAutosaveCheck();
}

bool MainWindow::onSaveFile()
{
    return documentSection_->onSaveFile();
}

bool MainWindow::onSaveFileAs()
{
    return documentSection_->onSaveFileAs();
}

bool MainWindow::saveToPath(const QString& path)
{
    return documentSection_->saveToPath(path);
}

bool MainWindow::applyBatchTransform(const QString& opName, const BatchTransform& transform)
{
    return documentSection_->applyBatchTransform(opName, transform);
}

bool MainWindow::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
{
    return documentSection_->applySelectionBatchTransform(opName, transform);
}

std::pair<int, int> MainWindow::currentCursorLineCol() const
{
    return documentSection_->currentCursorLineCol();
}

std::pair<int, int> MainWindow::currentSelectionOrCursorLineCol() const
{
    return documentSection_->currentSelectionOrCursorLineCol();
}

bool MainWindow::currentSelectionRange(int* startPos, int* endPos) const
{
    return documentSection_->currentSelectionRange(startPos, endPos);
}

void MainWindow::setMetadataExtraText(const QString& text)
{
    documentSection_->setMetadataExtraText(text);
}

void MainWindow::setEditorText(const QString& text)
{
    documentSection_->setEditorText(text);
}

void MainWindow::updatePauseButtonAppearance()
{
    documentSection_->updatePauseButtonAppearance();
}

void MainWindow::updateDirtyState()
{
    documentSection_->updateDirtyState();
}

bool MainWindow::currentFieldHasUndoChanges() const
{
    return documentSection_->currentFieldHasUndoChanges();
}

void MainWindow::refreshCurrentFieldDirtyState()
{
    documentSection_->refreshCurrentFieldDirtyState();
}

void MainWindow::markCurrentFieldDirty()
{
    documentSection_->markCurrentFieldDirty();
}

void MainWindow::clearDeletedDifficultyUndoState()
{
    documentSection_->clearDeletedDifficultyUndoState();
}

bool MainWindow::undoDeletedDifficultyField()
{
    return documentSection_->undoDeletedDifficultyField();
}

void MainWindow::updateEditorHeader()
{
    documentSection_->updateEditorHeader();
}

void MainWindow::updateDifficultyScopedActionStates()
{
    documentSection_->updateDifficultyScopedActionStates();
}

void MainWindow::updateEditorHeaderLayoutMode()
{
    documentSection_->updateEditorHeaderLayoutMode();
}

void MainWindow::syncEditorHeaderMinimumWidth()
{
    documentSection_->syncEditorHeaderMinimumWidth();
}

void MainWindow::updateEditorStatus()
{
    documentSection_->updateEditorStatus();
}

void MainWindow::updateEditorEmptyState()
{
    documentSection_->updateEditorEmptyState();
}

void MainWindow::updateMetadataPageMode()
{
    documentSection_->updateMetadataPageMode();
}

bool MainWindow::deleteDifficultyField(int difficultyId)
{
    return documentSection_->deleteDifficultyField(difficultyId);
}

void MainWindow::updateDifficultyDeleteButton(bool visible)
{
    documentSection_->updateDifficultyDeleteButton(visible);
}

void MainWindow::rebuildFieldSidebar()
{
    documentSection_->rebuildFieldSidebar();
}

void MainWindow::populateMetadataPage()
{
    documentSection_->populateMetadataPage();
}

void MainWindow::populateDifficultyPage(int difficultyId)
{
    documentSection_->populateDifficultyPage(difficultyId);
}

bool MainWindow::switchToMetadataField()
{
    return documentSection_->switchToMetadataField();
}

bool MainWindow::switchToWelcomePage()
{
    return documentSection_->switchToWelcomePage();
}

bool MainWindow::switchToDifficultyField(int difficultyId)
{
    return documentSection_->switchToDifficultyField(difficultyId);
}

void MainWindow::activateInitialField()
{
    documentSection_->activateInitialField();
}

void MainWindow::loadDocument(const SimaiDocument& document)
{
    documentSection_->loadDocument(document);
}

void MainWindow::clearTimelineAndPreview()
{
    documentSection_->clearTimelineAndPreview();
}
