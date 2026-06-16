#include "NetClient.h"

#include "tools/zip_export/ChartZipPackager.h"

#include <miniz.h>

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>

#include <cstring>

namespace miacode::net {
namespace {

constexpr int kRequestTimeoutMs = 60000;

QString stringValue(const QJsonObject& object, const QString& key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

QStringList stringArrayValue(const QJsonObject& object, const QString& key)
{
    QStringList values;
    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        return values;
    }
    const QJsonArray array = value.toArray();
    values.reserve(array.size());
    for (const QJsonValue& item : array) {
        values.append(item.isString() ? item.toString() : QString());
    }
    return values;
}

void appendNonEmptyUnique(QStringList* values, const QString& value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !values->contains(trimmed, Qt::CaseInsensitive)) {
        values->append(trimmed);
    }
}

void appendNonEmptyUnique(QStringList* values, const QStringList& candidates)
{
    for (const QString& value : candidates) {
        appendNonEmptyUnique(values, value);
    }
}

bool looksLikeChallengePage(const QByteArray& payload, const QString& contentType)
{
    const QByteArray lower = payload.left(4096).toLower();
    return contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive)
        || lower.contains("<!doctype html")
        || lower.contains("cloudflare")
        || lower.contains("cf-chl")
        || lower.contains("challenge-platform");
}

bool addZipMem(mz_zip_archive* zip, const QString& entryName, const QByteArray& data, int compressionLevel)
{
    const QByteArray nameUtf8 = entryName.toUtf8();
    return mz_zip_writer_add_mem(
        zip,
        nameUtf8.constData(),
        data.constData(),
        static_cast<size_t>(data.size()),
        compressionLevel);
}

bool addZipFile(mz_zip_archive* zip, const QString& entryName, const QString& filePath, int compressionLevel)
{
    const QByteArray entryNameUtf8 = entryName.toUtf8();
    const QByteArray filePathUtf8 = QDir::toNativeSeparators(filePath).toUtf8();
    return mz_zip_writer_add_file(
        zip,
        entryNameUtf8.constData(),
        filePathUtf8.constData(),
        nullptr,
        0,
        compressionLevel);
}

QString tagSearchQueryFor(const QString& tagKeyword)
{
    const QString trimmedTag = tagKeyword.trimmed();
    if (trimmedTag.isEmpty()) {
        return {};
    }
    return trimmedTag.startsWith(QStringLiteral("tag:"), Qt::CaseInsensitive)
        ? trimmedTag
        : QStringLiteral("tag:%1").arg(trimmedTag);
}

QStringList fuzzyUploaderFallbackQueriesFor(const QString& username)
{
    QStringList queries;
    const QString trimmed = username.trimmed();
    if (trimmed.isEmpty()) {
        return queries;
    }

    const auto appendValue = [&](const QString& value) {
        const QString trimmedValue = value.trimmed();
        if (trimmedValue.isEmpty()) {
            return;
        }
        const QString uploaderQuery = QStringLiteral("uploader:%1").arg(trimmedValue);
        if (!queries.contains(uploaderQuery, Qt::CaseInsensitive)) {
            queries.append(uploaderQuery);
        }
        if (!queries.contains(trimmedValue, Qt::CaseInsensitive)) {
            queries.append(trimmedValue);
        }
    };

    appendValue(trimmed.toLower());
    appendValue(trimmed.toUpper());
    appendValue(trimmed.left(1).toUpper() + trimmed.mid(1).toLower());
    return queries;
}

QString resourceFileName(const QString& resourcePath)
{
    if (resourcePath == QStringLiteral("track")) {
        return QStringLiteral("track.mp3");
    }
    if (resourcePath == QStringLiteral("image?fullImage=true")) {
        return QStringLiteral("bg.jpg");
    }
    if (resourcePath == QStringLiteral("chart")) {
        return QStringLiteral("maidata.txt");
    }
    return resourcePath;
}

}  // namespace

