#pragma once

#include <QObject>
#include <QVariantList>

#include "document/AnalysisProjection.h"
#include "app/services/AnalysisService.h"
#include "app/services/ChartWorkspace.h"

namespace miacode::ui {

class AnalysisModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList validationRows READ validationRows NOTIFY changed)
    Q_PROPERTY(QVariantList muriRows READ muriRows NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int difficultyId READ difficultyId NOTIFY changed)
    Q_PROPERTY(qulonglong revision READ revision NOTIFY changed)
    Q_PROPERTY(int markerCount READ markerCount NOTIFY changed)

public:
    AnalysisModel(
        miacode::ChartWorkspace& workspace,
        miacode::AnalysisService& analysisService, QObject* parent = nullptr);

    QVariantList validationRows() const;
    QVariantList muriRows() const;
    bool pending() const;
    bool available() const;
    int difficultyId() const;
    qulonglong revision() const;
    int markerCount() const;
    void refreshPreferences();
    Q_INVOKABLE void activateRow(const QVariantMap& row);
    Q_INVOKABLE bool completeRowActivation(
        int difficultyId, qulonglong revision, int line, int column, int endColumn, double second);
    Q_INVOKABLE void cancelRowActivation(
        int difficultyId, qulonglong revision, int line, int column, int endColumn, double second);

signals:
    void changed();
    void rowActivated(int difficultyId, qulonglong revision, int line, int column, int endColumn, double second);

private:
    void refresh();
    QVariantList rowsToVariantList(const QVector<miacode::ui::AnalysisRow>& rows) const;

    miacode::ChartWorkspace* workspace_ = nullptr;
    miacode::AnalysisService* analysisService_ = nullptr;
    miacode::ui::AnalysisProjection projection_;
    miacode::ui::AnalysisActivationState activationState_;
};
} // namespace miacode::ui
