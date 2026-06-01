// Spec for miacode::zip_export::packChartToZip — verifies the packaged
// archive contents against the handover-doc rules: maidata.txt is renamed
// from the chart body, canonical siblings are collected, *_bak backups are
// excluded, and an out-of-folder &video= target is skipped. Reads the
// resulting zip back with miniz to assert on real archive entries.

#include "ChartZipPackager.h"

#include <miniz.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>

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
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(bytes);
    file.close();
    return true;
}

// Returns the archive entry names of a zip, or empty on failure.
QStringList readZipEntryNames(const QString& zipPath, bool* ok)
{
    QStringList names;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zipPath.toUtf8().constData(), 0)) {
        *ok = false;
        return names;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&zip, i, &stat)) {
            names << QString::fromUtf8(stat.m_filename);
        }
    }
    mz_zip_reader_end(&zip);
    *ok = true;
    return names;
}

QByteArray readZipEntry(const QString& zipPath, const QString& entryName)
{
    QByteArray out;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zipPath.toUtf8().constData(), 0)) {
        return out;
    }
    size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip, entryName.toUtf8().constData(), &size, 0);
    if (data != nullptr) {
        out = QByteArray(static_cast<const char*>(data), static_cast<int>(size));
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return out;
}

}  // namespace

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    using namespace miacode::zip_export;

    // sanitizedZipStem -------------------------------------------------
    check(sanitizedZipStem(QStringLiteral("Hello")) == QStringLiteral("Hello"), "stem keeps plain text");
    check(sanitizedZipStem(QStringLiteral("a/b:c*?")) == QStringLiteral("a_b_c__"), "stem replaces reserved chars");
    check(sanitizedZipStem(QString()) == QStringLiteral("chart"), "empty title falls back to chart");
    check(sanitizedZipStem(QStringLiteral("  名字  ")) == QStringLiteral("名字"), "stem keeps CJK and trims");

    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        QTextStream(stdout) << "[FAIL] could not create temp dir\n";
        return 1;
    }
    const QString dir = tmp.path();
    const QString chartPath = QDir(dir).filePath(QStringLiteral("maidata.txt"));

    const QByteArray chartBody = QByteArray("&title=Test\n&first=0\n");
    check(writeFile(chartPath, chartBody), "wrote chart file");
    check(writeFile(QDir(dir).filePath(QStringLiteral("track.mp3")), QByteArray("MP3")), "wrote track.mp3");
    check(writeFile(QDir(dir).filePath(QStringLiteral("bg.jpg")), QByteArray("JPG")), "wrote bg.jpg");
    check(writeFile(QDir(dir).filePath(QStringLiteral("pv.mp4")), QByteArray("MP4")), "wrote pv.mp4");
    // Backups that must NOT be collected.
    check(writeFile(QDir(dir).filePath(QStringLiteral("track_bak.mp3")), QByteArray("BAK")), "wrote track_bak.mp3");

    // --- Case 1: canonical siblings, no &video= override --------------
    {
        const QString zipPath = QDir(dir).filePath(QStringLiteral("out1.zip"));
        ChartZipInput input;
        input.chartText = QString::fromUtf8(chartBody);
        input.chartPath = chartPath;
        input.outputZipPath = zipPath;

        const ChartZipResult result = packChartToZip(input);
        check(result.ok, "case1 packs ok");

        bool readOk = false;
        const QStringList names = readZipEntryNames(zipPath, &readOk);
        check(readOk, "case1 zip is readable");
        check(names.contains(QStringLiteral("maidata.txt")), "case1 has maidata.txt");
        check(names.contains(QStringLiteral("track.mp3")), "case1 has track.mp3");
        check(names.contains(QStringLiteral("bg.jpg")), "case1 has bg.jpg");
        check(names.contains(QStringLiteral("pv.mp4")), "case1 has pv.mp4");
        check(!names.contains(QStringLiteral("track_bak.mp3")), "case1 excludes track_bak.mp3");
        check(readZipEntry(zipPath, QStringLiteral("maidata.txt")) == chartBody, "case1 maidata.txt content matches");
    }

    // --- Case 2: &video= pointing OUTSIDE the chart folder is skipped --
    {
        QTemporaryDir otherTmp;
        const QString outsideVideo = QDir(otherTmp.path()).filePath(QStringLiteral("outside.mp4"));
        check(writeFile(outsideVideo, QByteArray("OUT")), "wrote outside video");

        const QString zipPath = QDir(dir).filePath(QStringLiteral("out2.zip"));
        ChartZipInput input;
        input.chartText = QString::fromUtf8(chartBody);
        input.chartPath = chartPath;
        input.videoFieldValue = outsideVideo;
        input.outputZipPath = zipPath;

        const ChartZipResult result = packChartToZip(input);
        check(result.ok, "case2 packs ok");

        bool readOk = false;
        const QStringList names = readZipEntryNames(zipPath, &readOk);
        check(readOk, "case2 zip is readable");
        check(!names.contains(QStringLiteral("outside.mp4")), "case2 skips out-of-folder &video=");
        // An explicit out-of-folder &video= override wins over the sibling
        // heuristic, so we intentionally collect no PV at all rather than
        // packaging a leftover sibling pv.mp4 the chart doesn't use.
        check(!names.contains(QStringLiteral("pv.mp4")), "case2 does not fall back to sibling pv.mp4");
    }

    // --- Case 3: empty chart body is a hard error ---------------------
    {
        ChartZipInput input;
        input.chartText = QString();
        input.chartPath = chartPath;
        input.outputZipPath = QDir(dir).filePath(QStringLiteral("out3.zip"));
        const ChartZipResult result = packChartToZip(input);
        check(!result.ok, "case3 empty chart fails");
        check(!QFile::exists(input.outputZipPath), "case3 writes no file on failure");
    }

    QTextStream(stdout) << (g_failures == 0 ? "ALL PASS\n" : QStringLiteral("%1 FAILURES\n").arg(g_failures));
    return g_failures == 0 ? 0 : 1;
}
