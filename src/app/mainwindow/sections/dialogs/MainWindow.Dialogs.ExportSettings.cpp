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
        // Skip the +4 bump for controls that already fix their height (combos /
        // combo-like dropdown button); otherwise their taller QToolButton
        // sizeHint+4 pushes minimumHeight above the fixed max and the button
        // ends up taller than the sibling combo in the same row.
        if (control->minimumHeight() != control->maximumHeight()) {
            control->setMinimumHeight(qMax(control->minimumHeight(), control->sizeHint().height() + 4));
        }
        fieldLayout->addWidget(control, 0);
        gameplayLayout->addWidget(field, row, column);
    };

    // Skin + judge line moved to the shared 皮肤 tab (buildSkinSettings).

    // Judge effect (multi-select tap/break/touch/slide overlays).
    const QString slideJudgeChoiceLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect.slide"));
    const QString tapJudgeChoiceLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect.tap"));
    const QString breakJudgeChoiceLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect.break"));
    const QString touchJudgeChoiceLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect.touch"));
    const QString disabledLabel = UiText::text(QStringLiteral("dialog.render_settings.option.disabled"));
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
        gameplay, judgeEffectButtonLabel(), &judgeEffectMenu);
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
        const bool initialChecked = owner_.muriRenderOptions_.*memberPtr;
        auto* checkbox = miacode::ui::addDialogMenuCheckChoice(
            judgeEffectMenu,
            judgeEffectChoiceText(label, initialChecked),
            initialChecked,
            parent,
            [this, judgeEffectButton, judgeEffectButtonLabel, judgeEffectChoiceText, label, memberPtr](
                QCheckBox* checkbox,
                bool checked) {
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
        if (checkbox != nullptr) {
            judgeEffectRefreshEntries.push_back({checkbox, label, memberPtr});
        }
    };
    addJudgeEffectChoice(slideJudgeChoiceLabel, &MuriRenderOptions::showChartReviewSlideJudgeOverlay);
    addJudgeEffectChoice(tapJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTapJudgeOverlay);
    addJudgeEffectChoice(breakJudgeChoiceLabel, &MuriRenderOptions::showChartReviewBreakJudgeOverlay);
    addJudgeEffectChoice(touchJudgeChoiceLabel, &MuriRenderOptions::showChartReviewTouchJudgeOverlay);

    // Slide stack order.
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
    auto* slideStackOrderCombo = miacode::ui::createDialogComboBox(gameplay, 12);
    slideStackOrderCombo->addItem(slideStackOrderDxLabel, true);
    slideStackOrderCombo->addItem(slideStackOrderFinaleLabel, false);
    slideStackOrderCombo->setCurrentIndex(owner_.previewSlideEarlierSecondAndTextOnTop_ ? 0 : 1);
    miacode::ui::applyDialogComboBoxStyle(slideStackOrderCombo, 12);
    judgeEffectButton->setFixedHeight(slideStackOrderCombo->minimumHeight());
    connect(slideStackOrderCombo,
            qOverload<int>(&QComboBox::currentIndexChanged),
            gameplay,
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
    auto* tapJudgeTextDistanceCombo = miacode::ui::createDialogComboBox(gameplay, 12);
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
            gameplay,
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

    // Center display.
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
    auto* centerDisplayCombo = miacode::ui::createDialogComboBox(gameplay, 12);
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
            gameplay,
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

    addGameplayField(0, 0, UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_effect")), judgeEffectButton);
    addGameplayField(0, 1, UiText::text(QStringLiteral("dialog.render_settings.gameplay.slide_stack_order")), slideStackOrderCombo);
    addGameplayField(1, 0, UiText::text(QStringLiteral("dialog.render_settings.gameplay.center_display")), centerDisplayCombo);
    addGameplayField(1, 1, UiText::text(QStringLiteral("dialog.render_settings.gameplay.tap_judge_text_distance")), tapJudgeTextDistanceCombo);

    if (refreshOut != nullptr) {
        const QPointer<QWidget> gameplayGuard(gameplay);
        *refreshOut =
            [this,
             gameplayGuard,
             judgeEffectButton,
             judgeEffectButtonLabel,
             judgeEffectChoiceText,
             judgeEffectRefreshEntries,
             slideStackOrderCombo,
             centerDisplayCombo,
             tapJudgeTextDistanceCombo]() {
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
                if (slideStackOrderCombo != nullptr) {
                    const QSignalBlocker blocker(slideStackOrderCombo);
                    slideStackOrderCombo->setCurrentIndex(
                        owner_.previewSlideEarlierSecondAndTextOnTop_ ? 0 : 1);
                }
                if (centerDisplayCombo != nullptr) {
                    const QSignalBlocker blocker(centerDisplayCombo);
                    centerDisplayCombo->setCurrentIndex(qMax(
                        0,
                        centerDisplayCombo->findData(
                            static_cast<int>(owner_.previewCenterDisplayMode_))));
                }
                if (tapJudgeTextDistanceCombo != nullptr) {
                    const QSignalBlocker blocker(tapJudgeTextDistanceCombo);
                    tapJudgeTextDistanceCombo->setCurrentIndex(qMax(
                        0,
                        tapJudgeTextDistanceCombo->findData(
                            static_cast<int>(owner_.previewTapJudgeTextDistance_))));
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
    auto* root = new QWidget(parent);
    auto* rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(10);

    const auto comboActionRow = [root](QComboBox* combo, QPushButton* button) -> QWidget* {
        auto* row = new QWidget(root);
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        combo->ensurePolished();
        button->ensurePolished();
        // The combo carries the W1 fixed height from createDialogComboBox
        // (minimumHeight == the fixed value); level the action button to it.
        const int rowHeight = qMax(
            combo->minimumHeight(),
            qMax(combo->sizeHint().height(), button->sizeHint().height()));
        button->setFixedHeight(rowHeight);
        layout->addWidget(combo, 1);
        layout->addWidget(button, 0);
        return row;
    };

    // ---- 谱面皮肤: skin + judge line (+ optional open-folder actions) ----
    auto* skinForm = miacode::ui::createFormGroup(
        UiText::text(QStringLiteral("dialog.skin_settings.section.chart_skin")), root, rootLayout);

    auto* skinCombo = miacode::ui::createDialogComboBox(root, 12, Qt::AlignLeft | Qt::AlignVCenter);
    QPushButton* openSkinDirectoryButton = nullptr;
    if (includeFolderButtons) {
        openSkinDirectoryButton = miacode::ui::createDialogAuxiliaryButton(
            root, UiText::text(QStringLiteral("dialog.skin_settings.open_directory")));
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
        miacode::ui::applyDialogComboBoxStyle(skinCombo, 12);
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
        UiText::text(QStringLiteral("dialog.render_settings.video.skin")),
        openSkinDirectoryButton != nullptr ? comboActionRow(skinCombo, openSkinDirectoryButton) : skinCombo);

    const auto judgeEffectStyleLabelForValue = [](PreviewJudgeEffectStyle style) -> QString {
        switch (style) {
        case PreviewJudgeEffectStyle::Starry:
            return UiText::text(QStringLiteral("dialog.skin_settings.chart_effect.starry"));
        case PreviewJudgeEffectStyle::Standard:
        default:
            return UiText::text(QStringLiteral("dialog.skin_settings.chart_effect.standard"));
        }
    };
    auto* chartEffectCombo = miacode::ui::createDialogComboBox(root, 12, Qt::AlignLeft | Qt::AlignVCenter);
    for (const PreviewJudgeEffectStyle style : {
             PreviewJudgeEffectStyle::Standard,
             PreviewJudgeEffectStyle::Starry,
         }) {
        chartEffectCombo->addItem(judgeEffectStyleLabelForValue(style), static_cast<int>(style));
    }
    chartEffectCombo->setCurrentIndex(
        qMax(0, chartEffectCombo->findData(static_cast<int>(owner_.previewJudgeEffectStyle_))));
    miacode::ui::applyDialogComboBoxStyle(chartEffectCombo, 12);
    connect(chartEffectCombo, qOverload<int>(&QComboBox::currentIndexChanged), root, [this, chartEffectCombo](int index) {
        if (index < 0) {
            return;
        }
        const auto style = static_cast<PreviewJudgeEffectStyle>(chartEffectCombo->itemData(index).toInt());
        if (owner_.previewJudgeEffectStyle_ == style) {
            return;
        }
        owner_.previewJudgeEffectStyle_ = style;
        if (owner_.previewCanvas_ != nullptr) {
            owner_.previewCanvas_->setJudgeEffectStyle(style);
        }
        owner_.savePortableState();
    });
    const QString judgeLinePointLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_line.point"));
    const QString judgeLineLineLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_line.line"));
    const QString judgeLineAreaLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_line.area"));
    const QString judgeLineAreaLabeledLabel = UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_line.area_labeled"));
    auto* judgeLineCombo = miacode::ui::createDialogComboBox(root, 12, Qt::AlignLeft | Qt::AlignVCenter);
    QPushButton* openJudgeLineDirectoryButton = nullptr;
    if (includeFolderButtons) {
        openJudgeLineDirectoryButton = miacode::ui::createDialogAuxiliaryButton(
            root, UiText::text(QStringLiteral("dialog.skin_settings.open_directory")));
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
        miacode::ui::applyDialogComboBoxStyle(judgeLineCombo, 12);
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
        UiText::text(QStringLiteral("dialog.render_settings.gameplay.judge_line")),
        openJudgeLineDirectoryButton != nullptr ? comboActionRow(judgeLineCombo, openJudgeLineDirectoryButton) : judgeLineCombo);
    skinForm->addRow(
        UiText::text(QStringLiteral("dialog.skin_settings.chart_effect")),
        chartEffectCombo);

    // ---- 字体: embedded HUD-font picker (combo + import/reset + live sample),
    //      the same controls the old "字体设置" sub-dialog hosted, inlined. ----
    auto* fontGroup = new QGroupBox(UiText::text(QStringLiteral("dialog.video_export.section.font")), root);
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
            [this, rootGuard, refreshSkinCombo, refreshJudgeLineCombo, refreshHudFontSettings, chartEffectCombo]() {
                if (rootGuard.isNull()) {
                    return;
                }
                refreshSkinCombo();
                if (chartEffectCombo != nullptr) {
                    const QSignalBlocker blocker(chartEffectCombo);
                    chartEffectCombo->setCurrentIndex(qMax(
                        0,
                        chartEffectCombo->findData(static_cast<int>(owner_.previewJudgeEffectStyle_))));
                }
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
    miacode::ui::TabbedSettingsDialog dialog(
        &owner_,
        UiText::text(QStringLiteral("dialog.skin_settings.dialog_title")),
        miacode::ui::SettingsDialogChrome::Settings);
    QVBoxLayout* rootLayout = dialog.contentLayout();

    QWidget* skinContent = nullptr;
    buildSkinSettings(&dialog, &skinContent, /*includeFolderButtons=*/true);
    if (skinContent != nullptr) {
        skinContent->setMinimumWidth(360);
        rootLayout->addWidget(skinContent, 0);
    }

    dialog.addCloseButton(UiText::text(QStringLiteral("dialog.render_settings.button.close")), true);

    dialog.adjustSize();
    dialog.exec();
}
