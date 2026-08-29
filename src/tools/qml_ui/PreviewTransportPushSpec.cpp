// Drift guard for how the QML transport learns that the preview moved.
//
// The bug this pins: retiring QuickShellController removed a polling timer that
// had been the shell's only source of preview state. The replacement pushed the
// bottom-panel half and left the preview half to a timer gated on `playing_` —
// but nothing ever told the model that playback had started, so the gate never
// opened and the transport sat at 0 while the timeline followed along fine.
//
// The invariant now: the playhead is announced by the one function that moves
// it, the playing flag by the one function that writes it, and the model
// samples nothing.

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

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

// Every assignment to the flag, wherever it lives, with the file it came from.
QStringList playingFlagAssignmentSites()
{
    static const QRegularExpression assignment(
        QStringLiteral("qtPreviewPlaying_\\s*=[^=]"));
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
        for (int i = 0; i < lines.size(); ++i) {
            if (!assignment.match(lines.at(i)).hasMatch()) continue;
            // The storage block declares the member and its reference alias;
            // those are definitions, not writers.
            if (path.endsWith(QStringLiteral("MainWindowMemberStorage.inc"))) continue;
            sites.append(QStringLiteral("%1:%2")
                             .arg(QFileInfo(path).fileName())
                             .arg(i + 1));
        }
    }
    sites.sort();
    return sites;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);
    QTextStream out(stdout);
    int failed = 0;

    // 1. 播放标志只有一个写入者。
    const QStringList sites = playingFlagAssignmentSites();
    if (sites.size() != 1) {
        out << "  assignment sites: " << sites.join(QStringLiteral(", ")) << '\n';
    }
    expect(sites.size() == 1 && sites.first().startsWith(QStringLiteral("MainWindow.WindowSection.cpp")),
           QStringLiteral("qtPreviewPlaying_ is assigned in exactly one place"),
           out, &failed);

    // 2. 那个写入者会广播出去。
    const QString windowSection = readSource(
        QStringLiteral("src/app/mainwindow/sections/window/MainWindow.WindowSection.cpp"));
    expect(!windowSection.isEmpty(), QStringLiteral("MainWindow.WindowSection.cpp is readable"),
           out, &failed);
    const int writerAt = windowSection.indexOf(QStringLiteral("void MainWindow::setPreviewPlayingFlag"));
    expect(writerAt >= 0, QStringLiteral("setPreviewPlayingFlag exists"), out, &failed);
    if (writerAt >= 0) {
        const QString writerBody = windowSection.mid(writerAt, 900);
        expect(writerBody.contains(QStringLiteral("emit shellPresentationChanged()")),
               QStringLiteral("setPreviewPlayingFlag announces the flip"), out, &failed);
    }

    // 3. 移动播放头的那个函数会广播出去。
    const QString previewTick = readSource(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.PreviewTick.cpp"));
    const int applyAt = previewTick.indexOf(
        QStringLiteral("void MainWindow::TimelineSection::applyQtPreviewPosition"));
    expect(applyAt >= 0, QStringLiteral("applyQtPreviewPosition exists"), out, &failed);
    if (applyAt >= 0) {
        const int nextFunctionAt = previewTick.indexOf(QStringLiteral("\nvoid MainWindow::"), applyAt + 1);
        const QString applyBody = previewTick.mid(
            applyAt, nextFunctionAt > applyAt ? nextFunctionAt - applyAt : -1);
        expect(applyBody.contains(QStringLiteral("emit owner_.shellPreviewPlayheadChanged()")),
               QStringLiteral("applyQtPreviewPosition announces the playhead"), out, &failed);
    }

    // 4. QML 侧接收推送，且不再自己采样。
    const QString modelHeader = readSource(QStringLiteral("src/app/qml_ui/QmlPreviewModel.h"));
    const QString modelSource = readSource(QStringLiteral("src/app/qml_ui/QmlPreviewModel.cpp"));
    expect(modelSource.contains(QStringLiteral("MainWindow::shellPreviewPlayheadChanged")),
           QStringLiteral("QmlPreviewModel listens for the playhead"), out, &failed);
    expect(modelSource.contains(QStringLiteral("MainWindow::shellPresentationChanged")),
           QStringLiteral("QmlPreviewModel listens for shell presentation"), out, &failed);
    // A timer here would mean the transport is sampling MainWindow again, which
    // is what let a missing announcement go unnoticed in the first place.
    expect(!modelHeader.contains(QStringLiteral("QTimer"))
               && !modelSource.contains(QStringLiteral("QTimer")),
           QStringLiteral("QmlPreviewModel polls nothing"), out, &failed);

    out << (failed == 0 ? "ALL PASS" : QStringLiteral("%1 FAILED").arg(failed)) << '\n';
    return failed == 0 ? 0 : 1;
}
