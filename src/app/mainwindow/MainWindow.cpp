#include "MainWindow.h"
#include "MainWindowShared.h"
#include "sections/editor/MainWindow.EditorSection.h"
#include "sections/document/MainWindow.DocumentSection.h"
#include "sections/dialogs/MainWindow.DialogsSection.h"
#include "sections/export/MainWindow.ExportSection.h"
#include "sections/frame/MainWindow.FrameSection.h"
#include "sections/preferences/MainWindow.PreferencesSection.h"
#include "sections/preview/MainWindow.PreviewSection.h"
#include "sections/timeline/MainWindow.TimelineSection.h"
#include "sections/validation/MainWindow.ValidationSection.h"
#include "sections/window/MainWindow.WindowSection.h"
#include "AppVersion.h"
#include "BracketScopeHighlighter.h"
#include "PlainCodeEditor.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "app/quick_shell/QuickShellPreviewSurfacePolicy.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "WindowParityMetrics.h"
#include "core/chart/transform/ChartBatchTransform.h"
#include "core/chart/transform/ChartNormalization.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"
#include "tools/video_export/VideoExportDialog.h"
#include "tools/video_export/VideoExportController.h"
#include "common/AssetPaths.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewGameplayConfig.h"
#include "common/PreviewInteractionConfig.h"

#include <algorithm>
#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QWindow>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleValidator>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontInfo>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QGridLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMoveEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPlatformSurfaceEvent>
#include <QPropertyAnimation>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QEasingCurve>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QSoundEffect>
#include <QSplitter>
#include <QSplitterHandle>
#include <QSpinBox>
#include <QScreen>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QSysInfo>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QStyleOptionSlider>
#include <QStringList>
#include <QTimer>
#include <QTextBlock>
#include <QTextStream>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextOption>
#include <QToolBar>
#include <QThreadPool>
#include <QWidgetAction>
#include <QToolTip>
#include <QUrl>
#include <QWheelEvent>
#include <QWindowStateChangeEvent>
#include <QUuid>
#include <QtMath>
#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#endif
#include <QVBoxLayout>
#include <QShowEvent>
#include <QSizePolicy>

#include <cmath>

#include "../third_party/miniaudio/miniaudio.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace miacode::mainwindow::shared;

