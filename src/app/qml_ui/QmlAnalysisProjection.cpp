#include "QmlAnalysisProjection.h"

namespace miacode::qml_ui {

AnalysisProjection projectAnalysis(const AnalysisProjectionInput& input)
{
    AnalysisProjection result;
    result.revision = input.validation.revision;
    const bool aligned = input.validation.available
        && !input.validation.pending
        && input.activeDifficultyId > 0
        && input.muriDifficultyId == input.activeDifficultyId
        && input.muriRevision == input.validation.revision
        && input.muriSignatureAligned
        && input.muriStaticReferencesAligned;
    if (!aligned) {
        return result;
    }

    result.available = true;
    result.pending = false;
    result.validationRows.reserve(input.validation.issues.size());
    for (const DocumentValidationProjectionIssue& issue : input.validation.issues) {
        AnalysisRow row;
        row.line = issue.line;
        row.column = issue.column;
        row.endColumn = issue.endColumn;
        row.severity = issue.severity == DocumentValidationIssueSeverity::Warning
            ? QStringLiteral("warning") : QStringLiteral("error");
        row.title = row.severity == QLatin1String("warning")
            ? QStringLiteral("Warning") : QStringLiteral("Error");
        row.detail = issue.message;
        row.difficultyId = input.activeDifficultyId;
        row.revision = input.validation.revision;
        result.validationRows.append(row);
    }
    result.muriRows = input.muriRows;
    for (AnalysisRow& row : result.muriRows) {
        row.difficultyId = input.activeDifficultyId;
        row.revision = input.validation.revision;
        row.title = QStringLiteral("Muri: %1").arg(row.title);
    }
    return result;
}

bool analysisRowIsCurrent(const AnalysisProjection& projection, const AnalysisRow& row)
{
    if (!projection.available || row.difficultyId <= 0 || row.revision != projection.revision) {
        return false;
    }
    const auto matches = [&row](const AnalysisRow& candidate) {
        return candidate.line == row.line && candidate.column == row.column
            && candidate.endColumn == row.endColumn
            && candidate.second == row.second && candidate.difficultyId == row.difficultyId
            && candidate.revision == row.revision && candidate.title == row.title
            && candidate.detail == row.detail;
    };
    for (const AnalysisRow& candidate : projection.validationRows) {
        if (matches(candidate)) return true;
    }
    for (const AnalysisRow& candidate : projection.muriRows) {
        if (matches(candidate)) return true;
    }
    return false;
}

bool analysisRowCanActivate(
    const AnalysisProjection& projection, const AnalysisRow& row, int currentDifficultyId)
{
    return currentDifficultyId == row.difficultyId && analysisRowIsCurrent(projection, row);
}

void QmlAnalysisActivationState::begin(const AnalysisRow& row)
{
    pending_ = row;
    hasPending_ = true;
}

bool QmlAnalysisActivationState::cancel(const AnalysisRow& row)
{
    if (!hasPending_ || !sameIdentity(pending_, row)) return false;
    hasPending_ = false;
    return true;
}

bool QmlAnalysisActivationState::complete(const AnalysisRow& row, AnalysisRow* completed)
{
    if (!hasPending_ || !sameIdentity(pending_, row)) return false;
    if (completed != nullptr) *completed = pending_;
    hasPending_ = false;
    return true;
}

bool QmlAnalysisActivationState::hasPending() const { return hasPending_; }
const AnalysisRow& QmlAnalysisActivationState::pending() const { return pending_; }

bool QmlAnalysisActivationState::sameIdentity(const AnalysisRow& left, const AnalysisRow& right)
{
    return left.difficultyId == right.difficultyId && left.revision == right.revision
        && left.line == right.line && left.column == right.column && left.endColumn == right.endColumn
        && left.second == right.second;
}

}  // namespace miacode::qml_ui
