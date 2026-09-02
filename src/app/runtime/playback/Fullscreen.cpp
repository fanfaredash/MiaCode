#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/Session.h"
#include "runtime/Shared.h"
#include "runtime/shell/ShellHost.h"

#include "BracketScopeHighlighter.h"
#include "DialogLocalization.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "common/ChartAssetPaths.h"
#include "common/ContentDurationConfig.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewInteractionConfig.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "timeline/quick/TimelineQuickStateBridge.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

using namespace miacode::runtime::shared;

void miacode::runtime::PlaybackCoordinator::togglePreviewFullscreen()
{
    if (state_.previewFullscreenActive_) {
        exitPreviewFullscreen();
        return;
    }
    enterPreviewFullscreen();
}

void miacode::runtime::PlaybackCoordinator::enterPreviewFullscreen()
{
    if (state_.previewFullscreenActive_) {
        return;
    }
    state_.previewFullscreenActive_ = true;
    state_.previewFullscreenControlsVisible_ = false;
    state_.previewFullscreenCursorTrackingInitialized_ = false;
    if (state_.scene_ != nullptr) {
        state_.scene_->setSuppressObjectStatsHud(true);
    }
    refreshQuickShellPreviewCompositeSurfaceState(state_, owner_);
    updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
}

void miacode::runtime::PlaybackCoordinator::exitPreviewFullscreen()
{
    if (!state_.previewFullscreenActive_) {
        return;
    }
    state_.previewFullscreenActive_ = false;
    state_.previewFullscreenControlsVisible_ = false;
    state_.previewFullscreenCursorTrackingInitialized_ = false;
    if (state_.scene_ != nullptr) {
        state_.scene_->setSuppressObjectStatsHud(false);
    }
    refreshQuickShellPreviewCompositeSurfaceState(state_, owner_);
    updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
}

