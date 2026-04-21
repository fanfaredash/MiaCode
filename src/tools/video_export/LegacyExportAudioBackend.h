#pragma once

#include "VideoExportAudioBackend.h"

namespace miacode::video_export {

class LegacyExportAudioBackend final : public VideoExportAudioBackend
{
public:
    QString backendId() const override;
    bool isSupported(QString* reason = nullptr) const override;
    bool renderMixedTrackToWav(
        const VideoExportAudioRenderPlan& plan,
        const QString& outputPath,
        QString* errorMessage = nullptr) override;
};

}  // namespace miacode::video_export
