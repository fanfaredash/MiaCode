#include "QmlDocumentProjection.h"

#include "core/chart/document/SimaiTimingMetadata.h"

#include <QRegularExpression>

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

DocumentPresentationState projectDocumentPresentation(const DocumentPresentationInput& input)
{
    DocumentPresentationState state;
    state.activeDifficultyId = input.activeDifficultyId;
    state.dirty = input.dirty;
    state.documentRevision = input.documentRevision;
    state.validationRevision = input.validation.revision;
    state.validationPending = input.validation.pending;
    state.validationAvailable = input.validation.available;
    if (input.dirty) {
        state.dirtyEditorKeys = input.activeDifficultyId > 0
            ? QStringList{QStringLiteral("difficulty:%1").arg(input.activeDifficultyId)}
            : QStringList{QStringLiteral("metadata")};
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
    DocumentSourcePreflightResult result;
    result.candidate = SimaiDocument::fromText(source);
    const miacode::simai::SimaiTimingMetadata timing =
        miacode::simai::buildTimingMetadata(result.candidate);
    struct SourceFieldSpan { int valueStart = 0; int valueLine = 1; int valueColumn = 1; };
    QHash<int, SourceFieldSpan> effectiveInoteSpans;
    QHash<int, SourceFieldSpan> effectiveLevelSpans;
    const QRegularExpression header(QStringLiteral(R"((?m)^[^\S\r\n]*&(inote|lv)_(\d+)=)"));
    QRegularExpressionMatchIterator fields = header.globalMatch(source);
    while (fields.hasNext()) {
        const QRegularExpressionMatch match = fields.next();
        bool idOk = false;
        const int id = match.captured(2).toInt(&idOk);
        if (!idOk) continue;
        const int valueStart = match.capturedEnd(0);
        const int line = source.left(valueStart).count(QLatin1Char('\n')) + 1;
        const int lastNewline = source.lastIndexOf(QLatin1Char('\n'), valueStart - 1);
        const SourceFieldSpan span{valueStart, line, valueStart - lastNewline};
        if (match.captured(1) == QLatin1String("inote")) {
            effectiveInoteSpans.insert(id, span);
        } else {
            effectiveLevelSpans.insert(id, span);
        }
    }
    result.accepted = true;
    for (int difficultyId : result.candidate.difficultyIds()) {
        const SimaiDifficultyData* difficulty = result.candidate.difficulty(difficultyId);
        const SimaiNativeValidationReport report = SimaiNativeParser::buildValidationReport(
            difficulty->chart, locale, nullptr, timing);
        const SourceFieldSpan span = effectiveInoteSpans.contains(difficultyId)
            ? effectiveInoteSpans.value(difficultyId)
            : effectiveLevelSpans.value(difficultyId);
        for (const SimaiNativeValidationIssue& issue : report.issues) {
            const int line = span.valueLine + issue.line - 1;
            const int column = issue.line == 1 ? span.valueColumn + issue.col - 1 : issue.col;
            const int endColumn = issue.line == 1 ? span.valueColumn + issue.endCol - 1 : issue.endCol;
            result.issues.append({line, column, endColumn,
                issue.severity == SimaiNativeValidationSeverity::Warning
                    ? DocumentValidationIssueSeverity::Warning : DocumentValidationIssueSeverity::Error,
                issue.displayMessage});
        }
        result.accepted = result.accepted && report.errorCount == 0;
    }
    return result;
}

}  // namespace miacode::qml_ui
