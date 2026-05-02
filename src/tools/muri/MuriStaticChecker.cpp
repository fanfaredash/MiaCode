#include "tools/muri/MuriStaticChecker.h"

#include <algorithm>

#include <QSet>
#include <QString>
#include <QtMath>

#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"
#include "common/MuriTypes.h"

namespace {

constexpr double kStaticOverlayThresholdSeconds = 6.0 / miacode::muri::kJudgeTps;
constexpr double kStaticTimeEpsilonSeconds = 1e-9;

QVector<MuriStaticReference> dedupeDenseOverlapReferences(const QVector<MuriStaticReference>& references)
{
    QVector<MuriStaticReference> deduped;
    deduped.reserve(references.size());

    QHash<qint64, int> keptOverlapCountBySecond;
    keptOverlapCountBySecond.reserve(references.size());

    for (const MuriStaticReference& reference : references) {
        if (reference.kind != MuriKind::Overlap) {
            deduped.append(reference);
            continue;
        }

        const qint64 secondKey = qRound64(reference.affected.second * 1000000.0);
        const int keptCount = keptOverlapCountBySecond.value(secondKey, 0);
        if (keptCount >= 1) {
            continue;
        }
        keptOverlapCountBySecond.insert(secondKey, keptCount + 1);
        deduped.append(reference);
    }

    return deduped;
}

bool isSlideLike(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi");
}

bool hasUsableSlideTraceTiming(const TimelineNoteMarker& marker)
{
    return isSlideLike(marker)
        && marker.slideTraceSecond + kStaticTimeEpsilonSeconds >= marker.second
        && marker.endSecond + kStaticTimeEpsilonSeconds >= marker.slideTraceSecond;
}

bool isTapMarker(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("tap");
}

bool isHoldMarker(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("hold");
}

bool isTouchMarker(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("touch");
}

bool isTouchHoldMarker(const TimelineNoteMarker& marker)
{
    return marker.type == QLatin1String("touch_hold");
}

bool isTapLikeMarker(const TimelineNoteMarker& marker)
{
    return isTapMarker(marker) || isTouchMarker(marker);
}

bool isHoldLikeMarker(const TimelineNoteMarker& marker)
{
    return isHoldMarker(marker) || isTouchHoldMarker(marker);
}

bool isNonSlideMarker(const TimelineNoteMarker& marker)
{
    return isTapLikeMarker(marker) || isHoldLikeMarker(marker);
}

QString headStarJudgeEmitKey(const TimelineNoteMarker& marker)
{
    if (marker.sameHeadSlide) {
        return QStringLiteral("same_head_star_note|%1|%2|%3|%4|%5|%6")
            .arg(marker.second, 0, 'f', 6)
            .arg(marker.sourceLine)
            .arg(marker.lane)
            .arg(marker.headBreak ? 1 : 0)
            .arg(marker.headEx ? 1 : 0)
            .arg(marker.headEach ? 1 : 0);
    }

    return QStringLiteral("head_star_note|%1|%2|%3|%4|%5")
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.lane)
        .arg(marker.parseOrder);
}

QString slideHeadStarMarkerKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("slide_head_star|%1|%2|%3|%4|%5")
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.lane)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.eachGroupId);
}

int slideHeadStarSourceCol(const TimelineNoteMarker& marker)
{
    return qMax(1, marker.sourceCol + 1);
}

bool shouldCreateHeadStarTarget(const TimelineNoteMarker& marker)
{
    return isSlideLike(marker) && marker.hasHeadStar && marker.lane >= 1 && marker.lane <= 8;
}

QString lanePadToken(int lane)
{
    if (lane < 1 || lane > 8) {
        return QString();
    }
    return QStringLiteral("A%1").arg(lane);
}

QString markerPadToken(const TimelineNoteMarker& marker)
{
    if (isTouchMarker(marker) || isTouchHoldMarker(marker)) {
        return marker.touchPad.trimmed().toUpper();
    }
    if (isTapMarker(marker) || isHoldMarker(marker)) {
        return lanePadToken(marker.lane);
    }
    return QString();
}

