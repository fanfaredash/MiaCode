#include "MainWindow.DialogsSection.h"
#include "app/v2/MediaToolsService.h"

void MainWindow::onPreviewAudioSettings()
{
    dialogsSection_->onPreviewAudioSettings();
}

void MainWindow::onPreviewVideoSettings()
{
    dialogsSection_->onPreviewVideoSettings();
}

void MainWindow::onSkinSettings()
{
    dialogsSection_->onSkinSettings();
}

void MainWindow::onMediaProcessingTools()
{
    emit mediaToolsRequested();
}

void MainWindow::onAbout()
{
    dialogsSection_->onAbout();
}

void MainWindow::onCompressBackgroundVideo()
{
    auto* const engine = applicationServices_.mediaToolsEngine();
    if (engine != nullptr) {
        engine->compressBackgroundVideo();
    }
}

void MainWindow::onConvertTrackTo44100Hz()
{
    auto* const engine = applicationServices_.mediaToolsEngine();
    if (engine != nullptr) {
        engine->convertTrackTo44100Hz();
    }
}


void MainWindow::onReadTitleFromTrack()
{
    dialogsSection_->onReadTitleFromTrack();
}

void MainWindow::onReadArtistFromTrack()
{
    dialogsSection_->onReadArtistFromTrack();
}

void MainWindow::onExtractBackgroundFromTrack()
{
    dialogsSection_->onExtractBackgroundFromTrack();
}
