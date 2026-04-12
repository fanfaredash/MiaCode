#include "../../MainWindow.h"
#include "../../MainWindowShared.h"

#include "PlainCodeEditor.h"
#include "UiText.h"
#include "common/PreviewInteractionConfig.h"

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

}  // namespace

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (editorFindGeometryHost_ != nullptr
        && watched == editorFindGeometryHost_
        && event != nullptr
        && event->type() == QEvent::Resize) {
        updateEditorFindBarGeometry();
        applyFindOverlayInset();
    }
    if (bottomTabs_ != nullptr && watched == bottomTabs_->tabBar() && event->type() == QEvent::Wheel) {
        return true;
    }
    if (exportVideoButton_ != nullptr && watched == exportVideoButton_) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::HoverEnter || event->type() == QEvent::MouseMove) {
            if (exportVideoHoverMenuTimer_ != nullptr && !QApplication::mouseButtons().testAnyFlag(Qt::AllButtons)) {
                exportVideoHoverMenuTimer_->start();
            }
        } else if (event->type() == QEvent::Leave
                   || event->type() == QEvent::MouseButtonPress
                   || event->type() == QEvent::Hide) {
            if (exportVideoHoverMenuTimer_ != nullptr) {
                exportVideoHoverMenuTimer_->stop();
            }
        }
    }
    if (aboutIconLabel_ != nullptr && watched == aboutIconLabel_) {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                if (invalidStarPreviewEasterEggEnabled_) {
                    invalidStarPreviewAboutClickCount_ = 0;
                    invalidStarPreviewAboutClickElapsed_.invalidate();
                    setInvalidStarPreviewEasterEggEnabled(false);
                } else {
                    if (!invalidStarPreviewAboutClickElapsed_.isValid()
                        || invalidStarPreviewAboutClickElapsed_.elapsed() > kInvalidStarPreviewAboutClickWindowMs) {
                        invalidStarPreviewAboutClickCount_ = 0;
                    }
                    ++invalidStarPreviewAboutClickCount_;
                    if (invalidStarPreviewAboutClickElapsed_.isValid()) {
                        invalidStarPreviewAboutClickElapsed_.restart();
                    } else {
                        invalidStarPreviewAboutClickElapsed_.start();
                    }
                    if (invalidStarPreviewAboutClickCount_ >= 3) {
                        invalidStarPreviewAboutClickCount_ = 0;
                        invalidStarPreviewAboutClickElapsed_.invalidate();
                        setInvalidStarPreviewEasterEggEnabled(true);
                    }
                }
                return true;
            }
        }
    }
    if (outlineList_ != nullptr && watched == outlineList_->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            QListWidgetItem* hoveredItem = outlineList_->itemAt(mouseEvent->pos());
            const bool showButton =
                hoveredItem != nullptr
                && hoveredItem == outlineList_->currentItem()
                && SimaiDocument::isDifficultyId(hoveredItem->data(Qt::UserRole + 1).toInt());
            updateDifficultyDeleteButton(showButton);
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::Wheel) {
            updateDifficultyDeleteButton(false);
        } else if (event->type() == QEvent::Resize && deleteDifficultyButton_ != nullptr && deleteDifficultyButton_->isVisible()) {
            updateDifficultyDeleteButton(true);
        }
    }
    if ((errorList_ != nullptr && watched == errorList_->viewport())
        || (muriList_ != nullptr && watched == muriList_->viewport())) {
        if (event->type() == QEvent::Resize
            || event->type() == QEvent::Show
            || event->type() == QEvent::LayoutRequest
            || event->type() == QEvent::PolishRequest) {
            scheduleWrappedListRelayout(
                watched == (errorList_ != nullptr ? errorList_->viewport() : nullptr) ? errorList_ : muriList_
            );
        }
    }
    const bool previewKeyScope =
        watched == previewSlider_
        || watched == previewCanvasContainer_
        || watched == previewCanvasFrame_
        || watched == previewPanel_
        || watched == previewFullscreenWindow_
        || watched == previewFullscreenHost_
        || watched == previewFullscreenControlsWindow_
        || watched == previewFullscreenButton_;
    const bool previewMouseFocusScope =
        watched == previewCanvasContainer_
        || watched == previewCanvasFrame_
        || watched == previewPanel_
        || watched == previewFullscreenWindow_
        || watched == previewFullscreenHost_;
    const bool previewFullscreenOverlayScope =
        watched == previewFullscreenWindow_
        || watched == previewFullscreenHost_
        || watched == previewFullscreenControlsWindow_
        || watched == previewFullscreenHintWindow_
        || watched == previewCanvasContainer_
        || watched == previewCanvasFrame_
        || watched == previewControlCard_
        || watched == previewSlider_
        || watched == stopPreviewButton_
        || watched == pausePreviewButton_
        || watched == previewSpeedButton_
        || watched == previewFullscreenButton_;
    if (previewMouseFocusScope
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel)) {
        if (QWidget* widget = qobject_cast<QWidget*>(watched);
            widget != nullptr && widget->focusPolicy() != Qt::NoFocus) {
            widget->setFocus(Qt::MouseFocusReason);
        }
    }
    if (previewFullscreenActive_ && previewFullscreenOverlayScope) {
        if (event->type() == QEvent::MouseMove
            || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::Wheel) {
            const QPoint globalCursorPos = QCursor::pos();
            if (shouldRevealPreviewFullscreenControls(globalCursorPos)) {
                showPreviewFullscreenControls(event->type() != QEvent::MouseButtonPress);
            } else if (previewFullscreenControlsVisible_) {
                schedulePreviewFullscreenControlsAutoHide();
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_F11) {
                togglePreviewFullscreen();
                return true;
            }
            if (!keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_Escape) {
                exitPreviewFullscreen();
                return true;
            }
        }
    }
    if (previewFullscreenWindow_ != nullptr && watched == previewFullscreenWindow_) {
        if (event->type() == QEvent::Close) {
            exitPreviewFullscreen();
            event->ignore();
            return true;
        }
        if (previewFullscreenActive_
            && (event->type() == QEvent::Move
                || event->type() == QEvent::Resize
                || event->type() == QEvent::Show
                || event->type() == QEvent::WindowStateChange)) {
            updatePreviewFullscreenOverlayGeometry();
        }
    }
    if (previewSlider_ != nullptr && watched == previewSlider_) {
        if (event->type() == QEvent::Wheel) {
            stopPreviewHeldSeek();
            if (handlePreviewSeekWheel(static_cast<QWheelEvent*>(event))) {
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            stopPreviewHeldSeek();
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QStyleOptionSlider option;
                option.initFrom(previewSlider_);
                option.subControls = QStyle::SC_SliderHandle;
                option.orientation = previewSlider_->orientation();
                option.minimum = previewSlider_->minimum();
                option.maximum = previewSlider_->maximum();
                option.sliderPosition = previewSlider_->sliderPosition();
                option.sliderValue = previewSlider_->value();
                option.upsideDown = false;
                const QRect handleRect = previewSlider_->style()->subControlRect(
                    QStyle::CC_Slider,
                    &option,
                    QStyle::SC_SliderHandle,
                    previewSlider_
                );
                if (!handleRect.contains(mouseEvent->pos())) {
                    const int value = QStyle::sliderValueFromPosition(
                        previewSlider_->minimum(),
                        previewSlider_->maximum(),
                        mouseEvent->pos().x(),
                        qMax(1, previewSlider_->width()),
                        false
                    );
                    previewSlider_->setFocus(Qt::MouseFocusReason);
                    previewSlider_->setValue(value);
                    showPreviewSliderTimeHint(value);
                    seekPreviewToSecond(static_cast<double>(value) / 1000.0, true);
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
                togglePreviewFullscreen();
                return true;
            }
            if (previewFullscreenActive_
                && !keyEvent->isAutoRepeat()
                && keyEvent->modifiers() == Qt::NoModifier
                && keyEvent->key() == Qt::Key_Escape) {
                exitPreviewFullscreen();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Space
                && keyEvent->modifiers() == Qt::NoModifier
                && !keyEvent->isAutoRepeat()) {
                onTogglePreviewPause();
                return true;
            }
            if (previewSlider_ == nullptr) {
                return QMainWindow::eventFilter(watched, event);
            }
            int direction = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                direction = -1;
            } else if (keyEvent->key() == Qt::Key_Right) {
                direction = 1;
            }
            if (direction != 0) {
                if (keyEvent->modifiers() != Qt::NoModifier) {
                    return QMainWindow::eventFilter(watched, event);
                }
                if (keyEvent->isAutoRepeat()) {
                    return true;
                }
                beginPreviewHeldSeek(direction, keyEvent->key());
                stepPreviewBySeconds(
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
            if (previewSlider_ == nullptr) {
                return QMainWindow::eventFilter(watched, event);
            }
            if (!keyEvent->isAutoRepeat()
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && previewSeekHeldArrowKey_ == keyEvent->key()) {
                stopPreviewHeldSeek(keyEvent->key());
                return true;
            }
        }
    }
    if (watched == editorFindEdit_ || watched == editorReplaceEdit_) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const bool ctrlOnly = (keyEvent->modifiers() & Qt::ControlModifier)
                && !(keyEvent->modifiers() & (Qt::AltModifier | Qt::MetaModifier));
            if ((keyEvent->matches(QKeySequence::Find))
                || (ctrlOnly && keyEvent->key() == Qt::Key_F)) {
                onToggleFindReplace();
                return true;
            }
        }
    }
    if (watched == editorViewport_ && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const bool ctrlLeftClick = mouseEvent->button() == Qt::LeftButton
            && (mouseEvent->modifiers() & Qt::ControlModifier);
        if (ctrlLeftClick) {
            editorCtrlLeftJumpPending_ = true;
            editorCtrlLeftJumpDragged_ = false;
            editorCtrlLeftJumpPressPos_ = mouseEvent->pos();
        } else if (mouseEvent->button() == Qt::LeftButton) {
            editorCtrlLeftJumpPending_ = false;
            editorCtrlLeftJumpDragged_ = false;
        }
        if (mouseEvent->button() == Qt::LeftButton && !qtPreviewPlaying_ && !ctrlLeftClick) {
            QTimer::singleShot(0, this, [this]() {
                syncTimelineToEditorCursor(true);
            });
        }
    }
    if (watched == editorViewport_ && event->type() == QEvent::MouseMove && editorCtrlLeftJumpPending_) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->buttons().testFlag(Qt::LeftButton)
            && (mouseEvent->pos() - editorCtrlLeftJumpPressPos_).manhattanLength() >= QApplication::startDragDistance()) {
            editorCtrlLeftJumpDragged_ = true;
        }
    }
    if (watched == editorViewport_ && event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton && editorCtrlLeftJumpPending_) {
            const bool shouldJump = !editorCtrlLeftJumpDragged_
                && (mouseEvent->modifiers() & Qt::ControlModifier);
            const QPoint releasePos = mouseEvent->pos();
            editorCtrlLeftJumpPending_ = false;
            editorCtrlLeftJumpDragged_ = false;
            if (shouldJump) {
                QTimer::singleShot(0, this, [this, releasePos]() {
                    auto* editor = qobject_cast<PlainCodeEditor*>(editorWidget_);
                    if (editor == nullptr) {
                        return;
                    }
                    const QTextCursor cursor = editor->cursorForPosition(releasePos);
                    const int line = cursor.blockNumber() + 1;
                    const int col = cursor.positionInBlock() + 1;
                    const double second = timelineSecondForCursor(line, col);
                    if (qtPreviewPlaying_) {
                        stopQtPreviewPlayback(true);
                    }
                    seekPreviewToSecond(second, false);
                    if (timelineView_ != nullptr) {
                        timelineView_->setCursorSeconds(second, false);
                        timelineView_->focusCursor(true);
                    }
                });
            }
        }
    }
    if (watched == editorViewport_ && event->type() == QEvent::FocusIn && !qtPreviewPlaying_) {
        QTimer::singleShot(0, this, [this]() {
            syncTimelineToEditorCursor(true);
        });
    }
    return QMainWindow::eventFilter(watched, event);
}

