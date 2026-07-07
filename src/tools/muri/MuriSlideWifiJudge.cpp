#include "tools/muri/MuriSlideWifiJudge.h"

#include <algorithm>

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "SimaiNativeParser.h"
#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"
#include "common/MuriTypes.h"  // makeMarkerAnalysisKey + Muri* state types
#include "tools/muri/MuriAnalyzerGeometry.h"     // pointDistance
#include "tools/muri/MuriAnalyzerInternal.h"     // timing / hand-action helpers
#include "tools/muri/MuriDiagnosticLabels.h"     // config labels / source anchors
#include "tools/muri/MuriSlideReferenceData.h"   // slideRuntimeRoot + load*

namespace miacode::muri::detail {

constexpr double kPadTimeEpsilon = 1e-6;

// --- per-note working state (private to this stage) ----------------------------

struct RuntimeSlideNoteState {
    const TimelineNoteMarker* marker = nullptr;
    QString markerKey;
    int line = 1;
    int col = 1;
    double availableSecond = 0.0;
    double shootSecond = 0.0;
    double endSecond = 0.0;
    double criticalSecond = 0.0;
    double criticalDeltaSecond = 0.0;
    QVector<QStringList> judgeSequence;
    QVector<bool> partition;
    QVector<int> segmentStartAreaIndices;
    QVector<int> segmentEndAreaIndices;
    QVector<RuntimeJudgeHit> areaHits;
    int totalAreaNum = 0;
    int curAreaIdx = 0;
    int curSegmentIdx = 0;
    QString pressingPad;
    bool isL = false;
    bool isSpecialL = false;
    bool judged = false;
    bool judgeBad = false;
    double judgeSecond = -1.0;
    RuntimePadEvent judgeAction;
};

struct RuntimeWifiNoteState {
    const TimelineNoteMarker* marker = nullptr;
    QString markerKey;
    int line = 1;
    int col = 1;
    double availableSecond = 0.0;
    double shootSecond = 0.0;
    double endSecond = 0.0;
    double criticalSecond = 0.0;
    double criticalDeltaSecond = 0.0;
    QVector<QVector<QStringList>> laneJudgeSequence;
    QVector<QVector<RuntimeJudgeHit>> areaHits;
    QVector<QVector<double>> laneProgressSeconds;
    QVector<int> curAreaIdxes;
    QVector<QString> pressingPads;
    QVector<bool> laneFinished;
    int totalAreaNum = 0;
    bool padCPassed = true;
    double padCSecond = -1.0;
    bool judged = false;
    bool judgeBad = false;
    double judgeSecond = -1.0;
    RuntimePadEvent judgeAction;
};

bool runtimePadEventIsForeignJudge(
    const RuntimePadEvent& event,
    const QString& ownerMarkerKey,
    const TimelineNoteMarker* ownerMarker = nullptr)
{
    if (event.sourceMarkerKey.isEmpty()) {
        return false;
    }
    if (event.sourceMarkerKey == ownerMarkerKey) {
        return false;
    }
    if (ownerMarker != nullptr && event.sourceMarkerKey == slideHeadStarMarkerKey(*ownerMarker)) {
        return false;
    }
    return true;
}

bool slideFinalAreaWasJudgedByOther(const RuntimeSlideNoteState& note)
{
    if (note.areaHits.isEmpty()) {
        return false;
    }
    const RuntimeJudgeHit& finalHit = note.areaHits.constLast();
    return finalHit.judged && runtimePadEventIsForeignJudge(finalHit.cause, note.markerKey, note.marker);
}

bool wifiFinalJudgeWasByOther(const RuntimeWifiNoteState& note)
{
    return runtimePadEventIsForeignJudge(note.judgeAction, note.markerKey, note.marker);
}

bool padStateForTick(
    const QHash<QString, RuntimePadEvent>& padStates,
    const QString& pad,
    RuntimePadEvent* event)
{
    const auto it = padStates.constFind(pad);
    if (it == padStates.constEnd()) {
        return false;
    }
    if (event != nullptr) {
        *event = it.value();
    }
    return true;
}

bool padReleasedThisTick(
    const QHash<QString, RuntimePadEvent>& padUpThisTick,
    const QString& pad,
    RuntimePadEvent* event)
{
    const auto it = padUpThisTick.constFind(pad);
    if (it == padUpThisTick.constEnd()) {
        return false;
    }
    if (event != nullptr) {
        *event = it.value();
    }
    return true;
}

bool buildRuntimeSlideJudgeSequence(
    const TimelineNoteMarker& marker,
    QVector<QStringList>* judgeSequence,
    QVector<bool>* partition,
    QVector<int>* segmentStartAreaIndices,
    QVector<int>* segmentEndAreaIndices,
    bool* isL,
    bool* isSpecialL)
{
    if (judgeSequence == nullptr
        || partition == nullptr
        || segmentStartAreaIndices == nullptr
        || segmentEndAreaIndices == nullptr) {
        return false;
    }

    const QJsonObject slides = slideRuntimeRoot().value(QStringLiteral("slides")).toObject();
    if (slides.isEmpty()) {
        return false;
    }

    judgeSequence->clear();
    partition->clear();
    segmentStartAreaIndices->clear();
    segmentEndAreaIndices->clear();
    if (isL != nullptr) {
        *isL = false;
    }
    if (isSpecialL != nullptr) {
        *isSpecialL = false;
    }

    for (int segmentIndex = 0; segmentIndex < marker.slideSegmentKeys.size(); ++segmentIndex) {
        const QString& segmentKey = marker.slideSegmentKeys.at(segmentIndex);
        const QJsonObject entry = slides.value(segmentKey).toObject();
        if (entry.isEmpty()) {
            return false;
        }

        QVector<QStringList> segmentSequence =
            loadPadAreaSequence(entry.value(QStringLiteral("judge_sequence")).toArray());
        if (segmentSequence.isEmpty()) {
            return false;
        }

        int segmentStartAreaIndex = 0;
        if (segmentIndex == 0) {
            *judgeSequence = segmentSequence;
            *partition = QVector<bool>(segmentSequence.size(), false);
        } else {
            const bool sharedHead =
                !judgeSequence->isEmpty() && !segmentSequence.isEmpty() && judgeSequence->constLast() == segmentSequence.constFirst();
            if (sharedHead && !partition->isEmpty()) {
                segmentStartAreaIndex = judgeSequence->size() - 1;
                (*partition)[segmentStartAreaIndex] = true;
            } else {
                segmentStartAreaIndex = judgeSequence->size();
            }

            const int appendStart = sharedHead ? 1 : 0;
            for (int areaIndex = appendStart; areaIndex < segmentSequence.size(); ++areaIndex) {
                judgeSequence->append(segmentSequence.at(areaIndex));
                partition->append(false);
            }
        }

        segmentStartAreaIndices->append(segmentStartAreaIndex);
        segmentEndAreaIndices->append(judgeSequence->size() - 1);
        if (segmentIndex == 0 && marker.slideSegmentKeys.size() == 1) {
            if (isL != nullptr) {
                *isL = entry.value(QStringLiteral("is_l")).toBool(false);
            }
            if (isSpecialL != nullptr) {
                *isSpecialL = entry.value(QStringLiteral("is_special_l")).toBool(false);
            }
        }
    }

    partition->append(false);
    return !judgeSequence->isEmpty();
}

bool buildRuntimeWifiJudgeSequence(
    const TimelineNoteMarker& marker,
    QVector<QVector<QStringList>>* laneJudgeSequence,
    bool* padCPassed,
    const MuriRenderOptions& renderOptions)
{
    if (laneJudgeSequence == nullptr) {
        return false;
    }

    const QJsonObject root = slideRuntimeRoot();
    const QJsonObject wifi = root.value(QStringLiteral("wifi")).toObject();
    const QJsonObject entry = wifi.value(marker.slideTrackKey).toObject();
    if (entry.isEmpty()) {
        return false;
    }

    *laneJudgeSequence =
        loadTriPadAreaSequence(entry.value(QStringLiteral("tri_judge_sequence")).toArray());
    if (laneJudgeSequence->size() != 3) {
        return false;
    }

    const int totalAreaNum = laneJudgeSequence->value(1).size();
    if (totalAreaNum <= 0) {
        return false;
    }
    for (const QVector<QStringList>& lane : *laneJudgeSequence) {
        if (lane.size() != totalAreaNum) {
            return false;
        }
    }

    if (padCPassed != nullptr) {
        *padCPassed = !renderOptions.wifiNeedC;
    }
    return true;
}

bool buildRuntimeSlideNoteState(const TimelineNoteMarker& marker, RuntimeSlideNoteState* note)
{
    if (note == nullptr || marker.type != QLatin1String("slide")) {
        return false;
    }

    QVector<QStringList> judgeSequence;
    QVector<bool> partition;
    QVector<int> segmentStartAreaIndices;
    QVector<int> segmentEndAreaIndices;
    bool isL = false;
    bool isSpecialL = false;
    if (!buildRuntimeSlideJudgeSequence(
            marker,
            &judgeSequence,
            &partition,
            &segmentStartAreaIndices,
            &segmentEndAreaIndices,
            &isL,
            &isSpecialL)) {
        return false;
    }

    note->marker = &marker;
    note->markerKey = makeMarkerAnalysisKey(marker);
    note->line = marker.sourceLine;
    note->col = marker.sourceCol;
    note->availableSecond = marker.second - miacode::muri::kSlideLeadingSeconds;
    note->shootSecond = marker.slideTraceSecond;
    note->endSecond = marker.endSecond;
    note->judgeSequence = judgeSequence;
    note->partition = partition;
    note->segmentStartAreaIndices = segmentStartAreaIndices;
    note->segmentEndAreaIndices = segmentEndAreaIndices;
    note->areaHits = QVector<RuntimeJudgeHit>(judgeSequence.size());
    note->totalAreaNum = judgeSequence.size();
    note->isL = isL;
    note->isSpecialL = isSpecialL;

    const int lastSegmentIndex = qMin(marker.slideSegmentDurations.size(), marker.slideSegmentCriticalProportions.size()) - 1;
    if (lastSegmentIndex >= 0) {
        const double lastDurationSecond = qMax(0.0, marker.slideSegmentDurations.at(lastSegmentIndex));
        const double criticalProportion = marker.slideSegmentCriticalProportions.at(lastSegmentIndex);
        const double lastAreaDurationSecond = (1.0 - criticalProportion) * lastDurationSecond;
        note->criticalSecond = marker.endSecond - lastAreaDurationSecond;
        note->criticalDeltaSecond = slideCriticalDeltaSecond(lastAreaDurationSecond);
    } else {
        note->criticalSecond = marker.endSecond;
        note->criticalDeltaSecond = slideCriticalDeltaSecond(0.0);
    }

    return true;
}

bool buildRuntimeWifiNoteState(
    const TimelineNoteMarker& marker,
    RuntimeWifiNoteState* note,
    const MuriRenderOptions& renderOptions)
{
    if (note == nullptr || marker.type != QLatin1String("wifi")) {
        return false;
    }

    QVector<QVector<QStringList>> laneJudgeSequence;
    bool padCPassed = true;
    if (!buildRuntimeWifiJudgeSequence(marker, &laneJudgeSequence, &padCPassed, renderOptions)) {
        return false;
    }

    note->marker = &marker;
    note->markerKey = makeMarkerAnalysisKey(marker);
    note->line = marker.sourceLine;
    note->col = marker.sourceCol;
    note->availableSecond = marker.second - miacode::muri::kSlideLeadingSeconds;
    note->shootSecond = marker.slideTraceSecond;
    note->endSecond = marker.endSecond;
    note->laneJudgeSequence = laneJudgeSequence;
    note->totalAreaNum = laneJudgeSequence.value(1).size();
    note->areaHits = QVector<QVector<RuntimeJudgeHit>>(3, QVector<RuntimeJudgeHit>(note->totalAreaNum));
    note->laneProgressSeconds = QVector<QVector<double>>(3, QVector<double>(note->totalAreaNum + 1, -1.0));
    note->curAreaIdxes = QVector<int>(3, 0);
    note->pressingPads = QVector<QString>(3);
    note->laneFinished = QVector<bool>(3, false);
    note->padCPassed = padCPassed;

    const double durationSecond = qMax(0.0, marker.endSecond - marker.slideTraceSecond);
    const double lastAreaDurationSecond = (1.0 - marker.wifiCriticalProportion) * durationSecond;
    note->criticalSecond = marker.endSecond - lastAreaDurationSecond;
    note->criticalDeltaSecond = slideCriticalDeltaSecond(lastAreaDurationSecond);
    return true;
}

bool runtimeSlideCanSkipArea(const RuntimeSlideNoteState& note)
{
    if (note.curAreaIdx >= note.totalAreaNum - 1) {
        return false;
    }

    if (!note.pressingPad.isEmpty()) {
        return true;
    }

    if (note.segmentEndAreaIndices.size() == 1) {
        if (note.isSpecialL) {
            return note.curAreaIdx != 1 && note.curAreaIdx != 3;
        }
        if (note.isL) {
            return note.curAreaIdx != 1;
        }
    }

    if (note.totalAreaNum >= 4) {
        return true;
    }

    return note.curAreaIdx != (note.totalAreaNum - 2);
}

bool progressRuntimeSlideOnce(
    RuntimeSlideNoteState* note,
    double nowSecond,
    const QHash<QString, RuntimePadEvent>& padStates,
    const QHash<QString, RuntimePadEvent>& padUpThisTick)
{
    if (note == nullptr || note->curAreaIdx >= note->totalAreaNum) {
        return false;
    }

    if (note->pressingPad.isEmpty()) {
        for (const QString& pad : note->judgeSequence.at(note->curAreaIdx)) {
            RuntimePadEvent cause;
            if (!padStateForTick(padStates, pad, &cause)) {
                continue;
            }

            note->pressingPad = pad;
            note->areaHits[note->curAreaIdx].judged = true;
            note->areaHits[note->curAreaIdx].second = nowSecond;
            note->areaHits[note->curAreaIdx].cause = cause;
            if (note->curAreaIdx >= note->totalAreaNum - 1) {
                ++note->curAreaIdx;
                note->judgeAction = cause;
            }
            if (note->partition.value(note->curAreaIdx)) {
                ++note->curSegmentIdx;
            }
            return true;
        }
    } else {
        if (!padStates.contains(note->pressingPad)) {
            note->pressingPad.clear();
            ++note->curAreaIdx;
            return true;
        }
    }

    if (runtimeSlideCanSkipArea(*note)) {
        const int skippedAreaIndex = note->curAreaIdx;
        for (const QString& pad : note->judgeSequence.at(note->curAreaIdx + 1)) {
            RuntimePadEvent cause;
            RuntimePadEvent padStateCause;
            const bool down = padStateForTick(padStates, pad, &padStateCause);
            const bool up = padReleasedThisTick(padUpThisTick, pad, &cause);
            if (!down && !up) {
                continue;
            }

            if (!note->areaHits[skippedAreaIndex].judged) {
                note->areaHits[skippedAreaIndex].judged = true;
                note->areaHits[skippedAreaIndex].skipped = true;
                note->areaHits[skippedAreaIndex].second = nowSecond;
            }
            note->pressingPad = pad;
            ++note->curAreaIdx;
            note->areaHits[note->curAreaIdx].judged = true;
            note->areaHits[note->curAreaIdx].second = nowSecond;
            note->areaHits[note->curAreaIdx].cause = down ? padStateCause : cause;
            if (note->curAreaIdx >= note->totalAreaNum - 1) {
                ++note->curAreaIdx;
                note->judgeAction = down ? padStateCause : RuntimePadEvent{};
            }
            if (note->partition.value(note->curAreaIdx)) {
                ++note->curSegmentIdx;
            }
            return true;
        }
    }

    return false;
}

void updateRuntimeSlideNote(
    RuntimeSlideNoteState* note,
    double nowSecond,
    const QHash<QString, RuntimePadEvent>& padStates,
    const QHash<QString, RuntimePadEvent>& padUpThisTick)
{
    if (note == nullptr || note->judged || nowSecond + kPadTimeEpsilon < note->availableSecond) {
        return;
    }

    while (note->curAreaIdx < note->totalAreaNum) {
        if (!progressRuntimeSlideOnce(note, nowSecond, padStates, padUpThisTick)) {
            break;
        }
    }

    if (note->curAreaIdx >= note->totalAreaNum) {
        note->judged = true;
        note->judgeSecond = nowSecond;
        note->judgeBad =
            slideJudgeIsBad(nowSecond, note->criticalSecond, note->criticalDeltaSecond)
            && slideFinalAreaWasJudgedByOther(*note);
        return;
    }

    if (nowSecond > note->endSecond + miacode::muri::kSlideAvailableSeconds + kPadTimeEpsilon) {
        note->judged = true;
        note->judgeBad = true;
        note->judgeSecond = nowSecond;
    }
}

bool progressRuntimeWifiLaneOnce(
    RuntimeWifiNoteState* note,
    double nowSecond,
    int lane,
    const QHash<QString, RuntimePadEvent>& padStates,
    const QHash<QString, RuntimePadEvent>& padUpThisTick)
{
    if (note == nullptr || lane < 0 || lane >= note->laneJudgeSequence.size()) {
        return false;
    }
    if (note->curAreaIdxes.value(lane) >= note->totalAreaNum) {
        return false;
    }

    if (note->pressingPads.at(lane).isEmpty()) {
        for (const QString& pad : note->laneJudgeSequence.at(lane).at(note->curAreaIdxes.at(lane))) {
            RuntimePadEvent cause;
            if (!padStateForTick(padStates, pad, &cause)) {
                continue;
            }

            note->pressingPads[lane] = pad;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].judged = true;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].second = nowSecond;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].cause = cause;
            if (note->curAreaIdxes.at(lane) >= note->totalAreaNum - 1) {
                ++note->curAreaIdxes[lane];
                note->laneProgressSeconds[lane][note->curAreaIdxes.at(lane)] = nowSecond;
                note->laneFinished[lane] = true;
                note->judgeAction = cause;
            }
            return true;
        }
    } else {
        if (!padStates.contains(note->pressingPads.at(lane))) {
            note->pressingPads[lane].clear();
            ++note->curAreaIdxes[lane];
            note->laneProgressSeconds[lane][note->curAreaIdxes.at(lane)] = nowSecond;
            return true;
        }
    }

    if (note->curAreaIdxes.at(lane) < note->totalAreaNum - 1) {
        for (const QString& pad : note->laneJudgeSequence.at(lane).at(note->curAreaIdxes.at(lane) + 1)) {
            RuntimePadEvent cause;
            RuntimePadEvent padStateCause;
            const bool down = padStateForTick(padStates, pad, &padStateCause);
            const bool up = padReleasedThisTick(padUpThisTick, pad, &cause);
            if (!down && !up) {
                continue;
            }

            note->pressingPads[lane] = pad;
            ++note->curAreaIdxes[lane];
            note->laneProgressSeconds[lane][note->curAreaIdxes.at(lane)] = nowSecond;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].judged = true;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].second = nowSecond;
            note->areaHits[lane][note->curAreaIdxes.at(lane)].cause = down ? padStateCause : cause;
            if (note->curAreaIdxes.at(lane) >= note->totalAreaNum - 1) {
                ++note->curAreaIdxes[lane];
                note->laneProgressSeconds[lane][note->curAreaIdxes.at(lane)] = nowSecond;
                note->laneFinished[lane] = true;
                note->judgeAction = down ? padStateCause : RuntimePadEvent{};
            }
            return true;
        }
    }

    return false;
}

