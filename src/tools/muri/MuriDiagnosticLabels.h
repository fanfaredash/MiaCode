#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include "common/MuriTypes.h"             // MuriAlertLevel
#include "tools/muri/MuriAnalyzerModel.h"  // DiagnosticAnchor, MarkerSourceRef, JudgeableSimpleNote, RuntimeHandAction, RuntimePadEvent

struct TimelineNoteMarker;

// Diagnostic vocabulary layer of the Muri analyzer (2026-05-29 god-file
// decomposition). Pure formatting / lookup that turns analyzer state into the
// human-readable text, source anchors, and alert levels that diagnostics carry —
// no simulation state of its own. Extracted because it had become a broad shared
// surface leaning on MuriAnalyzerInternal.h (the slide/wifi judge, the simple-note
// judge, and analyze() all format diagnostics through it). Internal to the Muri
// analyzer — namespace miacode::muri::detail.
//
// Slide classification predicates it leans on (isSlideLike / hasUsableSlideTraceTiming)
// and the tick helper judgeTickForPadActiveStart stay shared in
// MuriAnalyzerInternal.h, since non-label code uses them too.
namespace miacode::muri::detail {

// Pad / source-type tokens.
QString normalizedPadToken(const QString& pad);
bool sourceTypeIsTouchLike(const QString& sourceType);

// Diagnostic source anchors — ordering + construction from each source kind, and
// resolution of a cause marker key (following synthetic slide-head ownership).
bool diagnosticAnchorComesBefore(const DiagnosticAnchor& left, const DiagnosticAnchor& right);
DiagnosticAnchor earlierDiagnosticAnchor(const DiagnosticAnchor& first, const DiagnosticAnchor& second);
DiagnosticAnchor diagnosticAnchorFromMarkerSourceRef(const MarkerSourceRef& ref);
DiagnosticAnchor diagnosticAnchorFromMarker(const TimelineNoteMarker& marker);
DiagnosticAnchor diagnosticAnchorFromNote(const JudgeableSimpleNote& note);
DiagnosticAnchor diagnosticAnchorFromAction(const RuntimeHandAction& action);
DiagnosticAnchor diagnosticAnchorForMarkerKey(
    const QString& sourceMarkerKey,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys);
DiagnosticAnchor diagnosticAnchorForCause(
    const RuntimePadEvent& cause,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys);

// Config labels — note / marker / action → display string.
QString slideLikeDisplayKey(const TimelineNoteMarker& marker);
QString noteTypeDisplayLabel(const QString& type);
QString laneDisplayTokenFromPad(const QString& pad);
QString simpleNoteLaneConfigToken(const QString& laneToken, bool hasProtection, bool isHold);
QString simpleNoteTargetLabel(const JudgeableSimpleNote& note);
QString slideHeadConfigLabel(const TimelineNoteMarker& marker);
QString formatMarkerConfigLabel(const TimelineNoteMarker& marker, bool slideHead = false);
QString markerConfigLabelForKey(
    const QHash<QString, QString>& markerConfigLabels,
    const QString& markerKey,
    const QString& fallbackType);
QString markerConfigLabelForSource(
    const QHash<QString, QString>& markerConfigLabels,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys,
    const QString& markerKey,
    const QString& fallbackType);
QString formatMultiTouchActionLabel(
    const RuntimeHandAction& action,
    const QHash<QString, const TimelineNoteMarker*>& markerLookup,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys);

// Per-chart lookup / index builders + slide-start resolution.
QHash<QString, const TimelineNoteMarker*> buildMarkerLookup(const QVector<TimelineNoteMarker>& noteMarkers);
QHash<QString, QString> buildMarkerConfigLabels(const QVector<TimelineNoteMarker>& noteMarkers);
QHash<QString, QString> buildSyntheticSlideHeadOwnerKeys(const QVector<TimelineNoteMarker>& noteMarkers);
QSet<QString> buildSlideKeysWithTapOnSlideHead(const QVector<TimelineNoteMarker>& noteMarkers);
QString slideStartLookupKey(int lane, double second);
QString slideStartOwnerKeyForSource(
    const QString& sourceMarkerKey,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys);
double slideStartSecondForSource(
    const RuntimePadEvent& cause,
    const QHash<QString, const TimelineNoteMarker*>& markerLookup,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys);

// Simple-note alert level + detail text.
MuriAlertLevel slideHeadTapAlertLevel(bool hasTapOnSlideHead, double gapSecond);
MuriAlertLevel tapOnSlideAlertLevel(double gapSecond, double thresholdSecond);
MuriAlertLevel downgradeProtectedSimpleNoteAlertLevel(MuriAlertLevel alertLevel, bool hasProtection);
MuriDetailKind slideHeadTapDetailKind(bool hasTapOnSlideHead);
MuriDetailArgs simpleGapDetailArgs(
    MuriAlertLevel alertLevel,
    const QString& left,
    const QString& right,
    double gapMs);
QString slideHeadTapDetailText(
    bool hasTapOnSlideHead,
    MuriAlertLevel alertLevel,
    const QString& causeConfig,
    const QString& affectedTarget,
    double gapMs);
QString tapOnSlideDetailText(
    MuriAlertLevel alertLevel,
    const QString& causeConfig,
    const QString& affectedTarget,
    double gapMs);

}  // namespace miacode::muri::detail
