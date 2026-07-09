#pragma once

#include <QString>

class QLabel;
class QAbstractScrollArea;
class QFrame;
class QPushButton;
class QSlider;
class QVBoxLayout;
class QWidget;

namespace miacode::ui {

class EditableValueLabel;

QFrame* createCard(const QString& titleText, QWidget* parent, QVBoxLayout** bodyLayoutOut = nullptr);
QLabel* createFormLabel(const QString& text, QWidget* parent);
QWidget* createSliderValueRow(QSlider* slider,
                              EditableValueLabel** valueOut,
                              const QString& suffix,
                              QWidget* parent);
QPushButton* createDialogPushButton(const QString& text, QWidget* parent, bool primary = false);
void applyCompactDialogButton(QPushButton* button, bool primary = false);
void applySmallDialogButton(QPushButton* button);
void applyScrollBarStyle(QAbstractScrollArea* area);

}  // namespace miacode::ui
