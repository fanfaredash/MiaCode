#pragma once

#include "app/services/AudioClockSource.h"
#include "app/services/PreviewPlaybackPort.h"
#include "app/services/PreviewSurface.h"

namespace miacode::runtime {

// Transitional Preview projection host. Render/settings/media operations are
// forwarded to the legacy composite implementation, while transport and
// canonical time are read and written only through PreviewPlaybackPort and
// AudioClockSource. It owns no playback engine or second playhead.
class PreviewHost final : public miacode::PreviewSurface
{
public:
    PreviewHost(miacode::PreviewSurface& legacySurface,
                miacode::PreviewPlaybackPort& playbackPort,
                miacode::AudioClockSource& audioClockSource);

    void invalidateSession();
    miacode::PlaybackSnapshot playbackSnapshot() const;
    double currentAudioClockSecond() const;

    bool playing() const override;
    miacode::PlaybackTransportState playbackTransportState() const override;
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
    int muriHandRadiusPx() const override;
    void setMuriHandRadiusPx(int radiusPx) override;
    int muriTapOnSlideThresholdMs() const override;
    void setMuriTapOnSlideThresholdMs(int thresholdMs) override;
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
    void prepareForMediaFileOperation() override;
    void refreshMediaAfterFileOperation() override;
    void applySfxLevels() override;
    void prepareForShutdown() override;
    PreviewAudioSettings audioSettings() const override;
    void applyAudioSettings(const PreviewAudioSettings& settings) override;
    void saveAudioSettingsAsSoftwareDefault() override;
    void restoreAudioSettingsFromSoftwareDefault() override;

private:
    miacode::PreviewSurface* legacySurface_ = nullptr;
    miacode::PreviewPlaybackPort* playbackPort_ = nullptr;
    miacode::AudioClockSource* audioClockSource_ = nullptr;
};

}  // namespace miacode::runtime
