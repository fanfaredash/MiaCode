#include "QmlAnalysisModel.h"

#include "app/mainwindow/MainWindow.h"

#include <QMetaObject>
#include <QVariantMap>

QmlAnalysisModel::QmlAnalysisModel(MainWindow& backend, QObject* parent)
    : QObject(parent), backend_(&backend)
{
    refresh();
    connect(backend_, &MainWindow::documentValidationChanged, this, [this] {
        QMetaObject::invokeMethod(this, [this] {
            refresh();
            emit changed();
        }, Qt::QueuedConnection);
    });
}

QVariantList QmlAnalysisModel::validationRows() const { return rowsToVariantList(projection_.validationRows); }
QVariantList QmlAnalysisModel::muriRows() const { return rowsToVariantList(projection_.muriRows); }
bool QmlAnalysisModel::pending() const { return projection_.pending; }
bool QmlAnalysisModel::available() const { return projection_.available; }

void QmlAnalysisModel::activateRow(const QVariantMap& row)
{
    const miacode::qml_ui::AnalysisProjection current = backend_->qmlAnalysisSnapshot();
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
            current, candidate, backend_->documentActiveDifficultyId())) return;
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
    const miacode::qml_ui::AnalysisProjection current = backend_->qmlAnalysisSnapshot();
    if (!miacode::qml_ui::analysisRowCanActivate(
            current, pending, backend_->documentActiveDifficultyId())) return;
    if (second >= 0.0) backend_->navigateShellTimelineToSecond(second);
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
    projection_ = backend_->qmlAnalysisSnapshot();
    if (activationState_.hasPending() && !miacode::qml_ui::analysisRowCanActivate(
            projection_, activationState_.pending(), backend_->documentActiveDifficultyId())) {
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
            {QStringLiteral("title"), row.title},
            {QStringLiteral("detail"), row.detail},
            {QStringLiteral("difficultyId"), row.difficultyId},
            {QStringLiteral("revision"), QVariant::fromValue<qulonglong>(row.revision)},
        });
    }
    return result;
}
