#include "QmlExportSession.h"

#include "app/v2/JobProgressService.h"
#include "mainwindow/MainWindow.h"
#include "mainwindow/sections/export/MainWindow.ExportSection.h"
#include "UiText.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "core/scene/PreviewHudState.h"
#include "core/video/PreviewRenderSettings.h"
#include "preview/runtime/PreviewRuntime.h"
#include "audio/QtPreviewSfxRuntime.h"
#include "tools/video_export/VideoExportPreferences.h"
#include "tools/video_export/VideoExportSettings.h"
#include "tools/video_export/FontLibrary.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSettings>
#include <QUrl>

#include <utility>

namespace {

inline constexpr auto& kResolutionPresets = miacode::video_export::kVideoExportResolutionPresets;
inline constexpr auto& kFpsOptions = miacode::video_export::kVideoExportFpsOptions;
inline constexpr auto& kAudioBitrateOptions =
    miacode::video_export::kVideoExportAudioBitrateOptionsKbps;
constexpr double kMinimumExportRangeSeconds = 5.0;

double minimumExportRangeSecondsForChart(double chartDurationSeconds)
{
    return qMin(kMinimumExportRangeSeconds, qMax(0.0, chartDurationSeconds));
}

}  // namespace

QmlExportSession::QmlExportSession(MainWindow& backend,
                                   miacode::v2::UiRequestService& uiRequests,
                                   QObject* parent)
    : QObject(parent)
    , uiRequests_(&uiRequests)
    , backend_(&backend)
{
    connect(&backend, &MainWindow::videoExportWorkerRunningChanged, this, [this](bool running) {
        if (batchExportRunning_ || exportRunning_ == running) {
            return;
        }
        exportRunning_ = running;
        emit exportRunningChanged();
    });
}

QString QmlExportSession::activeTab() const
{
    return activeTab_;
}

QVariantList QmlExportSession::resolutionOptions() const
{
    QVariantList list;
    for (const auto& preset : kResolutionPresets) {
        QVariantMap row;
        row.insert(QStringLiteral("label"), QString::fromLatin1(preset.label));
        row.insert(QStringLiteral("width"), preset.width);
        row.insert(QStringLiteral("height"), preset.height);
        list.append(row);
    }
    return list;
}

QVariantList QmlExportSession::fpsOptions() const
{
    QVariantList list;
    for (int fps : kFpsOptions) {
        list.append(fps);
    }
    return list;
}

QVariantList QmlExportSession::audioBitrateOptions() const
{
    QVariantList list;
    for (int kbps : kAudioBitrateOptions) {
        list.append(kbps);
    }
    return list;
}

QVariantList QmlExportSession::presetOptions() const
{
    return QVariantList{
        QStringLiteral("快速"),
        QStringLiteral("高质量"),
    };
}

QVariantList QmlExportSession::sizePresetOptions() const
{
    return QVariantList{
        QStringLiteral("标准"),
        QStringLiteral("紧凑"),
        QStringLiteral("超紧凑 (含 PV)"),
        QStringLiteral("超紧凑"),
    };
}

QVariantList QmlExportSession::backgroundScaleModeOptions() const
{
    return QVariantList{
        QStringLiteral("裁剪填充"),
        QStringLiteral("完整适应"),
        QStringLiteral("方形适应"),
        QStringLiteral("内圈适应外圈填充"),
    };
}

int QmlExportSession::presetIndex() const
{
    return task_.preset == VideoExportPreset::Fast ? 0 : 1;
}

int QmlExportSession::sizePresetIndex() const
{
    switch (task_.sizePreset) {
    case VideoExportSizePreset::Compact:
        return 1;
    case VideoExportSizePreset::UltraCompactWithPv:
        return 2;
    case VideoExportSizePreset::UltraCompact:
        return 3;
    case VideoExportSizePreset::Standard:
    default:
        return 0;
    }
}

int QmlExportSession::backgroundScaleModeIndex() const
{
    switch (task_.backgroundScaleMode) {
    case PreviewBackgroundScaleMode::FitContain:
        return 1;
    case PreviewBackgroundScaleMode::SquareFitContain:
        return 2;
    case PreviewBackgroundScaleMode::InnerCircleFitOuterFill:
        return 3;
    case PreviewBackgroundScaleMode::FillCrop:
    default:
        return 0;
    }
}

int QmlExportSession::introBackgroundModeIndex() const
{
    return task_.intro.backgroundMode.compare(QStringLiteral("custom"), Qt::CaseInsensitive) == 0 ? 1 : 0;
}

int QmlExportSession::introModeIndex() const
{
    if (isAutoIntroBannerMode(task_.intro.mode)) {
        return 0;
    }
    if (task_.intro.mode.compare(QStringLiteral("Standard"), Qt::CaseInsensitive) == 0) {
        return 2;
    }
    return 1;
}

bool QmlExportSession::introLevelTextRender() const
{
    return task_.intro.lvRenderMode.compare(QStringLiteral("text"), Qt::CaseInsensitive) == 0;
}

QVariantList QmlExportSession::introSoundOptions() const
{
    QVariantList list;
    list.append(QVariantMap{
        {QStringLiteral("label"),
         UiText::text(QStringLiteral("dialog.render_settings.music.default_intro_sound"))},
        {QStringLiteral("fileName"), QString()},
    });
    const QString musicDirectory = miacode::preview_sfx::assetMusicDirectory();
    if (musicDirectory.isEmpty()) {
        return list;
    }
    const QFileInfoList entries = QDir(musicDirectory).entryInfoList(
        miacode::preview_sfx::supportedIntroSoundFileExtensions(),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries) {
        list.append(QVariantMap{
            {QStringLiteral("label"), entry.fileName()},
            {QStringLiteral("fileName"), entry.fileName()},
        });
    }
    return list;
}

