// Spec for the preview audio output-buffer discontinuity probe.
//
// The BASS DSP callback itself needs a live mixer (see
// BassPreviewAudioBackend_EngineInit.cpp), so this spec pins the pure policy it is
// built on: the step/clip/late-callback detectors as caller-supplied-state
// transitions, the SPSC ring's push/pop/drop bookkeeping, and the log line format.

#include <QString>
#include <QTextStream>

#include "audio/PreviewAudioOutputGlitchProbe.h"
#include "audio/PreviewAudioOutputGlitchRing.h"

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
    namespace glitch = miacode::preview_audio::output_glitch;

    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    // ---- step detector -----------------------------------------------------
    {
        glitch::StepDetectorState state;
        float magnitude = -1.0f;
        ok &= require(
            !glitch::updateStepDetector(&state, 0.1f, glitch::kDefaultStepThreshold, &magnitude),
            QStringLiteral("first sample has no predecessor to jump from"), err);
        ok &= require(
            !glitch::updateStepDetector(&state, 0.2f, glitch::kDefaultStepThreshold, &magnitude),
            QStringLiteral("a small step under threshold does not fire"), err);
        // 0.2 -> 0.9 is a 0.7 jump, well past the 0.25 default threshold.
        ok &= require(
            glitch::updateStepDetector(&state, 0.9f, glitch::kDefaultStepThreshold, &magnitude),
            QStringLiteral("a jump past threshold fires"), err);
        ok &= require(
            qFuzzyCompare(magnitude, 0.7f), QStringLiteral("step magnitude is the absolute delta"), err);
        // A large step DOWN must also fire (magnitude uses absolute value).
        ok &= require(
            glitch::updateStepDetector(&state, 0.1f, glitch::kDefaultStepThreshold, &magnitude),
            QStringLiteral("a downward jump past threshold also fires"), err);
        ok &= require(
            qFuzzyCompare(magnitude, 0.8f), QStringLiteral("downward step magnitude is unsigned"), err);

        // A step that straddles a "block boundary" must still be caught: two separate
        // states seeded from the same lastSample split across two updateStepDetector
        // call sequences behave identically to one continuous sequence.
        glitch::StepDetectorState blockA;
        glitch::StepDetectorState blockB;
        glitch::updateStepDetector(&blockA, 0.0f, glitch::kDefaultStepThreshold, nullptr);
        blockB = blockA;  // simulates carrying tail state into the next DSP block
        float crossBlockMagnitude = 0.0f;
        ok &= require(
            glitch::updateStepDetector(&blockB, 0.9f, glitch::kDefaultStepThreshold, &crossBlockMagnitude),
            QStringLiteral("a step carried across a block boundary still fires"), err);
        ok &= require(
            qFuzzyCompare(crossBlockMagnitude, 0.9f),
            QStringLiteral("cross-block step magnitude is correct"), err);
    }

    // ---- clip run tracker ----------------------------------------------------
    {
        glitch::ClipRunState state;
        // Below threshold: no run.
        glitch::ClipRunUpdate update = glitch::updateClipRun(&state, 0.5f, glitch::kDefaultClipThreshold, 0);
        ok &= require(!update.runEnded, QStringLiteral("a sample under the clip threshold starts no run"), err);
        ok &= require(!state.active, QStringLiteral("state stays inactive under threshold"), err);

        // Three consecutive clipped samples starting at frame 10.
        update = glitch::updateClipRun(&state, 1.0f, glitch::kDefaultClipThreshold, 10);
        ok &= require(!update.runEnded && state.active && state.startFrame == 10 && state.length == 1,
                      QStringLiteral("first clipped sample opens a run"), err);
        update = glitch::updateClipRun(&state, -1.0f, glitch::kDefaultClipThreshold, 11);
        ok &= require(!update.runEnded && state.length == 2,
                      QStringLiteral("a negative full-scale sample also counts as clipping"), err);
        update = glitch::updateClipRun(&state, 0.9995f, glitch::kDefaultClipThreshold, 12);
        ok &= require(!update.runEnded && state.length == 3,
                      QStringLiteral("a run keeps extending while samples stay at/over threshold"), err);

        // Falling edge: the sample AFTER the run reports it closed.
        update = glitch::updateClipRun(&state, 0.3f, glitch::kDefaultClipThreshold, 13);
        ok &= require(update.runEnded && update.startFrame == 10 && update.length == 3,
                      QStringLiteral("the falling-edge sample reports the closed run's start and length"), err);
        ok &= require(!state.active, QStringLiteral("the tracker resets after the run closes"), err);

        // A run that never falls back under threshold before teardown must still be
        // reported via flushClipRun rather than silently dropped.
        glitch::updateClipRun(&state, 1.0f, glitch::kDefaultClipThreshold, 20);
        glitch::updateClipRun(&state, 1.0f, glitch::kDefaultClipThreshold, 21);
        const glitch::ClipRunUpdate flushed = glitch::flushClipRun(&state);
        ok &= require(flushed.runEnded && flushed.startFrame == 20 && flushed.length == 2,
                      QStringLiteral("flushClipRun closes a still-open run at teardown"), err);
        const glitch::ClipRunUpdate flushedAgain = glitch::flushClipRun(&state);
        ok &= require(!flushedAgain.runEnded,
                      QStringLiteral("flushing an already-closed tracker is a no-op"), err);
    }

    // ---- late-callback probe --------------------------------------------------
    {
        // 480 frames at 48000 Hz is exactly 10ms; a callback arriving right on time
        // must not fire.
        const glitch::LateCallbackProbe onTime = glitch::computeLateCallback(
            /*previousArrivalNs=*/0, /*previousBlockFrames=*/480, /*sampleRateHz=*/48000,
            /*currentArrivalNs=*/10'000'000);
        ok &= require(onTime.valid && onTime.lateMs == 0,
                      QStringLiteral("an on-time callback reports zero lateness"), err);
        ok &= require(!glitch::isLate(onTime, glitch::kDefaultLateCallbackThresholdMs),
                      QStringLiteral("an on-time callback is not flagged late"), err);

        // Arrives 5ms after the 10ms block should have finished: 5ms late.
        const glitch::LateCallbackProbe late = glitch::computeLateCallback(
            0, 480, 48000, 15'000'000);
        ok &= require(late.valid && late.lateMs == 5,
                      QStringLiteral("a delayed callback reports its lateness in ms"), err);
        ok &= require(glitch::isLate(late, glitch::kDefaultLateCallbackThresholdMs),
                      QStringLiteral("lateness past the threshold is flagged"), err);

        // Below the threshold: not flagged even though technically nonzero.
        const glitch::LateCallbackProbe barelyLate = glitch::computeLateCallback(
            0, 480, 48000, 11'000'000);  // 1ms late
        ok &= require(barelyLate.valid && barelyLate.lateMs == 1,
                      QStringLiteral("sub-threshold lateness is still measured"), err);
        ok &= require(!glitch::isLate(barelyLate, glitch::kDefaultLateCallbackThresholdMs),
                      QStringLiteral("sub-threshold lateness is not flagged"), err);

        // No previous block to compare against: not comparable.
        const glitch::LateCallbackProbe first = glitch::computeLateCallback(-1, 0, 48000, 10'000'000);
        ok &= require(!first.valid, QStringLiteral("the first callback is never comparable"), err);
        ok &= require(!glitch::isLate(first, glitch::kDefaultLateCallbackThresholdMs),
                      QStringLiteral("a not-comparable probe is never flagged late"), err);
    }

    // ---- log payload format ----------------------------------------------------
    {
        glitch::GlitchEvent step;
        step.kind = glitch::GlitchKind::Step;
        step.channel = 1;
        step.frame = 48000;  // exactly 1.0s at 48000 Hz
        step.magnitude = 0.6789;
        const QString stepLine = glitch::glitchEventPayload(7, 48000, step);
        ok &= require(stepLine.startsWith(QStringLiteral("bass_output_glitch ")),
                      QStringLiteral("glitch line has a greppable prefix"), err);
        ok &= require(stepLine.contains(QStringLiteral("kind=step"))
                          && stepLine.contains(QStringLiteral("txn=7"))
                          && stepLine.contains(QStringLiteral("mixer_pos=1.0000"))
                          && stepLine.contains(QStringLiteral("channel=1"))
                          && stepLine.contains(QStringLiteral("magnitude=0.6789")),
                      QStringLiteral("step line reports position, channel, and magnitude"), err);

        glitch::GlitchEvent clip;
        clip.kind = glitch::GlitchKind::Clip;
        clip.channel = 0;
        clip.frame = 24000;  // 0.5s
        clip.length = 12;
        const QString clipLine = glitch::glitchEventPayload(7, 48000, clip);
        ok &= require(clipLine.contains(QStringLiteral("kind=clip"))
                          && clipLine.contains(QStringLiteral("mixer_pos=0.5000"))
                          && clipLine.contains(QStringLiteral("channel=0"))
                          && clipLine.contains(QStringLiteral("samples=12")),
                      QStringLiteral("clip line reports position, channel, and run length"), err);

        glitch::GlitchEvent lateCallback;
        lateCallback.kind = glitch::GlitchKind::LateCallback;
        lateCallback.frame = 96000;  // 2.0s
        lateCallback.magnitude = 4.0;
        lateCallback.length = 480;
        const QString lateLine = glitch::glitchEventPayload(7, 48000, lateCallback);
        ok &= require(lateLine.contains(QStringLiteral("kind=late_callback"))
                          && lateLine.contains(QStringLiteral("mixer_pos=2.0000"))
                          && lateLine.contains(QStringLiteral("late_ms=4"))
                          && lateLine.contains(QStringLiteral("block_frames=480")),
                      QStringLiteral("late_callback line reports position, lateness, and block size"), err);

        // sampleRateHz == 0 (engine never initialized a rate) must not divide by zero.
        glitch::GlitchEvent unreadable;
        unreadable.kind = glitch::GlitchKind::Step;
        unreadable.frame = 100;
        const QString unreadableLine = glitch::glitchEventPayload(0, 0, unreadable);
        ok &= require(unreadableLine.contains(QStringLiteral("mixer_pos=-1.0000")),
                      QStringLiteral("an unknown sample rate renders mixer_pos as the -1 sentinel"), err);

        const QString droppedLine = glitch::glitchDroppedPayload(7, 3);
        ok &= require(droppedLine == QStringLiteral("bass_output_glitch_dropped txn=7 count=3"),
                      QStringLiteral("dropped-count line is greppable and exact"), err);
    }

    // ---- SPSC ring ---------------------------------------------------------
    {
        glitch::GlitchRing ring;
        glitch::GlitchEvent out;
        ok &= require(!ring.tryPop(&out), QStringLiteral("an empty ring pops nothing"), err);

        glitch::GlitchEvent in;
        in.kind = glitch::GlitchKind::Clip;
        in.channel = 1;
        in.frame = 42;
        in.length = 5;
        ok &= require(ring.tryPush(in), QStringLiteral("pushing into a non-full ring succeeds"), err);
        ok &= require(ring.tryPop(&out), QStringLiteral("popping a non-empty ring succeeds"), err);
        ok &= require(out.kind == glitch::GlitchKind::Clip && out.channel == 1 && out.frame == 42
                          && out.length == 5,
                      QStringLiteral("popped event matches what was pushed"), err);
        ok &= require(!ring.tryPop(&out), QStringLiteral("the ring is empty again after draining"), err);
        ok &= require(ring.takeDroppedCount() == 0,
                      QStringLiteral("nothing was dropped in the happy path"), err);

        // Fill the ring to capacity (one slot is always kept empty to distinguish
        // full from empty), then push one more and confirm it is dropped rather
        // than overwriting an unread slot or blocking.
        for (std::size_t i = 0; i < glitch::GlitchRing::kCapacity - 1; ++i) {
            glitch::GlitchEvent filler;
            filler.frame = i;
            ok &= require(ring.tryPush(filler),
                          QStringLiteral("filling the ring up to capacity-1 succeeds"), err);
        }
        glitch::GlitchEvent overflow;
        overflow.frame = 999999;
        ok &= require(!ring.tryPush(overflow),
                      QStringLiteral("pushing past capacity is rejected, not blocked"), err);
        ok &= require(ring.takeDroppedCount() == 1,
                      QStringLiteral("a rejected push is counted as dropped"), err);
        ok &= require(ring.takeDroppedCount() == 0,
                      QStringLiteral("takeDroppedCount reads-and-clears"), err);

        // Draining in FIFO order recovers exactly what was pushed, in order.
        for (std::size_t i = 0; i < glitch::GlitchRing::kCapacity - 1; ++i) {
            ok &= require(ring.tryPop(&out) && out.frame == i,
                          QStringLiteral("drained events preserve push order"), err);
        }
        ok &= require(!ring.tryPop(&out), QStringLiteral("the ring is empty after draining everything"), err);
    }

    if (ok) {
        out << "Preview audio output glitch probe spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
