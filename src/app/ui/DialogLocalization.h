#pragma once

#include "UiText.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QTimer>
#include <QVariant>
#include <QWindow>

class QWidget;

namespace UiDialogs {

inline bool shouldUseDetachedParent(QWidget* parent)
{
    return parent != nullptr && parent->property("miacode.dialog_parentless").toBool();
}

inline QWidget* effectiveParentWidget(QWidget* parent)
{
    return shouldUseDetachedParent(parent) ? nullptr : parent;
}

inline void applyDetachedParentBehavior(QDialog* dialog, QWidget* parent)
{
    if (dialog == nullptr || !shouldUseDetachedParent(parent)) {
        return;
    }
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setWindowFlag(Qt::Tool, false);
    dialog->setAttribute(Qt::WA_ShowWithoutActivating, false);
}

inline QRect dialogAnchorGeometry(QWidget* parent, QDialog* dialog = nullptr)
{
    QWidget* anchorWidget = parent != nullptr ? parent->window() : nullptr;
    QRect anchorRect;
    if (anchorWidget != nullptr) {
        const QVariant quickShellRootGeometry =
            anchorWidget->property("miacode.quick_root_window_frame_geometry");
        if (quickShellRootGeometry.isValid() && quickShellRootGeometry.canConvert<QRect>()) {
            const QRect quickRect = quickShellRootGeometry.toRect();
            if (quickRect.isValid()) {
                anchorRect = quickRect;
            }
        }
    }
    if (anchorWidget != nullptr
        && !anchorRect.isValid()
        && anchorWidget->isVisible()
        && !anchorWidget->windowState().testFlag(Qt::WindowMinimized)
        && !anchorWidget->windowFlags().testFlag(Qt::Tool)) {
        anchorRect = anchorWidget->frameGeometry();
    }

    const auto tryFindTopLevelWindow = [dialog](bool requireActive, bool allowToolWindows) -> QWindow* {
        const auto windows = QGuiApplication::topLevelWindows();
        for (QWindow* window : windows) {
            if (window == nullptr
                || (dialog != nullptr && window == dialog->windowHandle())
                || !window->isVisible()
                || window->visibility() == QWindow::Hidden
                || window->visibility() == QWindow::Minimized) {
                continue;
            }
            const Qt::WindowFlags flags = window->flags();
            if (!allowToolWindows && (flags.testFlag(Qt::Tool) || flags.testFlag(Qt::Popup))) {
                continue;
            }
            if (requireActive && !window->isActive()) {
                continue;
            }
            return window;
        }
        return nullptr;
    };

    if (!anchorRect.isValid()) {
        if (QWindow* topLevelWindow = tryFindTopLevelWindow(true, false); topLevelWindow != nullptr) {
            anchorRect = topLevelWindow->frameGeometry();
        }
    }
    if (!anchorRect.isValid()) {
        if (QWindow* topLevelWindow = tryFindTopLevelWindow(false, false); topLevelWindow != nullptr) {
            anchorRect = topLevelWindow->frameGeometry();
        }
    }
    if (!anchorRect.isValid()) {
        if (QWidget* activeWidget = QApplication::activeWindow();
            activeWidget != nullptr
            && activeWidget != dialog
            && activeWidget->isVisible()
            && !activeWidget->windowState().testFlag(Qt::WindowMinimized)
            && !activeWidget->windowFlags().testFlag(Qt::Tool)) {
            anchorRect = activeWidget->frameGeometry();
        }
    }
    if (!anchorRect.isValid()) {
        if (QWindow* topLevelWindow = tryFindTopLevelWindow(false, true); topLevelWindow != nullptr) {
            anchorRect = topLevelWindow->frameGeometry();
        }
    }
    if (!anchorRect.isValid()) {
        if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
            anchorRect = screen->availableGeometry();
        }
    }
    return anchorRect;
}

inline void centerDialogOnAnchor(QDialog* dialog, QWidget* parent)
{
    if (dialog == nullptr) {
        return;
    }
    dialog->adjustSize();
    const QRect anchorRect = dialogAnchorGeometry(parent, dialog);
    if (!anchorRect.isValid()) {
        return;
    }
    QPoint targetTopLeft(
        anchorRect.center().x() - dialog->width() / 2,
        anchorRect.center().y() - dialog->height() / 2
    );
    QScreen* targetScreen = QGuiApplication::screenAt(anchorRect.center());
    if (targetScreen == nullptr && parent != nullptr && parent->windowHandle() != nullptr) {
        targetScreen = parent->windowHandle()->screen();
    }
    if (targetScreen != nullptr) {
        const QRect avail = targetScreen->availableGeometry();
        targetTopLeft.setX(qBound(avail.left(), targetTopLeft.x(), avail.right() - dialog->width() + 1));
        targetTopLeft.setY(qBound(avail.top(), targetTopLeft.y(), avail.bottom() - dialog->height() + 1));
    }
    dialog->move(targetTopLeft);
}

inline void prepareDialogWindow(QDialog* dialog, QWidget* parent, bool activate = true)
{
    if (dialog == nullptr) {
        return;
    }
    applyDetachedParentBehavior(dialog, parent);
    centerDialogOnAnchor(dialog, parent);
    if (!activate) {
        return;
    }
    QPointer<QDialog> dialogGuard(dialog);
    QPointer<QWidget> parentGuard(parent);
    QTimer::singleShot(0, dialog, [dialogGuard, parentGuard]() {
        if (dialogGuard.isNull()) {
            return;
        }
        dialogGuard->adjustSize();
        centerDialogOnAnchor(dialogGuard, parentGuard);
        dialogGuard->raise();
        dialogGuard->activateWindow();
        if (QWidget* focusWidget = dialogGuard->focusWidget(); focusWidget != nullptr) {
            focusWidget->setFocus(Qt::ActiveWindowFocusReason);
        } else {
            dialogGuard->setFocus(Qt::ActiveWindowFocusReason);
        }
    });
}

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
    QMessageBox dialog(icon, title, message, QMessageBox::NoButton, effectiveParentWidget(parent));
    dialog.setStandardButtons(buttons);
    if (defaultButton != QMessageBox::NoButton) {
        dialog.setDefaultButton(defaultButton);
    }
    prepareDialogWindow(&dialog, parent);
    return execMessageBox(&dialog);
}

}  // namespace UiDialogs
