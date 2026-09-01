// Contract regression for the editor page-routing seam.
//
// Stage 3.5 items 2-3: QmlEditorPageHost held the QML page id but called
// MainWindow's switchTo*Field entry points, which live in a private .inc — so
// it needed `friend class`, the last such grant to a QML type. Publishing those
// entry points was the wrong fix: they also drive a hidden QStackedWidget that
// exists only because the window has not been deleted, and making that public
// API would formalize exactly what item 3 removes.
//
// This target links Qt6::Core / Qt6::Test only. miacode::v2::EditorPageRouter
// is not even a QObject, so anything Qt-GUI-shaped creeping into the contract
// fails to link.

#include "app/v2/ApplicationServices.h"
#include "app/v2/EditorPageRouter.h"

#include <QCoreApplication>
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <functional>

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

QString readFile(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

// A stand-in router. Its only job is to prove the contract can be implemented
// with no window; the production one is MainWindow.
class FakeRouter final : public miacode::v2::EditorPageRouter
{
public:
    bool hasActiveDifficulty() const override { return activeDifficulty > 0; }
    int activeDifficultyId() const override { return activeDifficulty; }

    bool enterDifficultyPage(int difficultyId) override
    {
        if (refuseSwitches) {
            return false;
        }
        activeDifficulty = difficultyId;
        page = QStringLiteral("difficulty");
        return true;
    }

    bool enterMetadataPage() override { return enterOverlay(QStringLiteral("metadata")); }
    bool enterLatencyPage() override { return enterOverlay(QStringLiteral("latency")); }
    bool enterExportPage() override { return enterOverlay(QStringLiteral("export")); }

    void packChartAsZip() override { ++packCount; }
    void openPreferences() override { ++preferencesCount; }

    void requestShellClose(std::function<void(bool)> onDecided) override
    {
        ++closeRequestCount;
        if (onDecided) {
            onDecided(!refuseSwitches);
        }
    }

    bool refuseSwitches = false;
    int activeDifficulty = 0;
    int packCount = 0;
    int preferencesCount = 0;
    int closeRequestCount = 0;
    QString page;

private:
    bool enterOverlay(const QString& id)
    {
        if (refuseSwitches) {
            return false;
        }
        activeDifficulty = 0;
        page = id;
        return true;
    }
};

bool verifyImplementableWithoutAWindow(QTextStream& err)
{
    FakeRouter router;
    miacode::v2::EditorPageRouter& contract = router;

    bool ok = require(!contract.hasActiveDifficulty() && contract.activeDifficultyId() == 0,
                      QStringLiteral("a fresh router reports no active difficulty"), err);
    ok &= require(contract.enterDifficultyPage(3) && contract.hasActiveDifficulty()
                      && contract.activeDifficultyId() == 3,
                  QStringLiteral("entering a difficulty page makes it the active one"), err);
    ok &= require(contract.enterExportPage() && !contract.hasActiveDifficulty(),
                  QStringLiteral("an overlay page clears the active difficulty, which is what "
                                 "the page host records a resume target for"), err);
    ok &= require(contract.enterLatencyPage() && contract.enterMetadataPage(),
                  QStringLiteral("every page is reachable through the contract"), err);

    contract.packChartAsZip();
    ok &= require(router.packCount == 1,
                  QStringLiteral("packing a zip reaches the implementation"), err);

    contract.openPreferences();
    ok &= require(router.preferencesCount == 1,
                  QStringLiteral("偏好设置 reaches the implementation"), err);

    // Declining and dismissing are the same answer, so the continuation always
    // runs with an explicit verdict rather than being dropped.
    bool closeAnswer = false;
    int closeAnswers = 0;
    contract.requestShellClose([&](bool confirmed) {
        closeAnswer = confirmed;
        ++closeAnswers;
    });
    ok &= require(router.closeRequestCount == 1 && closeAnswers == 1 && closeAnswer,
                  QStringLiteral("the close question answers exactly once"), err);
    return ok;
}

// The return value is not decorative: a caller that shows a page after a
// refused switch leaves the shell and the document disagreeing about where the
// user is. The unsaved-changes guard is the switch that gets declined.
bool verifyRefusedSwitchesAreReported(QTextStream& err)
{
    FakeRouter router;
    router.refuseSwitches = true;
    miacode::v2::EditorPageRouter& contract = router;

    return require(!contract.enterDifficultyPage(3) && !contract.enterMetadataPage()
                       && !contract.enterLatencyPage() && !contract.enterExportPage()
                       && router.page.isEmpty(),
                   QStringLiteral("every refused switch returns false and changes nothing"),
                   err);
}

// Same slot discipline as the export engine: the window installs itself and
// withdraws before teardown, and holders bind to the slot so the withdrawal is
// visible at once rather than leaving each with a dangling copy.
bool verifyTheSlotIsTheSingleSourceOfTruth(QTextStream& err)
{
    miacode::v2::ApplicationServices services;
    bool ok = require(services.editorPageRouter() == nullptr,
                      QStringLiteral("no router is installed until a window installs one"), err);

    miacode::v2::EditorPageRouter*& slot = services.editorPageRouterSlot();
    FakeRouter router;
    services.setEditorPageRouter(&router);
    ok &= require(slot == &router && services.editorPageRouter() == &router,
                  QStringLiteral("installing is visible through a slot bound earlier"), err);

    services.setEditorPageRouter(nullptr);
    ok &= require(slot == nullptr && services.editorPageRouter() == nullptr,
                  QStringLiteral("withdrawing is visible through that same bound slot"), err);
    return ok;
}

// switchToExportField used to return true BEFORE the switch ran: it deferred
// the real work one event-loop tick behind a busy spinner drawn over the
// "Export" sidebar row. Both reasons for that are gone — the embedded video
// panel it was hiding was deleted with the Widgets export dialog, and the
// spinner sat on the hidden widget list's viewport, so it could never reach a
// screen. What survived was a switch that reported success it had not performed.
bool verifyTheExportSwitchReportsWhatItDid(QTextStream& err)
{
    const QString documentUi = readFile(
        QStringLiteral("src/app/runtime/document/DocumentPages.cpp"));
    const QString sectionHeader = readFile(
        QStringLiteral("src/app/runtime/document/DocumentSessionHost.h"));
    bool ok = require(!documentUi.isEmpty() && !sectionHeader.isEmpty(),
                      QStringLiteral("the document section sources are readable"), err);
    if (!ok) {
        return false;
    }

    const qsizetype start =
        documentUi.indexOf(QStringLiteral("bool miacode::runtime::DocumentSessionHost::switchToExportField()"));
    ok &= require(start >= 0, QStringLiteral("switchToExportField is present"), err);
    if (start < 0) {
        return false;
    }
    const qsizetype end = documentUi.indexOf(QStringLiteral("\n}\n"), start);
    const QString body = documentUi.mid(start, (end >= 0 ? end : documentUi.size()) - start);

    ok &= require(body.contains(QStringLiteral("performSwitchToExportField();"))
                      && !body.contains(QStringLiteral("QTimer::singleShot")),
                  QStringLiteral("the export switch runs inline, not one tick later"), err);
    ok &= require(body.contains(QStringLiteral("state_.activeOutlineKey_ == QLatin1String(\"export\")")),
                  QStringLiteral("it returns whether the switch actually landed"), err);
    ok &= require(!documentUi.contains(QStringLiteral("OutlineExportBusySpinner"))
                      && !sectionHeader.contains(QStringLiteral("OutlineExportBusySpinner")),
                  QStringLiteral("the invisible sidebar spinner scaffolding is gone"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;

    ok &= verifyImplementableWithoutAWindow(err);
    ok &= verifyRefusedSwitchesAreReported(err);
    ok &= verifyTheSlotIsTheSingleSourceOfTruth(err);
    ok &= verifyTheExportSwitchReportsWhatItDid(err);

    if (ok) {
        QTextStream(stdout) << "editor_page_router_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
