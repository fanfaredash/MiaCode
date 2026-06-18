#include "core/scene/PreviewHudState.h"

#include "UiText.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHash>
#include <QJsonObject>
#include <QtMath>

namespace {

QString slideHeadEventKey(const TimelineNoteMarker& marker)
{
    return QStringLiteral("slide_head_star|%1|%2|%3|%4|%5")
        .arg(marker.second, 0, 'f', 6)
        .arg(marker.lane)
        .arg(marker.sourceLine)
        .arg(marker.sourceCol)
        .arg(marker.eachGroupId);
}

double slideJudgeSecondFor(const TimelineNoteMarker& marker)
{
    if (marker.endSecond > marker.second) {
        return marker.endSecond;
    }
    if (marker.slideTraceSecond > marker.second) {
        return marker.slideTraceSecond;
    }
    return marker.second;
}

bool eventPlayed(double second, double judgeSecond)
{
    return judgeSecond <= (second + 1e-6);
}

QString persistedHudFontPath()
{
    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject app = root.value(QStringLiteral("app")).toObject();
    const QJsonObject videoExport = app.value(QStringLiteral("video_export")).toObject();
    const QString path = videoExport.value(QStringLiteral("hud_font_path")).toString();
    if (path.isEmpty()) {
        return QString();
    }
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    if (!info.isFile() || (suffix != QStringLiteral("ttf") && suffix != QStringLiteral("otf"))) {
        return QString();
    }
    return info.absoluteFilePath();
}

QString& cachedHudFontPath()
{
    static QString path;
    return path;
}

QString& cachedHudFontFamily()
{
    static QString family;
    return family;
}

bool& hudFontCacheInitialized()
{
    static bool initialized = false;
    return initialized;
}

QString customHudFontFamily()
{
    QString& cachedPath = cachedHudFontPath();
    QString& cachedFamily = cachedHudFontFamily();
    bool& initialized = hudFontCacheInitialized();

    if (!initialized) {
        cachedPath = persistedHudFontPath();
        initialized = true;
    }

    if (cachedPath.isEmpty()) {
        cachedFamily.clear();
        return QString();
    }
    if (!cachedFamily.isEmpty()) {
        return cachedFamily;
    }

    const int fontId = QFontDatabase::addApplicationFont(cachedPath);
    if (fontId < 0) {
        cachedPath.clear();
        return QString();
    }
    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    cachedFamily = families.isEmpty() ? QString() : families.first();
    return cachedFamily;
}

}  // namespace

