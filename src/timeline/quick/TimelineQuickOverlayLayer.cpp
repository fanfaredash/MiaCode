#include "timeline/quick/TimelineQuickOverlayLayer.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>

#include "timeline/quick/TimelineQuickLayerUtils.h"
#include "timeline/quick/TimelineQuickTextureCache.h"

namespace {

struct TimelineQuickOverlayRootNode : public QSGNode {
    quint64 staticRevision = 0;
    quint64 transformedStaticNotesRevision = 0;
    quint64 transformedStaticRevision = 0;
    quint64 headerRevision = 0;
    quint64 headerDynamicRevision = 0;
    quint64 dynamicRevision = 0;
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
    for (int lookupIndex = 0; child != nullptr && lookupIndex < index; ++lookupIndex) {
        child = child->nextSibling();
    }
    return child;
}

void rebuildOverlaySlots(TimelineQuickOverlayRootNode* root)
{
    if (root == nullptr) {
        return;
    }
    clearChildren(root);

    root->appendChildNode(new QSGNode());

    auto* transformedClipRoot = new QSGClipNode();
    transformedClipRoot->setIsRectangular(true);
    auto* transformRoot = new QSGTransformNode();
    transformRoot->appendChildNode(new QSGNode());
    transformRoot->appendChildNode(new QSGNode());
    transformedClipRoot->appendChildNode(transformRoot);
    root->appendChildNode(transformedClipRoot);

    auto* viewportClipRoot = new QSGClipNode();
    viewportClipRoot->setIsRectangular(true);
    viewportClipRoot->appendChildNode(new QSGNode());
    root->appendChildNode(viewportClipRoot);

    auto* headerClipRoot = new QSGClipNode();
    headerClipRoot->setIsRectangular(true);
    auto* headerTransformRoot = new QSGTransformNode();
    headerTransformRoot->appendChildNode(new QSGNode());
    headerClipRoot->appendChildNode(headerTransformRoot);
    root->appendChildNode(headerClipRoot);
}

bool overlaySlotsValid(TimelineQuickOverlayRootNode* root)
{
    if (root == nullptr) {
        return false;
    }
    QSGNode* staticRoot = childAt(root, 0);
    auto* transformedClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 1));
    auto* viewportClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 2));
    auto* headerClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 3));
    if (staticRoot == nullptr || transformedClipRoot == nullptr || viewportClipRoot == nullptr
        || headerClipRoot == nullptr) {
        return false;
    }
    auto* transformRoot = dynamic_cast<QSGTransformNode*>(transformedClipRoot->firstChild());
    auto* headerTransformRoot = dynamic_cast<QSGTransformNode*>(headerClipRoot->firstChild());
    return transformRoot != nullptr
        && transformRoot->firstChild() != nullptr
        && transformRoot->firstChild()->nextSibling() != nullptr
        && viewportClipRoot->firstChild() != nullptr
        && headerTransformRoot != nullptr
        && headerTransformRoot->firstChild() != nullptr;
}

}  // namespace

