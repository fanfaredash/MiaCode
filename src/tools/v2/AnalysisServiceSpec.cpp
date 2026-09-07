#include "app/v2/AnalysisService.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTextStream>
#include <QTimer>

namespace {

bool expect(bool condition, const QString& message, QTextStream& out)
{
    if (!condition) out << "FAIL: " << message << Qt::endl;
    return condition;
}

QString sourceText()
{
    return QStringLiteral(
        "&first=1.25\n"
        "&lv_5=12\n"
        "&inote_5=(120){4}1,2,\n");
}

bool verifyRevisionStampedAnalysis(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    const auto opened = workspace.openSource(sourceText());
    const auto first = miacode::v2::AnalysisService::analyze(workspace);
    bool ok = expect(opened.accepted && first.available && first.revision == opened.revision
                         && first.difficultyId == 5 && first.validation.ok
                         && !first.noteMarkers.isEmpty() && !first.noteMarkerSignature.isEmpty(),
                     QStringLiteral("analysis is derived from one identified workspace snapshot"), out);

    const auto edited = workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}3,"));
    const auto second = miacode::v2::AnalysisService::analyze(workspace);
    ok &= expect(edited.accepted && second.available && second.revision == edited.revision
                     && second.revision != first.revision
                     && second.noteMarkerSignature != first.noteMarkerSignature,
                 QStringLiteral("a newer workspace revision yields a distinguishable analysis snapshot"), out);
    return ok;
}

bool verifyProductionPublicationDropsStaleWork(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    miacode::v2::AnalysisService service(workspace);
    QVector<miacode::v2::AnalysisSnapshot> publications;
    QObject::connect(
        &service, &miacode::v2::AnalysisService::snapshotChanged,
        &service, [&] { publications.append(service.snapshot()); });

    const auto opened = workspace.openSource(sourceText());
    const auto edited = workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}3,"));
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(
        &service, &miacode::v2::AnalysisService::analysisReady,
        &loop, [&](int difficultyId, quint64 revision) {
            if (difficultyId == 5 && revision == edited.revision) loop.quit();
        });
    timeout.start(10000);
    loop.exec();

    const miacode::v2::AnalysisSnapshot current = service.snapshot();
    bool oldAvailable = false;
    bool openedPending = false;
    bool editedPending = false;
    for (const auto& publication : publications) {
        openedPending = openedPending
            || (publication.revision == opened.revision && publication.pending
                && !publication.available);
        editedPending = editedPending
            || (publication.revision == edited.revision && publication.pending
                && !publication.available);
        oldAvailable = oldAvailable
            || (publication.revision == opened.revision && publication.available);
    }
    return expect(opened.accepted && edited.accepted && openedPending && editedPending,
                  QStringLiteral("each workspace identity atomically publishes pending before analysis"), out)
        && expect(!oldAvailable,
                  QStringLiteral("a completed old worker never publishes after a newer identity is requested"), out)
        && expect(current.available && !current.pending && current.difficultyId == 5
                      && current.revision == edited.revision && !current.noteMarkers.isEmpty()
                      && !current.noteMarkerSignature.isEmpty(),
                  QStringLiteral("the latest identity publishes validation, markers, Muri, and static references as one available snapshot"), out);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stderr);
    if (!verifyRevisionStampedAnalysis(out)
        || !verifyProductionPublicationDropsStaleWork(out)) return 1;
    QTextStream result(stdout);
    result << "Analysis service checks passed." << Qt::endl;
    return 0;
}
