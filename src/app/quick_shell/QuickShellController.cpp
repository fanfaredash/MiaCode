#include "QuickShellController.h"
#include "QuickShellKeyboardActivation.h"

#include "QuickShellNativeSurfaceHost.h"
#include "UiText.h"
#include "UiTheme.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/PreviewInteractionConfig.h"
#include "common/UiHangWatchdog.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QAction>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPoint>
#include <QPointer>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>
#include <QWindow>

#include <functional>
#include <utility>

namespace {

constexpr int kQuickShellActiveRefreshIntervalMs = 16;
constexpr int kQuickShellIdleRefreshIntervalMs = 250;
constexpr int kTimelineBrightnessPercentMin = 20;
constexpr int kTimelineBrightnessPercentMax = 200;
constexpr int kTimelineBrightnessPercentStep = 5;

QString timelineZoomMenuLabel(double scale)
{
    return QStringLiteral("%1%").arg(qRound(scale * 100.0));
}

int brightnessToPercent(double brightness, double fallback)
{
    const double normalized = qIsFinite(brightness) ? brightness : fallback;
    const int percent = qRound(normalized * 100.0);
    const int stepped = qRound(static_cast<double>(percent) / kTimelineBrightnessPercentStep)
        * kTimelineBrightnessPercentStep;
    return qBound(
        kTimelineBrightnessPercentMin,
        stepped,
        kTimelineBrightnessPercentMax);
}

double brightnessFromPercent(int percent)
{
    return static_cast<double>(qBound(
        kTimelineBrightnessPercentMin,
        percent,
        kTimelineBrightnessPercentMax)) / 100.0;
}

// Custom QMenu item widget for the timeline-follow settings menu.
// Paints a 14×14 rounded-square indicator + checkmark + label that
// pixel-matches the QML CheckBox style used elsewhere in the shell.
// Handles its own click events so QMenu doesn't dismiss when the
// user toggles a toggle — that's the "menu stays open" behaviour
// the spec asked for. The widget swallows the mouse release in its
// own area so QMenu's default close-on-release path never fires.
class FollowSettingsCheckItem : public QWidget
{
public:
    FollowSettingsCheckItem(const QString& text, bool initialChecked,
                            std::function<void(bool)> onToggled,
                            QWidget* parent = nullptr)
        : QWidget(parent)
        , text_(text)
        , checked_(initialChecked)
        , onToggled_(std::move(onToggled))
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setAccessibleName(text_);
        setAccessibleDescription(text_);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);
    }

    bool isChecked() const { return checked_; }

    void toggle()
    {
        checked_ = !checked_;
        update();
        if (onToggled_) {
            onToggled_(checked_);
        }
    }

    QSize sizeHint() const override
    {
        QFont labelFont = font();
        labelFont.setWeight(QFont::DemiBold);
        const QFontMetrics fm(labelFont);
        const int textW = fm.horizontalAdvance(text_);
        const int width = kPaddingLeft_ + kBoxSize_ + kBoxGap_ + textW + kPaddingRight_;
        const int height = qMax(kRowHeightMin_, fm.height() + kRowPadV_ * 2);
        return QSize(width, height);
    }

protected:
    void enterEvent(QEnterEvent*) override { hovered_ = true; update(); }
    void leaveEvent(QEvent*) override { hovered_ = false; update(); }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !rect().contains(event->pos())) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        toggle();
        // Consume so QMenu's release handler never fires — menu stays
        // open. The user dismisses via Esc or click-outside.
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (miacode::quick_shell::isMenuToggleActivationKey(event->key())) {
            toggle();
            event->accept();
            return;
        }
        // Keep Escape unhandled so QMenu retains its normal dismiss path.
        QWidget::keyPressEvent(event);
    }

    void paintEvent(QPaintEvent*) override
    {
        const UiTheme::Colors& c = UiTheme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        if (hovered_) {
            const QRect bg = rect().adjusted(2, 2, -2, -2);
            p.setPen(Qt::NoPen);
            p.setBrush(c.menuHoverBg);
            p.drawRoundedRect(bg, 4, 4);
        }

        const QRect box(
            kPaddingLeft_,
            (height() - kBoxSize_) / 2,
            kBoxSize_,
            kBoxSize_);
        if (checked_) {
            p.setPen(QPen(c.accent, 1));
            p.setBrush(c.accent);
            p.drawRoundedRect(box, 3, 3);
            // Checkmark — matches QML Canvas line path (0.24,0.55) →
            // (0.44,0.74) → (0.78,0.28).
            p.setPen(QPen(c.accentText, 1.6,
                          Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            QPainterPath path;
            path.moveTo(box.x() + kBoxSize_ * 0.24,
                        box.y() + kBoxSize_ * 0.55);
            path.lineTo(box.x() + kBoxSize_ * 0.44,
                        box.y() + kBoxSize_ * 0.74);
            path.lineTo(box.x() + kBoxSize_ * 0.78,
                        box.y() + kBoxSize_ * 0.28);
            p.drawPath(path);
        } else {
            p.setPen(QPen(hovered_ ? c.accent : c.border, 1));
            p.setBrush(c.cardBg);
            p.drawRoundedRect(box, 3, 3);
        }

        QFont labelFont = font();
        labelFont.setWeight(QFont::DemiBold);
        p.setFont(labelFont);
        p.setPen(c.textPrimary);
        const QRect textRect = rect().adjusted(
            kPaddingLeft_ + kBoxSize_ + kBoxGap_,
            0,
            -kPaddingRight_,
            0);
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text_);
    }

private:
    static constexpr int kPaddingLeft_ = 10;
    static constexpr int kPaddingRight_ = 14;
    static constexpr int kBoxSize_ = 14;
    static constexpr int kBoxGap_ = 8;
    static constexpr int kRowHeightMin_ = 26;
    static constexpr int kRowPadV_ = 5;

    QString text_;
    bool checked_ = false;
    bool hovered_ = false;
    std::function<void(bool)> onToggled_;
};