bool markerMomentInRange(double momentSecond, double startSecond, double endSecond)
{
    return momentSecond + kStaticTimeEpsilonSeconds >= startSecond
        && momentSecond <= endSecond + kStaticTimeEpsilonSeconds;
}

QString slideStartLookupKey(int lane, double second)
{
    return QStringLiteral("%1|%2").arg(lane).arg(qRound64(second * 1000000.0));
}

QSet<QString> buildSlideKeysWithTapOnSlideHead(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QHash<QString, QVector<QString>> slideKeysByStart;
    slideKeysByStart.reserve(noteMarkers.size());

    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!hasUsableSlideTraceTiming(marker)) {
            continue;
        }
        slideKeysByStart[slideStartLookupKey(marker.lane, marker.slideTraceSecond)].append(
            makeMarkerAnalysisKey(marker));
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
        const QVector<QString> matchingSlideKeys = slideKeysByStart.value(
            slideStartLookupKey(marker.lane, marker.second));
        for (const QString& slideKey : matchingSlideKeys) {
            slideKeys.insert(slideKey);
        }
    }
    return slideKeys;
}

MuriAlertLevel slideHeadTapAlertLevel(bool hasTapOnSlideHead, double gapSecond)
{
    if (!hasTapOnSlideHead
        && gapSecond > kStaticTimeEpsilonSeconds
        && gapSecond <= miacode::muri::kSlideHeadTapNoTapWarningCutoffSeconds + kStaticTimeEpsilonSeconds) {
        return MuriAlertLevel::Warning;
    }
    if (gapSecond + kStaticTimeEpsilonSeconds
        >= miacode::muri::kSlideHeadTapLateWarningCutoffSeconds) {
        return MuriAlertLevel::Warning;
    }
    return MuriAlertLevel::Muri;
}

MuriAlertLevel downgradeProtectedReferenceAlertLevel(MuriAlertLevel alertLevel, bool hasProtection)
{
    if (hasProtection) {
        return MuriAlertLevel::Warning;
    }
    return alertLevel;
}

MuriAlertLevel tapOnSlideAlertLevel(double gapSecond, double thresholdSecond)
{
    if (gapSecond > miacode::muri::kTapOnSlideWarningCutoffSeconds + kStaticTimeEpsilonSeconds
        && gapSecond <= thresholdSecond + kStaticTimeEpsilonSeconds) {
        return MuriAlertLevel::Warning;
    }
    return MuriAlertLevel::Muri;
}

double slideCriticalSecondForMarker(const TimelineNoteMarker& marker)
{
    if (marker.type == QLatin1String("slide")
        && !marker.slideSegmentShootSeconds.isEmpty()
        && !marker.slideSegmentDurations.isEmpty()
        && !marker.slideSegmentCriticalProportions.isEmpty()) {
        const int index = qMin(
            marker.slideSegmentShootSeconds.size(),
            qMin(marker.slideSegmentDurations.size(), marker.slideSegmentCriticalProportions.size())) - 1;
        if (index >= 0) {
            return marker.slideSegmentShootSeconds.at(index)
                + marker.slideSegmentDurations.at(index) * marker.slideSegmentCriticalProportions.at(index);
        }
    }
    if (marker.type == QLatin1String("wifi")
        && hasUsableSlideTraceTiming(marker)) {
        return marker.slideTraceSecond
            + (marker.endSecond - marker.slideTraceSecond) * marker.wifiCriticalProportion;
    }
    return -1.0;
}

MuriStaticReferenceNote staticReferenceNoteFromMarker(const TimelineNoteMarker& marker)
{
    MuriStaticReferenceNote note;
    note.markerKey = makeMarkerAnalysisKey(marker);
    note.markerType = marker.type;
    note.pad = markerPadToken(marker);
    note.slideDisplayKey = marker.slideDisplayKey;
    note.slideTrackKey = marker.slideTrackKey;
    note.line = marker.sourceLine;
    note.col = marker.sourceCol;
    note.lane = marker.lane;
    note.endLane = marker.endLane;
    note.second = marker.second;
    note.endSecond = marker.endSecond;
    note.slideTraceSecond = marker.slideTraceSecond;
    note.hasProtection = marker.isEx;
    return note;
}

