#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class MainWindow;

namespace miacode::qml_ui {

// 音频设置: the preview mixer as rows a QML page can render.
//
// The channels are a table rather than ten properties because that is what they
// are — the Widgets dialog built ten identical rows by hand and drifted (its
// mute buttons and its sliders were wired separately). One row description
// keeps a channel's label, level and mute state together.
class QmlAudioSettingsModel final : public QObject
{
    Q_OBJECT
    // Break-slide tail cheer is app-scoped, not part of the per-chart mix, so it
    // sits beside the channels rather than in them.
    Q_PROPERTY(bool breakSlideTailCheerMuted READ breakSlideTailCheerMuted
                   WRITE setBreakSlideTailCheerMuted NOTIFY changed)

public:
    explicit QmlAudioSettingsModel(MainWindow& backend, QObject* parent = nullptr);

    // [{ key, label, percent, muted }] in the order the page shows them.
    Q_INVOKABLE QVariantList channels() const;
    Q_INVOKABLE void setChannelPercent(const QString& key, int percent);
    Q_INVOKABLE void toggleChannelMuted(const QString& key);

    // The 本地预设 pair: remember this mix as the software default, or put the
    // software default back over the project's.
    Q_INVOKABLE void saveAsSoftwareDefault();
    Q_INVOKABLE void restoreSoftwareDefault();

    bool breakSlideTailCheerMuted() const;
    void setBreakSlideTailCheerMuted(bool muted);

signals:
    void changed();

private:
    MainWindow* backend_ = nullptr;
};

}  // namespace miacode::qml_ui
