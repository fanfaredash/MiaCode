#include "runtime/document/DocumentSessionHost.h"
#include "runtime/Shared.h"

#include <QtCore>

#include <utility>

QString miacode::runtime::DocumentSessionHost::documentField(Session::DocumentField field) const
{
    switch (field) {
    case Session::DocumentField::Title:
        return session_.applicationServices_.workspace().document().title;
    case Session::DocumentField::Artist:
        return session_.applicationServices_.workspace().document().artist;
    case Session::DocumentField::First:
        return session_.applicationServices_.workspace().document().first;
    case Session::DocumentField::Designer:
        return session_.applicationServices_.workspace().document().designer;
    case Session::DocumentField::VideoPath:
        return session_.applicationServices_.workspace().document().videoPath;
    }
    return {};
}

QString miacode::runtime::DocumentSessionHost::difficultyField(
    int difficultyId,
    Session::DifficultyField field) const
{
    const SimaiDifficultyData* difficulty = session_.applicationServices_.workspace().document().difficulty(difficultyId);
    if (difficulty == nullptr) {
        return {};
    }
    switch (field) {
    case Session::DifficultyField::Level:
        return difficulty->level;
    case Session::DifficultyField::Designer:
        return difficulty->designer;
    }
    return {};
}

bool miacode::runtime::DocumentSessionHost::updateDocumentField(
    Session::DocumentField field,
    const QString& value)
{
    // A designer write under the unified mode fans out to every &des_N inside
    // ChartWorkspace, so this path stays the same shape for every field.
    miacode::v2::ChartWorkspace& workspace = session_.applicationServices_.workspace();
    if (!workspace.updateDocumentField(
            static_cast<miacode::v2::ChartWorkspaceDocumentField>(field), value)) {
        return false;
    }

    const bool timingChanged = field == Session::DocumentField::First;
    state_.documentDirty_ = workspace.snapshot().dirty;
    markCurrentFieldDirty();
    updateDirtyState();
    session_.updateWindowTitle();
    if (timingChanged) {
        session_.refreshWaveformCache();
        session_.refreshTimelineMetadata();
        session_.scheduleTimelineRefresh();
    }
    return true;
}

bool miacode::runtime::DocumentSessionHost::updateDifficultyField(
    int difficultyId,
    Session::DifficultyField field,
    const QString& value)
{
    miacode::v2::ChartWorkspace& workspace = session_.applicationServices_.workspace();
    if (!workspace.updateDifficultyField(
            difficultyId, static_cast<miacode::v2::ChartWorkspaceDifficultyField>(field), value)) {
        return false;
    }

    state_.documentDirty_ = workspace.snapshot().dirty;
    markCurrentFieldDirty();
    updateDirtyState();
    return true;
}

bool miacode::runtime::DocumentSessionHost::updateActiveChartText(const QString& value)
{
    miacode::v2::ChartWorkspace& workspace = session_.applicationServices_.workspace();
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
    session_.scheduleTimelineRefresh();
    emit session_.documentValidationChanged();
    return true;
}

Session::DocumentSourceReplaceResult miacode::runtime::DocumentSessionHost::replaceDocumentSourceText(
    const QString& value)
{
    Session::DocumentSourceReplaceResult result;
    if (session_.applicationServices_.workspace().document().toText() == value) {
        result.accepted = true;
        result.revision = session_.documentValidationSnapshot().revision;
        return result;
    }

    // Phase 1: construct and strictly preflight the complete candidate.  Do
    // not invalidate validation, load widgets, or mutate state until every
    // candidate difficulty is known to be presentable.
    const miacode::qml_ui::DocumentSourcePreflightResult preflight =
        miacode::qml_ui::preflightDocumentSource(value, miacode::runtime::shared::uiValidationLocale());
    result.issues = preflight.issues;
    miacode::qml_ui::DocumentSourceTransactionInput transactionInput;
    transactionInput.committedSourceText = session_.applicationServices_.workspace().document().toText();
    transactionInput.attemptedSourceText = value;
    transactionInput.retainedRevision = session_.documentValidationSnapshot().revision;
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
        session_.applicationServices_.workspace().replaceSource(value);
    if (!replaced.accepted) {
        result.accepted = false;
        return result;
    }
    // Hand-written &des_N can now disagree with the shared name; the text the
    // user submitted wins and the mode steps down if it does.
    reconcileUnifiedDocumentDesigner(
        miacode::v2::DocumentBridge::UnifiedDesignerReconcileReason::SourceReplaced);
    loadDocument();
    state_.documentDirty_ = session_.applicationServices_.workspace().snapshot().dirty;
    markCurrentFieldDirty();
    updateDirtyState();
    session_.scheduleTimelineRefresh();
    result.revision = session_.documentValidationSnapshot().revision;
    return result;
}