int QmlExportSession::introSoundIndex() const
{
    const QString selected = miacode::preview_sfx::normalizeIntroSoundFileName(
        task_.introSoundFileName);
    const QVariantList options = introSoundOptions();
    for (int i = 0; i < options.size(); ++i) {
        if (options.at(i).toMap().value(QStringLiteral("fileName")).toString() == selected) {
            return i;
        }
    }
    return 0;
}

QVariantList QmlExportSession::fontLibraryOptions() const
{
    QVariantList list;
    const QVector<miacode::video_export::FontLibraryEntry> entries =
        miacode::video_export::fontLibraryEntries(
            true, UiText::text(QStringLiteral("card_font.default")));
    for (const miacode::video_export::FontLibraryEntry& entry : entries) {
        list.append(QVariantMap{
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("path"), entry.path},
            {QStringLiteral("family"), entry.family},
        });
    }
    return list;
}

QVariantList QmlExportSession::skinOptions() const
{
    QVariantList list;
    if (backend_ == nullptr) {
        return list;
    }
    for (const QString& name : backend_->availablePreviewSkinDirectoryNames()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), name},
            {QStringLiteral("label"), backend_->previewSkinDisplayName(name)},
        });
    }
    return list;
}

int QmlExportSession::skinIndex() const
{
    if (backend_ == nullptr) {
        return -1;
    }
    const QStringList names = backend_->availablePreviewSkinDirectoryNames();
    for (int i = 0; i < names.size(); ++i) {
        if (names.at(i).compare(backend_->previewSkinDirectoryName_, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return names.isEmpty() ? -1 : 0;
}

QVariantList QmlExportSession::skinJudgeEffectOptions() const
{
    return QVariantList{
        UiText::text(QStringLiteral("dialog.skin_settings.chart_effect.standard")),
        UiText::text(QStringLiteral("dialog.skin_settings.chart_effect.starry")),
    };
}

int QmlExportSession::skinJudgeEffectIndex() const
{
    return backend_ != nullptr && backend_->previewJudgeEffectStyle_ == PreviewJudgeEffectStyle::Starry
        ? 1
        : 0;
}

QVariantList QmlExportSession::outlineOptions() const
{
    return QVariantList{
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_line.point")),
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_line.line")),
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_line.area")),
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_line.area_labeled")),
    };
}

int QmlExportSession::outlineIndex() const
{
    if (backend_ == nullptr) {
        return 1;
    }
    switch (backend_->previewOutlineVariant_) {
    case PreviewOutlineVariant::Point:
        return 0;
    case PreviewOutlineVariant::JudgeArea:
        return 2;
    case PreviewOutlineVariant::JudgeAreaLabeled:
        return 3;
    case PreviewOutlineVariant::Line:
    default:
        return 1;
    }
}

QVariantList QmlExportSession::hudFontAreaOptions() const
{
    return QVariantList{
        QVariantMap{
            {QStringLiteral("label"), UiText::text(QStringLiteral("dialog.video_export.option.hud_font_area.chart_info"))},
            {QStringLiteral("sample"), QStringLiteral("Title / Artist / MASTER 13+ / Designer")},
        },
        QVariantMap{
            {QStringLiteral("label"), UiText::text(QStringLiteral("dialog.video_export.option.hud_font_area.timestamp"))},
            {QStringLiteral("sample"), QStringLiteral("12:34:567")},
        },
        QVariantMap{
            {QStringLiteral("label"), UiText::text(QStringLiteral("dialog.video_export.option.hud_font_area.object_stats"))},
            {QStringLiteral("sample"), QStringLiteral("DELUXE Rate: 101.0000%  TAP: 128/128")},
        },
        QVariantMap{
            {QStringLiteral("label"), UiText::text(QStringLiteral("dialog.video_export.option.hud_font_area.debug"))},
            {QStringLiteral("sample"), QStringLiteral("Present: 60.0 FPS  max=17ms")},
        },
    };
}

QString QmlExportSession::hudFontPath() const
{
    return miacode::preview::scene::previewHudCustomFontPath(
        static_cast<miacode::preview::scene::PreviewHudFontArea>(hudFontAreaIndex_));
}

QString QmlExportSession::hudFontSample() const
{
    const QVariantList areas = hudFontAreaOptions();
    return areas.at(qBound(0, hudFontAreaIndex_, static_cast<int>(areas.size()) - 1))
        .toMap().value(QStringLiteral("sample")).toString();
}

QString QmlExportSession::introSoundLabel() const
{
    return UiText::text(QStringLiteral("dialog.render_settings.music.intro_sound"));
}

QString QmlExportSession::introSoundVolumeLabel() const
{
    return UiText::text(QStringLiteral("dialog.render_settings.music.intro_sound_volume"));
}

QString QmlExportSession::introSoundImportLabel() const
{
    return UiText::text(QStringLiteral("dialog.render_settings.video.skin.import"));
}

double QmlExportSession::exportEndSeconds() const
{
    return task_.exportStartSeconds + qMax(0.0, task_.contentDurationSeconds);
}

double QmlExportSession::minimumExportRangeSeconds() const
{
    return minimumExportRangeSecondsForChart(chartDurationSeconds_);
}

IntroBannerSpec QmlExportSession::previewIntroSpec() const
{
    IntroBannerSpec spec = task_.intro;
    spec.enabled = task_.intro.enabled && task_.fullRangeExport;
    return spec;
}

QVariantList QmlExportSession::batchDifficultyChecks() const
{
    QVariantList list;
    for (int id = 1; id <= 7; ++id) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("name"), SimaiDocument::difficultyShortName(id));
        row.insert(QStringLiteral("checked"), batchSelectedDifficultyIds_.contains(id));
        list.append(row);
    }
    return list;
}

void QmlExportSession::setUnavailableReason(const QString& reason)
{
    if (unavailableReason_ == reason) {
        return;
    }
    unavailableReason_ = reason;
    emit unavailableReasonChanged();
}

bool QmlExportSession::difficultyExists(int difficultyId) const
{
    return backend_ != nullptr
        && SimaiDocument::isDifficultyId(difficultyId)
        && backend_->document_.difficulty(difficultyId) != nullptr;
}

bool QmlExportSession::difficultyHasChartBody(int difficultyId) const
{
    if (!difficultyExists(difficultyId)) {
        return false;
    }
    const SimaiDifficultyData* difficulty = backend_->document_.difficulty(difficultyId);
    return difficulty != nullptr && !difficulty->chart.trimmed().isEmpty();
}

int QmlExportSession::resolveDefaultDifficultyId(int previousActiveDifficultyId) const
{
    if (difficultyExists(previousActiveDifficultyId)) {
        return previousActiveDifficultyId;
    }
    if (difficultyExists(selectedDifficultyId_)) {
        return selectedDifficultyId_;
    }
    if (backend_ != nullptr && difficultyExists(backend_->projectLastOpenedDifficultyId_)) {
        return backend_->projectLastOpenedDifficultyId_;
    }
    if (backend_ != nullptr) {
        const QVector<int> ids = backend_->document_.difficultyIds();
        if (!ids.isEmpty()) {
            return ids.constFirst();
        }
    }
    return 0;
}

void QmlExportSession::enter(int previousActiveDifficultyId)
{
    if (!pageSessionActive_) {
        pageSessionActive_ = true;
        emit pageSessionActiveChanged();
    }
    selectDifficulty(resolveDefaultDifficultyId(previousActiveDifficultyId));
    refreshFromDocument();
    emit fontLibraryChanged();
    emit skinChanged();
    emit hudFontChanged();
}

void QmlExportSession::leave()
{
    // Idle no-op: must not tear down the v1 Widgets export audition/session.
    if (!pageSessionActive_) {
        return;
    }
    pageSessionActive_ = false;
    emit pageSessionActiveChanged();
    savePreferences();
    hasSeededTask_ = false;
    stopAudition();
    setUnavailableReason(QString());
}

void QmlExportSession::selectDifficulty(int difficultyId)
{
    const int next = difficultyExists(difficultyId) ? difficultyId : 0;
    if (selectedDifficultyId_ != next) {
        selectedDifficultyId_ = next;
        emit selectedDifficultyIdChanged();
    }
    if (!pageSessionActive_) {
        return;
    }
    seedFromDifficulty(selectedDifficultyId_);
    syncAudition();
}

void QmlExportSession::setActiveTab(const QString& tabId)
{
    const QString next = tabId == QLatin1String("batch")
        ? QStringLiteral("batch")
        : QStringLiteral("export");
    if (activeTab_ == next) {
        return;
    }
    activeTab_ = next;
    emit activeTabChanged();
    if (activeTab_ == QLatin1String("batch") && settingsTab_ == QLatin1String("range")) {
        setSettingsTab(QStringLiteral("output"));
    }
    if (pageSessionActive_) {
        syncAudition();
    }
}

void QmlExportSession::setSettingsTab(const QString& tabId)
{
    if (settingsTab_ == tabId) {
        return;
    }
    settingsTab_ = tabId;
    emit settingsTabChanged();
}

void QmlExportSession::rebuildDifficultyList()
{
    QVariantList next;
    if (backend_ != nullptr) {
        for (int id : backend_->document_.difficultyIds()) {
            QVariantMap row;
            row.insert(QStringLiteral("id"), id);
            row.insert(QStringLiteral("name"), SimaiDocument::difficultyShortName(id));
            next.append(row);
        }
    }
    if (difficulties_ != next) {
        difficulties_ = next;
        emit difficultiesChanged();
    }
}

void QmlExportSession::refreshFromDocument()
{
    if (!difficultyExists(selectedDifficultyId_)) {
        selectedDifficultyId_ = resolveDefaultDifficultyId(0);
        emit selectedDifficultyIdChanged();
    }
    rebuildDifficultyList();
    if (pageSessionActive_) {
        seedFromDifficulty(selectedDifficultyId_);
        syncAudition();
    }
    emit batchChanged();
}

void QmlExportSession::applyPreferences()
{
    const QJsonObject settings = miacode::video_export::loadDialogPreferences();
    // Keep the established first-run defaults shared with the Widgets dialog.
    task_.clockCountEnabled = false;
    task_.fixHudTextLayout = false;
    task_.intro.mode = QStringLiteral("auto");
    task_.intro.lvRenderMode = QStringLiteral("atlas");
    miacode::video_export::applyVideoExportPreferences(settings, &task_);
    miacode::preview_sfx::setSelectedIntroSoundVolume(task_.introSoundVolume);
    if (backend_ != nullptr
        && backend_->previewSfxRuntime_ != nullptr
        && backend_->previewSfxRuntime_->audioEngineInitialized()) {
        backend_->previewSfxRuntime_->applyLevels(backend_->previewAudioSettings_);
    }
    const int savedWidth = task_.outputWidth;
    const int savedHeight = task_.outputHeight;
    resolutionIndex_ = 1;
    for (int i = 0; i < static_cast<int>(std::size(kResolutionPresets)); ++i) {
        if (kResolutionPresets[i].width == savedWidth && kResolutionPresets[i].height == savedHeight) {
            resolutionIndex_ = i;
            break;
        }
    }
    task_.outputWidth = kResolutionPresets[resolutionIndex_].width;
    task_.outputHeight = kResolutionPresets[resolutionIndex_].height;
}

void QmlExportSession::savePreferences() const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    miacode::video_export::appendVideoExportPreferences(&settings, task_);
    miacode::video_export::saveDialogPreferences(settings);
}

