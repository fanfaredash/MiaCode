#pragma once

#include <QVector>

namespace miacode::chart_transform {

inline constexpr int kSnap384Modulus = 384;

struct SnapTarget {
    int p = 0;
    int q = 1;
};

struct SnapCollision {
    int y = 0;
    int x = 0;
    SnapTarget shared;
};

bool operator==(const SnapTarget& lhs, const SnapTarget& rhs);
bool operator!=(const SnapTarget& lhs, const SnapTarget& rhs);

// Returns the nearest p/q to x/y satisfying:
//   - q divides 384
//   - q <= 4y
// Tie-break: prefer smaller q (first hit in ascending divisor order).
//
// Intended for y values that are NOT themselves divisors of 384 (callers
// should bypass this for 384-divisor y where x/y already snaps cleanly).
SnapTarget nearestSnapTargetForNon384(int x, int y);

// Walks every (x, x+1) pair across every non-384-divisor y in [2, maxY]
// and reports adjacency collisions where both x land on the same (p, q).
// An empty result means the rule is usable for that y range.
QVector<SnapCollision> findNon384SnapTableCollisions(int maxY = kSnap384Modulus);

}  // namespace miacode::chart_transform
