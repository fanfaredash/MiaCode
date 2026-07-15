#include "tools/cover_export/CoverFramePickerPanel.h"

#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "UiText.h"
#include "UiComponents.h"
#include "UiTheme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSlider>

namespace miacode::cover_export {
namespace {

constexpr double kStepSeconds = 1.0 / 120.0;
constexpr int kFrameTransportSliderHeight = 24;

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

// Painted play / pause icon in the theme text colour (font glyphs render as a
// colour emoji on Windows). Mirrors CoverStudioPanel's makeTransportIcon.
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

}  // namespace

CoverFramePickerPanel::CoverFramePickerPanel(CoverStudioPanel* studio, QWidget* parent)
    : QWidget(parent)
    , studio_(studio)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    playIcon_ = makeTransportIcon(/*pause=*/false, UiTheme::colors().textPrimary);
    pauseIcon_ = makeTransportIcon(/*pause=*/true, UiTheme::colors().textPrimary);

    playButton_ = miacode::ui::createDialogPushButton(QString(), this);
    miacode::ui::applySmallDialogButton(playButton_);
    playButton_->setIcon(playIcon_);
    playButton_->setIconSize(QSize(16, 16));
    playButton_->setToolTip(UiText::text(QStringLiteral("cover.play_pause_space")));
    backButton_ = new QPushButton(QStringLiteral("‹"), this);
    forwardButton_ = new QPushButton(QStringLiteral("›"), this);
    backButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet()
        + QStringLiteral("QPushButton { min-width: 26px; max-width: 28px; padding: 0; }"));
    forwardButton_->setStyleSheet(backButton_->styleSheet());
    slider_ = new QSlider(Qt::Horizontal, this);
    label_ = new QLabel(formatSeconds(0.0) + QStringLiteral(" / ") + formatSeconds(0.0), this);
    label_->setMinimumWidth(112);
    label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* title = new QLabel(UiText::text(QStringLiteral("cover.frame")), this);
    title->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    // Keep the preview scrubber track, but make the frame playhead easier to see.
    slider_->setStyleSheet(frameTransportSliderStyleSheet());
    slider_->ensurePolished();
    slider_->setFixedHeight(qMax(slider_->sizeHint().height(), kFrameTransportSliderHeight));
    backButton_->setToolTip(UiText::text(QStringLiteral("cover.step_back")));
    forwardButton_->setToolTip(UiText::text(QStringLiteral("cover.step_forward")));
    slider_->setToolTip(UiText::text(QStringLiteral("cover.frame_time_for_the_selected")));
    playButton_->setAccessibleName(playButton_->toolTip());
    backButton_->setAccessibleName(backButton_->toolTip());
    forwardButton_->setAccessibleName(forwardButton_->toolTip());

    layout->addWidget(title);
    layout->addWidget(playButton_);
    layout->addWidget(backButton_);
    layout->addWidget(slider_, 1);
    layout->addWidget(forwardButton_);
    layout->addWidget(label_);

    slider_->installEventFilter(this);

    connect(playButton_, &QPushButton::clicked, this, [this] {
        if (studio_ != nullptr) studio_->togglePlayback();
    });
    connect(backButton_, &QPushButton::clicked, this, [this] {
        if (studio_ != nullptr) studio_->stepActiveFrameBySeconds(-kStepSeconds);
    });
    connect(forwardButton_, &QPushButton::clicked, this, [this] {
        if (studio_ != nullptr) studio_->stepActiveFrameBySeconds(kStepSeconds);
    });
    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        if (studio_ != nullptr) studio_->setActiveLayerFrameSeconds(value / 1000.0);
    });

    if (studio_ != nullptr) {
        connect(studio_, &CoverStudioPanel::activeLayerChanged, this, &CoverFramePickerPanel::refresh);
        connect(studio_, &CoverStudioPanel::compositionChanged, this, &CoverFramePickerPanel::refresh);
        connect(studio_, &CoverStudioPanel::playheadChanged, this, &CoverFramePickerPanel::setPositionSeconds);
        connect(studio_, &CoverStudioPanel::playbackStateChanged, this, &CoverFramePickerPanel::setPlaying);
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
    const double duration = studio_ != nullptr ? studio_->contentDurationSeconds() : 0.0;
    const int maxMs = qMax(1, qRound(duration * 1000.0));
    {
        const QSignalBlocker block(slider_);
        slider_->setRange(0, maxMs);
        slider_->setValue(enabled ? qRound(layer->frameSeconds() * 1000.0) : 0);
    }
    label_->setText(formatSeconds(enabled ? layer->frameSeconds() : 0.0)
                    + QStringLiteral(" / ") + formatSeconds(duration));
    playButton_->setEnabled(enabled);
    backButton_->setEnabled(enabled);
    forwardButton_->setEnabled(enabled);
    slider_->setEnabled(enabled);
    playButton_->setToolTip(enabled
        ? UiText::text(QStringLiteral("cover.play_pause_space"))
        : UiText::text(QStringLiteral("cover.select_a_chart_frame_layer")));
}

void CoverFramePickerPanel::setPositionSeconds(double seconds)
{
    if (!slider_->isEnabled()) {
        return;
    }
    {
        const QSignalBlocker block(slider_);   // reflect playback/scrub without re-seeking
        slider_->setValue(qRound(seconds * 1000.0));
    }
    const double duration = studio_ != nullptr ? studio_->contentDurationSeconds() : 0.0;
    label_->setText(formatSeconds(seconds) + QStringLiteral(" / ") + formatSeconds(duration));
}

void CoverFramePickerPanel::setPlaying(bool playing)
{
    playButton_->setIcon(playing ? pauseIcon_ : playIcon_);
}

bool CoverFramePickerPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == slider_ && studio_ != nullptr) {
        if ((event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
            && slider_->isEnabled()) {
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

}  // namespace miacode::cover_export