void QmlExportSession::seedFromDifficulty(int difficultyId)
{
    if (backend_ == nullptr || backend_->exportSection_ == nullptr || !difficultyHasChartBody(difficultyId)) {
        setUnavailableReason(
            difficultyExists(difficultyId)
                ? UiText::text(QStringLiteral("export_page.the_selected_difficulty_has_no"))
                : UiText::text(QStringLiteral("export_page.no_difficulty_is_available_to")));
        return;
    }
    setUnavailableReason(QString());
    VideoExportTask seededTask = backend_->exportSection_->buildVideoExportSeedTaskPublic(difficultyId);
    if (hasSeededTask_) {
        miacode::video_export::copyVideoExportUserSettings(task_, &seededTask);
    }
    task_ = std::move(seededTask);
    chartDurationSeconds_ = qMax(0.0, task_.contentDurationSeconds);
    if (!hasSeededTask_) {
        applyPreferences();
        hasSeededTask_ = true;
    }
    task_.exportStartSeconds = 0.0;
    task_.contentDurationSeconds = chartDurationSeconds_;
    task_.fullRangeExport = true;
    if (batchSelectedDifficultyIds_.isEmpty()) {
        batchSelectedDifficultyIds_.append(difficultyId);
    }
    if (batchOutputDirectory_.isEmpty()) {
        QSettings settings(QStringLiteral("fanfaredash"), QStringLiteral("MiaCode"));
        batchOutputDirectory_ = settings.value(QStringLiteral("batch_video_export_dialog/last_output_directory")).toString();
    }
    emit outputChanged();
    emit videoChanged();
    emit gameplayChanged();
    emit introChanged();
    emit rangeChanged();
    emit batchChanged();
}

