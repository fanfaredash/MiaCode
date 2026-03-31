#include "timeline/TimelineSlowRefresh.h"

#include "common/MuriTypes.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriStaticChecker.h"

namespace {

double shiftedTimelineSecond(double second, double offsetSeconds)
{
    if (!qIsFinite(second) || !qIsFinite(offsetSeconds)) {
        return second;
    }
    return second + offsetSeconds;
}

QVector<TimelineBeatMarker> shiftedBeatMarkers(
    const QVector<TimelineBeatMarker>& beatMarkers,
    double offsetSeconds)
{
    QVector<TimelineBeatMarker> shifted = beatMarkers;
    for (TimelineBeatMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds);
    }
    return shifted;
}

QVector<TimelineNoteMarker> shiftedNoteMarkers(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double offsetSeconds)
{
    QVector<TimelineNoteMarker> shifted = noteMarkers;
    for (TimelineNoteMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds);
        if (marker.endSecond >= 0.0) {
            marker.endSecond = shiftedTimelineSecond(marker.endSecond, offsetSeconds);
        }
        if (marker.slideTraceSecond >= 0.0) {
            marker.slideTraceSecond = shiftedTimelineSecond(marker.slideTraceSecond, offsetSeconds);
        }
        if (marker.availableSecond >= 0.0) {
            marker.availableSecond = shiftedTimelineSecond(marker.availableSecond, offsetSeconds);
        }
        for (double& shootSecond : marker.slideSegmentShootSeconds) {
            shootSecond = shiftedTimelineSecond(shootSecond, offsetSeconds);
        }
    }
    return shifted;
}

QByteArray noteMarkerSignature(const QVector<TimelineNoteMarker>& notes)
{
    QByteArray signature;
    signature.reserve(notes.size() * 128);
    for (const TimelineNoteMarker& marker : notes) {
        signature.append(QByteArray::number(marker.sourceLine));
        signature.append('|');
        signature.append(QByteArray::number(marker.sourceCol));
        signature.append('|');
        signature.append(QByteArray::number(marker.lane));
        signature.append('|');
        signature.append(QByteArray::number(marker.endLane));
        signature.append('|');
        signature.append(QByteArray::number(marker.second, 'f', 6));
        signature.append('|');
        signature.append(QByteArray::number(marker.endSecond, 'f', 6));
        signature.append('|');
        signature.append(QByteArray::number(marker.slideTraceSecond, 'f', 6));
        signature.append('|');
        signature.append(marker.type.toUtf8());
        signature.append('|');
        signature.append(marker.slideTrackKey.toUtf8());
        signature.append('|');
        signature.append(QByteArray::number(marker.slideSegmentKeys.size()));
        signature.append('|');
        for (int index = 0; index < marker.slideSegmentKeys.size(); ++index) {
            signature.append(marker.slideSegmentKeys.at(index).toUtf8());
            signature.append(',');
        }
        signature.append('|');
        signature.append(QByteArray::number(marker.slideSegmentShootSeconds.size()));
        signature.append('|');
        for (double second : marker.slideSegmentShootSeconds) {
            signature.append(QByteArray::number(second, 'f', 6));
            signature.append(',');
        }
        signature.append('|');
        signature.append(QByteArray::number(marker.slideSegmentDurations.size()));
        signature.append('|');
        for (double duration : marker.slideSegmentDurations) {
            signature.append(QByteArray::number(duration, 'f', 6));
            signature.append(',');
        }
        signature.append('|');
        signature.append(QByteArray::number(marker.eachGroupId));
        signature.append('|');
        signature.append(marker.isEach ? '1' : '0');
        signature.append('|');
        signature.append(marker.isBreak ? '1' : '0');
        signature.append('|');
        signature.append(marker.isEx ? '1' : '0');
        signature.append('|');
        signature.append(marker.isFirework ? '1' : '0');
        signature.append('|');
        signature.append(marker.headBreak ? '1' : '0');
        signature.append('|');
        signature.append(marker.trackBreak ? '1' : '0');
        signature.append('|');
        signature.append(marker.headEx ? '1' : '0');
        signature.append('|');
        signature.append(marker.headEach ? '1' : '0');
        signature.append('|');
        signature.append(marker.slideEach ? '1' : '0');
        signature.append('|');
        signature.append(marker.sameHeadSlide ? '1' : '0');
        signature.append('|');
        signature.append(marker.slideHead ? '1' : '0');
        signature.append('|');
        signature.append(marker.tailOnSlideHead ? '1' : '0');
        signature.append('|');
        signature.append(marker.onSlide ? '1' : '0');
        signature.append('|');
        signature.append(marker.beforeSlide ? '1' : '0');
        signature.append('|');
        signature.append(marker.afterSlide ? '1' : '0');
        signature.append('|');
        signature.append(marker.hasHeadStar ? '1' : '0');
        signature.append('|');
        signature.append(marker.touchPad.toUtf8());
        signature.append('\n');
    }
    return signature;
}