class TimelineBrightnessSliderItem : public QWidget
{
public:
    TimelineBrightnessSliderItem(
        const QString& labelText,
        int initialPercent,
        std::function<void(int)> onValueChanged,
        QWidget* parent = nullptr)
        : QWidget(parent)
        , onValueChanged_(std::move(onValueChanged))
    {
        const UiTheme::Colors& c = UiTheme::colors();
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(12, 8, 12, 8);
        root->setSpacing(6);

        auto* labelRow = new QHBoxLayout();
        labelRow->setContentsMargins(0, 0, 0, 0);
        labelRow->setSpacing(10);
        auto* titleLabel = new QLabel(labelText, this);
        QFont titleFont = titleLabel->font();
        titleFont.setWeight(QFont::DemiBold);
        titleLabel->setFont(titleFont);
        titleLabel->setStyleSheet(QStringLiteral("color:%1;").arg(c.textPrimary.name()));
        valueLabel_ = new QLabel(this);
        valueLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueLabel_->setMinimumWidth(QFontMetrics(valueLabel_->font()).horizontalAdvance(QStringLiteral("200%")));
        valueLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(c.textSecondary.name()));
        labelRow->addWidget(titleLabel, 1);
        labelRow->addWidget(valueLabel_, 0);
        root->addLayout(labelRow);

        slider_ = new QSlider(Qt::Horizontal, this);
        slider_->setRange(kTimelineBrightnessPercentMin, kTimelineBrightnessPercentMax);
        slider_->setSingleStep(kTimelineBrightnessPercentStep);
        slider_->setPageStep(kTimelineBrightnessPercentStep);
        slider_->setTickInterval(kTimelineBrightnessPercentStep);
        slider_->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        root->addWidget(slider_);

        setMinimumWidth(250);
        const int clamped = qBound(kTimelineBrightnessPercentMin, initialPercent, kTimelineBrightnessPercentMax);
        updateValueLabel(clamped);
        slider_->setValue(clamped);
        connect(slider_, &QSlider::valueChanged, this, [this](int value) {
            const int stepped = qBound(
                kTimelineBrightnessPercentMin,
                qRound(static_cast<double>(value) / kTimelineBrightnessPercentStep) * kTimelineBrightnessPercentStep,
                kTimelineBrightnessPercentMax);
            if (stepped != value) {
                slider_->setValue(stepped);
                return;
            }
            updateValueLabel(stepped);
            if (onValueChanged_) {
                onValueChanged_(stepped);
            }
        });
    }

    QSize sizeHint() const override
    {
        return QSize(270, QWidget::sizeHint().height());
    }

private:
    void updateValueLabel(int percent)
    {
        if (valueLabel_ != nullptr) {
            valueLabel_->setText(QStringLiteral("%1%").arg(percent));
        }
    }

    QLabel* valueLabel_ = nullptr;
    QSlider* slider_ = nullptr;
    std::function<void(int)> onValueChanged_;
};

template <typename T>
bool assignIfChanged(T& target, const T& value)
{
    if (target == value) {
        return false;
    }
    target = value;
    return true;
}

void appendQuickShellControllerLog(
    const QString& action,
    const QString& payload = QString(),
    miacode::debug_log::Level level = miacode::debug_log::Level::Info)
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/controller"),
        text,
        /*force=*/false,
        level
    );
}

void appendQuickShellLifecycleLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/lifecycle"),
        text
    );
}

}  // namespace

QuickShellController::QuickShellController(
    QuickShellCommandSink* commandSink,
    QuickShellStateSource* stateSource,
    QuickShellNativeSurfaceHost* surfaceHost,
    QObject* parent)
    : QObject(parent)
    , commandSink_(commandSink)
    , stateSource_(stateSource)
    , surfaceHost_(surfaceHost)
    , refreshTimer_(new QTimer(this))
{
    if (stateSource_ != nullptr) {
        if (auto* mediaHost =
                qobject_cast<PreviewStageMediaHost*>(stateSource_->shellPreviewStageMediaHostObject());
            mediaHost != nullptr) {
            connect(mediaHost, &PreviewStageMediaHost::mediaStateChanged, this, [this]() {
                refreshFromStateSource();
            });
        }
    }
    refreshTimer_->setInterval(kQuickShellIdleRefreshIntervalMs);
    connect(refreshTimer_, &QTimer::timeout, this, &QuickShellController::refreshFromStateSource);
    refreshTimer_->start();
    refreshFromStateSource();
}

