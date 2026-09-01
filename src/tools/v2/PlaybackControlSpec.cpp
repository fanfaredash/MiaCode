// Contract regression for the first Preview/Timeline split seam.
//
// The adapter deliberately wraps the current composite PreviewSurface during
// migration. The contract is already independent: commands are serialized by
// a playback sequence, document changes are stamped separately, and a session
// generation invalidates all callbacks from the previous runtime.

#include "app/v2/PlaybackControl.h"
#include "app/runtime/playback/PlaybackControlAdapter.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

class FakePreviewSurface final : public miacode::v2::PreviewSurface
{
public:
    bool playing() const override { return playing_; }
    miacode::v2::PlaybackTransportState playbackTransportState() const override
    {
        return transportState_;
    }
    double positionSeconds() const override { return position_; }
    double durationSeconds() const override { return 120.0; }
    double lowerBoundSeconds() const override { return -5.0; }
    void togglePlayback() override
    {
        playing_ = !playing_;
        transportState_ = playing_ ? miacode::v2::PlaybackTransportState::Playing
                                    : miacode::v2::PlaybackTransportState::Paused;
        ++toggleCount;
    }
    void stop() override
    {
        playing_ = false;
        transportState_ = miacode::v2::PlaybackTransportState::Stopped;
        ++stopCount;
    }
    void seek(double second) override
    {
        position_ = second;
        if (!playing_) {
            transportState_ = miacode::v2::PlaybackTransportState::Paused;
        }
        ++seekCount;
    }
    void beginScrub() override
    {
        transportState_ = miacode::v2::PlaybackTransportState::Scrubbing;
        ++beginScrubCount;
    }
    void updateScrub(double second, bool centerView) override
    {
        Q_UNUSED(centerView);
        position_ = second;
        ++updateScrubCount;
    }
    void endScrub(double second, bool centerView) override
    {
        Q_UNUSED(centerView);
        position_ = second;
        transportState_ = miacode::v2::PlaybackTransportState::Paused;
        ++endScrubCount;
    }
    double playbackRate() const override { return rate_; }
    void setPlaybackRate(double rate) override { rate_ = rate; }
    void nudgePlaybackRate(int direction) override { rate_ += direction * 0.25; }
    QString playbackRateLabel() const override { return QStringLiteral("presentation-only"); }
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

    bool playing_ = false;
    double position_ = 0.0;
    double rate_ = 1.0;
    miacode::v2::PlaybackTransportState transportState_ =
        miacode::v2::PlaybackTransportState::Stopped;
    int toggleCount = 0;
    int stopCount = 0;
    int seekCount = 0;
    int beginScrubCount = 0;
    int updateScrubCount = 0;
    int endScrubCount = 0;
};

bool verifyAdapterStampsAndForwardsTransport(QTextStream& err)
{
    FakePreviewSurface legacy;
    miacode::runtime::PlaybackControlAdapter adapter(legacy, 7);
    miacode::v2::PlaybackControl& control = adapter;

    adapter.setDocumentRevision(12);
    const miacode::v2::PlaybackSnapshot before = control.playbackSnapshot();
    bool ok = require(before.sessionGeneration == 7 && before.documentRevision == 12
                          && before.playbackSequence == 0
                          && before.transportState == miacode::v2::PlaybackTransportState::Stopped,
                      QStringLiteral("the initial snapshot carries the session and document identity"),
                      err);

    control.togglePlayback();
    const miacode::v2::PlaybackSnapshot playing = control.playbackSnapshot();
    ok &= require(legacy.toggleCount == 1 && playing.playbackSequence == 1
                      && playing.transportState == miacode::v2::PlaybackTransportState::Playing,
                  QStringLiteral("toggle forwards and advances the playback sequence"), err);
    const miacode::v2::PlaybackCallbackStamp currentStamp = playing.stamp();
    ok &= require(control.acceptsPlaybackCallback(currentStamp),
                  QStringLiteral("a callback stamped from the current snapshot is accepted"), err);

    control.seek(42.5);
    ok &= require(legacy.seekCount == 1 && qFuzzyCompare(legacy.position_, 42.5),
                  QStringLiteral("seek forwards through the adapter"), err);
    ok &= require(!control.acceptsPlaybackCallback(currentStamp),
                  QStringLiteral("a callback from an older command sequence is rejected"), err);

    adapter.invalidateSession();
    const miacode::v2::PlaybackSnapshot afterInvalidation = control.playbackSnapshot();
    ok &= require(afterInvalidation.sessionGeneration == 8
                      && control.acceptsPlaybackCallback(afterInvalidation.stamp()),
                  QStringLiteral("session invalidation advances generation without invalidating its new snapshot"),
                  err);
    const int stopCountBeforeInvalidatedCommand = legacy.stopCount;
    control.stop();
    ok &= require(legacy.stopCount == stopCountBeforeInvalidatedCommand
                      && afterInvalidation.transportState
                          == miacode::v2::PlaybackTransportState::Stopped,
                  QStringLiteral("an invalidated adapter drops commands without touching the old host"), err);
    return ok;
}

bool verifyScrubAndRateCommands(QTextStream& err)
{
    FakePreviewSurface legacy;
    miacode::runtime::PlaybackControlAdapter adapter(legacy);
    miacode::v2::PlaybackControl& control = adapter;

    control.beginScrub();
    control.updateScrub(18.0);
    control.endScrub(19.0);
    control.setPlaybackRate(1.5);
    control.nudgePlaybackRate(1);
    const miacode::v2::PlaybackSnapshot snapshot = control.playbackSnapshot();
    bool ok = require(legacy.beginScrubCount == 1 && legacy.updateScrubCount == 1
                          && legacy.endScrubCount == 1 && qFuzzyCompare(legacy.position_, 19.0),
                      QStringLiteral("scrub commands remain bracketed and forward their landing point"), err);
    ok &= require(snapshot.playbackRate > 1.5 && snapshot.playbackRate < 1.76,
                  QStringLiteral("the snapshot exposes the typed host playback rate, not its label"), err);
    control.stop();
    ok &= require(legacy.stopCount == 1
                      && control.playbackSnapshot().transportState
                          == miacode::v2::PlaybackTransportState::Stopped,
                  QStringLiteral("stop forwards and publishes the stopped state"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;
    ok &= verifyAdapterStampsAndForwardsTransport(err);
    ok &= verifyScrubAndRateCommands(err);
    if (ok) {
        QTextStream(stdout) << "playback_control_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
