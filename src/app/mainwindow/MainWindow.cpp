#include "MainWindow.h"
#include "AppVersion.h"
#include "BracketScopeHighlighter.h"
#include "PlainCodeEditor.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "PreviewMediaController.h"
#include "QtPreviewSfxRuntime.h"
#include "SimaiNativeParser.h"
#include "TimelineView.h"
#include "preview/scene/PreviewProgressStatsCache.h"
#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "WindowParityMetrics.h"
#include "simai/transform/ChartBatchTransform.h"
#include "simai/transform/ChartNormalization.h"
#include "tools/latency/LatencyDetectorDialog.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"
#include "tools/video_export/VideoExportDialog.h"
#include "tools/video_export/BatchVideoExportDialog.h"
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
#include <QThread>
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

namespace {
constexpr int kEmbeddedPreviewPanelMinWidth = miacode::window_parity::kEmbeddedPreviewPanelMinWidth;
constexpr qreal kEmbeddedPreviewPanelWidthRatio = miacode::window_parity::kEmbeddedPreviewPanelWidthRatio;
constexpr int kEmbeddedPreviewPanelWidthMax = miacode::window_parity::kEmbeddedPreviewPanelWidthMax;
constexpr int kPreviewPanelMarginX = miacode::window_parity::kPreviewPanelMarginX;
constexpr int kPreviewPanelMarginTop = miacode::window_parity::kPreviewPanelMarginTop;
constexpr int kPreviewPanelMarginBottom = 12;
constexpr int kPreviewCanvasControlGap = miacode::window_parity::kPreviewCanvasControlGap;
constexpr int kPreviewStatsBottomGap = miacode::window_parity::kPreviewStatsBottomGap;
constexpr int kPreviewControlStatsGap = miacode::window_parity::kPreviewControlStatsGap;
constexpr int kPreviewControlStatsCardMinWidth = miacode::window_parity::kPreviewControlStatsCardMinWidth;
constexpr int kPreviewFullscreenHintTopMargin = miacode::window_parity::kPreviewFullscreenHintTopMargin;
constexpr int kPreviewFullscreenOverlaySideMargin = miacode::window_parity::kPreviewFullscreenOverlaySideMargin;
constexpr int kPreviewFullscreenOverlayBottomMargin = miacode::window_parity::kPreviewFullscreenOverlayBottomMargin;
constexpr int kPreviewFullscreenOverlayMaxWidth = miacode::window_parity::kPreviewFullscreenOverlayMaxWidth;
constexpr int kPreviewFullscreenOverlayHideOffset = miacode::window_parity::kPreviewFullscreenOverlayHideOffset;
constexpr int kPreviewFullscreenControlsRevealHotzoneHeight = miacode::window_parity::kPreviewFullscreenControlsRevealHotzoneHeight;
constexpr int kPreviewFullscreenControlsAutoHideDelayMs = miacode::window_parity::kPreviewFullscreenControlsAutoHideDelayMs;
constexpr int kPreviewFullscreenControlsAnimationDurationMs = 180;
constexpr int kPreviewFullscreenControlsOpacityAnimationDurationMs = 180;
constexpr int kEmbeddedPreviewResizeSettleDelayMs = 120;
constexpr int kEditorTextFontSizeMin = 8;
constexpr int kEditorTextFontSizeMax = 28;
constexpr int kWaveformPeakCount = 1024;
constexpr double kEditorLineSpacingFactorDefault = 1.5;
constexpr int kEditorFindBarMinWidth = 300;
constexpr int kEditorFindBarMaxWidth = 500;
constexpr int kEditorFindBarHorizontalMargin = 14;
constexpr int kEditorFindBarTopMargin = 10;
constexpr int kEditorFindBarOverlayGap = 8;
constexpr int kPreviewScrubRenderIntervalMs = 33;
constexpr qint64 kInvalidStarPreviewAboutClickWindowMs = 900;
constexpr int kAutosaveIntervalMs = 15 * 60 * 1000;
constexpr int kAutosaveMaxVersions = 10;
const QList<double> kEditorLineSpacingFactorOptions{
    0.0, 1.0, 1.5, 2.0, 3.0, 5.0,
};
const QList<double> kPreviewPlaybackRateOptions{
    0.25, 0.5, 0.75, 1.0, 1.25, 2.0,
};

QString pointerHex(const void* pointer)
{
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(pointer), 0, 16);
}

QByteArray autosaveContentSignature(const QString& text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
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

bool shouldTraceDebugDialogWidgetEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Close:
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Polish:
    case QEvent::PolishRequest:
    case QEvent::WinIdChange:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
    case QEvent::ActivationChange:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::ParentChange:
    case QEvent::ShowToParent:
    case QEvent::HideToParent:
    case QEvent::Destroy:
        return true;
    default:
        return false;
    }
}

bool shouldTraceDebugDialogWindowEvent(QEvent::Type type)
{
    switch (type) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Expose:
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::PlatformSurface:
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
    case QEvent::WindowStateChange:
    case QEvent::Destroy:
        return true;
    default:
        return false;
    }
}

QString cssRgba(const QColor& color, int alpha)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(qBound(0, alpha, 255));
}

QString cssRgb(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

QColor previewFullscreenOverlayIconColor()
{
    return QColor(QStringLiteral("#F8FAFC"));
}

QString previewFullscreenControlCardStyleSheet()
{
    const QColor overlayText = previewFullscreenOverlayIconColor();
    const QColor accent = UiTheme::colors().accent;
    return QStringLiteral(
        "QFrame#PreviewControlCard {"
        " background: rgba(0, 0, 0, 188);"
        " border: 1px solid rgba(255, 255, 255, 42);"
        " border-radius: 18px;"
        "}"
        "QFrame#PreviewControls {"
        " background: transparent;"
        " border: none;"
        "}"
        "QToolButton#PreviewControlButton {"
        " color: %1;"
        " padding: 7px 10px;"
        " min-height: 32px;"
        " border: 1px solid rgba(255, 255, 255, 40);"
        " border-radius: 9px;"
        " background: rgba(255, 255, 255, 18);"
        " font-weight: 600;"
        "}"
        "QToolButton#PreviewControlButton:hover {"
        " background: rgba(255, 255, 255, 32);"
        " border-color: rgba(255, 255, 255, 76);"
        "}"
        "QToolButton#PreviewControlButton:pressed {"
        " background: rgba(255, 255, 255, 46);"
        "}"
        "QSlider::groove:horizontal {"
        " height: 6px;"
        " background: rgba(255, 255, 255, 42);"
        " border-radius: 3px;"
        "}"
        "QSlider::sub-page:horizontal {"
        " background: %2;"
        " border-radius: 3px;"
        "}"
        "QSlider::add-page:horizontal {"
        " background: rgba(255, 255, 255, 22);"
        " border-radius: 3px;"
        "}"
        "QSlider::handle:horizontal {"
        " width: 12px;"
        " margin: -4px 0;"
        " border-radius: 6px;"
        " background: %1;"
        " border: 1px solid rgba(15, 23, 42, 102);"
        "}"
    )
        .arg(cssRgb(overlayText))
        .arg(cssRgb(accent));
}

QString previewFullscreenHintStyleSheet()
{
    return QStringLiteral(
        "QLabel {"
        " background: rgba(0, 0, 0, 188);"
        " color: #F8FAFC;"
        " border: 1px solid rgba(255, 255, 255, 52);"
        " border-radius: 16px;"
        " padding: 10px 16px;"
        " font-size: 14px;"
        " font-weight: 600;"
        "}"
    );
}

QString outlineCollapseButtonStyleSheet()
{
    const UiTheme::Colors& c = UiTheme::colors();
    return QStringLiteral(
        "QToolButton {"
        " color: %1;"
        " background: %2;"
        " border: none;"
        " border-left: 1px solid %3;"
        " padding: 0px;"
        " font-size: 12px;"
        " font-weight: 700;"
        "}"
        "QToolButton:hover {"
        " background: %4;"
        "}"
        "QToolButton:pressed {"
        " background: %5;"
        "}"
    )
        .arg(cssRgb(c.iconSecondary))
        .arg(cssRgb(c.cardAltBg))
        .arg(cssRgb(c.border))
        .arg(cssRgb(c.menuHoverBg))
        .arg(cssRgb(c.borderSoft));
}

QString previewFullscreenPauseButtonStyleSheet(bool active)
{
    const QColor overlayText = previewFullscreenOverlayIconColor();
    const QColor accent = UiTheme::colors().accent;
    if (active) {
        return QStringLiteral(
            "QToolButton {"
            " color: %1;"
            " padding: 7px 10px;"
            " min-height: 32px;"
            " border: 1px solid %2;"
            " border-radius: 9px;"
            " background: %3;"
            " font-weight: 700;"
            "}"
            "QToolButton:hover { background: %4; }"
        )
            .arg(cssRgb(overlayText))
            .arg(cssRgba(accent, 220))
            .arg(cssRgba(accent, 208))
            .arg(cssRgba(accent, 255));
    }
    return QStringLiteral(
        "QToolButton {"
        " color: %1;"
        " padding: 7px 10px;"
        " min-height: 32px;"
        " border: 1px solid rgba(255, 255, 255, 40);"
        " border-radius: 9px;"
        " background: rgba(255, 255, 255, 18);"
        " font-weight: 600;"
        "}"
        "QToolButton:hover {"
        " background: rgba(255, 255, 255, 32);"
        " border-color: rgba(255, 255, 255, 76);"
        "}"
    )
        .arg(cssRgb(overlayText));
}

double normalizeEditorLineSpacingFactor(double factor)
{
    if (kEditorLineSpacingFactorOptions.isEmpty()) {
        return kEditorLineSpacingFactorDefault;
    }
    double best = kEditorLineSpacingFactorOptions.first();
    double bestDiff = qAbs(best - factor);
    for (double candidate : kEditorLineSpacingFactorOptions) {
        const double diff = qAbs(candidate - factor);
        if (diff < bestDiff) {
            best = candidate;
            bestDiff = diff;
        }
    }
    return best;
}

QString editorLineSpacingFactorLabel(double factor)
{
    if (qFuzzyCompare(factor + 1.0, 1.0)) {
        return QStringLiteral("0x");
    }
    const QString text = QString::number(factor, 'f', qFuzzyCompare(factor, qRound(factor)) ? 0 : 1);
    return text + QStringLiteral("x");
}

int nearestPreviewPlaybackRateIndex(double rate)
{
    if (kPreviewPlaybackRateOptions.isEmpty()) {
        return -1;
    }
    int bestIndex = 0;
    double bestDiff = qAbs(kPreviewPlaybackRateOptions.first() - rate);
    for (int index = 1; index < kPreviewPlaybackRateOptions.size(); ++index) {
        const double diff = qAbs(kPreviewPlaybackRateOptions[index] - rate);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIndex = index;
        }
    }
    return bestIndex;
}

double steppedPreviewPlaybackRate(double rate, int direction)
{
    const int currentIndex = nearestPreviewPlaybackRateIndex(rate);
    if (currentIndex < 0) {
        return qMax(0.25, rate);
    }
    const int targetIndex = qBound(0, currentIndex + direction, kPreviewPlaybackRateOptions.size() - 1);
    return kPreviewPlaybackRateOptions[targetIndex];
}

QString sanitizeExportFileStem(QString text, const QString& fallback = QStringLiteral("out"))
{
    text = text.trimmed();
    QString sanitized;
    sanitized.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.isNull() || ch.isLowSurrogate() || ch.isHighSurrogate()) {
            continue;
        }
        if (ch.unicode() < 0x20 || QStringLiteral("<>:\"/\\|?*").contains(ch)) {
            sanitized.append(QLatin1Char('_'));
            continue;
        }
        sanitized.append(ch);
    }
    sanitized = sanitized.trimmed();
    while (!sanitized.isEmpty() && (sanitized.endsWith(QLatin1Char('.')) || sanitized.endsWith(QLatin1Char(' ')))) {
        sanitized.chop(1);
    }
    return sanitized.isEmpty() ? fallback : sanitized;
}

QString appendMp4SuffixIfMissing(QString outputPath)
{
    outputPath = QDir::cleanPath(QDir::fromNativeSeparators(outputPath.trimmed()));
    if (outputPath.isEmpty()) {
        return QString();
    }
    if (!outputPath.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) {
        outputPath += QStringLiteral(".mp4");
    }
    return outputPath;
}

QString makeUniqueVideoExportOutputPath(const QString& outputPath)
{
    const QString normalizedPath = appendMp4SuffixIfMissing(outputPath);
    if (normalizedPath.isEmpty()) {
        return QString();
    }

    const QFileInfo outputInfo(normalizedPath);
    const QDir outputDir = outputInfo.absoluteDir();
    QString fileStem = outputInfo.fileName().trimmed();
    if (fileStem.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)) {
        fileStem.chop(4);
    }
    if (fileStem.isEmpty()) {
        fileStem = QStringLiteral("out");
    }

    const auto buildCandidatePath = [&outputDir, &fileStem](int duplicateIndex) {
        const QString fileName = duplicateIndex <= 0
            ? QStringLiteral("%1.mp4").arg(fileStem)
            : QStringLiteral("%1(%2).mp4").arg(fileStem).arg(duplicateIndex);
        return QDir::cleanPath(outputDir.filePath(fileName));
    };

    QString candidatePath = buildCandidatePath(0);
    if (!QFileInfo::exists(candidatePath)) {
        return candidatePath;
    }

    for (int duplicateIndex = 1;; ++duplicateIndex) {
        candidatePath = buildCandidatePath(duplicateIndex);
        if (!QFileInfo::exists(candidatePath)) {
            return candidatePath;
        }
    }
}

QString resolveVideoExportOutputPath(
    const QString& requestedOutputPath,
    const QString& defaultDirectory,
    const QString& defaultOutputName
)
{
    const QString baseDirectory = defaultDirectory.trimmed().isEmpty()
        ? QDir::currentPath()
        : QDir::cleanPath(QDir::fromNativeSeparators(defaultDirectory.trimmed()));
    const QString normalizedDefaultName = appendMp4SuffixIfMissing(
        defaultOutputName.trimmed().isEmpty() ? QStringLiteral("out.mp4") : defaultOutputName
    );

    const QString trimmedRequestedOutput = requestedOutputPath.trimmed();
    QString resolvedOutputPath = QDir::fromNativeSeparators(trimmedRequestedOutput);
    if (resolvedOutputPath.isEmpty()) {
        resolvedOutputPath = QDir(baseDirectory).filePath(normalizedDefaultName);
    } else {
        const bool trailingSeparator = trimmedRequestedOutput.endsWith('/') || trimmedRequestedOutput.endsWith('\\');
        const QFileInfo requestedInfo(resolvedOutputPath);
        if (requestedInfo.isRelative()) {
            resolvedOutputPath = QDir(baseDirectory).absoluteFilePath(resolvedOutputPath);
        }

        const QFileInfo absoluteInfo(resolvedOutputPath);
        if ((absoluteInfo.exists() && absoluteInfo.isDir()) || trailingSeparator) {
            const QString outputDirPath = absoluteInfo.exists() && absoluteInfo.isDir()
                ? absoluteInfo.absoluteFilePath()
                : resolvedOutputPath;
            resolvedOutputPath = QDir(outputDirPath).filePath(normalizedDefaultName);
        }
    }

    return makeUniqueVideoExportOutputPath(resolvedOutputPath);
}

QString videoExportBackgroundScaleModeToken(PreviewBackgroundScaleMode mode)
{
    switch (mode) {
    case PreviewBackgroundScaleMode::FitContain:
        return QStringLiteral("fit");
    case PreviewBackgroundScaleMode::FillCrop:
    default:
        return QStringLiteral("fill");
    }
}

QByteArray buildVideoExportWorkerStartPayload(const VideoExportSnapshot& snapshot)
{
    QJsonObject commandObject;
    commandObject.insert(QStringLiteral("cmd"), QStringLiteral("start_export"));
    commandObject.insert(QStringLiteral("protocol"), 1);
    commandObject.insert(QStringLiteral("snapshot"), snapshot.toJson());
    QByteArray payload = QJsonDocument(commandObject).toJson(QJsonDocument::Compact);
    payload.append('\n');
    return payload;
}

QString videoExportWorkerLogPathForUi()
{
    return miacode::debug_log::exportLogPath();
}

QString qProcessExitStatusForUi(QProcess::ExitStatus status)
{
    switch (status) {
    case QProcess::NormalExit:
        return QStringLiteral("NormalExit");
    case QProcess::CrashExit:
        return QStringLiteral("CrashExit");
    }
    return QStringLiteral("Unknown(%1)").arg(static_cast<int>(status));
}

QString truncateProcessTextForUi(QString text, int maxChars = 2000)
{
    text = text.trimmed();
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(maxChars) + QStringLiteral(" ...<truncated>");
}

QString appendVideoExportDiagnostics(const QString& base, const QString& extra)
{
    const QString trimmedBase = base.trimmed();
    const QString trimmedExtra = extra.trimmed();
    if (trimmedBase.isEmpty()) {
        return trimmedExtra;
    }
    if (trimmedExtra.isEmpty()) {
        return trimmedBase;
    }
    return trimmedBase + QStringLiteral("\n\n") + trimmedExtra;
}

QString buildWorkerProcessDiagnostics(
    int exitCode,
    QProcess::ExitStatus exitStatus,
    const QString& processError,
    const QString& stderrText,
    const QString& stdoutTailText
)
{
    QStringList lines;
    lines.append(
        QStringLiteral("Worker exitCode=%1 exitStatus=%2")
            .arg(exitCode)
            .arg(qProcessExitStatusForUi(exitStatus))
    );
    if (!processError.trimmed().isEmpty()) {
        lines.append(QStringLiteral("Process error: %1").arg(truncateProcessTextForUi(processError, 500)));
    }
    if (!stderrText.trimmed().isEmpty()) {
        lines.append(QStringLiteral("stderr: %1").arg(truncateProcessTextForUi(stderrText, 2000)));
    }
    if (!stdoutTailText.trimmed().isEmpty()) {
        lines.append(QStringLiteral("stdout_tail: %1").arg(truncateProcessTextForUi(stdoutTailText, 1000)));
    }
    if (miacode::debug_options::exportDebugOutputEnabled()) {
        lines.append(QStringLiteral("Debug log: %1").arg(videoExportWorkerLogPathForUi()));
    }
    lines.append(QStringLiteral("Error log: %1").arg(miacode::debug_log::fatalLogPath()));
    return lines.join(QStringLiteral("\n"));
}

QString compactWorkerExitSummary(
    int exitCode,
    QProcess::ExitStatus exitStatus,
    const QString& fallbackMessage
)
{
    return QStringLiteral("%1 [%2, code=%3]")
        .arg(fallbackMessage)
        .arg(qProcessExitStatusForUi(exitStatus))
        .arg(exitCode);
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

#ifdef Q_OS_WIN
QString sanitizeNativeDebugText(QString text)
{
    text.replace('\r', QStringLiteral("\\r"));
    text.replace('\n', QStringLiteral("\\n"));
    if (text.isEmpty()) {
        return QStringLiteral("(empty)");
    }
    constexpr int kMaxLength = 96;
    if (text.size() > kMaxLength) {
        text = text.left(kMaxLength) + QStringLiteral("...");
    }
    return text;
}

QString describeNativeWindowHandle(HWND hwnd)
{
    if (hwnd == nullptr) {
        return QStringLiteral("hwnd=0x0");
    }

    wchar_t classNameBuf[256] = {};
    const int classNameLen = GetClassNameW(hwnd, classNameBuf, 256);
    const QString className = classNameLen > 0
        ? sanitizeNativeDebugText(QString::fromWCharArray(classNameBuf, classNameLen))
        : QStringLiteral("(none)");

    wchar_t titleBuf[512] = {};
    const int titleLen = GetWindowTextW(hwnd, titleBuf, 512);
    const QString title = titleLen > 0
        ? sanitizeNativeDebugText(QString::fromWCharArray(titleBuf, titleLen))
        : QStringLiteral("(empty)");

    RECT rect{};
    const BOOL hasRect = GetWindowRect(hwnd, &rect);
    const int rectX = hasRect ? rect.left : -1;
    const int rectY = hasRect ? rect.top : -1;
    const int rectW = hasRect ? (rect.right - rect.left) : -1;
    const int rectH = hasRect ? (rect.bottom - rect.top) : -1;

    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    const HWND owner = GetWindow(hwnd, GW_OWNER);
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    const HWND rootOwner = GetAncestor(hwnd, GA_ROOTOWNER);
    const HWND lastPopup = GetLastActivePopup(hwnd);

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(WINDOWPLACEMENT);
    const BOOL hasPlacement = GetWindowPlacement(hwnd, &placement);
    const int showCmd = hasPlacement ? static_cast<int>(placement.showCmd) : -1;
    const int normalX = hasPlacement ? placement.rcNormalPosition.left : -1;
    const int normalY = hasPlacement ? placement.rcNormalPosition.top : -1;
    const int normalW = hasPlacement
        ? (placement.rcNormalPosition.right - placement.rcNormalPosition.left)
        : -1;
    const int normalH = hasPlacement
        ? (placement.rcNormalPosition.bottom - placement.rcNormalPosition.top)
        : -1;

    const auto style = static_cast<qulonglong>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const auto exStyle = static_cast<qulonglong>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));

    return QString(
               "hwnd=0x%1 class=%2 title=%3 vis=%4 ena=%5 iconic=%6 zoomed=%7 "
               "owner=0x%8 root=0x%9 root_owner=0x%10 popup=0x%11 pid=%12 tid=%13 "
               "rect=[%14,%15 %16x%17] show=%18 normal=[%19,%20 %21x%22] style=0x%23 ex=0x%24"
           )
        .arg(reinterpret_cast<quintptr>(hwnd), 0, 16)
        .arg(className)
        .arg(title)
        .arg(IsWindowVisible(hwnd) ? 1 : 0)
        .arg(IsWindowEnabled(hwnd) ? 1 : 0)
        .arg(IsIconic(hwnd) ? 1 : 0)
        .arg(IsZoomed(hwnd) ? 1 : 0)
        .arg(reinterpret_cast<quintptr>(owner), 0, 16)
        .arg(reinterpret_cast<quintptr>(root), 0, 16)
        .arg(reinterpret_cast<quintptr>(rootOwner), 0, 16)
        .arg(reinterpret_cast<quintptr>(lastPopup), 0, 16)
        .arg(pid)
        .arg(tid)
        .arg(rectX)
        .arg(rectY)
        .arg(rectW)
        .arg(rectH)
        .arg(showCmd)
        .arg(normalX)
        .arg(normalY)
        .arg(normalW)
        .arg(normalH)
        .arg(style, 0, 16)
        .arg(exStyle, 0, 16);
}

enum class EditorValidationSummaryIconKind {
    Error,
    Warning,
    Muri,
};

QIcon makeMenuSelectionCheckIcon(const QColor& color, bool visible = true);

QPixmap makeEditorValidationSummaryIcon(const QColor& color, EditorValidationSummaryIconKind kind)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    if (kind == EditorValidationSummaryIconKind::Warning) {
        QPainterPath triangle;
        triangle.moveTo(7.0, 1.2);
        triangle.lineTo(12.8, 12.0);
        triangle.lineTo(1.2, 12.0);
        triangle.closeSubpath();
        painter.drawPath(triangle);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(6.2, 4.0, 1.6, 4.0), 0.8, 0.8);
        painter.drawEllipse(QRectF(6.1, 9.2, 1.8, 1.8));
    } else if (kind == EditorValidationSummaryIconKind::Muri) {
        painter.drawEllipse(QRectF(1.2, 1.2, 11.6, 11.6));
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(11);
        painter.setFont(font);
        painter.drawText(QRectF(0.2, 0.6, 13.6, 12.8), Qt::AlignCenter, QStringLiteral("?"));
    } else {
        painter.drawEllipse(QRectF(1.2, 1.2, 11.6, 11.6));
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(6.1, 3.4, 1.8, 5.0), 0.9, 0.9);
        painter.drawEllipse(QRectF(6.0, 9.5, 2.0, 2.0));
    }

    return pixmap;
}

bool tryRestoreOwnedNativeFileDialog(HWND ownerHwnd, QString* detailOut)
{
    if (ownerHwnd == nullptr) {
        return false;
    }

    const HWND foregroundHwnd = GetForegroundWindow();
    if (foregroundHwnd == nullptr || foregroundHwnd == ownerHwnd) {
        return false;
    }

    wchar_t classNameBuf[64] = {};
    const int classNameLen = GetClassNameW(foregroundHwnd, classNameBuf, 64);
    if (classNameLen <= 0 || QString::fromWCharArray(classNameBuf, classNameLen) != QStringLiteral("#32770")) {
        return false;
    }

    const HWND owner = GetWindow(foregroundHwnd, GW_OWNER);
    const HWND rootOwner = GetAncestor(foregroundHwnd, GA_ROOTOWNER);
    if (owner != ownerHwnd && rootOwner != ownerHwnd) {
        return false;
    }
    if (!IsZoomed(foregroundHwnd)) {
        return false;
    }

    WINDOWPLACEMENT before{};
    before.length = sizeof(WINDOWPLACEMENT);
    const BOOL hasBefore = GetWindowPlacement(foregroundHwnd, &before);
    ShowWindow(foregroundHwnd, SW_RESTORE);
    WINDOWPLACEMENT after{};
    after.length = sizeof(WINDOWPLACEMENT);
    const BOOL hasAfter = GetWindowPlacement(foregroundHwnd, &after);

    if (detailOut != nullptr) {
        *detailOut = QString("restore hwnd=0x%1 owner=0x%2 root_owner=0x%3 before_show=%4 after_show=%5")
                         .arg(reinterpret_cast<quintptr>(foregroundHwnd), 0, 16)
                         .arg(reinterpret_cast<quintptr>(owner), 0, 16)
                         .arg(reinterpret_cast<quintptr>(rootOwner), 0, 16)
                         .arg(hasBefore ? static_cast<int>(before.showCmd) : -1)
                         .arg(hasAfter ? static_cast<int>(after.showCmd) : -1);
    }
    return true;
}

struct RelatedNativeWindowEnumContext {
    HWND ownerHwnd = nullptr;
    HWND foregroundHwnd = nullptr;
    HWND activeHwnd = nullptr;
    HWND focusHwnd = nullptr;
    HWND foregroundRootOwner = nullptr;
    DWORD ownerPid = 0;
    QSet<quintptr> seen;
    QList<HWND> matches;
};

BOOL CALLBACK collectRelatedNativeWindowProc(HWND hwnd, LPARAM lParam)
{
    auto* context = reinterpret_cast<RelatedNativeWindowEnumContext*>(lParam);
    if (context == nullptr || hwnd == nullptr) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    const HWND owner = GetWindow(hwnd, GW_OWNER);
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    const HWND rootOwner = GetAncestor(hwnd, GA_ROOTOWNER);

    const bool relevantByHandle =
        hwnd == context->ownerHwnd
        || hwnd == context->foregroundHwnd
        || hwnd == context->activeHwnd
        || hwnd == context->focusHwnd;
    const bool relevantByOwnerChain =
        owner == context->ownerHwnd
        || root == context->ownerHwnd
        || rootOwner == context->ownerHwnd
        || owner == context->foregroundHwnd
        || rootOwner == context->foregroundRootOwner;
    const bool relevantByProcess = context->ownerPid != 0 && pid == context->ownerPid;
    const bool visibleInteresting =
        IsWindowVisible(hwnd)
        && (relevantByOwnerChain || hwnd == context->foregroundHwnd);

    if (!(relevantByHandle || relevantByOwnerChain || relevantByProcess || visibleInteresting)) {
        return TRUE;
    }

    const quintptr key = reinterpret_cast<quintptr>(hwnd);
    if (context->seen.contains(key)) {
        return TRUE;
    }
    context->seen.insert(key);
    context->matches.append(hwnd);
    return TRUE;
}
#endif

class OutlineItemDelegate : public QStyledItemDelegate {
public:
    explicit OutlineItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem drawOption(option);
        initStyleOption(&drawOption, index);
        const UiTheme::Colors& c = UiTheme::colors();
        const QColor selectedBorder = c.dark ? QColor("#6B8BB8") : QColor("#9EC2EF");
        const QColor selectedFill = c.dark ? QColor("#314158") : QColor("#F1F6FF");
        const QColor hoverFill = c.dark ? QColor("#2A3442") : QColor("#F3F7FD");

        painter->save();
        const QRect fillRect = option.rect.adjusted(1, 1, -1, -1);
        if (option.state.testFlag(QStyle::State_Selected)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(selectedBorder, 1.0));
            painter->setBrush(selectedFill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        } else if (option.state.testFlag(QStyle::State_MouseOver)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(hoverFill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        }
        painter->restore();

        const int listWidth = option.widget != nullptr ? option.widget->width() : option.rect.width();
        constexpr int kIconOnlyThreshold = 120;
        const bool iconOnly = listWidth > 0 && listWidth < kIconOnlyThreshold;
        if (iconOnly) {
            drawOption.text.clear();
            drawOption.features &= ~QStyleOptionViewItem::HasDisplay;
            drawOption.decorationAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        }

        drawOption.state &= ~QStyle::State_Selected;
        drawOption.state &= ~QStyle::State_MouseOver;
        drawOption.backgroundBrush = Qt::NoBrush;
        drawOption.palette.setColor(QPalette::HighlightedText, c.textPrimary);
        QStyledItemDelegate::paint(painter, drawOption, index);
    }
};

class LeftPlaceholderLineEdit : public QLineEdit {
public:
    explicit LeftPlaceholderLineEdit(QWidget* parent = nullptr)
        : QLineEdit(parent)
    {}

