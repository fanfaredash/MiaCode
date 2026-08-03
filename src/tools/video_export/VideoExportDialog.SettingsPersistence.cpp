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

VideoExportSizePreset videoExportSizePresetFromStoredValue(
    const QJsonValue& value,
    VideoExportSizePreset fallback)
{
    const QString token = value.toString().trimmed();
    if (token.compare(QStringLiteral("compact"), Qt::CaseInsensitive) == 0) {
        return VideoExportSizePreset::Compact;
    }
    if (token.compare(QStringLiteral("ultra_compact"), Qt::CaseInsensitive) == 0) {
        return VideoExportSizePreset::UltraCompact;
    }
    if (token.compare(QStringLiteral("ultra_compact_with_pv"), Qt::CaseInsensitive) == 0) {
        return VideoExportSizePreset::UltraCompactWithPv;
    }
    if (token.compare(QStringLiteral("standard"), Qt::CaseInsensitive) == 0) {
        return VideoExportSizePreset::Standard;
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
        if (resolutionCombo_ != nullptr) {
            const QSignalBlocker blocker(resolutionCombo_);
            const int idx = resolutionCombo_->findData(selectedResolution_);
            if (idx >= 0) {
                resolutionCombo_->setCurrentIndex(idx);
            }
        }
        applySelectedAspectRatioToPreview(false);
    }

    const int savedFps = settings.value(QStringLiteral("fps")).toInt(selectedFps_);
    selectedFps_ = normaliseExportFps(savedFps);
    if (fpsCombo_ != nullptr) {
        const QSignalBlocker blocker(fpsCombo_);
        fpsCombo_->setCurrentIndex(qMax(0, fpsCombo_->findData(selectedFps_)));
    }

    const int savedAudioBitrate = settings.value(QStringLiteral("audio_bitrate_kbps"))
                                         .toInt(selectedAudioBitrateKbps_);
    selectedAudioBitrateKbps_ = normaliseAudioBitrateKbps(savedAudioBitrate);
    if (audioBitrateCombo_ != nullptr) {
        const QSignalBlocker blocker(audioBitrateCombo_);
        audioBitrateCombo_->setCurrentIndex(
            qMax(0, audioBitrateCombo_->findData(selectedAudioBitrateKbps_)));
    }

    selectedPreset_ = videoExportPresetFromStoredValue(
        settings.value(QStringLiteral("preset")),
        selectedPreset_
    );
    if (presetCombo_ != nullptr) {
        const QSignalBlocker blocker(presetCombo_);
        presetCombo_->setCurrentIndex(
            qMax(0, presetCombo_->findData(static_cast<int>(selectedPreset_))));
    }

    selectedSizePreset_ = videoExportSizePresetFromStoredValue(
        settings.value(QStringLiteral("size_preset")),
        selectedSizePreset_);
    if (sizePresetCombo_ != nullptr) {
        const QSignalBlocker blocker(sizePresetCombo_);
        sizePresetCombo_->setCurrentIndex(
            qMax(0, sizePresetCombo_->findData(static_cast<int>(selectedSizePreset_))));
    }

    // App-level count-in preference. Keep the historical opt-in default for
    // users who have not selected this option before.
    if (clockCountCheck_ != nullptr) {
        const QSignalBlocker blocker(clockCountCheck_);
        clockCountCheck_->setChecked(
            settings.value(QStringLiteral("clock_count_enabled"))
                .toBool(clockCountCheck_->isChecked()));
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
    if (introCardFontSelector_.widget != nullptr) {
        // setSelection suppresses the change callback; the refreshIntroPreview()
        // below picks the restored fonts up.
        introCardFontSelector_.setSelection(
            settings.value(QStringLiteral("intro_card_font_display")).toString(),
            settings.value(QStringLiteral("intro_card_font_body")).toString());
    }
    refreshIntroCardModeAutoLabel();
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
    settings.insert(
        QStringLiteral("size_preset"),
        miacode::video_export::videoExportSizePresetToken(task.sizePreset));
    settings.insert(QStringLiteral("clock_count_enabled"), task.clockCountEnabled);
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
    settings.insert(
        QStringLiteral("size_preset"),
        miacode::video_export::videoExportSizePresetToken(selectedSizePreset_));
    settings.insert(QStringLiteral("clock_count_enabled"),
                    clockCountCheck_ != nullptr && clockCountCheck_->isChecked());
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
    if (introCardFontSelector_.widget != nullptr) {
        settings->insert(QStringLiteral("intro_card_font_display"), introCardFontSelector_.displayPath());
        settings->insert(QStringLiteral("intro_card_font_body"), introCardFontSelector_.bodyPath());
    }
}