namespace {
constexpr qreal kEmbeddedPreviewPanelWidthRatio = miacode::window_parity::kEmbeddedPreviewPanelWidthRatio;
constexpr int kEmbeddedPreviewPanelWidthMax = miacode::window_parity::kEmbeddedPreviewPanelWidthMax;
constexpr int kPreviewPanelMarginTop = miacode::window_parity::kPreviewPanelMarginTop;
constexpr int kPreviewPanelMarginBottom = 12;
constexpr int kPreviewCanvasControlGap = miacode::window_parity::kPreviewCanvasControlGap;
constexpr int kPreviewStatsBottomGap = miacode::window_parity::kPreviewStatsBottomGap;
constexpr int kPreviewControlStatsGap = miacode::window_parity::kPreviewControlStatsGap;
constexpr int kPreviewFullscreenControlsAnimationDurationMs = 180;
constexpr int kPreviewFullscreenControlsOpacityAnimationDurationMs = 180;
constexpr int kEmbeddedPreviewResizeSettleDelayMs = 120;
constexpr int kEditorFindBarMinWidth = 300;
constexpr int kEditorFindBarMaxWidth = 500;
constexpr int kEditorFindBarHorizontalMargin = 14;
constexpr int kEditorFindBarTopMargin = 10;
constexpr int kEditorFindBarOverlayGap = 8;
constexpr int kPreviewScrubRenderIntervalMs = 67;
constexpr qint64 kInvalidStarPreviewAboutClickWindowMs = 900;

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

bool actionMatchesShortcut(QAction* action, const QKeySequence& sequence)
{
    if (action == nullptr || sequence.isEmpty()) {
        return false;
    }
    QList<QKeySequence> shortcuts = action->shortcuts();
    if (shortcuts.isEmpty() && !action->shortcut().isEmpty()) {
        shortcuts.append(action->shortcut());
    }
    for (const QKeySequence& shortcut : shortcuts) {
        if (!shortcut.isEmpty() && shortcut == sequence) {
            return true;
        }
    }
    return false;
}

QString platformSurfaceEventTypeName(QPlatformSurfaceEvent::SurfaceEventType type)
{
    switch (type) {
    case QPlatformSurfaceEvent::SurfaceCreated:
        return QStringLiteral("SurfaceCreated");
    case QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed:
        return QStringLiteral("SurfaceAboutToBeDestroyed");
    }
    return QStringLiteral("SurfaceType(%1)").arg(static_cast<int>(type));
}

QString windowVisibilityName(QWindow::Visibility visibility)
{
    switch (visibility) {
    case QWindow::Hidden:
        return QStringLiteral("Hidden");
    case QWindow::AutomaticVisibility:
        return QStringLiteral("Automatic");
    case QWindow::Windowed:
        return QStringLiteral("Windowed");
    case QWindow::Minimized:
        return QStringLiteral("Minimized");
    case QWindow::Maximized:
        return QStringLiteral("Maximized");
    case QWindow::FullScreen:
        return QStringLiteral("FullScreen");
    }
    return QStringLiteral("Visibility(%1)").arg(static_cast<int>(visibility));
}

QString platformSurfaceDetail(const QEvent* event)
{
    if (event == nullptr || event->type() != QEvent::PlatformSurface) {
        return QString();
    }
    const auto* surfaceEvent = static_cast<const QPlatformSurfaceEvent*>(event);
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

QIcon makeRecycleTrashIcon(const QColor& color)
{
    QPixmap pixmap(20, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(7.2, 4.0), QPointF(13.0, 4.0));
    painter.drawLine(QPointF(8.2, 2.8), QPointF(12.0, 2.8));
    painter.drawRoundedRect(QRectF(6.4, 4.6, 7.4, 8.4), 1.5, 1.5);
    painter.drawArc(QRectF(8.1, 6.2, 3.2, 3.2), 30 * 16, 260 * 16);
    painter.drawArc(QRectF(8.8, 7.0, 3.0, 3.0), 210 * 16, 230 * 16);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(QPolygonF{
        QPointF(11.6, 6.3),
        QPointF(12.9, 6.6),
        QPointF(12.0, 7.6),
    });
    painter.drawPolygon(QPolygonF{
        QPointF(8.2, 9.6),
        QPointF(7.0, 9.1),
        QPointF(7.8, 8.2),
    });
    return QIcon(pixmap);
}

QIcon makePreviewCursorIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(QPolygonF{
        QPointF(2.0, 3.0),
        QPointF(11.5, 10.0),
        QPointF(2.0, 17.0),
    });
    painter.drawPolygon(QPolygonF{
        QPointF(12.5, 2.5),
        QPointF(18.0, 12.5),
        QPointF(15.1, 12.2),
        QPointF(14.4, 17.2),
        QPointF(12.6, 16.9),
        QPointF(13.2, 11.8),
        QPointF(10.8, 13.4),
    });
    painter.end();
    return QIcon(pixmap);
}

}  // namespace

namespace {

struct PreviewMediaWarmupResult {
    quint64 generation = 0;
    QString chartPath;
    QString trackPath;
    QString resolvedMediaPath;
    qint64 workerElapsedMs = -1;
};

struct PreviewSfxWarmupResult {
    quint64 generation = 0;
    QString chartPath;
    QString trackPath;
    QString sfxDir;
    qint64 workerElapsedMs = -1;
};

QStringList previewSfxWarmupKinds()
{
    static const QStringList kinds{
        QStringLiteral("answer"),
        QStringLiteral("judge"),
        QStringLiteral("judge_break"),
        QStringLiteral("slide"),
        QStringLiteral("break"),
        QStringLiteral("break_slide_start"),
        QStringLiteral("break_slide"),
        QStringLiteral("judge_break_slide"),
        QStringLiteral("ex"),
        QStringLiteral("touch"),
        QStringLiteral("touchhold"),
        QStringLiteral("firework"),
    };
    return kinds;
}

void warmupFileIntoOsCache(const QString& path, qint64 maxBytes = -1)
{
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    constexpr qint64 kChunkBytes = 64 * 1024;
    qint64 remainingBytes = maxBytes;
    while (!file.atEnd()) {
        if (remainingBytes == 0) {
            break;
        }
        const qint64 requestBytes = remainingBytes > 0 ? qMin(remainingBytes, kChunkBytes) : kChunkBytes;
        const QByteArray chunk = file.read(requestBytes);
        if (chunk.isEmpty()) {
            break;
        }
        if (remainingBytes > 0) {
            remainingBytes -= chunk.size();
        }
    }
}

}  // namespace
