#include "tools/cover_export/CoverStudioPanel.h"

#include "tools/cover_export/CoverCompositionState.h"
#include "tools/cover_export/CoverCompositionPersistenceGuard.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/SceneFrameRenderer.h"
#include "tools/video_export/FontLibrary.h"
#include "common/DebugLog.h"
#include "common/PreviewInteractionConfig.h"
#include "UiNativeWindowTheme.h"
#include "UiText.h"
#include "UiComponents.h"
#include "UiTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
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
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantMap>

#include <algorithm>

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
constexpr qreal kPreviewZoomMin = 0.5;
constexpr qreal kPreviewZoomMax = 2.0;
constexpr qreal kPreviewZoomStep = 0.1;

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

QString inspectorSectionTitleStyle()
{
    const UiTheme::Colors& c = UiTheme::colors();
    return QStringLiteral(
        "QGroupBox { border: 1px solid %2; border-radius: 8px; margin-top: 13px;"
        " padding-top: 8px; color: %1; font-weight: 700; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px;"
        " color: %1; font-size: 13px; font-weight: 700; padding: 0 6px; }"
        "QGroupBox QLabel, QGroupBox QCheckBox, QGroupBox QComboBox,"
        " QGroupBox QLineEdit, QGroupBox QPushButton { font-weight: 400; }")
        .arg(c.textPrimary.name(QColor::HexRgb),
             c.borderSoft.name(QColor::HexRgb));
}

void emphasizeGroupTitle(QGroupBox* group)
{
    if (group == nullptr) {
        return;
    }
    group->setStyleSheet(inspectorSectionTitleStyle());
    QFont titleFont = group->font();
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setPointSizeF(qMax<qreal>(titleFont.pointSizeF(), 10.5));
    group->setFont(titleFont);
}