QString QuickShellController::windowTitle() const
{
    return windowTitle_;
}

bool QuickShellController::workspacePanelsSwapped() const
{
    return workspacePanelsSwapped_;
}

QString QuickShellController::previewSpeedLabel() const
{
    return previewSpeedLabel_;
}

bool QuickShellController::muriCheckRenderMode() const
{
    return muriCheckRenderMode_;
}

bool QuickShellController::previewPlaying() const
{
    return previewPlaying_;
}

double QuickShellController::previewPositionSeconds() const
{
    return previewPositionSeconds_;
}

double QuickShellController::videoExportProgressSeconds() const
{
    return videoExportProgressSeconds_;
}

double QuickShellController::previewDurationSeconds() const
{
    return previewDurationSeconds_;
}
double QuickShellController::previewLowerBoundSeconds() const
{
    return previewLowerBoundSeconds_;
}

QStringList QuickShellController::previewStatsTexts() const
{
    return previewStatsTexts_;
}

double QuickShellController::previewCanvasAspectRatio() const
{
    return previewCanvasAspectRatio_;
}

qulonglong QuickShellController::previewPaneRestoreGeneration() const
{
    return previewPaneRestoreGeneration_;
}

double QuickShellController::previewPaneWidthRatio() const
{
    return previewPaneWidthRatio_;
}

double QuickShellController::previewSeekSingleStepSeconds() const
{
    return miacode::preview_interaction::kSeekSingleStepSeconds;
}

bool QuickShellController::previewFullscreen() const
{
    return previewFullscreen_;
}

QObject* QuickShellController::previewRuntime() const
{
    return stateSource_ != nullptr ? stateSource_->shellPreviewRuntimeObject() : nullptr;
}

QObject* QuickShellController::previewStageMediaHost() const
{
    return stateSource_ != nullptr ? stateSource_->shellPreviewStageMediaHostObject() : nullptr;
}

QWindow* QuickShellController::previewCompositeWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().previewCompositeWindow : nullptr;
}

bool QuickShellController::previewUsesSeparateSurface() const
{
    return previewUsesSeparateSurface_;
}

QObject* QuickShellController::timelineStateBridge() const
{
    return stateSource_ != nullptr ? stateSource_->shellTimelineStateBridgeObject() : nullptr;
}

bool QuickShellController::timelineSurfaceReady() const
{
    return timelineSurfaceReady_;
}

QString QuickShellController::bottomTabsCurrentTabId() const
{
    return bottomTabsCurrentTabId_;
}

int QuickShellController::bottomTabsHostHeight() const
{
    return bottomTabsHostHeight_;
}

double QuickShellController::bottomTabsHeaderScale() const
{
    return bottomTabsHeaderScale_;
}

bool QuickShellController::bottomTabsVisible() const
{
    return bottomTabsVisible_;
}

bool QuickShellController::timelineTabVisible() const
{
    return timelineTabVisible_;
}

bool QuickShellController::validationTabVisible() const
{
    return validationTabVisible_;
}

bool QuickShellController::muriTabVisible() const
{
    return muriTabVisible_;
}

bool QuickShellController::exportPageActive() const
{
    return exportPageActive_;
}

QString QuickShellController::timelineTabLabel() const
{
    // Mirrors MainWindow::bottomTabsFallbackLabel for the legacy QSG
    // path — keeps the QuickShell bottom-tab labels identical to the
    // ones the legacy QTabBar code uses.
    return UiText::text(QStringLiteral("window.timeline"));
}

QString QuickShellController::validationTabLabel() const
{
    return UiText::text(QStringLiteral("window.syntax"));
}

QString QuickShellController::muriTabLabel() const
{
    return UiText::text(QStringLiteral("window.muri"));
}

QString QuickShellController::timelineViewLockLabel() const
{
    return UiText::text(QStringLiteral("shell.view_lock"));
}

QString QuickShellController::timelineSyncLabel() const
{
    return UiText::text(QStringLiteral("shell.timeline_sync"));
}

QString QuickShellController::timelineFollowCodeLabel() const
{
    return UiText::text(QStringLiteral("shell.follow_code"));
}

QWindow* QuickShellController::topChromeWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().topChrome : nullptr;
}

QWindow* QuickShellController::sidebarWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().sidebar : nullptr;
}

QWindow* QuickShellController::workspaceWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().workspace : nullptr;
}

QWindow* QuickShellController::bottomTabsWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().bottomTabs : nullptr;
}

QWindow* QuickShellController::statusWindow() const
{
    return surfaceHost_ != nullptr ? surfaceHost_->surfaceBundle().status : nullptr;
}

