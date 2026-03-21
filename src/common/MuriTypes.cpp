#include "common/MuriTypes.h"

#include "TimelineView.h"

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
        return chineseUi ? QStringLiteral("内屏无理") : QStringLiteral("Slide Too Fast");
    case MuriKind::SlideHeadTap:
        return chineseUi ? QStringLiteral("外键无理") : QStringLiteral("Slide Head Tap");
    case MuriKind::TapOnSlide:
        return chineseUi ? QStringLiteral("撞尾无理") : QStringLiteral("Tap On Slide");
    case MuriKind::Overlap:
        return chineseUi ? QStringLiteral("叠键无理") : QStringLiteral("Overlap");
    case MuriKind::MultiTouch:
        return chineseUi ? QStringLiteral("多押无理") : QStringLiteral("Multi Touch");
    }
    return chineseUi ? QStringLiteral("无理") : QStringLiteral("Muri");
}
