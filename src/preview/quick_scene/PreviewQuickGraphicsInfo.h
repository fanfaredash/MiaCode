#pragma once

#include <QString>

class QQuickWindow;

namespace miacode::preview::quick_scene {

struct QuickGraphicsInfo {
    QString apiName;
    QString deviceName;
    QString logFields;
    bool hardwareAccelerated = false;
    bool deviceIdentified = false;
};

// Query the graphics device owned by this Quick window. Call from the render
// thread after scene-graph initialization so native RHI resources are valid.
QuickGraphicsInfo queryQuickGraphicsInfo(QQuickWindow* window);

}  // namespace miacode::preview::quick_scene
