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
//
// The same file also pins who OWNS the export/latency audition's playback
// readiness, because that was the same mistake in another form: the audition
// borrowed the edited difficulty's snapshot fields, so difficulty bookkeeping
// silently wedged a page that has no difficulty.

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

    // 3. 发布播放头的那**一个**地方会广播出去。
    //
    // 它是 updatePreviewSliderPosition 而不是 applyQtPreviewPosition：后者只是
    // 众多调用方之一，而导出片头的 lead-in 会移动播放头却从不经过它——那正是
    // 「片头在放、QML 滑块停在 0」的成因。
    const QString layoutUi = readSource(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.TimelineLayoutUI.cpp"));
    const int publishAt = layoutUi.indexOf(
        QStringLiteral("void MainWindow::TimelineSection::updatePreviewSliderPosition"));
    expect(publishAt >= 0, QStringLiteral("updatePreviewSliderPosition exists"), out, &failed);
    if (publishAt >= 0) {
        const int nextFunctionAt = layoutUi.indexOf(QStringLiteral("\nvoid MainWindow::"), publishAt + 1);
        const QString publishBody = layoutUi.mid(
            publishAt, nextFunctionAt > publishAt ? nextFunctionAt - publishAt : -1);
        const int emitAt = publishBody.indexOf(
            QStringLiteral("emit owner_.shellPreviewPlayheadChanged()"));
        const int guardAt = publishBody.indexOf(QStringLiteral("ui_.previewSlider_ == nullptr"));
        expect(emitAt >= 0, QStringLiteral("updatePreviewSliderPosition announces the playhead"),
               out, &failed);
        // Before the guard: the guard is about a v1 widget that may be gone.
        expect(emitAt >= 0 && guardAt > emitAt,
               QStringLiteral("it announces before the v1 widget guard"), out, &failed);
    }

    // 4. 片头 lead-in 走的是同一个发布点。
    const QString introRegion = readSource(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.PreviewIntroRegion.cpp"));
    const int tickAt = introRegion.indexOf(
        QStringLiteral("void MainWindow::TimelineSection::tickExportIntroLeadIn"));
    expect(tickAt >= 0, QStringLiteral("tickExportIntroLeadIn exists"), out, &failed);
    if (tickAt >= 0) {
        const int nextFunctionAt = introRegion.indexOf(QStringLiteral("\nbool MainWindow::"), tickAt + 1);
        const QString tickBody = introRegion.mid(
            tickAt, nextFunctionAt > tickAt ? nextFunctionAt - tickAt : -1);
        expect(tickBody.contains(QStringLiteral("updatePreviewSliderPosition(")),
               QStringLiteral("the export intro publishes its playhead the same way"), out, &failed);
    }

    // 5. 播放/暂停呈现也会广播——片头 lead-in 不写 qtPreviewPlaying_，所以单一写入者
    //    那条播报覆盖不到它。
    const QString editorState = readSource(
        QStringLiteral("src/app/mainwindow/sections/document/MainWindow.DocumentEditorState.cpp"));
    const int pauseAt = editorState.indexOf(
        QStringLiteral("void MainWindow::DocumentSection::updatePauseButtonAppearance"));
    expect(pauseAt >= 0, QStringLiteral("updatePauseButtonAppearance exists"), out, &failed);
    if (pauseAt >= 0) {
        const int nextFunctionAt = editorState.indexOf(QStringLiteral("\nvoid MainWindow::"), pauseAt + 1);
        const QString pauseBody = editorState.mid(
            pauseAt, nextFunctionAt > pauseAt ? nextFunctionAt - pauseAt : -1);
        expect(pauseBody.contains(QStringLiteral("shellPresentationChanged()")),
               QStringLiteral("the play/pause presentation announces itself"), out, &failed);
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

    // 6. 试听场景的播放就绪是它自己的，且失配时能自愈。
    const QString playbackGlue = readSource(
        QStringLiteral("src/app/mainwindow/sections/timeline/MainWindow.PreviewPlaybackGlue.cpp"));
    const int gateAt = playbackGlue.indexOf(
        QStringLiteral("bool MainWindow::TimelineSection::preparePreviewStartState"));
    expect(gateAt >= 0, QStringLiteral("preparePreviewStartState exists"), out, &failed);
    if (gateAt >= 0) {
        const QString gateBody = playbackGlue.mid(gateAt);
        const int auditionAt = gateBody.indexOf(QStringLiteral("exportPreviewAuditionActive_"));
        const int ordinaryAt = gateBody.indexOf(QStringLiteral("hasActiveDifficulty()"));
        const QString auditionBranch = (auditionAt >= 0 && ordinaryAt > auditionAt)
            ? gateBody.mid(auditionAt, ordinaryAt - auditionAt)
            : QString();
        expect(auditionBranch.contains(QStringLiteral("ensureAuditionSceneReady()")),
               QStringLiteral("the audition branch asks its own readiness"), out, &failed);
        // The pair below belongs to the difficulty being edited. On the export
        // page there is none, which is exactly why reading it here wedged.
        expect(!auditionBranch.contains(QStringLiteral("latestTimelinePreviewSnapshotReady_"))
                   && !auditionBranch.contains(QStringLiteral("timelineRevision_")),
               QStringLiteral("it no longer borrows the edited difficulty's snapshot"), out, &failed);
    }
    expect(playbackGlue.contains(QStringLiteral("bool MainWindow::ensureAuditionSceneReady"))
               && playbackGlue.contains(QStringLiteral("reinstall()")),
           QStringLiteral("a stale audition scene rebuilds instead of refusing forever"), out, &failed);

    // 7. 两个安装方都登记，两个拆除方都清除。
    const QString exportSnapshot = readSource(
        QStringLiteral("src/app/mainwindow/sections/export/MainWindow.ExportSnapshot.cpp"));
    expect(exportSnapshot.contains(QStringLiteral("setAuditionSceneReady("))
               && exportSnapshot.contains(QStringLiteral("clearAuditionSceneReady()")),
           QStringLiteral("the export audition registers and releases its scene"), out, &failed);
    const QString latencySandbox = readSource(
        QStringLiteral("src/tools/latency/LatencySandboxController.cpp"));
    expect(latencySandbox.contains(QStringLiteral("setAuditionSceneReady("))
               && latencySandbox.contains(QStringLiteral("clearAuditionSceneReady()")),
           QStringLiteral("the latency sandbox registers and releases its scene"), out, &failed);

    out << (failed == 0 ? "ALL PASS" : QStringLiteral("%1 FAILED").arg(failed)) << '\n';
    return failed == 0 ? 0 : 1;
}
