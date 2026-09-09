#pragma once

#include "../../MainWindow.h"

#include <functional>

class MainWindow::DialogsSection {
public:
    DialogsSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void onPreviewAudioSettings();
    void onPreviewVideoSettings();
    void onSkinSettings();
    void onMediaProcessingTools();
    void onAbout();
    void onPrependTrackSilence();
    void onPrependPvBlack();
    void onCompressBackgroundVideo();
    void onBatchCompressPv();
    void onConvertTrackTo44100Hz();
    void onReadTitleFromTrack();
    void onReadArtistFromTrack();
    void onExtractBackgroundFromTrack();
    void onImportBackgroundImage();
    void onImportBackgroundVideo();
    void onDeleteBackgroundVideo();

private:
    enum class MediaBlankTarget {
        Track,
        Pv,
    };

    QString resolveLatencyDetectorTrackPath() const;
    QString resolveCurrentChartDirectory() const;
    void releasePreviewMediaForFileOperation();
    void reloadPreviewMediaAfterFileOperation(bool reloadTrack);
    void importBackgroundMedia(bool video);
    void onPrependMediaBlank(MediaBlankTarget target);
    // Shows an export-style "done" dialog naming the produced file, with an
    // "Open Folder" button that reveals its containing directory.
    void showMediaOperationCompleteDialog(
        const QString& title,
        const QString& summary,
        const QString& producedFilePath);
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
