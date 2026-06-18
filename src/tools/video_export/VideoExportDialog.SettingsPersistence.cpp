#include "VideoExportDialog.h"

#include "BusySpinner.h"
#include "DialogLocalization.h"
#include "EditableValueLabel.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/PreviewInteractionConfig.h"
#include "core/scene/PreviewHudState.h"
#include "tools/video_export/HudFontSettings.h"
#include "tools/video_export/IntroPreviewWidget.h"
#include "tools/video_export/VideoExportPreferences.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleValidator>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <QWidgetAction>

#include <limits>
#include <utility>

#include "VideoExportDialogInternal.h"

using namespace miacode::video_export::dialog_detail;

namespace {

struct ResolutionPreset {
    int width = 1080;
    int height = 1080;
    const char* label = "1080x1080 (1:1)";
    double aspectRatio = 1.0;
};

constexpr ResolutionPreset kResolutionPresets[] = {
    {720, 720, "720x720 (1:1)", 1.0},
    {1024, 1024, "1024x1024 (1:1)", 1.0},
    {960, 720, "960x720 (4:3)", 4.0 / 3.0},
    {1280, 720, "1280x720 (16:9)", 16.0 / 9.0},
    {1080, 1080, "1080x1080 (1:1)", 1.0},
    {1440, 1080, "1440x1080 (4:3)", 4.0 / 3.0},
    {1920, 1080, "1920x1080 (16:9)", 16.0 / 9.0},
    {1440, 1440, "1440x1440 (1:1)", 1.0},
    {1920, 1440, "1920x1440 (4:3)", 4.0 / 3.0},
    {2560, 1440, "2560x1440 (16:9)", 16.0 / 9.0},
};

QString exportDialogResolutionLabel(const QSize& size)
{
    for (const ResolutionPreset& preset : kResolutionPresets) {
        if (preset.width == size.width() && preset.height == size.height()) {
            return QString::fromLatin1(preset.label);
        }
    }
    return QStringLiteral("%1x%2").arg(qMax(1, size.width())).arg(qMax(1, size.height()));
}

QString videoExportPresetToken(VideoExportPreset preset)
{
    switch (preset) {
    case VideoExportPreset::HighQuality:
        return QStringLiteral("high_quality");
    case VideoExportPreset::Fast:
    default:
        return QStringLiteral("fast");
    }
}

VideoExportPreset videoExportPresetFromStoredValue(const QJsonValue& value, VideoExportPreset fallback)
{
    if (value.isString()) {
        const QString token = value.toString().trimmed();
        if (token.compare(QStringLiteral("high_quality"), Qt::CaseInsensitive) == 0) {
            return VideoExportPreset::HighQuality;
        }
        if (token.compare(QStringLiteral("high_compression"), Qt::CaseInsensitive) == 0) {
            return VideoExportPreset::HighQuality;
        }
        if (token.compare(QStringLiteral("fast"), Qt::CaseInsensitive) == 0) {
            return VideoExportPreset::Fast;
        }
    }
    return fallback;
}

}  // namespace

