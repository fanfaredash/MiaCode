// Storage-boundary regression for stage 4.9b.
//
// 4.9a externalized the transitional Ui/State records out of Session. 4.9b
// starts slicing that shared bag per domain: timeline refresh, snapshot,
// readiness, cursor and analysis-scheduling storage is owned by
// RuntimeContext::TimelineState, and the legacy State record only keeps
// migration-period references into it.
//
// This is a compile-only spec: it is the independent translation unit that
// actually parses RuntimeContext.h, so the assertions below fail the build
// rather than a text scan if someone re-adds an owning copy of a timeline
// field to State (which would silently diverge from TimelineState) or turns a
// TimelineState field back into a borrowed reference.

#include "runtime/RuntimeContext.h"

#include <type_traits>
#include <utility>

namespace {

using miacode::runtime::RuntimeContext;

// State borrows the timeline record itself, so a later step can hand the same
// storage to a timeline-owning host without moving the fields again.
static_assert(
    std::is_same_v<decltype(std::declval<RuntimeContext::State&>().timeline_),
                   RuntimeContext::TimelineState&>,
    "RuntimeContext::State must borrow the TimelineState record by reference");

#define MIACODE_TIMELINE_STORAGE_MOVED(member)                                             \
    static_assert(                                                                         \
        std::is_reference_v<decltype(std::declval<RuntimeContext::State&>().member)>,       \
        "RuntimeContext::State must not own " #member "; it belongs to TimelineState");     \
    static_assert(                                                                         \
        !std::is_reference_v<decltype(std::declval<RuntimeContext::TimelineState&>().member)>, \
        "RuntimeContext::TimelineState must own " #member ", not borrow it")

MIACODE_TIMELINE_STORAGE_MOVED(timelineQuickStateBridge_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineSlowRefreshPool_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineAnalysisPool_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineReady_);
MIACODE_TIMELINE_STORAGE_MOVED(deferredTimelineBridgeFlushGeneration_);
MIACODE_TIMELINE_STORAGE_MOVED(pendingQuickTimelineCursorSync_);
MIACODE_TIMELINE_STORAGE_MOVED(pendingQuickTimelineCursorSecond_);
MIACODE_TIMELINE_STORAGE_MOVED(pendingQuickTimelineCursorCenterView_);
MIACODE_TIMELINE_STORAGE_MOVED(lastTimelineParseDifficultyId_);
MIACODE_TIMELINE_STORAGE_MOVED(lastTimelineParseChartText_);
MIACODE_TIMELINE_STORAGE_MOVED(lastTimelineParseTimingMetadata_);
MIACODE_TIMELINE_STORAGE_MOVED(lastTimelineParseResult_);
MIACODE_TIMELINE_STORAGE_MOVED(latestTimelineNoteMarkers_);
MIACODE_TIMELINE_STORAGE_MOVED(latestTimelineNoteMarkerSignature_);
MIACODE_TIMELINE_STORAGE_MOVED(latestTimelinePreviewRevision_);
MIACODE_TIMELINE_STORAGE_MOVED(latestTimelinePreviewSnapshotReady_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineQuickModel_);
MIACODE_TIMELINE_STORAGE_MOVED(pendingTimelineSlowRefresh_);
MIACODE_TIMELINE_STORAGE_MOVED(pendingTimelineAnalysisRefresh_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineRevision_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineSlowRequestedRevision_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineSlowRunningRevision_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineAnalysisRequestedRevision_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineAnalysisRunningRevision_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineSlowWorkerRunning_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineAnalysisWorkerRunning_);
MIACODE_TIMELINE_STORAGE_MOVED(waveformRefreshGeneration_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineFrameRateMode_);
MIACODE_TIMELINE_STORAGE_MOVED(timelineSyncEnabled_);

#undef MIACODE_TIMELINE_STORAGE_MOVED

}  // namespace

int main()
{
    return 0;
}
