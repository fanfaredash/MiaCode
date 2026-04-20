#pragma once

#include <QtGlobal>

namespace miacode::preview_global_timing {

inline double effectiveFirstSeconds(double rawFirstSeconds)
{
    return qIsFinite(rawFirstSeconds) ? -rawFirstSeconds : 0.0;
}

}  // namespace miacode::preview_global_timing
