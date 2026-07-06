#include "MainWindow.WindowSection.h"
#include "../../MainWindowShared.h"

#include "PlainCodeEditor.h"
#include "QtPreviewSfxRuntime.h"
#include "DialogLocalization.h"
#include "ShortcutRegistry.h"
#include "UiText.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QQuickWindow>
#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

constexpr qint64 kInvalidStarPreviewAboutClickWindowMs = 900;
constexpr int kEditorFindBarMinWidth = 300;
constexpr int kEditorFindBarMaxWidth = 500;
constexpr int kEditorFindBarHorizontalMargin = 14;
constexpr int kEditorFindBarTopMargin = 10;
constexpr int kEditorFindBarOverlayGap = 8;
constexpr int kBottomTabsResizeHotzonePx = 8;

bool widgetMatchesOrDescendsFrom(QWidget* widget, QWidget* root)
{
    return widget != nullptr && root != nullptr && (widget == root || root->isAncestorOf(widget));
}

bool isUnmodifiedHorizontalArrowKey(const QKeyEvent* event)
{
    return event != nullptr
        && event->modifiers() == Qt::NoModifier
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right);
}

bool bottomTabsResizeHotzoneContains(QWidget* bottomTabs, QWidget* watchedWidget, const QPoint& localPos)
{
    if (bottomTabs == nullptr || watchedWidget == nullptr) {
        return false;
    }
    const QPoint bottomTabsPos =
        watchedWidget == bottomTabs ? localPos : watchedWidget->mapTo(bottomTabs, localPos);
    return bottomTabsPos.y() >= 0 && bottomTabsPos.y() <= kBottomTabsResizeHotzonePx;
}

// The pause-display hold key (default Alt, id preview.pause_display_hold,
// rebindable via 首选项 → 快捷键) flips 判定区 ⇄ PV while the preview is
// paused, for as long as the key is physically held. Hold semantics need raw
// press/release pairs — possibly of a bare modifier — so it cannot ride the
// QAction/QShortcut path; the event filter below matches against this combo.
struct PauseDisplayHoldKey {
    int key = Qt::Key_Alt;
    Qt::KeyboardModifiers pressModifiers = Qt::AltModifier;
};

PauseDisplayHoldKey pauseDisplayHoldKey()
{
    PauseDisplayHoldKey hold;
    const QKeySequence sequence = ShortcutRegistry::instance().sequence(
        QStringLiteral("preview.pause_display_hold"), QKeySequence(Qt::Key_Alt));
    if (sequence.isEmpty()) {
        return hold;
    }
    const QKeyCombination combination = sequence[0];
    hold.key = combination.key();
    switch (hold.key) {
    // A bare modifier press reports its own flag in modifiers(); requiring
    // exactly that flag keeps prefixed combos (e.g. Alt+F4) from arming the
    // hold — same gate the original Alt-only implementation used.
    case Qt::Key_Alt: hold.pressModifiers = Qt::AltModifier; break;
    case Qt::Key_Control: hold.pressModifiers = Qt::ControlModifier; break;
    case Qt::Key_Shift: hold.pressModifiers = Qt::ShiftModifier; break;
    case Qt::Key_Meta: hold.pressModifiers = Qt::MetaModifier; break;
    default: hold.pressModifiers = combination.keyboardModifiers(); break;
    }
    return hold;
}

void appendPreviewInteractionLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/interaction"),
        text
    );
}

}  // namespace

bool MainWindow::WindowSection::quickShellFocusBridgeActive() const
{
    return state_.quickShellUiFocusBridgeMode_;
}

QTextEdit* MainWindow::WindowSection::resolveRestorableTextEdit(QWidget* widget) const
{
    if (owner_.editorWidget_ != nullptr && widgetMatchesOrDescendsFrom(widget, owner_.editorWidget_)) {
        return qobject_cast<QTextEdit*>(owner_.editorWidget_);
    }
    if (owner_.metadataExtraEdit_ != nullptr && widgetMatchesOrDescendsFrom(widget, owner_.metadataExtraEdit_)) {
        return owner_.metadataExtraEdit_;
    }
    return nullptr;
}

bool MainWindow::WindowSection::isTextInputWidget(QWidget* widget) const
{
    if (widget == nullptr) {
        return false;
    }
    if (this->resolveRestorableTextEdit(widget) != nullptr) {
        return true;
    }

    for (QWidget* current = widget; current != nullptr; current = current->parentWidget()) {
        if (qobject_cast<QLineEdit*>(current) != nullptr
            || qobject_cast<QTextEdit*>(current) != nullptr
            || qobject_cast<QPlainTextEdit*>(current) != nullptr
            || qobject_cast<QAbstractSpinBox*>(current) != nullptr) {
            return true;
        }
        if (auto* combo = qobject_cast<QComboBox*>(current);
            combo != nullptr && combo->isEditable()) {
            return true;
        }
    }
    return false;
}

bool MainWindow::WindowSection::hasActiveTextInputFocus() const
{
    return this->isTextInputWidget(QApplication::focusWidget());
}

bool MainWindow::WindowSection::shouldRespectFocusedWidgetOnRestore(QWidget* widget, QTextEdit* target) const
{
    if (widget == nullptr || target == nullptr) {
        return false;
    }
    if (widget == target || target->isAncestorOf(widget)) {
        return false;
    }
    if (widget == &owner_ || widget == owner_.centralWidget()) {
        return false;
    }

    return qobject_cast<QLineEdit*>(widget) != nullptr
        || qobject_cast<QTextEdit*>(widget) != nullptr
        || qobject_cast<QAbstractButton*>(widget) != nullptr
        || qobject_cast<QAbstractSlider*>(widget) != nullptr
        || qobject_cast<QAbstractItemView*>(widget) != nullptr
        || qobject_cast<QComboBox*>(widget) != nullptr
        || qobject_cast<QSpinBox*>(widget) != nullptr
        || qobject_cast<QDateTimeEdit*>(widget) != nullptr
        || qobject_cast<QTabBar*>(widget) != nullptr;
}

QWindow* MainWindow::WindowSection::previewVisibleHostWindow() const
{
    return owner_.previewCanvas_ != nullptr ? owner_.previewCanvas_->visibleHostWindow() : nullptr;
}

void MainWindow::WindowSection::focusPreviewInteractionTarget(QObject* watched, Qt::FocusReason reason)
{
    QWidget* widget = qobject_cast<QWidget*>(watched);
    if (widget == nullptr || widget->focusPolicy() == Qt::NoFocus) {
        if (owner_.previewFullscreenActive_ && owner_.previewFullscreenHost_ != nullptr
            && owner_.previewFullscreenHost_->focusPolicy() != Qt::NoFocus) {
            widget = owner_.previewFullscreenHost_;
        } else if (owner_.previewFullscreenWindow_ != nullptr && owner_.previewFullscreenWindow_->focusPolicy() != Qt::NoFocus) {
            widget = owner_.previewFullscreenWindow_;
        } else if (owner_.previewCanvasContainer_ != nullptr && owner_.previewCanvasContainer_->focusPolicy() != Qt::NoFocus) {
            widget = owner_.previewCanvasContainer_;
        } else if (owner_.previewCanvasFrame_ != nullptr && owner_.previewCanvasFrame_->focusPolicy() != Qt::NoFocus) {
            widget = owner_.previewCanvasFrame_;
        } else if (owner_.previewPanel_ != nullptr && owner_.previewPanel_->focusPolicy() != Qt::NoFocus) {
            widget = owner_.previewPanel_;
        }
    }
    if (widget != nullptr) {
        widget->setFocus(reason);
    }
    if (owner_.previewCanvas_ != nullptr) {
        owner_.previewCanvas_->requestActivate();
    }
}

