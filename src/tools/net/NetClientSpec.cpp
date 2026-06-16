#include "NetClient.h"

#include <miniz.h>

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstring>

namespace {

bool check(bool condition, const char* message)
{
    if (!condition) {
        QTextStream(stderr) << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

QStringList readZipEntryNames(const QString& zipPath)
{
    QStringList names;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zipPath.toUtf8().constData(), 0)) {
        return names;
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&zip, i, &stat)) {
            names.append(QString::fromUtf8(stat.m_filename));
        }
    }
    mz_zip_reader_end(&zip);
    return names;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace miacode::net;

    bool ok = true;
    const QByteArray json = R"([
      {"id":"a","title":"A/B","artist":"aa","designer":"da","uploader":"u","timestamp":"2026-01-02T14:10:57.494Z","levels":["","","13+"],"publicTags":["EventTag"]},
      {"id":"b","title":"B","artist":"bb","designer":"db","uploader":"u","timestamp":"2026-01-03T00:00:00Z","levels":[null,"12",""],"contestTag":"OtherTag"},
      {"id":"","title":"bad","timestamp":"bad"}
    ])";
    QString error;
    const QList<NetChartSummary> charts = parseChartListJson(json, &error);
    ok &= check(error.isEmpty(), "valid json has no error");
    ok &= check(charts.size() == 2, "parser skips invalid chart rows");
    ok &= check(formatLevels(charts.at(0).levels) == QStringLiteral("13+"), "levels skip empty values");
    ok &= check(formatLevels(charts.at(1).levels) == QStringLiteral("12"), "levels skip null values");
    ok &= check(charts.at(0).publicTags.contains(QStringLiteral("EventTag")), "parser keeps publicTags");
    ok &= check(charts.at(1).publicTags.contains(QStringLiteral("OtherTag")), "parser keeps contestTag");

    const QDate localDate = charts.at(0).timestampUtc.toLocalTime().date();
    const QList<NetChartSummary> filtered = filterChartsByLocalDateRange(charts, localDate, localDate);
    ok &= check(!filtered.isEmpty(), "local date filter includes boundary day");

    QTemporaryDir temp;
    ok &= check(temp.isValid(), "temporary dir created");
    const QString firstPath = uniqueZipPathForTitle(temp.path(), QStringLiteral("A/B"));
    QFile marker(firstPath);
    marker.open(QIODevice::WriteOnly);
    marker.write("exists");
    marker.close();
    const QString secondPath = uniqueZipPathForTitle(temp.path(), QStringLiteral("A/B"));
    ok &= check(secondPath.endsWith(QStringLiteral("A_B (2).zip")), "unique path appends numeric suffix");
    const QString chartDir = chartDirectoryPathForTitle(temp.path(), QStringLiteral("A/B"), QStringLiteral("abc123"));
    ok &= check(chartDir.endsWith(QStringLiteral("A_B [abc123]")), "chart folder path includes sanitized title and id");

    NetResourcePayload payload;
    payload.trackMp3 = QByteArray("track");
    payload.bgJpg = QByteArray("image");
    payload.maidataTxt = QByteArray("&title=test\n");
    QStringList entries;
    QString zipError;
    ok &= check(packNetChartZip(secondPath, payload, &entries, &zipError), "remote zip packaging succeeds");
    const QStringList zipNames = readZipEntryNames(secondPath);
    ok &= check(zipNames.contains(QStringLiteral("track.mp3")), "zip contains track.mp3");
    ok &= check(zipNames.contains(QStringLiteral("bg.jpg")), "zip contains bg.jpg");
    ok &= check(zipNames.contains(QStringLiteral("maidata.txt")), "zip contains maidata.txt");

    QString folderError;
    ok &= check(writeNetChartFolder(chartDir, payload, &folderError), "chart folder writer succeeds");
    ok &= check(QFile::exists(QDir(chartDir).filePath(QStringLiteral("track.mp3"))), "chart folder contains track.mp3");
    const QString folderZipPath = uniqueZipPathForTitle(temp.path(), QStringLiteral("Folder Zip"));
    QStringList folderEntries;
    ok &= check(
        packNetChartFolderZip(chartDir, folderZipPath, &folderEntries, &folderError),
        "chart folder zip packaging succeeds");
    const QStringList folderZipNames = readZipEntryNames(folderZipPath);
    ok &= check(folderZipNames.contains(QStringLiteral("track.mp3")), "folder zip contains track.mp3");
    ok &= check(folderZipNames.contains(QStringLiteral("bg.jpg")), "folder zip contains bg.jpg");
    ok &= check(folderZipNames.contains(QStringLiteral("maidata.txt")), "folder zip contains maidata.txt");

    return ok ? 0 : 1;
}
