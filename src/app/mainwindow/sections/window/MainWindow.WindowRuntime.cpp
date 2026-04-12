#include "MainWindow.WindowSection.h"
#include "../../MainWindowShared.h"

#include "../dialogs/MainWindow.DialogsSection.h"
#include "../document/MainWindow.DocumentSection.h"
#include "../editor/MainWindow.EditorSection.h"
#include "../export/MainWindow.ExportSection.h"
#include "../frame/MainWindow.FrameSection.h"
#include "../preferences/MainWindow.PreferencesSection.h"
#include "../preview/MainWindow.PreviewSection.h"
#include "../timeline/MainWindow.TimelineSection.h"
#include "../validation/MainWindow.ValidationSection.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "UiText.h"
#include "common/AssetPaths.h"
#include "common/PreviewSfxAssets.h"
#include "common/DebugOptions.h"
#include "common/DebugLog.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <QSoundEffect>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace miacode::mainwindow::shared;

namespace {

QString pointerHex(const void* pointer)
{
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(pointer), 0, 16);
}

QString qEventTypeName(QEvent::Type type)
{
    switch (type) {
    case QEvent::None:
        return QStringLiteral("None");
    case QEvent::Show:
        return QStringLiteral("Show");
    case QEvent::Hide:
        return QStringLiteral("Hide");
    case QEvent::Close:
        return QStringLiteral("Close");
    case QEvent::Move:
        return QStringLiteral("Move");
    case QEvent::Resize:
        return QStringLiteral("Resize");
    case QEvent::Polish:
        return QStringLiteral("Polish");
    case QEvent::PolishRequest:
        return QStringLiteral("PolishRequest");
    case QEvent::UpdateRequest:
        return QStringLiteral("UpdateRequest");
    case QEvent::WinIdChange:
        return QStringLiteral("WinIdChange");
    case QEvent::PlatformSurface:
        return QStringLiteral("PlatformSurface");
    case QEvent::Expose:
        return QStringLiteral("Expose");
    case QEvent::WindowActivate:
        return QStringLiteral("WindowActivate");
    case QEvent::WindowDeactivate:
        return QStringLiteral("WindowDeactivate");
    case QEvent::ActivationChange:
        return QStringLiteral("ActivationChange");
    case QEvent::FocusIn:
        return QStringLiteral("FocusIn");
    case QEvent::FocusOut:
        return QStringLiteral("FocusOut");
    case QEvent::ParentChange:
        return QStringLiteral("ParentChange");
    case QEvent::ShowToParent:
        return QStringLiteral("ShowToParent");
    case QEvent::HideToParent:
        return QStringLiteral("HideToParent");
    case QEvent::WindowStateChange:
        return QStringLiteral("WindowStateChange");
    case QEvent::WindowBlocked:
        return QStringLiteral("WindowBlocked");
    case QEvent::WindowUnblocked:
        return QStringLiteral("WindowUnblocked");
    case QEvent::Destroy:
        return QStringLiteral("Destroy");
    default:
        break;
    }
    return QStringLiteral("Type(%1)").arg(static_cast<int>(type));
}

QString platformSurfaceEventTypeName(QPlatformSurfaceEvent::SurfaceEventType type)
{
    switch (type) {
    case QPlatformSurfaceEvent::SurfaceCreated:
        return QStringLiteral("SurfaceCreated");
    case QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed:
        return QStringLiteral("SurfaceAboutToBeDestroyed");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(type));
}

QString windowVisibilityName(QWindow::Visibility visibility)
{
    switch (visibility) {
    case QWindow::Hidden:
        return QStringLiteral("Hidden");
    case QWindow::AutomaticVisibility:
        return QStringLiteral("AutomaticVisibility");
    case QWindow::Windowed:
        return QStringLiteral("Windowed");
    case QWindow::Minimized:
        return QStringLiteral("Minimized");
    case QWindow::Maximized:
        return QStringLiteral("Maximized");
    case QWindow::FullScreen:
        return QStringLiteral("FullScreen");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(visibility));
}

