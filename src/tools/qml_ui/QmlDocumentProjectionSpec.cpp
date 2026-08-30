#include <QTextStream>

#include "app/qml_ui/QmlDocumentProjection.h"
#include "app/v2/AnalysisService.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

miacode::qml_ui::DocumentValidationProjectionInput currentInput()
{
    miacode::qml_ui::DocumentValidationProjectionInput input;
    input.difficultyId = 5;
    input.chartTextSignature = QStringLiteral("chart@42");
    input.timingSignature = QStringLiteral("4/4");
    input.timelineRevision = 42;
    return input;
}

bool verifyMutationPresentationState(QTextStream& err)
{
    const auto verify = [&err](const QString& mutation, bool validationAvailable) {
        miacode::qml_ui::DocumentPresentationInput input;
        input.activeDifficultyId = 5;
        input.dirty = true;
        input.documentRevision = 43;
        input.validation.revision = 43;
        input.validation.available = validationAvailable;
        input.validation.pending = !validationAvailable;
        const auto state = miacode::qml_ui::projectDocumentPresentation(input);
        return require(state.activeDifficultyId == 5 && state.dirty
                           && state.documentRevision == 43 && state.validationRevision == 43
                           && state.validationAvailable == validationAvailable
                           && state.validationPending == !validationAvailable,
                       mutation + QStringLiteral(" publishes one coherent active/dirty/revision validation state"), err);
    };
    return verify(QStringLiteral("body mutation"), false)
        && verify(QStringLiteral("&first mutation"), false)
        && verify(QStringLiteral("extra timing mutation"), false)
        && verify(QStringLiteral("difficulty level/designer mutation"), true)
        && verify(QStringLiteral("accepted full-source mutation"), false);
}

// The dirty dot marks the section that changed, not the tab that happens to be
// showing. Editing Expert and switching to Master used to move the dot with the
// user, because the file's single flag was all the shell had to draw with.
bool verifyDirtyEditorKeys(QTextStream& err)
{
    miacode::qml_ui::DocumentPresentationInput input;
    input.dirty = true;
    input.dirtyDifficultyIds = {3};
    input.activeDifficultyId = 5;
    const auto state = miacode::qml_ui::projectDocumentPresentation(input);
    bool ok = require(state.dirtyEditorKeys.contains(QStringLiteral("difficulty:3"))
                          && !state.dirtyEditorKeys.contains(QStringLiteral("difficulty:5")),
                      QStringLiteral("the edited difficulty is marked, not the shown one"), err);

    // The metadata tab renders the whole file's source, so it answers the
    // file's question — dirty whenever the document is, whichever difficulty
    // moved.
    ok &= require(state.dirtyEditorKeys.contains(QStringLiteral("metadata")),
                  QStringLiteral("the whole-source tab follows the document"), err);

    miacode::qml_ui::DocumentPresentationInput clean;
    clean.activeDifficultyId = 5;
    const auto cleanState = miacode::qml_ui::projectDocumentPresentation(clean);
    ok &= require(cleanState.dirtyEditorKeys.isEmpty(),
                  QStringLiteral("a saved document marks nothing"), err);

    miacode::qml_ui::DocumentPresentationInput many;
    many.dirty = true;
    many.dirtyDifficultyIds = {2, 3, 4};
    many.activeDifficultyId = 2;
    const auto manyState = miacode::qml_ui::projectDocumentPresentation(many);
    ok &= require(manyState.dirtyEditorKeys.size() == 4,
                  QStringLiteral("every edited difficulty keeps its own mark"), err);
    return ok;
}

bool verifyRejectedSourceTransaction(QTextStream& err)
{
    miacode::qml_ui::DocumentSourceTransactionInput input;
    input.committedSourceText = QStringLiteral("&title=old\n");
    input.attemptedSourceText = QStringLiteral("&inote_5=bad-token\n");
    input.retainedRevision = 42;
    input.issues = {{2, 3, 7, miacode::qml_ui::DocumentValidationIssueSeverity::Error,
                     QStringLiteral("bad token")}};
    const auto state = miacode::qml_ui::projectDocumentSourceTransaction(input);
    return require(!state.accepted && state.revision == 42
                       && state.editorSourceText == input.attemptedSourceText
                       && state.committedSourceText == input.committedSourceText
                       && state.issues.size() == 1 && state.issues.at(0).line == 2
                       && state.issues.at(0).column == 3 && state.issues.at(0).message == QLatin1String("bad token"),
                   QStringLiteral("rejected source retains attempted editor text, revision, and location"), err);
}