    void setLeftPlaceholderText(const QString& text)
    {
        leftPlaceholderText_ = text;
        QLineEdit::setPlaceholderText(QString());
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QLineEdit::paintEvent(event);
        if (!text().isEmpty() || leftPlaceholderText_.isEmpty()) {
            return;
        }

        QStyleOptionFrame option;
        initStyleOption(&option);
        const QRect contentsRect = style()->subElementRect(QStyle::SE_LineEditContents, &option, this);

        QPainter painter(this);
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.setFont(font());
        painter.drawText(contentsRect.adjusted(0, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, leftPlaceholderText_);
    }

private:
    QString leftPlaceholderText_;
};

QString timestampLine(const QString& title)
{
    return miacode::debug_log::formatTitleLine(title);
}

QString uiText(const QString& key, const QString& fallback)
{
    const QString localized = UiText::text(key);
    return localized.isEmpty() ? fallback : localized;
}

QString normalizeLanguageToken(QString token)
{
    token = token.trimmed().toLower();
    token.replace('-', '_');
    return token;
}

bool systemLanguagePrefersChinese()
{
    static const bool prefersChinese = []() {
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        for (const QString& language : uiLanguages) {
            const QString token = normalizeLanguageToken(language);
            if (token.startsWith(QStringLiteral("zh"))) {
                return true;
            }
            if (token.startsWith(QStringLiteral("en"))) {
                return false;
            }
        }
        return normalizeLanguageToken(QLocale::system().name()).startsWith(QStringLiteral("zh"));
    }();
    return prefersChinese;
}

QString systemL10n(const QString& en, const QString& zh)
{
    return systemLanguagePrefersChinese() ? zh : en;
}

void centerDialogOnAnchor(QDialog* dialog, QWidget* parent)
{
    if (dialog == nullptr) {
        return;
    }
    dialog->adjustSize();

    QWidget* anchorWidget = parent != nullptr ? parent->window() : nullptr;
    QRect anchorRect;
    if (anchorWidget != nullptr && anchorWidget->isVisible()) {
        anchorRect = anchorWidget->frameGeometry();
    } else if (anchorWidget != nullptr) {
        anchorRect = QRect(anchorWidget->mapToGlobal(QPoint(0, 0)), anchorWidget->size());
    }
    if (!anchorRect.isValid()) {
        if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
            anchorRect = screen->availableGeometry();
        }
    }
    if (anchorRect.isValid()) {
        QPoint targetTopLeft(
            anchorRect.center().x() - dialog->width() / 2,
            anchorRect.center().y() - dialog->height() / 2
        );
        QScreen* targetScreen = QGuiApplication::screenAt(anchorRect.center());
        if (targetScreen == nullptr && anchorWidget != nullptr && anchorWidget->windowHandle() != nullptr) {
            targetScreen = anchorWidget->windowHandle()->screen();
        }
        if (targetScreen != nullptr) {
            const QRect avail = targetScreen->availableGeometry();
            targetTopLeft.setX(qBound(avail.left(), targetTopLeft.x(), avail.right() - dialog->width() + 1));
            targetTopLeft.setY(qBound(avail.top(), targetTopLeft.y(), avail.bottom() - dialog->height() + 1));
        }
        dialog->move(targetTopLeft);
    }
}

QMessageBox::StandardButton showCenteredLocalizedMessageBox(
    QMessageBox::Icon icon,
    QWidget* parent,
    const QString& title,
    const QString& message,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton
)
{
    QMessageBox dialog(icon, title, message, QMessageBox::NoButton, parent);
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dialog.setStandardButtons(buttons);
    if (defaultButton != QMessageBox::NoButton) {
        dialog.setDefaultButton(defaultButton);
    }
    UiDialogs::localizeMessageBox(&dialog);
    centerDialogOnAnchor(&dialog, parent);

    dialog.exec();
    return dialog.standardButton(dialog.clickedButton());
}

QString formatExportRemainingDuration(qint64 seconds)
{
    seconds = qMax<qint64>(0, seconds);
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;
    if (UiText::isChineseUi()) {
        if (hours > 0) {
            return QStringLiteral("%1小时%2分").arg(hours).arg(minutes);
        }
        if (minutes > 0) {
            return QStringLiteral("%1分%2秒").arg(minutes).arg(secs);
        }
        return QStringLiteral("%1秒").arg(secs);
    }
    if (hours > 0) {
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    }
    if (minutes > 0) {
        return QStringLiteral("%1m %2s").arg(minutes).arg(secs);
    }
    return QStringLiteral("%1s").arg(secs);
}

QString localizeExportWorkerMessageForSystemLanguage(const QString& rawMessage)
{
    const QString trimmed = rawMessage.trimmed();
    if (trimmed.isEmpty()) {
        return trimmed;
    }

    static const QRegularExpression renderProgressPattern(
        QStringLiteral("^Rendering frames and encoding\\.\\.\\.\\s+(\\d+)/(\\d+)$")
    );
    const QRegularExpressionMatch renderMatch = renderProgressPattern.match(trimmed);
    if (renderMatch.hasMatch()) {
        return systemL10n(
            QStringLiteral("Rendering frames... %1/%2").arg(renderMatch.captured(1), renderMatch.captured(2)),
            QStringLiteral("正在渲染帧... %1/%2").arg(renderMatch.captured(1), renderMatch.captured(2))
        );
    }

    if (trimmed == QLatin1String("Preparing SFX track...")) {
        return systemL10n(QStringLiteral("Preparing audio..."), QStringLiteral("正在准备音频..."));
    }
    if (trimmed == QLatin1String("Starting ffmpeg...")) {
        return systemL10n(QStringLiteral("Starting ffmpeg..."), QStringLiteral("正在启动 ffmpeg..."));
    }
    if (trimmed == QLatin1String("Rendering frames and encoding...")) {
        return systemL10n(QStringLiteral("Rendering frames..."), QStringLiteral("正在渲染帧..."));
    }
    if (trimmed == QLatin1String("Repacking MP4 for fast start...")) {
        return systemL10n(QStringLiteral("Finalizing video..."), QStringLiteral("正在整理视频..."));
    }
    if (trimmed == QLatin1String("Collecting export summary...")) {
        return systemL10n(QStringLiteral("Finishing up..."), QStringLiteral("正在收尾..."));
    }
    if (trimmed == QLatin1String("Export completed.")) {
        return systemL10n(QStringLiteral("Done."), QStringLiteral("导出完成。"));
    }
    return systemL10n(rawMessage, rawMessage);
}

bool exportWorkerProgressUsesBusyIndicator(const QString& rawMessage)
{
    const QString trimmed = rawMessage.trimmed();
    return trimmed == QLatin1String("Finalizing encoded video stream...")
        || trimmed == QLatin1String("Repacking MP4 for fast start...")
        || trimmed == QLatin1String("Collecting export summary...")
        || trimmed == QLatin1String("Export completed.");
}

QString localizeExportWorkerMessageForUiLanguage(const QString& rawMessage)
{
    const QString trimmed = rawMessage.trimmed();
    if (trimmed.isEmpty()) {
        return trimmed;
    }

    static const QRegularExpression renderProgressPattern(
        QStringLiteral("^Rendering frames and encoding\\.\\.\\.\\s+(\\d+)/(\\d+)$")
    );
    const QRegularExpressionMatch renderMatch = renderProgressPattern.match(trimmed);
    if (renderMatch.hasMatch()) {
        return uiText("dialog.video_export.progress.rendering_count", "Rendering frames... %1/%2")
            .arg(renderMatch.captured(1), renderMatch.captured(2));
    }

    if (trimmed == QLatin1String("Preparing SFX track...")) {
        return uiText("dialog.video_export.progress.preparing_audio", "Preparing audio...");
    }
    if (trimmed == QLatin1String("Starting ffmpeg...")) {
        return uiText("dialog.video_export.progress.starting_ffmpeg", "Starting ffmpeg...");
    }
    if (trimmed == QLatin1String("Rendering frames and encoding...")) {
        return uiText("dialog.video_export.progress.rendering", "Rendering frames...");
    }
    if (trimmed == QLatin1String("Finalizing encoded video stream...")) {
        return uiText("dialog.video_export.progress.finalizing_encode", "Finalizing video...");
    }
    if (trimmed == QLatin1String("Repacking MP4 for fast start...")) {
        return uiText("dialog.video_export.progress.repacking", "Finalizing video...");
    }
    if (trimmed == QLatin1String("Collecting export summary...")) {
        return uiText("dialog.video_export.progress.finishing", "Finishing up...");
    }
    if (trimmed == QLatin1String("Export completed.")) {
        return uiText("dialog.video_export.progress.done", "Done.");
    }
    return rawMessage;
}

QString buildExportProgressLabelText(
    const QString& rawMessage,
    int percent,
    const QElapsedTimer& elapsed,
    qint64* smoothedEtaSeconds
)
{
    QString text = localizeExportWorkerMessageForUiLanguage(rawMessage.trimmed());
    if (text.isEmpty()) {
        text = systemL10n(QStringLiteral("Exporting..."), QStringLiteral("正在导出..."));
    }
    if (!elapsed.isValid() || percent < 5) {
        return text;
    }

    const qint64 elapsedMs = elapsed.elapsed();
    if (elapsedMs < 1500) {
        return text;
    }

    const double totalMs = (static_cast<double>(elapsedMs) * 100.0) / static_cast<double>(percent);
    const qint64 estimatedRemainingSeconds = qRound64(qMax(0.0, totalMs - static_cast<double>(elapsedMs)) / 1000.0);
    if (estimatedRemainingSeconds <= 0) {
        return text;
    }

    qint64 displayEtaSeconds = estimatedRemainingSeconds;
    if (smoothedEtaSeconds != nullptr) {
        if (*smoothedEtaSeconds >= 0) {
            displayEtaSeconds = qRound64((static_cast<double>(*smoothedEtaSeconds) * 2.0 + estimatedRemainingSeconds) / 3.0);
        }
        *smoothedEtaSeconds = displayEtaSeconds;
    }

    const QString etaLine = systemL10n(
        QStringLiteral("About %1 remaining").arg(formatExportRemainingDuration(displayEtaSeconds)),
        QStringLiteral("预计剩余 %1").arg(formatExportRemainingDuration(displayEtaSeconds))
    );
    return QStringLiteral("%1\n%2").arg(text, etaLine);
}

QString buildExportProgressLabelTextForUiLanguage(
    const QString& rawMessage,
    int percent,
    const QElapsedTimer& elapsed,
    qint64* smoothedEtaSeconds
)
{
    QString text = localizeExportWorkerMessageForUiLanguage(rawMessage.trimmed());
    if (text.isEmpty()) {
        text = uiText("dialog.video_export.progress.generic", "Exporting...");
    }
    if (exportWorkerProgressUsesBusyIndicator(rawMessage)) {
        if (smoothedEtaSeconds != nullptr) {
            *smoothedEtaSeconds = -1;
        }
        return text;
    }
    if (!elapsed.isValid() || percent < 5) {
        return text;
    }

    const qint64 elapsedMs = elapsed.elapsed();
    if (elapsedMs < 1500) {
        return text;
    }

    const double totalMs = (static_cast<double>(elapsedMs) * 100.0) / static_cast<double>(percent);
    const qint64 estimatedRemainingSeconds =
        qRound64(qMax(0.0, totalMs - static_cast<double>(elapsedMs)) / 1000.0);
    if (estimatedRemainingSeconds <= 0) {
        return text;
    }

    qint64 displayEtaSeconds = estimatedRemainingSeconds;
    if (smoothedEtaSeconds != nullptr) {
        if (*smoothedEtaSeconds >= 0) {
            displayEtaSeconds = qRound64(
                (static_cast<double>(*smoothedEtaSeconds) * 2.0 + estimatedRemainingSeconds) / 3.0
            );
        }
        *smoothedEtaSeconds = displayEtaSeconds;
    }

    const QString etaLine = uiText("dialog.video_export.progress.remaining", "About %1 remaining")
        .arg(formatExportRemainingDuration(displayEtaSeconds));
    return QStringLiteral("%1\n%2").arg(text, etaLine);
}

QFont editorFont(int pointSize = -1)
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(font).fixedPitch()) {
        for (const QString& family : QStringList{"SF Mono", "Menlo", "Monaco", "Noto Sans Mono", "JetBrains Mono"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0 && QFontInfo(font).fixedPitch()) {
                break;
            }
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(pointSize > 0 ? pointSize : 13);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    static const QString embeddedConsolasFamily = []() -> QString {
        const int fontId = QFontDatabase::addApplicationFont(":/fonts/consola.ttf");
        if (fontId < 0) {
            return QString();
        }
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        if (families.isEmpty()) {
            return QString();
        }
        return families.first();
    }();

    QFont font;
    if (!embeddedConsolasFamily.isEmpty()) {
        font.setFamily(embeddedConsolasFamily);
    } else {
        font.setFamily("Consolas");
        if (!QFontInfo(font).exactMatch()) {
            font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        }
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPointSize(pointSize > 0 ? pointSize : 11);
    return font;
#endif
}

int blockSpacingPixelsForPointSize(int pointSize, double spacingFactor)
{
    const int baseSpacing = qBound(1, qRound(static_cast<double>(pointSize) * 0.18), 6);
    return qMax(0, qRound(static_cast<double>(baseSpacing) * qMax(0.0, spacingFactor)));
}

void applyBlockSpacingToTextEdit(QTextEdit* editor, int blockSpacingPixels)
{
    if (editor == nullptr || editor->document() == nullptr) {
        return;
    }
    QSignalBlocker blocker(editor);
    QTextCursor cursor(editor->document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    QTextBlockFormat fmt;
    fmt.setBottomMargin(static_cast<qreal>(qMax(0, blockSpacingPixels)));
    cursor.mergeBlockFormat(fmt);
    cursor.endEditBlock();
}

QFont uiOutputFont()
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(12);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (UiText::isChineseUi()) {
        for (const QString& family : QStringList{"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
    }
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setPointSize(11);
    return font;
#endif
}

QFont uiAccentFont(int pointSize, QFont::Weight weight = QFont::Medium)
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (UiText::isChineseUi()) {
        for (const QString& family : QStringList{"PingFang SC", "Hiragino Sans GB"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
    }
    font.setPointSize(pointSize + ((pointSize <= 11) ? 1 : 0));
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    QFont font;
    QStringList familyCandidates;
    if (UiText::isChineseUi()) {
        familyCandidates << "Microsoft YaHei UI" << "Microsoft YaHei" << "PingFang SC" << "Noto Sans CJK SC";
    }
    familyCandidates << "Segoe UI Variable Text" << "Segoe UI";
    for (const QString& family : familyCandidates) {
        font.setFamily(family);
        if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    }
    font.setPointSize(pointSize);
    font.setWeight(weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#endif
}

QFont uiMonoFont(int pointSize, QFont::Weight weight = QFont::Medium)
{
#ifdef Q_OS_MACOS
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (!QFontInfo(font).fixedPitch()) {
        for (const QString& family : QStringList{"SF Mono", "Menlo", "Monaco", "Noto Sans Mono", "JetBrains Mono"}) {
            font.setFamily(family);
            if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0 && QFontInfo(font).fixedPitch()) {
                break;
            }
        }
    }
    font.setPointSize(pointSize + 1);
    font.setWeight(weight);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
#else
    QFont font;
    for (const QString& family : QStringList{"Cascadia Mono", "JetBrains Mono", "Cascadia Code", "Consolas"}) {
        font.setFamily(family);
        if (QFontInfo(font).family().compare(family, Qt::CaseInsensitive) == 0) {
            break;
        }
    }
    if (font.family().isEmpty()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setPointSize(pointSize);
    font.setWeight(weight);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
#endif
}

QByteArray noteMarkerSignature(const QVector<TimelineNoteMarker>& notes)
{
    QByteArray signature;
    signature.reserve(notes.size() * 128);
    for (const TimelineNoteMarker& marker : notes) {
        signature.append(QByteArray::number(marker.sourceLine));
        signature.append('|');
        signature.append(QByteArray::number(marker.sourceCol));
        signature.append('|');
        signature.append(QByteArray::number(marker.lane));
        signature.append('|');
        signature.append(QByteArray::number(marker.endLane));
        signature.append('|');
        signature.append(QByteArray::number(marker.second, 'f', 6));
        signature.append('|');
        signature.append(QByteArray::number(marker.endSecond, 'f', 6));
        signature.append('|');
        signature.append(QByteArray::number(marker.slideTraceSecond, 'f', 6));
        signature.append('|');
        signature.append(marker.type.toUtf8());
        signature.append('|');
        signature.append(marker.slideTrackKey.toUtf8());
        signature.append('|');
        signature.append(QByteArray::number(marker.slideSegmentKeys.size()));
        signature.append('|');
        for (int index = 0; index < marker.slideSegmentKeys.size(); ++index) {
            signature.append(marker.slideSegmentKeys.at(index).toUtf8());
            signature.append(',');
        }
        signature.append('|');
        signature.append(QByteArray::number(marker.slideSegmentShootSeconds.size()));
        signature.append('|');
        for (double second : marker.slideSegmentShootSeconds) {
            signature.append(QByteArray::number(second, 'f', 6));
            signature.append(',');
        }
        signature.append('|');
        signature.append(QByteArray::number(marker.slideSegmentDurations.size()));
        signature.append('|');
        for (double duration : marker.slideSegmentDurations) {
            signature.append(QByteArray::number(duration, 'f', 6));
            signature.append(',');
        }
        signature.append('|');
        signature.append(marker.tapUsesStarMaterial ? '1' : '0');
        signature.append('|');
        signature.append(marker.tapStarDouble ? '1' : '0');
        signature.append('|');
        signature.append(marker.isEach ? '1' : '0');
        signature.append('|');
        signature.append(marker.isBreak ? '1' : '0');
        signature.append('|');
        signature.append(marker.isEx ? '1' : '0');
        signature.append('|');
        signature.append(marker.isFirework ? '1' : '0');
        signature.append('|');
        signature.append(marker.headEach ? '1' : '0');
        signature.append('|');
        signature.append(marker.headBreak ? '1' : '0');
        signature.append('|');
        signature.append(marker.headEx ? '1' : '0');
        signature.append('|');
        signature.append(marker.slideHeadUsesTapMaterial ? '1' : '0');
        signature.append('|');
        signature.append(marker.trackBreak ? '1' : '0');
        signature.append('|');
        signature.append(marker.hasHeadStar ? '1' : '0');
        signature.append('|');
        signature.append(marker.headlessImmediate ? '1' : '0');
        signature.append(';');
    }
    return signature;
}

QString renderModeToken(RenderMode mode)
{
    return mode == RenderMode::MaimuriDxStyle
        ? QStringLiteral("maimuri_dx_style")
        : QStringLiteral("native");
}

RenderMode renderModeFromToken(const QString& token)
{
    return token.trimmed().compare(QStringLiteral("maimuri_dx_style"), Qt::CaseInsensitive) == 0
        ? RenderMode::MaimuriDxStyle
        : RenderMode::Native;
}

int difficultyIdFromCliToken(const QString& rawToken)
{
    const QString token = rawToken.trimmed().toUpper();
    if (token.isEmpty()) {
        return 0;
    }
    bool numericOk = false;
    const int numericId = token.toInt(&numericOk);
    if (numericOk && SimaiDocument::isDifficultyId(numericId)) {
        return numericId;
    }
    for (int id = 1; id <= 7; ++id) {
        if (SimaiDocument::difficultyShortName(id).compare(token, Qt::CaseInsensitive) == 0) {
            return id;
        }
        if (SimaiDocument::difficultyName(id).compare(token, Qt::CaseInsensitive) == 0) {
            return id;
        }
    }
    return 0;
}

QString resolveChartPathFromCliInput(const QString& inputPath)
{
    const QString cleaned = QDir::cleanPath(inputPath.trimmed());
    if (cleaned.isEmpty()) {
        return QString();
    }

    const QFileInfo info(cleaned);
    if (info.isFile()) {
        return info.absoluteFilePath();
    }
    if (!info.isDir()) {
        return QString();
    }

    const QDir dir(info.absoluteFilePath());
    const QStringList preferredNames{
        QStringLiteral("majdata.txt"),
        QStringLiteral("maidata.txt"),
        QStringLiteral("majdata.simai"),
        QStringLiteral("maidata.simai"),
        QStringLiteral("chart.txt"),
        QStringLiteral("chart.simai"),
    };
    for (const QString& name : preferredNames) {
        const QString candidate = dir.filePath(name);
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    QStringList filters;
    filters << QStringLiteral("*.simai") << QStringLiteral("*.txt");
    const QStringList files = dir.entryList(filters, QDir::Files | QDir::Readable, QDir::Name);
    if (!files.isEmpty()) {
        return QDir::cleanPath(dir.filePath(files.constFirst()));
    }
    return QString();
}

QString readTextFileWithFallbackEncoding(const QString& path, bool* usedSystemEncoding)
{
    if (usedSystemEncoding != nullptr) {
        *usedSystemEncoding = false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    const QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(bytes.mid(3));
    }

    QStringDecoder utf8Decoder(QStringConverter::Utf8);
    const QString utf8Text = utf8Decoder.decode(bytes);
    if (!utf8Decoder.hasError()) {
        return utf8Text;
    }

    if (usedSystemEncoding != nullptr) {
        *usedSystemEncoding = true;
    }
    QStringDecoder systemDecoder(QStringConverter::System);
    return systemDecoder.decode(bytes);
}

struct PreparedDocumentOpenPayload {
    bool success = false;
    bool usedSystemEncoding = false;
    QString normalizedPath;
    SimaiDocument document;
    QString resolvedTrackPath;
    double trackDurationSeconds = 0.0;
    bool hasTrackDuration = false;
    qint64 readElapsedMs = 0;
    qint64 decodeElapsedMs = 0;
    qint64 parseElapsedMs = 0;
    qint64 trackProbeElapsedMs = 0;
    qint64 totalElapsedMs = 0;
};

qint64 fileLastModifiedMs(const QFileInfo& fileInfo)
{
    return fileInfo.exists() ? fileInfo.lastModified().toMSecsSinceEpoch() : -1;
}

double probeAudioDurationSeconds(const QString& trackPath)
{
    if (trackPath.isEmpty() || !QFileInfo::exists(trackPath)) {
        return 0.0;
    }

    const QByteArray pathBytes = QFile::encodeName(trackPath);
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoder decoder;
    if (ma_decoder_init_file(pathBytes.constData(), &config, &decoder) != MA_SUCCESS) {
        return 0.0;
    }

    ma_uint64 totalFrames = 0;
    const bool ok = ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS
        && totalFrames > 0;
    ma_decoder_uninit(&decoder);
    if (!ok) {
        return 0.0;
    }

    return static_cast<double>(totalFrames) / 48000.0;
}

PreparedDocumentOpenPayload prepareDocumentOpenPayload(const QString& path, bool probeTrackDuration)
{
    PreparedDocumentOpenPayload payload;
    payload.normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (payload.normalizedPath.isEmpty()) {
        return payload;
    }

    QFile file(payload.normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return payload;
    }

    QElapsedTimer totalTimer;
    totalTimer.start();
    QElapsedTimer phaseTimer;
    phaseTimer.start();
    const QByteArray bytes = file.readAll();
    payload.readElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    QString text;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(bytes.mid(3));
    } else {
        QStringDecoder utf8Decoder(QStringConverter::Utf8);
        text = utf8Decoder.decode(bytes);
        if (utf8Decoder.hasError()) {
            QStringDecoder systemDecoder(QStringConverter::System);
            text = systemDecoder.decode(bytes);
            payload.usedSystemEncoding = true;
        }
    }
    payload.decodeElapsedMs = phaseTimer.elapsed();

    phaseTimer.restart();
    payload.document = SimaiDocument::fromText(text);
    payload.parseElapsedMs = phaseTimer.elapsed();

    if (probeTrackDuration) {
        payload.resolvedTrackPath = miacode::chart_assets::resolveTrackPath(payload.normalizedPath);
        phaseTimer.restart();
        if (!payload.resolvedTrackPath.isEmpty()) {
            payload.trackDurationSeconds = probeAudioDurationSeconds(payload.resolvedTrackPath);
            payload.hasTrackDuration = payload.trackDurationSeconds > 0.0;
        }
        payload.trackProbeElapsedMs = phaseTimer.elapsed();
    }

    payload.totalElapsedMs = totalTimer.elapsed();
    payload.success = true;
    return payload;
}

QVector<float> buildWaveformPeaks(const QString& trackPath, double* durationSeconds, int peakCount = 1024)
{
    QVector<float> peaks;
    if (durationSeconds != nullptr) {
        *durationSeconds = 0.0;
    }
    if (trackPath.isEmpty() || !QFileInfo::exists(trackPath) || peakCount <= 0) {
        return peaks;
    }

    const QByteArray pathBytes = QFile::encodeName(trackPath);
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoder decoder;
    if (ma_decoder_init_file(pathBytes.constData(), &config, &decoder) != MA_SUCCESS) {
        return peaks;
    }

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) != MA_SUCCESS || totalFrames == 0) {
        ma_decoder_uninit(&decoder);
        return peaks;
    }

    const double sampleRate = 48000.0;
    if (durationSeconds != nullptr) {
        *durationSeconds = static_cast<double>(totalFrames) / sampleRate;
    }
    peaks.fill(0.0f, peakCount);
    constexpr ma_uint64 kChunkFrames = 4096;
    QVector<float> buffer(static_cast<int>(kChunkFrames), 0.0f);
    ma_uint64 frameCursor = 0;

    while (frameCursor < totalFrames) {
        ma_uint64 framesRead = 0;
        if (ma_decoder_read_pcm_frames(&decoder, buffer.data(), kChunkFrames, &framesRead) != MA_SUCCESS || framesRead == 0) {
            break;
        }
        for (ma_uint64 i = 0; i < framesRead; ++i) {
            const ma_uint64 absoluteFrame = frameCursor + i;
            const int binIndex = qBound(
                0,
                static_cast<int>((absoluteFrame * peakCount) / qMax<ma_uint64>(1, totalFrames)),
                peakCount - 1
            );
            peaks[binIndex] = qMax(peaks[binIndex], qAbs(buffer[static_cast<int>(i)]));
        }
        frameCursor += framesRead;
    }

    ma_decoder_uninit(&decoder);
    return peaks;
}

QIcon makePreviewPlayIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(QPolygonF{
        QPointF(5.0, 3.0),
        QPointF(15.5, 10.0),
        QPointF(5.0, 17.0),
    });
    return QIcon(pixmap);
}

QIcon makePreviewStopIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(4.5, 4.5, 11.0, 11.0), 1.8, 1.8);
    return QIcon(pixmap);
}

QColor difficultyColor(int difficultyId)
{
    switch (difficultyId) {
    case 1:
        return QColor("#69A6FF");
    case 2:
        return QColor("#78C85A");
    case 3:
        return QColor("#DCC548");
    case 4:
        return QColor("#E35C50");
    case 5:
        return QColor("#7A4FD1");
    case 6:
        return QColor("#D548B6");
    case 7:
        return QColor("#E29A46");
    default:
        return QColor("#8A8F98");
    }
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

QIcon makeOutlineCloseIcon(const QColor& color)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(QPointF(3.0, 3.0), QPointF(9.0, 9.0));
    painter.drawLine(QPointF(9.0, 3.0), QPointF(3.0, 9.0));
    return QIcon(pixmap);
}

QIcon makeMenuSelectionCheckIcon(const QColor& color, bool visible)
{
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    if (visible) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(color, 1.8);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.drawLine(QPointF(5.0, 7.4), QPointF(7.7, 10.1));
        painter.drawLine(QPointF(7.7, 10.1), QPointF(12.4, 4.4));
    }
    return QIcon(pixmap);
}

QPixmap makeDifficultyBadgePixmap(int difficultyId)
{
    const QColor fill = difficultyColor(difficultyId);
    const qreal dpr = qApp != nullptr ? qApp->devicePixelRatio() : 1.0;
    QImage image(static_cast<int>(14 * dpr), static_cast<int>(14 * dpr), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawRoundedRect(QRectF(2.0 * dpr, 2.0 * dpr, 10.0 * dpr, 10.0 * dpr), 3.0 * dpr, 3.0 * dpr);
    painter.end();
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QIcon makeDifficultyBadgeIcon(int difficultyId)
{
    const QPixmap pixmap = makeDifficultyBadgePixmap(difficultyId);
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Selected, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Selected, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::On);
    return icon;
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

QIcon makePreviewPauseIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(5.0, 3.0, 3.5, 14.0), 1.2, 1.2);
    painter.drawRoundedRect(QRectF(11.5, 3.0, 3.5, 14.0), 1.2, 1.2);
    return QIcon(pixmap);
}

QIcon makePreviewResumeIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(3.0, 4.0, 2.6, 12.0), 1.0, 1.0);
    painter.drawPolygon(QPolygonF{
        QPointF(7.5, 3.0),
        QPointF(16.5, 10.0),
        QPointF(7.5, 17.0),
    });
    return QIcon(pixmap);
}

QIcon makePreviewEnterFullscreenIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(7.8, 4.8), QPointF(4.8, 4.8));
    painter.drawLine(QPointF(4.8, 4.8), QPointF(4.8, 7.8));
    painter.drawLine(QPointF(12.2, 4.8), QPointF(15.2, 4.8));
    painter.drawLine(QPointF(15.2, 4.8), QPointF(15.2, 7.8));
    painter.drawLine(QPointF(4.8, 12.2), QPointF(4.8, 15.2));
    painter.drawLine(QPointF(4.8, 15.2), QPointF(7.8, 15.2));
    painter.drawLine(QPointF(12.2, 15.2), QPointF(15.2, 15.2));
    painter.drawLine(QPointF(15.2, 12.2), QPointF(15.2, 15.2));
    return QIcon(pixmap);
}

QIcon makePreviewExitFullscreenIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(QPointF(8.2, 4.8), QPointF(8.2, 8.2));
    painter.drawLine(QPointF(4.8, 8.2), QPointF(8.2, 8.2));
    painter.drawLine(QPointF(11.8, 4.8), QPointF(11.8, 8.2));
    painter.drawLine(QPointF(11.8, 8.2), QPointF(15.2, 8.2));
    painter.drawLine(QPointF(4.8, 11.8), QPointF(8.2, 11.8));
    painter.drawLine(QPointF(8.2, 11.8), QPointF(8.2, 15.2));
    painter.drawLine(QPointF(11.8, 11.8), QPointF(15.2, 11.8));
    painter.drawLine(QPointF(11.8, 11.8), QPointF(11.8, 15.2));
    return QIcon(pixmap);
}

QIcon makeSettingsGearIcon(const QColor& color)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(9.0, 9.0), 3.0, 3.0);
    for (int i = 0; i < 8; ++i) {
        const qreal angle = (i * 45.0) * 0.017453292519943295;
        const QPointF outer(9.0 + qCos(angle) * 7.0, 9.0 + qSin(angle) * 7.0);
        const QPointF inner(9.0 + qCos(angle) * 5.0, 9.0 + qSin(angle) * 5.0);
        painter.drawLine(inner, outer);
    }
    painter.end();
    return QIcon(pixmap);
}

