// Contract regression for the export engine seam.
//
// Stage 3.5 item 2: QmlExportSession used to reach MainWindow::exportSection_,
// a private member holding a 3,600-line partial-class section — the last direct
// private-member read anywhere in src/app/qml_ui. An accessor could not honestly
// fix that: the page was depending on a concrete piece of the window's insides.
//
// miacode::v2::ExportEngine is the seam that replaced it. This target links
// Qt6::Core / Qt6::Gui / Qt6::Test only, so the interface failing to link here
// is how a QtWidgets type creeping into the contract gets caught — which is the
// whole point, since the implementation still lives inside a QMainWindow.
//
// The behaviour worth pinning is the slot discipline. The engine is the one
// service ApplicationServices does not own: the window installs itself and
// withdraws before teardown, and consumers bind to the slot rather than to a
// snapshot, so the withdrawal is visible to every holder at once.

#include "app/v2/ApplicationServices.h"
#include "app/v2/ExportEngine.h"

#include <QCoreApplication>
#include <QDirIterator>
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

// A stand-in implementation. Its only job is to prove the contract can be
// implemented without a window — the production one is MainWindow::ExportSection.
class FakeExportEngine final : public miacode::v2::ExportEngine
{
public:
    VideoExportTask buildSeedTask(int difficultyId) override
    {
        seededDifficultyId = difficultyId;
        VideoExportTask task;
        task.contentDurationSeconds = 12.5;
        return task;
    }

    void applySharedTaskSettings(const VideoExportTask& task) override
    {
        appliedDurationSeconds = task.contentDurationSeconds;
    }

    bool startAudition(int difficultyId, const VideoExportTask&) override
    {
        auditioningDifficultyId = difficultyId;
        return true;
    }

    void stopAudition() override { auditioningDifficultyId = 0; }

    bool launchVideoExport(const VideoExportTask&, int difficultyId,
                           QString* errorMessage) override
    {
        if (difficultyId == 0) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("no difficulty");
            }
            return false;
        }
        launchedDifficultyId = difficultyId;
        return true;
    }

    bool launchBatchExport(const VideoExportTask&, const QStringList& chartDirectories,
                           const QList<int>& selectedDifficultyIds, const QString&,
                           BatchResult* result, const BatchCallbacks& callbacks,
                           QString*) override
    {
        if (callbacks.progressChanged) {
            callbacks.progressChanged(50, QStringLiteral("half"));
        }
        if (callbacks.cancellationRequested && callbacks.cancellationRequested()) {
            if (result != nullptr) {
                result->canceled = true;
            }
            return true;
        }
        if (result != nullptr) {
            result->successCount = chartDirectories.size() * selectedDifficultyIds.size();
        }
        return true;
    }

    void cancelVideoExport() override { ++cancelCount; }

    int seededDifficultyId = 0;
    int auditioningDifficultyId = 0;
    int launchedDifficultyId = 0;
    int cancelCount = 0;
    double appliedDurationSeconds = 0.0;
};

// The contract is implementable with no window, no QApplication, no widget.
bool verifyImplementableWithoutAWindow(QTextStream& err)
{
    FakeExportEngine engine;
    miacode::v2::ExportEngine& contract = engine;

    const VideoExportTask seed = contract.buildSeedTask(3);
    bool ok = require(engine.seededDifficultyId == 3 && seed.contentDurationSeconds > 0.0,
                      QStringLiteral("seeding a task reaches the implementation"), err);

    contract.applySharedTaskSettings(seed);
    ok &= require(engine.appliedDurationSeconds == seed.contentDurationSeconds,
                  QStringLiteral("live settings reach the implementation"), err);

    ok &= require(contract.startAudition(4, seed) && engine.auditioningDifficultyId == 4,
                  QStringLiteral("audition starts on the requested difficulty"), err);
    contract.stopAudition();
    ok &= require(engine.auditioningDifficultyId == 0,
                  QStringLiteral("stopping the audition clears it"), err);

    contract.cancelVideoExport();
    ok &= require(engine.cancelCount == 1,
                  QStringLiteral("cancel reaches the implementation"), err);
    return ok;
}

// A launch that cannot start must say why, so the page can show it rather than
// failing silently.
bool verifyFailedLaunchReportsItsReason(QTextStream& err)
{
    FakeExportEngine engine;
    QString error;
    bool ok = require(!engine.launchVideoExport(VideoExportTask{}, 0, &error)
                          && !error.isEmpty(),
                      QStringLiteral("a refused launch returns false and fills the message"),
                      err);
    ok &= require(engine.launchVideoExport(VideoExportTask{}, 2, &error)
                      && engine.launchedDifficultyId == 2,
                  QStringLiteral("an accepted launch reports the difficulty it started"), err);
    return ok;
}

