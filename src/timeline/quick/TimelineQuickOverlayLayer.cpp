#include "timeline/quick/TimelineQuickOverlayLayer.h"

#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGNode>
#include <QSGSimpleTextureNode>

#include "timeline/quick/TimelineQuickLayerUtils.h"
#include "timeline/quick/TimelineQuickTextureCache.h"

namespace {

struct TimelineQuickOverlayRootNode : public QSGNode {
    quint64 staticRevision = 0;
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

QSGNode* ensureStaticRoot(TimelineQuickOverlayRootNode* root)
{
    if (root == nullptr) {
        return nullptr;
    }
    QSGNode* staticRoot = childAt(root, 0);
    if (staticRoot == nullptr) {
        staticRoot = new QSGNode();
        root->appendChildNode(staticRoot);
    }
    return staticRoot;
}

QSGClipNode* ensureDynamicClipRoot(TimelineQuickOverlayRootNode* root)
{
    if (root == nullptr) {
        return nullptr;
    }
    QSGNode* staticRoot = ensureStaticRoot(root);
    Q_UNUSED(staticRoot);
    QSGNode* child = childAt(root, 1);
    auto* clipRoot = dynamic_cast<QSGClipNode*>(child);
    if (clipRoot != nullptr) {
        if (clipRoot->firstChild() == nullptr) {
            clipRoot->appendChildNode(new QSGNode());
        }
        return clipRoot;
    }

    if (child != nullptr) {
        root->removeChildNode(child);
        delete child;
    }
    clipRoot = new QSGClipNode();
    clipRoot->setIsRectangular(true);
    clipRoot->appendChildNode(new QSGNode());
    root->appendChildNode(clipRoot);
    return clipRoot;
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
    QSGNode* staticRoot = ensureStaticRoot(root);
    QSGClipNode* dynamicClipRoot = ensureDynamicClipRoot(root);
    QSGNode* dynamicRoot = dynamicClipRoot != nullptr ? dynamicClipRoot->firstChild() : nullptr;

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

    if (appearanceChanged || root->dynamicRevision != state.overlayRevision) {
        if (dynamicClipRoot != nullptr) {
            dynamicClipRoot->setClipRect(QRectF(
                state.timelineLeft,
                state.timelineTop,
                qMax(0, state.viewportSize.width() - state.timelineLeft),
                state.timelineHeight));
        }
        clearChildren(dynamicRoot);
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
                    dynamicRoot->appendChildNode(node);
                }
            }
        }
        if (state.hasCursorLine) {
            dynamicRoot->appendChildNode(buildTimelineLineNode(state.cursorLine));
        }
        if (state.hasPlayheadLine) {
            dynamicRoot->appendChildNode(buildTimelineLineNode(state.playheadLine));
        }
        if (state.hasDragCenterLine) {
            dynamicRoot->appendChildNode(buildTimelineLineNode(state.dragCenterLine));
        }
        root->dynamicRevision = state.overlayRevision;
    }
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
