// The rate commands are the one transport pair the backend applies WITHOUT
// announcing. PlaybackCoordinator::applyPreviewPlaybackRate writes the new rate
// and drives audio and the media route, but emits no shell notification; while
// the preview is paused no playhead tick arrives either. A model that only
// listens therefore keeps reporting the rate the session started with, and
// everything downstream reads from it — the menu's tick, the transport label,
// the rate HUD — so the rate menu and Ctrl+O / Ctrl+P look completely dead from
// the outside while the backend is in fact obeying them.
//
// This pins the read-back, not the notification: the shell must end a rate
// command knowing what the backend actually settled on.

#include "app/qml_ui/QmlPreviewModel.h"
#include "app/v2/PlaybackControl.h"
#include "app/v2/PreviewSurface.h"
#include "app/v2/ShellNotifications.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTextStream>

namespace {

bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    out << (condition ? "[PASS] " : "[FAIL] ") << message << '\n';
    out.flush();
    if (!condition) ++*failed;
    return condition;
}

QString rateLabelFor(double rate)
{
    QString text = QString::number(rate, 'f', 2);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    return QStringLiteral("%1x").arg(text);
}

// One rate lives behind both faces, the way the runtime has it: the control
// writes it, the surface reports it, and nothing signals in between.
struct FakeBackendRate {
    double rate = 1.0;
    int setCount = 0;
    int nudgeCount = 0;
};

class FakeSurface final : public miacode::v2::PreviewSurface
{
public:
    explicit FakeSurface(FakeBackendRate& shared) : shared_(shared) {}

    bool playing() const override { return false; }
    miacode::v2::PlaybackTransportState playbackTransportState() const override
    {
        return miacode::v2::PlaybackTransportState::Paused;
    }
    double positionSeconds() const override { return 16.0; }
    double durationSeconds() const override { return 164.0; }
    double lowerBoundSeconds() const override { return 0.0; }
    double playbackRate() const override { return shared_.rate; }
    QString playbackRateLabel() const override { return rateLabelFor(shared_.rate); }
    QObject* previewRuntimeObject() const override { return nullptr; }
    QObject* stageMediaHostObject() const override { return nullptr; }
    double canvasAspectRatio() const override { return 1.0; }
    QStringList statsTexts() const override { return {}; }
    RenderMode muriRenderMode() const override { return RenderMode::Native; }
    void setMuriRenderMode(RenderMode) override {}
    void toggleMuriRenderMode() override {}
    QStringList availableSkinDirectoryNames() const override { return {}; }
    QString skinDisplayName(const QString&) const override { return {}; }
    QString resolveSkinDir() const override { return {}; }
    QString resolveSkinRootDir() const override { return {}; }
    QString resolveCustomOutlineDir() const override { return {}; }
    void applyOutlineVariant(PreviewOutlineVariant, bool, bool) override {}
    QVariantMap renderSettings() const override { return {}; }
    void setRenderSetting(const QString&, const QVariant&) override {}
    void refreshSurfaces() override {}
    void applySfxLevels() override {}
    void prepareForShutdown() override {}
    PreviewAudioSettings audioSettings() const override { return {}; }
    void applyAudioSettings(const PreviewAudioSettings&) override {}
    void saveAudioSettingsAsSoftwareDefault() override {}
    void restoreAudioSettingsFromSoftwareDefault() override {}

private:
    FakeBackendRate& shared_;
};

class FakeControl final : public miacode::v2::PlaybackControl
{
public:
    explicit FakeControl(FakeBackendRate& shared) : shared_(shared) {}

    miacode::v2::PlaybackSnapshot playbackSnapshot() const override { return {}; }
    bool acceptsPlaybackCallback(const miacode::v2::PlaybackCallbackStamp&) const override
    {
        return true;
    }
    void togglePlayback() override {}
    void stop() override {}
    void seek(double) override {}
    void beginScrub() override {}
    void updateScrub(double) override {}
    void endScrub(double) override {}
    void setPlaybackRate(double rate) override
    {
        shared_.rate = qMax(0.25, rate);
        ++shared_.setCount;
    }
    // The same ladder runtime/Shared.cpp steps through.
    void nudgePlaybackRate(int direction) override
    {
        static const QList<double> ladder{0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
        int nearest = 0;
        for (int i = 1; i < ladder.size(); ++i) {
            if (qAbs(ladder.at(i) - shared_.rate) < qAbs(ladder.at(nearest) - shared_.rate))
                nearest = i;
        }
        shared_.rate = ladder.at(qBound(0, nearest + direction, ladder.size() - 1));
        ++shared_.nudgeCount;
    }

private:
    FakeBackendRate& shared_;
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    int failed = 0;

    FakeBackendRate backend;
    FakeSurface surface(backend);
    FakeControl control(backend);
    miacode::v2::ShellNotifications notifications;
    miacode::v2::PreviewSurface* surfaceSlot = &surface;
    miacode::v2::PlaybackControl* controlSlot = &control;
    QmlPreviewModel model(notifications, surfaceSlot, controlSlot);

    expect(qFuzzyCompare(model.rate(), 1.0),
           QStringLiteral("the model starts on the backend's rate"), out, &failed);

    // Nothing pushes while paused, so the read-back is the model's only chance.
    QSignalSpy transportSpy(&model, &QmlPreviewModel::transportChanged);
    model.setRate(1.25);
    expect(backend.setCount == 1, QStringLiteral("a rate pick reaches the backend"), out, &failed);
    expect(qFuzzyCompare(model.rate(), 1.25),
           QStringLiteral("the model reports the rate the backend settled on"), out, &failed);
    expect(transportSpy.count() >= 1,
           QStringLiteral("the rate change is announced to QML"), out, &failed);

    model.adjustRate(-1);
    expect(backend.nudgeCount == 1, QStringLiteral("a speed shortcut reaches the backend"), out, &failed);
    expect(qFuzzyCompare(model.rate(), 1.0),
           QStringLiteral("a nudge is visible to QML without a playhead tick"), out, &failed);

    // The clamp lives in the backend, and the shell must report the clamped
    // value rather than the one it asked for.
    model.setRate(0.1);
    expect(qFuzzyCompare(model.rate(), 0.25),
           QStringLiteral("a clamped request reports the clamped rate, not the request"),
           out, &failed);

    if (failed != 0) {
        out << "QmlPreviewRateFeedback spec failed: " << failed << '\n';
        return 1;
    }
    out << "QmlPreviewRateFeedback spec passed.\n";
    return 0;
}
