#include "NetBatchUploadWorker.h"

#include "NetUploadDiagnostics.h"
#include "UiText.h"

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace miacode::net {
namespace {

constexpr int kRequestTimeoutMs = 90000;
constexpr int kSuccessfulUploadDelaySeconds = 5;
constexpr int kRateLimitFallbackDelaySeconds = 60;

struct UploadAttemptResult {
    bool succeeded = false;
    bool rateLimited = false;
    bool stopBatch = false;
    int retryAfterSeconds = 0;
    QString summary;
    QString details;
};

NetUploadResponseInfo waitForReply(QNetworkReply* reply, const std::atomic_bool* cancelRequested)
{
    QEventLoop loop;
    QTimer timeout;
    QTimer cancelPoll;
    timeout.setSingleShot(true);
    cancelPoll.setInterval(100);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        reply->abort();
        loop.quit();
    });
    QObject::connect(&cancelPoll, &QTimer::timeout, &loop, [&]() {
        if (cancelRequested != nullptr && cancelRequested->load()) {
            reply->abort();
            loop.quit();
        }
    });
    timeout.start(kRequestTimeoutMs);
    cancelPoll.start();
    loop.exec();

    NetUploadResponseInfo response;
    response.url = reply->url();
    response.timedOut = !timeout.isActive();
    response.canceled = cancelRequested != nullptr && cancelRequested->load();
    timeout.stop();
    cancelPoll.stop();
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    response.statusCode = status.isValid() ? status.toInt() : 0;
    response.reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
    response.payload = reply->readAll();
    response.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    response.server = QString::fromLatin1(reply->rawHeader("Server"));
    response.cfRay = QString::fromLatin1(reply->rawHeader("CF-Ray"));
    response.retryAfter = QString::fromLatin1(reply->rawHeader("Retry-After"));
    if (reply->error() != QNetworkReply::NoError) {
        response.networkError = reply->errorString();
    }
    reply->deleteLater();
    return response;
}

void addTextPart(QHttpMultiPart* multiPart, const QByteArray& name, const QByteArray& value)
{
    QHttpPart part;
    part.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QStringLiteral("form-data; name=\"%1\"").arg(QString::fromLatin1(name)));
    part.setBody(value);
    multiPart->append(part);
}

bool addFilePart(
    QHttpMultiPart* multiPart,
    const QString& filePath,
    const QString& uploadName,
    QString* errorMessage)
{
    auto* file = new QFile(filePath, multiPart);
    if (!file->open(QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = UiText::text(QStringLiteral("net.upload_could_not_open_file_1_2"))
                .arg(filePath, file->errorString());
        }
        return false;
    }
    QHttpPart part;
    part.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QStringLiteral("form-data; name=\"formfiles\"; filename=\"%1\"").arg(uploadName));
    part.setBodyDevice(file);
    multiPart->append(part);
    return true;
}

QString responseSummary(
    const NetUploadResponseInfo& response,
    const NetUploadResponseAssessment& assessment)
{
    if (response.timedOut) {
        return UiText::text(QStringLiteral("net.upload_request_timed_out"));
    }
    if (assessment.isRateLimited) {
        return UiText::text(QStringLiteral("net.upload_rate_limited"));
    }
    if (assessment.isPayloadTooLarge) {
        return UiText::text(QStringLiteral("net.upload_payload_too_large"));
    }
    if (assessment.isCloudflareChallenge) {
        return UiText::text(QStringLiteral("net.upload_cloudflare_challenge"));
    }
    const QString detail = !assessment.serverMessage.isEmpty()
        ? assessment.serverMessage
        : response.networkError;
    if (response.statusCode > 0) {
        return detail.isEmpty()
            ? QStringLiteral("HTTP %1 %2").arg(response.statusCode).arg(response.reasonPhrase).trimmed()
            : QStringLiteral("HTTP %1: %2").arg(response.statusCode).arg(detail);
    }
    return detail.isEmpty() ? UiText::text(QStringLiteral("net.upload_network_error")) : detail;
}