QIcon makeToolboxAccessIcon(const QColor& toolboxColor, const QColor& gearColor)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPen toolboxPen(toolboxColor, 1.8);
    toolboxPen.setCapStyle(Qt::RoundCap);
    toolboxPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(toolboxPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(4.0, 9.0, 11.8, 7.8), 2.2, 2.2);
    painter.drawRoundedRect(QRectF(6.6, 6.0, 6.4, 3.5), 1.3, 1.3);
    painter.drawLine(QPointF(4.2, 10.9), QPointF(15.6, 10.9));

    const QPointF gearCenter(17.0, 16.4);
    QPen gearPen(gearColor, 1.35);
    gearPen.setCapStyle(Qt::RoundCap);
    gearPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(gearPen);
    painter.drawEllipse(gearCenter, 2.35, 2.35);
    for (int i = 0; i < 8; ++i) {
        const qreal angle = (i * 45.0) * 0.017453292519943295;
        const QPointF inner(gearCenter.x() + qCos(angle) * 3.1, gearCenter.y() + qSin(angle) * 3.1);
        const QPointF outer(gearCenter.x() + qCos(angle) * 4.25, gearCenter.y() + qSin(angle) * 4.25);
        painter.drawLine(inner, outer);
    }

    painter.end();
    return QIcon(pixmap);
}

QIcon makeTransformMirrorLeftRightIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(4.2, 10.0), QPointF(15.8, 10.0));
    p.drawLine(QPointF(5.9, 8.3), QPointF(4.2, 10.0));
    p.drawLine(QPointF(5.9, 11.7), QPointF(4.2, 10.0));
    p.drawLine(QPointF(14.1, 8.3), QPointF(15.8, 10.0));
    p.drawLine(QPointF(14.1, 11.7), QPointF(15.8, 10.0));
    p.end();
    return QIcon(pixmap);
}

QIcon makeTransformMirrorUpDownIcon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(10.0, 4.2), QPointF(10.0, 15.8));
    p.drawLine(QPointF(8.3, 5.9), QPointF(10.0, 4.2));
    p.drawLine(QPointF(11.7, 5.9), QPointF(10.0, 4.2));
    p.drawLine(QPointF(8.3, 14.1), QPointF(10.0, 15.8));
    p.drawLine(QPointF(11.7, 14.1), QPointF(10.0, 15.8));
    p.end();
    return QIcon(pixmap);
}

QIcon makeTransformRotate180Icon(const QColor& color)
{
    QPixmap pixmap(20, 20);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawArc(QRectF(3.6, 3.6, 12.8, 12.8), 35 * 16, 150 * 16);
    p.drawArc(QRectF(3.6, 3.6, 12.8, 12.8), 215 * 16, 150 * 16);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(QPolygonF{
        QPointF(15.9, 6.8),
        QPointF(17.6, 4.9),
        QPointF(14.9, 4.6),
    });
    p.drawPolygon(QPolygonF{
        QPointF(4.1, 13.2),
        QPointF(2.4, 15.1),
        QPointF(5.1, 15.4),
    });
    p.end();
    return QIcon(pixmap);
}

QPointF pointOnArcPrecise(const QRectF& rect, qreal deg)
{
    const qreal rad = qDegreesToRadians(deg);
    const qreal cx = rect.center().x();
    const qreal cy = rect.center().y();
    const qreal rx = rect.width() * 0.5;
    const qreal ry = rect.height() * 0.5;
    return QPointF(
        cx + rx * std::cos(rad),
        cy - ry * std::sin(rad)
    );
}

QPointF tangentOnArcPrecise(const QRectF& rect, qreal deg, qreal sweepDeg)
{
    const qreal rad = qDegreesToRadians(deg);
    const qreal rx = rect.width() * 0.5;
    const qreal ry = rect.height() * 0.5;
    QPointF t(
        -rx * std::sin(rad),
        -ry * std::cos(rad)
    );
    if (sweepDeg < 0.0) {
        t = -t;
    }
    const qreal len = std::hypot(t.x(), t.y());
    if (len < 1e-6) {
        return QPointF(1.0, 0.0);
    }
    return QPointF(t.x() / len, t.y() / len);
}

QPixmap makeTransformRotate45Pixmap(const QColor& color, bool clockwise)
{
    constexpr qreal logicalSize = 20.0;
    constexpr qreal dpr = 2.0;
    QPixmap pixmap(static_cast<int>(logicalSize * dpr), static_cast<int>(logicalSize * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF arcRect(3.4, 3.4, 13.2, 13.2);
    const qreal startDeg = clockwise ? 42.0 : 138.0;
    const qreal fullSweepDeg = clockwise ? -250.0 : 250.0;
    const qreal arcSweepDeg = clockwise ? -236.0 : 236.0;

    QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(arcRect, qRound(startDeg * 16.0), qRound(arcSweepDeg * 16.0));

    const qreal endDeg = startDeg + arcSweepDeg;
    const QPointF tip = pointOnArcPrecise(arcRect, endDeg);
    const QPointF dir = tangentOnArcPrecise(arcRect, endDeg, arcSweepDeg);
    const QPointF normal(-dir.y(), dir.x());
    constexpr qreal headLength = 5.2;
    constexpr qreal headWidth = 4.2;
    const QPointF baseCenter = tip;
    const QPointF headTip = baseCenter + dir * headLength;
    QPolygonF head;
    head << headTip
         << (baseCenter + normal * (headWidth * 0.5))
         << (baseCenter - normal * (headWidth * 0.5));
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPolygon(head);
    p.end();
    return pixmap;
}

QIcon makeTransformRotateCcw45Icon(const QColor& color)
{
    return QIcon(makeTransformRotate45Pixmap(color, false));
}

QIcon makeTransformRotateCw45Icon(const QColor& color)
{
    return QIcon(makeTransformRotate45Pixmap(color, true));
}

QString modernScrollBarStyle()
{
    return UiTheme::scrollBarStyleSheet();
}

void styleRoundedMenu(QMenu& menu)
{
    UiTheme::styleRoundedMenu(menu);
}

#ifdef Q_OS_WIN
constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaBorderColor = 34;
constexpr DWORD kDwmwaCaptionColor = 35;
constexpr DWORD kDwmwaTextColor = 36;
constexpr DWORD kDwmwaSystemBackdropType = 38;
constexpr DWORD kDwmwaMicaEffect = 1029;
constexpr int kDwmsbtNone = 1;
constexpr int kDwmsbtMainWindow = 2;
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;

bool setDwmWindowAttribute(HWND hwnd, DWORD attribute, const void* value, DWORD size)
{
    if (hwnd == nullptr || value == nullptr || size == 0) {
        return false;
    }
    static HMODULE dwmapiModule = ::LoadLibraryW(L"dwmapi.dll");
    if (dwmapiModule == nullptr) {
        return false;
    }
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        ::GetProcAddress(dwmapiModule, "DwmSetWindowAttribute")
    );
    if (setWindowAttribute == nullptr) {
        return false;
    }
    return SUCCEEDED(setWindowAttribute(hwnd, attribute, value, size));
}

COLORREF colorRefForDwm(const QColor& color)
{
    return RGB(color.red(), color.green(), color.blue());
}

void applySystemBackdropToWidget(QWidget* widget, bool enabled, bool darkTheme)
{
    if (widget == nullptr) {
        return;
    }
    widget->winId();
    QWidget* topLevel = widget->window();
    const WId nativeId = topLevel != nullptr ? topLevel->winId() : widget->winId();
    const HWND hwnd = reinterpret_cast<HWND>(nativeId);
    if (hwnd == nullptr) {
        return;
    }

    const BOOL darkMode = darkTheme ? TRUE : FALSE;
    setDwmWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &darkMode, sizeof(darkMode));

    if (UiText::preferredTheme() == UiText::ThemePreference::System) {
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
    } else {
        const UiTheme::Colors& themeColors = UiTheme::colors();
        const bool active = topLevel != nullptr ? topLevel->isActiveWindow() : widget->isActiveWindow();
        const COLORREF borderColor = colorRefForDwm(active ? themeColors.borderStrong : themeColors.borderSoft);
        const COLORREF captionColor = colorRefForDwm(active ? themeColors.toolbarBg : themeColors.windowAltBg);
        const COLORREF textColor = colorRefForDwm(active ? themeColors.textPrimary : themeColors.textSecondary);
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &borderColor, sizeof(borderColor));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &captionColor, sizeof(captionColor));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &textColor, sizeof(textColor));
    }

    const int backdropType = enabled ? kDwmsbtMainWindow : kDwmsbtNone;
    if (!setDwmWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdropType, sizeof(backdropType))) {
        const BOOL micaEnabled = enabled ? TRUE : FALSE;
        setDwmWindowAttribute(hwnd, kDwmwaMicaEffect, &micaEnabled, sizeof(micaEnabled));
    }
}
#endif

}  // namespace

void MainWindow::applyUiTheme()
{
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        UiTheme::applyApplicationTheme(*app);
    }

    if (editorWidget_ != nullptr) {
        editorWidget_->setStyleSheet(UiTheme::editorTextEditStyleSheet());
        if (auto* scrollArea = qobject_cast<QAbstractScrollArea*>(editorWidget_)) {
            if (QScrollBar* vbar = scrollArea->verticalScrollBar()) {
                vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
            }
            if (QScrollBar* hbar = scrollArea->horizontalScrollBar()) {
                hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
            }
        }
    }
    if (editorFindBar_ != nullptr) {
        editorFindBar_->setStyleSheet(UiTheme::editorFindBarStyleSheet());
    }
    if (welcomePage_ != nullptr) {
        welcomePage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    }
    if (welcomeEmptyHintLabel_ != nullptr) {
        welcomeEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    }
    if (metadataPage_ != nullptr) {
        metadataPage_->setStyleSheet(UiTheme::metadataPageStyleSheet());
    }
    if (metadataEmptyHintLabel_ != nullptr) {
        metadataEmptyHintLabel_->setStyleSheet(UiTheme::metadataEmptyHintLabelStyleSheet());
    }
    if (metadataExtraEdit_ != nullptr) {
        if (QScrollBar* vbar = metadataExtraEdit_->verticalScrollBar()) {
            vbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
        if (QScrollBar* hbar = metadataExtraEdit_->horizontalScrollBar()) {
            hbar->setStyleSheet(UiTheme::scrollBarStyleSheet());
        }
    }
    if (outlineList_ != nullptr) {
        outlineList_->setStyleSheet(UiTheme::outlineListStyleSheet());
    }
    if (deleteDifficultyButton_ != nullptr) {
        deleteDifficultyButton_->setStyleSheet(UiTheme::deleteDifficultyButtonStyleSheet());
        deleteDifficultyButton_->setIcon(makeOutlineCloseIcon(UiTheme::colors().iconSecondary));
    }
    if (timelineView_ != nullptr) {
        timelineView_->refreshTheme();
    }
    updateBottomTabsDeviceHeight();
    if (chartBracketHighlighter_ != nullptr) {
        chartBracketHighlighter_->rehighlight();
    }
    if (metadataBracketHighlighter_ != nullptr) {
        metadataBracketHighlighter_->rehighlight();
    }
    if (QWidget* editorShell = findChild<QWidget*>(QStringLiteral("EditorShell")); editorShell != nullptr) {
        editorShell->setStyleSheet(UiTheme::editorShellStyleSheet());
    }
    const QList<QMenu*> menus = findChildren<QMenu*>();
    for (QMenu* menu : menus) {
        if (menu != nullptr) {
            UiTheme::styleRoundedMenu(*menu);
        }
    }

    const QColor iconColor = UiTheme::colors().iconPrimary;
    const QColor previewControlIconColor =
        previewFullscreenActive_ ? previewFullscreenOverlayIconColor() : iconColor;
    const QColor secondaryIconColor = UiTheme::colors().iconSecondary;
    if (stopPreviewAction_ != nullptr) {
        stopPreviewAction_->setIcon(makePreviewStopIcon(previewControlIconColor));
    }
    if (settingsPlaceholderAction_ != nullptr) {
        settingsPlaceholderAction_->setIcon(makeSettingsGearIcon(secondaryIconColor));
    }
    if (previewAudioSettingsButton_ != nullptr) {
        previewAudioSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (previewVideoSettingsButton_ != nullptr) {
        previewVideoSettingsButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    applyWorkspacePanelArrangement();
    applySystemWindowBackdrop();
    if (syntaxCheckButton_ != nullptr) {
        syntaxCheckButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (exportVideoButton_ != nullptr) {
        exportVideoButton_->setStyleSheet(UiTheme::compactToolbarButtonStyleSheet());
    }
    if (outlineCollapseButton_ != nullptr) {
        outlineCollapseButton_->setStyleSheet(outlineCollapseButtonStyleSheet());
        updateOutlineDockCollapseButton();
    }
    if (previewFullscreenHintLabel_ != nullptr) {
        previewFullscreenHintLabel_->setStyleSheet(previewFullscreenHintStyleSheet());
    }
    if (previewFullscreenActive_
        && previewControlCard_ != nullptr
        && previewControlCard_->parentWidget() == previewFullscreenControlsWindow_) {
        previewControlCard_->setStyleSheet(previewFullscreenControlCardStyleSheet());
    }
    updateEditorValidationSummary();
    updatePauseButtonAppearance();
    updatePreviewFullscreenButtonAppearance();
    update();
}

void MainWindow::updateOutlineDockCollapseButton()
{
    if (outlineCollapseButton_ == nullptr) {
        return;
    }
    outlineCollapseButton_->setText(outlineDockCollapsed_ ? QStringLiteral("▶") : QStringLiteral("◀"));
    outlineCollapseButton_->setToolTip(
        outlineDockCollapsed_
            ? (UiText::isChineseUi() ? QStringLiteral("展开左侧字段栏") : QStringLiteral("Expand left sidebar"))
            : (UiText::isChineseUi() ? QStringLiteral("折叠左侧字段栏") : QStringLiteral("Collapse left sidebar"))
    );
}

void MainWindow::setOutlineDockCollapsed(bool collapsed)
{
    if (outlineDock_ == nullptr || outlineList_ == nullptr) {
        return;
    }

    constexpr int kCollapsedWidth = miacode::window_parity::kOutlineCollapsedWidth;
    constexpr int kExpandedMinWidth = miacode::window_parity::kOutlineExpandedMinWidth;
    if (collapsed) {
        const int currentWidth = outlineDock_->width();
        if (currentWidth > kCollapsedWidth) {
            outlineDockExpandedWidth_ = currentWidth;
        }
    }

    outlineDockCollapsed_ = collapsed;
    outlineList_->setVisible(!collapsed);
    if (collapsed) {
        updateDifficultyDeleteButton(false);
    }

    const int targetWidth = collapsed ? kCollapsedWidth : qMax(kExpandedMinWidth, outlineDockExpandedWidth_);
    outlineDock_->setMinimumWidth(collapsed ? kCollapsedWidth : kExpandedMinWidth);
    outlineDock_->setMaximumWidth(collapsed ? kCollapsedWidth : QWIDGETSIZE_MAX);
    outlineDock_->resize(targetWidth, outlineDock_->height());
    if (QWidget* widget = outlineDock_->widget(); widget != nullptr) {
        widget->updateGeometry();
    }
    outlineDock_->updateGeometry();
    updateOutlineDockCollapseButton();
}

void MainWindow::applySystemWindowBackdrop(QWidget* target) const
{
#ifdef Q_OS_WIN
    if (target != nullptr) {
        applySystemBackdropToWidget(target, true, UiTheme::isDarkTheme());
        return;
    }
    applySystemBackdropToWidget(const_cast<MainWindow*>(this), true, UiTheme::isDarkTheme());
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget* topLevel : topLevels) {
        if (topLevel == nullptr
            || topLevel == this
            || topLevel->parentWidget() != this
            || !topLevel->isVisible()
            || topLevel->windowState().testFlag(Qt::WindowMinimized)) {
            continue;
        }
        applySystemBackdropToWidget(topLevel, true, UiTheme::isDarkTheme());
    }
#else
    Q_UNUSED(target);
#endif
}

int MainWindow::computeBottomTabsDeviceHeight() const
{
    if (bottomTabs_ == nullptr || timelineView_ == nullptr) {
        return 0;
    }

    bottomTabs_->ensurePolished();
    timelineView_->ensurePolished();
    QTabBar* tabBar = bottomTabs_->tabBar();
    if (tabBar != nullptr) {
        tabBar->ensurePolished();
    }

    const int timelineHeight = qMax(timelineView_->minimumHeight(), timelineView_->minimumSizeHint().height());
    const int tabBarHeight = tabBar != nullptr
        ? qMax(tabBar->minimumSizeHint().height(), tabBar->sizeHint().height())
        : 0;
    const int frameWidth = qMax(0, bottomTabs_->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, bottomTabs_));
    return miacode::window_parity::computeBottomTabsDeviceHeight(timelineHeight, tabBarHeight, frameWidth);
}

void MainWindow::updateBottomTabsDeviceHeight()
{
    if (bottomTabs_ == nullptr) {
        return;
    }

    const int targetHeight = computeBottomTabsDeviceHeight();
    if (targetHeight <= 0) {
        return;
    }
    if (bottomTabs_->minimumHeight() == targetHeight && bottomTabs_->maximumHeight() == targetHeight) {
        return;
    }

    bottomTabs_->setMinimumHeight(targetHeight);
    bottomTabs_->setMaximumHeight(targetHeight);
    bottomTabs_->updateGeometry();
    if (previewLeftColumn_ != nullptr) {
        previewLeftColumn_->updateGeometry();
    }
}

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

void appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs)
{
    miacode::debug_log::appendStartupTimingStage(stage, elapsedMs, deltaMs);
}

}  // namespace

const MuriAnalysisReport& MainWindow::alignedMuriAnalysisReportForPreview() const
{
    static const MuriAnalysisReport kEmptyReport;
    if (latestTimelineNoteMarkerSignature_ != muriAnalysisReportNoteMarkerSignature_) {
        return kEmptyReport;
    }
    return muriAnalysisReport_;
}

void MainWindow::applyAlignedMuriAnalysisReportToViews()
{
    const MuriAnalysisReport& alignedReport = alignedMuriAnalysisReportForPreview();
    if (timelineView_ != nullptr) {
        timelineView_->setMuriAnalysisReport(alignedReport);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setMuriAnalysisReport(alignedReport);
    }
}

MainWindow::~MainWindow()
{
    shutdownPreviewStageMediaHost();
    shutdownPreviewMediaController();
    delete quickShellTopChromeSurfaceWidget_;
    quickShellTopChromeSurfaceWidget_ = nullptr;
    delete quickShellWorkspaceSurfaceWidget_;
    quickShellWorkspaceSurfaceWidget_ = nullptr;
    delete quickShellPreviewControlsSurfaceWidget_;
    quickShellPreviewControlsSurfaceWidget_ = nullptr;
    delete quickShellStatusSurfaceWidget_;
    quickShellStatusSurfaceWidget_ = nullptr;
    delete quickShellChartSurfaceWidget_;
    quickShellChartSurfaceWidget_ = nullptr;
    delete quickShellTimelineSurfaceWidget_;
    quickShellTimelineSurfaceWidget_ = nullptr;
}

void MainWindow::configureRuntimeDebugOutput()
{
    runtimeDebugOutputEnabled_ = miacode::debug_options::runtimeDebugOutputEnabled();
}

void MainWindow::dispatchPreviewMediaControllerCall(
    std::function<void(PreviewMediaController*)> call,
    Qt::ConnectionType connectionType) const
{
    if (previewMediaController_ == nullptr || !call) {
        return;
    }
    PreviewMediaController* controller = previewMediaController_;
    if (connectionType == Qt::BlockingQueuedConnection && controller->thread() == QThread::currentThread()) {
        call(controller);
        return;
    }
    QMetaObject::invokeMethod(
        controller,
        [controller, call = std::move(call)]() mutable {
            call(controller);
        },
        connectionType
    );
}

bool MainWindow::queryPreviewMediaControllerHasVideoMedia() const
{
    bool hasVideoMedia = false;
    dispatchPreviewMediaControllerCall(
        [&hasVideoMedia](PreviewMediaController* controller) {
            hasVideoMedia = controller->hasVideoMedia();
        },
        Qt::BlockingQueuedConnection
    );
    return hasVideoMedia;
}

double MainWindow::queryPreviewMediaControllerCurrentPlaybackSecond() const
{
    double second = 0.0;
    dispatchPreviewMediaControllerCall(
        [&second](PreviewMediaController* controller) {
            second = controller->currentPlaybackSecond();
        },
        Qt::BlockingQueuedConnection
    );
    return second;
}

void MainWindow::ensurePreviewMediaControllerInitialized()
{
    if (previewMediaController_ != nullptr) {
        return;
    }

    if (previewMediaControllerThread_ == nullptr) {
        previewMediaControllerThread_ = new QThread(this);
        previewMediaControllerThread_->setObjectName(QStringLiteral("PreviewMediaControllerThread"));
        previewMediaControllerThread_->start();
    }

#ifdef HAVE_QT_MULTIMEDIA
    qRegisterMetaType<QVideoFrame>("QVideoFrame");
#endif

    previewMediaController_ = new PreviewMediaController();
    previewMediaController_->moveToThread(previewMediaControllerThread_);

    connect(previewMediaController_, &PreviewMediaController::mediaStateChanged, this, [this](bool hasResolvedMedia, bool) {
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setStageMediaAvailable(hasResolvedMedia);
        }
    });
    connect(previewMediaController_, &PreviewMediaController::frameChanged, this, [this](const QImage& frame) {
        if (previewCanvas_ == nullptr) {
            return;
        }
        previewCanvas_->setMediaFrame(frame);
        previewCanvas_->update();
    });
#ifdef HAVE_QT_MULTIMEDIA
    connect(
        previewMediaController_,
        &PreviewMediaController::resolvedStageVideoFrameChanged,
        this,
        [this](const QVideoFrame& frame, const QImage& resolvedImage, bool cacheable, quint64 serial, double toImageMs) {
            if (previewCanvas_ == nullptr) {
                return;
            }
            previewCanvas_->setResolvedStageVideoFrame(frame, resolvedImage, cacheable, serial, toImageMs);
            if (!qtPreviewPlaying_) {
                previewCanvas_->update();
            }
        }
    );
    connect(previewMediaController_, &PreviewMediaController::videoFallbackFrameChanged, this, [this](const QImage& frame) {
        if (previewCanvas_ == nullptr) {
            return;
        }
        previewCanvas_->setRetainedVideoFallbackFrame(frame);
    });
#endif
    connect(
        previewMediaController_,
        &PreviewMediaController::backgroundBrightnessChanged,
        previewCanvas_,
        &PreviewRuntime::setBackgroundBrightnessOuter
    );
    connect(previewMediaController_, &PreviewMediaController::playbackPositionChanged, this, [this](double second) {
        if (qtPreviewPlaying_) {
            return;
        }
        if (pausedPreviewMediaSeekPending_) {
            if (qAbs(second - pausedPreviewMediaSeekSecond_) > 0.05) {
                return;
            }
            second = pausedPreviewMediaSeekSecond_;
            pausedPreviewMediaSeekPending_ = false;
        }
        qtPreviewStartSecond_ = second;
        qtPreviewElapsed_.restart();
        applyQtPreviewPosition(second, true);
    });
    connect(previewMediaController_, &PreviewMediaController::playbackFinished, this, [this]() {
        finishQtPreviewPlaybackAndReturnToEntry("Qt preview reached the end of current media.");
    });

    if (previewCanvas_ != nullptr) {
        previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
        previewCanvas_->setNoteFlowSpeed(previewNoteFlowSpeed_);
    }

    dispatchPreviewMediaControllerCall([this](PreviewMediaController* controller) {
        controller->initializeBackendObjects();
        controller->setWarmupResolvedMediaPath(previewMediaWarmupChartPath_, previewMediaWarmupResolvedPath_);
        controller->setBackgroundTrackVolume(previewAudioSettings_.bgmVolume);
        controller->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
        controller->setBackgroundTrackPath(lastTrackPath_);
        controller->setBackgroundBrightness(previewBackgroundBrightnessOuter_);
        controller->setTimelineOffsetSeconds(0.0);
        controller->setChartPath(currentFilePath_);
        controller->setPlayheadSeconds(qtPreviewPauseSecond_);
    });
}

void MainWindow::shutdownPreviewMediaController()
{
    if (previewMediaController_ != nullptr) {
        PreviewMediaController* controller = previewMediaController_;
        previewMediaController_ = nullptr;
        if (previewMediaControllerThread_ != nullptr && previewMediaControllerThread_->isRunning()) {
            QMetaObject::invokeMethod(
                controller,
                [controller]() {
                    delete controller;
                },
                Qt::BlockingQueuedConnection
            );
        } else {
            delete controller;
        }
    }
    if (previewMediaControllerThread_ != nullptr) {
        previewMediaControllerThread_->quit();
        previewMediaControllerThread_->wait();
        delete previewMediaControllerThread_;
        previewMediaControllerThread_ = nullptr;
    }
}

void MainWindow::ensurePreviewSfxRuntimePrepared()
{
    if (previewSfxRuntime_ == nullptr || previewSfxRuntimePrepared_) {
        return;
    }
    QElapsedTimer initTimer;
    initTimer.start();
    previewSfxRuntime_->setWarmupResolvedPaths(
        previewSfxWarmupChartPath_,
        previewSfxWarmupTrackPath_,
        previewSfxWarmupSfxDir_
    );
    previewSfxRuntime_->reloadAssets(previewAudioSettings_);
    previewSfxRuntime_->setChartPath(currentFilePath_);
    previewSfxRuntime_->setBackgroundTrackPlaybackRate(previewPlaybackRate_);
    previewSfxRuntimePrepared_ = true;
    const qint64 elapsedMs = initTimer.elapsed();
    appendStartupTimingStage("mainwindow/preview_sfx_runtime_prepare_on_demand", elapsedMs, elapsedMs);
}

void MainWindow::schedulePreviewSubsystemWarmup()
{
    if (previewWarmupPool_ == nullptr) {
        return;
    }
    const quint64 generation = ++previewWarmupGeneration_;
    PreviewAudioSettings audioSettingsSnapshot = previewAudioSettings_;
    audioSettingsSnapshot.normalize();
    const QString chartPathSnapshot = currentFilePath_;
    const QString trackPathSnapshot = lastTrackPath_;
    const double playbackRateSnapshot = previewPlaybackRate_;
    schedulePreviewMediaWarmup(generation, chartPathSnapshot, trackPathSnapshot);
    schedulePreviewSfxWarmup(generation, chartPathSnapshot, trackPathSnapshot, audioSettingsSnapshot, playbackRateSnapshot);
}

void MainWindow::schedulePreviewMediaWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot)
{
    if (previewWarmupPool_ == nullptr) {
        return;
    }
    QPointer<MainWindow> guard(this);
    previewWarmupPool_->start([guard, generation, chartPathSnapshot, trackPathSnapshot]() {
        QElapsedTimer timer;
        timer.start();
        PreviewMediaWarmupResult result;
        result.generation = generation;
        result.chartPath = chartPathSnapshot;
        result.trackPath = trackPathSnapshot;
#ifdef HAVE_QT_MULTIMEDIA
        result.resolvedMediaPath = miacode::chart_assets::resolveBackgroundMediaPath(chartPathSnapshot, true);
#else
        result.resolvedMediaPath = miacode::chart_assets::resolveBackgroundMediaPath(chartPathSnapshot, false);
#endif
        if (!result.resolvedMediaPath.isEmpty()) {
            const QString suffix = QFileInfo(result.resolvedMediaPath).suffix().toLower();
            if (suffix == QStringLiteral("mp4")) {
                warmupFileIntoOsCache(result.resolvedMediaPath, 256 * 1024);
            } else {
                warmupFileIntoOsCache(result.resolvedMediaPath);
            }
        }
        result.workerElapsedMs = timer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                if (guard.isNull()) {
                    return;
                }
                guard->applyPreviewMediaWarmupResult(
                    result.generation,
                    result.chartPath,
                    result.resolvedMediaPath,
                    result.trackPath,
                    result.workerElapsedMs
                );
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::schedulePreviewSfxWarmup(
    quint64 generation,
    const QString& chartPathSnapshot,
    const QString& trackPathSnapshot,
    const PreviewAudioSettings& audioSettingsSnapshot,
    double playbackRateSnapshot)
{
    Q_UNUSED(audioSettingsSnapshot);
    Q_UNUSED(playbackRateSnapshot);
    if (previewWarmupPool_ == nullptr) {
        return;
    }
    QPointer<MainWindow> guard(this);
    previewWarmupPool_->start([guard, generation, chartPathSnapshot, trackPathSnapshot]() {
        QElapsedTimer timer;
        timer.start();
        PreviewSfxWarmupResult result;
        result.generation = generation;
        result.chartPath = chartPathSnapshot;
        result.trackPath = trackPathSnapshot;
        result.sfxDir = miacode::preview_sfx::resolveSfxDirectory();
        for (const QString& kind : previewSfxWarmupKinds()) {
            const QString path = miacode::preview_sfx::assetFilePathForKind(result.sfxDir, kind);
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                warmupFileIntoOsCache(path);
            }
        }
        if (!result.trackPath.isEmpty()) {
            warmupFileIntoOsCache(result.trackPath, 256 * 1024);
        }
        result.workerElapsedMs = timer.elapsed();
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)]() mutable {
                if (guard.isNull()) {
                    return;
                }
                guard->applyPreviewSfxWarmupResult(
                    result.generation,
                    result.chartPath,
                    result.trackPath,
                    result.sfxDir,
                    result.workerElapsedMs
                );
            },
            Qt::QueuedConnection
        );
    });
}

void MainWindow::applyPreviewMediaWarmupResult(
    quint64 generation,
    const QString& chartPath,
    const QString& resolvedMediaPath,
    const QString& trackPath,
    qint64 workerElapsedMs)
{
    if (generation != previewWarmupGeneration_) {
        return;
    }
    previewMediaWarmupAppliedGeneration_ = generation;
    previewMediaWarmupChartPath_ = chartPath;
    previewMediaWarmupResolvedPath_ = resolvedMediaPath;
    previewMediaWarmupTrackPath_ = trackPath;
    appendStartupTimingStage("mainwindow/preview_media_data_warmup", workerElapsedMs, workerElapsedMs);
    applyPreviewMediaWarmupToStageMediaRoute(chartPath, resolvedMediaPath, trackPath);
}