void miacode::runtime::PlaybackCoordinator::updatePreviewFullscreenButtonAppearance()
{
    if (ui_.previewFullscreenButton_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(ui_.previewFullscreenButton_);
    const QColor iconColor =
        state_.previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : UiTheme::colors().iconPrimary;
    ui_.previewFullscreenButton_->setChecked(state_.previewFullscreenActive_);
    ui_.previewFullscreenButton_->setText(QString());
    ui_.previewFullscreenButton_->setIcon(
        state_.previewFullscreenActive_ ? makePreviewExitFullscreenIcon(iconColor) : makePreviewEnterFullscreenIcon(iconColor)
    );
    ui_.previewFullscreenButton_->setToolTip(QString());
}

bool miacode::runtime::PlaybackCoordinator::shouldRevealPreviewFullscreenControls(const QPoint& globalCursorPos) const
{
    if (!state_.previewFullscreenActive_ || ui_.previewFullscreenWindow_ == nullptr) {
        return false;
    }

    const QRect windowGlobalRect(
        ui_.previewFullscreenWindow_->mapToGlobal(QPoint(0, 0)),
        ui_.previewFullscreenWindow_->size()
    );
    if (!windowGlobalRect.contains(globalCursorPos)) {
        return false;
    }

    if (ui_.previewFullscreenControlsWindow_ != nullptr
        && ui_.previewFullscreenControlsWindow_->isVisible()
        && ui_.previewFullscreenControlsWindow_->geometry().contains(globalCursorPos)) {
        return true;
    }

    const int controlsHeight =
        ui_.previewControlCard_ != nullptr
            ? qMax(ui_.previewControlCard_->minimumSizeHint().height(), ui_.previewControlCard_->sizeHint().height())
            : 0;
    const int revealHotzoneHeight = qMin(
        windowGlobalRect.height(),
        qMax(kPreviewFullscreenControlsRevealHotzoneHeight, controlsHeight + kPreviewFullscreenOverlayBottomMargin)
    );
    return globalCursorPos.y() >= windowGlobalRect.bottom() - revealHotzoneHeight;
}

QRect miacode::runtime::PlaybackCoordinator::previewFullscreenControlCardRect(bool visible) const
{
    if (ui_.previewFullscreenWindow_ == nullptr || ui_.previewControlCard_ == nullptr) {
        return QRect();
    }
    const QRect windowRect = ui_.previewFullscreenWindow_->contentsRect();
    if (windowRect.width() <= 0 || windowRect.height() <= 0) {
        return QRect();
    }
    const QPoint globalTopLeft = ui_.previewFullscreenWindow_->mapToGlobal(windowRect.topLeft());

    const int horizontalMargin = qMin(kPreviewFullscreenOverlaySideMargin, qMax(12, windowRect.width() / 20));
    const int availableWidth = qMax(0, windowRect.width() - horizontalMargin * 2);
    if (availableWidth <= 0) {
        return QRect();
    }

    const QSize preferredSize = ui_.previewControlCard_->sizeHint().expandedTo(ui_.previewControlCard_->minimumSizeHint());
    const int cardWidth = qMax(
        ui_.previewControlCard_->minimumSizeHint().width(),
        qMin(availableWidth, kPreviewFullscreenOverlayMaxWidth)
    );
    const int cardHeight = qMax(preferredSize.height(), ui_.previewControlCard_->minimumSizeHint().height());
    const int cardX = globalTopLeft.x() + qMax(0, (windowRect.width() - cardWidth) / 2);
    const int visibleY = globalTopLeft.y() + windowRect.height() - cardHeight - kPreviewFullscreenOverlayBottomMargin;
    const int hiddenY = globalTopLeft.y() + windowRect.height() + kPreviewFullscreenOverlayHideOffset;
    return QRect(cardX, visible ? visibleY : hiddenY, cardWidth, cardHeight);
}

void miacode::runtime::PlaybackCoordinator::showPreviewFullscreenControls(bool animate)
{
    if (!state_.previewFullscreenActive_
        || ui_.previewFullscreenWindow_ == nullptr
        || ui_.previewFullscreenControlsWindow_ == nullptr
        || ui_.previewControlCard_ == nullptr
        || ui_.previewControlCard_->parentWidget() != ui_.previewFullscreenControlsWindow_) {
        return;
    }

    const QRect targetRect = previewFullscreenControlCardRect(true);
    if (!targetRect.isValid()) {
        return;
    }

    if (ui_.previewFullscreenControlsAnimation_ != nullptr) {
        ui_.previewFullscreenControlsAnimation_->stop();
    }
    if (ui_.previewFullscreenControlsOpacityAnimation_ != nullptr) {
        ui_.previewFullscreenControlsOpacityAnimation_->stop();
    }

    QRect currentRect = ui_.previewFullscreenControlsWindow_->geometry();
    if (!currentRect.isValid()) {
        currentRect = previewFullscreenControlCardRect(false);
    }
    if (!ui_.previewFullscreenControlsWindow_->isVisible()) {
        ui_.previewFullscreenControlsWindow_->setGeometry(currentRect);
        ui_.previewFullscreenControlsWindow_->setWindowOpacity(0.0);
    }

    ui_.previewFullscreenControlsWindow_->show();
    ui_.previewFullscreenControlsWindow_->raise();
    ui_.previewControlCard_->show();

    const qreal currentOpacity = ui_.previewFullscreenControlsWindow_->windowOpacity();
    if (!animate || !currentRect.isValid() || currentRect == targetRect) {
        ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
        ui_.previewFullscreenControlsWindow_->setWindowOpacity(1.0);
    } else {
        if (ui_.previewFullscreenControlsAnimation_ != nullptr) {
            ui_.previewFullscreenControlsAnimation_->setStartValue(currentRect);
            ui_.previewFullscreenControlsAnimation_->setEndValue(targetRect);
            ui_.previewFullscreenControlsAnimation_->start();
        } else {
            ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
        }
        if (ui_.previewFullscreenControlsOpacityAnimation_ != nullptr) {
            ui_.previewFullscreenControlsOpacityAnimation_->setStartValue(currentOpacity);
            ui_.previewFullscreenControlsOpacityAnimation_->setEndValue(1.0);
            ui_.previewFullscreenControlsOpacityAnimation_->start();
        } else {
            ui_.previewFullscreenControlsWindow_->setWindowOpacity(1.0);
        }
    }

    state_.previewFullscreenControlsVisible_ = true;
    schedulePreviewFullscreenControlsAutoHide();
}

void miacode::runtime::PlaybackCoordinator::hidePreviewFullscreenControls(bool animate)
{
    if (!state_.previewFullscreenActive_
        || ui_.previewFullscreenWindow_ == nullptr
        || ui_.previewFullscreenControlsWindow_ == nullptr
        || ui_.previewControlCard_ == nullptr
        || ui_.previewControlCard_->parentWidget() != ui_.previewFullscreenControlsWindow_) {
        return;
    }

    const bool pointerOverControls =
        ui_.previewControlCard_->underMouse()
        || (ui_.previewSlider_ != nullptr && ui_.previewSlider_->underMouse())
        || (ui_.stopPreviewButton_ != nullptr && ui_.stopPreviewButton_->underMouse())
        || (ui_.pausePreviewButton_ != nullptr && ui_.pausePreviewButton_->underMouse())
        || (ui_.previewSpeedButton_ != nullptr && ui_.previewSpeedButton_->underMouse())
        || (ui_.previewFullscreenButton_ != nullptr && ui_.previewFullscreenButton_->underMouse());
    const bool speedMenuVisible =
        ui_.previewSpeedButton_ != nullptr
        && ui_.previewSpeedButton_->menu() != nullptr
        && ui_.previewSpeedButton_->menu()->isVisible();
    if (state_.previewScrubDragging_ || pointerOverControls || speedMenuVisible) {
        schedulePreviewFullscreenControlsAutoHide();
        return;
    }

    const QRect targetRect = previewFullscreenControlCardRect(false);
    if (!targetRect.isValid()) {
        return;
    }

    if (ui_.previewFullscreenControlsAnimation_ != nullptr) {
        ui_.previewFullscreenControlsAnimation_->stop();
    }
    if (ui_.previewFullscreenControlsOpacityAnimation_ != nullptr) {
        ui_.previewFullscreenControlsOpacityAnimation_->stop();
    }

    const QRect currentRect = ui_.previewFullscreenControlsWindow_->geometry();
    if (!animate || !currentRect.isValid() || currentRect == targetRect) {
        ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
        ui_.previewFullscreenControlsWindow_->setWindowOpacity(0.0);
        ui_.previewFullscreenControlsWindow_->hide();
    } else {
        if (ui_.previewFullscreenControlsAnimation_ != nullptr) {
            ui_.previewFullscreenControlsAnimation_->setStartValue(currentRect);
            ui_.previewFullscreenControlsAnimation_->setEndValue(targetRect);
            ui_.previewFullscreenControlsAnimation_->start();
        } else {
            ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
        }
        if (ui_.previewFullscreenControlsOpacityAnimation_ != nullptr) {
            ui_.previewFullscreenControlsOpacityAnimation_->setStartValue(ui_.previewFullscreenControlsWindow_->windowOpacity());
            ui_.previewFullscreenControlsOpacityAnimation_->setEndValue(0.0);
            ui_.previewFullscreenControlsOpacityAnimation_->start();
        } else {
            ui_.previewFullscreenControlsWindow_->setWindowOpacity(0.0);
            ui_.previewFullscreenControlsWindow_->hide();
        }
    }

    state_.previewFullscreenControlsVisible_ = false;
}

void miacode::runtime::PlaybackCoordinator::schedulePreviewFullscreenControlsAutoHide()
{
    if (!state_.previewFullscreenActive_ || ui_.previewFullscreenControlsTimer_ == nullptr) {
        return;
    }
    ui_.previewFullscreenControlsTimer_->start(kPreviewFullscreenControlsAutoHideDelayMs);
}

void miacode::runtime::PlaybackCoordinator::pollPreviewFullscreenCursor()
{
    if (!state_.previewFullscreenActive_ || ui_.previewFullscreenWindow_ == nullptr) {
        return;
    }

    const QPoint globalCursorPos = QCursor::pos();
    const QRect windowGlobalRect(
        ui_.previewFullscreenWindow_->mapToGlobal(QPoint(0, 0)),
        ui_.previewFullscreenWindow_->size()
    );
    const bool insideWindow = windowGlobalRect.contains(globalCursorPos);
    if (!insideWindow) {
        if (state_.previewFullscreenControlsVisible_) {
            schedulePreviewFullscreenControlsAutoHide();
        }
        state_.previewFullscreenCursorTrackingInitialized_ = false;
        return;
    }

    if (!state_.previewFullscreenCursorTrackingInitialized_) {
        state_.previewFullscreenLastCursorPos_ = globalCursorPos;
        state_.previewFullscreenCursorTrackingInitialized_ = true;
        return;
    }

    if (state_.previewFullscreenLastCursorPos_ != globalCursorPos) {
        state_.previewFullscreenLastCursorPos_ = globalCursorPos;
        if (shouldRevealPreviewFullscreenControls(globalCursorPos)) {
            showPreviewFullscreenControls(true);
        } else if (state_.previewFullscreenControlsVisible_) {
            schedulePreviewFullscreenControlsAutoHide();
        }
    }
}

void miacode::runtime::PlaybackCoordinator::updatePreviewFullscreenOverlayGeometry()
{
    if (!state_.previewFullscreenActive_ || ui_.previewFullscreenWindow_ == nullptr) {
        return;
    }

    const QRect windowRect = ui_.previewFullscreenWindow_->contentsRect();
    const QPoint globalTopLeft = ui_.previewFullscreenWindow_->mapToGlobal(windowRect.topLeft());

    if (ui_.previewFullscreenHintWindow_ != nullptr
        && ui_.previewFullscreenHintLabel_ != nullptr
        && ui_.previewFullscreenHintLabel_->isVisible()) {
        const QSize hintSize = ui_.previewFullscreenHintLabel_->sizeHint().expandedTo(QSize(220, 42));
        ui_.previewFullscreenHintWindow_->resize(hintSize);
        ui_.previewFullscreenHintWindow_->move(
            globalTopLeft.x() + qMax(16, (windowRect.width() - hintSize.width()) / 2),
            globalTopLeft.y() + kPreviewFullscreenHintTopMargin
        );
        ui_.previewFullscreenHintLabel_->resize(hintSize);
        ui_.previewFullscreenHintLabel_->raise();
        ui_.previewFullscreenHintWindow_->raise();
    }

    if (ui_.previewFullscreenControlsWindow_ != nullptr
        && ui_.previewControlCard_ != nullptr
        && ui_.previewControlCard_->parentWidget() == ui_.previewFullscreenControlsWindow_) {
        const QRect targetRect = previewFullscreenControlCardRect(state_.previewFullscreenControlsVisible_);
        if (targetRect.isValid()) {
            if (ui_.previewFullscreenControlsAnimation_ != nullptr
                && ui_.previewFullscreenControlsAnimation_->state() == QAbstractAnimation::Running) {
                ui_.previewFullscreenControlsAnimation_->stop();
            }
            ui_.previewFullscreenControlsWindow_->setGeometry(targetRect);
            ui_.previewFullscreenControlsWindow_->setWindowOpacity(state_.previewFullscreenControlsVisible_ ? 1.0 : 0.0);
            if (state_.previewFullscreenControlsVisible_) {
                ui_.previewFullscreenControlsWindow_->show();
            }
            ui_.previewFullscreenControlsWindow_->raise();
        }
    }
}
