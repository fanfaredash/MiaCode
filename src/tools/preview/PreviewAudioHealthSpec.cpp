// Spec for the preview audio underrun / buffer-health policy.
//
// The BASS calls themselves need a sound device, so the backend TU owns them; what this
// spec pins is the policy that decides WHEN a line is written and WHAT it says — the
// stall latch (two lines per underrun, not one per poll), the sampling throttle, and the
// payload keys the OBS-contention triage reads.

#include <QString>
#include <QTextStream>

#include "audio/PreviewAudioHealth.h"

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
    namespace health = miacode::preview_audio::health;
    using health::ChannelActivity;
    using health::StallEdge;

    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    // ---- vocabulary ------------------------------------------------------------
    ok &= require(QLatin1String(health::activityName(ChannelActivity::Stalled))
                      == QLatin1String("stalled"),
                  QStringLiteral("stalled activity name"), err);
    ok &= require(QLatin1String(health::activityName(ChannelActivity::Playing))
                      == QLatin1String("playing"),
                  QStringLiteral("playing activity name"), err);
    ok &= require(QLatin1String(health::activityName(ChannelActivity::Unknown))
                      == QLatin1String("unknown"),
                  QStringLiteral("unknown activity name"), err);
    // BASS_ACTIVE_STALLED is the underrun signal; nothing else is.
    ok &= require(health::isUnderrun(ChannelActivity::Stalled),
                  QStringLiteral("stalled counts as underrun"), err);
    ok &= require(!health::isUnderrun(ChannelActivity::Playing)
                      && !health::isUnderrun(ChannelActivity::Paused)
                      && !health::isUnderrun(ChannelActivity::Stopped)
                      && !health::isUnderrun(ChannelActivity::PausedDevice),
                  QStringLiteral("only stalled counts as underrun"), err);

    // ---- stall latch -----------------------------------------------------------
    health::StallTracker tracker;
    ok &= require(health::updateStall(&tracker, false, 1.0) == StallEdge::None,
                  QStringLiteral("healthy playback produces no edge"), err);
    ok &= require(tracker.stallCount == 0,
                  QStringLiteral("healthy playback does not count a stall"), err);
    ok &= require(health::updateStall(&tracker, true, 2.0) == StallEdge::Begin,
                  QStringLiteral("entering a stall reports a begin edge"), err);
    ok &= require(tracker.stallCount == 1 && tracker.stalled,
                  QStringLiteral("stall is latched and counted once"), err);
    // The latch is what keeps a multi-second underrun from writing one line per tick.
    ok &= require(health::updateStall(&tracker, true, 2.5) == StallEdge::None,
                  QStringLiteral("a continuing stall does not re-log"), err);
    ok &= require(tracker.stallCount == 1,
                  QStringLiteral("a continuing stall is not double counted"), err);
    ok &= require(health::stallDurationMs(tracker, 2.5) == 500,
                  QStringLiteral("stall duration is measured from the begin edge"), err);
    ok &= require(health::updateStall(&tracker, false, 3.0) == StallEdge::End,
                  QStringLiteral("recovery reports an end edge"), err);
    ok &= require(!tracker.stalled && tracker.stallCount == 1,
                  QStringLiteral("recovery clears the latch without recounting"), err);
    ok &= require(health::updateStall(&tracker, true, 9.0) == StallEdge::Begin
                      && tracker.stallCount == 2,
                  QStringLiteral("a second underrun is counted separately"), err);

    health::StallTracker fresh;
    ok &= require(health::stallDurationMs(fresh, 12.0) == -1,
                  QStringLiteral("never-stalled reports no duration"), err);

    // ---- sampling throttle -----------------------------------------------------
    ok &= require(health::shouldLogHealth(0.0, -1.0),
                  QStringLiteral("first sample always logs"), err);
    ok &= require(!health::shouldLogHealth(4.9, 0.0),
                  QStringLiteral("inside the interval does not log"), err);
    ok &= require(health::shouldLogHealth(5.0, 0.0),
                  QStringLiteral("interval boundary logs"), err);
    // A backwards seek must not suppress sampling until the playhead catches up.
    ok &= require(health::shouldLogHealth(2.0, 30.0),
                  QStringLiteral("a backwards seek re-arms sampling"), err);

    // ---- payload contract ------------------------------------------------------
    health::BufferSnapshot buffers;
    buffers.minBufferMs = 10;
    buffers.initLatencyMs = 0;
    buffers.configBufferMs = 500;
    buffers.updatePeriodMs = 100;
    buffers.updateThreads = 1;
    buffers.deviceFreq = 48000;
    buffers.bufferedBytes = 8192;
    buffers.bufferedMs = 21;

    health::StallTracker payloadTracker;
    health::updateStall(&payloadTracker, true, 40.0);
    const QString payload = health::healthPayload(
        7, 41.0, ChannelActivity::Stalled, ChannelActivity::Playing, payloadTracker, buffers,
        /*mmcssRegisteredOnAudioThreads=*/false, QStringLiteral("Games"));
    ok &= require(payload.startsWith(QStringLiteral("bass_audio_health ")),
                  QStringLiteral("health line has a greppable prefix"), err);
    ok &= require(payload.contains(QStringLiteral("txn=7"))
                      && payload.contains(QStringLiteral("mixer_active=stalled"))
                      && payload.contains(QStringLiteral("bgm_active=playing")),
                  QStringLiteral("health line reports both channels"), err);
    ok &= require(payload.contains(QStringLiteral("underrun=1"))
                      && payload.contains(QStringLiteral("stall_count=1"))
                      && payload.contains(QStringLiteral("stall_ms=1000")),
                  QStringLiteral("health line reports the underrun state"), err);
    ok &= require(payload.contains(QStringLiteral("buffered_ms=21"))
                      && payload.contains(QStringLiteral("buffered_bytes=8192"))
                      && payload.contains(QStringLiteral("config_buffer_ms=500"))
                      && payload.contains(QStringLiteral("min_buffer_ms=10"))
                      && payload.contains(QStringLiteral("update_period_ms=100"))
                      && payload.contains(QStringLiteral("device_freq=48000")),
                  QStringLiteral("health line reports buffer levels"), err);
    // The MMCSS fact is the point of the line under CPU contention: BASS threads are
    // unprotected even though the render thread is registered.
    ok &= require(payload.contains(QStringLiteral("bass_mmcss_registered_by_app=0"))
                      && payload.contains(QStringLiteral("app_mmcss_task_class=Games")),
                  QStringLiteral("health line records BASS MMCSS coverage"), err);

    const QString noMmcss = health::healthPayload(
        0, 0.0, ChannelActivity::Playing, ChannelActivity::Unknown, fresh, health::BufferSnapshot(),
        false, QString());
    ok &= require(noMmcss.contains(QStringLiteral("app_mmcss_task_class=(none)"))
                      && noMmcss.contains(QStringLiteral("underrun=0")),
                  QStringLiteral("unregistered MMCSS renders as (none)"), err);
    ok &= require(noMmcss.contains(QStringLiteral("buffered_ms=-1")),
                  QStringLiteral("unreadable buffer fields render as -1"), err);

    const QString beginLine = health::stallEdgePayload(
        StallEdge::Begin, 7, 40.0, ChannelActivity::Stalled, ChannelActivity::Playing,
        payloadTracker);
    ok &= require(beginLine.startsWith(QStringLiteral("bass_audio_stall "))
                      && beginLine.contains(QStringLiteral("edge=begin"))
                      && beginLine.contains(QStringLiteral("stall_count=1")),
                  QStringLiteral("stall begin line is greppable"), err);
    const QString endLine = health::stallEdgePayload(
        StallEdge::End, 7, 40.5, ChannelActivity::Playing, ChannelActivity::Playing,
        payloadTracker);
    ok &= require(endLine.contains(QStringLiteral("edge=end"))
                      && endLine.contains(QStringLiteral("stall_ms=500")),
                  QStringLiteral("stall end line reports the underrun length"), err);

    if (ok) {
        out << "Preview audio health spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
