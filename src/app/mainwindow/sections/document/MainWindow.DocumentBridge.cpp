#include "MainWindow.DocumentSection.h"
#include "app/mainwindow/MainWindowShared.h"


#include <QtCore>
#include <QtWidgets>

#include <utility>

QString MainWindow::DocumentSection::documentField(MainWindow::DocumentField field) const
{
    switch (field) {
    case MainWindow::DocumentField::Title:
        return state_.document_.title;
    case MainWindow::DocumentField::Artist:
        return state_.document_.artist;
    case MainWindow::DocumentField::First:
        return state_.document_.first;
    case MainWindow::DocumentField::Designer:
        return state_.document_.designer;
    case MainWindow::DocumentField::VideoPath:
        return state_.document_.videoPath;
    case MainWindow::DocumentField::ExtraText:
        return SimaiDocument::serializeRawFields(state_.document_.extraFields);
    }
    return {};
}

QString MainWindow::DocumentSection::difficultyField(
    int difficultyId,
    MainWindow::DifficultyField field) const
{
    const SimaiDifficultyData* difficulty = state_.document_.difficulty(difficultyId);
    if (difficulty == nullptr) {
        return {};
    }
    switch (field) {
    case MainWindow::DifficultyField::Level:
        return difficulty->level;
    case MainWindow::DifficultyField::Designer:
        return difficulty->designer;
    }
    return {};
}

bool MainWindow::DocumentSection::updateDocumentField(
    MainWindow::DocumentField field,
    const QString& value)
{
    if (field == MainWindow::DocumentField::Designer && state_.unifiedDesignerEnabled_) {
        if (state_.document_.designer == value) {
            return false;
        }
        applyUnifiedDesignerName(value);
        return true;
    }

    bool timingChanged = false;
    switch (field) {
    case MainWindow::DocumentField::Title:
        if (state_.document_.title == value) return false;
        state_.document_.title = value;
        if (ui_.titleEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.titleEdit_);
            ui_.titleEdit_->setText(value);
        }
        break;
    case MainWindow::DocumentField::Artist:
        if (state_.document_.artist == value) return false;
        state_.document_.artist = value;
        if (ui_.artistEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.artistEdit_);
            ui_.artistEdit_->setText(value);
        }
        break;
    case MainWindow::DocumentField::First:
        if (state_.document_.first == value) return false;
        state_.document_.first = value;
        if (ui_.firstEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.firstEdit_);
            ui_.firstEdit_->setText(value);
        }
        timingChanged = true;
        break;
    case MainWindow::DocumentField::Designer:
        if (state_.document_.designer == value) return false;
        state_.document_.designer = value;
        if (ui_.designerEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.designerEdit_);
            ui_.designerEdit_->setText(value);
        }
        break;
    case MainWindow::DocumentField::VideoPath:
        if (state_.document_.videoPath == value) return false;
        state_.document_.videoPath = value;
        break;
    case MainWindow::DocumentField::ExtraText: {
        QVector<SimaiRawField> fields = SimaiDocument::parseUnmanagedFields(value, true);
        SimaiDocument::ensureDefaultClockCount(&fields);
        if (state_.document_.extraFields == fields) return false;
        state_.document_.extraFields = fields;
        setMetadataExtraText(SimaiDocument::serializeRawFields(fields));
        timingChanged = true;
        break;
    }
    }

    state_.documentDirty_ = true;
    markCurrentFieldDirty();
    updateDirtyState();
    owner_.updateWindowTitle();
    rebuildFieldSidebar();
    if (timingChanged) {
        owner_.refreshWaveformCache();
        owner_.refreshTimelineMetadata();
        owner_.scheduleTimelineRefresh();
    }
    return true;
}

bool MainWindow::DocumentSection::updateDifficultyField(
    int difficultyId,
    MainWindow::DifficultyField field,
    const QString& value)
{
    SimaiDifficultyData* difficulty = state_.document_.difficulty(difficultyId);
    if (difficulty == nullptr) {
        return false;
    }
    if (field == MainWindow::DifficultyField::Designer && state_.unifiedDesignerEnabled_) {
        if (difficulty->designer == value && state_.document_.designer == value) {
            return false;
        }
        applyUnifiedDesignerName(value);
        return true;
    }

    switch (field) {
    case MainWindow::DifficultyField::Level:
        if (difficulty->level == value) return false;
        difficulty->level = value;
        if (difficultyId == state_.activeDifficultyId_ && ui_.difficultyLevelEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.difficultyLevelEdit_);
            ui_.difficultyLevelEdit_->setText(value);
        }
        break;
    case MainWindow::DifficultyField::Designer:
        if (difficulty->designer == value) return false;
        difficulty->designer = value;
        if (difficultyId == state_.activeDifficultyId_ && ui_.difficultyDesignerEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.difficultyDesignerEdit_);
            ui_.difficultyDesignerEdit_->setText(value);
        }
        break;
    }

    state_.documentDirty_ = true;
    markCurrentFieldDirty();
    updateDirtyState();
    updateEditorHeader();
    rebuildFieldSidebar();
    return true;
}