// Chart-frame square render-size clamp (px); the natural size is the layer's
// export pixel height, but keep it sane for tiny / huge canvases.
constexpr int kChartFrameMinPx = 384;
constexpr int kChartFrameMaxPx = 2048;

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
    setWindowTitle(UiText::text(QStringLiteral("cover.export_cover")));
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
    // §4 — a canvas tap/drag selects a layer; route it to the single source of truth.
    connect(composerView_, &miacode::cover_export::CoverComposerView::layerSelectionRequested,
            this, &CoverStudioPanel::setActiveLayerKey);
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
    previewFrame->setObjectName(QStringLiteral("CoverPreviewFrame"));
    previewFrame->setFrameShape(QFrame::StyledPanel);
    previewFrame->setStyleSheet(QStringLiteral(
        "QFrame#CoverPreviewFrame { background: #14141C; border: 1px solid rgba(255,255,255,40); border-radius: 6px; }"));
    auto* previewFrameLayout = new QVBoxLayout(previewFrame);
    previewFrameLayout->setContentsMargins(1, 1, 1, 1);

    auto* previewScroll = new QScrollArea(previewFrame);
    previewScrollArea_ = previewScroll;
    previewScroll->setWidgetResizable(false);
    previewScroll->setAlignment(Qt::AlignCenter);
    previewScroll->setFrameShape(QFrame::NoFrame);
    previewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    previewScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    previewScroll->viewport()->setAutoFillBackground(false);
    previewScroll->viewport()->installEventFilter(this);
    previewScroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"));
    miacode::ui::applyScrollBarStyle(previewScroll);
    previewFrameLayout->addWidget(previewScrollArea_, 1);

    QWidget* container = composerView_->createContainer(previewScroll);
    if (container != nullptr) {
        previewContainer_ = container;
        previewScroll->setWidget(previewContainer_);
        // Esc must close the dialog even while focus sits inside the embedded
        // NATIVE Quick window (after clicking the preview, keys go there and
        // never reach QDialog's default Esc-reject) — filter it ourselves.
        if (QObject* quickWindow = composerView_->previewWindowObject()) {
            quickWindow->installEventFilter(this);
        }
    } else {
        auto* failed = new QLabel(
            UiText::text(QStringLiteral("cover.failed_to_start_the_composer")).arg(composerView_->lastError()),
            previewScroll);
        failed->setWordWrap(true);
        previewScroll->setWidget(failed);
    }
    previewColumn->addWidget(previewFrame, 1);

    resetLayoutButton_ = new QPushButton(
        UiText::text(QStringLiteral("cover.reset_layout")), this);
    resetLayoutButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    saveLayoutButton_ = new QPushButton(
        UiText::text(QStringLiteral("cover.save_layout")), this);
    saveLayoutButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    importLayoutButton_ = new QPushButton(
        UiText::text(QStringLiteral("cover.import_layout")), this);
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
        UiText::text(QStringLiteral("cover.canvas")), this);
    canvasGroup_ = canvasGroup;
    auto* form = new QFormLayout(canvasGroup);
    form->setSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Size.
    sizeCombo_ = new QComboBox(this);
    sizeCombo_->setProperty("miacode.combo_text_alignment",
                            static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
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
    UiTheme::styleDialogComboBox(sizeCombo_, 12);
    form->addRow(UiText::text(QStringLiteral("cover.size_2")), sizeCombo_);

    // Background source.
    backgroundCombo_ = new QComboBox(this);
    backgroundCombo_->setProperty("miacode.combo_text_alignment",
                                  static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
    backgroundCombo_->addItem(UiText::text(QStringLiteral("cover.chart_jacket")),
                              QStringLiteral("jacket"));
    backgroundCombo_->addItem(UiText::text(QStringLiteral("cover.custom_image")),
                              QStringLiteral("custom"));
    backgroundCombo_->addItem(UiText::text(QStringLiteral("cover.transparent")),
                              QStringLiteral("transparent"));
    UiTheme::styleDialogComboBox(backgroundCombo_, 12);
    form->addRow(UiText::text(QStringLiteral("cover.background")), backgroundCombo_);

    // Custom background path + browse.
    auto* pathRow = new QWidget(this);
    auto* pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(8);
    backgroundPathEdit_ = new QLineEdit(pathRow);
    backgroundPathEdit_->setPlaceholderText(
        UiText::text(QStringLiteral("cover.custom_background_image_path")));
    backgroundPathEdit_->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet());
    backgroundBrowse_ = miacode::ui::createDialogAuxiliaryButton(
        pathRow, UiText::text(QStringLiteral("cover.browse")));
    pathLayout->addWidget(backgroundPathEdit_, 1);
    pathLayout->addWidget(backgroundBrowse_, 0);
    form->addRow(QString(), pathRow);

    // Backdrop blur.
    blurCheck_ = new QCheckBox(UiText::text(QStringLiteral("cover.blur_background")), this);
    blurCheck_->setChecked(true);
    form->addRow(QString(), blurCheck_);

    // Backdrop brightness (§3.4.4): adjustable dim. 0..100 → coverBgBrightness 0..1;
    // default 45 reproduces the old fixed dim pixel-for-pixel.
    bgBrightnessSlider_ = new QSlider(Qt::Horizontal, this);
    bgBrightnessSlider_->setRange(0, 100);
    bgBrightnessSlider_->setValue(45);
    bgBrightnessSlider_->setStyleSheet(UiTheme::dialogSliderStyleSheet());
    bgBrightnessSlider_->setToolTip(
        UiText::text(QStringLiteral("cover.backdrop_brightness")));
    form->addRow(UiText::text(QStringLiteral("cover.brightness_2")), bgBrightnessSlider_);

    controlsColumn->addWidget(canvasGroup);
    emphasizeGroupTitle(canvasGroup);

    // ---- Difficulty-card section (an opt-in layer, like the chart frame) ----
    auto* cardGroup = new QGroupBox(
        UiText::text(QStringLiteral("cover.difficulty_card_options")), this);
    cardGroup_ = cardGroup;
    auto* cardForm = new QFormLayout(cardGroup);
    cardForm->setSpacing(10);
    cardForm->setLabelAlignment(Qt::AlignLeft);
    cardForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // The card's "shown" state is now the inspector's 显示 checkbox (§3.2). This
    // legacy "add card" toggle is kept (it still mirrors card visibility for
    // reset / import / enable-gating) but hidden so there is only one control.
    cardCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("cover.add_difficulty_card")), this);
    cardCheck_->setChecked(true);
    cardForm->addRow(QString(), cardCheck_);
    cardCheck_->hide();

    // DX / SD chart type. "Standard" is the QML-side mode value (the card shows
    // the スタンダード plate top-right and mirrors the tab shoulder); anything
    // else shows the でらっくす plate top-left.
    cardModeCombo_ = new QComboBox(this);
    cardModeCombo_->setProperty("miacode.combo_text_alignment",
                                static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
    cardModeCombo_->addItem(QStringLiteral("DX"), QStringLiteral("DX"));
    cardModeCombo_->addItem(QStringLiteral("SD"), QStringLiteral("Standard"));
    {
        const int modeIdx = cardModeCombo_->findData(banner_.mode);
        if (modeIdx >= 0) cardModeCombo_->setCurrentIndex(modeIdx);
    }
    UiTheme::styleDialogComboBox(cardModeCombo_, 12);
    cardForm->addRow(UiText::text(QStringLiteral("cover.chart_type")), cardModeCombo_);

    // Card drop shadow.
    cardShadowCheck_ = new QCheckBox(UiText::text(QStringLiteral("cover.card_drop_shadow")), this);
    cardShadowCheck_->setChecked(false);
    cardForm->addRow(QString(), cardShadowCheck_);

    // Level text render.
    levelTextRenderCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("cover.render_level_as_text")), this);
    levelTextRenderCheck_->setChecked(false);
    cardForm->addRow(QString(), levelTextRenderCheck_);

    // Long-text overflow.
    textOverflowCombo_ = new QComboBox(this);
    textOverflowCombo_->setProperty("miacode.combo_text_alignment",
                                    static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
    textOverflowCombo_->addItem(
        UiText::text(QStringLiteral("cover.shrink_to_fit")), QStringLiteral("shrink"));
    textOverflowCombo_->addItem(
        UiText::text(QStringLiteral("cover.keep_size_ellipsis")),
        QStringLiteral("ellipsis"));
    UiTheme::styleDialogComboBox(textOverflowCombo_, 12);
    cardForm->addRow(UiText::text(QStringLiteral("cover.long_text")), textOverflowCombo_);

    // Custom card fonts (标题字体 / 正文字体) — a font change refreshes the live
    // preview and export via cachedTemplate_'s fonts override in buildInputs().
    cardFontSelector_ = miacode::video_export::createCardFontSelector(
        this,
        [this]() {
            pushInputs();
            emit compositionChanged();
        },
        miacode::video_export::FontComboWidthMode::NarrowInspector);
    if (cardFontSelector_.widget != nullptr) {
        cardForm->addRow(cardFontSelector_.widget);   // spans both columns (has own labels)
    }

    controlsColumn->addWidget(cardGroup);
    emphasizeGroupTitle(cardGroup);

    // ---- Chart-frame section (an opt-in layer) ----
    auto* frameGroup = new QGroupBox(
        UiText::text(QStringLiteral("cover.chart_frame")), this);
    auto* frameForm = new QFormLayout(frameGroup);
    frameForm->setSpacing(10);
    frameForm->setLabelAlignment(Qt::AlignLeft);
    frameForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Chart frame (a square playfield grab at a picked time, added as a layer).
    chartFrameCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("cover.add_chart_frame")), this);
    chartFrameCheck_->setChecked(false);
    chartFrameCheck_->setEnabled(chartFrameAvailable_);
    if (!chartFrameAvailable_) {
        chartFrameCheck_->setToolTip(
            UiText::text(QStringLiteral("cover.this_difficulty_has_no_chart")));
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
    playButton_->setToolTip(UiText::text(QStringLiteral("cover.play_pause_visual_only")));
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
    frameForm->addRow(UiText::text(QStringLiteral("cover.frame_time_2")), frameRow);

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

    // Legacy hidden controls kept as a compatibility bridge for older code paths;
    // the visible inspector exposes the current [Jacket | Transparent] mode UI.
    chartFrameBgCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("cover.chart_frame_inner_background")), this);
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
    frameForm->addRow(UiText::text(QStringLiteral("cover.background_brightness")),
                      chartFrameBgBrightnessSlider_);

    chartFrameBgTransparencySlider_ = new QSlider(Qt::Horizontal, this);
    chartFrameBgTransparencySlider_->setMinimum(0);
    chartFrameBgTransparencySlider_->setMaximum(100);
    chartFrameBgTransparencySlider_->setValue(50);
    chartFrameBgTransparencySlider_->setEnabled(false);
    frameForm->addRow(UiText::text(QStringLiteral("cover.background_transparency")),
                      chartFrameBgTransparencySlider_);

    controlsColumn->addWidget(frameGroup);
    frameGroup->hide();
    controlsColumn->addStretch(1);

    // Height of the three stacked control groups + the two inter-group gaps. The
    // preview frame is sized to this (minus its 1px margins) so its BOTTOM lines up
    // with the bottom of the 谱面帧 group — both columns start at the same top.
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* exportButton = buttonBox->addButton(
        UiText::text(QStringLiteral("cover.export")), QDialogButtonBox::AcceptRole);
    exportButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    if (QPushButton* cancelButton = buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelButton->setText(UiText::text(QStringLiteral("media_tools.cancel")));
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
    connect(bgBrightnessSlider_, &QSlider::valueChanged, this, [this] {
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
            const bool imageMode = chartFrameBgCheck_ != nullptr && chartFrameBgCheck_->isChecked();
            layer->setFrameBgMode(imageMode ? QStringLiteral("image") : QStringLiteral("transparent"));
            if (!imageMode) {
                layer->setFrameBgTransparency(1.0);
            }
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
    connect(chartFrameBgTransparencySlider_, &QSlider::valueChanged, this, [this](int value) {
        if (miacode::cover_export::CoverLayer* layer = activeChartFrameLayer()) {
            layer->setFrameBgTransparency(value / 100.0);
        }
        pushInputs();
        emit compositionChanged();
    });

    syncControlEnabled();
    schedulePreviewResize();
    pushInputs();

    // App-level preference restore: reopen with the last edited composition.
    // The final state is saved on export and again while closing. Silent —
    // fallback notices are suppressed; the explicit 导入布局 path stays interactive.
    const QJsonObject savedPreferences = miacode::cover_export::CoverCompositionState::loadPreferences();
    if (!savedPreferences.isEmpty()) {
        applyCompositionJson(savedPreferences, /*interactive=*/false);
    }
    compositionPersistenceGuard_ = std::make_unique<miacode::cover_export::CoverCompositionPersistenceGuard>(
        [this]() { return exportCompositionJson(); },
        [](const QJsonObject& payload) {
            return miacode::cover_export::CoverCompositionState::savePreferences(payload);
        });
}

void CoverStudioPanel::persistCompositionNow()
{
    if (compositionPersistenceGuard_ == nullptr) {
        return;
    }
    if (!compositionPersistenceGuard_->persistNow()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Export,
            QStringLiteral("cover/preferences"),
            QStringLiteral("failed to persist final cover composition"),
            /*force=*/true,
            miacode::debug_log::Level::Warn);
    }
    // Widgets are still alive here; stop the guard from reading them again during
    // the later widget-tree teardown (see persistCompositionNow's header note).
    compositionPersistenceGuard_->disarm();
}

CoverStudioPanel::~CoverStudioPanel()
{
    // Do NOT persist here: exportCompositionJson() reads the option-group widgets
    // (backgroundCombo_ / cardModeCombo_ / textOverflowCombo_ …) that CoverStudioWindow
    // reparents into its inspector column. During widget-tree teardown those live in a
    // sibling subtree that is freed BEFORE this panel, so reading them from the
    // destructor dereferences dangling QComboBox pointers (they are raw, non-QPointer
    // members, so the != nullptr guards do not catch the free) → EXC_BAD_ACCESS.
    // The real save happens in CoverStudioWindow::closeEvent while everything is alive;
    // here we only disarm so this guard (and its own destructor) never touch the widgets.
    if (compositionPersistenceGuard_ != nullptr) {
        compositionPersistenceGuard_->disarm();
    }
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

bool CoverStudioPanel::chartFrameImageBackgroundAvailable() const
{
    const QString bg = backgroundCombo_ != nullptr ? backgroundCombo_->currentData().toString()
                                                   : QStringLiteral("jacket");
    return bg != QStringLiteral("transparent");
}

void CoverStudioPanel::setActiveLayerKey(const QString& key)
{
    if (model_ == nullptr || activeLayerKey_ == key) {
        return;
    }
    if (!key.isEmpty() && model_->layer(key) == nullptr) {
        return;
    }
    cancelFrameTransportHold();
    // P3 / §7.1 — freeze the OUTGOING chart frame into a still (only if its time
    // changed since the last grab, §12.8) so it stays visible after it loses the
    // live scene. activeChartFrameLayer() still reads the OLD active key here.
    if (miacode::cover_export::CoverLayer* outgoing = activeChartFrameLayer()) {
        if (outgoing->visible() && chartFrameStillDirty(outgoing)) {
            renderChartFrameLayerNow(outgoing);
        }
    }
    activeLayerKey_ = key;
    if (miacode::cover_export::CoverLayer* layer = activeLayer();
        layer != nullptr && layer->kind() == QStringLiteral("chartFrame")) {
        lastChartFrameTemplateKey_ = layer->key();
    }
    if (composerView_ != nullptr) {
        const miacode::cover_export::CoverLayer* layer = activeChartFrameLayer();
        composerView_->setActiveChartFrameKey(layer != nullptr ? layer->key() : QString());
        composerView_->setSelectedKey(activeLayerKey_);   // §4 — move the canvas chrome
    }
    syncActiveLayerControls();
    emit activeLayerChanged(activeLayerKey_);
}

bool CoverStudioPanel::chartFrameStillDirty(const miacode::cover_export::CoverLayer* layer) const
{
    if (layer == nullptr) {
        return false;
    }
    if (layer->imageRevision() < 0) {
        return true;   // never grabbed
    }
    const auto it = grabbedSeconds_.constFind(layer->key());
    if (it == grabbedSeconds_.constEnd()) {
        return true;
    }
    return qAbs(it.value() - layer->frameSeconds()) > 1e-4;
}

miacode::cover_export::CoverLayer* CoverStudioPanel::chartFrameTemplateLayer() const
{
    if (model_ == nullptr) {
        return nullptr;
    }
    if (miacode::cover_export::CoverLayer* layer = model_->layer(lastChartFrameTemplateKey_);
        layer != nullptr && layer->kind() == QStringLiteral("chartFrame")) {
        return layer;
    }

    QList<miacode::cover_export::CoverLayer*> frames = model_->chartFrameLayers();
    std::sort(frames.begin(), frames.end(), [](const auto* a, const auto* b) {
        return a->z() > b->z();
    });
    return frames.isEmpty() ? nullptr : frames.constFirst();
}

void CoverStudioPanel::addChartFrameLayer()
{
    if (model_ == nullptr || !chartFrameAvailable_) {
        return;
    }
    miacode::cover_export::CoverLayer* templateLayer = chartFrameTemplateLayer();
    miacode::cover_export::CoverLayer* layer =
        model_->addChartFrameLayerFromTemplate(
            templateLayer,
            sceneFrameRenderer_ != nullptr ? sceneFrameRenderer_->playheadSeconds() : 0.0);
    if (layer == nullptr) {
        return;
    }
    // Stagger the new frame so it doesn't perfectly cover the centred card / prior
    // frames — visibility no longer depends on z (§12.7); z stays user-adjustable.
    const int frameCount = model_->chartFrameLayers().size();
    const qreal offset = 0.04 * ((frameCount - 1) % 6);
    if (offset > 0.0) {
        layer->setNx(0.5 + offset);
        layer->setNy(0.5 + offset);
    }
    setActiveLayerKey(layer->key());
    if (chartFrameCheck_ != nullptr) {
        const QSignalBlocker block(chartFrameCheck_);
        chartFrameCheck_->setChecked(true);
    }
    // P3 / §7.1 — grab a still immediately so the new frame stays visible the moment
    // another layer is selected (active frame = live scene, the rest = cached still).
    // The cold first grab settles (~0.3 s) → flag the GUI busy.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    renderChartFrameLayerNow(layer);
    QApplication::restoreOverrideCursor();
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

void CoverStudioPanel::addImageLayer()
{
    if (model_ == nullptr) return;
    const QString file = QFileDialog::getOpenFileName(
        this,
        UiText::text(QStringLiteral("cover.choose_image")),
        QString(),
        UiText::text(QStringLiteral("cover.images_png_jpg_jpeg_bmp")));
    if (file.isEmpty()) {
        return;
    }
    miacode::cover_export::CoverLayer* layer = model_->addImageLayer(file);
    if (layer == nullptr) {
        return;
    }
    setActiveLayerKey(layer->key());
    pushInputs();
    emit compositionChanged();
}

void CoverStudioPanel::addTextLayer()
{
    if (model_ == nullptr) return;
    miacode::cover_export::CoverLayer* layer =
        model_->addTextLayer(UiText::text(QStringLiteral("cover.text_layer_default")));
    if (layer == nullptr) {
        return;
    }
    setActiveLayerKey(layer->key());
    pushInputs();
    emit compositionChanged();
}

void CoverStudioPanel::setActiveLayerImagePath(const QString& path)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setImagePath(path);
        pushInputs();
        emit compositionChanged();
    }
}

void CoverStudioPanel::browseActiveLayerImage()
{
    miacode::cover_export::CoverLayer* layer = activeLayer();
    if (layer == nullptr) {
        return;
    }
    const QString start = !layer->imagePath().isEmpty()
        ? QFileInfo(layer->imagePath()).absolutePath()
        : QString();
    const QString file = QFileDialog::getOpenFileName(
        this,
        UiText::text(QStringLiteral("cover.choose_image")),
        start,
        UiText::text(QStringLiteral("cover.images_png_jpg_jpeg_bmp")));
    if (file.isEmpty()) {
        return;
    }
    setActiveLayerImagePath(file);
}

void CoverStudioPanel::setActiveLayerText(const QString& text)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setText(text);
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerFontPath(const QString& path)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setFontPath(path);
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerTextColor(const QString& color)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setTextColor(color);
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerTextBold(bool bold)
{
    if (miacode::cover_export::CoverLayer* layer = activeLayer()) {
        layer->setTextBold(bold);
        emit compositionChanged();
    }
}

void CoverStudioPanel::removeActiveLayer()
{
    if (model_ == nullptr) return;
    const QString toRemove = activeLayerKey_;
    // Card is the stable layer — Delete on it is a no-op (don't error, don't move
    // selection), so holding Delete clears the chart frames one by one then stops.
    if (toRemove == miacode::cover_export::CoverLayoutModel::cardKey()) {
        return;
    }
    // Compute the neighbour to select BEFORE removing (needs the current ordering),
    // so repeated Delete walks down the list instead of jumping back to the card.
    const QString nextKey = model_->selectionAfterRemoval(toRemove);
    if (!model_->removeLayer(toRemove)) return;
    setActiveLayerKey(model_->layer(nextKey) != nullptr
                          ? nextKey
                          : miacode::cover_export::CoverLayoutModel::cardKey());
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

void CoverStudioPanel::setLayerVisible(const QString& key, bool visible)
{
    if (miacode::cover_export::CoverLayer* layer = model_ != nullptr ? model_->layer(key) : nullptr) {
        layer->setVisible(visible);
        if (!visible && layer == activeChartFrameLayer()) {
            stopPlayback();
            cancelFrameTransportHold();
        }
        if (visible && layer->kind() == QStringLiteral("chartFrame")
            && layer != activeChartFrameLayer()
            && chartFrameStillDirty(layer)) {
            renderChartFrameLayerNow(layer);
        }
        syncActiveLayerControls();
        pushInputs();
        emit compositionChanged();
    }
}

void CoverStudioPanel::setLayerLocked(const QString& key, bool locked)
{
    if (miacode::cover_export::CoverLayer* layer = model_ != nullptr ? model_->layer(key) : nullptr) {
        layer->setLocked(locked);
        syncActiveLayerControls();
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerVisible(bool visible)
{
    setLayerVisible(activeLayerKey_, visible);
}

void CoverStudioPanel::setActiveLayerLocked(bool locked)
{
    setLayerLocked(activeLayerKey_, locked);
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
    if (playing_) {
        stopPlayback();   // a user scrub (transport slider / Home-End) pauses playback
    }
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
        layer->setFrameBgMode(enabled ? QStringLiteral("image") : QStringLiteral("transparent"));
        if (!enabled) {
            layer->setFrameBgTransparency(1.0);
        }
        syncActiveLayerControls();
        pushInputs();
        emit compositionChanged();
    }
}

void CoverStudioPanel::setActiveLayerFrameBgMode(const QString& mode)
{
    if (miacode::cover_export::CoverLayer* layer = activeChartFrameLayer()) {
        layer->setFrameBgMode(mode);
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

void CoverStudioPanel::setActiveLayerFrameBgTransparency(qreal transparency)
{
    if (miacode::cover_export::CoverLayer* layer = activeChartFrameLayer()) {
        layer->setFrameBgTransparency(transparency);
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

void CoverStudioPanel::cancelFrameTransportHold()
{
    stopFrameHeldSeek();
}

QWidget* CoverStudioPanel::canvasOptionsGroup(QWidget* parent)
{
    if (canvasGroup_ != nullptr) {
        canvasGroup_->setParent(parent);
        canvasGroup_->show();
    }
    return canvasGroup_;
}

QWidget* CoverStudioPanel::cardOptionsGroup(QWidget* parent)
{
    if (cardGroup_ != nullptr) {
        cardGroup_->setParent(parent);
        // Polymorphic §3.3 slot: the card-options group is only shown while the card
        // layer is selected; seed from the current selection so it isn't flashed on open.
        const miacode::cover_export::CoverLayer* sel = activeLayer();
        cardGroup_->setVisible(sel != nullptr && sel->kind() == QStringLiteral("card"));
    }
    return cardGroup_;
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
        layer = model_->addChartFrameLayerFromTemplate(
            chartFrameTemplateLayer(),
            sceneFrameRenderer_ != nullptr ? sceneFrameRenderer_->playheadSeconds() : 0.0);
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
            UiText::text(QStringLiteral("cover.chart_frame")),
            UiText::text(QStringLiteral("cover.could_not_render_the_chart")));
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
    if (!hasFrame) {
        stopPlayback();
    }
    if (chartFrameBgCheck_ != nullptr && hasFrame) {
        const QSignalBlocker block(chartFrameBgCheck_);
        chartFrameBgCheck_->setChecked(layer->frameBgMode() == QStringLiteral("image"));
    }
    if (chartFrameBgBrightnessSlider_ != nullptr && hasFrame) {
        const QSignalBlocker block(chartFrameBgBrightnessSlider_);
        chartFrameBgBrightnessSlider_->setValue(qRound(layer->frameBgBrightness() * 100.0));
    }
    if (chartFrameBgTransparencySlider_ != nullptr && hasFrame) {
        const QSignalBlocker block(chartFrameBgTransparencySlider_);
        chartFrameBgTransparencySlider_->setValue(qRound(layer->frameBgTransparency() * 100.0));
    }
    if (sceneFrameRenderer_ != nullptr && hasFrame) {
        sceneFrameRenderer_->setPlayheadSeconds(layer->frameSeconds());
    }
    if (composerView_ != nullptr) {
        composerView_->refreshLiveChartScene();
    }
    // Polymorphic §3.3 slot: the card-options group follows the selection.
    if (cardGroup_ != nullptr) {
        const miacode::cover_export::CoverLayer* sel = activeLayer();
        cardGroup_->setVisible(sel != nullptr && sel->kind() == QStringLiteral("card"));
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
        grabbedSeconds_[layer->key()] = seconds;   // P3 dirty-check baseline
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
    emit playheadChanged(seconds);   // §12.9 — drive the transport view
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
    emit playbackStateChanged(true);
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
    emit playbackStateChanged(false);
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

bool CoverStudioPanel::handleShortcutKey(QKeyEvent* event)
{
    if (event == nullptr) {
        return false;
    }
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool ctrl = mods.testFlag(Qt::ControlModifier);
    const bool shift = mods.testFlag(Qt::ShiftModifier);
    miacode::cover_export::CoverLayer* layer = activeLayer();
    switch (event->key()) {
    case Qt::Key_Space:
        togglePlayback();
        return true;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (!ctrl && !shift) { removeActiveLayer(); return true; }
        break;
    case Qt::Key_A:
        if (mods == Qt::NoModifier) { addChartFrameLayer(); return true; }
        break;
    case Qt::Key_V:
        if (mods == Qt::NoModifier && layer != nullptr) { setActiveLayerVisible(!layer->visible()); return true; }
        break;
    case Qt::Key_L:
        if (mods == Qt::NoModifier && layer != nullptr) { setActiveLayerLocked(!layer->locked()); return true; }
        break;
    case Qt::Key_BracketLeft:
        if (ctrl) moveActiveLayerToBottom(); else moveActiveLayerDown();
        return true;
    case Qt::Key_BracketRight:
        if (ctrl) moveActiveLayerToTop(); else moveActiveLayerUp();
        return true;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        if (ctrl) { zoomPreviewIn(); return true; }
        nudgeActiveLayerScale(0.02);
        return true;
    case Qt::Key_Minus:
        if (ctrl) { zoomPreviewOut(); return true; }
        nudgeActiveLayerScale(-0.02);
        return true;
    case Qt::Key_0:
        if (ctrl) { resetPreviewZoom(); return true; }
        break;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        if (!ctrl) { nudgeActiveLayerPosition(event->key(), shift); return true; }
        break;
    default:
        break;
    }
    return false;
}

bool CoverStudioPanel::handleFrameTransportShortcut(QKeyEvent* event)
{
    if (event == nullptr) {
        return false;
    }
    if (event->type() == QEvent::KeyRelease) {
        if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right)
            && frameSeekHoldKey_ == event->key()) {
            if (event->isAutoRepeat()) {
                return true;
            }
            stopFrameHeldSeek(event->key());
            return true;
        }
        if (!chartFrameAvailable_ || activeChartFrameLayer() == nullptr) {
            return false;
        }
        if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
            return true;
        }
        return false;
    }
    if (!chartFrameAvailable_ || activeChartFrameLayer() == nullptr) {
        return false;
    }
    if (event->type() != QEvent::KeyPress || event->modifiers() != Qt::NoModifier) {
        return false;
    }
    switch (event->key()) {
    case Qt::Key_Space:
        if (!event->isAutoRepeat()) {
            togglePlayback();
        }
        return true;
    case Qt::Key_Left:
    case Qt::Key_Right: {
        if (event->isAutoRepeat()) {
            return true;
        }
        if (playing_) {
            stopPlayback();   // arrow scrub pauses, like any other scrub
        }
        const int direction = event->key() == Qt::Key_Left ? -1 : 1;
        beginFrameHeldSeek(direction, event->key());
        stepFrameBySeconds(
            static_cast<double>(direction)
            * miacode::preview_interaction::kSeekSingleStepSeconds);
        emit compositionChanged();
        return true;
    }
    case Qt::Key_Home:
        if (!event->isAutoRepeat()) {
            setActiveLayerFrameSeconds(0.0);
        }
        return true;
    case Qt::Key_End:
        if (!event->isAutoRepeat()) {
            setActiveLayerFrameSeconds(contentDurationSeconds_);
        }
        return true;
    default:
        return false;
    }
}

void CoverStudioPanel::nudgeActiveLayerPosition(int key, bool coarse)
{
    miacode::cover_export::CoverLayer* layer = activeLayer();
    if (layer == nullptr || layer->locked()) {
        return;   // lock freezes position (§12.6)
    }
    const QSize size = currentSize();
    // §12.3 — fine = 1 output px, coarse (Shift) = 10 px, in normalized units.
    const qreal stepX = (coarse ? 10.0 : 1.0) / qMax(1, size.width());
    const qreal stepY = (coarse ? 10.0 : 1.0) / qMax(1, size.height());
    qreal nx = layer->nx();
    qreal ny = layer->ny();
    switch (key) {
    case Qt::Key_Left:  nx -= stepX; break;
    case Qt::Key_Right: nx += stepX; break;
    case Qt::Key_Up:    ny -= stepY; break;
    case Qt::Key_Down:  ny += stepY; break;
    default: return;
    }
    setActiveLayerCenter(nx, ny);
}

void CoverStudioPanel::nudgeActiveLayerScale(qreal delta)
{
    miacode::cover_export::CoverLayer* layer = activeLayer();
    if (layer == nullptr || layer->locked()) {
        return;   // lock freezes size (§12.6)
    }
    setActiveLayerSizeFraction(layer->sizeFraction() + delta);
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
        // §8 keymap — the canvas (native quick window) is the primary editing focus.
        if (handleShortcutKey(keyEvent)) {
            return true;
        }
    }
    if (watched == frameSlider_) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (handleFrameTransportShortcut(keyEvent)) {
                return true;
            }
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
            cancelFrameTransportHold();
        }
    }
    if (previewScrollArea_ != nullptr
        && watched == previewScrollArea_->viewport()
        && event->type() == QEvent::Resize) {
        schedulePreviewResize();
    }
    return QWidget::eventFilter(watched, event);
}

void CoverStudioPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    schedulePreviewResize();
}

void CoverStudioPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    schedulePreviewResize();
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
    // Overlay the difficulty-card custom fonts (empty == bundled default). Applied
    // to the per-call copy, so cachedTemplate_ stays pristine; the same override
    // drives the live preview and the export (both grab this scene).
    if (cardFontSelector_.widget != nullptr) {
        miacode::video_export::applyBannerFontOverride(
            in.templateMap, cardFontSelector_.displayPath(), cardFontSelector_.bodyPath());
    }

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
    in.coverBgBrightness = bgBrightnessSlider_ != nullptr ? bgBrightnessSlider_->value() / 100.0 : 0.45;
    in.cardShadow = cardShadowCheck_ != nullptr && cardShadowCheck_->isChecked();

    // B1 — chart-frame inner-ring background (reuses the cover background image via
    // the QML's backdropSourceUrl). The disk diameter is the playfield ring as a
    // fraction of the square frame; 0 when the renderer isn't bootstrapped.
    const miacode::cover_export::CoverLayer* frameLayer = activeChartFrameLayer();
    in.chartFrameBackground = frameLayer != nullptr
        ? frameLayer->frameBgMode() == QStringLiteral("image")
        : (chartFrameBgCheck_ != nullptr && chartFrameBgCheck_->isChecked());
    in.chartFrameBgBrightness = frameLayer != nullptr
        ? frameLayer->frameBgBrightness()
        : (chartFrameBgBrightnessSlider_ != nullptr ? chartFrameBgBrightnessSlider_->value() / 100.0 : 0.8);
    in.chartFrameBgTransparency = frameLayer != nullptr
        ? frameLayer->frameBgTransparency()
        : (chartFrameBgTransparencySlider_ != nullptr ? chartFrameBgTransparencySlider_->value() / 100.0 : 0.5);
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

void CoverStudioPanel::zoomPreviewIn()
{
    setPreviewZoom(previewZoom_ + kPreviewZoomStep);
}

void CoverStudioPanel::zoomPreviewOut()
{
    setPreviewZoom(previewZoom_ - kPreviewZoomStep);
}

void CoverStudioPanel::resetPreviewZoom()
{
    setPreviewZoom(1.0);
}

void CoverStudioPanel::setPreviewZoom(qreal zoom)
{
    const qreal next = qBound(kPreviewZoomMin, zoom, kPreviewZoomMax);
    if (qFuzzyCompare(previewZoom_, next)) {
        return;
    }
    previewZoom_ = next;
    resizePreviewToAspect();
    emit previewZoomChanged(previewZoom_);
}

void CoverStudioPanel::centerPreviewScroll()
{
    if (previewScrollArea_ == nullptr) {
        return;
    }
    if (QScrollBar* bar = previewScrollArea_->horizontalScrollBar()) {
        bar->setValue((bar->minimum() + bar->maximum()) / 2);
    }
    if (QScrollBar* bar = previewScrollArea_->verticalScrollBar()) {
        bar->setValue((bar->minimum() + bar->maximum()) / 2);
    }
}

void CoverStudioPanel::schedulePreviewResize()
{
    if (previewResizeQueued_) {
        return;
    }
    previewResizeQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        previewResizeQueued_ = false;
        resizePreviewToAspect();
    });
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
    QSize available = previewScrollArea_ != nullptr ? previewScrollArea_->viewport()->size()
                                                    : (previewFrame_ != nullptr ? previewFrame_->contentsRect().size() : size());
    available -= QSize(2, 2);
    if (available.width() < 160 || available.height() < 160) {
        available = QSize(kPreviewBox, kPreviewBox);
    }
    const double aspect = sz.height() > 0 ? static_cast<double>(sz.width()) / sz.height() : 1.0;
    int pw = available.width();
    int ph = qRound(pw / aspect);
    if (ph > available.height()) {
        ph = available.height();
        pw = qRound(ph * aspect);
    }
    previewContainer_->setFixedSize(qMax(1, qRound(pw * previewZoom_)),
                                    qMax(1, qRound(ph * previewZoom_)));
    centerPreviewScroll();

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
    // Checkpoint ALL settings app-wide before export. Closing checkpoints the
    // final edited state again, so edits made after an export are not lost.
    if (compositionPersistenceGuard_ != nullptr) {
        compositionPersistenceGuard_->persistNow();
    }
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
                UiText::text(QStringLiteral("cover.chart_frame")),
                UiText::text(QStringLiteral("cover.the_chart_frame_could_not")));
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
    // Blur + backdrop brightness only apply when there is a backdrop behind the card.
    if (blurCheck_ != nullptr) blurCheck_->setEnabled(!isTransparent);
    if (bgBrightnessSlider_ != nullptr) bgBrightnessSlider_->setEnabled(!isTransparent);

    // Card sub-options only matter while the card layer is shown. Read the card's
    // visibility from the model (the inspector's 显示 drives it now; the legacy
    // cardCheck_ is hidden), so gating stays correct however it was toggled.
    const miacode::cover_export::CoverLayer* cardLayer =
        model_ != nullptr ? model_->layer(miacode::cover_export::CoverLayoutModel::cardKey()) : nullptr;
    const bool cardOn = cardLayer == nullptr || cardLayer->visible();
    if (cardModeCombo_ != nullptr) cardModeCombo_->setEnabled(cardOn);
    if (cardShadowCheck_ != nullptr) cardShadowCheck_->setEnabled(cardOn);
    if (levelTextRenderCheck_ != nullptr) levelTextRenderCheck_->setEnabled(cardOn);
    if (textOverflowCombo_ != nullptr) textOverflowCombo_->setEnabled(cardOn);

    // B1 inner-ring background: image mode needs a cover background to sample;
    // transparent mode draws its own black disk and remains available without one.
    const miacode::cover_export::CoverLayer* frameLayer = activeChartFrameLayer();
    const bool chartFrameOn =
        frameLayer != nullptr && frameLayer->visible() && chartFrameAvailable_;
    const bool innerBgEnable = chartFrameOn;
    const bool imageMode = frameLayer != nullptr && frameLayer->frameBgMode() == QStringLiteral("image");
    if (chartFrameBgCheck_ != nullptr) chartFrameBgCheck_->setEnabled(innerBgEnable);
    if (chartFrameBgBrightnessSlider_ != nullptr) {
        chartFrameBgBrightnessSlider_->setEnabled(innerBgEnable && imageMode && !isTransparent);
    }
    if (chartFrameBgTransparencySlider_ != nullptr) {
        chartFrameBgTransparencySlider_->setEnabled(innerBgEnable && !imageMode);
    }
}

