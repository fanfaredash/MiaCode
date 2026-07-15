#include "ChartNormalizationSegmentPolicy.h"

#include <algorithm>
#include <limits>
#include <numeric>

namespace miacode::chart_transform::segment_policy {
namespace {

RationalValue normalized(RationalValue value)
{
    if (value.denominator == 0) {
        return RationalValue{0, 1};
    }
    if (value.denominator < 0) {
        value.numerator = -value.numerator;
        value.denominator = -value.denominator;
    }
    if (value.numerator == 0) {
        return RationalValue{0, 1};
    }
    const qint64 gcd = std::gcd(qAbs(value.numerator), value.denominator);
    if (gcd > 1) {
        value.numerator /= gcd;
        value.denominator /= gcd;
    }
    return value;
}

qint64 safeLcm(qint64 left, qint64 right)
{
    if (left <= 0) {
        return qMax<qint64>(1, right);
    }
    if (right <= 0) {
        return qMax<qint64>(1, left);
    }
    const qint64 gcd = std::gcd(left, right);
    const qint64 scaled = left / gcd;
    if (scaled > (std::numeric_limits<qint64>::max() / right)) {
        return 0;
    }
    return scaled * right;
}

bool scalesExactly(const RationalValue& rawValue, int scale)
{
    if (scale <= 0) {
        return false;
    }
    const RationalValue value = normalized(rawValue);
    const qint64 scaledNumerator = value.numerator * static_cast<qint64>(scale);
    return (scaledNumerator % value.denominator) == 0;
}

bool terminalMarkerAt(const QString& text, int index)
{
    if (index < 0 || index >= text.size()) {
        return false;
    }
    const QChar marker = text.at(index);
    if (marker != QLatin1Char('E') && marker != QLatin1Char('e')) {
        return false;
    }
    for (int i = index + 1; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\n')) {
            return true;
        }
        if (!ch.isSpace()) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool followingTextNeedsBeatsCarry(const QString& remainder)
{
    for (int index = 0; index < remainder.size(); ++index) {
        const QChar ch = remainder.at(index);
        if (ch.isSpace()) {
            continue;
        }
        if (terminalMarkerAt(remainder, index)) {
            return false;
        }
        if (ch == QLatin1Char('{')) {
            return false;
        }
        if (ch == QLatin1Char('|')
            && index + 1 < remainder.size()
            && remainder.at(index + 1) == QLatin1Char('|')) {
            return false;
        }
        if (ch == QLatin1Char(',')) {
            return true;
        }
        return true;
    }
    return false;
}

bool subdivisionFits384Grid(int beats)
{
    return beats > 0 && (kSnap384Modulus % beats) == 0;
}

bool rationalFits384Grid(const RationalValue& value)
{
    return scalesExactly(value, kSnap384Modulus);
}

int approximateSegmentSubdivision(
    int preferredBeats,
    const RationalValue& segmentLengthWhole,
    int maximumBeats)
{
    const int maximum = qMax(1, maximumBeats);
    int chosen = qBound(1, preferredBeats, maximum);
    if (scalesExactly(segmentLengthWhole, chosen)) {
        return chosen;
    }

    const RationalValue length = normalized(segmentLengthWhole);
    const qint64 lcm = safeLcm(chosen, length.denominator);
    if (lcm > 0 && lcm <= maximum && (maximum % lcm) == 0) {
        return static_cast<int>(lcm);
    }
    return maximum;
}

bool specialSegmentForcesReset(
    const SpecialSegmentInput& segment,
    int meterDenominator)
{
    if (!segment.consumedComma || subdivisionFits384Grid(segment.beats)) {
        return false;
    }

    const int halfBeatScale = qMax(1, meterDenominator) * 2;
    return !scalesExactly(segment.startPhaseWhole, halfBeatScale)
        || !scalesExactly(segment.endPhaseWhole, halfBeatScale);
}

}  // namespace miacode::chart_transform::segment_policy
