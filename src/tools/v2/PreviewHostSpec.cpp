// Contract regression for the Preview half of the Preview/Timeline split.
//
// PreviewHost owns the PreviewSurface projection boundary, but transport
// commands and the authoritative chart time must travel through the playback
// port. The legacy PreviewSurface remains only the transitional render/settings
// implementation in this stage.

#include "app/runtime/preview/PreviewHost.h"

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
    bool playing() const override { return playingValue; }
    miacode::v2::PlaybackTransportState playbackTransportState() const override
    {
        return transportState;
    }
    double positionSeconds() const override { return position; }
    double durationSeconds() const override { return duration; }
    double lowerBoundSeconds() const override { return lowerBound; }
    void togglePlayback() override { ++legacyToggleCount; }
    void stop() override { ++legacyStopCount; }
    void seek(double second) override
    {
        position = second;
        ++legacySeekCount;
    }
    void beginScrub() override { ++legacyBeginScrubCount; }
    void updateScrub(double, bool) override { ++legacyUpdateScrubCount; }
    void endScrub(double, bool) override { ++legacyEndScrubCount; }
    double playbackRate() const override { return rate; }
    void setPlaybackRate(double value) override
    {
        rate = value;
        ++legacySetRateCount;
    }
    void nudgePlaybackRate(int) override { ++legacyNudgeRateCount; }
    QString playbackRateLabel() const override { return QStringLiteral("legacy-label"); }
    QObject* previewRuntimeObject() const override { return runtimeObject; }
    QObject* stageMediaHostObject() const override { return stageMediaObject; }
    double canvasAspectRatio() const override { return 1.5; }
    QStringList statsTexts() const override { return {QStringLiteral("stats")}; }
    RenderMode muriRenderMode() const override { return RenderMode::Native; }
    void setMuriRenderMode(RenderMode) override { ++setMuriModeCount; }
    void toggleMuriRenderMode() override { ++toggleMuriModeCount; }
    QStringList availableSkinDirectoryNames() const override { return {}; }
    QString skinDisplayName(const QString&) const override { return {}; }
    QString resolveSkinDir() const override { return QStringLiteral("skin"); }
    QString resolveSkinRootDir() const override { return QStringLiteral("root"); }
    QString resolveCustomOutlineDir() const override { return QStringLiteral("outline"); }
    void applyOutlineVariant(PreviewOutlineVariant, bool, bool) override {}
    QVariantMap renderSettings() const override { return settings; }
    void setRenderSetting(const QString&, const QVariant&) override { ++setRenderSettingCount; }
    void refreshSurfaces() override { ++refreshSurfacesCount; }
    void applySfxLevels() override { ++applySfxCount; }
    void prepareForShutdown() override { ++shutdownCount; }
    PreviewAudioSettings audioSettings() const override { return {}; }
    void applyAudioSettings(const PreviewAudioSettings&) override { ++applyAudioSettingsCount; }
    void saveAudioSettingsAsSoftwareDefault() override { ++saveAudioSettingsCount; }
    void restoreAudioSettingsFromSoftwareDefault() override { ++restoreAudioSettingsCount; }

    bool playingValue = false;
    miacode::v2::PlaybackTransportState transportState =
        miacode::v2::PlaybackTransportState::Paused;
    double position = 2.0;
    double duration = 99.0;
    double lowerBound = -3.0;
    double rate = 1.25;
    QObject* runtimeObject = nullptr;
    QObject* stageMediaObject = nullptr;
    QVariantMap settings{{QStringLiteral("quality"), QStringLiteral("high")}};
    int legacyToggleCount = 0;
    int legacyStopCount = 0;
    int legacySeekCount = 0;
    int legacyBeginScrubCount = 0;
    int legacyUpdateScrubCount = 0;
    int legacyEndScrubCount = 0;
    int legacySetRateCount = 0;
    int legacyNudgeRateCount = 0;
    int setMuriModeCount = 0;
    int toggleMuriModeCount = 0;
    int setRenderSettingCount = 0;
    int refreshSurfacesCount = 0;
    int applySfxCount = 0;
    int shutdownCount = 0;
    int applyAudioSettingsCount = 0;
    int saveAudioSettingsCount = 0;
    int restoreAudioSettingsCount = 0;
};

