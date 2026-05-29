#include "tools/muri/MuriDiagnosticCollector.h"

#include <algorithm>
#include <cmath>

#include <QHash>

#include "timeline/TimelineData.h"
#include "common/MuriTypes.h"  // muriKindDisplayName, makeMarkerAnalysisKey

namespace miacode::muri::detail {

// File-local helpers (no header declaration — private to this TU).

QVector<MuriDiagnostic> dedupeDenseOverlapDiagnostics(const QVector<MuriDiagnostic>& diagnostics)
{
    QVector<MuriDiagnostic> deduped;
    deduped.reserve(diagnostics.size());

    QHash<qint64, int> keptOverlapCountBySecond;
    keptOverlapCountBySecond.reserve(diagnostics.size());

    for (const MuriDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.kind != MuriKind::Overlap) {
            deduped.append(diagnostic);
            continue;
        }

        const qint64 secondKey = static_cast<qint64>(std::llround(diagnostic.second * 1000000.0));
        const int keptCount = keptOverlapCountBySecond.value(secondKey, 0);
        if (keptCount >= 1) {
            continue;
        }
        keptOverlapCountBySecond.insert(secondKey, keptCount + 1);
        deduped.append(diagnostic);
    }

    return deduped;
}

bool slideKeyUsesCcwJudgeSprite(const QString& key)
{
    if (key.isEmpty()) {
        return false;
    }
    int startLane = key.at(0).digitValue();
    if (startLane < 1 || startLane > 8) {
        startLane = 1;
    }
    const bool outerStart = startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8;
    if (key.contains(QLatin1Char('<'))) {
        return outerStart;
    }
    if (key.contains(QLatin1Char('>'))) {
        return !outerStart;
    }
    return false;
}

bool slideKeyUsesCwJudgeSprite(const QString& key)
{
    if (key.isEmpty()) {
        return false;
    }
    int startLane = key.at(0).digitValue();
    if (startLane < 1 || startLane > 8) {
        startLane = 1;
    }
    const bool outerStart = startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8;
    if (key.contains(QLatin1Char('>'))) {
        return outerStart;
    }
    if (key.contains(QLatin1Char('<'))) {
        return !outerStart;
    }
    return false;
}

// --- MuriDiagnosticCollector ---------------------------------------------------

void MuriDiagnosticCollector::addDiagnostic(
    MuriKind kind,
    MuriAlertLevel alertLevel,
    double second,
    const TimelineNoteMarker& marker,
    const QString& markerKey,
    const QString& detail,
    const DiagnosticAnchor& anchor)
{
    MuriDiagnostic diagnostic;
    diagnostic.kind = kind;
    diagnostic.alertLevel = alertLevel;
    diagnostic.second = second;
    diagnostic.anchorSecond = anchor.valid ? anchor.second : diagnostic.second;
    diagnostic.line = anchor.valid ? anchor.line : marker.sourceLine;
    diagnostic.col = anchor.valid ? anchor.col : marker.sourceCol;
    diagnostic.markerKey = markerKey;
    diagnostic.title = muriKindDisplayName(kind, true);
    diagnostic.detail = detail;
    diagnostics.append(diagnostic);
}

void MuriDiagnosticCollector::addSimpleNoteDiagnostic(
    MuriKind kind,
    MuriAlertLevel alertLevel,
    double second,
    const JudgeableSimpleNote& note,
    const QString& detail,
    const DiagnosticAnchor& anchor)
{
    MuriDiagnostic diagnostic;
    diagnostic.kind = kind;
    diagnostic.alertLevel = alertLevel;
    diagnostic.second = second;
    diagnostic.anchorSecond = anchor.valid ? anchor.second : diagnostic.second;
    diagnostic.line = anchor.valid ? anchor.line : note.line;
    diagnostic.col = anchor.valid ? anchor.col : note.col;
    diagnostic.markerKey = note.markerKey;
    diagnostic.title = muriKindDisplayName(kind, true);
    diagnostic.detail = detail;
    diagnostics.append(diagnostic);
}

void MuriDiagnosticCollector::addSimpleJudgeSpriteEvent(
    const JudgeableSimpleNote& note,
    MuriSimpleJudgeEffect simpleEffect)
{
    if (!note.judged || !note.judgeBad || note.pad.isEmpty()) {
        return;
    }

    MuriJudgeSpriteEvent event;
    event.kind = MuriJudgeSpriteKind::Simple;
    event.simpleEffect = simpleEffect;
    event.second = note.judgeSecond;
    event.spawnSecond = event.second;
    if (note.marker != nullptr
        && (note.type == QLatin1String("hold") || note.type == QLatin1String("touch_hold"))
        && note.marker->endSecond >= 0.0) {
        event.spawnSecond = qMax(event.second, note.marker->endSecond);
    }
    event.markerKey = note.markerKey;
    event.pad = note.pad;
    judgeSpriteEvents.append(event);
}

bool MuriDiagnosticCollector::appendSlideJudgeSpriteEvent(const TimelineNoteMarker& marker, double judgeSecond)
{
    const int lane = qBound(1, marker.endLane, 8);
    if (lane < 1 || lane > 8) {
        return false;
    }

    MuriJudgeSpriteEvent event;
    event.second = judgeSecond;
    event.spawnSecond = event.second;
    event.markerKey = makeMarkerAnalysisKey(marker);
    event.lane = lane;

    if (marker.type == QLatin1String("wifi")) {
        event.kind =
            (lane == 1 || lane == 2 || lane == 7 || lane == 8)
            ? MuriJudgeSpriteKind::WifiUp
            : MuriJudgeSpriteKind::WifiDown;
        judgeSpriteEvents.append(event);
        return true;
    }

    const QString segmentKey = !marker.slideSegmentKeys.isEmpty()
        ? marker.slideSegmentKeys.constLast()
        : marker.slideTrackKey;
    if (slideKeyUsesCcwJudgeSprite(segmentKey)) {
        event.kind = MuriJudgeSpriteKind::SlideCircleCcw;
    } else if (slideKeyUsesCwJudgeSprite(segmentKey)) {
        event.kind = MuriJudgeSpriteKind::SlideCircleCw;
    } else {
        event.kind = MuriJudgeSpriteKind::SlideStraight;
    }

    judgeSpriteEvents.append(event);
    return true;
}

void MuriDiagnosticCollector::finalize()
{
    std::sort(diagnostics.begin(), diagnostics.end(), [](const MuriDiagnostic& a, const MuriDiagnostic& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
            return a.second < b.second;
        }
        if (a.line != b.line) {
            return a.line < b.line;
        }
        return a.col < b.col;
    });
    diagnostics = dedupeDenseOverlapDiagnostics(diagnostics);
    std::sort(judgeSpriteEvents.begin(), judgeSpriteEvents.end(), [](const MuriJudgeSpriteEvent& a, const MuriJudgeSpriteEvent& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
            return a.second < b.second;
        }
        if (!qFuzzyCompare(a.spawnSecond + 1.0, b.spawnSecond + 1.0)) {
            return a.spawnSecond < b.spawnSecond;
        }
        if (a.kind != b.kind) {
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        }
        if (a.pad != b.pad) {
            return a.pad < b.pad;
        }
        return a.markerKey < b.markerKey;
    });
}

}  // namespace miacode::muri::detail