void MainWindow::applyPreviewSfxWarmupResult(
    quint64 generation,
    const QString& chartPath,
    const QString& trackPath,
    const QString& sfxDir,
    qint64 workerElapsedMs)
{
    if (generation != previewWarmupGeneration_) {
        return;
    }
    previewSfxWarmupAppliedGeneration_ = generation;
    previewSfxWarmupChartPath_ = chartPath;
    previewSfxWarmupTrackPath_ = trackPath;
    previewSfxWarmupSfxDir_ = sfxDir;
    appendStartupTimingStage("mainwindow/preview_sfx_data_warmup", workerElapsedMs, workerElapsedMs);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->setWarmupResolvedPaths(chartPath, trackPath, sfxDir);
    }
}

void MainWindow::setupInitialWindowGeometry()
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
        resize(initialSize);
        move(workArea.center() - QPoint(width() / 2, height() / 2));
        return;
    }
    resize(initialSize);
}

QString MainWindow::formatWindowStateFlags(Qt::WindowStates states) const
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

void MainWindow::logWindowGeometryDebug(const QString& tag, const QString& detail)
{
    if (!runtimeDebugOutputEnabled_) {
        return;
    }

    const QRect clientRect = geometry();
    const QRect frameRect = frameGeometry();
    const Qt::WindowStates states = windowState();

    QString payload = QString(
        "seq=%1 tag=%2 geom=[%3,%4 %5x%6] frame=[%7,%8 %9x%10] state=%11 "
        "active=%12 visible=%13 minimized=%14 maximized=%15 fullscreen=%16 "
        "suspend_depth=%17 arrange_gen=%18 arrange_retry=%19"
    )
        .arg(++windowEventDebugSequence_)
        .arg(tag)
        .arg(clientRect.left())
        .arg(clientRect.top())
        .arg(clientRect.width())
        .arg(clientRect.height())
        .arg(frameRect.left())
        .arg(frameRect.top())
        .arg(frameRect.width())
        .arg(frameRect.height())
        .arg(formatWindowStateFlags(states))
        .arg(isActiveWindow() ? 1 : 0)
        .arg(isVisible() ? 1 : 0)
        .arg(isMinimized() ? 1 : 0)
        .arg(isMaximized() ? 1 : 0)
        .arg(isFullScreen() ? 1 : 0)
        .arg(0)
        .arg(0)
        .arg(0);

    if (!detail.isEmpty()) {
        payload += " detail=" + detail;
    }

#ifdef Q_OS_WIN
    const HWND selfHwnd = reinterpret_cast<HWND>(winId());
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

    appendOutput("window/event", payload);
}

void MainWindow::logTopLevelWindowSnapshot(const QString& tag)
{
    if (!runtimeDebugOutputEnabled_) {
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
                .arg(formatWindowStateFlags(window->windowState()))
                .arg(geom.left())
                .arg(geom.top())
                .arg(geom.width())
                .arg(geom.height())
                .arg(nativeDetail)
        );
    }
    appendOutput("window/top_levels", lines.join('\n'));
}

void MainWindow::logNativeWindowDebug(const QString& tag, WId dialogWId)
{
    if (!runtimeDebugOutputEnabled_) {
        return;
    }
#ifdef Q_OS_WIN
    const HWND selfHwnd = reinterpret_cast<HWND>(winId());
    const HWND foregroundHwnd = GetForegroundWindow();
    const HWND activeHwnd = GetActiveWindow();
    const HWND focusHwnd = GetFocus();

    QString payload = QString("tag=%1 self={%2} fg={%3} active={%4} focus={%5}")
                          .arg(tag)
                          .arg(describeNativeWindowHandle(selfHwnd))
                          .arg(describeNativeWindowHandle(foregroundHwnd))
                          .arg(describeNativeWindowHandle(activeHwnd))
                          .arg(describeNativeWindowHandle(focusHwnd));

    if (dialogWId != 0) {
        const HWND dialogHwnd = reinterpret_cast<HWND>(dialogWId);
        payload += QString(" dialog={%1}").arg(describeNativeWindowHandle(dialogHwnd));
    }

    GUITHREADINFO guiInfo{};
    guiInfo.cbSize = sizeof(GUITHREADINFO);
    if (GetGUIThreadInfo(0, &guiInfo)) {
        payload += QString(
                       " gui_active=0x%1 gui_focus=0x%2 gui_capture=0x%3 "
                       "gui_menu_owner=0x%4 gui_move_size=0x%5 gui_caret=0x%6 gui_flags=0x%7"
                   )
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndActive), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndFocus), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndCapture), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndMenuOwner), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndMoveSize), 0, 16)
                       .arg(reinterpret_cast<quintptr>(guiInfo.hwndCaret), 0, 16)
                       .arg(static_cast<qulonglong>(guiInfo.flags), 0, 16);
    } else {
        payload += QString(" gui_info_err=%1").arg(GetLastError());
    }

    appendOutput("window/native", payload);
#else
    Q_UNUSED(tag);
    Q_UNUSED(dialogWId);
#endif
}

void MainWindow::logOwnedNativeWindowSnapshot(const QString& tag, int maxWindows)
{
    if (!runtimeDebugOutputEnabled_) {
        return;
    }
#ifdef Q_OS_WIN
    const HWND ownerHwnd = reinterpret_cast<HWND>(winId());
    if (ownerHwnd == nullptr) {
        appendOutput("window/native_related", QString("tag=%1 owner=0x0 count=0").arg(tag));
        return;
    }

    RelatedNativeWindowEnumContext context;
    context.ownerHwnd = ownerHwnd;
    context.foregroundHwnd = GetForegroundWindow();
    context.activeHwnd = GetActiveWindow();
    context.focusHwnd = GetFocus();
    context.foregroundRootOwner =
        context.foregroundHwnd != nullptr ? GetAncestor(context.foregroundHwnd, GA_ROOTOWNER) : nullptr;
    GetWindowThreadProcessId(ownerHwnd, &context.ownerPid);
    EnumWindows(&collectRelatedNativeWindowProc, reinterpret_cast<LPARAM>(&context));

    QStringList lines;
    lines.reserve(qMin(maxWindows, context.matches.size()) + 2);
    lines.append(
        QString("tag=%1 owner=0x%2 owner_pid=%3 count=%4 fg=0x%5 active=0x%6 focus=0x%7 fg_root_owner=0x%8")
            .arg(tag)
            .arg(reinterpret_cast<quintptr>(ownerHwnd), 0, 16)
            .arg(context.ownerPid)
            .arg(context.matches.size())
            .arg(reinterpret_cast<quintptr>(context.foregroundHwnd), 0, 16)
            .arg(reinterpret_cast<quintptr>(context.activeHwnd), 0, 16)
            .arg(reinterpret_cast<quintptr>(context.focusHwnd), 0, 16)
            .arg(reinterpret_cast<quintptr>(context.foregroundRootOwner), 0, 16)
    );
    const int limit = maxWindows <= 0 ? context.matches.size() : qMin(maxWindows, context.matches.size());
    for (int index = 0; index < limit; ++index) {
        lines.append(QString("[%1] %2").arg(index).arg(describeNativeWindowHandle(context.matches[index])));
    }
    if (limit < context.matches.size()) {
        lines.append(QString("... truncated=%1").arg(context.matches.size() - limit));
    }
    appendOutput("window/native_related", lines.join('\n'));
#else
    Q_UNUSED(tag);
    Q_UNUSED(maxWindows);
#endif
}

#include "sections/frame/MainWindow.BootstrapAndMenus.cpp"
void MainWindow::closeEvent(QCloseEvent* event)
{
    logWindowGeometryDebug("close_event_enter");
    if (maybeSaveBeforeContinue()) {
        savePortableState();
        clearVideoExportWorkerState();
        event->accept();
        logWindowGeometryDebug("close_event_accept");
    } else {
        event->ignore();
        logWindowGeometryDebug("close_event_ignore");
    }
}

void MainWindow::appendOutput(const QString& title, const QString& payload)
{
    if (!runtimeDebugOutputEnabled_) {
        return;
    }
    miacode::debug_log::appendText(
        miacode::debug_log::Channel::Runtime,
        timestampLine(title) + QStringLiteral("\n") + payload + QStringLiteral("\n")
    );
    if (outputView_ == nullptr) {
        return;
    }
    outputView_->appendPlainText(timestampLine(title));
    outputView_->appendPlainText(payload);
    outputView_->appendPlainText(QString());
}

bool MainWindow::preparePreviewStartState()
{
    const bool chartFieldVisible = editorStack_ != nullptr && editorStack_->currentWidget() == chartPage_;
    if (currentFieldDirty_ && !chartFieldVisible && !applyCurrentFieldToDocument()) {
        return false;
    }

    if (!hasActiveDifficulty()) {
        return false;
    }

    if (latestTimelinePreviewSnapshotReady_ && latestTimelinePreviewRevision_ == timelineRevision_) {
        return true;
    }

    requestTimelineSlowRefresh();
    return false;
}

bool MainWindow::isQuickShellBackendMode() const
{
    return frontendHostMode_ == FrontendHostMode::QuickShellBackend;
}

void MainWindow::initializeQuickShellNativeRegions()
{
    if (!isQuickShellBackendMode()) {
        return;
    }

    const auto ensureBridgeSurface = [this](QWidget*& bridgeRoot, const QString& objectName) {
        if (bridgeRoot != nullptr) {
            return;
        }
        bridgeRoot = new QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint);
        bridgeRoot->setObjectName(objectName);
        bridgeRoot->setAttribute(Qt::WA_NativeWindow);
        bridgeRoot->setAttribute(Qt::WA_StyledBackground, true);
        bridgeRoot->setContentsMargins(0, 0, 0, 0);
        bridgeRoot->setMinimumSize(QSize(64, 64));
        bridgeRoot->resize(960, 720);
        bridgeRoot->installEventFilter(this);
        bridgeRoot->winId();
        bridgeRoot->hide();
    };

    ensureBridgeSurface(quickShellTopChromeSurfaceWidget_, QStringLiteral("QuickShellTopChromeSurface"));
    ensureBridgeSurface(quickShellWorkspaceSurfaceWidget_, QStringLiteral("QuickShellWorkspaceSurface"));
    ensureBridgeSurface(quickShellPreviewControlsSurfaceWidget_, QStringLiteral("QuickShellPreviewControlsSurface"));
    ensureBridgeSurface(quickShellStatusSurfaceWidget_, QStringLiteral("QuickShellStatusSurface"));
    if (previewPanel_ != nullptr) {
        quickShellPreviewControlsSurfaceWidget_->setStyleSheet(previewPanel_->styleSheet());
    }

    if (quickShellTopChromeSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QVBoxLayout(quickShellTopChromeSurfaceWidget_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }
    if (quickShellWorkspaceSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QHBoxLayout(quickShellWorkspaceSurfaceWidget_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }
    if (quickShellPreviewControlsSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QVBoxLayout(quickShellPreviewControlsSurfaceWidget_);
        layout->setContentsMargins(8, 12, 8, 12);
        layout->setSpacing(10);
    }
    if (quickShellStatusSurfaceWidget_->layout() == nullptr) {
        auto* layout = new QVBoxLayout(quickShellStatusSurfaceWidget_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }

    auto* topChromeLayout = qobject_cast<QBoxLayout*>(quickShellTopChromeSurfaceWidget_->layout());
    auto* workspaceLayout = qobject_cast<QBoxLayout*>(quickShellWorkspaceSurfaceWidget_->layout());
    auto* previewControlsLayout = qobject_cast<QBoxLayout*>(quickShellPreviewControlsSurfaceWidget_->layout());
    auto* statusLayout = qobject_cast<QBoxLayout*>(quickShellStatusSurfaceWidget_->layout());
    if (topChromeLayout == nullptr || workspaceLayout == nullptr || previewControlsLayout == nullptr || statusLayout == nullptr) {
        return;
    }

    if (QMenuBar* windowMenuBar = menuBar(); windowMenuBar != nullptr) {
        windowMenuBar->setNativeMenuBar(false);
        if (windowMenuBar->parentWidget() != quickShellTopChromeSurfaceWidget_) {
            windowMenuBar->setParent(quickShellTopChromeSurfaceWidget_);
        }
        if (topChromeLayout->indexOf(windowMenuBar) < 0) {
            topChromeLayout->addWidget(windowMenuBar);
        }
        windowMenuBar->show();
    }

    if (const QList<QToolBar*> toolBars = findChildren<QToolBar*>(); !toolBars.isEmpty()) {
        if (QToolBar* toolBar = toolBars.constFirst(); toolBar != nullptr) {
            removeToolBar(toolBar);
            if (toolBar->parentWidget() != quickShellTopChromeSurfaceWidget_) {
                toolBar->setParent(quickShellTopChromeSurfaceWidget_);
            }
            if (topChromeLayout->indexOf(toolBar) < 0) {
                topChromeLayout->addWidget(toolBar);
            }
            toolBar->show();
        }
    }

    if (QStatusBar* windowStatusBar = statusBar(); windowStatusBar != nullptr) {
        if (windowStatusBar->parentWidget() != quickShellStatusSurfaceWidget_) {
            windowStatusBar->setParent(quickShellStatusSurfaceWidget_);
        }
        if (statusLayout->indexOf(windowStatusBar) < 0) {
            statusLayout->addWidget(windowStatusBar);
        }
        windowStatusBar->show();
    }

    if (outlineDock_ != nullptr) {
        removeDockWidget(outlineDock_);
        if (outlineDock_->parentWidget() != quickShellWorkspaceSurfaceWidget_) {
            outlineDock_->setParent(quickShellWorkspaceSurfaceWidget_);
        }
        if (workspaceLayout->indexOf(outlineDock_) < 0) {
            workspaceLayout->addWidget(outlineDock_);
        }
        outlineDock_->show();
    }

    if (previewLeftColumn_ != nullptr) {
        if (previewLeftColumn_->parentWidget() != quickShellWorkspaceSurfaceWidget_) {
            previewLeftColumn_->setParent(quickShellWorkspaceSurfaceWidget_);
        }
        if (workspaceLayout->indexOf(previewLeftColumn_) < 0) {
            workspaceLayout->addWidget(previewLeftColumn_, 1);
        }
        previewLeftColumn_->show();
    }

    if (previewControlCard_ != nullptr) {
        if (previewControlCard_->parentWidget() != quickShellPreviewControlsSurfaceWidget_) {
            previewControlCard_->setParent(quickShellPreviewControlsSurfaceWidget_);
        }
        if (previewControlsLayout->indexOf(previewControlCard_) < 0) {
            previewControlsLayout->addWidget(previewControlCard_);
        }
        previewControlCard_->show();
    }

    if (previewStatsCard_ != nullptr) {
        if (previewStatsCard_->parentWidget() != quickShellPreviewControlsSurfaceWidget_) {
            previewStatsCard_->setParent(quickShellPreviewControlsSurfaceWidget_);
        }
        if (previewControlsLayout->indexOf(previewStatsCard_) < 0) {
            previewControlsLayout->addWidget(previewStatsCard_);
        }
        previewStatsCard_->show();
    }

    if (previewPanel_ != nullptr) {
        previewPanel_->hide();
    }
    if (previewCanvasFrame_ != nullptr) {
        previewCanvasFrame_->hide();
    }
    if (previewCanvasContainer_ != nullptr) {
        previewCanvasContainer_->hide();
    }

    topChromeLayout->activate();
    workspaceLayout->activate();
    previewControlsLayout->activate();
    statusLayout->activate();
    quickShellTopChromeSurfaceWidget_->show();
    quickShellWorkspaceSurfaceWidget_->show();
    quickShellPreviewControlsSurfaceWidget_->show();
    quickShellStatusSurfaceWidget_->show();
}

void MainWindow::setInvalidStarPreviewEasterEggEnabled(bool enabled)
{
    if (invalidStarPreviewEasterEggEnabled_ == enabled) {
        return;
    }
    invalidStarPreviewEasterEggEnabled_ = enabled;
    SimaiNativeParser::setInvalidStarPreviewEnabled(enabled);
    refreshTimelineMetadata();
    playInvalidStarPreviewEasterEggSound(enabled);
}

void MainWindow::ensureInvalidStarPreviewEasterEggSounds()
{
    if (invalidStarPreviewEnableSound_ == nullptr) {
        invalidStarPreviewEnableSound_ = new QSoundEffect(this);
        invalidStarPreviewEnableSound_->setLoopCount(1);
        invalidStarPreviewEnableSound_->setVolume(0.45f);
    }
    if (invalidStarPreviewDisableSound_ == nullptr) {
        invalidStarPreviewDisableSound_ = new QSoundEffect(this);
        invalidStarPreviewDisableSound_->setLoopCount(1);
        invalidStarPreviewDisableSound_->setVolume(0.45f);
    }

    const QString sfxDir = miacode::preview_sfx::resolveSfxDirectory();
    const QString forwardPath = miacode::preview_sfx::assetFilePathForKind(sfxDir, QStringLiteral("answer"));
    const QString reversePath = resolveInvalidStarPreviewReverseSoundPath();

    const QUrl forwardUrl = QFileInfo::exists(forwardPath) ? QUrl::fromLocalFile(forwardPath) : QUrl();
    const QUrl reverseUrl = QFileInfo::exists(reversePath) ? QUrl::fromLocalFile(reversePath) : QUrl();

    if (invalidStarPreviewDisableSound_->source() != forwardUrl) {
        invalidStarPreviewDisableSound_->setSource(forwardUrl);
    }
    if (invalidStarPreviewEnableSound_->source() != reverseUrl) {
        invalidStarPreviewEnableSound_->setSource(reverseUrl);
    }
}

void MainWindow::playInvalidStarPreviewEasterEggSound(bool enabled)
{
    ensureInvalidStarPreviewEasterEggSounds();
    QSoundEffect* effect = enabled ? invalidStarPreviewEnableSound_ : invalidStarPreviewDisableSound_;
    if (effect == nullptr || effect->source().isEmpty()) {
        return;
    }
    effect->stop();
    effect->play();
}

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
    QObject* previewWindowObject = previewCanvas_ != nullptr ? previewCanvas_->hostWindow() : nullptr;
    if (watched == previewWindowObject) {
        if (runtimeDebugOutputEnabled_ && shouldTracePreviewHostWindowEvent(event->type())) {
            auto* previewWindow = qobject_cast<QWindow*>(previewWindowObject);
            const QString surfaceDetail = platformSurfaceDetail(event);
            appendOutput(
                "preview/host_window_event",
                QString(
                    "event=%1 watched=%2 spontaneous=%3 visible=%4 exposed=%5 active=%6 visibility=%7 "
                    "pos=(%8,%9) size=%10x%11%12"
                )
                    .arg(qEventTypeName(event->type()))
                    .arg(pointerHex(previewWindow))
                    .arg(event->spontaneous() ? 1 : 0)
                    .arg(previewWindow != nullptr && previewWindow->isVisible() ? 1 : 0)
                    .arg(previewWindow != nullptr && previewWindow->isExposed() ? 1 : 0)
                    .arg(previewWindow != nullptr && previewWindow->isActive() ? 1 : 0)
                    .arg(previewWindow != nullptr ? windowVisibilityName(previewWindow->visibility()) : QStringLiteral("null"))
                    .arg(previewWindow != nullptr ? previewWindow->x() : 0)
                    .arg(previewWindow != nullptr ? previewWindow->y() : 0)
                    .arg(previewWindow != nullptr ? previewWindow->width() : 0)
                    .arg(previewWindow != nullptr ? previewWindow->height() : 0)
                    .arg(surfaceDetail.isEmpty() ? QString() : QStringLiteral(" ") + surfaceDetail)
            );
        }
        if (event->type() == QEvent::Resize) {
            noteEmbeddedPreviewResizeActivity("preview_host_window");
        }
        if (event->type() == QEvent::Show
            || event->type() == QEvent::Expose
            || event->type() == QEvent::PlatformSurface) {
            scheduleEmbeddedPreviewSurfaceRefresh();
        }
    }
    if ((watched == previewCanvasContainer_
            || watched == previewCanvasFrame_
            || watched == previewPanel_)
        && event->type() == QEvent::Resize) {
        noteEmbeddedPreviewResizeActivity("embedded_preview_widget");
    }
    if (auto* fileDialog = qobject_cast<QFileDialog*>(watched); fileDialog != nullptr) {
        const QString debugScope = fileDialog->property("miacode_debug_scope").toString();
        if (!debugScope.isEmpty()) {
            if (runtimeDebugOutputEnabled_ && shouldTraceDebugDialogWidgetEvent(event->type())) {
                const QRect geom = fileDialog->geometry();
                appendOutput(
                    "window/dialog_event",
                    QString(
                        "scope=%1 kind=widget event=%2 watched=%3 spontaneous=%4 visible=%5 active=%6 modal=%7 "
                        "result=%8 selected_count=%9 handle=%10 geom=[%11,%12 %13x%14] title=%15"
                    )
                        .arg(debugScope)
                        .arg(qEventTypeName(event->type()))
                        .arg(pointerHex(fileDialog))
                        .arg(event->spontaneous() ? 1 : 0)
                        .arg(fileDialog->isVisible() ? 1 : 0)
                        .arg(fileDialog->isActiveWindow() ? 1 : 0)
                        .arg(fileDialog->isModal() ? 1 : 0)
                        .arg(fileDialog->result())
                        .arg(fileDialog->selectedFiles().size())
                        .arg(pointerHex(fileDialog->windowHandle()))
                        .arg(geom.left())
                        .arg(geom.top())
                        .arg(geom.width())
                        .arg(geom.height())
                        .arg(fileDialog->windowTitle().isEmpty() ? QStringLiteral("(empty)") : fileDialog->windowTitle())
                );
            }
            if (QWindow* dialogWindow = fileDialog->windowHandle();
                dialogWindow != nullptr && !dialogWindow->property("miacode_debug_filter_installed").toBool()) {
                dialogWindow->setProperty(
                    "miacode_debug_scope",
                    debugScope + QStringLiteral("/window")
                );
                dialogWindow->setProperty("miacode_debug_filter_installed", true);
                dialogWindow->installEventFilter(this);
                if (runtimeDebugOutputEnabled_) {
                    appendOutput(
                        "window/dialog_watch",
                        QString("scope=%1 attach_window_handle handle=%2 visibility=%3")
                            .arg(debugScope)
                            .arg(pointerHex(dialogWindow))
                            .arg(windowVisibilityName(dialogWindow->visibility()))
                    );
                }
            }
        }
    } else if (auto* watchedWindow = qobject_cast<QWindow*>(watched); watchedWindow != nullptr) {
        const QString debugScope = watchedWindow->property("miacode_debug_scope").toString();
        if (runtimeDebugOutputEnabled_
            && !debugScope.isEmpty()
            && shouldTraceDebugDialogWindowEvent(event->type())) {
            const QString surfaceDetail = platformSurfaceDetail(event);
            appendOutput(
                "window/dialog_event",
                QString(
                    "scope=%1 kind=window event=%2 watched=%3 spontaneous=%4 visible=%5 exposed=%6 active=%7 "
                    "visibility=%8 pos=(%9,%10) size=%11x%12%13"
                )
                    .arg(debugScope)
                    .arg(qEventTypeName(event->type()))
                    .arg(pointerHex(watchedWindow))
                    .arg(event->spontaneous() ? 1 : 0)
                    .arg(watchedWindow->isVisible() ? 1 : 0)
                    .arg(watchedWindow->isExposed() ? 1 : 0)
                    .arg(watchedWindow->isActive() ? 1 : 0)
                    .arg(windowVisibilityName(watchedWindow->visibility()))
                    .arg(watchedWindow->x())
                    .arg(watchedWindow->y())
                    .arg(watchedWindow->width())
                    .arg(watchedWindow->height())
                    .arg(surfaceDetail.isEmpty() ? QString() : QStringLiteral(" ") + surfaceDetail)
            );
        }
    }
    const bool previewKeyScope =
        watched == previewSlider_
        || watched == previewWindowObject
        || watched == previewCanvasContainer_
        || watched == previewCanvasFrame_
        || watched == previewPanel_
        || watched == previewFullscreenWindow_
        || watched == previewFullscreenHost_
        || watched == previewFullscreenControlsWindow_
        || watched == previewFullscreenButton_;
    const bool previewFullscreenOverlayScope =
        watched == previewFullscreenWindow_
        || watched == previewFullscreenHost_
        || watched == previewFullscreenControlsWindow_
        || watched == previewFullscreenHintWindow_
        || watched == previewWindowObject
        || watched == previewCanvasContainer_
        || watched == previewCanvasFrame_
        || watched == previewControlCard_
        || watched == previewSlider_
        || watched == stopPreviewButton_
        || watched == pausePreviewButton_
        || watched == previewSpeedButton_
        || watched == previewFullscreenButton_;
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
            if (handlePreviewSliderWheel(static_cast<QWheelEvent*>(event))) {
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
                stepPreviewSliderBySeconds(
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
    noteEmbeddedPreviewResizeActivity("main_window");
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
    const QEvent::Type type = event != nullptr ? event->type() : QEvent::None;
    const bool handled = QMainWindow::event(event);
    if (type == QEvent::WindowBlocked) {
        logWindowGeometryDebug(
            "window_blocked",
            QString("handled=%1 spontaneous=%2")
                .arg(handled ? 1 : 0)
                .arg(event != nullptr && event->spontaneous() ? 1 : 0)
        );
        logNativeWindowDebug(QStringLiteral("window_blocked"));
        logOwnedNativeWindowSnapshot(QStringLiteral("window_blocked"), 16);
        scheduleEmbeddedPreviewSurfaceRefresh();
    } else if (type == QEvent::WindowUnblocked) {
        logWindowGeometryDebug(
            "window_unblocked",
            QString("handled=%1 spontaneous=%2")
                .arg(handled ? 1 : 0)
                .arg(event != nullptr && event->spontaneous() ? 1 : 0)
        );
        logNativeWindowDebug(QStringLiteral("window_unblocked"));
        logOwnedNativeWindowSnapshot(QStringLiteral("window_unblocked"), 16);
        scheduleEmbeddedPreviewSurfaceRefresh();
    }
    return handled;
}

void MainWindow::refreshEmbeddedPreviewSurface(bool force)
{
    if (isQuickShellBackendMode()) {
        Q_UNUSED(force);
        return;
    }
    if (embeddedPreviewRefreshSuspended_) {
        embeddedPreviewRefreshPending_ = true;
        if (runtimeDebugOutputEnabled_) {
            appendOutput(
                "preview/embedded_refresh",
                QString("action=refresh_skipped_suspended force=%1").arg(force ? 1 : 0)
            );
        }
        return;
    }
    if (!force && embeddedPreviewResizeActive_) {
        embeddedPreviewRefreshPending_ = true;
        if (embeddedPreviewResizeSettleTimer_ != nullptr) {
            embeddedPreviewResizeSettleTimer_->start(kEmbeddedPreviewResizeSettleDelayMs);
        }
        return;
    }
    embeddedPreviewRefreshPending_ = false;
    if (runtimeDebugOutputEnabled_) {
        QWindow* hostWindow = previewCanvas_ != nullptr ? previewCanvas_->hostWindow() : nullptr;
        appendOutput(
            "preview/embedded_refresh",
            QString(
                "action=refresh panel_visible=%1 frame_visible=%2 container_visible=%3 host=%4 host_visible=%5 "
                "host_exposed=%6 host_active=%7 host_visibility=%8 container_size=%9x%10 host_size=%11x%12"
            )
                .arg(previewPanel_ != nullptr && previewPanel_->isVisible() ? 1 : 0)
                .arg(previewCanvasFrame_ != nullptr && previewCanvasFrame_->isVisible() ? 1 : 0)
                .arg(previewCanvasContainer_ != nullptr && previewCanvasContainer_->isVisible() ? 1 : 0)
                .arg(pointerHex(hostWindow))
                .arg(hostWindow != nullptr && hostWindow->isVisible() ? 1 : 0)
                .arg(hostWindow != nullptr && hostWindow->isExposed() ? 1 : 0)
                .arg(hostWindow != nullptr && hostWindow->isActive() ? 1 : 0)
                .arg(hostWindow != nullptr ? windowVisibilityName(hostWindow->visibility()) : QStringLiteral("null"))
                .arg(previewCanvasContainer_ != nullptr ? previewCanvasContainer_->width() : 0)
                .arg(previewCanvasContainer_ != nullptr ? previewCanvasContainer_->height() : 0)
                .arg(hostWindow != nullptr ? hostWindow->width() : 0)
                .arg(hostWindow != nullptr ? hostWindow->height() : 0)
        );
    }
    if (previewPanel_ != nullptr && previewPanel_->isVisible()) {
        updatePreviewWorkspaceLayout();
    }
    if (previewCanvasFrame_ != nullptr) {
        previewCanvasFrame_->update();
    }
    if (previewCanvasContainer_ != nullptr) {
        previewCanvasContainer_->show();
        previewCanvasContainer_->updateGeometry();
        previewCanvasContainer_->update();
    }
    if (previewCanvas_ != nullptr) {
        if (QWindow* hostWindow = previewCanvas_->hostWindow(); hostWindow != nullptr) {
            hostWindow->requestUpdate();
        }
        previewCanvas_->update();
    }
}

void MainWindow::suspendEmbeddedPreviewForNativeDialog(const char* source)
{
    if (isQuickShellBackendMode()) {
        Q_UNUSED(source);
        return;
    }
    if (embeddedPreviewRefreshSuspended_) {
        return;
    }

    embeddedPreviewRefreshSuspended_ = true;
    embeddedPreviewRefreshPending_ = false;
    embeddedPreviewResizeActive_ = false;
    if (embeddedPreviewRefreshTimer_ != nullptr && embeddedPreviewRefreshTimer_->isActive()) {
        embeddedPreviewRefreshTimer_->stop();
    }
    if (embeddedPreviewResizeSettleTimer_ != nullptr && embeddedPreviewResizeSettleTimer_->isActive()) {
        embeddedPreviewResizeSettleTimer_->stop();
    }
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/embedded_refresh",
            QString("action=suspend_for_native_dialog source=%1 container_visible=%2 fullscreen=%3")
                .arg(QString::fromLatin1(source != nullptr ? source : "unknown"))
                .arg(previewCanvasContainer_ != nullptr && previewCanvasContainer_->isVisible() ? 1 : 0)
                .arg(previewFullscreenActive_ ? 1 : 0)
        );
    }
    if (previewCanvasContainer_ != nullptr) {
        previewCanvasContainer_->hide();
    }
}

void MainWindow::resumeEmbeddedPreviewForNativeDialog(const char* source)
{
    if (isQuickShellBackendMode()) {
        Q_UNUSED(source);
        return;
    }
    if (!embeddedPreviewRefreshSuspended_) {
        return;
    }

    embeddedPreviewRefreshSuspended_ = false;
    if (runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/embedded_refresh",
            QString("action=resume_after_native_dialog source=%1 visible=%2 minimized=%3")
                .arg(QString::fromLatin1(source != nullptr ? source : "unknown"))
                .arg(isVisible() ? 1 : 0)
                .arg(windowState().testFlag(Qt::WindowMinimized) ? 1 : 0)
        );
    }
    if (!isVisible() || windowState().testFlag(Qt::WindowMinimized)) {
        embeddedPreviewRefreshPending_ = true;
        return;
    }
    refreshEmbeddedPreviewSurface(true);
    scheduleEmbeddedPreviewSurfaceRefresh(120);
}

