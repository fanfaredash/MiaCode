#pragma once

#include "../../MainWindow.h"

class MainWindow::PreferencesSection {
public:
    PreferencesSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void onPreferences();
    // First-run onboarding: pick preview-pane side + light/dark theme with
    // live preview. Re-triggerable via the `--welcome` CLI flag. See
    // MainWindow.WelcomeDialog.cpp.
    void showWelcomeDialog();
    void applyConfiguredShortcuts();

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
