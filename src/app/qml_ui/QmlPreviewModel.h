#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "common/MuriRenderOptions.h"

#include "app/v2/PreviewSurface.h"

class MainWindow;

// Real-time preview state exposed to the pure-QML UI. Playback and rendering
// remain owned by MiaCode's preview runtime.
//
// This reads MainWindow directly. It used to read QuickShellController, which
// polled ~25 unrelated values off MainWindow on a timer that ran whether or not
// anything was playing. Everything is pushed now: shellPresentationChanged for
// discrete shell state, shellPreviewPlayheadChanged from the one function that
// moves the playhead. The playhead briefly went through a sampling timer
// instead, which is worse in both directions — it aliased the clock during
// playback, and it never started at all when playback began somewhere that did
// not announce itself.
class QmlPreviewModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double positionSeconds READ positionSeconds WRITE setPositionSeconds NOTIFY positionChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY transportChanged)
    Q_PROPERTY(double lowerBoundSeconds READ lowerBoundSeconds NOTIFY transportChanged)
    Q_PROPERTY(double rate READ rate WRITE setRate NOTIFY transportChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(QString renderMode READ renderMode NOTIFY renderModeChanged)
    Q_PROPERTY(QString renderModeLabel READ renderModeLabel NOTIFY renderModeChanged)
    Q_PROPERTY(bool muriCheckEnabled READ muriCheckEnabled NOTIFY renderModeChanged)
    Q_PROPERTY(bool smoothStarErase READ smoothStarErase NOTIFY renderModeChanged)
    Q_PROPERTY(QVariantList statistics READ statistics NOTIFY statisticsChanged)
    Q_PROPERTY(QObject* runtime READ runtime CONSTANT)
    Q_PROPERTY(QObject* mediaHost READ mediaHost CONSTANT)
    // Aspect of the preview canvas, used by the split view to size the pane.
    Q_PROPERTY(double canvasAspectRatio READ canvasAspectRatio NOTIFY presentationChanged)

public:
    explicit QmlPreviewModel(MainWindow& backend,
                             miacode::v2::PreviewSurface*& surfaceSlot,
                             QObject* parent = nullptr);

    double positionSeconds() const;
    double durationSeconds() const;
    double lowerBoundSeconds() const;
    double rate() const;
    bool playing() const;
    QString renderMode() const;
    QString renderModeLabel() const;
    bool muriCheckEnabled() const;
    bool smoothStarErase() const;
    QVariantList statistics() const;
    QString currentSkinDirectory() const;
    QObject* runtime() const;
    QObject* mediaHost() const;
    double canvasAspectRatio() const;

    void setPositionSeconds(double value);
    void setRate(double value);
    void setPlaying(bool value);
    Q_INVOKABLE void toggleRenderMode();
    Q_INVOKABLE void setMuriCheckEnabled(bool enabled);
    Q_INVOKABLE void setSmoothStarErase(bool enabled);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePlayback();
    // Step the rate one stop (-1 slower / +1 faster) along the menu's ladder.
    Q_INVOKABLE void adjustRate(int direction);
    // Debug hook the preview surface calls on pointer interaction.
    Q_INVOKABLE void logPreviewInteraction(const QString& action, const QString& payload = QString());
    Q_INVOKABLE void beginScrub();
    Q_INVOKABLE void updateScrub(double second);
    Q_INVOKABLE void endScrub(double second);

signals:
    void positionChanged();
    void transportChanged();
    void playingChanged();
    void renderModeChanged();
    void statisticsChanged();
    void presentationChanged();

private:
    void refreshFromBackend(bool force = false);
    void rebuildStatistics();
    void refreshSkinDirectory();
    void updateV2UiProbePlaybackState();
    void resetV2UiProbe();
    void appendV2UiProbeSummary() const;

    MainWindow* backend_ = nullptr;
    // Bound to the assembly's slot, not a snapshot.
    miacode::v2::PreviewSurface** surfaceSlot_ = nullptr;
    miacode::v2::PreviewSurface* surface() const
    {
        return surfaceSlot_ != nullptr ? *surfaceSlot_ : nullptr;
    }
    double positionSeconds_ = 0.0;
    double durationSeconds_ = 0.0;
    double lowerBoundSeconds_ = 0.0;
    double rate_ = 1.0;
    bool playing_ = false;
    QString renderMode_;
    QString renderModeLabel_;
    RenderMode lastRegularMode_ = RenderMode::Native;
    bool muriCheckEnabled_ = false;
    bool smoothStarErase_ = true;
    QStringList statisticsTexts_;
    QString skinDirectory_;
    QVariantList statistics_;
    bool v2UiProbeEnabled_ = false;
    bool v2UiProbePlaybackActive_ = false;
    mutable qint64 v2UiProbeStatisticsRebuildCount_ = 0;
    mutable qint64 v2UiProbeStatisticsBuildNs_ = 0;
    mutable qint64 v2UiProbeStatisticsBuildMaxNs_ = 0;
    mutable qint64 v2UiProbeSkinResolveNs_ = 0;
    mutable qint64 v2UiProbeSkinResolveMaxNs_ = 0;
    qint64 v2UiProbeShellStateChangeCount_ = 0;
};
