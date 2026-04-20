#pragma once

#include <QString>

#include "VideoExportAudioRenderPlan.h"

namespace miacode::video_export {

class VideoExportAudioBackend
{
public:
    virtual ~VideoExportAudioBackend() = default;

    virtual QString backendId() const = 0;
    virtual bool isSupported(QString* reason = nullptr) const = 0;
    virtual bool renderMixedTrackToWav(
        const VideoExportAudioRenderPlan& plan,
        const QString& outputPath,
        QString* errorMessage = nullptr) = 0;
};

}  // namespace miacode::video_export