void MainWindow::WindowSection::handleApplicationFocusChanged(QWidget* old, QWidget* now)
{
    this->logFocusDebug(
        quickShellFocusBridgeActive()
            ? QStringLiteral("app_focus_changed_quick_shell")
            : QStringLiteral("app_focus_changed"),
        old,
        now);
    QTextEdit* newTextEdit = this->resolveRestorableTextEdit(now);
    if (newTextEdit != nullptr) {
        this->logFocusDebug(
            QStringLiteral("app_focus_changed_clear_for_new_text"),
            old,
            now,
            QStringLiteral("new_restorable=%1").arg(this->describeFocusWidget(newTextEdit))
        );
        this->clearFocusedTextEditState();
        return;
    }

    if (!quickShellFocusBridgeActive() && now != nullptr && (now == &owner_ || owner_.isAncestorOf(now))) {
        this->logFocusDebug(QStringLiteral("app_focus_changed_clear_for_owner_child"), old, now);
        this->clearFocusedTextEditState();
        return;
    }

    if (QTextEdit* oldTextEdit = this->resolveRestorableTextEdit(old); oldTextEdit != nullptr) {
        this->logFocusDebug(
            QStringLiteral("app_focus_changed_remember_old_text"),
            old,
            now,
            QStringLiteral("old_restorable=%1").arg(this->describeFocusWidget(oldTextEdit))
        );
        this->rememberFocusedTextEditState(oldTextEdit);
    }
}

void MainWindow::WindowSection::recoverPreviewBackendsAfterApplicationResume()
{
    if (!state_.previewBackendRecoveryPending_) {
        return;
    }
    if (state_.qtPreviewPlaying_ || state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        return;
    }

    state_.previewBackendRecoveryPending_ = false;
    state_.previewSfxRuntimePrepared_ = false;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->stopAll();
    }
    owner_.ensurePreviewSfxRuntimePrepared();

    owner_.shutdownPreviewStageMediaHost();
    if (state_.currentFilePath_.isEmpty()) {
        owner_.clearPreviewStageMediaRoute();
    } else {
        owner_.syncPreviewStageMediaRouteChartPath(
            state_.currentFilePath_,
            state_.lastTrackPath_,
            qMax(0.0, state_.qtPreviewPauseSecond_),
            state_.document_.videoPath  // Phase 4c — &video= override
        );
        owner_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_, "fullscreen_return");
    }
    owner_.applyPreviewStageMediaRouteVisualSettings();
}

void MainWindow::WindowSection::handleApplicationStateChanged(Qt::ApplicationState state)
{
    this->logFocusDebug(
        QStringLiteral("application_state_changed"),
        QApplication::focusWidget(),
        owner_.focusWidget(),
        QStringLiteral("state=%1").arg(static_cast<int>(state))
    );
    if (state == Qt::ApplicationActive) {
        this->restoreFocusedTextEditState();
        this->recoverPreviewBackendsAfterApplicationResume();
        return;
    }
    if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) {
        state_.previewBackendRecoveryPending_ = true;
    }
    if (state == Qt::ApplicationInactive || state == Qt::ApplicationHidden || state == Qt::ApplicationSuspended) {
        this->rememberFocusedTextEditState();
    }
}

void MainWindow::WindowSection::rememberFocusedTextEditState(QTextEdit* textEdit)
{
    pendingTextFocusWidget_ = textEdit;
    pendingTextCursorAnchor_ = -1;
    pendingTextCursorPosition_ = -1;
    if (pendingTextFocusWidget_.isNull()) {
        return;
    }

    const QTextCursor cursor = pendingTextFocusWidget_->textCursor();
    pendingTextCursorAnchor_ = cursor.anchor();
    pendingTextCursorPosition_ = cursor.position();
    this->logFocusDebug(
        QStringLiteral("remember_text_focus_state"),
        textEdit,
        QApplication::focusWidget(),
        QStringLiteral("saved_anchor=%1 saved_pos=%2")
            .arg(pendingTextCursorAnchor_)
            .arg(pendingTextCursorPosition_)
    );
}

void MainWindow::WindowSection::rememberFocusedTextEditState()
{
    this->rememberFocusedTextEditState(this->resolveRestorableTextEdit(QApplication::focusWidget()));
}

void MainWindow::WindowSection::clearFocusedTextEditState()
{
    if (!pendingTextFocusWidget_.isNull()) {
        this->logFocusDebug(QStringLiteral("clear_text_focus_state"), pendingTextFocusWidget_.data(), QApplication::focusWidget());
    }
    pendingTextFocusWidget_.clear();
    pendingTextCursorAnchor_ = -1;
    pendingTextCursorPosition_ = -1;
}

void MainWindow::WindowSection::restoreFocusedTextEditState()
{
    if (pendingTextFocusWidget_.isNull()) {
        this->logFocusDebug(QStringLiteral("restore_text_focus_state_skip_no_pending"), nullptr, QApplication::focusWidget());
        return;
    }

    QPointer<QTextEdit> target = pendingTextFocusWidget_;
    const int savedAnchor = pendingTextCursorAnchor_;
    const int savedPosition = pendingTextCursorPosition_;
    this->logFocusDebug(
        QStringLiteral("restore_text_focus_state_schedule"),
        target.data(),
        QApplication::focusWidget(),
        QStringLiteral("saved_anchor=%1 saved_pos=%2").arg(savedAnchor).arg(savedPosition)
    );
    pendingTextFocusWidget_.clear();
    pendingTextCursorAnchor_ = -1;
    pendingTextCursorPosition_ = -1;
    QTimer::singleShot(0, &owner_, [this, target, savedAnchor, savedPosition]() {
        this->restoreFocusedTextEditStateAttempt(target, savedAnchor, savedPosition, 0);
    });
}