QString platformSurfaceDetail(const QEvent* event)
{
    const auto* surfaceEvent = dynamic_cast<const QPlatformSurfaceEvent*>(event);
    if (surfaceEvent == nullptr) {
        return QString();
    }
    return QStringLiteral("surface_event=%1")
        .arg(platformSurfaceEventTypeName(surfaceEvent->surfaceEventType()));
}

bool shouldTracePreviewHostWindowEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Expose:
    case QEvent::PlatformSurface:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
    case QEvent::Destroy:
        return true;
    default:
        return false;
    }
}

QString resolveInvalidStarPreviewReverseSoundPath()
{
    QStringList candidates;
    const auto appendCandidate = [&candidates](const QString& candidate) {
        if (candidate.isEmpty()) {
            return;
        }
        const QString cleanPath = QDir::cleanPath(candidate);
        if (!candidates.contains(cleanPath)) {
            candidates.append(cleanPath);
        }
    };

    const QString sfxDir = miacode::preview_sfx::resolveSfxDirectory();
    if (!sfxDir.isEmpty()) {
        appendCandidate(QDir(sfxDir).filePath(QStringLiteral("answer_reverse.wav")));
    }

    appendCandidate(miacode::assets::assetPath(QStringLiteral("SFX/answer_reverse.wav")));
    appendCandidate(miacode::assets::assetPath(QStringLiteral("sfx/answer_reverse.wav")));

    const QDir appDir(QCoreApplication::applicationDirPath());
    appendCandidate(appDir.filePath(QStringLiteral("assets/SFX/answer_reverse.wav")));
    appendCandidate(appDir.filePath(QStringLiteral("SFX/answer_reverse.wav")));
    appendCandidate(appDir.filePath(QStringLiteral("sfx/answer_reverse.wav")));
    appendCandidate(appDir.filePath(QStringLiteral("../Resources/assets/SFX/answer_reverse.wav")));

    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

QString timestampLine(const QString& title)
{
    return miacode::debug_log::formatTitleLine(title);
}

}  // namespace

MainWindow::~MainWindow()
{
    shutdownPreviewStageMediaHost();
}

void MainWindow::WindowSection::configureRuntimeDebugOutput()
{
    owner_.runtimeDebugOutputEnabled_ = miacode::debug_options::runtimeDebugOutputEnabled();
}

void MainWindow::WindowSection::setupInitialWindowGeometry()
{
    QSize initialSize(
        miacode::window_parity::kInitialWindowWidth,
        miacode::window_parity::kInitialWindowHeight
    );
    if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
        const QRect workArea = screen->availableGeometry();
        initialSize.setWidth(qMin(
            initialSize.width(),
            qMax(miacode::window_parity::kInitialWindowFloorWidth, workArea.width() - 120)
        ));
        initialSize.setHeight(qMin(
            initialSize.height(),
            qMax(miacode::window_parity::kInitialWindowFloorHeight, workArea.height() - 120)
        ));
        owner_.resize(initialSize);
        owner_.move(workArea.center() - QPoint(owner_.width() / 2, owner_.height() / 2));
        return;
    }
    owner_.resize(initialSize);
}

QString MainWindow::WindowSection::formatWindowStateFlags(Qt::WindowStates states) const
{
    QStringList flags;
    if (states.testFlag(Qt::WindowMinimized)) {
        flags.append("minimized");
    }
    if (states.testFlag(Qt::WindowMaximized)) {
        flags.append("maximized");
    }
    if (states.testFlag(Qt::WindowFullScreen)) {
        flags.append("fullscreen");
    }
    if (states.testFlag(Qt::WindowActive)) {
        flags.append("active");
    }
    if (flags.isEmpty()) {
        flags.append("normal");
    }
    return flags.join('|');
}

