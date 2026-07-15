#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include "core/video/PreviewRenderSettings.h"

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

inline QString outlinePointPath()
{
    return assetPath(QStringLiteral("background/outline_point.png"));
}

inline QString outlineLinePath()
{
    return assetPath(QStringLiteral("background/outline_line.png"));
}

inline QString outlineJudgeAreaPath()
{
    return assetPath(QStringLiteral("background/outline_area.png"));
}

inline QString outlineJudgeAreaLabeledPath()
{
    return assetPath(QStringLiteral("background/outline_area_labeled.png"));
}

inline QString outlineRegionLabelsOverlayPath()
{
    return assetPath(QStringLiteral("background/region_labels_overlay_transparent_v3.png"));
}

inline QString customOutlineRootPath()
{
    return assetPath(QStringLiteral("background/outlines"));
}

inline bool isSupportedCustomOutlinePath(const QString& path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    return info.exists()
        && info.isFile()
        && info.suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) == 0;
}

inline QString customOutlinePathForFileName(const QString& fileName)
{
    const QString trimmed = fileName.trimmed();
    if (trimmed.isEmpty() || QFileInfo(trimmed).fileName() != trimmed) {
        return QString();
    }

    const QString path = QDir(customOutlineRootPath()).filePath(trimmed);
    return isSupportedCustomOutlinePath(path) ? QDir::cleanPath(path) : QString();
}

inline QString outlinePathForVariant(PreviewOutlineVariant variant)
{
    QString preferredPath;
    switch (variant) {
    case PreviewOutlineVariant::Point:
        preferredPath = outlinePointPath();
        break;
    case PreviewOutlineVariant::JudgeArea:
        preferredPath = outlineJudgeAreaPath();
        break;
    case PreviewOutlineVariant::JudgeAreaLabeled:
        preferredPath = outlineJudgeAreaLabeledPath();
        break;
    case PreviewOutlineVariant::Line:
    default:
        preferredPath = outlineLinePath();
        break;
    }
    if (QFileInfo::exists(preferredPath)) {
        return preferredPath;
    }

    const QStringList fallbacks{
        outlineLinePath(),
        assetPath(QStringLiteral("background/outline.png")),
        assetPath(QStringLiteral("background/outline_2.png")),
    };
    for (const QString& fallbackPath : fallbacks) {
        if (QFileInfo::exists(fallbackPath)) {
            return fallbackPath;
        }
    }
    return QString();
}

inline QString outlinePathForVariantOrCustom(
    PreviewOutlineVariant variant,
    const QString& customOutlinePath)
{
    if (isSupportedCustomOutlinePath(customOutlinePath)) {
        return QDir::cleanPath(customOutlinePath);
    }
    return outlinePathForVariant(variant);
}

}  // namespace miacode::assets
