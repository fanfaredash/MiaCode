#include "MainWindow.DocumentSection.h"
#include "app/mainwindow/MainWindowShared.h"


#include <QtCore>
#include <QtWidgets>

#include <utility>

QString MainWindow::DocumentSection::documentField(MainWindow::DocumentField field) const
{
    switch (field) {
    case MainWindow::DocumentField::Title:
        return owner_.applicationServices_.workspace().document().title;
    case MainWindow::DocumentField::Artist:
        return owner_.applicationServices_.workspace().document().artist;
    case MainWindow::DocumentField::First:
        return owner_.applicationServices_.workspace().document().first;
    case MainWindow::DocumentField::Designer:
        return owner_.applicationServices_.workspace().document().designer;
    case MainWindow::DocumentField::VideoPath:
        return owner_.applicationServices_.workspace().document().videoPath;
    case MainWindow::DocumentField::ExtraText:
        return SimaiDocument::serializeRawFields(owner_.applicationServices_.workspace().document().extraFields);
    }
    return {};
}

QString MainWindow::DocumentSection::difficultyField(
    int difficultyId,
    MainWindow::DifficultyField field) const
{
    const SimaiDifficultyData* difficulty = owner_.applicationServices_.workspace().document().difficulty(difficultyId);
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
    miacode::v2::ChartWorkspace& workspace = owner_.applicationServices_.workspace();
    if (field == MainWindow::DocumentField::Designer && state_.unifiedDesignerEnabled_) {
        if (workspace.document().designer == value) {
            return false;
        }
        applyUnifiedDesignerName(value);
        return true;
    }

    if (!workspace.updateDocumentField(
            static_cast<miacode::v2::ChartWorkspaceDocumentField>(field), value)) {
        return false;
    }

    const bool timingChanged = field == MainWindow::DocumentField::First
        || field == MainWindow::DocumentField::ExtraText;
    switch (field) {
    case MainWindow::DocumentField::Title:
        if (ui_.titleEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.titleEdit_);
            ui_.titleEdit_->setText(value);
        }
        break;
    case MainWindow::DocumentField::Artist:
        if (ui_.artistEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.artistEdit_);
            ui_.artistEdit_->setText(value);
        }
        break;
    case MainWindow::DocumentField::First:
        if (ui_.firstEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.firstEdit_);
            ui_.firstEdit_->setText(value);
        }
        break;
    case MainWindow::DocumentField::Designer:
        if (ui_.designerEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.designerEdit_);
            ui_.designerEdit_->setText(value);
        }
        break;
    case MainWindow::DocumentField::VideoPath:
        break;
    case MainWindow::DocumentField::ExtraText:
        setMetadataExtraText(SimaiDocument::serializeRawFields(workspace.document().extraFields));
        break;
    }

    state_.documentDirty_ = workspace.snapshot().dirty;
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
    miacode::v2::ChartWorkspace& workspace = owner_.applicationServices_.workspace();
    if (field == MainWindow::DifficultyField::Designer && state_.unifiedDesignerEnabled_) {
        if (workspace.document().designerForSlot(difficultyId) == value
            && workspace.document().designer == value) {
            return false;
        }
        applyUnifiedDesignerName(value);
        return true;
    }

    if (!workspace.updateDifficultyField(
            difficultyId, static_cast<miacode::v2::ChartWorkspaceDifficultyField>(field), value)) {
        return false;
    }

    if (difficultyId == state_.activeDifficultyId_) {
        if (field == MainWindow::DifficultyField::Level && ui_.difficultyLevelEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.difficultyLevelEdit_);
            ui_.difficultyLevelEdit_->setText(value);
        }
        if (field == MainWindow::DifficultyField::Designer && ui_.difficultyDesignerEdit_ != nullptr) {
            QSignalBlocker blocker(ui_.difficultyDesignerEdit_);
            ui_.difficultyDesignerEdit_->setText(value);
        }
    }

    state_.documentDirty_ = workspace.snapshot().dirty;
    markCurrentFieldDirty();
    updateDirtyState();
    updateEditorHeader();
    rebuildFieldSidebar();
    return true;
}

bool MainWindow::DocumentSection::updateActiveChartText(const QString& value)
{
    miacode::v2::ChartWorkspace& workspace = owner_.applicationServices_.workspace();
    const SimaiDifficultyData* difficulty =
        workspace.document().difficulty(state_.activeDifficultyId_);
    if (difficulty == nullptr || difficulty->chart == value) {
        return false;
    }
    if (workspace.snapshot().activeDifficultyId != state_.activeDifficultyId_) {
        workspace.selectDifficulty(state_.activeDifficultyId_);
    }
    if (!workspace.replaceActiveDifficultyChart(value).accepted) {
        return false;
    }
    state_.documentDirty_ = workspace.snapshot().dirty;
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
    if (owner_.applicationServices_.workspace().document().toText() == value) {
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
    transactionInput.committedSourceText = owner_.applicationServices_.workspace().document().toText();
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
    const miacode::v2::ChartWorkspaceResult replaced =
        owner_.applicationServices_.workspace().replaceSource(value);
    if (!replaced.accepted) {
        result.accepted = false;
        return result;
    }
    loadDocument();
    state_.documentDirty_ = owner_.applicationServices_.workspace().snapshot().dirty;
    markCurrentFieldDirty();
    updateDirtyState();
    owner_.scheduleTimelineRefresh();
    result.revision = owner_.documentValidationSnapshot().revision;
    return result;
}

bool MainWindow::DocumentSection::addDocumentDifficulty(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId)
        || owner_.applicationServices_.workspace().document().difficulty(difficultyId) != nullptr) {
        return false;
    }
    if (!owner_.applicationServices_.workspace().addDifficulty(difficultyId)) {
        return false;
    }
    state_.documentDirty_ = owner_.applicationServices_.workspace().snapshot().dirty;
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
    return applicationServices_.workspace().document().difficultyIds();
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
    const miacode::v2::ChartWorkspaceSnapshot snapshot = applicationServices_.workspace().snapshot();
    return snapshot.hasDocument ? snapshot.sourceText : QString();
}

QString MainWindow::activeDocumentChartText() const
{
    return activeChartText();
}

QString MainWindow::documentDifficultyChartText(int difficultyId) const
{
    const SimaiDifficultyData* difficulty = applicationServices_.workspace().document().difficulty(difficultyId);
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
    Q_UNUSED(sourceText);
    Q_UNUSED(filePath);
    Q_UNUSED(activeDifficultyId);
    Q_UNUSED(dirty);
    Q_UNUSED(revision);
    Q_UNUSED(kind);
    Q_UNUSED(usedSystemEncoding);
    syncRuntimeFromWorkspace();
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
