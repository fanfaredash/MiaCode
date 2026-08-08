#include "QmlExportSession.h"

#include "mainwindow/MainWindow.h"
#include "mainwindow/sections/export/MainWindow.ExportSection.h"
#include "DialogLocalization.h"
#include "UiText.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "core/video/PreviewRenderSettings.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/video_export/VideoExportPreferences.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QJsonObject>
#include <QMessageBox>
#include <QSettings>
#include <QUrl>

namespace {

struct ResolutionPreset {
    int width = 0;
    int height = 0;
    const char* label = nullptr;
};

constexpr ResolutionPreset kResolutionPresets[] = {
    {720, 720, "720x720 (1:1)"},
    {1024, 1024, "1024x1024 (1:1)"},
    {960, 720, "960x720 (4:3)"},
    {1280, 720, "1280x720 (16:9)"},
    {1080, 1080, "1080x1080 (1:1)"},
    {1440, 1080, "1440x1080 (4:3)"},
    {1920, 1080, "1920x1080 (16:9)"},
    {1440, 1440, "1440x1440 (1:1)"},
    {1920, 1440, "1920x1440 (4:3)"},
    {2560, 1440, "2560x1440 (16:9)"},
};

constexpr int kFpsOptions[] = {30, 60, 120};
constexpr int kAudioBitrateOptions[] = {128, 160, 192, 256, 320};

}  // namespace

QmlExportSession::QmlExportSession(MainWindow& backend, QObject* parent)
    : QObject(parent)
    , backend_(&backend)
{
}

QString QmlExportSession::activeTab() const
{
    return activeTab_;
}

