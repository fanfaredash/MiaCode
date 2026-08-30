#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class MainWindow;
class QTimer;
class QtPreviewSfxRuntime;

namespace miacode::qml_ui {

// 音频设置: the preview mixer as rows a QML page can render.
//
// The channels are a table rather than ten properties because that is what they
// are — the Widgets dialog built ten identical rows by hand and drifted (its
// mute buttons and its sliders were wired separately). One row description
// keeps a channel's label, level, mute state and audition sample together.
class QmlAudioSettingsModel final : public QObject
{
    Q_OBJECT
    // Break-slide tail cheer is app-scoped, not part of the per-chart mix, so it
    // sits beside the channels rather than in them.
    Q_PROPERTY(bool breakSlideTailCheerMuted READ breakSlideTailCheerMuted
                   WRITE setBreakSlideTailCheerMuted NOTIFY changed)

public:
    explicit QmlAudioSettingsModel(MainWindow& backend, QObject* parent = nullptr);
    ~QmlAudioSettingsModel() override;

    // [{ key, label, percent, muted }] in the order the page shows them.
    Q_INVOKABLE QVariantList channels() const;
    Q_INVOKABLE void setChannelPercent(const QString& key, int percent);
    Q_INVOKABLE void toggleChannelMuted(const QString& key);

    // The 本地预设 pair: remember this mix as the software default, or put the
    // software default back over the project's.
    Q_INVOKABLE void saveAsSoftwareDefault();
    Q_INVOKABLE void restoreSoftwareDefault();

    // Slider drags deliver a change per pixel; auditioning each one would
    // stutter. The page reports whether a handle is held so the audition waits
    // for the release — the Widgets dialog read isSliderDown() for the same
    // reason.
    Q_INVOKABLE void setAuditionHeld(bool held);
    // Drops the audition runtime. The page calls this as it closes so the audio
    // worker thread and its decoded samples do not outlive the panel, which is
    // the lifetime the Widgets dialog gave its own local runtime.
    Q_INVOKABLE void releaseAudition();

    bool breakSlideTailCheerMuted() const;
    void setBreakSlideTailCheerMuted(bool muted);

signals:
    void changed();

private:
    // Debounce, then play — unless a handle is still down, in which case wait
    // for it. An empty kind cancels the pending audition instead.
    void queueAudition(const QString& kind);
    void flushAudition();
    bool playAudition(const QString& kind);

    MainWindow* backend_ = nullptr;
    // Both are created on the first audition rather than with the model: the
    // runtime spins up an audio worker thread, and most sessions never open
    // this page.
    QtPreviewSfxRuntime* auditionRuntime_ = nullptr;
    QTimer* auditionTimer_ = nullptr;
    QString pendingAudition_;
    QString auditionSfxDir_;
    // The kind waiting on an in-flight asset reload: auditioning needs the
    // samples loaded, so the first request after a (re)load replays itself from
    // the reload's completion.
    QString auditionKindAwaitingAssets_;
    quint64 auditionReloadAssetGeneration_ = 0;
    quint64 auditionReloadSequence_ = 0;
    bool auditionHeld_ = false;
};

}  // namespace miacode::qml_ui
