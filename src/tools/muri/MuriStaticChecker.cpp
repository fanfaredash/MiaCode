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
        if (!isSlideLike(marker) || marker.slideTraceSecond < 0.0) {
            continue;
        }
        slideKeysByStart[slideStartLookupKey(marker.lane, marker.slideTraceSecond)].append(
            makeMarkerAnalysisKey(marker));
    }

    QSet<QString> slideKeys;
    slideKeys.reserve(noteMarkers.size());
    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (marker.type != QLatin1String("tap") || !marker.slideHead) {
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
        && gapSecond <= miacode::muri::kSlideHeadTapWarningCutoffSeconds + kStaticTimeEpsilonSeconds) {
        return MuriAlertLevel::Warning;
    }
    return MuriAlertLevel::Muri;
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
        && marker.slideTraceSecond >= 0.0
        && marker.endSecond >= marker.slideTraceSecond) {
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
    note.slideTrackKey = marker.slideTrackKey;
    note.line = marker.sourceLine;
    note.col = marker.sourceCol;
    note.lane = marker.lane;
    note.endLane = marker.endLane;
    note.second = marker.second;
    note.endSecond = marker.endSecond;
    note.slideTraceSecond = marker.slideTraceSecond;
    return note;
}

TimelineNoteMarker makeStaticHelperHeadTapMarker(const TimelineNoteMarker& marker)
{
    TimelineNoteMarker helper;
    helper.second = marker.second;
    helper.endSecond = -1.0;
    helper.slideTraceSecond = -1.0;
    helper.availableSecond = -1.0;
    helper.parseOrder = marker.parseOrder;
    helper.eachGroupId = marker.eachGroupId;
    helper.sourceLine = marker.sourceLine;
    helper.sourceCol = qMax(1, marker.sourceCol + 1);
    helper.lane = marker.lane;
    helper.endLane = marker.lane;
    helper.type = QStringLiteral("tap");
    helper.isEach = marker.headEach;
    helper.isBreak = marker.headBreak;
    helper.isEx = marker.headEx;
    helper.hasHeadStar = true;
    helper.slideTrackKey = QStringLiteral("__static_helper_head_tap__");
    return helper;
}

QVector<TimelineNoteMarker> markersWithStaticHelperHeadTaps(const QVector<TimelineNoteMarker>& noteMarkers)
{
    QVector<TimelineNoteMarker> augmented = noteMarkers;
    augmented.reserve(noteMarkers.size() * 2);

    QSet<QString> emittedKeys;
    for (const TimelineNoteMarker& marker : noteMarkers) {
        if (!isSlideLike(marker) || !marker.hasHeadStar) {
            continue;
        }

        const int helperCol = qMax(1, marker.sourceCol + 1);
        const QString emitKey = marker.sameHeadSlide
            ? QStringLiteral("same_head_helper|%1|%2|%3|%4|%5|%6")
                  .arg(marker.second, 0, 'f', 6)
                  .arg(marker.sourceLine)
                  .arg(marker.lane)
                  .arg(marker.headBreak ? 1 : 0)
                  .arg(marker.headEx ? 1 : 0)
                  .arg(marker.headEach ? 1 : 0)
            : QStringLiteral("helper|%1|%2|%3|%4|%5")
                  .arg(marker.second, 0, 'f', 6)
                  .arg(marker.sourceLine)
                  .arg(helperCol)
                  .arg(marker.lane)
                  .arg(marker.parseOrder);
        if (emittedKeys.contains(emitKey)) {
            continue;
        }
        emittedKeys.insert(emitKey);
        augmented.append(makeStaticHelperHeadTapMarker(marker));
    }

    return augmented;
}

}  // namespace

