#include "tools/cover_export/CoverStudioPanel.h"

#include "tools/cover_export/CoverCompositionState.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/SceneFrameRenderer.h"
#include "common/PreviewInteractionConfig.h"
#include "UiNativeWindowTheme.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantMap>

namespace {

struct CoverResolutionPreset {
    int width;
    int height;
    const char* label;
};

// Mirrors VideoExportDialog's resolution presets so the cover size matches the
// video-size choices (tracked independently here).
constexpr CoverResolutionPreset kCoverResolutionPresets[] = {
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

// Longest side of the live-preview pane in logical px; the preview letterboxes
// to the chosen output aspect so the layout matches the export exactly. Sized
// generously so the card/frame are comfortable to drag-place and resize.
constexpr int kPreviewBox = 560;

// Visual-only playback tick (~60 fps). The live scene repaints from the shared
// playhead each tick; real elapsed wall time (not the tick count) sets the time,
// so playback stays at real speed even if a tick is late.
constexpr int kPlayTickMs = 16;

// Painted play / pause icons for the transport button (visual-only playback,
// no audio). Font glyphs proved uncontrollable: U+23F8 ⏸ renders as a COLOR
// emoji on Windows and the U+275A ❚❚ fallback was too heavy/wide — painting
// gives exact bar thickness / gap and the theme text colour. (Same pattern as
// the toolbar's makeSettingsGearIcon.)
QIcon makeTransportIcon(bool pause, const QColor& color)
{
    constexpr int kSize = 16;     // logical icon size
    constexpr qreal kDpr = 2.0;   // crisp on high-DPI
    QPixmap pixmap(qRound(kSize * kDpr), qRound(kSize * kDpr));
    pixmap.setDevicePixelRatio(kDpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    if (pause) {
        // Two slim bars with a tight gap (the whole point vs the glyph version).
        const qreal barWidth = 2.5;
        const qreal gap = 3.0;
        const qreal barHeight = 10.0;
        const qreal top = (kSize - barHeight) / 2.0;
        painter.drawRoundedRect(
            QRectF(kSize / 2.0 - gap / 2.0 - barWidth, top, barWidth, barHeight), 1.0, 1.0);
        painter.drawRoundedRect(
            QRectF(kSize / 2.0 + gap / 2.0, top, barWidth, barHeight), 1.0, 1.0);
    } else {
        // Right-pointing triangle with similar visual mass.
        const qreal height = 10.0;
        const qreal width = 9.0;
        const qreal left = (kSize - width) / 2.0 + 0.5;
        const qreal top = (kSize - height) / 2.0;
        QPainterPath triangle;
        triangle.moveTo(left, top);
        triangle.lineTo(left, top + height);
        triangle.lineTo(left + width, top + height / 2.0);
        triangle.closeSubpath();
        painter.fillPath(triangle, color);
    }
    return QIcon(pixmap);
}

// Chart-frame square render-size clamp (px); the natural size is the layer's
// export pixel height, but keep it sane for tiny / huge canvases.
constexpr int kChartFrameMinPx = 384;
constexpr int kChartFrameMaxPx = 2048;

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

// mm:ss.cs (centiseconds) for the frame-time readout.
QString formatFrameTime(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int totalCs = qRound(seconds * 100.0);
    const int cs = totalCs % 100;
    const int totalSec = totalCs / 100;
    const int s = totalSec % 60;
    const int m = totalSec / 60;
    return QStringLiteral("%1:%2.%3")
        .arg(m)
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(cs, 2, 10, QLatin1Char('0'));
}

QVariantMap loadBannerTemplate()
{
    QVariantMap templateMap;
    QFile templateFile(QStringLiteral(":/intro/templates/maimai_banner.json"));
    if (templateFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(templateFile.readAll());
        if (doc.isObject()) {
            templateMap = doc.object().toVariantMap();
        }
    }
    return templateMap;
}

}  // namespace

namespace miacode::cover_export {

CoverStudioPanel::CoverStudioPanel(const VideoExportTask& task, const QSize& initialSize, QWidget* parent)
    : QWidget(parent)
    , banner_(task.intro)
{
    setWindowTitle(l10n(QStringLiteral("Export Cover"), QStringLiteral("导出封面")));
    // Theme the native title bar — this dialog is opened from the tools
    // layer, so no MainWindow-side applySystemWindowBackdrop call covers it.
    UiNativeWindowTheme::applyToWidget(this);
    // exportDialogStyleSheet has no QGroupBox rule — append one for the three
    // option sections (same look as the preferences dialog's groups).
    setStyleSheet(UiTheme::exportDialogStyleSheet()
        + QStringLiteral(
              "QGroupBox { border: 1px solid %1; border-radius: 8px; margin-top: 12px;"
              " padding-top: 8px; color: %2; font-weight: 600; }"
              "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }")
              .arg(UiTheme::colors().borderSoft.name(QColor::HexRgb),
                   UiTheme::colors().textPrimary.name(QColor::HexRgb)));

    const QSize seededSize = initialSize.isValid() ? initialSize : QSize(1080, 1080);

    model_ = new miacode::cover_export::CoverLayoutModel(this);
    composerView_ = new miacode::cover_export::CoverComposerView(model_, this);
    cachedTemplate_ = loadBannerTemplate();

    // Bootstrap the in-process chart-frame renderer once from the export task.
    // Unavailable (→ the toggle stays disabled) when the difficulty has no notes
    // or the skin can't load.
    contentDurationSeconds_ = qMax(0.0, task.contentDurationSeconds);
    sceneFrameRenderer_ = std::make_unique<miacode::cover_export::SceneFrameRenderer>();
    if (!task.noteMarkers.isEmpty()) {
        chartFrameAvailable_ = sceneFrameRenderer_->bootstrap(task);
    }
    // A2 — hand the live composer scene the SAME bootstrapped frame state, so the
    // chart-frame layer can scrub/play by moving only the shared playhead (the
    // offscreen grab stays reserved for the export still).
    if (chartFrameAvailable_ && composerView_ != nullptr) {
        composerView_->setChartFrameState(sceneFrameRenderer_->frameState());
    }
    setActiveLayerKey(miacode::cover_export::CoverLayoutModel::cardKey());

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---- Left: live composer preview ----
    auto* previewColumn = new QVBoxLayout();
    previewColumn->setSpacing(8);

    auto* previewFrame = new QFrame(this);
    previewFrame_ = previewFrame;
    previewFrame->setFrameShape(QFrame::StyledPanel);
    previewFrame->setStyleSheet(QStringLiteral(
        "QFrame { background: #14141C; border: 1px solid rgba(255,255,255,40); border-radius: 6px; }"));
    auto* previewFrameLayout = new QVBoxLayout(previewFrame);
    previewFrameLayout->setContentsMargins(1, 1, 1, 1);

    QWidget* container = composerView_->createContainer(previewFrame);
    if (container != nullptr) {
        previewContainer_ = container;
        previewFrameLayout->addWidget(previewContainer_, 0, Qt::AlignCenter);
        // Esc must close the dialog even while focus sits inside the embedded
        // NATIVE Quick window (after clicking the preview, keys go there and
        // never reach QDialog's default Esc-reject) — filter it ourselves.
        if (QObject* quickWindow = composerView_->previewWindowObject()) {
            quickWindow->installEventFilter(this);
        }
    } else {
        auto* failed = new QLabel(
            l10n(QStringLiteral("Failed to start the composer:\n%1"),
                 QStringLiteral("合成器启动失败：\n%1")).arg(composerView_->lastError()),
            previewFrame);
        failed->setWordWrap(true);
        previewFrameLayout->addWidget(failed);
    }
    previewColumn->addWidget(previewFrame, 1);

    resetLayoutButton_ = new QPushButton(
        l10n(QStringLiteral("Reset layout"), QStringLiteral("重置布局")), this);
    resetLayoutButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    saveLayoutButton_ = new QPushButton(
        l10n(QStringLiteral("Save layout…"), QStringLiteral("保存布局…")), this);
    saveLayoutButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    importLayoutButton_ = new QPushButton(
        l10n(QStringLiteral("Import layout…"), QStringLiteral("导入布局…")), this);
    importLayoutButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    resetLayoutButton_->hide();
    saveLayoutButton_->hide();
    importLayoutButton_->hide();

    rootLayout->addLayout(previewColumn, 1);

    // ---- Right: controls, in three sections — canvas (size / background), the
    // difficulty card, and the chart frame. The card and the frame are each
    // opt-in layers behind their leading "add" checkbox. ----
    hiddenControlsHost_ = new QWidget(this);
    hiddenControlsHost_->hide();
    auto* controlsColumn = new QVBoxLayout(hiddenControlsHost_);
    controlsColumn->setSpacing(10);

    auto* canvasGroup = new QGroupBox(
        l10n(QStringLiteral("Size / Background"), QStringLiteral("尺寸 / 背景")), this);
    auto* form = new QFormLayout(canvasGroup);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Size.
    sizeCombo_ = new QComboBox(this);
    int selectedIndex = -1;
    for (const CoverResolutionPreset& preset : kCoverResolutionPresets) {
        const QSize size(preset.width, preset.height);
        sizeCombo_->addItem(QString::fromLatin1(preset.label), size);
        if (size == seededSize) {
            selectedIndex = sizeCombo_->count() - 1;
        }
    }
    if (selectedIndex < 0) {
        sizeCombo_->addItem(
            QStringLiteral("%1x%2").arg(seededSize.width()).arg(seededSize.height()), seededSize);
        selectedIndex = sizeCombo_->count() - 1;
    }
    sizeCombo_->setCurrentIndex(selectedIndex);
    form->addRow(l10n(QStringLiteral("Size"), QStringLiteral("尺寸")), sizeCombo_);

    // Background source.
    backgroundCombo_ = new QComboBox(this);
    backgroundCombo_->addItem(l10n(QStringLiteral("Chart jacket (曲绘)"), QStringLiteral("曲绘")),
                              QStringLiteral("jacket"));
    backgroundCombo_->addItem(l10n(QStringLiteral("Custom image"), QStringLiteral("自定义图片")),
                              QStringLiteral("custom"));
    backgroundCombo_->addItem(l10n(QStringLiteral("Transparent"), QStringLiteral("透明")),
                              QStringLiteral("transparent"));
    form->addRow(l10n(QStringLiteral("Background"), QStringLiteral("背景")), backgroundCombo_);

    // Custom background path + browse.
    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    backgroundPathEdit_ = new QLineEdit(pathRow);
    backgroundPathEdit_->setPlaceholderText(
        l10n(QStringLiteral("Custom background image path"), QStringLiteral("自定义背景图片路径")));
    backgroundBrowse_ = new QPushButton(l10n(QStringLiteral("Browse…"), QStringLiteral("浏览…")), pathRow);
    backgroundBrowse_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    pathLayout->addWidget(backgroundPathEdit_, 1);
    pathLayout->addWidget(backgroundBrowse_, 0);
    form->addRow(QString(), pathRow);

    // Backdrop blur.
    blurCheck_ = new QCheckBox(l10n(QStringLiteral("Blur background"), QStringLiteral("背景虚化")), this);
    blurCheck_->setChecked(true);
    form->addRow(QString(), blurCheck_);

    controlsColumn->addWidget(canvasGroup);

    // ---- Difficulty-card section (an opt-in layer, like the chart frame) ----
    auto* cardGroup = new QGroupBox(
        l10n(QStringLiteral("Difficulty card"), QStringLiteral("难度卡")), this);
    auto* cardForm = new QFormLayout(cardGroup);
    cardForm->setSpacing(10);
    cardForm->setLabelAlignment(Qt::AlignLeft);
    cardForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    cardCheck_ = new QCheckBox(
        l10n(QStringLiteral("Add difficulty card"), QStringLiteral("添加难度卡")), this);
    cardCheck_->setChecked(true);
    cardForm->addRow(QString(), cardCheck_);

    // DX / SD chart type. "Standard" is the QML-side mode value (the card shows
    // the スタンダード plate top-right and mirrors the tab shoulder); anything
    // else shows the でらっくす plate top-left.
    cardModeCombo_ = new QComboBox(this);
    cardModeCombo_->addItem(QStringLiteral("DX"), QStringLiteral("DX"));
    cardModeCombo_->addItem(QStringLiteral("SD"), QStringLiteral("Standard"));
    {
        const int modeIdx = cardModeCombo_->findData(banner_.mode);
        if (modeIdx >= 0) cardModeCombo_->setCurrentIndex(modeIdx);
    }
    cardForm->addRow(l10n(QStringLiteral("Chart type"), QStringLiteral("谱面类型")), cardModeCombo_);

    // Card drop shadow.
    cardShadowCheck_ = new QCheckBox(l10n(QStringLiteral("Card drop shadow"), QStringLiteral("难度卡阴影")), this);
    cardShadowCheck_->setChecked(false);
    cardForm->addRow(QString(), cardShadowCheck_);

    // Level text render.
    levelTextRenderCheck_ = new QCheckBox(
        l10n(QStringLiteral("Render level as text"), QStringLiteral("等级文本渲染")), this);
    levelTextRenderCheck_->setChecked(false);
    cardForm->addRow(QString(), levelTextRenderCheck_);

    // Long-text overflow.
    textOverflowCombo_ = new QComboBox(this);
    textOverflowCombo_->addItem(
        l10n(QStringLiteral("Shrink to fit"), QStringLiteral("缩小字体以放入全部")), QStringLiteral("shrink"));
    textOverflowCombo_->addItem(
        l10n(QStringLiteral("Keep size, ellipsis (…)"), QStringLiteral("保持字号，省略号(…)截断")),
        QStringLiteral("ellipsis"));
    cardForm->addRow(l10n(QStringLiteral("Long text"), QStringLiteral("文字超长")), textOverflowCombo_);

    controlsColumn->addWidget(cardGroup);

    // ---- Chart-frame section (an opt-in layer) ----
    auto* frameGroup = new QGroupBox(
        l10n(QStringLiteral("Chart frame"), QStringLiteral("谱面帧")), this);
    auto* frameForm = new QFormLayout(frameGroup);
    frameForm->setSpacing(10);
    frameForm->setLabelAlignment(Qt::AlignLeft);
    frameForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Chart frame (a square playfield grab at a picked time, added as a layer).
    chartFrameCheck_ = new QCheckBox(
        l10n(QStringLiteral("Add chart frame"), QStringLiteral("添加谱面帧")), this);
    chartFrameCheck_->setChecked(false);
    chartFrameCheck_->setEnabled(chartFrameAvailable_);
    if (!chartFrameAvailable_) {
        chartFrameCheck_->setToolTip(
            l10n(QStringLiteral("This difficulty has no chart notes to render."),
                 QStringLiteral("当前难度没有可渲染的谱面音符。")));
    }
    frameForm->addRow(QString(), chartFrameCheck_);

    // Frame-time picker: a play/pause button + slider over the chart content + a
    // mm:ss.cs readout. Scrubbing the slider or playing drives the live edit scene
    // directly (visual-only playback, no audio); the offscreen grab is reserved
    // for the export still.
    auto* frameRow = new QWidget(this);
    auto* frameLayout = new QHBoxLayout(frameRow);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    frameLayout->setSpacing(8);
    playIcon_ = makeTransportIcon(/*pause=*/false, UiTheme::colors().textPrimary);
    pauseIcon_ = makeTransportIcon(/*pause=*/true, UiTheme::colors().textPrimary);
    playButton_ = new QPushButton(frameRow);
    playButton_->setIcon(playIcon_);
    playButton_->setIconSize(QSize(16, 16));
    playButton_->setEnabled(false);
    playButton_->setToolTip(l10n(QStringLiteral("Play / pause (visual only)"),
                                 QStringLiteral("播放 / 暂停（仅画面）")));
    // Square transport button. Two QSS traps: the theme sheet's `min-width: 92px`
    // beats setFixedWidth, and its `min-height: 30px` is a CONTENT-box bound — with
    // the 1px borders the style wants 32px, so a 30px fixed height clipped the
    // bottom edge. Override BOTH bounds to a 26px content box (+2px borders = the
    // 28px fixed size) so nothing is clipped.
    playButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet()
        + QStringLiteral("QPushButton { min-width: 26px; max-width: 26px;"
                         " min-height: 26px; max-height: 26px; padding: 0; border-radius: 6px; }"));
    playButton_->setFixedSize(28, 28);
    frameSlider_ = new QSlider(Qt::Horizontal, frameRow);
    frameSlider_->setMinimum(0);
    frameSlider_->setMaximum(qMax(1, qRound(contentDurationSeconds_ * 1000.0)));
    frameSlider_->setValue(0);
    frameSlider_->setEnabled(false);
    frameTimeLabel_ = new QLabel(formatFrameTime(0.0), frameRow);
    frameTimeLabel_->setMinimumWidth(56);
    frameTimeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    frameLayout->addWidget(playButton_, 0);
    frameLayout->addWidget(frameSlider_, 1);
    frameLayout->addWidget(frameTimeLabel_, 0);
    frameForm->addRow(l10n(QStringLiteral("Frame time"), QStringLiteral("谱面时间")), frameRow);

    playClock_ = new QTimer(this);
    playClock_->setInterval(kPlayTickMs);
    connect(playClock_, &QTimer::timeout, this, &CoverStudioPanel::onPlayTick);

    // Held ←/→ seek: same tick cadence + acceleration/cap as the preview
    // transport (miacode::preview_interaction). The event filter intercepts the
    // slider's arrow keys (its default keyboard step is 1 ms — uselessly slow).
    frameSeekHoldTimer_ = new QTimer(this);
    frameSeekHoldTimer_->setTimerType(Qt::PreciseTimer);
    frameSeekHoldTimer_->setInterval(miacode::preview_interaction::kSeekHoldTickIntervalMs);
    connect(frameSeekHoldTimer_, &QTimer::timeout, this, &CoverStudioPanel::onFrameHeldSeekTick);
    frameSlider_->installEventFilter(this);

    // Chart-frame inner-ring background (B1): the playfield disk shows the cover
    // background (曲绘/custom) crisp + dimmed; the outer ring stays transparent.
    // Only meaningful when the chart frame is enabled and the background isn't
    // Transparent — gated in syncControlEnabled().
    chartFrameBgCheck_ = new QCheckBox(
        l10n(QStringLiteral("Chart-frame inner background"), QStringLiteral("谱面帧内圈背景")), this);
    chartFrameBgCheck_->setChecked(true);
    chartFrameBgCheck_->setEnabled(false);
    frameForm->addRow(QString(), chartFrameBgCheck_);

    chartFrameBgBrightnessSlider_ = new QSlider(Qt::Horizontal, this);
    chartFrameBgBrightnessSlider_->setMinimum(0);
    chartFrameBgBrightnessSlider_->setMaximum(100);
    // Seed from the user's preview "background inner brightness" so the same value
    // yields the same look as the realtime preview's 内圈亮度 (the QML dim is the
    // same black-overlay model the stage-background layer uses).
    chartFrameBgBrightnessSlider_->setValue(
        qBound(0, qRound(qBound(0.0, task.backgroundBrightnessInner, 1.0) * 100.0), 100));
    chartFrameBgBrightnessSlider_->setEnabled(false);
    frameForm->addRow(l10n(QStringLiteral("Background brightness"), QStringLiteral("背景亮度")),
                      chartFrameBgBrightnessSlider_);

    controlsColumn->addWidget(frameGroup);
    frameGroup->hide();
    controlsColumn->addStretch(1);

    // Height of the three stacked control groups + the two inter-group gaps. The
    // preview frame is sized to this (minus its 1px margins) so its BOTTOM lines up
    // with the bottom of the 谱面帧 group — both columns start at the same top.
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* exportButton = buttonBox->addButton(
        l10n(QStringLiteral("Export"), QStringLiteral("导出")), QDialogButtonBox::AcceptRole);
    exportButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    if (QPushButton* cancelButton = buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelButton->setText(l10n(QStringLiteral("Cancel"), QStringLiteral("取消")));
        cancelButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    }
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CoverStudioPanel::exportRequested);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &CoverStudioPanel::cancelRequested);
    controlsColumn->addWidget(buttonBox, 0);
    buttonBox->hide();

