#pragma once

#include <QString>
#include <QVector>

#include "common/MuriTypes.h"             // MuriDiagnostic, MuriJudgeSpriteEvent, MuriKind, ...
#include "tools/muri/MuriAnalyzerModel.h"  // DiagnosticAnchor, JudgeableSimpleNote

struct TimelineNoteMarker;

// Diagnostic-collection stage of the Muri analyzer (2026-05-29 god-file
// decomposition, stage 5). Owns the analyzer's two output streams — the
// diagnostics list and the judge-sprite events — while the pipeline stages inject
// into it through these methods. analyze() builds one collector, threads it
// through the simple-note and slide/wifi stages, calls finalize() (stable sort +
// dense-overlap dedupe), then drains it into the MuriAnalysisReport. Internal to
// the Muri analyzer — namespace miacode::muri::detail.
namespace miacode::muri::detail {

class MuriDiagnosticCollector {
public:
    // Marker-anchored diagnostic (slide/wifi "too fast", ...).
    void addDiagnostic(
        MuriKind kind,
        MuriAlertLevel alertLevel,
        double second,
        const TimelineNoteMarker& marker,
        const QString& markerKey,
        const QString& detail,
        const DiagnosticAnchor& anchor = DiagnosticAnchor());

    // Simple-note-anchored diagnostic (tap-on-slide / slide-head-tap / overlap).
    void addSimpleNoteDiagnostic(
        MuriKind kind,
        MuriAlertLevel alertLevel,
        double second,
        const JudgeableSimpleNote& note,
        const QString& detail,
        const DiagnosticAnchor& anchor = DiagnosticAnchor());

    // Judge-sprite events. The simple-note variant is a no-op unless the note was
    // judged bad on a real pad; the slide variant returns false for unusable lanes.
    void addSimpleJudgeSpriteEvent(
        const JudgeableSimpleNote& note,
        MuriSimpleJudgeEffect simpleEffect = MuriSimpleJudgeEffect::Good);
    bool appendSlideJudgeSpriteEvent(const TimelineNoteMarker& marker, double judgeSecond);

    // Stable-sort both streams by time/source and collapse dense same-second
    // overlap diagnostics. Call once, after every stage has injected.
    void finalize();

    QVector<MuriDiagnostic> diagnostics;
    QVector<MuriJudgeSpriteEvent> judgeSpriteEvents;
};

}  // namespace miacode::muri::detail
