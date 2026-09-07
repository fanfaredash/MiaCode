#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Shared.h"
#include "app/v2/ApplicationServices.h"

#include "common/ContentDurationConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QtCore>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace miacode::runtime::shared;

double miacode::runtime::PlaybackCoordinator::previewDurationSeconds() const
{
    // Unified content-duration policy = max(chartEnd + tail, music) — see
    // common/ContentDurationConfig.h. The chart end is the timeline bridge's
    // durationSeconds (last note/beat/measure); the runtime cursors below are
    // maxed in WITHOUT the tail so the range merely covers an active playhead.
    double chartEndSeconds = 0.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        chartEndSeconds = qMax(chartEndSeconds, state_.timelineQuickStateBridge_->durationSeconds());
    }
    double duration = miacode::content_duration::totalContentDurationSeconds(
        chartEndSeconds, state_.previewTrackDurationSeconds_);
    if (state_.playing_ && state_.qtPreviewPlaybackEndSecond_ > 0.0) {
        duration = qMax(duration, state_.qtPreviewPlaybackEndSecond_);
    }
    if (state_.timelineQuickStateBridge_ != nullptr) {
        duration = qMax(duration, state_.timelineQuickStateBridge_->playheadSeconds());
        duration = qMax(duration, state_.timelineQuickStateBridge_->playbackEntrySeconds());
    }
    duration = qMax(duration, qMax(0.0, state_.pauseSecond_));
    return qMax(0.0, duration);
}

double miacode::runtime::PlaybackCoordinator::previewPlaybackEndSeconds() const
{
    if (state_.playing_ && state_.qtPreviewPlaybackEndSecond_ > 0.0) {
        return qMax(0.0, state_.qtPreviewPlaybackEndSecond_);
    }
    // Same unified content-duration policy as previewDurationSeconds(), so
    // playback auto-stops exactly where the slider/total duration ends.
    double chartEndSeconds = 0.0;
    if (state_.timelineQuickStateBridge_ != nullptr) {
        chartEndSeconds = qMax(chartEndSeconds, state_.timelineQuickStateBridge_->durationSeconds());
    }
    return miacode::content_duration::totalContentDurationSeconds(
        chartEndSeconds, state_.previewTrackDurationSeconds_);
}

void miacode::runtime::PlaybackCoordinator::publishPreviewPlayhead()
{
    emit services_.shellNotifications().previewPlayheadChanged();
}

void miacode::runtime::PlaybackCoordinator::refreshPreviewObjectStatsTotals(const QVector<TimelineNoteMarker>& noteMarkers)
{
    auto cache = std::make_shared<miacode::preview::scene::PreviewProgressStatsCache>();
    cache->rebuild(noteMarkers);
    state_.previewProgressStatsCache_ = cache;
    if (state_.scene_ != nullptr) {
        state_.scene_->setProgressStatsCache(state_.previewProgressStatsCache_);
    }
    updatePreviewObjectStats(state_.pauseSecond_);
}

void miacode::runtime::PlaybackCoordinator::clearPreviewObjectStats()
{
    state_.previewProgressStatsCache_.reset();
    if (state_.scene_ != nullptr) {
        state_.scene_->setProgressStatsCache(state_.previewProgressStatsCache_);
    }
    updatePreviewObjectStats(0.0);
}

int miacode::runtime::PlaybackCoordinator::updatePreviewStatsLayoutMode(int hostWidth)
{
    // previewStatsGridLayout_ / previewStatsChips_ never exist on this side
    // of the QML migration.
    Q_UNUSED(hostWidth);
    return 0;
}

int miacode::runtime::PlaybackCoordinator::previewStatsMinimumHeightForPanelWidth(int panelWidth) const
{
    const int statsHostWidth = qMax(0, panelWidth - kPreviewPanelMarginX * 2 - 16);
    return miacode::window_parity::computePreviewStatsLayout(statsHostWidth).minCardHeight;
}

double miacode::runtime::PlaybackCoordinator::normalizedPreviewCanvasAspectRatio(double ratio) const
{
    if (!qIsFinite(ratio) || ratio <= 0.0) {
        return 1.0;
    }
    return qBound(1.0, ratio, 3.0);
}

void miacode::runtime::PlaybackCoordinator::setPreviewCanvasAspectRatio(double ratio, bool persistState)
{
    const double normalized = normalizedPreviewCanvasAspectRatio(ratio);
    if (qAbs(state_.previewCanvasAspectRatio_ - normalized) <= 1e-6) {
        return;
    }
    state_.previewCanvasAspectRatio_ = normalized;
    refreshQuickShellPreviewCompositeSurfaceState(state_, owner_);
    if (persistState) {
        preferences_.savePortableState();
    }
}

void miacode::runtime::PlaybackCoordinator::setWorkspacePanelsSwapped(bool swapped, bool persistState)
{
    if (state_.workspacePanelsSwapped_ == swapped) {
        return;
    }

    state_.workspacePanelsSwapped_ = swapped;
    if (persistState) {
        preferences_.savePortableState();
    }
}

void miacode::runtime::PlaybackCoordinator::updatePreviewObjectStats(double second)
{
    Q_UNUSED(second);
}
