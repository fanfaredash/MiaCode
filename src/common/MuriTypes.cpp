#include "common/MuriTypes.h"

#include "timeline/TimelineData.h"

QString makeMarkerAnalysisKey(const TimelineNoteMarker& marker)
{
    QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                      .arg(marker.type)
                      .arg(marker.second, 0, 'f', 6)
                      .arg(marker.lane)
                      .arg(marker.endLane)
                      .arg(marker.sourceLine)
                      .arg(marker.sourceCol)
                      .arg(marker.slideTrackKey);
    if (!marker.slideSegmentKeys.isEmpty()) {
        key += QStringLiteral("|%1").arg(marker.slideSegmentKeys.join(QLatin1Char('/')));
    }
    return key;
}

QString muriKindDisplayName(MuriKind kind, bool chineseUi)
{
    switch (kind) {
    case MuriKind::SlideTooFast:
        return chineseUi ? QStringLiteral("内无") : QStringLiteral("Inner");
    case MuriKind::SlideHeadTap:
        return chineseUi ? QStringLiteral("外无") : QStringLiteral("Outer");
    case MuriKind::TapOnSlide:
        return chineseUi ? QStringLiteral("撞尾") : QStringLiteral("Tail");
    case MuriKind::Overlap:
        return chineseUi ? QStringLiteral("叠键") : QStringLiteral("Overlap");
    case MuriKind::MultiTouch:
        return chineseUi ? QStringLiteral("多押") : QStringLiteral("Multi-touch");
    }
    return chineseUi ? QStringLiteral("无理") : QStringLiteral("Muri");
}

QString muriAlertLevelDisplayName(MuriAlertLevel level, bool chineseUi)
{
    switch (level) {
    case MuriAlertLevel::Muri:
        return chineseUi ? QStringLiteral("无理") : QStringLiteral("Muri");
    case MuriAlertLevel::Warning:
        return chineseUi ? QStringLiteral("警告") : QStringLiteral("Warning");
    }
    return chineseUi ? QStringLiteral("无理") : QStringLiteral("Muri");
}
