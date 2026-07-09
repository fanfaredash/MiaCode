#include "UiComponents.h"

#include "EditableValueLabel.h"
#include "UiTheme.h"
#include "mainwindow/MainWindowShared.h"

#include <QAbstractScrollArea>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

namespace miacode::ui {

QFrame* createCard(const QString& titleText, QWidget* parent, QVBoxLayout** bodyLayoutOut)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("LatencyCard"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto* title = new QLabel(titleText, card);
    title->setProperty("role", "cardTitle");
    title->setFont(miacode::mainwindow::shared::uiAccentFont(13, QFont::DemiBold));
    layout->addWidget(title);

    if (bodyLayoutOut != nullptr) {
        *bodyLayoutOut = layout;
    }
    return card;
}

QLabel* createFormLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "cardHint");
    label->setFont(miacode::mainwindow::shared::uiOutputFont());
    return label;
}

QWidget* createSliderValueRow(QSlider* slider,
                              EditableValueLabel** valueOut,
                              const QString& suffix,
                              QWidget* parent)
{
    if (slider != nullptr) {
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        slider->ensurePolished();
        slider->setFixedHeight(qMax(slider->sizeHint().height(), 20) + 2);
    }

    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto* value = new EditableValueLabel(QStringLiteral("0") + suffix, row);
    value->setMinimumWidth(46);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (slider != nullptr) {
        value->bindSlider(slider);
        layout->addWidget(slider, 1);
        QObject::connect(slider, &QSlider::valueChanged, value, [value, suffix](int v) {
            value->setText(QString::number(v) + suffix);
        });
    }
    layout->addWidget(value, 0);

    if (valueOut != nullptr) {
        *valueOut = value;
    }
    return row;
}

QPushButton* createDialogPushButton(const QString& text, QWidget* parent, bool primary)
{
    auto* button = new QPushButton(text, parent);
    button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(primary));
    return button;
}

void applyCompactDialogButton(QPushButton* button, bool primary)
{
    if (button == nullptr) {
        return;
    }
    button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(primary)
        + QStringLiteral("QPushButton { min-width: 0px; min-height: 30px; padding: 0 12px; }"));
}

void applySmallDialogButton(QPushButton* button)
{
    if (button == nullptr) {
        return;
    }
    button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet()
        + QStringLiteral("QPushButton { min-width: 28px; max-width: 30px;"
                         " min-height: 26px; max-height: 28px; padding: 0; border-radius: 6px; }"));
    button->setFixedSize(30, 28);
}

void applyScrollBarStyle(QAbstractScrollArea* area)
{
    if (area == nullptr) {
        return;
    }
    if (QScrollBar* vertical = area->verticalScrollBar(); vertical != nullptr) {
        vertical->setStyleSheet(UiTheme::scrollBarStyleSheet());
    }
    if (QScrollBar* horizontal = area->horizontalScrollBar(); horizontal != nullptr) {
        horizontal->setStyleSheet(UiTheme::scrollBarStyleSheet());
    }
}

}  // namespace miacode::ui
