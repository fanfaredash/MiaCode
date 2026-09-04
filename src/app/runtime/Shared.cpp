#include "runtime/Shared.h"

#include "app/v2/ApplicationServices.h"

#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/MiniaudioFileAccess.h"
#include "common/WaveformCache.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QSignalBlocker>
#include <QStringList>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QWindow>
#include <QtMath>

namespace miacode::runtime::shared {

const QList<double> kEditorLineSpacingFactorOptions{
    0.0, 1.0, 1.5, 2.0, 3.0, 5.0,
};

namespace {

const QList<double> kPreviewPlaybackRateOptions{
    0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0,
};

QString normalizeLanguageToken(QString token)
{
    token = token.trimmed().toLower();
    token.replace('-', '_');
    return token;
}

}  // namespace

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

SimaiNativeValidationLocale uiValidationLocale()
{
    // One implementation, owned by the non-Widget application layer. This name
    // stays because the widget-side call sites already use it, but the mapping
    // itself must not live in a QtWidgets translation unit — asking "which
    // locale does the parser validate in" should not require the widget layer.
    return miacode::v2::uiValidationLocale();
}

QByteArray autosaveContentSignature(const QString& text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
}

QString resolveProjectDataDirectoryPath(const QString& filePath)
{
    return miacode::waveform::projectDataDirectoryPathForFile(filePath);
}

void appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs)
{
    miacode::debug_log::appendStartupTimingStage(stage, elapsedMs, deltaMs);
}

void centerDialogOnAnchor(QDialog* dialog, QWidget* parent)
{
    if (dialog == nullptr) {
        return;
    }
    dialog->adjustSize();
    const bool debugDialogAnchor = miacode::debug_options::runtimeDebugOutputEnabled();
    const auto appendDialogAnchorLog = [debugDialogAnchor](const QString& text) {
        if (!debugDialogAnchor) {
            return;
        }
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("dialog/anchor"),
            text
        );
    };

    QWidget* anchorWidget = parent != nullptr ? parent->window() : nullptr;
    QRect anchorRect;
    QScreen* targetScreen = nullptr;
    QString anchorSource;
    if (anchorWidget != nullptr
        && !anchorRect.isValid()
        && anchorWidget->isVisible()
        && !anchorWidget->windowState().testFlag(Qt::WindowMinimized)
        && !anchorWidget->windowFlags().testFlag(Qt::Tool)) {
        anchorRect = anchorWidget->frameGeometry();
        if (anchorWidget->windowHandle() != nullptr) {
            targetScreen = anchorWidget->windowHandle()->screen();
        }
        anchorSource = QStringLiteral("parent_window");
    }

    const auto tryFindTopLevelWindow = [dialog](bool requireActive, bool allowToolWindows) -> QWindow* {
        const auto windows = QGuiApplication::topLevelWindows();
        for (QWindow* window : windows) {
            if (window == nullptr
                || window == dialog->windowHandle()
                || !window->isVisible()
                || window->visibility() == QWindow::Hidden
                || window->visibility() == QWindow::Minimized) {
                continue;
            }
            const Qt::WindowFlags flags = window->flags();
            if (!allowToolWindows && (flags.testFlag(Qt::Tool) || flags.testFlag(Qt::Popup))) {
                continue;
            }
            if (requireActive && !window->isActive()) {
                continue;
            }
            return window;
        }
        return nullptr;
    };

    if (!anchorRect.isValid()) {
        if (QWindow* topLevelWindow = tryFindTopLevelWindow(true, false); topLevelWindow != nullptr) {
            anchorRect = topLevelWindow->frameGeometry();
            targetScreen = topLevelWindow->screen();
            anchorSource = QStringLiteral("active_non_tool_qwindow");
        }
    }
    if (!anchorRect.isValid()) {
        if (QWindow* topLevelWindow = tryFindTopLevelWindow(false, false); topLevelWindow != nullptr) {
            anchorRect = topLevelWindow->frameGeometry();
            targetScreen = topLevelWindow->screen();
            anchorSource = QStringLiteral("visible_non_tool_qwindow");
        }
    }
    if (!anchorRect.isValid()) {
        if (QWidget* activeWidget = QApplication::activeWindow();
            activeWidget != nullptr
            && activeWidget != dialog
            && activeWidget->isVisible()
            && !activeWidget->windowState().testFlag(Qt::WindowMinimized)
            && !activeWidget->windowFlags().testFlag(Qt::Tool)) {
            anchorRect = activeWidget->frameGeometry();
            anchorWidget = activeWidget;
            if (activeWidget->windowHandle() != nullptr) {
                targetScreen = activeWidget->windowHandle()->screen();
            }
            anchorSource = QStringLiteral("active_qwidget");
        }
    }
    if (!anchorRect.isValid()) {
        if (QWindow* topLevelWindow = tryFindTopLevelWindow(false, true); topLevelWindow != nullptr) {
            anchorRect = topLevelWindow->frameGeometry();
            targetScreen = topLevelWindow->screen();
            anchorSource = QStringLiteral("visible_tool_qwindow");
        }
    }
    if (!anchorRect.isValid()) {
        if (QScreen* screen = QGuiApplication::primaryScreen(); screen != nullptr) {
            anchorRect = screen->availableGeometry();
            targetScreen = screen;
            anchorSource = QStringLiteral("primary_screen");
        }
    }
    appendDialogAnchorLog(
        QString("title=%1 source=%2 rect=[%3,%4 %5x%6] parent_vis=%7 parent_title=%8")
            .arg(dialog->windowTitle())
            .arg(anchorSource.isEmpty() ? QStringLiteral("none") : anchorSource)
            .arg(anchorRect.x())
            .arg(anchorRect.y())
            .arg(anchorRect.width())
            .arg(anchorRect.height())
            .arg(parent != nullptr && parent->window() != nullptr && parent->window()->isVisible() ? 1 : 0)
            .arg(parent != nullptr && parent->window() != nullptr ? parent->window()->windowTitle() : QStringLiteral("(null)"))
    );
    if (anchorRect.isValid()) {
        QPoint targetTopLeft(
            anchorRect.center().x() - dialog->width() / 2,
            anchorRect.center().y() - dialog->height() / 2
        );
        if (targetScreen == nullptr) {
            targetScreen = QGuiApplication::screenAt(anchorRect.center());
        }
        if (targetScreen == nullptr && anchorWidget != nullptr && anchorWidget->windowHandle() != nullptr) {
            targetScreen = anchorWidget->windowHandle()->screen();
        }
        if (targetScreen != nullptr) {
            const QRect avail = targetScreen->availableGeometry();
            targetTopLeft.setX(qBound(avail.left(), targetTopLeft.x(), avail.right() - dialog->width() + 1));
            targetTopLeft.setY(qBound(avail.top(), targetTopLeft.y(), avail.bottom() - dialog->height() + 1));
        }
        appendDialogAnchorLog(
            QString("title=%1 target=[%2,%3 %4x%5] screen=%6")
                .arg(dialog->windowTitle())
                .arg(targetTopLeft.x())
                .arg(targetTopLeft.y())
                .arg(dialog->width())
                .arg(dialog->height())
                .arg(targetScreen != nullptr ? targetScreen->name() : QStringLiteral("(null)"))
        );
        dialog->move(targetTopLeft);
    }
}

