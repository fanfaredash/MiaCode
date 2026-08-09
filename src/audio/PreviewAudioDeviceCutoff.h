#pragma once

#include "PreviewAudioDeviceChangePolicy.h"
#include "PreviewAudioWorkerProtocol.h"

#include <QMetaType>

namespace miacode::preview_audio {

// Captures one physical output-route change at the point Core Audio/Qt reports it.
// `cutoffSecond` is derived from the preview's monotonic wall-clock anchor on the
// notifying thread, before any queued GUI work can advance the displayed clock.
struct PreviewAudioDeviceCutoff {
    device_change::Change change = device_change::Change::None;
    CommandIdentity identity;
    WorkerPostResult post;
    double cutoffSecond = 0.0;
    qint64 eventMonotonicNs = 0;
    // Filled only once the notification reaches the QObject/GUI thread.  Keeping
    // this separate from the native event timestamp makes a blocked GUI loop visible
    // in logs without changing the captured audio cutoff second.
    qint64 guiDeliveryMonotonicNs = 0;
    qint64 emergencyPauseStartedNs = 0;
    qint64 emergencyPauseFinishedNs = 0;
    int emergencyPauseDeviceIndex = -1;
    int emergencyPauseError = 0;
    bool emergencyPauseAttempted = false;
    bool emergencyPauseSucceeded = false;
    bool armedPlaybackWasCut = false;
    // A real route change while playback is already paused must still release the
    // concrete output.  It deliberately has no cutoff second and must not freeze the
    // GUI again; the next explicit Play takes the cold Prepare path.
    bool outputRouteInvalidationOnly = false;
};

}  // namespace miacode::preview_audio

Q_DECLARE_METATYPE(miacode::preview_audio::PreviewAudioDeviceCutoff)
