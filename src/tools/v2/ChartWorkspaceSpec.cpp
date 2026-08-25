#include "app/v2/ChartWorkspace.h"

#include <QTextStream>

namespace {

bool expect(bool condition, const QString& message, QTextStream& out)
{
    if (!condition) out << "FAIL: " << message << Qt::endl;
    return condition;
}

QString sourceWithTwoDifficulties()
{
    return QStringLiteral(
        "&title=workspace\n"
        "&lv_5=12\n"
        "&inote_5=(120){4}1,\n"
        "&lv_6=13\n"
        "&inote_6=(120){4}2,\n");
}

bool verifySingleOwnerRevisionAndSavePoint(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    int changedCount = 0;
    quint64 emittedRevision = 0;
    QObject::connect(&workspace, &miacode::v2::ChartWorkspace::changed,
                     [&changedCount, &emittedRevision](quint64 revision) {
                         ++changedCount;
                         emittedRevision = revision;
                     });

    const auto opened = workspace.replaceSource(sourceWithTwoDifficulties(), QStringLiteral("chart.txt"));
    auto state = workspace.snapshot();
    bool ok = expect(opened.accepted && opened.revision == 1 && changedCount == 1
                         && emittedRevision == 1 && state.hasDocument && !state.dirty
                         && state.filePath == QLatin1String("chart.txt") && state.activeDifficultyId == 5,
                     QStringLiteral("open publishes one clean, fully identified workspace snapshot"), out);

    const auto edited = workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}3,"));
    state = workspace.snapshot();
    ok &= expect(edited.accepted && edited.revision == 2 && changedCount == 2
                     && state.dirty && workspace.document().difficulty(5)->chart == QLatin1String("(120){4}3,"),
                 QStringLiteral("one accepted chart transaction changes the owned document exactly once"), out);

    const auto restored = workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}1,"));
    state = workspace.snapshot();
    ok &= expect(restored.accepted && restored.revision == 3 && changedCount == 3 && !state.dirty,
                 QStringLiteral("replaying the saved content restores the dirty save point"), out);

    ok &= expect(workspace.selectDifficulty(6), QStringLiteral("existing difficulty selection is accepted"), out);
    state = workspace.snapshot();
    ok &= expect(state.activeDifficultyId == 6 && state.revision == 4 && changedCount == 4,
                 QStringLiteral("difficulty selection has one new revision and notification"), out);

    return ok;
}

bool verifyRejectionDoesNotExposeIntermediateState(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    int changedCount = 0;
    QObject::connect(&workspace, &miacode::v2::ChartWorkspace::changed,
                     [&changedCount](quint64) { ++changedCount; });
    const auto opened = workspace.replaceSource(sourceWithTwoDifficulties());
    const auto before = workspace.snapshot();
    const auto rejected = workspace.replaceSource(QStringLiteral("&inote_5=(120){4}bad,\n"));
    const auto after = workspace.snapshot();

    return expect(opened.accepted && !rejected.accepted && !rejected.issues.isEmpty()
                      && before.revision == after.revision && before.sourceText == after.sourceText
                      && before.activeDifficultyId == after.activeDifficultyId && changedCount == 1,
                  QStringLiteral("rejected full-source replacement retains the committed workspace without notification"), out);
}

bool verifyExplicitSavePoint(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    int changedCount = 0;
    QObject::connect(&workspace, &miacode::v2::ChartWorkspace::changed,
                     [&changedCount](quint64) { ++changedCount; });
    workspace.replaceSource(sourceWithTwoDifficulties());
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}4,"));
    const bool marked = workspace.markSaved();
    const auto state = workspace.snapshot();
    return expect(marked && !state.dirty && state.revision == 3 && changedCount == 3,
                  QStringLiteral("saving advances the snapshot identity once and establishes a new dirty save point"), out);
}

bool verifyInlineSourceSpanWinsOverLaterLevel(QTextStream& out)
{
    const auto preflight = miacode::v2::ChartWorkspace::preflightSource(
        QStringLiteral("&inote_5=(120){4}bad,\n&lv_5=13\n"),
        SimaiNativeValidationLocale::English);
    return expect(!preflight.accepted && !preflight.issues.isEmpty()
                      && preflight.issues.constFirst().line == 1,
                  QStringLiteral("full-source diagnostics prefer the inline chart span over a later level field"), out);
}

}  // namespace

int main()
{
    QTextStream out(stderr);
    const bool ok = verifySingleOwnerRevisionAndSavePoint(out)
        && verifyRejectionDoesNotExposeIntermediateState(out)
        && verifyExplicitSavePoint(out)
        && verifyInlineSourceSpanWinsOverLaterLevel(out);
    if (!ok) return 1;
    QTextStream result(stdout);
    result << "Chart workspace checks passed." << Qt::endl;
    return 0;
}
