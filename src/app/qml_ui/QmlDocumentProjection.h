#pragma once

#include <QString>
#include <QVector>

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

DocumentValidationProjection projectDocumentValidation(
    const DocumentValidationProjectionInput& input,
    const DocumentValidationProjectionCache& cache);

}  // namespace miacode::qml_ui
