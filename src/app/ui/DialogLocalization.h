#pragma once

#include "UiText.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QEventLoop>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QList>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWindow>

class QWidget;

namespace UiDialogs {

enum class PreviewShortcutPolicy {
    None = 0,
    BlockMainWindowPreview,
    LocalPlaybackControls,
};

inline constexpr char kPreviewShortcutPolicyProperty[] = "miacode.preview_shortcut_policy";
inline constexpr char kPreviewShortcutGuardInstalledProperty[] = "miacode.preview_shortcut_guard_installed";

inline bool isPreviewPlaybackShortcutEvent(const QKeyEvent* event)
{
    if (event == nullptr) {
        return false;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    return modifiers == (Qt::ControlModifier | Qt::ShiftModifier)
        && (event->key() == Qt::Key_C || event->key() == Qt::Key_X);
}

inline bool isPreviewShortcutEvent(const QKeyEvent* event)
{
    if (event == nullptr) {
        return false;
    }
    if (isPreviewPlaybackShortcutEvent(event)) {
        return true;
    }
    return event->modifiers() == Qt::NoModifier
        && (event->key() == Qt::Key_Space
            || event->key() == Qt::Key_Left
            || event->key() == Qt::Key_Right);
}

inline PreviewShortcutPolicy previewShortcutPolicy(const QWidget* widget)
{
    if (widget == nullptr) {
        return PreviewShortcutPolicy::None;
    }
    const int value = widget->property(kPreviewShortcutPolicyProperty).toInt();
    switch (value) {
    case static_cast<int>(PreviewShortcutPolicy::BlockMainWindowPreview):
        return PreviewShortcutPolicy::BlockMainWindowPreview;
    case static_cast<int>(PreviewShortcutPolicy::LocalPlaybackControls):
        return PreviewShortcutPolicy::LocalPlaybackControls;
    case static_cast<int>(PreviewShortcutPolicy::None):
    default:
        return PreviewShortcutPolicy::None;
    }
}

inline bool isProtectedPreviewDialogVisible(const QWidget* widget)
{
    const auto* dialog = qobject_cast<const QDialog*>(widget);
    return dialog != nullptr
        && previewShortcutPolicy(dialog) != PreviewShortcutPolicy::None
        && dialog->isVisible()
        && !dialog->windowState().testFlag(Qt::WindowMinimized);
}

inline bool hasVisibleProtectedPreviewDialog()
{
    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (QWidget* widget : topLevelWidgets) {
        if (isProtectedPreviewDialogVisible(widget)) {
            return true;
        }
    }
    return false;
}

inline bool isVisibleBlockingModalDialog(const QWidget* widget)
{
    const auto* dialog = qobject_cast<const QDialog*>(widget);
    return dialog != nullptr
        && dialog->isModal()
        && dialog->isVisible()
        && !dialog->windowState().testFlag(Qt::WindowMinimized);
}

inline QList<QPointer<QDialog>> visibleBlockingModalDialogs()
{
    QList<QPointer<QDialog>> dialogs;
    const auto appendDialog = [&dialogs](QDialog* dialog) {
        if (dialog == nullptr || !isVisibleBlockingModalDialog(dialog) || dialogs.contains(dialog)) {
            return;
        }
        dialogs.append(dialog);
    };

    appendDialog(qobject_cast<QDialog*>(QApplication::activeModalWidget()));
    const auto topLevelWidgets = QApplication::topLevelWidgets();
    for (auto it = topLevelWidgets.crbegin(); it != topLevelWidgets.crend(); ++it) {
        appendDialog(qobject_cast<QDialog*>(*it));
    }
    return dialogs;
}

inline bool hasVisibleBlockingModalDialog()
{
    return !visibleBlockingModalDialogs().isEmpty();
}

inline int closeVisibleBlockingModalDialogs()
{
    int closedCount = 0;
    constexpr int kMaxClosePasses = 8;
    for (int pass = 0; pass < kMaxClosePasses; ++pass) {
        const QList<QPointer<QDialog>> dialogs = visibleBlockingModalDialogs();
        if (dialogs.isEmpty()) {
            break;
        }

        bool closedAnyThisPass = false;
        for (const QPointer<QDialog>& dialog : dialogs) {
            if (dialog.isNull() || !isVisibleBlockingModalDialog(dialog)) {
                continue;
            }
            dialog->close();
            ++closedCount;
            closedAnyThisPass = true;
        }

        if (!closedAnyThisPass) {
            break;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return closedCount;
}

inline bool dialogOwnsPreviewShortcutScope(const QDialog* dialog)
{
    if (dialog == nullptr || !dialog->isVisible()) {
        return false;
    }
    QWidget* focusWidget = QApplication::focusWidget();
    return dialog->isActiveWindow() || (focusWidget != nullptr && dialog->isAncestorOf(focusWidget));
}

class PreviewShortcutOverrideGuard final : public QObject
{
public:
    explicit PreviewShortcutOverrideGuard(QDialog* dialog)
        : QObject(dialog)
        , dialog_(dialog)
    {
        if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
            app->installEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);
        if (dialog_.isNull()
            || event == nullptr
            || event->type() != QEvent::ShortcutOverride
            || !isProtectedPreviewDialogVisible(dialog_)) {
            return QObject::eventFilter(watched, event);
        }
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (!isPreviewPlaybackShortcutEvent(keyEvent)) {
            return QObject::eventFilter(watched, event);
        }
        event->accept();
        return true;
    }

private:
    QPointer<QDialog> dialog_;
};

inline void configureDialogPreviewShortcuts(
    QDialog* dialog,
    PreviewShortcutPolicy policy = PreviewShortcutPolicy::BlockMainWindowPreview
)
{
    if (dialog == nullptr) {
        return;
    }
    const PreviewShortcutPolicy currentPolicy = previewShortcutPolicy(dialog);
    const PreviewShortcutPolicy resolvedPolicy =
        currentPolicy == PreviewShortcutPolicy::LocalPlaybackControls
            && policy == PreviewShortcutPolicy::BlockMainWindowPreview
        ? currentPolicy
        : policy;
    dialog->setProperty(kPreviewShortcutPolicyProperty, static_cast<int>(resolvedPolicy));
    if (resolvedPolicy == PreviewShortcutPolicy::None) {
        return;
    }
    if (dialog->property(kPreviewShortcutGuardInstalledProperty).toBool()) {
        return;
    }
    dialog->setProperty(kPreviewShortcutGuardInstalledProperty, true);
    new PreviewShortcutOverrideGuard(dialog);
}

inline bool shouldUseDetachedParent(QWidget* parent)
{
    return parent != nullptr && parent->property("miacode.dialog_parentless").toBool();
}

class DialogStackingGuard;

inline QPointer<QWindow>& applicationDialogTransientParentStorage()
{
    static QPointer<QWindow> window;
    return window;
}

inline QWindow* applicationDialogTransientParent()
{
    return applicationDialogTransientParentStorage().data();
}

inline void bindDialogToApplicationTransientParent(QDialog* dialog)
{
    QWindow* transientParent = applicationDialogTransientParent();
    if (dialog == nullptr || transientParent == nullptr) {
        return;
    }
    dialog->winId();
    QWindow* dialogWindow = dialog->windowHandle();
    if (dialogWindow == nullptr || dialogWindow == transientParent) {
        return;
    }
    QWindow* currentParent = dialogWindow->transientParent();
    // Preserve nested-dialog ownership whenever the existing native parent is
    // visible. Only detached dialogs (or dialogs owned by the hidden QWidget
    // backend) need to be rebound to the visible QuickShell root.
    if (currentParent != nullptr && currentParent->isVisible()) {
        return;
    }
    if (currentParent != transientParent) {
        dialogWindow->setTransientParent(transientParent);
    }
}

class DialogStackingGuard final : public QObject
{
public:
    explicit DialogStackingGuard(QApplication* app)
        : QObject(app)
    {
        app->installEventFilter(this);
        QObject::connect(app, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive) {
                    restoreBlockingModalDialog();
                }
            });
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        QWindow* transientParent = applicationDialogTransientParent();
        if (event == nullptr || transientParent == nullptr) {
            return QObject::eventFilter(watched, event);
        }
        if (auto* dialog = qobject_cast<QDialog*>(watched);
            dialog != nullptr && dialog->isWindow() && event->type() == QEvent::Show) {
            QPointer<QDialog> dialogGuard(dialog);
            QTimer::singleShot(0, dialog, [dialogGuard]() {
                if (dialogGuard.isNull() || !dialogGuard->isVisible()) {
                    return;
                }
                bindDialogToApplicationTransientParent(dialogGuard);
                if (!isVisibleBlockingModalDialog(dialogGuard)) {
                    return;
                }
                dialogGuard->raise();
                dialogGuard->activateWindow();
            });
        } else if (watched == transientParent && event->type() == QEvent::WindowActivate) {
            restoreBlockingModalDialog();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void restoreBlockingModalDialog()
    {
        QTimer::singleShot(0, this, []() {
            const QList<QPointer<QDialog>> dialogs = visibleBlockingModalDialogs();
            if (dialogs.isEmpty() || dialogs.constFirst().isNull()) {
                return;
            }
            QDialog* dialog = dialogs.constFirst().data();
            bindDialogToApplicationTransientParent(dialog);
            dialog->raise();
            dialog->activateWindow();
        });
    }
};

inline QPointer<DialogStackingGuard>& applicationDialogStackingGuardStorage();

inline DialogStackingGuard* installApplicationDialogStackingGuard()
{
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (app == nullptr) {
        return nullptr;
    }
    QPointer<DialogStackingGuard>& guard = applicationDialogStackingGuardStorage();
    if (!guard.isNull()) {
        return guard.data();
    }
    guard = new DialogStackingGuard(app);
    return guard.data();
}

inline QPointer<DialogStackingGuard>& applicationDialogStackingGuardStorage()
{
    static QPointer<DialogStackingGuard> guard;
    return guard;
}

inline void setApplicationDialogTransientParent(QWindow* window)
{
    applicationDialogTransientParentStorage() = window;
    installApplicationDialogStackingGuard();
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

inline QString text(const char* key, const char* fallback)
{
    const QString translated = UiText::text(QString::fromLatin1(key));
    return translated.isEmpty() ? QString::fromLatin1(fallback) : translated;
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
        {QMessageBox::Reset, "action.reset", "Reset"},
    };

    for (const auto& entry : localizedButtons) {
        if (QAbstractButton* button = dialog->button(entry.button); button != nullptr) {
            button->setText(text(entry.key, entry.fallback));
        }
    }
}

}  // namespace UiDialogs
