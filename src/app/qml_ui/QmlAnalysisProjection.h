#pragma once

#include <QVector>

#include "QmlDocumentProjection.h"
#include "timeline/TimelineData.h"

namespace miacode::v2 {
struct AnalysisSnapshot;
}

namespace miacode::qml_ui {

struct AnalysisRow {
    int line = 1;
    int column = 1;
    int endColumn = 1;
    // Negative: this row has no playhead time (syntax diagnostics). Zero is a
    // real chart time and must still seek.
    double second = -1.0;
    QString severity;
    QString alert;
    QString title;
    QString detail;
    int difficultyId = 0;
    quint64 revision = 0;
};

struct AnalysisProjectionInput {
    DocumentValidationProjection validation;
    int activeDifficultyId = 0;
    int muriDifficultyId = 0;
    quint64 muriRevision = 0;
    bool muriSignatureAligned = false;
    bool muriStaticReferencesAligned = false;
    QVector<AnalysisRow> muriRows;
};

struct AnalysisProjection {
    bool available = false;
    bool pending = true;
    int difficultyId = 0;
    quint64 revision = 0;
    QVector<AnalysisRow> validationRows;
    QVector<AnalysisRow> muriRows;
    QVector<TimelineNoteMarker> noteMarkers;
};

AnalysisProjection projectAnalysis(const AnalysisProjectionInput& input);
AnalysisProjection projectAnalysis(
    const miacode::v2::AnalysisSnapshot& snapshot,
    int activeDifficultyId,
    quint64 documentRevision,
    const QVector<AnalysisRow>& muriRows);
bool analysisRowIsCurrent(const AnalysisProjection& projection, const AnalysisRow& row);
bool analysisRowCanActivate(
    const AnalysisProjection& projection, const AnalysisRow& row, int currentDifficultyId);

class QmlAnalysisActivationState {
public:
    void begin(const AnalysisRow& row);
    bool cancel(const AnalysisRow& row);
    bool complete(const AnalysisRow& row, AnalysisRow* completed = nullptr);
    bool hasPending() const;
    const AnalysisRow& pending() const;

private:
    static bool sameIdentity(const AnalysisRow& left, const AnalysisRow& right);
    AnalysisRow pending_;
    bool hasPending_ = false;
};

}  // namespace miacode::qml_ui
