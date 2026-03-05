#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace miacode::assets {

inline QString findAssetRoot()
{
    static const QString cachedRoot = []() -> QString {
        QDir cursor(QCoreApplication::applicationDirPath());
        for (int depth = 0; depth < 8; ++depth) {
            const QString candidate = QDir::cleanPath(cursor.filePath("assets"));
            if (QFileInfo(candidate).isDir()) {
                return candidate;
            }
            if (!cursor.cdUp()) {
                break;
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

}  // namespace miacode::assets
