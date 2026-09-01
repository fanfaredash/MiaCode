// Drift guard for how the window learns that the QSG timeline can be written to.
//
// The bug this pins: the timeline playhead froze during playback, while
// scrubbing still moved the preview and 跟随预览 still moved the caret.
//
// TimelineQuickItem refuses to be written to until it has a window, a state
// bridge and a non-zero size, so the window holds every timeline write back
// behind quickTimelineBridgeReady() and waits to be told. The report travels
// QML -> QmlTimelineModel::surfaceReady() -> the timeline surface -> the
// window's quickTimelineSurfaceReady_ flag.
//
// Routing that call through miacode::v2::TimelineSurface broke it. MainWindow
// carried two members named shellTimelineSurfaceReady — a `bool ... const`
// getter and a `void` notifier — and the interface kept only the getter's
// shape. The QML side went on calling "timelineSurfaceReady", which now asked a
// question and discarded the answer, so the flag never flipped. Everything
// outside the gate kept working, which is why only the timeline looked stuck:
// applyQtPreviewPosition still drove previewCanvas_, and
// flushQtPreviewTimelinePosition still ran syncEditorCursorToPreviewSecond.
//
// The invariant now: the readiness report is a command, it reaches the flag,
// and the flag is the one thing standing between playback and the timeline.

#include "app/v2/ShellNotifications.h"
#include "app/v2/TimelineSurface.h"
#include "app/qml_ui/QmlTimelineModel.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined (repo root absolute path)"
#endif

namespace {

bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    out << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    out.flush();
    if (!condition) ++*failed;
    return condition;
}

QString sourceRoot() { return QStringLiteral(MIACODE_SOURCE_ROOT); }

QString readSource(const QString& relativePath)
{
    QFile file(sourceRoot() + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}

// The body of a free-standing function definition, from its signature to the
// first line that closes it at column zero.
QString functionBody(const QString& source, const QString& signature)
{
    const qsizetype start = source.indexOf(signature);
    if (start < 0) return QString();
    const qsizetype end = source.indexOf(QStringLiteral("\n}\n"), start);
    return source.mid(start, (end >= 0 ? end : source.size()) - start);
}

// Every line in src/ that writes the flag, with the file it came from.
QStringList readyFlagWriteSites()
{
    static const QRegularExpression assignment(
        QStringLiteral("quickTimelineSurfaceReady_\\s*=[^=]"));
    QStringList sites;
    QDirIterator it(sourceRoot() + QStringLiteral("/src"),
                    QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                                QStringLiteral("*.inc"), QStringLiteral("*.mm")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QString text = QString::fromUtf8(file.readAll());
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            // The storage declaration and the reference-binding aliases in
            // MainWindowMemberStorage.inc introduce the flag; they do not write it.
            if (line.trimmed().startsWith(QStringLiteral("bool"))) continue;
            if (assignment.match(line).hasMatch()) {
                sites << QFileInfo(path).fileName() + QStringLiteral(": ") + line.trimmed();
            }
        }
    }
    sites.sort();
    return sites;
}

// A stand-in surface. Its only job is to prove the report is deliverable
// through the contract; the production one is MainWindow.
class FakeTimelineSurface final : public miacode::v2::TimelineSurface
{
public:
    QObject* timelineStateBridge() const override { return nullptr; }
    void noteTimelineSurfaceReady() override { ++readyReports; }

    void navigateToSecond(double) override {}
    void centerOnSecond(double) override {}
    void wheelNavigateToSecond(double) override {}
    void timelineDragStarted() override {}
    void timelineDragFinished(double) override {}
    void timelineUserInteractionStarted() override {}
    void setFollowPreviewEnabled(bool) override {}
    QString bottomTabsCurrentTabId() const override { return QString(); }
    void setBottomTabsCurrentTabId(const QString&) override {}
    bool bottomTabsVisible() const override { return true; }
    bool timelineTabVisible() const override { return true; }
    bool muriTabVisible() const override { return false; }
    bool validationTabVisible() const override { return false; }
    bool ignoreMuriIssuePrompts() const override { return false; }

    int readyReports = 0;
};

// The report has to survive the trip. A question would compile and run and
// change nothing, which is exactly how this broke.
bool verifyTheReportReachesTheSurface(QTextStream& out, int* failed)
{
    miacode::v2::ShellNotifications notifications;
    FakeTimelineSurface surface;
    miacode::v2::TimelineSurface* installed = &surface;
    miacode::qml_ui::QmlTimelineModel model(notifications, installed);

    bool ok = expect(surface.readyReports == 0,
                     QStringLiteral("a fresh model has reported nothing"), out, failed);

    model.surfaceReady();
    ok &= expect(surface.readyReports == 1,
                 QStringLiteral("the QML item's ready report reaches the surface"), out, failed);

    // The item can go through readiness again — a reparent, a resize back from
    // zero. De-duplicating is the window's job (noteQuickTimelineSurfaceReady
    // returns early once the flag is set); the model must not swallow repeats,
    // or a genuine re-arm after the flag was cleared would be lost.
    model.surfaceReady();
    ok &= expect(surface.readyReports == 2,
                 QStringLiteral("a repeated report is forwarded, not swallowed"), out, failed);
    return ok;
}

