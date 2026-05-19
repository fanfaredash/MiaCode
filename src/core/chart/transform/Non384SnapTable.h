#pragma once

#include <QString>
#include <QVector>

namespace miacode::chart_transform {

inline constexpr int kSnap384Modulus = 384;

struct UniformSnapTable {
    int q = 0;             // chosen subdivision; 0 means uncomputed/invalid
    QVector<int> pByX;     // pByX[x] = p for x/y; size == y when valid
};

struct SnapResult {
    bool ok = false;
    int p = 0;
    int q = 1;
};

// Snap x/y onto a (q | 384, q <= 4y) grid using a SINGLE q chosen per y.
//   - y | 384 (or y == 1): pass-through {true, x, y}
//   - 0 < y <= 384 and y does not divide 384: looks up the uniform table
//   - y > 384: falls back to 384-grid rounding {true, round(384*x/y), 384}
//   - x may exceed y; whole-note carry is split (x = k*y + r) and added back
//     as k*q in the returned p.
SnapResult snapXOverY(int x, int y);

// Direct table access (cache populated lazily via uniformSnapTableForY).
const UniformSnapTable& uniformSnapTableForY(int y);

// Force-populate the cache for every non-384-divisor y in [2, maxY].
void populateUniformSnapTableCache(int maxY = kSnap384Modulus);

// Default JSON path: <appDir>/non384_snap_table.json
QString defaultSnapTableCachePath();

// JSON I/O. Empty path uses the default location.
bool loadUniformSnapTableCacheFromJson(const QString& path = QString());
bool saveUniformSnapTableCacheToJson(const QString& path = QString());

// On first call: tries to load JSON; if missing or invalid, builds runtime
// and (best effort) saves it back. Subsequent calls are no-ops.
void ensureUniformSnapTableReady();

// Verification.
struct SnapCollision {
    int y = 0;
    int x = 0;
    int p = 0;
    int q = 0;
};
QVector<SnapCollision> findUniformSnapTableCollisions(int maxY = kSnap384Modulus);

}  // namespace miacode::chart_transform
