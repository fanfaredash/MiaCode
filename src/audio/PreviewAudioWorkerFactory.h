#pragma once

#include "PreviewAudioBackend.h"

#include <functional>
#include <memory>

namespace miacode::preview_audio {

using PreviewAudioBackendFactory = std::function<std::unique_ptr<PreviewAudioBackend>()>;

PreviewAudioBackendFactory productionPreviewAudioBackendFactory();

}  // namespace miacode::preview_audio
