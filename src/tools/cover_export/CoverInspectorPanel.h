#pragma once

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QEvent;
class QGroupBox;
class QLabel;
class QPushButton;
class QSlider;

namespace miacode::ui {
class EditableValueLabel;
}

namespace miacode::cover_export {

class CoverLayer;
class CoverStudioPanel;

// §3.2 + §3.3-frame of the inspector column. Hosts the selected layer's COMMON
// properties (show / lock / opacity / size / position, each with a click-to-type
// value) and — only while a chart frame is selected — the chart-frame-specific
// options (inner background / brightness / frame-time readout). The 画板 (§3.1)
// and 难度卡选项 (§3.3-card) groups are panel-owned and placed around this widget
// by CoverStudioWindow.
class CoverInspectorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CoverInspectorPanel(CoverStudioPanel* studio, QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    // §4.2 — (re)connect to the active layer's geometry/opacity signals so a canvas
    // drag/scale updates the sliders live; QPointer-guarded against layer deletion.
    void bindLayerSignals(CoverLayer* layer);

    CoverStudioPanel* studio_ = nullptr;
    QPointer<CoverLayer> boundLayer_;

    // §3.2 — common layer properties.
    QGroupBox* layerGroup_ = nullptr;
    QCheckBox* visibleCheck_ = nullptr;
    QCheckBox* lockedCheck_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QSlider* sizeSlider_ = nullptr;
    QSlider* xSlider_ = nullptr;
    QSlider* ySlider_ = nullptr;
    miacode::ui::EditableValueLabel* opacityValue_ = nullptr;
    miacode::ui::EditableValueLabel* sizeValue_ = nullptr;
    miacode::ui::EditableValueLabel* xValue_ = nullptr;
    miacode::ui::EditableValueLabel* yValue_ = nullptr;

    // §3.3-frame — chart-frame-specific options (shown only for chart frames).
    QGroupBox* frameOptionsGroup_ = nullptr;
    QCheckBox* frameBgCheck_ = nullptr;
    QSlider* frameBgBrightnessSlider_ = nullptr;
    miacode::ui::EditableValueLabel* frameBgBrightnessValue_ = nullptr;
    QPushButton* framePlayButton_ = nullptr;
    QSlider* frameTimeSlider_ = nullptr;
    QLabel* frameTimeReadout_ = nullptr;
};

}  // namespace miacode::cover_export
