#include "MainWindow.DialogsSection.h"
#include "../../MainWindowShared.h"

#include "AppVersion.h"
#include "QtPreviewSfxRuntime.h"
#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/ChartAssetPaths.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/latency/LatencyDetectorDialog.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

MainWindow::DialogsSection::DialogsSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

QString MainWindow::DialogsSection::resolveLatencyDetectorTrackPath() const
{
    if (owner_.currentFilePath_.isEmpty()) {
        return QString();
    }
    return miacode::chart_assets::resolveTrackPath(owner_.currentFilePath_);
}

void MainWindow::DialogsSection::updateLatencyDetectorAvailability()
{
    const bool enabled = !resolveLatencyDetectorTrackPath().isEmpty();
    if (owner_.latencyDetectorAction_ != nullptr) {
        owner_.latencyDetectorAction_->setEnabled(enabled);
    }
    if (owner_.latencyDetectorButton_ != nullptr) {
        owner_.latencyDetectorButton_->setEnabled(enabled);
    }
}

void MainWindow::DialogsSection::onPreviewAudioSettings()
{
    openPreviewSettingsDialog(
        true,
        false,
        uiText("dialog.audio_settings.title", "Audio Settings")
    );
}

void MainWindow::DialogsSection::onPreviewVideoSettings()
{
    openPreviewSettingsDialog(
        false,
        true,
        uiText("dialog.video_settings.title", "Preview Settings")
    );
}

void MainWindow::DialogsSection::onAbout()
{
    QString buildType = "Release";
#ifndef NDEBUG
    buildType = "Debug";
#endif
    const QString platform = QString("%1 / %2 / %3")
        .arg(QSysInfo::productType())
        .arg(QSysInfo::currentCpuArchitecture())
        .arg(QSysInfo::buildAbi());

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(uiText("action.about", "About"));
    dialog.setModal(true);
    dialog.setMinimumWidth(500);
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());
    owner_.applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(14, 14, 14, 12);
    rootLayout->setSpacing(10);

    auto* card = new QFrame(&dialog);
    card->setObjectName("AboutCard");
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    auto* iconLabel = new QLabel(card);
    iconLabel->setObjectName("AboutIcon");
    iconLabel->setFixedSize(64, 64);
    QPixmap appIcon = QIcon(":/icons/app.png").pixmap(48, 48);
    if (!appIcon.isNull()) {
        iconLabel->setPixmap(appIcon);
        iconLabel->setAlignment(Qt::AlignCenter);
    }
    owner_.aboutIconLabel_ = iconLabel;
    iconLabel->installEventFilter(&owner_);
    titleRow->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto* titleTextCol = new QVBoxLayout();
    titleTextCol->setSpacing(4);
    auto* titleLabel = new QLabel("MiaCode", card);
    titleLabel->setObjectName("AboutTitle");
    QString displayVersion = QString::fromLatin1(MIACODE_DISPLAY_VERSION_STRING).trimmed();
    if (displayVersion.isEmpty()) {
        displayVersion = QCoreApplication::applicationVersion().trimmed();
    }
    if (displayVersion.isEmpty()) {
        displayVersion = QStringLiteral("0.0.0");
    }
    auto* versionLabel = new QLabel(QStringLiteral("v%1").arg(displayVersion), card);
    versionLabel->setObjectName("AboutVersion");
    titleTextCol->addWidget(titleLabel, 0, Qt::AlignLeft);
    titleTextCol->addWidget(versionLabel, 0, Qt::AlignLeft);
    titleRow->addLayout(titleTextCol, 0);
    titleRow->addStretch(1);
    cardLayout->addLayout(titleRow);

    auto* infoGrid = new QGridLayout();
    infoGrid->setHorizontalSpacing(12);
    infoGrid->setVerticalSpacing(6);
    auto addRow = [card, infoGrid](int row, const QString& key, const QString& value) {
        auto* k = new QLabel(key, card);
        k->setObjectName("AboutKey");
        auto* v = new QLabel(value, card);
        v->setObjectName("AboutValue");
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoGrid->addWidget(k, row, 0);
        infoGrid->addWidget(v, row, 1);
    };
    addRow(0, uiText("about.platform", "Release Platform"), platform);
    addRow(1, uiText("about.build_type", "Build Type"), buildType);
    cardLayout->addLayout(infoGrid);
    rootLayout->addWidget(card);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);
    dialog.exec();
    if (owner_.aboutIconLabel_ != nullptr) {
        owner_.aboutIconLabel_->removeEventFilter(&owner_);
    }
    owner_.aboutIconLabel_.clear();
    owner_.invalidStarPreviewAboutClickCount_ = 0;
    owner_.invalidStarPreviewAboutClickElapsed_.invalidate();
}

