#pragma once

#include "../../MainWindow.h"

class MainWindow::DialogsSection {
public:
    DialogsSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void onPreviewAudioSettings();
    void onPreviewVideoSettings();
    void onMediaProcessingTools();
    void onAbout();
    void onPrependTrackSilence();
    void onPrependPvBlack();
    void onCompressBackgroundVideo();
    void onConvertTrackTo44100Hz();
    void onReadTitleFromTrack();
    void onReadArtistFromTrack();
    void onExtractBackgroundFromTrack();

private:
    enum class MediaBlankTarget {
        Track,
        Pv,
    };

    QString resolveLatencyDetectorTrackPath() const;
    QString resolveCurrentChartDirectory() const;
    void releasePreviewMediaForFileOperation();
    void reloadPreviewMediaAfterFileOperation(bool reloadTrack);
    void onPrependMediaBlank(MediaBlankTarget target);
    // Shows an export-style "done" dialog naming the produced file, with an
    // "Open Folder" button that reveals its containing directory.
    void showMediaOperationCompleteDialog(
        const QString& title,
        const QString& summary,
        const QString& producedFilePath);
    void openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title);

public:
    // Builds the owner_-wired Gameplay controls (skin / judge line / judge
    // effect / slide stack order / center display) for injection into the
    // export dialog's Gameplay tab. Mutates owner_ + previewCanvas_ live and
    // persists via savePortableState(), exactly like the settings dialog — the
    // two dialogs stay independently wired.
    void buildExportInjectedSettings(QWidget* parent, QWidget** gameplayOut);

private:
    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
