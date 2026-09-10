#include "export/ExportSession.h"

#include "core/chart/document/SimaiDocument.h"

#include "app/services/JobProgressService.h"
#include "ui/preferences/LocaleService.h"
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


namespace miacode::ui {
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

ExportSession::ExportSession(miacode::ShellNotifications& notifications,
                                   miacode::UiRequestService& uiRequests,
                                   miacode::JobProgressService& jobProgress,
                                   miacode::PreviewAppearanceState& appearance,
                                   miacode::ExportEngine*& engineSlot,
                                   miacode::PreviewSurface*& previewSlot,
                                   QObject* parent)
    : QObject(parent)
    , uiRequests_(&uiRequests)
    // From the application assembly, not from the hidden window.
    , jobProgress_(&jobProgress)
    , appearance_(&appearance)
    , engineSlot_(&engineSlot)
    , previewSlot_(&previewSlot)
    , notifications_(&notifications)
{
    connect(&miacode::LocaleService::instance(), &miacode::LocaleService::languageChanged,
            this, [this](const QString&) {
                fontLibraryOptionsCacheValid_ = false;
                emit localeLabelsChanged();
                emit fontLibraryChanged();
                emit introSoundOptionsChanged();
                emit skinChanged();
                emit hudFontChanged();
                emit introChanged();
            });
    connect(&notifications, &miacode::ShellNotifications::videoExportWorkerRunningChanged, this, [this](bool running) {
        if (batchExportRunning_ || exportRunning_ == running) {
            return;
        }
        exportRunning_ = running;
        emit exportRunningChanged();
    });
}

QString ExportSession::activeTab() const
{
    return activeTab_;
}

QVariantList ExportSession::resolutionOptions() const
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

QVariantList ExportSession::fpsOptions() const
{
    QVariantList list;
    for (int fps : kFpsOptions) {
        list.append(fps);
    }
    return list;
}

QVariantList ExportSession::audioBitrateOptions() const
{
    QVariantList list;
    for (int kbps : kAudioBitrateOptions) {
        list.append(kbps);
    }
    return list;
}

QVariantList ExportSession::presetOptions() const
{
    return QVariantList{
        qtTrId("dialog.video_export.preset.fast"),
        qtTrId("dialog.video_export.preset.high_quality"),
    };
}

QVariantList ExportSession::sizePresetOptions() const
{
    return QVariantList{
        qtTrId("dialog.video_export.size_preset.standard"),
        qtTrId("dialog.video_export.size_preset.compact"),
        qtTrId("dialog.video_export.size_preset.ultra_compact_with_pv"),
        qtTrId("dialog.video_export.size_preset.ultra_compact"),
    };
}

QVariantList ExportSession::backgroundScaleModeOptions() const
{
    return QVariantList{
        qtTrId("dialog.video_export.option.scale.fill"),
        qtTrId("dialog.video_export.option.scale.fit"),
        qtTrId("dialog.video_export.option.scale.square_fit"),
        qtTrId("dialog.video_export.option.scale.inner_circle_fit_outer_fill"),
    };
}

int ExportSession::presetIndex() const
{
    return task_.preset == VideoExportPreset::Fast ? 0 : 1;
}

int ExportSession::sizePresetIndex() const
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

int ExportSession::backgroundScaleModeIndex() const
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

int ExportSession::introBackgroundModeIndex() const
{
    return task_.intro.backgroundMode.compare(QStringLiteral("custom"), Qt::CaseInsensitive) == 0 ? 1 : 0;
}

int ExportSession::introModeIndex() const
{
    if (isAutoIntroBannerMode(task_.intro.mode)) {
        return 0;
    }
    if (task_.intro.mode.compare(QStringLiteral("Standard"), Qt::CaseInsensitive) == 0) {
        return 2;
    }
    return 1;
}

bool ExportSession::introLevelTextRender() const
{
    return task_.intro.lvRenderMode.compare(QStringLiteral("text"), Qt::CaseInsensitive) == 0;
}

QVariantList ExportSession::introSoundOptions() const
{
    QVariantList list;
    list.append(QVariantMap{
        {QStringLiteral("label"),
         qtTrId("dialog.render_settings.music.default_intro_sound")},
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

int ExportSession::introSoundIndex() const
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

QVariantList ExportSession::fontLibraryOptions() const
{
    const QString defaultLabel = qtTrId("card_font.default");
    if (fontLibraryOptionsCacheValid_
        && fontLibraryOptionsCacheDefaultLabel_ == defaultLabel) {
        return fontLibraryOptionsCache_;
    }
    QVariantList list;
    const QVector<miacode::video_export::FontLibraryEntry> entries =
        miacode::video_export::fontLibraryEntries(true, defaultLabel);
    for (const miacode::video_export::FontLibraryEntry& entry : entries) {
        list.append(QVariantMap{
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("path"), entry.path},
            {QStringLiteral("family"), entry.family},
        });
    }
    fontLibraryOptionsCache_ = list;
    fontLibraryOptionsCacheDefaultLabel_ = defaultLabel;
    fontLibraryOptionsCacheValid_ = true;
    return fontLibraryOptionsCache_;
}

QVariantList ExportSession::skinOptions() const
{
    QVariantList list;
    if (preview() == nullptr) {
        return list;
    }
    for (const QString& name : preview()->availableSkinDirectoryNames()) {
        list.append(QVariantMap{
            {QStringLiteral("id"), name},
            {QStringLiteral("label"), preview()->skinDisplayName(name)},
        });
    }
    return list;
}

int ExportSession::skinIndex() const
{
    if (preview() == nullptr) {
        return -1;
    }
    const QStringList names = preview()->availableSkinDirectoryNames();
    for (int i = 0; i < names.size(); ++i) {
        if (names.at(i).compare(appearance_->skinDirectoryName(), Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return names.isEmpty() ? -1 : 0;
}

QVariantList ExportSession::skinJudgeEffectOptions() const
{
    return QVariantList{
        qtTrId("dialog.skin_settings.chart_effect.standard"),
        qtTrId("dialog.skin_settings.chart_effect.starry"),
    };
}

int ExportSession::skinJudgeEffectIndex() const
{
    return appearance_->judgeEffectStyle() == PreviewJudgeEffectStyle::Starry ? 1 : 0;
}

QVariantList ExportSession::outlineOptions() const
{
    return QVariantList{
        qtTrId("dialog.render_settings.gameplay.judge_line.point"),
        qtTrId("dialog.render_settings.gameplay.judge_line.line"),
        qtTrId("dialog.render_settings.gameplay.judge_line.area"),
        qtTrId("dialog.render_settings.gameplay.judge_line.area_labeled"),
    };
}

int ExportSession::outlineIndex() const
{
    if (preview() == nullptr) {
        return 1;
    }
    switch (appearance_->outlineVariant()) {
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

QVariantList ExportSession::hudFontAreaOptions() const
{
    QVariantList result;
    for (const auto& choice : miacode::preview::scene::previewHudFontAreaChoices()) {
        result.append(QVariantMap{
            {QStringLiteral("label"), qtTrId(choice.labelKey)},
            {QStringLiteral("sample"), QLatin1String(choice.sample)},
            {QStringLiteral("areaId"), miacode::preview::scene::previewHudFontAreaId(choice.area)},
        });
    }
    return result;
}

int ExportSession::hudFontAreaIndex() const
{
    return miacode::preview::scene::previewHudFontAreaIndex(
        miacode::preview::scene::previewHudFontAreaFromId(hudFontAreaId_));
}

QString ExportSession::hudFontPath() const
{
    return miacode::preview::scene::previewHudCustomFontPath(
        miacode::preview::scene::previewHudFontAreaFromId(hudFontAreaId_));
}

QString ExportSession::hudFontSample() const
{
    const QVariantList areas = hudFontAreaOptions();
    return areas.at(qBound(0, hudFontAreaIndex(), static_cast<int>(areas.size()) - 1))
        .toMap().value(QStringLiteral("sample")).toString();
}

QString ExportSession::introSoundLabel() const
{
    return qtTrId("dialog.render_settings.music.intro_sound");
}

QString ExportSession::introSoundVolumeLabel() const
{
    return qtTrId("dialog.render_settings.music.intro_sound_volume");
}

QString ExportSession::introSoundImportLabel() const
{
    return qtTrId("dialog.render_settings.video.skin.import");
}

double ExportSession::exportEndSeconds() const
{
    return task_.exportStartSeconds + qMax(0.0, task_.contentDurationSeconds);
}

double ExportSession::minimumExportRangeSeconds() const
{
    return minimumExportRangeSecondsForChart(chartDurationSeconds_);
}

IntroBannerSpec ExportSession::previewIntroSpec() const
{
    IntroBannerSpec spec = task_.intro;
    spec.enabled = task_.intro.enabled && task_.fullRangeExport;
    return spec;
}

QVariantList ExportSession::batchDifficultyChecks() const
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

void ExportSession::setUnavailableReason(const QString& reason)
{
    if (unavailableReason_ == reason) {
        return;
    }
    unavailableReason_ = reason;
    emit unavailableReasonChanged();
}

bool ExportSession::difficultyExists(int difficultyId) const
{
    // documentDifficultyIds() lists exactly the difficulties the document
    // holds, so membership is the same question as difficulty(id) != nullptr.
    return engine() != nullptr
        && SimaiDocument::isDifficultyId(difficultyId)
        && engine()->difficultyIds().contains(difficultyId);
}

bool ExportSession::difficultyHasChartBody(int difficultyId) const
{
    if (!difficultyExists(difficultyId)) {
        return false;
    }
    return !engine()->difficultyChartText(difficultyId).trimmed().isEmpty();
}

int ExportSession::resolveDefaultDifficultyId(int previousActiveDifficultyId) const
{
    if (difficultyExists(previousActiveDifficultyId)) {
        return previousActiveDifficultyId;
    }
    if (difficultyExists(selectedDifficultyId_)) {
        return selectedDifficultyId_;
    }
    if (engine() != nullptr && difficultyExists(engine()->lastOpenedDifficultyId())) {
        return engine()->lastOpenedDifficultyId();
    }
    if (engine() != nullptr) {
        const QVector<int> ids = engine()->difficultyIds();
        if (!ids.isEmpty()) {
            return ids.constFirst();
        }
    }
    return 0;
}

void ExportSession::enter(int previousActiveDifficultyId)
{
    if (!pageSessionActive_) {
        pageSessionActive_ = true;
        emit pageSessionActiveChanged();
    }
    const int nextDifficultyId = resolveDefaultDifficultyId(previousActiveDifficultyId);
    if (selectedDifficultyId_ != nextDifficultyId) {
        selectedDifficultyId_ = nextDifficultyId;
        emit selectedDifficultyIdChanged();
    }
    rebuildDifficultyList();
    seedFromDifficulty(selectedDifficultyId_);
    syncAudition();
    // Re-scan once per real page entry so imports made by another QML surface
    // are visible, while repeated property reads during this entry share the
    // materialized option list.
    fontLibraryOptionsCacheValid_ = false;
    emit fontLibraryChanged();
    emit skinChanged();
    emit hudFontChanged();
}

void ExportSession::leave()
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

void ExportSession::selectDifficulty(int difficultyId)
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

void ExportSession::setActiveTab(const QString& tabId)
{
    const QString next = tabId == QLatin1String("batch")
        ? QStringLiteral("batch")
        : QStringLiteral("export");
    if (activeTab_ == next) {
        return;
    }
    activeTab_ = next;
    emit activeTabChanged();
    if (pageSessionActive_) {
        syncAudition();
    }
}

void ExportSession::setSettingsTab(const QString& tabId)
{
    const QString next = tabId == QLatin1String("range")
        ? QStringLiteral("output")
        : tabId;
    if (settingsTab_ == next) {
        return;
    }
    settingsTab_ = next;
    emit settingsTabChanged();
}

void ExportSession::rebuildDifficultyList()
{
    QVariantList next;
    if (engine() != nullptr) {
        for (int id : engine()->difficultyIds()) {
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

void ExportSession::refreshFromDocument()
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

void ExportSession::applyPreferences()
{
    const QJsonObject settings = miacode::video_export::loadDialogPreferences();
    // Keep the established first-run defaults shared with the Widgets dialog.
    task_.clockCountEnabled = false;
    task_.fixHudTextLayout = false;
    task_.intro.mode = QStringLiteral("auto");
    task_.intro.lvRenderMode = QStringLiteral("atlas");
    miacode::video_export::applyVideoExportPreferences(settings, &task_);
    miacode::preview_sfx::setSelectedIntroSoundVolume(task_.introSoundVolume);
    if (preview() != nullptr) {
        preview()->applySfxLevels();
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

void ExportSession::savePreferences() const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    miacode::video_export::appendVideoExportPreferences(&settings, task_);
    miacode::video_export::saveDialogPreferences(settings);
}

void ExportSession::seedFromDifficulty(int difficultyId)
{
    if (engine() == nullptr || !difficultyHasChartBody(difficultyId)) {
        setUnavailableReason(
            difficultyExists(difficultyId)
                ? qtTrId("export_page.the_selected_difficulty_has_no")
                : qtTrId("export_page.no_difficulty_is_available_to"));
        return;
    }
    setUnavailableReason(QString());
    VideoExportTask seededTask = engine()->buildSeedTask(difficultyId);
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
    // Applied last: this may re-emit rangeChanged()/introChanged() with the
    // requested range, overriding the full-range default just seeded above.
    applyPendingSelectionRangeExport();
}

void ExportSession::syncAudition()
{
    if (engine() == nullptr || !pageSessionActive_) {
        return;
    }
    if (!difficultyHasChartBody(selectedDifficultyId_)) {
        stopAudition();
        return;
    }
    engine()->startAudition(selectedDifficultyId_, task_);
}

void ExportSession::applyLivePreviewSettings()
{
    if (engine() == nullptr) {
        return;
    }
    VideoExportTask liveTask = task_;
    applyOwnerLiveFields(&liveTask);
    engine()->applySharedTaskSettings(liveTask);
    syncAudition();
}

void ExportSession::stopAudition()
{
    if (engine() == nullptr) {
        return;
    }
    engine()->stopAudition();
}

void ExportSession::applyOwnerLiveFields(VideoExportTask* task) const
{
    if (task == nullptr || preview() == nullptr) {
        return;
    }
    task->outlineVariant = appearance_->outlineVariant();
    task->slideEarlierSecondAndTextOnTop = appearance_->slideEarlierSecondAndTextOnTop();
    task->tapJudgeTextDistance = appearance_->tapJudgeTextDistance();
    task->judgeEffectStyle = appearance_->judgeEffectStyle();
    task->centerDisplayMode = appearance_->centerDisplayMode();
    task->muriRenderOptions = engine()->muriRenderOptions();
}

VideoExportTask ExportSession::buildRequestedTask() const
{
    VideoExportTask task = task_;
    task.outputWidth = kResolutionPresets[qBound(0, resolutionIndex_, static_cast<int>(std::size(kResolutionPresets)) - 1)].width;
    task.outputHeight = kResolutionPresets[qBound(0, resolutionIndex_, static_cast<int>(std::size(kResolutionPresets)) - 1)].height;
    task.fullRangeExport = miacode::video_export::isFullRangeVideoExport(task.exportStartSeconds);
    task.intro.enabled = task.intro.enabled && task.fullRangeExport;
    applyOwnerLiveFields(&task);
    return task;
}

void ExportSession::startExport()
{
    if (engine() == nullptr) {
        return;
    }
    if (activeTab_ == QLatin1String("batch")) {
        savePreferences();
        batchCancellationRequested_ = false;
        batchExportRunning_ = true;
        exportRunning_ = true;
        emit exportRunningChanged();
        miacode::ExportEngine::BatchResult result;
        miacode::ExportEngine::BatchCallbacks callbacks;
        // Batch runs synchronously on the UI thread, so it reports onto the same
        // shell overlay every other job uses. Before this the callback pumped
        // events and threw the percentage away, leaving batch with no progress
        // at all.
        miacode::JobProgressService* const jobProgress =
            jobProgress_;
        const QString batchJobTitle = qtTrId("dialog.batch_export.title");
        quint64 batchJobToken = 0;
        if (jobProgress != nullptr) {
            batchJobToken = jobProgress->begin(
                batchJobTitle,
                qtTrId("export.preparing_package"),
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
        const bool launched = engine()->launchBatchExport(
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
        const QString batchTitle = qtTrId("dialog.batch_export.title");
        if (!launched) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Error,
                batchTitle,
                error.isEmpty()
                    ? qtTrId("dialog.batch_export.error.export_failed")
                    : error);
            return;
        }
        if (result.canceled) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Information,
                batchTitle,
                qtTrId("dialog.batch_export.message.canceled"));
            return;
        }
        const auto shortenDetails = [](QString details) {
            return details.size() > 3000 ? details.left(3000) + QStringLiteral("\n...") : details;
        };
        const QString successDetails = shortenDetails(result.exportedFiles.join(QLatin1Char('\n')));
        if (result.failedCharts.isEmpty()) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Information,
                batchTitle,
                qtTrId("dialog.batch_export.message.success").arg(result.successCount),
                successDetails);
            return;
        }
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Warning,
            batchTitle,
            qtTrId("dialog.batch_export.message.partial_failed")
                .arg(result.successCount).arg(result.failedCharts.size()),
            (successDetails.isEmpty() ? QString()
                : qtTrId("dialog.batch_export.message.output_files")
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
    if (!engine()->launchVideoExport(
            buildRequestedTask(), selectedDifficultyId_, &error)) {
        exportRunning_ = false;
        emit exportRunningChanged();
        uiRequests_->postNotice(
            miacode::NoticeSeverity::Error,
            qtTrId("dialog.video_export.title"),
            error.isEmpty()
                ? qtTrId("dialog.video_export.error.launch_failed")
                : error);
        return;
    }
}

void ExportSession::cancelExport()
{
    if (engine() == nullptr) {
        return;
    }
    if (batchExportRunning_) {
        batchCancellationRequested_ = true;
        return;
    }
    engine()->cancelVideoExport();
}

void ExportSession::browseOutputPath()
{
    miacode::FileRequest request;
    request.title = qtTrId("dialog.video_export.title");
    request.startPath = task_.outputPath;
    request.nameFilters = QStringList{QStringLiteral("MP4 (*.mp4)")};
    request.saveMode = true;
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (!path.isEmpty()) {
            setOutputPath(path);
        }
    });
}

void ExportSession::browseIntroBackground()
{
    miacode::FileRequest request;
    request.title = qtTrId("dialog.video_export.choose_intro_background");
    request.startPath = task_.intro.customBackgroundPath;
    request.nameFilters = QStringList{QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (!path.isEmpty()) {
            setIntroCustomBackgroundPath(path);
        }
    });
}

void ExportSession::importIntroSound()
{
    miacode::FileRequest request;
    request.title = introSoundLabel();
    request.nameFilters = QStringList{QStringLiteral("Audio (*.wav *.mp3 *.ogg *.flac)")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        applyIntroSoundImport(path);
    });
}

void ExportSession::importIntroFont()
{
    miacode::FileRequest request;
    request.title = qtTrId("card_font.import");
    request.nameFilters = QStringList{QStringLiteral("Font Files (*.ttf *.otf)")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        applyFontImport(path);
    });
}

void ExportSession::applyFontImport(const QString& selectedPath)
{
    if (selectedPath.isEmpty()) {
        return;
    }
    const miacode::video_export::FontImportResult result =
        miacode::video_export::importFontFileIntoLibrary(selectedPath);
    if (result.path.isEmpty()) {
        const QString text = result.failure == miacode::video_export::FontImportFailure::CopyFailed
            ? qtTrId("card_font.copy_failed")
            : qtTrId("card_font.invalid_font");
        uiRequests_->postNotice(miacode::NoticeSeverity::Warning,
                                qtTrId("card_font.import"), text);
        return;
    }

    fontLibraryOptionsCacheValid_ = false;
    emit fontLibraryChanged();
    // Match the established card-font picker: an imported font becomes the
    // title/display choice while the body selection remains independent.
    setIntroFontDisplayPath(result.path);
}

void ExportSession::importHudFont()
{
    if (uiRequests_ == nullptr) {
        return;
    }
    miacode::FileRequest request;
    request.title = qtTrId("dialog.video_export.option.import_hud_font");
    request.nameFilters = QStringList{QStringLiteral("Font Files (*.ttf *.otf)")};
    uiRequests_->requestFile(request, [this](const QString& path) {
        applyHudFontImport(path);
    });
}

void ExportSession::applyHudFontImport(const QString& selectedPath)
{
    if (selectedPath.isEmpty()) {
        return;
    }
    const miacode::video_export::FontImportResult result =
        miacode::video_export::importFontFileIntoLibrary(selectedPath);
    if (result.path.isEmpty()) {
        if (uiRequests_ != nullptr) {
            uiRequests_->postNotice(
                miacode::NoticeSeverity::Warning,
                qtTrId("dialog.video_export.option.import_hud_font"),
                result.failure == miacode::video_export::FontImportFailure::CopyFailed
                    ? qtTrId("card_font.copy_failed")
                    : qtTrId("card_font.invalid_font"));
        }
        return;
    }
    fontLibraryOptionsCacheValid_ = false;
    emit fontLibraryChanged();
    setHudFontPath(result.path);
}

void ExportSession::resetHudFont()
{
    setHudFontPath(QString());
}

void ExportSession::openSkinDirectory()
{
    if (preview() == nullptr) {
        return;
    }
    const QString skinRoot = preview()->resolveSkinRootDir();
    if (!skinRoot.isEmpty()) {
        QDir().mkpath(skinRoot);
        QDesktopServices::openUrl(QUrl::fromLocalFile(skinRoot));
    }
}

void ExportSession::openJudgeLineDirectory()
{
    if (preview() == nullptr) {
        return;
    }
    const QString outlineDir = preview()->resolveCustomOutlineDir();
    if (!outlineDir.isEmpty()) {
        QDir().mkpath(outlineDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(outlineDir));
    }
}

void ExportSession::applyIntroSoundImport(const QString& selectedPath)
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

void ExportSession::browseBatchOutputDirectory()
{
    miacode::FileRequest request;
    request.title = qtTrId("dialog.batch_export.select_folder");
    request.startPath = batchOutputDirectory_;
    request.selectFolder = true;
    uiRequests_->requestFile(request, [this](const QString& path) {
        if (!path.isEmpty()) {
            setBatchOutputDirectory(path);
        }
    });
}

void ExportSession::addChartDirectories()
{
    miacode::FileRequest request;
    request.title = qtTrId("dialog.batch_export.select_charts");
    request.selectFolder = true;
    uiRequests_->requestFile(request, [this](const QString& path) {
        addChartDirectory(path);
    });
}

void ExportSession::addChartDirectory(const QString& path)
{
    if (path.isEmpty() || chartDirectories_.contains(path)) {
        return;
    }
    chartDirectories_.append(path);
    emit batchChanged();
}

void ExportSession::removeChartDirectory(int index)
{
    if (index < 0 || index >= chartDirectories_.size()) {
        return;
    }
    chartDirectories_.removeAt(index);
    emit batchChanged();
}

void ExportSession::clearChartDirectories()
{
    if (chartDirectories_.isEmpty()) {
        return;
    }
    chartDirectories_.clear();
    emit batchChanged();
}

void ExportSession::setBatchDifficultyChecked(int difficultyId, bool checked)
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

void ExportSession::setExportStartToCurrentPreview()
{
    if (engine() == nullptr) {
        return;
    }
    setExportStartSeconds(engine()->currentAudioClockSecond());
}

void ExportSession::setExportEndToCurrentPreview()
{
    if (engine() == nullptr) {
        return;
    }
    setExportEndSeconds(engine()->currentAudioClockSecond());
}

void ExportSession::setExportRangeSeconds(double start, double end)
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

void ExportSession::requestSelectionRangeExport(double startSecond, double endSecond)
{
    if (!qIsFinite(startSecond) || !qIsFinite(endSecond) || endSecond <= startSecond) {
        return;
    }
    hasPendingSelectionRangeExport_ = true;
    pendingRangeStartSeconds_ = startSecond;
    pendingRangeEndSeconds_ = endSecond;
}

void ExportSession::applyPendingSelectionRangeExport()
{
    if (!hasPendingSelectionRangeExport_) {
        return;
    }
    hasPendingSelectionRangeExport_ = false;
    setExportRangeSeconds(pendingRangeStartSeconds_, pendingRangeEndSeconds_);
}

void ExportSession::clearPendingSelectionRangeExport()
{
    hasPendingSelectionRangeExport_ = false;
}

QString ExportSession::setExportStartText(const QString& text)
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

QString ExportSession::setExportEndText(const QString& text)
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

void ExportSession::setOutputPath(const QString& path)
{
    if (task_.outputPath == path) {
        return;
    }
    task_.outputPath = path;
    emit outputChanged();
}

void ExportSession::setResolutionIndex(int index)
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

void ExportSession::setFps(int fps)
{
    if (task_.fps == fps) {
        return;
    }
    task_.fps = fps;
    emit outputChanged();
    savePreferences();
}

void ExportSession::setAudioBitrateKbps(int kbps)
{
    if (task_.audioBitrateKbps == kbps) {
        return;
    }
    task_.audioBitrateKbps = kbps;
    emit outputChanged();
    savePreferences();
}

void ExportSession::setPresetIndex(int index)
{
    const VideoExportPreset next = index == 0 ? VideoExportPreset::Fast : VideoExportPreset::HighQuality;
    if (task_.preset == next) {
        return;
    }
    task_.preset = next;
    emit outputChanged();
    savePreferences();
}

void ExportSession::setSizePresetIndex(int index)
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

void ExportSession::setBackgroundBrightnessOuter(double value)
{
    task_.backgroundBrightnessOuter = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void ExportSession::setBackgroundBrightnessInner(double value)
{
    task_.backgroundBrightnessInner = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void ExportSession::setLayoutSquareScale(double value)
{
    task_.layoutSquareScale = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void ExportSession::setBackgroundScaleModeIndex(int index)
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

void ExportSession::setSmoothBrightness(bool value)
{
    task_.smoothBrightness = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void ExportSession::setShowTimestamp(bool value)
{
    task_.showTimestamp = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void ExportSession::setShowObjectStatsHud(bool value)
{
    task_.showObjectStatsHud = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void ExportSession::setShowChartInfoHud(bool value)
{
    task_.showChartInfoHud = value;
    emit videoChanged();
    applyLivePreviewSettings();
}

void ExportSession::setFixHudTextLayout(bool value)
{
    task_.fixHudTextLayout = value;
    emit videoChanged();
    syncAudition();
    savePreferences();
}

void ExportSession::setClockCountEnabled(bool value)
{
    task_.clockCountEnabled = value;
    emit videoChanged();
    syncAudition();
    savePreferences();
}

void ExportSession::setTapFlowSpeed(double value)
{
    if (!qIsFinite(value)) {
        return;
    }
    task_.tapFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(value);
    emit gameplayChanged();
    applyLivePreviewSettings();
}

void ExportSession::setTouchFlowSpeed(double value)
{
    if (!qIsFinite(value)) {
        return;
    }
    task_.touchFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(value);
    emit gameplayChanged();
    applyLivePreviewSettings();
}

void ExportSession::setSkinIndex(int index)
{
    if (preview() == nullptr) {
        return;
    }
    const QStringList names = preview()->availableSkinDirectoryNames();
    if (index < 0 || index >= names.size()) {
        return;
    }
    const QString skinDirectoryName = names.at(index);
    // The owner decides whether this is a real change; the window reacts by
    // re-applying the skin to every surface and persisting it.
    if (!appearance_->setSkinDirectory(skinDirectoryName)) {
        return;
    }
    emit skinChanged();
}

void ExportSession::setSkinJudgeEffectIndex(int index)
{
    if (preview() == nullptr) {
        return;
    }
    const auto style = index == 1 ? PreviewJudgeEffectStyle::Starry : PreviewJudgeEffectStyle::Standard;
    if (!appearance_->setJudgeEffectStyle(style)) {
        return;
    }
    emit skinChanged();
}

void ExportSession::setOutlineIndex(int index)
{
    if (preview() == nullptr) {
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
    preview()->applyOutlineVariant(variant, /*useAutoSelection=*/false, /*persistState=*/true);
    emit skinChanged();
}

void ExportSession::setHudFontAreaIndex(int index)
{
    const auto choices = miacode::preview::scene::previewHudFontAreaChoices();
    if (choices.isEmpty()) return;
    const int normalized = qBound(0, index, choices.size() - 1);
    const int nextAreaId = miacode::preview::scene::previewHudFontAreaId(
        choices.at(normalized).area);
    if (hudFontAreaId_ == nextAreaId) {
        return;
    }
    hudFontAreaId_ = nextAreaId;
    emit hudFontChanged();
}

void ExportSession::setHudFontPath(const QString& path)
{
    const auto area = miacode::preview::scene::previewHudFontAreaFromId(hudFontAreaId_);
    if (miacode::preview::scene::previewHudCustomFontPath(area) == path) {
        return;
    }
    miacode::preview::scene::setPreviewHudCustomFontPath(area, path);
    if (preview() != nullptr) {
        preview()->refreshSurfaces();
    }
    emit hudFontChanged();
}

void ExportSession::setIntroEnabled(bool value)
{
    task_.intro.enabled = value;
    emit introChanged();
    savePreferences();
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::setIntroBackgroundModeIndex(int index)
{
    task_.intro.backgroundMode = index == 1 ? QStringLiteral("custom") : QStringLiteral("jacket");
    emit introChanged();
    savePreferences();
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::setIntroCustomBackgroundPath(const QString& path)
{
    task_.intro.customBackgroundPath = path;
    emit introChanged();
    savePreferences();
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::setIntroBlurBackground(bool value)
{
    task_.intro.blurBackground = value;
    emit introChanged();
    savePreferences();
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::setIntroModeIndex(int index)
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
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::setIntroCardShadow(bool value)
{
    task_.intro.cardShadow = value;
    emit introChanged();
    savePreferences();
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::setIntroLevelTextRender(bool value)
{
    task_.intro.lvRenderMode = value ? QStringLiteral("text") : QStringLiteral("atlas");
    emit introChanged();
    savePreferences();
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::setIntroFontDisplayPath(const QString& path)
{
    if (task_.intro.fontDisplayPath == path) {
        return;
    }
    task_.intro.fontDisplayPath = path;
    emit introChanged();
    savePreferences();
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::setIntroFontBodyPath(const QString& path)
{
    if (task_.intro.fontBodyPath == path) {
        return;
    }
    task_.intro.fontBodyPath = path;
    emit introChanged();
    savePreferences();
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::resetIntroFonts()
{
    const bool changed = !task_.intro.fontDisplayPath.isEmpty() || !task_.intro.fontBodyPath.isEmpty();
    task_.intro.fontDisplayPath.clear();
    task_.intro.fontBodyPath.clear();
    if (!changed) {
        return;
    }
    emit introChanged();
    savePreferences();
    if (engine() != nullptr) {
        engine()->refreshIntroState();
    }
}

void ExportSession::setIntroSoundIndex(int index)
{
    const QVariantList options = introSoundOptions();
    if (index < 0 || index >= options.size()) {
        return;
    }
    setIntroSoundFileName(
        options.at(index).toMap().value(QStringLiteral("fileName")).toString());
}

void ExportSession::setIntroSoundFileName(const QString& fileName)
{
    const QString normalized = miacode::preview_sfx::normalizeIntroSoundFileName(fileName);
    if (task_.introSoundFileName == normalized) {
        return;
    }
    task_.introSoundFileName = normalized;
    miacode::preview_sfx::setSelectedIntroSoundFileName(normalized);
    // The window reloads the SFX bank and persists in reaction to this.
    appearance_->setIntroSoundFileName(normalized);
    emit introChanged();
}

void ExportSession::setIntroSoundVolume(double value)
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
    if (preview() != nullptr) {
        preview()->applySfxLevels();
    }
    emit introChanged();
    savePreferences();
}

void ExportSession::setExportStartSeconds(double value)
{
    if (!qIsFinite(value)) {
        return;
    }
    const double end = exportEndSeconds();
    const double maximumStart = qMax(0.0, end - minimumExportRangeSeconds());
    setExportRangeSeconds(qBound(0.0, value, maximumStart), end);
}

void ExportSession::setExportEndSeconds(double value)
{
    if (!qIsFinite(value)) {
        return;
    }
    const double start = task_.exportStartSeconds;
    const double minimumEnd = start + minimumExportRangeSeconds();
    setExportRangeSeconds(start, qBound(minimumEnd, value, chartDurationSeconds_));
}

void ExportSession::setBatchOutputDirectory(const QString& path)
{
    if (batchOutputDirectory_ == path) {
        return;
    }
    batchOutputDirectory_ = path;
    QSettings settings(QStringLiteral("fanfaredash"), QStringLiteral("MiaCode"));
    settings.setValue(QStringLiteral("batch_video_export_dialog/last_output_directory"), path);
    emit batchChanged();
}

} // namespace miacode::ui
