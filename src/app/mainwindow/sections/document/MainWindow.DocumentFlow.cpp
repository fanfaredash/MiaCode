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

#define document_ state_.document_
#define editorCursorLabel_ ui_.editorCursorLabel_
#define outlineList_ ui_.outlineList_
#define bottomTabs_ ui_.bottomTabs_
#define difficultyDesignerEdit_ ui_.difficultyDesignerEdit_
#define difficultyLevelEdit_ ui_.difficultyLevelEdit_
#define activeOutlineKey_ state_.activeOutlineKey_
#define editorValidationSummaryWidget_ ui_.editorValidationSummaryWidget_
#define editorContextLabel_ ui_.editorContextLabel_
#define currentFilePath_ state_.currentFilePath_
#define metadataExtraEdit_ ui_.metadataExtraEdit_
#define currentFieldDirty_ state_.currentFieldDirty_
#define activeDifficultyId_ state_.activeDifficultyId_
#define deletedDifficultyUndoState_ state_.deletedDifficultyUndoState_
#define editorHeaderWidget_ ui_.editorHeaderWidget_
#define titleEdit_ ui_.titleEdit_
#define editorDifficultyControls_ ui_.editorDifficultyControls_
#define firstEdit_ ui_.firstEdit_
#define deleteDifficultyButton_ ui_.deleteDifficultyButton_
#define timelineView_ ui_.timelineView_
#define designerEdit_ ui_.designerEdit_
#define artistEdit_ ui_.artistEdit_
#define editorWidget_ ui_.editorWidget_
#define editorStack_ ui_.editorStack_
#define pausePreviewAction_ ui_.pausePreviewAction_
#define previewLeftColumn_ ui_.previewLeftColumn_
#define documentDirty_ state_.documentDirty_
#define startupRestorePending_ state_.startupRestorePending_
#define suppressTextDirtyTracking_ state_.suppressTextDirtyTracking_
#define lastSessionFilePath_ state_.lastSessionFilePath_
#define pausePreviewButton_ ui_.pausePreviewButton_
#define pendingPreviewPlaybackRevision_ state_.pendingPreviewPlaybackRevision_
#define pendingPreviewPlaybackDifficultyId_ state_.pendingPreviewPlaybackDifficultyId_
#define pendingPreviewPlaybackSecond_ state_.pendingPreviewPlaybackSecond_
#define difficultyLevelLabel_ ui_.difficultyLevelLabel_
#define welcomePage_ ui_.welcomePage_
#define startupRestoreGeneration_ state_.startupRestoreGeneration_
#define pendingPreviewPlaybackResumeFromPause_ state_.pendingPreviewPlaybackResumeFromPause_
#define difficultyDesignerLabel_ ui_.difficultyDesignerLabel_
#define editorBatchTransformControls_ ui_.editorBatchTransformControls_
#define qtPreviewPlaying_ state_.qtPreviewPlaying_
#define pendingPreviewPlaybackStart_ state_.pendingPreviewPlaybackStart_
#define projectLastOpenedDifficultyId_ state_.projectLastOpenedDifficultyId_
#define editorValidationWarningIconLabel_ ui_.editorValidationWarningIconLabel_
#define workspaceSplitter_ ui_.workspaceSplitter_
#define editorValidationWarningCountLabel_ ui_.editorValidationWarningCountLabel_
#define autosaveLastContentSignature_ state_.autosaveLastContentSignature_
#define editorValidationErrorCountLabel_ ui_.editorValidationErrorCountLabel_
#define editorValidationErrorIconLabel_ ui_.editorValidationErrorIconLabel_
#define editorValidationMuriCountLabel_ ui_.editorValidationMuriCountLabel_
#define editorValidationMuriIconLabel_ ui_.editorValidationMuriIconLabel_
#define muriAnalysisReport_ state_.muriAnalysisReport_
#define previewCanvas_ state_.previewCanvas_
#define lastPreviewNoteMarkerSignature_ state_.lastPreviewNoteMarkerSignature_
#define currentEncoding_ state_.currentEncoding_
#define timelineAnalysisIdleTimer_ ui_.timelineAnalysisIdleTimer_
#define previewSfxRuntime_ state_.previewSfxRuntime_
#define editorLineSpacingFactor_ state_.editorLineSpacingFactor_
#define chartPage_ ui_.chartPage_
#define metadataEmptyHintLabel_ ui_.metadataEmptyHintLabel_
#define editorEmptyStateLabel_ ui_.editorEmptyStateLabel_
#define previewWarmupPool_ state_.previewWarmupPool_
#define metadataCard_ ui_.metadataCard_
#define metadataPage_ ui_.metadataPage_
#define transformRotate45CounterClockwiseAction_ ui_.transformRotate45CounterClockwiseAction_
#define transformRotate180Action_ ui_.transformRotate180Action_
#define transformMirrorUpDownAction_ ui_.transformMirrorUpDownAction_
#define transformRotate45ClockwiseAction_ ui_.transformRotate45ClockwiseAction_
#define transformToggleExAction_ ui_.transformToggleExAction_
#define transformToggleBreakAction_ ui_.transformToggleBreakAction_
#define normalizeWholeChartAction_ ui_.normalizeWholeChartAction_
#define validateAction_ ui_.validateAction_
#define validationCacheByDifficulty_ state_.validationCacheByDifficulty_
#define editorTextFontPointSize_ state_.editorTextFontPointSize_
#define previewFullscreenActive_ state_.previewFullscreenActive_
#define transformMirrorLeftRightAction_ ui_.transformMirrorLeftRightAction_
#define stopPreviewAction_ ui_.stopPreviewAction_
#define exportVideoAction_ ui_.exportVideoAction_
#define exportVideoButton_ ui_.exportVideoButton_
#define syntaxCheckButton_ ui_.syntaxCheckButton_
#define transformRotate45CounterClockwiseButton_ ui_.transformRotate45CounterClockwiseButton_
#define transformRotate180Button_ ui_.transformRotate180Button_
#define transformMirrorUpDownButton_ ui_.transformMirrorUpDownButton_
#define transformRotate45ClockwiseButton_ ui_.transformRotate45ClockwiseButton_
#define autosaveReferenceContentSignature_ state_.autosaveReferenceContentSignature_
#define transformMirrorLeftRightButton_ ui_.transformMirrorLeftRightButton_
#define transformToggleFireworkAction_ ui_.transformToggleFireworkAction_
#define stopPreviewButton_ ui_.stopPreviewButton_
#define transformRandomRotateAction_ ui_.transformRandomRotateAction_
#define previewTrackDurationSeconds_ state_.previewTrackDurationSeconds_
#define pendingDeferredMuriUiRefresh_ state_.pendingDeferredMuriUiRefresh_
#define lastTimelineParseResult_ state_.lastTimelineParseResult_
#define muriAnalysisReportNoteMarkerSignature_ state_.muriAnalysisReportNoteMarkerSignature_
#define pendingDeferredValidationUiRefresh_ state_.pendingDeferredValidationUiRefresh_
#define qtPreviewTimelineDirty_ state_.qtPreviewTimelineDirty_
#define qtPreviewPlaybackReturnSecond_ state_.qtPreviewPlaybackReturnSecond_
#define qtPreviewPlaybackEndSecond_ state_.qtPreviewPlaybackEndSecond_
#define pendingTimelineSlowRefresh_ state_.pendingTimelineSlowRefresh_
#define qtPreviewTimelineStartSecond_ state_.qtPreviewTimelineStartSecond_
#define qtPreviewPendingTimelineSecond_ state_.qtPreviewPendingTimelineSecond_
#define qtPreviewPendingTimelineCenterView_ state_.qtPreviewPendingTimelineCenterView_
#define qtPreviewLastTimelineSecond_ state_.qtPreviewLastTimelineSecond_
#define lastTimelineParseTimingMetadata_ state_.lastTimelineParseTimingMetadata_
#define pendingTimelineAnalysisRefresh_ state_.pendingTimelineAnalysisRefresh_
#define timelineQuickModel_ state_.timelineQuickModel_
#define timelineSlowRunningRevision_ state_.timelineSlowRunningRevision_
#define timelineSlowRequestedRevision_ state_.timelineSlowRequestedRevision_
#define autoRestoreLastSessionFile_ state_.autoRestoreLastSessionFile_
#define outlineDock_ ui_.outlineDock_
#define latestTimelinePreviewSnapshotReady_ state_.latestTimelinePreviewSnapshotReady_
#define latestTimelinePreviewRevision_ state_.latestTimelinePreviewRevision_
#define lastTimelineParseChartText_ state_.lastTimelineParseChartText_
#define lastTimelineParseDifficultyId_ state_.lastTimelineParseDifficultyId_
#define timelineAnalysisRunningRevision_ state_.timelineAnalysisRunningRevision_
#define timelineAnalysisRequestedRevision_ state_.timelineAnalysisRequestedRevision_
#define latestTimelineNoteMarkerSignature_ state_.latestTimelineNoteMarkerSignature_
#define latestTimelineNoteMarkers_ state_.latestTimelineNoteMarkers_