bool MainWindow::DocumentSection::updateActiveChartText(const QString& value)
{
    SimaiDifficultyData* difficulty = state_.document_.difficulty(state_.activeDifficultyId_);
    if (difficulty == nullptr || difficulty->chart == value) {
        return false;
    }
    difficulty->chart = value;
    state_.documentDirty_ = true;
    markCurrentFieldDirty();
    updateDirtyState();
    owner_.scheduleTimelineRefresh();
    emit owner_.documentValidationChanged();
    return true;
}

MainWindow::DocumentSourceReplaceResult MainWindow::DocumentSection::replaceDocumentSourceText(
    const QString& value)
{
    MainWindow::DocumentSourceReplaceResult result;
    if (state_.document_.toText() == value) {
        result.accepted = true;
        result.revision = owner_.documentValidationSnapshot().revision;
        return result;
    }

    // Phase 1: construct and strictly preflight the complete candidate.  Do
    // not invalidate validation, load widgets, or mutate state until every
    // candidate difficulty is known to be presentable.
    const miacode::qml_ui::DocumentSourcePreflightResult preflight =
        miacode::qml_ui::preflightDocumentSource(value, miacode::mainwindow::shared::uiValidationLocale());
    result.issues = preflight.issues;
    miacode::qml_ui::DocumentSourceTransactionInput transactionInput;
    transactionInput.committedSourceText = state_.document_.toText();
    transactionInput.attemptedSourceText = value;
    transactionInput.retainedRevision = owner_.documentValidationSnapshot().revision;
    transactionInput.issues = result.issues;
    const miacode::qml_ui::DocumentSourceTransactionState transaction =
        miacode::qml_ui::projectDocumentSourceTransaction(transactionInput);
    result.accepted = transaction.accepted;
    result.revision = transaction.revision;
    result.issues = transaction.issues;
    if (!result.accepted) {
        return result;
    }

    // Phase 2: publish the fully preflighted document as one transaction and
    // immediately request a fresh timeline/validation revision.
    loadDocument(preflight.candidate);
    state_.documentDirty_ = true;
    markCurrentFieldDirty();
    updateDirtyState();
    owner_.scheduleTimelineRefresh();
    result.revision = owner_.documentValidationSnapshot().revision;
    return result;
}

bool MainWindow::DocumentSection::addDocumentDifficulty(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId)
        || state_.document_.difficulty(difficultyId) != nullptr) {
        return false;
    }
    state_.document_.ensureDifficulty(difficultyId);
    state_.documentDirty_ = true;
    markCurrentFieldDirty();
    rebuildFieldSidebar();
    const bool selected = switchToDifficultyField(difficultyId);
    updateDirtyState();
    return selected;
}

QString MainWindow::documentField(DocumentField field) const
{
    return documentSection_->documentField(field);
}

QString MainWindow::difficultyField(int difficultyId, DifficultyField field) const
{
    return documentSection_->difficultyField(difficultyId, field);
}

QVector<int> MainWindow::documentDifficultyIds() const
{
    return document_.difficultyIds();
}

int MainWindow::projectLastOpenedDifficultyId() const
{
    return projectLastOpenedDifficultyId_;
}

const MuriRenderOptions& MainWindow::muriRenderOptions() const
{
    return muriRenderOptions_;
}

// ---- miacode::v2::DocumentBridge ----
// Thin forwarders onto the document* names the widget side uses.

QString MainWindow::sourceText() const { return documentSourceText(); }

void MainWindow::importDroppedAudio(const QStringList& audioPaths, quint64 requestId,
                                    quint64 generation,
                                    miacode::v2::ChartDropImportService::Completion completion)
{
    handleAudioDrop(audioPaths, requestId, generation, std::move(completion));
}

void MainWindow::releaseChartDropImport()
{
    releaseChartDropImportService();
}
QString MainWindow::filePath() const { return documentFilePath(); }

bool MainWindow::applyCommittedDocument(const QString& sourceText, const QString& filePath,
                                        int activeDifficultyId, bool dirty, quint64 revision,
                                        CommitKind kind, bool usedSystemEncoding)
{
    return applyCommittedQmlDocument(sourceText, filePath, activeDifficultyId, dirty, revision,
                                     static_cast<QmlDocumentCommitKind>(kind),
                                     usedSystemEncoding);
}