void QmlExportSession::syncAudition()
{
    if (backend_ == nullptr || backend_->exportSection_ == nullptr || !pageSessionActive_) {
        return;
    }
    if (!difficultyHasChartBody(selectedDifficultyId_)) {
        stopAudition();
        return;
    }
    backend_->exportSection_->startQmlExportAudition(selectedDifficultyId_, task_);
}

void QmlExportSession::applyLivePreviewSettings()
{
    if (backend_ == nullptr || backend_->exportSection_ == nullptr) {
        return;
    }
    VideoExportTask liveTask = task_;
    applyOwnerLiveFields(&liveTask);
    backend_->exportSection_->applySharedExportTaskSettings(liveTask);
    syncAudition();
}

void QmlExportSession::stopAudition()
{
    if (backend_ == nullptr || backend_->exportSection_ == nullptr) {
        return;
    }
    backend_->exportSection_->stopQmlExportAudition();
}

void QmlExportSession::applyOwnerLiveFields(VideoExportTask* task) const
{
    if (task == nullptr || backend_ == nullptr) {
        return;
    }
    task->outlineVariant = backend_->previewOutlineVariant_;
    task->slideEarlierSecondAndTextOnTop = backend_->previewSlideEarlierSecondAndTextOnTop_;
    task->tapJudgeTextDistance = backend_->previewTapJudgeTextDistance_;
    task->judgeEffectStyle = backend_->previewJudgeEffectStyle_;
    task->centerDisplayMode = backend_->previewCenterDisplayMode_;
    task->muriRenderOptions = backend_->muriRenderOptions_;
}

VideoExportTask QmlExportSession::buildRequestedTask() const
{
    VideoExportTask task = task_;
    task.outputWidth = kResolutionPresets[qBound(0, resolutionIndex_, static_cast<int>(std::size(kResolutionPresets)) - 1)].width;
    task.outputHeight = kResolutionPresets[qBound(0, resolutionIndex_, static_cast<int>(std::size(kResolutionPresets)) - 1)].height;
    task.fullRangeExport = miacode::video_export::isFullRangeVideoExport(task.exportStartSeconds);
    task.intro.enabled = task.intro.enabled && task.fullRangeExport;
    applyOwnerLiveFields(&task);
    return task;
}

