#pragma once

#include "VideoExportAudioBackend.h"

namespace miacode::video_export {

class BassExportAudioBackend final : public VideoExportAudioBackend
{
public:
    BassExportAudioBackend();
    ~BassExportAudioBackend() override;

    QString backendId() const override;
    bool isSupported(QString* reason = nullptr) const override;
    bool renderMixedTrackToWav(
        const VideoExportAudioRenderPlan& plan,
        const QString& outputPath,
        QString* errorMessage = nullptr) override;

private:
    bool runtimeLibrariesPresent() const;
    bool initializeBass(QString* errorMessage);
    void shutdownBass();

    bool ownsBassInit_ = false;
};

}  // namespace miacode::video_export