miacode::chart_transform::ChartNormalizationOptions MainWindow::normalizationOptions() const
{
    return chartNormalizeOptions();
}

void MainWindow::setNormalizationOptions(
    const miacode::chart_transform::ChartNormalizationOptions& options)
{
    setChartNormalizeOptions(options);
}

void MainWindow::setDocumentSaveHandler(std::function<bool(const QString&)> handler)
{
    setQmlDocumentSaveHandler(std::move(handler));
}

void MainWindow::setChartTextHandler(std::function<bool(const QString&)> handler)
{
    setQmlChartTextHandler(std::move(handler));
}

void MainWindow::setLeaveDocumentHandler(std::function<void(std::function<void(bool)>)> handler)
{
    setQmlLeaveDocumentHandler(std::move(handler));
}

QString MainWindow::documentSourceText() const
{
    return document_.toText();
}

QString MainWindow::activeDocumentChartText() const
{
    return activeChartText();
}

QString MainWindow::documentDifficultyChartText(int difficultyId) const
{
    const SimaiDifficultyData* difficulty = document_.difficulty(difficultyId);
    return difficulty != nullptr ? difficulty->chart : QString();
}

QString MainWindow::documentFilePath() const
{
    return currentFilePath_;
}

int MainWindow::documentActiveDifficultyId() const
{
    return activeDifficultyId_;
}

bool MainWindow::documentUnifiedDesignerEnabled() const
{
    return unifiedDesignerEnabled_;
}

bool MainWindow::updateDocumentField(DocumentField field, const QString& value)
{
    return documentSection_->updateDocumentField(field, value);
}

bool MainWindow::updateDifficultyField(
    int difficultyId,
    DifficultyField field,
    const QString& value)
{
    return documentSection_->updateDifficultyField(difficultyId, field, value);
}

bool MainWindow::updateActiveChartText(const QString& value)
{
    return documentSection_->updateActiveChartText(value);
}

MainWindow::DocumentSourceReplaceResult MainWindow::replaceDocumentSourceText(const QString& value)
{
    return documentSection_->replaceDocumentSourceText(value);
}

bool MainWindow::saveDocument()
{
    return !currentFilePath_.isEmpty() && documentSection_->saveToPath(currentFilePath_);
}

bool MainWindow::saveDocumentAs(const QString& path)
{
    return !path.trimmed().isEmpty() && documentSection_->saveToPath(path);
}

bool MainWindow::discardDocumentChanges()
{
    return !currentFilePath_.isEmpty()
        && documentSection_->openFileAtPath(currentFilePath_, true, true);
}

bool MainWindow::selectDocumentDifficulty(int difficultyId)
{
    const bool selected = documentSection_->switchToDifficultyField(difficultyId);
    if (selected) {
        invalidateDocumentValidationRevision();
    }
    return selected;
}

bool MainWindow::addDocumentDifficulty(int difficultyId)
{
    return documentSection_->addDocumentDifficulty(difficultyId);
}

bool MainWindow::removeDocumentDifficulty(int difficultyId)
{
    // The shell asked before calling: DifficultyList.qml's own confirm dialog
    // is the question, and this used to raise a second one behind it.
    return documentSection_->deleteDifficultyField(difficultyId, /*alreadyConfirmed=*/true);
}

void MainWindow::enableUnifiedDocumentDesigner(const QString& canonicalName)
{
    documentSection_->enableUnifiedDocumentDesigner(canonicalName);
}

void MainWindow::disableUnifiedDocumentDesigner()
{
    documentSection_->disableUnifiedDocumentDesigner();
}

