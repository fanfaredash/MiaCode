#pragma once

#include <utility>

#include <QString>
#include <QStringList>

// Output-device change detection for the preview transport.
//
// Why this exists: the preview clock is an anchor-plus-advance model, not a follower of
// the device position. When the output device set changes underneath a running session
// the anchor goes stale and stays stale, which is the desync users report
// (docs/audit/AUDIO_CLOCK_DESYNC_AUDIT_ZH.md §1). Three attempts to re-anchor silently
// while playback continued did not fix it. A pause/resume cycle always does, because
// pause anchors and resume re-starts from that fresh anchor — so the fix is to pause on
// the device event and let the user resume.
//
// This header holds only the decision: what counts as a change, and whether that change
// should stop playback. The QMediaDevices plumbing lives in PreviewAudioDeviceWatcher so
// this stays free of Qt Multimedia and its spec runs on a machine with no sound device.
namespace miacode::preview_audio::device_change {

struct OutputSnapshot {
    QStringList outputIds;      // normalised (sorted) output device ids
    QString defaultOutputId;    // empty when the system reports no default output
};

enum class Change {
    None,
    OutputListChanged,      // a device appeared or disappeared — 插拔设备
    DefaultOutputChanged,   // the same devices, different system output — 调整设备
    Both,                   // typically unplugging the device that was the default
};

// Sorts the id list so a bare re-enumeration in a different order does not read as a
// hotplug. Qt makes no ordering promise for QMediaDevices::audioOutputs(), and an
// order-sensitive comparison would pause the preview for nothing.
inline OutputSnapshot makeOutputSnapshot(QStringList outputIds, QString defaultOutputId)
{
    outputIds.sort();
    return OutputSnapshot{std::move(outputIds), std::move(defaultOutputId)};
}

inline Change compareSnapshots(const OutputSnapshot& before, const OutputSnapshot& after)
{
    const bool outputListChanged = before.outputIds != after.outputIds;
    const bool defaultOutputChanged = before.defaultOutputId != after.defaultOutputId;
    if (outputListChanged && defaultOutputChanged) {
        return Change::Both;
    }
    if (outputListChanged) {
        return Change::OutputListChanged;
    }
    if (defaultOutputChanged) {
        return Change::DefaultOutputChanged;
    }
    return Change::None;
}

// The `previewPlaying` term is what makes a debounce timer unnecessary. One physical
// hotplug makes Qt emit several notifications (device removed, then default reassigned);
// the first pauses, and every later one finds playback already stopped and does nothing.
// It also covers video export, which pauses the preview before it starts.
inline bool shouldPausePreview(Change change, bool previewPlaying)
{
    return previewPlaying && change != Change::None;
}

inline const char* changeName(Change change)
{
    switch (change) {
    case Change::OutputListChanged:
        return "output_list";
    case Change::DefaultOutputChanged:
        return "default_output";
    case Change::Both:
        return "output_list_and_default";
    case Change::None:
    default:
        return "none";
    }
}

}  // namespace miacode::preview_audio::device_change