QSGNode* TimelineQuickOverlayLayer::updateNode(
    QSGNode* oldNode,
    const miacode::timeline::TimelineSceneState& state,
    QQuickWindow* window,
    TimelineQuickTextureCache* textures) const
{
    Q_UNUSED(window);
    auto* root = dynamic_cast<TimelineQuickOverlayRootNode*>(oldNode);
    if (root == nullptr) {
        delete oldNode;
        root = new TimelineQuickOverlayRootNode();
    }
    if (!overlaySlotsValid(root)) {
        rebuildOverlaySlots(root);
    }

    QSGNode* staticRoot = childAt(root, 0);
    auto* transformedClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 1));
    auto* transformRoot = transformedClipRoot != nullptr
        ? dynamic_cast<QSGTransformNode*>(transformedClipRoot->firstChild())
        : nullptr;
    QSGNode* transformedStaticRoot = transformRoot != nullptr ? childAt(transformRoot, 0) : nullptr;
    QSGNode* transformedDynamicRoot = transformRoot != nullptr ? childAt(transformRoot, 1) : nullptr;
    auto* viewportClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 2));
    QSGNode* viewportRoot = viewportClipRoot != nullptr ? viewportClipRoot->firstChild() : nullptr;
    auto* headerClipRoot = dynamic_cast<QSGClipNode*>(childAt(root, 3));
    auto* headerTransformRoot = headerClipRoot != nullptr
        ? dynamic_cast<QSGTransformNode*>(headerClipRoot->firstChild())
        : nullptr;
    QSGNode* headerTriangleRoot = headerTransformRoot != nullptr ? headerTransformRoot->firstChild() : nullptr;

    const QRectF timelineClipRect(
        state.timelineLeft,
        state.timelineTop,
        qMax(0, state.viewportSize.width() - state.timelineLeft),
        state.timelineHeight);
    const qreal headerLeft = qMax(state.timelineLeft, state.headerMarkerLeftLimit);
    const qreal headerRight = qBound<qreal>(headerLeft, state.headerMarkerRightLimit, state.viewportSize.width());
    const QRectF headerClipRect(headerLeft, 0.0, qMax<qreal>(0.0, headerRight - headerLeft), state.timelineTop);
    if (transformedClipRoot != nullptr) {
        transformedClipRoot->setClipRect(timelineClipRect);
    }
    if (viewportClipRoot != nullptr) {
        viewportClipRoot->setClipRect(timelineClipRect);
    }
    if (headerClipRoot != nullptr) {
        headerClipRoot->setClipRect(headerClipRect);
    }
    QMatrix4x4 scrollTransform;
    scrollTransform.translate(-static_cast<float>(state.horizontalScrollValue), 0.0f);
    if (transformRoot != nullptr) {
        transformRoot->setMatrix(scrollTransform);
    }
    if (headerTransformRoot != nullptr) {
        headerTransformRoot->setMatrix(scrollTransform);
    }

    const bool appearanceChanged = root->appearanceRevision != state.appearanceRevision;
    if (appearanceChanged || root->staticRevision != state.gridRevision) {
        clearChildren(staticRoot);
        for (const auto& rect : state.frameRects) {
            staticRoot->appendChildNode(buildTimelineRectNode(rect));
        }
        for (const auto& line : state.frameLines) {
            staticRoot->appendChildNode(buildTimelineLineNode(line));
        }
        if (textures != nullptr) {
            for (const auto& label : state.laneLabels) {
                if (label.text.isEmpty()) {
                    continue;
                }
                const TimelineQuickTextureHandle handle = textures->textTexture(label);
                if (handle.texture == nullptr) {
                    continue;
                }
                auto* node = new QSGSimpleTextureNode();
                node->setTexture(handle.texture);
                node->setRect(QRectF(label.topLeft, handle.logicalSize));
                staticRoot->appendChildNode(node);
            }
        }
        root->staticRevision = state.gridRevision;
    }

    if (appearanceChanged
        || root->transformedStaticNotesRevision != state.notesRevision
        || root->transformedStaticRevision != state.overlayRevision) {
        clearChildren(transformedStaticRoot);
        if (textures != nullptr) {
            for (const auto& glyph : state.muriDots) {
                const QString key = QStringLiteral("muri_dot|%1|%2|%3")
                                        .arg(qRound(glyph.rect.width()))
                                        .arg(qRound(glyph.rect.height()))
                                        .arg(glyph.fillColor.name(QColor::HexArgb));
                if (QSGTexture* texture = textures->textureForKey(key, makeTimelineGlyphImage(glyph)); texture != nullptr) {
                    auto* node = new QSGSimpleTextureNode();
                    node->setTexture(texture);
                    node->setRect(glyph.rect);
                    transformedStaticRoot->appendChildNode(node);
                }
            }
        }
        root->transformedStaticNotesRevision = state.notesRevision;
        root->transformedStaticRevision = state.overlayRevision;
    }

    if (appearanceChanged || root->dynamicRevision != state.overlayDynamicRevision) {
        clearChildren(transformedDynamicRoot);
        clearChildren(viewportRoot);
        if (state.hasPlayheadLine) {
            transformedDynamicRoot->appendChildNode(buildTimelineLineNode(state.playheadLine));
        }
        if (state.hasCursorLine) {
            transformedDynamicRoot->appendChildNode(buildTimelineLineNode(state.cursorLine));
        }
        if (state.hasDragCenterLine) {
            viewportRoot->appendChildNode(buildTimelineLineNode(state.dragCenterLine));
        }
        root->dynamicRevision = state.overlayDynamicRevision;
    }

    if (appearanceChanged
        || root->headerRevision != state.headerRevision
        || root->headerDynamicRevision != state.overlayDynamicRevision) {
        clearChildren(headerTriangleRoot);
        for (const auto& triangle : state.headerMarkers) {
            headerTriangleRoot->appendChildNode(buildTimelineTriangleNode(triangle));
        }
        if (state.hasEntryMarker) {
            headerTriangleRoot->appendChildNode(buildTimelineTriangleNode(state.entryMarker));
        }
        if (state.hasCursorMarker) {
            headerTriangleRoot->appendChildNode(buildTimelineTriangleNode(state.cursorMarker));
        }
        root->headerRevision = state.headerRevision;
        root->headerDynamicRevision = state.overlayDynamicRevision;
    }
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
