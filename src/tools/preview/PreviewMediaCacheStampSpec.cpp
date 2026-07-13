// Simulation spec for the preview audio/stage-media cache mechanism.
//
// What it proves (against REAL production code + the real filesystem):
//   1. miacode::fs::fileContentStamp — the exact function the BGM/stage-media
//      setChartPath() skip-decision uses — flips iff a same-path file's content
//      changes (size OR mtime) and stays equal when the file is untouched.
//   2. miacode::chart_assets::resolveTrackPath / resolveBackgroundMediaPath are
//      live (no caching): a sibling that is added / rewritten / removed is
//      reflected immediately, never shadowed by a stale resolved path.
//   3. End-to-end: a small "reload gate" that mirrors setChartPath's rule
//      (reload unless BOTH the resolved path AND its content stamp are
//      unchanged) makes the correct reload/skip decision as we simulate the
//      in-app audio/video tools rewriting track.mp3 / bg.png in place.
//   4. Track resolution recognizes the supported `track.*` audio candidates
//      used by the timeline waveform and preview/export audio paths.
//
// This is the regression net for the 2026-06-03 fix that removed the path-only
// resolved-path cache and switched the skip decision to a content stamp.

#include "common/ChartAssetPaths.h"
#include "common/FileContentStamp.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <chrono>
#include <filesystem>
#include <system_error>

