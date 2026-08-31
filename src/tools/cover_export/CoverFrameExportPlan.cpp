#include "tools/cover_export/CoverFrameExportPlan.h"

#include "tools/cover_export/CoverLayoutModel.h"

#include <QtGlobal>

#include <utility>

namespace miacode::cover_export {

CoverFrameExportPlan::CoverFrameExportPlan(QList<Frame> frames,
                                           QString activeLayerKey,
                                           double activeLayerSeconds)
    : frames_(std::move(frames))
    , activeLayerKey_(std::move(activeLayerKey))
    , activeLayerSeconds_(qMax(0.0, activeLayerSeconds))
{
}

CoverFrameExportPlan CoverFrameExportPlan::fromVisibleLayers(
    const CoverLayoutModel& model,
    const QString& activeLayerKey,
    double activeLayerSeconds)
{
    QList<Frame> frames;
    for (const auto* layer : model.visibleChartFrameLayers()) {
        if (layer == nullptr) {
            continue;
        }
        frames.append(Frame{layer->key(), qMax(0.0, layer->frameSeconds())});
    }
    return CoverFrameExportPlan(std::move(frames), activeLayerKey, activeLayerSeconds);
}

}  // namespace miacode::cover_export
