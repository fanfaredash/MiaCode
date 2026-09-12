#include <QCoreApplication>
#include <QString>
#include <QTextStream>

#include <deque>
#include <vector>

#include "tools/video_export/VideoExportPendingFrameRedraw.h"

namespace {

using miacode::video_export::redrawPendingPipelineFrames;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

// Stand-ins for PendingPboFrame / ReadyFramePayload: the recovery order and the
// per-frame parameters are what this contract protects, not the pixels.
struct StubPending {
    int frameIndex = -1;
    double exportSecond = 0.0;
    bool showTimestamp = false;
};

struct StubReady {
    int frameIndex = -1;
    double exportSecond = 0.0;
    bool showTimestamp = false;
};

std::deque<StubPending> makePipelineBacklog()
{
    return {
        StubPending{98, 1.6333, true},
        StubPending{99, 1.6500, false},
    };
}

// Every frame the pipeline still holds must come back out, in submission order,
// redrawn with the parameters it was submitted with. Dropping them renumbers the
// rest of the export (the raw pipe carries no frame numbers), and redrawing them
// with the *current* frame's HUD/intro state would ship the wrong overlay.
bool verifyBacklogIsRedrawnInOrder(QTextStream& err)
{
    std::deque<StubPending> pending = makePipelineBacklog();
    std::vector<StubReady> redrawn;
    const bool ok = redrawPendingPipelineFrames(
        &pending,
        [](StubPending& frame, StubReady* payload) {
            payload->frameIndex = frame.frameIndex;
            payload->exportSecond = frame.exportSecond;
            payload->showTimestamp = frame.showTimestamp;
            return true;
        },
        &redrawn);

    return require(ok, QStringLiteral("redrawing a healthy backlog must succeed"), err)
        && require(redrawn.size() == 2, QStringLiteral("both pending frames must be delivered"), err)
        && require(
               redrawn[0].frameIndex == 98 && redrawn[1].frameIndex == 99,
               QStringLiteral("pending frames must keep submission order"),
               err)
        && require(
               redrawn[0].showTimestamp && !redrawn[1].showTimestamp,
               QStringLiteral("each pending frame must be redrawn with its own parameters"),
               err)
        && require(
               redrawn[0].exportSecond > 1.63 && redrawn[0].exportSecond < 1.64,
               QStringLiteral("pending frames must keep their own export time"),
               err)
        && require(pending.empty(), QStringLiteral("the backlog must be fully consumed"), err);
}

// A redraw that cannot produce pixels has to surface as a failure. Continuing
// would ship a stream that is short by exactly the frames nobody can see.
bool verifyFailedRedrawStops(QTextStream& err)
{
    std::deque<StubPending> pending = makePipelineBacklog();
    std::vector<StubReady> redrawn;
    const bool ok = redrawPendingPipelineFrames(
        &pending,
        [](StubPending& frame, StubReady* payload) {
            if (frame.frameIndex == 99) {
                return false;
            }
            payload->frameIndex = frame.frameIndex;
            return true;
        },
        &redrawn);

    return require(!ok, QStringLiteral("a failed pending redraw must be reported"), err)
        && require(
               redrawn.size() == 1 && redrawn[0].frameIndex == 98,
               QStringLiteral("frames redrawn before the failure must be kept"),
               err);
}

bool verifyEmptyBacklogIsNoOp(QTextStream& err)
{
    std::deque<StubPending> pending;
    std::vector<StubReady> redrawn;
    const bool ok = redrawPendingPipelineFrames(
        &pending,
        [](StubPending&, StubReady*) { return true; },
        &redrawn);

    return require(ok, QStringLiteral("an empty pipeline must redraw cleanly"), err)
        && require(redrawn.empty(), QStringLiteral("an empty pipeline must deliver nothing"), err);
}

bool verifyMissingOutputsAreRejected(QTextStream& err)
{
    std::deque<StubPending> pending = makePipelineBacklog();
    std::vector<StubReady> redrawn;
    const auto renderer = [](StubPending&, StubReady*) { return true; };

    return require(
               !redrawPendingPipelineFrames<StubPending, StubReady>(nullptr, renderer, &redrawn),
               QStringLiteral("a missing backlog must be rejected"),
               err)
        && require(
               !redrawPendingPipelineFrames<StubPending, StubReady>(&pending, renderer, nullptr),
               QStringLiteral("a missing output sink must be rejected"),
               err);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyBacklogIsRedrawnInOrder(err)
        || !verifyFailedRedrawStops(err)
        || !verifyEmptyBacklogIsNoOp(err)
        || !verifyMissingOutputsAreRejected(err)) {
        return 1;
    }

    out << "video_export_pending_frame_redraw_spec ok" << Qt::endl;
    return 0;
}
