#include "Non384SnapTable.h"

#include <QtCore/QtGlobal>
#include <QtCore/QtMath>

#include <limits>

namespace miacode::chart_transform {
namespace {

const QVector<int>& divisorsOf384Ascending()
{
    static const QVector<int> divs = []() {
        QVector<int> result;
        for (int d = 1; d <= kSnap384Modulus; ++d) {
            if ((kSnap384Modulus % d) == 0) {
                result.append(d);
            }
        }
        return result;
    }();
    return divs;
}

inline qint64 roundDivPositive(qint64 num, qint64 den)
{
    return (num + den / 2) / den;
}

}  // namespace

bool operator==(const SnapTarget& lhs, const SnapTarget& rhs)
{
    return lhs.p == rhs.p && lhs.q == rhs.q;
}

bool operator!=(const SnapTarget& lhs, const SnapTarget& rhs)
{
    return !(lhs == rhs);
}

SnapTarget nearestSnapTargetForNon384(int x, int y)
{
    SnapTarget best{0, 1};
    if (y <= 0) {
        return best;
    }

    qint64 bestErrNum = 0;
    qint64 bestErrDen = 1;
    bool initialized = false;
    const qint64 fourY = static_cast<qint64>(4) * static_cast<qint64>(y);

    for (int q : divisorsOf384Ascending()) {
        if (static_cast<qint64>(q) > fourY) {
            break;
        }
        // p = round(q * x / y), all non-negative for x >= 0, y > 0.
        const qint64 p = roundDivPositive(
            static_cast<qint64>(q) * static_cast<qint64>(x),
            static_cast<qint64>(y));
        // |x/y - p/q| = |x*q - p*y| / (y*q)
        const qint64 errNum = qAbs(static_cast<qint64>(x) * q - p * y);
        const qint64 errDen = static_cast<qint64>(y) * q;
        // First candidate always wins; subsequent must strictly beat best.
        //   errNum / errDen < bestErrNum / bestErrDen
        //   <=> errNum * bestErrDen < bestErrNum * errDen
        // The sentinel below avoids overflow that an INT64_MAX seed would
        // cause when multiplied through.
        if (!initialized || errNum * bestErrDen < bestErrNum * errDen) {
            bestErrNum = errNum;
            bestErrDen = errDen;
            best = {static_cast<int>(p), q};
            initialized = true;
        }
    }
    return best;
}

QVector<SnapCollision> findNon384SnapTableCollisions(int maxY)
{
    QVector<SnapCollision> collisions;
    for (int y = 2; y <= maxY; ++y) {
        if ((kSnap384Modulus % y) == 0) {
            continue;
        }
        SnapTarget prev = nearestSnapTargetForNon384(0, y);
        for (int x = 1; x < y; ++x) {
            const SnapTarget curr = nearestSnapTargetForNon384(x, y);
            if (curr == prev) {
                collisions.append({y, x - 1, prev});
            }
            prev = curr;
        }
    }
    return collisions;
}

}  // namespace miacode::chart_transform