void MainWindow::WindowSection::restoreFocusedTextEditStateAttempt(
    QPointer<QTextEdit> target,
    int savedAnchor,
    int savedPosition,
    int attempt)
{
    constexpr int kMaxRestoreAttempts = 6;
    constexpr int kRestoreRetryDelayMs = 30;

    const Qt::ApplicationState appState =
        qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr
            ? qobject_cast<QGuiApplication*>(QCoreApplication::instance())->applicationState()
            : Qt::ApplicationInactive;
    QWidget* topLevel = !target.isNull() ? target->window() : nullptr;
    const bool topLevelVisible = topLevel != nullptr && topLevel->isVisible();
    const bool topLevelActive = topLevel != nullptr && topLevel->isActiveWindow();

    if (target.isNull() || !target->isVisible() || appState != Qt::ApplicationActive) {
        this->logFocusDebug(
            QStringLiteral("restore_text_focus_state_abort_target_invalid"),
            target.data(),
            QApplication::focusWidget(),
            QStringLiteral("attempt=%1 target_null=%2 app_state=%3 target_visible=%4 top_level_visible=%5 top_level_active=%6")
                .arg(attempt)
                .arg(target.isNull() ? 1 : 0)
                .arg(static_cast<int>(appState))
                .arg((!target.isNull() && target->isVisible()) ? 1 : 0)
                .arg(topLevelVisible ? 1 : 0)
                .arg(topLevelActive ? 1 : 0)
        );
        return;
    }

    if ((!topLevelVisible || !topLevelActive) && attempt < kMaxRestoreAttempts) {
        this->logFocusDebug(
            QStringLiteral("restore_text_focus_state_retry_wait_top_level"),
            target.data(),
            QApplication::focusWidget(),
            QStringLiteral("attempt=%1 top_level_visible=%2 top_level_active=%3")
                .arg(attempt)
                .arg(topLevelVisible ? 1 : 0)
                .arg(topLevelActive ? 1 : 0)
        );
        QTimer::singleShot(kRestoreRetryDelayMs, &owner_, [this, target, savedAnchor, savedPosition, attempt]() {
            this->restoreFocusedTextEditStateAttempt(target, savedAnchor, savedPosition, attempt + 1);
        });
        return;
    }

    QWidget* focus = QApplication::focusWidget();
    if (this->shouldRespectFocusedWidgetOnRestore(focus, target.data())) {
        this->logFocusDebug(
            QStringLiteral("restore_text_focus_state_abort_respect_current_focus"),
            target.data(),
            focus,
            QStringLiteral("attempt=%1 current_focus=%2").arg(attempt).arg(this->describeFocusWidget(focus))
        );
        return;
    }

    target->setFocus(Qt::ActiveWindowFocusReason);
    if (QTextDocument* document = target->document();
        document != nullptr && savedAnchor >= 0 && savedPosition >= 0) {
        const int maxPosition = qMax(0, document->characterCount() - 1);
        QTextCursor cursor(document);
        cursor.setPosition(qBound(0, savedAnchor, maxPosition));
        cursor.setPosition(qBound(0, savedPosition, maxPosition), QTextCursor::KeepAnchor);
        target->setTextCursor(cursor);
    }

    QWidget* appliedFocus = QApplication::focusWidget();
    const bool focusLanded = appliedFocus == target.data() || (appliedFocus != nullptr && target->isAncestorOf(appliedFocus));
    if (!focusLanded && attempt < kMaxRestoreAttempts) {
        this->logFocusDebug(
            QStringLiteral("restore_text_focus_state_retry_focus_not_landed"),
            target.data(),
            appliedFocus,
            QStringLiteral("attempt=%1 applied_focus=%2").arg(attempt).arg(this->describeFocusWidget(appliedFocus))
        );
        QTimer::singleShot(kRestoreRetryDelayMs, &owner_, [this, target, savedAnchor, savedPosition, attempt]() {
            this->restoreFocusedTextEditStateAttempt(target, savedAnchor, savedPosition, attempt + 1);
        });
        return;
    }

    this->logFocusDebug(
        QStringLiteral("restore_text_focus_state_applied"),
        target.data(),
        appliedFocus,
        QStringLiteral("attempt=%1 saved_anchor=%2 saved_pos=%3")
            .arg(attempt)
            .arg(savedAnchor)
            .arg(savedPosition)
    );
}

