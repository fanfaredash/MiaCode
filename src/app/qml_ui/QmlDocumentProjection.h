#pragma once

#include <QString>
#include <QVector>

#include "core/chart/document/SimaiDocument.h"
#include "core/chart/parser/SimaiNativeParser.h"

namespace miacode::v2 {
struct AnalysisSnapshot;
}

namespace miacode::qml_ui {

enum class DocumentValidationIssueSeverity {
    Error,
    Warning,
};

struct DocumentValidationProjectionIssue {
    int line = 1;
    int column = 1;
    int endColumn = 1;
    DocumentValidationIssueSeverity severity = DocumentValidationIssueSeverity::Error;
    QString message;
    QString code;
};

struct DocumentValidationProjectionInput {
    int difficultyId = 0;
    QString chartTextSignature;
    QString timingSignature;
    quint64 timelineRevision = 0;
};

struct DocumentValidationProjectionCache {
    int difficultyId = 0;
    QString chartTextSignature;
    QString timingSignature;
    quint64 validationRevision = 0;
    bool ok = true;
    int errorCount = 0;
    int warningCount = 0;
    int parsedNoteCount = 0;
    QVector<DocumentValidationProjectionIssue> issues;
};

struct DocumentValidationProjection {
    bool available = false;
    bool pending = false;
    quint64 revision = 0;
    bool ok = true;
    int errorCount = 0;
    int warningCount = 0;
    int parsedNoteCount = 0;
    QVector<DocumentValidationProjectionIssue> issues;
};

// The one state QML publishes for a document mutation.  Keep the validation
// availability tied to the same revision that made the document dirty.
struct DocumentPresentationInput {
    int activeDifficultyId = 0;
    bool dirty = false;
    bool metadataDirty = false;
    // The difficulties whose charts differ from the last save point, from the
    // workspace. Not derived from `dirty` and the active tab — that was the
    // defect: the file's one flag drawn as if it were the tab's.
    QVector<int> dirtyDifficultyIds;
    quint64 documentRevision = 0;
    DocumentValidationProjection validation;
};

struct DocumentPresentationState {
    int activeDifficultyId = 0;
    bool dirty = false;
    quint64 documentRevision = 0;
    quint64 validationRevision = 0;
    bool validationPending = false;
    bool validationAvailable = false;
    QStringList dirtyEditorKeys;
    DocumentValidationProjection validation;
};

struct DocumentSourceTransactionInput {
    QString committedSourceText;
    QString attemptedSourceText;
    quint64 retainedRevision = 0;
    QVector<DocumentValidationProjectionIssue> issues;
};

struct DocumentSourceTransactionState {
    bool accepted = false;
    quint64 revision = 0;
    QString editorSourceText;
    QString committedSourceText;
    QVector<DocumentValidationProjectionIssue> issues;
};

struct DocumentSourcePreflightResult {
    SimaiDocument candidate;
    QVector<DocumentValidationProjectionIssue> issues;
    bool accepted = false;
};

DocumentValidationProjection projectDocumentValidation(
    const DocumentValidationProjectionInput& input,
    const DocumentValidationProjectionCache& cache);
DocumentValidationProjection projectDocumentValidation(
    const miacode::v2::AnalysisSnapshot& snapshot,
    int activeDifficultyId,
    quint64 documentRevision);
DocumentPresentationState projectDocumentPresentation(const DocumentPresentationInput& input);
DocumentSourceTransactionState projectDocumentSourceTransaction(
    const DocumentSourceTransactionInput& input);
DocumentSourcePreflightResult preflightDocumentSource(
    const QString& source, SimaiNativeValidationLocale locale);

}  // namespace miacode::qml_ui
