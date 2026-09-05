#include "QmlDocumentProjection.h"

#include "app/v2/AnalysisService.h"
#include "app/v2/ChartWorkspace.h"

namespace miacode::qml_ui {

DocumentValidationProjection projectDocumentValidation(
    const DocumentValidationProjectionInput& input,
    const DocumentValidationProjectionCache& cache)
{
    DocumentValidationProjection projection;
    projection.revision = input.timelineRevision;
    if (input.difficultyId <= 0) {
        return projection;
    }

    const bool matches = cache.difficultyId == input.difficultyId
        && cache.chartTextSignature == input.chartTextSignature
        && cache.timingSignature == input.timingSignature
        && cache.validationRevision == input.timelineRevision;
    if (!matches) {
        projection.pending = true;
        return projection;
    }

    projection.available = true;
    projection.ok = cache.ok;
    projection.errorCount = cache.errorCount;
    projection.warningCount = cache.warningCount;
    projection.parsedNoteCount = cache.parsedNoteCount;
    projection.issues = cache.issues;
    return projection;
}

DocumentValidationProjection projectDocumentValidation(
    const miacode::v2::AnalysisSnapshot& snapshot,
    int activeDifficultyId,
    quint64 documentRevision)
{
    DocumentValidationProjection projection;
    projection.revision = documentRevision;
    if (activeDifficultyId <= 0) return projection;

    const bool current = snapshot.difficultyId == activeDifficultyId
        && snapshot.revision == documentRevision;
    if (!current || snapshot.pending || !snapshot.available) {
        projection.pending = true;
        return projection;
    }

    projection.available = true;
    projection.ok = snapshot.validation.ok;
    projection.errorCount = snapshot.validation.errorCount;
    projection.warningCount = snapshot.validation.warningCount;
    projection.parsedNoteCount = snapshot.validation.strictNoteCount;
    projection.issues.reserve(snapshot.validation.issues.size());
    for (const SimaiNativeValidationIssue& issue : snapshot.validation.issues) {
        projection.issues.append({
            issue.line,
            issue.col,
            issue.endCol,
            issue.severity == SimaiNativeValidationSeverity::Warning
                ? DocumentValidationIssueSeverity::Warning
                : DocumentValidationIssueSeverity::Error,
            issue.displayMessage,
            QString(),
        });
    }
    return projection;
}

DocumentPresentationState projectDocumentPresentation(const DocumentPresentationInput& input)
{
    DocumentPresentationState state;
    state.activeDifficultyId = input.activeDifficultyId;
    state.dirty = input.dirty;
    state.documentRevision = input.documentRevision;
    state.validationRevision = input.validation.revision;
    state.validationPending = input.validation.pending;
    state.validationAvailable = input.validation.available;
    for (const int difficultyId : input.dirtyDifficultyIds) {
        state.dirtyEditorKeys.append(QStringLiteral("difficulty:%1").arg(difficultyId));
    }
    // The metadata tab shows the whole file's source, so what it marks is the
    // file: it is dirty exactly when the document is. That is not the same
    // question as any one difficulty's, which is why both are listed.
    if (input.dirty) {
        state.dirtyEditorKeys.append(QStringLiteral("metadata"));
    }
    state.validation = input.validation;
    return state;
}

DocumentSourceTransactionState projectDocumentSourceTransaction(
    const DocumentSourceTransactionInput& input)
{
    DocumentSourceTransactionState state;
    state.revision = input.retainedRevision;
    state.editorSourceText = input.attemptedSourceText;
    state.committedSourceText = input.committedSourceText;
    state.issues = input.issues;
    state.accepted = true;
    for (const DocumentValidationProjectionIssue& issue : input.issues) {
        if (issue.severity == DocumentValidationIssueSeverity::Error) {
            state.accepted = false;
            break;
        }
    }
    if (state.accepted) {
        state.committedSourceText = input.attemptedSourceText;
    }
    return state;
}

DocumentSourcePreflightResult preflightDocumentSource(
    const QString& source, SimaiNativeValidationLocale locale)
{
    const miacode::v2::ChartWorkspacePreflightResult workspacePreflight =
        miacode::v2::ChartWorkspace::preflightSource(source, locale);
    DocumentSourcePreflightResult result;
    result.candidate = workspacePreflight.candidate;
    result.accepted = workspacePreflight.accepted;
    result.issues.reserve(workspacePreflight.issues.size());
    for (const miacode::v2::ChartWorkspaceIssue& issue : workspacePreflight.issues) {
        result.issues.append({issue.line, issue.column, issue.endColumn,
            issue.severity == miacode::v2::ChartWorkspaceIssueSeverity::Warning
                ? DocumentValidationIssueSeverity::Warning : DocumentValidationIssueSeverity::Error,
            issue.message,
            issue.code});
    }
    return result;
}

}  // namespace miacode::qml_ui
