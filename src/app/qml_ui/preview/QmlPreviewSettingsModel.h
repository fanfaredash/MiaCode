#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

class MainWindow;

namespace miacode::qml_ui {

// 预览设置: what the preview draws, and how the chart plays back inside it.
//
// The page is a form over one map. Values live on MainWindow — each has its own
// way of reaching the running preview — so this reads them as a snapshot and
// writes them back one key at a time; the model itself holds no state to drift
// out of step. Labels come from UiText, which is what keeps the 文案 the same
// wording the Widgets dialog shipped.
class QmlPreviewSettingsModel final : public QObject
{
    Q_OBJECT
    // Current values, keyed as MainWindow keys them; also carries the ranges
    // (square scale, flow speed) so the page states no limits of its own.
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    // Field labels under the same keys as `values`. Constant: a language change
    // needs a restart, so re-reading them would never show anything new.
    Q_PROPERTY(QVariantMap labels READ labels CONSTANT)
    Q_PROPERTY(QString videoGroupLabel READ videoGroupLabel CONSTANT)
    Q_PROPERTY(QString gameplayGroupLabel READ gameplayGroupLabel CONSTANT)
    // [{ value, label }] for the four dropdowns.
    Q_PROPERTY(QVariantList scaleModeOptions READ scaleModeOptions CONSTANT)
    Q_PROPERTY(QVariantList slideStackOrderOptions READ slideStackOrderOptions CONSTANT)
    Q_PROPERTY(QVariantList centerDisplayOptions READ centerDisplayOptions CONSTANT)
    Q_PROPERTY(QVariantList tapJudgeTextDistanceOptions READ tapJudgeTextDistanceOptions CONSTANT)
    // [{ key, label }] for 判定效果显示, whose four overlays are four separate
    // values behind one control.
    Q_PROPERTY(QVariantList judgeEffectOptions READ judgeEffectOptions CONSTANT)

public:
    explicit QmlPreviewSettingsModel(MainWindow& backend, QObject* parent = nullptr);

    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);

    QVariantMap values() const;
    QVariantMap labels() const;
    QString videoGroupLabel() const;
    QString gameplayGroupLabel() const;
    QVariantList scaleModeOptions() const;
    QVariantList slideStackOrderOptions() const;
    QVariantList centerDisplayOptions() const;
    QVariantList tapJudgeTextDistanceOptions() const;
    QVariantList judgeEffectOptions() const;

signals:
    void changed();

private:
    MainWindow* backend_ = nullptr;
};

}  // namespace miacode::qml_ui