MuriStaticReferenceNote staticReferenceNoteFromHeadStar(const TimelineNoteMarker& marker)
{
    MuriStaticReferenceNote note = staticReferenceNoteFromMarker(marker);
    note.markerKey = slideHeadStarMarkerKey(marker);
    note.markerType = QStringLiteral("tap");
    note.pad = lanePadToken(marker.lane);
    note.col = slideHeadStarSourceCol(marker);
    note.endLane = marker.lane;
    note.endSecond = -1.0;
    note.slideTraceSecond = -1.0;
    note.slideDisplayKey.clear();
    note.slideTrackKey.clear();
    note.hasProtection = marker.headEx;
    note.headStarTapLike = true;
    return note;
}

struct StaticHeadStarTarget {
    MuriStaticReferenceNote note;
    QString pad;
    double second = -1.0;
};

struct StaticOverlapCandidate {
    MuriStaticReferenceNote note;
    QString pad;
    bool tapLike = false;
    bool holdLike = false;
};

QVector<StaticHeadStarTarget> buildStaticHeadStarTargets(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QVector<StaticHeadStarTarget> targets;
    targets.reserve(noteMarkers.size());

    QSet<QString> emittedKeys;
    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!shouldCreateHeadStarTarget(marker)) {
            continue;
        }

        const QString emitKey = headStarJudgeEmitKey(marker);
        if (emittedKeys.contains(emitKey)) {
            continue;
        }
        emittedKeys.insert(emitKey);

        StaticHeadStarTarget target;
        target.note = staticReferenceNoteFromHeadStar(marker);
        target.pad = target.note.pad;
        target.second = marker.second;
        targets.append(target);
    }

    return targets;
}

}  // namespace

