#include "tools/cover_export/CoverInspectorPanel.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "tools/video_export/FontLibrary.h"
#include "EditableValueLabel.h"
#include "UiText.h"
#include "UiComponents.h"
#include "UiTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace miacode::cover_export {
namespace {

constexpr int kFrameTransportSliderHeight = 24;

QString displayLabel(const CoverLayer* layer)
{
    if (layer == nullptr) {
        return QString();
    }
    if (layer->kind() == QStringLiteral("card")) {
        return UiText::text(QStringLiteral("cover.difficulty_card"));
    }
    if (layer->kind() == QStringLiteral("chartFrame")) {
        return UiText::text(QStringLiteral("cover.chart_frame"));
    }
    return layer->label();
}

// mm:ss.cs for the read-only frame-time readout.
QString formatFrameTime(double seconds)
{
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int totalCs = qRound(seconds * 100.0);
    const int cs = totalCs % 100;
    const int totalSec = totalCs / 100;
    return QStringLiteral("%1:%2.%3")
        .arg(totalSec / 60)
        .arg(totalSec % 60, 2, 10, QLatin1Char('0'))
        .arg(cs, 2, 10, QLatin1Char('0'));
}

QString formatFrameTimeWithDuration(double seconds, double duration)
{
    return formatFrameTime(seconds) + QStringLiteral(" / ") + formatFrameTime(duration);
}

QIcon makeTransportIcon(bool pause, const QColor& color)
{
    constexpr int kSize = 16;
    constexpr qreal kDpr = 2.0;
    QPixmap pixmap(qRound(kSize * kDpr), qRound(kSize * kDpr));
    pixmap.setDevicePixelRatio(kDpr);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    if (pause) {
        const qreal barWidth = 2.5;
        const qreal gap = 3.0;
        const qreal barHeight = 10.0;
        const qreal top = (kSize - barHeight) / 2.0;
        painter.drawRoundedRect(QRectF(kSize / 2.0 - gap / 2.0 - barWidth, top, barWidth, barHeight), 1.0, 1.0);
        painter.drawRoundedRect(QRectF(kSize / 2.0 + gap / 2.0, top, barWidth, barHeight), 1.0, 1.0);
    } else {
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

QString frameTransportSliderStyleSheet()
{
    const UiTheme::Colors& c = UiTheme::colors();
    const QColor handleBg = c.dark ? c.accentText : QColor(QStringLiteral("#FFFFFF"));
    const QColor handleBorder = c.borderSoft;
    return UiTheme::formSliderStyleSheet()
        + QStringLiteral(
              "QSlider::handle:horizontal { width: 18px; height: 18px; margin: -6px 0; border-radius: 9px;"
              " background: %1; border: 1px solid %2; }"
              "QSlider::handle:horizontal:hover { background: %1; border-color: %2; }"
              "QSlider::handle:horizontal:pressed { background: %1; border-color: %2; }")
              .arg(handleBg.name(QColor::HexRgb),
                   handleBorder.name(QColor::HexRgb));
}

void configureTransportSlider(QSlider* slider)
{
    if (slider == nullptr) {
        return;
    }
    // Keep the preview scrubber track, but make the frame playhead easier to see.
    slider->setStyleSheet(frameTransportSliderStyleSheet());
    // W4: the styled handle uses negative margins, so pin a measured height.
    slider->ensurePolished();
    slider->setFixedHeight(qMax(slider->sizeHint().height(), kFrameTransportSliderHeight));
}

QString frameTimeButtonStyle()
{
    return UiTheme::dialogPushButtonStyleSheet()
        + QStringLiteral("QPushButton { min-width: 24px; max-width: 26px;"
                         " min-height: 24px; max-height: 26px; padding: 0; border-radius: 6px; }");
}

QString inspectorSectionTitleStyle()
{
    const UiTheme::Colors& c = UiTheme::colors();
    return QStringLiteral(
        "QGroupBox { border: 1px solid %3; border-radius: 8px; margin-top: 13px;"
        " padding-top: 8px; color: %1; font-weight: 700; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px;"
        " color: %1; font-size: 13px; font-weight: 700;"
        " padding: 0 6px; }"
        "QGroupBox QLabel, QGroupBox QCheckBox, QGroupBox QComboBox,"
        " QGroupBox QLineEdit, QGroupBox QPushButton { font-weight: 400; }"
        "QLabel[role=\"coverFrameTimeLabel\"] { color: %2; font-weight: 600; }")
        .arg(c.textPrimary.name(QColor::HexRgb),
             c.textSecondary.name(QColor::HexRgb),
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
    const QFont contentFont = qApp != nullptr ? qApp->font() : QWidget().font();
    const QList<QWidget*> children = group->findChildren<QWidget*>();
    for (QWidget* child : children) {
        if (child != nullptr) {
            child->setFont(contentFont);
        }
    }
}

}  // namespace

CoverInspectorPanel::CoverInspectorPanel(CoverStudioPanel* studio, QWidget* parent)
    : QWidget(parent)
    , studio_(studio)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    // ---- §3.2 layer (common) ----
    layerGroup_ = new QGroupBox(UiText::text(QStringLiteral("cover.layer")), this);
    auto* form = new QFormLayout(layerGroup_);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    visibleCheck_ = new QCheckBox(this);
    lockedCheck_ = new QCheckBox(this);
    visibleCheck_->setToolTip(UiText::text(QStringLiteral("cover.show_or_hide_this_layer")));
    lockedCheck_->setToolTip(UiText::text(QStringLiteral("cover.lock_position_and_size_l")));
    form->addRow(UiText::text(QStringLiteral("cover.visible")), visibleCheck_);
    form->addRow(UiText::text(QStringLiteral("cover.lock")), lockedCheck_);

    opacitySlider_ = new QSlider(Qt::Horizontal, this);
    opacitySlider_->setRange(0, 100);
    opacitySlider_->setToolTip(UiText::text(QStringLiteral("cover.layer_opacity")));
    form->addRow(UiText::text(QStringLiteral("cover.opacity")),
                 miacode::ui::createSliderValueRow(opacitySlider_, &opacityValue_, QStringLiteral("%"), this));

    sizeSlider_ = new QSlider(Qt::Horizontal, this);
    sizeSlider_->setRange(5, 200);
    sizeSlider_->setToolTip(UiText::text(QStringLiteral("cover.layer_size")));
    form->addRow(UiText::text(QStringLiteral("cover.size")),
                 miacode::ui::createSliderValueRow(sizeSlider_, &sizeValue_, QStringLiteral("%"), this));

    xSlider_ = new QSlider(Qt::Horizontal, this);
    xSlider_->setRange(0, 100);
    xSlider_->setToolTip(UiText::text(QStringLiteral("cover.horizontal_position")));
    form->addRow(UiText::text(QStringLiteral("cover.x")),
                 miacode::ui::createSliderValueRow(xSlider_, &xValue_, QString(), this));

    ySlider_ = new QSlider(Qt::Horizontal, this);
    ySlider_->setRange(0, 100);
    ySlider_->setToolTip(UiText::text(QStringLiteral("cover.vertical_position")));
    form->addRow(UiText::text(QStringLiteral("cover.y")),
                 miacode::ui::createSliderValueRow(ySlider_, &yValue_, QString(), this));

    root->addWidget(layerGroup_);

    // ---- §3.3 chart-frame options (polymorphic; shown only for chart frames) ----
    frameOptionsGroup_ = new QGroupBox(
        UiText::text(QStringLiteral("cover.chart_frame_options")), this);
    auto* frameForm = new QFormLayout(frameOptionsGroup_);
    frameForm->setContentsMargins(8, 8, 8, 8);
    frameForm->setSpacing(8);
    frameForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    frameBgModeCombo_ = new QComboBox(this);
    frameBgModeCombo_->setProperty("miacode.combo_text_alignment",
                                   static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter));
    frameBgModeCombo_->addItem(UiText::text(QStringLiteral("cover.jacket")),
                               QStringLiteral("image"));
    frameBgModeCombo_->addItem(UiText::text(QStringLiteral("cover.transparent")),
                               QStringLiteral("transparent"));
    frameBgModeCombo_->setToolTip(UiText::text(QStringLiteral("cover.chart_frame_inner_background")));
    UiTheme::styleDialogComboBox(frameBgModeCombo_, 12);
    frameForm->addRow(UiText::text(QStringLiteral("cover.inner_bg")), frameBgModeCombo_);

    frameBgBrightnessSlider_ = new QSlider(Qt::Horizontal, this);
    frameBgBrightnessSlider_->setRange(0, 100);
    frameBgBrightnessSlider_->setToolTip(UiText::text(QStringLiteral("cover.chart_frame_background_brightness")));
    frameBgBrightnessRow_ = miacode::ui::createSliderValueRow(
        frameBgBrightnessSlider_, &frameBgBrightnessValue_, QStringLiteral("%"), this);
    frameForm->addRow(UiText::text(QStringLiteral("cover.brightness")),
                      frameBgBrightnessRow_);

    frameBgTransparencySlider_ = new QSlider(Qt::Horizontal, this);
    frameBgTransparencySlider_->setRange(0, 100);
    frameBgTransparencySlider_->setToolTip(UiText::text(QStringLiteral("cover.chart_frame_background_transparency")));
    frameBgTransparencyRow_ = miacode::ui::createSliderValueRow(
        frameBgTransparencySlider_, &frameBgTransparencyValue_, QStringLiteral("%"), this);
    frameForm->addRow(UiText::text(QStringLiteral("cover.transparency")),
                      frameBgTransparencyRow_);

    auto* frameTimeRow = new QWidget(this);
    auto* frameTimeLayout = new QHBoxLayout(frameTimeRow);
    frameTimeLayout->setContentsMargins(0, 0, 0, 0);
    frameTimeLayout->setSpacing(6);
    framePlayButton_ = new QPushButton(frameTimeRow);
    framePlayButton_->setIcon(makeTransportIcon(false, UiTheme::colors().textPrimary));
    framePlayButton_->setIconSize(QSize(16, 16));
    framePlayButton_->setStyleSheet(frameTimeButtonStyle());
    framePlayButton_->setFixedSize(28, 28);
    framePlayButton_->setToolTip(UiText::text(QStringLiteral("cover.play_pause_space")));
    framePlayButton_->setAccessibleName(framePlayButton_->toolTip());
    frameTimeSlider_ = new QSlider(Qt::Horizontal, frameTimeRow);
    frameTimeSlider_->setRange(0, qMax(1, qRound(studio_ != nullptr ? studio_->contentDurationSeconds() * 1000.0 : 1.0)));
    configureTransportSlider(frameTimeSlider_);
    frameTimeSlider_->setToolTip(UiText::text(QStringLiteral("cover.frame_time_for_the_selected_2")));
    frameTimeSlider_->installEventFilter(this);
    frameTimeReadout_ = new QLabel(formatFrameTimeWithDuration(0.0, studio_ != nullptr ? studio_->contentDurationSeconds() : 0.0), frameTimeRow);
    frameTimeReadout_->setMinimumWidth(100);
    frameTimeReadout_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    frameTimeLayout->addWidget(framePlayButton_, 0);
    frameTimeLayout->addWidget(frameTimeSlider_, 1);
    frameTimeLayout->addWidget(frameTimeReadout_, 0);
    auto* frameTimeLabel = new QLabel(UiText::text(QStringLiteral("cover.frame_time")), frameOptionsGroup_);
    frameTimeLabel->setProperty("role", QStringLiteral("coverFrameTimeLabel"));
    frameTimeLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    frameTimeLayout->insertWidget(0, frameTimeLabel, 0);
    frameForm->addRow(frameTimeRow);

    // ---- §3.3 image options (polymorphic; shown only for image layers) ----
    imageOptionsGroup_ = new QGroupBox(UiText::text(QStringLiteral("cover.image_options")), this);
    auto* imageForm = new QFormLayout(imageOptionsGroup_);
    imageForm->setContentsMargins(8, 8, 8, 8);
    imageForm->setSpacing(8);
    imageForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    auto* imageRow = new QWidget(imageOptionsGroup_);
    auto* imageRowLayout = new QHBoxLayout(imageRow);
    imageRowLayout->setContentsMargins(0, 0, 0, 0);
    imageRowLayout->setSpacing(6);
    imagePathEdit_ = new QLineEdit(imageRow);
    imagePathEdit_->setPlaceholderText(UiText::text(QStringLiteral("cover.image_file")));
    imageBrowseButton_ = miacode::ui::createDialogAuxiliaryButton(
        imageRow, UiText::text(QStringLiteral("cover.browse")));
    imageRowLayout->addWidget(imagePathEdit_, 1);
    imageRowLayout->addWidget(imageBrowseButton_, 0);
    imageForm->addRow(UiText::text(QStringLiteral("cover.image_file")), imageRow);

    // ---- §3.3 text options (polymorphic; shown only for text layers) ----
    textOptionsGroup_ = new QGroupBox(UiText::text(QStringLiteral("cover.text_options")), this);
    auto* textForm = new QFormLayout(textOptionsGroup_);
    textForm->setContentsMargins(8, 8, 8, 8);
    textForm->setSpacing(8);
    textForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    textEdit_ = new QLineEdit(textOptionsGroup_);
    textEdit_->setPlaceholderText(UiText::text(QStringLiteral("cover.text_content")));
    textForm->addRow(UiText::text(QStringLiteral("cover.text_content")), textEdit_);

    auto* fontRow = new QWidget(textOptionsGroup_);
    auto* fontRowLayout = new QHBoxLayout(fontRow);
    fontRowLayout->setContentsMargins(0, 0, 0, 0);
    fontRowLayout->setSpacing(6);
    textFontCombo_ = miacode::ui::createDialogComboBox(fontRow, 12, Qt::AlignLeft | Qt::AlignVCenter);
    textFontImportButton_ = miacode::ui::createDialogAuxiliaryButton(
        fontRow, UiText::text(QStringLiteral("card_font.import")));
    fontRowLayout->addWidget(textFontCombo_, 1);
    fontRowLayout->addWidget(textFontImportButton_, 0);
    refreshTextFontCombo(QString());
    textForm->addRow(UiText::text(QStringLiteral("cover.font")), fontRow);

    textColorButton_ = miacode::ui::createDialogAuxiliaryButton(textOptionsGroup_, QStringLiteral("#FFFFFF"));
    textForm->addRow(UiText::text(QStringLiteral("cover.text_color")), textColorButton_);

    textBoldCheck_ = new QCheckBox(textOptionsGroup_);
    textForm->addRow(UiText::text(QStringLiteral("cover.bold")), textBoldCheck_);

    emphasizeGroupTitle(layerGroup_);
    emphasizeGroupTitle(frameOptionsGroup_);
    emphasizeGroupTitle(imageOptionsGroup_);
    emphasizeGroupTitle(textOptionsGroup_);

    root->addWidget(frameOptionsGroup_);
    root->addWidget(imageOptionsGroup_);
    root->addWidget(textOptionsGroup_);
    root->addStretch(1);

    // ---- wiring (identical setters to the old inspector) ----
    connect(visibleCheck_, &QCheckBox::toggled, studio_, &CoverStudioPanel::setActiveLayerVisible);
    connect(lockedCheck_, &QCheckBox::toggled, studio_, &CoverStudioPanel::setActiveLayerLocked);
    connect(opacitySlider_, &QSlider::valueChanged, this, [this](int value) {
        if (studio_ != nullptr) studio_->setActiveLayerOpacity(value / 100.0);
    });
    connect(sizeSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (studio_ != nullptr) studio_->setActiveLayerSizeFraction(value / 100.0);
    });
    connect(xSlider_, &QSlider::valueChanged, this, [this](int value) {
        CoverLayer* layer = nullptr;
        if (studio_ != nullptr && studio_->layoutModel() != nullptr) {
            layer = studio_->layoutModel()->layer(studio_->activeLayerKey());
        }
        if (studio_ != nullptr && layer != nullptr) studio_->setActiveLayerCenter(value / 100.0, layer->ny());
    });
    connect(ySlider_, &QSlider::valueChanged, this, [this](int value) {
        CoverLayer* layer = nullptr;
        if (studio_ != nullptr && studio_->layoutModel() != nullptr) {
            layer = studio_->layoutModel()->layer(studio_->activeLayerKey());
        }
        if (studio_ != nullptr && layer != nullptr) studio_->setActiveLayerCenter(layer->nx(), value / 100.0);
    });
    connect(frameBgModeCombo_, &QComboBox::currentIndexChanged, this, [this] {
        if (studio_ != nullptr) {
            const QString mode = frameBgModeCombo_ != nullptr
                ? frameBgModeCombo_->currentData().toString()
                : QStringLiteral("image");
            studio_->setActiveLayerFrameBgMode(
                mode == QStringLiteral("transparent") ? QStringLiteral("transparent")
                                                      : QStringLiteral("image"));
        }
    });
    connect(frameBgBrightnessSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (studio_ != nullptr) studio_->setActiveLayerFrameBgBrightness(value / 100.0);
    });
    connect(frameBgTransparencySlider_, &QSlider::valueChanged, this, [this](int value) {
        if (studio_ != nullptr) studio_->setActiveLayerFrameBgTransparency(value / 100.0);
    });
    connect(framePlayButton_, &QPushButton::clicked, this, [this] {
        if (studio_ != nullptr) studio_->togglePlayback();
    });
    connect(frameTimeSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (studio_ != nullptr) studio_->setActiveLayerFrameSeconds(value / 1000.0);
    });

    // ---- image options wiring ----
    connect(imageBrowseButton_, &QPushButton::clicked, this, [this] {
        if (studio_ != nullptr) studio_->browseActiveLayerImage();
    });
    connect(imagePathEdit_, &QLineEdit::editingFinished, this, [this] {
        if (studio_ != nullptr) studio_->setActiveLayerImagePath(imagePathEdit_->text().trimmed());
    });

    // ---- text options wiring ----
    connect(textEdit_, &QLineEdit::textEdited, this, [this](const QString& value) {
        if (studio_ != nullptr) studio_->setActiveLayerText(value);
    });
    connect(textFontCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (studio_ != nullptr && textFontCombo_ != nullptr) {
            studio_->setActiveLayerFontPath(textFontCombo_->itemData(index).toString());
        }
    });
    connect(textFontImportButton_, &QPushButton::clicked, this, [this] {
        const QString imported = miacode::video_export::importFontIntoLibrary(this);
        if (imported.isEmpty()) {
            return;
        }
        refreshTextFontCombo(imported);
        if (studio_ != nullptr) studio_->setActiveLayerFontPath(imported);
    });
    connect(textColorButton_, &QPushButton::clicked, this, [this] {
        CoverLayer* layer = nullptr;
        if (studio_ != nullptr && studio_->layoutModel() != nullptr) {
            layer = studio_->layoutModel()->layer(studio_->activeLayerKey());
        }
        const QColor initial(layer != nullptr ? layer->textColor() : QStringLiteral("#FFFFFF"));
        const QColor chosen = QColorDialog::getColor(
            initial.isValid() ? initial : QColor(Qt::white), this,
            UiText::text(QStringLiteral("cover.text_color")));
        if (!chosen.isValid()) {
            return;
        }
        const QString hex = chosen.name(QColor::HexRgb);
        updateTextColorSwatch(hex);
        if (studio_ != nullptr) studio_->setActiveLayerTextColor(hex);
    });
    connect(textBoldCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (studio_ != nullptr) studio_->setActiveLayerTextBold(checked);
    });

    if (studio_ != nullptr) {
        connect(studio_, &CoverStudioPanel::activeLayerChanged, this, &CoverInspectorPanel::refresh);
        connect(studio_, &CoverStudioPanel::compositionChanged, this, &CoverInspectorPanel::refresh);
        connect(studio_, &CoverStudioPanel::playheadChanged, this, [this](double seconds) {
            if (frameTimeSlider_ == nullptr || !frameTimeSlider_->isEnabled()) {
                return;
            }
            {
                const QSignalBlocker block(frameTimeSlider_);
                frameTimeSlider_->setValue(qRound(seconds * 1000.0));
            }
            if (frameTimeReadout_ != nullptr) {
                frameTimeReadout_->setText(formatFrameTimeWithDuration(
                    seconds, studio_ != nullptr ? studio_->contentDurationSeconds() : 0.0));
            }
        });
        connect(studio_, &CoverStudioPanel::playbackStateChanged, this, [this](bool playing) {
            if (framePlayButton_ != nullptr) {
                framePlayButton_->setIcon(makeTransportIcon(playing, UiTheme::colors().textPrimary));
            }
        });
    }
    refresh();
}

