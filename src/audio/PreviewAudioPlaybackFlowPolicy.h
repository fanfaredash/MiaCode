#pragma once

#include <QtGlobal>

// GUI-side acceptance policy for the preview audio startup handshake. It owns no
// QObject state and intentionally models only the identity and state transition.
namespace miacode::preview_audio::playback_flow {

enum class CompletionKind {
    Prepare,
    RetainedResume,
    RetainedSeek,
};

struct Request {
    quint64 generation = 0;
    quint64 transactionId = 0;
    quint64 sequence = 0;
    double requestedVisualSecond = 0.0;
};

struct Completion {
    CompletionKind kind = CompletionKind::Prepare;
    quint64 generation = 0;
    quint64 transactionId = 0;
    quint64 sequence = 0;
    double effectiveSecond = 0.0;
    bool success = false;
    bool degraded = false;
};

struct State {
    quint64 currentGeneration = 0;
    quint64 activeTransactionId = 0;
    quint64 pendingPrepareSequence = 0;
    quint64 pendingRetainedSequence = 0;
    quint64 pendingPlayingSeekSequence = 0;
    double visualSecond = 0.0;
    double requestedVisualSecond = 0.0;
    double effectiveWorkerSecond = 0.0;
    double transportAnchorSecond = 0.0;
    bool audioPrepared = false;
    bool uiPlaying = false;
    bool pendingPlayingSeekCenterView = false;
};

struct Decision {
    State state;
    bool matchesPending = false;
    bool commitsWorkerSecond = false;
    bool commitsAudioPreparation = false;
};

struct TickDecision {
    double visualSecond = 0.0;
    bool holdsPendingPlayingSeek = false;
    bool suppressesRunningClockSideEffects = false;
};

inline State beginColdPrepare(State state, const Request& request)
{
    state.currentGeneration = request.generation;
    state.activeTransactionId = request.transactionId;
    state.pendingPrepareSequence = request.sequence;
    state.pendingRetainedSequence = 0;
    state.pendingPlayingSeekSequence = 0;
    state.visualSecond = request.requestedVisualSecond;
    state.requestedVisualSecond = request.requestedVisualSecond;
    state.audioPrepared = false;
    state.uiPlaying = false;
    state.pendingPlayingSeekCenterView = false;
    return state;
}

inline State beginRetainedPlayback(State state, const Request& request, CompletionKind kind)
{
    (void)kind;
    state.currentGeneration = request.generation;
    state.activeTransactionId = request.transactionId;
    state.pendingPrepareSequence = 0;
    state.pendingRetainedSequence = request.sequence;
    state.pendingPlayingSeekSequence = 0;
    state.visualSecond = request.requestedVisualSecond;
    state.requestedVisualSecond = request.requestedVisualSecond;
    state.audioPrepared = false;
    state.uiPlaying = false;
    state.pendingPlayingSeekCenterView = false;
    return state;
}

inline State beginStartupPlayback(
    State state,
    const Request& request,
    CompletionKind kind,
    double workerConfirmedSecond)
{
    state = kind == CompletionKind::Prepare
        ? beginColdPrepare(state, request)
        : beginRetainedPlayback(state, request, kind);
    state.effectiveWorkerSecond = workerConfirmedSecond;
    state.transportAnchorSecond = workerConfirmedSecond;
    return state;
}

inline State beginPlayingRetainedSeek(State state, const Request& request)
{
    state.currentGeneration = request.generation;
    state.activeTransactionId = request.transactionId;
    state.pendingPrepareSequence = 0;
    state.pendingRetainedSequence = 0;
    state.pendingPlayingSeekSequence = request.sequence;
    state.visualSecond = request.requestedVisualSecond;
    state.requestedVisualSecond = request.requestedVisualSecond;
    state.pendingPlayingSeekCenterView = false;
    return state;
}

inline State beginPlayingRetainedSeek(
    State state,
    const Request& request,
    double workerConfirmedSecond)
{
    state = beginPlayingRetainedSeek(state, request);
    state.effectiveWorkerSecond = workerConfirmedSecond;
    state.transportAnchorSecond = workerConfirmedSecond;
    return state;
}

inline State beginPlayingRetainedSeek(
    State state,
    const Request& request,
    double workerConfirmedSecond,
    bool centerView)
{
    state = beginPlayingRetainedSeek(state, request, workerConfirmedSecond);
    state.pendingPlayingSeekCenterView = centerView;
    return state;
}

inline TickDecision decidePlayingTick(const State& state, double runningSecond)
{
    if (state.pendingPlayingSeekSequence != 0) {
        return TickDecision{
            state.visualSecond,
            true,
            true,
        };
    }
    return TickDecision{runningSecond, false, false};
}

inline double visualSecondAfterStartupCancellation(const State& state)
{
    return state.visualSecond;
}

inline Decision decideCompletion(State state, const Completion& completion)
{
    Decision decision;
    decision.state = state;

    quint64 expectedSequence = 0;
    if (completion.kind == CompletionKind::Prepare) {
        expectedSequence = state.pendingPrepareSequence;
    } else if (completion.kind == CompletionKind::RetainedSeek
               && state.pendingPlayingSeekSequence != 0) {
        expectedSequence = state.pendingPlayingSeekSequence;
    } else {
        expectedSequence = state.pendingRetainedSequence;
    }
    decision.matchesPending = expectedSequence != 0
        && completion.generation == state.currentGeneration
        && completion.transactionId == state.activeTransactionId
        && completion.sequence == expectedSequence;
    decision.commitsWorkerSecond = decision.matchesPending
        && completion.success
        && !completion.degraded;
    if (!decision.commitsWorkerSecond) {
        return decision;
    }

    if (completion.kind == CompletionKind::Prepare) {
        decision.state.pendingPrepareSequence = 0;
    } else if (completion.kind == CompletionKind::RetainedSeek
               && state.pendingPlayingSeekSequence != 0) {
        decision.state.pendingPlayingSeekSequence = 0;
    } else {
        decision.state.pendingRetainedSequence = 0;
    }
    decision.state.effectiveWorkerSecond = completion.effectiveSecond;
    decision.state.transportAnchorSecond = completion.effectiveSecond;
    if (state.pendingPlayingSeekSequence == 0) {
        decision.commitsAudioPreparation = true;
        decision.state.audioPrepared = true;
        decision.state.uiPlaying = false;
    }
    return decision;
}

}  // namespace miacode::preview_audio::playback_flow