void updateRuntimeWifiNote(
    RuntimeWifiNoteState* note,
    double nowSecond,
    const QHash<QString, RuntimePadEvent>& padStates,
    const QHash<QString, RuntimePadEvent>& padUpThisTick)
{
    if (note == nullptr || note->judged || nowSecond + kPadTimeEpsilon < note->availableSecond) {
        return;
    }

    for (int lane = 0; lane < note->laneJudgeSequence.size(); ++lane) {
        while (note->curAreaIdxes.at(lane) < note->totalAreaNum) {
            if (!progressRuntimeWifiLaneOnce(note, nowSecond, lane, padStates, padUpThisTick)) {
                break;
            }
        }
    }

    if (!note->padCPassed && note->curAreaIdxes.value(1) > 0) {
        RuntimePadEvent cause;
        if (padReleasedThisTick(padUpThisTick, QStringLiteral("C"), &cause)) {
            note->padCPassed = true;
            note->padCSecond = nowSecond;
            if (note->areaHits.size() > 1 && note->areaHits.at(1).size() > 2) {
                note->areaHits[1][2].judged = true;
                note->areaHits[1][2].second = nowSecond;
                note->areaHits[1][2].cause = cause;
            }
        }
    }

    if (std::all_of(note->laneFinished.cbegin(), note->laneFinished.cend(), [](bool finished) { return finished; })
        && note->padCPassed) {
        note->judged = true;
        note->judgeSecond = nowSecond;
        note->judgeBad =
            wifiJudgeIsBad(nowSecond, note->criticalSecond, note->criticalDeltaSecond)
            && wifiFinalJudgeWasByOther(*note);
        return;
    }

    if (nowSecond > note->endSecond + miacode::muri::kSlideAvailableSeconds + kPadTimeEpsilon) {
        note->judged = true;
        note->judgeBad = true;
        note->judgeSecond = nowSecond;
    }
}

