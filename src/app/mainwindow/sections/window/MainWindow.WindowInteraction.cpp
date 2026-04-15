#include "MainWindow.WindowSection.h"
#include "../../MainWindowShared.h"

#include "PlainCodeEditor.h"
#include "UiText.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"

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

bool widgetMatchesOrDescendsFrom(QWidget* widget, QWidget* root)
{
    return widget != nullptr && root != nullptr && (widget == root || root->isAncestorOf(widget));
}

}  // namespace

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
    this->logFocusDebug(QStringLiteral("app_focus_changed"), old, now);
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

    if (now != nullptr && (now == &owner_ || owner_.isAncestorOf(now))) {
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
        return;
    }
    if (state == Qt::ApplicationInactive || state == Qt::ApplicationHidden) {
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
        auto* widget = qobject_cast<QWidget*>(watched);
        auto* focusEvent = static_cast<QFocusEvent*>(event);
        this->logFocusDebug(
            event->type() == QEvent::FocusIn ? QStringLiteral("watched_focus_in") : QStringLiteral("watched_focus_out"),
            widget,
            QApplication::focusWidget(),
            QStringLiteral("watched=%1 reason=%2 spontaneous=%3")
                .arg(this->describeFocusWidget(widget))
                .arg(this->formatFocusReason(focusEvent != nullptr ? focusEvent->reason() : Qt::NoFocusReason))
                .arg(event->spontaneous() ? 1 : 0)
        );
    }

    if (event != nullptr && event->type() == QEvent::ToolTip) {
        auto* widget = qobject_cast<QWidget*>(watched);
        const bool allowPreviewTooltip =
            widgetMatchesOrDescendsFrom(widget, owner_.previewPanel_)
            || widgetMatchesOrDescendsFrom(widget, owner_.previewLeftColumn_)
            || widgetMatchesOrDescendsFrom(widget, owner_.previewCanvasContainer_)
            || widgetMatchesOrDescendsFrom(widget, owner_.previewCanvasFrame_)
            || widgetMatchesOrDescendsFrom(widget, owner_.previewControlCard_)
            || widgetMatchesOrDescendsFrom(widget, owner_.previewStatsCard_)
            || widgetMatchesOrDescendsFrom(widget, owner_.previewFullscreenWindow_)
            || widgetMatchesOrDescendsFrom(widget, owner_.previewFullscreenHost_)
            || widgetMatchesOrDescendsFrom(widget, owner_.previewFullscreenControlsWindow_)
            || widgetMatchesOrDescendsFrom(widget, owner_.previewFullscreenHintWindow_);
        if (widget != nullptr && !allowPreviewTooltip) {
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
    if (owner_.bottomTabs_ != nullptr && watched == owner_.bottomTabs_->tabBar() && event->type() == QEvent::Wheel) {
        return true;
    }
    if (owner_.exportVideoButton_ != nullptr && watched == owner_.exportVideoButton_) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter || event->type() == QEvent::MouseMove) {
            if (owner_.exportVideoHoverMenuTimer_ != nullptr && !QApplication::mouseButtons().testAnyFlag(Qt::AllButtons)) {
                owner_.exportVideoHoverMenuTimer_->start();
            }
        } else if (event->type() == QEvent::Leave
                   || event->type() == QEvent::MouseButtonPress
                   || event->type() == QEvent::Hide) {
            if (owner_.exportVideoHoverMenuTimer_ != nullptr) {
                owner_.exportVideoHoverMenuTimer_->stop();
            }
        }
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
    if (owner_.outlineList_ != nullptr && watched == owner_.outlineList_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            QListWidgetItem* hoveredItem = owner_.outlineList_->itemAt(mouseEvent->pos());
            const bool showButton =
                hoveredItem != nullptr
                && hoveredItem == owner_.outlineList_->currentItem()
                && SimaiDocument::isDifficultyId(hoveredItem->data(Qt::UserRole + 1).toInt());
            owner_.updateDifficultyDeleteButton(showButton);
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::Wheel) {
            owner_.updateDifficultyDeleteButton(false);
        } else if (event->type() == QEvent::Resize && owner_.deleteDifficultyButton_ != nullptr && owner_.deleteDifficultyButton_->isVisible()) {
            owner_.updateDifficultyDeleteButton(true);
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
                    owner_.seekPreviewToSecond(static_cast<double>(value) / 1000.0, true);
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
            owner_.editorCtrlLeftJumpPending_ = true;
            owner_.editorCtrlLeftJumpDragged_ = false;
            owner_.editorCtrlLeftJumpPressPos_ = mouseEvent->pos();
        } else if (mouseEvent->button() == Qt::LeftButton) {
            owner_.editorCtrlLeftJumpPending_ = false;
            owner_.editorCtrlLeftJumpDragged_ = false;
        }
        if (mouseEvent->button() == Qt::LeftButton && !owner_.qtPreviewPlaying_ && !ctrlLeftClick) {
            QTimer::singleShot(0, &owner_, [this]() {
                owner_.scheduleDeferredEditorUiUpdate(false, false, true, true, false, 0.0, false);
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
            const bool shouldJump = !owner_.editorCtrlLeftJumpDragged_
                && (mouseEvent->modifiers() & Qt::ControlModifier);
            const QPoint releasePos = mouseEvent->pos();
            owner_.editorCtrlLeftJumpPending_ = false;
            owner_.editorCtrlLeftJumpDragged_ = false;
            if (shouldJump) {
                QTimer::singleShot(0, &owner_, [this, releasePos]() {
                    auto* editor = qobject_cast<PlainCodeEditor*>(owner_.editorWidget_);
                    if (editor == nullptr) {
                        return;
                    }
                    const QTextCursor cursor = editor->cursorForPosition(releasePos);
                    const int line = cursor.blockNumber() + 1;
                    const int col = cursor.positionInBlock() + 1;
                    const double second = owner_.timelineSecondForCursor(line, col);
                    if (owner_.qtPreviewPlaying_) {
                        owner_.stopQtPreviewPlayback(true);
                    }
                    owner_.seekPreviewToSecond(second, false);
                    if (owner_.timelineView_ != nullptr) {
                        owner_.timelineView_->setCursorSeconds(second, true);
                        owner_.timelineView_->focusCursor(false);
                    }
                });
            }
        }
    }
    if (watched == owner_.editorViewport_ && event->type() == QEvent::FocusIn && !owner_.qtPreviewPlaying_) {
        QTimer::singleShot(0, &owner_, [this]() {
            owner_.scheduleDeferredEditorUiUpdate(false, false, true, true, false, 0.0, false);
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
    if (owner_.editorFindBar_ == nullptr) {
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
    this->runFindInEditor(false);
}

void MainWindow::WindowSection::onFindPrevious()
{
    this->runFindInEditor(true);
}

void MainWindow::WindowSection::onReplaceOne()
{
    QTextEdit* target = this->activeFindTarget();
    if (target == nullptr || owner_.editorFindEdit_ == nullptr || owner_.editorReplaceEdit_ == nullptr) {
        return;
    }
    const QString findText = owner_.editorFindEdit_->text();
    if (findText.isEmpty()) {
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
    QTextEdit* target = this->activeFindTarget();
    if (target == nullptr || owner_.editorFindEdit_ == nullptr || owner_.editorReplaceEdit_ == nullptr) {
        return;
    }
    const QString findText = owner_.editorFindEdit_->text();
    if (findText.isEmpty()) {
        return;
    }

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