bool miacode::runtime::DocumentSessionHost::addDocumentDifficulty(int difficultyId)
{
    if (!SimaiDocument::isDifficultyId(difficultyId)
        || session_.applicationServices_.workspace().document().difficulty(difficultyId) != nullptr) {
        return false;
    }
    if (!session_.applicationServices_.workspace().addDifficulty(difficultyId)) {
        return false;
    }
    state_.documentDirty_ = session_.applicationServices_.workspace().snapshot().dirty;
    markCurrentFieldDirty();
    const bool selected = switchToDifficultyField(difficultyId);
    updateDirtyState();
    return selected;
}

QString Session::documentField(DocumentField field) const
{
    return documents_->documentField(field);
}

QString Session::difficultyField(int difficultyId, DifficultyField field) const
{
    return documents_->difficultyField(difficultyId, field);
}

QVector<int> Session::documentDifficultyIds() const
{
    return applicationServices_.workspace().document().difficultyIds();
}

int Session::projectLastOpenedDifficultyId() const
{
    return projectLastOpenedDifficultyId_;
}

const MuriRenderOptions& Session::muriRenderOptions() const
{
    return muriRenderOptions_;
}

QString miacode::runtime::DocumentSessionHost::sourceText() const
{
    const miacode::v2::ChartWorkspaceSnapshot snapshot =
        session_.applicationServices_.workspace().snapshot();
    return snapshot.hasDocument ? snapshot.sourceText : QString();
}

void miacode::runtime::DocumentSessionHost::importDroppedAudio(
    const QStringList& audioPaths, quint64 requestId, quint64 generation,
    miacode::v2::ChartDropImportService::Completion completion)
{
    session_.handleAudioDrop(audioPaths, requestId, generation, std::move(completion));
}

void miacode::runtime::DocumentSessionHost::releaseChartDropImport()
{
    session_.releaseChartDropImportService();
}

QString miacode::runtime::DocumentSessionHost::filePath() const
{
    return session_.documentFilePath();
}

int miacode::runtime::DocumentSessionHost::activeDifficultyId() const
{
    return state_.activeDifficultyId_;
}

bool miacode::runtime::DocumentSessionHost::hasActiveDifficulty() const
{
    return state_.activeDifficultyId_ > 0
        && session_.applicationServices_.workspace().document().difficulty(state_.activeDifficultyId_)
            != nullptr;
}

bool miacode::runtime::DocumentSessionHost::applyCommittedDocument(
    const QString& sourceText, const QString& filePath, int activeDifficultyId, bool dirty,
    quint64 revision, CommitKind kind, bool usedSystemEncoding)
{
    return applyCommittedQmlDocument(
        sourceText, filePath, activeDifficultyId, dirty, revision,
        static_cast<Session::QmlDocumentCommitKind>(kind), usedSystemEncoding);
}

miacode::chart_transform::ChartNormalizationOptions
miacode::runtime::DocumentSessionHost::normalizationOptions() const
{
    return session_.chartNormalizeOptions();
}

void miacode::runtime::DocumentSessionHost::setNormalizationOptions(
    const miacode::chart_transform::ChartNormalizationOptions& options)
{
    session_.setChartNormalizeOptions(options);
}

void miacode::runtime::DocumentSessionHost::setDocumentSaveHandler(std::function<bool(const QString&)> handler)
{
    session_.setQmlDocumentSaveHandler(std::move(handler));
}

void miacode::runtime::DocumentSessionHost::setChartTextHandler(std::function<bool(const QString&)> handler)
{
    session_.setQmlChartTextHandler(std::move(handler));
}

void miacode::runtime::DocumentSessionHost::setLeaveDocumentHandler(
    std::function<void(std::function<void(bool)>)> handler)
{
    session_.setQmlLeaveDocumentHandler(std::move(handler));
}

QString Session::documentSourceText() const
{
    const miacode::v2::ChartWorkspaceSnapshot snapshot = applicationServices_.workspace().snapshot();
    return snapshot.hasDocument ? snapshot.sourceText : QString();
}

QString Session::activeDocumentChartText() const
{
    return activeChartText();
}

