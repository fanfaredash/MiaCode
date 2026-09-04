#pragma once

#include "UiText.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QObject>
#include <QPointer>

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
