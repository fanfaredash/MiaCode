#include <QTextStream>

#include "audio/PreviewAudioPlaybackFlowPolicy.h"

namespace {

using namespace miacode::preview_audio::playback_flow;

bool expect(bool condition, const char* message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

Completion completion(
    CompletionKind kind,
    quint64 generation,
    quint64 transactionId,
    quint64 sequence,
    double effectiveSecond,
    bool success = true,
    bool degraded = false)
{
    Completion value;
    value.kind = kind;
    value.generation = generation;
    value.transactionId = transactionId;
    value.sequence = sequence;
    value.effectiveSecond = effectiveSecond;
    value.success = success;
    value.degraded = degraded;
    return value;
}

bool verifyCurrentPrepareCommitsOnlyOnce(QTextStream& err)
{
    const State waiting = beginColdPrepare(State{}, Request{11, 101, 501, 12.0});
    const Decision accepted = decideCompletion(
        waiting,
        completion(CompletionKind::Prepare, 11, 101, 501, 12.25));
    const Decision duplicate = decideCompletion(
        accepted.state,
        completion(CompletionKind::Prepare, 11, 101, 501, 12.25));
    const Decision staleGeneration = decideCompletion(
        waiting,
        completion(CompletionKind::Prepare, 10, 101, 501, 10.0));
    const Decision staleTransaction = decideCompletion(
        waiting,
        completion(CompletionKind::Prepare, 11, 100, 501, 10.0));
    const Decision staleSequence = decideCompletion(
        waiting,
        completion(CompletionKind::Prepare, 11, 101, 500, 10.0));

    bool ok = true;
    ok &= expect(accepted.matchesPending && accepted.commitsAudioPreparation,
                 "current prepare completion commits audio preparation", err);
    ok &= expect(accepted.state.audioPrepared && !accepted.state.uiPlaying,
                 "accepted prepare keeps UI paused until the startup group commits", err);
    ok &= expect(!duplicate.matchesPending && !duplicate.commitsWorkerSecond
                     && !duplicate.commitsAudioPreparation,
                 "duplicate prepare completion is consumed after the first acceptance", err);
    ok &= expect(!staleGeneration.matchesPending && !staleGeneration.commitsAudioPreparation,
                 "old generation prepare is rejected", err);
    ok &= expect(!staleTransaction.matchesPending && !staleTransaction.commitsAudioPreparation,
                 "old transaction prepare is rejected", err);
    ok &= expect(!staleSequence.matchesPending && !staleSequence.commitsAudioPreparation,
                 "old sequence prepare is rejected", err);
    return ok;
}

bool verifyCurrentRetainedCompletionCommitsOnlyOnce(QTextStream& err)
{
    const State waiting = beginRetainedPlayback(
        State{}, Request{20, 202, 601, 8.0}, CompletionKind::RetainedResume);
    const Decision resume = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedResume, 20, 202, 601, 8.0));
    const Decision seek = decideCompletion(
        beginRetainedPlayback(State{}, Request{21, 203, 602, 9.0}, CompletionKind::RetainedSeek),
        completion(CompletionKind::RetainedSeek, 21, 203, 602, 9.125));

    bool ok = true;
    ok &= expect(resume.matchesPending && resume.commitsAudioPreparation,
                 "current retained-resume completion commits", err);
    ok &= expect(seek.matchesPending && seek.commitsAudioPreparation,
                 "current retained-seek completion commits", err);
    return ok;
}

bool verifyRetainedResumeRejectsEachStaleIdentity(QTextStream& err)
{
    const State waiting = beginRetainedPlayback(
        State{}, Request{22, 222, 622, 8.0}, CompletionKind::RetainedResume);
    const Decision staleGeneration = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedResume, 21, 222, 622, 8.0));
    const Decision staleTransaction = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedResume, 22, 221, 622, 8.0));
    const Decision staleSequence = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedResume, 22, 222, 621, 8.0));

    bool ok = true;
    ok &= expect(!staleGeneration.matchesPending,
                 "retained resume rejects a stale generation", err);
    ok &= expect(!staleTransaction.matchesPending,
                 "retained resume rejects a stale transaction", err);
    ok &= expect(!staleSequence.matchesPending,
                 "retained resume rejects a stale sequence", err);
    return ok;
}

bool verifyRetainedSeekRejectsEachStaleIdentity(QTextStream& err)
{
    State playing;
    playing.currentGeneration = 22;
    playing.activeTransactionId = 223;
    playing.transportAnchorSecond = 7.0;
    playing.audioPrepared = true;
    playing.uiPlaying = true;
    const State waiting = beginPlayingRetainedSeek(
        playing, Request{23, 223, 623, 9.0});
    const Decision staleGeneration = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedSeek, 22, 223, 623, 9.0));
    const Decision staleTransaction = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedSeek, 23, 222, 623, 9.0));
    const Decision staleSequence = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedSeek, 23, 223, 622, 9.0));

    bool ok = true;
    ok &= expect(!staleGeneration.matchesPending,
                 "retained seek rejects a stale generation", err);
    ok &= expect(!staleTransaction.matchesPending,
                 "retained seek rejects a stale transaction", err);
    ok &= expect(!staleSequence.matchesPending,
                 "retained seek rejects a stale sequence", err);
    return ok;
}

