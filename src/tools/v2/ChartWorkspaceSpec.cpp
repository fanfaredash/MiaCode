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

    const auto opened = workspace.openSource(sourceWithTwoDifficulties(), QStringLiteral("chart.txt"));
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
    const auto opened = workspace.openSource(sourceWithTwoDifficulties());
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
    workspace.openSource(sourceWithTwoDifficulties());
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}4,"));
    const bool marked = workspace.markSaved();
    const auto state = workspace.snapshot();
    return expect(marked && !state.dirty && state.revision == 3 && changedCount == 3,
                  QStringLiteral("saving advances the snapshot identity once and establishes a new dirty save point"), out);
}

bool verifyCompleteDocumentSaveAnchor(QTextStream& out)
{
    using miacode::v2::ChartWorkspaceDifficultyField;
    using miacode::v2::ChartWorkspaceDocumentField;

    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties(), QStringLiteral("first.txt"), 5);

    // Open -> edit -> undo to the opened content.
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}3,"));
    bool ok = expect(workspace.snapshot().dirty,
                     QStringLiteral("editing after open dirties the complete document"), out);
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}1,"));
    ok &= expect(!workspace.snapshot().dirty,
                 QStringLiteral("restoring the opened chart body returns to the initial save point"), out);

    // Save -> edit -> undo to the new save point.
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}4,"));
    workspace.markSaved();
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}5,"));
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}4,"));
    ok &= expect(!workspace.snapshot().dirty,
                 QStringLiteral("restoring the post-save chart body returns to the new save point"), out);

    // A chart undo must not hide unrelated metadata dirtiness.
    workspace.updateDocumentField(ChartWorkspaceDocumentField::Title, QStringLiteral("metadata-dirty"));
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}6,"));
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}4,"));
    ok &= expect(workspace.snapshot().dirty,
                 QStringLiteral("chart undo cannot clear an outstanding metadata edit"), out);

    // A branch can reach the same conceptual undo depth as a saved edit but
    // remains dirty because the complete serialized document differs.
    workspace.openSource(sourceWithTwoDifficulties(), QStringLiteral("branch.txt"), 5);
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}3,"));
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}1,"));
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}7,"));
    ok &= expect(workspace.snapshot().dirty,
                 QStringLiteral("a branch edit is compared by content, not undo-step depth"), out);

    // Difficulty selection is identified by revision but never changes the
    // full-document save anchor; level/designer edits do.
    const quint64 beforeSwitch = workspace.snapshot().revision;
    ok &= expect(workspace.selectDifficulty(6) && workspace.snapshot().dirty
                     && workspace.snapshot().revision == beforeSwitch + 1,
                 QStringLiteral("difficulty switching preserves dirty state and advances identity"), out);
    ok &= expect(workspace.updateDifficultyField(
                     6, ChartWorkspaceDifficultyField::Level, QStringLiteral("13+")),
                 QStringLiteral("difficulty metadata is a workspace transaction"), out);

    workspace.openSource(
        QStringLiteral("&title=second\n&lv_4=10\n&inote_4=(120){4}4,\n"),
        QStringLiteral("second.txt"), 4);
    ok &= expect(!workspace.snapshot().dirty
                     && workspace.snapshot().filePath == QLatin1String("second.txt")
                     && workspace.snapshot().activeDifficultyId == 4,
                 QStringLiteral("opening another document installs a distinct clean save point"), out);
    return ok;
}

bool verifyDocumentAndDifficultyTransactions(QTextStream& out)
{
    using miacode::v2::ChartWorkspaceDocumentField;

    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties());
    bool ok = expect(workspace.updateDocumentField(
                         ChartWorkspaceDocumentField::First, QStringLiteral("1.25"))
                         && workspace.snapshot().dirty,
                     QStringLiteral("timing metadata changes the complete document"), out);
    ok &= expect(workspace.addDifficulty(4)
                     && workspace.snapshot().activeDifficultyId == 4
                     && workspace.document().difficulty(4) != nullptr,
                 QStringLiteral("adding a difficulty commits and selects it"), out);
    ok &= expect(workspace.removeDifficulty(4)
                     && workspace.document().difficulty(4) == nullptr
                     && workspace.snapshot().activeDifficultyId == 5,
                 QStringLiteral("removing the active difficulty commits a valid fallback selection"), out);
    ok &= expect(workspace.unifyDesigners(QStringLiteral("shared"))
                     && workspace.document().designer == QLatin1String("shared")
                     && workspace.document().difficulty(5)->designer == QLatin1String("shared")
                     && workspace.document().difficulty(6)->designer == QLatin1String("shared"),
                 QStringLiteral("unified designer mutation is owned by the workspace"), out);
    return ok;
}

bool verifyIncrementalChartAllowsIntermediateText(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties());
    const auto result = workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}1["));
    return expect(result.accepted && workspace.snapshot().dirty
                      && workspace.document().difficulty(5)->chart == QLatin1String("(120){4}1["),
                  QStringLiteral("incremental chart edits accept an incomplete token for later analysis"), out);
}