bool MainWindow::DocumentSection::applyCommittedQmlDocument(
    const QString& sourceText, const QString& filePath, int activeDifficultyId,
    bool dirty, quint64 revision, MainWindow::QmlDocumentCommitKind kind,
    bool usedSystemEncoding)
{
    if (revision <= owner_.appliedQmlWorkspaceRevision_) return false;

    const SimaiDocument committedDocument = SimaiDocument::fromText(sourceText);
    const SimaiDocument previousDocument = state_.document_;
    const QString previousSource = previousDocument.toText();
    const int previousDifficultyId = state_.activeDifficultyId_;
    const bool sourceChanged = previousSource != sourceText;
    const bool timingChanged = previousDocument.first != committedDocument.first
        || previousDocument.extraFields != committedDocument.extraFields;
    const SimaiDifficultyData* previousDifficulty =
        previousDocument.difficulty(activeDifficultyId);
    const SimaiDifficultyData* committedDifficulty =
        committedDocument.difficulty(activeDifficultyId);
    const bool activeChartChanged = previousDifficulty == nullptr
        || committedDifficulty == nullptr
        || previousDifficulty->chart != committedDifficulty->chart;

    owner_.appliedQmlWorkspaceRevision_ = revision;
    switch (kind) {
    case MainWindow::QmlDocumentCommitKind::Open:
        applyOpenedDocumentState(
            filePath,
            usedSystemEncoding ? TextEncoding::System : TextEncoding::Utf8,
            committedDocument,
            false,
            -1.0);
        break;
    case MainWindow::QmlDocumentCommitKind::Structure:
    case MainWindow::QmlDocumentCommitKind::SourceReplacement:
        loadDocument(committedDocument);
        break;
    case MainWindow::QmlDocumentCommitKind::DifficultySelection:
        state_.document_ = committedDocument;
        if (activeDifficultyId > 0 && activeDifficultyId != previousDifficultyId) {
            switchToDifficultyField(activeDifficultyId);
        }
        break;
    case MainWindow::QmlDocumentCommitKind::SavePoint:
        state_.document_ = committedDocument;
        if (filePath != state_.currentFilePath_) {
            owner_.setCurrentFilePath(filePath, true);
            owner_.addRecentFilePath(filePath);
        }
        resetAutosaveState(sourceText);
        break;
    case MainWindow::QmlDocumentCommitKind::Incremental:
        state_.document_ = committedDocument;
        populateMetadataPage();
        if (activeDifficultyId > 0 && committedDifficulty != nullptr) {
            state_.activeDifficultyId_ = activeDifficultyId;
            populateDifficultyPage(activeDifficultyId);
        }
        rebuildFieldSidebar();
        if (timingChanged) {
            owner_.refreshWaveformCache();
            owner_.refreshTimelineMetadata();
        } else if (activeChartChanged) {
            owner_.scheduleTimelineRefresh();
        }
        break;
    }

    // loadDocument may apply a legacy project preference while constructing
    // hidden widgets. Reassert the committed value so MainWindow cannot
    // manufacture a second writable document during the transition.
    state_.document_ = committedDocument;
    if (activeDifficultyId > 0
        && state_.document_.difficulty(activeDifficultyId) != nullptr
        && state_.activeDifficultyId_ != activeDifficultyId) {
        switchToDifficultyField(activeDifficultyId);
    }
    if (kind == MainWindow::QmlDocumentCommitKind::Open
        || kind == MainWindow::QmlDocumentCommitKind::Structure
        || kind == MainWindow::QmlDocumentCommitKind::SourceReplacement
        || (sourceChanged && kind == MainWindow::QmlDocumentCommitKind::SavePoint)) {
        populateMetadataPage();
        if (activeDifficultyId > 0) populateDifficultyPage(activeDifficultyId);
        rebuildFieldSidebar();
    }
    state_.documentDirty_ = dirty;
    state_.currentFieldDirty_ = false;
    anchorCurrentFieldCleanState();
    updateDirtyState();
    // An edit landed: source text moved and the document is not at its save
    // point. Opening and saving both reach here too, and neither is an edit —
    // the first leaves the source unchanged relative to what was just loaded,
    // and both leave the document clean.
    //
    // updateDirtyState() runs first because it is what starts the routine
    // autosave timer; this adds the per-edit half back, which left with the
    // hidden chart editor that used to drive it.
    if (sourceChanged && dirty) {
        noteDocumentEditedForAutosave();
    }
    owner_.updateWindowTitle();
    return true;
}

bool MainWindow::applyCommittedQmlDocument(
    const QString& sourceText, const QString& filePath, int activeDifficultyId,
    bool dirty, quint64 revision, QmlDocumentCommitKind kind,
    bool usedSystemEncoding)
{
    return documentSection_->applyCommittedQmlDocument(
        sourceText, filePath, activeDifficultyId, dirty, revision, kind,
        usedSystemEncoding);
}

void MainWindow::setQmlChartTextHandler(std::function<bool(const QString&)> handler)
{
    qmlChartTextHandler_ = std::move(handler);
}

bool MainWindow::applyChartTextThroughWorkspace(const QString& text)
{
    // No handler means no QML document owner is attached, which in v2 only
    // happens before bootstrap finishes. Refuse rather than fall back to the
    // local copy: a write that does not reach the workspace is lost on the
    // next commit, which is worse than a reported failure.
    return qmlChartTextHandler_ ? qmlChartTextHandler_(text) : false;
}

void MainWindow::setQmlDocumentSaveHandler(
    std::function<bool(const QString&)> handler)
{
    qmlDocumentSaveHandler_ = std::move(handler);
}

void MainWindow::setQmlLeaveDocumentHandler(
    std::function<void(std::function<void(bool)>)> handler)
{
    qmlLeaveDocumentHandler_ = std::move(handler);
}
