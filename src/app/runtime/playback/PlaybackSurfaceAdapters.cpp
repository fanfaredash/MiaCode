#include "PlaybackSurfaceAdapters.h"

#include "runtime/playback/PlaybackCoordinator.h"

namespace miacode::runtime {

PlaybackPreviewSurfaceAdapter::PlaybackPreviewSurfaceAdapter(PlaybackCoordinator& coordinator)
    : coordinator_(&coordinator)
{
}

void PlaybackPreviewSurfaceAdapter::invalidateSession()
{
    coordinator_ = nullptr;
}

bool PlaybackPreviewSurfaceAdapter::playing() const
{
    return coordinator_ != nullptr && coordinator_->playing();
}

miacode::v2::PlaybackTransportState PlaybackPreviewSurfaceAdapter::playbackTransportState() const
{
    return coordinator_ != nullptr
        ? coordinator_->playbackTransportState()
        : miacode::v2::PlaybackTransportState::Stopped;
}

double PlaybackPreviewSurfaceAdapter::positionSeconds() const
{
    return coordinator_ != nullptr ? coordinator_->positionSeconds() : 0.0;
}

double PlaybackPreviewSurfaceAdapter::durationSeconds() const
{
    return coordinator_ != nullptr ? coordinator_->durationSeconds() : 0.0;
}

double PlaybackPreviewSurfaceAdapter::lowerBoundSeconds() const
{
    return coordinator_ != nullptr ? coordinator_->lowerBoundSeconds() : 0.0;
}

void PlaybackPreviewSurfaceAdapter::togglePlayback()
{
    if (coordinator_ != nullptr) {
        coordinator_->togglePlayback();
    }
}

void PlaybackPreviewSurfaceAdapter::stop()
{
    if (coordinator_ != nullptr) {
        coordinator_->stop();
    }
}

void PlaybackPreviewSurfaceAdapter::seek(double second)
{
    if (coordinator_ != nullptr) {
        coordinator_->seek(second);
    }
}

void PlaybackPreviewSurfaceAdapter::beginScrub()
{
    if (coordinator_ != nullptr) {
        coordinator_->beginScrub();
    }
}

void PlaybackPreviewSurfaceAdapter::updateScrub(double second, bool centerView)
{
    if (coordinator_ != nullptr) {
        coordinator_->updateScrub(second, centerView);
    }
}

void PlaybackPreviewSurfaceAdapter::endScrub(double second, bool centerView)
{
    if (coordinator_ != nullptr) {
        coordinator_->endScrub(second, centerView);
    }
}

double PlaybackPreviewSurfaceAdapter::playbackRate() const
{
    return coordinator_ != nullptr ? coordinator_->playbackRate() : 1.0;
}

void PlaybackPreviewSurfaceAdapter::setPlaybackRate(double rate)
{
    if (coordinator_ != nullptr) {
        coordinator_->setPlaybackRate(rate);
    }
}

void PlaybackPreviewSurfaceAdapter::nudgePlaybackRate(int direction)
{
    if (coordinator_ != nullptr) {
        coordinator_->nudgePlaybackRate(direction);
    }
}

QString PlaybackPreviewSurfaceAdapter::playbackRateLabel() const
{
    return coordinator_ != nullptr ? coordinator_->playbackRateLabel() : QString();
}

QObject* PlaybackPreviewSurfaceAdapter::previewRuntimeObject() const
{
    return coordinator_ != nullptr ? coordinator_->previewRuntimeObject() : nullptr;
}

QObject* PlaybackPreviewSurfaceAdapter::stageMediaHostObject() const
{
    return coordinator_ != nullptr ? coordinator_->stageMediaHostObject() : nullptr;
}

double PlaybackPreviewSurfaceAdapter::canvasAspectRatio() const
{
    return coordinator_ != nullptr ? coordinator_->canvasAspectRatio() : 1.0;
}

QStringList PlaybackPreviewSurfaceAdapter::statsTexts() const
{
    return coordinator_ != nullptr ? coordinator_->statsTexts() : QStringList();
}

RenderMode PlaybackPreviewSurfaceAdapter::muriRenderMode() const
{
    return coordinator_ != nullptr ? coordinator_->muriRenderMode() : RenderMode::Native;
}

void PlaybackPreviewSurfaceAdapter::setMuriRenderMode(RenderMode mode)
{
    if (coordinator_ != nullptr) {
        coordinator_->setMuriRenderMode(mode);
    }
}

void PlaybackPreviewSurfaceAdapter::toggleMuriRenderMode()
{
    if (coordinator_ != nullptr) {
        coordinator_->toggleMuriRenderMode();
    }
}

QStringList PlaybackPreviewSurfaceAdapter::availableSkinDirectoryNames() const
{
    return coordinator_ != nullptr ? coordinator_->availableSkinDirectoryNames() : QStringList();
}

QString PlaybackPreviewSurfaceAdapter::skinDisplayName(const QString& directoryName) const
{
    return coordinator_ != nullptr ? coordinator_->skinDisplayName(directoryName) : QString();
}

QString PlaybackPreviewSurfaceAdapter::resolveSkinDir() const
{
    return coordinator_ != nullptr ? coordinator_->resolveSkinDir() : QString();
}

QString PlaybackPreviewSurfaceAdapter::resolveSkinRootDir() const
{
    return coordinator_ != nullptr ? coordinator_->resolveSkinRootDir() : QString();
}

QString PlaybackPreviewSurfaceAdapter::resolveCustomOutlineDir() const
{
    return coordinator_ != nullptr ? coordinator_->resolveCustomOutlineDir() : QString();
}

void PlaybackPreviewSurfaceAdapter::applyOutlineVariant(
    PreviewOutlineVariant variant, bool useAutoSelection, bool persistState)
{
    if (coordinator_ != nullptr) {
        coordinator_->applyOutlineVariant(variant, useAutoSelection, persistState);
    }
}

QVariantMap PlaybackPreviewSurfaceAdapter::renderSettings() const
{
    return coordinator_ != nullptr ? coordinator_->renderSettings() : QVariantMap();
}

void PlaybackPreviewSurfaceAdapter::setRenderSetting(const QString& key, const QVariant& value)
{
    if (coordinator_ != nullptr) {
        coordinator_->setRenderSetting(key, value);
    }
}

void PlaybackPreviewSurfaceAdapter::refreshSurfaces()
{
    if (coordinator_ != nullptr) {
        coordinator_->refreshSurfaces();
    }
}

void PlaybackPreviewSurfaceAdapter::applySfxLevels()
{
    if (coordinator_ != nullptr) {
        coordinator_->applySfxLevels();
    }
}

void PlaybackPreviewSurfaceAdapter::prepareForShutdown()
{
    if (coordinator_ != nullptr) {
        coordinator_->prepareForShutdown();
    }
}

PreviewAudioSettings PlaybackPreviewSurfaceAdapter::audioSettings() const
{
    return coordinator_ != nullptr ? coordinator_->audioSettings() : PreviewAudioSettings();
}

void PlaybackPreviewSurfaceAdapter::applyAudioSettings(const PreviewAudioSettings& settings)
{
    if (coordinator_ != nullptr) {
        coordinator_->applyAudioSettings(settings);
    }
}

void PlaybackPreviewSurfaceAdapter::saveAudioSettingsAsSoftwareDefault()
{
    if (coordinator_ != nullptr) {
        coordinator_->saveAudioSettingsAsSoftwareDefault();
    }
}

void PlaybackPreviewSurfaceAdapter::restoreAudioSettingsFromSoftwareDefault()
{
    if (coordinator_ != nullptr) {
        coordinator_->restoreAudioSettingsFromSoftwareDefault();
    }
}

PlaybackTimelineSurfaceAdapter::PlaybackTimelineSurfaceAdapter(PlaybackCoordinator& coordinator)
    : coordinator_(&coordinator)
{
}

void PlaybackTimelineSurfaceAdapter::invalidateSession()
{
    coordinator_ = nullptr;
}

QObject* PlaybackTimelineSurfaceAdapter::timelineStateBridge() const
{
    return coordinator_ != nullptr ? coordinator_->timelineStateBridge() : nullptr;
}

void PlaybackTimelineSurfaceAdapter::noteTimelineSurfaceReady()
{
    if (coordinator_ != nullptr) {
        coordinator_->noteTimelineSurfaceReady();
    }
}

void PlaybackTimelineSurfaceAdapter::navigateToSecond(double second)
{
    if (coordinator_ != nullptr) {
        coordinator_->navigateToSecond(second);
    }
}

void PlaybackTimelineSurfaceAdapter::centerOnSecond(double second)
{
    if (coordinator_ != nullptr) {
        coordinator_->centerOnSecond(second);
    }
}

void PlaybackTimelineSurfaceAdapter::wheelNavigateToSecond(double second)
{
    if (coordinator_ != nullptr) {
        coordinator_->wheelNavigateToSecond(second);
    }
}

void PlaybackTimelineSurfaceAdapter::timelineDragStarted()
{
    if (coordinator_ != nullptr) {
        coordinator_->timelineDragStarted();
    }
}

void PlaybackTimelineSurfaceAdapter::timelineDragFinished(double second)
{
    if (coordinator_ != nullptr) {
        coordinator_->timelineDragFinished(second);
    }
}

void PlaybackTimelineSurfaceAdapter::timelineUserInteractionStarted()
{
    if (coordinator_ != nullptr) {
        coordinator_->timelineUserInteractionStarted();
    }
}

void PlaybackTimelineSurfaceAdapter::setFollowPreviewEnabled(bool enabled)
{
    if (coordinator_ != nullptr) {
        coordinator_->setFollowPreviewEnabled(enabled);
    }
}

QString PlaybackTimelineSurfaceAdapter::bottomTabsCurrentTabId() const
{
    return coordinator_ != nullptr ? coordinator_->bottomTabsCurrentTabId() : QString();
}

void PlaybackTimelineSurfaceAdapter::setBottomTabsCurrentTabId(const QString& tabId)
{
    if (coordinator_ != nullptr) {
        coordinator_->setBottomTabsCurrentTabId(tabId);
    }
}

bool PlaybackTimelineSurfaceAdapter::bottomTabsVisible() const
{
    return coordinator_ != nullptr && coordinator_->bottomTabsVisible();
}

bool PlaybackTimelineSurfaceAdapter::timelineTabVisible() const
{
    return coordinator_ != nullptr && coordinator_->timelineTabVisible();
}

bool PlaybackTimelineSurfaceAdapter::muriTabVisible() const
{
    return coordinator_ != nullptr && coordinator_->muriTabVisible();
}

bool PlaybackTimelineSurfaceAdapter::validationTabVisible() const
{
    return coordinator_ != nullptr && coordinator_->validationTabVisible();
}

bool PlaybackTimelineSurfaceAdapter::ignoreMuriIssuePrompts() const
{
    return coordinator_ != nullptr && coordinator_->ignoreMuriIssuePrompts();
}

}  // namespace miacode::runtime