void MainWindow::scheduleEmbeddedPreviewSurfaceRefresh(int delayMs)
{
    if (isQuickShellBackendMode()) {
        Q_UNUSED(delayMs);
        return;
    }
    const int effectiveDelayMs = qMax(0, delayMs);
    embeddedPreviewRefreshPending_ = true;
    if (embeddedPreviewRefreshSuspended_) {
        return;
    }
    if (embeddedPreviewResizeActive_) {
        if (embeddedPreviewResizeSettleTimer_ != nullptr) {
            embeddedPreviewResizeSettleTimer_->start(kEmbeddedPreviewResizeSettleDelayMs);
        }
        return;
    }
    if (embeddedPreviewRefreshTimer_ == nullptr) {
        refreshEmbeddedPreviewSurface();
        return;
    }
    const int remainingMs =
        embeddedPreviewRefreshTimer_->isActive() ? embeddedPreviewRefreshTimer_->remainingTime() : -1;
    if (remainingMs >= 0 && remainingMs <= effectiveDelayMs) {
        return;
    }
    embeddedPreviewRefreshTimer_->start(effectiveDelayMs);
}

void MainWindow::noteEmbeddedPreviewResizeActivity(const char* source)
{
    if (isQuickShellBackendMode()) {
        Q_UNUSED(source);
        return;
    }
    if (previewCanvas_ == nullptr || !isVisible() || windowState().testFlag(Qt::WindowMinimized)) {
        return;
    }
    if (embeddedPreviewRefreshSuspended_) {
        embeddedPreviewRefreshPending_ = true;
        return;
    }

    const bool wasActive = embeddedPreviewResizeActive_;
    embeddedPreviewResizeActive_ = true;
    embeddedPreviewRefreshPending_ = true;
    if (embeddedPreviewRefreshTimer_ != nullptr && embeddedPreviewRefreshTimer_->isActive()) {
        embeddedPreviewRefreshTimer_->stop();
    }
    if (embeddedPreviewResizeSettleTimer_ != nullptr) {
        embeddedPreviewResizeSettleTimer_->start(kEmbeddedPreviewResizeSettleDelayMs);
    }
    if (!wasActive && runtimeDebugOutputEnabled_) {
        appendOutput(
            "preview/embedded_refresh",
            QString("action=resize_degrade_begin source=%1 visible=%2 minimized=%3")
                .arg(QString::fromLatin1(source != nullptr ? source : "unknown"))
                .arg(isVisible() ? 1 : 0)
                .arg(windowState().testFlag(Qt::WindowMinimized) ? 1 : 0)
        );
    }
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
            scheduleEmbeddedPreviewSurfaceRefresh();
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
        logNativeWindowDebug(active ? QStringLiteral("activation_change_active") : QStringLiteral("activation_change_inactive"));
        logOwnedNativeWindowSnapshot(
            active ? QStringLiteral("activation_change_active") : QStringLiteral("activation_change_inactive"),
            16
        );
        if (active) {
            scheduleEmbeddedPreviewSurfaceRefresh();
        }
    } else if (type == QEvent::ZOrderChange) {
        logWindowGeometryDebug("zorder_change");
    }
}

#include "sections/document/MainWindow.DocumentFlow.cpp"
#include "sections/preview/MainWindow.PreviewStageMediaRoute.cpp"
#include "sections/timeline/MainWindow.PreviewTimelineFlow.cpp"
#include "sections/validation/MainWindow.ValidationFlow.cpp"
QString MainWindow::resolveDefaultTrackPath() const
{
    const QString envTrack = qEnvironmentVariable("MIACODE_TRACK_PATH", qEnvironmentVariable("MAIMURI_TRACK_PATH"));
    if (!envTrack.isEmpty() && QFileInfo::exists(envTrack)) {
        return envTrack;
    }
    if (!currentFilePath_.isEmpty()) {
        const QString siblingTrack = miacode::chart_assets::resolveTrackPath(currentFilePath_);
        if (!siblingTrack.isEmpty()) {
            return siblingTrack;
        }
    }
    if (!lastTrackPath_.isEmpty() && QFileInfo::exists(lastTrackPath_)) {
        return lastTrackPath_;
    }
    return QString();
}

QString MainWindow::resolveLatencyDetectorTrackPath() const
{
    if (currentFilePath_.isEmpty()) {
        return QString();
    }
    return miacode::chart_assets::resolveTrackPath(currentFilePath_);
}

void MainWindow::updateLatencyDetectorAvailability()
{
    const bool enabled = !resolveLatencyDetectorTrackPath().isEmpty();
    if (latencyDetectorAction_ != nullptr) {
        latencyDetectorAction_->setEnabled(enabled);
    }
    if (latencyDetectorButton_ != nullptr) {
        latencyDetectorButton_->setEnabled(enabled);
    }
}

MainWindow::PreviewSkinVariant MainWindow::previewSkinVariantFromStorageValue(const QString& value) const
{
    const QString normalized = value.trimmed().toLower();
    return normalized == QLatin1String("dx")
        || normalized == QLatin1String("skin_dx")
        || normalized == QLatin1String("skindx")
        ? PreviewSkinVariant::Dx
        : PreviewSkinVariant::Standard;
}

QString MainWindow::previewSkinVariantStorageValue() const
{
    return previewSkinVariant_ == PreviewSkinVariant::Dx
        ? QStringLiteral("dx")
        : QStringLiteral("standard");
}

QString MainWindow::resolvePreviewSkinDir() const
{
    const QStringList candidateDirs = previewSkinVariant_ == PreviewSkinVariant::Dx
        ? QStringList{QStringLiteral("skinDX"), QStringLiteral("skin")}
        : QStringList{QStringLiteral("skin"), QStringLiteral("skinDX")};
    for (const QString& candidateDirName : candidateDirs) {
        const QString candidateDir = miacode::assets::assetPath(candidateDirName);
        if (QFileInfo::exists(QDir(candidateDir).filePath("tap.png"))) {
            return candidateDir;
        }
    }
    return QString();
}

QString MainWindow::resolveProjectRenderStateFilePath() const
{
    if (currentFilePath_.isEmpty()) {
        return QString();
    }
    const QDir projectDir(QFileInfo(currentFilePath_).absolutePath());
    return projectDir.filePath(".miacode_render_settings.json");
}

QString MainWindow::resolveInitialOpenDirectory() const
{
    if (!lastOpenDir_.isEmpty() && QDir(lastOpenDir_).exists()) {
        return lastOpenDir_;
    }
    if (!currentFilePath_.isEmpty()) {
        const QString currentDir = QFileInfo(currentFilePath_).absolutePath();
        if (QDir(currentDir).exists()) {
            return currentDir;
        }
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QDir(appDir).exists()) {
        return appDir;
    }
    return QDir::currentPath();
}

#include "sections/editor/MainWindow.EditorDisplay.cpp"
void MainWindow::loadProjectRenderState()
{
    const double previousCanvasAspectRatio = previewCanvasAspectRatio_;
    const QJsonObject portableRoot = UiText::loadPreferencesObject();
    const QJsonObject portablePreview = portableRoot.value("app").toObject().value("preview").toObject();
    resetPortablePreviewSettingsToDefaults();
    applyPortablePreviewSettings(portablePreview);
    projectLastOpenedDifficultyId_ = 0;

    const QString path = resolveProjectRenderStateFilePath();
    if (!path.isEmpty()) {
        QFile file(path);
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject root = doc.object();
                if (root.value("audio").isObject()) {
                    previewAudioSettings_ = PreviewAudioSettings::fromJson(root.value("audio").toObject());
                } else if (root.value("preview_audio").isObject()) {
                    previewAudioSettings_ = PreviewAudioSettings::fromJson(root.value("preview_audio").toObject());
                }
                const QJsonObject render = root.value("render").toObject();
                if (!render.isEmpty()) {
                    const double legacyBrightness = qBound(
                        0.0,
                        render.value("background_brightness").toDouble(previewBackgroundBrightnessOuter_),
                        1.0
                    );
                    if (render.value("background_brightness_outer").isDouble()) {
                        previewBackgroundBrightnessOuter_ =
                            qBound(0.0, render.value("background_brightness_outer").toDouble(legacyBrightness), 1.0);
                    } else {
                        previewBackgroundBrightnessOuter_ = legacyBrightness;
                    }
                    if (render.value("background_brightness_inner").isDouble()) {
                        previewBackgroundBrightnessInner_ = qBound(
                            0.0,
                            render.value("background_brightness_inner").toDouble(previewBackgroundBrightnessOuter_),
                            1.0
                        );
                    } else {
                        previewBackgroundBrightnessInner_ = previewBackgroundBrightnessOuter_;
                    }
                    if (render.value("layout_square_scale").isDouble()) {
                        previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(
                            render.value("layout_square_scale").toDouble(previewLayoutSquareScale_)
                        );
                    }
                    if (render.value("smooth_brightness").isBool()) {
                        previewSmoothBrightness_ = render.value("smooth_brightness").toBool(previewSmoothBrightness_);
                    }
                    const QString scaleMode = render.value("background_scale_mode").toString().trimmed().toLower();
                    if (scaleMode == QLatin1String("fit") || scaleMode == QLatin1String("contain")) {
                        previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
                    } else if (!scaleMode.isEmpty()) {
                        previewBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
                    }
                    if (render.value("note_flow_speed").isDouble()) {
                        previewNoteFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(
                            render.value("note_flow_speed").toDouble(previewNoteFlowSpeed_)
                        );
                    }
                    if (render.value("skin_variant").isString()) {
                        previewSkinVariant_ = previewSkinVariantFromStorageValue(
                            render.value("skin_variant").toString()
                        );
                    }
                    if (render.value("render_mode").isString()) {
                        muriRenderOptions_.renderMode = renderModeFromToken(render.value("render_mode").toString());
                    }
                    if (render.value("show_chart_review_slide_judge_overlay").isBool()) {
                        muriRenderOptions_.showChartReviewSlideJudgeOverlay =
                            render.value("show_chart_review_slide_judge_overlay")
                                .toBool(muriRenderOptions_.showChartReviewSlideJudgeOverlay);
                    }
                    if (render.value("show_chart_review_simple_judge_overlay").isBool()) {
                        muriRenderOptions_.showChartReviewSimpleJudgeOverlay =
                            render.value("show_chart_review_simple_judge_overlay")
                                .toBool(muriRenderOptions_.showChartReviewSimpleJudgeOverlay);
                    }
                    if (render.value("wifi_need_c").isBool()) {
                        muriRenderOptions_.wifiNeedC = render.value("wifi_need_c").toBool(muriRenderOptions_.wifiNeedC);
                    }
                    showJudgeMarkers_ = false;
                    showTouchTrail_ = false;
                    if (render.value("canvas_frame_rate_mode").isString()) {
                        previewCanvasFrameRateMode_ =
                            previewCanvasFrameRateModeFromStorageValue(render.value("canvas_frame_rate_mode").toString());
                    }
                    if (render.value("show_debug_info").isBool()) {
                        previewShowDebugInfo_ = render.value("show_debug_info").toBool(previewShowDebugInfo_);
                    }
                    if (render.value("show_timestamp").isBool()) {
                        previewShowTimestamp_ = render.value("show_timestamp").toBool(previewShowTimestamp_);
                    }
                    if (render.value("show_object_stats_preview").isBool()) {
                        previewShowObjectStatsHud_ =
                            render.value("show_object_stats_preview").toBool(previewShowObjectStatsHud_);
                    }
                    if (render.value("show_object_stats_export").isBool()) {
                        exportShowObjectStatsHud_ =
                            render.value("show_object_stats_export").toBool(exportShowObjectStatsHud_);
                    }
                    if (render.value("show_validation_summary").isBool()) {
                        previewShowValidationSummary_ =
                            render.value("show_validation_summary").toBool(previewShowValidationSummary_);
                    }
                    const bool unifiedObjectStatsHud = previewShowObjectStatsHud_ || exportShowObjectStatsHud_;
                    previewShowObjectStatsHud_ = unifiedObjectStatsHud;
                    exportShowObjectStatsHud_ = unifiedObjectStatsHud;
                    previewAutoRestoreSquareAfterExport_ = false;
                    setPreviewCanvasAspectRatio(1.0, false);
                }
                const int savedDifficultyId = root.value("last_opened_difficulty").toInt(0);
                if (SimaiDocument::isDifficultyId(savedDifficultyId)) {
                    projectLastOpenedDifficultyId_ = savedDifficultyId;
                }
            }
        }
    }
    if (qAbs(previewCanvasAspectRatio_ - previousCanvasAspectRatio) > 1e-6) {
        if (previewCanvasAspectRatio_ + 1e-6 < previousCanvasAspectRatio) {
            updatePreviewWorkspaceLayout();
        } else {
            updatePreviewPanelLayout();
        }
    }
    previewAudioSettings_.normalize();
    refreshPreviewFrameRateTimers();
    applyPreviewStageMediaRouteVisualSettings();
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setSkinDirectory(resolvePreviewSkinDir());
        previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
        previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
        previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
        previewCanvas_->setNoteFlowSpeed(previewNoteFlowSpeed_);
        previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        previewCanvas_->setShowTimestamp(previewShowTimestamp_);
        previewCanvas_->setShowObjectStatsHud(previewShowObjectStatsHud_);
    }
    applyMuriRenderOptions();
}

void MainWindow::saveProjectRenderState() const
{
    const QString path = resolveProjectRenderStateFilePath();
    if (path.isEmpty()) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QJsonObject root;
    root.insert("audio", previewAudioSettings_.toJson());
    QJsonObject render;
    render.remove("show_judge_markers");
    render.remove("show_touch_trail");
    render.insert("background_brightness", previewBackgroundBrightnessOuter_);
    render.insert("background_brightness_outer", previewBackgroundBrightnessOuter_);
    render.insert("background_brightness_inner", previewBackgroundBrightnessInner_);
    render.insert("layout_square_scale", previewLayoutSquareScale_);
    render.insert("smooth_brightness", previewSmoothBrightness_);
    render.insert(
        "background_scale_mode",
        previewBackgroundScaleMode_ == PreviewBackgroundScaleMode::FitContain
            ? QStringLiteral("fit")
            : QStringLiteral("fill")
    );
    render.insert("note_flow_speed", previewNoteFlowSpeed_);
    render.insert("skin_variant", previewSkinVariantStorageValue());
    render.insert("render_mode", renderModeToken(muriRenderOptions_.renderMode));
    render.insert("show_chart_review_slide_judge_overlay", muriRenderOptions_.showChartReviewSlideJudgeOverlay);
    render.insert("show_chart_review_simple_judge_overlay", muriRenderOptions_.showChartReviewSimpleJudgeOverlay);
    render.insert("wifi_need_c", muriRenderOptions_.wifiNeedC);
    render.insert("canvas_frame_rate_mode", previewCanvasFrameRateModeStorageValue());
    render.insert("show_debug_info", previewShowDebugInfo_);
    render.insert("show_timestamp", previewShowTimestamp_);
    render.insert("show_object_stats_preview", previewShowObjectStatsHud_);
    render.insert("show_object_stats_export", exportShowObjectStatsHud_);
    render.insert("show_validation_summary", previewShowValidationSummary_);
    render.insert("canvas_aspect_ratio", 1.0);
    render.insert("auto_restore_square_after_export", false);
    root.insert("render", render);
    root.insert("last_opened_difficulty", projectLastOpenedDifficultyId_);
    root.insert("schema", "miacode_render_settings_v1");
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        return;
    }
    file.commit();
}

void MainWindow::removeProjectRenderState() const
{
    const QString path = resolveProjectRenderStateFilePath();
    if (path.isEmpty()) {
        return;
    }
    QFile::remove(path);
}

void MainWindow::applyPreviewAudioSettingsToRuntime()
{
    previewAudioSettings_.normalize();
    applyPreviewStageMediaRoutePlaybackRate(previewPlaybackRate_);
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->applyLevels(previewAudioSettings_);
    }
}

void MainWindow::setLastOpenDirectory(const QString& pathOrDir)
{
    if (pathOrDir.isEmpty()) {
        return;
    }

    QString dirCandidate;
    const QFileInfo info(pathOrDir);
    if (info.isDir()) {
        dirCandidate = info.absoluteFilePath();
    } else {
        dirCandidate = info.absolutePath();
    }
    dirCandidate = QDir::cleanPath(dirCandidate);
    if (!QDir(dirCandidate).exists()) {
        return;
    }
    if (lastOpenDir_ == dirCandidate) {
        return;
    }
    lastOpenDir_ = dirCandidate;
    savePortableState();
}

QString MainWindow::transformChartText(const QString& input, ChartTransformOp op, int* changedCount) const
{
    miacode::chart_transform::ChartTransformOp sharedOp = miacode::chart_transform::ChartTransformOp::MirrorLeftRight;
    switch (op) {
    case ChartTransformOp::MirrorLeftRight:
        sharedOp = miacode::chart_transform::ChartTransformOp::MirrorLeftRight;
        break;
    case ChartTransformOp::MirrorUpDown:
        sharedOp = miacode::chart_transform::ChartTransformOp::MirrorUpDown;
        break;
    case ChartTransformOp::Rotate180:
        sharedOp = miacode::chart_transform::ChartTransformOp::Rotate180;
        break;
    case ChartTransformOp::Rotate45CounterClockwise:
        sharedOp = miacode::chart_transform::ChartTransformOp::Rotate45CounterClockwise;
        break;
    case ChartTransformOp::Rotate45Clockwise:
        sharedOp = miacode::chart_transform::ChartTransformOp::Rotate45Clockwise;
        break;
    }
    return miacode::chart_transform::transformChartSelectionText(input, sharedOp, changedCount);
}

void MainWindow::onMirrorLeftRight()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Mirror Left/Right", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::MirrorLeftRight, changedCount);
    });
}

void MainWindow::onMirrorUpDown()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Mirror Up/Down", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::MirrorUpDown, changedCount);
    });
}

void MainWindow::onRotate180()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Rotate 180", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate180, changedCount);
    });
}

void MainWindow::onRotate45CounterClockwise()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Rotate -45", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate45CounterClockwise, changedCount);
    });
}

void MainWindow::onRotate45Clockwise()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Rotate +45", [this](const QString& text, int* changedCount) {
        return transformChartText(text, ChartTransformOp::Rotate45Clockwise, changedCount);
    });
}

void MainWindow::onNormalizeWholeChart()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage(
            uiText("status.normalize.no_difficulty", QStringLiteral("Select a difficulty field first."))
        );
        return;
    }

    if (!runValidateSimaiSilently(false)) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            uiText("dialog.normalize.title", QStringLiteral("Format Chart")),
            uiText(
                "dialog.normalize.fix_syntax_first",
                QStringLiteral("Fix syntax errors before normalizing this chart."))
        );
        return;
    }

    const miacode::chart_transform::ChartNormalizationResult normalized =
        miacode::chart_transform::normalizeChartText(activeChartText(), currentTimingMetadata());
    if (!normalized.ok) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            uiText("dialog.normalize.title", QStringLiteral("Format Chart")),
            normalized.errorMessage.isEmpty()
                ? uiText(
                    "dialog.normalize.failed",
                    QStringLiteral("Failed to normalize the current chart."))
                : normalized.errorMessage
        );
        return;
    }

    if (normalized.text == activeChartText()) {
        statusBar()->showMessage(
            uiText(
                "status.normalize.already_normalized",
                QStringLiteral("Format Chart: already normalized."))
        );
        return;
    }

    if (!applyBatchTransform(
            uiText("action.normalize_chart", QStringLiteral("Format Chart")),
            [normalized](const QString&, int* changedCount) {
                if (changedCount != nullptr) {
                    *changedCount = normalized.changedCount;
                }
                return normalized.text;
            })) {
        return;
    }

    statusBar()->showMessage(
        uiText(
            "status.normalize.applied",
            QStringLiteral("Format Chart applied: %1 measure line(s)."))
            .arg(normalized.measureLineCount)
    );
}

void MainWindow::onToggleBreakSelection()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle Break", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleBreakForSelection(text, changedCount);
    });
}

void MainWindow::onToggleExSelection()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle EX", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleExForSelection(text, changedCount);
    });
}

void MainWindow::onToggleFireworkSelection()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Toggle Firework", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::toggleFireworkForSelection(text, changedCount);
    });
}

void MainWindow::onRandomRotateSelection()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    applySelectionBatchTransform("Random Rotate", [](const QString& text, int* changedCount) {
        return miacode::chart_transform::randomRotateForSelection(text, changedCount);
    });
}

    void MainWindow::onStopPreview()
{
    const double returnSecond = qBound(0.0, qtPreviewPlaybackReturnSecond_, previewDurationSeconds());
    if (qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
    }
    pendingPreviewPlaybackStart_ = false;
    pendingPreviewPlaybackResumeFromPause_ = false;
    pendingPreviewPlaybackRevision_ = 0;
    pendingPreviewPlaybackDifficultyId_ = 0;
    pendingPreviewPlaybackSecond_ = 0.0;
    seekPreviewToSecond(returnSecond, true);
    statusBar()->showMessage("Qt preview stopped.");
}

void MainWindow::onTogglePreviewPause()
{
    if (qtPreviewPlaying_) {
        stopQtPreviewPlayback(true);
        updatePauseButtonAppearance();
        statusBar()->showMessage(QString("Qt preview paused at %1s.").arg(qtPreviewPauseSecond_, 0, 'f', 2));
        return;
    }

    if (!hasActiveDifficulty()) {
        statusBar()->showMessage("Select a difficulty field first.");
        return;
    }
    if (!startQtPreviewPlayback(qtPreviewPauseSecond_, true)) {
        return;
    }
    updatePauseButtonAppearance();
    statusBar()->showMessage(QString("Qt preview resumed at %1s.").arg(qtPreviewPauseSecond_, 0, 'f', 2));
}

void MainWindow::onToggleJudgeMarkers(bool checked)
{
    showJudgeMarkers_ = checked;
    applyMuriRenderOptions();
    savePortableState();
    saveProjectRenderState();
    statusBar()->showMessage(
        showJudgeMarkers_
            ? uiText("status.judge_marker_enabled", "Judge markers enabled.")
            : uiText("status.judge_marker_disabled", "Judge markers hidden.")
    );
}

void MainWindow::onToggleTouchTrail(bool checked)
{
    showTouchTrail_ = checked;
    applyMuriRenderOptions();
    savePortableState();
    saveProjectRenderState();
    statusBar()->showMessage(
        showTouchTrail_
            ? uiText("status.touch_trail_enabled", "Touch trail enabled.")
            : uiText("status.touch_trail_disabled", "Touch trail hidden.")
    );
}

void MainWindow::onEditStaticTapOnSlideThreshold()
{
    QDialog dialog(this);
    dialog.setWindowTitle(UiText::isChineseUi() ? QStringLiteral("撞尾阈值") : QStringLiteral("Tap-On-Slide Threshold"));
    dialog.setModal(true);
    dialog.setMinimumWidth(360);
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(10);

    auto* hintLabel = new QLabel(
        UiText::isChineseUi()
            ? QStringLiteral("调整静态“撞尾无理”参考检查阈值。")
            : QStringLiteral("Adjust the static Tap-On-Slide reference threshold."),
        &dialog);
    hintLabel->setWordWrap(true);
    rootLayout->addWidget(hintLabel);

    auto* valueRow = new QWidget(&dialog);
    auto* valueLayout = new QHBoxLayout(valueRow);
    valueLayout->setContentsMargins(0, 0, 0, 0);
    valueLayout->setSpacing(10);

    auto* slider = new QSlider(Qt::Horizontal, valueRow);
    slider->setRange(
        miacode::muri::kStaticTapOnSlideThresholdMinMs,
        miacode::muri::kStaticTapOnSlideThresholdMaxMs);
    slider->setValue(staticTapOnSlideThresholdMs_);

    auto* spinBox = new QSpinBox(valueRow);
    spinBox->setRange(
        miacode::muri::kStaticTapOnSlideThresholdMinMs,
        miacode::muri::kStaticTapOnSlideThresholdMaxMs);
    spinBox->setSuffix(QStringLiteral(" ms"));
    spinBox->setValue(staticTapOnSlideThresholdMs_);

    connect(slider, &QSlider::valueChanged, spinBox, &QSpinBox::setValue);
    connect(spinBox, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);

    valueLayout->addWidget(slider, 1);
    valueLayout->addWidget(spinBox, 0);
    rootLayout->addWidget(valueRow);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int newThresholdMs = spinBox->value();
    if (newThresholdMs == staticTapOnSlideThresholdMs_) {
        return;
    }

    staticTapOnSlideThresholdMs_ = newThresholdMs;
    savePortableState();
    if (hasActiveDifficulty()) {
        if (!scheduleTimelineAnalysisRefreshFromLatestPreviewState()) {
            refreshTimelineMetadata();
        }
    } else {
        muriStaticReferences_.clear();
        refreshMuriDiagnosticsPanel();
    }
    statusBar()->showMessage(
        UiText::isChineseUi()
            ? QStringLiteral("撞尾阈值已更新为 %1 ms。").arg(staticTapOnSlideThresholdMs_)
            : QStringLiteral("Tap-On-Slide threshold set to %1 ms.").arg(staticTapOnSlideThresholdMs_));
}

void MainWindow::applyMuriRenderOptions()
{
    showJudgeMarkers_ = false;
    showTouchTrail_ = false;
    muriRenderOptions_.showSlideTracks = showSlideTracks_;
    muriRenderOptions_.showJudgeMarkers = showJudgeMarkers_;
    muriRenderOptions_.showTouchTrail = showTouchTrail_;

    if (renderModeNativeAction_ != nullptr) {
        QSignalBlocker blocker(renderModeNativeAction_);
        renderModeNativeAction_->setChecked(muriRenderOptions_.renderMode == RenderMode::Native);
        renderModeNativeAction_->setIcon(
            makeMenuSelectionCheckIcon(UiTheme::colors().accent, renderModeNativeAction_->isChecked())
        );
    }
    if (renderModeMaimuriDxAction_ != nullptr) {
        QSignalBlocker blocker(renderModeMaimuriDxAction_);
        renderModeMaimuriDxAction_->setChecked(muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle);
        renderModeMaimuriDxAction_->setIcon(
            makeMenuSelectionCheckIcon(UiTheme::colors().accent, renderModeMaimuriDxAction_->isChecked())
        );
    }
    if (timelineView_ != nullptr) {
        timelineView_->setShowSlideTracks(muriRenderOptions_.showSlideTracks);
    }
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setMuriRenderOptions(muriRenderOptions_);
    }
    applyAlignedMuriAnalysisReportToViews();
}

void MainWindow::setMuriRenderMode(RenderMode mode, bool persistState)
{
    if (muriRenderOptions_.renderMode == mode && !persistState) {
        applyMuriRenderOptions();
        return;
    }
    muriRenderOptions_.renderMode = mode;
    applyMuriRenderOptions();
    if (persistState) {
        savePortableState();
        saveProjectRenderState();
    }
    if (hasActiveDifficulty() && !scheduleTimelineAnalysisRefreshFromLatestPreviewState()) {
        refreshTimelineMetadata();
    }
    statusBar()->showMessage(
        mode == RenderMode::MaimuriDxStyle
            ? uiText("status.muri_render_mode_dx", "Preview mode: muri check.")
            : uiText("status.muri_render_mode_native", "Preview mode: chart review.")
    );
}

void MainWindow::onPreviewAudioSettings()
{
    openPreviewSettingsDialog(
        true,
        false,
        uiText("dialog.audio_settings.title", "Audio Settings")
    );
}

void MainWindow::onPreviewVideoSettings()
{
    openPreviewSettingsDialog(
        false,
        true,
        uiText("dialog.video_settings.title", "Preview Settings")
    );
}

#include "sections/preferences/MainWindow.PreferencesDialog.cpp"
void MainWindow::onAbout()
{
    QString buildType = "Release";
#ifndef NDEBUG
    buildType = "Debug";
#endif
    const QString platform = QString("%1 / %2 / %3")
        .arg(QSysInfo::productType())
        .arg(QSysInfo::currentCpuArchitecture())
        .arg(QSysInfo::buildAbi());

    QDialog dialog(this);
    dialog.setWindowTitle(uiText("action.about", "About"));
    dialog.setModal(true);
    dialog.setMinimumWidth(500);
    dialog.setStyleSheet(UiTheme::aboutDialogStyleSheet());

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(14, 14, 14, 12);
    rootLayout->setSpacing(10);

    auto* card = new QFrame(&dialog);
    card->setObjectName("AboutCard");
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);

    auto* titleRow = new QHBoxLayout();
    titleRow->setSpacing(10);
    auto* iconLabel = new QLabel(card);
    iconLabel->setObjectName("AboutIcon");
    iconLabel->setFixedSize(64, 64);
    QPixmap appIcon = QIcon(":/icons/app.png").pixmap(48, 48);
    if (!appIcon.isNull()) {
        iconLabel->setPixmap(appIcon);
        iconLabel->setAlignment(Qt::AlignCenter);
    }
    aboutIconLabel_ = iconLabel;
    iconLabel->installEventFilter(this);
    titleRow->addWidget(iconLabel, 0, Qt::AlignVCenter);

    auto* titleTextCol = new QVBoxLayout();
    titleTextCol->setSpacing(4);
    auto* titleLabel = new QLabel("MiaCode", card);
    titleLabel->setObjectName("AboutTitle");
    QString displayVersion = QString::fromLatin1(MIACODE_DISPLAY_VERSION_STRING).trimmed();
    if (displayVersion.isEmpty()) {
        displayVersion = QCoreApplication::applicationVersion().trimmed();
    }
    if (displayVersion.isEmpty()) {
        displayVersion = QStringLiteral("0.0.0");
    }
    auto* versionLabel = new QLabel(QStringLiteral("v%1").arg(displayVersion), card);
    versionLabel->setObjectName("AboutVersion");
    titleTextCol->addWidget(titleLabel, 0, Qt::AlignLeft);
    titleTextCol->addWidget(versionLabel, 0, Qt::AlignLeft);
    titleRow->addLayout(titleTextCol, 0);
    titleRow->addStretch(1);
    cardLayout->addLayout(titleRow);

    auto* infoGrid = new QGridLayout();
    infoGrid->setHorizontalSpacing(12);
    infoGrid->setVerticalSpacing(6);
    auto addRow = [card, infoGrid](int row, const QString& key, const QString& value) {
        auto* k = new QLabel(key, card);
        k->setObjectName("AboutKey");
        auto* v = new QLabel(value, card);
        v->setObjectName("AboutValue");
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoGrid->addWidget(k, row, 0);
        infoGrid->addWidget(v, row, 1);
    };
    addRow(0, uiText("about.platform", "Release Platform"), platform);
    addRow(1, uiText("about.build_type", "Build Type"), buildType);
    cardLayout->addLayout(infoGrid);
    rootLayout->addWidget(card);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
    UiDialogs::localizeButtonBox(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox, 0, Qt::AlignRight);
    dialog.exec();
    if (aboutIconLabel_ != nullptr) {
        aboutIconLabel_->removeEventFilter(this);
    }
    aboutIconLabel_.clear();
    invalidStarPreviewAboutClickCount_ = 0;
    invalidStarPreviewAboutClickElapsed_.invalidate();
}

