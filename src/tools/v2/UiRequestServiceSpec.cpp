// Contract regression for the Widgets-free UI request boundary.
//
// This target deliberately links Qt6::Core only.  If any file it pulls in
// reaches for QFileDialog / QMessageBox again, the spec fails to build, which
// is a stronger guarantee than scanning the source for forbidden strings.

#include "app/v2/UiRequestService.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTextStream>
#include <QUrl>
#include <QVariantMap>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

miacode::v2::FileRequest sampleRequest()
{
    miacode::v2::FileRequest request;
    request.title = QStringLiteral("Choose output");
    request.startPath = QStringLiteral("/tmp/out.mp4");
    request.nameFilters = QStringList{QStringLiteral("MP4 (*.mp4)")};
    request.saveMode = true;
    return request;
}

bool verifyFileRequestRoundTrip(QTextStream& err)
{
    miacode::v2::UiRequestService service;
    QSignalSpy requested(&service, &miacode::v2::UiRequestService::fileRequested);

    QStringList resolved;
    const QString id = service.requestFile(sampleRequest(), [&resolved](const QString& path) {
        resolved.append(path);
    });

    bool ok = require(!id.isEmpty() && requested.count() == 1
                          && service.pendingFileRequestCount() == 1,
                      QStringLiteral("requestFile emits exactly one request and keeps it pending"),
                      err);
    if (!ok) {
        return false;
    }

    const QVariantMap payload = requested.at(0).at(1).toMap();
    ok &= require(requested.at(0).at(0).toString() == id
                      && payload.value(QStringLiteral("title")).toString()
                          == QStringLiteral("Choose output")
                      && payload.value(QStringLiteral("startPath")).toString()
                          == QStringLiteral("/tmp/out.mp4")
                      && payload.value(QStringLiteral("nameFilters")).toStringList()
                          == QStringList{QStringLiteral("MP4 (*.mp4)")}
                      && payload.value(QStringLiteral("saveMode")).toBool()
                      && !payload.value(QStringLiteral("selectFolder")).toBool(),
                  QStringLiteral("the emitted payload carries every field QML needs to build the dialog"),
                  err);

    service.submitFileResult(id, QUrl::fromLocalFile(QStringLiteral("/tmp/picked.mp4")));
    ok &= require(resolved == QStringList{QStringLiteral("/tmp/picked.mp4")}
                      && service.pendingFileRequestCount() == 0,
                  QStringLiteral("submitting a result delivers the local path once and clears the request"),
                  err);

    service.submitFileResult(id, QUrl::fromLocalFile(QStringLiteral("/tmp/again.mp4")));
    service.cancelFileRequest(id);
    ok &= require(resolved.size() == 1,
                  QStringLiteral("a resolved request never fires again for a repeated submit or cancel"),
                  err);
    return ok;
}

bool verifyCancellationAndUnknownIds(QTextStream& err)
{
    miacode::v2::UiRequestService service;
    QStringList resolved;
    const QString id = service.requestFile(sampleRequest(), [&resolved](const QString& path) {
        resolved.append(path);
    });

    service.cancelFileRequest(QStringLiteral("no-such-request"));
    service.submitFileResult(QStringLiteral("no-such-request"),
                            QUrl::fromLocalFile(QStringLiteral("/tmp/x.mp4")));
    bool ok = require(resolved.isEmpty() && service.pendingFileRequestCount() == 1,
                      QStringLiteral("unknown request ids are ignored without touching pending work"),
                      err);

    service.cancelFileRequest(id);
    ok &= require(resolved == QStringList{QString()} && service.pendingFileRequestCount() == 0,
                  QStringLiteral("cancelling delivers an empty path so callers can treat it as a value"),
                  err);

    miacode::v2::UiRequestService other;
    QString firstPath;
    QString secondPath;
    const QString firstId = other.requestFile(sampleRequest(),
                                              [&firstPath](const QString& p) { firstPath = p; });
    miacode::v2::FileRequest folderRequest;
    folderRequest.title = QStringLiteral("Choose folder");
    folderRequest.selectFolder = true;
    const QString secondId = other.requestFile(folderRequest,
                                               [&secondPath](const QString& p) { secondPath = p; });
    ok &= require(firstId != secondId && other.pendingFileRequestCount() == 2,
                  QStringLiteral("concurrent requests get distinct ids"), err);

    other.submitFileResult(secondId, QUrl::fromLocalFile(QStringLiteral("/tmp/folder")));
    ok &= require(secondPath == QStringLiteral("/tmp/folder") && firstPath.isEmpty()
                      && other.pendingFileRequestCount() == 1,
                  QStringLiteral("resolving one request leaves the other pending and untouched"),
                  err);
    return ok;
}

bool verifyNotices(QTextStream& err)
{
    miacode::v2::UiRequestService service;
    QSignalSpy notices(&service, &miacode::v2::UiRequestService::noticeRequested);

    service.postNotice(miacode::v2::NoticeSeverity::Error,
                       QStringLiteral("Export"),
                       QStringLiteral("Launch failed"),
                       QStringLiteral("ffmpeg missing"));
    service.postNotice(miacode::v2::NoticeSeverity::Information, QStringLiteral("Export"),
                       QStringLiteral("Done"));
    service.postNotice(miacode::v2::NoticeSeverity::Warning, QStringLiteral("Export"),
                       QStringLiteral("Partly failed"));

    bool ok = require(notices.count() == 3, QStringLiteral("every notice is published"), err);
    if (!ok) {
        return false;
    }
    const QVariantMap error = notices.at(0).at(0).toMap();
    ok &= require(error.value(QStringLiteral("severity")).toString() == QStringLiteral("error")
                      && error.value(QStringLiteral("title")).toString() == QStringLiteral("Export")
                      && error.value(QStringLiteral("text")).toString()
                          == QStringLiteral("Launch failed")
                      && error.value(QStringLiteral("details")).toString()
                          == QStringLiteral("ffmpeg missing"),
                  QStringLiteral("an error notice carries severity, title, text and details"), err);
    ok &= require(notices.at(1).at(0).toMap().value(QStringLiteral("severity")).toString()
                      == QStringLiteral("information")
                      && notices.at(2).at(0).toMap().value(QStringLiteral("severity")).toString()
                          == QStringLiteral("warning"),
                  QStringLiteral("severity maps to stable lowercase identifiers for QML"), err);
    ok &= require(notices.at(1).at(0).toMap().value(QStringLiteral("details")).toString().isEmpty(),
                  QStringLiteral("an omitted detail block stays empty rather than absent"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    const bool ok = verifyFileRequestRoundTrip(err) && verifyCancellationAndUnknownIds(err)
        && verifyNotices(err);
    if (ok) {
        QTextStream out(stdout);
        out << "ui_request_service_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
