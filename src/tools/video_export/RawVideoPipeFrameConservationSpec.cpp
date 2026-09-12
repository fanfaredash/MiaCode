#include <QByteArray>
#include <QCoreApplication>
#include <QTextStream>

#include "tools/video_export/RawVideoPipeTransport.h"

namespace {

using miacode::video_export::raw_pipe::enqueueRawVideoFrame;
using miacode::video_export::raw_pipe::finishRawVideoPipePump;
using miacode::video_export::raw_pipe::RawVideoPipePump;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

QByteArray stubFrameBytes()
{
    return QByteArray(4, '\0');
}

// The pump is driven without a writer thread on purpose: enqueue only touches
// the queue plus its bookkeeping under the mutex, so the sequence rules are
// observable without a real FIFO/named pipe (and without ffmpeg) on the other
// end. `writerThreadStarted` stays false, which is also what lets the written
// total stay at zero for the conservation checks below.
bool verifyFrameSequenceConservation(QTextStream& err)
{
    {
        RawVideoPipePump pump;
        QString detail;
        for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
            if (!require(
                    enqueueRawVideoFrame(
                        &pump, stubFrameBytes(), frameIndex, nullptr, nullptr, &detail),
                    QStringLiteral("consecutive frame %1 must enqueue (%2)")
                        .arg(frameIndex)
                        .arg(detail),
                    err)) {
                return false;
            }
        }
    }

    // A readback pipeline that drops in-flight frames does not leave a hole in
    // the rawvideo stream: the pipe carries pixels only, so the next frame
    // silently takes the missing slot and every later frame runs ahead of the
    // audio/background for the rest of the export. The enqueue side is the last
    // place that still knows the real frame number, so it has to reject the gap.
    {
        RawVideoPipePump pump;
        QString detail;
        for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
            enqueueRawVideoFrame(&pump, stubFrameBytes(), frameIndex, nullptr, nullptr, &detail);
        }
        detail.clear();
        if (!require(
                !enqueueRawVideoFrame(&pump, stubFrameBytes(), 5, nullptr, nullptr, &detail),
                QStringLiteral("a gap in the frame sequence must be rejected"),
                err)
            || !require(
                detail.contains(QStringLiteral("expected=3"))
                    && detail.contains(QStringLiteral("frame=5")),
                QStringLiteral("gap rejection must name both frame numbers (%1)").arg(detail),
                err)) {
            return false;
        }
    }

    {
        RawVideoPipePump pump;
        QString detail;
        for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
            enqueueRawVideoFrame(&pump, stubFrameBytes(), frameIndex, nullptr, nullptr, &detail);
        }
        detail.clear();
        if (!require(
                !enqueueRawVideoFrame(&pump, stubFrameBytes(), 2, nullptr, nullptr, &detail),
                QStringLiteral("a repeated frame number must be rejected"),
                err)) {
            return false;
        }
    }

    return true;
}

bool verifyWrittenFrameConservation(QTextStream& err)
{
    // Frames were produced and queued, but none reached the pipe. A container
    // with the planned frame count proves nothing here (ffmpeg extends the
    // overlay input's last frame), so the pump itself has to compare what it
    // was handed against what it actually wrote.
    {
        RawVideoPipePump pump;
        QString detail;
        for (int frameIndex = 0; frameIndex < 3; ++frameIndex) {
            enqueueRawVideoFrame(&pump, stubFrameBytes(), frameIndex, nullptr, nullptr, &detail);
        }
        detail.clear();
        if (!require(
                !finishRawVideoPipePump(&pump, 3, &detail),
                QStringLiteral("finalize must fail when no frame reached the pipe"),
                err)
            || !require(
                detail.contains(QStringLiteral("planned=3"))
                    && detail.contains(QStringLiteral("written=0")),
                QStringLiteral("finalize failure must report the shortfall (%1)").arg(detail),
                err)) {
            return false;
        }
    }

    {
        RawVideoPipePump pump;
        QString detail;
        if (!require(
                finishRawVideoPipePump(&pump, 0, &detail),
                QStringLiteral("a pump with no planned frames must finalize clean (%1)").arg(detail),
                err)) {
            return false;
        }
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyFrameSequenceConservation(err) || !verifyWrittenFrameConservation(err)) {
        return 1;
    }

    out << "raw_video_pipe_frame_conservation_spec ok" << Qt::endl;
    return 0;
}
