#include "tools/muri/MuriDiagnosticLabels.h"

#include <limits>

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "SimaiNativeParser.h"
#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"
#include "common/MuriTypes.h"  // makeMarkerAnalysisKey
#include "tools/muri/MuriAnalyzerModel.h"
#include "tools/muri/MuriAnalyzerInternal.h"  // isSlideLike / hasUsableSlideTraceTiming / judgeTickForPadActiveStart / head-star identity

namespace miacode::muri::detail {

constexpr double kPadTimeEpsilon = 1e-6;

QString normalizedPadToken(const QString& pad)
{
    return pad.trimmed().toUpper();
}

bool diagnosticAnchorComesBefore(const DiagnosticAnchor& left, const DiagnosticAnchor& right)
{
    if (!left.valid) {
        return false;
    }
    if (!right.valid) {
        return true;
    }
    if (left.second + kPadTimeEpsilon < right.second) {
        return true;
    }
    if (right.second + kPadTimeEpsilon < left.second) {
        return false;
    }
    if (left.order != right.order) {
        return left.order < right.order;
    }
    if (left.line != right.line) {
        return left.line < right.line;
    }
    return left.col < right.col;
}

DiagnosticAnchor earlierDiagnosticAnchor(const DiagnosticAnchor& first, const DiagnosticAnchor& second)
{
    return diagnosticAnchorComesBefore(second, first) ? second : first;
}

DiagnosticAnchor diagnosticAnchorFromMarkerSourceRef(const MarkerSourceRef& ref)
{
    DiagnosticAnchor anchor;
    anchor.valid = ref.order >= 0;
    anchor.second = ref.second;
    anchor.order = ref.order >= 0 ? ref.order : std::numeric_limits<int>::max();
    anchor.line = ref.line;
    anchor.col = ref.col;
    return anchor;
}

DiagnosticAnchor diagnosticAnchorFromMarker(const TimelineNoteMarker& marker)
{
    DiagnosticAnchor anchor;
    anchor.valid = true;
    anchor.second = marker.second;
    anchor.order = marker.parseOrder >= 0 ? marker.parseOrder : std::numeric_limits<int>::max();
    anchor.line = marker.sourceLine;
    anchor.col = marker.sourceCol;
    return anchor;
}

DiagnosticAnchor diagnosticAnchorFromNote(const JudgeableSimpleNote& note)
{
    DiagnosticAnchor anchor;
    anchor.valid = true;
    anchor.second = note.momentSecond;
    anchor.order = note.order >= 0 ? note.order : std::numeric_limits<int>::max();
    anchor.line = note.line;
    anchor.col = note.col;
    return anchor;
}

DiagnosticAnchor diagnosticAnchorFromAction(const RuntimeHandAction& action)
{
    DiagnosticAnchor anchor;
    anchor.valid = true;
    anchor.second = action.startSecond;
    anchor.order = action.order >= 0 ? action.order : std::numeric_limits<int>::max();
    anchor.line = action.line;
    anchor.col = action.col;
    return anchor;
}

QString slideLikeDisplayKey(const TimelineNoteMarker& marker)
{
    const QString displayKey = marker.slideDisplayKey.trimmed();
    if (!displayKey.isEmpty()) {
        return displayKey;
    }
    const QString trackKey = marker.slideTrackKey.trimmed();
    if (!trackKey.isEmpty()) {
        return trackKey;
    }
    return QStringLiteral("%1->%2").arg(marker.lane).arg(marker.endLane);
}

QString noteTypeDisplayLabel(const QString& type)
{
    const QString normalized = type.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QLatin1String("tap")) {
        return QStringLiteral("tap");
    }
    if (normalized == QLatin1String("hold")) {
        return QStringLiteral("hold");
    }
    if (normalized == QLatin1String("touch")) {
        return QStringLiteral("touch");
    }
    if (normalized == QLatin1String("touch_hold")) {
        return QStringLiteral("touch-hold");
    }
    if (normalized == QLatin1String("slide")) {
        return QStringLiteral("slide");
    }
    if (normalized == QLatin1String("wifi")) {
        return QStringLiteral("wifi");
    }
    if (normalized == QLatin1String("touch_group")) {
        return QStringLiteral("touch");
    }
    return normalized;
}

QString laneDisplayTokenFromPad(const QString& pad)
{
    const QString normalizedPad = normalizedPadToken(pad);
    if (normalizedPad.size() >= 2
        && normalizedPad.at(1) >= QLatin1Char('1')
        && normalizedPad.at(1) <= QLatin1Char('8')) {
        return normalizedPad.mid(1);
    }
    return QString();
}