QTextEdit* MainWindow::activeFindTarget() const
{
    auto* chartEditor = qobject_cast<QTextEdit*>(editorWidget_);
    QWidget* focus = QApplication::focusWidget();
    if (focus != nullptr) {
        if (chartEditor != nullptr && (focus == chartEditor || chartEditor->isAncestorOf(focus))) {
            return chartEditor;
        }
        if (metadataExtraEdit_ != nullptr && (focus == metadataExtraEdit_ || metadataExtraEdit_->isAncestorOf(focus))) {
            return metadataExtraEdit_;
        }
    }

    if (editorStack_ != nullptr && editorStack_->currentWidget() == chartPage_ && chartEditor != nullptr) {
        return chartEditor;
    }
    if (editorStack_ != nullptr && editorStack_->currentWidget() == metadataPage_ && metadataExtraEdit_ != nullptr) {
        return metadataExtraEdit_;
    }
    return chartEditor != nullptr ? chartEditor : metadataExtraEdit_;
}

bool MainWindow::runFindInEditor(bool backward)
{
    QTextEdit* target = activeFindTarget();
    if (target == nullptr || editorFindEdit_ == nullptr) {
        return false;
    }
    const QString pattern = editorFindEdit_->text();
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

void MainWindow::updateEditorFindBarGeometry()
{
    QWidget* geometryHost = editorFindGeometryHost_ != nullptr ? editorFindGeometryHost_ : editorStack_;
    if (editorFindBar_ == nullptr || geometryHost == nullptr) {
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
    const int height = editorFindBar_->sizeHint().height();
    editorFindBar_->setGeometry(x, y, width, height);
    editorFindBar_->raise();
}

void MainWindow::applyFindOverlayInset()
{
    const int topInset =
        (editorFindBar_ != nullptr && editorFindBar_->isVisible())
        ? editorFindBar_->height() + kEditorFindBarOverlayGap
        : 0;
    if (auto* plainEditor = qobject_cast<PlainCodeEditor*>(editorWidget_); plainEditor != nullptr) {
        plainEditor->setTopOverlayInsetPixels(topInset);
    }
}

void MainWindow::hideFindReplaceBar()
{
    if (editorFindBar_ == nullptr || !editorFindBar_->isVisible()) {
        return;
    }
    editorFindBar_->hide();
    applyFindOverlayInset();
    if (QTextEdit* target = activeFindTarget(); target != nullptr) {
        target->setFocus();
    }
}

void MainWindow::onToggleFindReplace()
{
    if (editorFindBar_ == nullptr) {
        return;
    }
    if (editorFindBar_->isVisible()) {
        hideFindReplaceBar();
        return;
    }

    updateEditorFindBarGeometry();
    editorFindBar_->show();
    editorFindBar_->raise();
    applyFindOverlayInset();
    QTextEdit* target = activeFindTarget();
    if (target != nullptr && editorFindEdit_ != nullptr && editorFindEdit_->text().isEmpty()) {
        const QTextCursor cursor = target->textCursor();
        const QString selected = cursor.selectedText();
        if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) {
            editorFindEdit_->setText(selected);
        }
    }
    if (editorFindEdit_ != nullptr) {
        editorFindEdit_->setFocus();
        editorFindEdit_->selectAll();
    }
}

void MainWindow::onFindNext()
{
    runFindInEditor(false);
}

void MainWindow::onFindPrevious()
{
    runFindInEditor(true);
}

void MainWindow::onReplaceOne()
{
    QTextEdit* target = activeFindTarget();
    if (target == nullptr || editorFindEdit_ == nullptr || editorReplaceEdit_ == nullptr) {
        return;
    }
    const QString findText = editorFindEdit_->text();
    if (findText.isEmpty()) {
        return;
    }

    QTextCursor cursor = target->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == findText) {
        cursor.insertText(editorReplaceEdit_->text());
        target->setTextCursor(cursor);
    }
    runFindInEditor(false);
}

