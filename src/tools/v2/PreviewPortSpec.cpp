// Contract regression for stage 4.9d-4b-2d's fourth narrow port.
//
// PlaybackPreviewPort is the coordinator's seam onto the preview stage-media
// route (warmup, chart-path resync, initialization-on-demand), the audio
// runtime / outline canvas re-applies that follow a chart or preference
// change, and preview shutdown. Like PlaybackPreferencesPort — cut by
// capability because eight of its nine methods' eventual owner is
// StageMediaHost while the ninth (preparePreviewForShutdown) is Session's own
// orchestration — Session implements the port.
//
// This target links Qt6::Core + Qt6::Test only. If the port ever grows a
// method that needs Session or a window to implement, FakePreview below
// fails to compile it — and if the header itself ever pulls in Session.h or
// a Widgets/QML type, this whole target fails to LINK, which is a stronger
// guarantee than grepping for the forbidden names.

#include "app/v2/PlaybackPreviewPort.h"

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

// A stand-in preview host. Its only job is to prove the contract can be
// implemented with no Session and no window; the production implementer is
// Session (forwarding into StageMediaHost for eight of the nine methods).
class FakePreview final : public miacode::v2::PlaybackPreviewPort
{
public:
    void ensurePreviewStageMediaRouteInitialized() override
    {
        ++ensurePreviewStageMediaRouteInitializedCount;
    }

    void syncPreviewStageMediaRouteChartPath(
        const QString& chartPath,
        const QString& trackPath,
        double pausedSecond,
        const QString& chartVideoOverridePath) override
    {
        lastChartPath = chartPath;
        lastTrackPath = trackPath;
        lastPausedSecond = pausedSecond;
        lastChartVideoOverridePath = chartVideoOverridePath;
        ++syncPreviewStageMediaRouteChartPathCount;
    }

    void schedulePreviewSubsystemWarmup() override { ++schedulePreviewSubsystemWarmupCount; }

    void applyPreviewAudioSettingsToRuntime() override { ++applyPreviewAudioSettingsToRuntimeCount; }

    void loadProjectAudioPreferences() override { ++loadProjectAudioPreferencesCount; }

    void applyEffectivePreviewOutlineVariantToCanvas() override
    {
        ++applyEffectivePreviewOutlineVariantToCanvasCount;
    }

    void preparePreviewForShutdown() override { ++preparePreviewForShutdownCount; }

    QString resolvePreviewSkinDir() const override { return skinDir; }

    void applyPreviewOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection,
                                     bool persistState) override
    {
        lastOutlineVariant = variant;
        lastUseAutoSelection = useAutoSelection;
        lastPersistState = persistState;
        ++applyPreviewOutlineVariantCount;
    }

    int ensurePreviewStageMediaRouteInitializedCount = 0;
    int syncPreviewStageMediaRouteChartPathCount = 0;
    QString lastChartPath;
    QString lastTrackPath;
    double lastPausedSecond = 0.0;
    QString lastChartVideoOverridePath;
    int schedulePreviewSubsystemWarmupCount = 0;
    int applyPreviewAudioSettingsToRuntimeCount = 0;
    int loadProjectAudioPreferencesCount = 0;
    int applyEffectivePreviewOutlineVariantToCanvasCount = 0;
    int preparePreviewForShutdownCount = 0;
    QString skinDir = QStringLiteral("/skins/standard");
    PreviewOutlineVariant lastOutlineVariant = PreviewOutlineVariant::Point;
    bool lastUseAutoSelection = false;
    bool lastPersistState = false;
    int applyPreviewOutlineVariantCount = 0;
};

bool verifyImplementableWithoutSessionOrAWindow(QTextStream& err)
{
    FakePreview preview;
    miacode::v2::PlaybackPreviewPort& contract = preview;

    contract.ensurePreviewStageMediaRouteInitialized();
    bool ok = require(preview.ensurePreviewStageMediaRouteInitializedCount == 1,
                      QStringLiteral("ensurePreviewStageMediaRouteInitialized reaches the implementation"), err);

    contract.syncPreviewStageMediaRouteChartPath(
        QStringLiteral("/charts/song.txt"),
        QStringLiteral("/charts/track.ogg"),
        12.5,
        QStringLiteral("/charts/pv.mp4"));
    ok &= require(preview.syncPreviewStageMediaRouteChartPathCount == 1
                      && preview.lastChartPath == QStringLiteral("/charts/song.txt")
                      && preview.lastTrackPath == QStringLiteral("/charts/track.ogg")
                      && preview.lastPausedSecond == 12.5
                      && preview.lastChartVideoOverridePath == QStringLiteral("/charts/pv.mp4"),
                  QStringLiteral("syncPreviewStageMediaRouteChartPath reaches the implementation with all four arguments"), err);

    contract.schedulePreviewSubsystemWarmup();
    ok &= require(preview.schedulePreviewSubsystemWarmupCount == 1,
                  QStringLiteral("schedulePreviewSubsystemWarmup reaches the implementation"), err);

    contract.applyPreviewAudioSettingsToRuntime();
    ok &= require(preview.applyPreviewAudioSettingsToRuntimeCount == 1,
                  QStringLiteral("applyPreviewAudioSettingsToRuntime reaches the implementation"), err);

    contract.loadProjectAudioPreferences();
    ok &= require(preview.loadProjectAudioPreferencesCount == 1,
                  QStringLiteral("loadProjectAudioPreferences reaches the implementation"), err);

    contract.applyEffectivePreviewOutlineVariantToCanvas();
    ok &= require(preview.applyEffectivePreviewOutlineVariantToCanvasCount == 1,
                  QStringLiteral("applyEffectivePreviewOutlineVariantToCanvas reaches the implementation"), err);

    contract.preparePreviewForShutdown();
    ok &= require(preview.preparePreviewForShutdownCount == 1,
                  QStringLiteral("preparePreviewForShutdown reaches the implementation"), err);

    ok &= require(contract.resolvePreviewSkinDir() == QStringLiteral("/skins/standard"),
                  QStringLiteral("resolvePreviewSkinDir reads through to the implementation"), err);

    contract.applyPreviewOutlineVariant(PreviewOutlineVariant::JudgeAreaLabeled, true, false);
    ok &= require(preview.applyPreviewOutlineVariantCount == 1
                      && preview.lastOutlineVariant == PreviewOutlineVariant::JudgeAreaLabeled
                      && preview.lastUseAutoSelection
                      && !preview.lastPersistState,
                  QStringLiteral("applyPreviewOutlineVariant reaches the implementation with all three arguments"), err);

    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = verifyImplementableWithoutSessionOrAWindow(err);

    if (ok) {
        QTextStream(stdout) << "preview_port_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
