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
#include "core/scene/PreviewHudState.h"
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
    QWidget** gameplayOut,
    std::function<void()>* refreshOut)
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
    //      center display. The VideoExportDialog owns the Tap/Touch flow-speed
    //      row above this injected MainWindow-wired grid. ----
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
        fieldLayout->setContentsMargins(0, 0, 0, 3);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        control->setMinimumHeight(qMax(control->minimumHeight(), control->sizeHint().height() + 4));
        fieldLayout->addWidget(control, 0);
        gameplayLayout->addWidget(field, row, column);
    };

    // Skin + judge line moved to the shared 皮肤 tab (buildSkinSettings).

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
    UiTheme::bindDialogMenuButtonPopupState(judgeEffectButton, judgeEffectMenu);
    const auto judgeEffectChoiceText = [](const QString& label, bool /*enabled*/) {
        return label;
    };
    struct JudgeEffectRefreshEntry {
        QCheckBox* checkbox = nullptr;
        QString label;
        bool MuriRenderOptions::*memberPtr = nullptr;
    };
    QVector<JudgeEffectRefreshEntry> judgeEffectRefreshEntries;
    const auto addJudgeEffectChoice = [&](const QString& label, bool MuriRenderOptions::*memberPtr) {
        auto* action = new QWidgetAction(judgeEffectMenu);
        const bool initialChecked = owner_.muriRenderOptions_.*memberPtr;
        auto* checkbox = new QCheckBox(judgeEffectChoiceText(label, initialChecked), judgeEffectMenu);
        checkbox->setChecked(initialChecked);
        judgeEffectRefreshEntries.push_back({checkbox, label, memberPtr});
        checkbox->setCursor(Qt::PointingHandCursor);
        checkbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        checkbox->setStyleSheet(UiTheme::dialogMenuCheckBoxStyleSheet());
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
    UiTheme::bindDialogMenuButtonPopupState(slideStackOrderButton, slideStackOrderMenu);
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
    UiTheme::bindDialogMenuButtonPopupState(centerDisplayButton, centerDisplayMenu);
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

    addGameplayField(0, 0, uiText("dialog.render_settings.gameplay.judge_effect", "Judge Effect Display"), judgeEffectButton);
    addGameplayField(0, 1, uiText("dialog.render_settings.gameplay.slide_stack_order", "Slide Stack Order"), slideStackOrderButton);
    addGameplayField(1, 0, uiText("dialog.render_settings.gameplay.center_display", "Center Display"), centerDisplayButton);

    if (refreshOut != nullptr) {
        const QPointer<QWidget> gameplayGuard(gameplay);
        *refreshOut =
            [this,
             gameplayGuard,
             judgeEffectButton,
             judgeEffectButtonLabel,
             judgeEffectChoiceText,
             judgeEffectRefreshEntries,
             slideStackOrderButton,
             slideStackOrderLabelForValue,
             centerDisplayButton,
             centerDisplayLabelForMode]() {
                if (gameplayGuard.isNull()) {
                    return;
                }
                if (judgeEffectButton != nullptr) {
                    judgeEffectButton->setText(judgeEffectButtonLabel());
                }
                for (const JudgeEffectRefreshEntry& entry : judgeEffectRefreshEntries) {
                    if (entry.checkbox == nullptr || entry.memberPtr == nullptr) {
                        continue;
                    }
                    const bool checked = owner_.muriRenderOptions_.*(entry.memberPtr);
                    const QSignalBlocker blocker(entry.checkbox);
                    entry.checkbox->setChecked(checked);
                    entry.checkbox->setText(judgeEffectChoiceText(entry.label, checked));
                }
                if (slideStackOrderButton != nullptr) {
                    slideStackOrderButton->setText(
                        slideStackOrderLabelForValue(owner_.previewSlideEarlierSecondAndTextOnTop_));
                }
                if (centerDisplayButton != nullptr) {
                    centerDisplayButton->setText(centerDisplayLabelForMode(owner_.previewCenterDisplayMode_));
                }
            };
    }

    if (gameplayOut != nullptr) {
        *gameplayOut = gameplay;
    }
}

