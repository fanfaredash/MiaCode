#include "PreviewHost.h"

namespace miacode::runtime {

PreviewHost::PreviewHost(miacode::v2::PreviewSurface& legacySurface,
                         miacode::v2::PreviewPlaybackPort& playbackPort,
                         miacode::v2::AudioClockSource& audioClockSource)
    : legacySurface_(&legacySurface)
    , playbackPort_(&playbackPort)
    , audioClockSource_(&audioClockSource)
{
}

void PreviewHost::invalidateSession()
{
    legacySurface_ = nullptr;
    playbackPort_ = nullptr;
    audioClockSource_ = nullptr;
}

miacode::v2::PlaybackSnapshot PreviewHost::playbackSnapshot() const
{
    return playbackPort_ != nullptr ? playbackPort_->playbackSnapshot()
                                    : miacode::v2::PlaybackSnapshot{};
}

double PreviewHost::currentAudioClockSecond() const
{
    return audioClockSource_ != nullptr ? audioClockSource_->currentAudioClockSecond() : 0.0;
}

bool PreviewHost::playing() const
{
    return playbackTransportState() == miacode::v2::PlaybackTransportState::Playing;
}

miacode::v2::PlaybackTransportState PreviewHost::playbackTransportState() const
{
    return playbackSnapshot().transportState;
}

double PreviewHost::positionSeconds() const
{
    return currentAudioClockSecond();
}

double PreviewHost::durationSeconds() const
{
    return playbackSnapshot().durationSeconds;
}

double PreviewHost::lowerBoundSeconds() const
{
    return playbackSnapshot().lowerBoundSeconds;
}

void PreviewHost::togglePlayback()
{
    if (playbackPort_ != nullptr) {
        playbackPort_->togglePlayback();
    }
}

void PreviewHost::stop()
{
    if (playbackPort_ != nullptr) {
        playbackPort_->stop();
    }
}

void PreviewHost::seek(double second)
{
    if (playbackPort_ != nullptr) {
        playbackPort_->seek(second);
    }
}

void PreviewHost::beginScrub()
{
    if (playbackPort_ != nullptr) {
        playbackPort_->beginScrub();
    }
}

void PreviewHost::updateScrub(double second, bool)
{
    if (playbackPort_ != nullptr) {
        playbackPort_->updateScrub(second);
    }
}

void PreviewHost::endScrub(double second, bool)
{
    if (playbackPort_ != nullptr) {
        playbackPort_->endScrub(second);
    }
}

double PreviewHost::playbackRate() const
{
    return playbackSnapshot().playbackRate;
}

void PreviewHost::setPlaybackRate(double rate)
{
    if (playbackPort_ != nullptr) {
        playbackPort_->setPlaybackRate(rate);
    }
}

void PreviewHost::nudgePlaybackRate(int direction)
{
    if (playbackPort_ != nullptr) {
        playbackPort_->nudgePlaybackRate(direction);
    }
}

QString PreviewHost::playbackRateLabel() const
{
    return legacySurface_ != nullptr ? legacySurface_->playbackRateLabel() : QString();
}

QObject* PreviewHost::previewRuntimeObject() const
{
    return legacySurface_ != nullptr ? legacySurface_->previewRuntimeObject() : nullptr;
}

QObject* PreviewHost::stageMediaHostObject() const
{
    return legacySurface_ != nullptr ? legacySurface_->stageMediaHostObject() : nullptr;
}

double PreviewHost::canvasAspectRatio() const
{
    return legacySurface_ != nullptr ? legacySurface_->canvasAspectRatio() : 1.0;
}

QStringList PreviewHost::statsTexts() const
{
    return legacySurface_ != nullptr ? legacySurface_->statsTexts() : QStringList();
}

RenderMode PreviewHost::muriRenderMode() const
{
    return legacySurface_ != nullptr ? legacySurface_->muriRenderMode() : RenderMode::Native;
}

void PreviewHost::setMuriRenderMode(RenderMode mode)
{
    if (legacySurface_ != nullptr) {
        legacySurface_->setMuriRenderMode(mode);
    }
}

void PreviewHost::toggleMuriRenderMode()
{
    if (legacySurface_ != nullptr) {
        legacySurface_->toggleMuriRenderMode();
    }
}

QStringList PreviewHost::availableSkinDirectoryNames() const
{
    return legacySurface_ != nullptr ? legacySurface_->availableSkinDirectoryNames() : QStringList();
}

QString PreviewHost::skinDisplayName(const QString& directoryName) const
{
    return legacySurface_ != nullptr ? legacySurface_->skinDisplayName(directoryName) : QString();
}

QString PreviewHost::resolveSkinDir() const
{
    return legacySurface_ != nullptr ? legacySurface_->resolveSkinDir() : QString();
}

QString PreviewHost::resolveSkinRootDir() const
{
    return legacySurface_ != nullptr ? legacySurface_->resolveSkinRootDir() : QString();
}

QString PreviewHost::resolveCustomOutlineDir() const
{
    return legacySurface_ != nullptr ? legacySurface_->resolveCustomOutlineDir() : QString();
}

void PreviewHost::applyOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection,
                                       bool persistState)
{
    if (legacySurface_ != nullptr) {
        legacySurface_->applyOutlineVariant(variant, useAutoSelection, persistState);
    }
}

QVariantMap PreviewHost::renderSettings() const
{
    return legacySurface_ != nullptr ? legacySurface_->renderSettings() : QVariantMap();
}

void PreviewHost::setRenderSetting(const QString& key, const QVariant& value)
{
    if (legacySurface_ != nullptr) {
        legacySurface_->setRenderSetting(key, value);
    }
}

void PreviewHost::refreshSurfaces()
{
    if (legacySurface_ != nullptr) {
        legacySurface_->refreshSurfaces();
    }
}

void PreviewHost::applySfxLevels()
{
    if (legacySurface_ != nullptr) {
        legacySurface_->applySfxLevels();
    }
}

void PreviewHost::prepareForShutdown()
{
    if (legacySurface_ != nullptr) {
        legacySurface_->prepareForShutdown();
    }
}

PreviewAudioSettings PreviewHost::audioSettings() const
{
    return legacySurface_ != nullptr ? legacySurface_->audioSettings() : PreviewAudioSettings();
}

void PreviewHost::applyAudioSettings(const PreviewAudioSettings& settings)
{
    if (legacySurface_ != nullptr) {
        legacySurface_->applyAudioSettings(settings);
    }
}

void PreviewHost::saveAudioSettingsAsSoftwareDefault()
{
    if (legacySurface_ != nullptr) {
        legacySurface_->saveAudioSettingsAsSoftwareDefault();
    }
}

void PreviewHost::restoreAudioSettingsFromSoftwareDefault()
{
    if (legacySurface_ != nullptr) {
        legacySurface_->restoreAudioSettingsFromSoftwareDefault();
    }
}

}  // namespace miacode::runtime
