#include "tools/muri/MuriOverlayBuilder.h"

#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"
#include "common/MuriTypes.h"  // makeMarkerAnalysisKey
#include "tools/muri/MuriAnalyzerModel.h"     // PadWindowIndex
#include "tools/muri/MuriAnalyzerInternal.h"  // pad-window/trail primitives + slide-judge math

namespace miacode::muri::detail {

constexpr double kPadTimeEpsilon = 1e-6;

// --- buildSlideState internals -------------------------------------------------
// These four helpers are private to the slide-state reconstruction below; they
// are defined before their callers so no forward declaration is needed.

QVector<QStringList> checkpointPadsForArea(
    const QVector<MuriPadTimeEntry>& padTimes,
    const QVector<double>& checkpointTimes)
{
    QVector<QStringList> result;
    result.reserve(checkpointTimes.size());
    for (double checkpoint : checkpointTimes) {
        QStringList pads;
        for (const MuriPadTimeEntry& entry : padTimes) {
            if (qAbs(entry.proportion - checkpoint) <= kPadTimeEpsilon && !entry.pad.isEmpty()) {
                pads.append(entry.pad);
            }
        }
        pads.removeDuplicates();
        result.append(pads);
    }
    return result;
}

PadWindowIndex earliestPadWindow(
    const QHash<QString, QVector<int>>& windowsByPad,
    const QVector<MuriPadWindow>& windows,
    const QStringList& pads,
    double earliestSecond)
{
    PadWindowIndex best;
    for (const QString& pad : pads) {
        const auto it = windowsByPad.constFind(pad);
        if (it == windowsByPad.constEnd()) {
            continue;
        }
        for (int windowIndex : it.value()) {
            if (windowIndex < 0 || windowIndex >= windows.size()) {
                continue;
            }
            const MuriPadWindow& window = windows.at(windowIndex);
            if (window.endSecond + kPadTimeEpsilon < earliestSecond) {
                continue;
            }
            const double activeSecond = qMax(earliestSecond, window.startSecond);
            if (best.index < 0 || activeSecond + kPadTimeEpsilon < best.activeSecond) {
                best.index = windowIndex;
                best.activeSecond = activeSecond;
            }
        }
    }
    return best;
}

double completionSecondForArea(const QVector<MuriCheckpointState>& checkpoints)
{
    double second = -1.0;
    for (const MuriCheckpointState& checkpoint : checkpoints) {
        second = qMax(second, checkpoint.second);
    }
    return second;
}

QVector<MuriCheckpointState> buildCheckpointStates(
    const QVector<double>& checkpointTimes,
    const QVector<QStringList>& checkpointPads,
    double segmentShootSecond,
    double durationSecond,
    double earliestAllowedSecond,
    const QHash<QString, QVector<int>>& windowsByPad,
    const QVector<MuriPadWindow>& allWindows,
    const QString& selfMarkerKey)
{
    QVector<MuriCheckpointState> states;
    states.reserve(checkpointTimes.size());

    double cursorSecond = earliestAllowedSecond;
    for (int index = 0; index < checkpointTimes.size(); ++index) {
        MuriCheckpointState state;
        state.proportion = checkpointTimes.at(index);
        state.pads = checkpointPads.value(index);
        const PadWindowIndex best = earliestPadWindow(windowsByPad, allWindows, state.pads, earliestAllowedSecond);
        double resolvedSecond = segmentShootSecond + state.proportion * durationSecond;
        if (best.index >= 0) {
            const MuriPadWindow& window = allWindows.at(best.index);
            resolvedSecond = qMin(resolvedSecond, best.activeSecond);
            state.causeMarkerKey = window.sourceMarkerKey;
            state.causeType = window.sourceType;
            state.cause = window.sourceMarkerKey == selfMarkerKey ? AreaJudgeCause::Self : AreaJudgeCause::Other;
        } else {
            state.cause = AreaJudgeCause::Self;
            state.causeMarkerKey = selfMarkerKey;
            state.causeType = QStringLiteral("slide");
        }
        if (resolvedSecond + kPadTimeEpsilon < cursorSecond) {
            resolvedSecond = cursorSecond;
        }
        state.second = resolvedSecond;
        cursorSecond = qMax(cursorSecond, resolvedSecond);
        states.append(state);
    }

    return states;
}

// --- stage entry points --------------------------------------------------------

MarkerMuriState buildSlideState(
    const TimelineNoteMarker& marker,
    const QHash<QString, QVector<int>>& windowsByPad,
    const QVector<MuriPadWindow>& allWindows)
{
    MarkerMuriState state;
    state.markerKey = makeMarkerAnalysisKey(marker);
    state.markerType = marker.type;
    state.line = marker.sourceLine;
    state.col = marker.sourceCol;
    state.second = marker.second;
    state.endSecond = marker.endSecond;

    const int segmentCount = qMin(
        qMin(marker.slideTrackAreaCheckpoints.size(), marker.slideSegmentShootSeconds.size()),
        qMin(marker.slideSegmentDurations.size(), marker.slideSegmentPadEnterTimes.size())
    );
    state.slideSegments.reserve(segmentCount);
    double chainCursorSecond = marker.second;
    for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
        MuriSegmentState segmentState;
        const double shootSecond = marker.slideSegmentShootSeconds.at(segmentIndex);
        const double durationSecond = qMax(0.0, marker.slideSegmentDurations.at(segmentIndex));
        const QVector<MuriPadTimeEntry>& padTimes = marker.slideSegmentPadEnterTimes.at(segmentIndex);
        const QVector<QVector<double>>& areas = marker.slideTrackAreaCheckpoints.at(segmentIndex);
        segmentState.areaCheckpoints.reserve(areas.size());

        double cursorSecond = chainCursorSecond;
        bool sawCheckpoint = false;
        for (const QVector<double>& area : areas) {
            const QVector<QStringList> pads = checkpointPadsForArea(padTimes, area);
            QVector<MuriCheckpointState> checkpointStates = buildCheckpointStates(
                area,
                pads,
                shootSecond,
                durationSecond,
                cursorSecond,
                windowsByPad,
                allWindows,
                state.markerKey
            );
            if (!checkpointStates.isEmpty()) {
                sawCheckpoint = true;
                const double areaSecond = completionSecondForArea(checkpointStates);
                if (areaSecond + kPadTimeEpsilon < cursorSecond) {
                    for (MuriCheckpointState& checkpointState : checkpointStates) {
                        checkpointState.second = cursorSecond;
                    }
                }
                cursorSecond = qMax(cursorSecond, completionSecondForArea(checkpointStates));
            }
            segmentState.areaCheckpoints.append(checkpointStates);
        }

        double expectedCompleted = shootSecond;
        for (const MuriPadTimeEntry& entry : padTimes) {
            expectedCompleted = qMax(expectedCompleted, shootSecond + entry.proportion * durationSecond);
        }
        segmentState.expectedCompletedSecond = expectedCompleted;
        segmentState.completedSecond = sawCheckpoint ? cursorSecond : expectedCompleted;
        if (segmentIndex < marker.slideSegmentCriticalProportions.size()) {
            segmentState.criticalSecond =
                shootSecond + marker.slideSegmentCriticalProportions.at(segmentIndex) * durationSecond;
        } else {
            segmentState.criticalSecond = expectedCompleted;
        }
        chainCursorSecond = qMax(chainCursorSecond, segmentState.completedSecond);
        state.slideSegments.append(segmentState);
    }