void MainWindow::WindowSection::logWindowGeometryDebug(const QString& tag, const QString& detail)
{
    if (!owner_.runtimeDebugOutputEnabled_) {
        return;
    }

    const QRect clientRect = owner_.geometry();
    const QRect frameRect = owner_.frameGeometry();
    const Qt::WindowStates states = owner_.windowState();

    QString payload = QString(
        "seq=%1 tag=%2 geom=[%3,%4 %5x%6] frame=[%7,%8 %9x%10] state=%11 "
        "active=%12 visible=%13 minimized=%14 maximized=%15 fullscreen=%16 "
        "suspend_depth=%17 arrange_gen=%18 arrange_retry=%19"
    )
        .arg(++owner_.windowEventDebugSequence_)
        .arg(tag)
        .arg(clientRect.left())
        .arg(clientRect.top())
        .arg(clientRect.width())
        .arg(clientRect.height())
        .arg(frameRect.left())
        .arg(frameRect.top())
        .arg(frameRect.width())
        .arg(frameRect.height())
        .arg(this->formatWindowStateFlags(states))
        .arg(owner_.isActiveWindow() ? 1 : 0)
        .arg(owner_.isVisible() ? 1 : 0)
        .arg(owner_.isMinimized() ? 1 : 0)
        .arg(owner_.isMaximized() ? 1 : 0)
        .arg(owner_.isFullScreen() ? 1 : 0)
        .arg(0)
        .arg(0)
        .arg(0);

    if (!detail.isEmpty()) {
        payload += " detail=" + detail;
    }

#ifdef Q_OS_WIN
    const HWND selfHwnd = reinterpret_cast<HWND>(owner_.winId());
    const HWND foregroundHwnd = GetForegroundWindow();
    const HWND foregroundOwner = foregroundHwnd != nullptr ? GetWindow(foregroundHwnd, GW_OWNER) : nullptr;
    const HWND foregroundRootOwner = foregroundHwnd != nullptr ? GetAncestor(foregroundHwnd, GA_ROOTOWNER) : nullptr;
    payload += QString(" self=0x%1 fg=0x%2 fg_owner=0x%3 fg_root_owner=0x%4 zoomed=%5 iconic=%6")
                   .arg(reinterpret_cast<quintptr>(selfHwnd), 0, 16)
                   .arg(reinterpret_cast<quintptr>(foregroundHwnd), 0, 16)
                   .arg(reinterpret_cast<quintptr>(foregroundOwner), 0, 16)
                   .arg(reinterpret_cast<quintptr>(foregroundRootOwner), 0, 16)
                   .arg(selfHwnd != nullptr ? (IsZoomed(selfHwnd) ? 1 : 0) : -1)
                   .arg(selfHwnd != nullptr ? (IsIconic(selfHwnd) ? 1 : 0) : -1);
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(WINDOWPLACEMENT);
    if (selfHwnd != nullptr && GetWindowPlacement(selfHwnd, &placement)) {
        payload += QString(" wp_show=%1 wp_normal=[%2,%3 %4x%5]")
                       .arg(static_cast<int>(placement.showCmd))
                       .arg(placement.rcNormalPosition.left)
                       .arg(placement.rcNormalPosition.top)
                       .arg(placement.rcNormalPosition.right - placement.rcNormalPosition.left)
                       .arg(placement.rcNormalPosition.bottom - placement.rcNormalPosition.top);
    } else {
        payload += " wp_show=-1";
    }
#endif

    this->appendOutput("window/event", payload);
}

