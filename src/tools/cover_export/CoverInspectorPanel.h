#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QSlider;

namespace miacode::cover_export {

class CoverLayer;
class CoverStudioPanel;

class CoverInspectorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CoverInspectorPanel(CoverStudioPanel* studio, QWidget* parent = nullptr);

private:
    void refresh();

    CoverStudioPanel* studio_ = nullptr;
    QLabel* nameLabel_ = nullptr;
    QCheckBox* visibleCheck_ = nullptr;
    QCheckBox* lockedCheck_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QSlider* sizeSlider_ = nullptr;
    QSlider* xSlider_ = nullptr;
    QSlider* ySlider_ = nullptr;
    QCheckBox* frameBgCheck_ = nullptr;
    QSlider* frameBgBrightnessSlider_ = nullptr;
};

}  // namespace miacode::cover_export
