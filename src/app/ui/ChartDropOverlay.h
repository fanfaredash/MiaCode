#pragma once

#include <QWidget>

class QWindow;

class ChartDropOverlay final : public QWidget {
public:
    ChartDropOverlay();
    void showForWindow(QWindow* target);
    void hideOverlay();
    void clearTransientParent();

protected:
    void paintEvent(QPaintEvent* event) override;
};
