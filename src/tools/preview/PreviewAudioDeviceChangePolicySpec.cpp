#include <QString>
#include <QStringList>
#include <QTextStream>

#include "audio/PreviewAudioDeviceChangePolicy.h"
#include "audio/PreviewAudioPlaybackFlowPolicy.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

}  // namespace

int main()
{
    using miacode::preview_audio::device_change::Change;
    using miacode::preview_audio::device_change::changeName;
    using miacode::preview_audio::device_change::compareSnapshots;
    using miacode::preview_audio::device_change::makeOutputSnapshot;
    using miacode::preview_audio::device_change::shouldPausePreview;

    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    const auto speakers = makeOutputSnapshot({QStringLiteral("speakers"), QStringLiteral("hdmi")},
                                             QStringLiteral("speakers"));

    ok &= require(
        compareSnapshots(speakers, speakers) == Change::None,
        QStringLiteral("an identical enumeration is not a change"), err);

    // Qt does not promise a stable order from QMediaDevices::audioOutputs(). A bare
    // list comparison would report every re-enumeration as a hotplug and pause the
    // preview for nothing, so the snapshot normalises order.
    ok &= require(
        compareSnapshots(speakers,
                         makeOutputSnapshot({QStringLiteral("hdmi"), QStringLiteral("speakers")},
                                            QStringLiteral("speakers")))
            == Change::None,
        QStringLiteral("a pure reorder is not a change"), err);

    // 插拔设备: the device set changed while the system default stayed put.
    ok &= require(
        compareSnapshots(speakers,
                         makeOutputSnapshot({QStringLiteral("speakers"),
                                             QStringLiteral("hdmi"),
                                             QStringLiteral("headset")},
                                            QStringLiteral("speakers")))
            == Change::OutputListChanged,
        QStringLiteral("plugging a device in is an output-list change"), err);
    ok &= require(
        compareSnapshots(speakers,
                         makeOutputSnapshot({QStringLiteral("speakers")},
                                            QStringLiteral("speakers")))
            == Change::OutputListChanged,
        QStringLiteral("unplugging a non-default device is an output-list change"), err);

    // 调整设备: the same devices are present, the system just switched output.
    ok &= require(
        compareSnapshots(speakers,
                         makeOutputSnapshot({QStringLiteral("speakers"), QStringLiteral("hdmi")},
                                            QStringLiteral("hdmi")))
            == Change::DefaultOutputChanged,
        QStringLiteral("switching the system default is a default-output change"), err);

    // Unplugging the device that was the default removes it AND reassigns the
    // default, which is the single most likely way to desync a running preview.
    ok &= require(
        compareSnapshots(speakers,
                         makeOutputSnapshot({QStringLiteral("hdmi")}, QStringLiteral("hdmi")))
            == Change::Both,
        QStringLiteral("unplugging the default device changes list and default"), err);

    // Losing every output leaves no default at all; it must still read as a change
    // rather than falling through to None on an empty comparison.
    ok &= require(
        compareSnapshots(speakers, makeOutputSnapshot({}, QString())) == Change::Both,
        QStringLiteral("losing all outputs is a change"), err);

    const Change allChanges[] = {
        Change::None,
        Change::OutputListChanged,
        Change::DefaultOutputChanged,
        Change::Both,
    };
    for (const Change change : allChanges) {
        ok &= require(
            !shouldPausePreview(change, /*previewPlaying=*/false),
            QStringLiteral("no pause while the preview is not playing (%1)")
                .arg(QLatin1String(changeName(change))),
            err);
        const bool expected = change != Change::None;
        ok &= require(
            shouldPausePreview(change, /*previewPlaying=*/true) == expected,
            QStringLiteral("playing preview pauses exactly on a real change (%1)")
                .arg(QLatin1String(changeName(change))),
            err);
    }

    using miacode::preview_audio::playback_flow::PauseKind;
    using miacode::preview_audio::playback_flow::PauseRequest;
    using miacode::preview_audio::playback_flow::PauseState;
    using miacode::preview_audio::playback_flow::beginDeviceChangePause;
    const PauseState paused = beginDeviceChangePause(
        PauseState{},
        PauseRequest{PauseKind::DeviceChange, 17, 71, 117, 5, 901, 6.5});
    const PauseState ignored = beginDeviceChangePause(
        paused,
        PauseRequest{PauseKind::DeviceChange, 18, 72, 118, 6, 902, 7.5});
    ok &= require(
        ignored.deviceSequence == 6
            && ignored.pendingDevicePauseToken == 901
            && ignored.pendingDevicePauseTransactionId == 71
            && ignored.devicePauseVisualSecond == 6.5,
        QStringLiteral("a paused duplicate notification is logged/coalesced without replacing its token"),
        err);

    if (ok) {
        out << "Preview audio device change policy spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