void VideoExportDialog::loadPersistedSettings()
{
    const QJsonObject settings = miacode::video_export::loadDialogPreferences();

    const int savedWidth = settings.value(QStringLiteral("resolution_width")).toInt(selectedResolution_.width());
    const int savedHeight = settings.value(QStringLiteral("resolution_height")).toInt(selectedResolution_.height());
    if (savedWidth > 0 && savedHeight > 0) {
        selectedResolution_ = QSize(savedWidth, savedHeight);
        if (resolutionButton_ != nullptr) {
            resolutionButton_->setText(exportDialogResolutionLabel(selectedResolution_));
        }
        applySelectedAspectRatioToPreview(false);
    }

    const int savedFps = settings.value(QStringLiteral("fps")).toInt(selectedFps_);
    selectedFps_ = savedFps >= 90 ? 120 : 60;
    if (fpsButton_ != nullptr) {
        fpsButton_->setText(QStringLiteral("%1 FPS").arg(selectedFps_));
    }

    const int savedAudioBitrate = settings.value(QStringLiteral("audio_bitrate_kbps"))
                                         .toInt(selectedAudioBitrateKbps_);
    selectedAudioBitrateKbps_ = normaliseAudioBitrateKbps(savedAudioBitrate);
    if (audioBitrateButton_ != nullptr) {
        audioBitrateButton_->setText(QStringLiteral("%1 kbps").arg(selectedAudioBitrateKbps_));
    }

    selectedPreset_ = videoExportPresetFromStoredValue(
        settings.value(QStringLiteral("preset")),
        selectedPreset_
    );
    if (presetButton_ != nullptr) {
        presetButton_->setText(exportDialogPresetLabel(selectedPreset_));
    }

    // App-level "add intro" preference (persists across sessions).
    if (addIntroCheck_ != nullptr) {
        const QSignalBlocker blocker(addIntroCheck_);
        addIntroCheck_->setChecked(
            settings.value(QStringLiteral("add_intro")).toBool(addIntroCheck_->isChecked()));
    }

    // "片头" tab styling (app-level preferences, like add_intro).
    if (introBackgroundCombo_ != nullptr) {
        const QSignalBlocker blocker(introBackgroundCombo_);
        const int idx = introBackgroundCombo_->findData(
            settings.value(QStringLiteral("intro_background_mode"))
                .toString(introBackgroundCombo_->currentData().toString()));
        if (idx >= 0) {
            introBackgroundCombo_->setCurrentIndex(idx);
        }
    }
    if (introBackgroundPathEdit_ != nullptr) {
        const QSignalBlocker blocker(introBackgroundPathEdit_);
        introBackgroundPathEdit_->setText(
            settings.value(QStringLiteral("intro_background_custom_path"))
                .toString(introBackgroundPathEdit_->text()));
    }
    if (introBlurCheck_ != nullptr) {
        const QSignalBlocker blocker(introBlurCheck_);
        introBlurCheck_->setChecked(
            settings.value(QStringLiteral("intro_background_blur")).toBool(introBlurCheck_->isChecked()));
    }
    if (introCardModeCombo_ != nullptr) {
        const QSignalBlocker blocker(introCardModeCombo_);
        const int idx = introCardModeCombo_->findData(
            settings.value(QStringLiteral("intro_card_type"))
                .toString(introCardModeCombo_->currentData().toString()));
        if (idx >= 0) {
            introCardModeCombo_->setCurrentIndex(idx);
        }
    }
    if (introCardShadowCheck_ != nullptr) {
        const QSignalBlocker blocker(introCardShadowCheck_);
        introCardShadowCheck_->setChecked(
            settings.value(QStringLiteral("intro_card_shadow")).toBool(introCardShadowCheck_->isChecked()));
    }
    if (introLevelTextCheck_ != nullptr) {
        const QSignalBlocker blocker(introLevelTextCheck_);
        introLevelTextCheck_->setChecked(
            settings.value(QStringLiteral("intro_level_text_render")).toBool(introLevelTextCheck_->isChecked()));
    }
    resizeIntroPreviewToAspect();
    syncIntroControlsEnabled();
    refreshIntroPreview();
}

void VideoExportDialog::savePersistedSettings(const VideoExportTask& task) const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    settings.insert(QStringLiteral("resolution_width"), task.outputWidth);
    settings.insert(QStringLiteral("resolution_height"), task.outputHeight);
    settings.insert(QStringLiteral("fps"), task.fps);
    settings.insert(QStringLiteral("audio_bitrate_kbps"), task.audioBitrateKbps);
    settings.insert(QStringLiteral("preset"), videoExportPresetToken(task.preset));
    appendIntroPersistedSettings(&settings);
    miacode::video_export::saveDialogPreferences(settings);
}

void VideoExportDialog::persistExportOnlySettings() const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    settings.insert(QStringLiteral("resolution_width"), selectedResolution().width());
    settings.insert(QStringLiteral("resolution_height"), selectedResolution().height());
    settings.insert(QStringLiteral("fps"), selectedFps_);
    settings.insert(QStringLiteral("audio_bitrate_kbps"), selectedAudioBitrateKbps_);
    settings.insert(QStringLiteral("preset"), videoExportPresetToken(selectedPreset_));
    appendIntroPersistedSettings(&settings);
    miacode::video_export::saveDialogPreferences(settings);
}

void VideoExportDialog::appendIntroPersistedSettings(QJsonObject* settings) const
{
    if (settings == nullptr) {
        return;
    }
    settings->insert(QStringLiteral("add_intro"),
                     addIntroCheck_ != nullptr && addIntroCheck_->isChecked());
    if (introBackgroundCombo_ != nullptr) {
        settings->insert(QStringLiteral("intro_background_mode"),
                         introBackgroundCombo_->currentData().toString());
    }
    if (introBackgroundPathEdit_ != nullptr) {
        settings->insert(QStringLiteral("intro_background_custom_path"),
                         introBackgroundPathEdit_->text().trimmed());
    }
    if (introBlurCheck_ != nullptr) {
        settings->insert(QStringLiteral("intro_background_blur"), introBlurCheck_->isChecked());
    }
    if (introCardModeCombo_ != nullptr) {
        settings->insert(QStringLiteral("intro_card_type"),
                         introCardModeCombo_->currentData().toString());
    }
    if (introCardShadowCheck_ != nullptr) {
        settings->insert(QStringLiteral("intro_card_shadow"), introCardShadowCheck_->isChecked());
    }
    if (introLevelTextCheck_ != nullptr) {
        settings->insert(QStringLiteral("intro_level_text_render"), introLevelTextCheck_->isChecked());
    }
}
