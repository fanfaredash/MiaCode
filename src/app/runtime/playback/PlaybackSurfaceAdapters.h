#pragma once

#include "app/v2/PreviewSurface.h"
#include "app/v2/TimelineSurface.h"

namespace miacode::runtime {

class PlaybackCoordinator;

// These adapters keep the legacy ApplicationServices surface contracts stable
// while the coordinator itself implements only playback contracts. They carry
// no transport, timeline, render, or document state of their own.
class PlaybackPreviewSurfaceAdapter final : public miacode::v2::PreviewSurface
{
public:
    explicit PlaybackPreviewSurfaceAdapter(PlaybackCoordinator& coordinator);

    void invalidateSession();

    bool playing() const override;
    miacode::v2::PlaybackTransportState playbackTransportState() const override;
    double positionSeconds() const override;
    double durationSeconds() const override;
    double lowerBoundSeconds() const override;
    double playbackRate() const override;
    QString playbackRateLabel() const override;
    QObject* previewRuntimeObject() const override;
    QObject* stageMediaHostObject() const override;
    double canvasAspectRatio() const override;
    QStringList statsTexts() const override;
    RenderMode muriRenderMode() const override;
    void setMuriRenderMode(RenderMode mode) override;
    void toggleMuriRenderMode() override;
    QStringList availableSkinDirectoryNames() const override;
    QString skinDisplayName(const QString& directoryName) const override;
    QString resolveSkinDir() const override;
    QString resolveSkinRootDir() const override;
    QString resolveCustomOutlineDir() const override;
    void applyOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection,
                             bool persistState) override;
    QVariantMap renderSettings() const override;
    void setRenderSetting(const QString& key, const QVariant& value) override;
    void refreshSurfaces() override;
    void applySfxLevels() override;
    void prepareForShutdown() override;
    PreviewAudioSettings audioSettings() const override;
    void applyAudioSettings(const PreviewAudioSettings& settings) override;
    void saveAudioSettingsAsSoftwareDefault() override;
    void restoreAudioSettingsFromSoftwareDefault() override;

private:
    PlaybackCoordinator* coordinator_ = nullptr;
};

class PlaybackTimelineSurfaceAdapter final : public miacode::v2::TimelineSurface
{
public:
    explicit PlaybackTimelineSurfaceAdapter(PlaybackCoordinator& coordinator);

    void invalidateSession();

    QObject* timelineStateBridge() const override;
    void noteTimelineSurfaceReady() override;
    void navigateToSecond(double second) override;
    void centerOnSecond(double second) override;
    void wheelNavigateToSecond(double second) override;
    void timelineDragStarted() override;
    void timelineDragFinished(double second) override;
    void timelineUserInteractionStarted() override;
    void setFollowPreviewEnabled(bool enabled) override;
    QString bottomTabsCurrentTabId() const override;
    void setBottomTabsCurrentTabId(const QString& tabId) override;
    bool bottomTabsVisible() const override;
    bool timelineTabVisible() const override;
    bool muriTabVisible() const override;
    bool validationTabVisible() const override;
    bool ignoreMuriIssuePrompts() const override;

private:
    PlaybackCoordinator* coordinator_ = nullptr;
};

}  // namespace miacode::runtime