class FakePreviewPlaybackPort final : public miacode::v2::PreviewPlaybackPort,
                                      public miacode::v2::AudioClockSource
{
public:
    miacode::v2::PlaybackSnapshot playbackSnapshot() const override { return snapshot; }
    bool acceptsPlaybackCallback(const miacode::v2::PlaybackCallbackStamp& stamp) const override
    {
        return stamp == snapshot.stamp();
    }
    double currentAudioClockSecond() const override { return snapshot.canonicalChartTime; }
    void togglePlayback() override { ++toggleCount; }
    void stop() override { ++stopCount; }
    void seek(double second) override
    {
        snapshot.canonicalChartTime = second;
        ++seekCount;
    }
    void beginScrub() override { ++beginScrubCount; }
    void updateScrub(double second) override
    {
        snapshot.canonicalChartTime = second;
        ++updateScrubCount;
    }
    void endScrub(double second) override
    {
        snapshot.canonicalChartTime = second;
        ++endScrubCount;
    }
    void setPlaybackRate(double rate) override
    {
        snapshot.playbackRate = rate;
        ++setRateCount;
    }
    void nudgePlaybackRate(int) override { ++nudgeRateCount; }

    miacode::v2::PlaybackSnapshot snapshot{
        5, 13, 8, 17.5, 120.0, -4.0, 1.75,
        miacode::v2::PlaybackTransportState::Playing};
    int toggleCount = 0;
    int stopCount = 0;
    int seekCount = 0;
    int beginScrubCount = 0;
    int updateScrubCount = 0;
    int endScrubCount = 0;
    int setRateCount = 0;
    int nudgeRateCount = 0;
};

bool verifyPreviewUsesPlaybackPort(QTextStream& err)
{
    FakePreviewSurface legacy;
    FakePreviewPlaybackPort port;
    miacode::runtime::PreviewHost host(legacy, port, port);

    const auto snapshot = host.playbackSnapshot();
    bool ok = require(host.playing() && host.playbackTransportState()
                          == miacode::v2::PlaybackTransportState::Playing
                          && qFuzzyCompare(host.positionSeconds(), 17.5)
                          && qFuzzyCompare(host.durationSeconds(), 120.0)
                          && qFuzzyCompare(host.lowerBoundSeconds(), -4.0)
                          && qFuzzyCompare(host.playbackRate(), 1.75)
                          && snapshot.sessionGeneration == 5,
                      QStringLiteral("preview transport reads the stamped playback port"), err);
    host.togglePlayback();
    host.stop();
    host.seek(22.0);
    host.beginScrub();
    host.updateScrub(23.0, true);
    host.endScrub(24.0, true);
    host.setPlaybackRate(1.5);
    host.nudgePlaybackRate(1);
    ok &= require(port.toggleCount == 1 && port.stopCount == 1 && port.seekCount == 1
                      && port.beginScrubCount == 1 && port.updateScrubCount == 1
                      && port.endScrubCount == 1 && port.setRateCount == 1
                      && port.nudgeRateCount == 1
                      && legacy.legacyToggleCount == 0 && legacy.legacyStopCount == 0
                      && legacy.legacySeekCount == 0,
                  QStringLiteral("preview transport commands use one playback port"), err);
    return ok;
}

bool verifyPreviewProjectionAndInvalidation(QTextStream& err)
{
    FakePreviewSurface legacy;
    FakePreviewPlaybackPort port;
    miacode::runtime::PreviewHost host(legacy, port, port);
    bool ok = require(host.previewRuntimeObject() == nullptr
                          && host.stageMediaHostObject() == nullptr
                          && qFuzzyCompare(host.canvasAspectRatio(), 1.5)
                          && host.statsTexts() == QStringList{QStringLiteral("stats")}
                          && host.renderSettings() == legacy.settings,
                      QStringLiteral("preview resource and settings projections stay in the legacy host"), err);
    host.refreshSurfaces();
    host.applySfxLevels();
    host.prepareForShutdown();
    ok &= require(legacy.refreshSurfacesCount == 1 && legacy.applySfxCount == 1
                      && legacy.shutdownCount == 1,
                  QStringLiteral("non-transport preview operations remain delegated"), err);
    host.invalidateSession();
    const int refreshCountBeforeWithdrawnCommand = legacy.refreshSurfacesCount;
    host.togglePlayback();
    host.refreshSurfaces();
    ok &= require(!host.playing() && host.playbackSnapshot().transportState
                          == miacode::v2::PlaybackTransportState::Stopped
                      && legacy.refreshSurfacesCount == refreshCountBeforeWithdrawnCommand,
                  QStringLiteral("an invalidated preview host is inert"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;
    ok &= verifyPreviewUsesPlaybackPort(err);
    ok &= verifyPreviewProjectionAndInvalidation(err);
    if (ok) {
        QTextStream(stdout) << "preview_host_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
