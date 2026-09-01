#pragma once

#include "../../MainWindow.h"

class MainWindow::PreferencesSection {
public:
    PreferencesSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void showWelcomeDialog();
    void applyConfiguredShortcuts();

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
