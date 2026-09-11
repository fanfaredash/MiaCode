// Storage-boundary regression for stage 4.9e-4.
//
// 4.9b sliced timeline refresh/snapshot/readiness/cursor/analysis storage out
// of the shared State bag into RuntimeContext::TimelineState. 4.9e-4 does the
// same for canonical playback-authority storage — the nine fields that decide
// "where is the playhead right now / is it playing / at what rate" — into
// RuntimeContext::PlaybackState, owned by PlaybackCoordinator.
//
// This move is stricter than 4.9b's: TimelineState's compatibility aliases in
// State stayed mutable (any cross-domain reader could still write through
// them), because nothing forced write-side enforcement at that stage. Here
// State borrows PlaybackState by const&, so only PlaybackCoordinator (which
// holds a mutable PlaybackState&) can write these fields at all — a stray
// cross-domain write is a compile error, not just a convention.
//
// This is a compile-only spec: it is the independent translation unit that
// actually parses RuntimeContext.h, so the assertions below fail the build
// rather than a text scan if someone re-adds an owning (or mutable) copy of a
// playback field to State, or turns a PlaybackState field back into a
// borrowed reference.

#include "runtime/RuntimeContext.h"

#include <type_traits>
#include <utility>

namespace {

using miacode::runtime::RuntimeContext;

// State borrows the playback record itself, so a later step can hand the
// same storage to a playback-owning host without moving the fields again.
static_assert(
    std::is_same_v<decltype(std::declval<RuntimeContext::State&>().playback_),
                   const RuntimeContext::PlaybackState&>,
    "RuntimeContext::State must borrow PlaybackState by const reference");

#define MIACODE_PLAYBACK_STORAGE_MOVED(member)                                              \
    static_assert(                                                                          \
        std::is_reference_v<decltype(std::declval<RuntimeContext::State&>().member)>         \
            && std::is_const_v<std::remove_reference_t<                                     \
                   decltype(std::declval<RuntimeContext::State&>().member)>>,                \
        "RuntimeContext::State must not own or mutably alias " #member                       \
        "; it belongs to PlaybackState, borrowed read-only");                                \
    static_assert(                                                                           \
        !std::is_reference_v<decltype(std::declval<RuntimeContext::PlaybackState&>().member)>, \
        "RuntimeContext::PlaybackState must own " #member ", not borrow it")

MIACODE_PLAYBACK_STORAGE_MOVED(playing_);
MIACODE_PLAYBACK_STORAGE_MOVED(previewTransportState_);
MIACODE_PLAYBACK_STORAGE_MOVED(pauseSecond_);
MIACODE_PLAYBACK_STORAGE_MOVED(previewPlaybackRate_);
MIACODE_PLAYBACK_STORAGE_MOVED(qtPreviewStartSecond_);
MIACODE_PLAYBACK_STORAGE_MOVED(qtPreviewElapsed_);
MIACODE_PLAYBACK_STORAGE_MOVED(previewStartupSyncPending_);
MIACODE_PLAYBACK_STORAGE_MOVED(previewLateVideoStartPending_);
MIACODE_PLAYBACK_STORAGE_MOVED(previewStartupPreparedSecond_);

#undef MIACODE_PLAYBACK_STORAGE_MOVED

}  // namespace

int main()
{
    return 0;
}