    // ---- Wiring ----
    connect(sizeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        resizePreviewToAspect();
        pushInputs();
        emit compositionChanged();
        // The live scene re-renders at the new preview resolution on its own; the
        // export re-grabs the still at the exact output resolution. No edit-time grab.
    });
    connect(backgroundCombo_, &QComboBox::currentIndexChanged, this, [this] {
        syncControlEnabled();
        pushInputs();
        emit compositionChanged();
    });
    connect(backgroundPathEdit_, &QLineEdit::textChanged, this, [this] {
        pushInputs();
        emit compositionChanged();
    });
    connect(backgroundBrowse_, &QPushButton::clicked, this, &CoverStudioPanel::browseBackground);
    connect(blurCheck_, &QCheckBox::toggled, this, [this] {
        pushInputs();
        emit compositionChanged();
    });
    // Card add-toggle drives the card layer's `visible` on the shared model — the
    // QML delegate and the export composite both follow it (NOTIFY binding).
    connect(cardCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (model_ != nullptr) {
            if (miacode::cover_export::CoverLayer* cardLayer =
                    model_->layer(miacode::cover_export::CoverLayoutModel::cardKey())) {
                cardLayer->setVisible(on);
            }
        }
        syncControlEnabled();
        pushInputs();
        emit compositionChanged();
    });
    connect(cardModeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        pushInputs();
        emit compositionChanged();
    });
    connect(cardShadowCheck_, &QCheckBox::toggled, this, [this] {
        pushInputs();
        emit compositionChanged();
    });
    connect(levelTextRenderCheck_, &QCheckBox::toggled, this, [this] {
        pushInputs();
        emit compositionChanged();
    });
    connect(textOverflowCombo_, &QComboBox::currentIndexChanged, this, [this] {
        pushInputs();
        emit compositionChanged();
    });
    if (resetLayoutButton_ != nullptr) {
        connect(resetLayoutButton_, &QPushButton::clicked, this, &CoverStudioPanel::resetLayout);
    }
    if (saveLayoutButton_ != nullptr) {
        connect(saveLayoutButton_, &QPushButton::clicked, this, &CoverStudioPanel::saveLayout);
    }
    if (importLayoutButton_ != nullptr) {
        connect(importLayoutButton_, &QPushButton::clicked, this, &CoverStudioPanel::importLayout);
    }

    connect(chartFrameCheck_, &QCheckBox::toggled, this, &CoverStudioPanel::onChartFrameToggled);
    connect(playButton_, &QPushButton::clicked, this, &CoverStudioPanel::togglePlayback);
    // Any genuine user scrub (handle drag, groove-click page-step, or keyboard
    // arrow/PageUp-Down — all emit valueChanged) moves the shared playhead and
    // repaints the live scene with no grab. If playback is running it must PAUSE
    // first, or the next tick would snap the value back to the play position. The
    // timer's own setValue is QSignalBlocker-guarded (see onPlayTick), so this
    // never fires from the clock and so never self-pauses.
    connect(frameSlider_, &QSlider::valueChanged, this, [this](int valueMs) {
        if (playing_) {
            stopPlayback();
        }
        applyFrameSeconds(valueMs / 1000.0);
    });
    // Grabbing the handle pauses immediately (before the first value change), so a
    // drag feels responsive. (Groove-click / keyboard are handled in valueChanged.)
    connect(frameSlider_, &QSlider::sliderPressed, this, [this] {
        if (playing_) {
            stopPlayback();
        }
    });
    // B1 inner-ring background controls.
    connect(chartFrameBgCheck_, &QCheckBox::toggled, this, [this] {
        if (miacode::cover_export::CoverLayer* layer = activeChartFrameLayer()) {
            layer->setFrameBgEnabled(chartFrameBgCheck_ != nullptr && chartFrameBgCheck_->isChecked());
        }
        syncControlEnabled();
        pushInputs();
        emit compositionChanged();
    });
    connect(chartFrameBgBrightnessSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (miacode::cover_export::CoverLayer* layer = activeChartFrameLayer()) {
            layer->setFrameBgBrightness(value / 100.0);
        }
        pushInputs();
        emit compositionChanged();
    });

    syncControlEnabled();
    resizePreviewToAspect();
    pushInputs();

    // App-level preference restore: reopen with the last exported composition
    // (saved in exportCover). Silent — fallback notices are suppressed; the
    // explicit 导入布局 path stays interactive.
    const QJsonObject savedPreferences = miacode::cover_export::CoverCompositionState::loadPreferences();
    if (!savedPreferences.isEmpty()) {
        applyCompositionJson(savedPreferences, /*interactive=*/false);
    }
}

