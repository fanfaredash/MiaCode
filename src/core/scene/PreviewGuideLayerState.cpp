#include "core/scene/PreviewGuideLayerState.h"

#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneMath.h"
#include "core/scene/PreviewSkinSelectors.h"

#include <QHash>

namespace {

using miacode::preview::scene::PreviewSpriteDescriptor;
using miacode::preview::scene::PreviewSpriteDescriptors;
using miacode::preview::scene::TapApproachSample;

TapApproachSample sampleHoldTailGuideApproach(qreal deltaSeconds, const miacode::preview::scene::PreviewTapTiming& tapTiming)
{
    TapApproachSample approach;
    approach.distance = miacode::preview::scene::kLogicalDistanceTap;
    approach.scale = 0.0;
    if (tapTiming.flyDurationSeconds <= 0.0 || deltaSeconds < -tapTiming.flyDurationSeconds) {
        return approach;
    }
    if (deltaSeconds < 0.0) {
        approach.distance = qBound<qreal>(
            miacode::preview::scene::kLogicalDistanceTap,
            miacode::preview::scene::kLogicalDistanceTap
                + (deltaSeconds + tapTiming.flyDurationSeconds) * tapTiming.unitsPerSecond,
            miacode::preview::scene::kLogicalDistanceEdge
        );
        approach.scale = 1.0;
        return approach;
    }
    approach.distance = miacode::preview::scene::kLogicalDistanceEdge;
    approach.scale = 1.0;
    return approach;
}

void appendGuideSprite(
    PreviewSpriteDescriptors* sprites,
    const QImage* image,
    const QPointF& logicalPos,
    qreal scale,
    qreal angleDegrees,
    const QRectF& playfieldRect
)
{
    if (sprites == nullptr || image == nullptr || image->isNull() || scale <= 0.0) {
        return;
    }
    const qreal canvasScale = playfieldRect.width() / miacode::preview::scene::kLogicalCanvasSize;
    PreviewSpriteDescriptor sprite;
    sprite.image = image;
    sprite.center = miacode::preview::scene::mapLogicalPointToRect(logicalPos, playfieldRect);
    sprite.width = qMax<qreal>(1.0, qRound(image->width() * canvasScale * scale));
    sprite.height = qMax<qreal>(1.0, qRound(image->height() * canvasScale * scale));
    sprite.rotationDegrees = angleDegrees;
    sprites->append(sprite);
}

void appendConcentricGuide(
    PreviewSpriteDescriptors* sprites,
    const QImage* image,
    qreal distance,
    qreal sourceRadius,
    qreal angleDegrees,
    const QRectF& playfieldRect
)
{
    if (distance <= 0.0 || sourceRadius <= 0.0) {
        return;
    }
    const qreal guideRadius = distance < miacode::preview::scene::kLogicalDistanceTap
        ? miacode::preview::scene::kLogicalDistanceTap
        : qMin(distance, miacode::preview::scene::kLogicalDistanceEdge);
    appendGuideSprite(
        sprites,
        image,
        QPointF(miacode::preview::scene::kLogicalCanvasCenter, miacode::preview::scene::kLogicalCanvasCenter),
        guideRadius / sourceRadius,
        angleDegrees,
        playfieldRect
    );
}

}  // namespace

