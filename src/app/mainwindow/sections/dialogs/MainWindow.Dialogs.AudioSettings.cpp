#include "MainWindow.DialogsSection.h"
#include "../../MainWindowShared.h"
#include "../window/MainWindow.WindowSection.h"

#include "AppVersion.h"
#include "QtPreviewSfxRuntime.h"
#include "DialogLocalization.h"
#include "EditableValueLabel.h"
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

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(title);
    dialog.setModal(true);
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
    dialog.setStyleSheet(UiTheme::settingsDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    const auto createDialogMenuButton = [](QWidget* parent, const QString& text) {
        auto* button = new QToolButton(parent);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
        button->setText(text);
        return button;
    };
    const auto flowSpeedValueLabel = [](double flowSpeed) {
        const double snapped = qRound(flowSpeed * 4.0) / 4.0;
        const double roundedOneDecimal = qRound(snapped * 10.0) / 10.0;
        const bool useSingleDecimal = qAbs(snapped - roundedOneDecimal) < 0.001;
        return QString::number(snapped, 'f', useSingleDecimal ? 1 : 2);
    };
    const auto addDialogMenuChoice = [](QMenu* menu, const QString& text, const std::function<void()>& onTriggered, bool italic = false) {
        auto* action = new QWidgetAction(menu);
        auto* button = new QToolButton(menu);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(text);
        QFont buttonFont = button->font();
        buttonFont.setItalic(italic);
        button->setFont(buttonFont);
        button->setCursor(Qt::PointingHandCursor);
        const auto& c = UiTheme::colors();
        button->setStyleSheet(
            QStringLiteral(
                "QToolButton {"
                " color: %1;"
                " background: transparent;"
                " border: none;"
                " padding: 6px 20px 6px 12px;"
                " text-align: left;"
                "}"
                "QToolButton:hover {"
                " background: %2;"
                " border-radius: 6px;"
                "}"
            )
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(c.menuHoverBg.name(QColor::HexRgb))
        );
        QObject::connect(button, &QToolButton::clicked, menu, [action, menu, onTriggered]() {
            if (onTriggered) {
                onTriggered();
            }
            action->trigger();
            menu->close();
        });
        action->setDefaultWidget(button);
        menu->addAction(action);
    };

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);
    rootLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto* audioGroup = new QGroupBox(uiText("dialog.render_settings.audio_group", "Audio"), &dialog);
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

    const QString masterAudioLabelText = uiText("dialog.render_settings.audio.global", "Global Volume");
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
    const QString bgmAudioLabelText = uiText("dialog.render_settings.audio.track", "Track Volume");
    QSlider* bgmSlider = nullptr;
    QLabel* bgmLabel = nullptr;
    QToolButton* bgmMuteButton = nullptr;
    addAudioRow(bgmAudioLabelText, owner_.previewAudioSettings_.trackPercent(), &bgmSlider, &bgmLabel, &bgmMuteButton);
    const QString answerAudioLabelText = uiText("dialog.render_settings.audio.answer", "Answer Volume");
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
    const QString judgeAudioLabelText = uiText("dialog.render_settings.audio.tap", "Tap Volume");
    QSlider* judgeSlider = nullptr;
    QLabel* judgeLabel = nullptr;
    QToolButton* judgeMuteButton = nullptr;
    addAudioRow(judgeAudioLabelText, owner_.previewAudioSettings_.tapPercent(), &judgeSlider, &judgeLabel, &judgeMuteButton);
    const QString exAudioLabelText = uiText("dialog.render_settings.audio.ex", "EX Volume");
    QSlider* exSlider = nullptr;
    QLabel* exLabel = nullptr;
    QToolButton* exMuteButton = nullptr;
    addAudioRow(exAudioLabelText, owner_.previewAudioSettings_.exPercent(), &exSlider, &exLabel, &exMuteButton);
    const QString breakAudioLabelText = uiText("dialog.render_settings.audio.break", "Break Volume");
    QSlider* breakSlider = nullptr;
    QLabel* breakLabel = nullptr;
    QToolButton* breakMuteButton = nullptr;
    addAudioRow(breakAudioLabelText, owner_.previewAudioSettings_.breakPercent(), &breakSlider, &breakLabel, &breakMuteButton);
    const QString breakSlideAudioLabelText = uiText("dialog.render_settings.audio.break_slide", "Break Slide Volume");
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
    const QString slideAudioLabelText = uiText("dialog.render_settings.audio.slide", "Slide Volume");
    QSlider* slideSlider = nullptr;
    QLabel* slideLabel = nullptr;
    QToolButton* slideMuteButton = nullptr;
    addAudioRow(slideAudioLabelText, owner_.previewAudioSettings_.slidePercent(), &slideSlider, &slideLabel, &slideMuteButton);
    auto* breakSlideTailCheerCheck = new QCheckBox(
        uiText("dialog.render_settings.audio.break_slide_tail_cheer_mute", "Disable breakslide tail cheer"),
        audioGroup);
    breakSlideTailCheerCheck->setChecked(owner_.previewAudioSettings_.breakSlideTailCheerMuted);
    const QString touchAudioLabelText = uiText("dialog.render_settings.audio.touch", "Touch Volume");
    QSlider* touchSlider = nullptr;
    QLabel* touchLabel = nullptr;
    QToolButton* touchMuteButton = nullptr;
    addAudioRow(touchAudioLabelText, owner_.previewAudioSettings_.touchPercent(), &touchSlider, &touchLabel, &touchMuteButton);
    const QString fireworkAudioLabelText = uiText("dialog.render_settings.audio.firework", "Firework Volume");
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

    auto* videoGroup = new QGroupBox(uiText("dialog.render_settings.video_group", "Video"), &dialog);
    auto* videoFormLayout = new QFormLayout(videoGroup);
    videoFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    videoFormLayout->setHorizontalSpacing(10);
    videoFormLayout->setVerticalSpacing(8);
    auto* gameplayGroup = new QGroupBox(uiText("dialog.render_settings.gameplay_group", "Gameplay"), &dialog);
    auto* gameplayLayout = new QGridLayout(gameplayGroup);
    gameplayLayout->setContentsMargins(10, 8, 10, 8);
    gameplayLayout->setHorizontalSpacing(10);
    gameplayLayout->setVerticalSpacing(8);
    gameplayLayout->setColumnStretch(0, 1);
    gameplayLayout->setColumnStretch(1, 1);

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
        .arg(uiText(
            "dialog.render_settings.preview.canvas_frame_rate.display",
            "Display Refresh Rate"
        ))
        .arg(QString::number(detectedRefreshRate, 'f', detectedRefreshRate >= 100.0 ? 0 : 1));
    QList<CanvasFrameRateOption> canvasFrameRateOptions;
    canvasFrameRateOptions.append({
        PreviewCanvasFrameRateMode::Fps60,
        uiText("dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS"),
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
            uiText("dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS"),
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
    auto* canvasFrameRateButton = createDialogMenuButton(videoGroup, selectedCanvasFrameRateLabel);
    canvasFrameRateButton->setFixedHeight(tapFlowSpeedEdit->sizeHint().height());
    canvasFrameRateButton->setStyleSheet(
        UiTheme::dialogMenuButtonStyleSheet()
        + QStringLiteral("QToolButton { text-align: center; padding: 2px 22px 2px 10px; }")
    );
    auto* canvasFrameRateMenu = new QMenu(canvasFrameRateButton);
    styleRoundedMenu(*canvasFrameRateMenu);
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        const PreviewCanvasFrameRateMode mode = option.mode;
        const QString label = option.label;
        addDialogMenuChoice(canvasFrameRateMenu, label, [this, canvasFrameRateButton, mode, label]() {
            canvasFrameRateButton->setText(label);
            owner_.setPreviewCanvasFrameRateMode(mode, true);
        });
    }
    canvasFrameRateButton->setMenu(canvasFrameRateMenu);

    const QString scaleFillLabel = uiText("dialog.render_settings.video.scale.fill", "Fill (crop if needed)");
    const QString scaleFitLabel = uiText("dialog.render_settings.video.scale.fit", "Fit (keep full image, may letterbox)");
    const QString scaleSquareFitLabel = uiText(
        "dialog.render_settings.video.scale.square_fit",
        "1:1 Fit (center square)");
    const QString importSkinLabel = uiText("dialog.render_settings.video.skin.import", "Import...");
    const QString slideStackOrderDxLabel = uiText(
        "dialog.render_settings.gameplay.slide_stack_order.dx_style",
        "DX Style"
    );
    const QString slideStackOrderFinaleLabel = uiText(
        "dialog.render_settings.gameplay.slide_stack_order.finale_style",
        "FiNALE Style"
    );
    const auto slideStackOrderLabelForValue = [slideStackOrderDxLabel, slideStackOrderFinaleLabel](bool earlierOnTop) {
        return earlierOnTop ? slideStackOrderDxLabel : slideStackOrderFinaleLabel;
    };
    const QString currentSkinButtonLabel = owner_.previewSkinDisplayName(owner_.previewSkinDirectoryName_);
    auto* skinButton = createDialogMenuButton(
        gameplayGroup,
        currentSkinButtonLabel
    );
    skinButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* skinMenu = new QMenu(skinButton);
    styleRoundedMenu(*skinMenu);
    for (const QString& skinDirectoryName : owner_.availablePreviewSkinDirectoryNames()) {
        const QString skinLabel = owner_.previewSkinDisplayName(skinDirectoryName);
        addDialogMenuChoice(skinMenu, skinLabel, [this, skinButton, skinDirectoryName, skinLabel]() {
            owner_.previewSkinDirectoryName_ = skinDirectoryName;
            owner_.previewSkinVariant_ =
                skinDirectoryName.compare(QStringLiteral("skinDX"), Qt::CaseInsensitive) == 0
                    ? PreviewSkinVariant::Dx
                    : PreviewSkinVariant::Standard;
            skinButton->setText(skinLabel);
            owner_.applyPreviewSkinDirectoryToSurfaces();
            owner_.savePortableState();
        });
    }
    if (!skinMenu->actions().isEmpty()) {
        skinMenu->addSeparator();
    }
    addDialogMenuChoice(skinMenu, importSkinLabel, [this]() {
        const QString skinRoot = owner_.resolvePreviewSkinRootDir();
        if (!skinRoot.isEmpty()) {
            QDir().mkpath(skinRoot);
            QDesktopServices::openUrl(QUrl::fromLocalFile(skinRoot));
        }
    });
    skinButton->setMenu(skinMenu);
    const QString disabledLabel = uiText("dialog.render_settings.option.disabled", "Disabled");
    const QString slideJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.slide", "slide");
    const QString tapJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.tap", "tap");
    const QString touchJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.touch", "touch");
    const auto judgeEffectButtonLabel = [
        this,
        slideJudgeChoiceLabel,
        tapJudgeChoiceLabel,
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
        if (owner_.muriRenderOptions_.showChartReviewTouchJudgeOverlay) {
            parts.append(touchJudgeChoiceLabel);
        }
        return parts.isEmpty() ? disabledLabel : parts.join(QStringLiteral(", "));
    };
    auto* judgeEffectButton = createDialogMenuButton(gameplayGroup, judgeEffectButtonLabel());
    judgeEffectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* judgeEffectMenu = new QMenu(judgeEffectButton);
    styleRoundedMenu(*judgeEffectMenu);
    // QCheckBox-in-QWidgetAction so the menu stays open during multi-selection (regular QActions auto-close).
    // We hide the native indicator and prefix the label with √/× — visually matches the user's request to
    // surface state via a tick / cross rather than a checkbox glyph, while preserving QCheckBox's click
    // semantics (toggled signal, keyboard focus, accessibility role).
    const auto judgeEffectChoiceText = [](const QString& label, bool enabled) {
        return QStringLiteral("%1  %2")
            .arg(enabled ? QStringLiteral("√") : QStringLiteral("×"), label);
    };
    const auto addJudgeEffectChoice = [
        this,
        &dialog,
        judgeEffectButton,
        judgeEffectButtonLabel,
        judgeEffectMenu,
        judgeEffectChoiceText
    ](const QString& label, bool MuriRenderOptions::*memberPtr) {
        auto* action = new QWidgetAction(judgeEffectMenu);
        const bool initialChecked = owner_.muriRenderOptions_.*memberPtr;
        auto* checkbox = new QCheckBox(judgeEffectChoiceText(label, initialChecked), judgeEffectMenu);
        checkbox->setChecked(initialChecked);
        checkbox->setCursor(Qt::PointingHandCursor);
        const auto& c = UiTheme::colors();
        checkbox->setStyleSheet(
            QStringLiteral(
                "QCheckBox {"
                " color: %1;"
                " background: transparent;"
                " padding: 6px 20px 6px 12px;"
                " spacing: 0px;"
                "}"
                "QCheckBox::indicator {"
                " width: 0px;"
                " height: 0px;"
                " margin: 0px;"
                " padding: 0px;"
                "}"
                "QCheckBox:hover {"
                " background: %2;"
                " border-radius: 6px;"
                "}"
            )
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(c.menuHoverBg.name(QColor::HexRgb))
        );
        QObject::connect(
            checkbox,
            &QCheckBox::toggled,
            &dialog,
            [this, judgeEffectButton, judgeEffectButtonLabel, judgeEffectChoiceText,
             checkbox, label, memberPtr](bool checked) {
                if (owner_.muriRenderOptions_.*memberPtr == checked) {
                    return;
                }
                owner_.muriRenderOptions_.*memberPtr = checked;
                checkbox->setText(judgeEffectChoiceText(label, checked));
                judgeEffectButton->setText(judgeEffectButtonLabel());
                owner_.applyMuriRenderOptions();
                owner_.savePortableState();
            }
        );
        action->setDefaultWidget(checkbox);
        judgeEffectMenu->addAction(action);
    };
    addJudgeEffectChoice(slideJudgeChoiceLabel, &MuriRenderOptions::showChartReviewSlideJudgeOverlay);
    addJudgeEffectChoice(tapJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTapJudgeOverlay);
    addJudgeEffectChoice(touchJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTouchJudgeOverlay);
    judgeEffectButton->setMenu(judgeEffectMenu);
    const QString judgeLinePointLabel = uiText("dialog.render_settings.gameplay.judge_line.point", "Point");
    const QString judgeLineLineLabel = uiText("dialog.render_settings.gameplay.judge_line.line", "Line");
    const QString judgeLineAreaLabel = uiText("dialog.render_settings.gameplay.judge_line.area", "Judge Area");
    const QString judgeLineAreaLabeledLabel = uiText(
        "dialog.render_settings.gameplay.judge_line.area_labeled",
        "Judge Area (Labeled)"
    );
    const QString judgeLineImportLabel = uiText("dialog.render_settings.gameplay.judge_line.import", "Import...");
    const auto judgeLineLabelForVariant = [
        judgeLinePointLabel,
        judgeLineLineLabel,
        judgeLineAreaLabel,
        judgeLineAreaLabeledLabel
    ](PreviewOutlineVariant variant) {
        switch (variant) {
        case PreviewOutlineVariant::Point:
            return judgeLinePointLabel;
        case PreviewOutlineVariant::JudgeArea:
            return judgeLineAreaLabel;
        case PreviewOutlineVariant::JudgeAreaLabeled:
            return judgeLineAreaLabeledLabel;
        case PreviewOutlineVariant::Line:
        default:
            return judgeLineLineLabel;
        }
    };
    const auto judgeLineButtonLabel = [&]() {
        return owner_.previewCustomOutlineFileName_.isEmpty()
            ? judgeLineLabelForVariant(owner_.previewOutlineVariant_)
            : owner_.previewCustomOutlineFileName_;
    };
    auto* judgeLineButton = createDialogMenuButton(gameplayGroup, judgeLineButtonLabel());
    judgeLineButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* judgeLineMenu = new QMenu(judgeLineButton);
    styleRoundedMenu(*judgeLineMenu);
    addDialogMenuChoice(judgeLineMenu, judgeLinePointLabel, [this, judgeLineButton, judgeLineLabelForVariant]() {
        owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::Point, false, true);
        judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
    });
    addDialogMenuChoice(judgeLineMenu, judgeLineLineLabel, [this, judgeLineButton, judgeLineLabelForVariant]() {
        owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::Line, false, true);
        judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
    });
    addDialogMenuChoice(judgeLineMenu, judgeLineAreaLabel, [this, judgeLineButton, judgeLineLabelForVariant]() {
        owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::JudgeArea, false, true);
        judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
    });
    addDialogMenuChoice(
        judgeLineMenu,
        judgeLineAreaLabeledLabel,
        [this, judgeLineButton, judgeLineLabelForVariant]() {
            owner_.applyPreviewOutlineVariant(PreviewOutlineVariant::JudgeAreaLabeled, false, true);
            judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
        }
    );
    const QStringList customOutlineNames = owner_.availablePreviewCustomOutlineFileNames();
    if (!customOutlineNames.isEmpty()) {
        judgeLineMenu->addSeparator();
    }
    for (const QString& fileName : customOutlineNames) {
        addDialogMenuChoice(judgeLineMenu, fileName, [this, judgeLineButton, fileName]() {
            owner_.applyPreviewCustomOutlineFileName(fileName, true);
            judgeLineButton->setText(fileName);
        });
    }
    judgeLineMenu->addSeparator();
    addDialogMenuChoice(judgeLineMenu, judgeLineImportLabel, [this]() {
        const QString outlineDir = owner_.resolvePreviewCustomOutlineDir();
        if (!outlineDir.isEmpty()) {
            QDir().mkpath(outlineDir);
            QDesktopServices::openUrl(QUrl::fromLocalFile(outlineDir));
        }
    });
    judgeLineButton->setMenu(judgeLineMenu);
    auto* forceLabeledJudgeLineWhenPausedCheck = new QCheckBox(
        uiText(
            "dialog.render_settings.gameplay.force_labeled_judge_line_when_paused",
            "Show judge area while preview is paused"
        ),
        videoGroup
    );
    forceLabeledJudgeLineWhenPausedCheck->setChecked(owner_.previewForceLabeledJudgeLineWhenPaused_);
    auto* slideStackOrderButton = createDialogMenuButton(
        gameplayGroup,
        slideStackOrderLabelForValue(owner_.previewSlideEarlierSecondAndTextOnTop_)
    );
    slideStackOrderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* slideStackOrderMenu = new QMenu(slideStackOrderButton);
    styleRoundedMenu(*slideStackOrderMenu);
    const auto setSlideStackOrder = [
        this,
        slideStackOrderButton,
        slideStackOrderLabelForValue
    ](bool earlierOnTop) {
        if (owner_.previewSlideEarlierSecondAndTextOnTop_ == earlierOnTop) {
            slideStackOrderButton->setText(slideStackOrderLabelForValue(earlierOnTop));
            return;
        }
        owner_.previewSlideEarlierSecondAndTextOnTop_ = earlierOnTop;
        slideStackOrderButton->setText(slideStackOrderLabelForValue(earlierOnTop));
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSlideEarlierSecondAndTextOnTop(earlierOnTop);
        }
        owner_.savePortableState();
    };
    addDialogMenuChoice(slideStackOrderMenu, slideStackOrderDxLabel, [setSlideStackOrder]() {
        setSlideStackOrder(true);
    });
    addDialogMenuChoice(slideStackOrderMenu, slideStackOrderFinaleLabel, [setSlideStackOrder]() {
        setSlideStackOrder(false);
    });
    slideStackOrderButton->setMenu(slideStackOrderMenu);
    PreviewBackgroundScaleMode selectedScaleMode = owner_.previewBackgroundScaleMode_;
    const auto scaleModeLabelFor = [&](PreviewBackgroundScaleMode mode) {
        switch (mode) {
        case PreviewBackgroundScaleMode::FitContain:
            return scaleFitLabel;
        case PreviewBackgroundScaleMode::SquareFitContain:
            return scaleSquareFitLabel;
        case PreviewBackgroundScaleMode::FillCrop:
        default:
            return scaleFillLabel;
        }
    };
    auto* scaleModeButton = createDialogMenuButton(
        videoGroup,
        scaleModeLabelFor(selectedScaleMode)
    );
    auto* scaleModeMenu = new QMenu(scaleModeButton);
    styleRoundedMenu(*scaleModeMenu);
    const auto setScaleMode = [&](PreviewBackgroundScaleMode mode) {
        selectedScaleMode = mode;
        scaleModeButton->setText(scaleModeLabelFor(selectedScaleMode));
        owner_.previewBackgroundScaleMode_ = selectedScaleMode;
        owner_.applyPreviewStageMediaRouteVisualSettings();
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundScaleMode(selectedScaleMode);
        }
        owner_.savePortableState();
    };
    addDialogMenuChoice(scaleModeMenu, scaleFillLabel, [&, setScaleMode]() {
        setScaleMode(PreviewBackgroundScaleMode::FillCrop);
    });
    addDialogMenuChoice(scaleModeMenu, scaleFitLabel, [&, setScaleMode]() {
        setScaleMode(PreviewBackgroundScaleMode::FitContain);
    });
    addDialogMenuChoice(scaleModeMenu, scaleSquareFitLabel, [&, setScaleMode]() {
        setScaleMode(PreviewBackgroundScaleMode::SquareFitContain);
    });
    scaleModeButton->setMenu(scaleModeMenu);

    auto* smoothBrightnessCheck = new QCheckBox(
        uiText("dialog.render_settings.video.smooth_brightness", "Smooth brightness"),
        videoGroup
    );
    smoothBrightnessCheck->setChecked(owner_.previewSmoothBrightness_);
    auto* timestampCheck = new QCheckBox(
        uiText("dialog.video_export.option.show_timestamp", "Show bottom-left timestamp"),
        videoGroup
    );
    timestampCheck->setChecked(owner_.previewShowTimestamp_);
    auto* debugCheck = new QCheckBox(
        uiText("dialog.render_settings.preview.debug", "Show preview debug info"),
        videoGroup
    );
    debugCheck->setChecked(owner_.previewShowDebugInfo_);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_outer", "Outer Brightness"), outerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_inner", "Inner Brightness"), innerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.layout_square_scale", "Stage Display Scale"), layoutSquareScaleRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.scale_mode", "Background / PV Scale Mode"), scaleModeButton);
    videoFormLayout->addRow(
        uiText("dialog.render_settings.preview.canvas_frame_rate", "Preview Refresh Rate"),
        canvasFrameRateButton
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
    videoCheckLayout->addWidget(debugCheck, 1, 0, Qt::AlignLeft);
    videoCheckLayout->addWidget(forceLabeledJudgeLineWhenPausedCheck, 1, 1, Qt::AlignLeft);
    videoFormLayout->addRow(QString(), videoCheckRow);

    const auto addGameplayField = [gameplayGroup, gameplayLayout](int row, int column, const QString& labelText, QWidget* control) {
        auto* field = new QWidget(gameplayGroup);
        auto* fieldLayout = new QVBoxLayout(field);
        // 1px bottom margin so the rounded control underneath isn't flush
        // against the field's edge — otherwise its bottom border is clipped
        // (visible as the "missing bottom edge" on every box).
        fieldLayout->setContentsMargins(0, 0, 0, 1);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        // The QSS border-radius controls report a sizeHint a hair short of what
        // the bottom border needs; pad the height by 2px so it renders fully.
        control->setMinimumHeight(qMax(control->minimumHeight(), control->sizeHint().height() + 2));
        fieldLayout->addWidget(control, 0);
        gameplayLayout->addWidget(field, row, column);
    };
    addGameplayField(
        0,
        0,
        uiText("dialog.render_settings.video.tap_flow_speed", "Tap Flow Speed"),
        tapFlowSpeedEdit
    );
    addGameplayField(
        0,
        1,
        uiText("dialog.render_settings.video.touch_flow_speed", "Touch Flow Speed"),
        touchFlowSpeedEdit
    );
    addGameplayField(
        1,
        0,
        uiText("dialog.render_settings.video.skin", "Skin"),
        skinButton
    );
    addGameplayField(
        1,
        1,
        uiText("dialog.render_settings.gameplay.judge_line", "Judge Line"),
        judgeLineButton
    );
    addGameplayField(
        2,
        0,
        uiText("dialog.render_settings.gameplay.judge_effect", "Judge Effect Display"),
        judgeEffectButton
    );
    addGameplayField(
        2,
        1,
        uiText("dialog.render_settings.gameplay.slide_stack_order", "Slide Stack Order"),
        slideStackOrderButton
    );
    const auto centerDisplayLabelForMode = [](miacode::preview_gameplay::CenterDisplayMode mode) -> QString {
        switch (mode) {
        case miacode::preview_gameplay::CenterDisplayMode::Off:
            return uiText("dialog.render_settings.gameplay.center_display.off", "Off");
        case miacode::preview_gameplay::CenterDisplayMode::Combo:
            return uiText("dialog.render_settings.gameplay.center_display.combo", "Combo");
        case miacode::preview_gameplay::CenterDisplayMode::AchievementDxPlus:
            return uiText("dialog.render_settings.gameplay.center_display.achievement_dx_plus", "ACHIEVEMENT DX (+)");
        case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100:
            return uiText("dialog.render_settings.gameplay.center_display.achievement_dx_minus_100", "ACHIEVEMENT DX (100-)");
        case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101:
            return uiText("dialog.render_settings.gameplay.center_display.achievement_dx_minus_101", "ACHIEVEMENT DX (101-)");
        case miacode::preview_gameplay::CenterDisplayMode::DxScorePlus:
            return uiText("dialog.render_settings.gameplay.center_display.dx_score_plus", "DX SCORE (+)");
        case miacode::preview_gameplay::CenterDisplayMode::DxScoreMinus:
            return uiText("dialog.render_settings.gameplay.center_display.dx_score_minus", "DX SCORE (-)");
        case miacode::preview_gameplay::CenterDisplayMode::AchievementFinalePlus:
            return uiText("dialog.render_settings.gameplay.center_display.achievement_finale_plus", "ACHIEVEMENT FINALE (+)");
        }
        return uiText("dialog.render_settings.gameplay.center_display.off", "Off");
    };
    auto* centerDisplayButton = createDialogMenuButton(
        gameplayGroup,
        centerDisplayLabelForMode(owner_.previewCenterDisplayMode_)
    );
    centerDisplayButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* centerDisplayMenu = new QMenu(centerDisplayButton);
    styleRoundedMenu(*centerDisplayMenu);
    const auto setCenterDisplay = [this, centerDisplayButton, centerDisplayLabelForMode](
        miacode::preview_gameplay::CenterDisplayMode mode
    ) {
        if (owner_.previewCenterDisplayMode_ == mode) {
            centerDisplayButton->setText(centerDisplayLabelForMode(mode));
            return;
        }
        owner_.previewCenterDisplayMode_ = mode;
        centerDisplayButton->setText(centerDisplayLabelForMode(mode));
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setCenterDisplayMode(mode);
        }
        owner_.savePortableState();
    };
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::Off), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::Off);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::Combo), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::Combo);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::AchievementDxPlus), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::AchievementDxPlus);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::DxScorePlus), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::DxScorePlus);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::DxScoreMinus), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::DxScoreMinus);
    });
    addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(miacode::preview_gameplay::CenterDisplayMode::AchievementFinalePlus), [setCenterDisplay]() {
        setCenterDisplay(miacode::preview_gameplay::CenterDisplayMode::AchievementFinalePlus);
    });
    centerDisplayButton->setMenu(centerDisplayMenu);
    addGameplayField(
        3,
        0,
        uiText("dialog.render_settings.gameplay.center_display", "Center Display"),
        centerDisplayButton
    );
    audioGroup->setVisible(includeAudioSettings);
    videoGroup->setVisible(includeVideoSettings);
    gameplayGroup->setVisible(includeVideoSettings);

    if (includeAudioSettings) {
        rootLayout->addWidget(audioGroup, 0);
    }
    if (includeVideoSettings) {
        // Surface the 视频 / 游戏 split as tab pages instead of two
        // stacked QGroupBoxes — matches the Preferences dialog pattern.
        // The groupboxes keep their layouts + children intact; we only
        // strip their title bar and frame chrome so they read as plain
        // tab contents (the tab strip already shows the section name).
        const auto flattenGroup = [](QGroupBox* group) {
            if (group == nullptr) {
                return;
            }
            group->setTitle(QString());
            group->setFlat(true);
            // Belt + braces: setFlat() leaves a 1px top line on most
            // styles; clearing the border via stylesheet kills that
            // residual line so the tab pane border is the only frame
            // the user sees.
            group->setStyleSheet(QStringLiteral(
                "QGroupBox { border: none; margin-top: 0; padding-top: 0; }"
            ));
        };
        flattenGroup(videoGroup);
        flattenGroup(gameplayGroup);
        auto* videoSettingsTabs = new QTabWidget(&dialog);
        videoSettingsTabs->setObjectName(QStringLiteral("RenderSettingsTabs"));
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
            uiText("dialog.render_settings.video_group", "Video"));
        videoSettingsTabs->addTab(gameplayGroup,
            uiText("dialog.render_settings.gameplay_group", "Gameplay"));
        // Font tab — the same HUD-font page the video-export dialog hosts (shared
        // dialog in tools/video_export/HudFontSettings.cpp). Top-aligned content
        // with a trailing stretch; the smallest page, so it adds no pane height.
        {
            auto* fontGroup = new QGroupBox(&dialog);
            auto* fontLayout = new QVBoxLayout(fontGroup);
            fontLayout->setContentsMargins(12, 10, 12, 10);
            fontLayout->setSpacing(8);
            auto* hudFontLabel = new QLabel(
                uiText("dialog.video_export.option.hud_font", "HUD font"), fontGroup);
            auto* hudFontSettingsButton = new QPushButton(
                uiText("dialog.video_export.option.hud_font_settings", "Font Settings"), fontGroup);
            hudFontSettingsButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
            fontLayout->addWidget(hudFontLabel, 0, Qt::AlignLeft);
            fontLayout->addWidget(hudFontSettingsButton, 0, Qt::AlignLeft);
            fontLayout->addStretch(1);
            connect(hudFontSettingsButton, &QPushButton::clicked, &dialog, [&dialog]() {
                // No live-refresh callback: the editor preview HUD re-reads the
                // global font on its next repaint (scrub/play), which is enough
                // outside the export dialog's frozen-frame preview.
                miacode::video_export::openHudFontSettingsDialog(&dialog);
            });
            flattenGroup(fontGroup);
            videoSettingsTabs->addTab(fontGroup,
                uiText("dialog.video_export.section.font", "Font"));
        }
        rootLayout->addWidget(videoSettingsTabs, 0);
    }
    auto* buttonBox = new QDialogButtonBox(&dialog);
    QPushButton* saveLocalAudioPresetButton = nullptr;
    QPushButton* applyLocalAudioPresetButton = nullptr;
    if (includeAudioSettings) {
        saveLocalAudioPresetButton = buttonBox->addButton(
            uiText("dialog.render_settings.button.set_software_default_audio", "Save Local Preset"),
            QDialogButtonBox::ActionRole
        );
        applyLocalAudioPresetButton = buttonBox->addButton(
            uiText("dialog.render_settings.button.restore_project_default", "Apply Local Preset"),
            QDialogButtonBox::ActionRole
        );
        if (saveLocalAudioPresetButton != nullptr) {
            saveLocalAudioPresetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
        if (applyLocalAudioPresetButton != nullptr) {
            applyLocalAudioPresetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
    }
    if (QPushButton* closeButton = buttonBox->addButton(uiText("dialog.render_settings.button.close", "Close"), QDialogButtonBox::RejectRole)) {
        closeButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    }
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);

    auto* audioApplyTimer = new QTimer(&dialog);
    audioApplyTimer->setSingleShot(true);
    audioApplyTimer->setInterval(220);
    QString pendingAudition;
    auto* dialogAuditionRuntime = new QtPreviewSfxRuntime(&dialog);
    QString dialogAuditionSfxDir;

    auto queueAudioApply = [audioApplyTimer, &pendingAudition](const QString& audition) {
        pendingAudition = audition;
        audioApplyTimer->start();
    };

    const auto playDialogLocalSfxAudition = [
        this,
        dialogAuditionRuntime,
        &dialogAuditionSfxDir
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
            dialogAuditionRuntime->reloadAssets(owner_.previewAudioSettings_);
            dialogAuditionSfxDir = resolvedSfxDir;
        } else {
            dialogAuditionRuntime->applyLevels(owner_.previewAudioSettings_);
        }
        if (!dialogAuditionRuntime->audioEngineInitialized()) {
            return false;
        }
        dialogAuditionRuntime->stopAll();
        return dialogAuditionRuntime->audition(resolvedKind);
    };

    const QString muteAudioButtonTooltip = uiText("dialog.render_settings.audio.button.mute", "Mute %1");
    const QString unmuteAudioButtonTooltip = uiText("dialog.render_settings.audio.button.unmute", "Unmute %1");
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
        owner_.previewAudioSettings_.breakSlideTailCheerMuted = checked;
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
            owner_.softwarePreviewAudioSettings_ = owner_.previewAudioSettings_;
            owner_.softwarePreviewAudioSettings_.normalize();
            owner_.savePortableState();
        });
    }
    if (applyLocalAudioPresetButton != nullptr) {
        connect(
            applyLocalAudioPresetButton,
            &QPushButton::clicked,
            &dialog,
            [this, audioApplyTimer, &pendingAudition, syncAudioControlsFromCurrentSettings]() {
                owner_.previewAudioSettings_ = owner_.softwarePreviewAudioSettings_;
                owner_.previewAudioSettings_.normalize();
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