void MainWindow::onReplaceAll()
{
    QTextEdit* target = activeFindTarget();
    if (target == nullptr || editorFindEdit_ == nullptr || editorReplaceEdit_ == nullptr) {
        return;
    }
    const QString findText = editorFindEdit_->text();
    if (findText.isEmpty()) {
        return;
    }

    QTextDocument* doc = target->document();
    QTextCursor editCursor(doc);
    editCursor.beginEditBlock();
    const QString replaceText = editorReplaceEdit_->text();
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
    statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("已替换 %1 处。").arg(replacedCount)
            : QStringLiteral("Replaced %1 occurrence(s).").arg(replacedCount)
    );
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (!outlineDockCollapsed_ && outlineDock_ != nullptr) {
        outlineDockExpandedWidth_ = qMax(120, outlineDock_->width());
    }
    updatePreviewWorkspaceLayout();
    updateEditorHeaderLayoutMode();
    updateEditorFindBarGeometry();
    applyFindOverlayInset();
    relayoutWrappedListRows(errorList_);
    relayoutWrappedListRows(muriList_);
    logWindowGeometryDebug(
        "resize_event",
        QString("old=%1x%2 new=%3x%4")
            .arg(event->oldSize().width())
            .arg(event->oldSize().height())
            .arg(event->size().width())
            .arg(event->size().height())
    );
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    refreshPreviewFrameRateTimers();
    logWindowGeometryDebug(
        "move_event",
        QString("old=(%1,%2) new=(%3,%4)")
            .arg(event->oldPos().x())
            .arg(event->oldPos().y())
            .arg(event->pos().x())
            .arg(event->pos().y())
    );
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    updateBottomTabsDeviceHeight();
    refreshPreviewFrameRateTimers();
    applySystemWindowBackdrop();
    logWindowGeometryDebug("show_event");
}