QString simpleNoteLaneConfigToken(const QString& laneToken, bool hasProtection, bool isHold)
{
    if (laneToken.isEmpty()) {
        return QString();
    }
    QString token = laneToken;
    if (hasProtection) {
        token += QLatin1Char('x');
    }
    if (isHold) {
        token += QLatin1Char('h');
    }
    return token;
}

QString simpleNoteTargetLabel(const JudgeableSimpleNote& note)
{
    if (note.headStarTapLike) {
        const QString laneToken =
            (note.marker != nullptr && note.marker->lane >= 1 && note.marker->lane <= 8)
            ? QString::number(note.marker->lane)
            : laneDisplayTokenFromPad(note.pad);
        const QString token = simpleNoteLaneConfigToken(laneToken, note.hasProtection, false);
        const QString base = token.isEmpty()
            ? QStringLiteral("star")
            : QStringLiteral("star %1").arg(token);
        return note.hasProtection ? QStringLiteral("protected %1").arg(base) : base;
    }

    const QString normalizedType = note.type.trimmed().toLower();
    if (normalizedType == QLatin1String("tap") || normalizedType == QLatin1String("hold")) {
        const QString laneToken =
            (note.marker != nullptr && note.marker->lane >= 1 && note.marker->lane <= 8)
            ? QString::number(note.marker->lane)
            : laneDisplayTokenFromPad(note.pad);
        const bool isHold = normalizedType == QLatin1String("hold");
        const QString configToken = simpleNoteLaneConfigToken(laneToken, note.hasProtection, isHold);
        const QString typeText = noteTypeDisplayLabel(note.type);
        const QString base = configToken.isEmpty() ? typeText : QStringLiteral("%1 %2").arg(typeText, configToken);
        return note.hasProtection ? QStringLiteral("protected %1").arg(base) : base;
    }

    if (normalizedType == QLatin1String("touch") || normalizedType == QLatin1String("touch_hold")) {
        const QString pad = normalizedPadToken(note.pad);
        const QString typeText = noteTypeDisplayLabel(note.type);
        return pad.isEmpty() ? typeText : QStringLiteral("%1 %2").arg(typeText, pad);
    }

    return noteTypeDisplayLabel(note.type);
}

QString slideHeadConfigLabel(const TimelineNoteMarker& marker)
{
    const QString laneToken =
        (marker.lane >= 1 && marker.lane <= 8) ? QString::number(marker.lane) : QString();
    const QString token = simpleNoteLaneConfigToken(laneToken, marker.headEx, false);
    const QString base = token.isEmpty() ? QStringLiteral("star") : QStringLiteral("star %1").arg(token);
    return marker.headEx ? QStringLiteral("protected %1").arg(base) : base;
}

QString formatMarkerConfigLabel(const TimelineNoteMarker& marker, bool slideHead)
{
    if (slideHead && isSlideLike(marker)) {
        return slideHeadConfigLabel(marker);
    }
    if (marker.type == QLatin1String("tap")) {
        return QStringLiteral("tap %1").arg(marker.lane);
    }
    if (marker.type == QLatin1String("hold")) {
        return QStringLiteral("hold %1").arg(marker.lane);
    }
    if (marker.type == QLatin1String("touch")) {
        const QString pad = normalizedPadToken(marker.touchPad);
        return pad.isEmpty() ? QStringLiteral("touch") : QStringLiteral("touch %1").arg(pad);
    }
    if (marker.type == QLatin1String("touch_hold")) {
        const QString pad = normalizedPadToken(marker.touchPad);
        return pad.isEmpty() ? QStringLiteral("touch-hold") : QStringLiteral("touch-hold %1").arg(pad);
    }
    if (marker.type == QLatin1String("slide")) {
        return QStringLiteral("slide %1").arg(slideLikeDisplayKey(marker));
    }
    if (marker.type == QLatin1String("wifi")) {
        return QStringLiteral("wifi %1").arg(slideLikeDisplayKey(marker));
    }
    return marker.type;
}

QHash<QString, const TimelineNoteMarker*> buildMarkerLookup(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, const TimelineNoteMarker*> lookup;
    lookup.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        lookup.insert(makeMarkerAnalysisKey(marker), &marker);
    }
    return lookup;
}

