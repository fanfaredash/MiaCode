#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
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

    explicit UiRequestService(QObject* parent = nullptr);

    // Returns the id the shell must echo back when the pick resolves.
    QString requestFile(const FileRequest& request, FileCallback onResolved);
    void postNotice(NoticeSeverity severity,
                    const QString& title,
                    const QString& text,
                    const QString& details = QString());

    int pendingFileRequestCount() const { return static_cast<int>(pendingFileRequests_.size()); }

    Q_INVOKABLE void submitFileResult(const QString& requestId, const QUrl& fileUrl);
    Q_INVOKABLE void cancelFileRequest(const QString& requestId);

signals:
    void fileRequested(const QString& requestId, const QVariantMap& request);
    void noticeRequested(const QVariantMap& notice);

private:
    void resolve(const QString& requestId, const QString& path);

    QHash<QString, FileCallback> pendingFileRequests_;
    quint64 nextRequestSerial_ = 1;
};

}  // namespace miacode::v2