CoverStudioPanel::~CoverStudioPanel()
{
    // Stop the play clock and sever the live scene's pointer to the shared frame
    // state HERE (destructor body), before the member SceneFrameRenderer that owns
    // that state is destroyed. Otherwise the live QQuickItem (torn down later, in
    // ~QObject) could read freed state.
    stopPlayback();
    if (composerView_ != nullptr) {
        composerView_->detachLiveChartScene();
    }
}

miacode::cover_export::CoverLayer* CoverStudioPanel::activeLayer() const
{
    return model_ != nullptr ? model_->layer(activeLayerKey_) : nullptr;
}

miacode::cover_export::CoverLayer* CoverStudioPanel::activeChartFrameLayer() const
{
    miacode::cover_export::CoverLayer* layer = activeLayer();
    return layer != nullptr && layer->kind() == QStringLiteral("chartFrame") ? layer : nullptr;
}

void CoverStudioPanel::setActiveLayerKey(const QString& key)
{
    if (model_ == nullptr || model_->layer(key) == nullptr || activeLayerKey_ == key) {
        return;
    }
    activeLayerKey_ = key;
    if (composerView_ != nullptr) {
        const miacode::cover_export::CoverLayer* layer = activeChartFrameLayer();
        composerView_->setActiveChartFrameKey(layer != nullptr ? layer->key() : QString());
    }
    syncActiveLayerControls();
    emit activeLayerChanged(activeLayerKey_);
}

