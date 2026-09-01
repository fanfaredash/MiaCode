#pragma once

// Shared file-local helpers for the Session timeline-playback partial-class
// slices, split out of MainWindow.TimelinePlayback.cpp (god-file decomposition).
// These are the original file-scope constants + stateless logging wrappers +
// the visual lead-in helper, relocated verbatim into a named `detail` namespace
// so the split translation units (PreviewSeek / PreviewPlaybackState /
// PreviewIntroRegion / PreviewTick + the core TU) can share them without
// re-declaration. Behaviour is identical to the former anonymous namespace.

#include <QString>
#include <QVector>

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "core/scene/PreviewOpacityCurves.h"
#include "timeline/TimelineData.h"

namespace miacode::runtime::playback_detail {

inline constexpr double kTimelineZeroSecondTolerance = 1e-6;
inline constexpr auto kQuickShellTransportSeekProperty = "miacode.quick_shell_transport_seek";
inline constexpr int kPreviewPlaybackRateToastMinWidth = 196;
inline constexpr int kPreviewPlaybackRateToastMinHeight = 96;
inline constexpr int kPreviewPlaybackRateToastHorizontalMargin = 20;

inline void appendQuickShellBackendLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/backend"),
        text
    );
}

inline void appendPreviewPlaybackLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QStringLiteral("preview/playback"),
        text
    );
}

inline void appendPreviewInteractionLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/interaction"),
        text
    );
}

inline void appendPreviewFramePacingDiagLog(const QString& action, const QString& payload = QString())
{
    if (!miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        return;
    }
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/frame_pacing"),
        text,
        true
    );
}

inline double previewVisualLeadInStartSecond(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double requestedSecond,
    double tapFlowSpeed,
    double touchFlowSpeed)
{
    if (requestedSecond > kTimelineZeroSecondTolerance || noteMarkers.isEmpty()) {
        return requestedSecond;
    }

    const auto tapTiming = miacode::preview::scene::previewTapTimingForFlowSpeed(static_cast<qreal>(tapFlowSpeed));
    const auto slideTrackTiming =
        miacode::preview::scene::previewSlideTrackTimingForFlowSpeed(static_cast<qreal>(tapFlowSpeed));
    const auto touchTiming =
        miacode::preview::scene::previewTouchTimingForFlowSpeed(static_cast<qreal>(touchFlowSpeed));

    double earliestSecond = requestedSecond;
    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString type = marker.type.toLower();
        if (type == QLatin1String("tap")) {
            if (marker.second + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - tapTiming.lifecycleDurationSeconds);
            }
            continue;
        }
        if (type == QLatin1String("hold")) {
            if (qMax(marker.second, marker.endSecond) + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - tapTiming.lifecycleDurationSeconds);
            }
            continue;
        }
        if (type == QLatin1String("slide") || type == QLatin1String("wifi")) {
            if (marker.second + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - tapTiming.lifecycleDurationSeconds);
            }
            if (qMax(marker.second, qMax(marker.endSecond, marker.slideTraceSecond)) + kTimelineZeroSecondTolerance
                >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - slideTrackTiming.appearLeadInSeconds);
            }
            continue;
        }
        if (type == QLatin1String("touch")) {
            if (marker.second + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - touchTiming.durationSeconds);
            }
            continue;
        }
        if (type == QLatin1String("touch_hold")) {
            if (qMax(marker.second, marker.endSecond) + kTimelineZeroSecondTolerance >= requestedSecond) {
                earliestSecond = qMin(earliestSecond, marker.second - touchTiming.durationSeconds);
            }
            continue;
        }
    }

    return earliestSecond;
}

}  // namespace miacode::runtime::playback_detail
