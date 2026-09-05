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

// The shared-designer mode is a workspace invariant, not a rule each call site
// has to remember: while it is on, every route into a designer name — the top
// &des, a difficulty's &des_N, a chart-less slot, a difficulty that did not
// exist yet — has to come out identical.
bool verifyUnifiedDesignerModeIsAWorkspaceInvariant(QTextStream& out)
{
    using miacode::v2::ChartWorkspaceDifficultyField;
    using miacode::v2::ChartWorkspaceDocumentField;

    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties());
    bool ok = expect(!workspace.unifiedDesignerEnabled(),
                     QStringLiteral("an opened document starts with the shared-designer mode off"), out);

    workspace.setDesignerForSlot(3, QStringLiteral("chartless"));
    ok &= expect(workspace.document().difficulty(3) == nullptr
                     && workspace.document().perDifficultyDesigners().size() == 3,
                 QStringLiteral("a chart-less designer slot is listed without materializing a difficulty"), out);

    const auto beforeFlip = workspace.snapshot();
    const bool flipped = workspace.setUnifiedDesignerEnabled(true);
    const auto afterFlip = workspace.snapshot();
    ok &= expect(flipped && workspace.unifiedDesignerEnabled()
                     && afterFlip.sourceText == beforeFlip.sourceText
                     && afterFlip.dirty == beforeFlip.dirty
                     && afterFlip.dirtyDifficultyIds == beforeFlip.dirtyDifficultyIds,
                 QStringLiteral("turning the mode on is a mode change, not a document change"), out);

    ok &= expect(workspace.updateDocumentField(
                     ChartWorkspaceDocumentField::Designer, QStringLiteral("one"))
                     && workspace.document().designerForSlot(5) == QLatin1String("one")
                     && workspace.document().designerForSlot(6) == QLatin1String("one")
                     && workspace.document().designerForSlot(3) == QLatin1String("one"),
                 QStringLiteral("a top &des edit broadcasts to charted and chart-less slots alike"), out);

    ok &= expect(workspace.updateDifficultyField(
                     6, ChartWorkspaceDifficultyField::Designer, QStringLiteral("two"))
                     && workspace.document().designer == QLatin1String("two")
                     && workspace.document().designerForSlot(5) == QLatin1String("two"),
                 QStringLiteral("a per-difficulty designer edit broadcasts back to every other slot"), out);

    ok &= expect(workspace.addDifficulty(4)
                     && workspace.document().difficulty(4) != nullptr
                     && workspace.document().difficulty(4)->designer == QLatin1String("two"),
                 QStringLiteral("a difficulty added under the mode is born with the shared name"), out);

    ok &= expect(workspace.document().isUnifiedDesignerTriviallySafe(),
                 QStringLiteral("every write under the mode leaves the document satisfying it"), out);

    ok &= expect(workspace.setDesignerForSlot(2, QString())
                     && workspace.document().designer.isEmpty()
                     && workspace.document().designerForSlot(5).isEmpty()
                     && !workspace.document().toText().contains(QLatin1String("&des_3=")),
                 QStringLiteral("an empty shared name clears every slot, chart-less entries included"), out);

    workspace.setUnifiedDesignerEnabled(false);
    ok &= expect(workspace.updateDifficultyField(
                     5, ChartWorkspaceDifficultyField::Designer, QStringLiteral("solo"))
                     && workspace.document().designerForSlot(6).isEmpty()
                     && workspace.document().designer.isEmpty(),
                 QStringLiteral("with the mode off a designer edit stays on its own difficulty"), out);

    workspace.setUnifiedDesignerEnabled(true);
    workspace.openSource(sourceWithTwoDifficulties());
    ok &= expect(!workspace.unifiedDesignerEnabled(),
                 QStringLiteral("opening another document drops the mode with the session it belonged to"), out);
    return ok;
}

// Unifying must not invent a `&des_N` line for a slot that has neither a chart
// nor a name: the file would grow entries for difficulties nobody authored.
bool verifyUnifyLeavesUntouchedSlotsAlone(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties());
    workspace.setUnifiedDesignerEnabled(true);
    workspace.updateDocumentField(
        miacode::v2::ChartWorkspaceDocumentField::Designer, QStringLiteral("solo"));
    const QString text = workspace.document().toText();
    return expect(text.contains(QLatin1String("&des_5=solo"))
                      && text.contains(QLatin1String("&des_6=solo"))
                      && !text.contains(QLatin1String("&des_1="))
                      && !text.contains(QLatin1String("&des_7=")),
                  QStringLiteral("unifying names the slots that exist and no others"), out);
}

// Materializing a difficulty over a chart-less `&des_N` has to adopt that name:
// toText() stops emitting the standalone record once the difficulty owns the id.
bool verifyAddedDifficultyAdoptsAChartlessName(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties());
    workspace.setDesignerForSlot(4, QStringLiteral("recorded"));
    workspace.addDifficulty(4);
    return expect(workspace.document().difficulty(4) != nullptr
                      && workspace.document().difficulty(4)->designer == QLatin1String("recorded")
                      && workspace.document().toText().contains(QLatin1String("&des_4=recorded")),
                  QStringLiteral("adding a difficulty keeps the name its slot already recorded"), out);
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