QList<NetChartSummary> parseChartListJson(const QByteArray& payload, QString* errorMessage)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Net returned an invalid chart list.");
        }
        return {};
    }

    QList<NetChartSummary> charts;
    const QJsonArray array = doc.array();
    charts.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        NetChartSummary chart;
        chart.id = stringValue(object, QStringLiteral("id"));
        chart.title = stringValue(object, QStringLiteral("title"));
        chart.artist = stringValue(object, QStringLiteral("artist"));
        chart.designer = stringValue(object, QStringLiteral("designer"));
        chart.uploader = stringValue(object, QStringLiteral("uploader"));
        chart.hash = stringValue(object, QStringLiteral("hash"));
        chart.levels = stringArrayValue(object, QStringLiteral("levels"));
        chart.publicTags = stringArrayValue(object, QStringLiteral("publicTags"));
        appendNonEmptyUnique(&chart.publicTags, stringArrayValue(object, QStringLiteral("tags")));
        appendNonEmptyUnique(&chart.publicTags, stringValue(object, QStringLiteral("contestTag")));
        appendNonEmptyUnique(&chart.publicTags, stringValue(object, QStringLiteral("tag")));
        chart.timestampUtc = QDateTime::fromString(stringValue(object, QStringLiteral("timestamp")), Qt::ISODateWithMs);
        if (!chart.timestampUtc.isValid()) {
            chart.timestampUtc = QDateTime::fromString(stringValue(object, QStringLiteral("timestamp")), Qt::ISODate);
        }
        if (chart.id.isEmpty() || !chart.timestampUtc.isValid()) {
            continue;
        }
        chart.timestampUtc = chart.timestampUtc.toUTC();
        charts.append(chart);
    }
    return charts;
}

QList<NetChartSummary> filterChartsByLocalDateRange(
    const QList<NetChartSummary>& charts,
    const QDate& startDate,
    const QDate& endDate)
{
    if (!startDate.isValid() || !endDate.isValid()) {
        return {};
    }
    const QDate safeStart = qMin(startDate, endDate);
    const QDate safeEnd = qMax(startDate, endDate);
    const QDateTime start(QDate(safeStart), QTime(0, 0, 0, 0), QTimeZone::LocalTime);
    const QDateTime end(QDate(safeEnd), QTime(23, 59, 59, 999), QTimeZone::LocalTime);

    QList<NetChartSummary> filtered;
    for (const NetChartSummary& chart : charts) {
        const QDateTime local = chart.timestampUtc.toLocalTime();
        if (local >= start && local <= end) {
            filtered.append(chart);
        }
    }
    return filtered;
}

QString formatLevels(const QStringList& levels)
{
    QStringList nonEmpty;
    for (const QString& level : levels) {
        if (!level.trimmed().isEmpty()) {
            nonEmpty.append(level.trimmed());
        }
    }
    return nonEmpty.join(QStringLiteral(" / "));
}

QString chartDirectoryPathForTitle(const QString& outputDirectory, const QString& title, const QString& chartId)
{
    const QString titleStem = miacode::zip_export::sanitizedZipStem(title.isEmpty() ? chartId : title);
    const QString idStem = miacode::zip_export::sanitizedZipStem(chartId);
    const QString stem = idStem.isEmpty()
        ? titleStem
        : QStringLiteral("%1 [%2]").arg(titleStem, idStem);
    return QDir(outputDirectory).filePath(stem);
}

QString uniqueZipPathForTitle(const QString& outputDirectory, const QString& title)
{
    const QString stem = miacode::zip_export::sanitizedZipStem(title);
    QDir dir(outputDirectory);
    QString path = dir.filePath(stem + QStringLiteral(".zip"));
    int suffix = 2;
    while (QFileInfo::exists(path)) {
        path = dir.filePath(QStringLiteral("%1 (%2).zip").arg(stem).arg(suffix));
        ++suffix;
    }
    return path;
}

bool writeNetChartFolder(
    const QString& outputDirectoryPath,
    const NetResourcePayload& payload,
    QString* errorMessage)
{
    if (payload.trackMp3.isEmpty() || payload.bgJpg.isEmpty() || payload.maidataTxt.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("One or more required Net resources are empty.");
        }
        return false;
    }
    if (!QDir().mkpath(outputDirectoryPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not create output folder:\n%1").arg(outputDirectoryPath);
        }
        return false;
    }

    const auto writeFile = [&](const QString& fileName, const QByteArray& data) {
        QFile file(QDir(outputDirectoryPath).filePath(fileName));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Could not write %1.").arg(file.fileName());
            }
            return false;
        }
        if (file.write(data) != data.size()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Could not finish writing %1.").arg(file.fileName());
            }
            return false;
        }
        return true;
    };

    return writeFile(QStringLiteral("track.mp3"), payload.trackMp3)
        && writeFile(QStringLiteral("bg.jpg"), payload.bgJpg)
        && writeFile(QStringLiteral("maidata.txt"), payload.maidataTxt);
}