void QuickShellController::setPreviewFullscreen(bool fullscreen)
{
    if (commandSink_ == nullptr || stateSource_ == nullptr) {
        return;
    }
    if (stateSource_->shellPreviewFullscreen() == fullscreen) {
        return;
    }
    commandSink_->setShellPreviewFullscreen(fullscreen);
    refreshFromStateSource();
}

void QuickShellController::refresh()
{
    refreshFromStateSource();
}

void QuickShellController::markNextCloseConfirmedExternally()
{
    closeConfirmedExternally_ = true;
}

void QuickShellController::clearPendingExternalCloseConfirmation()
{
    closeConfirmedExternally_ = false;
}

bool QuickShellController::confirmClose()
{
    QElapsedTimer timer;
    timer.start();
    appendQuickShellLifecycleLog(
        QStringLiteral("controller_confirm_close_enter"),
        QStringLiteral("bypass_pending=%1 has_command_sink=%2 preview_playing=%3 preview_fullscreen=%4")
            .arg(closeConfirmedExternally_ ? 1 : 0)
            .arg(commandSink_ != nullptr ? 1 : 0)
            .arg(previewPlaying_ ? 1 : 0)
            .arg(previewFullscreen_ ? 1 : 0)
    );
    if (closeConfirmedExternally_) {
        appendQuickShellControllerLog(QStringLiteral("confirm_close_bypass"));
        closeConfirmedExternally_ = false;
        appendQuickShellLifecycleLog(
            QStringLiteral("controller_confirm_close_exit"),
            QStringLiteral("result=bypass elapsed_ms=%1").arg(timer.elapsed())
        );
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("controller_confirm_close"),
            timer.elapsed(),
            QStringLiteral("result=bypass has_command_sink=%1").arg(commandSink_ != nullptr ? 1 : 0)
        );
        return true;
    }
    const bool confirmed = commandSink_ != nullptr ? commandSink_->confirmShellClose() : true;
    appendQuickShellLifecycleLog(
        QStringLiteral("controller_confirm_close_exit"),
        QStringLiteral("result=%1 elapsed_ms=%2 has_command_sink=%3 preview_playing=%4 preview_fullscreen=%5")
            .arg(confirmed ? QStringLiteral("confirmed") : QStringLiteral("rejected"))
            .arg(timer.elapsed())
            .arg(commandSink_ != nullptr ? 1 : 0)
            .arg(previewPlaying_ ? 1 : 0)
            .arg(previewFullscreen_ ? 1 : 0)
    );
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("controller_confirm_close"),
        timer.elapsed(),
        QStringLiteral("result=%1 has_command_sink=%2")
            .arg(confirmed ? QStringLiteral("confirmed") : QStringLiteral("rejected"))
            .arg(commandSink_ != nullptr ? 1 : 0)
    );
    return confirmed;
}

void QuickShellController::logShellLifecycle(const QString& action, const QString& payload)
{
    appendQuickShellLifecycleLog(action.trimmed().isEmpty() ? QStringLiteral("qml_lifecycle") : action, payload);
}

void QuickShellController::notifyRootCloseAccepted(const QString& source)
{
    const QString normalizedSource = source.trimmed().isEmpty() ? QStringLiteral("qml_root_close") : source.trimmed();
    appendQuickShellLifecycleLog(
        QStringLiteral("root_close_accepted_notify"),
        QStringLiteral("source=%1").arg(normalizedSource)
    );
    emit rootCloseAccepted(normalizedSource);
}

void QuickShellController::togglePreviewPlayback()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->toggleShellPreviewPlayback();
    refreshFromStateSource();
}

void QuickShellController::stopPreview()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->stopShellPreview();
    refreshFromStateSource();
}

void QuickShellController::seekPreview(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->seekShellPreview(second);
    refreshFromStateSource();
}

void QuickShellController::beginPreviewScrub()
{
    if (commandSink_ == nullptr) {
        return;
    }
    appendQuickShellControllerLog(QStringLiteral("preview_scrub_begin"));
    commandSink_->beginShellPreviewScrub();
    refreshFromStateSource();
}

void QuickShellController::updatePreviewScrub(double second, bool centerView)
{
    if (commandSink_ == nullptr) {
        return;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_scrub_update"),
        QString("second=%1 center=%2")
            .arg(second, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    commandSink_->updateShellPreviewScrub(second, centerView);
    refreshFromStateSource();
}

void QuickShellController::endPreviewScrub(double second, bool centerView)
{
    if (commandSink_ == nullptr) {
        return;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_scrub_end"),
        QString("second=%1 center=%2")
            .arg(second, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    commandSink_->endShellPreviewScrub(second, centerView);
    refreshFromStateSource();
}

void QuickShellController::setPreviewRate(double rate)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->setShellPreviewRate(rate);
    refreshFromStateSource();
}

void QuickShellController::toggleMuriRenderMode()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->toggleShellMuriRenderMode();
    refreshFromStateSource();
}

void QuickShellController::setPreviewPaneWidthRatio(double ratio)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->setShellPreviewPaneWidthRatio(ratio);
    refreshFromStateSource();
}

void QuickShellController::adjustPreviewSpeed(int direction)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->nudgeShellPreviewRate(direction);
    refreshFromStateSource();
}

