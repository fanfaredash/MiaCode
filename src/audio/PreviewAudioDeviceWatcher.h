#pragma once

#include <QObject>

#include "PreviewAudioDeviceChangePolicy.h"

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

    explicit PreviewAudioDeviceWatcher(QObject* parent = nullptr);
    ~PreviewAudioDeviceWatcher() override;

    // Called by the Windows Core Audio endpoint callback. The callback itself may
    // run outside the Qt GUI thread; this method only queues the lightweight
    // notification back to this QObject's thread.
    void handleNativeDefaultOutputChanged();

signals:
    void outputConfigurationChanged(Change change);

private:
    void handleAudioOutputsChanged();

    QMediaDevices* mediaDevices_ = nullptr;
    miacode::preview_audio::device_change::OutputSnapshot snapshot_;
    void* nativeEndpointNotificationClient_ = nullptr;
    bool nativeComInitialized_ = false;
};