void QmlExportSession::startExport()
{
    if (backend_ == nullptr || backend_->exportSection_ == nullptr) {
        return;
    }
    if (activeTab_ == QLatin1String("batch")) {
        savePreferences();
        batchCancellationRequested_ = false;
        batchExportRunning_ = true;
        exportRunning_ = true;
        emit exportRunningChanged();
        MainWindow::ExportSection::BatchExportResult result;
        MainWindow::ExportSection::BatchExportCallbacks callbacks;
        // Batch runs synchronously on the UI thread, so it reports onto the same
        // shell overlay every other job uses. Before this the callback pumped
        // events and threw the percentage away, leaving batch with no progress
        // at all.
        miacode::v2::JobProgressService* const jobProgress =
            backend_ != nullptr ? backend_->jobProgressService() : nullptr;
        const QString batchJobTitle = UiText::text(QStringLiteral("dialog.batch_export.title"));
        quint64 batchJobToken = 0;
        if (jobProgress != nullptr) {
            batchJobToken = jobProgress->begin(
                batchJobTitle,
                UiText::text(QStringLiteral("export.preparing_package")),
                /*cancellable=*/true);
        }
        callbacks.progressChanged = [jobProgress, batchJobToken](int percent, const QString& label) {
            if (jobProgress != nullptr && jobProgress->token() == batchJobToken) {
                jobProgress->report(percent, label);
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        };
        callbacks.cancellationRequested = [this, jobProgress, batchJobToken]() {
            const bool shellCancelled = jobProgress != nullptr
                && jobProgress->token() == batchJobToken
                && jobProgress->cancelRequested();
            return batchCancellationRequested_ || shellCancelled;
        };
        VideoExportTask batchTask = buildRequestedTask();
        // Batch is always full-range, so keep the user's intro preference even
        // if the single-export range currently starts after chart zero.
        batchTask.intro.enabled = task_.intro.enabled;
        QString error;
        const bool launched = backend_->exportSection_->launchQmlBatchExport(
            batchTask,
            chartDirectories_,
            batchSelectedDifficultyIds_,
            batchOutputDirectory_,
            &result,
            callbacks,
            &error);
        if (jobProgress != nullptr && jobProgress->token() == batchJobToken) {
            jobProgress->end();
        }
        batchExportRunning_ = false;
        exportRunning_ = false;
        emit exportRunningChanged();
        const QString batchTitle = UiText::text(QStringLiteral("dialog.batch_export.title"));
        if (!launched) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Error,
                batchTitle,
                error.isEmpty()
                    ? UiText::text(QStringLiteral("dialog.batch_export.error.export_failed"))
                    : error);
            return;
        }
        if (result.canceled) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Information,
                batchTitle,
                UiText::text(QStringLiteral("dialog.batch_export.message.canceled")));
            return;
        }
        const auto shortenDetails = [](QString details) {
            return details.size() > 3000 ? details.left(3000) + QStringLiteral("\n...") : details;
        };
        const QString successDetails = shortenDetails(result.exportedFiles.join(QLatin1Char('\n')));
        if (result.failedCharts.isEmpty()) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Information,
                batchTitle,
                UiText::text(QStringLiteral("dialog.batch_export.message.success")).arg(result.successCount),
                successDetails);
            return;
        }
        uiRequests_->postNotice(
            miacode::v2::NoticeSeverity::Warning,
            batchTitle,
            UiText::text(QStringLiteral("dialog.batch_export.message.partial_failed"))
                .arg(result.successCount).arg(result.failedCharts.size()),
            (successDetails.isEmpty() ? QString()
                : UiText::text(QStringLiteral("dialog.batch_export.message.output_files"))
                    + QStringLiteral("\n") + successDetails + QStringLiteral("\n\n"))
                + shortenDetails(result.failedCharts.join(QLatin1Char('\n'))));
        return;
    }

    if (!difficultyHasChartBody(selectedDifficultyId_)) {
        return;
    }
    savePreferences();
    QString error;
    exportRunning_ = true;
    emit exportRunningChanged();
    if (!backend_->exportSection_->launchQmlVideoExport(
            buildRequestedTask(), selectedDifficultyId_, &error)) {
        exportRunning_ = false;
        emit exportRunningChanged();
        uiRequests_->postNotice(
            miacode::v2::NoticeSeverity::Error,
            UiText::text(QStringLiteral("dialog.video_export.title")),
            error.isEmpty()
                ? UiText::text(QStringLiteral("dialog.video_export.error.launch_failed"))
                : error);
        return;
    }
}

void QmlExportSession::cancelExport()
{
    if (backend_ == nullptr || backend_->exportSection_ == nullptr) {
        return;
    }
    if (batchExportRunning_) {
        batchCancellationRequested_ = true;
        return;
    }
    backend_->exportSection_->cancelVideoExportWorker();
}

void QmlExportSession::browseOutputPath()
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("dialog.video_export.title"));
    request.startPath = task_.outputPath;
    request.nameFilters = QStringList{QStringLiteral("MP4 (*.mp4)")};
    request.saveMode = true;
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (!path.isEmpty()) {
            setOutputPath(path);
        }
    });
}

void QmlExportSession::browseIntroBackground()
{
    miacode::v2::FileRequest request;
    request.title = QStringLiteral("选择片头背景");
    request.startPath = task_.intro.customBackgroundPath;
    request.nameFilters = QStringList{QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (!path.isEmpty()) {
            setIntroCustomBackgroundPath(path);
        }
    });
}

void QmlExportSession::importIntroSound()
{
    miacode::v2::FileRequest request;
    request.title = introSoundLabel();
    request.nameFilters = QStringList{QStringLiteral("Audio (*.wav *.mp3 *.ogg *.flac)")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        applyIntroSoundImport(path);
    });
}

void QmlExportSession::importIntroFont()
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("card_font.import"));
    request.nameFilters = QStringList{QStringLiteral("Font Files (*.ttf *.otf)")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        applyFontImport(path);
    });
}

void QmlExportSession::applyFontImport(const QString& selectedPath)
{
    if (selectedPath.isEmpty()) {
        return;
    }
    const miacode::video_export::FontImportResult result =
        miacode::video_export::importFontFileIntoLibrary(selectedPath);
    if (result.path.isEmpty()) {
        const QString text = result.failure == miacode::video_export::FontImportFailure::CopyFailed
            ? UiText::text(QStringLiteral("card_font.copy_failed"))
            : UiText::text(QStringLiteral("card_font.invalid_font"));
        uiRequests_->postNotice(miacode::v2::NoticeSeverity::Warning,
                                UiText::text(QStringLiteral("card_font.import")), text);
        return;
    }

    emit fontLibraryChanged();
    // Match the established card-font picker: an imported font becomes the
    // title/display choice while the body selection remains independent.
    setIntroFontDisplayPath(result.path);
}

void QmlExportSession::importHudFont()
{
    if (uiRequests_ == nullptr) {
        return;
    }
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("dialog.video_export.option.import_hud_font"));
    request.nameFilters = QStringList{QStringLiteral("Font Files (*.ttf *.otf)")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        applyHudFontImport(path);
    });
}

void QmlExportSession::applyHudFontImport(const QString& selectedPath)
{
    if (selectedPath.isEmpty()) {
        return;
    }
    const miacode::video_export::FontImportResult result =
        miacode::video_export::importFontFileIntoLibrary(selectedPath);
    if (result.path.isEmpty()) {
        if (uiRequests_ != nullptr) {
            uiRequests_->postNotice(
                miacode::v2::NoticeSeverity::Warning,
                UiText::text(QStringLiteral("dialog.video_export.option.import_hud_font")),
                result.failure == miacode::video_export::FontImportFailure::CopyFailed
                    ? UiText::text(QStringLiteral("card_font.copy_failed"))
                    : UiText::text(QStringLiteral("card_font.invalid_font")));
        }
        return;
    }
    emit fontLibraryChanged();
    setHudFontPath(result.path);
}