void CoverInspectorPanel::bindLayerSignals(CoverLayer* layer)
{
    if (layer == boundLayer_.data()) {
        return;   // already bound — don't churn connections on every refresh
    }
    if (boundLayer_) {
        disconnect(boundLayer_.data(), nullptr, this, nullptr);
    }
    boundLayer_ = layer;
    if (layer != nullptr) {
        // Canvas drag/scale writes these directly on the layer (no compositionChanged),
        // so connect them straight to refresh; refresh blocks the sliders, so the
        // round trip can't loop.
        connect(layer, &CoverLayer::nxChanged, this, &CoverInspectorPanel::refresh);
        connect(layer, &CoverLayer::nyChanged, this, &CoverInspectorPanel::refresh);
        connect(layer, &CoverLayer::sizeFractionChanged, this, &CoverInspectorPanel::refresh);
        connect(layer, &CoverLayer::opacityChanged, this, &CoverInspectorPanel::refresh);
    }
}

bool CoverInspectorPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == frameTimeSlider_ && studio_ != nullptr && frameTimeSlider_ != nullptr) {
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
            && frameTimeSlider_->isEnabled()) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (studio_->handleFrameTransportShortcut(keyEvent)) {
                return true;
            }
        } else if (event->type() == QEvent::FocusOut) {
            studio_->cancelFrameTransportHold();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CoverInspectorPanel::refresh()
{
    CoverLayer* layer = nullptr;
    if (studio_ != nullptr && studio_->layoutModel() != nullptr) {
        layer = studio_->layoutModel()->layer(studio_->activeLayerKey());
    }
    bindLayerSignals(layer);

    const bool hasLayer = layer != nullptr;
    const bool isFrame = hasLayer && layer->kind() == QStringLiteral("chartFrame");
    const bool locked = hasLayer && layer->locked();

    layerGroup_->setVisible(hasLayer);
    layerGroup_->setTitle(hasLayer
        ? UiText::text(QStringLiteral("cover.layer_2")) + displayLabel(layer)
        : UiText::text(QStringLiteral("cover.layer")));

    {
        const QSignalBlocker b(visibleCheck_);
        visibleCheck_->setChecked(hasLayer && layer->visible());
    }
    {
        const QSignalBlocker b(lockedCheck_);
        lockedCheck_->setChecked(locked);
    }

    // Sliders are written with their signals blocked (so they don't re-enter the
    // studio setters), so the click-to-type readouts are refreshed by hand here.
    const int opacityVal = hasLayer ? qRound(layer->opacity() * 100.0) : 100;
    const int sizeVal = hasLayer ? qRound(layer->sizeFraction() * 100.0) : 85;
    const int xVal = hasLayer ? qRound(layer->nx() * 100.0) : 50;
    const int yVal = hasLayer ? qRound(layer->ny() * 100.0) : 50;
    {
        const QSignalBlocker b(opacitySlider_);
        opacitySlider_->setValue(opacityVal);
    }
    {
        const QSignalBlocker b(sizeSlider_);
        sizeSlider_->setValue(sizeVal);
    }
    {
        const QSignalBlocker b(xSlider_);
        xSlider_->setValue(xVal);
    }
    {
        const QSignalBlocker b(ySlider_);
        ySlider_->setValue(yVal);
    }
    opacityValue_->setText(QString::number(opacityVal) + QStringLiteral("%"));
    sizeValue_->setText(QString::number(sizeVal) + QStringLiteral("%"));
    xValue_->setText(QString::number(xVal));
    yValue_->setText(QString::number(yVal));

    visibleCheck_->setEnabled(hasLayer);
    lockedCheck_->setEnabled(hasLayer);
    opacitySlider_->setEnabled(hasLayer);
    opacityValue_->setEnabled(hasLayer);
    // Lock freezes position + size only — opacity / visibility stay editable (§12.6).
    sizeSlider_->setEnabled(hasLayer && !locked);
    sizeValue_->setEnabled(hasLayer && !locked);
    xSlider_->setEnabled(hasLayer && !locked);
    xValue_->setEnabled(hasLayer && !locked);
    ySlider_->setEnabled(hasLayer && !locked);
    yValue_->setEnabled(hasLayer && !locked);

    frameOptionsGroup_->setVisible(isFrame);
    if (isFrame) {
        const QString frameBgMode = layer->frameBgMode() == QStringLiteral("transparent")
            ? QStringLiteral("transparent")
            : QStringLiteral("image");
        {
            const QSignalBlocker b(frameBgModeCombo_);
            const int modeIndex = frameBgModeCombo_->findData(frameBgMode);
            if (modeIndex >= 0) {
                frameBgModeCombo_->setCurrentIndex(modeIndex);
            }
        }
        const int brightnessVal = qRound(layer->frameBgBrightness() * 100.0);
        const int transparencyVal = qRound(layer->frameBgTransparency() * 100.0);
        {
            const QSignalBlocker b(frameBgBrightnessSlider_);
            frameBgBrightnessSlider_->setValue(brightnessVal);
        }
        {
            const QSignalBlocker b(frameBgTransparencySlider_);
            frameBgTransparencySlider_->setValue(transparencyVal);
        }
        frameBgBrightnessValue_->setText(QString::number(brightnessVal) + QStringLiteral("%"));
        frameBgTransparencyValue_->setText(QString::number(transparencyVal) + QStringLiteral("%"));
        const int frameMs = qRound(layer->frameSeconds() * 1000.0);
        const int durationMs = qMax(1, qRound(studio_ != nullptr ? studio_->contentDurationSeconds() * 1000.0 : 1.0));
        {
            const QSignalBlocker b(frameTimeSlider_);
            frameTimeSlider_->setRange(0, durationMs);
            frameTimeSlider_->setValue(qBound(0, frameMs, durationMs));
        }
        frameTimeReadout_->setText(formatFrameTimeWithDuration(
            layer->frameSeconds(), studio_ != nullptr ? studio_->contentDurationSeconds() : 0.0));
        framePlayButton_->setEnabled(studio_ != nullptr && studio_->chartFrameAvailable() && layer->visible());
        frameTimeSlider_->setEnabled(studio_ != nullptr && studio_->chartFrameAvailable());
        const bool imageMode = frameBgMode == QStringLiteral("image");
        const bool frameControlsEnabled = studio_ != nullptr && studio_->chartFrameAvailable();
        const bool imageBackgroundAvailable = studio_ != nullptr && studio_->chartFrameImageBackgroundAvailable();
        if (frameBgModeCombo_ != nullptr) {
            frameBgModeCombo_->setEnabled(frameControlsEnabled);
            if (const int imageIndex = frameBgModeCombo_->findData(QStringLiteral("image")); imageIndex >= 0) {
                frameBgModeCombo_->setItemData(
                    imageIndex,
                    frameControlsEnabled && imageBackgroundAvailable ? QVariant() : QVariant(0),
                    Qt::UserRole - 1
                );
            }
        }
        if (auto* frameForm = qobject_cast<QFormLayout*>(frameOptionsGroup_->layout())) {
            if (frameBgBrightnessRow_ != nullptr) {
                frameForm->setRowVisible(frameBgBrightnessRow_, imageMode);
            }
            if (frameBgTransparencyRow_ != nullptr) {
                frameForm->setRowVisible(frameBgTransparencyRow_, !imageMode);
            }
        }
        frameBgBrightnessSlider_->setEnabled(frameControlsEnabled && imageMode && imageBackgroundAvailable);
        frameBgBrightnessValue_->setEnabled(frameControlsEnabled && imageMode && imageBackgroundAvailable);
        frameBgTransparencySlider_->setEnabled(frameControlsEnabled && !imageMode);
        frameBgTransparencyValue_->setEnabled(frameControlsEnabled && !imageMode);
    }

    // ---- image layer ----
    const bool isImage = hasLayer && layer->kind() == QStringLiteral("image");
    imageOptionsGroup_->setVisible(isImage);
    if (isImage && imagePathEdit_ != nullptr) {
        // Lock freezes position + size only (§12.6) — the image source, like
        // opacity / visibility, stays editable while locked.
        if (imagePathEdit_->text() != layer->imagePath()) {
            const QSignalBlocker b(imagePathEdit_);
            imagePathEdit_->setText(layer->imagePath());
        }
    }

    // ---- text layer ----
    const bool isText = hasLayer && layer->kind() == QStringLiteral("text");
    textOptionsGroup_->setVisible(isText);
    if (isText) {
        if (textEdit_ != nullptr && textEdit_->text() != layer->text()) {
            const QSignalBlocker b(textEdit_);
            textEdit_->setText(layer->text());
        }
        if (textFontCombo_ != nullptr) {
            const QString fontPath = layer->fontPath();
            const int idx = textFontCombo_->findData(fontPath);
            if (idx < 0 && !fontPath.isEmpty()) {
                refreshTextFontCombo(fontPath);   // font added elsewhere — re-list
            } else {
                const QSignalBlocker b(textFontCombo_);
                textFontCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
            }
        }
        if (textBoldCheck_ != nullptr) {
            const QSignalBlocker b(textBoldCheck_);
            textBoldCheck_->setChecked(layer->textBold());
        }
        updateTextColorSwatch(layer->textColor());
    }
}

void CoverInspectorPanel::refreshTextFontCombo(const QString& selectedPath)
{
    if (textFontCombo_ == nullptr) {
        return;
    }
    miacode::video_export::populateFontCombo(
        textFontCombo_, selectedPath, /*includeDefault=*/true,
        UiText::text(QStringLiteral("card_font.default")));
    miacode::ui::applyDialogComboBoxStyle(textFontCombo_, 12);
}

void CoverInspectorPanel::updateTextColorSwatch(const QString& color)
{
    if (textColorButton_ == nullptr) {
        return;
    }
    QColor c(color);
    if (!c.isValid()) {
        c = QColor(Qt::white);
    }
    const QString fg = (c.lightnessF() > 0.6) ? QStringLiteral("#101010") : QStringLiteral("#FFFFFF");
    textColorButton_->setText(c.name(QColor::HexRgb).toUpper());
    textColorButton_->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: 1px solid rgba(128,128,128,120);"
        " border-radius: 6px; padding: 4px 12px; }")
        .arg(c.name(QColor::HexRgb), fg));
}

}  // namespace miacode::cover_export