QString responseDetails(
    const QString& stage,
    const QString& chartName,
    const QString& directoryPath,
    const NetUploadResponseInfo& response,
    const NetUploadResponseAssessment& assessment)
{
    QStringList lines;
    lines.append(UiText::text(QStringLiteral("net.upload_detail_stage_1")).arg(stage));
    if (!chartName.isEmpty()) {
        lines.append(UiText::text(QStringLiteral("net.upload_detail_chart_1")).arg(chartName));
    }
    if (!directoryPath.isEmpty()) {
        lines.append(UiText::text(QStringLiteral("net.upload_detail_directory_1")).arg(directoryPath));
    }
    lines.append(UiText::text(QStringLiteral("net.upload_detail_url_1")).arg(response.url.toString()));
    if (response.statusCode > 0) {
        lines.append(UiText::text(QStringLiteral("net.upload_detail_http_1_2"))
                         .arg(response.statusCode)
                         .arg(response.reasonPhrase));
    }
    if (!response.networkError.isEmpty()) {
        lines.append(UiText::text(QStringLiteral("net.upload_detail_network_1")).arg(response.networkError));
    }
    if (!response.contentType.isEmpty()) {
        lines.append(QStringLiteral("Content-Type: %1").arg(response.contentType));
    }
    if (!response.server.isEmpty()) {
        lines.append(QStringLiteral("Server: %1").arg(response.server));
    }
    if (!response.cfRay.isEmpty()) {
        lines.append(QStringLiteral("CF-Ray: %1").arg(response.cfRay));
    }
    if (!response.retryAfter.isEmpty()) {
        lines.append(QStringLiteral("Retry-After: %1").arg(response.retryAfter));
    }
    if (assessment.isPayloadTooLarge) {
        lines.append(UiText::text(QStringLiteral("net.upload_detail_payload_too_large")));
    } else if (assessment.isCloudflareChallenge) {
        lines.append(UiText::text(QStringLiteral("net.upload_detail_cloudflare_challenge")));
    } else if (assessment.isRateLimited) {
        lines.append(UiText::text(QStringLiteral("net.upload_detail_rate_limited")));
    }
    if (!assessment.responseBody.isEmpty()) {
        lines.append(QString());
        lines.append(UiText::text(QStringLiteral("net.upload_detail_response_body")));
        lines.append(assessment.responseBody);
    }
    return lines.join(QLatin1Char('\n'));
}

UploadAttemptResult login(
    QNetworkAccessManager* manager,
    const QString& username,
    const QString& password,
    const std::atomic_bool* cancelRequested)
{
    UploadAttemptResult result;
    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    addTextPart(multiPart, QByteArrayLiteral("username"), username.toUtf8());
    addTextPart(
        multiPart,
        QByteArrayLiteral("password"),
        QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Md5).toHex());
    addTextPart(multiPart, QByteArrayLiteral("rememberMe"), QByteArrayLiteral("false"));

    QNetworkRequest request(QUrl(QStringLiteral("https://majdata.net/api3/api/account/Login")));
    request.setRawHeader("User-Agent", "MiaCode/net-batch-uploader");
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("Referer", "https://majdata.net/login");
    QNetworkReply* reply = manager->post(request, multiPart);
    multiPart->setParent(reply);
    const NetUploadResponseInfo response = waitForReply(reply, cancelRequested);
    if (response.canceled) {
        return result;
    }
    const NetUploadResponseAssessment assessment = assessNetUploadResponse(response);
    if (response.statusCode == 200
        && response.networkError.isEmpty()
        && !assessment.isCloudflareChallenge
        && !assessment.isRateLimited
        && !assessment.isApplicationError) {
        result.succeeded = true;
        return result;
    }
    result.rateLimited = assessment.isRateLimited;
    result.stopBatch = true;
    result.retryAfterSeconds = assessment.retryAfterSeconds;
    result.summary = UiText::text(QStringLiteral("net.upload_login_failed_1"))
        .arg(responseSummary(response, assessment));
    result.details = responseDetails(
        UiText::text(QStringLiteral("net.upload_stage_login")),
        QString(),
        QString(),
        response,
        assessment);
    return result;
}

