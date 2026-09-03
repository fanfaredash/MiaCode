#pragma once

#include "core/scene/PreviewFrameState.h"

#include <QtGlobal>
#include <QColor>

class QImage;
class QQuickWindow;
class QSGNode;
class PreviewTextureRepository;

class PreviewQuickStageBackgroundLayer
{
public:
    QSGNode* updateNode(
        QSGNode* oldNode,
        const miacode::preview::scene::PreviewFrameState& state,
        const QSize& renderSize,
        QQuickWindow* window,
        PreviewTextureRepository* textures,
        const QColor& backgroundColor = QColor(QStringLiteral("#191A1B"))
    ) const;

private:
    mutable quint64 lastDynamicStageMediaKey_ = 0;
    mutable quint64 lastProfiledResolvedStageMediaSerial_ = 0;
};
