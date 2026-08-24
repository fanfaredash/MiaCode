#include <QString>
#include <QTextStream>

#include "audio/BassPreviewSfxSchedulerPolicy.h"
#include "common/LogEmissionPolicy.h"
#include "common/UiHangWatchdogPolicy.h"

namespace {

bool expect(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    miacode::diagnostics::EdgeLogGate<QString> edge;
    ok &= expect(edge.shouldEmit(QStringLiteral("first")), QStringLiteral("first edge emits"), err);
    ok &= expect(!edge.shouldEmit(QStringLiteral("first")), QStringLiteral("unchanged edge suppresses"), err);
    ok &= expect(edge.shouldEmit(QStringLiteral("changed")), QStringLiteral("changed edge emits"), err);
    edge.reset();
    ok &= expect(edge.shouldEmit(QStringLiteral("first")), QStringLiteral("reset edge emits again"), err);

    miacode::diagnostics::StageRateKey stageA{1.0, 2};
    miacode::diagnostics::StageRateKey stageB{1.0, 3};
    miacode::diagnostics::EdgeLogGate<miacode::diagnostics::StageRateKey> stageGate;
    ok &= expect(stageGate.shouldEmit(stageA), QStringLiteral("first stage rate emits"), err);
    ok &= expect(!stageGate.shouldEmit(stageA), QStringLiteral("same stage rate suppresses"), err);
    ok &= expect(stageGate.shouldEmit(stageB), QStringLiteral("media kind change emits"), err);

    miacode::diagnostics::PlaybackRateLogGate playbackRate;
    ok &= expect(
        playbackRate.shouldEmit(miacode::diagnostics::PlaybackRateLogKind::Ordinary, stageA),
        QStringLiteral("first ordinary stage rate emits"), err);
    ok &= expect(
        !playbackRate.shouldEmit(miacode::diagnostics::PlaybackRateLogKind::Ordinary, stageA),
        QStringLiteral("unchanged ordinary stage rate suppresses"), err);
    ok &= expect(
        playbackRate.shouldEmit(miacode::diagnostics::PlaybackRateLogKind::Deferred, stageA),
        QStringLiteral("deferred playback rate bypasses ordinary gate"), err);
    ok &= expect(
        playbackRate.shouldEmit(miacode::diagnostics::PlaybackRateLogKind::Flushed, stageA),
        QStringLiteral("flushed playback rate bypasses ordinary gate"), err);
    ok &= expect(
        playbackRate.shouldEmit(miacode::diagnostics::PlaybackRateLogKind::Error, stageA),
        QStringLiteral("playback rate error bypasses ordinary gate"), err);
    playbackRate.reset();
    ok &= expect(
        playbackRate.shouldEmit(miacode::diagnostics::PlaybackRateLogKind::Ordinary, stageA),
        QStringLiteral("chart or media reset permits the same ordinary rate"), err);

    miacode::diagnostics::MousePressLogGate mousePress;
    const quint64 firstMousePress = mousePress.beginPress();
    ok &= expect(firstMousePress == 1, QStringLiteral("native press entry starts sequence one"), err);
    ok &= expect(
        mousePress.shouldEmit(QStringLiteral("watched=editor|focus=editor|armed=1")),
        QStringLiteral("first complete mouse signature emits"), err);
    ok &= expect(
        !mousePress.shouldEmit(QStringLiteral("watched=editor|focus=editor|armed=1")),
        QStringLiteral("identical watched-object signature suppresses within one press"), err);
    ok &= expect(
        mousePress.shouldEmit(QStringLiteral("watched=sidebar|focus=editor|armed=1")),
        QStringLiteral("mismatching watched-object signature remains visible"), err);
    const quint64 secondMousePress = mousePress.beginPress();
    ok &= expect(secondMousePress == 2, QStringLiteral("later native press increments sequence"), err);
    ok &= expect(
        mousePress.shouldEmit(QStringLiteral("watched=editor|focus=editor|armed=1")),
        QStringLiteral("new mouse press resets its signature lifecycle"), err);

    miacode::diagnostics::EdgeLogGate<QString> backgroundSettings;
    const QString backgroundA = QStringLiteral("enabled=1 path=/tmp/a.png opacity=0.2");
    const QString backgroundB = QStringLiteral("enabled=1 path=/tmp/b.png opacity=0.2");
    ok &= expect(backgroundSettings.shouldEmit(backgroundA),
                 QStringLiteral("first unchanged background settings record emits"), err);
    ok &= expect(!backgroundSettings.shouldEmit(backgroundA),
                 QStringLiteral("unchanged background settings suppress within lifecycle"), err);
    ok &= expect(backgroundSettings.shouldEmit(backgroundB),
                 QStringLiteral("real background settings change emits"), err);
    backgroundSettings.reset();
    ok &= expect(backgroundSettings.shouldEmit(backgroundB),
                 QStringLiteral("new background lifecycle emits unchanged signature once"), err);

    miacode::diagnostics::SurfaceLogGate surfaceLog;
    const miacode::diagnostics::SurfaceSizeKey surfaceA{0x100U, 800, 600};
    const miacode::diagnostics::SurfaceSizeKey surfaceB{0x200U, 800, 600};
    ok &= expect(surfaceLog.shouldEmit(surfaceA, false),
                 QStringLiteral("first surface handle and size emits"), err);
    ok &= expect(!surfaceLog.shouldEmit(surfaceA, false),
                 QStringLiteral("unchanged surface handle and size suppresses"), err);
    ok &= expect(surfaceLog.shouldEmit(surfaceB, false),
                 QStringLiteral("surface host replacement emits"), err);
    ok &= expect(surfaceLog.shouldEmit(surfaceB, true),
                 QStringLiteral("slow workspace call bypasses surface gate"), err);
    surfaceLog.reset();
    ok &= expect(surfaceLog.shouldEmit(surfaceB, false),
                 QStringLiteral("surface lifecycle reset permits unchanged state"), err);

    miacode::diagnostics::RebuildWindow rebuild;
    auto first = rebuild.observe(100, 1, QStringLiteral("line-a"));
    auto second = rebuild.observe(200, 1, QStringLiteral("line-b"));
    ok &= expect(!first.emitIndividual && !second.emitIndividual,
                 QStringLiteral("fast rebuilds aggregate"), err);
    auto interval = rebuild.observe(1200, 1, QStringLiteral("line-c"));
    ok &= expect(interval.summary.has_value()
                     && interval.summary->windowStartMs == 100
                     && interval.summary->rebuildCount == 2
                     && interval.summary->totalElapsedMs == 2
                     && interval.summary->maxElapsedMs == 1
                     && interval.summary->lastLines == QStringLiteral("line-b"),
                 QStringLiteral("one-second rebuild flush carries aggregate fields"), err);
    auto slow = rebuild.observe(1300, 4, QStringLiteral("slow"));
    ok &= expect(slow.emitIndividual && slow.individualElapsedMs == 4,
                 QStringLiteral("slow rebuild emits individually"), err);
    rebuild.observe(1400, 1, QStringLiteral("line-d"));
    auto error = rebuild.observe(1500, 1, QStringLiteral("error"), true);
    ok &= expect(error.summary.has_value() && error.emitIndividual,
                 QStringLiteral("error rebuild flushes pending window and emits itself"), err);
    rebuild.observe(1600, 1, QStringLiteral("destructor"));
    const auto destruction = rebuild.flushForDestruction();
    ok &= expect(destruction.has_value() && destruction->lastLines == QStringLiteral("destructor"),
                 QStringLiteral("destruction flush retains last lines"), err);

    ok &= expect(miacode::preview_audio::bass::shouldLogDisarm(true, false, -1),
                 QStringLiteral("active disarm remains visible"), err);
    ok &= expect(!miacode::preview_audio::bass::shouldLogDisarm(false, false, -1),
                 QStringLiteral("proven no-op disarm suppresses"), err);
    ok &= expect(miacode::preview_audio::bass::shouldLogDisarm(false, true, -1),
                 QStringLiteral("sync-error disarm remains visible"), err);

    using miacode::hang_watchdog::policy::Trigger;
    miacode::hang_watchdog::policy::SuppressionEpisode episode;
    ok &= expect(!episode.observe(false, Trigger::IdleHeartbeat).has_value(),
                 QStringLiteral("first suppressed watchdog poll opens episode"), err);
    ok &= expect(!episode.observe(false, Trigger::IdleHeartbeat).has_value(),
                 QStringLiteral("later suppressed watchdog poll aggregates"), err);
    const auto report = episode.observe(true, Trigger::IdleHeartbeat);
    ok &= expect(report.has_value()
                     && report->suppressedCount == 2
                     && report->trigger == Trigger::IdleHeartbeat,
                 QStringLiteral("next watchdog report flushes suppression summary"), err);
    ok &= expect(!episode.endEpisode().has_value(), QStringLiteral("flushed episode closes cleanly"), err);

    if (ok) {
        out << "Log pruning policy spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