void MainWindow::applySharedExportTaskSettings(const VideoExportTask& task)
{
    previewShowTimestamp_ = task.showTimestamp;
    previewShowObjectStatsHud_ = task.showObjectStatsHud;
    exportShowObjectStatsHud_ = task.showObjectStatsHud;
    previewBackgroundBrightnessOuter_ = qBound(0.0, task.backgroundBrightnessOuter, 1.0);
    previewBackgroundBrightnessInner_ = qBound(0.0, task.backgroundBrightnessInner, 1.0);
    previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(task.layoutSquareScale);
    previewSmoothBrightness_ = task.smoothBrightness;
    previewBackgroundScaleMode_ = task.backgroundScaleMode;
    previewNoteFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(task.noteFlowSpeed);

    applyPreviewStageMediaRouteVisualSettings();
    if (previewCanvas_ != nullptr) {
        previewCanvas_->setShowTimestamp(previewShowTimestamp_);
        previewCanvas_->setShowObjectStatsHud(previewShowObjectStatsHud_);
        previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
        previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
        previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
        previewCanvas_->setNoteFlowSpeed(previewNoteFlowSpeed_);
    }

    saveProjectRenderState();
    savePortableState();
}

void MainWindow::onExportPreviewVideo()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage(QStringLiteral("当前未选中难度，无法导出视频。"));
        return;
    }
    if (previewCanvas_ == nullptr) {
        statusBar()->showMessage(QStringLiteral("预览画布未初始化，无法导出视频。"));
        return;
    }
    if (qtPreviewPlaying_) {
        onTogglePreviewPause();
    }

    refreshTimelineMetadata();

    const auto previewMarkerEndSecond = [](const TimelineNoteMarker& marker) {
        double markerEnd = qMax(marker.second, marker.endSecond);
        markerEnd = qMax(markerEnd, marker.slideTraceSecond);
        markerEnd = qMax(markerEnd, marker.availableSecond);
        for (double shootSecond : marker.slideSegmentShootSeconds) {
            markerEnd = qMax(markerEnd, shootSecond);
        }
        return qMax(0.0, markerEnd);
    };
    double lastMarkerEndSecond = 0.0;
    for (const TimelineNoteMarker& marker : latestTimelineNoteMarkers_) {
        lastMarkerEndSecond = qMax(lastMarkerEndSecond, previewMarkerEndSecond(marker));
    }
    const double cappedExportEndSecond = qMax(
        0.0,
        qMin(previewDurationSeconds(), lastMarkerEndSecond + 3.0)
    );

    VideoExportTask task;
    task.chartPath = currentFilePath_;
    task.trackPath = resolveDefaultTrackPath();
    task.noteMarkers = latestTimelineNoteMarkers_;
    task.muriAnalysisReport = muriAnalysisReport_;
    task.muriRenderOptions = muriRenderOptions_;
    task.staticTapOnSlideThresholdSeconds = static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0;
    task.audioSettings = previewAudioSettings_;
    task.backgroundBrightnessOuter = previewBackgroundBrightnessOuter_;
    task.backgroundBrightnessInner = previewBackgroundBrightnessInner_;
    task.layoutSquareScale = previewLayoutSquareScale_;
    task.smoothBrightness = previewSmoothBrightness_;
    task.backgroundScaleMode = previewBackgroundScaleMode_;
    task.noteFlowSpeed = previewNoteFlowSpeed_;
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = cappedExportEndSecond;
    task.fullRangeExport = true;
    task.outputWidth = 1024;
    task.outputHeight = 1024;
    task.fps = 60;
    task.showTimestamp = previewShowTimestamp_;
    task.showObjectStatsHud = exportShowObjectStatsHud_;

    const QFileInfo chartInfo(currentFilePath_);
    QString chartTitle = document_.title;
    if (editorStack_ != nullptr && editorStack_->currentWidget() == metadataPage_ && titleEdit_ != nullptr) {
        chartTitle = titleEdit_->text();
    }
    const QString exportStem = sanitizeExportFileStem(chartTitle, QStringLiteral("out"));
    const QString difficultyName = hasActiveDifficulty()
        ? SimaiDocument::difficultyShortName(activeDifficultyId_).replace(':', '_')
        : QStringLiteral("chart");
    const QString outputName = QString("%1_%2.mp4")
        .arg(exportStem)
        .arg(difficultyName);
    task.outputPath = outputName;

    const auto currentPreviewSecond = [this]() -> double {
        double second = qMax(0.0, qtPreviewPauseSecond_);
        if (qtPreviewPlaying_) {
            if (previewSfxRuntime_ != nullptr && previewSfxRuntime_->hasBackgroundTrack()) {
                second = qMax(0.0, previewSfxRuntime_->backgroundPlaybackSecond());
            } else if (previewStageMediaRouteHasVideo()) {
                second = qMax(0.0, previewStageMediaRouteCurrentPlaybackSecond());
            }
        }
        return second;
    };
    VideoExportDialog dialog(
        task,
        [this](double second) {
            seekPreviewToSecond(second, false);
        },
        [this](double second) {
            startQtPreviewPlayback(second, true);
            updatePauseButtonAppearance();
        },
        [this]() {
            if (qtPreviewPlaying_) {
                stopQtPreviewPlayback(true);
                updatePauseButtonAppearance();
            }
        },
        [this]() -> bool {
            return qtPreviewPlaying_;
        },
        currentPreviewSecond,
        [this](bool showTimestamp) {
            previewShowTimestamp_ = showTimestamp;
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setShowTimestamp(previewShowTimestamp_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        [this](double ratio) {
            setPreviewCanvasAspectRatio(ratio, false);
        },
        [this](double outer, double inner) {
            previewBackgroundBrightnessOuter_ = qBound(0.0, outer, 1.0);
            previewBackgroundBrightnessInner_ = qBound(0.0, inner, 1.0);
            applyPreviewStageMediaRouteVisualSettings();
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
                previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        [this](double scale) {
            previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(scale);
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        [this](bool smooth) {
            previewSmoothBrightness_ = smooth;
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        [this](PreviewBackgroundScaleMode mode) {
            previewBackgroundScaleMode_ = mode;
            applyPreviewStageMediaRouteVisualSettings();
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setBackgroundScaleMode(previewBackgroundScaleMode_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        [this](double flowSpeed) {
            previewNoteFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
            if (previewCanvas_ != nullptr) {
                previewCanvas_->setNoteFlowSpeed(previewNoteFlowSpeed_);
            }
            saveProjectRenderState();
            savePortableState();
        },
        this
    );

    dialog.adjustSize();
    QRect anchorRect = geometry();
    bool hasAnchor = false;
    auto mergeGlobalRect = [&anchorRect, &hasAnchor](const QWidget* widget) {
        if (widget == nullptr || !widget->isVisible()) {
            return;
        }
        const QRect local = widget->rect();
        const QRect global(widget->mapToGlobal(local.topLeft()), local.size());
        if (!hasAnchor) {
            anchorRect = global;
            hasAnchor = true;
            return;
        }
        anchorRect = anchorRect.united(global);
    };
    mergeGlobalRect(outlineList_);
    mergeGlobalRect(previewLeftColumn_);
    if (!hasAnchor && workspaceSplitter_ != nullptr && previewPanel_ != nullptr && previewPanel_->isVisible()) {
        const QRect splitterRect = workspaceSplitter_->rect();
        const QRect previewRect = previewPanel_->geometry();
        const int leftWidth = qMax(1, previewRect.left());
        const QRect localLeftArea(0, 0, leftWidth, splitterRect.height());
        anchorRect = QRect(workspaceSplitter_->mapToGlobal(localLeftArea.topLeft()), localLeftArea.size());
    }
    if (hasAnchor) {
        const int preferredWidth = qRound(anchorRect.width() * 0.5);
        dialog.resize(qMax(dialog.minimumWidth(), preferredWidth), dialog.height());
    }
    QPoint targetTopLeft(
        anchorRect.center().x() - dialog.width() / 2,
        anchorRect.center().y() - dialog.height() / 2
    );
    QScreen* targetScreen = QGuiApplication::screenAt(anchorRect.center());
    if (targetScreen == nullptr && windowHandle() != nullptr) {
        targetScreen = windowHandle()->screen();
    }
    if (targetScreen != nullptr) {
        const QRect avail = targetScreen->availableGeometry();
        targetTopLeft.setX(qBound(avail.left(), targetTopLeft.x(), avail.right() - dialog.width() + 1));
        targetTopLeft.setY(qBound(avail.top(), targetTopLeft.y(), avail.bottom() - dialog.height() + 1));
    }
    dialog.move(targetTopLeft);
    dialog.exec();
    scheduleEmbeddedPreviewSurfaceRefresh();
    setPreviewCanvasAspectRatio(1.0, false);
    restoreSquareAfterVideoExport_ = false;
    if (dialog.exportRequested()) {
        const VideoExportTask requestedTask = dialog.requestedExportTask();
        applySharedExportTaskSettings(requestedTask);
        VideoExportSnapshot snapshot;
        QString launchError;
        if (!buildVideoExportSnapshot(requestedTask, &snapshot, &launchError)
            || !launchVideoExportWorker(snapshot, &launchError)) {
            UiDialogs::showMessageBox(
                QMessageBox::Critical,
                this,
                uiText("dialog.video_export.title", "Export Video"),
                launchError.isEmpty()
                    ? uiText("dialog.video_export.error.launch_failed", "Failed to start background export.")
                    : launchError
            );
        }
    }
}

void MainWindow::onBatchExportPreviewVideo()
{
    if (!hasActiveDifficulty()) {
        statusBar()->showMessage(uiText("dialog.batch_export.error.no_difficulty", QStringLiteral("No active difficulty is selected.")));
        return;
    }
    if (previewCanvas_ == nullptr) {
        statusBar()->showMessage(uiText("dialog.batch_export.error.no_preview", QStringLiteral("Preview canvas is not initialized.")));
        return;
    }
    if (videoExportWorkerProcess_ != nullptr && videoExportWorkerProcess_->state() != QProcess::NotRunning) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.video_export.error.worker_busy", QStringLiteral("Another export is already running."))
        );
        return;
    }
    if (qtPreviewPlaying_) {
        onTogglePreviewPause();
    }

    refreshTimelineMetadata();

    VideoExportTask task;
    task.chartPath = currentFilePath_;
    task.trackPath = resolveDefaultTrackPath();
    task.noteMarkers = latestTimelineNoteMarkers_;
    task.muriAnalysisReport = muriAnalysisReport_;
    task.muriRenderOptions = muriRenderOptions_;
    task.staticTapOnSlideThresholdSeconds = static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0;
    task.audioSettings = previewAudioSettings_;
    task.backgroundBrightnessOuter = previewBackgroundBrightnessOuter_;
    task.backgroundBrightnessInner = previewBackgroundBrightnessInner_;
    task.layoutSquareScale = previewLayoutSquareScale_;
    task.smoothBrightness = previewSmoothBrightness_;
    task.backgroundScaleMode = previewBackgroundScaleMode_;
    task.noteFlowSpeed = previewNoteFlowSpeed_;
    task.exportStartSeconds = 0.0;
    task.contentDurationSeconds = 0.0;
    task.fullRangeExport = true;
    task.outputWidth = 1024;
    task.outputHeight = 1024;
    task.fps = 60;
    task.showTimestamp = previewShowTimestamp_;
    task.showObjectStatsHud = exportShowObjectStatsHud_;

    const QString difficultyToken = SimaiDocument::difficultyShortName(activeDifficultyId_);
    BatchVideoExportDialog dialog(
        task,
        difficultyToken,
        [this](const VideoExportTask& sharedTask) {
            applySharedExportTaskSettings(sharedTask);
        },
        this
    );
    dialog.adjustSize();
    dialog.exec();
    if (!dialog.exportRequested()) {
        return;
    }

    const QStringList chartDirectories = dialog.selectedChartDirectories();
    const QList<int> selectedDifficultyIds = dialog.selectedDifficultyIds();
    const QString outputDirectory = dialog.outputDirectory();
    const VideoExportTask requestedTask = dialog.requestedTaskTemplate();
    applySharedExportTaskSettings(requestedTask);
    if (chartDirectories.isEmpty()) {
        return;
    }
    if (selectedDifficultyIds.isEmpty()) {
        return;
    }
    if (outputDirectory.trimmed().isEmpty()) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.error.no_output_dir", QStringLiteral("Please choose an output folder."))
        );
        return;
    }
    if (!QDir().mkpath(outputDirectory)) {
        UiDialogs::showMessageBox(
            QMessageBox::Critical,
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.error.output_dir_create_failed", QStringLiteral("Failed to create output folder."))
                + QStringLiteral("\n") + QDir::toNativeSeparators(outputDirectory)
        );
        return;
    }

    struct BatchExportJob {
        QString chartDirectory;
        int difficultyId = 0;
        QString difficultyToken;
        QString displayName;
    };
    QStringList failedCharts;
    QVector<BatchExportJob> jobs;
    jobs.reserve(chartDirectories.size() * selectedDifficultyIds.size());
    for (const QString& chartDirectory : chartDirectories) {
        const QFileInfo directoryInfo(chartDirectory);
        const QString folderName = directoryInfo.fileName();
        const QString trackPath = miacode::chart_assets::resolveTrackPathForDirectory(directoryInfo.absoluteFilePath());
        if (trackPath.isEmpty()) {
            failedCharts.append(
                QDir::toNativeSeparators(chartDirectory)
                + QStringLiteral(" - ")
                + uiText("dialog.batch_export.error.missing_track_file", QStringLiteral("Missing track.mp3."))
            );
            continue;
        }
        const QString chartPath = resolveChartPathFromCliInput(directoryInfo.absoluteFilePath());
        if (chartPath.isEmpty()) {
            failedCharts.append(
                QDir::toNativeSeparators(chartDirectory)
                + QStringLiteral(" - ")
                + uiText("dialog.batch_export.error.missing_chart_file", QStringLiteral("Missing majdata.txt (or maidata.txt)."))
            );
            continue;
        }

        bool usedSystemEncoding = false;
        const QString chartText = readTextFileWithFallbackEncoding(chartPath, &usedSystemEncoding);
        if (chartText.isNull()) {
            failedCharts.append(
                QDir::toNativeSeparators(chartDirectory)
                + QStringLiteral(" - ")
                + uiText("dialog.batch_export.error.read_chart_failed", QStringLiteral("Failed to read %1."))
                    .arg(QFileInfo(chartPath).fileName())
            );
            continue;
        }

        const SimaiDocument document = SimaiDocument::fromText(chartText);
        int matchedDifficulties = 0;
        for (int difficultyId : selectedDifficultyIds) {
            if (document.difficulty(difficultyId) == nullptr) {
                continue;
            }
            ++matchedDifficulties;
            const QString token = SimaiDocument::difficultyShortName(difficultyId);
            BatchExportJob job;
            job.chartDirectory = chartDirectory;
            job.difficultyId = difficultyId;
            job.difficultyToken = token;
            job.displayName = QStringLiteral("%1 [%2]").arg(folderName, token);
            jobs.append(job);
        }
        if (matchedDifficulties == 0) {
            const QString requested = [&selectedDifficultyIds]() {
                QStringList names;
                for (int id : selectedDifficultyIds) {
                    names.append(SimaiDocument::difficultyShortName(id));
                }
                return names.join(QStringLiteral(", "));
            }();
            failedCharts.append(
                QDir::toNativeSeparators(chartDirectory)
                + QStringLiteral(" - ")
                + uiText(
                    "dialog.batch_export.error.no_selected_difficulties_in_folder",
                    QStringLiteral("None of the selected difficulties exist in this folder: %1")
                ).arg(requested)
            );
        }
    }

    QProgressDialog progress(
        uiText("dialog.batch_export.progress.preparing", QStringLiteral("Preparing batch export...")),
        systemL10n(QStringLiteral("Cancel"), QStringLiteral("取消")),
        0,
        100,
        this
    );
    progress.setWindowTitle(uiText("dialog.batch_export.title", QStringLiteral("Batch Export")));
    progress.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);
    progress.show();

    QStringList exportedFiles;
    int successCount = 0;
    bool canceled = false;
    const int totalJobs = qMax(1, jobs.size());
    for (int index = 0; index < jobs.size(); ++index) {
        const BatchExportJob& job = jobs.at(index);
        progress.setValue(qRound(static_cast<double>(index) * 100.0 / totalJobs));
        progress.setLabelText(
            uiText("dialog.batch_export.progress.exporting_named", QStringLiteral("Exporting %1/%2\n%3"))
                .arg(index + 1)
                .arg(jobs.size())
                .arg(job.displayName)
        );
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (progress.wasCanceled()) {
            canceled = true;
            break;
        }

        VideoExportSnapshot snapshot;
        QString validationFailure;
        if (!buildVideoExportSnapshotForChartDirectory(
                job.chartDirectory,
                job.difficultyId,
                job.difficultyToken,
                requestedTask,
                outputDirectory,
                &snapshot,
                &validationFailure)) {
            failedCharts.append(job.displayName + QStringLiteral(" - ") + validationFailure);
            continue;
        }

        QString failureText;
        bool canceledThisItem = false;
        const auto updateBatchProgress = [this, &progress, index, totalJobs, &job](int percent, const QString& rawMessage) {
            const int clampedPercent = qBound(0, percent, 100);
            const double overall = (static_cast<double>(index) + static_cast<double>(clampedPercent) / 100.0)
                / static_cast<double>(totalJobs);
            progress.setValue(qBound(0, qRound(overall * 100.0), 100));
            progress.setLabelText(
                uiText("dialog.batch_export.progress.current_item", QStringLiteral("%1\n%2"))
                    .arg(job.displayName)
                    .arg(localizeExportWorkerMessageForUiLanguage(rawMessage))
            );
        };
        if (!runVideoExportWorkerSync(snapshot, &progress, &canceledThisItem, &failureText, updateBatchProgress)) {
            if (canceledThisItem) {
                canceled = true;
                break;
            }
            failedCharts.append(job.displayName + QStringLiteral(" - ") + failureText);
            continue;
        }

        ++successCount;
        exportedFiles.append(QFileInfo(snapshot.outputPath).fileName());
    }

    progress.setValue(100);
    progress.hide();

    if (canceled) {
        UiDialogs::showMessageBox(
            QMessageBox::Information,
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.message.canceled", QStringLiteral("Batch export canceled."))
        );
        return;
    }

    if (failedCharts.isEmpty()) {
        QString details = exportedFiles.join(QLatin1Char('\n'));
        if (details.size() > 3000) {
            details = details.left(3000) + QStringLiteral("\n...");
        }
        UiDialogs::showMessageBox(
            QMessageBox::Information,
            this,
            uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
            uiText("dialog.batch_export.message.success", QStringLiteral("Batch export completed: %1 file(s)."))
                .arg(successCount)
                + (details.isEmpty() ? QString() : QStringLiteral("\n\n") + details)
        );
        return;
    }

    QString details = failedCharts.join(QLatin1Char('\n'));
    if (details.size() > 3000) {
        details = details.left(3000) + QStringLiteral("\n...");
    }
    QString successDetails = exportedFiles.join(QLatin1Char('\n'));
    if (successDetails.size() > 3000) {
        successDetails = successDetails.left(3000) + QStringLiteral("\n...");
    }
    UiDialogs::showMessageBox(
        QMessageBox::Warning,
        this,
        uiText("dialog.batch_export.title", QStringLiteral("Batch Export")),
        uiText("dialog.batch_export.message.partial_failed", QStringLiteral("Batch export finished with failures.\nSucceeded: %1\nFailed: %2"))
            .arg(successCount)
            .arg(failedCharts.size())
            + (successDetails.isEmpty() ? QString() : QStringLiteral("\n\n") + uiText("dialog.batch_export.message.output_files", QStringLiteral("Output files:")) + QStringLiteral("\n") + successDetails)
            + QStringLiteral("\n\n") + details
    );
}

bool MainWindow::buildVideoExportSnapshot(
    const VideoExportTask& requestedTask,
    VideoExportSnapshot* snapshot,
    QString* errorMessage
)
{
    if (snapshot == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export snapshot output is null");
        }
        return false;
    }
    if (!hasActiveDifficulty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.no_difficulty", "No active difficulty is selected.");
        }
        return false;
    }
    if (!applyCurrentFieldToDocument()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.sync_failed", "Failed to sync current editor state.");
        }
        return false;
    }

    refreshTimelineMetadata();
    if (latestTimelineNoteMarkers_.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.no_markers", "No parsed note markers are available for export.");
        }
        return false;
    }

    VideoExportSnapshot built;
    built.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    built.createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    built.chartTextUtf8 = document_.toText();
    built.difficultyId = activeDifficultyId_;
    built.difficultyName = SimaiDocument::difficultyShortName(activeDifficultyId_);
    built.originalChartPath = currentFilePath_;
    built.projectDir = currentFilePath_.isEmpty()
        ? QString()
        : QFileInfo(currentFilePath_).absolutePath();
    built.trackPath = resolveDefaultTrackPath();
    built.backgroundMediaPath = miacode::chart_assets::resolveBackgroundMediaPath(currentFilePath_);
    built.skinDirectory = resolvePreviewSkinDir();
    built.audioSettings = previewAudioSettings_;
    built.audioSettings.normalize();
    built.backgroundBrightnessOuter = requestedTask.backgroundBrightnessOuter;
    built.backgroundBrightnessInner = requestedTask.backgroundBrightnessInner;
    built.layoutSquareScale = requestedTask.layoutSquareScale;
    built.smoothBrightness = requestedTask.smoothBrightness;
    built.backgroundScaleMode = requestedTask.backgroundScaleMode;
    built.noteFlowSpeed = requestedTask.noteFlowSpeed;
    built.muriRenderOptions = requestedTask.muriRenderOptions;
    built.staticTapOnSlideThresholdSeconds = requestedTask.staticTapOnSlideThresholdSeconds;
    built.exportStartSeconds = requestedTask.exportStartSeconds;
    built.contentDurationSeconds = requestedTask.contentDurationSeconds;
    built.outputWidth = requestedTask.outputWidth;
    built.outputHeight = requestedTask.outputHeight;
    built.fps = requestedTask.fps;
    built.preset = requestedTask.preset;
    built.fullRangeExport = requestedTask.fullRangeExport;
    const QString exportStem = sanitizeExportFileStem(document_.title, QStringLiteral("out"));
    const QString difficultyName = SimaiDocument::difficultyShortName(activeDifficultyId_).replace(':', '_');
    const QString defaultOutputName = QStringLiteral("%1_%2.mp4").arg(exportStem, difficultyName);
    built.outputPath = resolveVideoExportOutputPath(
        requestedTask.outputPath,
        built.projectDir,
        defaultOutputName
    );
    built.showTimestamp = requestedTask.showTimestamp;
    built.showObjectStatsHud = requestedTask.showObjectStatsHud;
    built.skinLoadWaitMs = requestedTask.skinLoadWaitMs;

    if (built.skinDirectory.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.skin_missing", "Preview skin assets were not found.");
        }
        return false;
    }

    *snapshot = built;
    return true;
}

bool MainWindow::buildVideoExportSnapshotForChartDirectory(
    const QString& chartDirectory,
    int difficultyId,
    const QString& difficultyToken,
    const VideoExportTask& requestedTask,
    const QString& outputDirectory,
    VideoExportSnapshot* snapshot,
    QString* errorMessage
)
{
    if (snapshot != nullptr) {
        *snapshot = VideoExportSnapshot();
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const QFileInfo directoryInfo(chartDirectory);
    if (!directoryInfo.exists() || !directoryInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.batch_export.error.invalid_folder", "The selected path is not a valid folder.");
        }
        return false;
    }

    const QString chartPath = resolveChartPathFromCliInput(directoryInfo.absoluteFilePath());
    if (chartPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.batch_export.error.missing_chart_file", "Missing majdata.txt (or maidata.txt).");
        }
        return false;
    }

    const QString trackPath = miacode::chart_assets::resolveTrackPathForDirectory(directoryInfo.absoluteFilePath());
    if (trackPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.batch_export.error.missing_track_file", "Missing track.mp3.");
        }
        return false;
    }

    bool usedSystemEncoding = false;
    const QString chartText = readTextFileWithFallbackEncoding(chartPath, &usedSystemEncoding);
    if (chartText.isNull()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.read_chart_failed",
                "Failed to read %1."
            ).arg(QFileInfo(chartPath).fileName());
        }
        return false;
    }

    const SimaiDocument document = SimaiDocument::fromText(chartText);
    const SimaiDifficultyData* difficulty = document.difficulty(difficultyId);
    if (difficulty == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.missing_requested_difficulty",
                "Requested difficulty is missing."
            );
        }
        return false;
    }

    const auto validationLocale = UiText::isChineseUi()
        ? SimaiNativeValidationLocale::Chinese
        : SimaiNativeValidationLocale::English;
    const miacode::simai::SimaiTimingMetadata timingMetadata = miacode::simai::buildTimingMetadata(document);
    const SimaiNativeValidationReport report =
        SimaiNativeParser::buildValidationReport(difficulty->chart, validationLocale, nullptr, timingMetadata);
    if (report.errorCount > 0) {
        QString issueSummary;
        if (!report.issues.isEmpty()) {
            issueSummary = report.issues.constFirst().displayMessage.trimmed();
        }
        if (errorMessage != nullptr) {
            *errorMessage = issueSummary.isEmpty()
                ? uiText(
                      "dialog.batch_export.error.validation_failed_count",
                      "Syntax check failed with %1 error(s)."
                  ).arg(report.errorCount)
                : uiText(
                      "dialog.batch_export.error.validation_failed_detail",
                      "Syntax check failed: %1"
                  ).arg(issueSummary);
        }
        return false;
    }

    const SimaiNativeParseResult parsedTimeline = SimaiNativeParser::parseForTimeline(
        difficulty->chart,
        timingMetadata);
    if (parsedTimeline.noteMarkers.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.no_markers",
                "No parsed note markers are available for this difficulty."
            );
        }
        return false;
    }

    const auto markerEndSecond = [](const TimelineNoteMarker& marker) {
        double value = qMax(marker.second, marker.endSecond);
        value = qMax(value, marker.slideTraceSecond);
        value = qMax(value, marker.availableSecond);
        for (double shootSecond : marker.slideSegmentShootSeconds) {
            value = qMax(value, shootSecond);
        }
        return qMax(0.0, value);
    };
    double lastMarkerEndSecond = 0.0;
    for (const TimelineNoteMarker& marker : parsedTimeline.noteMarkers) {
        lastMarkerEndSecond = qMax(lastMarkerEndSecond, markerEndSecond(marker));
    }
    double trackDurationSeconds = 0.0;
    buildWaveformPeaks(trackPath, &trackDurationSeconds, 1);
    double contentDurationSeconds = qMax(0.0, lastMarkerEndSecond + 3.0);
    if (trackDurationSeconds > 0.0) {
        contentDurationSeconds = qMax(0.0, qMin(trackDurationSeconds, contentDurationSeconds));
    }
    if (contentDurationSeconds <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.invalid_duration",
                "Failed to determine export duration for this chart."
            );
        }
        return false;
    }

    if (snapshot == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export snapshot output is null");
        }
        return false;
    }

    const QString exportStem = sanitizeExportFileStem(document.title, directoryInfo.completeBaseName());
    const QString difficultyName = SimaiDocument::difficultyShortName(difficultyId).replace(':', '_');
    const QString defaultOutputName = QStringLiteral("%1_%2.mp4").arg(exportStem, difficultyName);

    VideoExportSnapshot built;
    built.jobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    built.createdAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    built.chartTextUtf8 = chartText;
    built.difficultyId = difficultyId;
    built.difficultyName = difficultyToken;
    built.originalChartPath = chartPath;
    built.projectDir = QFileInfo(chartPath).absolutePath();
    built.trackPath = trackPath;
    built.backgroundMediaPath = miacode::chart_assets::resolveBackgroundMediaPath(chartPath);
    built.skinDirectory = resolvePreviewSkinDir();
    built.audioSettings = requestedTask.audioSettings;
    built.audioSettings.normalize();
    built.backgroundBrightnessOuter = requestedTask.backgroundBrightnessOuter;
    built.backgroundBrightnessInner = requestedTask.backgroundBrightnessInner;
    built.layoutSquareScale = requestedTask.layoutSquareScale;
    built.smoothBrightness = requestedTask.smoothBrightness;
    built.backgroundScaleMode = requestedTask.backgroundScaleMode;
    built.noteFlowSpeed = requestedTask.noteFlowSpeed;
    built.muriRenderOptions = requestedTask.muriRenderOptions;
    built.staticTapOnSlideThresholdSeconds = requestedTask.staticTapOnSlideThresholdSeconds;
    built.exportStartSeconds = 0.0;
    built.contentDurationSeconds = contentDurationSeconds;
    built.outputWidth = requestedTask.outputWidth;
    built.outputHeight = requestedTask.outputHeight;
    built.fps = requestedTask.fps;
    built.preset = requestedTask.preset;
    built.fullRangeExport = true;
    built.outputPath = resolveVideoExportOutputPath(
        QString(),
        outputDirectory,
        defaultOutputName
    );
    built.showTimestamp = requestedTask.showTimestamp;
    built.showObjectStatsHud = requestedTask.showObjectStatsHud;
    built.skinLoadWaitMs = requestedTask.skinLoadWaitMs;

    if (built.skinDirectory.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.batch_export.error.skin_missing",
                "Preview skin assets were not found."
            );
        }
        return false;
    }

    *snapshot = built;
    return true;
}

