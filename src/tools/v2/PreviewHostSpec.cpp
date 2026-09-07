// Contract regression for the Preview half of the Preview/Timeline split.
//
// PreviewHost owns the PreviewSurface projection boundary, but transport
// commands and the authoritative chart time must travel through the playback
// port. The legacy PreviewSurface remains only the transitional render/settings
// implementation in this stage.

#include "app/runtime/preview/PreviewHost.h"

#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined (repo root absolute path)"
#endif

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QString readSource(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
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
    double playbackRate() const override { return rate; }
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
    return ok;
}

bool verifyPreviewHostHasNoTransportSurface(QTextStream& err)
{
    // Before Stage 4.5 step B, PreviewSurface declared eight transport-command
    // pure virtuals (toggle/stop/seek/scrub/rate-set), and this test proved
    // "the preview projection does not own transport" by calling those methods
    // on PreviewHost and checking the calls landed on playbackPort_, never on
    // legacySurface_. Step B deleted those eight methods from PreviewSurface
    // outright: PlaybackControl is the one transport owner now, reached
    // directly by QmlPreviewModel, so PreviewHost has no such method left to
    // call. The invariant this test protects did not weaken — it got
    // STRONGER, from "true because of how it forwards" to "true because the
    // method does not exist" — so the check below asserts absence from
    // PreviewHost's own header instead of exercising a forward, and still
    // fails if transport is ever reintroduced onto this compatibility surface.
    const QString header = readSource(QStringLiteral("src/app/runtime/preview/PreviewHost.h"));
    bool ok = require(!header.isEmpty(), QStringLiteral("PreviewHost.h is present"), err);
    static const QStringList transportSymbols{
        QStringLiteral("togglePlayback"),
        QStringLiteral("stop("),
        QStringLiteral("seek("),
        QStringLiteral("beginScrub"),
        QStringLiteral("updateScrub"),
        QStringLiteral("endScrub"),
        QStringLiteral("setPlaybackRate"),
        QStringLiteral("nudgePlaybackRate"),
    };
    for (const QString& symbol : transportSymbols) {
        ok &= require(!header.contains(symbol),
                      QStringLiteral("PreviewHost.h no longer declares %1").arg(symbol), err);
    }
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
    ok &= verifyPreviewHostHasNoTransportSurface(err);
    ok &= verifyPreviewProjectionAndInvalidation(err);
    if (ok) {
        QTextStream(stdout) << "preview_host_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
