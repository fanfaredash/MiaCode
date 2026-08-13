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

enum class PauseKind {
    Manual,
    DeviceChange,
};

// Pause work has an immediate visual transition but an asynchronous backend result.
// The GUI accepts that result only when all immutable request identity fields still match.
struct PauseRequest {
    PauseKind kind = PauseKind::Manual;
    quint64 generation = 0;
    quint64 transactionId = 0;
    quint64 sequence = 0;
    quint64 deviceSequence = 0;
    quint64 pauseToken = 0;
    double wallSecond = 0.0;
};

struct PauseCompletion {
    PauseKind kind = PauseKind::Manual;
    quint64 generation = 0;
    quint64 transactionId = 0;
    quint64 sequence = 0;
    quint64 deviceSequence = 0;
    quint64 pauseToken = 0;
    double acceptedPauseSecond = 0.0;
    int retainedMode = 0;
    int retainedBgmState = 0;
    bool success = false;
    bool degraded = false;
};

struct PauseState {
    quint64 currentGeneration = 0;
    quint64 deviceSequence = 0;

    quint64 pendingManualPauseGeneration = 0;
    quint64 pendingManualPauseTransactionId = 0;
    quint64 pendingManualPauseSequence = 0;
    double manualPauseVisualSecond = 0.0;

    quint64 pendingDevicePauseGeneration = 0;
    quint64 pendingDevicePauseTransactionId = 0;
    quint64 pendingDevicePauseSequence = 0;
    quint64 pendingDevicePauseToken = 0;
    double devicePauseVisualSecond = 0.0;

    double visualSecond = 0.0;
    double authoritativeRetainedSecond = 0.0;
    int retainedMode = 0;
    int retainedBgmState = 0;
};

// A rejected completion must explain which immutable part of the GUI-side
// request identity no longer matches. This is diagnostic state only: it never
// changes the acceptance rules below.
enum class PauseCompletionRejection {
    None,
    NoPendingRequest,
    GenerationMismatch,
    TransactionMismatch,
    SequenceMismatch,
    PauseTokenMismatch,
};

inline const char* pauseCompletionRejectionLabel(PauseCompletionRejection rejection)
{
    switch (rejection) {
    case PauseCompletionRejection::None:
        return "none";
    case PauseCompletionRejection::NoPendingRequest:
        return "no_pending_request";
    case PauseCompletionRejection::GenerationMismatch:
        return "generation_mismatch";
    case PauseCompletionRejection::TransactionMismatch:
        return "transaction_mismatch";
    case PauseCompletionRejection::SequenceMismatch:
        return "sequence_mismatch";
    case PauseCompletionRejection::PauseTokenMismatch:
        return "pause_token_mismatch";
    }
    return "unknown";
}

struct PauseDecision {
    PauseState state;
    bool matchesPending = false;
    bool commitsRetainedState = false;
    PauseCompletionRejection rejectionReason = PauseCompletionRejection::None;
};

inline PauseState beginManualPause(PauseState state, const PauseRequest& request)
{
    state.currentGeneration = request.generation;
    state.pendingManualPauseGeneration = request.generation;
    state.pendingManualPauseTransactionId = request.transactionId;
    state.pendingManualPauseSequence = request.sequence;
    state.manualPauseVisualSecond = request.wallSecond;
    state.visualSecond = request.wallSecond;
    return state;
}

inline PauseState beginDeviceChangePause(PauseState state, const PauseRequest& request)
{
    state.deviceSequence = request.deviceSequence;
    if (state.pendingDevicePauseToken != 0) {
        return state;
    }
    state.currentGeneration = request.generation;
    state.pendingDevicePauseGeneration = request.generation;
    state.pendingDevicePauseTransactionId = request.transactionId;
    state.pendingDevicePauseSequence = request.sequence;
    state.pendingDevicePauseToken = request.pauseToken;
    state.devicePauseVisualSecond = request.wallSecond;
    state.visualSecond = request.wallSecond;
    return state;
}

inline PauseState supersedePendingPauseForPlay(PauseState state, const Request& request)
{
    state.currentGeneration = request.generation;
    state.pendingManualPauseGeneration = 0;
    state.pendingManualPauseTransactionId = 0;
    state.pendingManualPauseSequence = 0;
    state.pendingDevicePauseGeneration = 0;
    state.pendingDevicePauseTransactionId = 0;
    state.pendingDevicePauseSequence = 0;
    state.pendingDevicePauseToken = 0;
    return state;
}

inline PauseDecision decidePauseCompletion(PauseState state, const PauseCompletion& completion)
{
    PauseDecision decision;
    decision.state = state;
    if (completion.kind == PauseKind::Manual) {
        if (state.pendingManualPauseSequence == 0) {
            decision.rejectionReason = PauseCompletionRejection::NoPendingRequest;
            return decision;
        }
        if (completion.generation != state.currentGeneration
            || completion.generation != state.pendingManualPauseGeneration) {
            decision.rejectionReason = PauseCompletionRejection::GenerationMismatch;
            return decision;
        }
        if (completion.transactionId != state.pendingManualPauseTransactionId) {
            decision.rejectionReason = PauseCompletionRejection::TransactionMismatch;
            return decision;
        }
        if (completion.sequence != state.pendingManualPauseSequence) {
            decision.rejectionReason = PauseCompletionRejection::SequenceMismatch;
            return decision;
        }
        decision.matchesPending = true;
        decision.state.pendingManualPauseGeneration = 0;
        decision.state.pendingManualPauseTransactionId = 0;
        decision.state.pendingManualPauseSequence = 0;
        decision.commitsRetainedState = completion.success && !completion.degraded;
        if (decision.commitsRetainedState) {
            decision.state.authoritativeRetainedSecond = completion.acceptedPauseSecond;
            decision.state.retainedMode = completion.retainedMode;
            decision.state.retainedBgmState = completion.retainedBgmState;
        }
        return decision;
    }

    if (state.pendingDevicePauseToken == 0) {
        decision.rejectionReason = PauseCompletionRejection::NoPendingRequest;
        return decision;
    }
    if (completion.generation != state.pendingDevicePauseGeneration) {
        decision.rejectionReason = PauseCompletionRejection::GenerationMismatch;
        return decision;
    }
    if (completion.transactionId != state.pendingDevicePauseTransactionId) {
        decision.rejectionReason = PauseCompletionRejection::TransactionMismatch;
        return decision;
    }
    if (completion.sequence != state.pendingDevicePauseSequence) {
        decision.rejectionReason = PauseCompletionRejection::SequenceMismatch;
        return decision;
    }
    if (completion.pauseToken != state.pendingDevicePauseToken) {
        decision.rejectionReason = PauseCompletionRejection::PauseTokenMismatch;
        return decision;
    }
    decision.matchesPending = true;
    if (decision.matchesPending) {
        decision.state.pendingDevicePauseGeneration = 0;
        decision.state.pendingDevicePauseTransactionId = 0;
        decision.state.pendingDevicePauseSequence = 0;
        decision.state.pendingDevicePauseToken = 0;
    }
    return decision;
}

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
