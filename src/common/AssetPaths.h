#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace miacode::assets {

inline QString findAssetRoot()
{
    static const QString cachedRoot = []() -> QString {
        QStringList candidates;
        QDir cursor(QCoreApplication::applicationDirPath());
        candidates << QDir::cleanPath(cursor.filePath("../Resources/assets"));
        for (int depth = 0; depth < 8; ++depth) {
            candidates << QDir::cleanPath(cursor.filePath("assets"));
            if (!cursor.cdUp()) {
                break;
            }
        }
        for (const QString& candidate : candidates) {
            if (QFileInfo(candidate).isDir()) {
                return candidate;
            }
        }
        return QString();
    }();
    return cachedRoot;
}

inline QString assetPath(const QString& relativePath)
{
    const QString root = findAssetRoot();
    if (root.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(QDir(root).filePath(relativePath));
}

inline QString primaryOutlinePath()
{
    return assetPath(QStringLiteral("background/outline.png"));
}

inline QString legacyOutlinePath()
{
    return assetPath(QStringLiteral("background/outline_2.png"));
}

inline QString outlinePathForStageMedia(bool hasStageMedia)
{
    const QString preferredPath = hasStageMedia ? primaryOutlinePath() : legacyOutlinePath();
    if (QFileInfo::exists(preferredPath)) {
        return preferredPath;
    }

    const QString fallbackPath = hasStageMedia ? legacyOutlinePath() : primaryOutlinePath();
    if (QFileInfo::exists(fallbackPath)) {
        return fallbackPath;
    }

    return QString();
}

}  // namespace miacode::assets
