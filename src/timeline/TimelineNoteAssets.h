#pragma once

#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QString>

namespace miacode::timeline {

struct TimelineNoteAssetSet {
    QHash<QString, QPixmap> noteIcons;
    QHash<QString, int> noteIconBasePixelSizes;
};

struct TimelineHoldPixmapParts {
    QPixmap leftCap;
    QPixmap rightCap;
    QImage bodySlice;
};

TimelineNoteAssetSet loadTimelineNoteAssets();
int transformedPixmapScalePermille(qreal scale);
int transformedPixmapRotationTenths(qreal rotationDegrees);
QString transformedPixmapCacheKey(const QString& type, qreal scale, qreal rotationDegrees, bool mirrorX);
QString transformedPixmapCacheKey(const QString& type, const QSize& targetSize, qreal rotationDegrees, bool mirrorX);
QString holdPixmapCacheKey(const QString& type, qreal scale);
int noteIconBasePixelSizeForType(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    int fallbackPixelSize = 14);
QSize targetSizeForNoteType(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    qreal scale = 1.0,
    int fallbackPixelSize = 14);
QPixmap transformNotePixmapToTargetSize(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    const QSize& targetSize,
    qreal rotationDegrees = 0.0,
    bool mirrorX = false);
QPixmap transformNotePixmap(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    qreal scale = 1.0,
    qreal rotationDegrees = 0.0,
    bool mirrorX = false,
    int fallbackPixelSize = 14);
qreal holdScaleForBaseIconScale(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    qreal baseIconScale,
    int fallbackPixelSize = 14);
TimelineHoldPixmapParts buildHoldPixmapParts(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    qreal scale,
    int fallbackPixelSize = 14);
TimelineHoldPixmapParts buildHoldPixmapPartsForTargetSize(
    const TimelineNoteAssetSet& assets,
    const QString& type,
    const QSize& targetSize,
    int fallbackPixelSize = 14);

}  // namespace miacode::timeline