void CoverStudioPanel::browseBackground()
{
    const QString start = backgroundPathEdit_ != nullptr && !backgroundPathEdit_->text().trimmed().isEmpty()
        ? QFileInfo(backgroundPathEdit_->text().trimmed()).absolutePath()
        : QString();
    const QString file = QFileDialog::getOpenFileName(
        this,
        UiText::text(QStringLiteral("cover.choose_background_image")),
        start,
        UiText::text(QStringLiteral("cover.images_png_jpg_jpeg_bmp")));
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
    bg.insert(QStringLiteral("brightness"),
              bgBrightnessSlider_ != nullptr ? bgBrightnessSlider_->value() / 100.0 : 0.45);
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
    // Custom card fonts as absolute paths (empty == default). A path missing on
    // another machine falls back to the bundled font at render time (§8).
    if (cardFontSelector_.widget != nullptr) {
        card.insert(QStringLiteral("fontDisplay"), cardFontSelector_.displayPath());
        card.insert(QStringLiteral("fontBody"), cardFontSelector_.bodyPath());
    }
    state.card = card;

    // Layer geometry + visibility (chart-frame enabled == its layer visible) +
    // frameSeconds, via CoverLayoutModel (the layers are its responsibility).
    if (model_ != nullptr) {
        state.layout = model_->toJson();
    }
    return state.toJson();
}