    if (!state.slideSegments.isEmpty()) {
        const MuriSegmentState& lastSegment = state.slideSegments.constLast();
        const int lastSegmentIndex = state.slideSegments.size() - 1;
        const double lastDurationSecond = lastSegmentIndex < marker.slideSegmentDurations.size()
            ? qMax(0.0, marker.slideSegmentDurations.at(lastSegmentIndex))
            : qMax(0.0, marker.endSecond - marker.slideTraceSecond);
        const double criticalProportion = lastSegmentIndex < marker.slideSegmentCriticalProportions.size()
            ? marker.slideSegmentCriticalProportions.at(lastSegmentIndex)
            : 1.0;
        const double criticalDeltaSecond = slideCriticalDeltaSecond((1.0 - criticalProportion) * lastDurationSecond);
        if (slideJudgeIsBad(lastSegment.completedSecond, lastSegment.criticalSecond, criticalDeltaSecond)) {
            state.earlyCleared = true;
            state.flashSecond = lastSegment.completedSecond;
        }
    }

    return state;
}

MarkerMuriState buildWifiState(const TimelineNoteMarker& marker)
{
    MarkerMuriState state;
    state.markerKey = makeMarkerAnalysisKey(marker);
    state.markerType = marker.type;
    state.line = marker.sourceLine;
    state.col = marker.sourceCol;
    state.second = marker.second;
    state.endSecond = marker.endSecond;

    const double durationSecond = qMax(0.0, marker.endSecond - marker.slideTraceSecond);
    double expectedCompleted = marker.slideTraceSecond;
    for (const MuriPadTimeEntry& entry : marker.wifiPadEnterTimes) {
        expectedCompleted = qMax(expectedCompleted, marker.slideTraceSecond + entry.proportion * durationSecond);
    }
    state.wifiExpectedCompletedSecond = expectedCompleted;
    state.wifiCompletedSecond = expectedCompleted;
    state.wifiCriticalSecond = marker.slideTraceSecond + marker.wifiCriticalProportion * durationSecond;
    const double criticalDeltaSecond = slideCriticalDeltaSecond(
        (1.0 - marker.wifiCriticalProportion) * durationSecond
    );
    if (wifiJudgeIsBad(state.wifiCompletedSecond, state.wifiCriticalSecond, criticalDeltaSecond)) {
        state.earlyCleared = true;
        state.flashSecond = state.wifiCompletedSecond;
    }
    return state;
}