bool MainWindow::startVideoExportWorkerProcess(QProcess* process, const VideoExportSnapshot& snapshot, QString* errorMessage)
{
    if (process == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export worker process is null");
        }
        return false;
    }

    const QString executablePath = QCoreApplication::applicationFilePath();
    if (executablePath.trimmed().isEmpty() || !QFileInfo::exists(executablePath)) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.executable_missing", "Failed to locate MiaCode executable.");
        }
        return false;
    }

    process->setProcessChannelMode(QProcess::SeparateChannels);
    QStringList workerArgs;
    if (miacode::debug_options::debugModeEnabled()) {
        workerArgs.append(QStringLiteral("--debug"));
    }
    workerArgs.append(QStringLiteral("--export-video-worker"));
    process->start(executablePath, workerArgs, QIODevice::ReadWrite);
    if (!process->waitForStarted(5000)) {
        if (errorMessage != nullptr) {
            *errorMessage = process->errorString();
        }
        return false;
    }

    const QByteArray payload = buildVideoExportWorkerStartPayload(snapshot);
    if (process->write(payload) != payload.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText(
                "dialog.video_export.error.worker_write_failed",
                "Failed to send export snapshot to worker."
            );
        }
        process->kill();
        process->waitForFinished(1000);
        return false;
    }
    process->closeWriteChannel();
    return true;
}

bool MainWindow::runVideoExportWorkerSync(
    const VideoExportSnapshot& snapshot,
    QProgressDialog* progressDialog,
    bool* canceledByUser,
    QString* errorMessage,
    const std::function<void(int percent, const QString& rawMessage)>& progressCallback
)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (canceledByUser != nullptr) {
        *canceledByUser = false;
    }

    QProcess process;
    if (!startVideoExportWorkerProcess(&process, snapshot, errorMessage)) {
        return false;
    }

    QByteArray stdoutBuffer;
    QByteArray stderrBuffer;
    bool finishedEventReceived = false;
    bool success = false;
    QString resultMessage;
    QString resultDetails;
    QElapsedTimer itemElapsed;
    qint64 smoothedEtaSeconds = -1;
    itemElapsed.start();

    const auto parseStdoutLines = [&]() {
        while (true) {
            const int newlineIndex = stdoutBuffer.indexOf('\n');
            if (newlineIndex < 0) {
                break;
            }
            const QByteArray rawLine = stdoutBuffer.left(newlineIndex).trimmed();
            stdoutBuffer.remove(0, newlineIndex + 1);
            if (rawLine.isEmpty()) {
                continue;
            }
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(rawLine, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
                resultDetails += (resultDetails.isEmpty() ? QString() : QStringLiteral("\n")) + QString::fromUtf8(rawLine);
                continue;
            }
            const QJsonObject object = document.object();
            const QString eventType = object.value(QStringLiteral("event")).toString();
            if (eventType == QLatin1String("progress")) {
                const int percent = object.value(QStringLiteral("percent")).toInt(-1);
                const QString message = object.value(QStringLiteral("message")).toString();
                if (progressCallback) {
                    progressCallback(percent, message);
                } else if (progressDialog != nullptr && !message.trimmed().isEmpty()) {
                    progressDialog->setLabelText(
                        buildExportProgressLabelTextForUiLanguage(
                            message,
                            qBound(0, percent, 100),
                            itemElapsed,
                            &smoothedEtaSeconds
                        )
                    );
                }
                continue;
            }
            if (eventType == QLatin1String("finished")) {
                finishedEventReceived = true;
                success = object.value(QStringLiteral("success")).toBool(false);
                resultMessage = object.value(QStringLiteral("message")).toString();
                resultDetails = object.value(QStringLiteral("details")).toString();
            }
        }
    };

    while (process.state() != QProcess::NotRunning) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        stdoutBuffer.append(process.readAllStandardOutput());
        stderrBuffer.append(process.readAllStandardError());
        parseStdoutLines();
        if (progressDialog != nullptr && progressDialog->wasCanceled()) {
            if (canceledByUser != nullptr) {
                *canceledByUser = true;
            }
            process.kill();
            process.waitForFinished(2000);
            return false;
        }
        process.waitForFinished(50);
    }

    stdoutBuffer.append(process.readAllStandardOutput());
    stderrBuffer.append(process.readAllStandardError());
    parseStdoutLines();

    const QString stderrText = QString::fromUtf8(stderrBuffer).trimmed();
    const QString stdoutTailText = QString::fromUtf8(stdoutBuffer).trimmed();
    const QString processErrorText = process.errorString().trimmed();
    const QString workerDiagnostics = buildWorkerProcessDiagnostics(
        process.exitCode(),
        process.exitStatus(),
        processErrorText,
        stderrText,
        stdoutTailText
    );
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage != nullptr) {
            const QString summary = !resultMessage.trimmed().isEmpty()
                ? resultMessage
                : (!stderrText.isEmpty()
                    ? stderrText.split('\n').constFirst().trimmed()
                    : uiText("dialog.batch_export.error.export_failed", QStringLiteral("Export failed.")));
            *errorMessage = compactWorkerExitSummary(process.exitCode(), process.exitStatus(), summary);
        }
        return false;
    }

    if (!finishedEventReceived || !success) {
        if (errorMessage != nullptr) {
            const QString summary = !resultMessage.trimmed().isEmpty()
                ? resultMessage
                : (!stderrText.isEmpty()
                    ? stderrText.split('\n').constFirst().trimmed()
                    : uiText("dialog.batch_export.error.export_failed", QStringLiteral("Export failed.")));
            const bool genericFailure = resultMessage.trimmed().isEmpty() && stderrText.trimmed().isEmpty();
            *errorMessage = genericFailure
                ? appendVideoExportDiagnostics(summary, workerDiagnostics)
                : summary;
        }
        return false;
    }

    return true;
}

void MainWindow::showExportToolbarMenu()
{
    if (exportVideoButton_ == nullptr || exportVideoMenu_ == nullptr) {
        return;
    }
    if (!exportVideoButton_->isVisible() || !exportVideoButton_->underMouse()) {
        return;
    }
    if (QApplication::mouseButtons().testAnyFlag(Qt::AllButtons)) {
        return;
    }
    if (exportVideoMenu_->isVisible()) {
        return;
    }
    const QPoint globalPos = exportVideoButton_->mapToGlobal(QPoint(0, exportVideoButton_->height()));
    exportVideoMenu_->popup(globalPos);
}

bool MainWindow::launchVideoExportWorker(const VideoExportSnapshot& snapshot, QString* errorMessage)
{
    if (videoExportWorkerProcess_ != nullptr && videoExportWorkerProcess_->state() != QProcess::NotRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = uiText("dialog.video_export.error.worker_busy", "Another export is already running.");
        }
        return false;
    }

    clearVideoExportWorkerState();

    auto* progress = new QProgressDialog(
        systemL10n(QStringLiteral("Preparing export..."), QStringLiteral("准备导出...")),
        systemL10n(QStringLiteral("Cancel"), QStringLiteral("取消")),
        0,
        100,
        this
    );
    progress->setWindowTitle(systemL10n(QStringLiteral("Export Video"), QStringLiteral("导出视频")));
    progress->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    progress->setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    progress->setWindowModality(Qt::NonModal);
    progress->setLabelText(uiText("dialog.video_export.progress.preparing", "Preparing export..."));
    progress->setCancelButtonText(uiText("dialog.video_export.button.cancel", "Cancel"));
    progress->setWindowTitle(uiText("dialog.video_export.title", "Export Video"));
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);
    progress->setMinimumWidth(320);
    progress->setMaximumWidth(360);
    progress->setValue(0);
    if (QLabel* label = progress->findChild<QLabel*>(); label != nullptr) {
        label->setWordWrap(true);
    }
    progress->show();
    videoExportProgressDialog_ = progress;

    auto* process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    videoExportWorkerProcess_ = process;
    videoExportWorkerJobId_ = snapshot.jobId;
    videoExportWorkerOutputPath_ = snapshot.outputPath;
    videoExportWorkerResultMessage_.clear();
    videoExportWorkerResultDetails_.clear();
    videoExportWorkerStdoutBuffer_.clear();
    videoExportWorkerStderrBuffer_.clear();
    videoExportWorkerSuccess_ = false;
    videoExportWorkerCompletionReceived_ = false;
    videoExportWorkerCancelRequested_ = false;
    videoExportWorkerLastProgressPercent_ = 0;
    videoExportWorkerLastEtaSeconds_ = -1;
    videoExportWorkerElapsed_.start();

    connect(progress, &QProgressDialog::canceled, this, &MainWindow::cancelVideoExportWorker);
    connect(process, &QProcess::readyReadStandardOutput, this, &MainWindow::handleVideoExportWorkerStdout);
    connect(process, &QProcess::readyReadStandardError, this, &MainWindow::handleVideoExportWorkerStderr);
    connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        handleVideoExportWorkerProcessFinished(exitCode, static_cast<int>(exitStatus));
    });

    if (!startVideoExportWorkerProcess(process, snapshot, errorMessage)) {
        clearVideoExportWorkerState();
        return false;
    }
    return true;
}

void MainWindow::handleVideoExportWorkerStdout()
{
    if (videoExportWorkerProcess_ == nullptr) {
        return;
    }
    videoExportWorkerStdoutBuffer_.append(videoExportWorkerProcess_->readAllStandardOutput());
    while (true) {
        const int newlineIndex = videoExportWorkerStdoutBuffer_.indexOf('\n');
        if (newlineIndex < 0) {
            break;
        }
        const QByteArray rawLine = videoExportWorkerStdoutBuffer_.left(newlineIndex).trimmed();
        videoExportWorkerStdoutBuffer_.remove(0, newlineIndex + 1);
        if (rawLine.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(rawLine, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (!videoExportWorkerResultDetails_.isEmpty()) {
                videoExportWorkerResultDetails_.append('\n');
            }
            videoExportWorkerResultDetails_.append(QString::fromUtf8(rawLine));
            continue;
        }
        handleVideoExportWorkerEvent(document.object());
    }
}

void MainWindow::handleVideoExportWorkerStderr()
{
    if (videoExportWorkerProcess_ == nullptr) {
        return;
    }
    videoExportWorkerStderrBuffer_.append(videoExportWorkerProcess_->readAllStandardError());
}

void MainWindow::handleVideoExportWorkerEvent(const QJsonObject& eventObject)
{
    const QString eventType = eventObject.value(QStringLiteral("event")).toString();
    const bool suppressProgressUi = videoExportWorkerCancelRequested_;
    if (eventType == QLatin1String("worker_ready")) {
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr) {
            videoExportProgressDialog_->setValue(qMax(videoExportProgressDialog_->value(), 1));
            QTimer::singleShot(0, this, [this]() {
                if (videoExportProgressDialog_ != nullptr) {
                    videoExportProgressDialog_->setLabelText(
                        uiText("dialog.video_export.progress.worker_ready", "Worker ready...")
                    );
                }
            });
            videoExportProgressDialog_->setLabelText(
                uiText("dialog.video_export.progress.worker_ready", "Worker ready...")
            );
            videoExportProgressDialog_->setLabelText(systemL10n(
                QStringLiteral("Worker ready..."),
                QStringLiteral("后台已就绪...")
            ));
        }
        return;
    }
    if (eventType == QLatin1String("accepted")) {
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr) {
            videoExportProgressDialog_->setValue(qMax(videoExportProgressDialog_->value(), 2));
            QTimer::singleShot(0, this, [this]() {
                if (videoExportProgressDialog_ != nullptr) {
                    videoExportProgressDialog_->setLabelText(
                        uiText("dialog.video_export.progress.starting_export", "Starting export...")
                    );
                }
            });
            videoExportProgressDialog_->setLabelText(
                uiText("dialog.video_export.progress.starting_export", "Starting export...")
            );
            videoExportProgressDialog_->setLabelText(systemL10n(
                QStringLiteral("Starting export..."),
                QStringLiteral("开始导出...")
            ));
        }
        return;
    }
    if (eventType == QLatin1String("progress")) {
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr) {
            const int percent = qBound(0, eventObject.value(QStringLiteral("percent")).toInt(), 100);
            const QString rawMessage = eventObject.value(QStringLiteral("message")).toString(
                QStringLiteral("Exporting...")
            );
            const bool busyStage = exportWorkerProgressUsesBusyIndicator(rawMessage);
            if (busyStage) {
                if (videoExportProgressDialog_->minimum() != 0 || videoExportProgressDialog_->maximum() != 0) {
                    videoExportProgressDialog_->setRange(0, 0);
                }
                videoExportProgressDialog_->setLabelText(
                    buildExportProgressLabelTextForUiLanguage(
                        rawMessage,
                        videoExportWorkerLastProgressPercent_,
                        videoExportWorkerElapsed_,
                        &videoExportWorkerLastEtaSeconds_
                    )
                );
                return;
            }
            if (videoExportProgressDialog_->minimum() == 0 && videoExportProgressDialog_->maximum() == 0) {
                videoExportProgressDialog_->setRange(0, 100);
            }
            videoExportWorkerLastProgressPercent_ = qMax(videoExportWorkerLastProgressPercent_, percent);
            videoExportProgressDialog_->setValue(videoExportWorkerLastProgressPercent_);
            videoExportProgressDialog_->setLabelText(
                buildExportProgressLabelTextForUiLanguage(
                    rawMessage,
                    videoExportWorkerLastProgressPercent_,
                    videoExportWorkerElapsed_,
                    &videoExportWorkerLastEtaSeconds_
                )
            );
        }
        return;
    }
    if (eventType == QLatin1String("log")) {
        const QString message = eventObject.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            if (!videoExportWorkerResultDetails_.isEmpty()) {
                videoExportWorkerResultDetails_.append('\n');
            }
            videoExportWorkerResultDetails_.append(message);
        }
        return;
    }
    if (eventType == QLatin1String("finished")) {
        videoExportWorkerCompletionReceived_ = true;
        videoExportWorkerSuccess_ = eventObject.value(QStringLiteral("success")).toBool(false);
        videoExportWorkerOutputPath_ = eventObject.value(QStringLiteral("output_path")).toString(videoExportWorkerOutputPath_);
        videoExportWorkerResultMessage_ = videoExportWorkerSuccess_
            ? QStringLiteral("ok")
            : eventObject.value(QStringLiteral("error")).toString(uiText("dialog.video_export.error.failed", "Export failed."));
        const QString details = eventObject.value(QStringLiteral("details")).toString().trimmed();
        if (!details.isEmpty()) {
            videoExportWorkerResultDetails_ = details;
        }
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr) {
            if (videoExportProgressDialog_->minimum() == 0 && videoExportProgressDialog_->maximum() == 0) {
                videoExportProgressDialog_->setRange(0, 100);
            }
            videoExportProgressDialog_->setValue(videoExportWorkerSuccess_ ? 100 : videoExportProgressDialog_->value());
            if (videoExportWorkerSuccess_) {
                videoExportProgressDialog_->setLabelText(systemL10n(
                    QStringLiteral("Done."),
                    QStringLiteral("导出完成。")
                ));
            }
        }
        if (!suppressProgressUi && videoExportProgressDialog_ != nullptr && videoExportWorkerSuccess_) {
            videoExportProgressDialog_->setLabelText(
                uiText("dialog.video_export.progress.done", "Done.")
            );
        }
    }
}

void MainWindow::handleVideoExportWorkerProcessFinished(int exitCode, int exitStatus)
{
    Q_UNUSED(exitCode);

    const auto restorePreviewAspectIfNeeded = [this]() {
        if (!restoreSquareAfterVideoExport_) {
            return;
        }
        restoreSquareAfterVideoExport_ = false;
        setPreviewCanvasAspectRatio(1.0, false);
    };

    if (videoExportProgressDialog_ != nullptr) {
        videoExportProgressDialog_->hide();
    }

    const QString stderrText = QString::fromUtf8(videoExportWorkerStderrBuffer_).trimmed();
    const QString stdoutTailText = QString::fromUtf8(videoExportWorkerStdoutBuffer_).trimmed();
    const QString processErrorText = videoExportWorkerProcess_ != nullptr
        ? videoExportWorkerProcess_->errorString().trimmed()
        : QString();
    const QString workerDiagnostics = buildWorkerProcessDiagnostics(
        exitCode,
        static_cast<QProcess::ExitStatus>(exitStatus),
        processErrorText,
        stderrText,
        stdoutTailText
    );
    const bool canceledOutcome =
        videoExportWorkerCancelRequested_
        && (!videoExportWorkerCompletionReceived_ || !videoExportWorkerSuccess_);
    if (canceledOutcome) {
        restorePreviewAspectIfNeeded();
        showCenteredLocalizedMessageBox(
            QMessageBox::Information,
            this,
            uiText("dialog.video_export.title", "Export Video"),
            uiText("dialog.video_export.message.canceled", "Export canceled.")
        );
        clearVideoExportWorkerState();
        return;
    }

    if (!videoExportWorkerCompletionReceived_) {
        videoExportWorkerSuccess_ = false;
        videoExportWorkerResultMessage_ = exitStatus == static_cast<int>(QProcess::CrashExit)
            ? uiText("dialog.video_export.error.worker_crash", "Export worker crashed.")
            : uiText("dialog.video_export.error.worker_exit", "Export worker exited unexpectedly.");
        videoExportWorkerResultDetails_ = workerDiagnostics;
    } else if (!stderrText.isEmpty() && !videoExportWorkerSuccess_) {
        videoExportWorkerResultDetails_ = appendVideoExportDiagnostics(videoExportWorkerResultDetails_, workerDiagnostics);
    } else if (!videoExportWorkerSuccess_) {
        videoExportWorkerResultDetails_ = appendVideoExportDiagnostics(videoExportWorkerResultDetails_, workerDiagnostics);
    }
    if (!videoExportWorkerSuccess_) {
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("export/worker"),
            QStringLiteral("%1 | %2")
                .arg(videoExportWorkerResultMessage_.trimmed(), videoExportWorkerResultDetails_.trimmed())
        );
    }

    if (videoExportWorkerSuccess_) {
        const QFileInfo resolvedOutputInfo(videoExportWorkerOutputPath_);
        const QString resolvedOutputName = resolvedOutputInfo.fileName().trimmed().isEmpty()
            ? QDir::toNativeSeparators(videoExportWorkerOutputPath_)
            : resolvedOutputInfo.fileName();
        QMessageBox dialog(
            QMessageBox::Information,
            uiText("dialog.video_export.title", "Export Video"),
            QStringLiteral("%1\n\n%2")
                .arg(uiText("dialog.video_export.message.completed", "Export completed."))
                .arg(resolvedOutputName),
            QMessageBox::NoButton,
            this
        );
        dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        QPushButton* openButton = dialog.addButton(
            uiText("action.open", "Open"),
            QMessageBox::AcceptRole
        );
        dialog.addButton(
            uiText("action.close", "Close"),
            QMessageBox::RejectRole
        );
        dialog.setDefaultButton(openButton);
        centerDialogOnAnchor(&dialog, this);
        dialog.exec();
        if (dialog.clickedButton() == openButton) {
            const QFileInfo outputInfo(videoExportWorkerOutputPath_);
            const QString outputDir = outputInfo.absoluteDir().absolutePath();
            if (!outputDir.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir));
            }
        }
    } else {
        const QString details = videoExportWorkerResultDetails_.trimmed();
        showCenteredLocalizedMessageBox(
            QMessageBox::Critical,
            this,
            uiText("dialog.video_export.error.failed_title", "Export Failed"),
            details.isEmpty()
                ? videoExportWorkerResultMessage_
                : QStringLiteral("%1\n\n%2").arg(videoExportWorkerResultMessage_, details)
        );
    }

    restorePreviewAspectIfNeeded();
    clearVideoExportWorkerState();
}

void MainWindow::cancelVideoExportWorker()
{
    videoExportWorkerCancelRequested_ = true;
    if (videoExportProgressDialog_ != nullptr) {
        QTimer::singleShot(0, this, [this]() {
            if (videoExportProgressDialog_ != nullptr) {
                videoExportProgressDialog_->setLabelText(
                    uiText("dialog.video_export.progress.canceling", "Canceling export...")
                );
            }
        });
    }
    if (videoExportProgressDialog_ != nullptr) {
        videoExportProgressDialog_->setLabelText(systemL10n(
            QStringLiteral("Canceling export..."),
            QStringLiteral("正在取消导出...")
        ));
        videoExportProgressDialog_->hide();
    }
    if (videoExportWorkerProcess_ == nullptr || videoExportWorkerProcess_->state() == QProcess::NotRunning) {
        return;
    }

    QPointer<QProcess> processGuard(videoExportWorkerProcess_);
    videoExportWorkerProcess_->terminate();
    QTimer::singleShot(500, this, [this, processGuard]() {
        if (processGuard.isNull() || processGuard != videoExportWorkerProcess_) {
            return;
        }
        if (processGuard->state() != QProcess::NotRunning) {
            processGuard->kill();
        }
    });
}

void MainWindow::clearVideoExportWorkerState()
{
    if (videoExportProgressDialog_ != nullptr) {
        videoExportProgressDialog_->close();
        videoExportProgressDialog_->deleteLater();
        videoExportProgressDialog_ = nullptr;
    }
    if (videoExportWorkerProcess_ != nullptr) {
        videoExportWorkerProcess_->disconnect(this);
        if (videoExportWorkerProcess_->state() != QProcess::NotRunning) {
            videoExportWorkerProcess_->kill();
            videoExportWorkerProcess_->waitForFinished(2000);
        }
        videoExportWorkerProcess_->deleteLater();
        videoExportWorkerProcess_ = nullptr;
    }
    videoExportWorkerStdoutBuffer_.clear();
    videoExportWorkerStderrBuffer_.clear();
    videoExportWorkerJobId_.clear();
    videoExportWorkerOutputPath_.clear();
    videoExportWorkerResultMessage_.clear();
    videoExportWorkerResultDetails_.clear();
    videoExportWorkerElapsed_.invalidate();
    videoExportWorkerSuccess_ = false;
    videoExportWorkerCompletionReceived_ = false;
    videoExportWorkerCancelRequested_ = false;
    restoreSquareAfterVideoExport_ = false;
    videoExportWorkerLastProgressPercent_ = 0;
    videoExportWorkerLastEtaSeconds_ = -1;
}

bool MainWindow::exportPreviewVideoFromCli(
    const CliVideoExportRequest& request,
    QString* resolvedOutputPath,
    QString* errorMessage,
    QString* details
)
{
    if (resolvedOutputPath != nullptr) {
        resolvedOutputPath->clear();
    }
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (details != nullptr) {
        details->clear();
    }

    const auto fail = [errorMessage, details](const QString& message, const QString& detail = QString()) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        if (details != nullptr) {
            *details = detail;
        }
        return false;
    };

    if (request.outputWidth <= 0 || request.outputHeight <= 0 || request.fps <= 0) {
        return fail(QStringLiteral("output width/height and fps must be positive integers"));
    }
    if (request.outputWidth < request.outputHeight) {
        return fail(QStringLiteral("output size currently requires width >= height"));
    }

    const QString chartPath = resolveChartPathFromCliInput(request.chartPathOrDirectory);
    if (chartPath.isEmpty()) {
        return fail(
            QStringLiteral("cannot resolve chart file from input path"),
            request.chartPathOrDirectory
        );
    }

    bool usedSystemEncoding = false;
    const QString chartText = readTextFileWithFallbackEncoding(chartPath, &usedSystemEncoding);
    if (chartText.isNull()) {
        return fail(QStringLiteral("failed to read chart file"), chartPath);
    }

    setCurrentFilePath(chartPath);
    loadDocument(SimaiDocument::fromText(chartText));
    refreshWaveformCache();

    const int difficultyId = difficultyIdFromCliToken(request.difficulty);
    if (!SimaiDocument::isDifficultyId(difficultyId)) {
        return fail(
            QStringLiteral("invalid difficulty token"),
            QStringLiteral("expected one of: ESY/BAS/ADV/EXP/MAS/REM/UTG or 1..7")
        );
    }

    if (document_.difficulty(difficultyId) == nullptr) {
        QStringList available;
        const QVector<int> ids = document_.difficultyIds();
        available.reserve(ids.size());
        for (int id : ids) {
            available.append(SimaiDocument::difficultyShortName(id));
        }
        return fail(
            QStringLiteral("requested difficulty is missing in chart"),
            QStringLiteral("requested=%1 available=%2")
                .arg(SimaiDocument::difficultyShortName(difficultyId))
                .arg(available.join(','))
        );
    }
    if (!switchToDifficultyField(difficultyId)) {
        return fail(QStringLiteral("failed to switch to requested difficulty"));
    }

    refreshTimelineMetadata();
    if (latestTimelineNoteMarkers_.isEmpty()) {
        return fail(QStringLiteral("no parsed note markers for requested difficulty"));
    }

    const QString previewSkinDir = resolvePreviewSkinDir();
    if (previewSkinDir.trimmed().isEmpty()) {
        return fail(QStringLiteral("preview skin directory is empty"));
    }

    const QFileInfo chartInfo(currentFilePath_);
    const QString exportStem = sanitizeExportFileStem(document_.title, QStringLiteral("out"));
    const QString difficultyName = SimaiDocument::difficultyShortName(difficultyId).replace(':', '_');
    const QString defaultOutputName = QString("%1_%2.mp4")
        .arg(exportStem)
        .arg(difficultyName);

    QString outputPath = resolveVideoExportOutputPath(
        request.outputPath,
        chartInfo.absoluteDir().absolutePath(),
        defaultOutputName
    );

    const QFileInfo outputInfo(outputPath);
    const QString outputDirPath = outputInfo.absolutePath();
    if (!outputDirPath.isEmpty() && !QDir(outputDirPath).exists()) {
        if (!QDir().mkpath(outputDirPath)) {
            return fail(QStringLiteral("cannot create output directory"), outputDirPath);
        }
    }

    const double exportStartSeconds = qMax(0.0, request.exportStartSeconds);
    const auto previewMarkerEndSecond = [](const TimelineNoteMarker& marker) {
        double markerEnd = qMax(marker.second, marker.endSecond);
        markerEnd = qMax(markerEnd, marker.slideTraceSecond);
        markerEnd = qMax(markerEnd, marker.availableSecond);
        for (double shootSecond : marker.slideSegmentShootSeconds) {
            markerEnd = qMax(markerEnd, shootSecond);
        }
        return qMax(0.0, markerEnd);
    };
    double lastMarkerEndSecond = 0.0;
    for (const TimelineNoteMarker& marker : latestTimelineNoteMarkers_) {
        lastMarkerEndSecond = qMax(lastMarkerEndSecond, previewMarkerEndSecond(marker));
    }
    const double cappedExportEndSecond = qMax(
        0.0,
        qMin(previewDurationSeconds(), lastMarkerEndSecond + 3.0)
    );
    const double maxDuration = qMax(0.0, cappedExportEndSecond - exportStartSeconds);
    const double contentDurationSeconds = request.contentDurationSeconds > 0.0
        ? request.contentDurationSeconds
        : maxDuration;
    const bool fullRangeExport =
        exportStartSeconds <= 1e-6
        && exportStartSeconds + contentDurationSeconds + 1e-6 >= cappedExportEndSecond;
    if (contentDurationSeconds <= 0.0) {
        return fail(
            QStringLiteral("content duration is not positive"),
            QStringLiteral("start=%1 total=%2")
                .arg(exportStartSeconds, 0, 'f', 3)
                .arg(cappedExportEndSecond, 0, 'f', 3)
        );
    }

    VideoExportTask task;
    task.chartPath = currentFilePath_;
    task.trackPath = resolveDefaultTrackPath();
    task.skinDirectory = previewSkinDir;
    task.noteMarkers = latestTimelineNoteMarkers_;
    task.muriAnalysisReport = muriAnalysisReport_;
    task.muriRenderOptions = muriRenderOptions_;
    task.staticTapOnSlideThresholdSeconds = static_cast<double>(staticTapOnSlideThresholdMs_) / 1000.0;
    task.audioSettings = previewAudioSettings_;
    task.backgroundBrightnessOuter = request.backgroundBrightnessOuter;
    task.backgroundBrightnessInner = request.backgroundBrightnessInner;
    task.layoutSquareScale = request.layoutSquareScale;
    task.smoothBrightness = request.smoothBrightness;
    task.backgroundScaleMode = request.backgroundScaleMode;
    task.noteFlowSpeed = request.noteFlowSpeed;
    task.exportStartSeconds = exportStartSeconds;
    task.contentDurationSeconds = contentDurationSeconds;
    task.fullRangeExport = fullRangeExport;
    task.outputWidth = request.outputWidth;
    task.outputHeight = request.outputHeight;
    task.fps = request.fps;
    task.showTimestamp = request.showTimestamp;
    task.showObjectStatsHud = request.showObjectStatsHud;
    task.outputPath = outputPath;

    const VideoExportResult exportResult = VideoExportController::exportFullPreview(task, nullptr);
    if (!exportResult.success) {
        return fail(exportResult.message, exportResult.details);
    }

    if (resolvedOutputPath != nullptr) {
        *resolvedOutputPath = outputPath;
    }
    if (details != nullptr) {
        QStringList detailLines;
        detailLines << QStringLiteral("chart=%1").arg(chartPath);
        detailLines << QStringLiteral("difficulty=%1").arg(SimaiDocument::difficultyShortName(difficultyId));
        detailLines << QStringLiteral("encoding=%1").arg(usedSystemEncoding ? QStringLiteral("system") : QStringLiteral("utf8"));
        detailLines << QStringLiteral("noteCount=%1").arg(latestTimelineNoteMarkers_.size());
        detailLines << QStringLiteral("trackPath=%1").arg(task.trackPath.isEmpty() ? QStringLiteral("(none)") : task.trackPath);
        detailLines << QStringLiteral("skinLoaded=%1").arg(previewSkinDir.isEmpty() ? 0 : 1);
        *details = detailLines.join('\n');
    }
    return true;
}

