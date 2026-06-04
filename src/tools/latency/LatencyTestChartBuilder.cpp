#include "LatencyTestChartBuilder.h"

#include <QtMath>

#include <algorithm>

namespace miacode::latency {

namespace {

constexpr double kFallbackDurationSeconds = 180.0;
// No trailing margin: the test chart must not extend past the audio duration.
// A +16-beat tail made the latency page's preview ~3s LONGER than the song
// (2:35 vs 2:32); playing to that tail then switching back to a difficulty left
// the playhead beyond the song, so the difficulty "couldn't play". With margin
// 0, ceil(notesPerSecond*duration) keeps the last note strictly before the
// audio end, so the audition never exceeds the song. See
// project_pv_file_lock_release notes / latency preview duration.
constexpr int kTailNoteMargin = 0;
constexpr int kMaxNoteCount = 200000;  // hard cap to keep parser bounded

QString formatBpm(double bpm)
{
    QString text = QString::number(bpm, 'f', 6);
    while (text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }
    return text;
}

}  // namespace

QString buildTestChartText(double bpm, int subdivision, double durationSeconds)
{
    if (!(bpm > 0.0)) {
        return QString();
    }
    if (subdivision != 4 && subdivision != 8) {
        subdivision = 4;
    }
    if (!(durationSeconds > 0.0)) {
        durationSeconds = kFallbackDurationSeconds;
    }

    const double notesPerSecond = (bpm / 60.0) * (static_cast<double>(subdivision) / 4.0);
    const double requestedCount = std::ceil(notesPerSecond * durationSeconds) + kTailNoteMargin;
    int totalNotes = static_cast<int>(std::min<double>(requestedCount, kMaxNoteCount));
    if (totalNotes < 1) {
        totalNotes = 1;
    }

    QString text;
    text.reserve(totalNotes * 3 + 16);
    text.append(QLatin1Char('('));
    text.append(formatBpm(bpm));
    text.append(QStringLiteral("){"));
    text.append(QString::number(subdivision));
    text.append(QLatin1Char('}'));
    for (int i = 0; i < totalNotes; ++i) {
        text.append(QStringLiteral("1,"));
    }
    text.append(QLatin1Char('E'));
    return text;
}

}  // namespace miacode::latency
