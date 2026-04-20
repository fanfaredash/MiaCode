#pragma once

#include <QtGlobal>

namespace miacode::preview_global_timing {

constexpr double kFixedFirstOffsetSeconds = -(2.0 / 60.0);

inline double effectiveFirstSeconds(double rawFirstSeconds)
{
    return (qIsFinite(rawFirstSeconds) ? rawFirstSeconds : 0.0) + kFixedFirstOffsetSeconds;
}

}  // namespace miacode::preview_global_timing
