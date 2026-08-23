#include "QmlDocumentProjection.h"

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

}  // namespace miacode::qml_ui
