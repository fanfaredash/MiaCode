#include "UiRequestService.h"

namespace miacode::v2 {

namespace {

QString severityId(NoticeSeverity severity)
{
    switch (severity) {
    case NoticeSeverity::Warning:
        return QStringLiteral("warning");
    case NoticeSeverity::Error:
        return QStringLiteral("error");
    case NoticeSeverity::Information:
        break;
    }
    return QStringLiteral("information");
}

}  // namespace

UiRequestService::UiRequestService(QObject* parent)
    : QObject(parent)
{
}

QString UiRequestService::requestFile(const FileRequest& request, FileCallback onResolved)
{
    const QString requestId = QStringLiteral("file-%1").arg(nextRequestSerial_++);
    pendingFileRequests_.insert(requestId, std::move(onResolved));

    QVariantMap payload;
    payload.insert(QStringLiteral("title"), request.title);
    payload.insert(QStringLiteral("startPath"), request.startPath);
    payload.insert(QStringLiteral("nameFilters"), request.nameFilters);
    payload.insert(QStringLiteral("saveMode"), request.saveMode);
    payload.insert(QStringLiteral("selectFolder"), request.selectFolder);
    emit fileRequested(requestId, payload);
    return requestId;
}

void UiRequestService::postNotice(NoticeSeverity severity,
                                  const QString& title,
                                  const QString& text,
                                  const QString& details)
{
    emitNotice(severity, title, text, details, QString());
}

QString UiRequestService::requestNoticeAction(NoticeSeverity severity,
                                              const QString& title,
                                              const QString& text,
                                              const QString& details,
                                              const QString& actionLabel,
                                              NoticeCallback onResolved)
{
    const QString requestId = QStringLiteral("notice-%1").arg(nextRequestSerial_++);
    pendingNotices_.insert(requestId, std::move(onResolved));
    emitNotice(severity, title, text, details, actionLabel, requestId);
    return requestId;
}

QString UiRequestService::emitNotice(NoticeSeverity severity,
                                     const QString& title,
                                     const QString& text,
                                     const QString& details,
                                     const QString& actionLabel,
                                     const QString& requestId)
{
    QVariantMap notice;
    notice.insert(QStringLiteral("severity"), severityId(severity));
    notice.insert(QStringLiteral("title"), title);
    notice.insert(QStringLiteral("text"), text);
    notice.insert(QStringLiteral("details"), details);
    notice.insert(QStringLiteral("actionLabel"), actionLabel);
    emit noticeRequested(requestId, notice);
    return requestId;
}

void UiRequestService::submitNoticeResult(const QString& requestId, bool actionChosen)
{
    const auto pending = pendingNotices_.find(requestId);
    if (pending == pendingNotices_.end()) {
        return;
    }
    const NoticeCallback callback = std::move(pending.value());
    pendingNotices_.erase(pending);
    if (callback) {
        callback(actionChosen);
    }
}

void UiRequestService::submitFileResult(const QString& requestId, const QUrl& fileUrl)
{
    resolve(requestId, fileUrl.isLocalFile() ? fileUrl.toLocalFile() : QString());
}

void UiRequestService::cancelFileRequest(const QString& requestId)
{
    resolve(requestId, QString());
}

void UiRequestService::resolve(const QString& requestId, const QString& path)
{
    const auto pending = pendingFileRequests_.find(requestId);
    if (pending == pendingFileRequests_.end()) {
        return;
    }
    // Take the continuation out before running it: a callback that starts
    // another pick must not observe its own entry.
    const FileCallback callback = std::move(pending.value());
    pendingFileRequests_.erase(pending);
    if (callback) {
        callback(path);
    }
}

}  // namespace miacode::v2