namespace miacode::muri {

QVector<MuriStaticReference> buildStaticMuriReferences(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double collideThresholdSeconds)
{
    const QVector<StaticHeadStarTarget> headStarTargets = buildStaticHeadStarTargets(noteMarkers);
    const QSet<QString> slideKeysWithTapOnSlideHead = buildSlideKeysWithTapOnSlideHead(noteMarkers);
    const double normalizedCollideThresholdSeconds = qBound(
        static_cast<double>(kStaticTapOnSlideThresholdMinMs) / 1000.0,
        collideThresholdSeconds,
        static_cast<double>(kStaticTapOnSlideThresholdMaxMs) / 1000.0);
    const double collideExtraDeltaSeconds =
        normalizedCollideThresholdSeconds - miacode::muri::kTapAvailableSeconds;

    QVector<int> slideIndices;
    QVector<int> wifiIndices;
    QVector<int> nonSlideIndices;
    slideIndices.reserve(noteMarkers.size());
    wifiIndices.reserve(noteMarkers.size());
    nonSlideIndices.reserve(noteMarkers.size());

    for (int index = 0; index < noteMarkers.size(); ++index) {
        const TimelineNoteMarker& marker = noteMarkers.at(index);
        if (marker.type == QLatin1String("slide")) {
            slideIndices.append(index);
            continue;
        }
        if (marker.type == QLatin1String("wifi")) {
            wifiIndices.append(index);
            continue;
        }
        if (isNonSlideMarker(marker)) {
            nonSlideIndices.append(index);
        }
    }

    QVector<MuriStaticReference> records;
    records.reserve(128);

    struct CollideEntry {
        QString pad;
        double enterSecond = -1.0;
        double startSecond = -1.0;
        double endSecond = -1.0;
    };

    for (int slideIndex : slideIndices) {
        const TimelineNoteMarker& slide = noteMarkers.at(slideIndex);
        const double criticalSecond = slideCriticalSecondForMarker(slide);
        if (!hasUsableSlideTraceTiming(slide) || !qIsFinite(criticalSecond)) {
            continue;
        }

        const QString headPad = lanePadToken(slide.lane);
        const QString endPad = lanePadToken(slide.endLane);
        QVector<CollideEntry> collideEntries;
        const int segmentCount = qMin(
            slide.slideSegmentPadEnterTimes.size(),
            qMin(slide.slideSegmentShootSeconds.size(), slide.slideSegmentDurations.size()));
        collideEntries.reserve(segmentCount * 8);
        for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
            const double shootSecond = slide.slideSegmentShootSeconds.at(segmentIndex);
            const double durationSecond = slide.slideSegmentDurations.at(segmentIndex);
            for (const MuriPadTimeEntry& entry : slide.slideSegmentPadEnterTimes.at(segmentIndex)) {
                if (!entry.pad.startsWith(QLatin1Char('A'), Qt::CaseInsensitive)) {
                    continue;
                }
                CollideEntry collide;
                collide.pad = entry.pad.toUpper();
                collide.enterSecond = shootSecond + entry.proportion * durationSecond;
                collide.startSecond = qMax(
                    collide.enterSecond - collideExtraDeltaSeconds,
                    slide.slideTraceSecond + miacode::muri::kTapOnSlideThresholdSeconds);
                collide.endSecond = collide.enterSecond + normalizedCollideThresholdSeconds;
                collideEntries.append(collide);
            }
        }

        for (int noteIndex : nonSlideIndices) {
            const TimelineNoteMarker& note = noteMarkers.at(noteIndex);
            const QString notePad = markerPadToken(note);
            if (notePad.isEmpty()) {
                continue;
            }
            if (!isTapMarker(note) && !isHoldMarker(note)) {
                continue;
            }
            if (note.second + kStaticTimeEpsilonSeconds < slide.slideTraceSecond
                || note.second > slide.endSecond + normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                continue;
            }

            const double headDeltaSecond = note.second - slide.slideTraceSecond;
            if (!headPad.isEmpty()
                && notePad == headPad
                && headDeltaSecond + kStaticTimeEpsilonSeconds >= miacode::muri::kTapOnSlideThresholdSeconds
                && headDeltaSecond <= normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = staticReferenceNoteFromMarker(note);
                record.kind = MuriKind::SlideHeadTap;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    slideHeadTapAlertLevel(
                        slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(slide)),
                        qAbs(slide.slideTraceSecond - note.second)),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(slide);
                record.deltaSecond = slide.slideTraceSecond - note.second;
                record.hasDelta = true;
                record.slideHeadHasTapOnSlide =
                    slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(slide));
                records.append(record);
            }

            for (const CollideEntry& collide : collideEntries) {
                if (notePad != collide.pad) {
                    continue;
                }
                if (!markerMomentInRange(note.second, collide.startSecond, collide.endSecond)) {
                    continue;
                }
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = staticReferenceNoteFromMarker(note);
                record.kind = MuriKind::TapOnSlide;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    tapOnSlideAlertLevel(
                        qAbs(collide.enterSecond - note.second),
                        normalizedCollideThresholdSeconds),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(slide);
                record.deltaSecond = collide.enterSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }

