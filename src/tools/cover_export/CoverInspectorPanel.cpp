#include "tools/cover_export/CoverInspectorPanel.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "EditableValueLabel.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace miacode::cover_export {
namespace {

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

QString displayLabel(const CoverLayer* layer)
{
    if (layer == nullptr) {
        return QString();
    }
    if (layer->kind() == QStringLiteral("card")) {
        return l10n(QStringLiteral("Difficulty card"), QStringLiteral("难度卡"));
    }
    if (layer->kind() == QStringLiteral("chartFrame")) {
        return l10n(QStringLiteral("Chart frame"), QStringLiteral("谱面帧"));
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

// A slider + click-to-type value readout (§10). The value mirrors the slider on
// drag and commits a typed number back through the slider's setValue (which
// re-fires the studio setter), so dragging and typing behave identically.
QWidget* makeSliderValueRow(QSlider* slider, miacode::ui::EditableValueLabel** valueOut,
                            const QString& suffix, QWidget* parent)
{
    slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    auto* value = new miacode::ui::EditableValueLabel(QStringLiteral("0") + suffix, row);
    value->setMinimumWidth(46);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->bindSlider(slider);
    layout->addWidget(slider, 1);
    layout->addWidget(value, 0);
    QObject::connect(slider, &QSlider::valueChanged, value, [value, suffix](int v) {
        value->setText(QString::number(v) + suffix);
    });
    *valueOut = value;
    return row;
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
    layerGroup_ = new QGroupBox(l10n(QStringLiteral("Layer"), QStringLiteral("图层")), this);
    auto* form = new QFormLayout(layerGroup_);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    visibleCheck_ = new QCheckBox(this);
    lockedCheck_ = new QCheckBox(this);
    visibleCheck_->setToolTip(l10n(QStringLiteral("Show or hide this layer (V)"),
                                   QStringLiteral("显示或隐藏当前图层（快捷键 V）")));
    lockedCheck_->setToolTip(l10n(QStringLiteral("Lock position and size (L)"),
                                  QStringLiteral("锁定位置与大小，防止拖动（快捷键 L）")));
    form->addRow(l10n(QStringLiteral("Visible"), QStringLiteral("显示")), visibleCheck_);
    form->addRow(l10n(QStringLiteral("Lock"), QStringLiteral("锁定")), lockedCheck_);

    opacitySlider_ = new QSlider(Qt::Horizontal, this);
    opacitySlider_->setRange(0, 100);
    opacitySlider_->setToolTip(l10n(QStringLiteral("Layer opacity"), QStringLiteral("图层不透明度")));
    form->addRow(l10n(QStringLiteral("Opacity"), QStringLiteral("不透明度")),
                 makeSliderValueRow(opacitySlider_, &opacityValue_, QStringLiteral("%"), this));

    sizeSlider_ = new QSlider(Qt::Horizontal, this);
    sizeSlider_->setRange(5, 200);
    sizeSlider_->setToolTip(l10n(QStringLiteral("Layer size"), QStringLiteral("图层大小")));
    form->addRow(l10n(QStringLiteral("Size"), QStringLiteral("大小")),
                 makeSliderValueRow(sizeSlider_, &sizeValue_, QStringLiteral("%"), this));

    xSlider_ = new QSlider(Qt::Horizontal, this);
    xSlider_->setRange(0, 100);
    xSlider_->setToolTip(l10n(QStringLiteral("Horizontal position"), QStringLiteral("水平位置")));
    form->addRow(l10n(QStringLiteral("X"), QStringLiteral("水平位置")),
                 makeSliderValueRow(xSlider_, &xValue_, QString(), this));

    ySlider_ = new QSlider(Qt::Horizontal, this);
    ySlider_->setRange(0, 100);
    ySlider_->setToolTip(l10n(QStringLiteral("Vertical position"), QStringLiteral("垂直位置")));
    form->addRow(l10n(QStringLiteral("Y"), QStringLiteral("垂直位置")),
                 makeSliderValueRow(ySlider_, &yValue_, QString(), this));

    root->addWidget(layerGroup_);

    // ---- §3.3 chart-frame options (polymorphic; shown only for chart frames) ----
    frameOptionsGroup_ = new QGroupBox(
        l10n(QStringLiteral("Chart frame options"), QStringLiteral("谱面帧选项")), this);
    auto* frameForm = new QFormLayout(frameOptionsGroup_);
    frameForm->setContentsMargins(8, 8, 8, 8);
    frameForm->setSpacing(8);
    frameForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    frameBgCheck_ = new QCheckBox(this);
    frameBgCheck_->setToolTip(l10n(QStringLiteral("Use cover background inside the chart frame"),
                                   QStringLiteral("在谱面帧内圈使用封面背景")));
    frameForm->addRow(l10n(QStringLiteral("Inner bg"), QStringLiteral("内圈背景")), frameBgCheck_);

    frameBgBrightnessSlider_ = new QSlider(Qt::Horizontal, this);
    frameBgBrightnessSlider_->setRange(0, 100);
    frameBgBrightnessSlider_->setToolTip(l10n(QStringLiteral("Chart-frame background brightness"),
                                              QStringLiteral("谱面帧背景亮度")));
    frameForm->addRow(l10n(QStringLiteral("Brightness"), QStringLiteral("亮度")),
                      makeSliderValueRow(frameBgBrightnessSlider_, &frameBgBrightnessValue_,
                                         QStringLiteral("%"), this));

    frameTimeReadout_ = new QLabel(formatFrameTime(0.0), this);
    frameTimeReadout_->setToolTip(l10n(QStringLiteral("Frame time (edit on the playback bar below)"),
                                       QStringLiteral("谱面帧时间（在下方播放条编辑）")));
    frameForm->addRow(l10n(QStringLiteral("Frame time"), QStringLiteral("帧时间")), frameTimeReadout_);

    root->addWidget(frameOptionsGroup_);
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
    connect(frameBgCheck_, &QCheckBox::toggled, studio_, &CoverStudioPanel::setActiveLayerFrameBgEnabled);
    connect(frameBgBrightnessSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (studio_ != nullptr) studio_->setActiveLayerFrameBgBrightness(value / 100.0);
    });

    if (studio_ != nullptr) {
        connect(studio_, &CoverStudioPanel::activeLayerChanged, this, &CoverInspectorPanel::refresh);
        connect(studio_, &CoverStudioPanel::compositionChanged, this, &CoverInspectorPanel::refresh);
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

    layerGroup_->setTitle(hasLayer
        ? l10n(QStringLiteral("Layer · "), QStringLiteral("图层 · ")) + displayLabel(layer)
        : l10n(QStringLiteral("Layer"), QStringLiteral("图层")));

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
        {
            const QSignalBlocker b(frameBgCheck_);
            frameBgCheck_->setChecked(layer->frameBgEnabled());
        }
        const int brightnessVal = qRound(layer->frameBgBrightness() * 100.0);
        {
            const QSignalBlocker b(frameBgBrightnessSlider_);
            frameBgBrightnessSlider_->setValue(brightnessVal);
        }
        frameBgBrightnessValue_->setText(QString::number(brightnessVal) + QStringLiteral("%"));
        frameTimeReadout_->setText(formatFrameTime(layer->frameSeconds()));
        frameBgBrightnessSlider_->setEnabled(layer->frameBgEnabled());
        frameBgBrightnessValue_->setEnabled(layer->frameBgEnabled());
    }
}

}  // namespace miacode::cover_export