QVariantList QmlExportSession::resolutionOptions() const
{
    QVariantList list;
    for (const ResolutionPreset& preset : kResolutionPresets) {
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

double QmlExportSession::exportEndSeconds() const
{
    return task_.exportStartSeconds + qMax(0.0, task_.contentDurationSeconds);
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
    emit skinChanged();
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
    const int savedWidth = settings.value(QStringLiteral("resolution_width")).toInt(task_.outputWidth);
    const int savedHeight = settings.value(QStringLiteral("resolution_height")).toInt(task_.outputHeight);
    resolutionIndex_ = 1;
    for (int i = 0; i < static_cast<int>(std::size(kResolutionPresets)); ++i) {
        if (kResolutionPresets[i].width == savedWidth && kResolutionPresets[i].height == savedHeight) {
            resolutionIndex_ = i;
            break;
        }
    }
    task_.outputWidth = kResolutionPresets[resolutionIndex_].width;
    task_.outputHeight = kResolutionPresets[resolutionIndex_].height;
    task_.fps = settings.value(QStringLiteral("fps")).toInt(task_.fps);
    task_.audioBitrateKbps = settings.value(QStringLiteral("audio_bitrate_kbps")).toInt(task_.audioBitrateKbps);
    const QString preset = settings.value(QStringLiteral("preset")).toString();
    task_.preset = preset == QLatin1String("fast") ? VideoExportPreset::Fast : VideoExportPreset::HighQuality;
    const QString sizePreset = settings.value(QStringLiteral("size_preset")).toString();
    if (sizePreset == QLatin1String("compact")) {
        task_.sizePreset = VideoExportSizePreset::Compact;
    } else if (sizePreset == QLatin1String("ultra_compact_with_pv")) {
        task_.sizePreset = VideoExportSizePreset::UltraCompactWithPv;
    } else if (sizePreset == QLatin1String("ultra_compact")) {
        task_.sizePreset = VideoExportSizePreset::UltraCompact;
    } else {
        task_.sizePreset = VideoExportSizePreset::Standard;
    }
    task_.fixHudTextLayout = settings.value(QStringLiteral("fix_hud_text_layout")).toBool(false);
    task_.clockCountEnabled = settings.value(QStringLiteral("clock_count_enabled")).toBool(false);
    task_.intro.enabled = settings.value(QStringLiteral("add_intro")).toBool(task_.intro.enabled);
    const QString bgMode = settings.value(QStringLiteral("intro_background_mode")).toString(task_.intro.backgroundMode);
    task_.intro.backgroundMode = bgMode;
    task_.intro.customBackgroundPath =
        settings.value(QStringLiteral("intro_background_custom_path")).toString(task_.intro.customBackgroundPath);
    task_.intro.blurBackground = settings.value(QStringLiteral("intro_background_blur")).toBool(task_.intro.blurBackground);
    task_.intro.mode = settings.value(QStringLiteral("intro_card_type")).toString(QStringLiteral("auto"));
    task_.intro.cardShadow = settings.value(QStringLiteral("intro_card_shadow")).toBool(task_.intro.cardShadow);
    const bool levelText = settings.value(QStringLiteral("intro_level_text_render")).toBool(false);
    task_.intro.lvRenderMode = levelText ? QStringLiteral("text") : QStringLiteral("atlas");
}

void QmlExportSession::savePreferences() const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    settings.insert(QStringLiteral("resolution_width"), task_.outputWidth);
    settings.insert(QStringLiteral("resolution_height"), task_.outputHeight);
    settings.insert(QStringLiteral("fps"), task_.fps);
    settings.insert(QStringLiteral("audio_bitrate_kbps"), task_.audioBitrateKbps);
    settings.insert(
        QStringLiteral("preset"),
        task_.preset == VideoExportPreset::Fast ? QStringLiteral("fast") : QStringLiteral("high_quality"));
    QString sizeKey = QStringLiteral("standard");
    switch (task_.sizePreset) {
    case VideoExportSizePreset::Compact:
        sizeKey = QStringLiteral("compact");
        break;
    case VideoExportSizePreset::UltraCompactWithPv:
        sizeKey = QStringLiteral("ultra_compact_with_pv");
        break;
    case VideoExportSizePreset::UltraCompact:
        sizeKey = QStringLiteral("ultra_compact");
        break;
    case VideoExportSizePreset::Standard:
    default:
        break;
    }
    settings.insert(QStringLiteral("size_preset"), sizeKey);
    settings.insert(QStringLiteral("fix_hud_text_layout"), task_.fixHudTextLayout);
    settings.insert(QStringLiteral("clock_count_enabled"), task_.clockCountEnabled);
    settings.insert(QStringLiteral("add_intro"), task_.intro.enabled);
    settings.insert(QStringLiteral("intro_background_mode"), task_.intro.backgroundMode);
    settings.insert(QStringLiteral("intro_background_custom_path"), task_.intro.customBackgroundPath);
    settings.insert(QStringLiteral("intro_background_blur"), task_.intro.blurBackground);
    settings.insert(QStringLiteral("intro_card_type"), task_.intro.mode);
    settings.insert(QStringLiteral("intro_card_shadow"), task_.intro.cardShadow);
    settings.insert(
        QStringLiteral("intro_level_text_render"),
        task_.intro.lvRenderMode.compare(QStringLiteral("text"), Qt::CaseInsensitive) == 0);
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
        task_ = backend_->exportSection_->buildVideoExportSeedTaskPublic(difficultyId);
    chartDurationSeconds_ = qMax(0.0, task_.contentDurationSeconds);
    applyPreferences();
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
    task.fullRangeExport = task.exportStartSeconds <= 0.01;
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
        // Batch path: reuse existing Tools-menu batch flow with current prefs.
        // Full QML batch queue runner can expand later; for now open the shared
        // embedded batch path is not used — launch via ExportSection helper.
        QString error;
        if (!backend_->exportSection_->launchQmlBatchExport(
                buildRequestedTask(),
                chartDirectories_,
                batchSelectedDifficultyIds_,
                batchOutputDirectory_,
                &error)) {
            UiDialogs::showMessageBox(
                QMessageBox::Critical,
                backend_,
                UiText::text(QStringLiteral("dialog.batch_export.title")),
                error.isEmpty()
                    ? UiText::text(QStringLiteral("dialog.batch_export.error.export_failed"))
                    : error);
        }
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
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            backend_,
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
    backend_->exportSection_->cancelVideoExportWorker();
    exportRunning_ = false;
    emit exportRunningChanged();
}

void QmlExportSession::browseOutputPath()
{
    const QString path = QFileDialog::getSaveFileName(
        nullptr,
        UiText::text(QStringLiteral("dialog.video_export.title")),
        task_.outputPath,
        QStringLiteral("MP4 (*.mp4)"));
    if (!path.isEmpty()) {
        setOutputPath(path);
    }
}

void QmlExportSession::browseIntroBackground()
{
    const QString path = QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("选择片头背景"),
        task_.intro.customBackgroundPath,
        QStringLiteral("Images (*.png *.jpg *.jpeg *.webp)"));
    if (!path.isEmpty()) {
        setIntroCustomBackgroundPath(path);
    }
}

