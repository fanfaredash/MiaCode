#pragma once

#include <QObject>
#include <QVariantList>

#include "QmlAnalysisProjection.h"

class MainWindow;

class QmlAnalysisModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList validationRows READ validationRows NOTIFY changed)
    Q_PROPERTY(QVariantList muriRows READ muriRows NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)

public:
    QmlAnalysisModel(MainWindow& backend, QObject* parent = nullptr);

    QVariantList validationRows() const;
    QVariantList muriRows() const;
    bool pending() const;
    bool available() const;
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
    miacode::qml_ui::AnalysisProjection projection_;
    miacode::qml_ui::QmlAnalysisActivationState activationState_;
};
