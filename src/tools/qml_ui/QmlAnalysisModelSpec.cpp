#include <QByteArray>
#include <QTextStream>

#include "app/qml_ui/QmlAnalysisProjection.h"
#include "app/v2/AnalysisService.h"
#include "timeline/TimelineAnalysisPublication.h"

namespace {
bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) err << "FAIL: " << message << Qt::endl;
    return condition;
}

miacode::qml_ui::AnalysisProjectionInput alignedInput()
{
    miacode::qml_ui::AnalysisProjectionInput input;
    input.activeDifficultyId = 5;
    input.muriDifficultyId = 5;
    input.muriRevision = 72;
    input.muriSignatureAligned = true;
    input.muriStaticReferencesAligned = true;
    input.validation.available = true;
    input.validation.pending = false;
    input.validation.revision = 72;
    input.validation.issues = {{3, 4, 4,
        miacode::qml_ui::DocumentValidationIssueSeverity::Error, QStringLiteral("bad note")}};
    input.muriRows = {{8, 2, 2, 12.5, QStringLiteral("error"), QStringLiteral("muri"),
                       QStringLiteral("Overlap"), QStringLiteral("two notes overlap")}};
    return input;
}

struct ObservedAnalysisPublicationState {
    quint64 validationRevision = 0;
    int muriDifficultyId = 0;
    quint64 muriRevision = 0;
    QByteArray muriSignature;
    int staticReferencesDifficultyId = 0;
    quint64 staticReferencesRevision = 0;
    QByteArray staticReferencesSignature;
};

bool verifyAnalysisObserversSeeCompleteProvenance(QTextStream& err)
{
    constexpr quint64 kRevision = 72;
    constexpr int kDifficultyId = 5;
    const QByteArray signature("marker-signature");
    ObservedAnalysisPublicationState published;
    ObservedAnalysisPublicationState observed;
    int notificationCount = 0;

    miacode::timeline::publishTimelineAnalysisState(
        [&] { published.validationRevision = kRevision; },
        [&] {
            published.muriDifficultyId = kDifficultyId;
            published.muriRevision = kRevision;
            published.muriSignature = signature;
        },
        [&] {
            published.staticReferencesDifficultyId = kDifficultyId;
            published.staticReferencesRevision = kRevision;
            published.staticReferencesSignature = signature;
        },
        [&] {
            ++notificationCount;
            observed = published;
        });

    return require(notificationCount == 1
                       && observed.validationRevision == kRevision
                       && observed.muriDifficultyId == kDifficultyId
                       && observed.muriRevision == kRevision
                       && observed.muriSignature == signature
                       && observed.staticReferencesDifficultyId == kDifficultyId
                       && observed.staticReferencesRevision == kRevision
                       && observed.staticReferencesSignature == signature,
                   QStringLiteral("a validation observer sees validation, Muri, and static-reference provenance from one completed revision"), err);
}