// --- entry point: the time-stepped simulation ----------------------------------

QHash<QString, RuntimeSlideJudgeResult> simulateRuntimeSlideAndWifiJudgments(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<JudgeableSimpleNote>& notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex,
    const MuriRenderOptions& renderOptions)
{
    QVector<RuntimeSlideNoteState> slideNotes;
    QVector<RuntimeWifiNoteState> wifiNotes;
    slideNotes.reserve(noteMarkers.size());
    wifiNotes.reserve(noteMarkers.size());

    for (const TimelineNoteMarker& marker : noteMarkers) {
        // Mine slides/wifi are dodged by autoplay — exclude them from the
        // runtime judgment simulation entirely (no judged result is consumed,
        // and they must not occupy pads that real notes are judged against).
        if (marker.isMine || marker.trackMine) {
            continue;
        }
        if (marker.type == QLatin1String("slide")) {
            RuntimeSlideNoteState note;
            if (buildRuntimeSlideNoteState(marker, &note)) {
                slideNotes.append(note);
            }
            continue;
        }
        if (marker.type == QLatin1String("wifi")) {
            RuntimeWifiNoteState note;
            if (buildRuntimeWifiNoteState(marker, &note, renderOptions)) {
                wifiNotes.append(note);
            }
        }
    }

    QHash<QString, RuntimeSlideJudgeResult> results;
    if (slideNotes.isEmpty() && wifiNotes.isEmpty()) {
        return results;
    }

    const QVector<RuntimeHandAction> actions =
        buildRuntimeHandActions(
            noteMarkers,
            notes,
            touchGroups,
            touchGroupByChildNoteIndex,
            true);
    int maxActionTick = 0;
    for (const RuntimeHandAction& action : actions) {
        const int actionEndTick = judgeTickForPadActiveEnd(action.endSecond);
        maxActionTick = qMax(maxActionTick, actionEndTick);
    }

    static const QStringList allPads = []() {
        QStringList pads;
        pads.reserve(33);
        for (const QChar ring : {QLatin1Char('A'), QLatin1Char('B'), QLatin1Char('D'), QLatin1Char('E')}) {
            for (int lane = 1; lane <= 8; ++lane) {
                pads.append(QStringLiteral("%1%2").arg(ring).arg(lane));
            }
        }
        pads.append(QStringLiteral("C"));
        return pads;
    }();

    QHash<QString, RuntimePadEvent> previousPadStates;
    QVector<int> activeActionIndices;
    int actionPointer = 0;
    for (int tick = 0; tick <= maxActionTick; ++tick) {
        const double nowSecond = tickToSecond(tick);
        while (actionPointer < actions.size()) {
            const RuntimeHandAction& action = actions.at(actionPointer);
            if (nowSecond + kPadTimeEpsilon < action.startSecond) {
                break;
            }
            activeActionIndices.append(actionPointer);
            ++actionPointer;
        }

        const QVector<RuntimeTouchPoint> touchPoints =
            buildRuntimeTouchPoints(actions, activeActionIndices, nowSecond);
        QHash<QString, RuntimePadEvent> padStates;
        for (const RuntimeTouchPoint& touchPoint : touchPoints) {
            if (touchPoint.actionIndex < 0 || touchPoint.actionIndex >= actions.size()) {
                continue;
            }
            const RuntimeHandAction& action = actions.at(touchPoint.actionIndex);
            RuntimePadEvent event;
            event.tick = tick;
            event.second = nowSecond;
            event.sourceMarkerKey = action.markerKey;
            event.sourceType = action.sourceType;
            event.sourceOrder = action.order;
            event.line = action.line;
            event.col = action.col;

            for (const QString& pad : allPads) {
                if (pointDistance(touchPoint.center, miacode::muri::padCenter(pad))
                    > touchPoint.radius + miacode::muri::padRadius(pad) + kPadTimeEpsilon) {
                    continue;
                }

                event.pad = pad;
                padStates.insert(pad, event);
            }
        }

        QHash<QString, RuntimePadEvent> padUpThisTick;
        for (auto it = previousPadStates.constBegin(); it != previousPadStates.constEnd(); ++it) {
            if (!padStates.contains(it.key())) {
                padUpThisTick.insert(it.key(), it.value());
            }
        }

        for (RuntimeSlideNoteState& note : slideNotes) {
            updateRuntimeSlideNote(&note, nowSecond, padStates, padUpThisTick);
        }
        for (RuntimeWifiNoteState& note : wifiNotes) {
            updateRuntimeWifiNote(&note, nowSecond, padStates, padUpThisTick);
        }

        previousPadStates = padStates;

        QVector<int> finishedActionIndices;
        finishedActionIndices.reserve(activeActionIndices.size());
        for (int actionIndex : activeActionIndices) {
            if (actionIndex < 0 || actionIndex >= actions.size()) {
                continue;
            }
            if (nowSecond + kPadTimeEpsilon >= actions.at(actionIndex).endSecond) {
                finishedActionIndices.append(actionIndex);
            }
        }
        for (int actionIndex : finishedActionIndices) {
            activeActionIndices.removeAll(actionIndex);
        }
    }

    for (RuntimeSlideNoteState& note : slideNotes) {
        if (note.judged) {
            continue;
        }
        note.judged = true;
        note.judgeBad = false;
        note.judgeSecond = qMax(note.endSecond, note.criticalSecond);
    }
    for (RuntimeWifiNoteState& note : wifiNotes) {
        if (note.judged) {
            continue;
        }
        note.judged = true;
        note.judgeBad = false;
        note.judgeSecond = qMax(note.endSecond, note.criticalSecond);
    }

    for (const RuntimeSlideNoteState& note : slideNotes) {
        RuntimeSlideJudgeResult result;
        result.valid = true;
        result.isWifi = false;
        result.judgeBad = note.judgeBad;
        result.judgeSecond = note.judgeSecond;
        result.criticalSecond = note.criticalSecond;
        result.segmentCompletedSeconds.reserve(note.segmentEndAreaIndices.size());
        result.areaHits = note.areaHits;
        result.judgeSequence = note.judgeSequence;
        result.segmentStartAreaIndices = note.segmentStartAreaIndices;
        result.segmentEndAreaIndices = note.segmentEndAreaIndices;
        for (int segmentIndex = 0; segmentIndex < note.segmentEndAreaIndices.size(); ++segmentIndex) {
            const int areaIndex = note.segmentEndAreaIndices.at(segmentIndex);
            double completedSecond = (areaIndex >= 0 && areaIndex < note.areaHits.size())
                ? note.areaHits.at(areaIndex).second
                : -1.0;
            if (completedSecond < 0.0 && segmentIndex + 1 == note.segmentEndAreaIndices.size()) {
                completedSecond = note.judgeSecond;
            }
            result.segmentCompletedSeconds.append(completedSecond);
        }
        results.insert(note.markerKey, result);
    }
    for (const RuntimeWifiNoteState& note : wifiNotes) {
        RuntimeSlideJudgeResult result;
        result.valid = true;
        result.isWifi = true;
        result.judgeBad = note.judgeBad;
        result.judgeSecond = note.judgeSecond;
        result.criticalSecond = note.criticalSecond;
        result.segmentCompletedSeconds.append(note.judgeSecond);
        result.wifiLaneAreaHits = note.areaHits;
        result.wifiLaneJudgeSequence = note.laneJudgeSequence;
        result.wifiLaneProgressSeconds = note.laneProgressSeconds;
        result.wifiPadCSecond = note.padCSecond;
        results.insert(note.markerKey, result);
    }

    return results;
}

