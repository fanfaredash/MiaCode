#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace miacode::v2 {

enum class NoticeSeverity {
    Information,
    Warning,
    Error,
};

struct FileRequest {
    QString title;
    // File path for file pickers, directory path for folder pickers.  Empty is
    // allowed and means "let the platform choose".
    QString startPath;
    QStringList nameFilters;
    bool saveMode = false;
    bool selectFolder = false;
};

// The boundary that lets a Widgets-free application object ask the QML shell for
// a file path or show a message, without ever constructing a QFileDialog or a
// QMessageBox itself.
//
// Requests are values: the caller hands over a continuation, the shell answers
// later through submitFileResult() / cancelFileRequest(), and a cancelled pick
// arrives as an empty path rather than as a separate error channel.  Each
// request resolves at most once, so a shell that answers twice cannot re-run a
// continuation.
class UiRequestService final : public QObject
{
    Q_OBJECT

public:
    using FileCallback = std::function<void(const QString& path)>;
    // true when the viewer chose the notice's extra action.
    using NoticeCallback = std::function<void(bool actionChosen)>;
    // The id of the button that was pressed, or the request's dismissChoiceId
    // when the dialog was closed without pressing one.
    using ChoiceCallback = std::function<void(const QString& choiceId)>;

    explicit UiRequestService(QObject* parent = nullptr);

    // Returns the id the shell must echo back when the pick resolves.
    QString requestFile(const FileRequest& request, FileCallback onResolved);

    // Fire-and-forget message. Nothing is waiting on the viewer's dismissal.
    void postNotice(NoticeSeverity severity,
                    const QString& title,
                    const QString& text,
                    const QString& details = QString());

    // A yes/no question. The continuation runs once; declining and dismissing
    // are the same answer, so a caller can never mistake "closed the dialog"
    // for consent.
    QString requestConfirmation(const QString& title,
                                const QString& text,
                                const QString& acceptLabel,
                                NoticeCallback onResolved);

    // A question with more than two answers — 保存 / 放弃 / 取消 being the one
    // the app actually asks. `choices` are [{ id, label, role }] in button
    // order, role being "accept" | "destructive" | "reject" so the shell can
    // style them; `dismissChoiceId` is what closing the dialog resolves to, so
    // Escape and the window's close button can never mean anything the caller
    // did not list.
    QString requestChoice(const QString& title,
                          const QString& text,
                          const QVariantList& choices,
                          const QString& dismissChoiceId,
                          ChoiceCallback onResolved);

    // Message carrying one extra action button beside the dismissal. The
    // continuation runs once, with true only when that action was chosen.
    QString requestNoticeAction(NoticeSeverity severity,
                                const QString& title,
                                const QString& text,
                                const QString& details,
                                const QString& actionLabel,
                                NoticeCallback onResolved);

    int pendingFileRequestCount() const { return static_cast<int>(pendingFileRequests_.size()); }
    int pendingNoticeCount() const { return static_cast<int>(pendingNotices_.size()); }
    int pendingChoiceCount() const { return static_cast<int>(pendingChoices_.size()); }

    Q_INVOKABLE void submitFileResult(const QString& requestId, const QUrl& fileUrl);
    Q_INVOKABLE void cancelFileRequest(const QString& requestId);
    Q_INVOKABLE void submitNoticeResult(const QString& requestId, bool actionChosen);
    // An unknown choiceId resolves as the dismissal, not as nothing: a shell
    // that answers with a button the request never offered still leaves the
    // caller with a decision it listed.
    Q_INVOKABLE void submitChoiceResult(const QString& requestId, const QString& choiceId);

signals:
    void fileRequested(const QString& requestId, const QVariantMap& request);
    void noticeRequested(const QString& requestId, const QVariantMap& notice);
    void choiceRequested(const QString& requestId, const QVariantMap& request);

private:
    void resolve(const QString& requestId, const QString& path);
    QString emitNotice(NoticeSeverity severity,
                       const QString& title,
                       const QString& text,
                       const QString& details,
                       const QString& actionLabel,
                       const QString& requestId = QString(),
                       bool confirmation = false);

    QHash<QString, FileCallback> pendingFileRequests_;
    QHash<QString, NoticeCallback> pendingNotices_;
    struct PendingChoice {
        ChoiceCallback callback;
        QStringList offeredIds;
        QString dismissChoiceId;
    };
    QHash<QString, PendingChoice> pendingChoices_;
    quint64 nextRequestSerial_ = 1;
};

}  // namespace miacode::v2
