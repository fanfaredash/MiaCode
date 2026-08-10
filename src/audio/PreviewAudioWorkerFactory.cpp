#include "PreviewAudioWorkerFactory.h"

#ifdef MIACODE_HAS_BASS_AUDIO
#include "BassPreviewAudioBackend.h"
#else
#include "MiniaudioPreviewAudioBackend.h"
#endif

namespace miacode::preview_audio {

PreviewAudioBackendFactory productionPreviewAudioBackendFactory()
{
    return []() -> std::unique_ptr<PreviewAudioBackend> {
#ifdef MIACODE_HAS_BASS_AUDIO
        return std::make_unique<BassPreviewAudioBackend>();
#else
        return std::make_unique<MiniaudioPreviewAudioBackend>();
#endif
    };
}

}  // namespace miacode::preview_audio