void CoverStudioPanel::addChartFrameLayer()
{
    if (model_ == nullptr || !chartFrameAvailable_) {
        return;
    }
    miacode::cover_export::CoverLayer* layer =
        model_->addChartFrameLayer(sceneFrameRenderer_ != nullptr ? sceneFrameRenderer_->playheadSeconds() : 0.0);
    setActiveLayerKey(layer->key());
    if (chartFrameCheck_ != nullptr) {
        const QSignalBlocker block(chartFrameCheck_);
        chartFrameCheck_->setChecked(true);
    }
    syncActiveLayerControls();
    emit compositionChanged();
}

void CoverStudioPanel::duplicateActiveLayer()
{
    if (model_ == nullptr) return;
    if (miacode::cover_export::CoverLayer* layer = model_->duplicateLayer(activeLayerKey_)) {
        setActiveLayerKey(layer->key());
        emit compositionChanged();
    }
}

void CoverStudioPanel::removeActiveLayer()
{
    if (model_ == nullptr) return;
    if (!model_->removeLayer(activeLayerKey_)) return;
    setActiveLayerKey(miacode::cover_export::CoverLayoutModel::cardKey());
    emit compositionChanged();
}

void CoverStudioPanel::moveActiveLayerUp()
{
    if (model_ == nullptr) return;
    model_->normalizeZOrder();
    const int index = model_->indexOfKey(activeLayerKey_);
    const QList<miacode::cover_export::CoverLayer*> layers = model_->layers();
    if (index >= 0 && index + 1 < layers.size()) {
        model_->moveLayerAfter(activeLayerKey_, layers.at(index + 1)->key());
        emit compositionChanged();
    }
}

void CoverStudioPanel::moveActiveLayerDown()
{
    if (model_ == nullptr) return;
    model_->normalizeZOrder();
    const int index = model_->indexOfKey(activeLayerKey_);
    const QList<miacode::cover_export::CoverLayer*> layers = model_->layers();
    if (index > 0 && index < layers.size()) {
        model_->moveLayerBefore(activeLayerKey_, layers.at(index - 1)->key());
        emit compositionChanged();
    }
}

void CoverStudioPanel::moveActiveLayerToTop()
{
    if (model_ == nullptr) return;
    model_->normalizeZOrder();
    const int index = model_->indexOfKey(activeLayerKey_);
    if (index >= 0) {
        model_->bringToFront(index);
        emit compositionChanged();
    }
}