UploadAttemptResult uploadJob(
    QNetworkAccessManager* manager,
    const NetUploadJob& job,
    const std::atomic_bool* cancelRequested)
{
    UploadAttemptResult result;
    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    const QString backgroundName = QStringLiteral("bg.%1").arg(QFileInfo(job.backgroundPath).suffix().toLower());
    QString localError;
    if (!addFilePart(multiPart, job.chartPath, QStringLiteral("maidata.txt"), &localError)
        || !addFilePart(multiPart, job.backgroundPath, backgroundName, &localError)
        || !addFilePart(multiPart, job.trackPath, QStringLiteral("track.mp3"), &localError)
        || (!job.videoPath.isEmpty()
            && !addFilePart(multiPart, job.videoPath, QStringLiteral("pv.mp4"), &localError))) {
        delete multiPart;
        result.summary = localError;
        result.details = QStringList{
            UiText::text(QStringLiteral("net.upload_detail_stage_1"))
                .arg(UiText::text(QStringLiteral("net.upload_stage_local_file"))),
            UiText::text(QStringLiteral("net.upload_detail_chart_1")).arg(job.displayName),
            UiText::text(QStringLiteral("net.upload_detail_directory_1")).arg(job.directoryPath),
            UiText::text(QStringLiteral("net.upload_detail_local_error_1")).arg(localError),
        }.join(QLatin1Char('\n'));
        return result;
    }

    QNetworkRequest request(QUrl(QStringLiteral("https://majdata.net/api3/api/maichart/upload")));
    request.setRawHeader("User-Agent", "MiaCode/net-batch-uploader");
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("Referer", "https://majdata.net/user/charts");
    QNetworkReply* reply = manager->post(request, multiPart);
    multiPart->setParent(reply);
    const NetUploadResponseInfo response = waitForReply(reply, cancelRequested);
    if (response.canceled) {
        return result;
    }
    const NetUploadResponseAssessment assessment = assessNetUploadResponse(response);
    if (response.statusCode >= 200
        && response.statusCode < 300
        && response.networkError.isEmpty()
        && !assessment.isCloudflareChallenge
        && !assessment.isRateLimited
        && !assessment.isApplicationError) {
        result.succeeded = true;
        return result;
    }
    result.rateLimited = assessment.isRateLimited;
    result.stopBatch = assessment.shouldStopBatch;
    result.retryAfterSeconds = assessment.retryAfterSeconds;
    result.summary = responseSummary(response, assessment);
    result.details = responseDetails(
        UiText::text(QStringLiteral("net.upload_stage_upload")),
        job.displayName,
        job.directoryPath,
        response,
        assessment);
    return result;
}

}  // namespace

NetBatchUploadWorker::NetBatchUploadWorker(
    NetBatchUploadRequest request,
    std::atomic_bool* cancelRequested)
    : request_(std::move(request))
    , cancelRequested_(cancelRequested)
{}

bool NetBatchUploadWorker::isCanceled() const
{
    return cancelRequested_ != nullptr && cancelRequested_->load();
}