void MainWindow::onOpenLatencyDetector()
{
    const QString trackPath = resolveLatencyDetectorTrackPath();
    bool wholeBpmOk = false;
    const double wholeBpm = parsedWholeBpm(&wholeBpmOk);
    const QString meterId = parsedLatencyMeterId();
    const double offsetSeconds = parsedFirstSeconds();
    if (trackPath.isEmpty()) {
        statusBar()->showMessage(UiText::isChineseUi()
            ? QStringLiteral("当前谱面目录缺少 track.mp3，无法打开BPM&偏移检测。")
            : QStringLiteral("track.mp3 was not found next to the current chart."));
        updateLatencyDetectorAvailability();
        return;
    }

    if (latencyDetectorDialog_ != nullptr) {
        if (latencyDetectorDialog_->trackPath() == trackPath) {
            latencyDetectorDialog_->setOffsetSeconds(offsetSeconds);
            latencyDetectorDialog_->setBpm(wholeBpmOk ? wholeBpm : 0.0);
            latencyDetectorDialog_->setMeterId(meterId);
            latencyDetectorDialog_->raise();
            latencyDetectorDialog_->activateWindow();
            return;
        }
        latencyDetectorDialog_->close();
        latencyDetectorDialog_.clear();
    }

    latencyDetectorDialog_ = new LatencyDetectorDialog(trackPath, currentFilePath_, previewAudioSettings_, this);
    latencyDetectorDialog_->setOffsetSeconds(offsetSeconds);
    latencyDetectorDialog_->setBpm(wholeBpmOk ? wholeBpm : 0.0);
    latencyDetectorDialog_->setMeterId(meterId);
    connect(latencyDetectorDialog_, &LatencyDetectorDialog::offsetChanged, this, [this](double seconds) {
        applyLatencyDetectorOffset(seconds);
    });
    connect(latencyDetectorDialog_, &LatencyDetectorDialog::bpmChanged, this, [this](double bpm) {
        applyLatencyDetectorBpm(bpm);
    });
    connect(latencyDetectorDialog_, &QObject::destroyed, this, [this]() {
        latencyDetectorDialog_.clear();
    });
    latencyDetectorDialog_->show();
    latencyDetectorDialog_->raise();
    latencyDetectorDialog_->activateWindow();
}

void MainWindow::openPreviewSettingsDialog(bool includeAudioSettings, bool includeVideoSettings, const QString& title)
{
    if (!includeAudioSettings && !includeVideoSettings) {
        return;
    }
    previewAudioSettings_.normalize();

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.setModal(true);
    dialog.setMinimumWidth(520);
    dialog.setStyleSheet(UiTheme::settingsDialogStyleSheet());

    const auto createDialogMenuButton = [](QWidget* parent, const QString& text) {
        auto* button = new QToolButton(parent);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
        button->setText(text);
        return button;
    };
    const auto flowSpeedValueLabel = [](double flowSpeed) {
        const double snapped = qRound(flowSpeed * 4.0) / 4.0;
        const double roundedOneDecimal = qRound(snapped * 10.0) / 10.0;
        const bool useSingleDecimal = qAbs(snapped - roundedOneDecimal) < 0.001;
        return QString::number(snapped, 'f', useSingleDecimal ? 1 : 2);
    };
    const auto addDialogMenuChoice = [](QMenu* menu, const QString& text, const std::function<void()>& onTriggered) {
        auto* action = new QWidgetAction(menu);
        auto* button = new QToolButton(menu);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setText(text);
        button->setCursor(Qt::PointingHandCursor);
        const auto& c = UiTheme::colors();
        button->setStyleSheet(
            QStringLiteral(
                "QToolButton {"
                " color: %1;"
                " background: transparent;"
                " border: none;"
                " padding: 6px 20px 6px 12px;"
                " text-align: left;"
                "}"
                "QToolButton:hover {"
                " background: %2;"
                " border-radius: 6px;"
                "}"
            )
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(c.menuHoverBg.name(QColor::HexRgb))
        );
        QObject::connect(button, &QToolButton::clicked, menu, [action, menu, onTriggered]() {
            if (onTriggered) {
                onTriggered();
            }
            action->trigger();
            menu->close();
        });
        action->setDefaultWidget(button);
        menu->addAction(action);
    };

    auto* rootLayout = new QVBoxLayout(&dialog);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);
    rootLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto* audioGroup = new QGroupBox(uiText("dialog.render_settings.audio_group", "Audio"), &dialog);
    auto* audioFormLayout = new QFormLayout(audioGroup);
    audioFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    audioFormLayout->setHorizontalSpacing(10);
    audioFormLayout->setVerticalSpacing(8);

    const auto addAudioRow = [&](const QString& labelText, int valuePercent, QSlider** sliderOut, QLabel** labelOut) {
        auto* row = new QWidget(audioGroup);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, 100);
        slider->setValue(valuePercent);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        auto* label = new QLabel(QString::number(valuePercent) + "%", row);
        label->setMinimumWidth(44);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        audioFormLayout->addRow(labelText, row);
        *sliderOut = slider;
        *labelOut = label;
    };

    QSlider* bgmSlider = nullptr;
    QLabel* bgmLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.bgm", "BGM Volume"), previewAudioSettings_.bgmPercent(), &bgmSlider, &bgmLabel);
    QSlider* answerSlider = nullptr;
    QLabel* answerLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.answer", "Answer Volume"), previewAudioSettings_.answerPercent(), &answerSlider, &answerLabel);
    QSlider* judgeSlider = nullptr;
    QLabel* judgeLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.judge", "Judge Volume"), previewAudioSettings_.judgePercent(), &judgeSlider, &judgeLabel);
    QSlider* breakSlider = nullptr;
    QLabel* breakLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.break", "Break Volume"), previewAudioSettings_.breakPercent(), &breakSlider, &breakLabel);
    QSlider* slideSlider = nullptr;
    QLabel* slideLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.slide", "Slide Volume"), previewAudioSettings_.slidePercent(), &slideSlider, &slideLabel);
    QSlider* exSlider = nullptr;
    QLabel* exLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.ex", "EX Volume"), previewAudioSettings_.exPercent(), &exSlider, &exLabel);
    QSlider* touchSlider = nullptr;
    QLabel* touchLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.touch", "Touch Volume"), previewAudioSettings_.touchPercent(), &touchSlider, &touchLabel);
    QSlider* fireworkSlider = nullptr;
    QLabel* fireworkLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.firework", "Firework Volume"), previewAudioSettings_.fireworkPercent(), &fireworkSlider, &fireworkLabel);
    QSlider* breakSlideSlider = nullptr;
    QLabel* breakSlideLabel = nullptr;
    addAudioRow(uiText("dialog.render_settings.audio.break_slide", "Break Slide Volume"), previewAudioSettings_.breakSlidePercent(), &breakSlideSlider, &breakSlideLabel);

    const auto addVideoSliderRow = [](
        QWidget* parent,
        int minimum,
        int maximum,
        int step,
        int value,
        const QString& suffix,
        QSlider** sliderOut,
        QLabel** labelOut
    ) {
        auto* row = new QWidget(parent);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);
        auto* slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(minimum, maximum);
        slider->setSingleStep(step);
        slider->setPageStep(step);
        slider->setTickInterval(step);
        slider->setValue(value);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        auto* label = new QLabel(QString::number(value) + suffix, row);
        label->setMinimumWidth(44);
        rowLayout->addWidget(slider, 1);
        rowLayout->addWidget(label, 0);
        *sliderOut = slider;
        *labelOut = label;
        return row;
    };

    auto* videoGroup = new QGroupBox(uiText("dialog.render_settings.video_group", "Video"), &dialog);
    auto* videoFormLayout = new QFormLayout(videoGroup);
    videoFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    videoFormLayout->setHorizontalSpacing(10);
    videoFormLayout->setVerticalSpacing(8);

    QSlider* outerBrightnessSlider = nullptr;
    QLabel* outerBrightnessLabel = nullptr;
    QWidget* outerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(previewBackgroundBrightnessOuter_ * 100.0),
        QStringLiteral("%"),
        &outerBrightnessSlider,
        &outerBrightnessLabel
    );
    QSlider* innerBrightnessSlider = nullptr;
    QLabel* innerBrightnessLabel = nullptr;
    QWidget* innerBrightnessRow = addVideoSliderRow(
        videoGroup,
        0,
        100,
        1,
        qRound(previewBackgroundBrightnessInner_ * 100.0),
        QStringLiteral("%"),
        &innerBrightnessSlider,
        &innerBrightnessLabel
    );
    QSlider* layoutSquareScaleSlider = nullptr;
    QLabel* layoutSquareScaleLabel = nullptr;
    QWidget* layoutSquareScaleRow = addVideoSliderRow(
        videoGroup,
        qRound(miacode::preview_video::kLayoutSquareScaleMin * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleMax * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0),
        qRound(previewLayoutSquareScale_ * 100.0),
        QStringLiteral("%"),
        &layoutSquareScaleSlider,
        &layoutSquareScaleLabel
    );
    double selectedFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(previewNoteFlowSpeed_);
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    selectedFlowSpeed = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((selectedFlowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    auto* flowSpeedEdit = new QLineEdit(videoGroup);
    flowSpeedEdit->setAlignment(Qt::AlignCenter);
    flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
    flowSpeedEdit->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet());
    auto* flowSpeedValidator = new QDoubleValidator(flowSpeedMin, flowSpeedMax, 2, flowSpeedEdit);
    flowSpeedValidator->setNotation(QDoubleValidator::StandardNotation);
    flowSpeedEdit->setValidator(flowSpeedValidator);
    QObject::connect(flowSpeedEdit, &QLineEdit::editingFinished, &dialog, [&, flowSpeedEdit]() {
        bool ok = false;
        const double typedSpeed = flowSpeedEdit->text().trimmed().toDouble(&ok);
        if (!ok) {
            flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
            return;
        }
        selectedFlowSpeed = qBound(
            flowSpeedMin,
            flowSpeedMin + qRound((typedSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
            flowSpeedMax
        );
        flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed));
        previewNoteFlowSpeed_ = selectedFlowSpeed;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setNoteFlowSpeed(selectedFlowSpeed);
        }
        saveProjectRenderState();
        savePortableState();
    });

    auto* previewGroup = new QGroupBox(uiText("dialog.render_settings.preview_group", "Preview"), &dialog);
    auto* previewFormLayout = new QFormLayout(previewGroup);
    previewFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    previewFormLayout->setHorizontalSpacing(10);
    previewFormLayout->setVerticalSpacing(8);

    struct CanvasFrameRateOption {
        PreviewCanvasFrameRateMode mode;
        QString label;
    };
    const double detectedRefreshRate = currentPreviewCanvasRefreshRate();
    const QString displayRefreshLabel = QStringLiteral("%1 (%2 Hz)")
        .arg(uiText(
            "dialog.render_settings.preview.canvas_frame_rate.display",
            "Display Refresh Rate"
        ))
        .arg(QString::number(detectedRefreshRate, 'f', detectedRefreshRate >= 100.0 ? 0 : 1));
    const QList<CanvasFrameRateOption> canvasFrameRateOptions{
        {PreviewCanvasFrameRateMode::Fps60, uiText("dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS")},
        {PreviewCanvasFrameRateMode::Fps120, uiText("dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS")},
        {PreviewCanvasFrameRateMode::DisplayRefresh, displayRefreshLabel},
    };
    QString selectedCanvasFrameRateLabel = canvasFrameRateOptions.front().label;
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        if (option.mode == previewCanvasFrameRateMode_) {
            selectedCanvasFrameRateLabel = option.label;
            break;
        }
    }
    auto* canvasFrameRateButton = createDialogMenuButton(previewGroup, selectedCanvasFrameRateLabel);
    canvasFrameRateButton->setFixedHeight(flowSpeedEdit->sizeHint().height());
    canvasFrameRateButton->setStyleSheet(
        UiTheme::dialogMenuButtonStyleSheet()
        + QStringLiteral("QToolButton { text-align: center; padding: 2px 22px 2px 10px; }")
    );
    auto* canvasFrameRateMenu = new QMenu(canvasFrameRateButton);
    styleRoundedMenu(*canvasFrameRateMenu);
    for (const CanvasFrameRateOption& option : canvasFrameRateOptions) {
        const PreviewCanvasFrameRateMode mode = option.mode;
        const QString label = option.label;
        addDialogMenuChoice(canvasFrameRateMenu, label, [this, canvasFrameRateButton, mode, label]() {
            canvasFrameRateButton->setText(label);
            setPreviewCanvasFrameRateMode(mode, true);
        });
    }
    canvasFrameRateButton->setMenu(canvasFrameRateMenu);

    const QString scaleFillLabel = uiText("dialog.render_settings.video.scale.fill", "Fill (crop if needed)");
    const QString scaleFitLabel = uiText("dialog.render_settings.video.scale.fit", "Fit (keep full image, may letterbox)");
    const QString standardSkinLabel = uiText("dialog.render_settings.video.skin.standard", "Standard");
    const QString dxSkinLabel = uiText("dialog.render_settings.video.skin.dx", "DX");
    auto* skinButton = createDialogMenuButton(
        videoGroup,
        previewSkinVariant_ == PreviewSkinVariant::Dx ? dxSkinLabel : standardSkinLabel
    );
    auto* skinMenu = new QMenu(skinButton);
    styleRoundedMenu(*skinMenu);
    addDialogMenuChoice(skinMenu, standardSkinLabel, [this, skinButton, standardSkinLabel]() {
        previewSkinVariant_ = PreviewSkinVariant::Standard;
        skinButton->setText(standardSkinLabel);
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setSkinDirectory(resolvePreviewSkinDir());
        }
        saveProjectRenderState();
        savePortableState();
    });
    addDialogMenuChoice(skinMenu, dxSkinLabel, [this, skinButton, dxSkinLabel]() {
        previewSkinVariant_ = PreviewSkinVariant::Dx;
        skinButton->setText(dxSkinLabel);
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setSkinDirectory(resolvePreviewSkinDir());
        }
        saveProjectRenderState();
        savePortableState();
    });
    skinButton->setMenu(skinMenu);
    PreviewBackgroundScaleMode selectedScaleMode = previewBackgroundScaleMode_;
    auto* scaleModeButton = createDialogMenuButton(
        videoGroup,
        selectedScaleMode == PreviewBackgroundScaleMode::FitContain ? scaleFitLabel : scaleFillLabel
    );
    auto* scaleModeMenu = new QMenu(scaleModeButton);
    styleRoundedMenu(*scaleModeMenu);
    addDialogMenuChoice(scaleModeMenu, scaleFillLabel, [&, scaleFillLabel]() {
        selectedScaleMode = PreviewBackgroundScaleMode::FillCrop;
        scaleModeButton->setText(scaleFillLabel);
        previewBackgroundScaleMode_ = selectedScaleMode;
        applyPreviewStageMediaRouteVisualSettings();
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundScaleMode(selectedScaleMode);
        }
        saveProjectRenderState();
        savePortableState();
    });
    addDialogMenuChoice(scaleModeMenu, scaleFitLabel, [&, scaleFitLabel]() {
        selectedScaleMode = PreviewBackgroundScaleMode::FitContain;
        scaleModeButton->setText(scaleFitLabel);
        previewBackgroundScaleMode_ = selectedScaleMode;
        applyPreviewStageMediaRouteVisualSettings();
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundScaleMode(selectedScaleMode);
        }
        saveProjectRenderState();
        savePortableState();
    });
    scaleModeButton->setMenu(scaleModeMenu);

    auto* smoothBrightnessCheck = new QCheckBox(
        uiText("dialog.render_settings.video.smooth_brightness", "Smooth brightness"),
        videoGroup
    );
    smoothBrightnessCheck->setChecked(previewSmoothBrightness_);
    auto* timestampCheck = new QCheckBox(
        uiText("dialog.video_export.option.show_timestamp", "Show bottom-left timestamp"),
        previewGroup
    );
    timestampCheck->setChecked(previewShowTimestamp_);
    const bool unifiedObjectStatsChecked = previewShowObjectStatsHud_ || exportShowObjectStatsHud_;
    previewShowObjectStatsHud_ = unifiedObjectStatsChecked;
    exportShowObjectStatsHud_ = unifiedObjectStatsChecked;
    auto* objectStatsCheck = new QCheckBox(
        uiText("dialog.render_settings.preview.show_object_stats", "Show object stats in preview/export"),
        previewGroup
    );
    objectStatsCheck->setChecked(unifiedObjectStatsChecked);
    auto* validationSummaryCheck = new QCheckBox(
        uiText("dialog.render_settings.preview.show_validation_summary", "Show header error/warning summary"),
        previewGroup
    );
    validationSummaryCheck->setChecked(previewShowValidationSummary_);
    auto* debugCheck = new QCheckBox(
        uiText("dialog.render_settings.preview.debug", "Show preview debug info"),
        previewGroup
    );
    debugCheck->setChecked(previewShowDebugInfo_);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_outer", "Outer Brightness"), outerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.brightness_inner", "Inner Brightness"), innerBrightnessRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.layout_square_scale", "Layout Size"), layoutSquareScaleRow);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.flow_speed", "Flow Speed"), flowSpeedEdit);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.skin", "Skin"), skinButton);
    videoFormLayout->addRow(uiText("dialog.render_settings.video.scale_mode", "Background / PV Scale Mode"), scaleModeButton);
    auto* videoCheckRow = new QWidget(videoGroup);
    auto* videoCheckLayout = new QGridLayout(videoCheckRow);
    videoCheckLayout->setContentsMargins(0, 0, 0, 0);
    videoCheckLayout->setHorizontalSpacing(10);
    videoCheckLayout->setVerticalSpacing(6);
    videoCheckLayout->setColumnStretch(0, 1);
    videoCheckLayout->setColumnStretch(1, 1);
    videoCheckLayout->addWidget(smoothBrightnessCheck, 0, 0, Qt::AlignLeft);
    videoCheckLayout->addWidget(timestampCheck, 0, 1, Qt::AlignLeft);
    videoCheckLayout->addWidget(debugCheck, 1, 1, Qt::AlignLeft);
    videoFormLayout->addRow(QString(), videoCheckRow);

    videoCheckLayout->removeWidget(timestampCheck);
    videoCheckLayout->removeWidget(debugCheck);

    previewFormLayout->addRow(
        uiText("dialog.render_settings.preview.canvas_frame_rate", "Preview Refresh Rate"),
        canvasFrameRateButton
    );
    auto* previewCheckRow = new QWidget(previewGroup);
    auto* previewCheckLayout = new QGridLayout(previewCheckRow);
    previewCheckLayout->setContentsMargins(0, 0, 0, 0);
    previewCheckLayout->setHorizontalSpacing(6);
    previewCheckLayout->setVerticalSpacing(6);
    previewCheckLayout->setColumnStretch(0, 1);
    previewCheckLayout->setColumnStretch(1, 1);
    previewCheckLayout->addWidget(timestampCheck, 0, 0, Qt::AlignLeft);
    previewCheckLayout->addWidget(debugCheck, 0, 1, Qt::AlignLeft);
    previewCheckLayout->addWidget(objectStatsCheck, 1, 0, Qt::AlignLeft);
    previewCheckLayout->addWidget(validationSummaryCheck, 1, 1, Qt::AlignLeft);
    previewFormLayout->addRow(QString(), previewCheckRow);

    audioGroup->setVisible(includeAudioSettings);
    videoGroup->setVisible(includeVideoSettings);
    previewGroup->setVisible(includeVideoSettings);

    if (includeAudioSettings) {
        rootLayout->addWidget(audioGroup, 0);
    }
    if (includeVideoSettings) {
        rootLayout->addWidget(videoGroup, 0);
        rootLayout->addWidget(previewGroup, 0);
    }
    auto* buttonBox = new QDialogButtonBox(&dialog);
    QPushButton* saveLocalAudioPresetButton = nullptr;
    QPushButton* applyLocalAudioPresetButton = nullptr;
    if (includeAudioSettings) {
        saveLocalAudioPresetButton = buttonBox->addButton(
            uiText("dialog.render_settings.button.set_software_default_audio", "Save Local Preset"),
            QDialogButtonBox::ActionRole
        );
        applyLocalAudioPresetButton = buttonBox->addButton(
            uiText("dialog.render_settings.button.restore_project_default", "Apply Local Preset"),
            QDialogButtonBox::ActionRole
        );
        if (saveLocalAudioPresetButton != nullptr) {
            saveLocalAudioPresetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
        if (applyLocalAudioPresetButton != nullptr) {
            applyLocalAudioPresetButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
    }
    if (QPushButton* closeButton = buttonBox->addButton(uiText("dialog.render_settings.button.close", "Close"), QDialogButtonBox::RejectRole)) {
        closeButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    }
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    rootLayout->addWidget(buttonBox);

    auto* audioApplyTimer = new QTimer(&dialog);
    audioApplyTimer->setSingleShot(true);
    audioApplyTimer->setInterval(220);
    QString pendingAudition;

    auto queueAudioApply = [audioApplyTimer, &pendingAudition](const QString& audition) {
        pendingAudition = audition;
        audioApplyTimer->start();
    };

    const auto syncAudioControlsFromCurrentSettings = [
        this,
        bgmSlider,
        bgmLabel,
        answerSlider,
        answerLabel,
        judgeSlider,
        judgeLabel,
        breakSlider,
        breakLabel,
        slideSlider,
        slideLabel,
        exSlider,
        exLabel,
        touchSlider,
        touchLabel,
        fireworkSlider,
        fireworkLabel,
        breakSlideSlider,
        breakSlideLabel
    ]() {
        previewAudioSettings_.normalize();
        const auto syncAudioRow = [](QSlider* slider, QLabel* label, int valuePercent) {
            if (slider == nullptr || label == nullptr) {
                return;
            }
            const QSignalBlocker blocker(slider);
            slider->setValue(valuePercent);
            label->setText(QString::number(valuePercent) + "%");
        };
        syncAudioRow(bgmSlider, bgmLabel, previewAudioSettings_.bgmPercent());
        syncAudioRow(answerSlider, answerLabel, previewAudioSettings_.answerPercent());
        syncAudioRow(judgeSlider, judgeLabel, previewAudioSettings_.judgePercent());
        syncAudioRow(breakSlider, breakLabel, previewAudioSettings_.breakPercent());
        syncAudioRow(slideSlider, slideLabel, previewAudioSettings_.slidePercent());
        syncAudioRow(exSlider, exLabel, previewAudioSettings_.exPercent());
        syncAudioRow(touchSlider, touchLabel, previewAudioSettings_.touchPercent());
        syncAudioRow(fireworkSlider, fireworkLabel, previewAudioSettings_.fireworkPercent());
        syncAudioRow(breakSlideSlider, breakSlideLabel, previewAudioSettings_.breakSlidePercent());
    };

    connect(bgmSlider, &QSlider::valueChanged, &dialog, [this, bgmLabel, queueAudioApply](int value) {
        previewAudioSettings_.setBgmPercent(value);
        bgmLabel->setText(QString::number(previewAudioSettings_.bgmPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply(QString());
    });
    connect(answerSlider, &QSlider::valueChanged, &dialog, [this, answerLabel, queueAudioApply](int value) {
        previewAudioSettings_.setAnswerPercent(value);
        answerLabel->setText(QString::number(previewAudioSettings_.answerPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("answer");
    });
    connect(judgeSlider, &QSlider::valueChanged, &dialog, [this, judgeLabel, queueAudioApply](int value) {
        previewAudioSettings_.setJudgePercent(value);
        judgeLabel->setText(QString::number(previewAudioSettings_.judgePercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("judge");
    });
    connect(breakSlider, &QSlider::valueChanged, &dialog, [this, breakLabel, queueAudioApply](int value) {
        previewAudioSettings_.setBreakPercent(value);
        breakLabel->setText(QString::number(previewAudioSettings_.breakPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("break");
    });
    connect(slideSlider, &QSlider::valueChanged, &dialog, [this, slideLabel, queueAudioApply](int value) {
        previewAudioSettings_.setSlidePercent(value);
        slideLabel->setText(QString::number(previewAudioSettings_.slidePercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("slide");
    });
    connect(exSlider, &QSlider::valueChanged, &dialog, [this, exLabel, queueAudioApply](int value) {
        previewAudioSettings_.setExPercent(value);
        exLabel->setText(QString::number(previewAudioSettings_.exPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("ex");
    });
    connect(touchSlider, &QSlider::valueChanged, &dialog, [this, touchLabel, queueAudioApply](int value) {
        previewAudioSettings_.setTouchPercent(value);
        touchLabel->setText(QString::number(previewAudioSettings_.touchPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("touch");
    });
    connect(fireworkSlider, &QSlider::valueChanged, &dialog, [this, fireworkLabel, queueAudioApply](int value) {
        previewAudioSettings_.setFireworkPercent(value);
        fireworkLabel->setText(QString::number(previewAudioSettings_.fireworkPercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("firework");
    });
    connect(breakSlideSlider, &QSlider::valueChanged, &dialog, [this, breakSlideLabel, queueAudioApply](int value) {
        previewAudioSettings_.setBreakSlidePercent(value);
        breakSlideLabel->setText(QString::number(previewAudioSettings_.breakSlidePercent()) + "%");
        applyPreviewAudioSettingsToRuntime();
        saveProjectRenderState();
        queueAudioApply("break_slide");
    });
    if (saveLocalAudioPresetButton != nullptr) {
        connect(saveLocalAudioPresetButton, &QPushButton::clicked, &dialog, [this]() {
            previewAudioSettings_.normalize();
            softwarePreviewAudioSettings_ = previewAudioSettings_;
            softwarePreviewAudioSettings_.normalize();
            savePortableState();
        });
    }
    if (applyLocalAudioPresetButton != nullptr) {
        connect(
            applyLocalAudioPresetButton,
            &QPushButton::clicked,
            &dialog,
            [this, audioApplyTimer, &pendingAudition, syncAudioControlsFromCurrentSettings]() {
                previewAudioSettings_ = softwarePreviewAudioSettings_;
                previewAudioSettings_.normalize();
                syncAudioControlsFromCurrentSettings();
                applyPreviewAudioSettingsToRuntime();
                saveProjectRenderState();
                if (audioApplyTimer->isActive()) {
                    audioApplyTimer->stop();
                }
                pendingAudition.clear();
            }
        );
    }

    connect(audioApplyTimer, &QTimer::timeout, &dialog, [this, audioApplyTimer, bgmSlider, answerSlider, judgeSlider, breakSlider, slideSlider, exSlider, touchSlider, fireworkSlider, breakSlideSlider, &pendingAudition]() {
        if (bgmSlider->isSliderDown()
            || answerSlider->isSliderDown()
            || judgeSlider->isSliderDown()
            || breakSlider->isSliderDown()
            || slideSlider->isSliderDown()
            || exSlider->isSliderDown()
            || touchSlider->isSliderDown()
            || fireworkSlider->isSliderDown()
            || breakSlideSlider->isSliderDown()) {
            audioApplyTimer->start();
            return;
        }
        if (!pendingAudition.isEmpty()) {
            ensurePreviewSfxRuntimePrepared();
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && previewSfxRuntime_ != nullptr
            && previewSfxRuntime_->audition(pendingAudition);
        Q_UNUSED(handledLocally);
        pendingAudition.clear();
    });
    connect(&dialog, &QDialog::finished, &dialog, [this, audioApplyTimer, &pendingAudition]() {
        if (!audioApplyTimer->isActive()) {
            return;
        }
        audioApplyTimer->stop();
        if (!pendingAudition.isEmpty()) {
            ensurePreviewSfxRuntimePrepared();
        }
        const bool handledLocally = !pendingAudition.isEmpty()
            && previewSfxRuntime_ != nullptr
            && previewSfxRuntime_->audition(pendingAudition);
        Q_UNUSED(handledLocally);
        pendingAudition.clear();
    });

    connect(outerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, outerBrightnessLabel](int value) {
        previewBackgroundBrightnessOuter_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        outerBrightnessLabel->setText(QString::number(value) + "%");
        applyPreviewStageMediaRouteVisualSettings();
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundBrightnessOuter(previewBackgroundBrightnessOuter_);
        }
        saveProjectRenderState();
        savePortableState();
    });
    connect(innerBrightnessSlider, &QSlider::valueChanged, &dialog, [this, innerBrightnessLabel](int value) {
        previewBackgroundBrightnessInner_ = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
        innerBrightnessLabel->setText(QString::number(value) + "%");
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setBackgroundBrightnessInner(previewBackgroundBrightnessInner_);
        }
        saveProjectRenderState();
        savePortableState();
    });
    connect(layoutSquareScaleSlider, &QSlider::valueChanged, &dialog, [this, layoutSquareScaleLabel](int value) {
        previewLayoutSquareScale_ = miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(value) / 100.0);
        layoutSquareScaleLabel->setText(QString::number(qRound(previewLayoutSquareScale_ * 100.0)) + "%");
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setLayoutSquareScale(previewLayoutSquareScale_);
        }
        saveProjectRenderState();
        savePortableState();
    });
    connect(smoothBrightnessCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewSmoothBrightness_ = checked;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setSmoothBrightness(previewSmoothBrightness_);
        }
        saveProjectRenderState();
        savePortableState();
    });
    connect(timestampCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewShowTimestamp_ = checked;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setShowTimestamp(previewShowTimestamp_);
        }
        saveProjectRenderState();
        savePortableState();
    });

    connect(debugCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewShowDebugInfo_ = checked;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setShowDebugInfo(previewShowDebugInfo_);
        }
        saveProjectRenderState();
        savePortableState();
    });
    const auto setObjectStatsHudEnabled = [this, objectStatsCheck](bool checked) {
        previewShowObjectStatsHud_ = checked;
        exportShowObjectStatsHud_ = checked;
        if (previewCanvas_ != nullptr) {
            previewCanvas_->setShowObjectStatsHud(previewShowObjectStatsHud_);
        }
        const QSignalBlocker objectStatsBlocker(objectStatsCheck);
        objectStatsCheck->setChecked(checked);
        saveProjectRenderState();
        savePortableState();
    };
    connect(objectStatsCheck, &QCheckBox::toggled, &dialog, [setObjectStatsHudEnabled](bool checked) {
        setObjectStatsHudEnabled(checked);
    });
    connect(validationSummaryCheck, &QCheckBox::toggled, &dialog, [this](bool checked) {
        previewShowValidationSummary_ = checked;
        saveProjectRenderState();
        savePortableState();
        updateEditorValidationSummary();
    });
    dialog.adjustSize();
    dialog.exec();
    scheduleEmbeddedPreviewSurfaceRefresh();
}
