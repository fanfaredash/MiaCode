#include "timeline/quick/TimelineQuickNotesLayer.h"

#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGNode>
#include <QSGOpacityNode>
#include <QSGSimpleTextureNode>
#include <QTransform>

#include "timeline/quick/TimelineQuickLayerUtils.h"
#include "timeline/quick/TimelineQuickTextureCache.h"

namespace {

struct TimelineQuickNotesRootNode : public QSGNode {
    quint64 revision = 0;
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

void appendTextureNode(QSGNode* parent, QSGTexture* texture, const QRectF& rect)
{
    if (parent == nullptr || texture == nullptr || !rect.isValid()) {
        return;
    }
    auto* node = new QSGSimpleTextureNode();
    node->setTexture(texture);
    node->setRect(rect);
    parent->appendChildNode(node);
}

QSizeF rotatedSpriteLogicalSize(const QSize& baseSize, qreal rotationDegrees)
{
    if (!baseSize.isValid()) {
        return QSizeF();
    }
    const QRectF rect(0.0, 0.0, baseSize.width(), baseSize.height());
    if (qFuzzyIsNull(rotationDegrees)) {
        return rect.size();
    }
    return QTransform().rotate(rotationDegrees).mapRect(rect).size();
}

QRectF centeredSpriteRect(const QPointF& center, const QSizeF& size)
{
    if (!size.isValid()) {
        return QRectF();
    }
    return QRectF(
        center.x() - (size.width() / 2.0),
        center.y() - (size.height() / 2.0),
        size.width(),
        size.height());
}

void appendHoldSpanNode(
    QSGNode* root,
    const miacode::timeline::TimelineSceneHoldSpan& holdSpan,
    TimelineQuickTextureCache* textures)
{
    if (root == nullptr || textures == nullptr) {
        return;
    }

    const qreal holdScale = textures->holdScaleForBaseIconScale(holdSpan.spriteType, holdSpan.baseIconScale);
    const TimelineQuickHoldTextureParts parts = textures->holdTextureParts(holdSpan.spriteType, holdScale);
    if (parts.isValid()) {
        const qreal capY = holdSpan.rowTop + ((holdSpan.laneHeight - parts.leftCap.logicalSize.height()) / 2.0);
        const qreal leftCapX = holdSpan.startX - parts.leftCap.logicalSize.width();
        const qreal rightCapX = holdSpan.endX;
        const qreal bodyStartX = leftCapX + parts.leftCap.logicalSize.width();
        const qreal bodyWidth = qMax<qreal>(0.0, rightCapX - bodyStartX);
        if (bodyWidth > 0.0) {
            appendTextureNode(
                root,
                parts.bodySlice.texture,
                QRectF(bodyStartX, capY, bodyWidth, parts.bodySlice.logicalSize.height()));
        }
        appendTextureNode(
            root,
            parts.leftCap.texture,
            QRectF(leftCapX, capY, parts.leftCap.logicalSize.width(), parts.leftCap.logicalSize.height()));
        appendTextureNode(
            root,
            parts.rightCap.texture,
            QRectF(rightCapX, capY, parts.rightCap.logicalSize.width(), parts.rightCap.logicalSize.height()));
        return;
    }

    root->appendChildNode(buildTimelineLineNode(miacode::timeline::TimelineSceneLine{
        QPointF(holdSpan.startX, holdSpan.rowTop + (holdSpan.laneHeight / 2.0)),
        QPointF(holdSpan.endX, holdSpan.rowTop + (holdSpan.laneHeight / 2.0)),
        holdSpan.fallbackColor,
        holdSpan.fallbackWidth,
    }));
    const QSize targetSize = textures->noteTargetSize(holdSpan.spriteType, holdScale);
    if (!targetSize.isValid()) {
        return;
    }
    if (QSGTexture* texture = textures->noteTexture(holdSpan.spriteType, targetSize); texture != nullptr) {
        const qreal iconY = holdSpan.rowTop + ((holdSpan.laneHeight - targetSize.height()) / 2.0);
        appendTextureNode(
            root,
            texture,
            QRectF(
                holdSpan.startX - (targetSize.width() / 2.0),
                iconY,
                targetSize.width(),
                targetSize.height()));
        appendTextureNode(
            root,
            texture,
            QRectF(
                holdSpan.endX - (targetSize.width() / 2.0),
                iconY,
                targetSize.width(),
                targetSize.height()));
    }
}

}  // namespace

QSGNode* TimelineQuickNotesLayer::updateNode(
    QSGNode* oldNode,
    const miacode::timeline::TimelineSceneState& state,
    QQuickWindow* window,
    TimelineQuickTextureCache* textures) const
{
    Q_UNUSED(window);
    auto* root = dynamic_cast<TimelineQuickNotesRootNode*>(oldNode);
    if (root == nullptr) {
        delete oldNode;
        root = new TimelineQuickNotesRootNode();
    }
    if (root->revision == state.notesRevision && root->appearanceRevision == state.appearanceRevision) {
        return root;
    }
    clearChildren(root);
    auto* clipRoot = new QSGClipNode();
    clipRoot->setIsRectangular(true);
    clipRoot->setClipRect(QRectF(
        state.timelineLeft,
        state.timelineTop,
        qMax(0, state.viewportSize.width() - state.timelineLeft),
        state.timelineHeight));
    auto* bodyRoot = new QSGNode();
    clipRoot->appendChildNode(bodyRoot);
    root->appendChildNode(clipRoot);
    for (const auto& rect : state.fireworkBands) {
        auto* opacity = new QSGOpacityNode();
        opacity->setOpacity(0.55f);
        opacity->appendChildNode(buildTimelineRectNode(rect));
        bodyRoot->appendChildNode(opacity);
    }
    if (textures != nullptr) {
        for (const auto& sprite : state.trackSprites) {
            const QSize targetSize = textures->noteTargetSize(sprite.spriteType, sprite.scale);
            const QSizeF logicalSize = rotatedSpriteLogicalSize(targetSize, sprite.rotationDegrees);
            appendTextureNode(
                bodyRoot,
                textures->noteTexture(sprite.spriteType, targetSize, sprite.rotationDegrees, sprite.mirrorX),
                centeredSpriteRect(sprite.center, logicalSize));
        }
        for (const auto& holdSpan : state.holdSpans) {
            appendHoldSpanNode(bodyRoot, holdSpan, textures);
        }
    }
    for (const auto& line : state.touchHoldLines) {
        bodyRoot->appendChildNode(buildTimelineLineNode(line));
    }
    if (textures != nullptr) {
        for (const auto& sprite : state.noteSprites) {
            const QSize targetSize = textures->noteTargetSize(sprite.spriteType, sprite.scale);
            const QSizeF logicalSize = rotatedSpriteLogicalSize(targetSize, sprite.rotationDegrees);
            appendTextureNode(
                bodyRoot,
                textures->noteTexture(sprite.spriteType, targetSize, sprite.rotationDegrees, sprite.mirrorX),
                centeredSpriteRect(sprite.center, logicalSize));
        }
    }
    root->revision = state.notesRevision;
    root->appearanceRevision = state.appearanceRevision;
    return root;
}