#define statusBar() owner_.statusBar()
#define hasActiveDifficulty() owner_.hasActiveDifficulty()
#define activeDifficultyId() owner_.activeDifficultyId()
#define editorText() owner_.editorText()
#define setCurrentFilePath(...) owner_.setCurrentFilePath(__VA_ARGS__)
#define updateWindowTitle() owner_.updateWindowTitle()
#define resolveInitialOpenDirectory() owner_.resolveInitialOpenDirectory()
#define setLastOpenDirectory(...) owner_.setLastOpenDirectory(__VA_ARGS__)
#define parsedFirstSeconds(...) owner_.parsedFirstSeconds(__VA_ARGS__)
#define logTopLevelWindowSnapshot(...) owner_.logTopLevelWindowSnapshot(__VA_ARGS__)
#define logWindowGeometryDebug(...) owner_.logWindowGeometryDebug(__VA_ARGS__)
#define saveProjectRenderState() owner_.saveProjectRenderState()
#define cacheWorkspaceLayoutSizes() owner_.cacheWorkspaceLayoutSizes()
#define setValidationTabVisible(...) owner_.setValidationTabVisible(__VA_ARGS__)
#define refreshValidationPanelForActiveField() owner_.refreshValidationPanelForActiveField()
#define refreshMuriDiagnosticsPanel() owner_.refreshMuriDiagnosticsPanel()
#define updateEditorValidationSummary() owner_.updateEditorValidationSummary()
#define clearValidationCache() owner_.clearValidationCache()
#define clearValidationDecorations() owner_.clearValidationDecorations()
#define applyWaveformData(...) owner_.applyWaveformData(__VA_ARGS__)
#define refreshWaveformCache(...) owner_.refreshWaveformCache(__VA_ARGS__)
#define refreshTimelineMetadata() owner_.refreshTimelineMetadata()
#define scheduleTimelineRefresh() owner_.scheduleTimelineRefresh()
#define refreshLayoutAfterPageSwitch() owner_.refreshLayoutAfterPageSwitch()
#define updatePreviewWorkspaceLayout() owner_.updatePreviewWorkspaceLayout()
#define refreshQuickShellRehostedWidgetParent(...) owner_.refreshQuickShellRehostedWidgetParent(__VA_ARGS__)
#define stopQtPreviewPlayback(...) owner_.stopQtPreviewPlayback(__VA_ARGS__)
#define clearPreviewFollowDecoration() owner_.clearPreviewFollowDecoration()
#define clearPreviewObjectStats() owner_.clearPreviewObjectStats()
#define clearMuriDiagnostics() owner_.clearMuriDiagnostics()
#define clearPreviewStageMediaRoute() owner_.clearPreviewStageMediaRoute()
#define updatePreviewSliderRange() owner_.updatePreviewSliderRange()
#define updatePreviewSliderPosition(...) owner_.updatePreviewSliderPosition(__VA_ARGS__)
#define setWindowModified(...) owner_.setWindowModified(__VA_ARGS__)
#define style() owner_.style()

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
    if (!documentDirty_) {
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
    if (!currentFieldDirty_) {
        return true;
    }

    const QString fieldName = hasActiveDifficulty()
        ? SimaiDocument::difficultyName(activeDifficultyId_)
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
        currentFieldDirty_ = false;
        updateDirtyState();
        return true;
    }
    return false;
}