void CoverStudioPanel::moveActiveLayerToBottom()
{
    if (model_ == nullptr) return;
    model_->normalizeZOrder();
    const int index = model_->indexOfKey(activeLayerKey_);
    if (index >= 0) {
        model_->sendToBack(index);
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerVisible(bool visible)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setVisible(visible);
        if (!visible && layer == activeChartFrameLayer()) {
            stopPlayback();
        }
        syncActiveLayerControls();
        pushInputs();
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerLocked(bool locked)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setLocked(locked);
        syncActiveLayerControls();
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerOpacity(qreal opacity)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setOpacity(opacity);
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerSizeFraction(qreal sizeFraction)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setSizeFraction(sizeFraction);
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerCenter(qreal nx, qreal ny)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setNx(nx);
        layer->setNy(ny);
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerFrameSeconds(double seconds)
{
    if (activeChartFrameLayer() == nullptr) return;
    if (frameSlider_ != nullptr) {
        const QSignalBlocker block(frameSlider_);
        frameSlider_->setValue(qRound(qBound(0.0, seconds, contentDurationSeconds_) * 1000.0));
    }
    applyFrameSeconds(seconds);
    emit compositionChanged();
}

void CoverStudioPanel::setActiveLayerFrameBgEnabled(bool enabled)
{
    if (miacode::cover_export::CoverLayer* layer = activeChartFrameLayer()) {
        layer->setFrameBgEnabled(enabled);
        syncActiveLayerControls();
        pushInputs();
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerFrameBgBrightness(qreal brightness)
{
    if (miacode::cover_export::CoverLayer* layer = activeChartFrameLayer()) {
        layer->setFrameBgBrightness(brightness);
        syncActiveLayerControls();
        pushInputs();
        emit compositionChanged();
    }
}

void CoverStudioPanel::stepActiveFrameBySeconds(double deltaSeconds)
{
    if (activeChartFrameLayer() == nullptr) return;
    stepFrameBySeconds(deltaSeconds);
    emit compositionChanged();
}

QWidget* CoverStudioPanel::takeSettingsPanel(QWidget* parent)
{
    if (hiddenControlsHost_ == nullptr) {
        return nullptr;
    }
    hiddenControlsHost_->setParent(parent);
    hiddenControlsHost_->show();
    return hiddenControlsHost_;
}

void CoverStudioPanel::resetLayout()
{
    if (model_ != nullptr) model_->resetLayout();
    if (cardCheck_ != nullptr) cardCheck_->setChecked(true);
    if (chartFrameCheck_ != nullptr) chartFrameCheck_->setChecked(false);
    setActiveLayerKey(miacode::cover_export::CoverLayoutModel::cardKey());
    syncControlEnabled();
    pushInputs();
    emit compositionChanged();
}

void CoverStudioPanel::requestExport()
{
    emit exportRequested();
}

void CoverStudioPanel::requestCancel()
{
    emit cancelRequested();
}

void CoverStudioPanel::onChartFrameToggled(bool on)
{
    miacode::cover_export::CoverLayer* layer = activeChartFrameLayer();
    if (model_ != nullptr && layer == nullptr && on) {
        layer = model_->addChartFrameLayer(sceneFrameRenderer_ != nullptr ? sceneFrameRenderer_->playheadSeconds() : 0.0);
        setActiveLayerKey(layer->key());
    }
    if (layer != nullptr) {
        layer->setVisible(on);
    }
    if (frameSlider_ != nullptr) {
        frameSlider_->setEnabled(on && chartFrameAvailable_);
    }
    if (!on) {
        stopPlayback();
        if (playButton_ != nullptr) {
            playButton_->setEnabled(false);
        }
        syncControlEnabled();   // disable the inner-bg controls
        return;
    }
    // Enabling shows the LIVE edit scene (the QML Loader activates + binds), so no
    // grab is needed for display. We still do ONE offscreen grab here to (a) verify
    // the renderer actually works and (b) seed the static still the export path
    // uses. The cold settle (~0.3 s) flags the GUI busy. If it fails, don't leave
    // the layer enabled-but-blank (it would silently drop from the export) — revert
    // the toggle and tell the user.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool produced = renderChartFrameNow();
    QApplication::restoreOverrideCursor();
    if (!produced) {
        const QSignalBlocker block(chartFrameCheck_);   // avoid recursive toggled()
        chartFrameCheck_->setChecked(false);
        if (layer != nullptr) {
            layer->setVisible(false);
        }
        if (frameSlider_ != nullptr) {
            frameSlider_->setEnabled(false);
        }
        if (playButton_ != nullptr) {
            playButton_->setEnabled(false);
        }
        syncControlEnabled();   // re-disable the inner-bg controls after revert
        QMessageBox::warning(
            this,
            l10n(QStringLiteral("Chart frame"), QStringLiteral("谱面帧")),
            l10n(QStringLiteral("Could not render the chart frame."),
                 QStringLiteral("无法渲染谱面帧。")));
        return;
    }
    if (playButton_ != nullptr) {
        playButton_->setEnabled(true);
    }
    syncControlEnabled();   // enable the inner-bg controls (if bg isn't transparent)
}

void CoverStudioPanel::syncActiveLayerControls()
{
    miacode::cover_export::CoverLayer* layer = activeChartFrameLayer();
    const bool hasFrame = layer != nullptr;
    if (chartFrameCheck_ != nullptr) {
        const QSignalBlocker block(chartFrameCheck_);
        chartFrameCheck_->setChecked(hasFrame && layer->visible());
        chartFrameCheck_->setEnabled(chartFrameAvailable_);
    }
    if (frameSlider_ != nullptr) {
        const QSignalBlocker block(frameSlider_);
        frameSlider_->setEnabled(hasFrame && layer->visible() && chartFrameAvailable_);
        frameSlider_->setValue(qRound((hasFrame ? layer->frameSeconds() : 0.0) * 1000.0));
    }
    if (frameTimeLabel_ != nullptr) {
        frameTimeLabel_->setText(formatFrameTime(hasFrame ? layer->frameSeconds() : 0.0));
    }
    if (playButton_ != nullptr) {
        playButton_->setEnabled(hasFrame && layer->visible() && chartFrameAvailable_);
    }
    if (chartFrameBgCheck_ != nullptr && hasFrame) {
        const QSignalBlocker block(chartFrameBgCheck_);
        chartFrameBgCheck_->setChecked(layer->frameBgEnabled());
    }
    if (chartFrameBgBrightnessSlider_ != nullptr && hasFrame) {
        const QSignalBlocker block(chartFrameBgBrightnessSlider_);
        chartFrameBgBrightnessSlider_->setValue(qRound(layer->frameBgBrightness() * 100.0));
    }
    if (sceneFrameRenderer_ != nullptr && hasFrame) {
        sceneFrameRenderer_->setPlayheadSeconds(layer->frameSeconds());
    }
    if (composerView_ != nullptr) {
        composerView_->refreshLiveChartScene();
    }
    syncControlEnabled();
}

int CoverStudioPanel::chartFrameRenderPx(const miacode::cover_export::CoverLayer* layer) const
{
    qreal sizeFraction = 0.82;
    if (layer != nullptr) {
        sizeFraction = layer->sizeFraction();
    }
    const int outputHeight = qMax(1, currentSize().height());
    return qBound(kChartFrameMinPx, qRound(sizeFraction * outputHeight), kChartFrameMaxPx);
}

bool CoverStudioPanel::renderChartFrameNow()
{
    return renderChartFrameLayerNow(activeChartFrameLayer());
}

bool CoverStudioPanel::renderChartFrameLayerNow(miacode::cover_export::CoverLayer* layer)
{
    // renderAt() pumps the event loop (settle), so guard against re-entrancy. The
    // only callers (enable + export) are sequential single grabs, so a re-entry is
    // not expected — just drop it rather than re-arming a debounce.
    if (rendering_) {
        return false;
    }
    if (!chartFrameAvailable_ || sceneFrameRenderer_ == nullptr || model_ == nullptr
        || layer == nullptr || !layer->visible()) {
        return false;   // nothing to do (e.g. a stale call after a reset/disable)
    }
    rendering_ = true;
    // Grab at the CURRENT shared playhead — the exact time the live edit scene is
    // showing — so the exported still is WYSIWYG with the preview.
    const double seconds = layer->frameSeconds();
    sceneFrameRenderer_->setPlayheadSeconds(seconds);
    QString error;
    const QImage frame = sceneFrameRenderer_->renderAt(seconds, chartFrameRenderPx(layer), &error);
    bool produced = false;
    if (!frame.isNull()) {
        model_->setLayerImage(layer->key(), frame);
        produced = true;
    }
    // else: leave the previous still (if any) in place rather than blanking the layer.
    rendering_ = false;
    return produced;
}

void CoverStudioPanel::applyFrameSeconds(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    if (seconds > contentDurationSeconds_) {
        seconds = contentDurationSeconds_;
    }
    if (frameTimeLabel_ != nullptr) {
        frameTimeLabel_->setText(formatFrameTime(seconds));
    }
    if (sceneFrameRenderer_ != nullptr) {
        sceneFrameRenderer_->setPlayheadSeconds(seconds);
    }
    // Keep the persisted frame time current (toJson / future presets).
    if (miacode::cover_export::CoverLayer* layer = activeChartFrameLayer()) {
        layer->setFrameSeconds(seconds);
    }
    // Repaint the live edit scene at the new playhead — zero readback.
    if (composerView_ != nullptr) {
        composerView_->refreshLiveChartScene();
    }
}

void CoverStudioPanel::togglePlayback()
{
    if (playing_) {
        stopPlayback();
    } else {
        startPlayback();
    }
}

void CoverStudioPanel::startPlayback()
{
    if (playing_ || !chartFrameAvailable_ || model_ == nullptr || activeChartFrameLayer() == nullptr
        || !activeChartFrameLayer()->visible()
        || contentDurationSeconds_ <= 0.0) {
        return;
    }
    double cur = frameSlider_ != nullptr ? frameSlider_->value() / 1000.0 : 0.0;
    // Restart from the top if parked at (or past) the end.
    if (cur >= contentDurationSeconds_ - 1e-3) {
        cur = 0.0;
    }
    playStartSeconds_ = cur;
    playWall_.restart();
    playing_ = true;
    if (playClock_ != nullptr) {
        playClock_->start();
    }
    if (playButton_ != nullptr) {
        playButton_->setIcon(pauseIcon_);
    }
}

void CoverStudioPanel::stopPlayback()
{
    if (playClock_ != nullptr) {
        playClock_->stop();
    }
    playing_ = false;
    if (playButton_ != nullptr) {
        playButton_->setIcon(playIcon_);
    }
}

void CoverStudioPanel::onPlayTick()
{
    if (!playing_) {
        return;
    }
    double t = playStartSeconds_ + playWall_.elapsed() / 1000.0;
    bool ended = false;
    if (t >= contentDurationSeconds_) {
        t = contentDurationSeconds_;
        ended = true;
    }
    // Move the slider thumb WITHOUT re-triggering its user-drag handler.
    if (frameSlider_ != nullptr) {
        const QSignalBlocker block(frameSlider_);
        frameSlider_->setValue(qRound(t * 1000.0));
    }
    applyFrameSeconds(t);
    if (ended) {
        stopPlayback();
    }
}

bool CoverStudioPanel::eventFilter(QObject* watched, QEvent* event)
{
    // Esc from inside the embedded Quick window → close, mirroring QDialog's
    // default Esc-reject that the native child window otherwise swallows.
    if (event->type() == QEvent::KeyPress
        && composerView_ != nullptr && watched == composerView_->previewWindowObject()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && keyEvent->modifiers() == Qt::NoModifier
            && !keyEvent->isAutoRepeat()) {
            emit cancelRequested();
            return true;
        }
    }
    if (watched == frameSlider_) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            int direction = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                direction = -1;
            } else if (keyEvent->key() == Qt::Key_Right) {
                direction = 1;
            }
            if (direction != 0) {
                if (event->type() == QEvent::KeyPress) {
                    if (keyEvent->modifiers() != Qt::NoModifier) {
                        return QWidget::eventFilter(watched, event);
                    }
                    // OS auto-repeat presses are swallowed — the precise hold
                    // timer drives the motion (same as the preview transport).
                    if (keyEvent->isAutoRepeat()) {
                        return true;
                    }
                    if (playing_) {
                        stopPlayback();   // arrow scrub pauses, like any other scrub
                    }
                    beginFrameHeldSeek(direction, keyEvent->key());
                    // A quick tap still moves one fine step immediately.
                    stepFrameBySeconds(
                        static_cast<double>(direction)
                        * miacode::preview_interaction::kSeekSingleStepSeconds);
                    return true;
                }
                if (!keyEvent->isAutoRepeat() && frameSeekHoldKey_ == keyEvent->key()) {
                    stopFrameHeldSeek(keyEvent->key());
                    return true;
                }
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Defensive: never leave the hold timer running once the slider
            // can no longer see the key release.
            stopFrameHeldSeek();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CoverStudioPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    resizePreviewToAspect();
}

void CoverStudioPanel::beginFrameHeldSeek(int direction, int key)
{
    if (direction == 0) {
        return;
    }
    frameSeekHoldDirection_ = direction > 0 ? 1 : -1;
    frameSeekHoldKey_ = key;
    frameSeekHoldLastElapsedMs_ = 0;
    frameSeekHoldElapsed_.restart();
    if (frameSeekHoldTimer_ != nullptr && !frameSeekHoldTimer_->isActive()) {
        frameSeekHoldTimer_->start();
    }
}

void CoverStudioPanel::stopFrameHeldSeek(int key)
{
    if (key != 0 && frameSeekHoldKey_ != key) {
        return;
    }
    frameSeekHoldDirection_ = 0;
    frameSeekHoldKey_ = 0;
    frameSeekHoldLastElapsedMs_ = 0;
    frameSeekHoldElapsed_.invalidate();
    if (frameSeekHoldTimer_ != nullptr) {
        frameSeekHoldTimer_->stop();
    }
}

void CoverStudioPanel::onFrameHeldSeekTick()
{
    if (frameSeekHoldDirection_ == 0 || frameSeekHoldKey_ == 0
        || !frameSeekHoldElapsed_.isValid()) {
        return;
    }
    // Step by REAL elapsed wall time at the accelerated rate, exactly like
    // MainWindow::TimelineSection::applyPreviewHeldSeekTick — late ticks don't
    // slow the seek down, and the rate ramps 1× → 3× over the first 2 s.
    const int elapsedMs = static_cast<int>(frameSeekHoldElapsed_.elapsed());
    const int deltaMs = frameSeekHoldLastElapsedMs_ > 0
        ? (elapsedMs - frameSeekHoldLastElapsedMs_)
        : miacode::preview_interaction::kSeekHoldTickIntervalMs;
    frameSeekHoldLastElapsedMs_ = elapsedMs;
    const double heldSeconds = static_cast<double>(elapsedMs) / 1000.0;
    stepFrameBySeconds(
        static_cast<double>(frameSeekHoldDirection_)
        * miacode::preview_interaction::heldSeekStepSecondsForDeltaMs(deltaMs, heldSeconds));
}

void CoverStudioPanel::stepFrameBySeconds(double deltaSeconds)
{
    const double current = sceneFrameRenderer_ != nullptr
        ? sceneFrameRenderer_->playheadSeconds()
        : (frameSlider_ != nullptr ? frameSlider_->value() / 1000.0 : 0.0);
    const double next = qBound(0.0, current + deltaSeconds, contentDurationSeconds_);
    if (frameSlider_ != nullptr) {
        const QSignalBlocker block(frameSlider_);   // avoid the valueChanged scrub path
        frameSlider_->setValue(qRound(next * 1000.0));
    }
    applyFrameSeconds(next);
}

QSize CoverStudioPanel::currentSize() const
{
    const QVariant data = sizeCombo_ != nullptr ? sizeCombo_->currentData() : QVariant();
    return data.canConvert<QSize>() ? data.toSize() : QSize(1080, 1080);
}

miacode::cover_export::CoverComposerInputs CoverStudioPanel::buildInputs() const
{
    using miacode::cover_export::CoverBackgroundMode;
    miacode::cover_export::CoverComposerInputs in;
    in.templateMap = cachedTemplate_;

    QVariantMap track;
    track.insert(QStringLiteral("title"), banner_.title);
    track.insert(QStringLiteral("artist"), banner_.artist);
    track.insert(QStringLiteral("designer"), banner_.designer);
    track.insert(QStringLiteral("level"), banner_.level);
    track.insert(QStringLiteral("difficulty"), banner_.difficulty);
    track.insert(QStringLiteral("bpm"), banner_.bpm);
    track.insert(QStringLiteral("mode"),
                 cardModeCombo_ != nullptr ? cardModeCombo_->currentData().toString()
                                           : banner_.mode);
    track.insert(QStringLiteral("lvRenderMode"),
                 (levelTextRenderCheck_ != nullptr && levelTextRenderCheck_->isChecked())
                     ? QStringLiteral("text")
                     : QStringLiteral("atlas"));
    track.insert(QStringLiteral("stillTextMode"),
                 textOverflowCombo_ != nullptr ? textOverflowCombo_->currentData().toString()
                                               : QStringLiteral("shrink"));
    in.trackOverrides = track;

    in.jacketPath = banner_.jacketPath;

    const QString bg = backgroundCombo_ != nullptr ? backgroundCombo_->currentData().toString()
                                                   : QStringLiteral("jacket");
    if (bg == QStringLiteral("custom")) {
        in.backgroundMode = CoverBackgroundMode::Custom;
        in.backgroundPath = backgroundPathEdit_ != nullptr ? backgroundPathEdit_->text().trimmed() : QString();
    } else if (bg == QStringLiteral("transparent")) {
        in.backgroundMode = CoverBackgroundMode::Transparent;
    } else {
        in.backgroundMode = CoverBackgroundMode::Jacket;
    }

    in.blurBackground = blurCheck_ != nullptr && blurCheck_->isChecked();
    in.cardShadow = cardShadowCheck_ != nullptr && cardShadowCheck_->isChecked();

    // B1 — chart-frame inner-ring background (reuses the cover background image via
    // the QML's backdropSourceUrl). The disk diameter is the playfield ring as a
    // fraction of the square frame; 0 when the renderer isn't bootstrapped.
    const miacode::cover_export::CoverLayer* frameLayer = activeChartFrameLayer();
    in.chartFrameBackground = frameLayer != nullptr
        ? frameLayer->frameBgEnabled()
        : (chartFrameBgCheck_ != nullptr && chartFrameBgCheck_->isChecked());
    in.chartFrameBgBrightness = frameLayer != nullptr
        ? frameLayer->frameBgBrightness()
        : (chartFrameBgBrightnessSlider_ != nullptr ? chartFrameBgBrightnessSlider_->value() / 100.0 : 0.8);
    in.chartFrameDiskDiameter = (chartFrameAvailable_ && sceneFrameRenderer_ != nullptr)
        ? sceneFrameRenderer_->playfieldDiskDiameterFraction()
        : 0.0;
    return in;
}

void CoverStudioPanel::pushInputs()
{
    if (composerView_ != nullptr) {
        composerView_->setInputs(buildInputs());
    }
}

void CoverStudioPanel::resizePreviewToAspect()
{
    if (previewContainer_ == nullptr) {
        return;
    }
    const QSize sz = currentSize();
    // Anchor the preview HEIGHT so the preview-frame bottom lines up with the bottom
    // of the 谱面帧 group on the right (controlsStackHeight_ minus the frame's 1px
    // top+bottom margins), then let the WIDTH follow the chosen aspect ratio. Picking
    // a wider ratio (4:3 / 16:9) thus WIDENS the window rather than changing the
    // editing-area height. kPreviewBox is the fallback if the stack isn't measured.
    QSize available = previewFrame_ != nullptr ? previewFrame_->contentsRect().size() : size();
    available -= QSize(2, 2);
    if (available.width() <= 0 || available.height() <= 0) {
        available = QSize(kPreviewBox, kPreviewBox);
    }
    const double aspect = sz.height() > 0 ? static_cast<double>(sz.width()) / sz.height() : 1.0;
    int pw = available.width();
    int ph = qRound(pw / aspect);
    if (ph > available.height()) {
        ph = available.height();
        pw = qRound(ph * aspect);
    }
    previewContainer_->setFixedSize(qMax(1, pw), qMax(1, ph));

    // Once shown, a wider/narrower aspect ratio changes the dialog width; keep the
    // window on its previous CENTER so widening to 4:3 / 16:9 stays centered instead
    // of expanding to the right. The embedded native preview window relayouts the
    // dialog ASYNCHRONOUSLY, so measuring/moving now would use the stale (pre-grow)
    // size and leave it expanded rightward. Capture the current center and re-apply
    // it on the next event-loop turn, after the new size has settled. (Skipped
    // before first show — geometry isn't meaningful yet; the platform centres it.)
}

miacode::cover_export::CoverExportResult CoverStudioPanel::exportCover(const QString& outputDirectory)
{
    // Freeze playback first: the export grab pumps the event loop, and a live play
    // tick mid-grab would move the shared playhead out from under it. Stopping also
    // pins the exported still to exactly the frame the user is looking at.
    stopPlayback();
    // The user committed this composition — remember ALL its settings app-wide
    // (restored on the next dialog open).
    miacode::cover_export::CoverCompositionState::savePreferences(exportCompositionJson());
    // Re-grab the chart frame at the exact export resolution (chartFrameRenderPx
    // tracks currentSize().height(), which the composite scales the layer to), so
    // the still is crisp in the export.
    if (model_ != nullptr && chartFrameAvailable_) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        for (miacode::cover_export::CoverLayer* layer : model_->visibleChartFrameLayers()) {
            renderChartFrameLayerNow(layer);
        }
        QApplication::restoreOverrideCursor();
        // Defensive: the toggle-revert keeps an enabled frame from ever sitting
        // image-less, but the export is the irreversible output — if somehow it
        // still has no still, tell the user rather than silently ship a cover
        // missing the frame they enabled.
        bool missingFrame = false;
        for (const miacode::cover_export::CoverLayer* layer : model_->visibleChartFrameLayers()) {
            if (layer != nullptr && layer->imageRevision() < 0) {
                missingFrame = true;
                break;
            }
        }
        if (missingFrame) {
            QMessageBox::warning(
                this,
                l10n(QStringLiteral("Chart frame"), QStringLiteral("谱面帧")),
                l10n(QStringLiteral("The chart frame could not be rendered; the cover will not include it."),
                     QStringLiteral("谱面帧无法渲染，封面将不包含它。")));
        }
    }
    return miacode::cover_export::exportCoverComposite(
        model_, buildInputs(), currentSize(), outputDirectory);
}

void CoverStudioPanel::syncControlEnabled()
{
    const QString bg = backgroundCombo_ != nullptr ? backgroundCombo_->currentData().toString()
                                                   : QStringLiteral("jacket");
    const bool isCustom = (bg == QStringLiteral("custom"));
    const bool isTransparent = (bg == QStringLiteral("transparent"));
    if (backgroundPathEdit_ != nullptr) backgroundPathEdit_->setEnabled(isCustom);
    if (backgroundBrowse_ != nullptr) backgroundBrowse_->setEnabled(isCustom);
    // Blur only applies when there is a backdrop behind the card.
    if (blurCheck_ != nullptr) blurCheck_->setEnabled(!isTransparent);

    // Card sub-options only matter while the card layer is added. (The drop
    // shadow itself works in EVERY background mode, incl. Transparent.)
    const bool cardOn = cardCheck_ == nullptr || cardCheck_->isChecked();
    if (cardModeCombo_ != nullptr) cardModeCombo_->setEnabled(cardOn);
    if (cardShadowCheck_ != nullptr) cardShadowCheck_->setEnabled(cardOn);
    if (levelTextRenderCheck_ != nullptr) levelTextRenderCheck_->setEnabled(cardOn);
    if (textOverflowCombo_ != nullptr) textOverflowCombo_->setEnabled(cardOn);

    // B1 inner-ring background: only when the chart frame is enabled AND the cover
    // background isn't Transparent (no image to show in the disk). The brightness
    // slider additionally needs the inner-bg checkbox on.
    const miacode::cover_export::CoverLayer* frameLayer = activeChartFrameLayer();
    const bool chartFrameOn =
        frameLayer != nullptr && frameLayer->visible() && chartFrameAvailable_;
    const bool innerBgEnable = chartFrameOn && !isTransparent;
    if (chartFrameBgCheck_ != nullptr) chartFrameBgCheck_->setEnabled(innerBgEnable);
    if (chartFrameBgBrightnessSlider_ != nullptr) {
        chartFrameBgBrightnessSlider_->setEnabled(
            innerBgEnable && chartFrameBgCheck_ != nullptr && chartFrameBgCheck_->isChecked());
    }
}

void CoverStudioPanel::browseBackground()
{
    const QString start = backgroundPathEdit_ != nullptr && !backgroundPathEdit_->text().trimmed().isEmpty()
        ? QFileInfo(backgroundPathEdit_->text().trimmed()).absolutePath()
        : QString();
    const QString file = QFileDialog::getOpenFileName(
        this,
        l10n(QStringLiteral("Choose background image"), QStringLiteral("选择背景图片")),
        start,
        l10n(QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"),
             QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp)")));
    if (file.isEmpty()) {
        return;
    }
    if (backgroundCombo_ != nullptr) {
        const int customIndex = backgroundCombo_->findData(QStringLiteral("custom"));
        if (customIndex >= 0) backgroundCombo_->setCurrentIndex(customIndex);
    }
    if (backgroundPathEdit_ != nullptr) {
        backgroundPathEdit_->setText(file);
    }
}

// ===================== B2 — layout save / import =====================

QJsonObject CoverStudioPanel::exportCompositionJson() const
{
    miacode::cover_export::CoverCompositionState state;
    state.size = currentSize();

    QJsonObject bg;
    bg.insert(QStringLiteral("mode"),
              backgroundCombo_ != nullptr ? backgroundCombo_->currentData().toString()
                                          : QStringLiteral("jacket"));
    // Absolute path (chosen strategy): on a different machine a missing file
    // triggers the jacket fallback in applyCompositionJson().
    bg.insert(QStringLiteral("customPath"),
              backgroundPathEdit_ != nullptr ? backgroundPathEdit_->text().trimmed() : QString());
    bg.insert(QStringLiteral("blur"), blurCheck_ != nullptr && blurCheck_->isChecked());
    state.background = bg;

    QJsonObject card;
    card.insert(QStringLiteral("mode"),
                cardModeCombo_ != nullptr ? cardModeCombo_->currentData().toString()
                                          : QStringLiteral("DX"));
    card.insert(QStringLiteral("shadow"), cardShadowCheck_ != nullptr && cardShadowCheck_->isChecked());
    card.insert(QStringLiteral("levelTextRender"),
                levelTextRenderCheck_ != nullptr && levelTextRenderCheck_->isChecked());
    card.insert(QStringLiteral("longText"),
                textOverflowCombo_ != nullptr ? textOverflowCombo_->currentData().toString()
                                              : QStringLiteral("shrink"));
    state.card = card;

    // Layer geometry + visibility (chart-frame enabled == its layer visible) +
    // frameSeconds, via CoverLayoutModel (the layers are its responsibility).
    if (model_ != nullptr) {
        state.layout = model_->toJson();
    }
    return state.toJson();
}

void CoverStudioPanel::saveLayout()
{
    stopPlayback();
    QString path = QFileDialog::getSaveFileName(
        this,
        l10n(QStringLiteral("Save cover layout"), QStringLiteral("保存封面布局")),
        QStringLiteral("cover-layout.miacover"),
        l10n(QStringLiteral("Cover layout (*.miacover)"), QStringLiteral("封面布局 (*.miacover)")));
    if (path.isEmpty()) {
        return;
    }
    // Dedicated extension; the payload stays the same composition JSON.
    if (!path.endsWith(QStringLiteral(".miacover"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".miacover");
    }
    const QJsonDocument doc(exportCompositionJson());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(doc.toJson(QJsonDocument::Indented)) < 0) {
        QMessageBox::warning(
            this,
            l10n(QStringLiteral("Save layout"), QStringLiteral("保存布局")),
            l10n(QStringLiteral("Could not write the layout file."),
                 QStringLiteral("无法写入布局文件。")));
    }
}

void CoverStudioPanel::importLayout()
{
    // Legacy .json stays importable (early layouts saved with that suffix);
    // the identity check inside is what actually validates the file.
    const QString path = QFileDialog::getOpenFileName(
        this,
        l10n(QStringLiteral("Import cover layout"), QStringLiteral("导入封面布局")),
        QString(),
        l10n(QStringLiteral("Cover layout (*.miacover);;Legacy JSON (*.json)"),
             QStringLiteral("封面布局 (*.miacover);;旧版 JSON (*.json)")));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            this,
            l10n(QStringLiteral("Import layout"), QStringLiteral("导入布局")),
            l10n(QStringLiteral("Could not read the layout file."),
                 QStringLiteral("无法读取布局文件。")));
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        QMessageBox::warning(
            this,
            l10n(QStringLiteral("Import layout"), QStringLiteral("导入布局")),
            l10n(QStringLiteral("The layout file is not valid JSON."),
                 QStringLiteral("布局文件不是有效的 JSON。")));
        return;
    }
    const QJsonObject root = doc.object();
    // Identity guard: an unrelated but structurally-valid JSON would otherwise be
    // accepted and silently reset the composition toward defaults. Require our tag.
    if (root.value(QStringLiteral("kind")).toString() != QStringLiteral("miacode-cover-composition")) {
        QMessageBox::warning(
            this,
            l10n(QStringLiteral("Import layout"), QStringLiteral("导入布局")),
            l10n(QStringLiteral("This file is not a MiaCode cover layout."),
                 QStringLiteral("该文件不是 MiaCode 封面布局文件。")));
        return;
    }
    applyCompositionJson(root);
}

void CoverStudioPanel::applyCompositionJson(const QJsonObject& root, bool interactive)
{
    stopPlayback();
    miacode::cover_export::CoverCompositionState state;
    QString stateError;
    if (!miacode::cover_export::CoverCompositionState::fromJson(root, &state, &stateError)) {
        return;
    }

    // --- Size: match a preset or add a custom entry. ---
    const int w = state.size.width() > 0 ? state.size.width() : currentSize().width();
    const int h = state.size.height() > 0 ? state.size.height() : currentSize().height();
    if (sizeCombo_ != nullptr && w > 0 && h > 0) {
        const QSize wanted(w, h);
        int idx = -1;
        for (int i = 0; i < sizeCombo_->count(); ++i) {
            if (sizeCombo_->itemData(i).toSize() == wanted) { idx = i; break; }
        }
        if (idx < 0) {
            sizeCombo_->addItem(QStringLiteral("%1x%2").arg(w).arg(h), wanted);
            idx = sizeCombo_->count() - 1;
        }
        const QSignalBlocker block(sizeCombo_);
        sizeCombo_->setCurrentIndex(idx);
    }

    // --- Background, with the absolute-path → jacket fallback. ---
    const QJsonObject bg = state.background;
    QString bgMode = bg.value(QStringLiteral("mode")).toString(QStringLiteral("jacket"));
    const QString customPath = bg.value(QStringLiteral("customPath")).toString();
    bool fellBack = false;
    if (bgMode == QStringLiteral("custom")
        && (customPath.trimmed().isEmpty() || !QFileInfo::exists(customPath))) {
        bgMode = QStringLiteral("jacket");
        fellBack = true;
    }
    if (backgroundPathEdit_ != nullptr) {
        const QSignalBlocker block(backgroundPathEdit_);
        backgroundPathEdit_->setText(customPath);
    }
    if (backgroundCombo_ != nullptr) {
        const int modeIdx = backgroundCombo_->findData(bgMode);
        if (modeIdx >= 0) {
            const QSignalBlocker block(backgroundCombo_);
            backgroundCombo_->setCurrentIndex(modeIdx);
        }
    }
    if (blurCheck_ != nullptr) {
        const QSignalBlocker block(blurCheck_);
        blurCheck_->setChecked(bg.value(QStringLiteral("blur")).toBool(blurCheck_->isChecked()));
    }

    // --- Card ---
    const QJsonObject card = state.card;
    if (cardModeCombo_ != nullptr) {
        const int modeIdx = cardModeCombo_->findData(card.value(QStringLiteral("mode")).toString());
        if (modeIdx >= 0) {
            const QSignalBlocker block(cardModeCombo_);
            cardModeCombo_->setCurrentIndex(modeIdx);
        }
    }
    if (cardShadowCheck_ != nullptr) {
        const QSignalBlocker block(cardShadowCheck_);
        cardShadowCheck_->setChecked(card.value(QStringLiteral("shadow")).toBool(cardShadowCheck_->isChecked()));
    }
    if (levelTextRenderCheck_ != nullptr) {
        const QSignalBlocker block(levelTextRenderCheck_);
        levelTextRenderCheck_->setChecked(
            card.value(QStringLiteral("levelTextRender")).toBool(levelTextRenderCheck_->isChecked()));
    }
    if (textOverflowCombo_ != nullptr) {
        const int ltIdx = textOverflowCombo_->findData(card.value(QStringLiteral("longText")).toString());
        if (ltIdx >= 0) {
            const QSignalBlocker block(textOverflowCombo_);
            textOverflowCombo_->setCurrentIndex(ltIdx);
        }
    }

    // --- Layer geometry (restores positions/scale + per-layer visible + frameSeconds). ---
    if (model_ != nullptr) {
        model_->fromJson(state.layout);
    }

    // The card add-toggle mirrors the restored card layer's visibility (the model
    // already holds the value — just sync the checkbox without re-firing it).
    if (cardCheck_ != nullptr && model_ != nullptr) {
        if (const miacode::cover_export::CoverLayer* cardLayer =
                model_->layer(miacode::cover_export::CoverLayoutModel::cardKey())) {
            const QSignalBlocker block(cardCheck_);
            cardCheck_->setChecked(cardLayer->visible());
        }
    }

    // --- Reconcile the chart frame. The still image isn't persisted (only the
    // frame time), so re-grab at the restored frameSeconds. Set the playhead first,
    // then drive enabling through onChartFrameToggled (one path for model + slider +
    // grab/revert), forcing the checkbox to the wanted state without a spurious
    // signal. ---
    double frameSec = 0.0;
    QString firstVisibleFrameKey;
    bool chartFrameDropped = false;   // saved layout wanted it, but it can't render here
    if (model_ != nullptr) {
        const QList<miacode::cover_export::CoverLayer*> frames = model_->visibleChartFrameLayers();
        if (!frames.isEmpty()) {
            const miacode::cover_export::CoverLayer* cfl = frames.constFirst();
            frameSec = qBound(0.0, cfl->frameSeconds(), contentDurationSeconds_);
            firstVisibleFrameKey = cfl->key();
            chartFrameDropped = !chartFrameAvailable_;
            if (!chartFrameAvailable_) {
                for (miacode::cover_export::CoverLayer* layer : frames) {
                    layer->setVisible(false);
                }
                firstVisibleFrameKey.clear();
            }
        }
    }
    if (frameSlider_ != nullptr) {
        const QSignalBlocker block(frameSlider_);
        frameSlider_->setValue(qRound(frameSec * 1000.0));
    }
    if (!firstVisibleFrameKey.isEmpty()) {
        setActiveLayerKey(firstVisibleFrameKey);
        applyFrameSeconds(frameSec);
    } else {
        setActiveLayerKey(miacode::cover_export::CoverLayoutModel::cardKey());
    }
    syncActiveLayerControls();
    if (chartFrameAvailable_ && model_ != nullptr) {
        for (miacode::cover_export::CoverLayer* layer : model_->visibleChartFrameLayers()) {
            renderChartFrameLayerNow(layer);
        }
    }

    syncControlEnabled();
    resizePreviewToAspect();
    pushInputs();

    // Surface BOTH import fallbacks in a single notice (so they don't stack
    // dialogs). Suppressed for the silent app-preference restore on dialog open —
    // a stale custom-background path there should not greet the user with a popup.
    if (!interactive) {
        return;
    }
    QString notice;
    if (fellBack) {
        notice += l10n(
            QStringLiteral("The custom background image was not found; using the chart jacket instead."),
            QStringLiteral("自定义背景图片未找到，已回退为曲绘背景。"));
    }
    if (chartFrameDropped) {
        if (!notice.isEmpty()) {
            notice += QLatin1Char('\n');
        }
        notice += l10n(
            QStringLiteral("The imported layout included a chart frame, but this difficulty has no "
                           "renderable notes; the chart frame was skipped."),
            QStringLiteral("导入的布局包含谱面帧，但当前难度无可渲染音符，已跳过谱面帧。"));
    }
    if (!notice.isEmpty()) {
        QMessageBox::information(
            this, l10n(QStringLiteral("Import layout"), QStringLiteral("导入布局")), notice);
    }
}

}  // namespace miacode::cover_export
