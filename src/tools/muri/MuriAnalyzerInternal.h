#pragma once

#include <QHash>
#include <QMap>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

#include "common/MuriTypes.h"  // MuriPadWindow, MuriActionTrail, MuriDiagnostic, ...
#include "tools/muri/MuriAnalyzerModel.h"  // RuntimeHandAction, RuntimeTouchPoint, DiagnosticAnchor, ...

struct TimelineNoteMarker;

// Cross-cutting helpers shared by the Muri analyzer's pipeline stages, still
// defined in MuriAnalyzer.cpp; this header declares them so the extracted stage
// translation units can call them. The diagnostic "vocabulary" layer (labels /
// source anchors / alert text) now lives in its own TU — see
// MuriDiagnosticLabels.h — so this header holds the remaining cross-stage
// primitives: slide predicates, pad geometry, pad-window/trail builders, judge
// timing math, the runtime hand-action model, the pad-event timeline, and the
// diagnostic sinks. Internal to the Muri analyzer — namespace miacode::muri::detail.
namespace miacode::muri::detail {

// Slide head-star identity / emission helpers.
QString slideHeadStarMarkerKey(const TimelineNoteMarker& marker);
int slideHeadStarSourceCol(const TimelineNoteMarker& marker);
QString headStarJudgeEmitKey(const TimelineNoteMarker& marker);
bool shouldCreateHeadStarJudgeNote(const TimelineNoteMarker& marker);

// Slide classification predicates (also used by the diagnostic-label layer).
bool isSlideLike(const TimelineNoteMarker& marker);
bool hasUsableSlideTraceTiming(const TimelineNoteMarker& marker);

// Pad helpers.
QString slideHeadPad(int lane);
bool touchPadsAreAdjacent(const QString& a, const QString& b);
QString notePad(const TimelineNoteMarker& marker);

// Pad-window / hand-action-trail primitives. Shared by the overlay-build stage
// and the runtime pad-window builder, so they stay in MuriAnalyzer.cpp rather
// than living with either consumer.
void addPadWindow(
    QVector<MuriPadWindow>* windows,
    const QString& pad,
    double startSecond,
    double endSecond,
    const QString& markerKey,
    const QString& type);
void addActionTrail(
    QVector<MuriActionTrail>* trails,
    const QString& markerKey,
    const QString& type,
    double startSecond,
    double endSecond,
    double radius,
    const QVector<QPointF>& points);
void addSlidePadWindowsAndTrails(
    const TimelineNoteMarker& marker,
    const QString& markerKey,
    QVector<MuriPadWindow>* padWindows,
    QVector<MuriActionTrail>* actionTrails);
void addWifiPadWindowsAndTrails(
    const TimelineNoteMarker& marker,
    const QString& markerKey,
    QVector<MuriPadWindow>* padWindows,
    QVector<MuriActionTrail>* actionTrails);

// Slide / wifi judge timing math. Shared by the overlay-build stage (early-clear
// detection), the runtime judge stages, and the analyze() orchestrator.
double slideCriticalDeltaSecond(double lastAreaDurationSecond);
bool slideJudgeIsBad(double judgeSecond, double criticalSecond, double criticalDeltaSecond);
bool wifiJudgeIsBad(double judgeSecond, double criticalSecond, double criticalDeltaSecond);

// Timing / judge-window helpers.
double notePressEndSecond(const TimelineNoteMarker& marker);
bool buildSimpleNoteJudgeWindow(
    const TimelineNoteMarker& marker,
    QString* pad,
    double* criticalSeconds,
    double* availableSeconds);
int judgeTickForNoteExpiry(double momentSecond, double availableSeconds);
int judgeTickForPadActiveStart(double second);
int judgeTickForPadActiveEnd(double second);
double tickToSecond(int tick);

// Runtime hand-action / touch-point model. The simulated finger footprint shared
// by the slide/wifi judge (per-tick pad coverage) and the simple-note multi-touch
// diagnostics (action clustering). Defaults live here (the sole declaration);
// the definitions in MuriAnalyzer.cpp must not repeat them.
QVector<RuntimeHandAction> buildRuntimeHandActions(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<JudgeableSimpleNote>& notes,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex,
    bool includeSlideLike = false);
QVector<RuntimeTouchPoint> buildRuntimeTouchPoints(
    const QVector<RuntimeHandAction>& actions,
    const QVector<int>& activeActionIndices,
    double nowSecond,
    const QSet<quint64>* previousMergedSlidePairs = nullptr,
    QSet<quint64>* currentMergedSlidePairs = nullptr);

// Pad-event timeline (interval-based runtime pad windows → tick-bucketed events),
// consumed by the simple-note simulation. Only used by the simple-note stage.
QVector<MuriPadWindow> buildRuntimePadWindows(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<JudgeableSimpleNote>& notes,
    const QHash<QString, int>& noteIndexByMarkerKey,
    const QVector<RuntimeTouchGroup>& touchGroups,
    const QHash<int, int>& touchGroupByChildNoteIndex);
QMap<int, QMap<QString, RuntimePadEvent>> buildRuntimePadEvents(
    const QVector<TimelineNoteMarker>& noteMarkers,
    const QVector<MuriPadWindow>& padWindows,
    const QHash<QString, MarkerSourceRef>& markerRefs);

}  // namespace miacode::muri::detail
