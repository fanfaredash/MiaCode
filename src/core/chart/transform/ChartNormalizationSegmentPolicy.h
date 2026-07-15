#pragma once

#include <QtGlobal>

#include <QString>

#include "core/chart/transform/Non384SnapTable.h"

namespace miacode::chart_transform::segment_policy {

struct RationalValue {
    qint64 numerator = 0;
    qint64 denominator = 1;
};

struct SpecialSegmentInput {
    int beats = 1;
    RationalValue startPhaseWhole;
    RationalValue endPhaseWhole;
    bool consumedComma = false;
};

bool followingTextNeedsBeatsCarry(const QString& remainder);

bool subdivisionFits384Grid(int beats);
bool rationalFits384Grid(const RationalValue& value);

int approximateSegmentSubdivision(
    int preferredBeats,
    const RationalValue& segmentLengthWhole,
    int maximumBeats = kSnap384Modulus);

bool specialSegmentForcesReset(
    const SpecialSegmentInput& segment,
    int meterDenominator);

}  // namespace miacode::chart_transform::segment_policy
