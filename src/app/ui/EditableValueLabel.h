#pragma once

#include <QLabel>

class QSlider;
class QLineEdit;

namespace miacode::ui {

// A value-display QLabel (e.g. "50%") that turns into an inline editor when
// clicked, so the user can type a value instead of dragging the slider. The
// editor is a transient child that occupies exactly the label's own rectangle,
// so the surrounding layout is never disturbed — in its resting state this
// widget is indistinguishable from the QLabel it replaces (same type, size
// hint, alignment and text).
//
// Editing commits through the bound QSlider's setValue(), which re-fires every
// existing valueChanged connection on that slider (settings setter, label
// refresh, runtime apply). A typed edit therefore behaves identically to
// dragging the handle, with no apply logic duplicated at the call sites — they
// only swap `new QLabel(...)` for `new EditableValueLabel(...)` and add one
// `bindSlider(...)` call.
class EditableValueLabel : public QLabel
{
    Q_OBJECT

public:
    explicit EditableValueLabel(QWidget* parent = nullptr);
    explicit EditableValueLabel(const QString& text, QWidget* parent = nullptr);

    // Bind the label to the slider it mirrors. The inline editor edits the
    // slider's raw integer value (the bare number shown before any suffix),
    // constrained to [minimum, maximum] and snapped to the slider's singleStep
    // grid on commit. Safe to call again to re-bind; pass nullptr to detach.
    void bindSlider(QSlider* slider);

    // Open the inline editor programmatically (a left click does the same).
    void beginEdit();

signals:
    // Emitted after a typed value has been applied to the bound slider.
    void valueEdited(int value);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void commitEdit();
    void cancelEdit();
    void applyEditAffordance();

    QSlider* slider_ = nullptr;
    QLineEdit* editor_ = nullptr;
};

}  // namespace miacode::ui