void applyRuntimeJudgeResultToState(const RuntimeSlideJudgeResult& result, MarkerMuriState* state)
{
    if (state == nullptr || !result.valid) {
        return;
    }

    if (result.isWifi) {
        if (!result.segmentCompletedSeconds.isEmpty()) {
            state->wifiCompletedSecond = result.segmentCompletedSeconds.constFirst();
        }
        QVector<QVector<QVector<MuriCheckpointState>>> runtimeLanes;
        runtimeLanes.reserve(result.wifiLaneAreaHits.size());
        for (int laneIndex = 0; laneIndex < result.wifiLaneAreaHits.size(); ++laneIndex) {
            QVector<QVector<MuriCheckpointState>> laneAreas;
            const QVector<RuntimeJudgeHit>& laneHits = result.wifiLaneAreaHits.at(laneIndex);
            const QVector<QStringList>& lanePads = result.wifiLaneJudgeSequence.value(laneIndex);
            laneAreas.reserve(laneHits.size());
            for (int areaIndex = 0; areaIndex < laneHits.size(); ++areaIndex) {
                QVector<MuriCheckpointState> checkpoints;
                const RuntimeJudgeHit& hit = laneHits.at(areaIndex);
                if (hit.judged) {
                    MuriCheckpointState checkpoint;
                    checkpoint.second = hit.second;
                    checkpoint.pads = lanePads.value(areaIndex);
                    checkpoint.skipped = hit.skipped;
                    checkpoint.causeMarkerKey = hit.cause.sourceMarkerKey;
                    checkpoint.causeType = hit.skipped
                        ? QStringLiteral("skipped")
                        : hit.cause.sourceType;
                    if (hit.cause.sourceMarkerKey.isEmpty()) {
                        checkpoint.cause = AreaJudgeCause::Unknown;
                    } else if (hit.cause.sourceMarkerKey == state->markerKey) {
                        checkpoint.cause = AreaJudgeCause::Self;
                    } else {
                        checkpoint.cause = AreaJudgeCause::Other;
                    }
                    checkpoints.append(checkpoint);
                }
                laneAreas.append(checkpoints);
            }
            runtimeLanes.append(laneAreas);
        }
        if (!runtimeLanes.isEmpty()) {
            state->wifiLaneAreas = runtimeLanes;
        }
        if (!result.wifiLaneProgressSeconds.isEmpty()) {
            state->wifiLaneProgressSeconds = result.wifiLaneProgressSeconds;
        }
        state->wifiPadCSecond = result.wifiPadCSecond;
    } else {
        int fallbackStartAreaIndex = 0;
        for (int index = 0; index < result.segmentCompletedSeconds.size() && index < state->slideSegments.size(); ++index) {
            if (result.segmentCompletedSeconds.at(index) >= 0.0) {
                state->slideSegments[index].completedSecond = result.segmentCompletedSeconds.at(index);
            }

            QVector<QVector<MuriCheckpointState>> runtimeAreas;
            const int segmentStartAreaIndex = result.segmentStartAreaIndices.value(index, fallbackStartAreaIndex);
            const int segmentEndAreaIndex = result.segmentEndAreaIndices.value(index, segmentStartAreaIndex - 1);
            for (int areaIndex = segmentStartAreaIndex;
                 areaIndex <= segmentEndAreaIndex && areaIndex < result.areaHits.size();
                 ++areaIndex) {
                QVector<MuriCheckpointState> checkpoints;
                const RuntimeJudgeHit& hit = result.areaHits.at(areaIndex);
                if (hit.judged) {
                    MuriCheckpointState checkpoint;
                    checkpoint.second = hit.second;
                    checkpoint.pads = result.judgeSequence.value(areaIndex);
                    checkpoint.skipped = hit.skipped;
                    checkpoint.causeMarkerKey = hit.cause.sourceMarkerKey;
                    checkpoint.causeType = hit.skipped
                        ? QStringLiteral("skipped")
                        : hit.cause.sourceType;
                    if (hit.cause.sourceMarkerKey.isEmpty()) {
                        checkpoint.cause = AreaJudgeCause::Unknown;
                    } else if (hit.cause.sourceMarkerKey == state->markerKey) {
                        checkpoint.cause = AreaJudgeCause::Self;
                    } else {
                        checkpoint.cause = AreaJudgeCause::Other;
                    }
                    checkpoints.append(checkpoint);
                }
                runtimeAreas.append(checkpoints);
            }
            fallbackStartAreaIndex = segmentEndAreaIndex + 1;
            if (!runtimeAreas.isEmpty()) {
                state->slideSegments[index].areaCheckpoints = runtimeAreas;
            }
        }
    }

    state->earlyCleared = result.judgeBad;
    state->flashSecond = result.judgeBad ? result.judgeSecond : -1.0;
}