void NetBatchUploadWorker::run()
{
    QNetworkAccessManager manager;
    int succeeded = 0;
    int failed = 0;
    int completed = 0;
    QString fatalError;

    const auto waitWithCountdown = [this](int seconds, const QString& textKey, int row) {
        for (int remaining = seconds; remaining > 0 && !isCanceled(); --remaining) {
            const QString message = UiText::text(textKey).arg(remaining);
            emit summary(message);
            if (row >= 0) {
                emit rowStatus(row, message);
            }
            for (int tick = 0; tick < 10 && !isCanceled(); ++tick) {
                QThread::msleep(100);
            }
        }
        return !isCanceled();
    };
    const auto hasSelectedJobAfter = [this](int row) {
        for (int next = row + 1; next < request_.jobs.size(); ++next) {
            if (request_.jobs.at(next).selected) {
                return true;
            }
        }
        return false;
    };

    emit summary(UiText::text(QStringLiteral("net.upload_logging_in")));
    UploadAttemptResult loginResult = login(
        &manager, request_.username, request_.password, cancelRequested_);
    QString loginDetails = loginResult.details;
    if (loginResult.rateLimited && !isCanceled()) {
        const int retryDelay = loginResult.retryAfterSeconds > 0
            ? loginResult.retryAfterSeconds
            : kRateLimitFallbackDelaySeconds;
        loginDetails = UiText::text(QStringLiteral("net.upload_detail_attempt_1")).arg(1)
            + QLatin1Char('\n') + loginResult.details;
        if (waitWithCountdown(
                retryDelay,
                QStringLiteral("net.upload_rate_limited_retrying_1"),
                -1)) {
            loginResult = login(
                &manager, request_.username, request_.password, cancelRequested_);
            if (!loginResult.succeeded && !loginResult.details.isEmpty()) {
                loginDetails += QStringLiteral("\n\n")
                    + UiText::text(QStringLiteral("net.upload_detail_attempt_1")).arg(2)
                    + QLatin1Char('\n') + loginResult.details;
            }
            if (loginResult.rateLimited) {
                loginResult.summary = UiText::text(QStringLiteral("net.upload_rate_limit_retry_exhausted"));
            }
        }
    }
    if (!loginResult.succeeded) {
        if (!isCanceled()) {
            fatalError = loginResult.summary;
            emit failureDetail(-1, loginResult.summary, loginDetails);
        }
        emit finished(0, 0, isCanceled(), fatalError);
        return;
    }
    request_.password.clear();

    for (int row = 0; row < request_.jobs.size() && !isCanceled(); ++row) {
        const NetUploadJob& job = request_.jobs.at(row);
        if (!job.selected) {
            continue;
        }
        emit rowStatus(row, UiText::text(QStringLiteral("net.upload_uploading")));
        emit summary(UiText::text(QStringLiteral("net.upload_uploading_1")).arg(job.displayName));
        UploadAttemptResult result = uploadJob(&manager, job, cancelRequested_);
        QString details = result.details;
        if (result.rateLimited && !isCanceled()) {
            const int retryDelay = result.retryAfterSeconds > 0
                ? result.retryAfterSeconds
                : kRateLimitFallbackDelaySeconds;
            const QString firstAttemptDetails = UiText::text(QStringLiteral("net.upload_detail_attempt_1"))
                .arg(1) + QLatin1Char('\n') + result.details;
            if (!waitWithCountdown(
                    retryDelay,
                    QStringLiteral("net.upload_rate_limited_retrying_1"),
                    row)) {
                break;
            }
            emit rowStatus(row, UiText::text(QStringLiteral("net.upload_uploading")));
            result = uploadJob(&manager, job, cancelRequested_);
            details = firstAttemptDetails;
            if (!result.succeeded && !result.details.isEmpty()) {
                details += QStringLiteral("\n\n")
                    + UiText::text(QStringLiteral("net.upload_detail_attempt_1")).arg(2)
                    + QLatin1Char('\n') + result.details;
            }
            if (result.rateLimited) {
                result.stopBatch = true;
                result.summary = UiText::text(QStringLiteral("net.upload_rate_limit_retry_exhausted"));
            }
        }

        if (result.succeeded) {
            ++succeeded;
            emit rowStatus(row, UiText::text(QStringLiteral("net.upload_done")));
            emit rowOutcome(row, true);
        } else if (!isCanceled()) {
            ++failed;
            const QString status = UiText::text(QStringLiteral("net.failed_1")).arg(result.summary);
            emit rowStatus(row, status);
            emit rowOutcome(row, false);
            emit failureDetail(row, status, details);
        }
        ++completed;
        emit progress(completed);

        if (!result.succeeded && result.stopBatch && !isCanceled()) {
            fatalError = UiText::text(QStringLiteral("net.upload_batch_stopped_1")).arg(result.summary);
            for (int pendingRow = row + 1; pendingRow < request_.jobs.size(); ++pendingRow) {
                if (request_.jobs.at(pendingRow).selected) {
                    emit rowStatus(
                        pendingRow,
                        UiText::text(QStringLiteral("net.upload_not_uploaded_batch_stopped")));
                }
            }
            break;
        }

        if (!result.stopBatch && hasSelectedJobAfter(row)) {
            if (!waitWithCountdown(
                    kSuccessfulUploadDelaySeconds,
                    QStringLiteral("net.upload_waiting_before_next_1"),
                    -1)) {
                break;
            }
        }
    }

    emit finished(succeeded, failed, isCanceled(), fatalError);
}

}  // namespace miacode::net
