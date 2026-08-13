#pragma once

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QNetworkAccessManager>
#include <QString>
#include <QStringList>

namespace miacode::net {

struct NetChartSummary {
    QString id;
    QString title;
    QString artist;
    QString designer;
    QString uploader;
    QString hash;
    QStringList levels;
    QStringList publicTags;
    QDateTime timestampUtc;
};

struct NetDownloadJob {
    NetChartSummary chart;
    QString outputDirectoryPath;
    QString outputZipPath;
    bool selected = true;
    QString status;
    QString errorMessage;
};

struct NetResourcePayload {
    QByteArray trackMp3;
    QByteArray bgJpg;
    QByteArray maidataTxt;
};

struct NetDownloadResult {
    bool ok = false;
    bool blockingResponse = false;
    int statusCode = 0;
    qint64 bytesWritten = 0;
    qint64 elapsedMs = 0;
    QString errorMessage;
};

struct NetQueryOptions {
    bool fuzzyCaseInsensitive = true;
    QString titleKeyword;
};

bool netDownloadLengthIsComplete(qint64 expectedBytes, qint64 bytesWritten);

QList<NetChartSummary> parseChartListJson(const QByteArray& payload, QString* errorMessage);
QList<NetChartSummary> filterChartsByLocalDateRange(
    const QList<NetChartSummary>& charts,
    const QDate& startDate,
    const QDate& endDate);
QString formatLevels(const QStringList& levels);
QString netUserSpaceReferer(const QString& username);
QString chartDirectoryPathForTitle(const QString& outputDirectory, const QString& title, const QString& chartId);
QString uniqueZipPathForTitle(const QString& outputDirectory, const QString& title);
bool packNetChartZip(
    const QString& outputZipPath,
    const NetResourcePayload& payload,
    QStringList* includedEntries,
    QString* errorMessage);
bool writeNetChartFolder(
    const QString& outputDirectoryPath,
    const NetResourcePayload& payload,
    QString* errorMessage);
bool packNetChartFolderZip(
    const QString& chartDirectoryPath,
    const QString& outputZipPath,
    QStringList* includedEntries,
    QString* errorMessage);

class NetClient {
public:
    explicit NetClient(QObject* parent = nullptr);

    QList<NetChartSummary> queryCharts(
        const QString& username,
        const QString& tagKeyword,
        const NetQueryOptions& options,
        QString* errorMessage);
    QByteArray downloadResource(
        const QString& chartId,
        const QString& resourcePath,
        QString* errorMessage,
        bool* blockingResponse);
    NetDownloadResult downloadResourceToFile(
        const QString& chartId,
        const QString& resourcePath,
        const QString& outputPath);

private:
    QList<NetChartSummary> querySearchText(
        const QString& searchText,
        const QString& referer,
        QString* errorMessage);
    QByteArray getUrl(const QUrl& url, const QString& referer, QString* errorMessage, bool* blockingResponse);

    QNetworkAccessManager manager_;
};

}  // namespace miacode::net
