#include "tools/cover_export/CoverFramePickerPanel.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>

namespace miacode::cover_export {
namespace {

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

QString formatSeconds(double seconds)
{
    const int totalCs = qMax(0, qRound(seconds * 100.0));
    const int cs = totalCs % 100;
    const int s = (totalCs / 100) % 60;
    const int m = totalCs / 6000;
    return QStringLiteral("%1:%2.%3")
        .arg(m)
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(cs, 2, 10, QLatin1Char('0'));
}

}  // namespace

CoverFramePickerPanel::CoverFramePickerPanel(CoverStudioPanel* studio, QWidget* parent)
    : QWidget(parent)
    , studio_(studio)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    backButton_ = new QPushButton(QStringLiteral("<"), this);
    forwardButton_ = new QPushButton(QStringLiteral(">"), this);
    slider_ = new QSlider(Qt::Horizontal, this);
    label_ = new QLabel(formatSeconds(0.0), this);
    label_->setMinimumWidth(64);
    auto* title = new QLabel(l10n(QStringLiteral("Frame"), QStringLiteral("帧")), this);
    title->setMinimumWidth(42);

    backButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    forwardButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    slider_->setStyleSheet(UiTheme::dialogSliderStyleSheet());
    backButton_->setToolTip(l10n(QStringLiteral("Previous frame"), QStringLiteral("上一帧")));
    forwardButton_->setToolTip(l10n(QStringLiteral("Next frame"), QStringLiteral("下一帧")));
    slider_->setToolTip(l10n(QStringLiteral("Frame time for the selected chart frame"), QStringLiteral("当前谱面帧的时间")));
    backButton_->setAccessibleName(backButton_->toolTip());
    forwardButton_->setAccessibleName(forwardButton_->toolTip());

    layout->addWidget(title);
    layout->addWidget(backButton_);
    layout->addWidget(slider_, 1);
    layout->addWidget(forwardButton_);
    layout->addWidget(label_);

    connect(backButton_, &QPushButton::clicked, this, [this] {
        if (studio_ != nullptr) studio_->stepActiveFrameBySeconds(-1.0 / 120.0);
    });
    connect(forwardButton_, &QPushButton::clicked, this, [this] {
        if (studio_ != nullptr) studio_->stepActiveFrameBySeconds(1.0 / 120.0);
    });
    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        if (studio_ != nullptr) studio_->setActiveLayerFrameSeconds(value / 1000.0);
    });

    if (studio_ != nullptr) {
        connect(studio_, &CoverStudioPanel::activeLayerChanged, this, &CoverFramePickerPanel::refresh);
        connect(studio_, &CoverStudioPanel::compositionChanged, this, &CoverFramePickerPanel::refresh);
    }
    refresh();
}

void CoverFramePickerPanel::refresh()
{
    CoverLayer* layer = nullptr;
    if (studio_ != nullptr && studio_->layoutModel() != nullptr) {
        layer = studio_->layoutModel()->layer(studio_->activeLayerKey());
    }
    const bool enabled = layer != nullptr && layer->kind() == QStringLiteral("chartFrame")
        && studio_ != nullptr && studio_->chartFrameAvailable();
    const int maxMs = studio_ != nullptr ? qMax(1, qRound(studio_->contentDurationSeconds() * 1000.0)) : 1;
    {
        const QSignalBlocker b(slider_);
        slider_->setRange(0, maxMs);
        slider_->setValue(enabled ? qRound(layer->frameSeconds() * 1000.0) : 0);
    }
    label_->setText(formatSeconds(enabled ? layer->frameSeconds() : 0.0));
    backButton_->setEnabled(enabled);
    forwardButton_->setEnabled(enabled);
    slider_->setEnabled(enabled);
}

}  // namespace miacode::cover_export
