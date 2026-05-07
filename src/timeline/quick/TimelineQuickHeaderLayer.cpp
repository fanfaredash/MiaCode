#include "timeline/quick/TimelineQuickHeaderLayer.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>

#include "timeline/quick/TimelineQuickLayerUtils.h"
#include "timeline/quick/TimelineQuickTextureCache.h"

namespace {

struct TimelineQuickHeaderRootNode : public QSGNode {
    quint64 staticRevision = 0;
    quint64 gridRevision = 0;
    quint64 headerRevision = 0;
    quint64 appearanceRevision = 0;
};

void clearChildren(QSGNode* node)
{
    if (node == nullptr) {
        return;
    }
    while (QSGNode* child = node->firstChild()) {
        node->removeChildNode(child);
        delete child;
    }
}

QSGNode* childAt(QSGNode* parent, int index)
{
    if (parent == nullptr || index < 0) {
        return nullptr;
    }
    QSGNode* child = parent->firstChild();
    for (int currentIndex = 0; child != nullptr && currentIndex < index; ++currentIndex) {
        child = child->nextSibling();
    }
    return child;
}

void rebuildHeaderSlots(TimelineQuickHeaderRootNode* root)
{
    if (root == nullptr) {
        return;
    }
    clearChildren(root);

    root->appendChildNode(new QSGNode());

    auto* gridClipRoot = new QSGClipNode();
    gridClipRoot->setIsRectangular(true);
    auto* gridTransformRoot = new QSGTransformNode();
    gridTransformRoot->appendChildNode(new QSGNode());
    gridClipRoot->appendChildNode(gridTransformRoot);
    root->appendChildNode(gridClipRoot);

    auto* headerClipRoot = new QSGClipNode();
    headerClipRoot->setIsRectangular(true);
    auto* headerTransformRoot = new QSGTransformNode();
    headerTransformRoot->appendChildNode(new QSGNode());
    headerClipRoot->appendChildNode(headerTransformRoot);
    root->appendChildNode(headerClipRoot);
}

bool headerSlotsValid(TimelineQuickHeaderRootNode* root)
{
    if (root == nullptr) {
        return false;
    }
    QSGNode* staticRoot = childAt(root, 0);
    auto* gridClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 1));
    auto* headerClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 2));
    if (staticRoot == nullptr || gridClipRoot == nullptr || headerClipRoot == nullptr) {
        return false;
    }
    return dynamic_cast<QSGTransformNode*>(gridClipRoot->firstChild()) != nullptr
        && dynamic_cast<QSGTransformNode*>(headerClipRoot->firstChild()) != nullptr;
}

}  // namespace

