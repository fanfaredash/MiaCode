#pragma once

#include "../../MainWindow.h"

class MainWindow::DialogsSection {
public:
    DialogsSection(MainWindow& owner, MainWindow::MainWindowUiRefs& ui, MainWindow::MainWindowState& state);

    void onPreviewAudioSettings();
    void onPreviewVideoSettings();
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
    void openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title);

    MainWindow& owner_;
    MainWindow::MainWindowUiRefs& ui_;
    MainWindow::MainWindowState& state_;
};