QString Session::documentDifficultyChartText(int difficultyId) const
{
    const SimaiDifficultyData* difficulty = applicationServices_.workspace().document().difficulty(difficultyId);
    return difficulty != nullptr ? difficulty->chart : QString();
}

QString Session::documentFilePath() const
{
    return currentFilePath_;
}

int Session::documentActiveDifficultyId() const
{
    return activeDifficultyId_;
}

bool Session::updateDocumentField(DocumentField field, const QString& value)
{
    return documents_->updateDocumentField(field, value);
}

bool Session::updateDifficultyField(
    int difficultyId,
    DifficultyField field,
    const QString& value)
{
    return documents_->updateDifficultyField(difficultyId, field, value);
}

bool Session::updateActiveChartText(const QString& value)
{
    return documents_->updateActiveChartText(value);
}

Session::DocumentSourceReplaceResult Session::replaceDocumentSourceText(const QString& value)
{
    return documents_->replaceDocumentSourceText(value);
}

bool Session::saveDocument()
{
    return !currentFilePath_.isEmpty() && documents_->saveToPath(currentFilePath_);
}

bool Session::saveDocumentAs(const QString& path)
{
    return !path.trimmed().isEmpty() && documents_->saveToPath(path);
}

bool Session::discardDocumentChanges()
{
    return !currentFilePath_.isEmpty()
        && documents_->openFileAtPath(currentFilePath_, true);
}

bool Session::selectDocumentDifficulty(int difficultyId)
{
    const bool selected = documents_->switchToDifficultyField(difficultyId);
    if (selected) {
        invalidateDocumentValidationRevision();
    }
    return selected;
}

bool Session::addDocumentDifficulty(int difficultyId)
{
    return documents_->addDocumentDifficulty(difficultyId);
}

bool Session::removeDocumentDifficulty(int difficultyId)
{
    return documents_->deleteDifficultyField(difficultyId);
}

bool Session::applyDocumentDesignerSlots(
    const QVector<QPair<int, QString>>& slotValues, bool unified, const QString& canonicalName)
{
    return documents_->applyDocumentDesignerSlots(slotValues, unified, canonicalName);
}

bool miacode::runtime::DocumentSessionHost::applyCommittedQmlDocument(
    const QString& sourceText, const QString& filePath, int activeDifficultyId,
    bool dirty, quint64 revision, Session::QmlDocumentCommitKind kind,
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

bool Session::applyCommittedQmlDocument(
    const QString& sourceText, const QString& filePath, int activeDifficultyId,
    bool dirty, quint64 revision, QmlDocumentCommitKind kind,
    bool usedSystemEncoding)
{
    return documents_->applyCommittedQmlDocument(
        sourceText, filePath, activeDifficultyId, dirty, revision, kind,
        usedSystemEncoding);
}

void Session::setQmlChartTextHandler(std::function<bool(const QString&)> handler)
{
    qmlChartTextHandler_ = std::move(handler);
}

void Session::setQmlDocumentSaveHandler(
    std::function<bool(const QString&)> handler)
{
    qmlDocumentSaveHandler_ = std::move(handler);
}

void Session::setQmlLeaveDocumentHandler(
    std::function<void(std::function<void(bool)>)> handler)
{
    qmlLeaveDocumentHandler_ = std::move(handler);
}

bool miacode::runtime::DocumentSessionHost::requestEditorNavigation(
    int line, int column, int endLine, int endColumn, bool selectToken, bool focusEditor, bool centerView)
{
    if (!hasActiveDifficulty() || session_.editorSyncController_ == nullptr) {
        return false;
    }
    const QString text = session_.activeDocumentChartText();
    const auto positionFor = [&text](int targetLine, int targetColumn) {
        int position = 0;
        int currentLine = 1;
        while (currentLine < qMax(1, targetLine) && position < text.size()) {
            const int newline = text.indexOf(QLatin1Char('\n'), position);
            if (newline < 0) {
                return text.size();
            }
            position = newline + 1;
            ++currentLine;
        }
        return qBound(position, position + qMax(0, targetColumn - 1), text.size());
    };
    const int start = positionFor(line, column);
    const int end = selectToken
        ? qMax(start, positionFor(qMax(line, endLine), qMax(1, endColumn + 1)))
        : start;
    return session_.editorSyncController_->requestNavigation(
        state_.activeDifficultyId_, session_.appliedQmlWorkspaceRevision_, start, end,
        focusEditor, centerView) != 0;
}
