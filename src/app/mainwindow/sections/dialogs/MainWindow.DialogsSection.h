#pragma once

#include "../../MainWindow.h"

#include <functional>

class MainWindow::DialogsSection {
public:
    DialogsSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    bool releasePreviewMediaForFileOperation();
    bool reloadPreviewMediaAfterFileOperation(bool reloadTrack);

    void onPreviewAudioSettings();
    void onPreviewVideoSettings();
    void onSkinSettings();
    void onAbout();
    void onReadTitleFromTrack();
    void onReadArtistFromTrack();
    void onExtractBackgroundFromTrack();

private:
    QString resolveCurrentChartDirectory() const;
    void openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title);
    // Small owner-wired 皮肤 popup (skin / judge line / HUD font). Shares state
    // with the export dialog's 皮肤 tab — both mutate owner_ live and refresh
    // the same surfaces.
    void openSkinSettingsDialog();

public:
    // Builds the owner_-wired Gameplay controls (judge effect / slide stack
    // order / center display) for injection into the export dialog's Gameplay
    // tab. Mutates owner_ + previewCanvas_ live and persists via
    // savePortableState(), exactly like the settings dialog — the two dialogs
    // stay independently wired.
    void buildExportInjectedSettings(
        QWidget* parent,
        QWidget** gameplayOut,
        std::function<void()>* refreshOut = nullptr);
    // Builds the owner_-wired 皮肤 panel content (skin / judge line / HUD font),
    // reused by openSkinSettingsDialog() and injected into the export dialog's
    // 皮肤 tab. `includeFolderButtons` toggles the folder actions (shown in the
    // popup, hidden in the export tab per its compact layout).
    void buildSkinSettings(
        QWidget* parent,
        QWidget** skinOut,
        bool includeFolderButtons,
        std::function<void()>* refreshOut = nullptr);

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
