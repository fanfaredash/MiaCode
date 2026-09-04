#include "runtime/document/DocumentSessionHost.h"
#include "app/v2/UiRequestService.h"
#include "runtime/Shared.h"
#include "runtime/export/VideoExportHost.h"
#include "runtime/shell/ShellHost.h"

#include "BracketScopeHighlighter.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/ProjectPreferences.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <algorithm>

#include <QtCore>
#include <QtGui>
using namespace miacode::runtime::shared;
#include "runtime/document/DocumentFlow.Internal.h"

miacode::runtime::DocumentSessionHost::DocumentSessionHost(
    ::Session& session,
    RuntimeContext::Ui& ui,
    RuntimeContext::State& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
{}

bool miacode::runtime::DocumentSessionHost::applyCurrentFieldToDocument()
{
    // QML already commits into ChartWorkspace. Flushing hidden widget fields
    // would overwrite that. This path only clears the widget dirty latch.
    anchorCurrentFieldCleanState();
    state_.currentFieldDirty_ = false;
    updateDirtyState();
    return true;
}

void miacode::runtime::DocumentSessionHost::cancelPendingStartupRestore()
{
    if (!state_.startupRestorePending_) {
        return;
    }
    state_.startupRestorePending_ = false;
    ++state_.startupRestoreGeneration_;
}

bool Session::applyCurrentFieldToDocument()
{
    return documents_->applyCurrentFieldToDocument();
}

void Session::onOpenCurrentFolder()
{
    const QFileInfo fileInfo(currentFilePath_);
    const QString folderPath = currentFilePath_.isEmpty()
        ? QString()
        : fileInfo.absoluteDir().absolutePath();
    if (!folderPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
    }
}

void Session::addRecentFilePath(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return;
    }
    recentFilePaths_.removeAll(normalizedPath);
    recentFilePaths_.prepend(normalizedPath);
    while (recentFilePaths_.size() > 10) {
        recentFilePaths_.removeLast();
    }
    savePortableState();
}

QVariantList miacode::runtime::DocumentSessionHost::recentDocumentEntries()
{
    QStringList existing;
    QSet<QString> seen;
    QVariantList entries;
    for (const QString& path : state_.recentFilePaths_) {
        const QString normalized = path.isEmpty() ? QString() : QDir::cleanPath(path);
        if (normalized.isEmpty() || seen.contains(normalized)) {
            continue;
        }
        const QFileInfo info(normalized);
        if (!info.exists() || !info.isFile()) {
            continue;
        }
        seen.insert(normalized);
        existing.append(normalized);
        const QString folderName = info.absoluteDir().dirName().trimmed();
        entries.append(QVariantMap{
            {QStringLiteral("path"), normalized},
            {QStringLiteral("label"), folderName.isEmpty()
                 ? QDir::toNativeSeparators(info.absoluteFilePath())
                 : folderName},
        });
    }
    if (existing != state_.recentFilePaths_) {
        state_.recentFilePaths_ = existing;
        session_.savePortableState();
    }
    return entries;
}

void miacode::runtime::DocumentSessionHost::restoreBackupDocument(const QString& path)
{
    restoreBackupFilePath(path);
}

void miacode::runtime::DocumentSessionHost::noteRecentDocument(const QString& path)
{
    session_.addRecentFilePath(path);
}

bool Session::openFileAtPath(const QString& path, bool showErrors)
{
    return documents_->openFileAtPath(path, showErrors);
}

// openStartupTarget runs at launch and from the chart-drop switch prompt, both
// of which the v2 shell reaches, so what it has to say goes to the shell.
void Session::postShellNotice(const QString& title, const QString& text)
{
    if (miacode::v2::UiRequestService* const requests = uiRequestService()) {
        requests->postNotice(miacode::v2::NoticeSeverity::Warning, title, text);
    }
}

bool Session::openStartupTarget(const QString& path)
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return false;
    }

    const QFileInfo info(normalizedPath);
    if (info.isDir()) {
        const QString maidataPath = QDir(info.absoluteFilePath()).filePath(QStringLiteral("maidata.txt"));
        if (QFileInfo::exists(maidataPath) && QFileInfo(maidataPath).isFile()) {
            return openFileAtPath(maidataPath, true);
        }

        setCurrentFilePath(QString(), true);
        applicationServices_.workspace().openSource(SimaiDocument::createEmpty().toText());
        loadDocument();
        postShellNotice(
            UiText::text(QStringLiteral("dialog.open_startup_folder.missing_maidata.title")),
            UiText::text(QStringLiteral("dialog.open_startup_folder.missing_maidata.message"))
                .arg(QDir::toNativeSeparators(info.absoluteFilePath()))
        );
        return false;
    }

    if (info.exists() && info.isFile()) {
        return openFileAtPath(info.absoluteFilePath(), true);
    }

    postShellNotice(
        UiText::text(QStringLiteral("dialog.open_startup_target.missing.title")),
        UiText::text(QStringLiteral("dialog.open_startup_target.missing.message"))
            .arg(QDir::toNativeSeparators(normalizedPath))
    );
    return false;
}