bool packNetChartZip(
    const QString& outputZipPath,
    const NetResourcePayload& payload,
    QStringList* includedEntries,
    QString* errorMessage)
{
    if (outputZipPath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("No output path was provided.");
        }
        return false;
    }
    if (payload.trackMp3.isEmpty() || payload.bgJpg.isEmpty() || payload.maidataTxt.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("One or more required Net resources are empty.");
        }
        return false;
    }

    QDir().mkpath(QFileInfo(outputZipPath).absolutePath());
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    const QByteArray outUtf8 = QDir::toNativeSeparators(outputZipPath).toUtf8();
    if (!mz_zip_writer_init_file(&zip, outUtf8.constData(), 0)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not create the .zip file:\n%1").arg(outputZipPath);
        }
        return false;
    }

    QStringList entries;
    const auto fail = [&](const QString& message) {
        mz_zip_writer_end(&zip);
        QFile::remove(outputZipPath);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (!addZipMem(&zip, QStringLiteral("track.mp3"), payload.trackMp3, MZ_NO_COMPRESSION)) {
        return fail(QStringLiteral("Failed to add track.mp3."));
    }
    entries.append(QStringLiteral("track.mp3"));
    if (!addZipMem(&zip, QStringLiteral("bg.jpg"), payload.bgJpg, MZ_NO_COMPRESSION)) {
        return fail(QStringLiteral("Failed to add bg.jpg."));
    }
    entries.append(QStringLiteral("bg.jpg"));
    if (!addZipMem(&zip, QStringLiteral("maidata.txt"), payload.maidataTxt, MZ_BEST_COMPRESSION)) {
        return fail(QStringLiteral("Failed to add maidata.txt."));
    }
    entries.append(QStringLiteral("maidata.txt"));

    if (!mz_zip_writer_finalize_archive(&zip)) {
        return fail(QStringLiteral("Failed to finalize the .zip archive."));
    }
    mz_zip_writer_end(&zip);
    if (includedEntries != nullptr) {
        *includedEntries = entries;
    }
    return true;
}

bool packNetChartFolderZip(
    const QString& chartDirectoryPath,
    const QString& outputZipPath,
    QStringList* includedEntries,
    QString* errorMessage)
{
    if (outputZipPath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("No output path was provided.");
        }
        return false;
    }

    const QDir chartDir(chartDirectoryPath);
    const QString trackPath = chartDir.filePath(QStringLiteral("track.mp3"));
    const QString imagePath = chartDir.filePath(QStringLiteral("bg.jpg"));
    const QString chartPath = chartDir.filePath(QStringLiteral("maidata.txt"));
    if (!QFileInfo::exists(trackPath) || !QFileInfo::exists(imagePath) || !QFileInfo::exists(chartPath)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("The chart folder is missing track.mp3, bg.jpg, or maidata.txt.");
        }
        return false;
    }

    QDir().mkpath(QFileInfo(outputZipPath).absolutePath());
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    const QByteArray outUtf8 = QDir::toNativeSeparators(outputZipPath).toUtf8();
    if (!mz_zip_writer_init_file(&zip, outUtf8.constData(), 0)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Could not create the .zip file:\n%1").arg(outputZipPath);
        }
        return false;
    }

    QStringList entries;
    const auto fail = [&](const QString& message) {
        mz_zip_writer_end(&zip);
        QFile::remove(outputZipPath);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };

    if (!addZipFile(&zip, QStringLiteral("track.mp3"), trackPath, MZ_NO_COMPRESSION)) {
        return fail(QStringLiteral("Failed to add track.mp3."));
    }
    entries.append(QStringLiteral("track.mp3"));
    if (!addZipFile(&zip, QStringLiteral("bg.jpg"), imagePath, MZ_NO_COMPRESSION)) {
        return fail(QStringLiteral("Failed to add bg.jpg."));
    }
    entries.append(QStringLiteral("bg.jpg"));
    if (!addZipFile(&zip, QStringLiteral("maidata.txt"), chartPath, MZ_BEST_COMPRESSION)) {
        return fail(QStringLiteral("Failed to add maidata.txt."));
    }
    entries.append(QStringLiteral("maidata.txt"));

    if (!mz_zip_writer_finalize_archive(&zip)) {
        return fail(QStringLiteral("Failed to finalize the .zip archive."));
    }
    mz_zip_writer_end(&zip);
    if (includedEntries != nullptr) {
        *includedEntries = entries;
    }
    return true;
}

