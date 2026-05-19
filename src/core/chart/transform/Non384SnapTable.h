#pragma once

namespace miacode::chart_transform {

inline constexpr int kSnap384Modulus = 384;

struct SnapResult {
    bool ok = false;
    int p = 0;
    int q = 1;
};

// Snap x/y onto the (q | 384, q <= 4y) grid using the rule:
//   - q is the LARGEST divisor of 384 with q <= 4y
//   - p = round(q*x / y)  (round half away from zero)
//
//   - y | 384 (or y == 1): pass-through {true, x, y}
//   - y > 384: round to the 384 grid {true, round(384*x/y), 384}
//   - 0 < y <= 384 and y does not divide 384: applies the rule above
SnapResult snapXOverY(int x, int y);

}  // namespace miacode::chart_transform