void QuickShellController::setBottomTabsCurrentTabId(const QString& tabId)
{
    if (commandSink_ == nullptr || stateSource_ == nullptr) {
        return;
    }
    if (tabId.trimmed().isEmpty() || stateSource_->shellBottomTabsCurrentTabId() == tabId) {
        return;
    }
    commandSink_->setShellBottomTabsCurrentTab(tabId);
    refreshFromStateSource();
}

void QuickShellController::setBottomTabsHostHeight(int height)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->setShellBottomTabsHeight(height);
    refreshFromStateSource();
}

void QuickShellController::timelineHeaderNavigate(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->navigateShellTimelineToSecond(second);
    refreshFromStateSource();
}

void QuickShellController::timelineWheelNavigate(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->wheelShellTimelineNavigate(second);
    refreshFromStateSource();
}

void QuickShellController::timelineCenterNavigate(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->centerShellTimelineNavigate(second);
    refreshFromStateSource();
}

void QuickShellController::timelineDragStarted()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineDragStarted();
    refreshFromStateSource();
}

void QuickShellController::timelineDragFinished(double second)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineDragFinished(second);
    refreshFromStateSource();
}

void QuickShellController::timelineUserInteractionStarted()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineUserInteractionStarted();
    refreshFromStateSource();
}

void QuickShellController::noteTimelineSurfaceReady()
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineSurfaceReady();
    refreshFromStateSource();
}

void QuickShellController::timelineFollowPreviewToggled(bool enabled)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineFollowPreviewToggled(enabled);
    refreshFromStateSource();
}

void QuickShellController::timelineViewportLockToggled(bool enabled)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineViewportLockToggled(enabled);
    refreshFromStateSource();
}

void QuickShellController::timelineFollowProgressToggled(bool enabled)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineFollowProgressToggled(enabled);
    refreshFromStateSource();
}

void QuickShellController::timelineSyncToggled(bool enabled)
{
    if (commandSink_ == nullptr) {
        return;
    }
    commandSink_->shellTimelineSyncToggled(enabled);
    refreshFromStateSource();
}

void QuickShellController::openTimelineFollowSettingsMenu(int gearGlobalRight, int gearGlobalTop)
{
    if (stateSource_ == nullptr) {
        return;
    }
    auto* bridge = qobject_cast<TimelineQuickStateBridge*>(
        stateSource_->shellTimelineStateBridgeObject());
    if (bridge == nullptr) {
        return;
    }

    // Heap-allocate so popup() can return immediately; WA_DeleteOnClose
    // tears the menu down once the user dismisses (Esc / click-outside).
    auto* menu = new QMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    UiTheme::styleRoundedMenu(*menu);

    // Each item is a custom QWidget that paints the QML-CheckBox look
    // (rounded square indicator + accent fill + white checkmark + hover
    // background) and toggles via its own mouseReleaseEvent. The widget
    // accepts the event so QMenu's close-on-release path never fires —
    // that's how the menu stays open across multiple toggles.
    const auto addToggleWidget = [&](const QString& label, bool initialChecked,
                                     void (QuickShellController::*slot)(bool)) {
        auto* item = new FollowSettingsCheckItem(label, initialChecked,
            [this, slot](bool checked) {
                (this->*slot)(checked);
            });
        auto* widgetAction = new QWidgetAction(menu);
        widgetAction->setDefaultWidget(item);
        // Arrow-key navigation activates the QWidgetAction, while mouse and
        // focused Space/Enter invoke FollowSettingsCheckItem::toggle() directly.
        // Both paths deliberately share the row's one toggle routine.
        connect(widgetAction, &QWidgetAction::triggered, item, [item]() {
            item->toggle();
        });
        menu->addAction(widgetAction);
    };

    addToggleWidget(
        UiText::text(QStringLiteral("shell.view_lock")),
        bridge->viewportLockEnabled(),
        &QuickShellController::timelineViewportLockToggled);
    // Timeline Sync sits above Follow Code so the bottom-most menu
    // item matches the inline tab-strip chip (which now shows Follow
    // Code) — visually the two anchors are at the same Y on screen.
    addToggleWidget(
        UiText::text(QStringLiteral("shell.timeline_sync")),
        bridge->timelineSyncEnabled(),
        &QuickShellController::timelineSyncToggled);
    addToggleWidget(
        UiText::text(QStringLiteral("shell.follow_code")),
        bridge->followPreviewEnabled(),
        &QuickShellController::timelineFollowPreviewToggled);

    // Layout once so sizeHint reflects all three rows + the rounded
    // stylesheet padding; only after that can we anchor the bottom-
    // right corner. menu->popup() handles the rest of the show path.
    menu->adjustSize();
    const QSize menuSize = menu->sizeHint();
    const int x = gearGlobalRight - menuSize.width();
    const int y = gearGlobalTop - menuSize.height() - 4;
    menu->popup(QPoint(x, y));
}