bool verifyMultiDifficultyStrictPreflight(QTextStream& err)
{
    const QString committed = QStringLiteral("&inote_5=(120){4}1,\n");
    const QString attempted = QStringLiteral(
        "&inote_5=(120){4}1,\n"
        "&inote_6=(120){4}bad,\n");
    const auto preflight = miacode::qml_ui::preflightDocumentSource(
        attempted, SimaiNativeValidationLocale::English);
    miacode::qml_ui::DocumentSourceTransactionInput transaction;
    transaction.committedSourceText = committed;
    transaction.attemptedSourceText = attempted;
    transaction.retainedRevision = 42;
    transaction.issues = preflight.issues;
    const auto state = miacode::qml_ui::projectDocumentSourceTransaction(transaction);
    return require(!preflight.accepted && !state.accepted
                       && state.committedSourceText == committed
                       && state.editorSourceText == attempted && state.revision == 42
                       && !state.issues.isEmpty() && state.issues.constFirst().line == 2,
                   QStringLiteral("multi-difficulty strict preflight rejects without replacing committed source"), err);
}

bool verifyEffectiveInlineSourceSpan(QTextStream& err)
{
    const QString source = QStringLiteral(
        "&inote_6=(120){4}1,\n"
        "&inote_6=(120){4}bad,\n");
    const auto preflight = miacode::qml_ui::preflightDocumentSource(
        source, SimaiNativeValidationLocale::English);
    return require(!preflight.accepted && !preflight.issues.isEmpty()
                       && preflight.issues.constFirst().line == 2
                       && preflight.issues.constFirst().column == 18
                       && preflight.issues.constFirst().endColumn == 18,
                   QStringLiteral("effective duplicate inline inote maps parser range to full-source coordinates: got %1:%2-%3")
                       .arg(preflight.issues.constFirst().line).arg(preflight.issues.constFirst().column).arg(preflight.issues.constFirst().endColumn), err);
}

bool verifyLevelOnlyDifficultySourceSpan(QTextStream& err)
{
    const auto preflight = miacode::qml_ui::preflightDocumentSource(
        QStringLiteral("&title=x\n&lv_6=13"), SimaiNativeValidationLocale::English);
    return require(!preflight.accepted && !preflight.issues.isEmpty()
                       && preflight.issues.constFirst().line == 2
                       && preflight.issues.constFirst().column >= 1
                       && preflight.issues.constFirst().endColumn
                          >= preflight.issues.constFirst().column,
                   QStringLiteral("level-only difficulty validation maps empty-chart error to its full-source field"), err);
}

bool verifyAnalysisSnapshotIdentityGate(QTextStream& err)
{
    miacode::v2::AnalysisSnapshot snapshot;
    snapshot.difficultyId = 5;
    snapshot.revision = 42;
    snapshot.available = true;
    snapshot.validation.ok = false;
    snapshot.validation.errorCount = 1;
    snapshot.validation.warningCount = 1;
    snapshot.validation.strictNoteCount = 7;
    snapshot.validation.issues = {
        {2, 3, 5, SimaiNativeValidationSeverity::Error,
         QStringLiteral("bad token"), QStringLiteral("Bad token")},
        {4, 1, 2, SimaiNativeValidationSeverity::Warning,
         QStringLiteral("compatibility warning"), QStringLiteral("Compatibility warning")},
    };

    const auto current = miacode::qml_ui::projectDocumentValidation(snapshot, 5, 42);
    bool ok = require(current.available && !current.pending && current.revision == 42
                          && current.errorCount == 1 && current.warningCount == 1
                          && current.parsedNoteCount == 7 && current.issues.size() == 2,
                      QStringLiteral("document diagnostics project from the current service snapshot"), err);
    for (const auto& stale : {
             miacode::qml_ui::projectDocumentValidation(snapshot, 4, 42),
             miacode::qml_ui::projectDocumentValidation(snapshot, 5, 43),
             [&] {
                 auto pending = snapshot;
                 pending.available = false;
                 pending.pending = true;
                 return miacode::qml_ui::projectDocumentValidation(pending, 5, 42);
             }(),
         }) {
        ok &= require(!stale.available && stale.pending && stale.issues.isEmpty()
                          && stale.errorCount == 0 && stale.warningCount == 0
                          && stale.parsedNoteCount == 0,
                      QStringLiteral("difficulty, revision, and pending gates discard stale diagnostics"), err);
    }
    return ok;
}

