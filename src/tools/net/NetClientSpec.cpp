#include "NetClient.h"
#include "NetBatchUploadScanner.h"
#include "NetUploadDiagnostics.h"
#include "tools/media/PvBatchCompressionScanner.h"

#include <miniz.h>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimeZone>

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

bool writeFixtureFile(const QString& path, const QByteArray& payload = QByteArrayLiteral("fixture"))
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(payload) == payload.size();
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    using namespace miacode::net;

    bool ok = true;
    ok &= check(netDownloadLengthIsComplete(12, 12), "download length accepts a complete response");
    ok &= check(!netDownloadLengthIsComplete(12, 7), "download length rejects a truncated response");
    ok &= check(!netDownloadLengthIsComplete(12, 0), "download length rejects an empty response");
    ok &= check(netDownloadLengthIsComplete(-1, 7), "download length accepts data when length is unknown");
    ok &= check(
        netUserSpaceReferer(QStringLiteral(" 乐园杯 "))
            == QStringLiteral("https://majdata.net/space?id=%E4%B9%90%E5%9B%AD%E6%9D%AF"),
        "Chinese uploader ID is percent-encoded in the user-space referer");
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
    const QString uploadRoot = QDir(temp.path()).filePath(QStringLiteral("upload-root"));
    const QString chartA = QDir(uploadRoot).filePath(QStringLiteral("chart-a"));
    const QString chartB = QDir(uploadRoot).filePath(QStringLiteral("chart-b"));
    const QString invalidChart = QDir(uploadRoot).filePath(QStringLiteral("missing-track"));
    QDir().mkpath(chartA);
    QDir().mkpath(chartB);
    QDir().mkpath(invalidChart);
    ok &= check(writeFixtureFile(QDir(chartA).filePath(QStringLiteral("maidata.txt"))), "chart A maidata fixture");
    ok &= check(writeFixtureFile(QDir(chartA).filePath(QStringLiteral("bg.jpg"))), "chart A background fixture");
    ok &= check(writeFixtureFile(QDir(chartA).filePath(QStringLiteral("track.mp3"))), "chart A track fixture");
    ok &= check(writeFixtureFile(QDir(chartB).filePath(QStringLiteral("MAIDATA.TXT"))), "chart B case-insensitive maidata fixture");
    ok &= check(writeFixtureFile(QDir(chartB).filePath(QStringLiteral("bg.png"))), "chart B PNG fixture");
    ok &= check(writeFixtureFile(QDir(chartB).filePath(QStringLiteral("track.mp3"))), "chart B track fixture");
    ok &= check(writeFixtureFile(QDir(chartB).filePath(QStringLiteral("pv.mp4"))), "chart B video fixture");
    ok &= check(writeFixtureFile(QDir(invalidChart).filePath(QStringLiteral("maidata.txt"))), "invalid chart maidata fixture");
    ok &= check(writeFixtureFile(QDir(invalidChart).filePath(QStringLiteral("bg.jpg"))), "invalid chart background fixture");
    const QList<NetUploadJob> uploadJobs = scanNetUploadFolders(uploadRoot);
    ok &= check(uploadJobs.size() == 2, "upload scanner keeps only complete immediate chart folders");
    ok &= check(uploadJobs.at(0).displayName == QStringLiteral("chart-a"), "upload scanner sorts folders by name");
    ok &= check(uploadJobs.at(1).backgroundPath.endsWith(QStringLiteral("bg.png")), "upload scanner accepts PNG background");
    ok &= check(uploadJobs.at(1).videoPath.endsWith(QStringLiteral("pv.mp4")), "upload scanner keeps optional video");
    QList<NetUploadJob> uploadQueue;
    ok &= check(appendUniqueNetUploadJobs(&uploadQueue, uploadJobs) == 2, "upload queue appends scanned jobs");
    ok &= check(appendUniqueNetUploadJobs(&uploadQueue, uploadJobs) == 0, "upload queue ignores duplicate folders");
    ok &= check(uploadQueue.size() == 2, "upload queue remains deduplicated");

    const QString naturalRoot = QDir(temp.path()).filePath(QStringLiteral("upload-natural-root"));
    for (const QString& name : {QStringLiteral("chart-10"), QStringLiteral("chart-2"), QStringLiteral("chart-1")}) {
        const QString folder = QDir(naturalRoot).filePath(name);
        ok &= check(QDir().mkpath(folder), "natural sort folder created");
        ok &= check(writeFixtureFile(QDir(folder).filePath(QStringLiteral("maidata.txt"))), "natural sort maidata created");
        ok &= check(writeFixtureFile(QDir(folder).filePath(QStringLiteral("bg.jpg"))), "natural sort bg created");
        ok &= check(writeFixtureFile(QDir(folder).filePath(QStringLiteral("track.mp3"))), "natural sort track created");
    }
    const QList<NetUploadJob> naturalJobs = scanNetUploadFolders(naturalRoot);
    ok &= check(naturalJobs.size() == 3, "natural sort scanner keeps all numbered folders");
    ok &= check(naturalJobs.at(0).displayName == QStringLiteral("chart-1"), "upload scanner sorts numbers naturally (1 before 2)");
    ok &= check(naturalJobs.at(1).displayName == QStringLiteral("chart-2"), "upload scanner sorts numbers naturally (2 before 10)");
    ok &= check(naturalJobs.at(2).displayName == QStringLiteral("chart-10"), "upload scanner sorts numbers naturally (10 after 2)");

    const QString pvRoot = QDir(temp.path()).filePath(QStringLiteral("pv-root"));
    const QString largePvChart = QDir(pvRoot).filePath(QStringLiteral("large-pv"));
    const QString smallPvChart = QDir(pvRoot).filePath(QStringLiteral("small-pv"));
    const QString nestedGroup = QDir(pvRoot).filePath(QStringLiteral("group"));
    const QString nestedBgChart = QDir(nestedGroup).filePath(QStringLiteral("nested-bg"));
    QDir().mkpath(largePvChart);
    QDir().mkpath(smallPvChart);
    QDir().mkpath(nestedBgChart);
    QFile largePv(QDir(largePvChart).filePath(QStringLiteral("PV.MP4")));
    ok &= check(largePv.open(QIODevice::WriteOnly), "large PV fixture opens");
    ok &= check(largePv.resize(miacode::media::kPvCompressionTargetBytes + 1), "large PV fixture is over target");
    largePv.close();
    QFile smallPv(QDir(smallPvChart).filePath(QStringLiteral("pv.mp4")));
    ok &= check(smallPv.open(QIODevice::WriteOnly), "small PV fixture opens");
    ok &= check(smallPv.resize(miacode::media::kPvCompressionTargetBytes), "small PV fixture is at target");
    smallPv.close();
    ok &= check(writeFixtureFile(QDir(nestedBgChart).filePath(QStringLiteral("bg.mp4"))), "nested bg fixture");
    ok &= check(writeFixtureFile(QDir(nestedBgChart).filePath(QStringLiteral("pv.mp4"))), "nested pv fixture");
    const QList<miacode::media::PvCompressionJob> pvJobs = miacode::media::scanPvCompressionFolders(pvRoot);
    ok &= check(pvJobs.size() == 4, "video scanner keeps the root and immediate child folders");
    bool foundLargePv = false;
    bool foundNestedBg = false;
    int foldersWithoutVideo = 0;
    for (const miacode::media::PvCompressionJob& job : pvJobs) {
        if (job.displayName == QStringLiteral("large-pv")) {
            foundLargePv = job.videoPath.endsWith(QStringLiteral("PV.MP4"));
        } else if (job.displayName == QStringLiteral("nested-bg")) {
            foundNestedBg = job.videoPath.endsWith(QStringLiteral("bg.mp4"));
        }
        if (job.videoPath.isEmpty()) {
            ++foldersWithoutVideo;
        }
    }
    ok &= check(foundLargePv, "video scanner accepts case-insensitive pv.mp4");
    ok &= check(!foundNestedBg, "video scanner does not include grandchild folders");
    ok &= check(foldersWithoutVideo == 2, "video scanner retains folders without video");
    const QList<miacode::media::PvCompressionJob> nestedJobs =
        miacode::media::scanPvCompressionFolders(nestedGroup);
    ok &= check(nestedJobs.size() == 2, "video scanner includes a selected root and its immediate child");
    ok &= check(
        nestedJobs.at(1).videoPath.endsWith(QStringLiteral("bg.mp4")),
        "video scanner prefers bg.mp4 when both supported names exist");
    QList<miacode::media::PvCompressionJob> pvQueue;
    ok &= check(miacode::media::appendUniquePvCompressionJobs(&pvQueue, pvJobs) == 4, "video list appends scanned folders");
    ok &= check(miacode::media::appendUniquePvCompressionJobs(&pvQueue, pvJobs) == 0, "video list ignores duplicate folders");

    NetUploadResponseInfo rateLimitedResponse;
    rateLimitedResponse.statusCode = 429;
    rateLimitedResponse.reasonPhrase = QStringLiteral("Too Many Requests");
    rateLimitedResponse.contentType = QStringLiteral("application/json");
    rateLimitedResponse.server = QStringLiteral("cloudflare");
    rateLimitedResponse.cfRay = QStringLiteral("abc123-SJC");
    rateLimitedResponse.retryAfter = QStringLiteral("12");
    rateLimitedResponse.payload = QByteArrayLiteral("{\"message\":\"Slow down\",\"code\":429}");
    const NetUploadResponseAssessment rateLimited = assessNetUploadResponse(rateLimitedResponse);
    ok &= check(rateLimited.isRateLimited, "upload diagnostics classify HTTP 429 as rate limited");
    ok &= check(!rateLimited.shouldStopBatch, "first rate-limit response remains retryable");
    ok &= check(rateLimited.retryAfterSeconds == 12, "upload diagnostics parse Retry-After seconds");
    ok &= check(rateLimited.serverMessage == QStringLiteral("Slow down"), "upload diagnostics extract server message");
    ok &= check(rateLimited.responseBody.contains(QStringLiteral("\"code\": 429")), "upload diagnostics retain full JSON body");

    NetUploadResponseInfo challengeResponse;
    challengeResponse.statusCode = 403;
    challengeResponse.contentType = QStringLiteral("text/html; charset=UTF-8");
    challengeResponse.server = QStringLiteral("cloudflare");
    challengeResponse.cfRay = QStringLiteral("def456-LAX");
    challengeResponse.payload = QByteArrayLiteral(
        "<!doctype html><html><title>Just a moment...</title><script src='/cdn-cgi/challenge-platform/x'></script></html>");
    const NetUploadResponseAssessment challenge = assessNetUploadResponse(challengeResponse);
    ok &= check(challenge.isCloudflareChallenge, "upload diagnostics detect Cloudflare challenge HTML");
    ok &= check(challenge.shouldStopBatch, "Cloudflare challenge stops the upload batch");
    ok &= check(challenge.responseBody.contains(QStringLiteral("challenge-platform")), "challenge diagnostics retain HTML response");

    NetUploadResponseInfo payloadTooLargeResponse;
    payloadTooLargeResponse.statusCode = 413;
    payloadTooLargeResponse.contentType = QStringLiteral("text/html");
    payloadTooLargeResponse.server = QStringLiteral("cloudflare");
    payloadTooLargeResponse.cfRay = QStringLiteral("a225651769d91990-SJC");
    payloadTooLargeResponse.payload = QByteArrayLiteral(
        "<html><head><title>413 Request Entity Too Large</title></head>"
        "<body><h1>413 Request Entity Too Large</h1>"
        "<script src='/cdn-cgi/challenge-platform/scripts/jsd/main.js'></script></body></html>");
    const NetUploadResponseAssessment payloadTooLarge = assessNetUploadResponse(payloadTooLargeResponse);
    ok &= check(payloadTooLarge.isPayloadTooLarge, "upload diagnostics classify HTTP 413 as an oversized request");
    ok &= check(!payloadTooLarge.isCloudflareChallenge, "Cloudflare-injected script does not override HTTP 413");
    ok &= check(!payloadTooLarge.shouldStopBatch, "HTTP 413 remains a per-chart failure");
    ok &= check(payloadTooLarge.responseBody.contains(QStringLiteral("Request Entity Too Large")), "HTTP 413 diagnostics retain HTML response");

    NetUploadResponseInfo validationResponse;
    validationResponse.statusCode = 422;
    validationResponse.contentType = QStringLiteral("application/json");
    validationResponse.payload = QByteArrayLiteral("{\"detail\":{\"field\":\"maidata\",\"reason\":\"invalid\"}}");
    const NetUploadResponseAssessment validation = assessNetUploadResponse(validationResponse);
    ok &= check(!validation.shouldStopBatch, "HTTP 422 remains a per-chart failure");
    ok &= check(validation.serverMessage.contains(QStringLiteral("maidata")), "object-valued error detail is preserved");

    NetUploadResponseInfo applicationErrorResponse;
    applicationErrorResponse.statusCode = 200;
    applicationErrorResponse.contentType = QStringLiteral("application/json");
    applicationErrorResponse.payload = QByteArrayLiteral("{\"success\":false,\"error\":\"Chart already exists\"}");
    const NetUploadResponseAssessment applicationError = assessNetUploadResponse(applicationErrorResponse);
    ok &= check(applicationError.isApplicationError, "HTTP 200 application error is not treated as success");

    const QDateTime retryNow(QDate(2026, 7, 28), QTime(12, 0), QTimeZone::utc());
    ok &= check(
        parseNetUploadRetryAfterSeconds(QStringLiteral("Tue, 28 Jul 2026 12:00:30 GMT"), retryNow) == 30,
        "upload diagnostics parse Retry-After HTTP date");
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
