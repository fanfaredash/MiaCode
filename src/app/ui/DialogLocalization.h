#pragma once

#include "UiText.h"

#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>

class QWidget;

namespace UiDialogs {

inline QString text(const char* key, const char* fallback)
{
    const QString translated = UiText::text(QString::fromLatin1(key));
    return translated.isEmpty() ? QString::fromLatin1(fallback) : translated;
}

inline void localizeButtonBox(QDialogButtonBox* buttonBox)
{
    if (buttonBox == nullptr) {
        return;
    }

    const struct {
        QDialogButtonBox::StandardButton button;
        const char* key;
        const char* fallback;
    } localizedButtons[] = {
        {QDialogButtonBox::Ok, "action.ok", "OK"},
        {QDialogButtonBox::Cancel, "action.cancel", "Cancel"},
        {QDialogButtonBox::Close, "action.close", "Close"},
        {QDialogButtonBox::Yes, "action.yes", "Yes"},
        {QDialogButtonBox::No, "action.no", "No"},
        {QDialogButtonBox::Save, "action.save", "Save"},
        {QDialogButtonBox::Discard, "action.discard", "Discard"},
    };

    for (const auto& entry : localizedButtons) {
        if (QPushButton* button = buttonBox->button(entry.button); button != nullptr) {
            button->setText(text(entry.key, entry.fallback));
        }
    }
}

inline void localizeMessageBox(QMessageBox* dialog)
{
    if (dialog == nullptr) {
        return;
    }

    const struct {
        QMessageBox::StandardButton button;
        const char* key;
        const char* fallback;
    } localizedButtons[] = {
        {QMessageBox::Ok, "action.ok", "OK"},
        {QMessageBox::Cancel, "action.cancel", "Cancel"},
        {QMessageBox::Close, "action.close", "Close"},
        {QMessageBox::Yes, "action.yes", "Yes"},
        {QMessageBox::No, "action.no", "No"},
        {QMessageBox::Save, "action.save", "Save"},
        {QMessageBox::Discard, "action.discard", "Discard"},
    };

    for (const auto& entry : localizedButtons) {
        if (QAbstractButton* button = dialog->button(entry.button); button != nullptr) {
            button->setText(text(entry.key, entry.fallback));
        }
    }
}

inline QMessageBox::StandardButton execMessageBox(QMessageBox* dialog)
{
    if (dialog == nullptr) {
        return QMessageBox::NoButton;
    }

    localizeMessageBox(dialog);
    dialog->exec();
    return dialog->standardButton(dialog->clickedButton());
}

inline QMessageBox::StandardButton showMessageBox(
    QMessageBox::Icon icon,
    QWidget* parent,
    const QString& title,
    const QString& message,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton
)
{
    QMessageBox dialog(icon, title, message, QMessageBox::NoButton, parent);
    dialog.setStandardButtons(buttons);
    if (defaultButton != QMessageBox::NoButton) {
        dialog.setDefaultButton(defaultButton);
    }
    return execMessageBox(&dialog);
}

}  // namespace UiDialogs