void buildOverlayActions(
    const QVector<TimelineNoteMarker>& noteMarkers,
    QVector<MuriPadWindow>* padWindows,
    QVector<MuriActionTrail>* actionTrails)
{
    using namespace miacode::muri;
    if (padWindows == nullptr || actionTrails == nullptr) {
        return;
    }

    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString markerKey = makeMarkerAnalysisKey(marker);
        const QString pad = notePad(marker);
        if (marker.type == QLatin1String("tap")) {
            if (!marker.slideHead) {
                addPadWindow(padWindows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
                addActionTrail(actionTrails, markerKey, marker.type, marker.second, notePressEndSecond(marker), kHandRadiusNormal, {miacode::muri::padCenter(pad)});
            }
            continue;
        }
        if (marker.type == QLatin1String("hold")) {
            addPadWindow(padWindows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
            addActionTrail(actionTrails, markerKey, marker.type, marker.second, notePressEndSecond(marker), kHandRadiusNormal, {miacode::muri::padCenter(pad)});
            continue;
        }
        if (marker.type == QLatin1String("touch")) {
            if (!marker.onSlide) {
                addPadWindow(padWindows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
                addActionTrail(actionTrails, markerKey, marker.type, marker.second, notePressEndSecond(marker), kHandRadiusNormal, {marker.touchPoint});
            }
            continue;
        }
        if (marker.type == QLatin1String("touch_hold")) {
            addPadWindow(padWindows, pad, marker.second, notePressEndSecond(marker), markerKey, marker.type);
            addActionTrail(actionTrails, markerKey, marker.type, marker.second, notePressEndSecond(marker), kHandRadiusNormal, {marker.touchPoint});
            continue;
        }
        if (marker.type == QLatin1String("slide")) {
            addSlidePadWindowsAndTrails(marker, markerKey, padWindows, actionTrails);
            continue;
        }
        if (marker.type == QLatin1String("wifi")) {
            addWifiPadWindowsAndTrails(marker, markerKey, padWindows, actionTrails);
            continue;
        }
    }

}

}  // namespace miacode::muri::detail