bool MainWindow::DocumentSection::applyCurrentFieldToDocument()
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

void MainWindow::DocumentSection::onNewFile()
{
    if (!maybeSaveBeforeContinue()) {
        return;
    }

    const QString targetDirectory = QFileDialog::getExistingDirectory(
        &owner_,
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
    clearValidationCache();
    currentEncoding_ = TextEncoding::Utf8;
    setCurrentFilePath(targetPath);
    statusBar()->showMessage(QString("Created: %1").arg(targetPath));
}

void MainWindow::DocumentSection::onOpenFile()
{
    logTopLevelWindowSnapshot("open_file_flow/begin");
    const bool canContinue = maybeSaveBeforeContinue();
    if (!canContinue) {
        logTopLevelWindowSnapshot("open_file_flow/cancelled_before_dialog");
        return;
    }

    logWindowGeometryDebug("open_file_before_dialog");
    logTopLevelWindowSnapshot("open_file_before_dialog");
    const QString path = QFileDialog::getOpenFileName(
        &owner_,
        QStringLiteral("Open simai file"),
        resolveInitialOpenDirectory(),
        QStringLiteral("Simai (*.txt *.simai);;All Files (*.*)")
    );
    logWindowGeometryDebug("open_file_after_dialog", QString("selected_empty=%1").arg(path.isEmpty() ? 1 : 0));
    logTopLevelWindowSnapshot("open_file_after_dialog");
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

void MainWindow::DocumentSection::scheduleStartupRestoreLastSessionFile()
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
    QPointer<MainWindow> guard(&owner_);
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
    if (!startupRestorePending_) {
        return;
    }
    startupRestorePending_ = false;
    ++startupRestoreGeneration_;
}

void MainWindow::DocumentSection::applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared)
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
    owner_.scheduleDeferredQuickShellStartupStageMediaLoadIfReady();
}