bool verifyNewerPlaybackRejectsOldCompletion(QTextStream& err)
{
    State state = beginRetainedPlayback(
        State{}, Request{30, 301, 701, 4.0}, CompletionKind::RetainedSeek);
    state = beginColdPrepare(state, Request{31, 302, 702, 13.0});

    const Decision oldSeek = decideCompletion(
        state,
        completion(CompletionKind::RetainedSeek, 30, 301, 701, 4.0));
    const Decision oldPrepare = decideCompletion(
        state,
        completion(CompletionKind::Prepare, 30, 301, 701, 4.0));

    return expect(!oldSeek.matchesPending && !oldSeek.commitsAudioPreparation,
                  "newer seek/start rejects an old retained completion", err)
        && expect(!oldPrepare.matchesPending && !oldPrepare.commitsAudioPreparation,
                  "newer seek/start rejects an old prepare completion", err);
}

bool verifyVisualSecondWaitsForAuthoritativeCompletion(QTextStream& err)
{
    State initial;
    initial.effectiveWorkerSecond = 3.0;
    const State waiting = beginColdPrepare(initial, Request{40, 401, 801, 15.0});
    const Decision accepted = decideCompletion(
        waiting,
        completion(CompletionKind::Prepare, 40, 401, 801, 15.125));

    bool ok = true;
    ok &= expect(waiting.visualSecond == 15.0,
                 "visual second updates while audio work is pending", err);
    ok &= expect(waiting.requestedVisualSecond == 15.0,
                 "worker request retains the requested visual second", err);
    ok &= expect(waiting.effectiveWorkerSecond == 3.0,
                 "pending work does not replace the previous worker-effective second", err);
    ok &= expect(accepted.state.effectiveWorkerSecond == 15.125,
                 "accepted completion makes worker-effective second authoritative", err);
    return ok;
}

bool verifyPlayingSeekWaitsForAcceptedWorkerCompletion(QTextStream& err)
{
    State playing;
    playing.currentGeneration = 60;
    playing.activeTransactionId = 601;
    playing.transportAnchorSecond = 6.0;
    playing.effectiveWorkerSecond = 6.0;
    playing.audioPrepared = true;
    playing.uiPlaying = true;

    const State waiting = beginPlayingRetainedSeek(
        playing, Request{61, 601, 1001, 18.0});
    const Decision accepted = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedSeek, 61, 601, 1001, 18.125));
    const Decision failed = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedSeek, 61, 601, 1001, 18.0, false));
    const Decision degraded = decideCompletion(
        waiting,
        completion(CompletionKind::RetainedSeek, 61, 601, 1001, 18.0, true, true));

    bool ok = true;
    ok &= expect(waiting.visualSecond == 18.0,
                 "playing seek publishes the requested visual second immediately", err);
    ok &= expect(waiting.effectiveWorkerSecond == 6.0,
                 "playing seek keeps the prior worker-effective second pending completion", err);
    ok &= expect(waiting.transportAnchorSecond == 6.0,
                 "playing seek keeps the current transport anchor pending completion", err);
    ok &= expect(waiting.audioPrepared && waiting.uiPlaying,
                 "playing seek does not pause the established transport", err);
    ok &= expect(accepted.matchesPending && accepted.commitsWorkerSecond,
                 "current playing seek completion commits the worker second", err);
    ok &= expect(accepted.state.effectiveWorkerSecond == 18.125,
                 "accepted playing seek completion becomes authoritative", err);
    ok &= expect(accepted.state.transportAnchorSecond == 18.125,
                 "accepted playing seek completion reanchors the transport", err);
    ok &= expect(accepted.state.audioPrepared && accepted.state.uiPlaying,
                 "accepted playing seek preserves active transport state", err);
    ok &= expect(failed.matchesPending && !failed.commitsWorkerSecond
                     && failed.state.effectiveWorkerSecond == 6.0
                     && failed.state.transportAnchorSecond == 6.0
                     && failed.state.audioPrepared && failed.state.uiPlaying,
                 "failed playing seek leaves authoritative and transport state unchanged", err);
    ok &= expect(degraded.matchesPending && !degraded.commitsWorkerSecond
                     && degraded.state.effectiveWorkerSecond == 6.0
                     && degraded.state.transportAnchorSecond == 6.0
                     && degraded.state.audioPrepared && degraded.state.uiPlaying,
                 "degraded playing seek leaves authoritative and transport state unchanged", err);
    return ok;
}