QHash<QString, QString> buildMarkerConfigLabels(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, QString> labels;
    labels.reserve(noteMarkers.size() * 2);

    for (const TimelineNoteMarker& marker : noteMarkers) {
        labels.insert(makeMarkerAnalysisKey(marker), formatMarkerConfigLabel(marker));
        if (!shouldCreateHeadStarJudgeNote(marker)) {
            continue;
        }
        labels.insert(slideHeadStarMarkerKey(marker), formatMarkerConfigLabel(marker, true));
    }

    return labels;
}

QString markerConfigLabelForKey(
    const QHash<QString, QString>& markerConfigLabels,
    const QString& markerKey,
    const QString& fallbackType)
{
    const QString label = markerConfigLabels.value(markerKey).trimmed();
    if (!label.isEmpty()) {
        return label;
    }
    return fallbackType.trimmed().isEmpty() ? QStringLiteral("source note") : fallbackType.trimmed();
}

QString slideStartLookupKey(int lane, double second)
{
    return QStringLiteral("%1|%2").arg(lane).arg(judgeTickForPadActiveStart(second));
}

QHash<QString, QString> buildSyntheticSlideHeadOwnerKeys(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, QString> ownerKeys;
    ownerKeys.reserve(noteMarkers.size() * 2);
    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!shouldCreateHeadStarJudgeNote(marker)) {
            continue;
        }
        ownerKeys.insert(slideHeadStarMarkerKey(marker), makeMarkerAnalysisKey(marker));
    }
    return ownerKeys;
}

QSet<QString> buildSlideKeysWithTapOnSlideHead(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, QVector<QString>> slideKeysByStart;
    slideKeysByStart.reserve(noteMarkers.size());

    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!hasUsableSlideTraceTiming(marker)) {
            continue;
        }
        slideKeysByStart[slideStartLookupKey(marker.lane, marker.slideTraceSecond)].append(makeMarkerAnalysisKey(marker));
    }

    QSet<QString> slideKeys;
    slideKeys.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        const bool tapOnSlideHead = marker.type == QLatin1String("tap") && marker.slideHead;
        const bool syntheticHeadOnSlideHead =
            isSlideLike(marker) && marker.hasHeadStar && marker.slideHead;
        if (!tapOnSlideHead && !syntheticHeadOnSlideHead) {
            continue;
        }
        const QVector<QString> matchingSlideKeys =
            slideKeysByStart.value(slideStartLookupKey(marker.lane, marker.second));
        for (const QString& slideKey : matchingSlideKeys) {
            slideKeys.insert(slideKey);
        }
    }
    return slideKeys;
}

QString slideStartOwnerKeyForSource(
    const QString& sourceMarkerKey,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    return syntheticSlideHeadOwnerKeys.value(sourceMarkerKey, sourceMarkerKey);
}

DiagnosticAnchor diagnosticAnchorForMarkerKey(
    const QString& sourceMarkerKey,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    if (sourceMarkerKey.isEmpty()) {
        return DiagnosticAnchor();
    }

    const QString ownerKey = slideStartOwnerKeyForSource(sourceMarkerKey, syntheticSlideHeadOwnerKeys);
    const auto it = markerRefs.constFind(ownerKey);
    if (it == markerRefs.constEnd()) {
        return DiagnosticAnchor();
    }
    return diagnosticAnchorFromMarkerSourceRef(it.value());
}

DiagnosticAnchor diagnosticAnchorForCause(
    const RuntimePadEvent& cause,
    const QHash<QString, MarkerSourceRef>& markerRefs,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    if (cause.sourceMarkerKey.isEmpty()) {
        return DiagnosticAnchor();
    }

    DiagnosticAnchor anchor =
        diagnosticAnchorForMarkerKey(cause.sourceMarkerKey, markerRefs, syntheticSlideHeadOwnerKeys);
    if (anchor.valid) {
        return anchor;
    }
    anchor.valid = true;
    anchor.second = cause.second;
    anchor.order = cause.sourceOrder >= 0 ? cause.sourceOrder : std::numeric_limits<int>::max();
    anchor.line = cause.line;
    anchor.col = cause.col;
    return anchor;
}

QString markerConfigLabelForSource(
    const QHash<QString, QString>& markerConfigLabels,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys,
    const QString& markerKey,
    const QString& fallbackType)
{
    return markerConfigLabelForKey(
        markerConfigLabels,
        slideStartOwnerKeyForSource(markerKey, syntheticSlideHeadOwnerKeys),
        fallbackType);
}