bool MainWindow::WindowSection::eventFilter(QObject* watched, QEvent* event)
{
    auto* watchedWidget = qobject_cast<QWidget*>(watched);
    // Post-page-switch workspace-surface settle (armWorkspaceSurfaceSettleRelayout).
    // A switch that changes the preview aspect (export page) or the bottom-tabs
    // height drives the rehosted workspace surface to a new size ASYNCHRONOUSLY
    // from QML — after the synchronous page-build + refresh already ran. This
    // resize on the surface-filling workspace content is the reliable signal that
    // the final geometry has landed; re-run the layout finalize queued (after the
    // resize is fully processed) so the just-built page reflows + repaints instead
    // of staying composited at its stale, scrambled pre-resize arrangement. Observe
    // only — never consume the event.
    if (event != nullptr
        && event->type() == QEvent::Resize
        && watched == owner_.workspaceContentWidget_
        && owner_.workspaceSurfaceSettleRelayoutArmed_) {
        if (owner_.runtimeDebugOutputEnabled_) {
            const QSize size = watchedWidget != nullptr ? watchedWidget->size() : QSize();
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("layout/export_page"),
                QStringLiteral("action=workspace_surface_settle_relayout_queued size=%1x%2 armed=1 watched_class=%3 watched_name=%4")
                    .arg(size.width())
                    .arg(size.height())
                    .arg(watchedWidget != nullptr
                        ? QString::fromUtf8(watchedWidget->metaObject()->className())
                        : QStringLiteral("(null)"))
                    .arg(watchedWidget != nullptr && !watchedWidget->objectName().isEmpty()
                        ? watchedWidget->objectName()
                        : QStringLiteral("(empty)")));
        }
        QTimer::singleShot(0, &owner_, [this]() { owner_.refreshLayoutAfterPageSwitch(); });
    }
    // Top header validation/muri summary icons + counts route a left-button
    // press to the matching bottom panel tab so the user can jump straight
    // from the "?" / "!" badge into the issue list. The icons are only
    // visible (and thus event-deliverable) when their count > 0, so we
    // don't need an extra emptiness gate here.
    if (event != nullptr
        && event->type() == QEvent::MouseButtonRelease
        && watchedWidget != nullptr) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent != nullptr
            && mouseEvent->button() == Qt::LeftButton
            && watchedWidget->rect().contains(mouseEvent->pos())) {
            // The header summary icon/count labels are positional SLOTS — any
            // badge kind (error/warning/muri) can be rendered into any of these
            // six widgets depending on which kinds are present (see
            // updateEditorValidationSummary). Route by the badge kind actually
            // shown, stamped on the widget as "validationSummaryTab", NOT by the
            // widget's name (red/yellow → 语法, purple → 无理).
            const bool isSummaryBadge =
                watched == owner_.editorValidationErrorIconLabel_
                || watched == owner_.editorValidationErrorCountLabel_
                || watched == owner_.editorValidationWarningIconLabel_
                || watched == owner_.editorValidationWarningCountLabel_
                || watched == owner_.editorValidationMuriIconLabel_
                || watched == owner_.editorValidationMuriCountLabel_;
            if (isSummaryBadge) {
                const QString targetTab =
                    watchedWidget->property("validationSummaryTab").toString();
                if (!targetTab.isEmpty()) {
                    owner_.setCurrentBottomTabsTabId(targetTab);
                    return true;
                }
            }
        }
    }
    if (owner_.runtimeDebugOutputEnabled_
        && event != nullptr
        && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
        && (watched == owner_.editorWidget_
            || watched == owner_.editorViewport_
            || watched == owner_.metadataExtraEdit_
            || watched == owner_.editorFindEdit_
            || watched == owner_.editorReplaceEdit_
            || watched == owner_.previewPanel_
            || watched == owner_.previewCanvasContainer_
            || watched == owner_.previewCanvasFrame_
            || watched == owner_.previewSlider_)) {
        auto* focusEvent = static_cast<QFocusEvent*>(event);
        this->logFocusDebug(
            event->type() == QEvent::FocusIn ? QStringLiteral("watched_focus_in") : QStringLiteral("watched_focus_out"),
            watchedWidget,
            QApplication::focusWidget(),
            QStringLiteral("watched=%1 reason=%2 spontaneous=%3")
                .arg(this->describeFocusWidget(watchedWidget))
                .arg(this->formatFocusReason(focusEvent != nullptr ? focusEvent->reason() : Qt::NoFocusReason))
                .arg(event->spontaneous() ? 1 : 0)
        );
    }
    if (event != nullptr && event->type() == QEvent::FocusIn && this->isTextInputWidget(watchedWidget)) {
        owner_.stopPreviewHeldSeek();
    }
    if (watched == owner_.editorViewport_
        && event != nullptr
        && (event->type() == QEvent::Wheel || event->type() == QEvent::MouseButtonPress)
        && owner_.qtPreviewPlaying_
        && owner_.previewFollowEnabled_
        && owner_.previewViewportLockEnabled_) {
        owner_.pauseQtPreviewPlaybackExact();
        owner_.updatePauseButtonAppearance();
        const double second = qMax(0.0, owner_.qtPreviewPauseSecond_);
        QTimer::singleShot(0, &owner_, [this, second]() {
            if (owner_.previewFollowEnabled_ && owner_.previewViewportLockEnabled_) {
                owner_.syncEditorCursorToPreviewSecond(second, true, false);
            }
        });
    }
    // Hold-key pause display toggle (default Alt, rebindable — see
    // pauseDisplayHoldKey() above): while the preview is paused, holding the
    // key temporarily inverts the "暂停时显示判定区" option (judge area ⇄
    // PV/BG); releasing it restores. Losing app focus (e.g. Alt+Tab) eats the
    // release event, so ApplicationDeactivate also restores. This block only
    // OBSERVES — it never consumes the event — so Alt+F4, menu mnemonics and
    // every other combination keep working; a prefixed combo merely flips the
    // paused view for as long as the key is physically down. Auto-repeat
    // releases are ignored so a held non-modifier key doesn't flicker the
    // state on synthesized repeat pairs.
    if (event != nullptr
        && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        auto* holdKeyEvent = static_cast<QKeyEvent*>(event);
        const PauseDisplayHoldKey hold = pauseDisplayHoldKey();
        if (holdKeyEvent->key() == hold.key) {
            if (event->type() == QEvent::KeyPress
                && !holdKeyEvent->isAutoRepeat()
                && holdKeyEvent->modifiers() == hold.pressModifiers
                && QApplication::activeModalWidget() == nullptr
                && QApplication::activePopupWidget() == nullptr) {
                owner_.setPauseDisplayAltHoldActive(true);
            } else if (event->type() == QEvent::KeyRelease && !holdKeyEvent->isAutoRepeat()) {
                owner_.setPauseDisplayAltHoldActive(false);
            }
        }
    } else if (event != nullptr && event->type() == QEvent::ApplicationDeactivate) {
        owner_.setPauseDisplayAltHoldActive(false);
    }
    if (event != nullptr
        && (event->type() == QEvent::ShortcutOverride
            || event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (isUnmodifiedHorizontalArrowKey(keyEvent)
            && (this->isTextInputWidget(watchedWidget) || this->hasActiveTextInputFocus())) {
            owner_.stopPreviewHeldSeek();
            return owner_.QMainWindow::eventFilter(watched, event);
        }
    }

    // Per-widget opt-in tooltips: any widget can re-enable its own tooltip past
    // the global suppression by setting the dynamic property "miacodeAllowTooltip"
    // (e.g. the export dialog's "添加片头 (?)" help badge). Such a badge is usually
    // a lone allowed widget surrounded by suppressed ones, and relying on Qt's
    // default ToolTip delivery is unreliable there (an adjacent suppressed widget's
    // hideText()+return-true poisons Qt's tooltip session). So we drive the opt-in
    // tooltip ourselves:
    //   - show it eagerly on hover-enter and on click, not only after the long
    //     standard hover delay;
    //   - wrap the body in a fixed-width rich-text cell so long help text wraps
    //     instead of stretching into one very wide strip.
    const bool isOptInTooltipWidget =
        watchedWidget != nullptr
        && watchedWidget->property("miacodeAllowTooltip").toBool();
    if (isOptInTooltipWidget
        && event != nullptr
        && (event->type() == QEvent::Enter
            || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::ToolTip)) {
        const QString body = watchedWidget->toolTip();
        if (!body.isEmpty()) {
            constexpr int kOptInTooltipWidthPx = 300;
            // A <td width=...> cell is the reliable way to bound tooltip width in
            // Qt's rich-text engine; word-wrap then happens inside that width.
            // Escape first (the rich-text cell is HTML), then turn explicit
            // newlines into <br> so a tooltip body can force line breaks —
            // otherwise Qt's rich-text engine collapses the newline to a space.
            const QString wrapped =
                QStringLiteral("<table><tr><td width=\"%1\">%2</td></tr></table>")
                    .arg(kOptInTooltipWidthPx)
                    .arg(body.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
            const QPoint globalPos = event->type() == QEvent::ToolTip
                ? static_cast<QHelpEvent*>(event)->globalPos()
                : QCursor::pos();
            QToolTip::showText(globalPos, wrapped, watchedWidget);
        }
        // Consume only the standard ToolTip event (we fully own its display); let
        // Enter/clicks fall through to any other normal handling.
        if (event->type() == QEvent::ToolTip) {
            return true;
        }
    }

    if (event != nullptr && event->type() == QEvent::ToolTip) {
        const bool allowPreviewTooltip =
            widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewPanel_)
            || widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewLeftColumn_)
            || widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewCanvasContainer_)
            || widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewCanvasFrame_)
            || widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewControlCard_)
            || widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewStatsCard_)
            || widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewFullscreenWindow_)
            || widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewFullscreenHost_)
            || widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewFullscreenControlsWindow_)
            || widgetMatchesOrDescendsFrom(watchedWidget, owner_.previewFullscreenHintWindow_);
        if (watchedWidget != nullptr && !allowPreviewTooltip) {
            QToolTip::hideText();
            event->ignore();
            return true;
        }
    }

    if (owner_.editorFindGeometryHost_ != nullptr
        && watched == owner_.editorFindGeometryHost_
        && event != nullptr
        && event->type() == QEvent::Resize) {
        this->updateEditorFindBarGeometry();
        this->applyFindOverlayInset();
    }
    if (owner_.bottomTabs_ != nullptr && event != nullptr && watchedWidget != nullptr) {
        const bool watchedBottomTabsTop =
            watched == owner_.bottomTabs_
            || watched == owner_.bottomTabs_->tabBar();
        if (watchedBottomTabsTop) {
            if (event->type() == QEvent::MouseButtonPress) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton
                    && bottomTabsResizeHotzoneContains(owner_.bottomTabs_, watchedWidget, mouseEvent->pos())) {
                    owner_.bottomTabsResizeDragActive_ = true;
                    owner_.bottomTabsResizeStartGlobalY_ = mouseEvent->globalPosition().toPoint().y();
                    owner_.bottomTabsResizeStartHeight_ = owner_.bottomTabs_->height();
                    owner_.bottomTabs_->setCursor(Qt::SizeVerCursor);
                    event->accept();
                    return true;
                }
            } else if (event->type() == QEvent::MouseMove) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (owner_.bottomTabsResizeDragActive_) {
                    const int deltaY =
                        mouseEvent->globalPosition().toPoint().y() - owner_.bottomTabsResizeStartGlobalY_;
                    setShellBottomTabsHeight(owner_.bottomTabsResizeStartHeight_ - deltaY);
                    event->accept();
                    return true;
                }
                if (bottomTabsResizeHotzoneContains(owner_.bottomTabs_, watchedWidget, mouseEvent->pos())) {
                    owner_.bottomTabs_->setCursor(Qt::SizeVerCursor);
                } else {
                    owner_.bottomTabs_->unsetCursor();
                }
            } else if (event->type() == QEvent::MouseButtonRelease) {
                if (owner_.bottomTabsResizeDragActive_) {
                    owner_.bottomTabsResizeDragActive_ = false;
                    owner_.bottomTabs_->unsetCursor();
                    event->accept();
                    return true;
                }
            } else if (event->type() == QEvent::Leave) {
                if (!owner_.bottomTabsResizeDragActive_) {
                    owner_.bottomTabs_->unsetCursor();
                }
            }
        }
    }
    if (owner_.bottomTabs_ != nullptr && watched == owner_.bottomTabs_->tabBar() && event->type() == QEvent::Wheel) {
        return true;
    }
    if (owner_.aboutIconLabel_ != nullptr && watched == owner_.aboutIconLabel_) {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                if (owner_.invalidStarPreviewEasterEggEnabled_) {
                    owner_.invalidStarPreviewAboutClickCount_ = 0;
                    owner_.invalidStarPreviewAboutClickElapsed_.invalidate();
                    this->setInvalidStarPreviewEasterEggEnabled(false);
                } else {
                    if (!owner_.invalidStarPreviewAboutClickElapsed_.isValid()
                        || owner_.invalidStarPreviewAboutClickElapsed_.elapsed() > kInvalidStarPreviewAboutClickWindowMs) {
                        owner_.invalidStarPreviewAboutClickCount_ = 0;
                    }
                    ++owner_.invalidStarPreviewAboutClickCount_;
                    if (owner_.invalidStarPreviewAboutClickElapsed_.isValid()) {
                        owner_.invalidStarPreviewAboutClickElapsed_.restart();
                    } else {
                        owner_.invalidStarPreviewAboutClickElapsed_.start();
                    }
                    if (owner_.invalidStarPreviewAboutClickCount_ >= 3) {
                        owner_.invalidStarPreviewAboutClickCount_ = 0;
                        owner_.invalidStarPreviewAboutClickElapsed_.invalidate();
                        this->setInvalidStarPreviewEasterEggEnabled(true);
                    }
                }
                return true;
            }
        }
    }
    if ((owner_.errorList_ != nullptr && watched == owner_.errorList_->viewport())
        || (owner_.muriList_ != nullptr && watched == owner_.muriList_->viewport())) {
        if (event->type() == QEvent::Resize
            || event->type() == QEvent::Show
            || event->type() == QEvent::LayoutRequest
            || event->type() == QEvent::PolishRequest) {
            owner_.scheduleWrappedListRelayout(
                watched == (owner_.errorList_ != nullptr ? owner_.errorList_->viewport() : nullptr) ? owner_.errorList_ : owner_.muriList_
            );
        }
    }
    if (event != nullptr
        && UiDialogs::hasVisibleProtectedPreviewDialog()
        && (event->type() == QEvent::ShortcutOverride
            || event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (UiDialogs::isPreviewShortcutEvent(keyEvent)) {
            if (event->type() == QEvent::KeyRelease
                && owner_.previewSeekHeldArrowKey_ != 0
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && owner_.previewSeekHeldArrowKey_ == keyEvent->key()) {
                owner_.stopPreviewHeldSeek(keyEvent->key());
            }
            return owner_.QMainWindow::eventFilter(watched, event);
        }
    }
    const QWindow* previewVisibleWindow = this->previewVisibleHostWindow();
    const bool previewKeyScope =
        watched == owner_.previewSlider_
        || watched == owner_.previewCanvasContainer_
        || watched == owner_.previewCanvasFrame_
        || watched == owner_.previewPanel_
        || watched == owner_.previewFullscreenWindow_
        || watched == owner_.previewFullscreenHost_
        || watched == owner_.previewFullscreenControlsWindow_
        || watched == owner_.previewFullscreenButton_
        || watched == previewVisibleWindow;
    const bool previewMouseFocusScope =
        watched == owner_.previewCanvasContainer_
        || watched == owner_.previewCanvasFrame_
        || watched == owner_.previewPanel_
        || watched == owner_.previewFullscreenWindow_
        || watched == owner_.previewFullscreenHost_
        || watched == previewVisibleWindow;
    const bool previewFullscreenOverlayScope =
        watched == owner_.previewFullscreenWindow_
        || watched == owner_.previewFullscreenHost_
        || watched == owner_.previewFullscreenControlsWindow_
        || watched == owner_.previewFullscreenHintWindow_
        || watched == owner_.previewCanvasContainer_
        || watched == owner_.previewCanvasFrame_
        || watched == owner_.previewControlCard_
        || watched == owner_.previewSlider_
        || watched == owner_.stopPreviewButton_
        || watched == owner_.pausePreviewButton_
        || watched == owner_.previewSpeedButton_
        || watched == owner_.previewFullscreenButton_
        || watched == previewVisibleWindow;
    if (previewMouseFocusScope
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel)) {
        this->focusPreviewInteractionTarget(watched, Qt::MouseFocusReason);
    }
    if (owner_.previewFullscreenActive_ && previewFullscreenOverlayScope) {
        if (event->type() == QEvent::MouseMove
            || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::Wheel) {
            const QPoint globalCursorPos = QCursor::pos();
            if (owner_.shouldRevealPreviewFullscreenControls(globalCursorPos)) {
                owner_.showPreviewFullscreenControls(event->type() != QEvent::MouseButtonPress);
            } else if (owner_.previewFullscreenControlsVisible_) {
                owner_.schedulePreviewFullscreenControlsAutoHide();
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_F11) {
                owner_.togglePreviewFullscreen();
                return true;
            }
            if (!keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_Escape) {
                owner_.exitPreviewFullscreen();
                return true;
            }
        }
    }
    if (owner_.previewFullscreenWindow_ != nullptr && watched == owner_.previewFullscreenWindow_) {
        if (event->type() == QEvent::Close) {
            owner_.exitPreviewFullscreen();
            event->ignore();
            return true;
        }
        if (owner_.previewFullscreenActive_
            && (event->type() == QEvent::Move
                || event->type() == QEvent::Resize
                || event->type() == QEvent::Show
                || event->type() == QEvent::WindowStateChange)) {
            owner_.updatePreviewFullscreenOverlayGeometry();
        }
    }
    if (owner_.previewSlider_ != nullptr && watched == owner_.previewSlider_) {
        if ((event->type() == QEvent::Wheel || event->type() == QEvent::MouseButtonPress)
            && owner_.qtPreviewPlaying_
            && owner_.previewFollowEnabled_
            && owner_.previewViewportLockEnabled_) {
            owner_.pauseQtPreviewPlaybackExact();
            owner_.updatePauseButtonAppearance();
            QTimer::singleShot(0, &owner_, [this]() {
                if (owner_.previewFollowEnabled_ && owner_.previewViewportLockEnabled_) {
                    owner_.syncEditorCursorToPreviewSecond(qMax(0.0, owner_.qtPreviewPauseSecond_), true, false);
                }
            });
        }
        if (event->type() == QEvent::Wheel) {
            owner_.stopPreviewHeldSeek();
            if (owner_.handlePreviewSeekWheel(static_cast<QWheelEvent*>(event))) {
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            owner_.stopPreviewHeldSeek();
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QStyleOptionSlider option;
                option.initFrom(owner_.previewSlider_);
                option.subControls = QStyle::SC_SliderHandle;
                option.orientation = owner_.previewSlider_->orientation();
                option.minimum = owner_.previewSlider_->minimum();
                option.maximum = owner_.previewSlider_->maximum();
                option.sliderPosition = owner_.previewSlider_->sliderPosition();
                option.sliderValue = owner_.previewSlider_->value();
                option.upsideDown = false;
                const QRect handleRect = owner_.previewSlider_->style()->subControlRect(
                    QStyle::CC_Slider,
                    &option,
                    QStyle::SC_SliderHandle,
                    owner_.previewSlider_
                );
                if (!handleRect.contains(mouseEvent->pos())) {
                    const int value = QStyle::sliderValueFromPosition(
                        owner_.previewSlider_->minimum(),
                        owner_.previewSlider_->maximum(),
                        mouseEvent->pos().x(),
                        qMax(1, owner_.previewSlider_->width()),
                        false
                    );
                    owner_.previewSlider_->setFocus(Qt::MouseFocusReason);
                    owner_.previewSlider_->setValue(value);
                    owner_.showPreviewSliderTimeHint(value);
                    const double clickSecond = static_cast<double>(value) / 1000.0;
                    // Negative-time intro region: a click in [-duration, 0) shows
                    // a static intro frame instead of a chart seek.
                    if (!owner_.handleExportIntroSliderSeek(clickSecond)) {
                        owner_.seekPreviewDiscreteToSecond(clickSecond, true);
                    }
                    return true;
                }
            }
        }
    }
    if (previewKeyScope) {
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_F11) {
                owner_.togglePreviewFullscreen();
                return true;
            }
            if (owner_.previewFullscreenActive_
                && !keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_Escape) {
                owner_.exitPreviewFullscreen();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Space
                && keyEvent->modifiers() == Qt::NoModifier
                && !keyEvent->isAutoRepeat()) {
                owner_.onTogglePreviewPause();
                return true;
            }
            if (owner_.previewSlider_ == nullptr) {
                return owner_.QMainWindow::eventFilter(watched, event);
            }
            int direction = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                direction = -1;
            } else if (keyEvent->key() == Qt::Key_Right) {
                direction = 1;
            }
            if (direction != 0) {
                if (keyEvent->modifiers() != Qt::NoModifier) {
                    return owner_.QMainWindow::eventFilter(watched, event);
                }
                if (keyEvent->isAutoRepeat()) {
                    return true;
                }
                owner_.beginPreviewHeldSeek(direction, keyEvent->key());
                owner_.stepPreviewBySeconds(
                    static_cast<double>(direction) * miacode::preview_interaction::kSeekSingleStepSeconds,
                    true
                );
                return true;
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Space && keyEvent->modifiers() == Qt::NoModifier) {
                return true;
            }
            if (owner_.previewSlider_ == nullptr) {
                return owner_.QMainWindow::eventFilter(watched, event);
            }
            if (!keyEvent->isAutoRepeat()
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && owner_.previewSeekHeldArrowKey_ == keyEvent->key()) {
                owner_.stopPreviewHeldSeek(keyEvent->key());
                return true;
            }
        }
    }
    if (watched == owner_.editorFindEdit_ || watched == owner_.editorReplaceEdit_) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const bool ctrlOnly = (keyEvent->modifiers() & Qt::ControlModifier)
                && !(keyEvent->modifiers() & (Qt::AltModifier | Qt::MetaModifier));
            if ((keyEvent->matches(QKeySequence::Find))
                || (ctrlOnly && keyEvent->key() == Qt::Key_F)) {
                this->onToggleFindReplace();
                return true;
            }
        }
    }
    if (watched == owner_.editorViewport_ && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const bool ctrlLeftClick = mouseEvent->button() == Qt::LeftButton
            && (mouseEvent->modifiers() & Qt::ControlModifier);
        if (ctrlLeftClick) {
            if (!owner_.editorCtrlLeftJumpPending_) {
                owner_.editorCtrlLeftJumpInteractionOp_ = ++owner_.previewInteractionSequence_;
                const quint64 opId = owner_.editorCtrlLeftJumpInteractionOp_;
                appendPreviewInteractionLog(
                    QStringLiteral("ctrl_click_press"),
                    QString("op=%1 source=editor_ctrl_click x=%2 y=%3 modifiers=%4")
                        .arg(opId)
                        .arg(mouseEvent->pos().x())
                        .arg(mouseEvent->pos().y())
                        .arg(static_cast<int>(mouseEvent->modifiers())));
            }
            owner_.editorCtrlLeftJumpPending_ = true;
            owner_.editorCtrlLeftJumpDragged_ = false;
            owner_.editorCtrlLeftJumpPressPos_ = mouseEvent->pos();
        } else if (mouseEvent->button() == Qt::LeftButton) {
            owner_.editorCtrlLeftJumpPending_ = false;
            owner_.editorCtrlLeftJumpDragged_ = false;
            owner_.editorCtrlLeftJumpDispatchActive_ = false;
            owner_.editorCtrlLeftJumpInteractionOp_ = 0;
        }
        if (mouseEvent->button() == Qt::LeftButton && !owner_.qtPreviewPlaying_ && !ctrlLeftClick) {
            QTimer::singleShot(0, &owner_, [this]() {
                const bool syncTimelineCursor =
                    !owner_.quickShellUiFocusBridgeMode_ || owner_.quickTimelineSurfaceReady_;
                owner_.scheduleDeferredEditorUiUpdate(
                    false,
                    false,
                    syncTimelineCursor,
                    owner_.timelineSyncEnabled_,
                    false,
                    0.0,
                    false);
                // Intentionally no View Lock action here: when playback is
                // already paused, a left-click's text position is by
                // definition already visible (you can only click on visible
                // pixels). The previous applyPreviewFollowCursor(centerView=
                // true, suppressSignals=true) added in `42a9f06 view locked`
                // overwrote drag selections and froze the caret blink via
                // QSignalBlocker. The View Lock path while paused should be
                // a no-op — Qt's default click handling is correct.
            });
        }
    }
    if (watched == owner_.editorViewport_ && event->type() == QEvent::MouseMove && owner_.editorCtrlLeftJumpPending_) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->buttons().testFlag(Qt::LeftButton)
            && (mouseEvent->pos() - owner_.editorCtrlLeftJumpPressPos_).manhattanLength() >= QApplication::startDragDistance()) {
            owner_.editorCtrlLeftJumpDragged_ = true;
        }
    }
    if (watched == owner_.editorViewport_ && event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && owner_.editorCtrlLeftJumpPending_) {
            const bool wasDragged = owner_.editorCtrlLeftJumpDragged_;
            const bool shouldJump = !wasDragged
                && (mouseEvent->modifiers() & Qt::ControlModifier);
            const QPoint releasePos = mouseEvent->pos();
            const quint64 opId = owner_.editorCtrlLeftJumpInteractionOp_ != 0
                ? owner_.editorCtrlLeftJumpInteractionOp_
                : ++owner_.previewInteractionSequence_;
            owner_.editorCtrlLeftJumpPending_ = false;
            owner_.editorCtrlLeftJumpDragged_ = false;
            appendPreviewInteractionLog(
                QStringLiteral("ctrl_click_release"),
                QString("op=%1 source=editor_ctrl_click should_jump=%2 dragged=%3 x=%4 y=%5 modifiers=%6")
                    .arg(opId)
                    .arg(shouldJump ? 1 : 0)
                    .arg(wasDragged ? 1 : 0)
                    .arg(releasePos.x())
                    .arg(releasePos.y())
                    .arg(static_cast<int>(mouseEvent->modifiers())));
            if (shouldJump) {
                owner_.editorCtrlLeftJumpDispatchActive_ = true;
                owner_.deferredEditorUiTimelineCursorPending_ = false;
                owner_.deferredEditorUiCenterView_ = false;
                owner_.pendingQuickTimelineCursorSync_ = false;
                owner_.pendingQuickTimelineCursorSecond_ = 0.0;
                owner_.pendingQuickTimelineCursorCenterView_ = false;
                appendPreviewInteractionLog(
                    QStringLiteral("ctrl_click_dispatch"),
                    QString("op=%1 source=editor_ctrl_click x=%2 y=%3")
                        .arg(opId)
                        .arg(releasePos.x())
                        .arg(releasePos.y()));
                auto* editor = qobject_cast<PlainCodeEditor*>(owner_.editorWidget_);
                if (editor == nullptr) {
                    appendPreviewInteractionLog(
                        QStringLiteral("ctrl_click_abort"),
                        QString("op=%1 source=editor_ctrl_click reason=no_editor").arg(opId));
                    owner_.editorCtrlLeftJumpDispatchActive_ = false;
                    owner_.editorCtrlLeftJumpInteractionOp_ = 0;
                    return false;
                }
                const QPoint normalizedReleasePos =
                    editor->normalizedViewportHitPosition(QPointF(releasePos)).toPoint();
                const QTextCursor cursor = editor->cursorForPosition(normalizedReleasePos);
                const int line = cursor.blockNumber() + 1;
                const int col = cursor.positionInBlock() + 1;
                const double second = owner_.timelineSecondForCursor(line, col);
                appendPreviewInteractionLog(
                    QStringLiteral("ctrl_click_resolved"),
                    QString("op=%1 source=editor_ctrl_click line=%2 col=%3 second=%4 playing=%5 normalized_x=%6 normalized_y=%7")
                        .arg(opId)
                        .arg(line)
                        .arg(col)
                        .arg(second, 0, 'f', 6)
                        .arg(owner_.qtPreviewPlaying_ ? 1 : 0)
                        .arg(normalizedReleasePos.x())
                        .arg(normalizedReleasePos.y()));
                if (owner_.qtPreviewPlaying_) {
                    appendPreviewInteractionLog(
                        QStringLiteral("ctrl_click_pause_begin"),
                        QString("op=%1 source=editor_ctrl_click current_second=%2")
                            .arg(opId)
                            .arg(owner_.currentPreviewAuthoritativeAudioClockSecond(), 0, 'f', 6));
                    owner_.pauseQtPreviewPlaybackExact();
                    appendPreviewInteractionLog(
                        QStringLiteral("ctrl_click_pause_complete"),
                        QString("op=%1 source=editor_ctrl_click paused_second=%2")
                            .arg(opId)
                            .arg(owner_.qtPreviewPauseSecond_, 0, 'f', 6));
                }
                const bool previousSuppressTimelineCursorSync = owner_.suppressTimelineCursorSync_;
                owner_.suppressTimelineCursorSync_ = true;
                appendPreviewInteractionLog(
                    QStringLiteral("ctrl_click_seek_begin"),
                    QString("op=%1 source=editor_ctrl_click target_second=%2")
                        .arg(opId)
                        .arg(second, 0, 'f', 6));
                owner_.seekPreviewDiscreteToSecond(second, true);
                if (owner_.timelineQuickStateBridge_ != nullptr) {
                    owner_.deferTimelineCursorBridgeUpdate(second, false);
                }
                appendPreviewInteractionLog(
                    QStringLiteral("ctrl_click_seek_complete"),
                    QString("op=%1 source=editor_ctrl_click final_second=%2")
                        .arg(opId)
                        .arg(owner_.qtPreviewPauseSecond_, 0, 'f', 6));
                owner_.suppressTimelineCursorSync_ = previousSuppressTimelineCursorSync;
                owner_.editorCtrlLeftJumpDispatchActive_ = false;
                owner_.editorCtrlLeftJumpInteractionOp_ = 0;
            } else {
                owner_.editorCtrlLeftJumpDispatchActive_ = false;
                owner_.editorCtrlLeftJumpInteractionOp_ = 0;
            }
        }
    }
    if (watched == owner_.editorViewport_ && event->type() == QEvent::FocusIn && !owner_.qtPreviewPlaying_) {
        QTimer::singleShot(0, &owner_, [this]() {
            const bool syncTimelineCursor =
                !owner_.quickShellUiFocusBridgeMode_ || owner_.quickTimelineSurfaceReady_;
            owner_.scheduleDeferredEditorUiUpdate(
                false,
                false,
                syncTimelineCursor,
                owner_.timelineSyncEnabled_,
                false,
                0.0,
                false);
        });
    }
    return owner_.QMainWindow::eventFilter(watched, event);
}