void MainWindow::WindowSection::logTopLevelWindowSnapshot(const QString& tag)
{
    if (!owner_.runtimeDebugOutputEnabled_) {
        return;
    }

    QStringList lines;
    const auto topLevels = QApplication::topLevelWidgets();
    lines.reserve(topLevels.size() + 1);
    lines.append(QString("tag=%1 count=%2").arg(tag).arg(topLevels.size()));
    int index = 0;
    for (QWidget* window : topLevels) {
        if (window == nullptr) {
            continue;
        }
        const QRect geom = window->geometry();
#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        const HWND owner = hwnd != nullptr ? GetWindow(hwnd, GW_OWNER) : nullptr;
        const HWND rootOwner = hwnd != nullptr ? GetAncestor(hwnd, GA_ROOTOWNER) : nullptr;
        const QString nativeDetail = QString(" wid=0x%1 owner=0x%2 root_owner=0x%3 zoomed=%4 iconic=%5")
                                         .arg(reinterpret_cast<quintptr>(hwnd), 0, 16)
                                         .arg(reinterpret_cast<quintptr>(owner), 0, 16)
                                         .arg(reinterpret_cast<quintptr>(rootOwner), 0, 16)
                                         .arg(hwnd != nullptr ? (IsZoomed(hwnd) ? 1 : 0) : -1)
                                         .arg(hwnd != nullptr ? (IsIconic(hwnd) ? 1 : 0) : -1);
#else
        const QString nativeDetail;
#endif
        lines.append(
            QString("[%1] class=%2 title=%3 vis=%4 active=%5 modal=%6 state=%7 geom=[%8,%9 %10x%11]%12")
                .arg(index++)
                .arg(window->metaObject() != nullptr ? window->metaObject()->className() : "unknown")
                .arg(window->windowTitle().isEmpty() ? "(empty)" : window->windowTitle())
                .arg(window->isVisible() ? 1 : 0)
                .arg(window->isActiveWindow() ? 1 : 0)
                .arg(window->isModal() ? 1 : 0)
                .arg(this->formatWindowStateFlags(window->windowState()))
                .arg(geom.left())
                .arg(geom.top())
                .arg(geom.width())
                .arg(geom.height())
                .arg(nativeDetail)
        );
    }
    this->appendOutput("window/top_levels", lines.join('\n'));
}

void MainWindow::WindowSection::closeEvent(QCloseEvent* event)
{
    this->logWindowGeometryDebug("close_event_enter");
    if (owner_.maybeSaveBeforeContinue()) {
        owner_.savePortableState();
        owner_.exportSection_->clearVideoExportWorkerState();
        event->accept();
        this->logWindowGeometryDebug("close_event_accept");
    } else {
        event->ignore();
        this->logWindowGeometryDebug("close_event_ignore");
    }
}

void MainWindow::WindowSection::appendOutput(const QString& title, const QString& payload)
{
    if (!owner_.runtimeDebugOutputEnabled_) {
        return;
    }
    miacode::debug_log::appendText(
        miacode::debug_log::Channel::Runtime,
        timestampLine(title) + QStringLiteral("\n") + payload + QStringLiteral("\n")
    );
    if (owner_.outputView_ == nullptr) {
        return;
    }
    owner_.outputView_->appendPlainText(timestampLine(title));
    owner_.outputView_->appendPlainText(payload);
    owner_.outputView_->appendPlainText(QString());
}

QList<QAction*> MainWindow::WindowSection::quickShellShortcutActions() const
{
    return {
        owner_.newAction_,
        owner_.openAction_,
        owner_.saveAction_,
        owner_.saveAsAction_,
        owner_.findReplaceAction_,
        owner_.validateAction_,
        owner_.transformMirrorLeftRightAction_,
        owner_.transformMirrorUpDownAction_,
        owner_.transformRotate180Action_,
        owner_.transformRotate45CounterClockwiseAction_,
        owner_.transformRotate45ClockwiseAction_,
        owner_.normalizeWholeChartAction_,
        owner_.transformToggleBreakAction_,
        owner_.transformToggleExAction_,
        owner_.transformToggleFireworkAction_,
        owner_.transformRandomRotateAction_,
        owner_.stopPreviewAction_,
        owner_.pausePreviewAction_,
        owner_.previewSlowerAction_,
        owner_.previewFasterAction_,
        owner_.exportVideoAction_,
        owner_.latencyDetectorAction_,
        owner_.previewAudioSettingsAction_,
        owner_.previewVideoSettingsAction_,
        owner_.swapWorkspaceSidesAction_,
        owner_.preferencesAction_,
        owner_.aboutAction_,
    };
}

