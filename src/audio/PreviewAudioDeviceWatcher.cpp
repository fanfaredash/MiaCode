#include "PreviewAudioDeviceWatcher.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QAudioDevice>
#include <QMediaDevices>

namespace {

using miacode::preview_audio::device_change::OutputSnapshot;
using miacode::preview_audio::device_change::makeOutputSnapshot;

OutputSnapshot currentOutputSnapshot()
{
    QStringList outputIds;
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    outputIds.reserve(outputs.size());
    for (const QAudioDevice& output : outputs) {
        outputIds.append(QString::fromUtf8(output.id()));
    }
    return makeOutputSnapshot(std::move(outputIds),
                              QString::fromUtf8(QMediaDevices::defaultAudioOutput().id()));
}

}  // namespace

PreviewAudioDeviceWatcher::PreviewAudioDeviceWatcher(QObject* parent)
    : QObject(parent)
    , mediaDevices_(new QMediaDevices(this))
    , snapshot_(currentOutputSnapshot())
{
    // Queued on purpose. Qt Multimedia can raise this from its own device-notification
    // path, and the consumer pauses the BASS transport, stops timers, and flushes the
    // timeline. Hopping to the GUI event loop first keeps all of that off the notifier's
    // stack.
    connect(mediaDevices_, &QMediaDevices::audioOutputsChanged,
            this, &PreviewAudioDeviceWatcher::handleAudioOutputsChanged,
            Qt::QueuedConnection);
}

void PreviewAudioDeviceWatcher::handleAudioOutputsChanged()
{
    const OutputSnapshot previous = snapshot_;
    OutputSnapshot current = currentOutputSnapshot();
    const Change change = compareSnapshots(previous, current);
    if (change == Change::None) {
        return;
    }
    snapshot_ = std::move(current);

    // Audio channel, so this lands in miacode_audio_debug.log next to the
    // `preview/playback pause_exact` it causes and the `bass_status` / `bgm_delta_ms`
    // rows used to judge the desync.
    if (miacode::debug_options::audioDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/audio_device"),
            QStringLiteral("action=outputs_changed change=%1 outputs_before=%2 outputs_after=%3 "
                           "default_before=%4 default_after=%5")
                .arg(QLatin1String(changeName(change)))
                .arg(previous.outputIds.size())
                .arg(snapshot_.outputIds.size())
                .arg(previous.defaultOutputId.isEmpty() ? QStringLiteral("(none)")
                                                        : previous.defaultOutputId)
                .arg(snapshot_.defaultOutputId.isEmpty() ? QStringLiteral("(none)")
                                                         : snapshot_.defaultOutputId));
    }

    emit outputConfigurationChanged(change);
}