QTextEdit* MainWindow::WindowSection::activeFindTarget() const
{
    auto* chartEditor = qobject_cast<QTextEdit*>(owner_.editorWidget_);
    QWidget* focus = QApplication::focusWidget();
    if (focus != nullptr) {
        if (chartEditor != nullptr && (focus == chartEditor || chartEditor->isAncestorOf(focus))) {
            return chartEditor;
        }
        if (owner_.metadataExtraEdit_ != nullptr && (focus == owner_.metadataExtraEdit_ || owner_.metadataExtraEdit_->isAncestorOf(focus))) {
            return owner_.metadataExtraEdit_;
        }
    }

    if (owner_.editorStack_ != nullptr && owner_.editorStack_->currentWidget() == owner_.chartPage_ && chartEditor != nullptr) {
        return chartEditor;
    }
    if (owner_.editorStack_ != nullptr && owner_.editorStack_->currentWidget() == owner_.metadataPage_ && owner_.metadataExtraEdit_ != nullptr) {
        return owner_.metadataExtraEdit_;
    }
    return chartEditor != nullptr ? chartEditor : owner_.metadataExtraEdit_;
}

bool MainWindow::WindowSection::runFindInEditor(bool backward)
{
    QTextEdit* target = this->activeFindTarget();
    if (target == nullptr || owner_.editorFindEdit_ == nullptr) {
        return false;
    }
    const QString pattern = owner_.editorFindEdit_->text();
    if (pattern.isEmpty()) {
        return false;
    }

    QTextDocument::FindFlags flags;
    if (backward) {
        flags |= QTextDocument::FindBackward;
    }
    if (target->find(pattern, flags)) {
        return true;
    }

    QTextCursor resetCursor = target->textCursor();
    resetCursor.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    target->setTextCursor(resetCursor);
    return target->find(pattern, flags);
}

