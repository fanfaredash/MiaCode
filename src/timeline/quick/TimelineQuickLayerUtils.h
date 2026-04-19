#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QSizeF>

#include "timeline/TimelineSceneState.h"

class QSGNode;

struct TimelineQuickRasterizedImage {
    QImage image;
    QSizeF logicalSize;
};

QImage makeTimelineGlyphImage(const miacode::timeline::TimelineSceneGlyph& glyph);
TimelineQuickRasterizedImage makeTimelineTextImage(
    const miacode::timeline::TimelineSceneTextLabel& label,
    qreal devicePixelRatio = 1.0);
QSGNode* buildTimelineLineNode(const miacode::timeline::TimelineSceneLine& line);
QSGNode* buildTimelineRectNode(const miacode::timeline::TimelineSceneRect& rect);
QSGNode* buildTimelineTriangleNode(const miacode::timeline::TimelineSceneTriangle& triangle);