namespace {

int g_failures = 0;

void check(bool condition, const QString& label)
{
    QTextStream(stdout) << (condition ? "[ok]   " : "[FAIL] ") << label << '\n';
    if (!condition) {
        ++g_failures;
    }
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(bytes);
    file.close();
    return true;
}

// Deterministically bump a file's mtime so a same-size rewrite still produces a
// different stamp, without depending on wall-clock resolution or sleeps.
void advanceMtimeSeconds(const QString& path, int seconds)
{
    std::error_code ec;
    const std::filesystem::path p(path.toStdWString());
    const auto current = std::filesystem::last_write_time(p, ec);
    if (!ec) {
        std::filesystem::last_write_time(p, current + std::chrono::seconds(seconds), ec);
    }
}

// Mirror of the BassPreviewAudioBackend::setChartPath /
// PreviewStageMediaHost::setChartPath skip rule: reload unless the resolved
// path AND the content stamp are both unchanged. Returns true == "would reload".
struct ReloadGate {
    QString path;
    QString stamp;
    bool consider(const QString& newPath, const QString& newStamp)
    {
        const bool skip = (newPath == path && newStamp == stamp);
        path = newPath;
        stamp = newStamp;
        return !skip;
    }
};

void testStampSemantics(const QString& dir)
{
    const QString f = QDir(dir).filePath(QStringLiteral("stamp_probe.bin"));

    check(miacode::fs::fileContentStamp(f).isEmpty(),
          "absent file -> empty stamp");

    check(writeFile(f, QByteArray("AAAA")), "write probe (4 bytes)");
    const QString s1 = miacode::fs::fileContentStamp(f);
    check(!s1.isEmpty(), "existing file -> non-empty stamp");
    check(s1.contains(QLatin1Char(':')), "stamp is size:mtime");

    check(miacode::fs::fileContentStamp(f) == s1,
          "untouched file -> identical stamp (skip)");

    // Same path, DIFFERENT size -> must change (in-place rewrite, new content).
    check(writeFile(f, QByteArray("BBBBBBBB")), "rewrite probe (8 bytes)");
    const QString s2 = miacode::fs::fileContentStamp(f);
    check(s2 != s1, "same path + different size -> stamp changes (reload)");

    // Same path, SAME size, advanced mtime -> must change (relies on mtime leg).
    check(writeFile(f, QByteArray("12345678")), "rewrite probe (same 8 bytes)");
    advanceMtimeSeconds(f, 5);
    const QString s3 = miacode::fs::fileContentStamp(f);
    check(s3 != s2, "same path + same size + newer mtime -> stamp changes (reload)");

    QFile::remove(f);
    check(miacode::fs::fileContentStamp(f).isEmpty(),
          "removed file -> empty stamp again");
}

void testLiveResolution(const QString& dir)
{
    // A "chart" plus its sibling track.mp3 / bg.png, resolved the way the
    // preview backends resolve them.
    const QString chart = QDir(dir).filePath(QStringLiteral("maidata.txt"));
    const QString track = QDir(dir).filePath(QStringLiteral("track.mp3"));
    const QString bg = QDir(dir).filePath(QStringLiteral("bg.png"));
    check(writeFile(chart, QByteArray("&title=x\n")), "write maidata.txt");

    check(miacode::chart_assets::resolveTrackPath(chart).isEmpty(),
          "no sibling track -> resolveTrackPath empty");
    check(writeFile(track, QByteArray("ID3-fake")), "add track.mp3");
    check(miacode::chart_assets::resolveTrackPath(chart) == QDir::cleanPath(track),
          "sibling track present -> resolved live");
    QFile::remove(track);
    check(miacode::chart_assets::resolveTrackPath(chart).isEmpty(),
          "track removed -> resolver reflects it immediately (no stale cache)");

    check(miacode::chart_assets::resolveBackgroundMediaPath(chart).isEmpty(),
          "no sibling bg -> resolveBackgroundMediaPath empty");
    check(writeFile(bg, QByteArray("PNG-fake")), "add bg.png");
    check(miacode::chart_assets::resolveBackgroundMediaPath(chart) == QDir::cleanPath(bg),
          "sibling bg present -> resolved live");
}

void testSupportedTrackAudioCandidates(const QString& dir)
{
    const QString chart = QDir(dir).filePath(QStringLiteral("maidata.txt"));
    check(writeFile(chart, QByteArray("&title=z\n")), "write candidate maidata.txt");

    const QStringList candidates = miacode::chart_assets::trackCandidateFileNames();
    check(candidates.contains(QStringLiteral("track.mp3")), "track candidates include mp3");
    check(candidates.contains(QStringLiteral("track.wav")), "track candidates include wav");
    check(candidates.contains(QStringLiteral("track.flac")), "track candidates include flac");
    check(candidates.contains(QStringLiteral("track.ogg")), "track candidates include ogg");

    for (const QString& name : candidates) {
        QFile::remove(QDir(dir).filePath(name));
    }

    const QString wav = QDir(dir).filePath(QStringLiteral("track.wav"));
    check(writeFile(wav, QByteArray("WAV-fake")), "add track.wav");
    check(miacode::chart_assets::resolveTrackPath(chart) == QDir::cleanPath(wav),
          "track.wav resolves when no track.mp3 exists");

    const QString flac = QDir(dir).filePath(QStringLiteral("track.flac"));
    QFile::remove(wav);
    check(writeFile(flac, QByteArray("FLAC-fake")), "add track.flac");
    check(miacode::chart_assets::resolveTrackPath(chart) == QDir::cleanPath(flac),
          "track.flac resolves when earlier candidates are absent");

    const QString ogg = QDir(dir).filePath(QStringLiteral("track.ogg"));
    QFile::remove(flac);
    check(writeFile(ogg, QByteArray("OGG-fake")), "add track.ogg");
    check(miacode::chart_assets::resolveTrackPath(chart) == QDir::cleanPath(ogg),
          "track.ogg resolves when earlier candidates are absent");

    const QString mp3 = QDir(dir).filePath(QStringLiteral("track.mp3"));
    check(writeFile(mp3, QByteArray("MP3-fake")), "add track.mp3");
    check(miacode::chart_assets::resolveTrackPath(chart) == QDir::cleanPath(mp3),
          "track.mp3 keeps priority when multiple track files exist");

    for (const QString& name : candidates) {
        QFile::remove(QDir(dir).filePath(name));
    }
}

// Simulate the audio/video processing tools rewriting track.mp3 in place and
// confirm the reload gate flips correctly across the whole lifecycle.
void testInPlaceRewriteDecision(const QString& dir)
{
    const QString chart = QDir(dir).filePath(QStringLiteral("maidata.txt"));
    const QString track = QDir(dir).filePath(QStringLiteral("track.mp3"));
    writeFile(chart, QByteArray("&title=y\n"));

    auto resolved = [&]() { return miacode::chart_assets::resolveTrackPath(chart); };
    auto stamp = [&]() { return miacode::fs::fileContentStamp(resolved()); };

    ReloadGate gate;

    // (a) first time we see a track -> reload.
    writeFile(track, QByteArray("song-one"));
    check(gate.consider(resolved(), stamp()), "first track -> reload");

    // (b) nothing changed -> skip.
    check(!gate.consider(resolved(), stamp()), "unchanged track -> skip");

    // (c) tool rewrites track.mp3 in place, NEW content, SAME path -> reload.
    writeFile(track, QByteArray("song-two-longer"));
    check(gate.consider(resolved(), stamp()),
          "same-name new-content track -> reload (the core fix)");

    // (d) settled again -> skip.
    check(!gate.consider(resolved(), stamp()), "after reload, settled -> skip");

    // (e) same-size in-place rewrite (e.g. 44100Hz convert keeping length) with
    //     a newer mtime -> reload.
    writeFile(track, QByteArray("song-two-longeR"));  // same length as (c)
    advanceMtimeSeconds(track, 5);
    check(gate.consider(resolved(), stamp()),
          "same-name same-size newer-mtime track -> reload");

    // (f) track deleted -> reload (now resolves empty).
    QFile::remove(track);
    check(gate.consider(resolved(), stamp()), "track removed -> reload");

    // (g) track re-added -> reload.
    writeFile(track, QByteArray("song-three"));
    check(gate.consider(resolved(), stamp()), "track re-added -> reload");
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        QTextStream(stdout) << "[FAIL] could not create temp dir\n";
        return 1;
    }

    testStampSemantics(tmp.path());
    testLiveResolution(tmp.path());
    testSupportedTrackAudioCandidates(tmp.path());
    testInPlaceRewriteDecision(tmp.path());

    QTextStream(stdout) << (g_failures == 0 ? "ALL PASS\n"
                                            : QStringLiteral("%1 FAILURE(S)\n").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
