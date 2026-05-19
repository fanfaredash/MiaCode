#include "Non384SnapTable.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QtCore/QtGlobal>

#include <algorithm>

namespace miacode::chart_transform {
namespace {

QMutex g_cacheMutex;
QHash<int, UniformSnapTable>* g_cache = nullptr;

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

UniformSnapTable computeUniformSnapTable(int y)
{
    UniformSnapTable result;
    if (y <= 0 || y > kSnap384Modulus) {
        return result;
    }
    const qint64 fourY = static_cast<qint64>(4) * y;
    for (int q : divisorsOf384Ascending()) {
        if (static_cast<qint64>(q) > fourY) {
            break;
        }
        QSet<int> seen;
        QVector<int> ps(y);
        bool ok = true;
        for (int x = 0; x < y; ++x) {
            const qint64 p = roundDivPositive(static_cast<qint64>(q) * x, static_cast<qint64>(y));
            const int pi = static_cast<int>(p);
            if (seen.contains(pi)) {
                ok = false;
                break;
            }
            seen.insert(pi);
            ps[x] = pi;
        }
        if (ok) {
            result.q = q;
            result.pByX = ps;
            return result;
        }
    }
    return result;
}

QHash<int, UniformSnapTable>& cacheLocked()
{
    if (g_cache == nullptr) {
        g_cache = new QHash<int, UniformSnapTable>();
    }
    return *g_cache;
}

}  // namespace

const UniformSnapTable& uniformSnapTableForY(int y)
{
    static const UniformSnapTable kEmpty;
    QMutexLocker lock(&g_cacheMutex);
    auto& c = cacheLocked();
    auto it = c.find(y);
    if (it != c.end()) {
        return it.value();
    }
    auto inserted = c.insert(y, computeUniformSnapTable(y));
    return inserted.value();
}

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
    const UniformSnapTable& t = uniformSnapTableForY(y);
    if (t.q == 0) {
        return {false, 0, 0};
    }

    int k = 0;
    int r = x;
    if (r >= y) {
        k = r / y;
        r = r % y;
    }
    if (r < 0 || r >= t.pByX.size()) {
        return {false, 0, 0};
    }
    const qint64 fullP = static_cast<qint64>(k) * t.q + t.pByX[r];
    return {true, static_cast<int>(fullP), t.q};
}

void populateUniformSnapTableCache(int maxY)
{
    for (int y = 2; y <= maxY; ++y) {
        if ((kSnap384Modulus % y) == 0) {
            continue;
        }
        (void)uniformSnapTableForY(y);
    }
}

QString defaultSnapTableCachePath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/non384_snap_table.json");
}

bool loadUniformSnapTableCacheFromJson(const QString& path)
{
    const QString p = path.isEmpty() ? defaultSnapTableCachePath() : path;
    QFile file(p);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = file.readAll();
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    QJsonObject root = doc.object();
    if (root.value(QStringLiteral("modulus")).toInt() != kSnap384Modulus) {
        return false;
    }
    QJsonObject tables = root.value(QStringLiteral("tables")).toObject();

    QMutexLocker lock(&g_cacheMutex);
    auto& c = cacheLocked();
    c.clear();
    for (auto it = tables.begin(); it != tables.end(); ++it) {
        bool keyOk = false;
        const int y = it.key().toInt(&keyOk);
        if (!keyOk) {
            continue;
        }
        QJsonObject entry = it.value().toObject();
        UniformSnapTable t;
        t.q = entry.value(QStringLiteral("q")).toInt();
        const QJsonArray pArr = entry.value(QStringLiteral("p")).toArray();
        t.pByX.reserve(pArr.size());
        for (const QJsonValue& v : pArr) {
            t.pByX.append(v.toInt());
        }
        c.insert(y, t);
    }
    return true;
}

bool saveUniformSnapTableCacheToJson(const QString& path)
{
    const QString p = path.isEmpty() ? defaultSnapTableCachePath() : path;
    QJsonObject root;
    root.insert(QStringLiteral("modulus"), kSnap384Modulus);
    root.insert(QStringLiteral("version"), 1);
    QJsonObject tables;
    {
        QMutexLocker lock(&g_cacheMutex);
        auto& c = cacheLocked();
        QList<int> keys = c.keys();
        std::sort(keys.begin(), keys.end());
        for (int y : keys) {
            const UniformSnapTable& t = c.value(y);
            QJsonObject entry;
            entry.insert(QStringLiteral("q"), t.q);
            QJsonArray pArr;
            for (int v : t.pByX) {
                pArr.append(v);
            }
            entry.insert(QStringLiteral("p"), pArr);
            tables.insert(QString::number(y), entry);
        }
    }
    root.insert(QStringLiteral("tables"), tables);

    QFile file(p);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

void ensureUniformSnapTableReady()
{
    static QMutex initMutex;
    static bool initialized = false;
    QMutexLocker lock(&initMutex);
    if (initialized) {
        return;
    }
    initialized = true;
    if (loadUniformSnapTableCacheFromJson()) {
        return;
    }
    populateUniformSnapTableCache();
    saveUniformSnapTableCacheToJson();
}

QVector<SnapCollision> findUniformSnapTableCollisions(int maxY)
{
    QVector<SnapCollision> collisions;
    for (int y = 2; y <= maxY; ++y) {
        if ((kSnap384Modulus % y) == 0) {
            continue;
        }
        const UniformSnapTable& t = uniformSnapTableForY(y);
        if (t.q == 0 || t.pByX.size() != y) {
            collisions.append({y, 0, 0, 0});
            continue;
        }
        for (int x = 0; x + 1 < t.pByX.size(); ++x) {
            if (t.pByX[x] == t.pByX[x + 1]) {
                collisions.append({y, x, t.pByX[x], t.q});
            }
        }
    }
    return collisions;
}

}  // namespace miacode::chart_transform