bool verifyFailedOrDegradedPrepareLeavesUiPaused(QTextStream& err)
{
    const State waiting = beginColdPrepare(State{}, Request{50, 501, 901, 6.0});
    const Decision failed = decideCompletion(
        waiting,
        completion(CompletionKind::Prepare, 50, 501, 901, 6.0, false));
    const Decision degraded = decideCompletion(
        waiting,
        completion(CompletionKind::Prepare, 50, 501, 901, 6.0, true, true));

    bool ok = true;
    ok &= expect(failed.matchesPending && !failed.commitsAudioPreparation
                     && !failed.state.audioPrepared && !failed.state.uiPlaying,
                 "failed prepare leaves UI paused", err);
    ok &= expect(degraded.matchesPending && !degraded.commitsAudioPreparation
                     && !degraded.state.audioPrepared && !degraded.state.uiPlaying,
                 "degraded prepare leaves UI paused", err);
    return ok;
}

bool verifyPendingInitializersPreserveWorkerSecond(QTextStream& err)
{
    const State startup = beginStartupPlayback(
        State{}, Request{70, 701, 1701, 18.0}, CompletionKind::Prepare, 4.5);
    State playing;
    playing.audioPrepared = true;
    playing.uiPlaying = true;
    const State seek = beginPlayingRetainedSeek(
        playing, Request{71, 702, 1702, 19.0}, 5.5, false);
    const TickDecision pendingTick = decidePlayingTick(seek, 5.75);
    const Decision acceptedSeek = decideCompletion(
        seek,
        completion(CompletionKind::RetainedSeek, 71, 702, 1702, 19.125));
    const Decision duplicateSeek = decideCompletion(
        acceptedSeek.state,
        completion(CompletionKind::RetainedSeek, 71, 702, 1702, 19.125));

    bool ok = true;
    ok &= expect(startup.visualSecond == 18.0,
                 "pending startup keeps the requested visual second", err);
    ok &= expect(startup.effectiveWorkerSecond == 4.5
                     && startup.transportAnchorSecond == 4.5,
                 "pending startup preserves the independent worker-confirmed second", err);
    ok &= expect(startup.currentGeneration == 70
                     && startup.activeTransactionId == 701
                     && startup.pendingPrepareSequence == 1701,
                 "pending startup retains the request identity", err);
    ok &= expect(seek.visualSecond == 19.0,
                 "pending playing seek keeps the requested visual second", err);
    ok &= expect(seek.effectiveWorkerSecond == 5.5
                     && seek.transportAnchorSecond == 5.5,
                 "pending playing seek preserves the independent worker-confirmed second", err);
    ok &= expect(pendingTick.holdsPendingPlayingSeek
                     && pendingTick.visualSecond == 19.0
                     && pendingTick.suppressesRunningClockSideEffects,
                 "pending playing seek holds the requested visual second without running-clock side effects", err);
    ok &= expect(visualSecondAfterStartupCancellation(startup) == 18.0,
                 "cancelling startup preserves its pending visual second", err);
    ok &= expect(seek.currentGeneration == 71
                     && seek.activeTransactionId == 702
                     && seek.pendingPlayingSeekSequence == 1702
                     && !seek.pendingPlayingSeekCenterView
                     && seek.audioPrepared && seek.uiPlaying,
                 "pending playing seek retains identity, centering, and active transport state", err);
    ok &= expect(!duplicateSeek.matchesPending && !duplicateSeek.commitsWorkerSecond
                     && !duplicateSeek.commitsAudioPreparation,
                 "duplicate playing-seek completion is consumed after the first acceptance", err);
    return ok;
}

bool verifyManualPauseCompletionRequiresItsImmutableIdentity(QTextStream& err)
{
    PauseState state;
    const PauseRequest request{
        PauseKind::Manual,
        80,
        801,
        1801,
        0,
        0,
        14.0,
    };
    const PauseState pending = beginManualPause(state, request);
    const PauseDecision stale = decidePauseCompletion(
        pending,
        PauseCompletion{PauseKind::Manual, 79, 801, 1801, 0, 0, 12.0, 1, 1, true, false});
    const PauseDecision accepted = decidePauseCompletion(
        pending,
        PauseCompletion{PauseKind::Manual, 80, 801, 1801, 0, 0, 12.25, 1, 1, true, false});

    bool ok = true;
    ok &= expect(pending.visualSecond == 14.0
                     && pending.pendingManualPauseGeneration == 80
                     && pending.pendingManualPauseTransactionId == 801
                     && pending.pendingManualPauseSequence == 1801,
                 "manual pause freezes the visual second before the worker completion", err);
    ok &= expect(!stale.matchesPending && stale.state.currentGeneration == 80
                     && stale.state.visualSecond == 14.0,
                 "stale manual pause completion is diagnostic-only without a generation change", err);
    ok &= expect(accepted.matchesPending && accepted.commitsRetainedState
                     && accepted.state.pendingManualPauseSequence == 0
                     && accepted.state.visualSecond == 14.0
                     && accepted.state.authoritativeRetainedSecond == 12.25
                     && accepted.state.retainedMode == 1
                     && accepted.state.retainedBgmState == 1,
                 "matching manual pause completion commits retained state without replacing its wall second", err);
    return ok;
}

