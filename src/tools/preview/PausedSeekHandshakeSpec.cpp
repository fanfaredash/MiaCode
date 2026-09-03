// Regression spec for the paused-seek media handshake surviving a media-route
// clear.
//
// What it proves (against the REAL PreviewStageMediaHost, not a stand-in):
//   PreviewStageMediaHost::submitPausedSeek() puts the host into an
//   in-flight state (pausedSeekCompletionPending_ == true) while it waits
//   for the decoder to land on the seek target. That handshake is normally
//   settled by a decoded frame / position update / timeout, all of which
//   emit pausedSeekCompleted(second, generation) — the signal
//   PlaybackCoordinator::handlePausedPreviewMediaSeekCompleted() listens for
//   to release its own pausedSeekMediaPending_ gate (Seek.cpp).
//
//   clearMedia() (reached via setChartPath(QString()), the same call
//   DocumentSessionHost::clearTimelineAndPreview() makes right after
//   requesting a paused seek — see document/DocumentPages.cpp) resets
//   pausedSeekCompletionPending_ to false directly, WITHOUT emitting
//   pausedSeekCompleted. A caller who requested a paused seek and then
//   cleared the route (chart/difficulty switch) never gets an ack for that
//   seek, so its own "still waiting" flag never resets — see
//   docs/ops/DEBUG_INDEX.md and PlaybackCoordinator::requestPausedPreviewSeek
//   / submitPausedMediaSeek / maybeSubmitLatestPausedMediaSeek, all gated on
//   state_.pausedSeekMediaPending_.
//
// This spec drives the real submitPausedSeek() / clearMedia() pair with no
// event-loop turn in between, so the outcome does not depend on whether the
// dummy .mp4 sibling actually decodes: everything relevant here happens
// synchronously on the call stack, before any queued decoder signal could be
// delivered.
//
// Built with HAVE_QT_MULTIMEDIA (the QMediaPlayer backend branch of
// PreviewStageMediaHost) rather than MIACODE_USE_QTAVPLAYER (this machine's
// shipped backend) because that keeps the dev-tool link graph to Qt
// Multimedia instead of the vendored QtAVPlayer/FFmpeg sources — see
// PreviewStageMediaHostInternal.h's own
// `#if defined(HAVE_QT_MULTIMEDIA) && !defined(MIACODE_USE_QTAVPLAYER)`
// branch, an already-supported configuration in this codebase (every non
// Windows/macOS shipped build uses it). clearMedia() itself is NOT
// `#ifdef`-split between the two backends, so the bug and its fix are
// identical either way; only how pausedSeekCompletionPending_ gets set to
// true up front differs, and this spec drives that through the real
// submitPausedSeek() implementation, not a stand-in.

#include "common/ChartAssetPaths.h"
#include "preview/runtime/PreviewStageMediaHost.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

int g_failures = 0;

void check(bool condition, const QString& label)
{
    QTextStream(stdout) << (condition ? "[ok]   " : "[FAIL] ") << label << '\n';
    if (!condition) {
        ++g_failures;
    }
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(bytes);
    file.close();
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        QTextStream(stdout) << "[FAIL] could not create temp dir\n";
        return 1;
    }

    // A chart file plus a sibling bg.mp4 is all setChartPath() needs to
    // route into loadVideoMedia(): mediaKind_ flips to Video, and a real
    // QMediaPlayer is constructed, synchronously and independently of
    // whether the dummy .mp4 bytes actually decode.
    const QString chartPath = QDir(tmp.path()).filePath(QStringLiteral("chart.txt"));
    const QString videoPath = QDir(tmp.path()).filePath(QStringLiteral("bg.mp4"));
    check(writeFile(chartPath, QByteArrayLiteral("&title=probe\n")), "chart file written");
    check(writeFile(videoPath, QByteArrayLiteral("not a real mp4, existence is all that matters")),
          "bg.mp4 sibling written");

    PreviewStageMediaHost host;
    QSignalSpy completedSpy(&host, &PreviewStageMediaHost::pausedSeekCompleted);

    host.setChartPath(chartPath);
    check(host.hasVideoMedia(), "setChartPath(chart with bg.mp4 sibling) resolves to video media");
    check(completedSpy.isEmpty(), "no paused-seek ack yet -- none was requested");

    // Submit a paused seek. On a freshly loaded source lastSeekMs_ is -1, so
    // this cannot take the "already showing this frame" queued-ack shortcut
    // (Playback.cpp): it goes down the real player_->setPosition() +
    // schedulePausedSeekTimeout() path and leaves the handshake pending.
    const double requestedSecond = 1.234;
    const quint64 requestedGeneration = 42;
    host.submitPausedSeek(requestedSecond, requestedGeneration);
    check(completedSpy.isEmpty(),
          "paused seek is in flight immediately after submitPausedSeek "
          "(no queued/decoder signal has had a chance to run yet)");

    // This is the exact call clearPreviewStageMediaRoute() makes
    // (document/DocumentPages.cpp:728, via setChartPath("")) — reached ~9
    // lines after the paused-seek request in
    // DocumentSessionHost::clearTimelineAndPreview(). No QCoreApplication
    // event-loop turn has happened since submitPausedSeek(), so the pending
    // handshake is still exactly as that request left it.
    host.setChartPath(QString());
    check(!host.hasVideoMedia(), "clearing the chart path drops video media");

    // This is the assertion that is expected to fail before the fix: a
    // caller who cleared the media route while a paused seek was in flight
    // must still receive a termination ack for that seek (completed, not
    // silently abandoned) so its own pending-seek gate can release.
    check(!completedSpy.isEmpty(),
          "clearMedia() emits pausedSeekCompleted to release the abandoned "
          "in-flight paused seek");
    if (!completedSpy.isEmpty()) {
        const QList<QVariant> args = completedSpy.constFirst();
        const double ackedSecond = args.at(0).toDouble();
        const quint64 ackedGeneration = args.at(1).toULongLong();
        check(qFuzzyCompare(ackedSecond + 1.0, requestedSecond + 1.0),
              "pausedSeekCompleted reports the second the abandoned seek was targeting");
        check(ackedGeneration == requestedGeneration,
              "pausedSeekCompleted reports the generation the abandoned seek was submitted "
              "with (PlaybackCoordinator::handlePausedPreviewMediaSeekCompleted checks this "
              "against state_.pausedSeekMediaSubmittedGeneration_ and drops stale acks)");
    }

    QTextStream(stdout) << (g_failures == 0 ? "ALL PASS\n"
                                            : QStringLiteral("%1 FAILURE(S)\n").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