bool verifyDifficultyDiscardRestoresTheCompleteSection(QTextStream& out)
{
    using miacode::v2::ChartWorkspaceDifficultyField;

    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties());
    const QString savedChart = workspace.document().difficulty(5)->chart;
    const QString savedLevel = workspace.document().difficulty(5)->level;
    const QString savedDesigner = workspace.document().difficulty(5)->designer;

    bool ok = expect(workspace.updateDifficultyField(
                         5, ChartWorkspaceDifficultyField::Level, QStringLiteral("12+"))
                     && workspace.updateDifficultyField(
                         5, ChartWorkspaceDifficultyField::Designer, QStringLiteral("shared"))
                     && workspace.replaceDifficultyChart(5, QStringLiteral("(120){4}3,"))
                     && workspace.snapshot().dirtyDifficultyIds == QVector<int>{5},
                 QStringLiteral("a difficulty section can be dirty in chart, level, and designer together"), out);

    const auto reverted = workspace.revertDifficultyChart(5);
    const SimaiDifficultyData* restored = workspace.document().difficulty(5);
    ok &= expect(reverted.accepted && restored != nullptr
                     && restored->chart == savedChart
                     && restored->level == savedLevel
                     && restored->designer == savedDesigner
                     && workspace.snapshot().dirtyDifficultyIds.isEmpty(),
                 QStringLiteral("discard restores the complete difficulty section and clears its dirty marker"), out);

    workspace.unifyDesigners(QStringLiteral("shared"));
    ok &= expect(workspace.snapshot().dirtyDifficultyIds == QVector<int>{5, 6},
                 QStringLiteral("unified designer marks every affected difficulty"), out);
    workspace.revertDifficultyChart(5);
    ok &= expect(workspace.snapshot().dirtyDifficultyIds == QVector<int>{6}
                     && workspace.document().difficulty(5)->designer == savedDesigner,
                 QStringLiteral("discard clears a designer-only unified edit instead of looping"), out);

    workspace.addDifficulty(4);
    ok &= expect(workspace.document().difficulty(4) != nullptr
                     && workspace.snapshot().dirtyDifficultyIds.contains(4),
                 QStringLiteral("a newly added difficulty becomes a dirty section"), out);
    workspace.revertDifficultyChart(4);
    ok &= expect(workspace.document().difficulty(4) == nullptr
                     && !workspace.snapshot().dirtyDifficultyIds.contains(4),
                 QStringLiteral("discard removes a difficulty that did not exist at the save point"), out);

    miacode::v2::ChartWorkspace metadataWorkspace;
    metadataWorkspace.openSource(sourceWithTwoDifficulties());
    metadataWorkspace.updateDocumentField(
        miacode::v2::ChartWorkspaceDocumentField::Title, QStringLiteral("changed"));
    const auto metadataReverted = metadataWorkspace.revertDifficultyChart(0);
    ok &= expect(metadataReverted.accepted
                     && metadataWorkspace.document().title == QLatin1String("workspace")
                     && !metadataWorkspace.snapshot().dirty,
                 QStringLiteral("metadata discard restores the whole document save point"), out);
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

bool verifyExtraFieldTransactionIsAtomic(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(sourceWithTwoDifficulties());
    const auto before = workspace.snapshot();
    const QString beforeText = workspace.document().toText();
    const auto rejected = workspace.replaceExtraFields(
        QStringLiteral("  &missing_equals\r\n&=empty_key\r\n"));
    const auto after = workspace.snapshot();

    bool ok = expect(!rejected.accepted && rejected.issues.size() == 2
                         && rejected.issues.at(0).line == 1
                         && rejected.issues.at(0).column == 3
                         && rejected.issues.at(0).code == QLatin1String("invalid_property")
                         && before.revision == after.revision
                         && before.sourceText == after.sourceText
                         && before.dirty == after.dirty
                         && workspace.document().toText() == beforeText,
                     QStringLiteral("rejected extra fields retain source, revision, dirty state, and save point"), out);

    const auto accepted = workspace.replaceExtraFields(
        QStringLiteral("  &dummy=\r\n  &empty=\r\n"));
    bool hasDummy = false;
    bool hasEmpty = false;
    for (const SimaiRawField& field : workspace.document().extraFields) {
        hasDummy = hasDummy || (field.key == QLatin1String("dummy") && field.value.isEmpty());
        hasEmpty = hasEmpty || (field.key == QLatin1String("empty") && field.value.isEmpty());
    }
    ok &= expect(accepted.accepted && accepted.revision == before.revision + 1
                     && workspace.snapshot().dirty && hasDummy && hasEmpty,
                 QStringLiteral("valid indented CRLF fields, including empty values, commit atomically"), out);
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
        && verifyUnifiedDesignerModeIsAWorkspaceInvariant(out)
        && verifyUnifyLeavesUntouchedSlotsAlone(out)
        && verifyAddedDifficultyAdoptsAChartlessName(out)
        && verifyIncrementalChartAllowsIntermediateText(out)
        && verifyDifficultyFieldsDirtyTheirSection(out)
        && verifyDifficultyDiscardRestoresTheCompleteSection(out)
        && verifyOpenAcceptsEmptyInoteSlots(out)
        && verifyInlineSourceSpanWinsOverLaterLevel(out)
        && verifyOwnedFieldMutationsAndSavePointRebind(out)
        && verifyExtraFieldTransactionIsAtomic(out);
    if (!ok) return 1;
    QTextStream result(stdout);
    result << "Chart workspace checks passed." << Qt::endl;
    return 0;
}