void MainWindow::hideEvent(QHideEvent* event)
{
    QMainWindow::hideEvent(event);
    logWindowGeometryDebug("hide_event");
}

bool MainWindow::event(QEvent* event)
{
    return QMainWindow::event(event);
}

void MainWindow::changeEvent(QEvent* event)
{
    const QEvent::Type type = event != nullptr ? event->type() : QEvent::None;
    QMainWindow::changeEvent(event);
    if (type == QEvent::WindowStateChange) {
        auto* stateEvent = static_cast<QWindowStateChangeEvent*>(event);
        logWindowGeometryDebug(
            "window_state_change",
            QString("old_state=%1 new_state=%2")
                .arg(formatWindowStateFlags(stateEvent != nullptr ? stateEvent->oldState() : Qt::WindowNoState))
                .arg(formatWindowStateFlags(windowState()))
        );
        const bool wasMinimized =
            stateEvent != nullptr && stateEvent->oldState().testFlag(Qt::WindowMinimized);
        const bool isNowMinimized = windowState().testFlag(Qt::WindowMinimized);
        if (!isNowMinimized && wasMinimized) {
            refreshPreviewFrameRateTimers();
            applySystemWindowBackdrop();
        }
    } else if (type == QEvent::ScreenChangeInternal
        || type == QEvent::DevicePixelRatioChange
        || type == QEvent::FontChange
        || type == QEvent::StyleChange
        || type == QEvent::PaletteChange
        || type == QEvent::ThemeChange
        || type == QEvent::ApplicationPaletteChange) {
        updateBottomTabsDeviceHeight();
        refreshPreviewFrameRateTimers();
        applySystemWindowBackdrop();
    } else if (type == QEvent::ActivationChange) {
        applySystemWindowBackdrop();
        const bool active = isActiveWindow();
        logWindowGeometryDebug(
            "activation_change",
            QString("is_active=%1 spontaneous=%2")
                .arg(active ? 1 : 0)
                .arg(event != nullptr && event->spontaneous() ? 1 : 0)
        );
    } else if (type == QEvent::ZOrderChange) {
        logWindowGeometryDebug("zorder_change");
    }
}

