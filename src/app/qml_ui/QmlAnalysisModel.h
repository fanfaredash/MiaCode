#pragma once

#include <QObject>
#include <QVariantList>

#include "QmlAnalysisProjection.h"
#include "app/v2/AnalysisService.h"
#include "app/v2/ChartWorkspace.h"

class MainWindow;

class QmlAnalysisModel final : public QObject
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
    QmlAnalysisModel(
        MainWindow& backend, miacode::v2::ChartWorkspace& workspace,
        miacode::v2::AnalysisService& analysisService, QObject* parent = nullptr);

    QVariantList validationRows() const;
    QVariantList muriRows() const;
    bool pending() const;
    bool available() const;
    int difficultyId() const;
    qulonglong revision() const;
    int markerCount() const;
    void refreshPreferences();
    Q_INVOKABLE void activateRow(const QVariantMap& row);
    Q_INVOKABLE void completeRowActivation(
        int difficultyId, qulonglong revision, int line, int column, int endColumn, double second);
    Q_INVOKABLE void cancelRowActivation(
        int difficultyId, qulonglong revision, int line, int column, int endColumn, double second);

signals:
    void changed();
    void rowActivated(int difficultyId, qulonglong revision, int line, int column, int endColumn, double second);

private:
    void refresh();
    QVariantList rowsToVariantList(const QVector<miacode::qml_ui::AnalysisRow>& rows) const;

    MainWindow* backend_ = nullptr;
    miacode::v2::ChartWorkspace* workspace_ = nullptr;
    miacode::v2::AnalysisService* analysisService_ = nullptr;
    miacode::qml_ui::AnalysisProjection projection_;
    miacode::qml_ui::QmlAnalysisActivationState activationState_;
};
