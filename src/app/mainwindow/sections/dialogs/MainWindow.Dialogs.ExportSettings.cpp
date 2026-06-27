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

void MainWindow::DialogsSection::buildExportInjectedSettings(
    QWidget* parent,
    QWidget** gameplayOut)
{
    // Local clones of the menu-button / menu-choice helpers used by the
    // standalone settings dialog. Kept independent so this builder doesn't
    // depend on that function's internals (the two dialogs are wired
    // separately, per design).
    const auto createDialogMenuButton = [](QWidget* owner, const QString& text) {
        auto* button = new QToolButton(owner);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
        button->setText(text);
        // Vertical policy is Fixed (pins height to sizeHint and ignores a
        // min-height floor), so force the height a few px taller than the
        // styled sizeHint — otherwise the rounded bottom border is clipped.
        button->ensurePolished();
        button->setFixedHeight(qMax(button->sizeHint().height(), 30) + 4);
        return button;
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

    // ---- Gameplay page: skin / judge line / judge effect / slide stack /
    //      center display (matches the settings dialog's Gameplay grid, minus
    //      the flow-speed pair the export dialog already owns). ----
    auto* gameplay = new QWidget(parent);
    auto* gameplayLayout = new QGridLayout(gameplay);
    gameplayLayout->setContentsMargins(12, 0, 12, 0);
    gameplayLayout->setHorizontalSpacing(10);
    gameplayLayout->setVerticalSpacing(8);
    gameplayLayout->setColumnStretch(0, 1);
    gameplayLayout->setColumnStretch(1, 1);
    const auto addGameplayField = [gameplay, gameplayLayout](int row, int column, const QString& labelText, QWidget* control) {
        auto* field = new QWidget(gameplay);
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 1);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        control->setMinimumHeight(qMax(control->minimumHeight(), control->sizeHint().height() + 2));
        fieldLayout->addWidget(control, 0);
        gameplayLayout->addWidget(field, row, column);
    };

    // Skin.
    auto* skinButton = createDialogMenuButton(gameplay, owner_.previewSkinDisplayName(owner_.previewSkinDirectoryName_));
    skinButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* skinMenu = new QMenu(skinButton);
    UiTheme::styleRoundedMenu(*skinMenu);
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
    addDialogMenuChoice(skinMenu, uiText("dialog.render_settings.video.skin.import", "Import..."), [this]() {
        const QString skinRoot = owner_.resolvePreviewSkinRootDir();
        if (!skinRoot.isEmpty()) {
            QDir().mkpath(skinRoot);
            QDesktopServices::openUrl(QUrl::fromLocalFile(skinRoot));
        }
    });
    skinButton->setMenu(skinMenu);

    // Judge line (outline variant + custom outlines).
    const QString judgeLinePointLabel = uiText("dialog.render_settings.gameplay.judge_line.point", "Point");
    const QString judgeLineLineLabel = uiText("dialog.render_settings.gameplay.judge_line.line", "Line");
    const QString judgeLineAreaLabel = uiText("dialog.render_settings.gameplay.judge_line.area", "Judge Area");
    const QString judgeLineAreaLabeledLabel = uiText("dialog.render_settings.gameplay.judge_line.area_labeled", "Judge Area (Labeled)");
    const auto judgeLineLabelForVariant = [=](PreviewOutlineVariant variant) {
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
    const auto judgeLineButtonText = [this, judgeLineLabelForVariant]() {
        return owner_.previewCustomOutlineFileName_.isEmpty()
            ? judgeLineLabelForVariant(owner_.previewOutlineVariant_)
            : owner_.previewCustomOutlineFileName_;
    };
    auto* judgeLineButton = createDialogMenuButton(gameplay, judgeLineButtonText());
    judgeLineButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* judgeLineMenu = new QMenu(judgeLineButton);
    UiTheme::styleRoundedMenu(*judgeLineMenu);
    const auto addOutlineVariantChoice = [&](const QString& label, PreviewOutlineVariant variant) {
        addDialogMenuChoice(judgeLineMenu, label, [this, judgeLineButton, judgeLineLabelForVariant, variant]() {
            owner_.applyPreviewOutlineVariant(variant, false, true);
            judgeLineButton->setText(judgeLineLabelForVariant(owner_.previewOutlineVariant_));
        });
    };
    addOutlineVariantChoice(judgeLinePointLabel, PreviewOutlineVariant::Point);
    addOutlineVariantChoice(judgeLineLineLabel, PreviewOutlineVariant::Line);
    addOutlineVariantChoice(judgeLineAreaLabel, PreviewOutlineVariant::JudgeArea);
    addOutlineVariantChoice(judgeLineAreaLabeledLabel, PreviewOutlineVariant::JudgeAreaLabeled);
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
    addDialogMenuChoice(judgeLineMenu, uiText("dialog.render_settings.gameplay.judge_line.import", "Import..."), [this]() {
        const QString outlineDir = owner_.resolvePreviewCustomOutlineDir();
        if (!outlineDir.isEmpty()) {
            QDir().mkpath(outlineDir);
            QDesktopServices::openUrl(QUrl::fromLocalFile(outlineDir));
        }
    });
    judgeLineButton->setMenu(judgeLineMenu);

    // Judge effect (multi-select tap/touch/slide overlays).
    const QString slideJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.slide", "slide");
    const QString tapJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.tap", "tap");
    const QString touchJudgeChoiceLabel = uiText("dialog.render_settings.gameplay.judge_effect.touch", "touch");
    const QString disabledLabel = uiText("dialog.render_settings.option.disabled", "Disabled");
    const auto judgeEffectButtonLabel = [this, slideJudgeChoiceLabel, tapJudgeChoiceLabel, touchJudgeChoiceLabel, disabledLabel]() {
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
    auto* judgeEffectButton = createDialogMenuButton(gameplay, judgeEffectButtonLabel());
    judgeEffectButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* judgeEffectMenu = new QMenu(judgeEffectButton);
    UiTheme::styleRoundedMenu(*judgeEffectMenu);
    const auto judgeEffectChoiceText = [](const QString& label, bool enabled) {
        return QStringLiteral("%1  %2").arg(enabled ? QStringLiteral("√") : QStringLiteral("×"), label);
    };
    const auto addJudgeEffectChoice = [&](const QString& label, bool MuriRenderOptions::*memberPtr) {
        auto* action = new QWidgetAction(judgeEffectMenu);
        const bool initialChecked = owner_.muriRenderOptions_.*memberPtr;
        auto* checkbox = new QCheckBox(judgeEffectChoiceText(label, initialChecked), judgeEffectMenu);
        checkbox->setChecked(initialChecked);
        checkbox->setCursor(Qt::PointingHandCursor);
        const auto& c = UiTheme::colors();
        checkbox->setStyleSheet(
            QStringLiteral(
                "QCheckBox { color: %1; background: transparent; padding: 6px 20px 6px 12px; spacing: 0px; }"
                "QCheckBox::indicator { width: 0px; height: 0px; margin: 0px; padding: 0px; }"
                "QCheckBox:hover { background: %2; border-radius: 6px; }"
            )
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(c.menuHoverBg.name(QColor::HexRgb))
        );
        QObject::connect(checkbox, &QCheckBox::toggled, parent,
            [this, judgeEffectButton, judgeEffectButtonLabel, judgeEffectChoiceText, checkbox, label, memberPtr](bool checked) {
                if (owner_.muriRenderOptions_.*memberPtr == checked) {
                    return;
                }
                owner_.muriRenderOptions_.*memberPtr = checked;
                checkbox->setText(judgeEffectChoiceText(label, checked));
                judgeEffectButton->setText(judgeEffectButtonLabel());
                owner_.applyMuriRenderOptions();
                owner_.savePortableState();
            });
        action->setDefaultWidget(checkbox);
        judgeEffectMenu->addAction(action);
    };
    addJudgeEffectChoice(slideJudgeChoiceLabel, &MuriRenderOptions::showChartReviewSlideJudgeOverlay);
    addJudgeEffectChoice(tapJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTapJudgeOverlay);
    addJudgeEffectChoice(touchJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTouchJudgeOverlay);
    judgeEffectButton->setMenu(judgeEffectMenu);

    // Slide stack order.
    const QString slideStackOrderDxLabel = uiText("dialog.render_settings.gameplay.slide_stack_order.dx_style", "DX Style");
    const QString slideStackOrderFinaleLabel = uiText("dialog.render_settings.gameplay.slide_stack_order.finale_style", "FiNALE Style");
    const auto slideStackOrderLabelForValue = [=](bool earlierOnTop) {
        return earlierOnTop ? slideStackOrderDxLabel : slideStackOrderFinaleLabel;
    };
    auto* slideStackOrderButton = createDialogMenuButton(gameplay, slideStackOrderLabelForValue(owner_.previewSlideEarlierSecondAndTextOnTop_));
    slideStackOrderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* slideStackOrderMenu = new QMenu(slideStackOrderButton);
    UiTheme::styleRoundedMenu(*slideStackOrderMenu);
    const auto setSlideStackOrder = [this, slideStackOrderButton, slideStackOrderLabelForValue](bool earlierOnTop) {
        slideStackOrderButton->setText(slideStackOrderLabelForValue(earlierOnTop));
        if (owner_.previewSlideEarlierSecondAndTextOnTop_ == earlierOnTop) {
            return;
        }
        owner_.previewSlideEarlierSecondAndTextOnTop_ = earlierOnTop;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setSlideEarlierSecondAndTextOnTop(earlierOnTop);
        }
        owner_.savePortableState();
    };
    addDialogMenuChoice(slideStackOrderMenu, slideStackOrderDxLabel, [setSlideStackOrder]() { setSlideStackOrder(true); });
    addDialogMenuChoice(slideStackOrderMenu, slideStackOrderFinaleLabel, [setSlideStackOrder]() { setSlideStackOrder(false); });
    slideStackOrderButton->setMenu(slideStackOrderMenu);

    // Center display.
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
    auto* centerDisplayButton = createDialogMenuButton(gameplay, centerDisplayLabelForMode(owner_.previewCenterDisplayMode_));
    centerDisplayButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* centerDisplayMenu = new QMenu(centerDisplayButton);
    UiTheme::styleRoundedMenu(*centerDisplayMenu);
    const auto setCenterDisplay = [this, centerDisplayButton, centerDisplayLabelForMode](miacode::preview_gameplay::CenterDisplayMode mode) {
        centerDisplayButton->setText(centerDisplayLabelForMode(mode));
        if (owner_.previewCenterDisplayMode_ == mode) {
            return;
        }
        owner_.previewCenterDisplayMode_ = mode;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setCenterDisplayMode(mode);
        }
        owner_.savePortableState();
    };
    using miacode::preview_gameplay::CenterDisplayMode;
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
        addDialogMenuChoice(centerDisplayMenu, centerDisplayLabelForMode(mode), [setCenterDisplay, mode]() { setCenterDisplay(mode); });
    }
    centerDisplayButton->setMenu(centerDisplayMenu);

    addGameplayField(0, 0, uiText("dialog.render_settings.video.skin", "Skin"), skinButton);
    addGameplayField(0, 1, uiText("dialog.render_settings.gameplay.judge_line", "Judge Line"), judgeLineButton);
    addGameplayField(1, 0, uiText("dialog.render_settings.gameplay.judge_effect", "Judge Effect Display"), judgeEffectButton);
    addGameplayField(1, 1, uiText("dialog.render_settings.gameplay.slide_stack_order", "Slide Stack Order"), slideStackOrderButton);
    addGameplayField(2, 0, uiText("dialog.render_settings.gameplay.center_display", "Center Display"), centerDisplayButton);

    if (gameplayOut != nullptr) {
        *gameplayOut = gameplay;
    }
}