namespace miacode::preview::scene {

PreviewSpriteDescriptors buildPreviewGuideLayerSprites(
    const PreviewFrameState& state,
    const PreviewActiveMarkerView& markers,
    const QRectF& playfieldRect
)
{
    struct ActiveEachCandidate {
        const TimelineNoteMarker* marker = nullptr;
    };

    PreviewSpriteDescriptors sprites;
    sprites.reserve(markers.size() * 2);

    QHash<int, QVector<ActiveEachCandidate>> eachGroupsById;
    QHash<qint64, QVector<ActiveEachCandidate>> fallbackEachGroupsBySecond;
    const auto addEachCandidate = [&eachGroupsById, &fallbackEachGroupsBySecond](const TimelineNoteMarker& marker) {
        if (marker.eachGroupId >= 0) {
            eachGroupsById[marker.eachGroupId].append(ActiveEachCandidate{&marker});
            return;
        }
        const qint64 secondKey = qRound64(marker.second * 1000000.0);
        fallbackEachGroupsBySecond[secondKey].append(ActiveEachCandidate{&marker});
    };

    // Per-marker timing: each note's hsMultiplier scales the effective
    // flow speed. See PreviewHeadLayerState.cpp for the design rationale.
    const auto markerTapTiming = [&state](double hsMultiplier) {
        return previewTapTimingForEffectiveFlowSpeed(
            static_cast<qreal>(state.render.tapFlowSpeed * hsMultiplier));
    };
    const auto tapApproachWith = [](const PreviewTapTiming& timing, qreal deltaSeconds) {
        return sampleTapApproach(
            deltaSeconds,
            timing.lifecycleDurationSeconds,
            timing.spawnDurationSeconds,
            timing.flyDurationSeconds,
            timing.unitsPerSecond,
            kLogicalDistanceTap,
            kLogicalDistanceEdge,
            timing.directionSign
        );
    };

    for (int markerIndex = 0; markerIndex < markers.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = markers.markerAt(markerIndex);
        if (marker.type == QLatin1String("tap") || marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) {
            const bool slideHeadStar = marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi");
            if (slideHeadStar && !marker.hasHeadStar) {
                continue;
            }
            const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
            const PreviewTapTiming tapTiming = markerTapTiming(marker.hsMultiplier);
            const TapApproachSample approach = tapApproachWith(tapTiming, deltaSeconds);
            if (state.playheadSeconds > marker.second || approach.scale <= 0.0) {
                continue;
            }
            appendConcentricGuide(
                &sprites,
                selectTapNoteGuideImage(state.skin, marker),
                approach.distance,
                kNoteGuideSourceRadius,
                laneRotationDegrees(marker.lane),
                playfieldRect
            );
            const bool inEachGroup = slideHeadStar ? marker.headEach : marker.isEach;
            if (inEachGroup) {
                addEachCandidate(marker);
            }
        } else if (marker.type == QLatin1String("hold")) {
            if (marker.endSecond < marker.second || state.playheadSeconds > marker.endSecond) {
                continue;
            }
            const qreal deltaSeconds = static_cast<qreal>(state.playheadSeconds - marker.second);
            // Hold guides take the HS magnitude (never reverse), matching the
            // head layer's per-type sign policy.
            const PreviewTapTiming tapTiming = markerTapTiming(qAbs(marker.hsMultiplier));
            const TapApproachSample approach = tapApproachWith(tapTiming, deltaSeconds);
            if (approach.scale <= 0.0) {
                continue;
            }
            const QImage* headImage = marker.isBreak ? &state.skin.noteGuideBreakImage
                : marker.isEach ? &state.skin.noteGuideEachImage
                : &state.skin.noteGuideNormalImage;
            appendConcentricGuide(
                &sprites,
                headImage,
                approach.distance,
                kNoteGuideSourceRadius,
                laneRotationDegrees(marker.lane),
                playfieldRect
            );

            const qreal deltaEndSeconds = static_cast<qreal>(state.playheadSeconds - marker.endSecond);
            const TapApproachSample tailApproach = sampleHoldTailGuideApproach(deltaEndSeconds, tapTiming);
            if (tailApproach.scale > 0.0) {
                const QPointF unit = laneUnitVector(marker.lane);
                appendGuideSprite(
                    &sprites,
                    selectHoldEndNoteGuideImage(state.skin, marker),
                    QPointF(
                        kLogicalCanvasCenter + unit.x() * tailApproach.distance,
                        kLogicalCanvasCenter + unit.y() * tailApproach.distance
                    ),
                    tailApproach.scale,
                    laneRotationDegrees(marker.lane),
                    playfieldRect
                );
            }
            if (marker.isEach) {
                addEachCandidate(marker);
            }
        }
    }

    const auto appendEachGroupGuides = [&sprites, &playfieldRect, &state, &markerTapTiming, &tapApproachWith](const QVector<ActiveEachCandidate>& notes) {
        if (notes.size() < 2) {
            return;
        }

        const int groupSize = notes.size();
        int laneDistance = 4;
        if (groupSize == 2) {
            const int a = notes[0].marker->lane;
            const int b = notes[1].marker->lane;
            const int delta = qAbs(a - b);
            laneDistance = qMin(delta, 8 - delta);
        }

        const QImage* lineImage = nullptr;
        qreal sourceRadius = kEachLine4SourceRadius;
        if (groupSize >= 3 || laneDistance == 4) {
            lineImage = &state.skin.noteGuideEachLine4Image;
            sourceRadius = kEachLine4SourceRadius;
        } else if (laneDistance == 3) {
            lineImage = &state.skin.noteGuideEachLine3Image;
            sourceRadius = kEachLine3SourceRadius;
        } else if (laneDistance == 2) {
            lineImage = &state.skin.noteGuideEachLine2Image;
            sourceRadius = kEachLine2SourceRadius;
        } else if (laneDistance == 1) {
            lineImage = &state.skin.noteGuideEachLine1Image;
            sourceRadius = kEachLine1SourceRadius;
        }
        if (lineImage == nullptr || lineImage->isNull()) {
            return;
        }

        // Each-group guides share one timing across the group; use the
        // first member's HS (typical author convention is uniform-HS
        // within an each-group).
        const PreviewTapTiming groupTapTiming = markerTapTiming(notes[0].marker->hsMultiplier);
        const TapApproachSample approach = tapApproachWith(
            groupTapTiming,
            static_cast<qreal>(state.playheadSeconds - notes[0].marker->second));
        if (approach.scale <= 0.0) {
            return;
        }

        qreal angleDegrees = 0.0;
        if (!(groupSize >= 3 || laneDistance == 4)) {
            const int a = notes[0].marker->lane;
            const int b = notes[1].marker->lane;
            int diff = (b - a) % 8;
            if (diff > 4) {
                diff -= 8;
            } else if (diff < -4) {
                diff += 8;
            }
            qreal midpoint = a + diff / 2.0;
            while (midpoint <= 0.0) {
                midpoint += 8.0;
            }
            while (midpoint > 8.0) {
                midpoint -= 8.0;
            }
            const qreal sourceMidpoint = 1.0 + laneDistance / 2.0;
            angleDegrees = laneRotationDegreesForIndex(midpoint) - laneRotationDegreesForIndex(sourceMidpoint);
        }

        appendConcentricGuide(
            &sprites,
            lineImage,
            approach.distance,
            sourceRadius,
            angleDegrees,
            playfieldRect
        );
    };

    for (auto it = eachGroupsById.cbegin(); it != eachGroupsById.cend(); ++it) {
        appendEachGroupGuides(it.value());
    }
    for (auto it = fallbackEachGroupsBySecond.cbegin(); it != fallbackEachGroupsBySecond.cend(); ++it) {
        appendEachGroupGuides(it.value());
    }

    return sprites;
}

}  // namespace miacode::preview::scene