void QmlExportSession::resetHudFont()
{
    setHudFontPath(QString());
}

void QmlExportSession::openSkinDirectory()
{
    if (backend_ == nullptr) {
        return;
    }
    const QString skinRoot = backend_->resolvePreviewSkinRootDir();
    if (!skinRoot.isEmpty()) {
        QDir().mkpath(skinRoot);
        QDesktopServices::openUrl(QUrl::fromLocalFile(skinRoot));
    }
}

void QmlExportSession::openJudgeLineDirectory()
{
    if (backend_ == nullptr) {
        return;
    }
    const QString outlineDir = backend_->resolvePreviewCustomOutlineDir();
    if (!outlineDir.isEmpty()) {
        QDir().mkpath(outlineDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(outlineDir));
    }
}

void QmlExportSession::applyIntroSoundImport(const QString& selectedPath)
{
    if (selectedPath.isEmpty()) {
        return;
    }

    const QString musicDirectory = miacode::preview_sfx::assetMusicDirectory();
    if (musicDirectory.isEmpty() || !QDir().mkpath(musicDirectory)) {
        return;
    }

    const QFileInfo sourceInfo(selectedPath);
    QString importedName = sourceInfo.fileName();
    QString importedPath = QDir(musicDirectory).filePath(importedName);
    if (QFileInfo(selectedPath).canonicalFilePath() != QFileInfo(importedPath).canonicalFilePath()) {
        int suffix = 2;
        while (QFileInfo::exists(importedPath)) {
            importedName = QStringLiteral("%1_%2.%3")
                .arg(sourceInfo.completeBaseName())
                .arg(suffix++)
                .arg(sourceInfo.suffix());
            importedPath = QDir(musicDirectory).filePath(importedName);
        }
        if (!QFile::copy(selectedPath, importedPath)) {
            return;
        }
    }

    emit introSoundOptionsChanged();
    setIntroSoundFileName(importedName);
}

void QmlExportSession::browseBatchOutputDirectory()
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("dialog.batch_export.select_folder"));
    request.startPath = batchOutputDirectory_;
    request.selectFolder = true;
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (!path.isEmpty()) {
            setBatchOutputDirectory(path);
        }
    });
}

void QmlExportSession::addChartDirectories()
{
    miacode::v2::FileRequest request;
    request.title = UiText::text(QStringLiteral("dialog.batch_export.select_charts"));
    request.selectFolder = true;
    uiRequests_->requestFile(request, [this](const QString& path) {
        addChartDirectory(path);
    });
}

void QmlExportSession::addChartDirectory(const QString& path)
{
    if (path.isEmpty() || chartDirectories_.contains(path)) {
        return;
    }
    chartDirectories_.append(path);
    emit batchChanged();
}

void QmlExportSession::removeChartDirectory(int index)
{
    if (index < 0 || index >= chartDirectories_.size()) {
        return;
    }
    chartDirectories_.removeAt(index);
    emit batchChanged();
}

void QmlExportSession::clearChartDirectories()
{
    if (chartDirectories_.isEmpty()) {
        return;
    }
    chartDirectories_.clear();
    emit batchChanged();
}

void QmlExportSession::setBatchDifficultyChecked(int difficultyId, bool checked)
{
    if (!SimaiDocument::isDifficultyId(difficultyId)) {
        return;
    }
    if (checked) {
        if (!batchSelectedDifficultyIds_.contains(difficultyId)) {
            batchSelectedDifficultyIds_.append(difficultyId);
            emit batchChanged();
        }
    } else {
        if (batchSelectedDifficultyIds_.removeAll(difficultyId) > 0) {
            emit batchChanged();
        }
    }
}

void QmlExportSession::setExportStartToCurrentPreview()
{
    if (backend_ == nullptr) {
        return;
    }
    setExportStartSeconds(backend_->currentPreviewAuthoritativeAudioClockSecond());
}

void QmlExportSession::setExportEndToCurrentPreview()
{
    if (backend_ == nullptr) {
        return;
    }
    setExportEndSeconds(backend_->currentPreviewAuthoritativeAudioClockSecond());
}

void QmlExportSession::setExportRangeSeconds(double start, double end)
{
    if (!qIsFinite(start) || !qIsFinite(end)) {
        return;
    }
    const double minimumDuration = minimumExportRangeSeconds();
    const double boundedStart = qBound(0.0, start, qMax(0.0, chartDurationSeconds_ - minimumDuration));
    const double boundedEnd = qBound(boundedStart + minimumDuration, end, chartDurationSeconds_);
    task_.exportStartSeconds = boundedStart;
    task_.contentDurationSeconds = qMax(0.0, boundedEnd - boundedStart);
    task_.fullRangeExport = miacode::video_export::isFullRangeVideoExport(task_.exportStartSeconds);
    emit rangeChanged();
    emit introChanged();
}

QString QmlExportSession::setExportStartText(const QString& text)
{
    double seconds = 0.0;
    const QString normalized = miacode::video_export::sanitizeVideoExportTimestamp(text);
    bool parsed = false;
    if (normalized.contains(QLatin1Char(':'))) {
        parsed = miacode::video_export::parseVideoExportTimestamp(normalized, &seconds);
    } else {
        seconds = normalized.toDouble(&parsed);
    }
    if (parsed) {
        setExportStartSeconds(seconds);
    }
    return QString::number(task_.exportStartSeconds, 'f', 3);
}