bool Session::restoreLastSessionFile()
{
    return documents_->restoreLastSessionFile();
}

void Session::scheduleStartupRestoreLastSessionFile()
{
    documents_->scheduleStartupRestoreLastSessionFile();
}

void Session::cancelPendingStartupRestore()
{
    documents_->cancelPendingStartupRestore();
}

void Session::applyPreparedStartupRestoreDocument(const PreparedStartupRestoreDocument& prepared)
{
    documents_->applyPreparedStartupRestoreDocument(prepared);
}

void Session::applyOpenedDocumentState(
    const QString& normalizedPath,
    TextEncoding encodingUsed,
    const SimaiDocument& document,
    double knownTrackDurationSeconds)
{
    documents_->applyOpenedDocumentState(
        normalizedPath,
        encodingUsed,
        document,
        knownTrackDurationSeconds
    );
}

void Session::resetAutosaveState(const QString& referenceText)
{
    documents_->resetAutosaveState(referenceText);
}

QString Session::resolveAutosaveDirectoryPath() const
{
    return documents_->resolveAutosaveDirectoryPath();
}

QString Session::currentDocumentTextForAutosave() const
{
    return documents_->currentDocumentTextForAutosave();
}

void Session::pruneAutosaveFiles(const QString& autosaveDirectoryPath) const
{
    documents_->pruneAutosaveFiles(autosaveDirectoryPath);
}

void Session::runAutosaveCheck(bool allowHistory)
{
    documents_->runAutosaveCheck(allowHistory);
}

bool Session::saveToPath(const QString& path)
{
    return documents_->saveToPath(path);
}

void Session::handleAudioDrop(const QStringList& audioPaths,
                                 quint64 requestId,
                                 quint64 generation,
                                 miacode::v2::ChartDropImportService::Completion completion)
{
    if (documents_ == nullptr || chartDropImportService_ == nullptr) {
        if (completion) {
            completion({requestId, generation, true, true, true, 0,
                        static_cast<int>(audioPaths.size()), {}});
        }
        return;
    }
    chartDropImportService_->submit(
        audioPaths,
        requestId,
        generation,
        documents_->chartDropImportAdapter(),
        std::move(completion));
}

void Session::releaseChartDropImportService()
{
    if (chartDropImportService_ != nullptr) {
        chartDropImportService_->release();
    }
}

void Session::updatePauseButtonAppearance()
{
    documents_->updatePauseButtonAppearance();
}

void Session::updateDirtyState()
{
    documents_->updateDirtyState();
}

void Session::refreshCurrentFieldDirtyState()
{
    documents_->refreshCurrentFieldDirtyState();
}

void Session::markCurrentFieldDirty()
{
    documents_->markCurrentFieldDirty();
}

void Session::clearDeletedDifficultyUndoState()
{
    documents_->clearDeletedDifficultyUndoState();
}

bool Session::undoDeletedDifficultyField()
{
    return documents_->undoDeletedDifficultyField();
}

bool Session::switchToMetadataField()
{
    return documents_->switchToMetadataField();
}

bool Session::switchToDifficultyField(int difficultyId)
{
    return documents_->switchToDifficultyField(difficultyId);
}

bool Session::switchToLatencyField()
{
    return documents_->switchToLatencyField();
}

bool Session::switchToExportField()
{
    return documents_->switchToExportField();
}

bool miacode::runtime::DocumentSessionHost::enterDifficultyPage(int difficultyId)
{
    return switchToDifficultyField(difficultyId);
}

bool miacode::runtime::DocumentSessionHost::enterMetadataPage()
{
    return switchToMetadataField();
}

bool miacode::runtime::DocumentSessionHost::enterLatencyPage()
{
    return switchToLatencyField();
}

bool miacode::runtime::DocumentSessionHost::enterExportPage()
{
    return switchToExportField();
}

void miacode::runtime::DocumentSessionHost::packChartAsZip()
{
    session_.videoExport_->onPackAsZip();
}

void miacode::runtime::DocumentSessionHost::openPreferences()
{
    emit session_.preferencesRequested();
}

void miacode::runtime::DocumentSessionHost::requestShellClose(std::function<void(bool)> onDecided)
{
    session_.shell_->requestShellClose(std::move(onDecided));
}

void Session::activateInitialField()
{
    documents_->activateInitialField();
}

void Session::loadDocument()
{
    documents_->loadDocument();
}

void Session::clearTimelineAndPreview()
{
    documents_->clearTimelineAndPreview();
}
