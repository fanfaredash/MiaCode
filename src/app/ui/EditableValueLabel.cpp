#include "EditableValueLabel.h"

#include "UiText.h"
#include "UiTheme.h"

#include <QApplication>
#include <QDoubleValidator>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QSlider>

#include <algorithm>
#include <cmath>

namespace miacode::ui {

EditableValueLabel::EditableValueLabel(QWidget* parent)
    : QLabel(parent)
{
}

EditableValueLabel::EditableValueLabel(const QString& text, QWidget* parent)
    : QLabel(text, parent)
{
}

void EditableValueLabel::bindSlider(QSlider* slider)
{
    slider_ = slider;
    applyEditAffordance();
}

void EditableValueLabel::applyEditAffordance()
{
    if (slider_ != nullptr) {
        // Resting-state appearance is unchanged; only the hover cursor + tooltip
        // hint that the number is clickable. Neither affects layout geometry.
        setCursor(Qt::PointingHandCursor);
        setToolTip(UiText::localized(QStringLiteral("Click to type a value"), QStringLiteral("点击可输入数值")));
    } else {
        unsetCursor();
        setToolTip(QString());
    }
}

void EditableValueLabel::beginEdit()
{
    if (slider_ == nullptr || editor_ != nullptr) {
        return;
    }
    if (!isEnabled() || !slider_->isEnabled()) {
        return;
    }

    editor_ = new QLineEdit(this);
    editor_->setText(QString::number(slider_->value()));
    editor_->setAlignment(alignment());
    // Accept a decimal point while typing; the value is rounded to an integer on
    // commit. C locale so '.' is the separator (matches QString::toDouble).
    auto* validator = new QDoubleValidator(slider_->minimum(), slider_->maximum(), 6, editor_);
    validator->setNotation(QDoubleValidator::StandardNotation);
    validator->setLocale(QLocale::c());
    editor_->setValidator(validator);
    // Reuse the themed line-edit look, then neutralise the form-field min-height
    // and wide padding so the editor sits snugly inside the small value cell.
    editor_->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet()
        + QStringLiteral("QLineEdit { min-height: 0; padding: 0 4px; border-radius: 4px; }"));
    editor_->setGeometry(rect());
    // App-level filter: an object-level filter only sees the editor's own
    // events, so a click on a sibling widget (slider, group box, empty space)
    // — which often doesn't even take focus — would never reach us and the
    // value committed only on Enter. Installed only while editing.
    qApp->installEventFilter(this);
    editor_->show();
    editor_->setFocus(Qt::MouseFocusReason);
    editor_->selectAll();
}

void EditableValueLabel::commitEdit()
{
    if (editor_ == nullptr) {
        return;
    }
    // Detach first so the slider cascade below (and any focus churn it causes)
    // can't re-enter this commit.
    QLineEdit* editor = editor_;
    editor_ = nullptr;
    qApp->removeEventFilter(this);

    bool ok = false;
    const double typed = editor->text().toDouble(&ok);
    editor->deleteLater();

    if (!ok || slider_ == nullptr) {
        return;
    }

    // 四舍五入到整数, then clamp to the slider's range. setValue accepts any
    // in-range integer regardless of singleStep; the one coarse-step slider
    // (layout-square-scale) re-normalizes in its own valueChanged handler.
    const int value = std::clamp(
        static_cast<int>(std::lround(typed)), slider_->minimum(), slider_->maximum());

    // Route through setValue so every existing valueChanged connection fires —
    // a typed edit is indistinguishable from a drag downstream.
    slider_->setValue(value);
    emit valueEdited(slider_->value());
}

void EditableValueLabel::cancelEdit()
{
    if (editor_ == nullptr) {
        return;
    }
    QLineEdit* editor = editor_;
    editor_ = nullptr;
    qApp->removeEventFilter(this);
    editor->deleteLater();
}

void EditableValueLabel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && slider_ != nullptr) {
        beginEdit();
        event->accept();
        return;
    }
    QLabel::mousePressEvent(event);
}

void EditableValueLabel::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    if (editor_ != nullptr) {
        editor_->setGeometry(rect());
    }
}

bool EditableValueLabel::eventFilter(QObject* watched, QEvent* event)
{
    if (editor_ != nullptr) {
        if (watched == editor_) {
            if (event->type() == QEvent::KeyPress) {
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                const int key = keyEvent->key();
                if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                    commitEdit();
                    return true;  // don't let Enter trigger the dialog default button
                }
                if (key == Qt::Key_Escape) {
                    cancelEdit();
                    return true;
                }
            } else if (event->type() == QEvent::FocusOut) {
                // Tab-away / window deactivation. Mouse clicks on other widgets
                // are handled below — non-focusable targets never fire FocusOut.
                commitEdit();
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            // A press anywhere outside the editor commits, then proceeds to its
            // real target (we never consume it).
            commitEdit();
        }
    }
    return QLabel::eventFilter(watched, event);
}

}  // namespace miacode::ui
