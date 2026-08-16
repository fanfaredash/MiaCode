#include "VideoExportMediaTimeline.h"

#include "common/PreviewSfxTimeline.h"

#include <QString>

#include <algorithm>

namespace miacode::video_export {

namespace {

// Same tolerance the rest of the export timeline math uses when deciding
// whether a second-valued offset is meaningfully non-zero.
constexpr double kEpsilonSeconds = miacode::preview_sfx_timeline::kTimelineEpsilonSeconds;

QString secondsText(double seconds)
{
    return QString::number(seconds, 'f', 6);
}

}  // namespace

QStringList buildMediaTimelineFilters(const MediaTimelinePlan& plan)
{
    QStringList filters;
    if (plan.mediaIsImage) {
        return filters;
    }

    const double totalSeconds = std::max(0.0, plan.alignedTotalSeconds);
    const double leadInSeconds = std::max(0.0, plan.leadInSeconds);
    const bool freezeLeadIn = plan.partialRangeExport && leadInSeconds > kEpsilonSeconds;

    if (freezeLeadIn) {
        // Segment start in media time. The partial-range origin is
        // `segmentStart - leadIn`, so adding the lead-in back recovers the
        // exact second the chart resumes from — the frame we hold during the
        // pause. Clamped at 0 for segments that start inside the lead-in.
        const double sourceStartSecond = std::max(0.0, plan.timelineOriginSecond + leadInSeconds);
        const double playSeconds = std::max(0.0, totalSeconds - leadInSeconds);
        filters << QStringLiteral("trim=start=%1:end=%2")
                       .arg(secondsText(sourceStartSecond))
                       .arg(secondsText(sourceStartSecond + playSeconds))
                << QStringLiteral("setpts=PTS-STARTPTS")
                // Clone the segment-start frame backwards across the freeze
                // window: the PV is stationary under the pause glyph and
                // resumes from that same frame the instant playback starts.
                << QStringLiteral("tpad=start_mode=clone:start_duration=%1")
                       .arg(secondsText(leadInSeconds));
    } else if (plan.timelineOriginSecond > kEpsilonSeconds) {
        filters << QStringLiteral("trim=start=%1:end=%2")
                       .arg(secondsText(plan.timelineOriginSecond))
                       .arg(secondsText(plan.timelineOriginSecond + totalSeconds))
                << QStringLiteral("setpts=PTS-STARTPTS");
    } else if (plan.timelineOriginSecond < -kEpsilonSeconds) {
        filters << QStringLiteral("trim=start=0:end=%1")
                       .arg(secondsText(std::max(0.0, totalSeconds + plan.timelineOriginSecond)))
                << QStringLiteral("setpts=PTS-STARTPTS+%1/TB")
                       .arg(secondsText(-plan.timelineOriginSecond));
    }

    filters << QStringLiteral("tpad=stop_mode=clone:stop_duration=%1").arg(secondsText(totalSeconds));
    return filters;
}

}  // namespace miacode::video_export
