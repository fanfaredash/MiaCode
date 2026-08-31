// Contract regression for the non-Widget application service assembly.
//
// Stage 3.5 item 1 of docs/specs/ui/QML_UI_V2_PHASE1_TODO_ZH.md: the document,
// analysis, editor-sync, chart-drop, UI-request and job-progress services must
// have an owner that is not a QWidget and does not need one to exist. Before
// ApplicationServices they were split between MainWindow (UiRequestService,
// JobProgressService, EditorSyncController, ChartDropImportService) and
// QmlApplicationContext (ChartWorkspace, ChartWorkspaceFileService,
// AnalysisService), so "who owns the document domain" had two answers and both
// of them were UI objects.
//
// This target links Qt6::Core / Qt6::Gui only. If ApplicationServices ever
// reaches for QtWidgets — directly or through a header it includes — the spec
// fails to LINK, which is a stronger guarantee than grepping for QWidget.

#include "app/v2/ApplicationServices.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStringList>
#include <QTextStream>

#include <algorithm>

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

// The whole point of the assembly: it stands up with no window, no
// QApplication and no widget parent anywhere in the chain.
bool verifyConstructsWithoutAnyWidgetHost(QTextStream& err)
{
    miacode::v2::ApplicationServices services;
    bool ok = require(services.parent() == nullptr,
                      QStringLiteral("the assembly needs no parent object"), err);
    ok &= require(services.workspace().parent() == &services
                      && services.analysis().parent() == &services
                      && services.editorSync().parent() == &services
                      && services.chartDropImport().parent() == &services
                      && services.uiRequests().parent() == &services
                      && services.jobProgress().parent() == &services,
                  QStringLiteral("every QObject service is parented to the assembly"), err);
    return ok;
}

// One owner means one instance: a second UiRequestService would mean a second
// dialog host, and a second ChartWorkspace would mean a second document truth.
bool verifySingleInstancePerService(QTextStream& err)
{
    miacode::v2::ApplicationServices services;
    bool ok = require(&services.workspace() == &services.workspace()
                          && &services.files() == &services.files()
                          && &services.analysis() == &services.analysis()
                          && &services.editorSync() == &services.editorSync()
                          && &services.chartDropImport() == &services.chartDropImport()
                          && &services.uiRequests() == &services.uiRequests()
                          && &services.jobProgress() == &services.jobProgress(),
                      QStringLiteral("each accessor returns the same instance every call"), err);

    const miacode::v2::ApplicationServices& constView = services;
    ok &= require(&constView.workspace() == &services.workspace()
                      && &constView.uiRequests() == &services.uiRequests(),
                  QStringLiteral("the const accessors name the same instances"), err);
    return ok;
}

// The file service and the analysis service must be wired onto the assembly's
// own workspace, not onto one they built themselves.
bool verifyServicesShareOneWorkspace(QTextStream& err)
{
    miacode::v2::ApplicationServices services;
    const miacode::v2::ChartWorkspaceResult opened = services.workspace().openSource(
        QStringLiteral("&title=spec\n&wholebpm=120\n&inote_3=(120){1}1,\nE\n"));
    bool ok = require(opened.accepted,
                      QStringLiteral("the assembly's workspace accepts a document"), err);
    if (!ok) {
        return false;
    }

    const miacode::v2::ChartWorkspaceSnapshot workspaceState = services.workspace().snapshot();
    const miacode::v2::AnalysisSnapshot analysed =
        miacode::v2::AnalysisService::analyze(services.workspace(),
                                              services.validationLocale());
    ok &= require(analysed.difficultyId == workspaceState.activeDifficultyId
                      && analysed.revision == workspaceState.revision,
                  QStringLiteral("analysis reads the assembly's workspace, "
                                 "stamped with its revision"),
                  err);

    // The file service must hold the same workspace, not one of its own: a
    // second workspace is how "save" and "what is on screen" drift apart.
    ok &= require(&services.files().workspace() == &services.workspace(),
                  QStringLiteral("the file service is wired onto the assembly's workspace"), err);
    return ok;
}

// Destroying the assembly destroys everything it owns, in one step, with no
// dangling service left behind for a late callback to reach.
bool verifyDestructionReleasesEverything(QTextStream& err)
{
    QPointer<QObject> workspace;
    QPointer<QObject> uiRequests;
    QPointer<QObject> jobProgress;
    QPointer<QObject> chartDropImport;
    {
        miacode::v2::ApplicationServices services;
        workspace = &services.workspace();
        uiRequests = &services.uiRequests();
        jobProgress = &services.jobProgress();
        chartDropImport = &services.chartDropImport();
    }
    return require(workspace.isNull() && uiRequests.isNull() && jobProgress.isNull()
                       && chartDropImport.isNull(),
                   QStringLiteral("every owned service dies with the assembly"), err);
}

