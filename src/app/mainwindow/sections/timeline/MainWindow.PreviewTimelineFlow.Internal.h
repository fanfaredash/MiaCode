#pragma once

// Shared file-local helpers for the MainWindow preview-timeline-flow partial-class
// slices, split out of MainWindow.PreviewTimelineFlow.cpp (god-file decomposition).
// Only the stateless helpers used by MORE THAN ONE of the resulting translation
// units live here, relocated verbatim into a named `detail` namespace so the split
// TUs (TimelineQuickParse / TimelineAnalysisFlow / TimelinePreviewFollowSync + the
// core TU) can share them without re-declaration. Behaviour is identical to the
// former anonymous-namespace definitions.
//
// NOTE: a sibling split owns MainWindow.TimelinePlayback.Internal.h for a DIFFERENT
// file; this header name is unique to MainWindow.PreviewTimelineFlow.cpp.

#include <QString>

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

namespace miacode::mainwindow::preview_timeline_flow_detail {

inline void appendTimelinePerfLog(const QString& tag, const QString& payload)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        tag,
        payload,
        true
    );
}

}  // namespace miacode::mainwindow::preview_timeline_flow_detail