void MainWindow::WindowSection::updateEditorFindBarGeometry()
{
    QWidget* geometryHost = owner_.editorFindGeometryHost_ != nullptr ? owner_.editorFindGeometryHost_ : owner_.editorStack_;
    if (owner_.editorFindBar_ == nullptr || geometryHost == nullptr) {
        return;
    }
    const int availableWidth = qMax(0, geometryHost->width() - (kEditorFindBarHorizontalMargin * 2));
    if (availableWidth <= 0) {
        return;
    }
    int width = qMin(kEditorFindBarMaxWidth, availableWidth);
    if (availableWidth >= kEditorFindBarMinWidth) {
        width = qMax(kEditorFindBarMinWidth, width);
    }
    const int x = qMax(kEditorFindBarHorizontalMargin, geometryHost->width() - kEditorFindBarHorizontalMargin - width);
    const int y = kEditorFindBarTopMargin;
    const int height = owner_.editorFindBar_->sizeHint().height();
    owner_.editorFindBar_->setGeometry(x, y, width, height);
    owner_.editorFindBar_->raise();
}

void MainWindow::WindowSection::applyFindOverlayInset()
{
    const int topInset =
        (owner_.editorFindBar_ != nullptr && owner_.editorFindBar_->isVisible())
        ? owner_.editorFindBar_->height() + kEditorFindBarOverlayGap
        : 0;
    if (auto* plainEditor = qobject_cast<PlainCodeEditor*>(owner_.editorWidget_); plainEditor != nullptr) {
        plainEditor->setTopOverlayInsetPixels(topInset);
    }
}