QJsonObject CoverStudioPanel::exportPresetCompositionJson() const
{
    QJsonObject root = exportCompositionJson();
    root.remove(QStringLiteral("size"));
    return root;
}

void CoverStudioPanel::applyPresetCompositionJson(const QJsonObject& root)
{
    applyCompositionJson(root, /*interactive=*/true);
}

void CoverStudioPanel::saveLayout()
{
    stopPlayback();
    QString path = QFileDialog::getSaveFileName(
        this,
        UiText::text(QStringLiteral("cover.save_cover_layout")),
        QStringLiteral("cover-layout.miacover"),
        UiText::text(QStringLiteral("cover.cover_layout_miacover")));
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
            UiText::text(QStringLiteral("cover.save_layout_2")),
            UiText::text(QStringLiteral("cover.could_not_write_the_layout")));
        return;
    }
    miacode::cover_export::CoverCompositionState::pushRecentFile(path);
}

void CoverStudioPanel::importLayout()
{
    // Legacy .json stays importable (early layouts saved with that suffix);
    // the identity check inside is what actually validates the file.
    const QString path = QFileDialog::getOpenFileName(
        this,
        UiText::text(QStringLiteral("cover.import_cover_layout")),
        QString(),
        UiText::text(QStringLiteral("cover.cover_layout_miacover_legacy_json")));
    if (path.isEmpty()) {
        return;
    }
    importLayoutFromPath(path);
}