namespace miacode::preview::scene {

PreviewHudStats computePreviewHudStats(const QVector<TimelineNoteMarker>& noteMarkers, double second)
{
    struct StatsAccumulator {
        int tapTotal = 0;
        int tapPlayed = 0;
        int holdTotal = 0;
        int holdPlayed = 0;
        int slideTotal = 0;
        int slidePlayed = 0;
        int touchTotal = 0;
        int touchPlayed = 0;
        int breakTotal = 0;
        int breakPlayed = 0;
        double finaleBaseTotal = 0.0;
        double finaleCurrent = 0.0;
        double deluxeBaseTotal = 0.0;
        double deluxeBaseCurrent = 0.0;
        int deluxeBreakTotal = 0;
        int deluxeBreakCurrent = 0;
    } stats;

    QHash<QString, bool> slideHeadSeen;
    slideHeadSeen.reserve(noteMarkers.size());

    const auto addNormalEvent = [&stats, second](
        double judgeSecond,
        int finaleScore,
        int deluxeValue,
        int* totalCounter,
        int* playedCounter
    ) {
        const bool played = eventPlayed(second, judgeSecond);
        ++(*totalCounter);
        stats.finaleBaseTotal += static_cast<double>(finaleScore);
        stats.deluxeBaseTotal += static_cast<double>(deluxeValue);
        if (played) {
            ++(*playedCounter);
            stats.finaleCurrent += static_cast<double>(finaleScore);
            stats.deluxeBaseCurrent += static_cast<double>(deluxeValue);
        }
    };

    const auto addBreakEvent = [&stats, second](double judgeSecond) {
        const bool played = eventPlayed(second, judgeSecond);
        ++stats.breakTotal;
        ++stats.deluxeBreakTotal;
        stats.finaleBaseTotal += 2500.0;
        stats.deluxeBaseTotal += 5.0;
        if (played) {
            ++stats.breakPlayed;
            ++stats.deluxeBreakCurrent;
            stats.finaleCurrent += 2600.0;
            stats.deluxeBaseCurrent += 5.0;
        }
    };

    for (const TimelineNoteMarker& marker : noteMarkers) {
        const QString type = marker.type.toLower();
        if (type == QLatin1String("tap")) {
            if (marker.isBreak) {
                addBreakEvent(marker.second);
            } else {
                addNormalEvent(marker.second, 500, 1, &stats.tapTotal, &stats.tapPlayed);
            }
            continue;
        }
        if (type == QLatin1String("hold")) {
            if (marker.isBreak) {
                addBreakEvent(marker.endSecond > marker.second ? marker.endSecond : marker.second);
            } else {
                addNormalEvent(
                    marker.endSecond > marker.second ? marker.endSecond : marker.second,
                    1000,
                    2,
                    &stats.holdTotal,
                    &stats.holdPlayed
                );
            }
            continue;
        }
        if (type == QLatin1String("touch")) {
            if (marker.isBreak) {
                addBreakEvent(marker.second);
            } else {
                addNormalEvent(marker.second, 500, 1, &stats.touchTotal, &stats.touchPlayed);
            }
            continue;
        }
        if (type == QLatin1String("touch_hold")) {
            if (marker.isBreak) {
                addBreakEvent(marker.endSecond > marker.second ? marker.endSecond : marker.second);
            } else {
                addNormalEvent(
                    marker.endSecond > marker.second ? marker.endSecond : marker.second,
                    1000,
                    2,
                    &stats.holdTotal,
                    &stats.holdPlayed
                );
            }
            continue;
        }
        if (type == QLatin1String("slide") || type == QLatin1String("wifi")) {
            if (marker.hasHeadStar) {
                const QString key = slideHeadEventKey(marker);
                if (!slideHeadSeen.contains(key)) {
                    slideHeadSeen.insert(key, true);
                    if (marker.headBreak) {
                        addBreakEvent(marker.second);
                    } else {
                        addNormalEvent(marker.second, 500, 1, &stats.tapTotal, &stats.tapPlayed);
                    }
                }
            }
            const double judgeSecond = slideJudgeSecondFor(marker);
            if (marker.trackBreak) {
                addBreakEvent(judgeSecond);
            } else {
                addNormalEvent(judgeSecond, 1500, 3, &stats.slideTotal, &stats.slidePlayed);
            }
        }
    }

    PreviewHudStats result;
    result.tapPlayed = stats.tapPlayed;
    result.holdPlayed = stats.holdPlayed;
    result.slidePlayed = stats.slidePlayed;
    result.touchPlayed = stats.touchPlayed;
    result.breakPlayed = stats.breakPlayed;
    result.tapTotal = stats.tapTotal;
    result.holdTotal = stats.holdTotal;
    result.slideTotal = stats.slideTotal;
    result.touchTotal = stats.touchTotal;
    result.breakTotal = stats.breakTotal;
    result.combo =
        stats.tapPlayed + stats.holdPlayed + stats.slidePlayed + stats.touchPlayed + stats.breakPlayed;
    result.totalNotes =
        stats.tapTotal + stats.holdTotal + stats.slideTotal + stats.touchTotal + stats.breakTotal;
    result.dxScore = result.combo * 3;
    result.dxScoreMax = result.totalNotes * 3;
    result.finaleRate = stats.finaleBaseTotal > 0.0
        ? (stats.finaleCurrent / stats.finaleBaseTotal) * 100.0
        : 0.0;
    result.deluxeRate = (stats.deluxeBaseTotal > 0.0
        ? (stats.deluxeBaseCurrent / stats.deluxeBaseTotal) * 100.0
        : 0.0)
        + (stats.deluxeBreakTotal > 0
            ? (static_cast<double>(stats.deluxeBreakCurrent) / static_cast<double>(stats.deluxeBreakTotal))
            : 0.0);
    result.deluxeBreakCurrent = stats.deluxeBreakCurrent;
    result.deluxeBreakTotal = stats.deluxeBreakTotal;
    return result;
}

QString formatPreviewHudTimeLabel(double seconds)
{
    const qint64 signedMs = qRound64(seconds * 1000.0);
    const bool negative = signedMs < 0;
    const qint64 totalMs = qAbs(signedMs);
    const qint64 minutes = totalMs / 60000;
    const qint64 sec = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;
    return QString("%1%2:%3:%4")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(minutes, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

QFont previewHudTimestampFont(int pointSize, QFont::Weight weight)
{
    const QString customFamily = customHudFontFamily();
    if (!customFamily.isEmpty()) {
        QFont font(customFamily);
        font.setPointSize(pointSize);
        font.setWeight(weight);
        return font;
    }

    static const QString embeddedXiaolaiMonoFamily = []() -> QString {
        const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/xiaolai_mono.ttf"));
        if (fontId < 0) {
            return QString();
        }
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        return families.isEmpty() ? QString() : families.first();
    }();

    QFont font;
    if (!embeddedXiaolaiMonoFamily.isEmpty()) {
        font.setFamily(embeddedXiaolaiMonoFamily);
    } else {
        font.setFamily(QStringLiteral("Xiaolai Mono"));
    }
    if (font.family().isEmpty() || QFontInfo(font).family().compare(font.family(), Qt::CaseInsensitive) != 0) {
        font = previewHudMonoFont(pointSize, weight);
    } else {
        font.setPointSize(pointSize);
        font.setWeight(weight);
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

QFont previewHudMonoFont(int pointSize, QFont::Weight weight)
{
    const QString customFamily = customHudFontFamily();
    if (!customFamily.isEmpty()) {
        QFont font(customFamily);
        font.setPointSize(pointSize);
        font.setWeight(weight);
        return font;
    }

    QFont font;
    for (const QString& family : QStringList{QStringLiteral("Cascadia Mono"), QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Code"), QStringLiteral("Consolas")}) {
        font.setFamily(family);
        if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setPointSize(pointSize);
    font.setWeight(weight);
    return font;
}

QString previewHudFontDisplayName()
{
    const QString customFamily = customHudFontFamily();
    return customFamily.isEmpty() ? QStringLiteral("Default") : customFamily;
}

void setPreviewHudCustomFontPath(const QString& fontPath)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QStringLiteral("app")).toObject();
    QJsonObject videoExport = app.value(QStringLiteral("video_export")).toObject();
    const QString normalizedPath = fontPath.isEmpty() ? QString() : QFileInfo(fontPath).absoluteFilePath();
    if (normalizedPath.isEmpty()) {
        videoExport.remove(QStringLiteral("hud_font_path"));
    } else {
        videoExport.insert(QStringLiteral("hud_font_path"), normalizedPath);
    }
    app.insert(QStringLiteral("video_export"), videoExport);
    root.insert(QStringLiteral("app"), app);
    UiText::savePreferencesObject(root);

    cachedHudFontPath() = normalizedPath;
    cachedHudFontFamily().clear();
    hudFontCacheInitialized() = true;
    customHudFontFamily();
}

}  // namespace miacode::preview::scene