bool verifyWorkspaceSnapshotProjection(QTextStream& err)
{
    miacode::v2::AnalysisSnapshot snapshot;
    snapshot.difficultyId = 5;
    snapshot.revision = 72;
    snapshot.available = true;
    snapshot.validation.ok = false;
    snapshot.validation.errorCount = 1;
    snapshot.validation.strictNoteCount = 7;
    snapshot.validation.issues = {{3, 4, 6, SimaiNativeValidationSeverity::Error,
                                   QStringLiteral("bad note"), QStringLiteral("Bad note")}};
    TimelineNoteMarker marker;
    marker.second = 12.5;
    marker.sourceLine = 3;
    marker.sourceCol = 4;
    snapshot.noteMarkers.append(marker);
    const QVector<miacode::qml_ui::AnalysisRow> muriRows = {
        {8, 2, 2, 12.5, QStringLiteral("error"), QStringLiteral("muri"),
         QStringLiteral("Overlap"), QStringLiteral("two notes overlap")},
    };

    const auto current = miacode::qml_ui::projectAnalysis(snapshot, 5, 72, muriRows);
    bool ok = require(current.available && !current.pending && current.difficultyId == 5
                          && current.revision == 72 && current.validationRows.size() == 1
                          && current.muriRows.size() == 1 && current.noteMarkers.size() == 1,
                      QStringLiteral("one current service snapshot publishes diagnostics, markers, and Muri together"), err);
    ok &= require(current.validationRows.constFirst().difficultyId == 5
                      && current.validationRows.constFirst().revision == 72
                      && current.muriRows.constFirst().difficultyId == 5
                      && current.muriRows.constFirst().revision == 72,
                  QStringLiteral("every QML row inherits the accepted workspace identity"), err);

    for (const auto& stale : {
             miacode::qml_ui::projectAnalysis(snapshot, 4, 72, muriRows),
             miacode::qml_ui::projectAnalysis(snapshot, 5, 73, muriRows),
             [&] {
                 auto pending = snapshot;
                 pending.available = false;
                 pending.pending = true;
                 pending.noteMarkers.clear();
                 return miacode::qml_ui::projectAnalysis(pending, 5, 72, muriRows);
             }(),
         }) {
        ok &= require(!stale.available && stale.pending
                          && stale.validationRows.isEmpty() && stale.muriRows.isEmpty()
                          && stale.noteMarkers.isEmpty(),
                      QStringLiteral("a stale or pending identity leaks no partial diagnostics, markers, or Muri"), err);
    }
    return ok;
}
}

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;
    ok &= verifyAnalysisObserversSeeCompleteProvenance(err);
    ok &= verifyWorkspaceSnapshotProjection(err);
    const auto aligned = miacode::qml_ui::projectAnalysis(alignedInput());
    ok &= require(aligned.available && !aligned.pending && aligned.validationRows.size() == 1
                      && aligned.muriRows.size() == 1 && aligned.validationRows.constFirst().revision == 72
                      && aligned.muriRows.constFirst().difficultyId == 5
                      && aligned.muriRows.constFirst().title == QLatin1String("Muri: Overlap"),
                  QStringLiteral("matching validation and Muri snapshots display only their current rows"), err);
    ok &= require(aligned.validationRows.constFirst().endColumn == 4,
                  QStringLiteral("validation multi-column range keeps its end column through the analysis row"), err);
    for (auto mismatch : {[] { auto input = alignedInput(); input.muriSignatureAligned = false; return input; }(),
                          [] { auto input = alignedInput(); input.muriDifficultyId = 4; return input; }(),
                          [] { auto input = alignedInput(); input.muriRevision = 71; return input; }()}) {
        const auto projection = miacode::qml_ui::projectAnalysis(mismatch);
        ok &= require(projection.pending && !projection.available
                          && projection.validationRows.isEmpty() && projection.muriRows.isEmpty(),
                      QStringLiteral("mismatched Muri signature, difficulty, or revision is pending and leaks no old rows"), err);
    }
    auto noResult = alignedInput();
    noResult.muriSignatureAligned = false;
    noResult.muriRows.clear();
    const auto noResultProjection = miacode::qml_ui::projectAnalysis(noResult);
    ok &= require(noResultProjection.pending && noResultProjection.validationRows.isEmpty()
                      && noResultProjection.muriRows.isEmpty(),
                  QStringLiteral("missing Muri result is pending and exposes no validation or Muri rows"), err);
    auto staleStaticReferences = alignedInput();
    staleStaticReferences.muriStaticReferencesAligned = false;
    const auto staleStaticProjection = miacode::qml_ui::projectAnalysis(staleStaticReferences);
    ok &= require(staleStaticProjection.pending && staleStaticProjection.muriRows.isEmpty(),
                  QStringLiteral("command-route stale static references cannot combine with a current Muri report"), err);
    auto staleRow = aligned.muriRows.constFirst();
    staleRow.revision = 71;
    auto differentDifficultyRow = aligned.muriRows.constFirst();
    differentDifficultyRow.difficultyId = 4;
    ok &= require(miacode::qml_ui::analysisRowCanActivate(
                      aligned, aligned.muriRows.constFirst(), 5),
                  QStringLiteral("current aligned row is accepted for reveal and timeline seek"), err)
        && require(!miacode::qml_ui::analysisRowCanActivate(aligned, staleRow, 5),
                  QStringLiteral("stale activation is rejected before it can switch difficulty, reveal, or seek"), err)
        && require(!miacode::qml_ui::analysisRowCanActivate(aligned, differentDifficultyRow, 5),
                  QStringLiteral("different-difficulty row is rejected without switching or navigating"), err);
    auto changedAfterReveal = aligned;
    changedAfterReveal.revision = 73;
    ok &= require(!miacode::qml_ui::analysisRowCanActivate(
                      changedAfterReveal, aligned.muriRows.constFirst(), 5),
                  QStringLiteral("delayed seek callback is rejected after its reveal provenance becomes stale"), err);
    miacode::qml_ui::QmlAnalysisActivationState activation;
    const auto currentRow = aligned.muriRows.constFirst();
    auto otherRow = currentRow;
    otherRow.line += 1;
    activation.begin(currentRow);
    ok &= require(!activation.cancel(otherRow) && activation.hasPending(),
                  QStringLiteral("mismatched cancellation does not clear a pending activation"), err)
        && require(activation.cancel(currentRow) && !activation.hasPending()
                       && !activation.complete(currentRow),
                   QStringLiteral("matching cancellation settles activation and blocks its delayed seek"), err);
    activation.begin(currentRow);
    miacode::qml_ui::AnalysisRow completed;
    ok &= require(activation.complete(currentRow, &completed) && !activation.hasPending()
                       && completed.title == currentRow.title && completed.detail == currentRow.detail
                       && miacode::qml_ui::analysisRowCanActivate(aligned, completed, 5),
                  QStringLiteral("matching completion restores the full pending row and permits one fresh seek"), err)
        && require(!activation.complete(currentRow),
                   QStringLiteral("settled or mismatched completion cannot cause a second seek"), err);
    activation.begin(currentRow);
    activation.begin(otherRow);
    ok &= require(!activation.complete(currentRow) && activation.hasPending()
                       && activation.cancel(otherRow),
                  QStringLiteral("replacement invalidates old activation before a stale completion can seek"), err);
    if (ok) out << "qml_analysis_model_spec ok" << Qt::endl;
    return ok ? 0 : 1;
}
