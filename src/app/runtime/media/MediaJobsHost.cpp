#include "runtime/media/MediaJobsHost.h"

void Session::onPreviewAudioSettings()
{
    mediaJobs_->onPreviewAudioSettings();
}

void Session::onPreviewVideoSettings()
{
    mediaJobs_->onPreviewVideoSettings();
}

void Session::onSkinSettings()
{
    mediaJobs_->onSkinSettings();
}

void Session::onMediaProcessingTools()
{
    emit mediaToolsRequested();
}

void Session::onAbout()
{
    mediaJobs_->onAbout();
}

void miacode::runtime::MediaJobsHost::convertTrackTo44100Hz()
{
    onConvertTrackTo44100Hz();
}

void miacode::runtime::MediaJobsHost::compressBackgroundVideo()
{
    onCompressBackgroundVideo();
}

QVariantMap miacode::runtime::MediaJobsHost::mediaBlankContext(bool isTrack)
{
    return prependMediaBlankContext(
        isTrack ? MediaBlankTarget::Track : MediaBlankTarget::Pv);
}

QVariantMap miacode::runtime::MediaJobsHost::detectMediaBlankTiming(bool isTrack)
{
    return detectMediaBlankTiming(
        isTrack ? MediaBlankTarget::Track : MediaBlankTarget::Pv);
}

void miacode::runtime::MediaJobsHost::restoreMediaBlankBackup(bool isTrack)
{
    restoreMediaBlankBackup(
        isTrack ? MediaBlankTarget::Track : MediaBlankTarget::Pv);
}

void miacode::runtime::MediaJobsHost::applyMediaBlank(bool isTrack, double beats, double bpm)
{
    applyMediaBlank(
        isTrack ? MediaBlankTarget::Track : MediaBlankTarget::Pv, beats, bpm);
}


void Session::onReadTitleFromTrack()
{
    mediaJobs_->onReadTitleFromTrack();
}

void Session::onReadArtistFromTrack()
{
    mediaJobs_->onReadArtistFromTrack();
}

void Session::onExtractBackgroundFromTrack()
{
    mediaJobs_->onExtractBackgroundFromTrack();
}