void QuickShellController::openTimelineBrightnessMenu(int gearGlobalRight, int gearGlobalTop)
{
    if (stateSource_ == nullptr) {
        return;
    }
    auto* bridge = qobject_cast<TimelineQuickStateBridge*>(
        stateSource_->shellTimelineStateBridgeObject());
    if (bridge == nullptr) {
        return;
    }

    auto* menu = new QMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    UiTheme::styleRoundedMenu(*menu);

    const QPointer<TimelineQuickStateBridge> bridgeGuard(bridge);
    const auto addBrightnessSlider = [&](const QString& label, int initialPercent, std::function<void(TimelineQuickStateBridge*, double)> setter) {
        auto* item = new TimelineBrightnessSliderItem(
            label,
            initialPercent,
            [bridgeGuard, setter = std::move(setter)](int percent) {
                if (bridgeGuard == nullptr) {
                    return;
                }
                setter(bridgeGuard.data(), brightnessFromPercent(percent));
            });
        auto* widgetAction = new QWidgetAction(menu);
        widgetAction->setDefaultWidget(item);
        menu->addAction(widgetAction);
    };

    addBrightnessSlider(
        UiText::text(QStringLiteral("shell.timeline_waveform_brightness")),
        brightnessToPercent(
            bridge->waveformBrightness(),
            miacode::timeline::kTimelineWaveformBrightnessDefault),
        [](TimelineQuickStateBridge* target, double brightness) {
            target->setWaveformBrightness(brightness);
        });
    addBrightnessSlider(
        UiText::text(QStringLiteral("shell.timeline_measure_line_brightness")),
        brightnessToPercent(
            bridge->measureLineBrightness(),
            miacode::timeline::kTimelineMeasureLineBrightnessDefault),
        [](TimelineQuickStateBridge* target, double brightness) {
            target->setMeasureLineBrightness(brightness);
        });

    menu->adjustSize();
    const QSize menuSize = menu->sizeHint();
    const int x = gearGlobalRight - menuSize.width();
    const int y = gearGlobalTop - menuSize.height() - 4;
    menu->popup(QPoint(x, y));
}

void QuickShellController::openTimelineZoomMenu(int controlGlobalLeft, int controlGlobalTop, int controlWidth)
{
    if (stateSource_ == nullptr) {
        return;
    }
    auto* bridge = qobject_cast<TimelineQuickStateBridge*>(
        stateSource_->shellTimelineStateBridgeObject());
    if (bridge == nullptr) {
        return;
    }

    const QVector<double> presets = bridge->zoomPresets();
    if (presets.isEmpty()) {
        return;
    }

    auto* menu = new QMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    UiTheme::styleRoundedMenu(*menu);
    menu->setMinimumWidth(qMax(1, controlWidth));

    const QPointer<TimelineQuickStateBridge> bridgeGuard(bridge);
    const double currentScale = bridge->zoomScale();
    for (double preset : presets) {
        QAction* action = menu->addAction(timelineZoomMenuLabel(preset));
        action->setCheckable(true);
        action->setChecked(qAbs(preset - currentScale) <= 1e-6);
        QObject::connect(action, &QAction::triggered, menu, [bridgeGuard, preset]() {
            if (bridgeGuard == nullptr) {
                return;
            }
            bridgeGuard->setZoomScaleAnchored(preset, bridgeGuard->viewportCenterSecond());
        });
    }

    menu->adjustSize();
    const QSize menuSize = menu->sizeHint();
    const int x = controlGlobalLeft;
    const int y = controlGlobalTop - menuSize.height() - 4;
    menu->popup(QPoint(x, y));
}

bool QuickShellController::stepPreviewBySeconds(double deltaSeconds, bool centerView)
{
    if (commandSink_ == nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("preview_step_ignored"),
            QString("reason=no_command_sink delta=%1 center=%2")
                .arg(deltaSeconds, 0, 'f', 6)
                .arg(centerView ? 1 : 0)
        );
        return false;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_step_request"),
        QString("delta=%1 center=%2")
            .arg(deltaSeconds, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
    );
    const bool moved = commandSink_->stepShellPreviewBySeconds(deltaSeconds, centerView);
    appendQuickShellControllerLog(
        QStringLiteral("preview_step_result"),
        QString("delta=%1 center=%2 moved=%3 pos=%4 duration=%5")
            .arg(deltaSeconds, 0, 'f', 6)
            .arg(centerView ? 1 : 0)
            .arg(moved ? 1 : 0)
            .arg(previewPositionSeconds_, 0, 'f', 6)
            .arg(previewDurationSeconds_, 0, 'f', 6)
    );
    if (moved) {
        refreshFromStateSource();
    }
    return moved;
}