void MainWindow::DialogsSection::onOpenLatencyDetector()
{
    const QString trackPath = resolveLatencyDetectorTrackPath();
    bool wholeBpmOk = false;
    const double wholeBpm = owner_.parsedWholeBpm(&wholeBpmOk);
    const QString meterId = owner_.parsedLatencyMeterId();
    const double offsetSeconds = owner_.parsedFirstSeconds();
    if (trackPath.isEmpty()) {
        owner_.statusBar()->showMessage(UiText::isChineseUi()
            ? QStringLiteral("当前谱面目录缺少 track.mp3，无法打开BPM&偏移检测。")
            : QStringLiteral("track.mp3 was not found next to the current chart."));
        updateLatencyDetectorAvailability();
        return;
    }

    if (owner_.latencyDetectorDialog_ != nullptr) {
        if (owner_.latencyDetectorDialog_->trackPath() == trackPath) {
            owner_.latencyDetectorDialog_->setOffsetSeconds(offsetSeconds);
            owner_.latencyDetectorDialog_->setBpm(wholeBpmOk ? wholeBpm : 0.0);
            owner_.latencyDetectorDialog_->setMeterId(meterId);
            owner_.latencyDetectorDialog_->raise();
            owner_.latencyDetectorDialog_->activateWindow();
            return;
        }
        owner_.latencyDetectorDialog_->close();
        owner_.latencyDetectorDialog_.clear();
    }

    owner_.latencyDetectorDialog_ = new LatencyDetectorDialog(
        trackPath,
        owner_.currentFilePath_,
        owner_.previewAudioSettings_,
        UiDialogs::effectiveParentWidget(&owner_)
    );
    owner_.applySystemWindowBackdrop(owner_.latencyDetectorDialog_);
    UiDialogs::prepareDialogWindow(owner_.latencyDetectorDialog_, &owner_);
    owner_.latencyDetectorDialog_->setOffsetSeconds(offsetSeconds);
    owner_.latencyDetectorDialog_->setBpm(wholeBpmOk ? wholeBpm : 0.0);
    owner_.latencyDetectorDialog_->setMeterId(meterId);
    connect(owner_.latencyDetectorDialog_, &LatencyDetectorDialog::offsetChanged, &owner_, [this](double seconds) {
        owner_.applyLatencyDetectorOffset(seconds);
    });
    connect(owner_.latencyDetectorDialog_, &LatencyDetectorDialog::bpmChanged, &owner_, [this](double bpm) {
        owner_.applyLatencyDetectorBpm(bpm);
    });
    connect(owner_.latencyDetectorDialog_, &QObject::destroyed, &owner_, [this]() {
        owner_.latencyDetectorDialog_.clear();
    });
    owner_.latencyDetectorDialog_->show();
    owner_.latencyDetectorDialog_->raise();
    owner_.latencyDetectorDialog_->activateWindow();
}

