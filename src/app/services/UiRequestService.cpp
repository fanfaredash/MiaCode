#include "UiRequestService.h"

#include <QDir>
#include <QFileInfo>

namespace miacode {

namespace {

// Resolves FileRequest::startPath into the `startFolder` / `startFile` URLs the
// shell hands straight to its dialogs.  They must come from
// QUrl::fromLocalFile: spelling them in QML as "file://" + "C:/charts" parses
// the drive letter as a host, and the dialog then blocks the GUI thread for
// seconds per lookup resolving the SMB share \\c\charts before giving up.
void addStartLocation(const FileRequest& request, QVariantMap& payload)
{
    const QString path = request.startPath.trimmed();
    // Qt resource paths are not browsable folders.
    if (path.isEmpty() || path.startsWith(QLatin1Char(':'))) {
        return;
    }
    const QFileInfo info(QDir::cleanPath(path));
    const bool absolute = info.isAbsolute();
    if (request.selectFolder || info.isDir()) {
        if (absolute) {
            payload.insert(QStringLiteral("startFolder"),
                           QUrl::fromLocalFile(info.absoluteFilePath()));
        }
        return;
    }
    if (absolute) {
        payload.insert(QStringLiteral("startFolder"), QUrl::fromLocalFile(info.absolutePath()));
    }
    // An open dialog refuses a preselection that does not exist, while a save
    // dialog proposes the name even when there is no folder to open it in.
    if (request.saveMode || info.isFile()) {
        payload.insert(QStringLiteral("startFile"), QUrl::fromLocalFile(info.filePath()));
    }
}

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
    addStartLocation(request, payload);
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

QString UiRequestService::requestConfirmation(const QString& title,
                                              const QString& text,
                                              const QString& acceptLabel,
                                              NoticeCallback onResolved)
{
    const QString requestId = QStringLiteral("confirm-%1").arg(nextRequestSerial_++);
    pendingNotices_.insert(requestId, std::move(onResolved));
    emitNotice(NoticeSeverity::Information, title, text, QString(), acceptLabel, requestId,
               /*confirmation=*/true);
    return requestId;
}

QString UiRequestService::requestChoice(const QString& title,
                                       const QString& text,
                                       const QVariantList& choices,
                                       const QString& dismissChoiceId,
                                       ChoiceCallback onResolved)
{
    const QString requestId = QStringLiteral("choice-%1").arg(nextRequestSerial_++);

    PendingChoice pending;
    pending.callback = std::move(onResolved);
    pending.dismissChoiceId = dismissChoiceId;
    for (const QVariant& choice : choices) {
        const QString id = choice.toMap().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            pending.offeredIds.append(id);
        }
    }
    pendingChoices_.insert(requestId, std::move(pending));

    QVariantMap payload;
    payload.insert(QStringLiteral("title"), title);
    payload.insert(QStringLiteral("text"), text);
    payload.insert(QStringLiteral("choices"), choices);
    payload.insert(QStringLiteral("dismissChoiceId"), dismissChoiceId);
    emit choiceRequested(requestId, payload);
    return requestId;
}

void UiRequestService::submitChoiceResult(const QString& requestId, const QString& choiceId)
{
    const auto pending = pendingChoices_.find(requestId);
    if (pending == pendingChoices_.end()) {
        return;
    }
    // Take the continuation out before running it: a callback that asks the
    // next question must not observe its own entry.
    const PendingChoice resolved = std::move(pending.value());
    pendingChoices_.erase(pending);
    if (!resolved.callback) {
        return;
    }
    resolved.callback(resolved.offeredIds.contains(choiceId) ? choiceId : resolved.dismissChoiceId);
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
                                     const QString& requestId,
                                     bool confirmation)
{
    QVariantMap notice;
    notice.insert(QStringLiteral("confirmation"), confirmation);
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

}  // namespace miacode
