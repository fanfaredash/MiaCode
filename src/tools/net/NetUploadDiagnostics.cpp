#include "NetUploadDiagnostics.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimeZone>

#include <algorithm>

namespace miacode::net {
namespace {

constexpr int kMaximumRetryAfterSeconds = 3600;

QString jsonValueText(const QJsonValue& value)
{
    if (value.isString()) {
        return value.toString().trimmed();
    }
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    if (!value.isNull() && !value.isUndefined()) {
        return value.toVariant().toString().trimmed();
    }
    return {};
}

QString firstServerMessage(const QJsonObject& object)
{
    for (const QString& key : {
             QStringLiteral("message"),
             QStringLiteral("error"),
             QStringLiteral("detail"),
             QStringLiteral("title")}) {
        const QString value = jsonValueText(object.value(key));
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

bool containsCloudflareChallengeMarker(const QString& lowerBody)
{
    return lowerBody.contains(QStringLiteral("just a moment"))
        || lowerBody.contains(QStringLiteral("challenge-platform"))
        || lowerBody.contains(QStringLiteral("cf-chl-"))
        || lowerBody.contains(QStringLiteral("/cdn-cgi/challenge"))
        || lowerBody.contains(QStringLiteral("attention required"));
}

}  // namespace

int parseNetUploadRetryAfterSeconds(const QString& value, const QDateTime& nowUtc)
{
    const QString trimmed = value.trimmed();
    bool ok = false;
    const int seconds = trimmed.toInt(&ok);
    if (ok) {
        return std::clamp(seconds, 1, kMaximumRetryAfterSeconds);
    }

    QDateTime retryTime = QDateTime::fromString(trimmed, Qt::RFC2822Date);
    if (!retryTime.isValid()) {
        retryTime = QDateTime::fromString(
            trimmed,
            QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
        if (retryTime.isValid()) {
            retryTime.setTimeZone(QTimeZone::utc());
        }
    }
    if (!retryTime.isValid()) {
        return 0;
    }
    const qint64 delta = nowUtc.toUTC().secsTo(retryTime.toUTC());
    return static_cast<int>(std::clamp<qint64>(delta, 0, kMaximumRetryAfterSeconds));
}

NetUploadResponseAssessment assessNetUploadResponse(const NetUploadResponseInfo& response)
{
    NetUploadResponseAssessment assessment;
    const QJsonDocument json = QJsonDocument::fromJson(response.payload);
    if (json.isObject()) {
        const QJsonObject object = json.object();
        assessment.serverMessage = firstServerMessage(object);
        assessment.responseBody = QString::fromUtf8(json.toJson(QJsonDocument::Indented)).trimmed();
        const QString status = object.value(QStringLiteral("status")).toString().trimmed().toLower();
        assessment.isApplicationError = (object.value(QStringLiteral("success")).isBool()
                && !object.value(QStringLiteral("success")).toBool())
            || (!object.value(QStringLiteral("error")).isNull()
                && !object.value(QStringLiteral("error")).isUndefined()
                && !jsonValueText(object.value(QStringLiteral("error"))).isEmpty())
            || status == QStringLiteral("error")
            || status == QStringLiteral("failed");
    } else if (json.isArray()) {
        assessment.responseBody = QString::fromUtf8(json.toJson(QJsonDocument::Indented)).trimmed();
    } else {
        assessment.responseBody = QString::fromUtf8(response.payload).trimmed();
    }

    const QString lowerBody = assessment.responseBody.toLower();
    assessment.isHtml = response.contentType.contains(QStringLiteral("text/html"), Qt::CaseInsensitive)
        || lowerBody.contains(QStringLiteral("<!doctype html"))
        || lowerBody.contains(QStringLiteral("<html"));
    assessment.isCloudflare = !response.cfRay.trimmed().isEmpty()
        || response.server.contains(QStringLiteral("cloudflare"), Qt::CaseInsensitive)
        || lowerBody.contains(QStringLiteral("cloudflare"));
    const bool hasError1015 = lowerBody.contains(QStringLiteral("error 1015"))
        || lowerBody.contains(QStringLiteral("error code: 1015"));
    assessment.isPayloadTooLarge = response.statusCode == 413;
    assessment.isRateLimited = response.statusCode == 429 || hasError1015;
    assessment.isCloudflareChallenge = assessment.isCloudflare
        && assessment.isHtml
        && containsCloudflareChallengeMarker(lowerBody)
        && !assessment.isPayloadTooLarge
        && !hasError1015;
    assessment.retryAfterSeconds = parseNetUploadRetryAfterSeconds(response.retryAfter);
    assessment.shouldStopBatch = response.statusCode == 401
        || (response.statusCode == 403 && !assessment.isRateLimited)
        || assessment.isCloudflareChallenge;
    return assessment;
}

}  // namespace miacode::net
