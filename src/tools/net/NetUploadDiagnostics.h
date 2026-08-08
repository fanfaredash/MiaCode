#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

namespace miacode::net {

struct NetUploadResponseInfo {
    QUrl url;
    int statusCode = 0;
    QString reasonPhrase;
    QByteArray payload;
    QString contentType;
    QString server;
    QString cfRay;
    QString retryAfter;
    QString networkError;
    bool timedOut = false;
    bool canceled = false;
};

struct NetUploadResponseAssessment {
    QString serverMessage;
    QString responseBody;
    bool isHtml = false;
    bool isCloudflare = false;
    bool isCloudflareChallenge = false;
    bool isPayloadTooLarge = false;
    bool isRateLimited = false;
    bool isApplicationError = false;
    bool shouldStopBatch = false;
    int retryAfterSeconds = 0;
};

int parseNetUploadRetryAfterSeconds(
    const QString& value,
    const QDateTime& nowUtc = QDateTime::currentDateTimeUtc());
NetUploadResponseAssessment assessNetUploadResponse(const NetUploadResponseInfo& response);

}  // namespace miacode::net
