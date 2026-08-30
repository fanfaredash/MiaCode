#pragma once

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "app/v2/UiRequestService.h"

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
    Q_PROPERTY(QString skinGroupLabel READ skinGroupLabel CONSTANT)
    // [{ value, label }] for the four dropdowns.
    Q_PROPERTY(QVariantList scaleModeOptions READ scaleModeOptions CONSTANT)
    Q_PROPERTY(QVariantList slideStackOrderOptions READ slideStackOrderOptions CONSTANT)
    Q_PROPERTY(QVariantList centerDisplayOptions READ centerDisplayOptions CONSTANT)
    Q_PROPERTY(QVariantList tapJudgeTextDistanceOptions READ tapJudgeTextDistanceOptions CONSTANT)
    // [{ key, label }] for 判定效果显示, whose four overlays are four separate
    // values behind one control.
    Q_PROPERTY(QVariantList judgeEffectOptions READ judgeEffectOptions CONSTANT)

    // 皮肤 is a global preview concern, not an export-page setting. The model
    // writes through MainWindow's owner-live preview path and keeps the HUD
    // font picker on the shell's native request boundary.
    Q_PROPERTY(QVariantList skinOptions READ skinOptions NOTIFY skinChanged)
    Q_PROPERTY(int skinIndex READ skinIndex WRITE setSkinIndex NOTIFY skinChanged)
    Q_PROPERTY(QVariantList skinJudgeEffectOptions READ skinJudgeEffectOptions CONSTANT)
    Q_PROPERTY(int skinJudgeEffectIndex READ skinJudgeEffectIndex WRITE setSkinJudgeEffectIndex NOTIFY skinChanged)
    Q_PROPERTY(QVariantList outlineOptions READ outlineOptions CONSTANT)
    Q_PROPERTY(int outlineIndex READ outlineIndex WRITE setOutlineIndex NOTIFY skinChanged)
    Q_PROPERTY(QVariantList fontLibraryOptions READ fontLibraryOptions NOTIFY fontLibraryChanged)
    Q_PROPERTY(QVariantList hudFontAreaOptions READ hudFontAreaOptions CONSTANT)
    Q_PROPERTY(int hudFontAreaIndex READ hudFontAreaIndex WRITE setHudFontAreaIndex NOTIFY hudFontChanged)
    Q_PROPERTY(QString hudFontPath READ hudFontPath WRITE setHudFontPath NOTIFY hudFontChanged)
    Q_PROPERTY(QString hudFontSample READ hudFontSample NOTIFY hudFontChanged)

public:
    explicit QmlPreviewSettingsModel(MainWindow& backend, QObject* parent = nullptr);

    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);

    QVariantMap values() const;
    QVariantMap labels() const;
    QString videoGroupLabel() const;
    QString gameplayGroupLabel() const;
    QString skinGroupLabel() const;
    QVariantList scaleModeOptions() const;
    QVariantList slideStackOrderOptions() const;
    QVariantList centerDisplayOptions() const;
    QVariantList tapJudgeTextDistanceOptions() const;
    QVariantList judgeEffectOptions() const;
    QVariantList skinOptions() const;
    int skinIndex() const;
    QVariantList skinJudgeEffectOptions() const;
    int skinJudgeEffectIndex() const;
    QVariantList outlineOptions() const;
    int outlineIndex() const;
    QVariantList fontLibraryOptions() const;
    QVariantList hudFontAreaOptions() const;
    int hudFontAreaIndex() const { return hudFontAreaIndex_; }
    QString hudFontPath() const;
    QString hudFontSample() const;

    void setSkinIndex(int index);
    void setSkinJudgeEffectIndex(int index);
    void setOutlineIndex(int index);
    void setHudFontAreaIndex(int index);
    void setHudFontPath(const QString& path);

    Q_INVOKABLE void openSkinDirectory();
    Q_INVOKABLE void openJudgeLineDirectory();
    Q_INVOKABLE void importHudFont();
    Q_INVOKABLE void resetHudFont();
    Q_INVOKABLE void refreshFontLibrary();

signals:
    void changed();
    void skinChanged();
    void fontLibraryChanged();
    void hudFontChanged();

private:
    void applyHudFontImport(const QString& selectedPath);

    miacode::v2::UiRequestService* uiRequests_ = nullptr;
    MainWindow* backend_ = nullptr;
    int hudFontAreaIndex_ = 0;
};

}  // namespace miacode::qml_ui
