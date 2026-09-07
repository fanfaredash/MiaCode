#include "QmlAnalysisModel.h"

#include "common/MuriTypes.h"
#include "tools/muri/MuriPanelEntries.h"
#include "ui/UiText.h"

#include <QVariantMap>

#include <utility>

namespace {

QVector<miacode::qml_ui::AnalysisRow> muriRowsForSnapshot(
    const miacode::v2::AnalysisSnapshot& snapshot)
{
    QVector<miacode::qml_ui::AnalysisRow> rows;
    const QVector<miacode::muri::MuriPanelEntry> entries =
        miacode::muri::buildVisibleMuriPanelEntries(
            snapshot.muri, snapshot.muriStaticReferences);
    rows.reserve(entries.size());
    for (const miacode::muri::MuriPanelEntry& entry : entries) {
        miacode::qml_ui::AnalysisRow row;
        row.line = qMax(1, entry.line);
        row.column = qMax(1, entry.col);
        row.endColumn = row.column;
        row.second = entry.second;
        row.severity = entry.alertLevel == MuriAlertLevel::Warning
            ? QStringLiteral("warning") : QStringLiteral("error");
        row.alert = entry.alertLevel == MuriAlertLevel::Warning
            ? QStringLiteral("warning") : QStringLiteral("muri");
        switch (entry.kind) {
        case MuriKind::SlideTooFast: row.title = QStringLiteral("Slide too fast"); break;
        case MuriKind::SlideHeadTap: row.title = QStringLiteral("Slide head tap"); break;
        case MuriKind::TapOnSlide: row.title = QStringLiteral("Tap on slide"); break;
        case MuriKind::Overlap: row.title = QStringLiteral("Overlap"); break;
        case MuriKind::MultiTouch: row.title = QStringLiteral("Multi-touch"); break;
        }
        row.detail = renderMuriDetail(
            entry.detailKind, entry.detailArgs, snapshot.locale).trimmed();
        if (row.detail.isEmpty()) row.detail = entry.rawDetail;
        rows.append(std::move(row));
    }
    return rows;
}

}  // namespace

QmlAnalysisModel::QmlAnalysisModel(
    miacode::v2::ChartWorkspace& workspace,
    miacode::v2::AnalysisService& analysisService,
    miacode::v2::TimelineSurface*& surfaceSlot, QObject* parent)
    : QObject(parent)
    , surfaceSlot_(&surfaceSlot)
    , workspace_(&workspace)
    , analysisService_(&analysisService)
{
    refresh();
    connect(analysisService_, &miacode::v2::AnalysisService::snapshotChanged,
            this, [this](int, quint64) {
        refresh();
        emit changed();
    });
}

QVariantList QmlAnalysisModel::validationRows() const { return rowsToVariantList(projection_.validationRows); }
QVariantList QmlAnalysisModel::muriRows() const { return rowsToVariantList(projection_.muriRows); }
bool QmlAnalysisModel::pending() const { return projection_.pending; }
bool QmlAnalysisModel::available() const { return projection_.available; }
int QmlAnalysisModel::difficultyId() const { return projection_.difficultyId; }
qulonglong QmlAnalysisModel::revision() const { return projection_.revision; }
int QmlAnalysisModel::markerCount() const { return projection_.noteMarkers.size(); }

void QmlAnalysisModel::refreshPreferences()
{
    refresh();
    emit changed();
}

void QmlAnalysisModel::activateRow(const QVariantMap& row)
{
    refresh();
    const miacode::qml_ui::AnalysisProjection current = projection_;
    const int difficultyId = row.value(QStringLiteral("difficultyId")).toInt();
    const quint64 revision = row.value(QStringLiteral("revision")).toULongLong();
    const int line = row.value(QStringLiteral("line")).toInt();
    const int column = row.value(QStringLiteral("column")).toInt();
    const int endColumn = row.value(QStringLiteral("endColumn"), column).toInt();
    const double second = row.value(QStringLiteral("second"), -1.0).toDouble();
    miacode::qml_ui::AnalysisRow candidate;
    candidate.line = line;
    candidate.column = column;
    candidate.endColumn = endColumn;
    candidate.second = second;
    candidate.difficultyId = difficultyId;
    candidate.revision = revision;
    candidate.title = row.value(QStringLiteral("title")).toString();
    candidate.detail = row.value(QStringLiteral("detail")).toString();
    if (!miacode::qml_ui::analysisRowCanActivate(
            current, candidate, current.difficultyId)) return;
    activationState_.begin(candidate);
    emit rowActivated(
        difficultyId, revision, qMax(1, line), qMax(1, column), qMax(column, endColumn), second);
}