            if (!endPad.isEmpty()
                && notePad == endPad
                && qIsFinite(criticalSecond)
                && note.second > criticalSecond + normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds
                && note.second <= slide.endSecond + collideExtraDeltaSeconds + kStaticTimeEpsilonSeconds) {
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = staticReferenceNoteFromMarker(note);
                record.kind = MuriKind::TapOnSlide;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    tapOnSlideAlertLevel(
                        qAbs(criticalSecond - note.second),
                        normalizedCollideThresholdSeconds),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(slide);
                record.deltaSecond = criticalSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }
        }

        for (const StaticHeadStarTarget& target : headStarTargets) {
            if (target.pad.isEmpty()) {
                continue;
            }
            if (target.second + kStaticTimeEpsilonSeconds < slide.slideTraceSecond
                || target.second > slide.endSecond + normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                continue;
            }

            const double headDeltaSecond = target.second - slide.slideTraceSecond;
            if (!headPad.isEmpty()
                && target.pad == headPad
                && headDeltaSecond + kStaticTimeEpsilonSeconds >= miacode::muri::kTapOnSlideThresholdSeconds
                && headDeltaSecond <= normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = target.note;
                record.kind = MuriKind::SlideHeadTap;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    slideHeadTapAlertLevel(
                        slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(slide)),
                        qAbs(slide.slideTraceSecond - target.second)),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(slide);
                record.deltaSecond = slide.slideTraceSecond - target.second;
                record.hasDelta = true;
                record.slideHeadHasTapOnSlide =
                    slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(slide));
                records.append(record);
            }

            for (const CollideEntry& collide : collideEntries) {
                if (target.pad != collide.pad) {
                    continue;
                }
                if (!markerMomentInRange(target.second, collide.startSecond, collide.endSecond)) {
                    continue;
                }
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = target.note;
                record.kind = MuriKind::TapOnSlide;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    tapOnSlideAlertLevel(
                        qAbs(collide.enterSecond - target.second),
                        normalizedCollideThresholdSeconds),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(slide);
                record.deltaSecond = collide.enterSecond - target.second;
                record.hasDelta = true;
                records.append(record);
            }

            if (!endPad.isEmpty()
                && target.pad == endPad
                && qIsFinite(criticalSecond)
                && target.second > criticalSecond + normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds
                && target.second <= slide.endSecond + collideExtraDeltaSeconds + kStaticTimeEpsilonSeconds) {
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = target.note;
                record.kind = MuriKind::TapOnSlide;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    tapOnSlideAlertLevel(
                        qAbs(criticalSecond - target.second),
                        normalizedCollideThresholdSeconds),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(slide);
                record.deltaSecond = criticalSecond - target.second;
                record.hasDelta = true;
                records.append(record);
            }
        }
    }

    for (int wifiIndex : wifiIndices) {
        const TimelineNoteMarker& wifi = noteMarkers.at(wifiIndex);
        const double criticalSecond = slideCriticalSecondForMarker(wifi);
        if (!hasUsableSlideTraceTiming(wifi) || !qIsFinite(criticalSecond)) {
            continue;
        }

        const int endLane = wifi.endLane;
        const QSet<int> endLanes{
            endLane,
            ((endLane + 6) % 8) + 1,
            (endLane % 8) + 1,
        };
        const double startSecond = qMax(
            criticalSecond - collideExtraDeltaSeconds,
            wifi.slideTraceSecond + miacode::muri::kTapOnSlideThresholdSeconds);
        const double endSecond = qMax(
            criticalSecond + normalizedCollideThresholdSeconds,
            wifi.endSecond + collideExtraDeltaSeconds);

        for (int noteIndex : nonSlideIndices) {
            const TimelineNoteMarker& note = noteMarkers.at(noteIndex);
            if (!isTapMarker(note) && !isHoldMarker(note)) {
                continue;
            }
            if (note.second + kStaticTimeEpsilonSeconds < wifi.slideTraceSecond
                || note.second > wifi.endSecond + normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                continue;
            }

            const double headDeltaSecond = note.second - wifi.slideTraceSecond;
            if (note.lane == wifi.lane
                && headDeltaSecond + kStaticTimeEpsilonSeconds >= miacode::muri::kTapOnSlideThresholdSeconds
                && headDeltaSecond <= normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = staticReferenceNoteFromMarker(note);
                record.kind = MuriKind::SlideHeadTap;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    slideHeadTapAlertLevel(
                        slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(wifi)),
                        qAbs(wifi.slideTraceSecond - note.second)),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(wifi);
                record.deltaSecond = wifi.slideTraceSecond - note.second;
                record.hasDelta = true;
                record.slideHeadHasTapOnSlide =
                    slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(wifi));
                records.append(record);
            }

            if (endLanes.contains(note.lane) && markerMomentInRange(note.second, startSecond, endSecond)) {
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = staticReferenceNoteFromMarker(note);
                record.kind = MuriKind::TapOnSlide;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    tapOnSlideAlertLevel(
                        qAbs(criticalSecond - note.second),
                        normalizedCollideThresholdSeconds),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(wifi);
                record.deltaSecond = criticalSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }
        }

        for (const StaticHeadStarTarget& target : headStarTargets) {
            if (target.second + kStaticTimeEpsilonSeconds < wifi.slideTraceSecond
                || target.second > wifi.endSecond + normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                continue;
            }

            const double headDeltaSecond = target.second - wifi.slideTraceSecond;
            if (target.note.lane == wifi.lane
                && headDeltaSecond + kStaticTimeEpsilonSeconds >= miacode::muri::kTapOnSlideThresholdSeconds
                && headDeltaSecond <= normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds) {
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = target.note;
                record.kind = MuriKind::SlideHeadTap;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    slideHeadTapAlertLevel(
                        slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(wifi)),
                        qAbs(wifi.slideTraceSecond - target.second)),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(wifi);
                record.deltaSecond = wifi.slideTraceSecond - target.second;
                record.hasDelta = true;
                record.slideHeadHasTapOnSlide =
                    slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(wifi));
                records.append(record);
            }

            if (endLanes.contains(target.note.lane) && markerMomentInRange(target.second, startSecond, endSecond)) {
                MuriStaticReference record;
                const MuriStaticReferenceNote affected = target.note;
                record.kind = MuriKind::TapOnSlide;
                record.alertLevel = downgradeProtectedReferenceAlertLevel(
                    tapOnSlideAlertLevel(
                        qAbs(criticalSecond - target.second),
                        normalizedCollideThresholdSeconds),
                    affected.hasProtection);
                record.affected = affected;
                record.cause = staticReferenceNoteFromMarker(wifi);
                record.deltaSecond = criticalSecond - target.second;
                record.hasDelta = true;
                records.append(record);
            }
        }
    }

    QVector<StaticOverlapCandidate> overlapCandidates;
    overlapCandidates.reserve(nonSlideIndices.size() + headStarTargets.size());
    for (int noteIndex : nonSlideIndices) {
        const TimelineNoteMarker& note = noteMarkers.at(noteIndex);
        const QString pad = markerPadToken(note);
        if (pad.isEmpty()) {
            continue;
        }

        StaticOverlapCandidate candidate;
        candidate.note = staticReferenceNoteFromMarker(note);
        candidate.pad = pad;
        candidate.tapLike = isTapLikeMarker(note);
        candidate.holdLike = isHoldLikeMarker(note);
        overlapCandidates.append(candidate);
    }
    // Slide head-star overlap candidates.
    //
    // Bug fix: previously this loop iterated `headStarTargets`, which
    // deduplicates same-head-slide pairs (`5-1[8:1]/5-2[8:1]`) into a
    // single judge target via the `same_head_star_note|...` emit key
    // in headStarJudgeEmitKey. That dedup is correct for visual judge
    // representation (one rendered double-star → one judge target),
    // but it silently hid the head-star *collision* between the two
    // slides — so the symmetry the user expects between
    //   `5/5`            (tap+tap)        → Overlap fires ✓
    //   `5/5-1[8:1]`     (tap+slideHead)  → Overlap fires ✓
    //   `5-1/5-2[8:1]`   (slideHead+slideHead) → Overlap should fire
    // was broken on the third case. Iterate noteMarkers directly here
    // so each slide head contributes its own candidate. The pairwise
    // overlap loop below then sees both heads and fires Overlap.
    QSet<QString> emittedOverlapHeadKeys;
    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!shouldCreateHeadStarTarget(marker)) {
            continue;
        }
        const QString pad = lanePadToken(marker.lane);
        if (pad.isEmpty()) {
            continue;
        }
        // Dedup key: sourceLine + sourceCol + lane + second uniquely
        // identifies one user-typed chart cell. Critically, parseOrder
        // is OMITTED — when the user writes the explicit same-head
        // chain operator `*` (e.g. `5-1[8:1]*-2[8:1]`), the parser
        // emits multiple slide markers that share sourceLine+sourceCol
        // (both paths emanate from the same `5` token) but have
        // distinct parseOrders. Including parseOrder would treat them
        // as separate overlap candidates and fire a false-positive
        // Overlap on intentionally-stacked paths. The `/` synchronous-
        // group separator (`5-1[8:1]/5-2[8:1]`), by contrast, gives
        // each slide its own `5` token at a different sourceCol, so
        // they survive dedup as two distinct candidates and Overlap
        // correctly fires.
        const QString key = QStringLiteral("overlap_head|%1|%2|%3|%4")
                                .arg(marker.sourceLine)
                                .arg(marker.sourceCol)
                                .arg(marker.lane)
                                .arg(marker.second, 0, 'f', 6);
        if (emittedOverlapHeadKeys.contains(key)) {
            continue;
        }
        emittedOverlapHeadKeys.insert(key);
        StaticOverlapCandidate candidate;
        candidate.note = staticReferenceNoteFromHeadStar(marker);
        candidate.pad = pad;
        candidate.tapLike = true;
        overlapCandidates.append(candidate);
    }

    for (int i = 0; i < overlapCandidates.size(); ++i) {
        const StaticOverlapCandidate& candidate = overlapCandidates.at(i);
        for (int j = 0; j < overlapCandidates.size(); ++j) {
            if (i == j) {
                continue;
            }
            const StaticOverlapCandidate& other = overlapCandidates.at(j);
            if (candidate.pad != other.pad) {
                continue;
            }

            bool overlap = false;
            if (candidate.tapLike) {
                if (other.tapLike) {
                    overlap = (i < j)
                        && qAbs(candidate.note.second - other.note.second)
                            <= kStaticOverlayThresholdSeconds + kStaticTimeEpsilonSeconds;
                } else if (other.holdLike && other.note.endSecond >= 0.0) {
                    overlap = markerMomentInRange(
                        candidate.note.second,
                        other.note.second - kStaticOverlayThresholdSeconds,
                        other.note.endSecond + kStaticOverlayThresholdSeconds);
                }
            } else if (candidate.holdLike && candidate.note.endSecond >= 0.0) {
                if (other.holdLike && other.note.endSecond >= 0.0 && i < j) {
                    overlap =
                        markerMomentInRange(
                            candidate.note.second,
                            other.note.second - kStaticOverlayThresholdSeconds,
                            other.note.endSecond + kStaticOverlayThresholdSeconds)
                        || markerMomentInRange(
                            other.note.second,
                            candidate.note.second - kStaticOverlayThresholdSeconds,
                            candidate.note.endSecond + kStaticOverlayThresholdSeconds);
                }
            }

            if (!overlap) {
                continue;
            }

            MuriStaticReference record;
            record.kind = MuriKind::Overlap;
            record.affected = candidate.note;
            record.cause = other.note;
            records.append(record);
        }
    }

    std::sort(records.begin(), records.end(), [](const MuriStaticReference& a, const MuriStaticReference& b) {
        if (!qFuzzyCompare(a.affected.second + 1.0, b.affected.second + 1.0)) {
            return a.affected.second < b.affected.second;
        }
        if (a.affected.line != b.affected.line) {
            return a.affected.line < b.affected.line;
        }
        if (a.affected.col != b.affected.col) {
            return a.affected.col < b.affected.col;
        }
        if (a.kind != b.kind) {
            return static_cast<int>(a.kind) < static_cast<int>(b.kind);
        }
        if (a.cause.line != b.cause.line) {
            return a.cause.line < b.cause.line;
        }
        return a.cause.col < b.cause.col;
    });

    return dedupeDenseOverlapReferences(records);
}

}  // namespace miacode::muri
