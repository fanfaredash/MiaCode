#include "runtime/export/VideoExportHost.h"

#include "runtime/shell/ShellHost.h"
#include "app/qml_ui/export/QmlExportSession.h"

#include <QPointer>

miacode::runtime::VideoExportHost::VideoExportHost(Session& session, RuntimeContext::Ui& ui, RuntimeContext::State& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
{}

// ---- miacode::v2::ExportEngine ----
//
// Thin forwarders. The existing widget-side names stay because ~20 call sites
// inside Session use them; the interface names are what the QML page sees.
// When the engine moves out of the window, these bodies move with it and the
// page is untouched.

VideoExportTask miacode::runtime::VideoExportHost::buildSeedTask(int difficultyId)
{
    return buildVideoExportSeedTaskPublic(difficultyId);
}

void miacode::runtime::VideoExportHost::applySharedTaskSettings(const VideoExportTask& task)
{
    applySharedExportTaskSettings(task);
}

bool miacode::runtime::VideoExportHost::startAudition(int difficultyId, const VideoExportTask& visualTask)
{
    return startQmlExportAudition(difficultyId, visualTask);
}

void miacode::runtime::VideoExportHost::stopAudition()
{
    stopQmlExportAudition();
}

bool miacode::runtime::VideoExportHost::launchVideoExport(const VideoExportTask& requestedTask,
                                                  int difficultyId, QString* errorMessage)
{
    return launchQmlVideoExport(requestedTask, difficultyId, errorMessage);
}

bool miacode::runtime::VideoExportHost::launchBatchExport(const VideoExportTask& templateTask,
                                                  const QStringList& chartDirectories,
                                                  const QList<int>& selectedDifficultyIds,
                                                  const QString& outputDirectory,
                                                  BatchResult* result,
                                                  const BatchCallbacks& callbacks,
                                                  QString* errorMessage)
{
    return launchQmlBatchExport(templateTask, chartDirectories, selectedDifficultyIds,
                                outputDirectory, result, callbacks, errorMessage);
}

void miacode::runtime::VideoExportHost::cancelVideoExport()
{
    cancelVideoExportWorker();
}

QList<int> miacode::runtime::VideoExportHost::difficultyIds() const
{
    const QVector<int> ids = session_.documentDifficultyIds();
    return QList<int>(ids.cbegin(), ids.cend());
}

QString miacode::runtime::VideoExportHost::difficultyChartText(int difficultyId) const
{
    return session_.documentDifficultyChartText(difficultyId);
}

int miacode::runtime::VideoExportHost::lastOpenedDifficultyId() const
{
    return session_.projectLastOpenedDifficultyId();
}

MuriRenderOptions miacode::runtime::VideoExportHost::muriRenderOptions() const
{
    return session_.muriRenderOptions();
}

double miacode::runtime::VideoExportHost::currentAudioClockSecond() const
{
    return session_.currentPreviewAuthoritativeAudioClockSecond();
}

void miacode::runtime::VideoExportHost::refreshIntroState()
{
    session_.refreshExportIntroState();
}

int Session::resolveToolsMenuExportDifficultyId() const
{
    // While the export page is open it owns the difficulty the Tools-menu
    // export actions apply to; Session keeps activeDifficultyId_ == 0 there.
    if (qmlExportSession_ != nullptr && qmlExportSession_->pageSessionActive()) {
        const int selectedDifficultyId = qmlExportSession_->selectedDifficultyId();
        if (SimaiDocument::isDifficultyId(selectedDifficultyId)
            && applicationServices_.workspace().document().difficulty(selectedDifficultyId) != nullptr) {
            return selectedDifficultyId;
        }
    }
    return activeDifficultyId_;
}

void Session::onExportCover()
{
    emit coverExportRequested(resolveToolsMenuExportDifficultyId());
}

void Session::onBatchExportPreviewVideo()
{
    const int difficultyId = resolveToolsMenuExportDifficultyId();
    videoExport_->onBatchExportPreviewVideo(difficultyId);
}

void Session::onPackAsZip()
{
    videoExport_->onPackAsZip();
}

bool Session::exportPreviewVideoFromCli(const CliVideoExportRequest& request, QString* resolvedOutputPath, QString* errorMessage, QString* details)
{
    return videoExport_->exportPreviewVideoFromCli(request, resolvedOutputPath, errorMessage, details);
}