void MainWindow::WindowSection::hideFindReplaceBar()
{
    if (owner_.editorFindBar_ == nullptr || !owner_.editorFindBar_->isVisible()) {
        return;
    }
    owner_.editorFindBar_->hide();
    this->applyFindOverlayInset();
    if (QTextEdit* target = this->activeFindTarget(); target != nullptr) {
        target->setFocus();
    }
}

void MainWindow::WindowSection::onToggleFindReplace()
{
    MC_OP("MainWindow::WindowSection::onToggleFindReplace");
    if (owner_.editorFindBar_ == nullptr) {
        _mc_op_.fail(QStringLiteral("editorFindBar_ null"));
        return;
    }
    if (owner_.editorFindBar_->isVisible()) {
        this->hideFindReplaceBar();
        return;
    }

    this->updateEditorFindBarGeometry();
    owner_.editorFindBar_->show();
    owner_.editorFindBar_->raise();
    this->applyFindOverlayInset();
    QTextEdit* target = this->activeFindTarget();
    if (target != nullptr && owner_.editorFindEdit_ != nullptr && owner_.editorFindEdit_->text().isEmpty()) {
        const QTextCursor cursor = target->textCursor();
        const QString selected = cursor.selectedText();
        if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) {
            owner_.editorFindEdit_->setText(selected);
        }
    }
    if (owner_.editorFindEdit_ != nullptr) {
        owner_.editorFindEdit_->setFocus();
        owner_.editorFindEdit_->selectAll();
    }
}