QFont editorFont(int pointSize)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    static const QString bundledEditorFontFamily = []() -> QString {
        const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/maple_mono_cn.ttf"));
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        return families.isEmpty() ? QString() : families.first();
    }();
    if (!bundledEditorFontFamily.isEmpty()) {
        font.setFamily(bundledEditorFontFamily);
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    if (pointSize > 0) {
        font.setPointSize(pointSize);
    }
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
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
    QTextBlockFormat format;
    format.setBottomMargin(static_cast<qreal>(qMax(0, blockSpacingPixels)));
    cursor.mergeBlockFormat(format);
    cursor.endEditBlock();
}

void styleRoundedMenu(QMenu& menu)
{
    UiTheme::styleRoundedMenu(menu);
}

qint64 fileLastModifiedMs(const QFileInfo& fileInfo)
{
    return fileInfo.exists() ? fileInfo.lastModified().toMSecsSinceEpoch() : -1;
}

double probeAudioDurationSeconds(const QString& trackPath)
{
    if (trackPath.isEmpty() || !QFileInfo::exists(trackPath)) {
        return 0.0;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoder decoder;
    if (miacode::audio_io::decoderInitFile(trackPath, &config, &decoder) != MA_SUCCESS) {
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

// See the declaration in Shared.h for why this must stay the only writer of
// `playing_`. Moved verbatim from ShellHost.cpp's `Session::setPreviewPlayingFlag`
// (Stage 4.9d-4b-2); the broadcast target changed from `Session::presentationChanged`
// (which only forwarded it, see SessionBootstrap.cpp) to `notifications` directly,
// the same retargeting PlaybackState.cpp's `updatePauseButtonAppearance` already did
// for the sibling presentation announcement. Stage 4.9e-4 retargeted the
// parameter from RuntimeContext::State to RuntimeContext::PlaybackState (see
// Shared.h) — kept the parameter NAME `state` so the `state_?\.playing_`
// single-writer grep/spec pattern still matches this exact line.
void writePreviewPlayingFlag(
    RuntimeContext::PlaybackState& state,
    miacode::v2::ShellNotifications& notifications,
    bool playing)
{
    if (state.playing_ == playing) {
        return;
    }
    state.playing_ = playing;
    state.previewTransportState_ = playing
        ? miacode::v2::PlaybackTransportState::Playing
        : (state.previewTransportState_ == miacode::v2::PlaybackTransportState::Stopped
               ? miacode::v2::PlaybackTransportState::Stopped
               : miacode::v2::PlaybackTransportState::Paused);
    QMetaObject::invokeMethod(
        &notifications,
        [&notifications]() { emit notifications.presentationChanged(); },
        Qt::QueuedConnection);
}

}  // namespace miacode::runtime::shared