void MainWindow::DialogsSection::openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title)
{
    if (!includeAudioSettings && !includeVideoSettings) {
        return;
    }
    owner_.previewAudioSettings_.normalize();

    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setMinimumWidth(520);
    dialog.setStyleSheet(UiTheme::settingsDialogStyleSheet());
    owner_.applySystemWindowBackdrop(&dialog);
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
    const auto addDialogMenuChoice = [](QMenu* menu, const QString& text, const std::function<void()>& onTriggered) {
        auto* action = new QWidgetAction(menu);
        auto* button = new QToolButton(menu);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(text);
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

    const auto addAudioRow = [&](const QString& labelText, int valuePercent, QSlider** sliderOut, QLabel** labelOut) {
        auto* row = new QWidget(audioGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, 100);
        slider->setValue(valuePercent);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        auto* label = new QLabel(QString::number(valuePercent) + "%", row);
        label->setMinimumWidth(44);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        audioFormLayout->addRow(labelText, row);
        *sliderOut = slider;
        *labelOut = label;
    };

    QSlider* bgmSlider = nullptr;
    QLabel* bgmLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.bgm", "BGM Volume"), owner_.previewAudioSettings_.bgmPercent(), &bgmSlider, &bgmLabel);
    QSlider* answerSlider = nullptr;
    QLabel* answerLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.answer", "Answer Volume"), owner_.previewAudioSettings_.answerPercent(), &answerSlider, &answerLabel);
    QSlider* judgeSlider = nullptr;
    QLabel* judgeLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.judge", "Judge Volume"), owner_.previewAudioSettings_.judgePercent(), &judgeSlider, &judgeLabel);
    QSlider* breakSlider = nullptr;
    QLabel* breakLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.break", "Break Volume"), owner_.previewAudioSettings_.breakPercent(), &breakSlider, &breakLabel);
    QSlider* slideSlider = nullptr;
    QLabel* slideLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.slide", "Slide Volume"), owner_.previewAudioSettings_.slidePercent(), &slideSlider, &slideLabel);
    QSlider* exSlider = nullptr;
    QLabel* exLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.ex", "EX Volume"), owner_.previewAudioSettings_.exPercent(), &exSlider, &exLabel);
    QSlider* touchSlider = nullptr;
    QLabel* touchLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.touch", "Touch Volume"), owner_.previewAudioSettings_.touchPercent(), &touchSlider, &touchLabel);
    QSlider* fireworkSlider = nullptr;
    QLabel* fireworkLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.firework", "Firework Volume"), owner_.previewAudioSettings_.fireworkPercent(), &fireworkSlider, &fireworkLabel);
    QSlider* breakSlideSlider = nullptr;
    QLabel* breakSlideLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.break_slide", "Break Slide Volume"), owner_.previewAudioSettings_.breakSlidePercent(), &breakSlideSlider, &breakSlideLabel);

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
        auto* label = new QLabel(QString::number(value) + suffix, row);
        label->setMinimumWidth(44);
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
    double selectedFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(owner_.previewNoteFlowSpeed_);
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    selectedFlowSpeed = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((selectedFlowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    auto* flowSpeedEdit = new QLineEdit(gameplayGroup);
    flowSpeedEdit->setAlignment(Qt::AlignCenter);
    flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
    flowSpeedEdit->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet());
    flowSpeedEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* flowSpeedValidator = new QDoubleValidator(flowSpeedMin, flowSpeedMax, 2, flowSpeedEdit);
    flowSpeedValidator->setNotation(QDoubleValidator::StandardNotation);
    flowSpeedEdit->setValidator(flowSpeedValidator);
    QObject::connect(flowSpeedEdit, &QLineEdit::editingFinished, &dialog, [&, flowSpeedEdit]() {
        bool ok = false;
        const double typedSpeed = flowSpeedEdit->text().trimmed().toDouble(&ok);
        if (!ok) {
            flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
            return;
        }
        selectedFlowSpeed = qBound(
            flowSpeedMin,
            flowSpeedMin + qRound((typedSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
            flowSpeedMax
        );
        flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
        owner_.previewNoteFlowSpeed_ = selectedFlowSpeed;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setNoteFlowSpeed(selectedFlowSpeed);
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
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
    const QList<CanvasFrameRateOption> canvasFrameRateOptions{
        {PreviewCanvasFrameRateMode::Fps60, uiText("dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS")},
        {PreviewCanvasFrameRateMode::Fps120, uiText("dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS")},
        {PreviewCanvasFrameRateMode::DisplayRefresh, displayRefreshLabel},
    };
    QString selectedCanvasFrameRateLabel = canvasFrameRateOptions.front().label;
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        if (option.mode == owner_.previewCanvasFrameRateMode_) {
            selectedCanvasFrameRateLabel = option.label;
            break;
        }
    }
    auto* canvasFrameRateButton = createDialogMenuButton(videoGroup, selectedCanvasFrameRateLabel);
    canvasFrameRateButton->setFixedHeight(flowSpeedEdit->sizeHint().height());
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
    const QString standardSkinLabel = uiText("dialog.render_settings.video.skin.standard", "Standard");
    const QString dxSkinLabel = uiText("dialog.render_settings.video.skin.dx", "DX");
    auto* skinButton = createDialogMenuButton(
        gameplayGroup,
        owner_.previewSkinVariant_ == PreviewSkinVariant::Dx ? dxSkinLabel : standardSkinLabel
    );
    skinButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* skinMenu = new QMenu(skinButton);
    styleRoundedMenu(*skinMenu);
    addDialogMenuChoice(skinMenu, standardSkinLabel, [this, skinButton, standardSkinLabel]() {
        owner_.previewSkinVariant_ = PreviewSkinVariant::Standard;
        skinButton->setText(standardSkinLabel);
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSkinDirectory(owner_.resolvePreviewSkinDir());
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });
    addDialogMenuChoice(skinMenu, dxSkinLabel, [this, skinButton, dxSkinLabel]() {
        owner_.previewSkinVariant_ = PreviewSkinVariant::Dx;
        skinButton->setText(dxSkinLabel);
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSkinDirectory(owner_.resolvePreviewSkinDir());
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });
    skinButton->setMenu(skinMenu);
    const QString enabledLabel = uiText("dialog.render_settings.option.enabled", "Enabled");
    const QString disabledLabel = uiText("dialog.render_settings.option.disabled", "Disabled");
    auto* slideJudgeEffectButton = createDialogMenuButton(
        gameplayGroup,
        owner_.muriRenderOptions_.showChartReviewSlideJudgeOverlay ? enabledLabel : disabledLabel
    );
    slideJudgeEffectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* slideJudgeEffectMenu = new QMenu(slideJudgeEffectButton);
    styleRoundedMenu(*slideJudgeEffectMenu);
    const auto setSlideJudgeEffectEnabled = [this, slideJudgeEffectButton, enabledLabel, disabledLabel](bool enabled) {
        slideJudgeEffectButton->setText(enabled ? enabledLabel : disabledLabel);
        if (owner_.muriRenderOptions_.showChartReviewSlideJudgeOverlay == enabled) {
            return;
        }
        owner_.muriRenderOptions_.showChartReviewSlideJudgeOverlay = enabled;
        owner_.applyMuriRenderOptions();
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    };
    addDialogMenuChoice(slideJudgeEffectMenu, enabledLabel, [setSlideJudgeEffectEnabled]() {
        setSlideJudgeEffectEnabled(true);
    });
    addDialogMenuChoice(slideJudgeEffectMenu, disabledLabel, [setSlideJudgeEffectEnabled]() {
        setSlideJudgeEffectEnabled(false);
    });
    slideJudgeEffectButton->setMenu(slideJudgeEffectMenu);
    const QString judgeLinePointLabel = uiText("dialog.render_settings.gameplay.judge_line.point", "Point");
    const QString judgeLineLineLabel = uiText("dialog.render_settings.gameplay.judge_line.line", "Line");
    const QString judgeLineAreaLabel = uiText("dialog.render_settings.gameplay.judge_line.area", "Judge Area");
    const QString judgeLineAreaLabeledLabel = uiText(
        "dialog.render_settings.gameplay.judge_line.area_labeled",
        "Judge Area (Labeled)"
    );
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
    auto* judgeLineButton = createDialogMenuButton(gameplayGroup, judgeLineLabelForVariant(owner_.previewOutlineVariant_));
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
    judgeLineButton->setMenu(judgeLineMenu);
    auto* forceLabeledJudgeLineWhenPausedCheck = new QCheckBox(
        uiText(
            "dialog.render_settings.gameplay.force_labeled_judge_line_when_paused",
            "Hide PV / BG and force labeled judge area while preview is paused"
        ),
        gameplayGroup
    );
    forceLabeledJudgeLineWhenPausedCheck->setChecked(owner_.previewForceLabeledJudgeLineWhenPaused_);
    PreviewBackgroundScaleMode selectedScaleMode = owner_.previewBackgroundScaleMode_;
    auto* scaleModeButton = createDialogMenuButton(
        videoGroup,
        selectedScaleMode == PreviewBackgroundScaleMode::FitContain ? scaleFitLabel : scaleFillLabel
    );
    auto* scaleModeMenu = new QMenu(scaleModeButton);
    styleRoundedMenu(*scaleModeMenu);
    addDialogMenuChoice(scaleModeMenu, scaleFillLabel, [&, scaleFillLabel]() {
        selectedScaleMode = PreviewBackgroundScaleMode::FillCrop;
        scaleModeButton->setText(scaleFillLabel);
        owner_.previewBackgroundScaleMode_ = selectedScaleMode;
        owner_.applyPreviewStageMediaRouteVisualSettings();
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundScaleMode(selectedScaleMode);
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });
    addDialogMenuChoice(scaleModeMenu, scaleFitLabel, [&, scaleFitLabel]() {
        selectedScaleMode = PreviewBackgroundScaleMode::FitContain;
        scaleModeButton->setText(scaleFitLabel);
        owner_.previewBackgroundScaleMode_ = selectedScaleMode;
        owner_.applyPreviewStageMediaRouteVisualSettings();
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundScaleMode(selectedScaleMode);
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
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
    const bool unifiedObjectStatsChecked = owner_.previewShowObjectStatsHud_ || owner_.exportShowObjectStatsHud_;
    owner_.previewShowObjectStatsHud_ = unifiedObjectStatsChecked;
    owner_.exportShowObjectStatsHud_ = unifiedObjectStatsChecked;
    auto* objectStatsCheck = new QCheckBox(
        uiText("dialog.render_settings.preview.show_object_stats", "Show object stats in preview/export"),
        videoGroup
    );
    objectStatsCheck->setChecked(unifiedObjectStatsChecked);
    auto* debugCheck = new QCheckBox(
        uiText("dialog.render_settings.preview.debug", "Show preview debug info"),
        videoGroup
    );
    debugCheck->setChecked(owner_.previewShowDebugInfo_);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_outer", "Outer Brightness"), outerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_inner", "Inner Brightness"), innerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.layout_square_scale", "Layout Size"), layoutSquareScaleRow);
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
    videoCheckLayout->addWidget(objectStatsCheck, 1, 1, Qt::AlignLeft);
    videoFormLayout->addRow(QString(), videoCheckRow);

    const auto addGameplayField = [gameplayGroup, gameplayLayout](int row, int column, const QString& labelText, QWidget* control) {
        auto* field = new QWidget(gameplayGroup);
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        fieldLayout->addWidget(control, 0);
        gameplayLayout->addWidget(field, row, column);
    };
    addGameplayField(0, 0, uiText("dialog.render_settings.video.flow_speed", "Flow Speed"), flowSpeedEdit);
    addGameplayField(0, 1, uiText("dialog.render_settings.video.skin", "Skin"), skinButton);
    addGameplayField(
        1,
        0,
        uiText("dialog.render_settings.gameplay.slide_judge_effect", "Slide Judge Effect"),
        slideJudgeEffectButton
    );
    addGameplayField(
        1,
        1,
        uiText("dialog.render_settings.gameplay.judge_line", "Judge Line"),
        judgeLineButton
    );
    gameplayLayout->addWidget(forceLabeledJudgeLineWhenPausedCheck, 2, 0, 1, 2, Qt::AlignLeft);
    audioGroup->setVisible(includeAudioSettings);
    videoGroup->setVisible(includeVideoSettings);
    gameplayGroup->setVisible(includeVideoSettings);

    if (includeAudioSettings) {
        rootLayout->addWidget(audioGroup, 0);
    }
    if (includeVideoSettings) {
        rootLayout->addWidget(videoGroup, 0);
        rootLayout->addWidget(gameplayGroup, 0);
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

    auto queueAudioApply = [audioApplyTimer, &pendingAudition](const QString& audition) {
        pendingAudition = audition;
        audioApplyTimer->start();
    };

    const auto syncAudioControlsFromCurrentSettings = [
        this,
        bgmSlider,
        bgmLabel,
        answerSlider,
        answerLabel,
        judgeSlider,
        judgeLabel,
        breakSlider,
        breakLabel,
        slideSlider,
        slideLabel,
        exSlider,
        exLabel,
        touchSlider,
        touchLabel,
        fireworkSlider,
        fireworkLabel,
        breakSlideSlider,
        breakSlideLabel
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
        syncAudioRow(bgmSlider, bgmLabel, owner_.previewAudioSettings_.bgmPercent());
        syncAudioRow(answerSlider, answerLabel, owner_.previewAudioSettings_.answerPercent());
        syncAudioRow(judgeSlider, judgeLabel, owner_.previewAudioSettings_.judgePercent());
        syncAudioRow(breakSlider, breakLabel, owner_.previewAudioSettings_.breakPercent());
        syncAudioRow(slideSlider, slideLabel, owner_.previewAudioSettings_.slidePercent());
        syncAudioRow(exSlider, exLabel, owner_.previewAudioSettings_.exPercent());
        syncAudioRow(touchSlider, touchLabel, owner_.previewAudioSettings_.touchPercent());
        syncAudioRow(fireworkSlider, fireworkLabel, owner_.previewAudioSettings_.fireworkPercent());
        syncAudioRow(breakSlideSlider, breakSlideLabel, owner_.previewAudioSettings_.breakSlidePercent());
    };

    connect(bgmSlider, &QSlider::valueChanged, &dialog, [this, bgmLabel, queueAudioApply](int value) {
        owner_.previewAudioSettings_.setBgmPercent(value);
        bgmLabel->setText(QString::number(owner_.previewAudioSettings_.bgmPercent()) + "%");
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.saveProjectRenderState();
        queueAudioApply(QString());
    });
    connect(answerSlider, &QSlider::valueChanged, &dialog, [this, answerLabel, queueAudioApply](int value) {
        owner_.previewAudioSettings_.setAnswerPercent(value);
        answerLabel->setText(QString::number(owner_.previewAudioSettings_.answerPercent()) + "%");
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.saveProjectRenderState();
        queueAudioApply("answer");
    });
    connect(judgeSlider, &QSlider::valueChanged, &dialog, [this, judgeLabel, queueAudioApply](int value) {
        owner_.previewAudioSettings_.setJudgePercent(value);
        judgeLabel->setText(QString::number(owner_.previewAudioSettings_.judgePercent()) + "%");
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.saveProjectRenderState();
        queueAudioApply("judge");
    });
    connect(breakSlider, &QSlider::valueChanged, &dialog, [this, breakLabel, queueAudioApply](int value) {
        owner_.previewAudioSettings_.setBreakPercent(value);
        breakLabel->setText(QString::number(owner_.previewAudioSettings_.breakPercent()) + "%");
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.saveProjectRenderState();
        queueAudioApply("break");
    });
    connect(slideSlider, &QSlider::valueChanged, &dialog, [this, slideLabel, queueAudioApply](int value) {
        owner_.previewAudioSettings_.setSlidePercent(value);
        slideLabel->setText(QString::number(owner_.previewAudioSettings_.slidePercent()) + "%");
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.saveProjectRenderState();
        queueAudioApply("slide");
    });
    connect(exSlider, &QSlider::valueChanged, &dialog, [this, exLabel, queueAudioApply](int value) {
        owner_.previewAudioSettings_.setExPercent(value);
        exLabel->setText(QString::number(owner_.previewAudioSettings_.exPercent()) + "%");
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.saveProjectRenderState();
        queueAudioApply("ex");
    });
    connect(touchSlider, &QSlider::valueChanged, &dialog, [this, touchLabel, queueAudioApply](int value) {
        owner_.previewAudioSettings_.setTouchPercent(value);
        touchLabel->setText(QString::number(owner_.previewAudioSettings_.touchPercent()) + "%");
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.saveProjectRenderState();
        queueAudioApply("touch");
    });
    connect(fireworkSlider, &QSlider::valueChanged, &dialog, [this, fireworkLabel, queueAudioApply](int value) {
        owner_.previewAudioSettings_.setFireworkPercent(value);
        fireworkLabel->setText(QString::number(owner_.previewAudioSettings_.fireworkPercent()) + "%");
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.saveProjectRenderState();
        queueAudioApply("firework");
    });
    connect(breakSlideSlider, &QSlider::valueChanged, &dialog, [this, breakSlideLabel, queueAudioApply](int value) {
        owner_.previewAudioSettings_.setBreakSlidePercent(value);
        breakSlideLabel->setText(QString::number(owner_.previewAudioSettings_.breakSlidePercent()) + "%");
        owner_.applyPreviewAudioSettingsToRuntime();
        owner_.saveProjectRenderState();
        queueAudioApply("break_slide");
    });
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
                owner_.saveProjectRenderState();
                if (audioApplyTimer->isActive()) {
                    audioApplyTimer->stop();
                }
                pendingAudition.clear();
            }
        );
    }

    connect(audioApplyTimer, &QTimer::timeout, &dialog, [this, audioApplyTimer, bgmSlider, answerSlider, judgeSlider, breakSlider, slideSlider, exSlider, touchSlider, fireworkSlider, breakSlideSlider, &pendingAudition]() {
        if (bgmSlider->isSliderDown()
            || answerSlider->isSliderDown()
            || judgeSlider->isSliderDown()
            || breakSlider->isSliderDown()
            || slideSlider->isSliderDown()
            || exSlider->isSliderDown()
            || touchSlider->isSliderDown()
            || fireworkSlider->isSliderDown()
            || breakSlideSlider->isSliderDown()) {
            audioApplyTimer->start();
            return;
        }
        if (!pendingAudition.isEmpty()) {
            owner_.ensurePreviewSfxRuntimePrepared();
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && owner_.previewSfxRuntime_ != nullptr
            && owner_.previewSfxRuntime_->audition(pendingAudition);
        Q_UNUSED(handledLocally);
        pendingAudition.clear();
    });
    connect(&dialog, &QDialog::finished, &dialog, [this, audioApplyTimer, &pendingAudition]() {
        if (!audioApplyTimer->isActive()) {
            return;
        }
        audioApplyTimer->stop();
        if (!pendingAudition.isEmpty()) {
            owner_.ensurePreviewSfxRuntimePrepared();
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && owner_.previewSfxRuntime_ != nullptr
            && owner_.previewSfxRuntime_->audition(pendingAudition);
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
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });
    connect(innerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, innerBrightnessLabel](int value) {
        owner_.previewBackgroundBrightnessInner_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        innerBrightnessLabel->setText(QString::number(value) + "%");
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setBackgroundBrightnessInner(owner_.previewBackgroundBrightnessInner_);
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });
    connect(layoutSquareScaleSlider, &QSlider::valueChanged, &dialog, [this, layoutSquareScaleLabel](int value) {
        owner_.previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(value) / 100.0);
        layoutSquareScaleLabel->setText(QString::number(qRound(owner_.previewLayoutSquareScale_ * 100.0)) + "%");
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setLayoutSquareScale(owner_.previewLayoutSquareScale_);
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });
    connect(smoothBrightnessCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewSmoothBrightness_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSmoothBrightness(owner_.previewSmoothBrightness_);
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });
    connect(timestampCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewShowTimestamp_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setShowTimestamp(owner_.previewShowTimestamp_);
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });
    connect(forceLabeledJudgeLineWhenPausedCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewForceLabeledJudgeLineWhenPaused_ = checked;
        owner_.applyEffectivePreviewOutlineVariantToCanvas();
        owner_.applyPreviewStageMediaRouteVisualSettings();
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });

    connect(debugCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        owner_.previewShowDebugInfo_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setShowDebugInfo(owner_.previewShowDebugInfo_);
        }
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    });
    const auto setObjectStatsHudEnabled = [this, objectStatsCheck](bool checked) {
        owner_.previewShowObjectStatsHud_ = checked;
        owner_.exportShowObjectStatsHud_ = checked;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setShowObjectStatsHud(owner_.previewShowObjectStatsHud_);
        }
        const QSignalBlocker objectStatsBlocker(objectStatsCheck);
        objectStatsCheck->setChecked(checked);
        owner_.saveProjectRenderState();
        owner_.savePortableState();
    };
    connect(objectStatsCheck, &QCheckBox::toggled, &dialog, [setObjectStatsHudEnabled](bool checked) {
        setObjectStatsHudEnabled(checked);
    });
    dialog.adjustSize();
    dialog.exec();
}

