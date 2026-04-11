#pragma once

#include "../../MainWindow.h"

class MainWindow::DialogsSection {
public:
    DialogsSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void updateLatencyDetectorAvailability();
    void onPreviewAudioSettings();
    void onPreviewVideoSettings();
    void onAbout();
    void onOpenLatencyDetector();

private:
    QString resolveLatencyDetectorTrackPath() const;
    void openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title);

    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