void CoverStudioPanel::importLayoutFromPath(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            this,
            UiText::text(QStringLiteral("cover.import_layout_2")),
            UiText::text(QStringLiteral("cover.could_not_read_the_layout")));
        return;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        QMessageBox::warning(
            this,
            UiText::text(QStringLiteral("cover.import_layout_2")),
            UiText::text(QStringLiteral("cover.the_layout_file_is_not")));
        return;
    }
    const QJsonObject root = doc.object();
    // Identity guard: an unrelated but structurally-valid JSON would otherwise be
    // accepted and silently reset the composition toward defaults. Require our tag.
    if (root.value(QStringLiteral("kind")).toString() != QStringLiteral("miacode-cover-composition")) {
        QMessageBox::warning(
            this,
            UiText::text(QStringLiteral("cover.import_layout_2")),
            UiText::text(QStringLiteral("cover.this_file_is_not_a")));
        return;
    }
    applyCompositionJson(root);
    miacode::cover_export::CoverCompositionState::pushRecentFile(path);
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
    if (bgBrightnessSlider_ != nullptr) {
        // Old layouts predate this key → fall back to 0.45 (the legacy fixed look).
        const QSignalBlocker block(bgBrightnessSlider_);
        bgBrightnessSlider_->setValue(qBound(0, qRound(bg.value(QStringLiteral("brightness")).toDouble(0.45) * 100.0), 100));
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
    if (cardFontSelector_.widget != nullptr) {
        // setSelection suppresses onChanged; the trailing pushInputs() applies it.
        cardFontSelector_.setSelection(card.value(QStringLiteral("fontDisplay")).toString(),
                                       card.value(QStringLiteral("fontBody")).toString());
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
        notice += UiText::text(QStringLiteral("cover.the_custom_background_image_was"));
    }
    if (chartFrameDropped) {
        if (!notice.isEmpty()) {
            notice += QLatin1Char('\n');
        }
        notice += UiText::text(QStringLiteral("cover.the_imported_layout_included_a_chart_frame"));
    }
    if (!notice.isEmpty()) {
        QMessageBox::information(
            this, UiText::text(QStringLiteral("cover.import_layout_2")), notice);
    }
}

}  // namespace miacode::cover_export