NetClient::NetClient(QObject* parent)
    : manager_(parent)
{}

QList<NetChartSummary> NetClient::queryCharts(
    const QString& username,
    const QString& tagKeyword,
    const NetQueryOptions& options,
    QString* errorMessage)
{
    const QString trimmedUser = username.trimmed();
    const QString tagSearch = tagSearchQueryFor(tagKeyword);
    QList<NetChartSummary> merged;
    QSet<QString> seenIds;

    const auto appendUnique = [&](const QList<NetChartSummary>& charts) {
        for (const NetChartSummary& chart : charts) {
            if (seenIds.contains(chart.id)) {
                continue;
            }
            seenIds.insert(chart.id);
            merged.append(chart);
        }
    };

    if (!trimmedUser.isEmpty()) {
        const QString referer = QStringLiteral("https://majdata.net/space?id=%1").arg(trimmedUser);
        appendUnique(querySearchText(QStringLiteral("uploader:%1").arg(trimmedUser), referer, errorMessage));
        if (errorMessage != nullptr && !errorMessage->isEmpty()) {
            return {};
        }
        if (options.fuzzyCaseInsensitive && merged.isEmpty()) {
            for (const QString& fuzzyQuery : fuzzyUploaderFallbackQueriesFor(trimmedUser)) {
                appendUnique(querySearchText(fuzzyQuery, referer, errorMessage));
                if (errorMessage != nullptr && !errorMessage->isEmpty()) {
                    return {};
                }
                if (!merged.isEmpty()) {
                    break;
                }
            }
        }
    }
    if (trimmedUser.isEmpty() && !tagSearch.isEmpty()) {
        appendUnique(querySearchText(tagSearch, QStringLiteral("https://majdata.net/"), errorMessage));
        if (errorMessage != nullptr && !errorMessage->isEmpty()) {
            return {};
        }
        if (options.fuzzyCaseInsensitive) {
            const QString lowerTagSearch = tagSearch.toLower();
            if (lowerTagSearch != tagSearch) {
                appendUnique(querySearchText(lowerTagSearch, QStringLiteral("https://majdata.net/"), errorMessage));
                if (errorMessage != nullptr && !errorMessage->isEmpty()) {
                    return {};
                }
            }
            QString plainTag = tagKeyword.trimmed();
            if (plainTag.startsWith(QStringLiteral("tag:"), Qt::CaseInsensitive)) {
                plainTag = plainTag.mid(4).trimmed();
            }
            if (!plainTag.isEmpty() && plainTag != tagSearch) {
                appendUnique(querySearchText(plainTag, QStringLiteral("https://majdata.net/"), errorMessage));
                if (errorMessage != nullptr && !errorMessage->isEmpty()) {
                    return {};
                }
            }
            const QString lowerPlainTag = plainTag.toLower();
            if (!lowerPlainTag.isEmpty() && lowerPlainTag != plainTag && lowerPlainTag != lowerTagSearch) {
                appendUnique(querySearchText(lowerPlainTag, QStringLiteral("https://majdata.net/"), errorMessage));
                if (errorMessage != nullptr && !errorMessage->isEmpty()) {
                    return {};
                }
            }
        }
    }
    if (trimmedUser.isEmpty() && tagSearch.isEmpty() && errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Please enter a user ID or tag.");
    }
    return merged;
}

