#include "Non384SnapTable.h"

#include <QVector>
#include <QtCore/QtGlobal>

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

int largestDivisorOf384AtMost(qint64 cap)
{
    int chosen = 0;
    for (int q : divisorsOf384Ascending()) {
        if (static_cast<qint64>(q) <= cap) {
            chosen = q;
        } else {
            break;
        }
    }
    return chosen;
}

}  // namespace

SnapResult snapXOverY(int x, int y)
{
    if (y <= 0) {
        return {false, 0, 0};
    }
    if ((kSnap384Modulus % y) == 0) {
        return {true, x, y};
    }
    if (y > kSnap384Modulus) {
        const qint64 p = roundDivPositive(
            static_cast<qint64>(kSnap384Modulus) * x,
            static_cast<qint64>(y));
        return {true, static_cast<int>(p), kSnap384Modulus};
    }
    const int chosenQ = largestDivisorOf384AtMost(static_cast<qint64>(4) * y);
    if (chosenQ <= 0) {
        return {false, 0, 0};
    }
    const qint64 p = roundDivPositive(
        static_cast<qint64>(chosenQ) * x,
        static_cast<qint64>(y));
    return {true, static_cast<int>(p), chosenQ};
}

}  // namespace miacode::chart_transform