// --- slide-too-fast diagnostic support -----------------------------------------

struct EarlyJudgeCauseInfo {
    bool valid = false;
    double second = -1.0;
    QString markerKey;
    QString causeType;
};

void updateLatestEarlyJudgeCause(const MuriCheckpointState& checkpoint, EarlyJudgeCauseInfo* latestCause)
{
    if (latestCause == nullptr
        || checkpoint.second < 0.0
        || checkpoint.cause != AreaJudgeCause::Other
        || checkpoint.causeMarkerKey.isEmpty()) {
        return;
    }

    if (!latestCause->valid || checkpoint.second > latestCause->second + kPadTimeEpsilon) {
        latestCause->valid = true;
        latestCause->second = checkpoint.second;
        latestCause->markerKey = checkpoint.causeMarkerKey;
        latestCause->causeType = checkpoint.causeType;
    }
}

EarlyJudgeCauseInfo latestEarlyJudgeCauseForState(const MarkerMuriState& state)
{
    EarlyJudgeCauseInfo latestCause;
    for (const MuriSegmentState& segment : state.slideSegments) {
        for (const QVector<MuriCheckpointState>& area : segment.areaCheckpoints) {
            for (const MuriCheckpointState& checkpoint : area) {
                updateLatestEarlyJudgeCause(checkpoint, &latestCause);
            }
        }
    }
    for (const QVector<QVector<MuriCheckpointState>>& laneAreas : state.wifiLaneAreas) {
        for (const QVector<MuriCheckpointState>& area : laneAreas) {
            for (const MuriCheckpointState& checkpoint : area) {
                updateLatestEarlyJudgeCause(checkpoint, &latestCause);
            }
        }
    }
    return latestCause;
}