QList<NetChartSummary> NetClient::querySearchText(
    const QString& searchText,
    const QString& referer,
    QString* errorMessage)
{
    QUrl url(QStringLiteral("https://majdata.net/api3/api/maichart/list"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("sort"), QString());
    query.addQueryItem(QStringLiteral("search"), searchText);
    url.setQuery(query);

    bool blocking = false;
    const QByteArray payload = getUrl(
        url,
        referer,
        errorMessage,
        &blocking);
    if (payload.isEmpty()) {
        return {};
    }
    if (blocking && errorMessage != nullptr) {
        *errorMessage = QStringLiteral("Net/Cloudflare blocked the request. Please try again later in a browser.");
        return {};
    }
    return parseChartListJson(payload, errorMessage);
}

QByteArray NetClient::downloadResource(
    const QString& chartId,
    const QString& resourcePath,
    QString* errorMessage,
    bool* blockingResponse)
{
    const QUrl url(QStringLiteral("https://majdata.net/api3/api/maichart/%1/%2").arg(chartId, resourcePath));
    return getUrl(url, QStringLiteral("https://majdata.net/song?id=%1").arg(chartId), errorMessage, blockingResponse);
}

NetDownloadResult NetClient::downloadResourceToFile(
    const QString& chartId,
    const QString& resourcePath,
    const QString& outputPath)
{
    NetDownloadResult result;
    QDir().mkpath(QFileInfo(outputPath).absolutePath());

    const QUrl url(QStringLiteral("https://majdata.net/api3/api/maichart/%1/%2").arg(chartId, resourcePath));
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "MiaCode/net-downloader");
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Referer", QStringLiteral("https://majdata.net/song?id=%1").arg(chartId).toUtf8());

    QElapsedTimer elapsed;
    elapsed.start();
    QNetworkReply* reply = manager_.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        reply->abort();
        loop.quit();
    });

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        reply->abort();
        reply->deleteLater();
        result.errorMessage = QStringLiteral("Could not open %1 for writing.").arg(outputPath);
        return result;
    }

    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
        const QByteArray chunk = reply->readAll();
        result.bytesWritten += file.write(chunk);
    });
    timeout.start(kRequestTimeoutMs);
    loop.exec();
    if (reply->bytesAvailable() > 0) {
        const QByteArray chunk = reply->readAll();
        result.bytesWritten += file.write(chunk);
    }
    file.close();

    const QVariant statusVariant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    result.statusCode = statusVariant.isValid() ? statusVariant.toInt() : 0;
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const bool timedOut = !timeout.isActive();
    timeout.stop();

    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();
    result.elapsedMs = elapsed.elapsed();

    QFile payloadProbe(outputPath);
    QByteArray probe;
    if (payloadProbe.open(QIODevice::ReadOnly)) {
        probe = payloadProbe.read(4096);
    }
    result.blockingResponse =
        result.statusCode == 403
        || result.statusCode == 429
        || looksLikeChallengePage(probe, contentType);

    if (timedOut) {
        QFile::remove(outputPath);
        result.errorMessage = QStringLiteral("Request timed out.");
        return result;
    }
    if (result.blockingResponse) {
        QFile::remove(outputPath);
        result.errorMessage = QStringLiteral("Request was blocked by Net/Cloudflare.");
        return result;
    }
    if (networkError != QNetworkReply::NoError || result.statusCode < 200 || result.statusCode >= 300) {
        QFile::remove(outputPath);
        result.errorMessage = result.statusCode > 0
            ? QStringLiteral("HTTP %1: %2").arg(result.statusCode).arg(networkErrorText)
            : networkErrorText;
        return result;
    }

    result.ok = result.bytesWritten > 0 || resourceFileName(resourcePath) == QStringLiteral("maidata.txt");
    if (!result.ok) {
        QFile::remove(outputPath);
        result.errorMessage = QStringLiteral("Downloaded resource is empty.");
    }
    return result;
}

QByteArray NetClient::getUrl(const QUrl& url, const QString& referer, QString* errorMessage, bool* blockingResponse)
{
    if (blockingResponse != nullptr) {
        *blockingResponse = false;
    }

    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "MiaCode/net-downloader");
    request.setRawHeader("Accept", "*/*");
    if (!referer.isEmpty()) {
        request.setRawHeader("Referer", referer.toUtf8());
    }

    QNetworkReply* reply = manager_.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        reply->abort();
        loop.quit();
    });
    timeout.start(kRequestTimeoutMs);
    loop.exec();

    const QVariant statusVariant = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    const int statusCode = statusVariant.isValid() ? statusVariant.toInt() : 0;
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const QByteArray payload = reply->readAll();
    const bool timedOut = !timeout.isActive();
    timeout.stop();

    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    const bool blocking =
        statusCode == 403
        || statusCode == 429
        || looksLikeChallengePage(payload, contentType);
    if (blockingResponse != nullptr) {
        *blockingResponse = blocking;
    }

    if (timedOut) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Request timed out.");
        }
        return {};
    }
    if (blocking) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Request was blocked by Net/Cloudflare.");
        }
        return {};
    }
    if (networkError != QNetworkReply::NoError || statusCode < 200 || statusCode >= 300) {
        if (errorMessage != nullptr) {
            *errorMessage = statusCode > 0
                ? QStringLiteral("HTTP %1: %2").arg(statusCode).arg(networkErrorText)
                : networkErrorText;
        }
        return {};
    }
    return payload;
}

}  // namespace miacode::net