QString QmlExportSession::setExportEndText(const QString& text)
{
    double seconds = 0.0;
    const QString normalized = miacode::video_export::sanitizeVideoExportTimestamp(text);
    bool parsed = false;
    if (normalized.contains(QLatin1Char(':'))) {
        parsed = miacode::video_export::parseVideoExportTimestamp(normalized, &seconds);
    } else {
        seconds = normalized.toDouble(&parsed);
    }
    if (parsed) {
        setExportEndSeconds(seconds);
    }
    return QString::number(exportEndSeconds(), 'f', 3);
}

void QmlExportSession::setOutputPath(const QString& path)
{
    if (task_.outputPath == path) {
        return;
    }
    task_.outputPath = path;
    emit outputChanged();
}

void QmlExportSession::setResolutionIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(std::size(kResolutionPresets)) || resolutionIndex_ == index) {
        return;
    }
    resolutionIndex_ = index;
    task_.outputWidth = kResolutionPresets[index].width;
    task_.outputHeight = kResolutionPresets[index].height;
    emit outputChanged();
    savePreferences();
    if (pageSessionActive_) {
        syncAudition();
    }
}

void QmlExportSession::setFps(int fps)
{
    if (task_.fps == fps) {
        return;
    }
    task_.fps = fps;
    emit outputChanged();
    savePreferences();
}

void QmlExportSession::setAudioBitrateKbps(int kbps)
{
    if (task_.audioBitrateKbps == kbps) {
        return;
    }
    task_.audioBitrateKbps = kbps;
    emit outputChanged();
    savePreferences();
}

void QmlExportSession::setPresetIndex(int index)
{
    const VideoExportPreset next = index == 0 ? VideoExportPreset::Fast : VideoExportPreset::HighQuality;
    if (task_.preset == next) {
        return;
    }
    task_.preset = next;
    emit outputChanged();
    savePreferences();
}

void QmlExportSession::setSizePresetIndex(int index)
{
    VideoExportSizePreset next = VideoExportSizePreset::Standard;
    switch (index) {
    case 1:
        next = VideoExportSizePreset::Compact;
        break;
    case 2:
        next = VideoExportSizePreset::UltraCompactWithPv;
        break;
    case 3:
        next = VideoExportSizePreset::UltraCompact;
        break;
    default:
        break;
    }
    if (task_.sizePreset == next) {
        return;
    }
    task_.sizePreset = next;
    emit outputChanged();
    savePreferences();
}