DiagnosticAnchor diagnosticAnchorForSlideJudge(
    const TimelineNoteMarker& marker,
    const MarkerMuriState& state,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    const EarlyJudgeCauseInfo latestCause = latestEarlyJudgeCauseForState(state);
    if (latestCause.valid) {
        const DiagnosticAnchor causeAnchor = diagnosticAnchorForMarkerKey(
            latestCause.markerKey,
            markerRefs,
            syntheticSlideHeadOwnerKeys);
        if (causeAnchor.valid) {
            return causeAnchor;
        }
    }
    return diagnosticAnchorFromMarker(marker);
}

MuriDetailArgs slideTooFastDetailArgs(
    const TimelineNoteMarker& marker,
    const MarkerMuriState& state,
    const QHash<QString, QString>& markerConfigLabels,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys,
    MuriDetailKind* detailKind)
{
    const EarlyJudgeCauseInfo latestCause = latestEarlyJudgeCauseForState(state);
    const double expectedSecond = marker.type == QLatin1String("wifi")
        ? state.wifiExpectedCompletedSecond
        : (state.slideSegments.isEmpty() ? -1.0 : state.slideSegments.constLast().expectedCompletedSecond);
    const double resolvedSecond = marker.type == QLatin1String("wifi")
        ? state.wifiCompletedSecond
        : (state.slideSegments.isEmpty() ? -1.0 : state.slideSegments.constLast().completedSecond);
    const double normalJudgeSecond = marker.endSecond >= 0.0 ? marker.endSecond : expectedSecond;
    const double gapMs = qMax(0.0, normalJudgeSecond - resolvedSecond) * 1000.0;
    const QString targetLabel = formatMarkerConfigLabel(marker);
    MuriDetailArgs args;
    args.left = targetLabel;
    args.gapText = QStringLiteral("%1 ms").arg(QString::number(gapMs, 'f', 1));
    args.alert = MuriAlertLevel::Muri;
    if (latestCause.valid) {
        const QString causeLabel = markerConfigLabelForSource(
            markerConfigLabels,
            syntheticSlideHeadOwnerKeys,
            latestCause.markerKey,
            latestCause.causeType);
        args.right = causeLabel;
        if (detailKind != nullptr) {
            *detailKind = MuriDetailKind::EarlyJudgedBy;
        }
        return args;
    }
    if (detailKind != nullptr) {
        *detailKind = MuriDetailKind::ResolvedOutsideWindow;
    }
    return args;
}

QString formatSlideTooFastDetail(
    const TimelineNoteMarker& marker,
    const MarkerMuriState& state,
    const QHash<QString, QString>& markerConfigLabels,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    MuriDetailKind detailKind = MuriDetailKind::None;
    const MuriDetailArgs args = slideTooFastDetailArgs(
        marker,
        state,
        markerConfigLabels,
        syntheticSlideHeadOwnerKeys,
        &detailKind);
    return renderMuriDetail(detailKind, args, SimaiNativeValidationLocale::English);
}

}  // namespace miacode::muri::detail