void MainWindow::WindowSection::onFindNext()
{
    MC_OP("MainWindow::WindowSection::onFindNext");
    this->runFindInEditor(false);
}

void MainWindow::WindowSection::onFindPrevious()
{
    MC_OP("MainWindow::WindowSection::onFindPrevious");
    this->runFindInEditor(true);
}

void MainWindow::WindowSection::onReplaceOne()
{
    MC_OP("MainWindow::WindowSection::onReplaceOne");
    QTextEdit* target = this->activeFindTarget();
    if (target == nullptr || owner_.editorFindEdit_ == nullptr || owner_.editorReplaceEdit_ == nullptr) {
        _mc_op_.fail(QStringLiteral("find/replace UI not initialised"));
        return;
    }
    const QString findText = owner_.editorFindEdit_->text();
    if (findText.isEmpty()) {
        _mc_op_.fail(QStringLiteral("empty findText"));
        return;
    }

    QTextCursor cursor = target->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == findText) {
        cursor.insertText(owner_.editorReplaceEdit_->text());
        target->setTextCursor(cursor);
    }
    this->runFindInEditor(false);
}

void MainWindow::WindowSection::onReplaceAll()
{
    MC_OP("MainWindow::WindowSection::onReplaceAll");
    QTextEdit* target = this->activeFindTarget();
    if (target == nullptr || owner_.editorFindEdit_ == nullptr || owner_.editorReplaceEdit_ == nullptr) {
        _mc_op_.fail(QStringLiteral("find/replace UI not initialised"));
        return;
    }
    const QString findText = owner_.editorFindEdit_->text();
    if (findText.isEmpty()) {
        _mc_op_.fail(QStringLiteral("empty findText"));
        return;
    }
    _mc_op_.note(QStringLiteral("findText_len=%1").arg(findText.size()));

    QTextDocument* doc = target->document();
    QTextCursor editCursor(doc);
    editCursor.beginEditBlock();
    const QString replaceText = owner_.editorReplaceEdit_->text();
    int replacedCount = 0;
    QTextCursor searchCursor = doc->find(findText, 0);
    while (true) {
        if (searchCursor.isNull()) {
            break;
        }
        searchCursor.insertText(replaceText);
        ++replacedCount;
        searchCursor = doc->find(findText, searchCursor);
    }
    editCursor.endEditBlock();
    owner_.statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已替换 %1 处。").arg(replacedCount)
            : QStringLiteral("Replaced %1 occurrence(s).").arg(replacedCount)
    );
}

void MainWindow::WindowSection::resizeEvent(QResizeEvent* event)
{
    owner_.QMainWindow::resizeEvent(event);
    if (!owner_.outlineDockCollapsed_ && owner_.outlineDock_ != nullptr) {
        owner_.outlineDockExpandedWidth_ = qMax(120, owner_.outlineDock_->width());
    }
    owner_.updatePreviewWorkspaceLayout();
    owner_.updatePreviewPlaybackRateToastGeometry();
    owner_.updateEditorHeaderLayoutMode();
    this->updateEditorFindBarGeometry();
    this->applyFindOverlayInset();
    owner_.relayoutWrappedListRows(owner_.errorList_);
    owner_.relayoutWrappedListRows(owner_.muriList_);
    this->logWindowGeometryDebug(
        "resize_event",
        QString("old=%1x%2 new=%3x%4")
            .arg(event->oldSize().width())
            .arg(event->oldSize().height())
            .arg(event->size().width())
            .arg(event->size().height())
    );
}

void MainWindow::WindowSection::moveEvent(QMoveEvent* event)
{
    owner_.QMainWindow::moveEvent(event);
    owner_.refreshPreviewFrameRateTimers();
    this->logWindowGeometryDebug(
        "move_event",
        QString("old=(%1,%2) new=(%3,%4)")
            .arg(event->oldPos().x())
            .arg(event->oldPos().y())
            .arg(event->pos().x())
            .arg(event->pos().y())
    );
}

void MainWindow::WindowSection::showEvent(QShowEvent* event)
{
    owner_.QMainWindow::showEvent(event);
    this->updateBottomTabsDeviceHeight();
    owner_.refreshPreviewFrameRateTimers();
    owner_.updatePreviewPlaybackRateToastGeometry();
    this->applySystemWindowBackdrop();
    this->logWindowGeometryDebug("show_event");
}

void MainWindow::WindowSection::hideEvent(QHideEvent* event)
{
    owner_.QMainWindow::hideEvent(event);
    this->logWindowGeometryDebug("hide_event");
}

bool MainWindow::WindowSection::event(QEvent* event)
{
    return owner_.QMainWindow::event(event);
}

void MainWindow::WindowSection::changeEvent(QEvent* event)
{
    const QEvent::Type type = event != nullptr ? event->type() : QEvent::None;
    owner_.QMainWindow::changeEvent(event);
    if (type == QEvent::WindowStateChange) {
        auto* stateEvent = static_cast<QWindowStateChangeEvent*>(event);
        this->logWindowGeometryDebug(
            "window_state_change",
            QString("old_state=%1 new_state=%2")
                .arg(this->formatWindowStateFlags(stateEvent != nullptr ? stateEvent->oldState() : Qt::WindowNoState))
                .arg(this->formatWindowStateFlags(owner_.windowState()))
        );
        const bool wasMinimized =
            stateEvent != nullptr && stateEvent->oldState().testFlag(Qt::WindowMinimized);
        const bool isNowMinimized = owner_.windowState().testFlag(Qt::WindowMinimized);
        if (!isNowMinimized && wasMinimized) {
            owner_.refreshPreviewFrameRateTimers();
            this->applySystemWindowBackdrop();
        }
    } else if (type == QEvent::ScreenChangeInternal
        || type == QEvent::DevicePixelRatioChange
        || type == QEvent::FontChange
        || type == QEvent::StyleChange
        || type == QEvent::PaletteChange
        || type == QEvent::ThemeChange
        || type == QEvent::ApplicationPaletteChange) {
        this->updateBottomTabsDeviceHeight();
        owner_.refreshPreviewFrameRateTimers();
        this->applySystemWindowBackdrop();
    } else if (type == QEvent::ActivationChange) {
        this->applySystemWindowBackdrop();
        const bool active = owner_.isActiveWindow();
        this->logFocusDebug(
            active ? QStringLiteral("activation_change_active") : QStringLiteral("activation_change_inactive"),
            QApplication::focusWidget(),
            owner_.focusWidget(),
            QStringLiteral("spontaneous=%1").arg(event != nullptr && event->spontaneous() ? 1 : 0)
        );
        this->logWindowGeometryDebug(
            "activation_change",
            QString("is_active=%1 spontaneous=%2")
                .arg(active ? 1 : 0)
                .arg(event != nullptr && event->spontaneous() ? 1 : 0)
        );
        if (!active) {
            this->rememberFocusedTextEditState();
            owner_.runAutosaveCheck(false);
        }
    } else if (type == QEvent::ZOrderChange) {
        this->logWindowGeometryDebug("zorder_change");
    }
}