void MainWindow::DialogsSection::buildSkinSettings(
    QWidget* parent,
    QWidget** skinOut,
    bool includeFolderButtons,
    std::function<void()>* refreshOut)
{
    const auto createDialogComboBox = [](QWidget* owner) {
        auto* combo = new QComboBox(owner);
        combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        UiTheme::applyComboBoxPopupLimit(combo, 12);
        return combo;
    };
    const auto makePushButton = [](QWidget* owner, const QString& text) {
        auto* button = new QPushButton(text, owner);
        button->setObjectName(QStringLiteral("DialogAuxiliaryButton"));
        button->setProperty("miacodeAuxiliaryButton", true);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(UiTheme::dialogAuxiliaryButtonStyleSheet());
        return button;
    };

    auto* root = new QWidget(parent);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(10);

    const auto makeGroupForm = [root, rootLayout](const QString& title) -> QFormLayout* {
        auto* group = new QGroupBox(title, root);
        auto* form = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setHorizontalSpacing(10);
        form->setVerticalSpacing(8);
        form->setContentsMargins(10, 8, 10, 8);
        rootLayout->addWidget(group);
        return form;
    };
    const auto comboActionRow = [root](QComboBox* combo, QPushButton* button) -> QWidget* {
        auto* row = new QWidget(root);
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        combo->ensurePolished();
        button->ensurePolished();
        const int rowHeight = qMax(combo->sizeHint().height(), button->sizeHint().height());
        combo->setMinimumHeight(rowHeight);
        button->setFixedHeight(rowHeight);
        layout->addWidget(combo, 1);
        layout->addWidget(button, 0);
        return row;
    };

    // ---- 谱面皮肤: skin + judge line (+ optional open-folder actions) ----
    auto* skinForm = makeGroupForm(uiText("dialog.skin_settings.section.chart_skin", "Chart Skin"));

    auto* skinCombo = createDialogComboBox(root);
    QPushButton* openSkinDirectoryButton = nullptr;
    if (includeFolderButtons) {
        openSkinDirectoryButton = makePushButton(root, uiText("dialog.skin_settings.open_directory", "Open Directory"));
        connect(openSkinDirectoryButton, &QPushButton::clicked, root, [this]() {
            const QString skinRoot = owner_.resolvePreviewSkinRootDir();
            if (!skinRoot.isEmpty()) {
                QDir().mkpath(skinRoot);
                QDesktopServices::openUrl(QUrl::fromLocalFile(skinRoot));
            }
        });
    }
    const auto refreshSkinCombo = [this, skinCombo]() {
        const QSignalBlocker blocker(skinCombo);
        skinCombo->clear();
        QStringList skinDirectoryNames = owner_.availablePreviewSkinDirectoryNames();
        const QString currentSkinDirectoryName = owner_.previewSkinDirectoryName_;
        const bool currentListed = std::any_of(
            skinDirectoryNames.cbegin(),
            skinDirectoryNames.cend(),
            [&currentSkinDirectoryName](const QString& name) {
                return name.compare(currentSkinDirectoryName, Qt::CaseInsensitive) == 0;
            });
        if (!currentSkinDirectoryName.isEmpty() && !currentListed) {
            skinDirectoryNames.prepend(currentSkinDirectoryName);
        }

        int selectedIndex = 0;
        for (int i = 0; i < skinDirectoryNames.size(); ++i) {
            const QString& skinDirectoryName = skinDirectoryNames.at(i);
            skinCombo->addItem(owner_.previewSkinDisplayName(skinDirectoryName), skinDirectoryName);
            if (skinDirectoryName.compare(currentSkinDirectoryName, Qt::CaseInsensitive) == 0) {
                selectedIndex = i;
            }
        }
        skinCombo->setCurrentIndex(skinCombo->count() > 0 ? selectedIndex : -1);
        UiTheme::applyComboBoxPopupLimit(skinCombo, 12);
    };
    refreshSkinCombo();
    connect(skinCombo, qOverload<int>(&QComboBox::currentIndexChanged), root, [this, skinCombo](int index) {
        if (index < 0) {
            return;
        }
        const QString skinDirectoryName = skinCombo->itemData(index).toString();
        if (owner_.previewSkinDirectoryName_.compare(skinDirectoryName, Qt::CaseInsensitive) == 0) {
            return;
        }
        owner_.previewSkinDirectoryName_ = skinDirectoryName;
        owner_.previewSkinVariant_ =
            skinDirectoryName.compare(QStringLiteral("skinDX"), Qt::CaseInsensitive) == 0
                ? PreviewSkinVariant::Dx
                : PreviewSkinVariant::Standard;
        owner_.applyPreviewSkinDirectoryToSurfaces();
        owner_.savePortableState();
    });
    skinForm->addRow(
        uiText("dialog.render_settings.video.skin", "Skin"),
        openSkinDirectoryButton != nullptr ? comboActionRow(skinCombo, openSkinDirectoryButton) : skinCombo);

    const QString judgeLinePointLabel = uiText("dialog.render_settings.gameplay.judge_line.point", "Point");
    const QString judgeLineLineLabel = uiText("dialog.render_settings.gameplay.judge_line.line", "Line");
    const QString judgeLineAreaLabel = uiText("dialog.render_settings.gameplay.judge_line.area", "Judge Area");
    const QString judgeLineAreaLabeledLabel = uiText("dialog.render_settings.gameplay.judge_line.area_labeled", "Judge Area (Labeled)");
    auto* judgeLineCombo = createDialogComboBox(root);
    QPushButton* openJudgeLineDirectoryButton = nullptr;
    if (includeFolderButtons) {
        openJudgeLineDirectoryButton = makePushButton(root, uiText("dialog.skin_settings.open_directory", "Open Directory"));
        connect(openJudgeLineDirectoryButton, &QPushButton::clicked, root, [this]() {
            const QString outlineDir = owner_.resolvePreviewCustomOutlineDir();
            if (!outlineDir.isEmpty()) {
                QDir().mkpath(outlineDir);
                QDesktopServices::openUrl(QUrl::fromLocalFile(outlineDir));
            }
        });
    }
    constexpr int kOutlineVariantKind = 0;
    constexpr int kCustomOutlineKind = 1;
    const auto addOutlineVariantComboItem = [judgeLineCombo, kOutlineVariantKind](const QString& label, PreviewOutlineVariant variant) {
        judgeLineCombo->addItem(label);
        const int index = judgeLineCombo->count() - 1;
        judgeLineCombo->setItemData(index, kOutlineVariantKind, Qt::UserRole);
        judgeLineCombo->setItemData(index, static_cast<int>(variant), Qt::UserRole + 1);
    };
    const auto addCustomOutlineComboItem = [judgeLineCombo, kCustomOutlineKind](const QString& fileName) {
        judgeLineCombo->addItem(fileName);
        const int index = judgeLineCombo->count() - 1;
        judgeLineCombo->setItemData(index, kCustomOutlineKind, Qt::UserRole);
        judgeLineCombo->setItemData(index, fileName, Qt::UserRole + 1);
    };
    const auto refreshJudgeLineCombo = [
        this,
        judgeLineCombo,
        addOutlineVariantComboItem,
        addCustomOutlineComboItem,
        judgeLinePointLabel,
        judgeLineLineLabel,
        judgeLineAreaLabel,
        judgeLineAreaLabeledLabel,
        kOutlineVariantKind,
        kCustomOutlineKind
    ]() {
        const QSignalBlocker blocker(judgeLineCombo);
        judgeLineCombo->clear();
        addOutlineVariantComboItem(judgeLinePointLabel, PreviewOutlineVariant::Point);
        addOutlineVariantComboItem(judgeLineLineLabel, PreviewOutlineVariant::Line);
        addOutlineVariantComboItem(judgeLineAreaLabel, PreviewOutlineVariant::JudgeArea);
        addOutlineVariantComboItem(judgeLineAreaLabeledLabel, PreviewOutlineVariant::JudgeAreaLabeled);

        QStringList customOutlineNames = owner_.availablePreviewCustomOutlineFileNames();
        const QString currentCustomOutline = owner_.previewCustomOutlineFileName_;
        const bool currentCustomListed = std::any_of(
            customOutlineNames.cbegin(),
            customOutlineNames.cend(),
            [&currentCustomOutline](const QString& name) {
                return name.compare(currentCustomOutline, Qt::CaseInsensitive) == 0;
            });
        if (!currentCustomOutline.isEmpty() && !currentCustomListed) {
            customOutlineNames.prepend(currentCustomOutline);
        }
        for (const QString& fileName : customOutlineNames) {
            addCustomOutlineComboItem(fileName);
        }

        int selectedIndex = 0;
        for (int i = 0; i < judgeLineCombo->count(); ++i) {
            const int kind = judgeLineCombo->itemData(i, Qt::UserRole).toInt();
            if (currentCustomOutline.isEmpty()
                && kind == kOutlineVariantKind
                && static_cast<PreviewOutlineVariant>(judgeLineCombo->itemData(i, Qt::UserRole + 1).toInt()) == owner_.previewOutlineVariant_) {
                selectedIndex = i;
                break;
            }
            if (!currentCustomOutline.isEmpty()
                && kind == kCustomOutlineKind
                && judgeLineCombo->itemData(i, Qt::UserRole + 1).toString().compare(currentCustomOutline, Qt::CaseInsensitive) == 0) {
                selectedIndex = i;
                break;
            }
        }
        judgeLineCombo->setCurrentIndex(selectedIndex);
        UiTheme::applyComboBoxPopupLimit(judgeLineCombo, 12);
    };
    refreshJudgeLineCombo();
    connect(judgeLineCombo, qOverload<int>(&QComboBox::currentIndexChanged), root, [this, judgeLineCombo, kOutlineVariantKind](int index) {
        if (index < 0) {
            return;
        }
        const int kind = judgeLineCombo->itemData(index, Qt::UserRole).toInt();
        if (kind == kOutlineVariantKind) {
            const auto variant =
                static_cast<PreviewOutlineVariant>(judgeLineCombo->itemData(index, Qt::UserRole + 1).toInt());
            if (owner_.previewCustomOutlineFileName_.isEmpty() && owner_.previewOutlineVariant_ == variant) {
                return;
            }
            owner_.applyPreviewOutlineVariant(variant, false, true);
            return;
        }
        const QString fileName = judgeLineCombo->itemData(index, Qt::UserRole + 1).toString();
        if (owner_.previewCustomOutlineFileName_.compare(fileName, Qt::CaseInsensitive) == 0) {
            return;
        }
        owner_.applyPreviewCustomOutlineFileName(fileName, true);
    });
    skinForm->addRow(
        uiText("dialog.render_settings.gameplay.judge_line", "Judge Line"),
        openJudgeLineDirectoryButton != nullptr ? comboActionRow(judgeLineCombo, openJudgeLineDirectoryButton) : judgeLineCombo);

    // ---- 字体: embedded HUD-font picker (combo + import/reset + live sample),
    //      the same controls the old "字体设置" sub-dialog hosted, inlined. ----
    auto* fontGroup = new QGroupBox(uiText("dialog.video_export.section.font", "Font"), root);
    auto* fontGroupLayout = new QVBoxLayout(fontGroup);
    fontGroupLayout->setContentsMargins(10, 8, 10, 8);
    fontGroupLayout->setSpacing(8);
    // {} refresh callback: the preview HUD re-reads the global font on its next
    // repaint (scrub/play), matching the former font-tab behavior.
    std::function<void()> refreshHudFontSettings;
    fontGroupLayout->addWidget(
        miacode::video_export::createHudFontSettingsWidget(fontGroup, {}, &refreshHudFontSettings));
    rootLayout->addWidget(fontGroup);

    if (refreshOut != nullptr) {
        const QPointer<QWidget> rootGuard(root);
        *refreshOut =
            [rootGuard, refreshSkinCombo, refreshJudgeLineCombo, refreshHudFontSettings]() {
                if (rootGuard.isNull()) {
                    return;
                }
                refreshSkinCombo();
                refreshJudgeLineCombo();
                if (refreshHudFontSettings) {
                    refreshHudFontSettings();
                }
            };
    }

    if (skinOut != nullptr) {
        *skinOut = root;
    }
}