void QuickShellController::beginPreviewHeldSeek(int direction, int key)
{
    if (commandSink_ == nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("preview_hold_begin_ignored"),
            QString("reason=no_command_sink direction=%1 key=%2").arg(direction).arg(key)
        );
        return;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_hold_begin"),
        QString("direction=%1 key=%2").arg(direction).arg(key)
    );
    commandSink_->beginShellPreviewHeldSeek(direction, key);
}

void QuickShellController::stopPreviewHeldSeek(int key)
{
    if (commandSink_ == nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("preview_hold_stop_ignored"),
            QString("reason=no_command_sink key=%1").arg(key)
        );
        return;
    }
    appendQuickShellControllerLog(
        QStringLiteral("preview_hold_stop"),
        QString("key=%1").arg(key)
    );
    commandSink_->stopShellPreviewHeldSeek(key);
    refreshFromStateSource();
}

QString QuickShellController::formatPreviewTimestamp(double second) const
{
    const int totalCentiseconds = qMax(0, qRound(second * 100.0));
    const int minutes = totalCentiseconds / 6000;
    const int secondsPart = (totalCentiseconds / 100) % 60;
    const int centiseconds = totalCentiseconds % 100;
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secondsPart, 2, 10, QChar('0'))
        .arg(centiseconds, 2, 10, QChar('0'));
}

void QuickShellController::logPreviewInteraction(const QString& action, const QString& payload)
{
    appendQuickShellControllerLog(action.trimmed().isEmpty() ? QStringLiteral("preview_interaction") : action, payload);
}

void QuickShellController::syncTopChromeSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncTopChromeSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->topChromeSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_top_chrome"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncSidebarSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncSidebarSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->sidebarSurfaceWidget(); surface != nullptr) {
        const miacode::diagnostics::SurfaceSizeKey surfaceKey{
            static_cast<quintptr>(surface->winId()), surface->width(), surface->height()};
        if (sidebarSurfaceLogGate_.shouldEmit(surfaceKey, false)) {
            appendQuickShellControllerLog(
                QStringLiteral("sync_sidebar"),
                QString("size=%1x%2 handle=0x%3")
                    .arg(surface->width())
                    .arg(surface->height())
                    .arg(static_cast<quintptr>(surface->winId()), 0, 16)
            );
        }
    }
}

void QuickShellController::syncWorkspaceSurfaceSize(int width, int height)
{
    MC_OP("QuickShellController::syncWorkspaceSurfaceSize");
    QElapsedTimer elapsed;
    elapsed.start();
    MIACODE_HANG_PHASE(
        "QuickShellController::syncWorkspaceSurfaceSize",
        QStringLiteral("requested=%1x%2").arg(width).arg(height));
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncWorkspaceSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->workspaceSurfaceWidget(); surface != nullptr) {
        const qint64 elapsedMs = elapsed.elapsed();
        const bool slow = elapsedMs >= 50;
        const miacode::diagnostics::SurfaceSizeKey surfaceKey{
            static_cast<quintptr>(surface->winId()), surface->width(), surface->height()};
        if (workspaceSurfaceLogGate_.shouldEmit(surfaceKey, slow)) {
            appendQuickShellControllerLog(
                QStringLiteral("sync_workspace"),
                QString("size=%1x%2 requested=%3x%4 elapsed_ms=%5 handle=0x%6")
                    .arg(surface->width())
                    .arg(surface->height())
                    .arg(width)
                    .arg(height)
                    .arg(elapsedMs)
                    .arg(static_cast<quintptr>(surface->winId()), 0, 16),
                slow ? miacode::debug_log::Level::Warn : miacode::debug_log::Level::Info
            );
        }
    }
}

void QuickShellController::syncBottomTabsSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncBottomTabsSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->bottomTabsSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_bottom_tabs"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

void QuickShellController::syncBottomTabsToastAnchor(int x, int y, int width, int height, bool visible)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncBottomTabsToastAnchor(x, y, width, height, visible);
}

void QuickShellController::syncStatusSurfaceSize(int width, int height)
{
    if (surfaceHost_ == nullptr) {
        return;
    }
    surfaceHost_->syncStatusSurfaceSize(width, height);
    if (QWidget* surface = surfaceHost_->statusSurfaceWidget(); surface != nullptr) {
        appendQuickShellControllerLog(
            QStringLiteral("sync_status"),
            QString("size=%1x%2 handle=0x%3")
                .arg(surface->width())
                .arg(surface->height())
                .arg(static_cast<quintptr>(surface->winId()), 0, 16)
        );
    }
}

bool QuickShellController::hasShortcut(const QKeySequence& sequence) const
{
    return commandSink_ != nullptr ? commandSink_->shellHasShortcut(sequence) : false;
}

bool QuickShellController::triggerShortcut(const QKeySequence& sequence)
{
    if (commandSink_ == nullptr) {
        return false;
    }
    const bool triggered = commandSink_->shellTriggerShortcut(sequence);
    if (triggered) {
        refreshFromStateSource();
    }
    return triggered;
}

