#include <QTextStream>

#include "VideoExportController.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

TimelineNoteMarker makeMarker(const QString& type)
{
    TimelineNoteMarker marker;
    marker.type = type;
    marker.second = 0.0;
    return marker;
}

TimelineNoteMarker makeSlide(int segmentCount = 1)
{
    TimelineNoteMarker marker = makeMarker(QStringLiteral("slide"));
    for (int i = 0; i < qMax(1, segmentCount); ++i) {
        marker.slideSegmentKeys.append(QStringLiteral("segment_%1").arg(i));
    }
    return marker;
}

bool expectDetectedMode(
    const QVector<TimelineNoteMarker>& markers,
    const QString& expectedMode,
    const QString& message,
    QTextStream& err)
{
    return require(
        detectedIntroBannerMode(markers) == expectedMode,
        message,
        err);
}

bool verifyIntroModeDetection(QTextStream& err)
{
    if (!expectDetectedMode({}, QStringLiteral("Standard"), QStringLiteral("empty chart should be SD"), err)) {
        return false;
    }

    TimelineNoteMarker tap = makeMarker(QStringLiteral("tap"));
    if (!expectDetectedMode({tap}, QStringLiteral("Standard"), QStringLiteral("plain tap should stay SD"), err)) {
        return false;
    }

    tap.isBreak = true;
    if (!expectDetectedMode({tap}, QStringLiteral("Standard"), QStringLiteral("break tap alone should stay SD"), err)) {
        return false;
    }

    if (!expectDetectedMode({makeMarker(QStringLiteral("touch"))}, QStringLiteral("DX"), QStringLiteral("touch should be DX"), err)) {
        return false;
    }
    if (!expectDetectedMode({makeMarker(QStringLiteral("touch_hold"))}, QStringLiteral("DX"), QStringLiteral("touchhold should be DX"), err)) {
        return false;
    }

    TimelineNoteMarker hold = makeMarker(QStringLiteral("hold"));
    hold.isBreak = true;
    if (!expectDetectedMode({hold}, QStringLiteral("DX"), QStringLiteral("break hold should be DX"), err)) {
        return false;
    }

    TimelineNoteMarker trackBreakSlide = makeSlide();
    trackBreakSlide.trackBreak = true;
    if (!expectDetectedMode({trackBreakSlide}, QStringLiteral("DX"), QStringLiteral("track-break slide should be DX"), err)) {
        return false;
    }

    if (!expectDetectedMode({makeSlide(2)}, QStringLiteral("DX"), QStringLiteral("multi-segment slide should be DX"), err)) {
        return false;
    }

    TimelineNoteMarker protectedTap = makeMarker(QStringLiteral("tap"));
    protectedTap.isEx = true;
    if (!expectDetectedMode({protectedTap}, QStringLiteral("DX"), QStringLiteral("protected x target should be DX"), err)) {
        return false;
    }

    TimelineNoteMarker protectedStar = makeSlide();
    protectedStar.headEx = true;
    if (!expectDetectedMode({protectedStar}, QStringLiteral("DX"), QStringLiteral("protected x star head should be DX"), err)) {
        return false;
    }

    return true;
}

bool verifyAutoCopyKeepsDetectedMode(QTextStream& err)
{
    IntroBannerSpec from;
    IntroBannerSpec to;

    from.mode = QStringLiteral("auto");
    to.mode = QStringLiteral("Standard");
    copyIntroStyling(from, &to);
    if (!require(to.mode == QStringLiteral("Standard"), QStringLiteral("auto copy should keep detected SD"), err)) {
        return false;
    }

    from.mode = QStringLiteral("DX");
    to.mode = QStringLiteral("Standard");
    copyIntroStyling(from, &to);
    if (!require(to.mode == QStringLiteral("DX"), QStringLiteral("manual DX should override detected SD"), err)) {
        return false;
    }

    from.mode = QStringLiteral("Standard");
    to.mode = QStringLiteral("DX");
    copyIntroStyling(from, &to);
    if (!require(to.mode == QStringLiteral("Standard"), QStringLiteral("manual SD should override detected DX"), err)) {
        return false;
    }

    return true;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    if (!verifyIntroModeDetection(err)) {
        return 1;
    }
    if (!verifyAutoCopyKeepsDetectedMode(err)) {
        return 1;
    }

    QTextStream out(stdout);
    out << "video_export_intro_mode_spec ok" << Qt::endl;
    return 0;
}