void MainWindow::DocumentSection::applyOpenedDocumentState(
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

void MainWindow::DocumentSection::resetAutosaveState(const QString& referenceText)
{
    autosaveReferenceContentSignature_ = autosaveContentSignature(referenceText);
    autosaveLastContentSignature_.clear();
}

QString MainWindow::DocumentSection::resolveAutosaveDirectoryPath() const
{
    if (currentFilePath_.isEmpty()) {
        return QString();
    }

    const QFileInfo currentFileInfo(currentFilePath_);
    const QString projectDirectoryPath = currentFileInfo.absolutePath();
    if (projectDirectoryPath.isEmpty()) {
        return QString();
    }

    return QDir(projectDirectoryPath).filePath(QStringLiteral(".autosave"));
}

QString MainWindow::DocumentSection::currentDocumentTextForAutosave() const
{
    SimaiDocument snapshot = document_;
    if (hasActiveDifficulty()) {
        SimaiDifficultyData& difficultyData = snapshot.ensureDifficulty(activeDifficultyId_);
        difficultyData.level = difficultyLevelEdit_ != nullptr ? difficultyLevelEdit_->text() : QString();
        difficultyData.designer = difficultyDesignerEdit_ != nullptr ? difficultyDesignerEdit_->text() : QString();
        difficultyData.chart = editorText();
    } else if (activeOutlineKey_ == QLatin1String("metadata")) {
        snapshot.title = titleEdit_ != nullptr ? titleEdit_->text() : QString();
        snapshot.artist = artistEdit_ != nullptr ? artistEdit_->text() : QString();
        snapshot.first = firstEdit_ != nullptr ? firstEdit_->text() : QString();
        snapshot.designer = designerEdit_ != nullptr ? designerEdit_->text() : QString();
        snapshot.extraFields = SimaiDocument::parseRawFields(
            metadataExtraEdit_ != nullptr ? metadataExtraEdit_->toPlainText() : QString(),
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
    if (currentFilePath_.isEmpty()) {
        return;
    }

    const QString snapshotText = currentDocumentTextForAutosave();
    const QByteArray snapshotSignature = autosaveContentSignature(snapshotText);
    if (snapshotSignature == autosaveReferenceContentSignature_
        || snapshotSignature == autosaveLastContentSignature_) {
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

    QString autosaveBaseName = QFileInfo(currentFilePath_).completeBaseName();
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

    autosaveLastContentSignature_ = snapshotSignature;
    pruneAutosaveFiles(autosaveDirectoryPath);
}

bool MainWindow::DocumentSection::onSaveFile()
{
    if (currentFilePath_.isEmpty()) {
        return onSaveFileAs();
    }
    return saveToPath(currentFilePath_);
}

bool MainWindow::DocumentSection::onSaveFileAs()
{
    logTopLevelWindowSnapshot("save_file_as_dialog/begin");
    logWindowGeometryDebug("save_file_as_before_dialog");
    const QString path = QFileDialog::getSaveFileName(
        &owner_,
        QStringLiteral("Save simai file"),
        currentFilePath_.isEmpty() ? QStringLiteral("chart.txt") : currentFilePath_,
        QStringLiteral("Simai (*.txt *.simai);;All Files (*.*)")
    );
    logWindowGeometryDebug("save_file_as_after_dialog", QString("selected_empty=%1").arg(path.isEmpty() ? 1 : 0));
    logTopLevelWindowSnapshot("save_file_as_dialog/after_dialog");
    if (path.isEmpty()) {
        return false;
    }
    setLastOpenDirectory(path);
    return saveToPath(path);
}

bool MainWindow::DocumentSection::saveToPath(const QString& path)
{
    if (!applyCurrentFieldToDocument()) {
        return false;
    }
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    bool firstOk = false;
    (void)parsedFirstSeconds(&firstOk);
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
    const QString serialized = document_.toText();
    if (currentEncoding_ == TextEncoding::System) {
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
    if (normalizedPath != currentFilePath_) {
        setCurrentFilePath(normalizedPath);
    }
    resetAutosaveState(serialized);
    documentDirty_ = false;
    currentFieldDirty_ = false;
    updateDirtyState();
    updateWindowTitle();
    statusBar()->showMessage("Saved: " + QFileInfo(path).fileName());
    return true;
}

bool MainWindow::DocumentSection::applyBatchTransform(const QString& opName, const BatchTransform& transform)
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

bool MainWindow::DocumentSection::applySelectionBatchTransform(const QString& opName, const BatchTransform& transform)
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

std::pair<int, int> MainWindow::DocumentSection::currentCursorLineCol() const
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const QTextCursor cursor = editor->textCursor();
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

bool MainWindow::DocumentSection::currentSelectionRange(int* startPos, int* endPos) const
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

std::pair<int, int> MainWindow::DocumentSection::currentSelectionOrCursorLineCol() const
{
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    QTextCursor cursor = editor->textCursor();
    if (!cursor.hasSelection()) {
        return currentCursorLineCol();
    }
    cursor.setPosition(cursor.selectionStart());
    return {cursor.blockNumber() + 1, cursor.positionInBlock() + 1};
}

void MainWindow::DocumentSection::setMetadataExtraText(const QString& text)
{
    if (metadataExtraEdit_ == nullptr) {
        return;
    }
    const bool previousSuppress = suppressTextDirtyTracking_;
    suppressTextDirtyTracking_ = true;
    QSignalBlocker blocker(metadataExtraEdit_);
    metadataExtraEdit_->setPlainText(text);
    metadataExtraEdit_->document()->clearUndoRedoStacks();
    metadataExtraEdit_->document()->setModified(false);
    applyBlockSpacingToTextEdit(metadataExtraEdit_, blockSpacingPixelsForPointSize(editorTextFontPointSize_, editorLineSpacingFactor_));
    suppressTextDirtyTracking_ = previousSuppress;
}

void MainWindow::DocumentSection::setEditorText(const QString& text)
{
    const bool previousSuppress = suppressTextDirtyTracking_;
    suppressTextDirtyTracking_ = true;
    QSignalBlocker blocker(editorWidget_);
    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
    const int blockSpacingPixels = blockSpacingPixelsForPointSize(editorTextFontPointSize_, editorLineSpacingFactor_);
    editor->setPlainText(text);
    editor->setBlockSpacingPixels(blockSpacingPixels);
    editor->document()->clearUndoRedoStacks();
    editor->document()->setModified(false);
    // QSignalBlocker suppresses blockCountChanged, so force line-number gutter recompute.
    editor->refreshLineNumberAreaLayout();
    suppressTextDirtyTracking_ = previousSuppress;
}

void MainWindow::DocumentSection::updatePauseButtonAppearance()
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

void MainWindow::DocumentSection::updateDirtyState()
{
    setWindowModified(documentDirty_ || currentFieldDirty_);
    updateWindowTitle();
}

bool MainWindow::DocumentSection::currentFieldHasUndoChanges() const
{
    if (hasActiveDifficulty()) {
        const bool levelDirty = difficultyLevelEdit_ != nullptr && difficultyLevelEdit_->isUndoAvailable();
        const bool designerDirty = difficultyDesignerEdit_ != nullptr && difficultyDesignerEdit_->isUndoAvailable();
        bool chartDirty = false;
        if (auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
            editor != nullptr && editor->document() != nullptr) {
            chartDirty = editor->document()->isUndoAvailable();
        }
        return levelDirty || designerDirty || chartDirty;
    }

    if (activeOutlineKey_ == QLatin1String("metadata")) {
        const bool titleDirty = titleEdit_ != nullptr && titleEdit_->isUndoAvailable();
        const bool artistDirty = artistEdit_ != nullptr && artistEdit_->isUndoAvailable();
        const bool firstDirty = firstEdit_ != nullptr && firstEdit_->isUndoAvailable();
        const bool designerDirty = designerEdit_ != nullptr && designerEdit_->isUndoAvailable();
        bool extraDirty = false;
        if (metadataExtraEdit_ != nullptr && metadataExtraEdit_->document() != nullptr) {
            extraDirty = metadataExtraEdit_->document()->isUndoAvailable();
        }
        return titleDirty || artistDirty || firstDirty || designerDirty || extraDirty;
    }

    return false;
}

void MainWindow::DocumentSection::refreshCurrentFieldDirtyState()
{
    currentFieldDirty_ = currentFieldHasUndoChanges();
    updateDirtyState();
}

void MainWindow::DocumentSection::markCurrentFieldDirty()
{
    refreshCurrentFieldDirtyState();
}

void MainWindow::DocumentSection::clearDeletedDifficultyUndoState()
{
    deletedDifficultyUndoState_ = DeletedDifficultyUndoState{};
}

bool MainWindow::DocumentSection::undoDeletedDifficultyField()
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
        QTimer::singleShot(0, &owner_, [this]() { refreshLayoutAfterPageSwitch(); });
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

void MainWindow::DocumentSection::updateEditorHeader()
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

void MainWindow::DocumentSection::updateDifficultyScopedActionStates()
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

void MainWindow::DocumentSection::updateEditorHeaderLayoutMode()
{
    if (editorHeaderWidget_ == nullptr || editorCursorLabel_ == nullptr || editorContextLabel_ == nullptr) {
        return;
    }

    if (!hasActiveDifficulty()) {
        editorCursorLabel_->setVisible(false);
        if (editorValidationSummaryWidget_ != nullptr) {
            editorValidationSummaryWidget_->setVisible(false);
        }
        syncEditorHeaderMinimumWidth();
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
            width += qMax(icon->sizeHint().width(), icon->minimumWidth());
        }
        if (count != nullptr) {
            if (width > 0) {
                width += spacing;
            }
            width += qMax(count->sizeHint().width(), count->minimumWidth());
        }
        return width;
    };
    int summaryFullWidth = 0;
    int summaryVisibleGroups = 0;
    for (const int groupWidth : {
             summaryGroupFullWidth(editorValidationMuriIconLabel_, editorValidationMuriCountLabel_, 4),
             summaryGroupFullWidth(editorValidationWarningIconLabel_, editorValidationWarningCountLabel_, 3),
             summaryGroupFullWidth(editorValidationErrorIconLabel_, editorValidationErrorCountLabel_, 6)}) {
        if (groupWidth <= 0) {
            continue;
        }
        if (summaryVisibleGroups > 0) {
            summaryFullWidth += 8;
        }
        summaryFullWidth += groupWidth;
        ++summaryVisibleGroups;
    }

    if (difficultyLevelLabel_ != nullptr) {
        difficultyLevelLabel_->setVisible(true);
    }
    if (difficultyDesignerLabel_ != nullptr) {
        difficultyDesignerLabel_->setVisible(true);
    }
    if (difficultyLevelEdit_ != nullptr) {
        difficultyLevelEdit_->setFixedWidth(48);
        difficultyLevelEdit_->setVisible(true);
    }
    if (difficultyDesignerEdit_ != nullptr) {
        difficultyDesignerEdit_->setFixedWidth(96);
        difficultyDesignerEdit_->setVisible(true);
    }
    if (editorDifficultyControls_ != nullptr) {
        editorDifficultyControls_->setVisible(true);
    }

    if (editorValidationSummaryWidget_ != nullptr) {
        const auto applySummaryVisibility = [](QLabel* icon, QLabel* count) {
            const bool hasContent = (icon != nullptr && icon->property("hasContent").toBool())
                || (count != nullptr && count->property("hasContent").toBool());
            if (icon != nullptr) {
                icon->setVisible(hasContent);
            }
            if (count != nullptr) {
                count->setVisible(hasContent);
            }
        };
        applySummaryVisibility(editorValidationErrorIconLabel_, editorValidationErrorCountLabel_);
        applySummaryVisibility(editorValidationWarningIconLabel_, editorValidationWarningCountLabel_);
        applySummaryVisibility(editorValidationMuriIconLabel_, editorValidationMuriCountLabel_);
        const int reservedSummaryWidth = qMax(
            editorValidationSummaryWidget_->minimumSizeHint().width(),
            editorValidationSummaryWidget_->sizeHint().width()
        );
        editorValidationSummaryWidget_->setVisible(true);
        editorValidationSummaryWidget_->setMinimumWidth(reservedSummaryWidth);
        editorValidationSummaryWidget_->setMaximumWidth(reservedSummaryWidth);
        editorValidationSummaryWidget_->adjustSize();
    }

    const auto [line, col] = currentCursorLineCol();
    const QString cursorText = UiText::isChineseUi()
        ? QStringLiteral("%1行 %2列").arg(line).arg(col)
        : QStringLiteral("Ln %1, Col %2").arg(line).arg(col);
    editorCursorLabel_->setText(cursorText);
    editorCursorLabel_->setFixedWidth(QFontMetrics(editorCursorLabel_->font()).horizontalAdvance(cursorText) + 10);
    editorCursorLabel_->setVisible(true);
    const QString correctedCursorText = UiText::isChineseUi()
        ? QStringLiteral("%1行 %2列").arg(line).arg(col)
        : QStringLiteral("Ln %1, Col %2").arg(line).arg(col);
    const QString correctedCursorWidthTemplate = UiText::isChineseUi()
        ? QStringLiteral("9999行 9999列")
        : QStringLiteral("Ln 9999, Col 9999");
    editorCursorLabel_->setText(correctedCursorText);
    editorCursorLabel_->setFixedWidth(
        QFontMetrics(editorCursorLabel_->font()).horizontalAdvance(correctedCursorWidthTemplate) + 10);

    if (QLayout* headerLayout = editorHeaderWidget_->layout(); headerLayout != nullptr) {
        headerLayout->activate();
    }
    syncEditorHeaderMinimumWidth();
    return;

    struct HeaderLayoutCandidate {
        bool showLevelControls = true;
        bool showDesignerControls = true;
        bool showSummary = false;
        bool showSummaryCounts = false;
        bool showCursor = false;
        bool compactCursor = false;
    };

    const auto applyCandidate = [this, summaryHasContent](const HeaderLayoutCandidate& candidate) {
        constexpr int kLevelWidth = 48;
        constexpr int kDesignerWidth = 96;

        if (difficultyLevelLabel_ != nullptr) {
            difficultyLevelLabel_->setVisible(candidate.showLevelControls);
        }
        if (difficultyDesignerLabel_ != nullptr) {
            difficultyDesignerLabel_->setVisible(candidate.showDesignerControls);
        }
        if (difficultyLevelEdit_ != nullptr) {
            difficultyLevelEdit_->setFixedWidth(kLevelWidth);
            difficultyLevelEdit_->setVisible(candidate.showLevelControls);
        }
        if (difficultyDesignerEdit_ != nullptr) {
            difficultyDesignerEdit_->setFixedWidth(kDesignerWidth);
            difficultyDesignerEdit_->setVisible(candidate.showDesignerControls);
        }
        if (editorDifficultyControls_ != nullptr) {
            editorDifficultyControls_->setVisible(candidate.showLevelControls || candidate.showDesignerControls);
        }

        if (editorValidationSummaryWidget_ != nullptr) {
            const bool showSummary = summaryHasContent && candidate.showSummary;
            editorValidationSummaryWidget_->setVisible(showSummary);
            const auto applySummaryVisibility = [showSummary, candidate](QLabel* icon, QLabel* count) {
                const bool hasContent = (icon != nullptr && icon->property("hasContent").toBool())
                    || (count != nullptr && count->property("hasContent").toBool());
                if (icon != nullptr) {
                    icon->setVisible(showSummary && hasContent);
                }
                if (count != nullptr) {
                    count->setVisible(showSummary && candidate.showSummaryCounts && hasContent);
                }
            };
            applySummaryVisibility(editorValidationErrorIconLabel_, editorValidationErrorCountLabel_);
            applySummaryVisibility(editorValidationWarningIconLabel_, editorValidationWarningCountLabel_);
            applySummaryVisibility(editorValidationMuriIconLabel_, editorValidationMuriCountLabel_);
            editorValidationSummaryWidget_->adjustSize();
        }
    };

    const QVector<HeaderLayoutCandidate> candidates = summaryHasContent
        ? QVector<HeaderLayoutCandidate>{
              HeaderLayoutCandidate{true, true, true, true, true, false},
              HeaderLayoutCandidate{true, true, true, false, true, false},
              HeaderLayoutCandidate{true, true, false, false, true, false},
              HeaderLayoutCandidate{true, true, false, false, true, true},
              HeaderLayoutCandidate{true, true, false, false, false, false},
              HeaderLayoutCandidate{true, false, false, false, false, false},
              HeaderLayoutCandidate{false, false, false, false, false, false},
          }
        : QVector<HeaderLayoutCandidate>{
              HeaderLayoutCandidate{true, true, false, false, true, false},
              HeaderLayoutCandidate{true, true, false, false, true, true},
              HeaderLayoutCandidate{true, true, false, false, false, false},
              HeaderLayoutCandidate{true, false, false, false, false, false},
              HeaderLayoutCandidate{false, false, false, false, false, false},
          };

    for (int index = 0; index < candidates.size(); ++index) {
        const HeaderLayoutCandidate& candidate = candidates.at(index);
        applyCandidate(candidate);

        if (candidate.showCursor) {
            const auto [line, col] = currentCursorLineCol();
            if (candidate.compactCursor) {
                editorCursorLabel_->setText(QStringLiteral("%1:%2").arg(line).arg(col));
            } else if (UiText::isChineseUi()) {
                editorCursorLabel_->setText(QStringLiteral("%1琛?%2鍒?").arg(line).arg(col));
            } else {
                editorCursorLabel_->setText(QStringLiteral("Ln %1, Col %2").arg(line).arg(col));
            }
            editorCursorLabel_->setFixedWidth(
                QFontMetrics(editorCursorLabel_->font()).horizontalAdvance(editorCursorLabel_->text()) + 10);
        }
        editorCursorLabel_->setVisible(candidate.showCursor);

        if (QLayout* headerLayout = editorHeaderWidget_->layout(); headerLayout != nullptr) {
            headerLayout->activate();
        }
        const int requiredWidth = editorHeaderWidget_->minimumSizeHint().width();
        if (headerWidth >= requiredWidth || index == candidates.size() - 1) {
            break;
        }
    }

    const bool showCursor = editorCursorLabel_->isVisible();
    const bool compactCursor = showCursor
        && editorCursorLabel_->text().contains(QLatin1Char(':'))
        && !editorCursorLabel_->text().contains(QLatin1Char(','));
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
    syncEditorHeaderMinimumWidth();
}

void MainWindow::DocumentSection::syncEditorHeaderMinimumWidth()
{
    if (editorHeaderWidget_ == nullptr) {
        return;
    }

    int headerMinimumWidth = 0;
    if (QLayout* headerLayout = editorHeaderWidget_->layout(); headerLayout != nullptr) {
        headerLayout->activate();
        const auto widgetMinimumWidth = [](const QWidget* widget) {
            if (widget == nullptr || widget->isHidden()) {
                return 0;
            }
            return qMax(widget->minimumSizeHint().width(), widget->sizeHint().width());
        };
        const QMargins margins = headerLayout->contentsMargins();
        headerMinimumWidth = margins.left() + margins.right();
        int visibleSectionCount = 0;
        const auto addSectionWidth = [&](int width) {
            if (width <= 0) {
                return;
            }
            if (visibleSectionCount > 0) {
                headerMinimumWidth += qMax(0, headerLayout->spacing());
            }
            headerMinimumWidth += width;
            ++visibleSectionCount;
        };
        addSectionWidth(widgetMinimumWidth(editorContextLabel_));
        addSectionWidth(widgetMinimumWidth(editorDifficultyControls_));
        addSectionWidth(widgetMinimumWidth(editorValidationSummaryWidget_));
        addSectionWidth(widgetMinimumWidth(editorCursorLabel_ != nullptr ? editorCursorLabel_->parentWidget() : nullptr));
        headerMinimumWidth = qMax(headerMinimumWidth, margins.left() + margins.right());
        editorHeaderWidget_->setMinimumWidth(headerMinimumWidth);
    }
    editorHeaderWidget_->updateGeometry();

    if (previewLeftColumn_ != nullptr) {
        const int baseMinimumWidth = qMax(0, previewLeftColumn_->property("baseMinimumWidth").toInt());
        const int previousMinimumWidth = previewLeftColumn_->minimumWidth();
        if (QLayout* leftColumnLayout = previewLeftColumn_->layout(); leftColumnLayout != nullptr) {
            leftColumnLayout->activate();
        }
        const int nextMinimumWidth = qMax(
            baseMinimumWidth,
            headerMinimumWidth
        );
        previewLeftColumn_->setMinimumWidth(nextMinimumWidth);
        previewLeftColumn_->updateGeometry();
        if (nextMinimumWidth != previousMinimumWidth && workspaceSplitter_ != nullptr) {
            updatePreviewWorkspaceLayout();
        }
    }

    refreshQuickShellRehostedWidgetParent(outlineDock_);
    refreshQuickShellRehostedWidgetParent(previewLeftColumn_);

    if (workspaceSplitter_ != nullptr) {
        workspaceSplitter_->updateGeometry();
    }
}

void MainWindow::DocumentSection::updateEditorStatus()
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

void MainWindow::DocumentSection::updateEditorEmptyState()
{
    if (editorEmptyStateLabel_ != nullptr) {
        editorEmptyStateLabel_->hide();
    }
}

void MainWindow::DocumentSection::updateMetadataPageMode()
{
    if (metadataCard_ == nullptr || metadataEmptyHintLabel_ == nullptr) {
        return;
    }
    metadataCard_->setVisible(true);
    metadataEmptyHintLabel_->hide();
}

bool MainWindow::DocumentSection::deleteDifficultyField(int difficultyId)
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
            &owner_,
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
        QTimer::singleShot(0, &owner_, [this]() { refreshLayoutAfterPageSwitch(); });
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

void MainWindow::DocumentSection::updateDifficultyDeleteButton(bool visible)
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

void MainWindow::DocumentSection::rebuildFieldSidebar()
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
            ? QStringLiteral("打开工具箱：无理检测 / 谱面整理 / 官谱镜像站")
            : QStringLiteral("Open toolbox: Muri Check / Format Chart / Official Chart Mirror")
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

void MainWindow::DocumentSection::populateMetadataPage()
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

void MainWindow::DocumentSection::populateDifficultyPage(int difficultyId)
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

bool MainWindow::DocumentSection::switchToMetadataField()
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
        QTimer::singleShot(0, &owner_, [this]() { refreshLayoutAfterPageSwitch(); });
    return true;
}

bool MainWindow::DocumentSection::switchToWelcomePage()
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
        QTimer::singleShot(0, &owner_, [this]() { refreshLayoutAfterPageSwitch(); });
    return true;
}

bool MainWindow::DocumentSection::switchToDifficultyField(int difficultyId)
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
        QTimer::singleShot(0, &owner_, [this]() { refreshLayoutAfterPageSwitch(); });
    return true;
}

void MainWindow::DocumentSection::activateInitialField()
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

void MainWindow::DocumentSection::loadDocument(const SimaiDocument& document)
{
    clearDeletedDifficultyUndoState();
    document_ = document;
    resetAutosaveState(document_.toText());
    documentDirty_ = false;
    currentFieldDirty_ = false;
    activeDifficultyId_ = 0;
    activeOutlineKey_ = document_.difficultyIds().isEmpty() ? QStringLiteral("welcome") : QStringLiteral("chart");
    activateInitialField();
    updateMetadataPageMode();
    updateDirtyState();
    updateWindowTitle();
}

void MainWindow::DocumentSection::clearTimelineAndPreview()
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
    clearPreviewStageMediaRoute();
    updatePreviewSliderRange();
    updatePreviewSliderPosition(0.0);
}