void QuickShellController::refreshFromStateSource()
{
    if (stateSource_ == nullptr) {
        updateRefreshTimerInterval();
        return;
    }

    const QString previousBottomTabsCurrentTabId = bottomTabsCurrentTabId_;
    const QString previousPreviewSpeedLabel = previewSpeedLabel_;
    bool stateChanged = false;
    stateChanged |= assignIfChanged(windowTitle_, stateSource_->shellWindowTitle());
    stateChanged |= assignIfChanged(workspacePanelsSwapped_, stateSource_->shellWorkspacePanelsSwapped());
    stateChanged |= assignIfChanged(previewSpeedLabel_, stateSource_->shellPreviewSpeedLabel());
    stateChanged |= assignIfChanged(muriCheckRenderMode_, stateSource_->shellMuriCheckRenderMode());
    stateChanged |= assignIfChanged(previewPlaying_, stateSource_->shellPreviewPlaying());
    stateChanged |= assignIfChanged(previewPositionSeconds_, stateSource_->shellPreviewPositionSeconds());
    stateChanged |= assignIfChanged(previewDurationSeconds_, stateSource_->shellPreviewDurationSeconds());
    stateChanged |= assignIfChanged(previewLowerBoundSeconds_, stateSource_->shellPreviewLowerBoundSeconds());
    stateChanged |= assignIfChanged(videoExportProgressSeconds_, stateSource_->shellVideoExportProgressSeconds());
    stateChanged |= assignIfChanged(previewStatsTexts_, stateSource_->shellPreviewStatsTexts());
    stateChanged |= assignIfChanged(previewCanvasAspectRatio_, stateSource_->shellPreviewCanvasAspectRatio());
    stateChanged |= assignIfChanged(previewPaneRestoreGeneration_, stateSource_->shellPreviewPaneRestoreGeneration());
    stateChanged |= assignIfChanged(previewPaneWidthRatio_, stateSource_->shellPreviewPaneWidthRatio());
    stateChanged |= assignIfChanged(previewUsesSeparateSurface_, stateSource_->shellPreviewUsesSeparateSurface());
    stateChanged |= assignIfChanged(timelineSurfaceReady_, stateSource_->shellTimelineSurfaceReady());
    stateChanged |= assignIfChanged(bottomTabsCurrentTabId_, stateSource_->shellBottomTabsCurrentTabId());
    stateChanged |= assignIfChanged(bottomTabsHostHeight_, stateSource_->shellBottomTabsHeight());
    stateChanged |= assignIfChanged(bottomTabsHeaderScale_, stateSource_->shellBottomTabsHeaderScale());
    stateChanged |= assignIfChanged(bottomTabsVisible_, stateSource_->shellBottomTabsVisible());
    stateChanged |= assignIfChanged(timelineTabVisible_, stateSource_->shellTimelineTabVisible());
    stateChanged |= assignIfChanged(validationTabVisible_, stateSource_->shellValidationTabVisible());
    stateChanged |= assignIfChanged(muriTabVisible_, stateSource_->shellMuriTabVisible());
    stateChanged |= assignIfChanged(exportPageActive_, stateSource_->shellExportPageActive());

    const bool nextPreviewFullscreen = stateSource_->shellPreviewFullscreen();
    if (assignIfChanged(previewFullscreen_, nextPreviewFullscreen)) {
        stateChanged = true;
        emit previewFullscreenChanged();
    }

    if (surfaceHost_ != nullptr && previousBottomTabsCurrentTabId != bottomTabsCurrentTabId_) {
        surfaceHost_->refreshBottomTabsSurfaceVisibility();
    }
    if (surfaceHost_ != nullptr) {
        // The bottom-tabs speed toast is anchored to the timeline strip, which is
        // gone in fullscreen — there, MainWindow's own centered
        // previewPlaybackRateToast_ (shown from applyPreviewPlaybackRate) is the
        // speed feedback. Showing both gave the "两个 widget" report, so suppress
        // this one whenever fullscreen.
        if (!previewSpeedToastInitialized_) {
            previewSpeedToastInitialized_ = true;
        } else if (previousPreviewSpeedLabel != previewSpeedLabel_ && !previewFullscreen_) {
            surfaceHost_->showBottomTabsSpeedToast(previewSpeedLabel_);
        }
        if (!bottomTabsVisible_ || previewFullscreen_) {
            surfaceHost_->hideBottomTabsSpeedToast();
        }
    }

    if (stateChanged) {
        emit shellStateChanged();
    }
    updateRefreshTimerInterval();
}

void QuickShellController::updateRefreshTimerInterval()
{
    if (refreshTimer_ == nullptr) {
        return;
    }
    // Inline export progress counts as "active": poll fast enough that the
    // transport's progress display tracks the worker smoothly.
    const int nextIntervalMs = (previewPlaying_ || videoExportProgressSeconds_ >= 0.0)
        ? kQuickShellActiveRefreshIntervalMs
        : kQuickShellIdleRefreshIntervalMs;
    if (refreshTimer_->interval() != nextIntervalMs) {
        refreshTimer_->setInterval(nextIntervalMs);
    }
}