void MainWindow::WindowSection::refreshQuickShellRehostedWidgetParent(QWidget* widget)
{
    if (widget == nullptr) {
        return;
    }
    if (auto* dock = qobject_cast<QDockWidget*>(widget); dock != nullptr) {
        if (QWidget* dockWidget = dock->widget(); dockWidget != nullptr) {
            dockWidget->updateGeometry();
        }
    }
    if (QLayout* layout = widget->layout(); layout != nullptr) {
        layout->activate();
    }
    widget->updateGeometry();
    widget->update();
    widget->show();
    if (QWidget* parentWidget = widget->parentWidget(); parentWidget != nullptr) {
        if (QLayout* parentLayout = parentWidget->layout(); parentLayout != nullptr) {
            parentLayout->activate();
        }
        parentWidget->updateGeometry();
        parentWidget->update();
        parentWidget->show();
    }
}

void MainWindow::WindowSection::setInvalidStarPreviewEasterEggEnabled(bool enabled)
{
    if (owner_.invalidStarPreviewEasterEggEnabled_ == enabled) {
        return;
    }
    owner_.invalidStarPreviewEasterEggEnabled_ = enabled;
    SimaiNativeParser::setInvalidStarPreviewEnabled(enabled);
    owner_.refreshTimelineMetadata();
    this->playInvalidStarPreviewEasterEggSound(enabled);
}

void MainWindow::WindowSection::ensureInvalidStarPreviewEasterEggSounds()
{
    if (owner_.invalidStarPreviewEnableSound_ == nullptr) {
        owner_.invalidStarPreviewEnableSound_ = new QSoundEffect(&owner_);
        owner_.invalidStarPreviewEnableSound_->setLoopCount(1);
        owner_.invalidStarPreviewEnableSound_->setVolume(0.45f);
    }
    if (owner_.invalidStarPreviewDisableSound_ == nullptr) {
        owner_.invalidStarPreviewDisableSound_ = new QSoundEffect(&owner_);
        owner_.invalidStarPreviewDisableSound_->setLoopCount(1);
        owner_.invalidStarPreviewDisableSound_->setVolume(0.45f);
    }

    const QString sfxDir = miacode::preview_sfx::resolveSfxDirectory();
    const QString forwardPath = miacode::preview_sfx::assetFilePathForKind(sfxDir, QStringLiteral("answer"));
    const QString reversePath = resolveInvalidStarPreviewReverseSoundPath();

    const QUrl forwardUrl = QFileInfo::exists(forwardPath) ? QUrl::fromLocalFile(forwardPath) : QUrl();
    const QUrl reverseUrl = QFileInfo::exists(reversePath) ? QUrl::fromLocalFile(reversePath) : QUrl();

    if (owner_.invalidStarPreviewDisableSound_->source() != forwardUrl) {
        owner_.invalidStarPreviewDisableSound_->setSource(forwardUrl);
    }
    if (owner_.invalidStarPreviewEnableSound_->source() != reverseUrl) {
        owner_.invalidStarPreviewEnableSound_->setSource(reverseUrl);
    }
}

void MainWindow::WindowSection::playInvalidStarPreviewEasterEggSound(bool enabled)
{
    this->ensureInvalidStarPreviewEasterEggSounds();
    QSoundEffect* effect = enabled ? owner_.invalidStarPreviewEnableSound_ : owner_.invalidStarPreviewDisableSound_;
    if (effect == nullptr || effect->source().isEmpty()) {
        return;
    }
    effect->stop();
    effect->play();
}