void QmlExportSession::setBackgroundBrightnessOuter(double value)
{
    task_.backgroundBrightnessOuter = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setBackgroundBrightnessInner(double value)
{
    task_.backgroundBrightnessInner = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setLayoutSquareScale(double value)
{
    task_.layoutSquareScale = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setBackgroundScaleModeIndex(int index)
{
    PreviewBackgroundScaleMode next = PreviewBackgroundScaleMode::FillCrop;
    switch (index) {
    case 1:
        next = PreviewBackgroundScaleMode::FitContain;
        break;
    case 2:
        next = PreviewBackgroundScaleMode::SquareFitContain;
        break;
    case 3:
        next = PreviewBackgroundScaleMode::InnerCircleFitOuterFill;
        break;
    default:
        break;
    }
    task_.backgroundScaleMode = next;
    emit videoChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setSmoothBrightness(bool value)
{
    task_.smoothBrightness = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setShowTimestamp(bool value)
{
    task_.showTimestamp = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setShowObjectStatsHud(bool value)
{
    task_.showObjectStatsHud = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setShowChartInfoHud(bool value)
{
    task_.showChartInfoHud = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setFixHudTextLayout(bool value)
{
    task_.fixHudTextLayout = value;
    emit videoChanged();
    syncAudition();
    savePreferences();
}

void QmlExportSession::setClockCountEnabled(bool value)
{
    task_.clockCountEnabled = value;
    emit videoChanged();
    syncAudition();
    savePreferences();
}

void QmlExportSession::setTapFlowSpeed(double value)
{
    if (!qIsFinite(value)) {
        return;
    }
    task_.tapFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(value);
    emit gameplayChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setTouchFlowSpeed(double value)
{
    if (!qIsFinite(value)) {
        return;
    }
    task_.touchFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(value);
    emit gameplayChanged();
    applyLivePreviewSettings();
}

void QmlExportSession::setSkinIndex(int index)
{
    if (backend_ == nullptr) {
        return;
    }
    const QStringList names = backend_->availablePreviewSkinDirectoryNames();
    if (index < 0 || index >= names.size()) {
        return;
    }
    const QString skinDirectoryName = names.at(index);
    if (backend_->previewSkinDirectoryName_.compare(skinDirectoryName, Qt::CaseInsensitive) == 0) {
        return;
    }
    backend_->previewSkinDirectoryName_ = skinDirectoryName;
    backend_->previewSkinVariant_ =
        skinDirectoryName.compare(QStringLiteral("skinDX"), Qt::CaseInsensitive) == 0
            ? MainWindow::PreviewSkinVariant::Dx
            : MainWindow::PreviewSkinVariant::Standard;
    backend_->applyPreviewSkinDirectoryToSurfaces();
    backend_->savePortableState();
    emit skinChanged();
}

void QmlExportSession::setSkinJudgeEffectIndex(int index)
{
    if (backend_ == nullptr) {
        return;
    }
    const auto style = index == 1 ? PreviewJudgeEffectStyle::Starry : PreviewJudgeEffectStyle::Standard;
    if (backend_->previewJudgeEffectStyle_ == style) {
        return;
    }
    backend_->previewJudgeEffectStyle_ = style;
    if (backend_->previewCanvas_ != nullptr) {
        backend_->previewCanvas_->setJudgeEffectStyle(style);
    }
    backend_->savePortableState();
    emit skinChanged();
}

void QmlExportSession::setOutlineIndex(int index)
{
    if (backend_ == nullptr) {
        return;
    }
    PreviewOutlineVariant variant = PreviewOutlineVariant::Line;
    switch (index) {
    case 0:
        variant = PreviewOutlineVariant::Point;
        break;
    case 2:
        variant = PreviewOutlineVariant::JudgeArea;
        break;
    case 3:
        variant = PreviewOutlineVariant::JudgeAreaLabeled;
        break;
    case 1:
    default:
        break;
    }
    backend_->applyPreviewOutlineVariant(variant, /*useAutoSelection=*/false, /*persistState=*/true);
    emit skinChanged();
}

void QmlExportSession::setHudFontAreaIndex(int index)
{
    const int normalized = qBound(0, index, 3);
    if (hudFontAreaIndex_ == normalized) {
        return;
    }
    hudFontAreaIndex_ = normalized;
    emit hudFontChanged();
}

void QmlExportSession::setHudFontPath(const QString& path)
{
    const auto area = static_cast<miacode::preview::scene::PreviewHudFontArea>(hudFontAreaIndex_);
    if (miacode::preview::scene::previewHudCustomFontPath(area) == path) {
        return;
    }
    miacode::preview::scene::setPreviewHudCustomFontPath(area, path);
    if (backend_ != nullptr && backend_->previewCanvas_ != nullptr) {
        backend_->previewCanvas_->update();
    }
    emit hudFontChanged();
}

void QmlExportSession::setIntroEnabled(bool value)
{
    task_.intro.enabled = value;
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroBackgroundModeIndex(int index)
{
    task_.intro.backgroundMode = index == 1 ? QStringLiteral("custom") : QStringLiteral("jacket");
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroCustomBackgroundPath(const QString& path)
{
    task_.intro.customBackgroundPath = path;
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroBlurBackground(bool value)
{
    task_.intro.blurBackground = value;
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroModeIndex(int index)
{
    if (index == 0) {
        task_.intro.mode = QStringLiteral("auto");
    } else if (index == 2) {
        task_.intro.mode = QStringLiteral("Standard");
    } else {
        task_.intro.mode = QStringLiteral("DX");
    }
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroCardShadow(bool value)
{
    task_.intro.cardShadow = value;
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroLevelTextRender(bool value)
{
    task_.intro.lvRenderMode = value ? QStringLiteral("text") : QStringLiteral("atlas");
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroFontDisplayPath(const QString& path)
{
    if (task_.intro.fontDisplayPath == path) {
        return;
    }
    task_.intro.fontDisplayPath = path;
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroFontBodyPath(const QString& path)
{
    if (task_.intro.fontBodyPath == path) {
        return;
    }
    task_.intro.fontBodyPath = path;
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::resetIntroFonts()
{
    const bool changed = !task_.intro.fontDisplayPath.isEmpty() || !task_.intro.fontBodyPath.isEmpty();
    task_.intro.fontDisplayPath.clear();
    task_.intro.fontBodyPath.clear();
    if (!changed) {
        return;
    }
    emit introChanged();
    savePreferences();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroSoundIndex(int index)
{
    const QVariantList options = introSoundOptions();
    if (index < 0 || index >= options.size()) {
        return;
    }
    setIntroSoundFileName(
        options.at(index).toMap().value(QStringLiteral("fileName")).toString());
}

void QmlExportSession::setIntroSoundFileName(const QString& fileName)
{
    const QString normalized = miacode::preview_sfx::normalizeIntroSoundFileName(fileName);
    if (task_.introSoundFileName == normalized) {
        return;
    }
    task_.introSoundFileName = normalized;
    miacode::preview_sfx::setSelectedIntroSoundFileName(normalized);
    if (backend_ != nullptr) {
        backend_->previewIntroSoundFileName_ = normalized;
        if (backend_->previewSfxRuntime_ != nullptr
            && backend_->previewSfxRuntime_->audioEngineInitialized()) {
            backend_->previewSfxRuntime_->reloadAssets(backend_->previewAudioSettings_);
        }
        backend_->savePortableState();
    }
    emit introChanged();
}

void QmlExportSession::setIntroSoundVolume(double value)
{
    if (!qIsFinite(value)) {
        return;
    }
    const double normalized = qBound(0.0, value, 2.0);
    if (qFuzzyCompare(task_.introSoundVolume + 1.0, normalized + 1.0)) {
        return;
    }
    task_.introSoundVolume = normalized;
    miacode::preview_sfx::setSelectedIntroSoundVolume(normalized);
    if (backend_ != nullptr
        && backend_->previewSfxRuntime_ != nullptr
        && backend_->previewSfxRuntime_->audioEngineInitialized()) {
        backend_->previewSfxRuntime_->applyLevels(backend_->previewAudioSettings_);
    }
    emit introChanged();
    savePreferences();
}

void QmlExportSession::setExportStartSeconds(double value)
{
    if (!qIsFinite(value)) {
        return;
    }
    const double end = exportEndSeconds();
    const double maximumStart = qMax(0.0, end - minimumExportRangeSeconds());
    setExportRangeSeconds(qBound(0.0, value, maximumStart), end);
}

void QmlExportSession::setExportEndSeconds(double value)
{
    if (!qIsFinite(value)) {
        return;
    }
    const double start = task_.exportStartSeconds;
    const double minimumEnd = start + minimumExportRangeSeconds();
    setExportRangeSeconds(start, qBound(minimumEnd, value, chartDurationSeconds_));
}

void QmlExportSession::setBatchOutputDirectory(const QString& path)
{
    if (batchOutputDirectory_ == path) {
        return;
    }
    batchOutputDirectory_ = path;
    QSettings settings(QStringLiteral("fanfaredash"), QStringLiteral("MiaCode"));
    settings.setValue(QStringLiteral("batch_video_export_dialog/last_output_directory"), path);
    emit batchChanged();
}