// The services stay usable through the assembly — the accessors hand out live
// objects, not copies or freshly-defaulted ones.
bool verifyServicesAreLiveThroughTheAssembly(QTextStream& err)
{
    miacode::v2::ApplicationServices services;

    QSignalSpy fileRequested(&services.uiRequests(),
                             &miacode::v2::UiRequestService::fileRequested);
    miacode::v2::FileRequest request;
    request.title = QStringLiteral("spec");
    const QString id = services.uiRequests().requestFile(request, [](const QString&) {});
    bool ok = require(!id.isEmpty() && fileRequested.count() == 1
                          && services.uiRequests().pendingFileRequestCount() == 1,
                      QStringLiteral("the assembly's UI request service is the live one"), err);
    services.uiRequests().cancelFileRequest(id);

    const quint64 token = services.jobProgress().begin(
        QStringLiteral("spec job"), QStringLiteral("running"), true);
    ok &= require(token != 0 && services.jobProgress().token() == token
                      && services.jobProgress().active(),
                  QStringLiteral("the assembly's job progress service is the live one"), err);
    services.jobProgress().end();

    return ok;
}

// Stage 3.5 requires the validation locale to have a non-Widget owner too: it
// used to come from MainWindowShared, which is a QtWidgets translation unit, so
// asking "what locale does the parser validate in" pulled in the widget layer.
bool verifyValidationLocaleHasANonWidgetOwner(QTextStream& err)
{
    miacode::v2::ApplicationServices services;
    const SimaiNativeValidationLocale locale = services.validationLocale();
    return require(locale == miacode::v2::uiValidationLocale(),
                   QStringLiteral("the assembly publishes the shared UI validation locale"), err);
}

// An assembly nobody owns through is worse than no assembly: the services would
// simply have three owners instead of two. Scan the product tree (src/app,
// excluding the assembly itself) for anything that constructs one of the owned
// services — `new X`, `make_unique<X>`, or a value member — and fail if it does.
// This is what stops a future change from quietly re-creating a second
// UiRequestService inside MainWindow while ApplicationServices sits unused.
bool verifyNothingElseInTheProductConstructsTheServices(QTextStream& err)
{
    static const QStringList ownedTypes = {
        QStringLiteral("ChartWorkspace"),
        QStringLiteral("ChartWorkspaceFileService"),
        QStringLiteral("AnalysisService"),
        QStringLiteral("EditorSyncController"),
        QStringLiteral("ChartDropImportService"),
        QStringLiteral("UiRequestService"),
        QStringLiteral("JobProgressService"),
    };

    const QString root = QStringLiteral(MIACODE_SOURCE_ROOT) + QStringLiteral("/src/app");
    // The assembly declares the members and constructs them; it is the one
    // place allowed to.
    const QStringList assembly = {QStringLiteral("/v2/ApplicationServices.h"),
                                  QStringLiteral("/v2/ApplicationServices.cpp")};

    bool ok = true;
    int scanned = 0;
    QDirIterator walk(root,
                      QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                                  QStringLiteral("*.mm"), QStringLiteral("*.inc")},
                      QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString path = walk.next();
        if (std::any_of(assembly.cbegin(), assembly.cend(),
                        [&path](const QString& owned) { return path.endsWith(owned); })) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const QString text = QString::fromUtf8(file.readAll());
        ++scanned;
        for (const QString& type : ownedTypes) {
            // `new X(`, `make_unique<X>`, or a value/unique_ptr member of it.
            const QRegularExpression construction(
                QStringLiteral("(new\\s+(miacode::v2::)?%1\\s*[({])"
                               "|(make_unique\\s*<\\s*(miacode::v2::)?%1\\s*>)"
                               "|(^\\s*(miacode::v2::)?%1\\s+\\w+_\\s*[;{=])")
                    .arg(type),
                QRegularExpression::MultilineOption);
            const auto match = construction.match(text);
            if (match.hasMatch()) {
                ok = require(false,
                             QStringLiteral("%1 is constructed outside ApplicationServices: "
                                            "%2 -> %3")
                                 .arg(type,
                                      QString(path).remove(0, QStringLiteral(MIACODE_SOURCE_ROOT).size() + 1),
                                      match.captured(0).trimmed()),
                             err);
            }
        }
    }
    ok &= require(scanned > 0, QStringLiteral("the product tree scan found files"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;

    ok &= verifyConstructsWithoutAnyWidgetHost(err);
    ok &= verifySingleInstancePerService(err);
    ok &= verifyServicesShareOneWorkspace(err);
    ok &= verifyDestructionReleasesEverything(err);
    ok &= verifyServicesAreLiveThroughTheAssembly(err);
    ok &= verifyValidationLocaleHasANonWidgetOwner(err);
    ok &= verifyNothingElseInTheProductConstructsTheServices(err);

    if (ok) {
        QTextStream(stdout) << "application_services_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
