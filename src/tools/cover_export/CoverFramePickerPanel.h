#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;

namespace miacode::cover_export {

class CoverStudioPanel;

class CoverFramePickerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CoverFramePickerPanel(CoverStudioPanel* studio, QWidget* parent = nullptr);

private:
    void refresh();

    CoverStudioPanel* studio_ = nullptr;
    QPushButton* backButton_ = nullptr;
    QPushButton* forwardButton_ = nullptr;
    QSlider* slider_ = nullptr;
    QLabel* label_ = nullptr;
};

}  // namespace miacode::cover_export