void MainWindow::DialogsSection::onSkinSettings()
{
    MC_OP("MainWindow::DialogsSection::onSkinSettings");
    openSkinSettingsDialog();
}

void MainWindow::DialogsSection::openSkinSettingsDialog()
{
    QDialog dialog(UiDialogs::effectiveParentWidget(&owner_));
    dialog.setWindowTitle(uiText("dialog.skin_settings.dialog_title", "Skin Settings"));
    dialog.setModal(true);
    dialog.setStyleSheet(UiTheme::settingsDialogStyleSheet());
    owner_.windowSection_->applySystemWindowBackdrop(&dialog);
    UiDialogs::prepareDialogWindow(&dialog, &owner_);

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);
    rootLayout->setSizeConstraint(QLayout::SetFixedSize);

    QWidget* skinContent = nullptr;
    buildSkinSettings(&dialog, &skinContent, /*includeFolderButtons=*/true);
    if (skinContent != nullptr) {
        skinContent->setMinimumWidth(360);
        rootLayout->addWidget(skinContent, 0);
    }

    auto* buttonBox = new QDialogButtonBox(&dialog);
    if (QPushButton* closeButton = buttonBox->addButton(uiText("dialog.render_settings.button.close", "Close"), QDialogButtonBox::RejectRole)) {
        closeButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    }
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);

    dialog.adjustSize();
    dialog.exec();
}
