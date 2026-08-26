#include "app/v2/AnalysisService.h"

#include <QTextStream>

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

}  // namespace

int main()
{
    QTextStream out(stderr);
    if (!verifyRevisionStampedAnalysis(out)) return 1;
    QTextStream result(stdout);
    result << "Analysis service checks passed." << Qt::endl;
    return 0;
}
