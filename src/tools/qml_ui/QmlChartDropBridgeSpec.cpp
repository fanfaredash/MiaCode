#include "app/qml_ui/drop/QmlChartDropBridge.h"

#include <QGuiApplication>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

#include <functional>

namespace {

bool require(bool condition, const QString& message, QTextStream& out)
{
    if (!condition) {
        out << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool testContract(QTextStream& out)
{
    QObject window;
    bool rootAcceptsDrops = false;
    QTemporaryDir temp;
    const QString trackPath = temp.filePath(QStringLiteral("track.mp3"));
    const QString unsupportedPath = temp.filePath(QStringLiteral("notes.txt"));
    QFile track(trackPath);
    QFile unsupported(unsupportedPath);
    if (!require(temp.isValid() && track.open(QIODevice::WriteOnly)
                     && unsupported.open(QIODevice::WriteOnly),
                 QStringLiteral("track fixtures"), out)) {
        return false;
    }
    track.write("track");
    track.close();
    unsupported.write("notes");
    unsupported.close();

    QMimeData supportedMime;
    supportedMime.setUrls({QUrl::fromLocalFile(trackPath)});
    QMimeData unsupportedMime;
    unsupportedMime.setUrls({QUrl::fromLocalFile(unsupportedPath)});
    int submitted = 0;
    int completed = 0;
    quint64 requestId = 0;
    std::function<void(const miacode::qml_ui::QmlChartDropResult&)> delayedCompletion;
    miacode::qml_ui::QmlChartDropBridge bridge(
        window,
        [&rootAcceptsDrops] { rootAcceptsDrops = true; },
        [&submitted, &requestId, &delayedCompletion](const QStringList&, quint64 id, quint64,
                                                     std::function<void(const miacode::qml_ui::QmlChartDropResult&)> done) {
            ++submitted;
            requestId = id;
            delayedCompletion = std::move(done);
        },
        [&completed](const miacode::qml_ui::QmlChartDropResult&) { ++completed; });

    if (!require(rootAcceptsDrops,
                 QStringLiteral("bridge preserves the QML item drop route"), out)) {
        return false;
    }
    QDragEnterEvent unsupportedEnter(QPoint(10, 10), Qt::CopyAction, &unsupportedMime,
                                     Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &unsupportedEnter);
    if (!require(!unsupportedEnter.isAccepted() && submitted == 0,
                 QStringLiteral("unsupported drops continue to other handlers"), out)) {
        return false;
    }

    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &supportedMime,
                          Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &enter);
    if (!require(enter.isAccepted() && bridge.dragActive() && submitted == 0,
                 QStringLiteral("supported drag enter is accepted without submitting"), out)) {
        return false;
    }
    QDragLeaveEvent leave;
    QCoreApplication::sendEvent(&window, &leave);
    QDragMoveEvent move(QPoint(10, 10), Qt::CopyAction, &supportedMime,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &move);
    if (!require(move.isAccepted() && bridge.dragActive(),
                 QStringLiteral("drag move keeps the leave timer from hiding the overlay"), out)) {
        return false;
    }

    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &supportedMime,
                    Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &drop);
    if (!require(drop.isAccepted() && submitted == 1 && requestId != 0 && bridge.busy(),
                 QStringLiteral("supported local track drop is submitted exactly once"), out)) {
        return false;
    }

    QDropEvent busyDrop(QPointF(10, 10), Qt::CopyAction, &supportedMime,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &busyDrop);
    if (!require(!busyDrop.isAccepted() && submitted == 1,
                 QStringLiteral("busy request rejects a second submission"), out)) {
        return false;
    }

    bridge.release();
    bridge.release();
    if (delayedCompletion) {
        delayedCompletion(miacode::qml_ui::QmlChartDropResult{requestId, 1, true, true, false, 1, 0, {}});
    }
    return require(submitted == 1 && completed == 0 && !bridge.busy(),
                   QStringLiteral("release invalidates the active request without callback"), out);
}

} // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QTextStream out(stdout);
    const bool ok = testContract(out);
    if (ok) {
        out << "qml_chart_drop_bridge_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
