#pragma once

#include <QWidget>

class QWindow;

class ChartDropOverlay final : public QWidget {
public:
    enum class Mode {
        CreateFromAudio,
        OpenChart,
        InvalidSelection,
    };

    ChartDropOverlay();
    void setMode(Mode mode);
    void showForWindow(QWindow* target);
    void hideOverlay();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Mode mode_ = Mode::CreateFromAudio;
};