void QmlExportSession::browseBatchOutputDirectory()
{
    const QString path = QFileDialog::getExistingDirectory(
        nullptr,
        UiText::text(QStringLiteral("dialog.batch_export.select_folder")),
        batchOutputDirectory_);
    if (!path.isEmpty()) {
        setBatchOutputDirectory(path);
    }
}

void QmlExportSession::addChartDirectories()
{
    const QString path = QFileDialog::getExistingDirectory(
        nullptr,
        UiText::text(QStringLiteral("dialog.batch_export.select_charts")),
        QString());
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
}

void QmlExportSession::setAudioBitrateKbps(int kbps)
{
    if (task_.audioBitrateKbps == kbps) {
        return;
    }
    task_.audioBitrateKbps = kbps;
    emit outputChanged();
}

void QmlExportSession::setPresetIndex(int index)
{
    const VideoExportPreset next = index == 0 ? VideoExportPreset::Fast : VideoExportPreset::HighQuality;
    if (task_.preset == next) {
        return;
    }
    task_.preset = next;
    emit outputChanged();
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
}

void QmlExportSession::setBackgroundBrightnessOuter(double value)
{
    task_.backgroundBrightnessOuter = value;
    emit videoChanged();
    syncAudition();
}

void QmlExportSession::setBackgroundBrightnessInner(double value)
{
    task_.backgroundBrightnessInner = value;
    emit videoChanged();
    syncAudition();
}

void QmlExportSession::setLayoutSquareScale(double value)
{
    task_.layoutSquareScale = value;
    emit videoChanged();
    syncAudition();
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
    syncAudition();
}

void QmlExportSession::setSmoothBrightness(bool value)
{
    task_.smoothBrightness = value;
    emit videoChanged();
    syncAudition();
}

void QmlExportSession::setShowTimestamp(bool value)
{
    task_.showTimestamp = value;
    emit videoChanged();
}

void QmlExportSession::setShowObjectStatsHud(bool value)
{
    task_.showObjectStatsHud = value;
    emit videoChanged();
}

void QmlExportSession::setShowChartInfoHud(bool value)
{
    task_.showChartInfoHud = value;
    emit videoChanged();
}

void QmlExportSession::setFixHudTextLayout(bool value)
{
    task_.fixHudTextLayout = value;
    emit videoChanged();
}

void QmlExportSession::setClockCountEnabled(bool value)
{
    task_.clockCountEnabled = value;
    emit videoChanged();
    syncAudition();
}

void QmlExportSession::setTapFlowSpeed(double value)
{
    task_.tapFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(value);
    emit gameplayChanged();
    syncAudition();
}

void QmlExportSession::setTouchFlowSpeed(double value)
{
    task_.touchFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(value);
    emit gameplayChanged();
    syncAudition();
}

