#include "runtime/shell/ShellHost.h"
#include "runtime/Shared.h"

#include "QtPreviewSfxRuntime.h"
#include "DialogLocalization.h"
#include "ShortcutRegistry.h"
#include "UiText.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/PreviewInteractionConfig.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneGeometry.h"
#include "core/scene/PreviewSceneMath.h"
#include "preview/runtime/PreviewRuntime.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QQuickWindow>
#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::runtime::shared;

namespace {

constexpr qint64 kInvalidStarPreviewAboutClickWindowMs = 900;
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

bool miacode::runtime::ShellHost::quickShellFocusBridgeActive() const
{
    return state_.uiFocusBridgeMode_;
}

QTextEdit* miacode::runtime::ShellHost::resolveRestorableTextEdit(QWidget* widget) const
{
    if (session_.metadataExtraEdit_ != nullptr && widgetMatchesOrDescendsFrom(widget, session_.metadataExtraEdit_)) {
        return session_.metadataExtraEdit_;
    }
    return nullptr;
}

bool miacode::runtime::ShellHost::isTextInputWidget(QWidget* widget) const
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

bool miacode::runtime::ShellHost::hasActiveTextInputFocus() const
{
    return this->isTextInputWidget(QApplication::focusWidget());
}

bool miacode::runtime::ShellHost::shouldRespectFocusedWidgetOnRestore(QWidget* widget, QTextEdit* target) const
{
    if (widget == nullptr || target == nullptr) {
        return false;
    }
    if (widget == target || target->isAncestorOf(widget)) {
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

QWindow* miacode::runtime::ShellHost::previewVisibleHostWindow() const
{
    return session_.scene_ != nullptr ? session_.scene_->visibleHostWindow() : nullptr;
}

void miacode::runtime::ShellHost::focusPreviewInteractionTarget(QObject* watched, Qt::FocusReason reason)
{
    QWidget* widget = qobject_cast<QWidget*>(watched);
    if (widget == nullptr || widget->focusPolicy() == Qt::NoFocus) {
        if (session_.previewFullscreenWindow_ != nullptr && session_.previewFullscreenWindow_->focusPolicy() != Qt::NoFocus) {
            widget = session_.previewFullscreenWindow_;
        }
    }
    if (widget != nullptr) {
        widget->setFocus(reason);
    }
    if (session_.scene_ != nullptr) {
        session_.scene_->requestActivate();
    }
}

bool miacode::runtime::ShellHost::touchPadAuthoringEditableContext() const
{
    return state_.previewTouchPadAuthoringShortcutEnabled_
        && session_.hasActiveDifficulty()
        && session_.editorAuthoringContextActive()
        && !state_.exportPreviewActive_
        && QApplication::activeModalWidget() == nullptr
        && QApplication::activePopupWidget() == nullptr;
}

void miacode::runtime::ShellHost::setHoveredTouchPad(const QString& pad)
{
    if (session_.scene_ == nullptr) {
        return;
    }
    session_.scene_->setHoveredTouchPad(pad);
}

void miacode::runtime::ShellHost::handleApplicationFocusChanged(QWidget* old, QWidget* now)
{
    this->logFocusDebug(
        quickShellFocusBridgeActive()
            ? QStringLiteral("app_focus_changed_quick_shell")
            : QStringLiteral("app_focus_changed"),
        old,
        now);
    if (QApplication::activeModalWidget() != nullptr || QApplication::activePopupWidget() != nullptr) {
        session_.setTouchPadAuthoringCtrlHoldActive(false);
        this->setHoveredTouchPad(QString());
    }
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

    if (!quickShellFocusBridgeActive() && now != nullptr) {
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

void miacode::runtime::ShellHost::recoverPreviewBackendsAfterApplicationResume()
{
    if (!state_.previewBackendRecoveryPending_) {
        return;
    }
    if (state_.playing_ || state_.previewStartupSyncPending_ || state_.previewLateVideoStartPending_) {
        return;
    }

    state_.previewBackendRecoveryPending_ = false;
    state_.previewSfxRuntimePrepared_ = false;
    // Recovery means "discard whatever was in flight and start over". Drop the
    // pending reload identity explicitly: ensurePreviewSfxRuntimePrepared() now
    // treats a non-zero preparation sequence as prepare-in-progress and would
    // otherwise skip the recovery reload, waiting forever on a completion that
    // this path has just invalidated by resetting the backend.
    state_.previewSfxRuntimePreparationAssetGeneration_ = 0;
    state_.previewSfxRuntimePreparationSequence_ = 0;
    if (state_.previewSfxRuntime_ != nullptr) {
        state_.previewSfxRuntime_->stopAll();
    }
    session_.ensurePreviewSfxRuntimePrepared();

    session_.shutdownPreviewStageMediaHost();
    if (state_.currentFilePath_.isEmpty()) {
        session_.clearPreviewStageMediaRoute();
    } else {
        session_.syncPreviewStageMediaRouteChartPath(
            state_.currentFilePath_,
            state_.lastTrackPath_,
            qMax(0.0, state_.pauseSecond_),
            session_.applicationServices_.workspace().document().videoPath  // Phase 4c — &video= override
        );
        session_.applyPreviewStageMediaRoutePlaybackRate(state_.previewPlaybackRate_, "fullscreen_return");
    }
    session_.applyPreviewStageMediaRouteVisualSettings();
}

void miacode::runtime::ShellHost::handleApplicationStateChanged(Qt::ApplicationState state)
{
    this->logFocusDebug(
        QStringLiteral("application_state_changed"),
        QApplication::focusWidget(),
        QApplication::focusWidget(),
        QStringLiteral("state=%1").arg(static_cast<int>(state))
    );
    if (state == Qt::ApplicationActive) {
        this->restoreFocusedTextEditState();
        this->recoverPreviewBackendsAfterApplicationResume();
        return;
    }
    session_.setTouchPadAuthoringCtrlHoldActive(false);
    this->setHoveredTouchPad(QString());
    if (state == Qt::ApplicationSuspended || state == Qt::ApplicationHidden) {
        state_.previewBackendRecoveryPending_ = true;
    }
    if (state == Qt::ApplicationInactive || state == Qt::ApplicationHidden || state == Qt::ApplicationSuspended) {
        this->rememberFocusedTextEditState();
    }
}

void miacode::runtime::ShellHost::rememberFocusedTextEditState(QTextEdit* textEdit)
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

void miacode::runtime::ShellHost::rememberFocusedTextEditState()
{
    this->rememberFocusedTextEditState(this->resolveRestorableTextEdit(QApplication::focusWidget()));
}

void miacode::runtime::ShellHost::clearFocusedTextEditState()
{
    if (!pendingTextFocusWidget_.isNull()) {
        this->logFocusDebug(QStringLiteral("clear_text_focus_state"), pendingTextFocusWidget_.data(), QApplication::focusWidget());
    }
    pendingTextFocusWidget_.clear();
    pendingTextCursorAnchor_ = -1;
    pendingTextCursorPosition_ = -1;
}

void miacode::runtime::ShellHost::restoreFocusedTextEditState()
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
    QTimer::singleShot(0, &session_, [this, target, savedAnchor, savedPosition]() {
        this->restoreFocusedTextEditStateAttempt(target, savedAnchor, savedPosition, 0);
    });
}

void miacode::runtime::ShellHost::restoreFocusedTextEditStateAttempt(
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
        QTimer::singleShot(kRestoreRetryDelayMs, &session_, [this, target, savedAnchor, savedPosition, attempt]() {
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
        QTimer::singleShot(kRestoreRetryDelayMs, &session_, [this, target, savedAnchor, savedPosition, attempt]() {
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

void miacode::runtime::ShellHost::attachRootWindow(QWindow* window)
{
    state_.rootWindow_ = window;
    UiDialogs::setApplicationDialogTransientParent(window);
}

bool miacode::runtime::ShellHost::eventFilter(QObject* watched, QEvent* event)
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
        && watched == session_.workspaceContentWidget_
        && session_.workspaceSurfaceSettleRelayoutArmed_) {
        if (session_.runtimeDebugOutputEnabled_) {
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
        QTimer::singleShot(0, &session_, [this]() { session_.refreshLayoutAfterPageSwitch(); });
    }
    if (session_.runtimeDebugOutputEnabled_
        && event != nullptr
        && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
        && (watched == session_.metadataExtraEdit_
            || watched == session_.previewSlider_)) {
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
        if (holdKeyEvent->key() == Qt::Key_Control && !holdKeyEvent->isAutoRepeat()) {
            if (event->type() == QEvent::KeyPress && touchPadAuthoringEditableContext()) {
                session_.setTouchPadAuthoringCtrlHoldActive(true);
            } else if (event->type() == QEvent::KeyRelease) {
                session_.setTouchPadAuthoringCtrlHoldActive(false);
            }
        }
        const PauseDisplayHoldKey hold = pauseDisplayHoldKey();
        if (holdKeyEvent->key() == hold.key
            && !(hold.key == Qt::Key_Control && state_.touchPadAuthoringCtrlHoldActive_)) {
            if (event->type() == QEvent::KeyPress
                && !holdKeyEvent->isAutoRepeat()
                && holdKeyEvent->modifiers() == hold.pressModifiers
                && QApplication::activeModalWidget() == nullptr
                && QApplication::activePopupWidget() == nullptr) {
                session_.setPauseDisplayAltHoldActive(true);
            } else if (event->type() == QEvent::KeyRelease && !holdKeyEvent->isAutoRepeat()) {
                session_.setPauseDisplayAltHoldActive(false);
                this->setHoveredTouchPad(QString());
            }
        }
    } else if (event != nullptr && event->type() == QEvent::ApplicationDeactivate) {
        session_.setPauseDisplayAltHoldActive(false);
        session_.setTouchPadAuthoringCtrlHoldActive(false);
        this->setHoveredTouchPad(QString());
    }
    if (event != nullptr
        && (event->type() == QEvent::ShortcutOverride
            || event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (isUnmodifiedHorizontalArrowKey(keyEvent)
            && (this->isTextInputWidget(watchedWidget) || this->hasActiveTextInputFocus())) {
            session_.stopPreviewHeldSeek();
            return session_.QObject::eventFilter(watched, event);
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
            widgetMatchesOrDescendsFrom(watchedWidget, session_.previewControlCard_)
            || widgetMatchesOrDescendsFrom(watchedWidget, session_.previewFullscreenWindow_)
            || widgetMatchesOrDescendsFrom(watchedWidget, session_.previewFullscreenControlsWindow_);
        if (watchedWidget != nullptr && !allowPreviewTooltip) {
            QToolTip::hideText();
            event->ignore();
            return true;
        }
    }

    if (session_.bottomTabs_ != nullptr && event != nullptr && watchedWidget != nullptr) {
        const bool watchedBottomTabsTop =
            watched == session_.bottomTabs_
            || watched == session_.bottomTabs_->tabBar();
        if (watchedBottomTabsTop) {
            if (event->type() == QEvent::MouseButtonPress) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton
                    && bottomTabsResizeHotzoneContains(session_.bottomTabs_, watchedWidget, mouseEvent->pos())) {
                    session_.bottomTabsResizeDragActive_ = true;
                    session_.bottomTabsResizeStartGlobalY_ = mouseEvent->globalPosition().toPoint().y();
                    session_.bottomTabsResizeStartHeight_ = session_.bottomTabs_->height();
                    session_.bottomTabs_->setCursor(Qt::SizeVerCursor);
                    event->accept();
                    return true;
                }
            } else if (event->type() == QEvent::MouseMove) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (session_.bottomTabsResizeDragActive_) {
                    const int deltaY =
                        mouseEvent->globalPosition().toPoint().y() - session_.bottomTabsResizeStartGlobalY_;
                    setBottomTabsHeight(session_.bottomTabsResizeStartHeight_ - deltaY);
                    event->accept();
                    return true;
                }
                if (bottomTabsResizeHotzoneContains(session_.bottomTabs_, watchedWidget, mouseEvent->pos())) {
                    session_.bottomTabs_->setCursor(Qt::SizeVerCursor);
                } else {
                    session_.bottomTabs_->unsetCursor();
                }
            } else if (event->type() == QEvent::MouseButtonRelease) {
                if (session_.bottomTabsResizeDragActive_) {
                    session_.bottomTabsResizeDragActive_ = false;
                    session_.bottomTabs_->unsetCursor();
                    event->accept();
                    return true;
                }
            } else if (event->type() == QEvent::Leave) {
                if (!session_.bottomTabsResizeDragActive_) {
                    session_.bottomTabs_->unsetCursor();
                }
            }
        }
    }
    if (session_.bottomTabs_ != nullptr && watched == session_.bottomTabs_->tabBar() && event->type() == QEvent::Wheel) {
        return true;
    }
    if (session_.aboutIconLabel_ != nullptr && watched == session_.aboutIconLabel_) {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                if (session_.invalidStarPreviewEasterEggEnabled_) {
                    session_.invalidStarPreviewAboutClickCount_ = 0;
                    session_.invalidStarPreviewAboutClickElapsed_.invalidate();
                    this->setInvalidStarPreviewEasterEggEnabled(false);
                } else {
                    if (!session_.invalidStarPreviewAboutClickElapsed_.isValid()
                        || session_.invalidStarPreviewAboutClickElapsed_.elapsed() > kInvalidStarPreviewAboutClickWindowMs) {
                        session_.invalidStarPreviewAboutClickCount_ = 0;
                    }
                    ++session_.invalidStarPreviewAboutClickCount_;
                    if (session_.invalidStarPreviewAboutClickElapsed_.isValid()) {
                        session_.invalidStarPreviewAboutClickElapsed_.restart();
                    } else {
                        session_.invalidStarPreviewAboutClickElapsed_.start();
                    }
                    if (session_.invalidStarPreviewAboutClickCount_ >= 3) {
                        session_.invalidStarPreviewAboutClickCount_ = 0;
                        session_.invalidStarPreviewAboutClickElapsed_.invalidate();
                        this->setInvalidStarPreviewEasterEggEnabled(true);
                    }
                }
                return true;
            }
        }
    }
    if ((session_.errorList_ != nullptr && watched == session_.errorList_->viewport())
        || (session_.muriList_ != nullptr && watched == session_.muriList_->viewport())) {
        if (event->type() == QEvent::Resize
            || event->type() == QEvent::Show
            || event->type() == QEvent::LayoutRequest
            || event->type() == QEvent::PolishRequest) {
            session_.scheduleWrappedListRelayout(
                watched == (session_.errorList_ != nullptr ? session_.errorList_->viewport() : nullptr) ? session_.errorList_ : session_.muriList_
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
                && session_.previewSeekHeldArrowKey_ != 0
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && session_.previewSeekHeldArrowKey_ == keyEvent->key()) {
                session_.stopPreviewHeldSeek(keyEvent->key());
            }
            return session_.QObject::eventFilter(watched, event);
        }
    }
    QWindow* previewVisibleWindow = this->previewVisibleHostWindow();
    if (previewVisibleWindow != nullptr
        && !previewVisibleWindow->property("miacodeWindowSectionEventFilterInstalled").toBool()) {
        previewVisibleWindow->installEventFilter(&session_);
        previewVisibleWindow->setProperty("miacodeWindowSectionEventFilterInstalled", true);
    }
    const bool previewKeyScope =
        watched == session_.previewSlider_
        || watched == session_.previewFullscreenWindow_
        || watched == session_.previewFullscreenControlsWindow_
        || watched == session_.previewFullscreenButton_
        || (watched == previewVisibleWindow
            && (previewVisibleWindow != state_.rootWindow_
                || !state_.rootWindow_->property("sourceEditorFocused").toBool()));
    const bool previewMouseFocusScope =
        watched == session_.previewFullscreenWindow_
        || watched == previewVisibleWindow;
    const bool previewFullscreenOverlayScope =
        watched == session_.previewFullscreenWindow_
        || watched == session_.previewFullscreenControlsWindow_
        || watched == session_.previewControlCard_
        || watched == session_.previewSlider_
        || watched == session_.stopPreviewButton_
        || watched == session_.pausePreviewButton_
        || watched == session_.previewSpeedButton_
        || watched == session_.previewFullscreenButton_
        || watched == previewVisibleWindow;
    if (previewMouseFocusScope
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel)) {
        this->focusPreviewInteractionTarget(watched, Qt::MouseFocusReason);
    }
    if (session_.previewFullscreenActive_ && previewFullscreenOverlayScope) {
        if (event->type() == QEvent::MouseMove
            || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::Wheel) {
            const QPoint globalCursorPos = QCursor::pos();
            if (session_.shouldRevealPreviewFullscreenControls(globalCursorPos)) {
                session_.showPreviewFullscreenControls(event->type() != QEvent::MouseButtonPress);
            } else if (session_.previewFullscreenControlsVisible_) {
                session_.schedulePreviewFullscreenControlsAutoHide();
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_F11) {
                session_.togglePreviewFullscreen();
                return true;
            }
            if (!keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_Escape) {
                session_.exitPreviewFullscreen();
                return true;
            }
        }
    }
    if (session_.previewFullscreenWindow_ != nullptr && watched == session_.previewFullscreenWindow_) {
        if (event->type() == QEvent::Close) {
            session_.exitPreviewFullscreen();
            event->ignore();
            return true;
        }
        if (session_.previewFullscreenActive_
            && (event->type() == QEvent::Move
                || event->type() == QEvent::Resize
                || event->type() == QEvent::Show
                || event->type() == QEvent::WindowStateChange)) {
            session_.updatePreviewFullscreenOverlayGeometry();
        }
    }
    if (session_.previewSlider_ != nullptr && watched == session_.previewSlider_) {
        if ((event->type() == QEvent::Wheel || event->type() == QEvent::MouseButtonPress)
            && session_.playing_
            && session_.previewFollowEnabled_
            && session_.previewViewportLockEnabled_) {
            session_.pauseQtPreviewPlaybackExact();
            session_.updatePauseButtonAppearance();
            QTimer::singleShot(0, &session_, [this]() {
                if (session_.previewFollowEnabled_ && session_.previewViewportLockEnabled_) {
                    session_.syncEditorCursorToPreviewSecond(qMax(0.0, session_.pauseSecond_), true, false);
                }
            });
        }
        if (event->type() == QEvent::Wheel) {
            session_.stopPreviewHeldSeek();
            if (session_.handlePreviewSeekWheel(static_cast<QWheelEvent*>(event))) {
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            session_.stopPreviewHeldSeek();
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QStyleOptionSlider option;
                option.initFrom(session_.previewSlider_);
                option.subControls = QStyle::SC_SliderHandle;
                option.orientation = session_.previewSlider_->orientation();
                option.minimum = session_.previewSlider_->minimum();
                option.maximum = session_.previewSlider_->maximum();
                option.sliderPosition = session_.previewSlider_->sliderPosition();
                option.sliderValue = session_.previewSlider_->value();
                option.upsideDown = false;
                const QRect handleRect = session_.previewSlider_->style()->subControlRect(
                    QStyle::CC_Slider,
                    &option,
                    QStyle::SC_SliderHandle,
                    session_.previewSlider_
                );
                if (!handleRect.contains(mouseEvent->pos())) {
                    const int value = QStyle::sliderValueFromPosition(
                        session_.previewSlider_->minimum(),
                        session_.previewSlider_->maximum(),
                        mouseEvent->pos().x(),
                        qMax(1, session_.previewSlider_->width()),
                        false
                    );
                    session_.previewSlider_->setFocus(Qt::MouseFocusReason);
                    session_.previewSlider_->setValue(value);
                    session_.showPreviewSliderTimeHint(value);
                    const double clickSecond = static_cast<double>(value) / 1000.0;
                    // Negative-time intro region: a click in [-duration, 0) shows
                    // a static intro frame instead of a chart seek.
                    if (!session_.handleExportIntroSliderSeek(clickSecond)) {
                        session_.seekPreviewDiscreteToSecond(clickSecond, true);
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
                session_.togglePreviewFullscreen();
                return true;
            }
            if (session_.previewFullscreenActive_
                && !keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_Escape) {
                session_.exitPreviewFullscreen();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Space
                && keyEvent->modifiers() == Qt::NoModifier
                && !keyEvent->isAutoRepeat()) {
                session_.onTogglePreviewPause();
                return true;
            }
            if (session_.previewSlider_ == nullptr) {
                return session_.QObject::eventFilter(watched, event);
            }
            int direction = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                direction = -1;
            } else if (keyEvent->key() == Qt::Key_Right) {
                direction = 1;
            }
            if (direction != 0) {
                if (keyEvent->modifiers() != Qt::NoModifier) {
                    return session_.QObject::eventFilter(watched, event);
                }
                if (keyEvent->isAutoRepeat()) {
                    return true;
                }
                session_.beginPreviewHeldSeek(direction, keyEvent->key());
                session_.stepPreviewBySeconds(
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
            if (session_.previewSlider_ == nullptr) {
                return session_.QObject::eventFilter(watched, event);
            }
            if (!keyEvent->isAutoRepeat()
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && session_.previewSeekHeldArrowKey_ == keyEvent->key()) {
                session_.stopPreviewHeldSeek(keyEvent->key());
                return true;
            }
        }
    }
    return session_.QObject::eventFilter(watched, event);
}

QTextEdit* miacode::runtime::ShellHost::activeFindTarget() const
{
    QWidget* focus = QApplication::focusWidget();
    if (focus != nullptr) {
        if (session_.metadataExtraEdit_ != nullptr && (focus == session_.metadataExtraEdit_ || session_.metadataExtraEdit_->isAncestorOf(focus))) {
            return session_.metadataExtraEdit_;
        }
    }

    // The chart body's find/replace is the QML bar's; only the metadata extra
    // fields still have a QTextEdit on this side.
    return session_.metadataExtraEdit_;
}

bool miacode::runtime::ShellHost::runFindInEditor(bool backward)
{
    // The find field (editorFindEdit_) never exists on this side of the QML
    // migration, so there is never a pattern to search for.
    Q_UNUSED(backward);
    return false;
}

void miacode::runtime::ShellHost::updateEditorFindBarGeometry()
{
    // editorFindBar_ / editorFindGeometryHost_ never exist on this side of
    // the QML migration.
}

void miacode::runtime::ShellHost::applyFindOverlayInset()
{
}

void miacode::runtime::ShellHost::hideFindReplaceBar()
{
    // editorFindBar_ never exists on this side of the QML migration.
}

void miacode::runtime::ShellHost::onToggleFindReplace()
{
    MC_OP("miacode::runtime::ShellHost::onToggleFindReplace");
    // editorFindBar_ never exists on this side of the QML migration.
    _mc_op_.fail(QStringLiteral("editorFindBar_ null"));
}

void miacode::runtime::ShellHost::onFindNext()
{
    MC_OP("miacode::runtime::ShellHost::onFindNext");
    this->runFindInEditor(false);
}

void miacode::runtime::ShellHost::onFindPrevious()
{
    MC_OP("miacode::runtime::ShellHost::onFindPrevious");
    this->runFindInEditor(true);
}

void miacode::runtime::ShellHost::onReplaceOne()
{
    MC_OP("miacode::runtime::ShellHost::onReplaceOne");
    // The find/replace fields (editorFindEdit_/editorReplaceEdit_) never
    // exist on this side of the QML migration.
    _mc_op_.fail(QStringLiteral("find/replace UI not initialised"));
}

void miacode::runtime::ShellHost::onReplaceAll()
{
    MC_OP("miacode::runtime::ShellHost::onReplaceAll");
    // The find/replace fields (editorFindEdit_/editorReplaceEdit_) never
    // exist on this side of the QML migration.
    _mc_op_.fail(QStringLiteral("find/replace UI not initialised"));
}

void miacode::runtime::ShellHost::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event);
    session_.updatePreviewWorkspaceLayout();
    session_.updatePreviewPlaybackRateToastGeometry();
    session_.updateEditorHeaderLayoutMode();
    this->updateEditorFindBarGeometry();
    this->applyFindOverlayInset();
    session_.relayoutWrappedListRows(session_.errorList_);
    session_.relayoutWrappedListRows(session_.muriList_);
}

void miacode::runtime::ShellHost::moveEvent(QMoveEvent* event)
{
    Q_UNUSED(event);
    session_.refreshPreviewFrameRateTimers();
}

void miacode::runtime::ShellHost::showEvent(QShowEvent* event)
{
    Q_UNUSED(event);
    this->updateBottomTabsDeviceHeight();
    session_.refreshPreviewFrameRateTimers();
    session_.updatePreviewPlaybackRateToastGeometry();
}

void miacode::runtime::ShellHost::hideEvent(QHideEvent* event)
{
    Q_UNUSED(event);
}
bool miacode::runtime::ShellHost::event(QEvent* event)
{
    return session_.QObject::event(event);
}

void miacode::runtime::ShellHost::changeEvent(QEvent* event)
{
    Q_UNUSED(event);
}