void QmlAnalysisModel::completeRowActivation(
    int difficultyId, qulonglong revision, int line, int column, int endColumn, double second)
{
    miacode::qml_ui::AnalysisRow completionIdentity;
    completionIdentity.difficultyId = difficultyId;
    completionIdentity.revision = revision;
    completionIdentity.line = line;
    completionIdentity.column = column;
    completionIdentity.endColumn = endColumn;
    completionIdentity.second = second;
    miacode::qml_ui::AnalysisRow pending;
    if (!activationState_.complete(completionIdentity, &pending)) return;
    refresh();
    const miacode::qml_ui::AnalysisProjection current = projection_;
    if (!miacode::qml_ui::analysisRowCanActivate(
            current, pending, current.difficultyId)) return;
    if (pending.second >= 0.0 && surface() != nullptr)
        surface()->navigateToSecond(pending.second);
}

void QmlAnalysisModel::cancelRowActivation(
    int difficultyId, qulonglong revision, int line, int column, int endColumn, double second)
{
    miacode::qml_ui::AnalysisRow pending;
    pending.difficultyId = difficultyId;
    pending.revision = revision;
    pending.line = line;
    pending.column = column;
    pending.endColumn = endColumn;
    pending.second = second;
    activationState_.cancel(pending);
}

void QmlAnalysisModel::refresh()
{
    const miacode::v2::ChartWorkspaceSnapshot workspaceSnapshot = workspace_ != nullptr
        ? workspace_->snapshot() : miacode::v2::ChartWorkspaceSnapshot();
    const miacode::v2::AnalysisSnapshot analysisSnapshot = analysisService_ != nullptr
        ? analysisService_->snapshot() : miacode::v2::AnalysisSnapshot();
    const bool current = analysisSnapshot.available && !analysisSnapshot.pending
        && analysisSnapshot.difficultyId == workspaceSnapshot.activeDifficultyId
        && analysisSnapshot.revision == workspaceSnapshot.revision;
    projection_ = miacode::qml_ui::projectAnalysis(
        analysisSnapshot, workspaceSnapshot.activeDifficultyId, workspaceSnapshot.revision,
        current ? muriRowsForSnapshot(analysisSnapshot)
                : QVector<miacode::qml_ui::AnalysisRow>());
    if (current && workspace_ != nullptr) {
        const SimaiDifficultyData* difficulty =
            workspace_->document().difficulty(workspaceSnapshot.activeDifficultyId);
        if (difficulty != nullptr && difficulty->level.trimmed().isEmpty()) {
            miacode::qml_ui::AnalysisRow row;
            row.line = 0;
            row.column = 0;
            row.endColumn = 0;
            row.severity = QStringLiteral("error");
            row.alert = QStringLiteral("metadata");
            row.code = QStringLiteral("missing_difficulty_level");
            row.title = UiText::text(QStringLiteral("validation.difficulty_level_missing_type"));
            row.detail = UiText::text(QStringLiteral("validation.difficulty_level_missing"));
            row.difficultyId = workspaceSnapshot.activeDifficultyId;
            row.revision = workspaceSnapshot.revision;
            projection_.validationRows.append(std::move(row));
        }
    }
    if (activationState_.hasPending() && !miacode::qml_ui::analysisRowCanActivate(
            projection_, activationState_.pending(), workspaceSnapshot.activeDifficultyId)) {
        activationState_.cancel(activationState_.pending());
    }
}

QVariantList QmlAnalysisModel::rowsToVariantList(
    const QVector<miacode::qml_ui::AnalysisRow>& rows) const
{
    QVariantList result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.append(QVariantMap{
            {QStringLiteral("line"), row.line},
            {QStringLiteral("column"), row.column},
            {QStringLiteral("endColumn"), row.endColumn},
            {QStringLiteral("second"), row.second},
            {QStringLiteral("severity"), row.severity},
            {QStringLiteral("alert"), row.alert},
            {QStringLiteral("code"), row.code},
            {QStringLiteral("title"), row.title},
            {QStringLiteral("detail"), row.detail},
            {QStringLiteral("difficultyId"), row.difficultyId},
            {QStringLiteral("revision"), QVariant::fromValue<qulonglong>(row.revision)},
        });
    }
    return result;
}