bool verifyDifficultyFieldsDirtyTheirSection(QTextStream& out)
{
    using miacode::v2::ChartWorkspaceDifficultyField;

    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties());
    bool ok = expect(workspace.snapshot().dirtyDifficultyIds.isEmpty(),
                     QStringLiteral("an opened chart has no dirty sections"), out);

    ok &= expect(workspace.updateDifficultyField(
                     5, ChartWorkspaceDifficultyField::Level, QStringLiteral("12+"))
                     && workspace.snapshot().dirty
                     && workspace.snapshot().dirtyDifficultyIds == QVector<int>{5},
                 QStringLiteral("a level edit marks that difficulty immediately"), out);

    workspace.selectDifficulty(6);
    ok &= expect(workspace.snapshot().dirtyDifficultyIds == QVector<int>{5},
                 QStringLiteral("switching away keeps the edited difficulty marked"), out);

    ok &= expect(workspace.updateDifficultyField(
                     6, ChartWorkspaceDifficultyField::Designer, QStringLiteral("other"))
                     && workspace.snapshot().dirtyDifficultyIds == (QVector<int>{5, 6}),
                 QStringLiteral("a designer edit marks that difficulty without clearing the other"), out);

    workspace.updateDifficultyField(5, ChartWorkspaceDifficultyField::Level, QStringLiteral("12"));
    ok &= expect(workspace.snapshot().dirtyDifficultyIds == QVector<int>{6},
                 QStringLiteral("restoring the saved level clears only that difficulty"), out);
    return ok;
}

bool verifyOpenAcceptsEmptyInoteSlots(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    const auto opened = workspace.openSource(
        QStringLiteral(
            "&title=empty-slots\n"
            "&lv_2=\n"
            "&inote_2=\n"
            "&lv_5=12\n"
            "&inote_5=(120){4}1,\n"
            "&lv_7=\n"
            "&inote_7=\n"),
        QStringLiteral("chart.txt"));
    const auto state = workspace.snapshot();
    return expect(opened.accepted && state.hasDocument && !state.dirty
                      && state.activeDifficultyId == 5
                      && workspace.document().difficulty(2) != nullptr
                      && workspace.document().difficulty(7) != nullptr
                      && !opened.issues.isEmpty(),
                  QStringLiteral("empty inote slots load; chart diagnostics stay on the result"), out);
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

bool verifyOwnedFieldMutationsAndSavePointRebind(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties());
    bool ok = expect(workspace.setDesignerForSlot(5, QStringLiteral("slot"))
                         && workspace.document().designerForSlot(5) == QLatin1String("slot")
                         && workspace.snapshot().dirty,
                     QStringLiteral("setDesignerForSlot is owned by the workspace"), out);
    ok &= expect(workspace.upsertExtraField(QStringLiteral("wholebpm"), QStringLiteral("140")),
                 QStringLiteral("upsertExtraField accepts a wholebpm write"), out);
    bool foundWholeBpm = false;
    for (const SimaiRawField& field : workspace.document().extraFields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) == 0
            && field.value == QLatin1String("140")) {
            foundWholeBpm = true;
            break;
        }
    }
    ok &= expect(foundWholeBpm, QStringLiteral("upsertExtraField writes wholebpm on the owned document"), out);
    ok &= expect(workspace.replaceDifficultyChart(6, QStringLiteral("(120){4}9,"))
                     && workspace.document().difficulty(6)->chart == QLatin1String("(120){4}9,"),
                 QStringLiteral("replaceDifficultyChart mutates a non-active slot"), out);

    const QString current = workspace.document().toText();
    ok &= expect(workspace.rebindSavePoint(sourceWithTwoDifficulties()) && workspace.snapshot().dirty,
                 QStringLiteral("rebinding the save point to the opened text keeps later edits dirty"), out);
    ok &= expect(workspace.rebindSavePoint(current) && !workspace.snapshot().dirty,
                 QStringLiteral("rebinding the save point to the current text clears dirty"), out);
    return ok;
}

}  // namespace

int main()
{
    QTextStream out(stderr);
    const bool ok = verifySingleOwnerRevisionAndSavePoint(out)
        && verifyRejectionDoesNotExposeIntermediateState(out)
        && verifyExplicitSavePoint(out)
        && verifyCompleteDocumentSaveAnchor(out)
        && verifyDocumentAndDifficultyTransactions(out)
        && verifyIncrementalChartAllowsIntermediateText(out)
        && verifyDifficultyFieldsDirtyTheirSection(out)
        && verifyOpenAcceptsEmptyInoteSlots(out)
        && verifyInlineSourceSpanWinsOverLaterLevel(out)
        && verifyOwnedFieldMutationsAndSavePointRebind(out);
    if (!ok) return 1;
    QTextStream result(stdout);
    result << "Chart workspace checks passed." << Qt::endl;
    return 0;
}
