#pragma once

#include <QObject>

#include "PreviewAudioDeviceChangePolicy.h"
#include "PreviewAudioDeviceCutoff.h"

#include <functional>
#include <mutex>

class QMediaDevices;

// Turns Qt's output-enumeration notification into a "the output configuration really
// changed" signal. Qt re-notifies on re-enumeration and emits several times for one
// physical hotplug; PreviewAudioDeviceChangePolicy decides which of those are real, and
// this class holds the snapshot the policy compares against.
//
// Deliberately owns no playback state. The consumer (MainWindow::TimelineSection) decides
// what to do with the signal; see docs/superpowers/specs/
// 2026-08-06-preview-audio-device-autopause-design.md.
class PreviewAudioDeviceWatcher : public QObject
{
    Q_OBJECT

public:
    using Change = miacode::preview_audio::device_change::Change;
    using DeviceCutoff = miacode::preview_audio::PreviewAudioDeviceCutoff;
    // This callback is deliberately synchronous and must be safe to invoke on
    // Core Audio's MTA callback thread.  It only posts an audio-worker barrier;
    // GUI work remains on this QObject's thread through deviceCutoffRequested.
    using DirectCutoffHandler = std::function<DeviceCutoff(Change)>;

    explicit PreviewAudioDeviceWatcher(QObject* parent = nullptr);
    ~PreviewAudioDeviceWatcher() override;

    void setDirectCutoffHandler(DirectCutoffHandler handler);

    // Called by the Windows Core Audio endpoint callback. The callback itself may
    // run outside the Qt GUI thread; it first posts the audio cutoff barrier, then
    // queues only the GUI-freeze notification back to this QObject's thread.
    void handleNativeOutputChanged(Change change);

signals:
    void deviceCutoffRequested(const PreviewAudioDeviceWatcher::DeviceCutoff& cutoff);
    void outputConfigurationChanged(Change change);

private:
    // QMediaDevices is the portability fallback. On Windows it is created only when
    // Core Audio endpoint notification registration is unavailable: enumerating Qt's
    // output list synchronously on the GUI thread may block in AudioSes during a
    // hotplug, whereas IMMNotificationClient already tells us that the topology moved.
    void enableQtFallback();
    DeviceCutoff requestDirectCutoff(Change change);
    void deliverNativeOutputChange(const DeviceCutoff& cutoff);
    void handleAudioOutputsChanged();

    QMediaDevices* mediaDevices_ = nullptr;
    miacode::preview_audio::device_change::OutputSnapshot snapshot_;
    void* nativeEndpointNotificationClient_ = nullptr;
    bool nativeComInitialized_ = false;
    std::mutex directCutoffHandlerMutex_;
    DirectCutoffHandler directCutoffHandler_;
};
