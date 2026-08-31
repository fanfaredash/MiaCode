#include "MainWindow.DialogsSection.h"

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

// ---- miacode::v2::MediaToolsEngine ----
// The two menu-action names stay: BootstrapAndMenus connects QActions to them.
void MainWindow::convertTrackTo44100Hz()
{
    onConvertTrackTo44100Hz();
}

void MainWindow::compressBackgroundVideo()
{
    onCompressBackgroundVideo();
}

QVariantMap MainWindow::mediaBlankContext(bool isTrack)
{
    return prependMediaBlankContext(isTrack);
}

QVariantMap MainWindow::prependMediaBlankContext(bool isTrack)
{
    return dialogsSection_->prependMediaBlankContext(
        isTrack ? DialogsSection::MediaBlankTarget::Track
                : DialogsSection::MediaBlankTarget::Pv);
}

QVariantMap MainWindow::detectMediaBlankTiming(bool isTrack)
{
    return dialogsSection_->detectMediaBlankTiming(
        isTrack ? DialogsSection::MediaBlankTarget::Track
                : DialogsSection::MediaBlankTarget::Pv);
}

void MainWindow::restoreMediaBlankBackup(bool isTrack)
{
    dialogsSection_->restoreMediaBlankBackup(
        isTrack ? DialogsSection::MediaBlankTarget::Track
                : DialogsSection::MediaBlankTarget::Pv);
}

void MainWindow::applyMediaBlank(bool isTrack, double beats, double bpm)
{
    dialogsSection_->applyMediaBlank(
        isTrack ? DialogsSection::MediaBlankTarget::Track
                : DialogsSection::MediaBlankTarget::Pv,
        beats, bpm);
}

void MainWindow::onCompressBackgroundVideo()
{
    dialogsSection_->onCompressBackgroundVideo();
}

void MainWindow::onConvertTrackTo44100Hz()
{
    dialogsSection_->onConvertTrackTo44100Hz();
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