miacode::qml_ui::DocumentValidationProjectionCache matchingCache()
{
    miacode::qml_ui::DocumentValidationProjectionCache cache;
    cache.difficultyId = 5;
    cache.chartTextSignature = QStringLiteral("chart@42");
    cache.timingSignature = QStringLiteral("4/4");
    cache.validationRevision = 42;
    cache.ok = false;
    cache.errorCount = 1;
    cache.warningCount = 1;
    cache.parsedNoteCount = 7;
    cache.issues = {
        {2, 3, 5, miacode::qml_ui::DocumentValidationIssueSeverity::Error,
         QStringLiteral("bad token")},
        {4, 1, 2, miacode::qml_ui::DocumentValidationIssueSeverity::Warning,
         QStringLiteral("compatibility warning")},
    };
    return cache;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    ok &= verifyMutationPresentationState(err);
    ok &= verifyDirtyEditorKeys(err);
    ok &= verifyRejectedSourceTransaction(err);
    ok &= verifyMultiDifficultyStrictPreflight(err);
    ok &= verifyEffectiveInlineSourceSpan(err);
    ok &= verifyLevelOnlyDifficultySourceSpan(err);
    ok &= verifyAnalysisSnapshotIdentityGate(err);

    const miacode::qml_ui::DocumentValidationProjection projection =
        miacode::qml_ui::projectDocumentValidation(currentInput(), matchingCache());
    ok &= require(projection.available && !projection.pending && projection.revision == 42,
                  QStringLiteral("matching cache is available at the current revision"), err);
    ok &= require(!projection.ok && projection.errorCount == 1 && projection.warningCount == 1
                      && projection.parsedNoteCount == 7,
                  QStringLiteral("matching cache projects validation counts"), err);
    ok &= require(projection.issues.size() == 2
                      && projection.issues.at(0).line == 2
                      && projection.issues.at(0).column == 3
                      && projection.issues.at(0).endColumn == 5
                      && projection.issues.at(0).severity
                          == miacode::qml_ui::DocumentValidationIssueSeverity::Error
                      && projection.issues.at(0).message == QLatin1String("bad token"),
                  QStringLiteral("matching cache projects issue locations, severity, and message"), err);

    auto lateA = matchingCache();
    lateA.chartTextSignature = QStringLiteral("chart@41");
    lateA.validationRevision = 41;
    const miacode::qml_ui::DocumentValidationProjection staleProjection =
        miacode::qml_ui::projectDocumentValidation(currentInput(), lateA);
    ok &= require(!staleProjection.available && staleProjection.pending
                      && staleProjection.revision == 42 && staleProjection.errorCount == 0
                      && staleProjection.warningCount == 0 && staleProjection.parsedNoteCount == 0
                      && staleProjection.issues.isEmpty(),
                  QStringLiteral("late A revision 41 cannot overwrite current B revision 42"), err);

    for (const miacode::qml_ui::DocumentValidationProjectionCache mismatchedCache : {
             [&] { auto cache = matchingCache(); cache.difficultyId = 4; return cache; }(),
             [&] { auto cache = matchingCache(); cache.chartTextSignature = QStringLiteral("chart@41"); return cache; }(),
             [&] { auto cache = matchingCache(); cache.timingSignature = QStringLiteral("3/4"); return cache; }(),
         }) {
        const miacode::qml_ui::DocumentValidationProjection mismatchProjection =
            miacode::qml_ui::projectDocumentValidation(currentInput(), mismatchedCache);
        ok &= require(!mismatchProjection.available && mismatchProjection.pending
                          && mismatchProjection.errorCount == 0
                          && mismatchProjection.warningCount == 0
                          && mismatchProjection.parsedNoteCount == 0
                          && mismatchProjection.issues.isEmpty(),
                      QStringLiteral("difficulty, text, and timing mismatches expose no stale diagnostics"), err);
    }

    auto noDifficulty = currentInput();
    noDifficulty.difficultyId = 0;
    const miacode::qml_ui::DocumentValidationProjection noDifficultyProjection =
        miacode::qml_ui::projectDocumentValidation(noDifficulty, matchingCache());
    ok &= require(!noDifficultyProjection.available && !noDifficultyProjection.pending,
                  QStringLiteral("no active difficulty is unavailable without a pending validation"), err);

    if (!ok) {
        return 1;
    }
    out << "Qml document projection checks passed." << Qt::endl;
    return 0;
}
