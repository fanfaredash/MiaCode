// Contract regression for stage 4.9d-4b-2a's first narrow port.
//
// PlaybackPreferencesPort is the coordinator's one seam onto preferences and
// persisted state (portable-state disk I/O, the 预览设置 render-setting map,
// the 音频设置 software-default round trip, and the last-open directory).
// Session implements it because the eight methods' eventual owners are split
// across three hosts — but the coordinator must be able to reach the port
// without reaching Session, QWidget, or QML/QSG.
//
// This target links Qt6::Core + Qt6::Test only. If the port ever grows a
// method that needs Session or a window to implement, FakePreferences below
// fails to compile it — and if the header itself ever pulls in Session.h or
// a Widgets/QML type, this whole target fails to LINK, which is a stronger
// guarantee than grepping for the forbidden names.

#include "app/v2/PlaybackPreferencesPort.h"

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

// A stand-in preferences store. Its only job is to prove the contract can be
// implemented with no Session and no window; the production implementer is
// Session itself.
class FakePreferences final : public miacode::v2::PlaybackPreferencesPort
{
public:
    void savePortableState() const override { ++savePortableStateCount; }

    void setPreviewRenderSetting(const QString& key, const QVariant& value) override
    {
        renderSettings[key] = value;
    }

    QVariantMap previewRenderSettings() const override { return renderSettings; }

    void loadProjectRenderState() override { ++loadProjectRenderStateCount; }

    void savePreviewAudioSettingsAsSoftwareDefault() override
    {
        softwareDefaultAudioSettings = liveAudioSettings;
    }

    void restorePreviewAudioSettingsFromSoftwareDefault() override
    {
        applyPreviewAudioSettingsFromUi(softwareDefaultAudioSettings);
    }

    void applyPreviewAudioSettingsFromUi(const PreviewAudioSettings& settings) override
    {
        liveAudioSettings = settings;
        ++applyPreviewAudioSettingsFromUiCount;
    }

    void setLastOpenDirectory(const QString& pathOrDir) override { lastOpenDirectory = pathOrDir; }

    // Mutable: savePortableState() is const on the port (it is called from
    // const contexts in the coordinator), but a fake still has to record that
    // it ran.
    mutable int savePortableStateCount = 0;
    int loadProjectRenderStateCount = 0;
    int applyPreviewAudioSettingsFromUiCount = 0;
    QVariantMap renderSettings;
    PreviewAudioSettings liveAudioSettings;
    PreviewAudioSettings softwareDefaultAudioSettings;
    QString lastOpenDirectory;
};

bool verifyImplementableWithoutSessionOrAWindow(QTextStream& err)
{
    FakePreferences preferences;
    miacode::v2::PlaybackPreferencesPort& contract = preferences;

    contract.savePortableState();
    contract.savePortableState();
    bool ok = require(preferences.savePortableStateCount == 2,
                      QStringLiteral("savePortableState reaches the implementation each call"), err);

    contract.setPreviewRenderSetting(QStringLiteral("brightnessOuter"), 80);
    ok &= require(contract.previewRenderSettings().value(QStringLiteral("brightnessOuter")).toInt() == 80,
                  QStringLiteral("a written render setting round-trips through the read map"), err);

    contract.loadProjectRenderState();
    ok &= require(preferences.loadProjectRenderStateCount == 1,
                  QStringLiteral("loadProjectRenderState reaches the implementation"), err);

    PreviewAudioSettings edited;
    edited.globalVolume = 0.55;
    edited.trackVolume = 0.4;
    contract.applyPreviewAudioSettingsFromUi(edited);
    ok &= require(preferences.applyPreviewAudioSettingsFromUiCount == 1
                      && preferences.liveAudioSettings.globalVolume == 0.55,
                  QStringLiteral("an applied audio setting is visible to the implementation"), err);

    contract.savePreviewAudioSettingsAsSoftwareDefault();
    PreviewAudioSettings reverted;
    reverted.globalVolume = 0.1;
    contract.applyPreviewAudioSettingsFromUi(reverted);
    contract.restorePreviewAudioSettingsFromSoftwareDefault();
    ok &= require(preferences.liveAudioSettings.globalVolume == 0.55,
                  QStringLiteral("restoring the software default undoes an unsaved edit"), err);

    contract.setLastOpenDirectory(QStringLiteral("/tmp/charts"));
    ok &= require(preferences.lastOpenDirectory == QStringLiteral("/tmp/charts"),
                  QStringLiteral("the last-open directory reaches the implementation"), err);

    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = verifyImplementableWithoutSessionOrAWindow(err);

    if (ok) {
        QTextStream(stdout) << "preferences_port_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
