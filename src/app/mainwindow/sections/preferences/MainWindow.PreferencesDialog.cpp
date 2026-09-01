#include "MainWindow.PreferencesSection.h"

#include "ShortcutRegistry.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QAction>
#include <QKeySequence>

namespace {

void applyConfiguredShortcut(
    QAction* action,
    const QString& id,
    const QKeySequence& fallback,
    Qt::ShortcutContext context = Qt::WindowShortcut)
{
    ShortcutRegistry::instance().applyShortcut(action, id, fallback);
    if (action != nullptr) {
        action->setShortcutContext(context);
    }
}

}  // namespace

MainWindow::PreferencesSection::PreferencesSection(
    MainWindow& owner,
    MainWindow::MainWindowUiRefs& ui,
    MainWindow::MainWindowState& state)
    : owner_(owner)
    , ui_(ui)
    , state_(state)
{}

void MainWindow::PreferencesSection::showWelcomeDialog()
{
}

void MainWindow::PreferencesSection::applyConfiguredShortcuts()
{
    applyConfiguredShortcut(
        owner_.stopOrPlayPreviewShortcutAction_,
        QStringLiteral("preview.stop_or_play"),
        QKeySequence(QStringLiteral("Ctrl+Shift+C")),
        Qt::ApplicationShortcut);
    applyConfiguredShortcut(
        owner_.playPausePreviewShortcutAction_,
        QStringLiteral("preview.play_pause_global"),
        QKeySequence(QStringLiteral("Ctrl+Shift+X")),
        Qt::ApplicationShortcut);
    applyConfiguredShortcut(
        owner_.previewSlowerAction_,
        QStringLiteral("preview.speed_down"),
        QKeySequence(QStringLiteral("Ctrl+O")));
    applyConfiguredShortcut(
        owner_.previewFasterAction_,
        QStringLiteral("preview.speed_up"),
        QKeySequence(QStringLiteral("Ctrl+P")));
    if (owner_.timelineQuickStateBridge_ != nullptr) {
        owner_.timelineQuickStateBridge_->setZoomWheelShortcuts(
            ShortcutRegistry::instance().shortcutTexts(
                QStringLiteral("timeline.zoom_in"),
                {QStringLiteral("Ctrl+WheelUp")}),
            ShortcutRegistry::instance().shortcutTexts(
                QStringLiteral("timeline.zoom_out"),
                {QStringLiteral("Ctrl+WheelDown")}));
    }
    applyConfiguredShortcut(
        owner_.fontDecreaseAction_,
        QStringLiteral("editor.font_decrease"),
        QKeySequence(QStringLiteral("Ctrl+Alt+-")),
        Qt::WindowShortcut);
    applyConfiguredShortcut(
        owner_.fontIncreaseAction_,
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Alt+=")),
        Qt::WindowShortcut);
}

void MainWindow::onPreferences()
{
    emit preferencesRequested();
}

void MainWindow::applyConfiguredShortcuts()
{
    preferencesSection_->applyConfiguredShortcuts();
}

void MainWindow::showWelcomeDialog()
{
    preferencesSection_->showWelcomeDialog();
}
