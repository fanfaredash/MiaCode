#pragma once

#include <QList>
#include <QString>

namespace miacode::cover_export {

class CoverLayoutModel;

// Immutable export snapshot of the chart-frame times. Each layer keeps its own
// saved time; the shared renderer playhead is only a temporary transport cursor
// and must never overwrite another layer's frameSeconds.
class CoverFrameExportPlan final
{
public:
    struct Frame {
        QString key;
        double seconds = 0.0;
    };

    static CoverFrameExportPlan fromVisibleLayers(const CoverLayoutModel& model,
                                                  const QString& activeLayerKey,
                                                  double activeLayerSeconds);

    const QList<Frame>& frames() const { return frames_; }
    QString activeLayerKey() const { return activeLayerKey_; }
    double activeLayerSeconds() const { return activeLayerSeconds_; }

private:
    CoverFrameExportPlan(QList<Frame> frames,
                         QString activeLayerKey,
                         double activeLayerSeconds);

    QList<Frame> frames_;
    QString activeLayerKey_;
    double activeLayerSeconds_ = 0.0;
};

}  // namespace miacode::cover_export