QString formatMultiTouchActionLabel(
    const RuntimeHandAction& action,
    const QHash<QString, const TimelineNoteMarker*>& markerLookup,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    const QString ownerKey = syntheticSlideHeadOwnerKeys.value(action.markerKey);
    const QString effectiveKey = ownerKey.isEmpty() ? action.markerKey : ownerKey;
    const auto it = markerLookup.constFind(effectiveKey);
    if (it != markerLookup.constEnd() && it.value() != nullptr) {
        const TimelineNoteMarker& marker = *it.value();
        if (!ownerKey.isEmpty()) {
            return formatMarkerConfigLabel(marker, true);
        }
        if (marker.type == QLatin1String("tap")) {
            return QStringLiteral("tap %1").arg(marker.lane);
        }
        return formatMarkerConfigLabel(marker);
    }

    const QString fallbackType = noteTypeDisplayLabel(action.sourceType);
    return fallbackType.isEmpty() ? QStringLiteral("note") : fallbackType;
}

bool sourceTypeIsTouchLike(const QString& sourceType)
{
    const QString normalized = sourceType.trimmed().toLower();
    return normalized == QLatin1String("touch")
        || normalized == QLatin1String("touch_hold")
        || normalized == QLatin1String("touch_group");
}

double slideStartSecondForSource(
    const RuntimePadEvent& cause,
    const QHash<QString, const TimelineNoteMarker*>& markerLookup,
    const QHash<QString, QString>& syntheticSlideHeadOwnerKeys)
{
    const QString ownerKey = slideStartOwnerKeyForSource(cause.sourceMarkerKey, syntheticSlideHeadOwnerKeys);
    const auto it = markerLookup.constFind(ownerKey);
    if (it != markerLookup.constEnd()
        && it.value() != nullptr
        && hasUsableSlideTraceTiming(*it.value())) {
        return it.value()->slideTraceSecond;
    }
    return cause.second;
}

MuriAlertLevel slideHeadTapAlertLevel(bool hasTapOnSlideHead, double gapSecond)
{
    if (!hasTapOnSlideHead
        && gapSecond > kPadTimeEpsilon
        && gapSecond <= miacode::muri::kSlideHeadTapNoTapWarningCutoffSeconds + kPadTimeEpsilon) {
        return MuriAlertLevel::Warning;
    }
    if (gapSecond + kPadTimeEpsilon >= miacode::muri::kSlideHeadTapLateWarningCutoffSeconds) {
        return MuriAlertLevel::Warning;
    }
    return MuriAlertLevel::Muri;
}

MuriAlertLevel downgradeProtectedSimpleNoteAlertLevel(MuriAlertLevel alertLevel, bool hasProtection)
{
    if (hasProtection) {
        return MuriAlertLevel::Warning;
    }
    return alertLevel;
}

MuriAlertLevel tapOnSlideAlertLevel(double gapSecond, double thresholdSecond)
{
    if (gapSecond > miacode::muri::kTapOnSlideWarningCutoffSeconds + kPadTimeEpsilon
        && gapSecond <= thresholdSecond + kPadTimeEpsilon) {
        return MuriAlertLevel::Warning;
    }
    return MuriAlertLevel::Muri;
}

MuriDetailKind slideHeadTapDetailKind(bool hasTapOnSlideHead)
{
    return hasTapOnSlideHead
        ? MuriDetailKind::SlideHeadStartEarlyJudge
        : MuriDetailKind::SlideHeadJumpStartEarlyJudge;
}

MuriDetailArgs simpleGapDetailArgs(
    MuriAlertLevel alertLevel,
    const QString& left,
    const QString& right,
    double gapMs)
{
    MuriDetailArgs args;
    args.left = left;
    args.right = right;
    args.gapText = QStringLiteral("%1 ms").arg(QString::number(gapMs, 'f', 1));
    args.alert = alertLevel;
    return args;
}

QString slideHeadTapDetailText(
    bool hasTapOnSlideHead,
    MuriAlertLevel alertLevel,
    const QString& causeConfig,
    const QString& affectedTarget,
    double gapMs)
{
    return renderMuriDetail(
        slideHeadTapDetailKind(hasTapOnSlideHead),
        simpleGapDetailArgs(alertLevel, causeConfig, affectedTarget, gapMs),
        SimaiNativeValidationLocale::English);
}

QString tapOnSlideDetailText(
    MuriAlertLevel alertLevel,
    const QString& causeConfig,
    const QString& affectedTarget,
    double gapMs)
{
    return renderMuriDetail(
        MuriDetailKind::TapOnSlideCollide,
        simpleGapDetailArgs(alertLevel, causeConfig, affectedTarget, gapMs),
        SimaiNativeValidationLocale::English);
}

}  // namespace miacode::muri::detail
