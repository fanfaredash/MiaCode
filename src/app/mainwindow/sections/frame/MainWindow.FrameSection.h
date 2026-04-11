#pragma once

#include "../../MainWindow.h"

class MainWindow::FrameSection {
public:
    FrameSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void setupMenusAndActions(QMenu* fileMenu, QMenu* editMenu, QMenu* transformMenu, QMenu* previewMenu, QMenu* helpMenu);

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