// Batch runs synchronously, so progress and cancellation travel through the
// caller's callbacks; a cancel is an outcome, not an error.
bool verifyBatchReportsThroughItsCallbacks(QTextStream& err)
{
    FakeExportEngine engine;

    int lastPercent = -1;
    miacode::v2::ExportEngine::BatchCallbacks callbacks;
    callbacks.progressChanged = [&lastPercent](int percent, const QString&) {
        lastPercent = percent;
    };
    callbacks.cancellationRequested = [] { return false; };

    miacode::v2::ExportEngine::BatchResult result;
    QString error;
    bool ok = require(engine.launchBatchExport(VideoExportTask{},
                                               QStringList{QStringLiteral("/a"),
                                                           QStringLiteral("/b")},
                                               QList<int>{3, 4}, QStringLiteral("/out"),
                                               &result, callbacks, &error)
                          && lastPercent == 50 && result.successCount == 4
                          && !result.canceled,
                      QStringLiteral("batch reports progress and its success count"), err);

    miacode::v2::ExportEngine::BatchResult cancelled;
    callbacks.cancellationRequested = [] { return true; };
    ok &= require(engine.launchBatchExport(VideoExportTask{},
                                           QStringList{QStringLiteral("/a")},
                                           QList<int>{3}, QStringLiteral("/out"),
                                           &cancelled, callbacks, &error)
                      && cancelled.canceled,
                  QStringLiteral("a user cancel is an outcome, not a launch failure"), err);
    return ok;
}

// The slot, not a snapshot: whoever holds the engine sees the window's
// withdrawal immediately. This is what stops the export page — a QObject child
// that outlives the section it used to point at — from calling into a
// destroyed engine during teardown.
bool verifyTheSlotIsTheSingleSourceOfTruth(QTextStream& err)
{
    miacode::v2::ApplicationServices services;
    bool ok = require(services.exportEngine() == nullptr,
                      QStringLiteral("no engine is installed until a window installs one"),
                      err);

    miacode::v2::ExportEngine*& slot = services.exportEngineSlot();
    ok &= require(slot == nullptr,
                  QStringLiteral("the slot starts empty too"), err);

    FakeExportEngine engine;
    services.setExportEngine(&engine);
    ok &= require(slot == &engine && services.exportEngine() == &engine,
                  QStringLiteral("installing is visible through a slot bound earlier"), err);

    services.setExportEngine(nullptr);
    ok &= require(slot == nullptr && services.exportEngine() == nullptr,
                  QStringLiteral("withdrawing is visible through that same bound slot"), err);
    return ok;
}

// Stage 3 declared "Widget 对话框归零" while checking only src/tools and
// src/app/qml_ui — so three QMessageBoxes survived in the export WORKER, which
// lives in src/app/mainwindow. They were not dead code: finishing, failing or
// cancelling a v2 export all ran through them, which is why cancelling a single
// export showed a Qt-styled box while cancelling a batch export showed the
// app's own notice. The whole export section is reachable only from v2 now, so
// it gets the check stage 3's grep could not give it.
bool verifyTheExportSectionShowsNoWidgetDialogs(QTextStream& err)
{
    static const QStringList forbidden = {
        QStringLiteral("QMessageBox "),
        QStringLiteral("QMessageBox("),
        QStringLiteral("QMessageBox::"),
        QStringLiteral("QFileDialog::"),
        QStringLiteral("QInputDialog::"),
        QStringLiteral("QProgressDialog"),
    };

    bool ok = true;
    int scanned = 0;
    QDirIterator walk(QStringLiteral(MIACODE_SOURCE_ROOT)
                          + QStringLiteral("/src/app/mainwindow/sections/export"),
                      QStringList{QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                      QDir::Files, QDirIterator::Subdirectories);
    while (walk.hasNext()) {
        const QString path = walk.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        ++scanned;
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QString line = lines.at(i);
            // Comments may name these types; the guard is about code.
            if (line.trimmed().startsWith(QStringLiteral("//"))) {
                continue;
            }
            for (const QString& token : forbidden) {
                if (line.contains(token)) {
                    ok = require(false,
                                 QStringLiteral("%1:%2 shows a Widgets dialog from the export "
                                                "section — route it through UiRequestService")
                                     .arg(QString(path).remove(
                                              0, QStringLiteral(MIACODE_SOURCE_ROOT).size() + 1))
                                     .arg(i + 1),
                                 err);
                }
            }
        }
    }
    ok &= require(scanned > 0, QStringLiteral("the export section scan found files"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;

    ok &= verifyImplementableWithoutAWindow(err);
    ok &= verifyFailedLaunchReportsItsReason(err);
    ok &= verifyBatchReportsThroughItsCallbacks(err);
    ok &= verifyTheSlotIsTheSingleSourceOfTruth(err);
    ok &= verifyTheExportSectionShowsNoWidgetDialogs(err);

    if (ok) {
        QTextStream(stdout) << "export_engine_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