QSGNode* TimelineQuickHeaderLayer::updateNode(
    QSGNode* oldNode,
    const miacode::timeline::TimelineSceneState& state,
    QQuickWindow* window,
    TimelineQuickTextureCache* textures) const
{
    Q_UNUSED(window);
    auto* root = dynamic_cast<TimelineQuickHeaderRootNode*>(oldNode);
    if (root == nullptr) {
        delete oldNode;
        root = new TimelineQuickHeaderRootNode();
    }
    if (!headerSlotsValid(root)) {
        rebuildHeaderSlots(root);
    }

    QSGNode* staticRoot = childAt(root, 0);
    auto* gridClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 1));
    auto* gridTransformRoot =
        gridClipRoot != nullptr ? dynamic_cast<QSGTransformNode*>(gridClipRoot->firstChild()) : nullptr;
    QSGNode* gridContentRoot = gridTransformRoot != nullptr ? gridTransformRoot->firstChild() : nullptr;
    auto* headerClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 2));
    auto* headerTransformRoot =
        headerClipRoot != nullptr ? dynamic_cast<QSGTransformNode*>(headerClipRoot->firstChild()) : nullptr;
    QSGNode* headerContentRoot = headerTransformRoot != nullptr ? headerTransformRoot->firstChild() : nullptr;

    const QRectF timelineClipRect(
        state.timelineLeft,
        state.timelineTop,
        qMax(0, state.viewportSize.width() - state.timelineLeft),
        state.timelineHeight);
    const qreal headerLeft = qMax(state.timelineLeft, state.headerLeftLimit);
    const qreal headerRight = qBound<qreal>(headerLeft, state.headerRightLimit, state.viewportSize.width());
    const QRectF headerClipRect(headerLeft, 0.0, qMax<qreal>(0.0, headerRight - headerLeft), state.timelineTop);
    if (gridClipRoot != nullptr) {
        gridClipRoot->setClipRect(timelineClipRect);
    }
    if (headerClipRoot != nullptr) {
        headerClipRoot->setClipRect(headerClipRect);
    }
    QMatrix4x4 transform;
    transform.translate(-static_cast<float>(state.horizontalScrollValue), 0.0f);
    if (gridTransformRoot != nullptr) {
        gridTransformRoot->setMatrix(transform);
    }
    if (headerTransformRoot != nullptr) {
        headerTransformRoot->setMatrix(transform);
    }

    const bool appearanceChanged = root->appearanceRevision != state.appearanceRevision;
    if (appearanceChanged || root->staticRevision != state.gridRevision) {
        clearChildren(staticRoot);
        for (const auto& rect : state.laneOverlayRects) {
            staticRoot->appendChildNode(buildTimelineRectNode(rect));
        }
        // Header controls (zoom button + follow checkbox). Mirrors the
        // DComp TimelineHeaderSource rendering at lines 146-220 of
        // src/sources/timeline/TimelineHeaderSource.cpp. Lives in
        // staticRoot (no horizontal-scroll transform) because the
        // controls are screen-space — pinned to viewport corners,
        // independent of timeline scroll position.
        //
        // Without this block the zoom% button and Follow Preview
        // checkbox are missing from the QSG render path; they exist
        // as data on TimelineSceneState but only the DComp path was
        // emitting them as visible pixels.
        if (state.hasHeaderControls) {
            // Order matters — same as the DComp source path:
            //   1. zoomButtonBg + followCheckBg (covers widget areas)
            //   2. followCheckIndicator (the box itself, on top of bg)
            //   3. borders (4 hairlines per frame)
            //   4. tick mark when checked
            //   5. text labels (via texture cache)
            staticRoot->appendChildNode(buildTimelineRectNode(state.zoomButtonBg));
            staticRoot->appendChildNode(buildTimelineRectNode(state.followCheckBg));
            staticRoot->appendChildNode(buildTimelineRectNode(state.followCheckIndicator));
            const auto pushFrame = [&staticRoot](
                const miacode::timeline::TimelineSceneRect& r) {
                const qreal x0 = r.rect.left();
                const qreal y0 = r.rect.top();
                const qreal x1 = r.rect.right();
                const qreal y1 = r.rect.bottom();
                const QColor c = r.color;
                staticRoot->appendChildNode(buildTimelineLineNode(
                    miacode::timeline::TimelineSceneLine{
                        QPointF(x0, y0), QPointF(x1, y0), c, 1.0}));
                staticRoot->appendChildNode(buildTimelineLineNode(
                    miacode::timeline::TimelineSceneLine{
                        QPointF(x1, y0), QPointF(x1, y1), c, 1.0}));
                staticRoot->appendChildNode(buildTimelineLineNode(
                    miacode::timeline::TimelineSceneLine{
                        QPointF(x1, y1), QPointF(x0, y1), c, 1.0}));
                staticRoot->appendChildNode(buildTimelineLineNode(
                    miacode::timeline::TimelineSceneLine{
                        QPointF(x0, y1), QPointF(x0, y0), c, 1.0}));
            };
            pushFrame(state.zoomButtonBorder);
            pushFrame(state.followCheckIndicatorBorder);
            if (state.followCheckChecked) {
                // Tick mark — two diagonal hairlines inside the box.
                // Same ratios as the DComp source for visual parity.
                const QRectF box = state.followCheckIndicator.rect;
                const qreal x = box.left();
                const qreal y = box.top();
                const qreal w = box.width();
                const qreal h = box.height();
                const QColor tickColor(255, 255, 255);
                staticRoot->appendChildNode(buildTimelineLineNode(
                    miacode::timeline::TimelineSceneLine{
                        QPointF(x + w * 0.24, y + h * 0.55),
                        QPointF(x + w * 0.44, y + h * 0.74),
                        tickColor, 1.8}));
                staticRoot->appendChildNode(buildTimelineLineNode(
                    miacode::timeline::TimelineSceneLine{
                        QPointF(x + w * 0.44, y + h * 0.74),
                        QPointF(x + w * 0.78, y + h * 0.28),
                        tickColor, 1.8}));
            }
            if (textures != nullptr) {
                const TimelineQuickTextureHandle zoomHandle =
                    textures->textTexture(state.zoomButtonLabel);
                if (zoomHandle.texture != nullptr) {
                    auto* node = new QSGSimpleTextureNode();
                    node->setTexture(zoomHandle.texture);
                    node->setRect(QRectF(state.zoomButtonLabel.topLeft,
                                          zoomHandle.logicalSize));
                    staticRoot->appendChildNode(node);
                }
                const TimelineQuickTextureHandle followHandle =
                    textures->textTexture(state.followCheckLabel);
                if (followHandle.texture != nullptr) {
                    auto* node = new QSGSimpleTextureNode();
                    node->setTexture(followHandle.texture);
                    node->setRect(QRectF(state.followCheckLabel.topLeft,
                                          followHandle.logicalSize));
                    staticRoot->appendChildNode(node);
                }
            }
        }
        root->staticRevision = state.gridRevision;
    }
    if (appearanceChanged || root->gridRevision != state.gridRevision) {
        // Beta21-fix7 — grid lines (bar lines + per-comma note ticks)
        // are rendered by the dedicated TimelineQuickGridLinesLayer,
        // which sits in its own slot AFTER waveformLayer and after
        // this headerLayer. The previous in-place batch builder is
        // gone; this branch just keeps gridContentRoot empty so QSG
        // doesn't carry stale geometry from the earlier layout.
        clearChildren(gridContentRoot);
        root->gridRevision = state.gridRevision;
    }
    if (appearanceChanged || root->headerRevision != state.headerRevision) {
        clearChildren(headerContentRoot);
        for (const auto& triangle : state.headerMarkers) {
            headerContentRoot->appendChildNode(buildTimelineTriangleNode(triangle));
        }
        if (state.hasEntryMarker) {
            headerContentRoot->appendChildNode(buildTimelineTriangleNode(state.entryMarker));
        }
        if (textures != nullptr) {
            for (const auto& label : state.headerLabels) {
                const TimelineQuickTextureHandle handle = textures->textTexture(label);
                if (handle.texture == nullptr) {
                    continue;
                }
                auto* node = new QSGSimpleTextureNode();
                node->setTexture(handle.texture);
                node->setRect(QRectF(label.topLeft, handle.logicalSize));
                headerContentRoot->appendChildNode(node);
            }
        }
        root->headerRevision = state.headerRevision;
    }
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
