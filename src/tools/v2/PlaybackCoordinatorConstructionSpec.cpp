// Stage 4.9f pre-work probe: can PlaybackCoordinator be constructed and
// linked outside the full Session/SessionBootstrap assembly?
//
// This is NOT the fake-clock transition-coverage spec the completion gate
// asks for. It is the minimum viable construction: one QObject owner, one
// ApplicationServices, one RuntimeContext (supplying Ui/State by reference),
// and the four narrow ports satisfied by trivial fakes (same shape as
// PreferencesPortSpec.cpp et al.). If this links, the coordinator is
// link-independent of Session; if it does not, the SOURCES list added while
// chasing undefined symbols is the evidence of how far the split still has
// to go.

#include "runtime/RuntimeContext.h"
#include "runtime/playback/PlaybackCoordinator.h"

#include "app/v2/ApplicationServices.h"
#include "app/v2/PlaybackDocumentPort.h"
#include "app/v2/PlaybackPreferencesPort.h"
#include "app/v2/PlaybackPreviewPort.h"
#include "app/v2/PlaybackValidationPort.h"

#include <QCoreApplication>
#include <QObject>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

class FakePreferences final : public miacode::v2::PlaybackPreferencesPort
{
public:
    void savePortableState() const override {}
    void setPreviewRenderSetting(const QString&, const QVariant&) override {}
    QVariantMap previewRenderSettings() const override { return {}; }
    void loadProjectRenderState() override {}
    void savePreviewAudioSettingsAsSoftwareDefault() override {}
    void restorePreviewAudioSettingsFromSoftwareDefault() override {}
    void applyPreviewAudioSettingsFromUi(const PreviewAudioSettings&) override {}
    void setLastOpenDirectory(const QString&) override {}
};

class FakeValidation final : public miacode::v2::PlaybackValidationPort
{
public:
    void setMuriRenderMode(RenderMode, bool) override {}
    void clearValidationCache() override {}
    void clearValidationDecorations() override {}
    void applyAlignedMuriAnalysisReportToViews() override {}
    void applyDeferredAnalysisUiUpdates() override {}
    void notifyDocumentValidationChanged() override {}
};

class FakeDocuments final : public miacode::v2::PlaybackDocumentPort
{
public:
    void updateDirtyState() override {}
    bool applyCurrentFieldToDocument() override { return true; }
    bool requestEditorNavigation(int, int, int, int, bool, bool, bool) override { return true; }
    quint64 appliedWorkspaceRevision() const override { return 0; }
};

class FakePreview final : public miacode::v2::PlaybackPreviewPort
{
public:
    void ensurePreviewStageMediaRouteInitialized() override {}
    void syncPreviewStageMediaRouteChartPath(const QString&, const QString&, double, const QString&) override {}
    void schedulePreviewSubsystemWarmup() override {}
    void applyPreviewAudioSettingsToRuntime() override {}
    void loadProjectAudioPreferences() override {}
    void applyEffectivePreviewOutlineVariantToCanvas() override {}
    void preparePreviewForShutdown() override {}
    QString resolvePreviewSkinDir() const override { return {}; }
    void applyPreviewOutlineVariant(PreviewOutlineVariant, bool, bool) override {}
};

bool verifyCoordinatorConstructs(QTextStream& err)
{
    QObject owner;
    miacode::v2::ApplicationServices services;
    miacode::runtime::RuntimeContext context;
    FakePreferences preferences;
    FakeValidation validation;
    FakeDocuments documents;
    FakePreview preview;

    miacode::runtime::PlaybackCoordinator coordinator(
        owner, services, context.ui, context.state,
        preferences, validation, documents, preview,
        /*sessionGeneration=*/7);

    bool ok = require(!coordinator.playing(),
                      QStringLiteral("a freshly constructed coordinator is not playing"), err);
    ok &= require(coordinator.positionSeconds() == 0.0,
                  QStringLiteral("a freshly constructed coordinator starts at position 0"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = verifyCoordinatorConstructs(err);

    if (ok) {
        QTextStream(stdout) << "playback_coordinator_construction_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
