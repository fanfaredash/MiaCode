// Contract regression for stage 4.9d-4b-2b's second narrow port.
//
// PlaybackValidationPort is the coordinator's seam onto muri validation and
// analysis presentation (render-mode selection, the validation cache, muri
// decorations, and the deferred analysis UI tail). Unlike
// PlaybackPreferencesPort — cut by capability because its methods' eventual
// owners are split across three hosts — this port is cut by host: all
// methods already belong to ValidationHost, and Session implements the port
// only insofar as ValidationHost does.
//
// This target links Qt6::Core + Qt6::Test only. If the port ever grows a
// method that needs Session or a window to implement, FakeValidation below
// fails to compile it — and if the header itself ever pulls in Session.h or
// a Widgets/QML type, this whole target fails to LINK, which is a stronger
// guarantee than grepping for the forbidden names.

#include "app/v2/PlaybackValidationPort.h"

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

// A stand-in validation presenter. Its only job is to prove the contract can
// be implemented with no Session and no window; the production implementer
// is ValidationHost.
class FakeValidation final : public miacode::v2::PlaybackValidationPort
{
public:
    void setMuriRenderMode(RenderMode mode, bool persistState) override
    {
        lastMode = mode;
        lastPersistState = persistState;
        ++setMuriRenderModeCount;
    }

    void clearValidationCache() override { ++clearValidationCacheCount; }

    void clearValidationDecorations() override { ++clearValidationDecorationsCount; }

    void applyAlignedMuriAnalysisReportToViews() override { ++applyAlignedMuriAnalysisReportToViewsCount; }

    void applyDeferredAnalysisUiUpdates() override { ++applyDeferredAnalysisUiUpdatesCount; }

    void flushPendingMuriDiagnosticsPanelRefresh() override { ++flushPendingMuriDiagnosticsPanelRefreshCount; }

    void scheduleBottomTabsIssueListRelayout() override { ++scheduleBottomTabsIssueListRelayoutCount; }

    void notifyDocumentValidationChanged() override { ++notifyDocumentValidationChangedCount; }

    RenderMode lastMode = RenderMode::Native;
    bool lastPersistState = false;
    int setMuriRenderModeCount = 0;
    int clearValidationCacheCount = 0;
    int clearValidationDecorationsCount = 0;
    int applyAlignedMuriAnalysisReportToViewsCount = 0;
    int applyDeferredAnalysisUiUpdatesCount = 0;
    int flushPendingMuriDiagnosticsPanelRefreshCount = 0;
    int scheduleBottomTabsIssueListRelayoutCount = 0;
    int notifyDocumentValidationChangedCount = 0;
};

bool verifyImplementableWithoutSessionOrAWindow(QTextStream& err)
{
    FakeValidation validation;
    miacode::v2::PlaybackValidationPort& contract = validation;

    contract.setMuriRenderMode(RenderMode::MaimuriDxStyle, true);
    bool ok = require(validation.setMuriRenderModeCount == 1
                      && validation.lastMode == RenderMode::MaimuriDxStyle
                      && validation.lastPersistState,
                  QStringLiteral("setMuriRenderMode reaches the implementation with both arguments"), err);

    contract.clearValidationCache();
    ok &= require(validation.clearValidationCacheCount == 1,
                  QStringLiteral("clearValidationCache reaches the implementation"), err);

    contract.clearValidationDecorations();
    ok &= require(validation.clearValidationDecorationsCount == 1,
                  QStringLiteral("clearValidationDecorations reaches the implementation"), err);

    contract.applyAlignedMuriAnalysisReportToViews();
    ok &= require(validation.applyAlignedMuriAnalysisReportToViewsCount == 1,
                  QStringLiteral("applyAlignedMuriAnalysisReportToViews reaches the implementation"), err);

    contract.applyDeferredAnalysisUiUpdates();
    ok &= require(validation.applyDeferredAnalysisUiUpdatesCount == 1,
                  QStringLiteral("applyDeferredAnalysisUiUpdates reaches the implementation"), err);

    contract.flushPendingMuriDiagnosticsPanelRefresh();
    ok &= require(validation.flushPendingMuriDiagnosticsPanelRefreshCount == 1,
                  QStringLiteral("flushPendingMuriDiagnosticsPanelRefresh reaches the implementation"), err);

    contract.scheduleBottomTabsIssueListRelayout();
    ok &= require(validation.scheduleBottomTabsIssueListRelayoutCount == 1,
                  QStringLiteral("scheduleBottomTabsIssueListRelayout reaches the implementation"), err);

    contract.notifyDocumentValidationChanged();
    ok &= require(validation.notifyDocumentValidationChangedCount == 1,
                  QStringLiteral("notifyDocumentValidationChanged reaches the implementation"), err);

    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = verifyImplementableWithoutSessionOrAWindow(err);

    if (ok) {
        QTextStream(stdout) << "validation_port_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