QVariantList QmlExportSession::skinOptions() const
{
    QVariantList list;
    if (backend_ == nullptr) {
        return list;
    }
    for (const QString& name : backend_->availablePreviewSkinDirectoryNames()) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), name);
        row.insert(QStringLiteral("label"), backend_->previewSkinDisplayName(name));
        list.append(row);
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

QVariantList QmlExportSession::judgeEffectOptions() const
{
    return QVariantList{
        UiText::text(QStringLiteral("dialog.skin_settings.chart_effect.standard")),
        UiText::text(QStringLiteral("dialog.skin_settings.chart_effect.starry")),
    };
}

int QmlExportSession::judgeEffectIndex() const
{
    if (backend_ == nullptr) {
        return 0;
    }
    return backend_->previewJudgeEffectStyle_ == PreviewJudgeEffectStyle::Starry ? 1 : 0;
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

void QmlExportSession::setJudgeEffectIndex(int index)
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
        variant = PreviewOutlineVariant::Line;
        break;
    }
    backend_->applyPreviewOutlineVariant(variant, /*useAutoSelection=*/false, /*persistState=*/true);
    emit skinChanged();
}

void QmlExportSession::openSkinDirectory()
{
    if (backend_ == nullptr) {
        return;
    }
    const QString skinRoot = backend_->resolvePreviewSkinRootDir();
    if (skinRoot.isEmpty()) {
        return;
    }
    QDir().mkpath(skinRoot);
    QDesktopServices::openUrl(QUrl::fromLocalFile(skinRoot));
}

void QmlExportSession::openJudgeLineDirectory()
{
    if (backend_ == nullptr) {
        return;
    }
    const QString outlineDir = backend_->resolvePreviewCustomOutlineDir();
    if (outlineDir.isEmpty()) {
        return;
    }
    QDir().mkpath(outlineDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(outlineDir));
}

void QmlExportSession::setIntroEnabled(bool value)
{
    task_.intro.enabled = value;
    emit introChanged();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroBackgroundModeIndex(int index)
{
    task_.intro.backgroundMode = index == 1 ? QStringLiteral("custom") : QStringLiteral("jacket");
    emit introChanged();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroCustomBackgroundPath(const QString& path)
{
    task_.intro.customBackgroundPath = path;
    emit introChanged();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroBlurBackground(bool value)
{
    task_.intro.blurBackground = value;
    emit introChanged();
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
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroCardShadow(bool value)
{
    task_.intro.cardShadow = value;
    emit introChanged();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setIntroLevelTextRender(bool value)
{
    task_.intro.lvRenderMode = value ? QStringLiteral("text") : QStringLiteral("atlas");
    emit introChanged();
    if (backend_ != nullptr) {
        backend_->refreshExportIntroState();
    }
}

void QmlExportSession::setExportStartSeconds(double value)
{
    const double clamped = qBound(0.0, value, chartDurationSeconds_);
    task_.exportStartSeconds = clamped;
    if (task_.exportStartSeconds + task_.contentDurationSeconds > chartDurationSeconds_) {
        task_.contentDurationSeconds = qMax(0.0, chartDurationSeconds_ - task_.exportStartSeconds);
    }
    task_.fullRangeExport = task_.exportStartSeconds <= 0.01;
    if (!task_.fullRangeExport) {
        task_.intro.enabled = false;
    }
    emit rangeChanged();
    emit introChanged();
}

void QmlExportSession::setExportEndSeconds(double value)
{
    const double end = qBound(task_.exportStartSeconds, value, chartDurationSeconds_);
    task_.contentDurationSeconds = qMax(0.0, end - task_.exportStartSeconds);
    task_.fullRangeExport = task_.exportStartSeconds <= 0.01
        && qAbs(task_.contentDurationSeconds - chartDurationSeconds_) <= 0.01;
    emit rangeChanged();
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
