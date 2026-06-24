#include "tools/cover_export/CoverInspectorPanel.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>

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
        return l10n(QStringLiteral("Difficulty card"), QStringLiteral("难度卡片"));
    }
    if (layer->kind() == QStringLiteral("chartFrame")) {
        return l10n(QStringLiteral("Chart frame"), QStringLiteral("谱面帧"));
    }
    return layer->label();
}

void setupSlider(QSlider* slider)
{
    slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
}

}  // namespace

CoverInspectorPanel::CoverInspectorPanel(CoverStudioPanel* studio, QWidget* parent)
    : QWidget(parent)
    , studio_(studio)
{
    auto* form = new QFormLayout(this);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(8);

    auto* title = new QLabel(l10n(QStringLiteral("Inspector"), QStringLiteral("属性")), this);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    form->addRow(title);

    nameLabel_ = new QLabel(this);
    form->addRow(l10n(QStringLiteral("Layer"), QStringLiteral("图层")), nameLabel_);

    visibleCheck_ = new QCheckBox(this);
    lockedCheck_ = new QCheckBox(this);
    visibleCheck_->setToolTip(l10n(QStringLiteral("Show or hide this layer"), QStringLiteral("显示或隐藏当前图层")));
    lockedCheck_->setToolTip(l10n(QStringLiteral("Lock this layer on the preview canvas"), QStringLiteral("锁定当前图层，防止在预览画布中拖动")));
    form->addRow(l10n(QStringLiteral("Visible"), QStringLiteral("显示")), visibleCheck_);
    form->addRow(l10n(QStringLiteral("Lock"), QStringLiteral("锁定")), lockedCheck_);

    opacitySlider_ = new QSlider(Qt::Horizontal, this);
    opacitySlider_->setRange(0, 100);
    setupSlider(opacitySlider_);
    opacitySlider_->setToolTip(l10n(QStringLiteral("Layer opacity"), QStringLiteral("图层不透明度")));
    form->addRow(l10n(QStringLiteral("Opacity"), QStringLiteral("不透明度")), opacitySlider_);

    sizeSlider_ = new QSlider(Qt::Horizontal, this);
    sizeSlider_->setRange(5, 200);
    setupSlider(sizeSlider_);
    sizeSlider_->setToolTip(l10n(QStringLiteral("Layer size"), QStringLiteral("图层大小")));
    form->addRow(l10n(QStringLiteral("Size"), QStringLiteral("大小")), sizeSlider_);

    xSlider_ = new QSlider(Qt::Horizontal, this);
    xSlider_->setRange(0, 100);
    setupSlider(xSlider_);
    xSlider_->setToolTip(l10n(QStringLiteral("Horizontal position"), QStringLiteral("水平位置")));
    form->addRow(l10n(QStringLiteral("X"), QStringLiteral("水平位置")), xSlider_);

    ySlider_ = new QSlider(Qt::Horizontal, this);
    ySlider_->setRange(0, 100);
    setupSlider(ySlider_);
    ySlider_->setToolTip(l10n(QStringLiteral("Vertical position"), QStringLiteral("垂直位置")));
    form->addRow(l10n(QStringLiteral("Y"), QStringLiteral("垂直位置")), ySlider_);

    frameBgCheck_ = new QCheckBox(this);
    frameBgBrightnessSlider_ = new QSlider(Qt::Horizontal, this);
    frameBgBrightnessSlider_->setRange(0, 100);
    setupSlider(frameBgBrightnessSlider_);
    frameBgCheck_->setToolTip(l10n(QStringLiteral("Use cover background inside the chart frame"), QStringLiteral("在谱面帧内圈使用封面背景")));
    frameBgBrightnessSlider_->setToolTip(l10n(QStringLiteral("Chart-frame background brightness"), QStringLiteral("谱面帧背景亮度")));
    form->addRow(l10n(QStringLiteral("Frame bg"), QStringLiteral("帧背景")), frameBgCheck_);
    form->addRow(l10n(QStringLiteral("Brightness"), QStringLiteral("亮度")), frameBgBrightnessSlider_);

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

void CoverInspectorPanel::refresh()
{
    CoverLayer* layer = nullptr;
    if (studio_ != nullptr && studio_->layoutModel() != nullptr) {
        layer = studio_->layoutModel()->layer(studio_->activeLayerKey());
    }

    const bool hasLayer = layer != nullptr;
    const bool isFrame = hasLayer && layer->kind() == QStringLiteral("chartFrame");
    nameLabel_->setText(hasLayer ? displayLabel(layer) : QString());

    {
        const QSignalBlocker b(visibleCheck_);
        visibleCheck_->setChecked(hasLayer && layer->visible());
    }
    {
        const QSignalBlocker b(lockedCheck_);
        lockedCheck_->setChecked(hasLayer && layer->locked());
    }
    {
        const QSignalBlocker b(opacitySlider_);
        opacitySlider_->setValue(hasLayer ? qRound(layer->opacity() * 100.0) : 100);
    }
    {
        const QSignalBlocker b(sizeSlider_);
        sizeSlider_->setValue(hasLayer ? qRound(layer->sizeFraction() * 100.0) : 85);
    }
    {
        const QSignalBlocker b(xSlider_);
        xSlider_->setValue(hasLayer ? qRound(layer->nx() * 100.0) : 50);
    }
    {
        const QSignalBlocker b(ySlider_);
        ySlider_->setValue(hasLayer ? qRound(layer->ny() * 100.0) : 50);
    }
    {
        const QSignalBlocker b(frameBgCheck_);
        frameBgCheck_->setChecked(isFrame && layer->frameBgEnabled());
    }
    {
        const QSignalBlocker b(frameBgBrightnessSlider_);
        frameBgBrightnessSlider_->setValue(isFrame ? qRound(layer->frameBgBrightness() * 100.0) : 80);
    }

    visibleCheck_->setEnabled(hasLayer);
    lockedCheck_->setEnabled(hasLayer);
    opacitySlider_->setEnabled(hasLayer);
    sizeSlider_->setEnabled(hasLayer);
    xSlider_->setEnabled(hasLayer);
    ySlider_->setEnabled(hasLayer);
    frameBgCheck_->setEnabled(isFrame);
    frameBgBrightnessSlider_->setEnabled(isFrame);
}

}  // namespace miacode::cover_export