double computeDurationSeconds(
    const SimaiNativeParseResult& parseResult,
    const QVector<TimelineBeatMarker>& beatMarkers,
    const QVector<TimelineNoteMarker>& noteMarkers,
    double firstSeconds)
{
    double durationSeconds = qMax(0.0, parseResult.durationSeconds + firstSeconds);
    for (const TimelineNoteMarker& marker : noteMarkers) {
        durationSeconds = qMax(durationSeconds, marker.second);
        if (marker.endSecond > marker.second) {
            durationSeconds = qMax(durationSeconds, marker.endSecond);
        }
    }
    for (const TimelineBeatMarker& marker : beatMarkers) {
        durationSeconds = qMax(durationSeconds, marker.second);
    }
    return durationSeconds;
}

}  // namespace

TimelinePreviewRefreshState buildTimelinePreviewRefreshState(
    const SimaiNativeParseResult& parseResult,
    double firstSeconds)
{
    TimelinePreviewRefreshState state;
    state.shiftedNoteMarkers = shiftedNoteMarkers(parseResult.noteMarkers, firstSeconds);
    state.noteMarkerSignature = noteMarkerSignature(state.shiftedNoteMarkers);
    return state;
}

TimelinePreviewRefreshResult buildTimelinePreviewRefreshResult(
    const TimelineSlowRefreshRequest& request,
    const SimaiNativeParseResult& parseResult,
    const TimelinePreviewRefreshState& previewState)
{
    TimelinePreviewRefreshResult result;
    result.revision = request.revision;
    result.difficultyId = request.difficultyId;
    result.chartText = request.chartText;
    result.firstSeconds = request.firstSeconds;
    result.parseResult = parseResult;
    result.shiftedBeatMarkers = shiftedBeatMarkers(result.parseResult.beatMarkers, request.firstSeconds);
    result.shiftedNoteMarkers = previewState.shiftedNoteMarkers;
    result.noteMarkerSignature = previewState.noteMarkerSignature;
    result.durationSeconds = computeDurationSeconds(
        result.parseResult,
        result.shiftedBeatMarkers,
        result.shiftedNoteMarkers,
        request.firstSeconds);
    return result;
}

TimelinePreviewRefreshResult buildTimelinePreviewRefreshResult(const TimelineSlowRefreshRequest& request)
{
    const SimaiNativeParseResult parseResult = SimaiNativeParser::parseForTimeline(request.chartText);
    const TimelinePreviewRefreshState previewState = buildTimelinePreviewRefreshState(
        parseResult,
        request.firstSeconds);
    return buildTimelinePreviewRefreshResult(request, parseResult, previewState);
}

TimelinePreviewRefreshState buildTimelinePreviewRefreshState(const QString& chartText, double firstSeconds)
{
    return buildTimelinePreviewRefreshState(SimaiNativeParser::parseForTimeline(chartText), firstSeconds);
}

TimelineValidationRefreshResult buildTimelineValidationRefreshResult(const TimelineValidationRefreshRequest& request)
{
    TimelineValidationRefreshResult result;
    result.revision = request.revision;
    result.difficultyId = request.difficultyId;
    result.chartText = request.chartText;
    result.chineseUi = request.chineseUi;
    result.validationReport = SimaiNativeParser::buildValidationReport(
        request.chartText,
        request.chineseUi ? SimaiNativeValidationLocale::Chinese : SimaiNativeValidationLocale::English,
        &request.parseResult);
    return result;
}

TimelineMuriRefreshResult buildTimelineMuriRefreshResult(const TimelineMuriRefreshRequest& request)
{
    TimelineMuriRefreshResult result;
    result.revision = request.revision;
    result.difficultyId = request.difficultyId;
    result.noteMarkerSignature = request.noteMarkerSignature;
    result.analysisReport = MuriAnalyzer::analyze(
        request.noteMarkers,
        request.renderOptions,
        request.staticTapOnSlideThresholdSeconds);
    result.staticReferences = miacode::muri::buildStaticMuriReferences(
        request.noteMarkers,
        request.staticTapOnSlideThresholdSeconds);
    return result;
}