namespace miacode::muri {

QVector<MuriStaticReference> buildStaticMuriReferences(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double collideThresholdSeconds)
{
    const QVector<TimelineNoteMarker> augmentedMarkers = markersWithStaticHelperHeadTaps(noteMarkers);
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
    slideIndices.reserve(augmentedMarkers.size());
    wifiIndices.reserve(augmentedMarkers.size());
    nonSlideIndices.reserve(augmentedMarkers.size());

    for (int index = 0; index < augmentedMarkers.size(); ++index) {
        const TimelineNoteMarker& marker = augmentedMarkers.at(index);
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
        const TimelineNoteMarker& slide = augmentedMarkers.at(slideIndex);
        const double criticalSecond = slideCriticalSecondForMarker(slide);
        if (slide.slideTraceSecond < 0.0 || slide.endSecond < slide.slideTraceSecond || criticalSecond < 0.0) {
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
            const TimelineNoteMarker& note = augmentedMarkers.at(noteIndex);
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
                record.kind = MuriKind::SlideHeadTap;
                record.alertLevel = slideHeadTapAlertLevel(
                    slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(slide)),
                    qAbs(slide.slideTraceSecond - note.second));
                record.affected = staticReferenceNoteFromMarker(note);
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
                record.kind = MuriKind::TapOnSlide;
                record.alertLevel = tapOnSlideAlertLevel(
                    qAbs(collide.enterSecond - note.second),
                    normalizedCollideThresholdSeconds);
                record.affected = staticReferenceNoteFromMarker(note);
                record.cause = staticReferenceNoteFromMarker(slide);
                record.deltaSecond = collide.enterSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }

            if (!endPad.isEmpty()
                && notePad == endPad
                && criticalSecond >= 0.0
                && note.second > criticalSecond + normalizedCollideThresholdSeconds + kStaticTimeEpsilonSeconds
                && note.second <= slide.endSecond + collideExtraDeltaSeconds + kStaticTimeEpsilonSeconds) {
                MuriStaticReference record;
                record.kind = MuriKind::TapOnSlide;
                record.alertLevel = tapOnSlideAlertLevel(
                    qAbs(criticalSecond - note.second),
                    normalizedCollideThresholdSeconds);
                record.affected = staticReferenceNoteFromMarker(note);
                record.cause = staticReferenceNoteFromMarker(slide);
                record.deltaSecond = criticalSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }
        }
    }

    for (int wifiIndex : wifiIndices) {
        const TimelineNoteMarker& wifi = augmentedMarkers.at(wifiIndex);
        const double criticalSecond = slideCriticalSecondForMarker(wifi);
        if (wifi.slideTraceSecond < 0.0 || wifi.endSecond < wifi.slideTraceSecond || criticalSecond < 0.0) {
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
            const TimelineNoteMarker& note = augmentedMarkers.at(noteIndex);
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
                record.kind = MuriKind::SlideHeadTap;
                record.alertLevel = slideHeadTapAlertLevel(
                    slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(wifi)),
                    qAbs(wifi.slideTraceSecond - note.second));
                record.affected = staticReferenceNoteFromMarker(note);
                record.cause = staticReferenceNoteFromMarker(wifi);
                record.deltaSecond = wifi.slideTraceSecond - note.second;
                record.hasDelta = true;
                record.slideHeadHasTapOnSlide =
                    slideKeysWithTapOnSlideHead.contains(makeMarkerAnalysisKey(wifi));
                records.append(record);
            }

            if (endLanes.contains(note.lane) && markerMomentInRange(note.second, startSecond, endSecond)) {
                MuriStaticReference record;
                record.kind = MuriKind::TapOnSlide;
                record.alertLevel = tapOnSlideAlertLevel(
                    qAbs(criticalSecond - note.second),
                    normalizedCollideThresholdSeconds);
                record.affected = staticReferenceNoteFromMarker(note);
                record.cause = staticReferenceNoteFromMarker(wifi);
                record.deltaSecond = criticalSecond - note.second;
                record.hasDelta = true;
                records.append(record);
            }
        }
    }

    for (int i = 0; i < nonSlideIndices.size(); ++i) {
        const int noteIndex = nonSlideIndices.at(i);
        const TimelineNoteMarker& note = augmentedMarkers.at(noteIndex);
        if (note.slideTrackKey == QLatin1String("__static_helper_head_tap__")) {
            continue;
        }
        const QString notePad = markerPadToken(note);
        if (notePad.isEmpty()) {
            continue;
        }

        for (int j = 0; j < nonSlideIndices.size(); ++j) {
            const int note2Index = nonSlideIndices.at(j);
            if (noteIndex == note2Index) {
                continue;
            }
            const TimelineNoteMarker& note2 = augmentedMarkers.at(note2Index);
            if (note2.slideTrackKey == QLatin1String("__static_helper_head_tap__")) {
                continue;
            }
            if (notePad != markerPadToken(note2)) {
                continue;
            }

            bool overlap = false;
            if (isTapLikeMarker(note)) {
                if (isTapLikeMarker(note2)) {
                    overlap = (i < j)
                        && qAbs(note.second - note2.second) <= kStaticOverlayThresholdSeconds + kStaticTimeEpsilonSeconds;
                } else if (isHoldLikeMarker(note2) && note2.endSecond >= 0.0) {
                    overlap = markerMomentInRange(
                        note.second,
                        note2.second - kStaticOverlayThresholdSeconds,
                        note2.endSecond + kStaticOverlayThresholdSeconds);
                }
            } else if (isHoldLikeMarker(note) && note.endSecond >= 0.0) {
                if (isHoldLikeMarker(note2) && note2.endSecond >= 0.0 && i < j) {
                    overlap =
                        markerMomentInRange(
                            note.second,
                            note2.second - kStaticOverlayThresholdSeconds,
                            note2.endSecond + kStaticOverlayThresholdSeconds)
                        || markerMomentInRange(
                            note2.second,
                            note.second - kStaticOverlayThresholdSeconds,
                            note.endSecond + kStaticOverlayThresholdSeconds);
                }
            }

            if (!overlap) {
                continue;
            }

            MuriStaticReference record;
            record.kind = MuriKind::Overlap;
            record.affected = staticReferenceNoteFromMarker(note);
            record.cause = staticReferenceNoteFromMarker(note2);
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