bool verifyNewerGenerationSupersedesManualPauseCompletion(QTextStream& err)
{
    const PauseState pending = beginManualPause(
        PauseState{},
        PauseRequest{PauseKind::Manual, 81, 811, 1811, 0, 0, 15.0});
    PauseState superseded = pending;
    // A retained reset after reanchoring advances the runtime generation before the
    // asynchronous manual pause completion returns.
    superseded.currentGeneration = 82;
    superseded.authoritativeRetainedSecond = 9.0;
    superseded.retainedMode = 2;
    superseded.retainedBgmState = 2;
    const PauseDecision stale = decidePauseCompletion(
        superseded,
        PauseCompletion{PauseKind::Manual, 81, 811, 1811, 0, 0, 15.25, 1, 1, true, false});

    return expect(!stale.matchesPending && !stale.commitsRetainedState
                      && stale.state.authoritativeRetainedSecond == 9.0
                      && stale.state.retainedMode == 2
                      && stale.state.retainedBgmState == 2,
                  "a newer retained-reset generation makes an old manual pause completion diagnostic-only",
                  err);
}

bool verifyDevicePauseTokenCoalescesAndPlaySupersedesIt(QTextStream& err)
{
    PauseState state;
    const PauseState first = beginDeviceChangePause(
        state,
        PauseRequest{PauseKind::DeviceChange, 90, 901, 1901, 7, 70, 21.0});
    const PauseState duplicate = beginDeviceChangePause(
        first,
        PauseRequest{PauseKind::DeviceChange, 91, 902, 1902, 8, 80, 22.0});
    const PauseState played = supersedePendingPauseForPlay(
        duplicate,
        Request{92, 902, 1903, 22.0});
    const PauseDecision late = decidePauseCompletion(
        played,
        PauseCompletion{PauseKind::DeviceChange, 90, 901, 1901, 8, 70, 21.5, 2, 2, true, false});

    bool ok = true;
    ok &= expect(first.deviceSequence == 7 && first.pendingDevicePauseGeneration == 90
                     && first.pendingDevicePauseTransactionId == 901
                     && first.pendingDevicePauseToken == 70
                     && first.devicePauseVisualSecond == 21.0,
                 "first real device change captures one immutable pause identity", err);
    ok &= expect(duplicate.deviceSequence == 8
                     && duplicate.pendingDevicePauseGeneration == 90
                     && duplicate.pendingDevicePauseTransactionId == 901
                     && duplicate.pendingDevicePauseToken == 70
                     && duplicate.devicePauseVisualSecond == 21.0,
                 "duplicate device change advances sequence but preserves the pending pause token", err);
    ok &= expect(played.currentGeneration == 92
                     && played.pendingDevicePauseToken == 0
                     && played.pendingManualPauseSequence == 0,
                 "play supersedes a pending pause at a strictly higher generation", err);
    ok &= expect(!late.matchesPending && late.state.currentGeneration == 92
                     && late.state.visualSecond == 21.0,
                 "late device pause completion cannot overwrite a newer play transition", err);
    return ok;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;
    ok &= verifyCurrentPrepareCommitsOnlyOnce(err);
    ok &= verifyCurrentRetainedCompletionCommitsOnlyOnce(err);
    ok &= verifyRetainedResumeRejectsEachStaleIdentity(err);
    ok &= verifyRetainedSeekRejectsEachStaleIdentity(err);
    ok &= verifyNewerPlaybackRejectsOldCompletion(err);
    ok &= verifyVisualSecondWaitsForAuthoritativeCompletion(err);
    ok &= verifyPlayingSeekWaitsForAcceptedWorkerCompletion(err);
    ok &= verifyFailedOrDegradedPrepareLeavesUiPaused(err);
    ok &= verifyPendingInitializersPreserveWorkerSecond(err);
    ok &= verifyManualPauseCompletionRequiresItsImmutableIdentity(err);
    ok &= verifyNewerGenerationSupersedesManualPauseCompletion(err);
    ok &= verifyDevicePauseTokenCoalescesAndPlaySupersedesIt(err);
    if (ok) {
        out << "preview_audio_playback_flow_policy_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
