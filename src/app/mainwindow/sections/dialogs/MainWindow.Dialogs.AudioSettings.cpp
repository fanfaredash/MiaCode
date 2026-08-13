#include "MainWindow.DialogsSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "AppVersion.h"
#include "QtPreviewSfxRuntime.h"
#include "DialogLocalization.h"
#include "EditableValueLabel.h"
#include "UiComponents.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/ChartAssetPaths.h"
#include "common/ChartClockCount.h"
#include "common/Id3TagReader.h"
#include "common/OperationLog.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewGameplayConfig.h"
#include "common/WaveformCache.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/latency/LatencyAnalysis.h"
#include "tools/video_export/HudFontSettings.h"

#include <QDesktopServices>
#include <QUrl>
#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <algorithm>
#include <cmath>
#include <utility>

#include "common/DebugLog.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <RestartManager.h>
#pragma comment(lib, "Rstrtmgr.lib")
#endif

using namespace miacode::mainwindow::shared;

void MainWindow::DialogsSection::openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title)
{
    if (!includeAudioSettings && !includeVideoSettings) {
        return;
    }
    owner_.previewAudioSettings_.normalize();

    miacode::ui::TabbedSettingsDialog dialog(
        &owner_, title, miacode::ui::SettingsDialogChrome::Settings);
    // Was 520 → 600 → 720 → 880. The QFormLayout's label column eats ~180px before
    // the videoCheckRow grid even sees the field, and each right-half checkbox cell
    // must fit an 8-char CJK label ("显示左下角时间戳" / "暂停时显示判定区") plus the
    // checkbox indicator + spacing. 720 still clipped the trailing glyph at some
    // fonts/DPI scales; 880 leaves comfortable margin for both checkboxes per row.
    // The video-settings variant carries the wider gameplay/video controls and
    // two CJK checkbox columns ("显示左下角时间戳" / "暂停时显示判定区") that clip
    // at narrower widths, so it gets extra width; the audio-only variant keeps
    // the 880 baseline.
    // NOTE: rootLayout uses QLayout::SetFixedSize (below), which locks the dialog
    // to its content sizeHint and IGNORES this dialog-level minimum width — that
    // is exactly why bumping this value had no visible effect. The video width is
    // now driven by the tab widget's minimumWidth instead (SetFixedSize honors a
    // *child* widget's minimum via QWidgetItem). This call stays only as a soft
    // floor for the audio-only variant.
    dialog.setMinimumWidth(880);
    const auto flowSpeedValueLabel = [](double flowSpeed) {
        const double snapped = qRound(flowSpeed * 4.0) / 4.0;
        const double roundedOneDecimal = qRound(snapped * 10.0) / 10.0;
        const bool useSingleDecimal = qAbs(snapped - roundedOneDecimal) < 0.001;
        return QString::number(snapped, 'f', useSingleDecimal ? 1 : 2);
    };

    QVBoxLayout* rootLayout = dialog.contentLayout();

    auto* audioGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.render_settings.audio_group")), &dialog);
    auto* audioFormLayout = new QFormLayout(audioGroup);
    audioFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    audioFormLayout->setHorizontalSpacing(10);
    audioFormLayout->setVerticalSpacing(8);

    const auto addAudioRow = [&](const QString& labelText,
                                 int valuePercent,
                                 QSlider** sliderOut,
                                 QLabel** labelOut,
                                 QToolButton** muteButtonOut = nullptr,
                                 int maximumPercent = 100) {
        auto* row = new QWidget(audioGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, maximumPercent);
        slider->setValue(valuePercent);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        auto* label = new miacode::ui::EditableValueLabel(QString::number(valuePercent) + "%", row);
        label->setMinimumWidth(44);
        label->bindSlider(slider);
        QToolButton* muteButton = nullptr;
        if (muteButtonOut != nullptr) {
            muteButton = new QToolButton(row);
            muteButton->setCursor(Qt::PointingHandCursor);
            muteButton->setAutoRaise(true);
            muteButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            muteButton->setFixedSize(18, 18);
            muteButton->setIconSize(QSize(14, 14));
        }
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        if (muteButton != nullptr) {
            rowLayout->addWidget(muteButton, 0);
        }
        audioFormLayout->addRow(labelText, row);
        *sliderOut = slider;
        *labelOut = label;
        if (muteButtonOut != nullptr) {
            *muteButtonOut = muteButton;
        }
    };

    const QString masterAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.global"));
    QSlider* masterSlider = nullptr;
    QLabel* masterLabel = nullptr;
    QToolButton* masterMuteButton = nullptr;
    addAudioRow(
        masterAudioLabelText,
        owner_.previewAudioSettings_.globalPercent(),
        &masterSlider,
        &masterLabel,
        &masterMuteButton,
        100
    );
    const QString bgmAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.track"));
    QSlider* bgmSlider = nullptr;
    QLabel* bgmLabel = nullptr;
    QToolButton* bgmMuteButton = nullptr;
    addAudioRow(bgmAudioLabelText, owner_.previewAudioSettings_.trackPercent(), &bgmSlider, &bgmLabel, &bgmMuteButton);
    const QString answerAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.answer"));
    QSlider* answerSlider = nullptr;
    QLabel* answerLabel = nullptr;
    QToolButton* answerMuteButton = nullptr;
    addAudioRow(
        answerAudioLabelText,
        owner_.previewAudioSettings_.answerPercent(),
        &answerSlider,
        &answerLabel,
        &answerMuteButton
    );
    const QString judgeAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.tap"));
    QSlider* judgeSlider = nullptr;
    QLabel* judgeLabel = nullptr;
    QToolButton* judgeMuteButton = nullptr;
    addAudioRow(judgeAudioLabelText, owner_.previewAudioSettings_.tapPercent(), &judgeSlider, &judgeLabel, &judgeMuteButton);
    const QString exAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.ex"));
    QSlider* exSlider = nullptr;
    QLabel* exLabel = nullptr;
    QToolButton* exMuteButton = nullptr;
    addAudioRow(exAudioLabelText, owner_.previewAudioSettings_.exPercent(), &exSlider, &exLabel, &exMuteButton);
    const QString breakAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.break"));
    QSlider* breakSlider = nullptr;
    QLabel* breakLabel = nullptr;
    QToolButton* breakMuteButton = nullptr;
    addAudioRow(breakAudioLabelText, owner_.previewAudioSettings_.breakPercent(), &breakSlider, &breakLabel, &breakMuteButton);
    const QString breakSlideAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.break_slide"));
    QSlider* breakSlideSlider = nullptr;
    QLabel* breakSlideLabel = nullptr;
    QToolButton* breakSlideMuteButton = nullptr;
    addAudioRow(
        breakSlideAudioLabelText,
        owner_.previewAudioSettings_.breakSlidePercent(),
        &breakSlideSlider,
        &breakSlideLabel,
        &breakSlideMuteButton
    );
    const QString slideAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.slide"));
    QSlider* slideSlider = nullptr;
    QLabel* slideLabel = nullptr;
    QToolButton* slideMuteButton = nullptr;
    addAudioRow(slideAudioLabelText, owner_.previewAudioSettings_.slidePercent(), &slideSlider, &slideLabel, &slideMuteButton);
    auto* breakSlideTailCheerCheck = new QCheckBox(
        UiText::text(QStringLiteral("dialog.render_settings.audio.break_slide_tail_cheer_mute")),
        audioGroup);
    breakSlideTailCheerCheck->setChecked(owner_.previewAudioSettings_.breakSlideTailCheerMuted);
    const QString touchAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.touch"));
    QSlider* touchSlider = nullptr;
    QLabel* touchLabel = nullptr;
    QToolButton* touchMuteButton = nullptr;
    addAudioRow(touchAudioLabelText, owner_.previewAudioSettings_.touchPercent(), &touchSlider, &touchLabel, &touchMuteButton);
    const QString fireworkAudioLabelText = UiText::text(QStringLiteral("dialog.render_settings.audio.firework"));
    QSlider* fireworkSlider = nullptr;
    QLabel* fireworkLabel = nullptr;
    QToolButton* fireworkMuteButton = nullptr;
    addAudioRow(
        fireworkAudioLabelText,
        owner_.previewAudioSettings_.fireworkPercent(),
        &fireworkSlider,
        &fireworkLabel,
        &fireworkMuteButton
    );
    audioFormLayout->addRow(QString(), breakSlideTailCheerCheck);

    const auto addVideoSliderRow = [](
        QWidget* parent,
        int minimum,
        int maximum,
        int step,
        int value,
        const QString& suffix,
        QSlider** sliderOut,
        QLabel** labelOut
    ) {
        auto* row = new QWidget(parent);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(minimum, maximum);
        slider->setSingleStep(step);
        slider->setPageStep(step);
        slider->setTickInterval(step);
        slider->setValue(value);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        auto* label = new miacode::ui::EditableValueLabel(QString::number(value) + suffix, row);
        label->setMinimumWidth(44);
        label->bindSlider(slider);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        *sliderOut = slider;
        *labelOut = label;
        return row;
    };

    auto* videoGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.render_settings.video_group")), &dialog);
    auto* videoFormLayout = new QFormLayout(videoGroup);
    videoFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    videoFormLayout->setHorizontalSpacing(10);
    videoFormLayout->setVerticalSpacing(8);
    auto* gameplayGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.render_settings.gameplay_group")), &dialog);
    auto* gameplayLayout = new QGridLayout(gameplayGroup);
    gameplayLayout->setContentsMargins(10, 8, 10, 8);
    gameplayLayout->setHorizontalSpacing(10);
    gameplayLayout->setVerticalSpacing(8);
    gameplayLayout->setColumnStretch(0, 1);
    gameplayLayout->setColumnStretch(1, 1);
    auto* performanceGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.render_settings.performance_group")), &dialog);
    auto* performanceFormLayout = new QFormLayout(performanceGroup);
    performanceFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    performanceFormLayout->setHorizontalSpacing(10);
    performanceFormLayout->setVerticalSpacing(8);

    QSlider* outerBrightnessSlider = nullptr;
    QLabel* outerBrightnessLabel = nullptr;
    QWidget* outerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(owner_.previewBackgroundBrightnessOuter_ * 100.0),
        QStringLiteral("%"),
        &outerBrightnessSlider,
        &outerBrightnessLabel
    );
    QSlider* innerBrightnessSlider = nullptr;
    QLabel* innerBrightnessLabel = nullptr;
    QWidget* innerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(owner_.previewBackgroundBrightnessInner_ * 100.0),
        QStringLiteral("%"),
        &innerBrightnessSlider,
        &innerBrightnessLabel
    );
    QSlider* layoutSquareScaleSlider = nullptr;
    QLabel* layoutSquareScaleLabel = nullptr;
    QWidget* layoutSquareScaleRow = addVideoSliderRow(
        videoGroup,
        qRound(miacode::preview_video::kLayoutSquareScaleMin * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleMax * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0),
        qRound(owner_.previewLayoutSquareScale_ * 100.0),
        QStringLiteral("%"),
        &layoutSquareScaleSlider,
        &layoutSquareScaleLabel
    );
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    const auto snapFlowSpeed = [flowSpeedMin, flowSpeedMax, flowSpeedStep](double flowSpeed) {
        return qBound(
            flowSpeedMin,
            flowSpeedMin + qRound((flowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
            flowSpeedMax
        );
    };
    double selectedTapFlowSpeed = snapFlowSpeed(owner_.previewTapFlowSpeed_);
    double selectedTouchFlowSpeed = snapFlowSpeed(owner_.previewTouchFlowSpeed_);
    const auto createFlowSpeedEdit = [&](double& selectedFlowSpeed, const std::function<void(double)>& applyFlowSpeed) {
        auto* flowSpeedEdit = new QLineEdit(gameplayGroup);
        flowSpeedEdit->setAlignment(Qt::AlignCenter);
        flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
        flowSpeedEdit->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet());
        flowSpeedEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* flowSpeedValidator = new QDoubleValidator(flowSpeedMin, flowSpeedMax, 2, flowSpeedEdit);
        flowSpeedValidator->setNotation(QDoubleValidator::StandardNotation);
        flowSpeedEdit->setValidator(flowSpeedValidator);
        QObject::connect(flowSpeedEdit, &QLineEdit::editingFinished, &dialog, [&, flowSpeedEdit, applyFlowSpeed]() {
            bool ok = false;
            const double typedSpeed = flowSpeedEdit->text().trimmed().toDouble(&ok);
            if (!ok) {
                flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
                return;
            }
            selectedFlowSpeed = snapFlowSpeed(typedSpeed);
            flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
            applyFlowSpeed(selectedFlowSpeed);
            owner_.savePortableState();
        });
        return flowSpeedEdit;
    };
    auto* tapFlowSpeedEdit = createFlowSpeedEdit(selectedTapFlowSpeed, [this](double flowSpeed) {
        owner_.previewTapFlowSpeed_ = flowSpeed;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setTapFlowSpeed(flowSpeed);
        }
    });
    auto* touchFlowSpeedEdit = createFlowSpeedEdit(selectedTouchFlowSpeed, [this](double flowSpeed) {
        owner_.previewTouchFlowSpeed_ = flowSpeed;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setTouchFlowSpeed(flowSpeed);
        }
    });

    struct CanvasFrameRateOption {
        PreviewCanvasFrameRateMode mode;
        QString label;
    };
    const double detectedRefreshRate = owner_.currentPreviewCanvasRefreshRate();
    const QString displayRefreshLabel = QStringLiteral("%1 (%2 Hz)")
        .arg(UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.display")))
        .arg(QString::number(detectedRefreshRate, 'f', detectedRefreshRate >= 100.0 ? 0 : 1));
    QList<CanvasFrameRateOption> canvasFrameRateOptions;
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::Fps60,
        UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.60")),
    });
    // Only expose the 120 FPS option on a display that can sustain it.
    // The backend clamps Fps120 to display refresh at runtime (see
    // previewCanvasTargetFrameIntervalNs), so leaving it in the menu
    // would advertise a setting that silently degrades to display
    // refresh — confusing the user. Threshold uses an epsilon (119.5)
    // so panels that report 119.88 Hz (common OEM round-down of true
    // 120 Hz) still see the option.
    if (detectedRefreshRate >= 119.5) {
        canvasFrameRateOptions.append({
            PreviewCanvasFrameRateMode::Fps120,
            UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate.120")),
        });
    }
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::DisplayRefresh,
        displayRefreshLabel,
    });

    QString selectedCanvasFrameRateLabel = canvasFrameRateOptions.front().label;
    bool foundExactSelectedFrameRate = false;
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        if (option.mode == owner_.previewCanvasFrameRateMode_) {
            selectedCanvasFrameRateLabel = option.label;
            foundExactSelectedFrameRate = true;
            break;
        }
    }
    // Saved mode is no longer offered (user previously had Fps120 on a
    // higher-refresh display, then connected to a lower one). Show the
    // DisplayRefresh label since that's what the backend actually
    // applies at runtime — the saved value itself is preserved so
    // reconnecting to a 120 Hz panel restores the original selection.
    if (!foundExactSelectedFrameRate) {
        for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
            if (option.mode == PreviewCanvasFrameRateMode::DisplayRefresh) {
                selectedCanvasFrameRateLabel = option.label;
                break;
            }
        }
    }
    auto* canvasFrameRateCombo = miacode::ui::createDialogComboBox(videoGroup, 12);
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        canvasFrameRateCombo->addItem(option.label, static_cast<int>(option.mode));
    }
    canvasFrameRateCombo->setCurrentIndex(
        qMax(0, canvasFrameRateCombo->findText(selectedCanvasFrameRateLabel)));
    miacode::ui::applyDialogComboBoxStyle(canvasFrameRateCombo, 12);
    connect(canvasFrameRateCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog,
            [this, canvasFrameRateCombo](int index) {
                if (index < 0) {
                    return;
                }
                owner_.setPreviewCanvasFrameRateMode(
                    static_cast<PreviewCanvasFrameRateMode>(
                        canvasFrameRateCombo->itemData(index).toInt()),
                    true);
            });

    const QString scaleFillLabel = UiText::text(QStringLiteral("dialog.render_settings.video.scale.fill"));
    const QString scaleFitLabel = UiText::text(QStringLiteral("dialog.render_settings.video.scale.fit"));
    const QString scaleSquareFitLabel = UiText::text(QStringLiteral("dialog.render_settings.video.scale.square_fit"));
    const QString scaleInnerCircleFitOuterFillLabel = UiText::text(QStringLiteral("dialog.render_settings.video.scale.inner_circle_fit_outer_fill"));
    const QString slideStackOrderDxLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.slide_stack_order.dx_style"));
    const QString slideStackOrderFinaleLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.slide_stack_order.finale_style"));
    const auto tapJudgeTextDistanceLabelForValue = [](PreviewTapJudgeTextDistance distance) -> QString {
        switch (distance) {
        case PreviewTapJudgeTextDistance::Inner:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.tap_judge_text_distance.inner"));
        case PreviewTapJudgeTextDistance::Middle:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.tap_judge_text_distance.middle"));
        case PreviewTapJudgeTextDistance::Outer:
        default:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.tap_judge_text_distance.outer"));
        }
    };
    // 皮肤 / 判定线 moved to the shared 皮肤 popup (buildSkinSettings).
    const QString disabledLabel = UiText::text(QStringLiteral("dialog.render_settings.option.disabled"));
    const QString slideJudgeChoiceLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect.slide"));
    const QString tapJudgeChoiceLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect.tap"));
    const QString breakJudgeChoiceLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect.break"));
    const QString touchJudgeChoiceLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect.touch"));
    const auto judgeEffectButtonLabel = [
        this,
        slideJudgeChoiceLabel,
        tapJudgeChoiceLabel,
        breakJudgeChoiceLabel,
        touchJudgeChoiceLabel,
        disabledLabel
    ]() {
        QStringList parts;
        if (owner_.muriRenderOptions_.showChartReviewSlideJudgeOverlay) {
            parts.append(slideJudgeChoiceLabel);
        }
        if (owner_.muriRenderOptions_.showChartReviewTapJudgeOverlay) {
            parts.append(tapJudgeChoiceLabel);
        }
        if (owner_.muriRenderOptions_.showChartReviewBreakJudgeOverlay) {
            parts.append(breakJudgeChoiceLabel);
        }
        if (owner_.muriRenderOptions_.showChartReviewTouchJudgeOverlay) {
            parts.append(touchJudgeChoiceLabel);
        }
        return parts.isEmpty() ? disabledLabel : parts.join(QStringLiteral(", "));
    };
    QMenu* judgeEffectMenu = nullptr;
    auto* judgeEffectButton = miacode::ui::createDialogDropdownButton(
        gameplayGroup, judgeEffectButtonLabel(), &judgeEffectMenu);
    const auto judgeEffectChoiceText = [](const QString& label, bool /*enabled*/) {
        return label;
    };
    const auto addJudgeEffectChoice = [
        this,
        &dialog,
        judgeEffectButton,
        judgeEffectButtonLabel,
        judgeEffectMenu,
        judgeEffectChoiceText
    ](const QString& label, bool MuriRenderOptions::*memberPtr) {
        const bool initialChecked = owner_.muriRenderOptions_.*memberPtr;
        auto* checkbox = miacode::ui::addDialogMenuCheckChoice(
            judgeEffectMenu,
            judgeEffectChoiceText(label, initialChecked),
            initialChecked,
            &dialog,
            [this, judgeEffectButton, judgeEffectButtonLabel, judgeEffectChoiceText,
             label, memberPtr](QCheckBox* checkbox, bool checked) {
                if (owner_.muriRenderOptions_.*memberPtr == checked) {
                    return;
                }
                owner_.muriRenderOptions_.*memberPtr = checked;
                if (checkbox != nullptr) {
                    checkbox->setText(judgeEffectChoiceText(label, checked));
                }
                judgeEffectButton->setText(judgeEffectButtonLabel());
                owner_.applyMuriRenderOptions();
                owner_.savePortableState();
            }
        );
    };
    addJudgeEffectChoice(slideJudgeChoiceLabel, &MuriRenderOptions::showChartReviewSlideJudgeOverlay);
    addJudgeEffectChoice(tapJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTapJudgeOverlay);
    addJudgeEffectChoice(breakJudgeChoiceLabel, &MuriRenderOptions::showChartReviewBreakJudgeOverlay);
    addJudgeEffectChoice(touchJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTouchJudgeOverlay);
    // 判定线 (outline variant) moved to the shared 皮肤 popup (buildSkinSettings).
    // The "暂停时显示判定区" toggle stays here — it is a video-side option.
    auto* forceLabeledJudgeLineWhenPausedCheck = new QCheckBox(
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.force_labeled_judge_line_when_paused")),
        videoGroup
    );
    forceLabeledJudgeLineWhenPausedCheck->setChecked(owner_.previewForceLabeledJudgeLineWhenPaused_);
    auto* slideStackOrderCombo = miacode::ui::createDialogComboBox(gameplayGroup, 12);
    slideStackOrderCombo->addItem(slideStackOrderDxLabel, true);
    slideStackOrderCombo->addItem(slideStackOrderFinaleLabel, false);
    slideStackOrderCombo->setCurrentIndex(owner_.previewSlideEarlierSecondAndTextOnTop_ ? 0 : 1);
    miacode::ui::applyDialogComboBoxStyle(slideStackOrderCombo, 12);
    judgeEffectButton->setFixedHeight(slideStackOrderCombo->minimumHeight());
    connect(slideStackOrderCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog,
            [this, slideStackOrderCombo](int index) {
                if (index < 0) {
                    return;
                }
                const bool earlierOnTop = slideStackOrderCombo->itemData(index).toBool();
                if (owner_.previewSlideEarlierSecondAndTextOnTop_ == earlierOnTop) {
                    return;
                }
                owner_.previewSlideEarlierSecondAndTextOnTop_ = earlierOnTop;
                if (owner_.previewCanvas_ != nullptr) {
                    owner_.previewCanvas_->setSlideEarlierSecondAndTextOnTop(earlierOnTop);
                }
                owner_.savePortableState();
            });
    auto* tapJudgeTextDistanceCombo = miacode::ui::createDialogComboBox(gameplayGroup, 12);
    for (const PreviewTapJudgeTextDistance distance : {
             PreviewTapJudgeTextDistance::Inner,
             PreviewTapJudgeTextDistance::Middle,
             PreviewTapJudgeTextDistance::Outer,
         }) {
        tapJudgeTextDistanceCombo->addItem(tapJudgeTextDistanceLabelForValue(distance), static_cast<int>(distance));
    }
    tapJudgeTextDistanceCombo->setCurrentIndex(
        qMax(0, tapJudgeTextDistanceCombo->findData(static_cast<int>(owner_.previewTapJudgeTextDistance_))));
    miacode::ui::applyDialogComboBoxStyle(tapJudgeTextDistanceCombo, 12);
    connect(tapJudgeTextDistanceCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog,
            [this, tapJudgeTextDistanceCombo](int index) {
                if (index < 0) {
                    return;
                }
                const auto distance =
                    static_cast<PreviewTapJudgeTextDistance>(tapJudgeTextDistanceCombo->itemData(index).toInt());
                if (owner_.previewTapJudgeTextDistance_ == distance) {
                    return;
                }
                owner_.previewTapJudgeTextDistance_ = distance;
                if (owner_.previewCanvas_ != nullptr) {
                    owner_.previewCanvas_->setTapJudgeTextDistance(distance);
                }
                owner_.savePortableState();
            });
    auto* scaleModeCombo = miacode::ui::createDialogComboBox(videoGroup, 12);
    scaleModeCombo->addItem(scaleFillLabel, static_cast<int>(PreviewBackgroundScaleMode::FillCrop));
    scaleModeCombo->addItem(scaleFitLabel, static_cast<int>(PreviewBackgroundScaleMode::FitContain));
    scaleModeCombo->addItem(scaleSquareFitLabel, static_cast<int>(PreviewBackgroundScaleMode::SquareFitContain));
    scaleModeCombo->addItem(
        scaleInnerCircleFitOuterFillLabel,
        static_cast<int>(PreviewBackgroundScaleMode::InnerCircleFitOuterFill));
    scaleModeCombo->setCurrentIndex(
        qMax(0, scaleModeCombo->findData(static_cast<int>(owner_.previewBackgroundScaleMode_))));
    miacode::ui::applyDialogComboBoxStyle(scaleModeCombo, 12);
    connect(scaleModeCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog,
            [this, scaleModeCombo](int index) {
                if (index < 0) {
                    return;
                }
                const auto mode =
                    static_cast<PreviewBackgroundScaleMode>(scaleModeCombo->itemData(index).toInt());
                if (owner_.previewBackgroundScaleMode_ == mode) {
                    return;
                }
                owner_.previewBackgroundScaleMode_ = mode;
                owner_.applyPreviewStageMediaRouteVisualSettings();
                if (owner_.previewCanvas_ != nullptr) {
                    owner_.previewCanvas_->setBackgroundScaleMode(mode);
                }
                owner_.savePortableState();
            });

    auto* smoothBrightnessCheck = new QCheckBox(
        UiText::text(QStringLiteral("dialog.render_settings.video.smooth_brightness")),
        videoGroup
    );
    smoothBrightnessCheck->setChecked(owner_.previewSmoothBrightness_);
    auto* timestampCheck = new QCheckBox(
        UiText::text(QStringLiteral("dialog.video_export.option.show_timestamp")),
        videoGroup
    );
    timestampCheck->setChecked(owner_.previewShowTimestamp_);
    auto* touchPadAuthoringShortcutCheck = new QCheckBox(
        UiText::text(QStringLiteral("dialog.render_settings.video.touch_pad_authoring_shortcut")),
        videoGroup
    );
    touchPadAuthoringShortcutCheck->setChecked(owner_.previewTouchPadAuthoringShortcutEnabled_);
    auto* debugCheck = new QCheckBox(
        UiText::text(QStringLiteral("dialog.render_settings.preview.debug")),
        videoGroup
    );
    debugCheck->setChecked(owner_.previewShowDebugInfo_);
    videoFormLayout->addRow(UiText::text(QStringLiteral("dialog.render_settings.video.brightness_outer")), outerBrightnessRow);
    videoFormLayout->addRow(UiText::text(QStringLiteral("dialog.render_settings.video.brightness_inner")), innerBrightnessRow);
    videoFormLayout->addRow(UiText::text(QStringLiteral("dialog.render_settings.video.layout_square_scale")), layoutSquareScaleRow);
    videoFormLayout->addRow(UiText::text(QStringLiteral("dialog.render_settings.video.scale_mode")), scaleModeCombo);
    // 预览刷新率 lives on the 性能 tab now.
    performanceFormLayout->addRow(
        UiText::text(QStringLiteral("dialog.render_settings.preview.canvas_frame_rate")),
        canvasFrameRateCombo
    );
    auto* videoCheckRow = new QWidget(videoGroup);
    auto* videoCheckLayout = new QGridLayout(videoCheckRow);
    videoCheckLayout->setContentsMargins(0, 0, 0, 0);
    videoCheckLayout->setHorizontalSpacing(6);
    videoCheckLayout->setVerticalSpacing(6);
    videoCheckLayout->setColumnStretch(0, 1);
    videoCheckLayout->setColumnStretch(1, 1);
    videoCheckLayout->addWidget(smoothBrightnessCheck, 0, 0, Qt::AlignLeft);
    videoCheckLayout->addWidget(timestampCheck, 0, 1, Qt::AlignLeft);
    videoCheckLayout->addWidget(touchPadAuthoringShortcutCheck, 1, 0, Qt::AlignLeft);
    videoCheckLayout->addWidget(debugCheck, 1, 1, Qt::AlignLeft);
    videoCheckLayout->addWidget(forceLabeledJudgeLineWhenPausedCheck, 2, 0, Qt::AlignLeft);
    videoFormLayout->addRow(QString(), videoCheckRow);

    const auto addSettingsField = [](QWidget* parent, QGridLayout* layout, int row, int column, const QString& labelText, QWidget* control) {
        auto* field = new QWidget(parent);
        auto* fieldLayout = new QVBoxLayout(field);
        // 3px bottom margin so the rounded control underneath isn't flush
        // against the field's edge — otherwise its bottom border is clipped
        // (visible as the "missing bottom edge" on every box).
        fieldLayout->setContentsMargins(0, 0, 0, 3);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        // The QSS border-radius controls report a sizeHint a hair short of what
        // the bottom border needs; pad the height so it renders fully — but
        // ONLY for controls that don't already carry a fixed height. Combos and
        // the combo-like dropdown button set min==max themselves; bumping
        // minimumHeight to their (taller) QToolButton sizeHint+4 would push min
        // above max and make the button taller than the sibling combo.
        if (control->minimumHeight() != control->maximumHeight()) {
            control->setMinimumHeight(qMax(control->minimumHeight(), control->sizeHint().height() + 4));
        }
        fieldLayout->addWidget(control, 0);
        layout->addWidget(field, row, column);
    };
    const auto addGameplayField = [gameplayGroup, gameplayLayout, addSettingsField](
        int row,
        int column,
        const QString& labelText,
        QWidget* control
    ) {
        addSettingsField(gameplayGroup, gameplayLayout, row, column, labelText, control);
    };
    addGameplayField(
        0,
        0,
        UiText::text(QStringLiteral("dialog.render_settings.video.tap_flow_speed")),
        tapFlowSpeedEdit
    );
    addGameplayField(
        0,
        1,
        UiText::text(QStringLiteral("dialog.render_settings.video.touch_flow_speed")),
        touchFlowSpeedEdit
    );
    addGameplayField(
        1,
        0,
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect")),
        judgeEffectButton
    );
    addGameplayField(
        1,
        1,
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.slide_stack_order")),
        slideStackOrderCombo
    );
    const auto centerDisplayLabelForMode = [](miacode::preview_gameplay::CenterDisplayMode mode) -> QString {
        switch (mode) {
        case miacode::preview_gameplay::CenterDisplayMode::Off:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display.off"));
        case miacode::preview_gameplay::CenterDisplayMode::Combo:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display.combo"));
        case miacode::preview_gameplay::CenterDisplayMode::AchievementDxPlus:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display.achievement_dx_plus"));
        case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display.achievement_dx_minus_100"));
        case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display.achievement_dx_minus_101"));
        case miacode::preview_gameplay::CenterDisplayMode::DxScorePlus:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display.dx_score_plus"));
        case miacode::preview_gameplay::CenterDisplayMode::DxScoreMinus:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display.dx_score_minus"));
        case miacode::preview_gameplay::CenterDisplayMode::AchievementFinalePlus:
            return UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display.achievement_finale_plus"));
        }
        return UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display.off"));
    };
    using miacode::preview_gameplay::CenterDisplayMode;
    auto* centerDisplayCombo = miacode::ui::createDialogComboBox(gameplayGroup, 12);
    for (const CenterDisplayMode mode : {
             CenterDisplayMode::Off,
             CenterDisplayMode::Combo,
             CenterDisplayMode::AchievementDxPlus,
             CenterDisplayMode::AchievementDxMinus100,
             CenterDisplayMode::AchievementDxMinus101,
             CenterDisplayMode::DxScorePlus,
             CenterDisplayMode::DxScoreMinus,
             CenterDisplayMode::AchievementFinalePlus,
         }) {
        centerDisplayCombo->addItem(centerDisplayLabelForMode(mode), static_cast<int>(mode));
    }
    centerDisplayCombo->setCurrentIndex(
        qMax(0, centerDisplayCombo->findData(static_cast<int>(owner_.previewCenterDisplayMode_))));
    miacode::ui::applyDialogComboBoxStyle(centerDisplayCombo, 12);
    connect(centerDisplayCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            &dialog,
            [this, centerDisplayCombo](int index) {
                if (index < 0) {
                    return;
                }
                const auto mode =
                    static_cast<CenterDisplayMode>(centerDisplayCombo->itemData(index).toInt());
                if (owner_.previewCenterDisplayMode_ == mode) {
                    return;
                }
                owner_.previewCenterDisplayMode_ = mode;
                if (owner_.previewCanvas_ != nullptr) {
                    owner_.previewCanvas_->setCenterDisplayMode(mode);
                }
                owner_.savePortableState();
            });
    addGameplayField(
        2,
        0,
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display")),
        centerDisplayCombo
    );
    addGameplayField(
        2,
        1,
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.tap_judge_text_distance")),
        tapJudgeTextDistanceCombo
    );
    // Intro sound is no longer hosted by this preview-settings dialog; the
    // shared skin panel stays focused on skin / judge line / HUD font.
    audioGroup->setVisible(includeAudioSettings);
    videoGroup->setVisible(includeVideoSettings);
    gameplayGroup->setVisible(includeVideoSettings);
    performanceGroup->setVisible(includeVideoSettings);

    if (includeAudioSettings) {
        rootLayout->addWidget(audioGroup, 0);
    }
    if (includeVideoSettings) {
        // Surface the 视频 / 游戏 split as tab pages instead of two
        // stacked QGroupBoxes — matches the Preferences dialog pattern.
        // The groupboxes keep their layouts + children intact; we only
        // strip their title bar and frame chrome so they read as plain
        // tab contents (the tab strip already shows the section name).
        miacode::ui::flattenGroupForTabPage(videoGroup);
        miacode::ui::flattenGroupForTabPage(gameplayGroup);
        miacode::ui::flattenGroupForTabPage(performanceGroup);
        auto* videoSettingsTabs = dialog.createTabs(QStringLiteral("RenderSettingsTabs"));
        // Drive the dialog width from HERE, not from dialog.setMinimumWidth():
        // the rootLayout's SetFixedSize constraint sizes the dialog to its
        // content sizeHint and ignores the dialog's own minimum width, but it
        // DOES honor a child widget's minimum width (QWidgetItem expands the
        // hint up to the child minimum). Setting the tab's minimum width
        // reliably widens the dialog so the two CJK checkbox columns
        // ("显示左下角时间戳" / "暂停时显示判定区") stop clipping. The video form
        // uses ExpandingFieldsGrow, so the extra width flows into those cells.
        videoSettingsTabs->setMinimumWidth(420);
        // documentMode left at the default so the QTabWidget::pane
        // stylesheet (rounded bottom corners + no top border) actually
        // takes effect; documentMode=true would skip drawing the pane
        // frame and re-expose the global hair line above the tab strip.
        videoSettingsTabs->addTab(videoGroup,
            UiText::text(QStringLiteral("dialog.render_settings.video_group")));
        videoSettingsTabs->addTab(gameplayGroup,
            UiText::text(QStringLiteral("dialog.render_settings.gameplay_group")));
        // 皮肤 / 判定线 / HUD 字体 / 片头音效 moved to the shared 皮肤 popup; the
        // former 音乐 / 字体 tabs are gone. 预览刷新率 lives on the 性能 tab.
        videoSettingsTabs->addTab(performanceGroup,
            UiText::text(QStringLiteral("dialog.render_settings.performance_group")));
        // Height companion to setMinimumWidth above: without this the 游戏 tab
        // (the tallest — flow / judge / slide / 中心显示 rows) clips its bottom
        // row because QStyleSheetStyle omits the pane padding from the tab
        // sizeHint (W2). Pins the tab tall enough for the tallest page.
        miacode::ui::pinTabWidgetToContentHeight(videoSettingsTabs, &dialog, rootLayout);
    }
    auto* buttonBox = dialog.buttonBox();
    QPushButton* saveLocalAudioPresetButton = nullptr;
    QPushButton* applyLocalAudioPresetButton = nullptr;
    if (includeAudioSettings) {
        saveLocalAudioPresetButton = miacode::ui::createDialogPushButton(
            UiText::text(QStringLiteral("dialog.render_settings.button.set_software_default_audio")),
            buttonBox);
        buttonBox->addButton(saveLocalAudioPresetButton, QDialogButtonBox::ActionRole);
        applyLocalAudioPresetButton = miacode::ui::createDialogPushButton(
            UiText::text(QStringLiteral("dialog.render_settings.button.restore_project_default")),
            buttonBox);
        buttonBox->addButton(applyLocalAudioPresetButton, QDialogButtonBox::ActionRole);
    }
    dialog.addCloseButton(UiText::text(QStringLiteral("dialog.render_settings.button.close")), true);

    auto* audioApplyTimer = new QTimer(&dialog);
    audioApplyTimer->setSingleShot(true);
    audioApplyTimer->setInterval(220);
    QString pendingAudition;
    auto* dialogAuditionRuntime = new QtPreviewSfxRuntime(&dialog);
    QString dialogAuditionSfxDir;
    QString dialogAuditionPendingKind;
    quint64 dialogAuditionReloadAssetGeneration = 0;
    quint64 dialogAuditionReloadSequence = 0;

    auto queueAudioApply = [audioApplyTimer, &pendingAudition](const QString& audition) {
        pendingAudition = audition;
        audioApplyTimer->start();
    };

    const auto playDialogLocalSfxAudition = [
        this,
        dialogAuditionRuntime,
        &dialogAuditionSfxDir,
        &dialogAuditionPendingKind,
        &dialogAuditionReloadAssetGeneration,
        &dialogAuditionReloadSequence
    ](const QString& audition) {
        if (audition.isEmpty()) {
            return false;
        }
        // Don't audition over a running preview: the user already hears the
        // real SFX from live playback, so a second (dialog-local) runtime
        // playing the same sample just doubles/garbles it. Audition is only
        // meaningful when the preview is idle.
        if (state_.qtPreviewPlaying_) {
            return false;
        }
        QString resolvedKind = previewSfxNormalizedKind(audition);
        if (resolvedKind == QStringLiteral("break_slide")) {
            resolvedKind = QStringLiteral("break_slide_start");
        }
        const QString sfxDir = miacode::preview_sfx::resolveSfxDirectory();
        if (sfxDir.isEmpty()) {
            return false;
        }
        const QString resolvedSfxDir = QDir::cleanPath(sfxDir);
        if (dialogAuditionSfxDir != resolvedSfxDir || !dialogAuditionRuntime->audioEngineInitialized()) {
            dialogAuditionRuntime->setWarmupResolvedPaths(QString(), QString(), resolvedSfxDir);
            const QtPreviewSfxRuntime::AssetSubmission reload =
                dialogAuditionRuntime->reloadAssets(owner_.previewAudioSettings_);
            dialogAuditionSfxDir = resolvedSfxDir;
            dialogAuditionPendingKind = resolvedKind;
            dialogAuditionReloadAssetGeneration = reload.post.accepted
                ? reload.identity.assetGeneration
                : 0;
            dialogAuditionReloadSequence = reload.post.accepted
                ? reload.identity.sequence
                : 0;
            return false;
        } else {
            dialogAuditionRuntime->applyLevels(owner_.previewAudioSettings_);
        }
        dialogAuditionRuntime->stopAll();
        return dialogAuditionRuntime->audition(resolvedKind);
    };

    connect(dialogAuditionRuntime,
            &QtPreviewSfxRuntime::commandCompleted,
            &dialog,
            [this,
             dialogAuditionRuntime,
             &dialogAuditionPendingKind,
             &dialogAuditionReloadAssetGeneration,
             &dialogAuditionReloadSequence](const QtPreviewSfxRuntime::Completion& completion) {
                using namespace miacode::preview_audio;
                if (completion.kind != CommandKind::ReloadAssets
                    || completion.identity.sequence != dialogAuditionReloadSequence
                    || !acceptsAssetCompletion(dialogAuditionReloadAssetGeneration, completion)) {
                    return;
                }
                dialogAuditionReloadAssetGeneration = 0;
                dialogAuditionReloadSequence = 0;
                const QString auditionKind = std::exchange(dialogAuditionPendingKind, QString());
                if (!completion.success || auditionKind.isEmpty() || state_.qtPreviewPlaying_) {
                    return;
                }
                dialogAuditionRuntime->applyLevels(owner_.previewAudioSettings_);
                dialogAuditionRuntime->stopAll();
                dialogAuditionRuntime->audition(auditionKind);
            });

    const QString muteAudioButtonTooltip = UiText::text(QStringLiteral("dialog.render_settings.audio.button.mute"));
    const QString unmuteAudioButtonTooltip = UiText::text(QStringLiteral("dialog.render_settings.audio.button.unmute"));
    const QString muteButtonStyleSheet = QStringLiteral(
        "QToolButton { border: none; background: transparent; padding: 0; margin: 0; }"
        "QToolButton:hover { border: none; background: transparent; }"
        "QToolButton:pressed { border: none; background: transparent; }"
        "QToolButton:disabled { border: none; background: transparent; }"
    );

    const auto makeAudioMuteIcon = [](bool muted) {
        constexpr int kExtent = 16;
        QPixmap pixmap(kExtent, kExtent);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        // Theme-aware foreground: iconPrimary is the brand's "appropriate
        // tint for an icon on the dialog backdrop" — dark on light themes,
        // light on dark themes — so the speaker reads correctly when the
        // user has dark mode enabled. Previously hard-coded #101010 which
        // disappeared into the dark backdrop.
        const QColor iconColor = UiTheme::colors().iconPrimary;
        painter.setBrush(iconColor);
        painter.drawPolygon(QPolygonF{
            QPointF(1.5, 5.5),
            QPointF(4.4, 5.5),
            QPointF(8.0, 2.8),
            QPointF(8.0, 13.2),
            QPointF(4.4, 10.5),
            QPointF(1.5, 10.5),
        });

        QPen pen(iconColor);
        pen.setWidthF(1.35);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        if (muted) {
            painter.drawLine(QPointF(10.1, 5.5), QPointF(13.4, 10.5));
            painter.drawLine(QPointF(13.4, 5.5), QPointF(10.1, 10.5));
        } else {
            QPainterPath waveNear;
            waveNear.moveTo(9.3, 6.2);
            waveNear.quadTo(10.8, 8.0, 9.3, 9.8);
            painter.drawPath(waveNear);

            QPainterPath waveFar;
            waveFar.moveTo(10.8, 4.5);
            waveFar.quadTo(13.9, 8.0, 10.8, 11.5);
            painter.drawPath(waveFar);
        }

        return QIcon(pixmap);
    };

    const auto syncAudioControlsFromCurrentSettings = [
        this,
        masterSlider,
        masterLabel,
        masterMuteButton,
        masterAudioLabelText,
        muteAudioButtonTooltip,
        unmuteAudioButtonTooltip,
        muteButtonStyleSheet,
        makeAudioMuteIcon,
        bgmSlider,
        bgmLabel,
        bgmMuteButton,
        answerSlider,
        answerLabel,
        answerMuteButton,
        judgeSlider,
        judgeLabel,
        judgeMuteButton,
        breakSlider,
        breakLabel,
        breakMuteButton,
        breakSlideSlider,
        breakSlideLabel,
        breakSlideMuteButton,
        slideSlider,
        slideLabel,
        slideMuteButton,
        breakSlideTailCheerCheck,
        exSlider,
        exLabel,
        exMuteButton,
        touchSlider,
        touchLabel,
        touchMuteButton,
        fireworkSlider,
        fireworkLabel,
        fireworkMuteButton,
        bgmAudioLabelText,
        answerAudioLabelText,
        judgeAudioLabelText,
        exAudioLabelText,
        breakAudioLabelText,
        breakSlideAudioLabelText,
        slideAudioLabelText,
        touchAudioLabelText,
        fireworkAudioLabelText
    ]() {
        owner_.previewAudioSettings_.normalize();
        const auto syncAudioRow = [](QSlider* slider, QLabel* label, int valuePercent) {
            if (slider == nullptr || label == nullptr) {
                return;
            }
            const QSignalBlocker blocker(slider);
            slider->setValue(valuePercent);
            label->setText(QString::number(valuePercent) + "%");
        };
        syncAudioRow(masterSlider, masterLabel, owner_.previewAudioSettings_.globalPercent());
        if (masterMuteButton != nullptr) {
            masterMuteButton->setStyleSheet(muteButtonStyleSheet);
            masterMuteButton->setIcon(makeAudioMuteIcon(owner_.previewAudioSettings_.globalMuted()));
            masterMuteButton->setToolTip(
                (owner_.previewAudioSettings_.globalMuted() ? unmuteAudioButtonTooltip : muteAudioButtonTooltip)
                    .arg(masterAudioLabelText)
            );
        }
        const auto syncPerChannelMuteButton = [
            &makeAudioMuteIcon,
            &muteButtonStyleSheet,
            &muteAudioButtonTooltip,
            &unmuteAudioButtonTooltip
        ](QToolButton* button, bool muted, const QString& labelText) {
            if (button == nullptr) {
                return;
            }
            button->setStyleSheet(muteButtonStyleSheet);
            button->setIcon(makeAudioMuteIcon(muted));
            button->setToolTip((muted ? unmuteAudioButtonTooltip : muteAudioButtonTooltip).arg(labelText));
        };
        syncAudioRow(bgmSlider, bgmLabel, owner_.previewAudioSettings_.trackPercent());
        syncAudioRow(answerSlider, answerLabel, owner_.previewAudioSettings_.answerPercent());
        syncAudioRow(judgeSlider, judgeLabel, owner_.previewAudioSettings_.tapPercent());
        syncAudioRow(exSlider, exLabel, owner_.previewAudioSettings_.exPercent());
        syncAudioRow(breakSlider, breakLabel, owner_.previewAudioSettings_.breakPercent());
        syncAudioRow(breakSlideSlider, breakSlideLabel, owner_.previewAudioSettings_.breakSlidePercent());
        syncAudioRow(slideSlider, slideLabel, owner_.previewAudioSettings_.slidePercent());
        if (breakSlideTailCheerCheck != nullptr) {
            const QSignalBlocker blocker(breakSlideTailCheerCheck);
            breakSlideTailCheerCheck->setChecked(owner_.previewAudioSettings_.breakSlideTailCheerMuted);
        }
        syncAudioRow(touchSlider, touchLabel, owner_.previewAudioSettings_.touchPercent());
        syncAudioRow(fireworkSlider, fireworkLabel, owner_.previewAudioSettings_.fireworkPercent());
        syncPerChannelMuteButton(bgmMuteButton, owner_.previewAudioSettings_.trackMuted(), bgmAudioLabelText);
        syncPerChannelMuteButton(answerMuteButton, owner_.previewAudioSettings_.answerMuted(), answerAudioLabelText);
        syncPerChannelMuteButton(judgeMuteButton, owner_.previewAudioSettings_.tapMuted(), judgeAudioLabelText);
        syncPerChannelMuteButton(exMuteButton, owner_.previewAudioSettings_.exMuted(), exAudioLabelText);
        syncPerChannelMuteButton(breakMuteButton, owner_.previewAudioSettings_.breakMuted(), breakAudioLabelText);
        syncPerChannelMuteButton(
            breakSlideMuteButton,
            owner_.previewAudioSettings_.breakSlideMuted(),
            breakSlideAudioLabelText
        );
        syncPerChannelMuteButton(slideMuteButton, owner_.previewAudioSettings_.slideMuted(), slideAudioLabelText);
        syncPerChannelMuteButton(touchMuteButton, owner_.previewAudioSettings_.touchMuted(), touchAudioLabelText);
        syncPerChannelMuteButton(fireworkMuteButton, owner_.previewAudioSettings_.fireworkMuted(), fireworkAudioLabelText);
    };

    const auto commitAudioSettingsChange = [
        this,
        syncAudioControlsFromCurrentSettings,
        queueAudioApply
    ](const QString& audition) {
        owner_.previewAudioSettings_.normalize();
        syncAudioControlsFromCurrentSettings();
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.savePortableState();
        queueAudioApply(audition);
    };

    const auto connectAudioSlider = [
        this,
        &dialog,
        commitAudioSettingsChange
    ](QSlider* slider, void (PreviewAudioSettings::*setter)(int), const QString& audition) {
        connect(slider, &QSlider::valueChanged, &dialog, [this, setter, audition, commitAudioSettingsChange](int value) {
            (owner_.previewAudioSettings_.*setter)(value);
            commitAudioSettingsChange(audition);
        });
    };

    syncAudioControlsFromCurrentSettings();

    connectAudioSlider(masterSlider, &PreviewAudioSettings::setGlobalPercent, "answer");
    connect(masterMuteButton, &QToolButton::clicked, &dialog, [this, commitAudioSettingsChange]() {
        const bool muteAll = !owner_.previewAudioSettings_.globalMuted();
        if (muteAll) {
            owner_.previewAudioSettings_.toggleGlobalMuted();
            if (!owner_.previewAudioSettings_.trackMuted()) {
                owner_.previewAudioSettings_.toggleTrackMuted();
            }
            if (!owner_.previewAudioSettings_.answerMuted()) {
                owner_.previewAudioSettings_.toggleAnswerMuted();
            }
            if (!owner_.previewAudioSettings_.tapMuted()) {
                owner_.previewAudioSettings_.toggleTapMuted();
            }
            if (!owner_.previewAudioSettings_.exMuted()) {
                owner_.previewAudioSettings_.toggleExMuted();
            }
            if (!owner_.previewAudioSettings_.breakMuted()) {
                owner_.previewAudioSettings_.toggleBreakMuted();
            }
            if (!owner_.previewAudioSettings_.breakSlideMuted()) {
                owner_.previewAudioSettings_.toggleBreakSlideMuted();
            }
            if (!owner_.previewAudioSettings_.slideMuted()) {
                owner_.previewAudioSettings_.toggleSlideMuted();
            }
            if (!owner_.previewAudioSettings_.touchMuted()) {
                owner_.previewAudioSettings_.toggleTouchMuted();
            }
            if (!owner_.previewAudioSettings_.fireworkMuted()) {
                owner_.previewAudioSettings_.toggleFireworkMuted();
            }
        } else {
            owner_.previewAudioSettings_.toggleGlobalMuted();
            if (owner_.previewAudioSettings_.trackMuted()) {
                owner_.previewAudioSettings_.toggleTrackMuted();
            }
            if (owner_.previewAudioSettings_.answerMuted()) {
                owner_.previewAudioSettings_.toggleAnswerMuted();
            }
            if (owner_.previewAudioSettings_.tapMuted()) {
                owner_.previewAudioSettings_.toggleTapMuted();
            }
            if (owner_.previewAudioSettings_.exMuted()) {
                owner_.previewAudioSettings_.toggleExMuted();
            }
            if (owner_.previewAudioSettings_.breakMuted()) {
                owner_.previewAudioSettings_.toggleBreakMuted();
            }
            if (owner_.previewAudioSettings_.breakSlideMuted()) {
                owner_.previewAudioSettings_.toggleBreakSlideMuted();
            }
            if (owner_.previewAudioSettings_.slideMuted()) {
                owner_.previewAudioSettings_.toggleSlideMuted();
            }
            if (owner_.previewAudioSettings_.touchMuted()) {
                owner_.previewAudioSettings_.toggleTouchMuted();
            }
            if (owner_.previewAudioSettings_.fireworkMuted()) {
                owner_.previewAudioSettings_.toggleFireworkMuted();
            }
        }
        commitAudioSettingsChange(owner_.previewAudioSettings_.globalMuted() ? QString() : QStringLiteral("answer"));
    });
    connectAudioSlider(bgmSlider, &PreviewAudioSettings::setTrackPercent, QString());
    connectAudioSlider(answerSlider, &PreviewAudioSettings::setAnswerPercent, "answer");
    connectAudioSlider(judgeSlider, &PreviewAudioSettings::setTapPercent, "judge");
    connectAudioSlider(exSlider, &PreviewAudioSettings::setExPercent, "ex");
    connectAudioSlider(breakSlider, &PreviewAudioSettings::setBreakPercent, "break");
    connectAudioSlider(breakSlideSlider, &PreviewAudioSettings::setBreakSlidePercent, "break_slide");
    connectAudioSlider(slideSlider, &PreviewAudioSettings::setSlidePercent, "slide");
    connectAudioSlider(touchSlider, &PreviewAudioSettings::setTouchPercent, "touch");
    connectAudioSlider(fireworkSlider, &PreviewAudioSettings::setFireworkPercent, "firework");
    connect(breakSlideTailCheerCheck, &QCheckBox::toggled, &dialog, [this, commitAudioSettingsChange](bool checked) {
        owner_.breakSlideTailCheerMutedPreference_ = checked;
        owner_.previewAudioSettings_.breakSlideTailCheerMuted = checked;
        owner_.softwarePreviewAudioSettings_.breakSlideTailCheerMuted = checked;
        commitAudioSettingsChange(QString());
    });
    const auto connectPerChannelMute = [
        this,
        &dialog,
        commitAudioSettingsChange
    ](QToolButton* button, void (PreviewAudioSettings::*toggleMuted)(), bool (PreviewAudioSettings::*mutedGetter)() const, const QString& audition) {
        connect(button, &QToolButton::clicked, &dialog, [this, toggleMuted, mutedGetter, audition, commitAudioSettingsChange]() {
            (owner_.previewAudioSettings_.*toggleMuted)();
            const bool mutedNow = (owner_.previewAudioSettings_.*mutedGetter)();
            commitAudioSettingsChange(mutedNow ? QString() : audition);
        });
    };
    connectPerChannelMute(bgmMuteButton, &PreviewAudioSettings::toggleTrackMuted, &PreviewAudioSettings::trackMuted, QString());
    connectPerChannelMute(answerMuteButton, &PreviewAudioSettings::toggleAnswerMuted, &PreviewAudioSettings::answerMuted, "answer");
    connectPerChannelMute(judgeMuteButton, &PreviewAudioSettings::toggleTapMuted, &PreviewAudioSettings::tapMuted, "judge");
    connectPerChannelMute(exMuteButton, &PreviewAudioSettings::toggleExMuted, &PreviewAudioSettings::exMuted, "ex");
    connectPerChannelMute(breakMuteButton, &PreviewAudioSettings::toggleBreakMuted, &PreviewAudioSettings::breakMuted, "break");
    connectPerChannelMute(
        breakSlideMuteButton,
        &PreviewAudioSettings::toggleBreakSlideMuted,
        &PreviewAudioSettings::breakSlideMuted,
        "break_slide"
    );
    connectPerChannelMute(slideMuteButton, &PreviewAudioSettings::toggleSlideMuted, &PreviewAudioSettings::slideMuted, "slide");
    connectPerChannelMute(touchMuteButton, &PreviewAudioSettings::toggleTouchMuted, &PreviewAudioSettings::touchMuted, "touch");
    connectPerChannelMute(fireworkMuteButton, &PreviewAudioSettings::toggleFireworkMuted, &PreviewAudioSettings::fireworkMuted, "firework");
    if (saveLocalAudioPresetButton != nullptr) {
        connect(saveLocalAudioPresetButton, &QPushButton::clicked, &dialog, [this]() {
            owner_.previewAudioSettings_.normalize();
            owner_.softwarePreviewAudioSettings_ = previewAudioSettingsWithBreakSlideTailCheerPreference(
                owner_.previewAudioSettings_, owner_.breakSlideTailCheerMutedPreference_);
            owner_.savePortableState();
        });
    }
    if (applyLocalAudioPresetButton != nullptr) {
        connect(
            applyLocalAudioPresetButton,
            &QPushButton::clicked,
            &dialog,
            [this, audioApplyTimer, &pendingAudition, syncAudioControlsFromCurrentSettings]() {
                owner_.previewAudioSettings_ = previewAudioSettingsWithBreakSlideTailCheerPreference(
                    owner_.softwarePreviewAudioSettings_, owner_.breakSlideTailCheerMutedPreference_);
                syncAudioControlsFromCurrentSettings();
                owner_.applyPreviewAudioSettingsToRuntime();
                owner_.savePortableState();
                if (audioApplyTimer->isActive()) {
                    audioApplyTimer->stop();
                }
                pendingAudition.clear();
            }
        );
    }

    connect(audioApplyTimer, &QTimer::timeout, &dialog, [this, audioApplyTimer, masterSlider, bgmSlider, answerSlider, judgeSlider, breakSlider, breakSlideSlider, slideSlider, exSlider, touchSlider, fireworkSlider, &pendingAudition, playDialogLocalSfxAudition]() {
        if (masterSlider->isSliderDown()
            || bgmSlider->isSliderDown()
            || answerSlider->isSliderDown()
            || judgeSlider->isSliderDown()
            || breakSlider->isSliderDown()
            || breakSlideSlider->isSliderDown()
            || slideSlider->isSliderDown()
            || exSlider->isSliderDown()
            || touchSlider->isSliderDown()
            || fireworkSlider->isSliderDown()) {
            audioApplyTimer->start();
            return;
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && playDialogLocalSfxAudition(pendingAudition);
        Q_UNUSED(handledLocally);
        pendingAudition.clear();
    });
    connect(&dialog, &QDialog::finished, &dialog, [this, audioApplyTimer, &pendingAudition, playDialogLocalSfxAudition]() {
        if (!audioApplyTimer->isActive()) {
            return;
        }
        audioApplyTimer->stop();
        const bool handledLocally = !pendingAudition.isEmpty()
            && playDialogLocalSfxAudition(pendingAudition);
        Q_UNUSED(handledLocally);
        pendingAudition.clear();
    });

    connect(outerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, outerBrightnessLabel](int value) {
        owner_.previewBackgroundBrightnessOuter_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        outerBrightnessLabel->setText(QString::number(value) + "%");
        owner_.applyPreviewStageMediaRouteVisualSettings();
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundBrightnessOuter(owner_.previewBackgroundBrightnessOuter_);
        }
        owner_.savePortableState();
    });
    connect(innerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, innerBrightnessLabel](int value) {
        owner_.previewBackgroundBrightnessInner_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        innerBrightnessLabel->setText(QString::number(value) + "%");
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundBrightnessInner(owner_.previewBackgroundBrightnessInner_);
        }
        owner_.savePortableState();
    });
    connect(layoutSquareScaleSlider, &QSlider::valueChanged, &dialog, [this, layoutSquareScaleLabel](int value) {
        owner_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(value) / 100.0);
        layoutSquareScaleLabel->setText(QString::number(qRound(owner_.previewLayoutSquareScale_ * 100.0)) + "%");
        owner_.applyPreviewStageMediaRouteVisualSettings();
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setLayoutSquareScale(owner_.previewLayoutSquareScale_);
        }
        owner_.savePortableState();
    });
    connect(smoothBrightnessCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewSmoothBrightness_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSmoothBrightness(owner_.previewSmoothBrightness_);
        }
        owner_.savePortableState();
    });
    connect(timestampCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewShowTimestamp_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setShowTimestamp(owner_.previewShowTimestamp_);
        }
        owner_.savePortableState();
    });
    connect(forceLabeledJudgeLineWhenPausedCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewForceLabeledJudgeLineWhenPaused_ = checked;
        owner_.applyEffectivePreviewOutlineVariantToCanvas();
        owner_.applyPreviewStageMediaRouteVisualSettings();
        owner_.savePortableState();
    });

    connect(touchPadAuthoringShortcutCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewTouchPadAuthoringShortcutEnabled_ = checked;
        owner_.applyEffectivePreviewOutlineVariantToCanvas();
        owner_.savePortableState();
    });

    connect(debugCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewShowDebugInfo_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setShowDebugInfo(owner_.previewShowDebugInfo_);
        }
        owner_.savePortableState();
    });
    dialog.adjustSize();
    dialog.exec();
}