// Withdrawing the surface must be visible at once: the model binds the slot,
// not a copy, so a teardown cannot leave it calling into a dead window.
bool verifyAWithdrawnSurfaceIsInert(QTextStream& out, int* failed)
{
    miacode::v2::ShellNotifications notifications;
    FakeTimelineSurface surface;
    miacode::v2::TimelineSurface* installed = &surface;
    miacode::qml_ui::QmlTimelineModel model(notifications, installed);

    installed = nullptr;
    model.surfaceReady();
    return expect(surface.readyReports == 0,
                  QStringLiteral("no surface installed means the report goes nowhere, not "
                                 "into a stale pointer"), out, failed);
}

// The shape of the contract member is the bug. A `bool ... const` named for an
// event reads as a getter at every call site, and C++ will happily let a caller
// invoke one for effect.
bool verifyTheContractDeclaresACommand(QTextStream& out, int* failed)
{
    const QString contract = readSource(QStringLiteral("src/app/v2/TimelineSurface.h"));
    bool ok = expect(!contract.isEmpty(),
                     QStringLiteral("TimelineSurface.h is readable"), out, failed);
    if (!ok) return false;

    ok &= expect(contract.contains(QStringLiteral("virtual void noteTimelineSurfaceReady() = 0;")),
                 QStringLiteral("readiness is announced through a void command"), out, failed);
    ok &= expect(!contract.contains(QStringLiteral("timelineSurfaceReady() const")),
                 QStringLiteral("the contract carries no same-shaped readiness getter for a "
                                "call site to bind to by accident"), out, failed);
    return ok;
}

// The window's forwarder is where the report turns into the flag. It reached a
// getter instead, which is why the whole chain went quiet without a diagnostic.
bool verifyTheWindowForwardsToTheFlag(QTextStream& out, int* failed)
{
    const QString host = readSource(
        QStringLiteral("src/app/mainwindow/sections/window/MainWindow.BottomTabsHost.cpp"));
    bool ok = expect(!host.isEmpty(),
                     QStringLiteral("MainWindow.BottomTabsHost.cpp is readable"), out, failed);
    if (!ok) return false;

    const QString forwarder = functionBody(
        host, QStringLiteral("void MainWindow::noteTimelineSurfaceReady()"));
    ok &= expect(!forwarder.isEmpty(),
                 QStringLiteral("the window implements the readiness command"), out, failed);
    ok &= expect(forwarder.contains(QStringLiteral("noteQuickTimelineSurfaceReady()")),
                 QStringLiteral("the command reaches the flag rather than reading it back"),
                 out, failed);

    const QString header = readSource(QStringLiteral("src/app/mainwindow/MainWindow.h"));
    ok &= expect(!header.contains(QStringLiteral("void shellTimelineSurfaceReady();")),
                 QStringLiteral("no void notifier shares a name with the "
                                "shellTimelineSurfaceReady getter — that overload pair is what "
                                "let the refactor pick the wrong member silently"), out, failed);
    return ok;
}

// Why a lost report freezes only the timeline: the flag gates the bridge
// writes, and nothing else on the playback tick.
bool verifyTheFlagIsWhatGatesPlaybackWrites(QTextStream& out, int* failed)
{
    const QString flow = readSource(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.PreviewTimelineFlow.cpp"));
    const QString tick = readSource(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp"));
    bool ok = expect(!flow.isEmpty() && !tick.isEmpty(),
                     QStringLiteral("the timeline section sources are readable"), out, failed);
    if (!ok) return false;

    const QString gate = functionBody(
        flow, QStringLiteral("bool MainWindow::TimelineSection::quickTimelineBridgeReady() const"));
    ok &= expect(gate.contains(QStringLiteral("state_.quickTimelineSurfaceReady_")),
                 QStringLiteral("the write gate is the readiness flag"), out, failed);

    const QString flush = functionBody(
        tick, QStringLiteral("void MainWindow::TimelineSection::flushQtPreviewTimelinePosition()"));
    ok &= expect(!flush.isEmpty(), QStringLiteral("the playback flush is present"), out, failed);
    ok &= expect(flush.contains(QStringLiteral("quickTimelineBridgeReady()"))
                     && flush.contains(QStringLiteral("setPlayheadSeconds(")),
                 QStringLiteral("the playback flush writes the playhead behind that gate"),
                 out, failed);
    // The two behaviours the user still saw working, so a future edit that
    // moves them inside the gate has to justify itself.
    ok &= expect(flush.contains(QStringLiteral("syncEditorCursorToPreviewSecond(")),
                 QStringLiteral("跟随预览 runs outside the gate, so a stuck timeline never "
                                "explains itself by the caret stopping too"), out, failed);

    const QStringList writes = readyFlagWriteSites();
    ok &= expect(writes.size() == 2, QStringLiteral("the readiness flag has exactly two writers, "
                                                    "one arming it and one clearing it; found: ")
                                         + writes.join(QStringLiteral(" | ")), out, failed);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    int failed = 0;

    verifyTheReportReachesTheSurface(out, &failed);
    verifyAWithdrawnSurfaceIsInert(out, &failed);
    verifyTheContractDeclaresACommand(out, &failed);
    verifyTheWindowForwardsToTheFlag(out, &failed);
    verifyTheFlagIsWhatGatesPlaybackWrites(out, &failed);

    if (failed == 0) {
        out << "timeline_surface_ready_spec: OK\n";
    } else {
        out << "timeline_surface_ready_spec: " << failed << " failure(s)\n";
    }
    out.flush();
    return failed == 0 ? 0 : 1;
}