#undef document_
#undef editorCursorLabel_
#undef outlineList_
#undef bottomTabs_
#undef difficultyDesignerEdit_
#undef difficultyLevelEdit_
#undef activeOutlineKey_
#undef editorValidationSummaryWidget_
#undef editorContextLabel_
#undef currentFilePath_
#undef metadataExtraEdit_
#undef currentFieldDirty_
#undef activeDifficultyId_
#undef deletedDifficultyUndoState_
#undef editorHeaderWidget_
#undef titleEdit_
#undef editorDifficultyControls_
#undef firstEdit_
#undef deleteDifficultyButton_
#undef timelineView_
#undef designerEdit_
#undef artistEdit_
#undef editorWidget_
#undef editorStack_
#undef pausePreviewAction_
#undef previewLeftColumn_
#undef documentDirty_
#undef startupRestorePending_
#undef suppressTextDirtyTracking_
#undef lastSessionFilePath_
#undef pausePreviewButton_
#undef pendingPreviewPlaybackRevision_
#undef pendingPreviewPlaybackDifficultyId_
#undef pendingPreviewPlaybackSecond_
#undef difficultyLevelLabel_
#undef welcomePage_
#undef startupRestoreGeneration_
#undef pendingPreviewPlaybackResumeFromPause_
#undef difficultyDesignerLabel_
#undef editorBatchTransformControls_
#undef qtPreviewPlaying_
#undef pendingPreviewPlaybackStart_
#undef projectLastOpenedDifficultyId_
#undef editorValidationWarningIconLabel_
#undef workspaceSplitter_
#undef editorValidationWarningCountLabel_
#undef autosaveLastContentSignature_
#undef editorValidationErrorCountLabel_
#undef editorValidationErrorIconLabel_
#undef editorValidationMuriCountLabel_
#undef editorValidationMuriIconLabel_
#undef muriAnalysisReport_
#undef previewCanvas_
#undef lastPreviewNoteMarkerSignature_
#undef currentEncoding_
#undef timelineAnalysisIdleTimer_
#undef previewSfxRuntime_
#undef editorLineSpacingFactor_
#undef chartPage_
#undef metadataEmptyHintLabel_
#undef editorEmptyStateLabel_
#undef previewWarmupPool_
#undef metadataCard_
#undef metadataPage_
#undef transformRotate45CounterClockwiseAction_
#undef transformRotate180Action_
#undef transformMirrorUpDownAction_
#undef transformRotate45ClockwiseAction_
#undef transformToggleExAction_
#undef transformToggleBreakAction_
#undef normalizeWholeChartAction_
#undef validateAction_
#undef validationCacheByDifficulty_
#undef editorTextFontPointSize_
#undef previewFullscreenActive_
#undef transformMirrorLeftRightAction_
#undef stopPreviewAction_
#undef exportVideoAction_
#undef exportVideoButton_
#undef syntaxCheckButton_
#undef transformRotate45CounterClockwiseButton_
#undef transformRotate180Button_
#undef transformMirrorUpDownButton_
#undef transformRotate45ClockwiseButton_
#undef autosaveReferenceContentSignature_
#undef transformMirrorLeftRightButton_
#undef transformToggleFireworkAction_
#undef stopPreviewButton_
#undef transformRandomRotateAction_
#undef previewTrackDurationSeconds_
#undef pendingDeferredMuriUiRefresh_
#undef lastTimelineParseResult_
#undef muriAnalysisReportNoteMarkerSignature_
#undef pendingDeferredValidationUiRefresh_
#undef qtPreviewTimelineDirty_
#undef qtPreviewPlaybackReturnSecond_
#undef qtPreviewPlaybackEndSecond_
#undef pendingTimelineSlowRefresh_
#undef qtPreviewTimelineStartSecond_
#undef qtPreviewPendingTimelineSecond_
#undef qtPreviewPendingTimelineCenterView_
#undef qtPreviewLastTimelineSecond_
#undef lastTimelineParseTimingMetadata_
#undef pendingTimelineAnalysisRefresh_
#undef timelineQuickModel_
#undef timelineSlowRunningRevision_
#undef timelineSlowRequestedRevision_
#undef autoRestoreLastSessionFile_
#undef outlineDock_
#undef latestTimelinePreviewSnapshotReady_
#undef latestTimelinePreviewRevision_
#undef lastTimelineParseChartText_
#undef lastTimelineParseDifficultyId_
#undef timelineAnalysisRunningRevision_
#undef timelineAnalysisRequestedRevision_
#undef latestTimelineNoteMarkerSignature_
#undef latestTimelineNoteMarkers_
#undef statusBar
#undef hasActiveDifficulty
#undef activeDifficultyId
#undef editorText
#undef setCurrentFilePath
#undef updateWindowTitle
#undef resolveInitialOpenDirectory
#undef setLastOpenDirectory
#undef parsedFirstSeconds
#undef logTopLevelWindowSnapshot
#undef logWindowGeometryDebug
#undef saveProjectRenderState
#undef cacheWorkspaceLayoutSizes
#undef setValidationTabVisible
#undef refreshValidationPanelForActiveField
#undef refreshMuriDiagnosticsPanel
#undef updateEditorValidationSummary
#undef clearValidationCache
#undef refreshWaveformCache
#undef refreshTimelineMetadata
#undef refreshLayoutAfterPageSwitch
#undef stopQtPreviewPlayback
#undef clearPreviewFollowDecoration
#undef clearPreviewObjectStats
#undef clearMuriDiagnostics
#undef clearPreviewStageMediaRoute
#undef updatePreviewSliderRange
#undef updatePreviewSliderPosition

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
