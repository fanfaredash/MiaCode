#include "VideoExportDialog.h"

#include "DialogLocalization.h"
#include "EditableValueLabel.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/PreviewInteractionConfig.h"
#include "core/scene/PreviewHudState.h"
#include "tools/video_export/HudFontSettings.h"
#include "tools/video_export/IntroPreviewWidget.h"
#include "tools/video_export/VideoExportPreferences.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleValidator>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <QWidgetAction>

#include <limits>
#include <utility>

namespace {

constexpr int kPreviewSliderScale = 1000;
constexpr int kPreviewTickIntervalMs = 33;
constexpr int kFormLabelWidth = 52;
constexpr int kRangeLabelWidth = 36;
constexpr int kFormRowSpacing = 3;
constexpr int kSectionContentLeftInset = 12;
constexpr int kDialogMinWidth = 560;
constexpr int kSetButtonLeftGap = 12;
constexpr int kPreviewControlButtonWidth = 32;
constexpr int kPreviewControlButtonHeight = 26;
constexpr int kPreviewControlSpacing = 6;
constexpr int kPreviewControlsToSliderGap = 8;
constexpr int kDialogActionButtonMinWidth = 92;
constexpr int kRangeSetButtonWidth = 72;
constexpr int kPreviewScrubRenderIntervalMs = 33;

struct ResolutionPreset {
    int width = 1080;
    int height = 1080;
    const char* label = "1080x1080 (1:1)";
    double aspectRatio = 1.0;
};

constexpr ResolutionPreset kResolutionPresets[] = {
    {720, 720, "720x720 (1:1)", 1.0},
    {1024, 1024, "1024x1024 (1:1)", 1.0},
    {960, 720, "960x720 (4:3)", 4.0 / 3.0},
    {1280, 720, "1280x720 (16:9)", 16.0 / 9.0},
    {1080, 1080, "1080x1080 (1:1)", 1.0},
    {1440, 1080, "1440x1080 (4:3)", 4.0 / 3.0},
    {1920, 1080, "1920x1080 (16:9)", 16.0 / 9.0},
    {1440, 1440, "1440x1440 (1:1)", 1.0},
    {1920, 1440, "1920x1440 (4:3)", 4.0 / 3.0},
    {2560, 1440, "2560x1440 (16:9)", 16.0 / 9.0},
};

constexpr int kFpsOptions[] = {60, 120};
constexpr int kAudioBitrateOptionsKbps[] = {128, 160, 192, 256, 320};

int normaliseAudioBitrateKbps(int requested)
{
    int closest = kAudioBitrateOptionsKbps[0];
    int closestDelta = qAbs(requested - closest);
    for (int candidate : kAudioBitrateOptionsKbps) {
        const int delta = qAbs(requested - candidate);
        if (delta < closestDelta) {
            closest = candidate;
            closestDelta = delta;
        }
    }
    return closest;
}

// (The HUD-font library helpers + the font-settings dialog moved to
// tools/video_export/HudFontSettings.cpp on 2026-06-10, shared with the main
// window's 视频设置 dialog.)

enum class VideoExportShortcutAction {
    None,
    TogglePlayPause,
    StopOrPlay,
};

VideoExportShortcutAction matchVideoExportShortcut(const QKeyEvent* event)
{
    if (event == nullptr) {
        return VideoExportShortcutAction::None;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers == Qt::NoModifier && event->key() == Qt::Key_Space) {
        return VideoExportShortcutAction::TogglePlayPause;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        return VideoExportShortcutAction::StopOrPlay;
    }
    if (modifiers == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_X) {
        return VideoExportShortcutAction::TogglePlayPause;
    }
    return VideoExportShortcutAction::None;
}

QString uiText(const char* key, const QString& fallback)
{
    const QString translated = UiText::text(QString::fromLatin1(key));
    return translated.isEmpty() ? fallback : translated;
}

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

QString exportDialogResolutionLabel(const QSize& size)
{
    for (const ResolutionPreset& preset : kResolutionPresets) {
        if (preset.width == size.width() && preset.height == size.height()) {
            return QString::fromLatin1(preset.label);
        }
    }
    return QStringLiteral("%1x%2").arg(qMax(1, size.width())).arg(qMax(1, size.height()));
}

QString videoExportPresetToken(VideoExportPreset preset)
{
    switch (preset) {
    case VideoExportPreset::HighQuality:
        return QStringLiteral("high_quality");
    case VideoExportPreset::Fast:
    default:
        return QStringLiteral("fast");
    }
}

VideoExportPreset videoExportPresetFromStoredValue(const QJsonValue& value, VideoExportPreset fallback)
{
    if (value.isString()) {
        const QString token = value.toString().trimmed();
        if (token.compare(QStringLiteral("high_quality"), Qt::CaseInsensitive) == 0) {
            return VideoExportPreset::HighQuality;
        }
        if (token.compare(QStringLiteral("high_compression"), Qt::CaseInsensitive) == 0) {
            return VideoExportPreset::HighQuality;
        }
        if (token.compare(QStringLiteral("fast"), Qt::CaseInsensitive) == 0) {
            return VideoExportPreset::Fast;
        }
    }
    return fallback;
}

QString exportDialogPresetLabel(VideoExportPreset preset)
{
    switch (preset) {
    case VideoExportPreset::HighQuality:
        return uiText("dialog.video_export.preset.high_quality", QStringLiteral("High Quality"));
    case VideoExportPreset::Fast:
    default:
        return uiText("dialog.video_export.preset.fast", QStringLiteral("Fast"));
    }
}

QString exportDialogBackgroundScaleModeLabel(PreviewBackgroundScaleMode mode)
{
    switch (mode) {
    case PreviewBackgroundScaleMode::FitContain:
        return uiText("dialog.video_export.option.scale.fit", QStringLiteral("Fit (keep full image, may letterbox)"));
    case PreviewBackgroundScaleMode::SquareFitContain:
        return uiText("dialog.video_export.option.scale.square_fit", QStringLiteral("1:1 Fit (center square)"));
    case PreviewBackgroundScaleMode::FillCrop:
    default:
        return uiText("dialog.video_export.option.scale.fill", QStringLiteral("Fill (crop if needed)"));
    }
}

int secondToSliderValue(double second)
{
    return qMax(0, qRound(second * kPreviewSliderScale));
}

QString exportBaseDirectory(const VideoExportTask& task)
{
    const QFileInfo chartInfo(task.chartPath);
    if (!chartInfo.absoluteDir().path().isEmpty()) {
        return chartInfo.absoluteDir().absolutePath();
    }
    return QDir::currentPath();
}

QString displayOutputPathForDialog(const QString& outputPath, const QString& baseDirectory)
{
    if (outputPath.trimmed().isEmpty()) {
        return QString();
    }
    const QFileInfo outputInfo(outputPath);
    const QString absolutePath = outputInfo.isRelative()
        ? QDir(baseDirectory).absoluteFilePath(outputPath)
        : outputInfo.absoluteFilePath();
    return QDir::toNativeSeparators(QDir(baseDirectory).relativeFilePath(QDir::cleanPath(absolutePath)));
}

QString resolveOutputPathForExport(const QString& outputPath, const QString& baseDirectory)
{
    const QString trimmed = QDir::fromNativeSeparators(outputPath.trimmed());
    if (trimmed.isEmpty()) {
        return QString();
    }
    const QFileInfo outputInfo(trimmed);
    const QString absolutePath = outputInfo.isRelative()
        ? QDir(baseDirectory).absoluteFilePath(trimmed)
        : outputInfo.absoluteFilePath();
    return QDir::cleanPath(absolutePath);
}

QToolButton* createDialogMenuButton(QWidget* parent, const QString& text, int minimumWidth = 0)
{
    auto* button = new QToolButton(parent);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
    button->setText(text);
    if (minimumWidth > 0) {
        button->setMinimumWidth(minimumWidth);
    }
    // The rounded bottom border gets clipped because QStyleSheetStyle
    // under-reports the height by a px or two and the layout hands the button
    // exactly that. A min-height floor is ignored here (vertical policy is
    // Fixed, which pins height to sizeHint), so force the height explicitly:
    // ensurePolished() first so sizeHint() reflects the styled metrics, then
    // setFixedHeight() a few px taller so the full border renders.
    button->ensurePolished();
    button->setFixedHeight(qMax(button->sizeHint().height(), 30) + 4);
    return button;
}

QWidgetAction* addDialogMenuChoice(
    QMenu* menu,
    const QString& text,
    const std::function<void()>& onTriggered
)
{
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
    return action;
}

QPoint desiredDialogTopLeft(QWidget* owner, const QSize& dialogSize)
{
    if (owner == nullptr) {
        return QPoint();
    }
    const QRect ownerFrame = owner->frameGeometry();
    int targetCenterX = ownerFrame.center().x();
    QRect anchorRect;
    bool hasAnchor = false;
    const auto mergeGlobalRect = [&](QWidget* widget) {
        if (widget == nullptr || !widget->isVisible()) {
            return;
        }
        const QRect rect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
        if (!hasAnchor) {
            anchorRect = rect;
            hasAnchor = true;
        } else {
            anchorRect = anchorRect.united(rect);
        }
    };
    if (QDockWidget* outlineDock = owner->findChild<QDockWidget*>(QStringLiteral("OutlineDock")); outlineDock != nullptr) {
        mergeGlobalRect(outlineDock);
    }
    if (QWidget* previewPanel = owner->findChild<QWidget*>(QStringLiteral("PreviewPanel")); previewPanel != nullptr) {
        if (QSplitter* splitter = qobject_cast<QSplitter*>(previewPanel->parentWidget()); splitter != nullptr && splitter->count() > 0) {
            mergeGlobalRect(splitter->widget(0));
        }
    }
    if (hasAnchor) {
        targetCenterX = anchorRect.center().x();
    }
    const int targetCenterY = ownerFrame.top() + (ownerFrame.height() / 3);
    return QPoint(targetCenterX - (dialogSize.width() / 2), targetCenterY - (dialogSize.height() / 2));
}

double sliderValueToSecond(int sliderValue)
{
    return qMax(0.0, static_cast<double>(sliderValue) / kPreviewSliderScale);
}

QIcon makePreviewPlayIcon(const QColor& color)
{
    return UiTheme::dialogTransportPlayIcon(color);
}

QIcon makePreviewStopIcon(const QColor& color)
{
    return UiTheme::dialogTransportStopIcon(color);
}

QIcon makePreviewPauseIcon(const QColor& color)
{
    return UiTheme::dialogTransportPauseIcon(color);
}

class TimestampSpinBox : public QDoubleSpinBox
{
public:
    explicit TimestampSpinBox(QWidget* parent = nullptr)
        : QDoubleSpinBox(parent)
    {
        setDecimals(3);
        setSingleStep(0.05);
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setAlignment(Qt::AlignCenter);
        setKeyboardTracking(false);
        // Fluid width: the redesigned range editor lays the spin boxes out in
        // full-width rows, so they grow/shrink with the content column instead
        // of pinning a fixed 92px (which overflowed the 440px budget).
        setMinimumWidth(96);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(28);
    }

protected:
    QString textFromValue(double value) const override
    {
        const qint64 totalMs = qMax<qint64>(0, qRound64(value * 1000.0));
        const qint64 minutes = totalMs / 60000;
        const qint64 sec = (totalMs / 1000) % 60;
        const qint64 ms = totalMs % 1000;
        return QStringLiteral("%1:%2:%3")
            .arg(minutes, 2, 10, QChar('0'))
            .arg(sec, 2, 10, QChar('0'))
            .arg(ms, 3, 10, QChar('0'));
    }

    double valueFromText(const QString& text) const override
    {
        static const QRegularExpression re(QStringLiteral("^\\s*(\\d{1,3}):(\\d{2}):(\\d{3})\\s*$"));
        const QRegularExpressionMatch match = re.match(text);
        if (!match.hasMatch()) {
            return 0.0;
        }
        bool minOk = false;
        bool secOk = false;
        bool msOk = false;
        const int minutes = match.captured(1).toInt(&minOk);
        const int sec = match.captured(2).toInt(&secOk);
        const int ms = match.captured(3).toInt(&msOk);
        if (!minOk || !secOk || !msOk || sec < 0 || sec > 59 || ms < 0 || ms > 999) {
            return 0.0;
        }
        return static_cast<double>(minutes) * 60.0 + static_cast<double>(sec) + static_cast<double>(ms) / 1000.0;
    }

    QValidator::State validate(QString& text, int& pos) const override
    {
        Q_UNUSED(pos);
        static const QRegularExpression partial(QStringLiteral("^\\s*\\d{0,3}(:\\d{0,2}(:\\d{0,3})?)?\\s*$"));
        static const QRegularExpression full(QStringLiteral("^\\s*\\d{1,3}:\\d{2}:\\d{3}\\s*$"));
        if (full.match(text).hasMatch()) {
            return QValidator::Acceptable;
        }
        if (partial.match(text).hasMatch()) {
            return QValidator::Intermediate;
        }
        return QValidator::Invalid;
    }
};

// Export-range selector: a full-duration lane with a highlighted [start, end]
// segment, two draggable handles, and a READ-ONLY gold playhead mirroring the
// preview clock. Pointer-only (the spin boxes below are the keyboard path); all
// linkage flows through std::function callbacks so the class stays moc-free and
// file-local (same pattern as TimestampSpinBox). The track is never a transport:
// body clicks are inert — only the two handles act, and a handle drag live-seeks
// the preview through the dialog's single seek callback. Per the layout skill
// (Z2) one canonical xForSecond()/secondForX() feeds BOTH paint and hit test.
class ExportRangeTrack : public QWidget
{
public:
    explicit ExportRangeTrack(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumWidth(180);
        setMinimumHeight(52);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
    }

    void setTotalSeconds(double total) { total_ = qMax(0.0, total); clampValues(); update(); }
    void setRange(double start, double end) { start_ = start; end_ = end; clampValues(); update(); }
    void setPlayheadSeconds(double second)
    {
        const double clamped = qBound(0.0, second, total_);
        if (qFuzzyCompare(clamped + 1.0, playhead_ + 1.0)) {
            return;
        }
        playhead_ = clamped;
        update();
    }
    void setIntroBannerText(const QString& text) { introBannerText_ = text; update(); }
    void setIntroBannerShown(bool shown)
    {
        if (introBannerShown_ == shown) {
            return;
        }
        introBannerShown_ = shown;
        update();
    }

    std::function<void()> onPressed;
    std::function<void(double)> onStartDragged;
    std::function<void(double)> onEndDragged;
    std::function<void(double)> onReleased;

protected:
    QSize sizeHint() const override { return QSize(320, 52); }

    void paintEvent(QPaintEvent*) override
    {
        const auto& c = UiTheme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF lane = laneRect();
        const double cy = lane.center().y();
        const double xs = xForSecond(start_);
        const double xe = xForSecond(end_);
        const double xp = xForSecond(playhead_);

        // 1) base lane
        p.setPen(QPen(c.border, 1.0));
        p.setBrush(c.inputBg);
        p.drawRoundedRect(lane, 3.0, 3.0);

        // 2) selected [start, end] segment
        QColor segFill = c.accent;
        segFill.setAlpha(70);
        p.setPen(Qt::NoPen);
        p.setBrush(segFill);
        p.drawRect(QRectF(xs, lane.top(), qMax(1.0, xe - xs), lane.height()));
        p.setPen(QPen(c.accent, 1.4));
        p.drawLine(QPointF(xs, lane.top()), QPointF(xs, lane.bottom()));
        p.drawLine(QPointF(xe, lane.top()), QPointF(xe, lane.bottom()));

        // 3) read-only playhead (gold) — mirrors the preview, never seeks
        p.setPen(QPen(c.timelinePlayhead, 2.0));
        p.drawLine(QPointF(xp, cy - 12.0), QPointF(xp, cy + 12.0));
        QPainterPath tri;
        tri.moveTo(xp - 4.0, cy - 17.0);
        tri.lineTo(xp + 4.0, cy - 17.0);
        tri.lineTo(xp, cy - 12.0);
        tri.closeSubpath();
        p.fillPath(tri, c.timelinePlayhead);

        // 4) handles
        auto paintHandle = [&](double x) {
            p.setPen(QPen(c.accentText, 1.0));
            p.setBrush(c.accent);
            p.drawRoundedRect(QRectF(x - 5.0, cy - 9.0, 10.0, 18.0), 3.0, 3.0);
        };
        paintHandle(xs);
        paintHandle(xe);

        // 5) axis labels + optional 含片头 tag
        QFont small = font();
        small.setPointSizeF(qMax(7.0, small.pointSizeF() - 1.0));
        p.setFont(small);
        p.setPen(c.textMuted);
        const double labelY = height() - 15.0;
        p.drawText(QRectF(sideInset_, labelY, width() * 0.5, 14.0),
                   Qt::AlignLeft | Qt::AlignVCenter, axisLabel(0.0));
        p.drawText(QRectF(width() * 0.5, labelY, width() * 0.5 - sideInset_, 14.0),
                   Qt::AlignRight | Qt::AlignVCenter, axisLabel(total_));
        if (introBannerShown_ && !introBannerText_.isEmpty()) {
            p.setPen(c.accent);
            p.drawText(QRectF(xs + 6.0, lane.top() - 16.0, 120.0, 14.0),
                       Qt::AlignLeft | Qt::AlignVCenter, introBannerText_);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || total_ <= 0.0) {
            QWidget::mousePressEvent(event);
            return;
        }
        const int hit = hitTestHandle(event->position().toPoint());
        if (hit < 0) {
            return;  // body clicks are inert — the right transport owns seeking
        }
        dragging_ = hit;
        setCursor(Qt::SizeHorCursor);
        if (onPressed) {
            onPressed();
        }
        emitDrag(secondForX(event->position().x()));
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (dragging_ < 0) {
            setCursor(hitTestHandle(event->position().toPoint()) >= 0 ? Qt::SizeHorCursor
                                                                      : Qt::ArrowCursor);
            return;
        }
        emitDrag(secondForX(event->position().x()));
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (dragging_ < 0) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        const int which = dragging_;
        dragging_ = -1;
        const double s = secondForX(event->position().x());
        const double clamped = (which == 0) ? qBound(0.0, s, end_) : qBound(start_, s, total_);
        setCursor(hitTestHandle(event->position().toPoint()) >= 0 ? Qt::SizeHorCursor
                                                                  : Qt::ArrowCursor);
        if (onReleased) {
            onReleased(clamped);
        }
    }

private:
    static QString axisLabel(double seconds)
    {
        const qint64 t = qMax<qint64>(0, qRound64(seconds));
        return QStringLiteral("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
    }
    QRectF laneRect() const
    {
        const double cy = (height() - 16.0) * 0.5;
        return QRectF(sideInset_, cy - 3.0, qMax(1.0, width() - 2.0 * sideInset_), 6.0);
    }
    double xForSecond(double s) const
    {
        const QRectF lane = laneRect();
        if (total_ <= 0.0) {
            return lane.left();
        }
        return lane.left() + (qBound(0.0, s, total_) / total_) * lane.width();
    }
    double secondForX(double x) const
    {
        const QRectF lane = laneRect();
        if (lane.width() <= 0.0 || total_ <= 0.0) {
            return 0.0;
        }
        return qBound(0.0, (x - lane.left()) / lane.width(), 1.0) * total_;
    }
    int hitTestHandle(const QPoint& pos) const
    {
        if (total_ <= 0.0) {
            return -1;
        }
        const double cy = laneRect().center().y();
        if (pos.y() < cy - 14.0 || pos.y() > cy + 14.0) {
            return -1;
        }
        const double ds = qAbs(pos.x() - xForSecond(start_));
        const double de = qAbs(pos.x() - xForSecond(end_));
        constexpr double grab = 9.0;
        if (ds <= grab || de <= grab) {
            return de < ds ? 1 : 0;
        }
        return -1;
    }
    void emitDrag(double second)
    {
        if (dragging_ == 0) {
            start_ = qBound(0.0, second, end_);
            if (onStartDragged) {
                onStartDragged(start_);
            }
        } else if (dragging_ == 1) {
            end_ = qBound(start_, second, total_);
            if (onEndDragged) {
                onEndDragged(end_);
            }
        }
        update();
    }
    void clampValues()
    {
        start_ = qBound(0.0, start_, total_);
        end_ = qBound(start_, end_, total_);
        playhead_ = qBound(0.0, playhead_, total_);
    }

    double total_ = 0.0;
    double start_ = 0.0;
    double end_ = 0.0;
    double playhead_ = 0.0;
    bool introBannerShown_ = false;
    QString introBannerText_;
    int dragging_ = -1;   // -1 none, 0 start handle, 1 end handle
    static constexpr double sideInset_ = 8.0;
};

}  // namespace

VideoExportDialog::VideoExportDialog(
    const VideoExportTask& baseTask,
    SeekPreviewCallback seekPreviewCallback,
    PlayPreviewCallback playPreviewCallback,
    PausePreviewCallback pausePreviewCallback,
    IsPreviewPlayingCallback isPreviewPlayingCallback,
    CurrentPreviewSecondCallback currentPreviewSecondCallback,
    PreviewTimestampCallback previewTimestampCallback,
    PreviewObjectStatsCallback previewObjectStatsCallback,
    PreviewChartInfoCallback previewChartInfoCallback,
    PreviewAspectRatioCallback previewAspectRatioCallback,
    PreviewBrightnessCallback previewBrightnessCallback,
    PreviewLayoutScaleCallback previewLayoutScaleCallback,
    PreviewSmoothBrightnessCallback previewSmoothBrightnessCallback,
    PreviewScaleModeCallback previewScaleModeCallback,
    PreviewTapFlowSpeedCallback previewTapFlowSpeedCallback,
    PreviewTouchFlowSpeedCallback previewTouchFlowSpeedCallback,
    QWidget* parent
)
    : QDialog(parent)
    , baseTask_(baseTask)
    , seekPreviewCallback_(std::move(seekPreviewCallback))
    , playPreviewCallback_(std::move(playPreviewCallback))
    , pausePreviewCallback_(std::move(pausePreviewCallback))
    , isPreviewPlayingCallback_(std::move(isPreviewPlayingCallback))
    , currentPreviewSecondCallback_(std::move(currentPreviewSecondCallback))
    , previewTimestampCallback_(std::move(previewTimestampCallback))
    , previewObjectStatsCallback_(std::move(previewObjectStatsCallback))
    , previewChartInfoCallback_(std::move(previewChartInfoCallback))
    , previewAspectRatioCallback_(std::move(previewAspectRatioCallback))
    , previewBrightnessCallback_(std::move(previewBrightnessCallback))
    , previewLayoutScaleCallback_(std::move(previewLayoutScaleCallback))
    , previewSmoothBrightnessCallback_(std::move(previewSmoothBrightnessCallback))
    , previewScaleModeCallback_(std::move(previewScaleModeCallback))
    , previewTapFlowSpeedCallback_(std::move(previewTapFlowSpeedCallback))
    , previewTouchFlowSpeedCallback_(std::move(previewTouchFlowSpeedCallback))
    , totalDurationSeconds_(qMax(0.0, baseTask.contentDurationSeconds))
{
    setWindowTitle(uiText("dialog.video_export.title", QStringLiteral("Export Video")));
    setModal(true);
    setMinimumWidth(kDialogMinWidth);
    resize(680, 360);
    setStyleSheet(UiTheme::exportDialogStyleSheet());
    UiDialogs::configureDialogPreviewShortcuts(this, UiDialogs::PreviewShortcutPolicy::LocalPlaybackControls);
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance()); app != nullptr) {
        app->installEventFilter(this);
    }
    initialShowTimestamp_ = baseTask_.showTimestamp;
    initialShowObjectStats_ = baseTask_.showObjectStatsHud;
    initialShowChartInfo_ = baseTask_.showChartInfoHud;

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 10, 12, 10);
    rootLayout->setSpacing(8);
    rootLayout->setSizeConstraint(QLayout::SetDefaultConstraint);

    // Tabbed settings. The persistent time bar (scrubber + transport) is
    // built later and lives OUTSIDE this tab widget so it stays reachable
    // on every page — you can scrub and watch the live preview while tuning
    // any setting. Three pages:
    //   Output       — export-only encoding params.
    //   Visuals      — everything that changes the rendered picture (live),
    //                  including the former HUD overlay toggles + HUD font.
    //   Export Range — the export in/out range.
    settingsTabs_ = new QTabWidget(this);
    settingsTabs_->setObjectName(QStringLiteral("VideoExportTabs"));
    rootLayout->addWidget(settingsTabs_, 0);

    auto* outputPage = new QWidget(settingsTabs_);
    auto* outputPageLayout = new QVBoxLayout(outputPage);
    outputPageLayout->setContentsMargins(4, 6, 4, 6);
    outputPageLayout->setSpacing(10);

    auto* visualsPage = new QWidget(settingsTabs_);
    auto* visualsPageLayout = new QVBoxLayout(visualsPage);
    visualsPageLayout_ = visualsPageLayout;
    visualsPageLayout->setContentsMargins(4, 6, 4, 6);
    visualsPageLayout->setSpacing(8);

    // Gameplay page — owner-wired controls (skin / judge line / judge effect /
    // slide stack order / center display) are injected post-construction via
    // injectOwnerWiredSettings(); the dialog's own Tap/Touch flow-speed rows
    // are placed here too (they belong to the gameplay group).
    gameplayPage_ = new QWidget(settingsTabs_);
    gameplayPageLayout_ = new QVBoxLayout(gameplayPage_);
    gameplayPageLayout_->setContentsMargins(4, 6, 4, 6);
    gameplayPageLayout_->setSpacing(8);

    auto* rangePage = new QWidget(settingsTabs_);
    auto* rangePageLayout = new QVBoxLayout(rangePage);
    rangePageLayout->setContentsMargins(4, 6, 4, 6);
    rangePageLayout->setSpacing(8);

    // Output section — same label-above-control style as the dropdown
    // grid below. Top line is the "Output" label, bottom line carries
    // the path field (stretches) and the Browse button (fixed width).
    auto* outputRow = new QWidget(outputPage);
    auto* outputColumn = new QVBoxLayout(outputRow);
    outputColumn->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    outputColumn->setSpacing(6);
    auto* outputLabel = new QLabel(l10n(QStringLiteral("Output"), QStringLiteral("杈撳嚭")), outputRow);
    outputLabel->setText(uiText("dialog.video_export.output", QStringLiteral("Output")));
    outputColumn->addWidget(outputLabel, 0);
    auto* outputControlRow = new QWidget(outputRow);
    auto* outputControlLayout = new QHBoxLayout(outputControlRow);
    outputControlLayout->setContentsMargins(0, 0, 0, 0);
    outputControlLayout->setSpacing(kFormRowSpacing);
    outputPathEdit_ = new QLineEdit(outputControlRow);
    outputPathEdit_->setText(displayOutputPathForDialog(baseTask_.outputPath, exportBaseDirectory(baseTask_)));
    auto* browseButton = new QPushButton(l10n(QStringLiteral("Browse..."), QStringLiteral("娴忚...")), outputRow);
    outputBrowseButton_ = browseButton;
    browseButton->setText(uiText("dialog.video_export.browse", QStringLiteral("Browse...")));
    browseButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    const int rightAlignedButtonWidth = qMax(browseButton->sizeHint().width(), kDialogActionButtonMinWidth);
    browseButton->setFixedWidth(rightAlignedButtonWidth);
    connect(browseButton, &QPushButton::clicked, this, &VideoExportDialog::browseOutputPath);
    outputControlLayout->addWidget(outputPathEdit_, 1);
    outputControlLayout->addWidget(browseButton, 0);
    outputColumn->addWidget(outputControlRow, 0);
    outputPageLayout->addWidget(outputRow, 0);
    // Beta20-fix — 2x2 grid layout for the 4 dropdown options.
    //
    // Mirrors the "Gameplay" group in the Video Settings dialog: each
    // cell stacks its label above its control, two cells per row, equal
    // column stretch. Replaces the previous single-row "label + button
    // | label + button" arrangement whose labels were either truncated
    // (English ran past the 52 px label clamp, rendering as "Resolutio"
    // / "Audio qu" / "Export Se") or cramped against the dropdown when
    // the clamp was removed.
    //
    // Layout:
    //   ┌────────────────────────┬────────────────────────┐
    //   │ Resolution             │ FPS                    │
    //   │ [1920 × 1080       ▼] │ [60 FPS            ▼] │
    //   ├────────────────────────┼────────────────────────┤
    //   │ Audio quality          │ Export Settings        │
    //   │ [192 kbps          ▼] │ [Fast              ▼] │
    //   └────────────────────────┴────────────────────────┘
    auto* optionsGrid = new QWidget(outputPage);
    auto* optionsGridLayout = new QGridLayout(optionsGrid);
    optionsGridLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    optionsGridLayout->setHorizontalSpacing(16);
    optionsGridLayout->setVerticalSpacing(8);
    optionsGridLayout->setColumnStretch(0, 1);
    optionsGridLayout->setColumnStretch(1, 1);

    const auto addOptionField = [optionsGrid, optionsGridLayout](
        int row,
        int column,
        const QString& labelText,
        QWidget* control
    ) {
        auto* field = new QWidget(optionsGrid);
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        fieldLayout->addWidget(control, 0);
        optionsGridLayout->addWidget(field, row, column);
    };

    // Resolution dropdown (row 0, col 0).
    int currentPresetIndex = -1;
    for (int i = 0; i < static_cast<int>(std::size(kResolutionPresets)); ++i) {
        const QSize size(kResolutionPresets[i].width, kResolutionPresets[i].height);
        if (size.width() == qMax(1, baseTask_.outputWidth)
            && size.height() == qMax(1, baseTask_.outputHeight)) {
            currentPresetIndex = i;
            break;
        }
    }
    if (currentPresetIndex < 0) {
        const double currentAspect =
            static_cast<double>(qMax(1, baseTask_.outputWidth)) / static_cast<double>(qMax(1, baseTask_.outputHeight));
        double bestScore = (std::numeric_limits<double>::max)();
        for (int i = 0; i < static_cast<int>(std::size(kResolutionPresets)); ++i) {
            const QSize size(kResolutionPresets[i].width, kResolutionPresets[i].height);
            const double optionAspect = size.height() > 0
                ? static_cast<double>(size.width()) / static_cast<double>(size.height())
                : 1.0;
            const double aspectPenalty = qAbs(optionAspect - currentAspect) * 1000.0;
            const double areaPenalty = qAbs(static_cast<double>(size.width() * size.height()) - (1920.0 * 1080.0));
            const double score = aspectPenalty + areaPenalty;
            if (score < bestScore) {
                bestScore = score;
                currentPresetIndex = i;
            }
        }
    }
    currentPresetIndex = currentPresetIndex >= 0 ? currentPresetIndex : 0;
    selectedResolution_ = QSize(kResolutionPresets[currentPresetIndex].width, kResolutionPresets[currentPresetIndex].height);
    resolutionButton_ = createDialogMenuButton(
        optionsGrid,
        QString::fromLatin1(kResolutionPresets[currentPresetIndex].label)
    );
    resolutionMenu_ = new QMenu(resolutionButton_);
    UiTheme::styleRoundedMenu(*resolutionMenu_);
    for (const ResolutionPreset& preset : kResolutionPresets) {
        const QSize size(preset.width, preset.height);
        const QString label = QString::fromLatin1(preset.label);
        addDialogMenuChoice(resolutionMenu_, label, [this, size, label]() {
            selectedResolution_ = size;
            if (resolutionButton_ != nullptr) {
                resolutionButton_->setText(label);
            }
            applySelectedAspectRatioToPreview(true);
            persistExportOnlySettings();
        });
    }
    resolutionButton_->setMenu(resolutionMenu_);
    addOptionField(0, 0, uiText("dialog.video_export.resolution", QStringLiteral("Resolution")), resolutionButton_);

    // FPS dropdown (row 0, col 1).
    selectedFps_ = baseTask_.fps >= 90 ? 120 : 60;
    fpsButton_ = createDialogMenuButton(optionsGrid, QStringLiteral("%1 FPS").arg(selectedFps_));
    fpsMenu_ = new QMenu(fpsButton_);
    UiTheme::styleRoundedMenu(*fpsMenu_);
    for (int fps : kFpsOptions) {
        const QString label = QStringLiteral("%1 FPS").arg(fps);
        addDialogMenuChoice(fpsMenu_, label, [this, fps, label]() {
            selectedFps_ = fps;
            if (fpsButton_ != nullptr) {
                fpsButton_->setText(label);
            }
            persistExportOnlySettings();
        });
    }
    fpsButton_->setMenu(fpsMenu_);
    addOptionField(0, 1, uiText("dialog.video_export.fps", QStringLiteral("FPS")), fpsButton_);

    // Audio quality dropdown (row 1, col 0) — picks AAC bitrate forwarded
    // to ffmpeg as `-b:a <kbps>k`. Default 192 is a step above the previous
    // hard-coded 160k baseline; 320k matches the AAC LC stereo ceiling for
    // users who care about bgm fidelity in the exported clip.
    selectedAudioBitrateKbps_ = normaliseAudioBitrateKbps(baseTask_.audioBitrateKbps);
    const auto formatAudioBitrateLabel = [](int kbps) -> QString {
        return QStringLiteral("%1 kbps").arg(kbps);
    };
    audioBitrateButton_ = createDialogMenuButton(optionsGrid, formatAudioBitrateLabel(selectedAudioBitrateKbps_));
    audioBitrateMenu_ = new QMenu(audioBitrateButton_);
    UiTheme::styleRoundedMenu(*audioBitrateMenu_);
    for (int kbps : kAudioBitrateOptionsKbps) {
        const QString label = formatAudioBitrateLabel(kbps);
        addDialogMenuChoice(audioBitrateMenu_, label, [this, kbps, label]() {
            selectedAudioBitrateKbps_ = kbps;
            if (audioBitrateButton_ != nullptr) {
                audioBitrateButton_->setText(label);
            }
            persistExportOnlySettings();
        });
    }
    audioBitrateButton_->setMenu(audioBitrateMenu_);
    addOptionField(
        1,
        0,
        uiText("dialog.video_export.audio_bitrate", QStringLiteral("Audio quality")),
        audioBitrateButton_
    );

    // Export Settings dropdown (row 1, col 1).
    selectedPreset_ = baseTask_.preset;
    presetButton_ = createDialogMenuButton(optionsGrid, exportDialogPresetLabel(selectedPreset_));
    presetMenu_ = new QMenu(presetButton_);
    UiTheme::styleRoundedMenu(*presetMenu_);
    addDialogMenuChoice(
        presetMenu_,
        uiText("dialog.video_export.preset.fast", QStringLiteral("Fast")),
        [this]() {
            selectedPreset_ = VideoExportPreset::Fast;
            if (presetButton_ != nullptr) {
                presetButton_->setText(exportDialogPresetLabel(selectedPreset_));
            }
            persistExportOnlySettings();
        }
    );
    addDialogMenuChoice(
        presetMenu_,
        uiText("dialog.video_export.preset.high_quality", QStringLiteral("High Quality")),
        [this]() {
            selectedPreset_ = VideoExportPreset::HighQuality;
            if (presetButton_ != nullptr) {
                presetButton_->setText(exportDialogPresetLabel(selectedPreset_));
            }
            persistExportOnlySettings();
        }
    );
    presetButton_->setMenu(presetMenu_);
    addOptionField(
        1,
        1,
        uiText("dialog.video_export.preset", QStringLiteral("Export Quality")),
        presetButton_
    );

    outputPageLayout->addWidget(optionsGrid, 0);

    rangeContent_ = new QWidget(rangePage);
    auto* rangeLayout = new QVBoxLayout(rangeContent_);
    rangeLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    rangeLayout->setSpacing(6);

    auto* rangeTitleLabel = new QLabel(
        uiText("dialog.video_export.section.range", QStringLiteral("Export Range")),
        rangeContent_
    );
    QFont rangeTitleFont = rangeTitleLabel->font();
    rangeTitleFont.setWeight(QFont::DemiBold);
    rangeTitleLabel->setFont(rangeTitleFont);
    // The range now occupies its own tab whose label already names it, so the
    // in-panel title would just duplicate the tab caption — keep it built (for
    // any external references) but hidden.
    rangeTitleLabel->hide();
    rangeLayout->addWidget(rangeTitleLabel, 0);

    startSecondSpin_ = new TimestampSpinBox(rangeContent_);
    startSecondSpin_->setRange(0.0, totalDurationSeconds_);
    endSecondSpin_ = new TimestampSpinBox(rangeContent_);
    endSecondSpin_->setRange(0.0, totalDurationSeconds_);

    const double defaultStart = qBound(0.0, baseTask_.exportStartSeconds, totalDurationSeconds_);
    const double defaultEnd = qBound(
        defaultStart,
        defaultStart + qMax(0.0, baseTask_.contentDurationSeconds),
        totalDurationSeconds_
    );
    startSecondSpin_->setValue(defaultStart);
    endSecondSpin_->setValue(defaultEnd);

    // Visual range selector -- replaces the old fixed-width spin/Set/tall-readout
    // grid that overflowed the 440px content column. A full-duration track shows
    // the highlighted [start, end] segment with two draggable handles plus a
    // read-only gold playhead mirroring the preview clock; the Start/End spin
    // boxes (kept as the data model + keyboard path) sit below in full-width
    // rows. Dragging a handle live-seeks the preview through the SAME single
    // seek entry the right-side transport uses -- the two stay in sync and the
    // track is never itself a transport (body clicks are inert).
    auto* track = new ExportRangeTrack(rangeContent_);
    rangeTrack_ = track;
    track->setTotalSeconds(totalDurationSeconds_);
    track->setRange(defaultStart, defaultEnd);
    track->setPlayheadSeconds(previewCursorSecond_);
    track->setIntroBannerText(uiText("dialog.video_export.range.intro_tag", QStringLiteral("intro")));
    track->onPressed = [this]() {
        // Trimming should reveal frames, not fight playback -- pause on grab.
        if (isPreviewPlaying()) {
            stopRangePreview(false);
        }
        previewScrubRenderElapsed_.invalidate();
    };
    track->onStartDragged = [this](double second) {
        startSecondSpin_->setValue(second);   // -> onRangeSpinChanged -> syncRangeUi
        scrubPreviewFromTrack(second, true);
    };
    track->onEndDragged = [this](double second) {
        endSecondSpin_->setValue(second);
        scrubPreviewFromTrack(second, true);
    };
    track->onReleased = [this](double second) {
        scrubPreviewFromTrack(second, false);
    };
    rangeLayout->addWidget(track, 0);

    // Start / End rows: full-width (the layout playbook bans two-column grids in
    // this dialog -- the right column clips). Label + fluid spin + "set current"
    // (captures the live preview position via setRange*FromPreview()).
    auto* startRow = new QWidget(rangeContent_);
    auto* startRowLayout = new QHBoxLayout(startRow);
    startRowLayout->setContentsMargins(0, 0, 0, 0);
    startRowLayout->setSpacing(kSetButtonLeftGap);
    auto* startLabel = new QLabel(uiText("dialog.video_export.range.start", QStringLiteral("Start")), startRow);
    startLabel->setFixedWidth(kRangeLabelWidth);
    startLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* setStartButton = new QPushButton(
        uiText("dialog.video_export.range.set_current", QStringLiteral("Set to current value")), startRow);
    setStartButton_ = setStartButton;
    setStartButton->setToolTip(
        uiText("dialog.video_export.range.set_current.tip", QStringLiteral("Set to the current preview position")));
    setStartButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    startRowLayout->addWidget(startLabel, 0);
    startRowLayout->addWidget(startSecondSpin_, 1);
    startRowLayout->addWidget(setStartButton, 0);
    rangeLayout->addWidget(startRow, 0);

    auto* endRow = new QWidget(rangeContent_);
    auto* endRowLayout = new QHBoxLayout(endRow);
    endRowLayout->setContentsMargins(0, 0, 0, 0);
    endRowLayout->setSpacing(kSetButtonLeftGap);
    auto* endLabel = new QLabel(uiText("dialog.video_export.range.end", QStringLiteral("End")), endRow);
    endLabel->setFixedWidth(kRangeLabelWidth);
    endLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* setEndButton = new QPushButton(
        uiText("dialog.video_export.range.set_current", QStringLiteral("Set to current value")), endRow);
    setEndButton_ = setEndButton;
    setEndButton->setToolTip(
        uiText("dialog.video_export.range.set_current.tip", QStringLiteral("Set to the current preview position")));
    setEndButton->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    endRowLayout->addWidget(endLabel, 0);
    endRowLayout->addWidget(endSecondSpin_, 1);
    endRowLayout->addWidget(setEndButton, 0);
    rangeLayout->addWidget(endRow, 0);

    // Width budget for the modal transport's time label below (the embedded
    // panel hides that strip); no longer derived from fixed control widths.
    const int rangeControlWidth =
        kRangeLabelWidth + kSetButtonLeftGap + 110 + kSetButtonLeftGap + kRangeSetButtonWidth;

    // Top-aligned flow: the range controls, then a trailing stretch. (Earlier
    // the controls were pushed to the window bottom by a stretch, which
    // overflowed the pane on shorter dialogs — clipping the End row.)
    rangePageLayout->addWidget(rangeContent_, 0);
    rangePageLayout->addStretch(1);
    outputPageLayout->addStretch(1);

    // Persistent time bar — lives OUTSIDE the tab widget so the scrubber and
    // transport stay reachable on every settings page. Added to rootLayout
    // after the tabs, below.
    previewStrip_ = new QFrame(this);
    QFrame* previewStrip = previewStrip_;
    previewStrip->setObjectName(QStringLiteral("VideoExportPrimaryPanel"));
    auto* previewStripLayout = new QVBoxLayout(previewStrip);
    previewStripLayout->setContentsMargins(10, 8, 10, 8);
    previewStripLayout->setSpacing(2);

    previewCursorSecond_ = qBound(0.0, currentPreviewSecond(), totalDurationSeconds_);
    previewSlider_ = new QSlider(Qt::Horizontal, this);
    previewSlider_->setRange(0, secondToSliderValue(totalDurationSeconds_));
    previewSlider_->setValue(secondToSliderValue(previewCursorSecond_));
    previewSlider_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    previewSlider_->setStyleSheet(UiTheme::formSliderStyleSheet());
    auto* previewControlsRow = new QWidget(previewStrip);
    auto* previewControlsLayout = new QHBoxLayout(previewControlsRow);
    previewControlsLayout->setContentsMargins(kSectionContentLeftInset, 2, kSectionContentLeftInset, 0);
    previewControlsLayout->setSpacing(kPreviewControlSpacing);

    previewTimeLabel_ = new QLabel(previewStrip);
    previewTimeLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    previewTimeLabel_->setFixedWidth(rangeControlWidth);
    auto* previewTimeRow = new QWidget(previewStrip);
    auto* previewTimeLayout = new QHBoxLayout(previewTimeRow);
    const int previewControlsWidth =
        (kPreviewControlButtonWidth * 2)
        + kPreviewControlSpacing
        + kPreviewControlsToSliderGap;
    previewTimeLayout->setContentsMargins(kSectionContentLeftInset + previewControlsWidth, 0, kSectionContentLeftInset, 2);
    previewTimeLayout->setSpacing(0);
    previewTimeLayout->addWidget(previewTimeLabel_, 0, Qt::AlignLeft);
    previewTimeLayout->addStretch(1);

    stopPreviewButton_ = new QToolButton(previewControlsRow);
    stopPreviewButton_->setObjectName(QStringLiteral("RangePreviewControlButton"));
    stopPreviewButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stopPreviewButton_->setIconSize(QSize(16, 16));
    stopPreviewButton_->setFixedSize(QSize(kPreviewControlButtonWidth, kPreviewControlButtonHeight));
    stopPreviewButton_->setIcon(makePreviewStopIcon(UiTheme::colors().iconPrimary));
    stopPreviewButton_->setToolTip(uiText("dialog.video_export.preview.stop", QStringLiteral("Stop")));
    stopPreviewButton_->setAutoRaise(false);
    stopPreviewButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
    previewRangeButton_ = new QToolButton(previewControlsRow);
    previewRangeButton_->setObjectName(QStringLiteral("RangePreviewControlButton"));
    previewRangeButton_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    previewRangeButton_->setIconSize(QSize(16, 16));
    previewRangeButton_->setFixedSize(QSize(kPreviewControlButtonWidth, kPreviewControlButtonHeight));
    previewRangeButton_->setAutoRaise(false);
    previewRangeButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
    stopPreviewButton_->setEnabled(false);
    previewControlsLayout->addWidget(stopPreviewButton_, 0);
    previewControlsLayout->addWidget(previewRangeButton_, 0);
    previewControlsLayout->addSpacing(kPreviewControlsToSliderGap);
    previewControlsLayout->addWidget(previewSlider_, 1);
    previewStripLayout->addWidget(previewControlsRow, 0);
    previewStripLayout->addWidget(previewTimeRow, 0);
    rootLayout->addWidget(previewStrip, 0);

    // optionsContent_ holds the Visuals-page controls; it is added to the
    // Visuals tab below. The former HUD overlay toggles + HUD font picker are
    // appended onto the same Visuals tab further down. "Add intro" is built but
    // kept hidden.
    optionsContent_ = new QWidget(visualsPage);
    auto* optionsLayout = new QGridLayout(optionsContent_);
    optionsLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    optionsLayout->setHorizontalSpacing(10);
    optionsLayout->setVerticalSpacing(8);
    optionsLayout->setColumnStretch(0, 1);
    optionsLayout->setColumnStretch(1, 1);
    showTimestampCheck_ = new QCheckBox(
        l10n(QStringLiteral("Show bottom-left timestamp"), QStringLiteral("鏄剧ず宸︿笅瑙掓椂闂存埑")),
        optionsContent_
    );
    showTimestampCheck_->setChecked(baseTask_.showTimestamp);
    showTimestampCheck_->setText(uiText("dialog.video_export.option.show_timestamp", QStringLiteral("Show bottom-left timestamp")));
    showObjectStatsCheck_ = new QCheckBox(
        uiText("dialog.video_export.option.show_object_stats", QStringLiteral("Show object stats")),
        optionsContent_
    );
    showObjectStatsCheck_->setChecked(baseTask_.showObjectStatsHud);
    showChartInfoCheck_ = new QCheckBox(
        uiText("dialog.video_export.option.show_chart_info", QStringLiteral("Show chart info")),
        optionsContent_
    );
    showChartInfoCheck_->setChecked(baseTask_.showChartInfoHud);
    clockCountCheck_ = new QCheckBox(
        l10n(QStringLiteral("Enable clock_count (%1)"), QStringLiteral("启用 clock_count (%1)"))
            .arg(baseTask_.clockCount),
        optionsContent_
    );
    // Opt-in count-in: OFF by default. When checked, applyUiToTask bakes the
    // chart's clock_count beats (the value shown in the label, carried in
    // baseTask_) into the export; unchecked exports with no clock.wav ticks.
    clockCountCheck_->setChecked(false);
    addIntroCheck_ = new QCheckBox(
        l10n(QStringLiteral("Add intro"), QStringLiteral("添加片头")),
        optionsContent_
    );
    addIntroCheck_->setChecked(baseTask_.intro.enabled);
    smoothBrightnessCheck_ = new QCheckBox(
        l10n(QStringLiteral("Smooth brightness"), QStringLiteral("骞虫粦浜害")),
        optionsContent_
    );
    smoothBrightnessCheck_->setChecked(baseTask_.smoothBrightness);
    smoothBrightnessCheck_->setText(uiText("dialog.video_export.option.smooth_brightness", QStringLiteral("Smooth brightness")));
    const auto addPercentSliderOption = [](
        QWidget* parent,
        const QString& title,
        int minimum,
        int maximum,
        int step,
        int valuePercent,
        QSlider** sliderOut,
        QLabel** valueOut
    ) {
        auto* container = new QWidget(parent);
        auto* containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(3);
        auto* header = new QWidget(container);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(6);
        auto* titleLabel = new QLabel(title, header);
        auto* valueLabel = new miacode::ui::EditableValueLabel(QStringLiteral("%1%").arg(valuePercent), header);
        valueLabel->setMinimumWidth(40);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        headerLayout->addWidget(titleLabel, 1);
        headerLayout->addWidget(valueLabel, 0);
        auto* slider = new QSlider(Qt::Horizontal, container);
        slider->setRange(minimum, maximum);
        slider->setSingleStep(step);
        slider->setPageStep(step);
        slider->setTickInterval(step);
        slider->setValue(valuePercent);
        slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        // Fit the styled handle (groove 6px + -4px margins = 14px) without
        // clipping, kept compact so the page stays short.
        slider->setFixedHeight(20);
        valueLabel->bindSlider(slider);
        containerLayout->addWidget(header, 0);
        containerLayout->addWidget(slider, 0);
        *sliderOut = slider;
        *valueOut = valueLabel;
        return container;
    };
    const auto setSliderOptionTitle = [](QWidget* option, const QString& title) {
        if (option == nullptr) {
            return;
        }
        auto* optionLayout = qobject_cast<QVBoxLayout*>(option->layout());
        if (optionLayout == nullptr || optionLayout->count() <= 0) {
            return;
        }
        QWidget* header = optionLayout->itemAt(0)->widget();
        if (header == nullptr) {
            return;
        }
        auto* headerLayout = qobject_cast<QHBoxLayout*>(header->layout());
        if (headerLayout == nullptr || headerLayout->count() <= 0) {
            return;
        }
        auto* titleLabel = qobject_cast<QLabel*>(headerLayout->itemAt(0)->widget());
        if (titleLabel != nullptr) {
            titleLabel->setText(title);
        }
    };
    QWidget* outerBrightnessOption = addPercentSliderOption(
        optionsContent_,
        uiText("dialog.video_export.option.brightness_outer", QStringLiteral("Brightness (Outer)")),
        0,
        100,
        1,
        qRound(qBound(0.0, baseTask_.backgroundBrightnessOuter, 1.0) * 100.0),
        &brightnessOuterSlider_,
        &brightnessOuterValueLabel_
    );
    QWidget* innerBrightnessOption = addPercentSliderOption(
        optionsContent_,
        uiText("dialog.video_export.option.brightness_inner", QStringLiteral("Brightness (Inner)")),
        0,
        100,
        1,
        qRound(qBound(0.0, baseTask_.backgroundBrightnessInner, 1.0) * 100.0),
        &brightnessInnerSlider_,
        &brightnessInnerValueLabel_
    );
    setSliderOptionTitle(outerBrightnessOption, uiText("dialog.video_export.option.brightness_outer", QStringLiteral("Brightness (Outer)")));
    setSliderOptionTitle(innerBrightnessOption, uiText("dialog.video_export.option.brightness_inner", QStringLiteral("Brightness (Inner)")));
    optionsLayout->addWidget(outerBrightnessOption, 1, 0, 1, 1);
    optionsLayout->addWidget(innerBrightnessOption, 1, 1, 1, 1);
    QWidget* layoutSquareScaleOption = addPercentSliderOption(
        optionsContent_,
        l10n(QStringLiteral("Layout Size"), QStringLiteral("Layout鏁村浘澶у皬")),
        qRound(miacode::preview_video::kLayoutSquareScaleMin * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleMax * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0),
        qRound(miacode::preview_video::normalizedLayoutSquareScale(baseTask_.layoutSquareScale) * 100.0),
        &layoutSquareScaleSlider_,
        &layoutSquareScaleValueLabel_
    );
    setSliderOptionTitle(layoutSquareScaleOption, uiText("dialog.video_export.option.layout_size", QStringLiteral("Stage Display Scale")));
    optionsLayout->addWidget(layoutSquareScaleOption, 2, 0, 1, 2);
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    const auto snapFlowSpeed = [flowSpeedMin, flowSpeedMax, flowSpeedStep](double flowSpeed) {
        return qBound(
            flowSpeedMin,
            flowSpeedMin + qRound((flowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
            flowSpeedMax
        );
    };
    selectedTapFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(baseTask_.tapFlowSpeed);
    selectedTouchFlowSpeed_ = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(baseTask_.touchFlowSpeed);
    selectedTapFlowSpeed_ = snapFlowSpeed(selectedTapFlowSpeed_);
    selectedTouchFlowSpeed_ = snapFlowSpeed(selectedTouchFlowSpeed_);
    // Tap/Touch flow speed are no longer shown in the export dialog (they are
    // tuned in the standalone 视频设置 dialog). selectedTap/TouchFlowSpeed_ stay
    // initialised from baseTask_ above so applyUiToTask still bakes the current
    // value; the Gameplay tab below carries only the injected owner-wired
    // controls (skin / judge line / judge effect / slide stack / center).
    gameplayPageLayout_->addStretch(1);
    const QString scaleFillLabel = uiText("dialog.video_export.option.scale.fill", QStringLiteral("Fill (crop if needed)"));
    const QString scaleFitLabel = uiText("dialog.video_export.option.scale.fit", QStringLiteral("Fit (keep full image, may letterbox)"));
    const QString scaleSquareFitLabel = uiText(
        "dialog.video_export.option.scale.square_fit",
        QStringLiteral("1:1 Fit (center square)"));
    selectedBackgroundScaleMode_ = baseTask_.backgroundScaleMode;
    backgroundScaleModeButton_ = createDialogMenuButton(
        optionsContent_,
        exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_)
    );
    backgroundScaleModeMenu_ = new QMenu(backgroundScaleModeButton_);
    UiTheme::styleRoundedMenu(*backgroundScaleModeMenu_);
    addDialogMenuChoice(backgroundScaleModeMenu_, scaleFillLabel, [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FillCrop;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
        }
        if (previewScaleModeCallback_) {
            previewScaleModeCallback_(selectedBackgroundScaleMode_);
        }
    });
    addDialogMenuChoice(backgroundScaleModeMenu_, scaleFitLabel, [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::FitContain;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
        }
        if (previewScaleModeCallback_) {
            previewScaleModeCallback_(selectedBackgroundScaleMode_);
        }
    });
    addDialogMenuChoice(backgroundScaleModeMenu_, scaleSquareFitLabel, [this]() {
        selectedBackgroundScaleMode_ = PreviewBackgroundScaleMode::SquareFitContain;
        if (backgroundScaleModeButton_ != nullptr) {
            backgroundScaleModeButton_->setText(exportDialogBackgroundScaleModeLabel(selectedBackgroundScaleMode_));
        }
        if (previewScaleModeCallback_) {
            previewScaleModeCallback_(selectedBackgroundScaleMode_);
        }
    });
    backgroundScaleModeButton_->setMenu(backgroundScaleModeMenu_);
    auto* backgroundScaleModeRow = new QWidget(optionsContent_);
    auto* backgroundScaleModeLayout = new QHBoxLayout(backgroundScaleModeRow);
    backgroundScaleModeLayout->setContentsMargins(0, 0, 0, 0);
    backgroundScaleModeLayout->setSpacing(6);
    auto* backgroundScaleModeLabel = new QLabel(
        uiText("dialog.video_export.option.scale_mode", QStringLiteral("Background / PV Scale Mode")),
        backgroundScaleModeRow
    );
    backgroundScaleModeLayout->addWidget(backgroundScaleModeLabel, 0);
    backgroundScaleModeLayout->addWidget(backgroundScaleModeButton_, 1);
    optionsLayout->addWidget(backgroundScaleModeRow, 3, 0, 1, 2);
    visualsPageLayout->addWidget(optionsContent_, 0);

    // Former HUD page — overlay toggles — merged onto the Video tab below the
    // picture controls, in an aligned 2-column grid so the checkboxes line up
    // (the HUD font picker lives on its own "Font" tab):
    //   平滑亮度       | 显示左下角时间戳
    //   显示物量统计    | Show chart info
    auto* hudToggles = new QWidget(visualsPage);
    auto* hudTogglesLayout = new QGridLayout(hudToggles);
    hudTogglesLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    hudTogglesLayout->setHorizontalSpacing(16);
    hudTogglesLayout->setVerticalSpacing(6);
    hudTogglesLayout->setColumnStretch(0, 1);
    hudTogglesLayout->setColumnStretch(1, 1);
    hudTogglesLayout->addWidget(smoothBrightnessCheck_, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
    hudTogglesLayout->addWidget(showTimestampCheck_, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);
    hudTogglesLayout->addWidget(showObjectStatsCheck_, 1, 0, Qt::AlignLeft | Qt::AlignVCenter);
    hudTogglesLayout->addWidget(showChartInfoCheck_, 1, 1, Qt::AlignLeft | Qt::AlignVCenter);
    hudTogglesLayout->addWidget(clockCountCheck_, 2, 0, 1, 2, Qt::AlignLeft | Qt::AlignVCenter);

    // ("添加片头" moved to its own "片头" tab, built below.)
    visualsPageLayout->addWidget(hudToggles, 0);
    visualsPageLayout->addStretch(1);

    // Font tab — the HUD font picker on its own page. (The cover export moved to
    // the toolbar Export dropdown, 2026-06-10.)
    auto* fontPage = new QWidget(settingsTabs_);
    auto* fontPageLayout = new QVBoxLayout(fontPage);
    fontPageLayout->setContentsMargins(kSectionContentLeftInset, 6, kSectionContentLeftInset, 6);
    fontPageLayout->setSpacing(8);
    auto* fontPageLabel = new QLabel(
        uiText("dialog.video_export.option.hud_font", l10n(QStringLiteral("HUD font"), QStringLiteral("HUD 字体"))),
        fontPage
    );
    hudFontSettingsButton_ = new QPushButton(
        uiText("dialog.video_export.option.hud_font_settings", QStringLiteral("Font Settings")),
        fontPage
    );
    hudFontSettingsButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    fontPageLayout->addWidget(fontPageLabel, 0, Qt::AlignLeft);
    fontPageLayout->addWidget(hudFontSettingsButton_, 0, Qt::AlignLeft);

    fontPageLayout->addStretch(1);

    // ---- Intro tab ("片头") — pre-roll settings + read-only live preview ----
    // Mirrors the cover dialog's 背景/难度卡 controls, minus size (follows the
    // export resolution) and minus 文字超长 (the ANIMATED intro always marquee-
    // scrolls long text; shrink/ellipsis are still-cover-only modes).
    auto* introPage = new QWidget(settingsTabs_);
    auto* introPageLayout = new QHBoxLayout(introPage);
    introPageLayout->setContentsMargins(kSectionContentLeftInset, 6, kSectionContentLeftInset, 6);
    introPageLayout->setSpacing(14);

    // Left: the live IntroOverlay preview, pinned at the card-hold frame and
    // sized to the export aspect ratio. Display-only (no drag / no layout IO).
    auto* introPreviewColumn = new QWidget(introPage);
    auto* introPreviewLayout = new QVBoxLayout(introPreviewColumn);
    introPreviewLayout->setContentsMargins(0, 0, 0, 0);
    introPreviewLayout->setSpacing(4);
    introPreview_ = new IntroPreviewWidget(introPreviewColumn);
    introPreviewLayout->addWidget(introPreview_, 0, Qt::AlignTop | Qt::AlignHCenter);
    introPreviewLayout->addStretch(1);
    introPageLayout->addWidget(introPreviewColumn, 0, Qt::AlignTop);

    // Right: controls column.
    auto* introControls = new QWidget(introPage);
    auto* introControlsLayout = new QVBoxLayout(introControls);
    introControlsLayout->setContentsMargins(0, 0, 0, 0);
    introControlsLayout->setSpacing(6);

    // "添加片头" — THE master switch of this tab; bold + one size up so it
    // reads as such (moved from the Video tab).
    {
        QFont introCheckFont = addIntroCheck_->font();
        introCheckFont.setBold(true);
        introCheckFont.setPointSizeF(introCheckFont.pointSizeF() + 1.0);
        addIntroCheck_->setFont(introCheckFont);
    }
    introControlsLayout->addWidget(addIntroCheck_, 0, Qt::AlignLeft);   // reparents to introControls

    // 背景 group — backdrop source + blur (size follows the export resolution).
    auto* introBgGroup = new QGroupBox(
        l10n(QStringLiteral("Background"), QStringLiteral("背景")), introControls);
    auto* introBgForm = new QFormLayout(introBgGroup);
    introBgForm->setSpacing(8);
    introBgForm->setLabelAlignment(Qt::AlignLeft);
    introBgForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    introBackgroundCombo_ = new QComboBox(introBgGroup);
    introBackgroundCombo_->addItem(
        l10n(QStringLiteral("Chart jacket (曲绘)"), QStringLiteral("曲绘")), QStringLiteral("jacket"));
    introBackgroundCombo_->addItem(
        l10n(QStringLiteral("Custom image"), QStringLiteral("自定义图片")), QStringLiteral("custom"));
    introBgForm->addRow(l10n(QStringLiteral("Background"), QStringLiteral("背景")), introBackgroundCombo_);
    auto* introBgPathRow = new QWidget(introBgGroup);
    auto* introBgPathLayout = new QHBoxLayout(introBgPathRow);
    introBgPathLayout->setContentsMargins(0, 0, 0, 0);
    introBgPathLayout->setSpacing(8);
    introBackgroundPathEdit_ = new QLineEdit(introBgPathRow);
    introBackgroundPathEdit_->setPlaceholderText(
        l10n(QStringLiteral("Custom background image path"), QStringLiteral("自定义背景图片路径")));
    introBackgroundBrowse_ = new QPushButton(
        l10n(QStringLiteral("Browse…"), QStringLiteral("浏览…")), introBgPathRow);
    introBackgroundBrowse_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
    introBgPathLayout->addWidget(introBackgroundPathEdit_, 1);
    introBgPathLayout->addWidget(introBackgroundBrowse_, 0);
    introBgForm->addRow(QString(), introBgPathRow);
    introBlurCheck_ = new QCheckBox(
        l10n(QStringLiteral("Blur background"), QStringLiteral("背景虚化")), introBgGroup);
    introBlurCheck_->setChecked(baseTask_.intro.blurBackground);
    introBgForm->addRow(QString(), introBlurCheck_);
    introControlsLayout->addWidget(introBgGroup, 0);

    // 难度卡 group — DX/SD type + shadow + level-text render. (No card toggle:
    // an intro always carries the difficulty card.)
    auto* introCardGroup = new QGroupBox(
        l10n(QStringLiteral("Difficulty card"), QStringLiteral("难度卡")), introControls);
    auto* introCardForm = new QFormLayout(introCardGroup);
    introCardForm->setSpacing(8);
    introCardForm->setLabelAlignment(Qt::AlignLeft);
    introCardForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    // DX / SD chart type. "Standard" is the QML-side mode value: the card shows
    // the スタンダード plate top-right and mirrors the tab shoulder under it.
    introCardModeCombo_ = new QComboBox(introCardGroup);
    introCardModeCombo_->addItem(QStringLiteral("DX"), QStringLiteral("DX"));
    introCardModeCombo_->addItem(QStringLiteral("SD"), QStringLiteral("Standard"));
    {
        const int modeIdx = introCardModeCombo_->findData(baseTask_.intro.mode);
        if (modeIdx >= 0) {
            introCardModeCombo_->setCurrentIndex(modeIdx);
        }
    }
    introCardForm->addRow(
        l10n(QStringLiteral("Chart type"), QStringLiteral("谱面类型")), introCardModeCombo_);
    introCardShadowCheck_ = new QCheckBox(
        l10n(QStringLiteral("Card drop shadow"), QStringLiteral("难度卡阴影")), introCardGroup);
    introCardShadowCheck_->setChecked(baseTask_.intro.cardShadow);
    introCardForm->addRow(QString(), introCardShadowCheck_);
    introLevelTextCheck_ = new QCheckBox(
        l10n(QStringLiteral("Render level as text"), QStringLiteral("等级文本渲染")), introCardGroup);
    introLevelTextCheck_->setChecked(baseTask_.intro.lvRenderMode == QStringLiteral("text"));
    // Opt past the app-wide tooltip suppression (MainWindow installs a global
    // event filter that hides tooltips outside the preview area).
    introLevelTextCheck_->setProperty("miacodeAllowTooltip", true);
    introLevelTextCheck_->setToolTip(l10n(
        QStringLiteral("The baked LV sprites only cover digits 0-9 and \"+\". Tick this to render "
                       "the level as text when it needs any other character."),
        QStringLiteral("原生材质仅支持等级为数字0~9与“+”；如果等级需要其他字符，请勾选这个选项。")));
    introCardForm->addRow(QString(), introLevelTextCheck_);
    introControlsLayout->addWidget(introCardGroup, 0);
    introControlsLayout->addStretch(1);
    introPageLayout->addWidget(introControls, 1);

    // Wiring: every control re-gates sub-controls, refreshes the read-only
    // preview, and persists (app-level preferences, like add_intro).
    const auto introUiChanged = [this]() {
        syncIntroControlsEnabled();
        refreshIntroPreview();
        persistExportOnlySettings();
        // Embedded mode: the host refreshes the negative-time intro region (its
        // slider range + the overlay) to match 添加片头 / the 片头 settings.
        emit introPreviewSettingsChanged();
    };
    connect(addIntroCheck_, &QCheckBox::toggled, this, introUiChanged);
    connect(introBackgroundCombo_, &QComboBox::currentIndexChanged, this, introUiChanged);
    connect(introBlurCheck_, &QCheckBox::toggled, this, introUiChanged);
    connect(introCardModeCombo_, &QComboBox::currentIndexChanged, this, introUiChanged);
    connect(introCardShadowCheck_, &QCheckBox::toggled, this, introUiChanged);
    connect(introLevelTextCheck_, &QCheckBox::toggled, this, introUiChanged);
    // The path edit refreshes the live preview per keystroke but only hits the
    // preference file when editing finishes.
    connect(introBackgroundPathEdit_, &QLineEdit::textChanged, this, [this]() { refreshIntroPreview(); });
    connect(introBackgroundPathEdit_, &QLineEdit::editingFinished, this, [this]() { persistExportOnlySettings(); });
    connect(introBackgroundBrowse_, &QPushButton::clicked, this, [this]() { browseIntroBackground(); });

    refreshAddIntroEnabledState();

    settingsTabs_->addTab(
        outputPage,
        uiText("dialog.video_export.section.output", l10n(QStringLiteral("Output"), QStringLiteral("输出")))
    );
    settingsTabs_->addTab(
        visualsPage,
        uiText("dialog.render_settings.video_group", l10n(QStringLiteral("Video"), QStringLiteral("视频")))
    );
    settingsTabs_->addTab(
        gameplayPage_,
        uiText("dialog.render_settings.gameplay_group", l10n(QStringLiteral("Gameplay"), QStringLiteral("游戏")))
    );
    settingsTabs_->addTab(
        introPage,
        uiText("dialog.video_export.section.intro", l10n(QStringLiteral("Intro"), QStringLiteral("片头")))
    );
    settingsTabs_->addTab(
        fontPage,
        uiText("dialog.video_export.section.font", l10n(QStringLiteral("Font"), QStringLiteral("字体")))
    );
    settingsTabs_->addTab(
        rangePage,
        uiText("dialog.video_export.section.range", l10n(QStringLiteral("Export Range"), QStringLiteral("导出区间")))
    );

    buttonBox_ = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QDialogButtonBox* buttonBox = buttonBox_;
    exportButton_ = buttonBox->addButton(uiText("dialog.video_export.button.export", QStringLiteral("Export")), QDialogButtonBox::AcceptRole);
    exportButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    exportButton_->setMinimumWidth(kDialogActionButtonMinWidth);
    cancelButton_ = buttonBox->button(QDialogButtonBox::Cancel);
    if (cancelButton_ != nullptr) {
        cancelButton_->setText(uiText("dialog.video_export.button.cancel", QStringLiteral("Cancel")));
        cancelButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        cancelButton_->setMinimumWidth(kDialogActionButtonMinWidth);
    }
    connect(exportButton_, &QPushButton::clicked, this, &VideoExportDialog::onExportButtonClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttonBox);

    previewTimer_ = new QTimer(this);
    previewTimer_->setInterval(kPreviewTickIntervalMs);
    connect(previewTimer_, &QTimer::timeout, this, &VideoExportDialog::onRangePreviewTick);
    previewHeldSeekTimer_ = new QTimer(this);
    previewHeldSeekTimer_->setTimerType(Qt::PreciseTimer);
    previewHeldSeekTimer_->setInterval(miacode::preview_interaction::kSeekHoldTickIntervalMs);
    connect(previewHeldSeekTimer_, &QTimer::timeout, this, &VideoExportDialog::applyPreviewHeldSeekTick);

    connect(startSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(endSecondSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &VideoExportDialog::onRangeSpinChanged);
    connect(previewSlider_, &QSlider::valueChanged, this, &VideoExportDialog::onPreviewSliderChanged);
    connect(previewSlider_, &QSlider::sliderPressed, this, [this]() {
        stopPreviewHeldSeek();
        previewScrubRenderElapsed_.invalidate();
    });
    connect(previewSlider_, &QSlider::sliderReleased, this, [this]() {
        stopPreviewHeldSeek();
        previewScrubRenderElapsed_.invalidate();
        seekPreview(previewCursorSecond_);
        syncRangeUi();
    });
    connect(setStartButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeStartFromPreview);
    connect(setEndButton, &QPushButton::clicked, this, &VideoExportDialog::setRangeEndFromPreview);
    connect(previewRangeButton_, &QToolButton::clicked, this, &VideoExportDialog::toggleRangePreview);
    connect(stopPreviewButton_, &QToolButton::clicked, this, &VideoExportDialog::stopRangePreviewToStart);
    connect(showTimestampCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        syncLivePreviewTimestampVisibility();
        initialShowTimestamp_ = checked;
    });
    connect(showObjectStatsCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        syncLivePreviewObjectStatsVisibility();
        initialShowObjectStats_ = checked;
    });
    connect(showChartInfoCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        syncLivePreviewChartInfoVisibility();
        initialShowChartInfo_ = checked;
    });
    connect(hudFontSettingsButton_, &QPushButton::clicked, this, &VideoExportDialog::openHudFontSettingsDialog);
    connect(smoothBrightnessCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (previewSmoothBrightnessCallback_) {
            previewSmoothBrightnessCallback_(checked);
        }
    });
    connect(brightnessOuterSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (brightnessOuterValueLabel_ != nullptr) {
            brightnessOuterValueLabel_->setText(QStringLiteral("%1%").arg(value));
        }
        if (previewBrightnessCallback_) {
            const double outer = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
            const double inner = brightnessInnerSlider_ != nullptr
                ? qBound(0.0, static_cast<double>(brightnessInnerSlider_->value()) / 100.0, 1.0)
                : baseTask_.backgroundBrightnessInner;
            previewBrightnessCallback_(outer, inner);
        }
    });
    connect(layoutSquareScaleSlider_, &QSlider::valueChanged, this, [this](int value) {
        const double scale = miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(value) / 100.0);
        if (layoutSquareScaleValueLabel_ != nullptr) {
            layoutSquareScaleValueLabel_->setText(QStringLiteral("%1%").arg(qRound(scale * 100.0)));
        }
        if (previewLayoutScaleCallback_) {
            previewLayoutScaleCallback_(scale);
        }
    });
    connect(brightnessInnerSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (brightnessInnerValueLabel_ != nullptr) {
            brightnessInnerValueLabel_->setText(QStringLiteral("%1%").arg(value));
        }
        if (previewBrightnessCallback_) {
            const double outer = brightnessOuterSlider_ != nullptr
                ? qBound(0.0, static_cast<double>(brightnessOuterSlider_->value()) / 100.0, 1.0)
                : baseTask_.backgroundBrightnessOuter;
            const double inner = qBound(0.0, static_cast<double>(value) / 100.0, 1.0);
            previewBrightnessCallback_(outer, inner);
        }
    });
    previewSlider_->setFocusPolicy(Qt::StrongFocus);
    previewSlider_->installEventFilter(this);

    loadPersistedSettings();
    initialResolutionAspectRatio_ = selectedResolutionAspectRatio();
    syncRangeUi();
    updatePreviewPlayPauseUi();
    refreshDialogGeometry();
    if (QWidget* owner = parentWidget(); owner != nullptr && !embeddedPanelMode_) {
        move(desiredDialogTopLeft(owner, size()));
    }
    QTimer::singleShot(0, this, [this]() {
        if (outputPathEdit_ != nullptr) {
            outputPathEdit_->clearFocus();
        }
        if (settingsTabs_ != nullptr) {
            settingsTabs_->setFocus(Qt::OtherFocusReason);
        }
        refreshDialogGeometry();
        if (QWidget* owner = parentWidget(); owner != nullptr && !embeddedPanelMode_) {
            move(desiredDialogTopLeft(owner, size()));
        }
    });
}

void VideoExportDialog::setEmbeddedPanelMode(bool embedded)
{
    if (embeddedPanelMode_ == embedded) {
        return;
    }
    embeddedPanelMode_ = embedded;
    if (!embedded) {
        return;
    }
    setModal(false);
    // CRITICAL: QDialog carries Qt::Dialog (a Qt::Window flag) from its ctor,
    // and QLayout::addWidget only strips window flags when it has to
    // REPARENT — when the panel was constructed with the host as parent
    // already, the flag survives and the "embedded" panel pops up as a
    // top-level window while its layout slot stays behind as a phantom
    // (the 2026-06-11 导出页错位+弹窗 bug). Clear it explicitly.
    setWindowFlags(Qt::Widget);
    // Surface unification: the ctor applied exportDialogStyleSheet(), whose
    // `QDialog { background: windowAltBg }` rule keeps matching this widget by
    // class even after it is reparented into the page — so the whole video
    // sub-page painted a windowAltBg rectangle against the page's windowBg (the
    // "整块面板偏色" color seam). Re-paint THIS instance with the page background
    // via a higher-specificity ID selector so the embedded panel reads as one
    // continuous surface with the export page header (the modal dialog, which
    // keeps its own objectName-less stylesheet, is unaffected).
    setObjectName(QStringLiteral("EmbeddedVideoExportPanel"));
    setStyleSheet(styleSheet()
        + QStringLiteral("QDialog#EmbeddedVideoExportPanel { background: %1; }")
              .arg(UiTheme::colors().windowBg.name(QColor::HexRgb)));
    // The ctor's refreshDialogGeometry() already locked the dialog height —
    // clear the locks so the host layout owns sizing from here on. The
    // 560px dialog minimum width is also dropped: the quick-shell workspace
    // surface can be ~700 logical px total, so the embedded panel must be
    // allowed to compress to its real layout minimum instead of forcing a
    // horizontal scrollbar onto the page.
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    setMinimumWidth(0);
    // Vertical Expanding: the host page hands the panel ALL remaining height
    // (fixed header above, nothing below) and the tab area absorbs it.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    if (cancelButton_ != nullptr) {
        cancelButton_->hide();
    }
    if (exportButton_ != nullptr) {
        exportButton_->setText(uiText(
            "dialog.video_export.button.start_export",
            l10n(QStringLiteral("Start Export"), QStringLiteral("开始导出"))));
    }

    // ---- Fixed-frame page layout (2026-06-12 redesign) ----
    // The page never scrolls as a whole: the tab bar and the Start-Export
    // footer stay put; only a tab's own content scrolls (vertically) when the
    // viewport is genuinely too short. Horizontal scrolling is forbidden —
    // content must compress into the available width.

    // The in-panel transport strip is gone: the preview-area transport on the
    // right is the single seek/play surface; the range tab mirrors its clock.
    if (previewStrip_ != nullptr) {
        previewStrip_->hide();
    }

    // The 片头 live preview is sacrificed in the embedded page (product
    // decision 2026-06-12): it was the tallest tab content by far, and the
    // page must fit the viewport without scrolling at default window sizes.
    if (introPreview_ != nullptr) {
        QWidget* previewColumn = introPreview_->parentWidget();
        introPreview_ = nullptr;
        delete previewColumn;
    }

    // Re-host every tab page inside a vertical-only scroll viewport. The
    // ctor's modal-path refreshDialogGeometry() floored each page to the
    // tallest page — undo that first; pages now keep natural height and the
    // scroll area is only a too-short-window fallback.
    if (settingsTabs_ != nullptr) {
        const int currentIndex = settingsTabs_->currentIndex();
        QStringList tabLabels;
        QList<QWidget*> tabPages;
        while (settingsTabs_->count() > 0) {
            tabLabels.append(settingsTabs_->tabText(0));
            tabPages.append(settingsTabs_->widget(0));
            settingsTabs_->removeTab(0);
        }
        for (int i = 0; i < tabPages.size(); ++i) {
            QWidget* page = tabPages.at(i);
            page->setMinimumHeight(0);
            page->setAutoFillBackground(false);
            auto* pageScroll = new QScrollArea(settingsTabs_);
            pageScroll->setObjectName(QStringLiteral("EmbeddedExportTabScroll"));
            pageScroll->setWidgetResizable(true);
            pageScroll->setFrameShape(QFrame::NoFrame);
            pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            pageScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            pageScroll->viewport()->setAutoFillBackground(false);
            pageScroll->setWidget(page);
            settingsTabs_->addTab(pageScroll, tabLabels.at(i));
        }
        settingsTabs_->setCurrentIndex(qMax(0, currentIndex));
        settingsTabs_->setStyleSheet(UiTheme::embeddedExportTabStyleSheet());
    }

    if (auto* root = qobject_cast<QVBoxLayout*>(layout()); root != nullptr) {
        if (settingsTabs_ != nullptr) {
            root->setStretchFactor(settingsTabs_, 1);
        }
        // Hairline above the pinned footer. Plain styled QWidget — QFrame::HLine
        // is a known-rejected divider (its content rect collapses under QSS).
        if (buttonBox_ != nullptr) {
            auto* footerRule = new QWidget(this);
            footerRule->setObjectName(QStringLiteral("EmbeddedExportFooterRule"));
            footerRule->setAttribute(Qt::WA_StyledBackground, true);
            footerRule->setFixedHeight(1);
            footerRule->setStyleSheet(QStringLiteral("background: %1;")
                .arg(UiTheme::colors().border.name(QColor::HexRgb)));
            root->insertWidget(root->indexOf(buttonBox_), footerRule);
        }
    }

    // With no in-panel transport the tick timer doubles as the clock mirror
    // for the range tab's current-time readout — keep it running for the
    // panel's whole life (see onRangePreviewTick's embedded branch).
    if (previewTimer_ != nullptr) {
        previewTimer_->start();
    }

    refreshDialogGeometry();
}

void VideoExportDialog::setEmbeddedExportRunning(bool running)
{
    embeddedExportRunning_ = running;
    if (!embeddedPanelMode_ || exportButton_ == nullptr) {
        return;
    }
    exportButton_->setText(running
        ? uiText("dialog.video_export.button.cancel_export",
                 l10n(QStringLiteral("Cancel Export"), QStringLiteral("取消导出")))
        : uiText("dialog.video_export.button.start_export",
                 l10n(QStringLiteral("Start Export"), QStringLiteral("开始导出"))));
}

void VideoExportDialog::finalizeEmbeddedSession()
{
    stopRangePreview(false);
    restoreLivePreviewState();
}

void VideoExportDialog::applyThemeStyles()
{
    // Mirror every baked stylesheet/icon site in buildUi(). The dialog's own
    // sheet re-themes most children by QSS type-selector cascade; the rest set
    // their own literal stylesheet (or an accent-/icon-tinted QIcon) and must be
    // re-applied by hand.
    // In embedded mode setEmbeddedPanelMode() appended a higher-specificity
    // `QDialog#EmbeddedVideoExportPanel { background: windowBg }` override so the
    // panel reads as one continuous surface with the export-page header. A bare
    // setStyleSheet(exportDialogStyleSheet()) would DROP that override and leave
    // the panel painting the baked startup-theme color — re-append it here.
    QString sheet = UiTheme::exportDialogStyleSheet();
    if (embeddedPanelMode_) {
        sheet += QStringLiteral("QDialog#EmbeddedVideoExportPanel { background: %1; }")
                     .arg(UiTheme::colors().windowBg.name(QColor::HexRgb));
    }
    setStyleSheet(sheet);

    // Dropdown menu buttons (createDialogMenuButton).
    for (QToolButton* button : {resolutionButton_, fpsButton_, audioBitrateButton_,
                                presetButton_, backgroundScaleModeButton_}) {
        if (button != nullptr) {
            button->setStyleSheet(UiTheme::dialogMenuButtonStyleSheet());
        }
    }

    // Plain push buttons.
    for (QPushButton* button : {outputBrowseButton_, setStartButton_, setEndButton_,
                                hudFontSettingsButton_, introBackgroundBrowse_, cancelButton_}) {
        if (button != nullptr) {
            button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet());
        }
    }
    if (exportButton_ != nullptr) {
        exportButton_->setStyleSheet(UiTheme::dialogPushButtonStyleSheet(true));
    }

    if (rangeTrack_ != nullptr) {
        rangeTrack_->update();
    }

    // Sliders: the scrubber uses the form style; the visuals sliders the dialog
    // style.
    if (previewSlider_ != nullptr) {
        previewSlider_->setStyleSheet(UiTheme::formSliderStyleSheet());
    }
    for (QSlider* slider : {brightnessOuterSlider_, brightnessInnerSlider_, layoutSquareScaleSlider_}) {
        if (slider != nullptr) {
            slider->setStyleSheet(UiTheme::dialogSliderStyleSheet());
        }
    }

    // Transport controls: stylesheet + accent/icon-tinted QIcon.
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
        stopPreviewButton_->setIcon(makePreviewStopIcon(UiTheme::colors().iconPrimary));
    }
    // Re-renders previewRangeButton_'s play/pause icon + stylesheet for the
    // current state and theme.
    updatePreviewPlayPauseUi();

    // Embedded panel chrome: flat-underline tab strip + the footer hairline,
    // both of which bake colors at construction.
    if (embeddedPanelMode_) {
        if (settingsTabs_ != nullptr) {
            settingsTabs_->setStyleSheet(UiTheme::embeddedExportTabStyleSheet());
        }
        if (auto* footerRule = findChild<QWidget*>(QStringLiteral("EmbeddedExportFooterRule"));
            footerRule != nullptr) {
            footerRule->setStyleSheet(
                QStringLiteral("background: %1;").arg(UiTheme::colors().border.name(QColor::HexRgb)));
        }
    }
}

void VideoExportDialog::injectOwnerWiredSettings(QWidget* videoExtras, QWidget* gameplayWidget)
{
    // The injected widgets are built by MainWindow (they need owner-side data
    // + wiring the decoupled dialog can't reach). Drop them in just before each
    // page's trailing stretch so they hug the existing controls.
    if (videoExtras != nullptr && visualsPageLayout_ != nullptr) {
        const int insertIndex = qMax(0, visualsPageLayout_->count() - 1);
        visualsPageLayout_->insertWidget(insertIndex, videoExtras, 0, Qt::AlignTop);
    }
    if (gameplayWidget != nullptr && gameplayPageLayout_ != nullptr) {
        const int insertIndex = qMax(0, gameplayPageLayout_->count() - 1);
        gameplayPageLayout_->insertWidget(insertIndex, gameplayWidget, 0, Qt::AlignTop);
    }
    refreshDialogGeometry();
}

void VideoExportDialog::refreshDialogGeometry()
{
    // Embedded panel mode: the host page owns the outer geometry — the tab
    // area stretches to fill it and each page lives in its own vertical-only
    // scroll viewport (see setEmbeddedPanelMode). No equal-height flooring,
    // no window-only height lock / resize below.
    if (embeddedPanelMode_) {
        updateGeometry();
        return;
    }

    // Clear any prior height lock first so the content is measured freely.
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);

    // Size the tab content area to the TALLEST tab, not the current one, so no
    // tab's bottom rows get clipped. Measure each page's natural content height
    // (reset its floor + activate its layout first), take the max, then floor
    // every page to it (each page ends with a stretch, so shorter tabs just gain
    // trailing slack — nothing overlaps).
    int maxPageHeight = 0;
    if (settingsTabs_ != nullptr) {
        settingsTabs_->setMinimumHeight(0);
        for (int i = 0; i < settingsTabs_->count(); ++i) {
            QWidget* page = settingsTabs_->widget(i);
            if (page == nullptr) {
                continue;
            }
            page->setMinimumHeight(0);
            if (QLayout* pageLayout = page->layout()) {
                pageLayout->invalidate();
                pageLayout->activate();
            }
            maxPageHeight = qMax(maxPageHeight, page->sizeHint().height());
        }
        for (int i = 0; i < settingsTabs_->count(); ++i) {
            if (QWidget* page = settingsTabs_->widget(i)) {
                page->setMinimumHeight(maxPageHeight);
            }
        }
        settingsTabs_->updateGeometry();
    }

    if (QLayout* l = layout()) {
        l->invalidate();
        l->activate();
    }
    adjustSize();
    // Flush any deferred LayoutRequest on the tab widget so its internal stacked
    // pane geometry is current before we read it for the chrome calculation
    // below (otherwise the first call would measure a stale/zero pane height).
    QCoreApplication::sendPostedEvents(settingsTabs_, QEvent::LayoutRequest);
    const int targetWidth = qMax(minimumWidth(), width());
    int targetHeight = sizeHint().height();

    // sizeHint() under-reports here: QStyleSheetStyle does NOT fold the styled
    // QTabWidget::pane padding (the 14px export override = 28px vertical) into
    // the tab widget's sizeHint, so a sizeHint-locked dialog leaves the tallest
    // tab clipped by that padding. Once the dialog is realized, the LIVE
    // geometry of the internal stacked pane tells us the true chrome, so size
    // from that instead: make the pane exactly tall enough for the tallest page.
    //   targetHeight = maxPage + tabChrome + nonTabHeight
    // where tabChrome = settingsTabs - stack, nonTabHeight = dialog - settingsTabs.
    // Both terms are invariant under our own resize, so this is idempotent (a
    // second pass computes the same height rather than collapsing back).
    if (settingsTabs_ != nullptr && maxPageHeight > 0) {
        if (QStackedWidget* stack = settingsTabs_->findChild<QStackedWidget*>()) {
            if (stack->height() > 0 && settingsTabs_->height() > 0) {
                const int tabChrome = qMax(0, settingsTabs_->height() - stack->height());
                const int nonTabHeight = qMax(0, height() - settingsTabs_->height());
                targetHeight = qMax(targetHeight, maxPageHeight + tabChrome + nonTabHeight);
            }
        }
    }

    setMinimumHeight(targetHeight);
    setMaximumHeight(targetHeight);
    resize(targetWidth, targetHeight);
}

void VideoExportDialog::syncLivePreviewTimestampVisibility()
{
    if (showTimestampCheck_ == nullptr || !previewTimestampCallback_) {
        return;
    }
    previewTimestampCallback_(showTimestampCheck_->isChecked());
}

void VideoExportDialog::syncLivePreviewObjectStatsVisibility()
{
    if (showObjectStatsCheck_ == nullptr || !previewObjectStatsCallback_) {
        return;
    }
    previewObjectStatsCallback_(showObjectStatsCheck_->isChecked());
}

void VideoExportDialog::syncLivePreviewChartInfoVisibility()
{
    if (showChartInfoCheck_ == nullptr || !previewChartInfoCallback_) {
        return;
    }
    previewChartInfoCallback_(showChartInfoCheck_->isChecked());
}

void VideoExportDialog::restoreLivePreviewState()
{
    if (previewStateRestored_) {
        return;
    }
    previewStateRestored_ = true;
    if (previewTimestampCallback_) {
        previewTimestampCallback_(initialShowTimestamp_);
    }
    if (previewObjectStatsCallback_) {
        previewObjectStatsCallback_(initialShowObjectStats_);
    }
    if (previewChartInfoCallback_) {
        previewChartInfoCallback_(initialShowChartInfo_);
    }
    if (previewAspectRatioCallback_) {
        previewAspectRatioCallback_(1.0);
    }
}

void VideoExportDialog::openHudFontSettingsDialog()
{
    // Shared dialog (tools/video_export/HudFontSettings.cpp). The callback
    // re-renders this dialog's paused preview frame so the new HUD font shows
    // immediately.
    miacode::video_export::openHudFontSettingsDialog(this, [this]() {
        refreshLivePreviewHudFont();
    });
}

void VideoExportDialog::refreshLivePreviewHudFont()
{
    const double second = currentPreviewSecond();
    if (qIsFinite(second)) {
        seekPreview(second);
    } else {
        seekPreview(previewCursorSecond_);
    }
}

void VideoExportDialog::loadPersistedSettings()
{
    const QJsonObject settings = miacode::video_export::loadDialogPreferences();

    const int savedWidth = settings.value(QStringLiteral("resolution_width")).toInt(selectedResolution_.width());
    const int savedHeight = settings.value(QStringLiteral("resolution_height")).toInt(selectedResolution_.height());
    if (savedWidth > 0 && savedHeight > 0) {
        selectedResolution_ = QSize(savedWidth, savedHeight);
        if (resolutionButton_ != nullptr) {
            resolutionButton_->setText(exportDialogResolutionLabel(selectedResolution_));
        }
        applySelectedAspectRatioToPreview(false);
    }

    const int savedFps = settings.value(QStringLiteral("fps")).toInt(selectedFps_);
    selectedFps_ = savedFps >= 90 ? 120 : 60;
    if (fpsButton_ != nullptr) {
        fpsButton_->setText(QStringLiteral("%1 FPS").arg(selectedFps_));
    }

    const int savedAudioBitrate = settings.value(QStringLiteral("audio_bitrate_kbps"))
                                         .toInt(selectedAudioBitrateKbps_);
    selectedAudioBitrateKbps_ = normaliseAudioBitrateKbps(savedAudioBitrate);
    if (audioBitrateButton_ != nullptr) {
        audioBitrateButton_->setText(QStringLiteral("%1 kbps").arg(selectedAudioBitrateKbps_));
    }

    selectedPreset_ = videoExportPresetFromStoredValue(
        settings.value(QStringLiteral("preset")),
        selectedPreset_
    );
    if (presetButton_ != nullptr) {
        presetButton_->setText(exportDialogPresetLabel(selectedPreset_));
    }

    // App-level "add intro" preference (persists across sessions).
    if (addIntroCheck_ != nullptr) {
        const QSignalBlocker blocker(addIntroCheck_);
        addIntroCheck_->setChecked(
            settings.value(QStringLiteral("add_intro")).toBool(addIntroCheck_->isChecked()));
    }

    // "片头" tab styling (app-level preferences, like add_intro).
    if (introBackgroundCombo_ != nullptr) {
        const QSignalBlocker blocker(introBackgroundCombo_);
        const int idx = introBackgroundCombo_->findData(
            settings.value(QStringLiteral("intro_background_mode"))
                .toString(introBackgroundCombo_->currentData().toString()));
        if (idx >= 0) {
            introBackgroundCombo_->setCurrentIndex(idx);
        }
    }
    if (introBackgroundPathEdit_ != nullptr) {
        const QSignalBlocker blocker(introBackgroundPathEdit_);
        introBackgroundPathEdit_->setText(
            settings.value(QStringLiteral("intro_background_custom_path"))
                .toString(introBackgroundPathEdit_->text()));
    }
    if (introBlurCheck_ != nullptr) {
        const QSignalBlocker blocker(introBlurCheck_);
        introBlurCheck_->setChecked(
            settings.value(QStringLiteral("intro_background_blur")).toBool(introBlurCheck_->isChecked()));
    }
    if (introCardModeCombo_ != nullptr) {
        const QSignalBlocker blocker(introCardModeCombo_);
        const int idx = introCardModeCombo_->findData(
            settings.value(QStringLiteral("intro_card_type"))
                .toString(introCardModeCombo_->currentData().toString()));
        if (idx >= 0) {
            introCardModeCombo_->setCurrentIndex(idx);
        }
    }
    if (introCardShadowCheck_ != nullptr) {
        const QSignalBlocker blocker(introCardShadowCheck_);
        introCardShadowCheck_->setChecked(
            settings.value(QStringLiteral("intro_card_shadow")).toBool(introCardShadowCheck_->isChecked()));
    }
    if (introLevelTextCheck_ != nullptr) {
        const QSignalBlocker blocker(introLevelTextCheck_);
        introLevelTextCheck_->setChecked(
            settings.value(QStringLiteral("intro_level_text_render")).toBool(introLevelTextCheck_->isChecked()));
    }
    resizeIntroPreviewToAspect();
    syncIntroControlsEnabled();
    refreshIntroPreview();
}

void VideoExportDialog::savePersistedSettings(const VideoExportTask& task) const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    settings.insert(QStringLiteral("resolution_width"), task.outputWidth);
    settings.insert(QStringLiteral("resolution_height"), task.outputHeight);
    settings.insert(QStringLiteral("fps"), task.fps);
    settings.insert(QStringLiteral("audio_bitrate_kbps"), task.audioBitrateKbps);
    settings.insert(QStringLiteral("preset"), videoExportPresetToken(task.preset));
    appendIntroPersistedSettings(&settings);
    miacode::video_export::saveDialogPreferences(settings);
}

void VideoExportDialog::persistExportOnlySettings() const
{
    QJsonObject settings = miacode::video_export::loadDialogPreferences();
    settings.insert(QStringLiteral("resolution_width"), selectedResolution().width());
    settings.insert(QStringLiteral("resolution_height"), selectedResolution().height());
    settings.insert(QStringLiteral("fps"), selectedFps_);
    settings.insert(QStringLiteral("audio_bitrate_kbps"), selectedAudioBitrateKbps_);
    settings.insert(QStringLiteral("preset"), videoExportPresetToken(selectedPreset_));
    appendIntroPersistedSettings(&settings);
    miacode::video_export::saveDialogPreferences(settings);
}

void VideoExportDialog::appendIntroPersistedSettings(QJsonObject* settings) const
{
    if (settings == nullptr) {
        return;
    }
    settings->insert(QStringLiteral("add_intro"),
                     addIntroCheck_ != nullptr && addIntroCheck_->isChecked());
    if (introBackgroundCombo_ != nullptr) {
        settings->insert(QStringLiteral("intro_background_mode"),
                         introBackgroundCombo_->currentData().toString());
    }
    if (introBackgroundPathEdit_ != nullptr) {
        settings->insert(QStringLiteral("intro_background_custom_path"),
                         introBackgroundPathEdit_->text().trimmed());
    }
    if (introBlurCheck_ != nullptr) {
        settings->insert(QStringLiteral("intro_background_blur"), introBlurCheck_->isChecked());
    }
    if (introCardModeCombo_ != nullptr) {
        settings->insert(QStringLiteral("intro_card_type"),
                         introCardModeCombo_->currentData().toString());
    }
    if (introCardShadowCheck_ != nullptr) {
        settings->insert(QStringLiteral("intro_card_shadow"), introCardShadowCheck_->isChecked());
    }
    if (introLevelTextCheck_ != nullptr) {
        settings->insert(QStringLiteral("intro_level_text_render"), introLevelTextCheck_->isChecked());
    }
}

bool VideoExportDialog::stepPreviewSliderBySeconds(double deltaSeconds)
{
    if (previewSlider_ == nullptr || !qIsFinite(deltaSeconds)) {
        return false;
    }
    const int deltaMs = qRound(deltaSeconds * 1000.0);
    if (deltaMs == 0) {
        return false;
    }
    const int value = qBound(
        previewSlider_->minimum(),
        previewSlider_->value() + deltaMs,
        previewSlider_->maximum()
    );
    if (value == previewSlider_->value()) {
        return true;
    }
    previewSlider_->setValue(value);
    return true;
}

bool VideoExportDialog::handlePreviewSliderWheel(QWheelEvent* event)
{
    if (previewSlider_ == nullptr || event == nullptr) {
        return false;
    }
    int delta = event->angleDelta().y();
    if (delta == 0) {
        delta = event->angleDelta().x();
    }
    if (delta == 0) {
        delta = event->pixelDelta().y();
    }
    if (delta == 0) {
        delta = event->pixelDelta().x();
    }
    if (delta == 0) {
        return false;
    }
    const int steps = delta > 0 ? qMax(1, qRound(static_cast<double>(delta) / 120.0))
                                : qMin(-1, qRound(static_cast<double>(delta) / 120.0));
    previewSlider_->setFocus(Qt::MouseFocusReason);
    const bool handled = stepPreviewSliderBySeconds(
        static_cast<double>(steps) * miacode::preview_interaction::kSeekSingleStepSeconds
    );
    if (handled) {
        event->accept();
    }
    return handled;
}

void VideoExportDialog::beginPreviewHeldSeek(int direction, int key)
{
    if (direction == 0 || previewSlider_ == nullptr) {
        return;
    }
    previewHeldSeekDirection_ = direction > 0 ? 1 : -1;
    previewSeekHeldArrowKey_ = key;
    previewSeekHeldArrowLastElapsedMs_ = 0;
    previewSeekHeldArrowElapsed_.restart();
    if (previewHeldSeekTimer_ != nullptr && !previewHeldSeekTimer_->isActive()) {
        previewHeldSeekTimer_->start();
    }
}

void VideoExportDialog::stopPreviewHeldSeek(int key)
{
    if (key != 0 && previewSeekHeldArrowKey_ != key) {
        return;
    }
    previewHeldSeekDirection_ = 0;
    previewSeekHeldArrowKey_ = 0;
    previewSeekHeldArrowLastElapsedMs_ = 0;
    previewSeekHeldArrowElapsed_.invalidate();
    if (previewHeldSeekTimer_ != nullptr) {
        previewHeldSeekTimer_->stop();
    }
}

void VideoExportDialog::applyPreviewHeldSeekTick()
{
    if (previewHeldSeekDirection_ == 0
        || previewSeekHeldArrowKey_ == 0
        || !previewSeekHeldArrowElapsed_.isValid()) {
        return;
    }
    const int elapsedMs = static_cast<int>(previewSeekHeldArrowElapsed_.elapsed());
    const int deltaMs = previewSeekHeldArrowLastElapsedMs_ > 0
        ? (elapsedMs - previewSeekHeldArrowLastElapsedMs_)
        : miacode::preview_interaction::kSeekHoldTickIntervalMs;
    previewSeekHeldArrowLastElapsedMs_ = elapsedMs;
    const double heldSeconds = static_cast<double>(elapsedMs) / 1000.0;
    stepPreviewSliderBySeconds(
        static_cast<double>(previewHeldSeekDirection_)
            * miacode::preview_interaction::heldSeekStepSecondsForDeltaMs(deltaMs, heldSeconds)
    );
}

void VideoExportDialog::applySelectedAspectRatioToPreview(bool markChanged)
{
    const double aspectRatio = selectedResolutionAspectRatio();
    if (markChanged && qAbs(aspectRatio - initialResolutionAspectRatio_) > 0.0001) {
        previewAspectChangedByDialog_ = true;
    }
    if (previewAspectRatioCallback_) {
        previewAspectRatioCallback_(aspectRatio);
    }
    // The intro tab's read-only preview tracks the export aspect too.
    resizeIntroPreviewToAspect();
}

void VideoExportDialog::browseOutputPath()
{
    const QString baseDirectory = exportBaseDirectory(baseTask_);
    const QString initial = outputPathEdit_ != nullptr
        ? resolveOutputPathForExport(outputPathEdit_->text(), baseDirectory)
        : QString();
    const QString selected = QFileDialog::getSaveFileName(
        this,
        l10n(QStringLiteral("Export Video"), QStringLiteral("瀵煎嚭瑙嗛")),
        initial,
        QStringLiteral("MP4 Video (*.mp4)")
    );
    if (selected.isEmpty() || outputPathEdit_ == nullptr) {
        return;
    }
    outputPathEdit_->setText(displayOutputPathForDialog(selected, baseDirectory));
}

bool VideoExportDialog::applyUiToTask(VideoExportTask* task, QString* errorMessage) const
{
    if (task == nullptr) {
        return false;
    }
    VideoExportTask updated = baseTask_;
    const QString baseDirectory = exportBaseDirectory(updated);
    const QString outputPath = outputPathEdit_ != nullptr ? outputPathEdit_->text().trimmed() : QString();
    if (outputPath.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Please choose an output path."), QStringLiteral("请先选择输出路径。"));
        }
        return false;
    }
    updated.outputPath = resolveOutputPathForExport(outputPath, baseDirectory);
    const QSize selectedSize = selectedResolution();
    updated.outputWidth = selectedSize.width() > 0 ? selectedSize.width() : updated.outputWidth;
    updated.outputHeight = selectedSize.height() > 0 ? selectedSize.height() : updated.outputHeight;
    updated.fps = qMax(1, selectedFps_);
    updated.audioBitrateKbps = normaliseAudioBitrateKbps(selectedAudioBitrateKbps_);
    updated.preset = selectedPreset_;
    updated.showTimestamp = showTimestampCheck_ != nullptr ? showTimestampCheck_->isChecked() : true;
    updated.showObjectStatsHud = showObjectStatsCheck_ != nullptr ? showObjectStatsCheck_->isChecked() : false;
    updated.showChartInfoHud = showChartInfoCheck_ != nullptr ? showChartInfoCheck_->isChecked() : false;
    // clock_count count-in is opt-in: off → 0 (no ticks), on → the document's
    // clock_count value carried in baseTask_.
    updated.clockCount = (clockCountCheck_ != nullptr && clockCountCheck_->isChecked())
        ? baseTask_.clockCount
        : 0;
    updated.backgroundBrightnessOuter = brightnessOuterSlider_ != nullptr
        ? qBound(0.0, static_cast<double>(brightnessOuterSlider_->value()) / 100.0, 1.0)
        : updated.backgroundBrightnessOuter;
    updated.backgroundBrightnessInner = brightnessInnerSlider_ != nullptr
        ? qBound(0.0, static_cast<double>(brightnessInnerSlider_->value()) / 100.0, 1.0)
        : updated.backgroundBrightnessInner;
    updated.layoutSquareScale = layoutSquareScaleSlider_ != nullptr
        ? miacode::preview_video::normalizedLayoutSquareScale(static_cast<double>(layoutSquareScaleSlider_->value()) / 100.0)
        : updated.layoutSquareScale;
    updated.smoothBrightness = smoothBrightnessCheck_ != nullptr
        ? smoothBrightnessCheck_->isChecked()
        : updated.smoothBrightness;
    updated.backgroundScaleMode = selectedBackgroundScaleMode_;
    double exportTapFlowSpeed = selectedTapFlowSpeed_;
    if (tapFlowSpeedEdit_ != nullptr) {
        bool ok = false;
        const double typedSpeed = tapFlowSpeedEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            exportTapFlowSpeed = typedSpeed;
        }
    }
    double exportTouchFlowSpeed = selectedTouchFlowSpeed_;
    if (touchFlowSpeedEdit_ != nullptr) {
        bool ok = false;
        const double typedSpeed = touchFlowSpeedEdit_->text().trimmed().toDouble(&ok);
        if (ok) {
            exportTouchFlowSpeed = typedSpeed;
        }
    }
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    updated.tapFlowSpeed = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((exportTapFlowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    updated.touchFlowSpeed = qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((exportTouchFlowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
    const double selectedRangeStart = rangeStartSeconds();
    const double selectedRangeEnd = rangeEndSeconds();
    updated.exportStartSeconds = selectedRangeStart;
    updated.contentDurationSeconds = qMax(0.0, selectedRangeEnd - updated.exportStartSeconds);
    // A range that STARTS at chart 0 behaves exactly like a full export
    // regardless of where it ends: the deliverable begins with the chart's
    // real opening (2 s count-down lead-in, BGM from source 0, clock count),
    // so the partial-range frozen preload + pause glyph would be wrong
    // there. Only a non-zero start needs the freeze-then-go pre-roll.
    // The epsilon tolerates spinbox / floating-point slop (the dialog
    // displays at 1 ms precision).
    constexpr double kFullRangeEpsilonSeconds = 0.01;
    updated.fullRangeExport = selectedRangeStart <= kFullRangeEpsilonSeconds;
    // The maimai intro is a full-range-only pre-roll; clips starting
    // mid-chart never get it regardless of the checkbox. (A clip starting
    // at chart 0 counts as full-range, so it may carry the intro.)
    // "片头" tab styling (background + difficulty card) — shared with the
    // read-only preview via currentIntroSpec, so preview == export. `enabled`
    // is recomputed below from the checkbox + range.
    updated.intro = currentIntroSpec();
    updated.intro.enabled =
        (addIntroCheck_ != nullptr && addIntroCheck_->isChecked())
        && updated.fullRangeExport;
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Export,
        QStringLiteral("dialog_range_decision"),
        QStringLiteral("rangeStart=%1 rangeEnd=%2 totalDuration=%3 fullRangeExport=%4 epsilon=%5")
            .arg(selectedRangeStart, 0, 'f', 9)
            .arg(selectedRangeEnd, 0, 'f', 9)
            .arg(totalDurationSeconds_, 0, 'f', 9)
            .arg(updated.fullRangeExport ? 1 : 0)
            .arg(kFullRangeEpsilonSeconds, 0, 'f', 6));

    const QFileInfo outputInfo(updated.outputPath);
    const QDir outputDir = outputInfo.absoluteDir();
    if (!outputDir.exists()) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Output directory does not exist."), QStringLiteral("输出目录不存在。"));
        }
        return false;
    }
    if (updated.outputWidth <= 0 || updated.outputHeight <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Resolution is invalid."), QStringLiteral("分辨率无效。"));
        }
        return false;
    }
    if (updated.contentDurationSeconds <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Export range is empty."), QStringLiteral("导出区间为空。"));
        }
        return false;
    }
    if (updated.exportStartSeconds < 0.0 || updated.exportStartSeconds > totalDurationSeconds_ + 1e-6) {
        if (errorMessage != nullptr) {
            *errorMessage = l10n(QStringLiteral("Export start is out of range."), QStringLiteral("导出起始时间超出范围。"));
        }
        return false;
    }
    *task = updated;
    return true;
}

QSize VideoExportDialog::selectedResolution() const
{
    if (selectedResolution_.width() <= 0 || selectedResolution_.height() <= 0) {
        return QSize(qMax(1, baseTask_.outputWidth), qMax(1, baseTask_.outputHeight));
    }
    return selectedResolution_;
}

double VideoExportDialog::selectedResolutionAspectRatio() const
{
    const QSize size = selectedResolution();
    if (size.width() <= 0 || size.height() <= 0) {
        return 1.0;
    }
    return static_cast<double>(size.width()) / static_cast<double>(size.height());
}

void VideoExportDialog::onRangeSpinChanged()
{
    if (syncingRangeUi_) {
        return;
    }
    const double start = rangeStartSeconds();
    const double end = rangeEndSeconds();
    if (end <= start && totalDurationSeconds_ > 0.0) {
        const bool fromStart = sender() == startSecondSpin_;
        syncingRangeUi_ = true;
        if (fromStart && endSecondSpin_ != nullptr) {
            endSecondSpin_->setValue(qMin(totalDurationSeconds_, start + 0.001));
        } else if (startSecondSpin_ != nullptr) {
            startSecondSpin_->setValue(qMax(0.0, end - 0.001));
        }
        syncingRangeUi_ = false;
    }
    syncRangeUi();
    refreshAddIntroEnabledState();
}

void VideoExportDialog::refreshAddIntroEnabledState()
{
    if (addIntroCheck_ == nullptr) {
        return;
    }
    // MUST mirror applyUiToTask's bake gate: any range STARTING at chart 0
    // counts as full-range and may carry the intro (see the comment there).
    // The old extra "end reaches the chart tail" condition froze a checked-
    // but-disabled box for [0, partial] clips whose state was STILL baked
    // into the export — misleading and uncontrollable.
    constexpr double kFullRangeEpsilonSeconds = 0.01;
    addIntroCheck_->setEnabled(rangeStartSeconds() <= kFullRangeEpsilonSeconds);
    syncIntroControlsEnabled();
}

void VideoExportDialog::syncIntroControlsEnabled()
{
    // Everything on the "片头" tab follows 添加片头 (which itself is greyed on a
    // partial range); the path row additionally follows the 背景 combo.
    const bool introOn = addIntroCheck_ != nullptr
        && addIntroCheck_->isEnabled() && addIntroCheck_->isChecked();
    const bool customBg = introBackgroundCombo_ != nullptr
        && introBackgroundCombo_->currentData().toString() == QStringLiteral("custom");
    if (introBackgroundCombo_ != nullptr) introBackgroundCombo_->setEnabled(introOn);
    if (introBackgroundPathEdit_ != nullptr) introBackgroundPathEdit_->setEnabled(introOn && customBg);
    if (introBackgroundBrowse_ != nullptr) introBackgroundBrowse_->setEnabled(introOn && customBg);
    if (introBlurCheck_ != nullptr) introBlurCheck_->setEnabled(introOn);
    if (introCardModeCombo_ != nullptr) introCardModeCombo_->setEnabled(introOn);
    if (introCardShadowCheck_ != nullptr) introCardShadowCheck_->setEnabled(introOn);
    if (introLevelTextCheck_ != nullptr) introLevelTextCheck_->setEnabled(introOn);
}

void VideoExportDialog::browseIntroBackground()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        l10n(QStringLiteral("Choose background image"), QStringLiteral("选择背景图片")),
        QString(),
        l10n(QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"),
             QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp *.webp)")));
    if (file.isEmpty()) {
        return;
    }
    if (introBackgroundCombo_ != nullptr) {
        const int customIndex = introBackgroundCombo_->findData(QStringLiteral("custom"));
        if (customIndex >= 0) {
            introBackgroundCombo_->setCurrentIndex(customIndex);   // fires introUiChanged
        }
    }
    if (introBackgroundPathEdit_ != nullptr) {
        introBackgroundPathEdit_->setText(file);
    }
    persistExportOnlySettings();
}

bool VideoExportDialog::isAddIntroActiveForPreview() const
{
    const bool checked = addIntroCheck_ != nullptr && addIntroCheck_->isChecked();
    // Full-range gate — mirror refreshAddIntroEnabledState's RANGE logic directly
    // rather than addIntroCheck_->isEnabled(). isEnabled() reads the widget's
    // *effective* enabled state, which the embedded tab-rehost can flip to false
    // even while 添加片头 stays checked; that was collapsing the intro region /
    // negative slider while the overlay lingered.
    constexpr double kFullRangeEpsilonSeconds = 0.01;
    const bool fullRange = rangeStartSeconds() <= kFullRangeEpsilonSeconds;
    return checked && fullRange;
}

IntroBannerSpec VideoExportDialog::currentIntroSpec() const
{
    IntroBannerSpec spec = baseTask_.intro;   // chart payload: title/level/曲绘…
    if (introCardModeCombo_ != nullptr) {
        spec.mode = introCardModeCombo_->currentData().toString();
    }
    if (introLevelTextCheck_ != nullptr) {
        spec.lvRenderMode = introLevelTextCheck_->isChecked()
            ? QStringLiteral("text")
            : QStringLiteral("atlas");
    }
    if (introBackgroundCombo_ != nullptr) {
        spec.backgroundMode = introBackgroundCombo_->currentData().toString();
    }
    if (introBackgroundPathEdit_ != nullptr) {
        spec.customBackgroundPath = introBackgroundPathEdit_->text().trimmed();
    }
    if (introBlurCheck_ != nullptr) {
        spec.blurBackground = introBlurCheck_->isChecked();
    }
    if (introCardShadowCheck_ != nullptr) {
        spec.cardShadow = introCardShadowCheck_->isChecked();
    }
    return spec;
}

void VideoExportDialog::refreshIntroPreview()
{
    if (introPreview_ != nullptr) {
        introPreview_->applySpec(currentIntroSpec());
    }
}

void VideoExportDialog::resizeIntroPreviewToAspect()
{
    // The preview widget is a FIXED box (the dialog never resizes when the
    // resolution changes); only the letterboxed frame inside it follows the
    // output aspect. Reuses the same ratio source as the main live preview.
    if (introPreview_ != nullptr) {
        introPreview_->setOutputAspectRatio(selectedResolutionAspectRatio());
    }
}

void VideoExportDialog::onPreviewSliderChanged(int sliderValue)
{
    if (syncingRangeUi_) {
        return;
    }
    previewCursorSecond_ = qBound(0.0, sliderValueToSecond(sliderValue), totalDurationSeconds_);
    const bool shouldRenderNow = previewSlider_ == nullptr
        || !previewSlider_->isSliderDown()
        || !previewScrubRenderElapsed_.isValid()
        || previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs;
    if (shouldRenderNow) {
        seekPreview(previewCursorSecond_);
        previewScrubRenderElapsed_.restart();
    }
    syncRangeUi();
}

void VideoExportDialog::setRangeStartFromPreview()
{
    if (startSecondSpin_ == nullptr) {
        return;
    }
    // Embedded panel: read the main preview's clock at click time — the
    // preview-area transport is the only seek surface.
    if (embeddedPanelMode_) {
        previewCursorSecond_ = qBound(0.0, currentPreviewSecond(), totalDurationSeconds_);
    }
    startSecondSpin_->setValue(previewCursorSecond_);
}

void VideoExportDialog::setRangeEndFromPreview()
{
    if (endSecondSpin_ == nullptr) {
        return;
    }
    if (embeddedPanelMode_) {
        previewCursorSecond_ = qBound(0.0, currentPreviewSecond(), totalDurationSeconds_);
    }
    endSecondSpin_->setValue(previewCursorSecond_);
}

void VideoExportDialog::scrubPreviewFromTrack(double second, bool live)
{
    // C2 linkage: a handle drag live-seeks the preview through seekPreviewCallback_
    // (== MainWindow::seekPreviewToSecond, the single seek entry the right-side
    // transport also uses), so the on-screen time bar stays in sync. The gold
    // marker (previewCursorSecond_) always tracks; the decode seek is throttled.
    const double clamped = qBound(0.0, second, totalDurationSeconds_);
    previewCursorSecond_ = clamped;
    if (rangeTrack_ != nullptr) {
        static_cast<ExportRangeTrack*>(rangeTrack_)->setPlayheadSeconds(clamped);
    }
    bool doSeek = !live;
    if (live) {
        if (!previewScrubRenderElapsed_.isValid()
            || previewScrubRenderElapsed_.elapsed() >= kPreviewScrubRenderIntervalMs) {
            doSeek = true;
            previewScrubRenderElapsed_.restart();
        }
    }
    if (doSeek && seekPreviewCallback_) {
        seekPreviewCallback_(clamped);
    }
}

void VideoExportDialog::toggleRangePreview()
{
    if (rangePreviewPlaying_) {
        stopRangePreview(false);
        return;
    }
    playPreview(previewCursorSecond_);
    rangePreviewPlaying_ = true;
    updatePreviewPlayPauseUi();
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(true);
    }
    if (previewTimer_ != nullptr && !previewTimer_->isActive()) {
        previewTimer_->start();
    }
}

void VideoExportDialog::stopRangePreview(bool seekToCurrent)
{
    // Embedded mode keeps the timer alive — it is the range tab's clock
    // mirror, not a play-state ticker.
    if (previewTimer_ != nullptr && !embeddedPanelMode_) {
        previewTimer_->stop();
    }
    if (rangePreviewPlaying_ || isPreviewPlaying()) {
        pausePreview();
        previewCursorSecond_ = qBound(0.0, currentPreviewSecond(), totalDurationSeconds_);
    }
    rangePreviewPlaying_ = false;
    updatePreviewPlayPauseUi();
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(previewCursorSecond_ > 0.0005);
    }
    if (seekToCurrent) {
        seekPreview(previewCursorSecond_);
    }
}

void VideoExportDialog::handlePreviewPlayPauseShortcut()
{
    if (rangePreviewPlaying_ || isPreviewPlaying()) {
        stopRangePreview(false);
        return;
    }
    toggleRangePreview();
}

void VideoExportDialog::handlePreviewStopOrPlayShortcut()
{
    if (rangePreviewPlaying_ || isPreviewPlaying()) {
        stopRangePreviewToStart();
        return;
    }
    toggleRangePreview();
}

void VideoExportDialog::stopRangePreviewToStart()
{
    stopRangePreview(false);
    previewCursorSecond_ = 0.0;
    seekPreview(previewCursorSecond_);
    syncRangeUi();
}

void VideoExportDialog::updatePreviewPlayPauseUi()
{
    if (previewRangeButton_ == nullptr) {
        return;
    }
    const QColor iconColor = UiTheme::colors().iconPrimary;
    if (rangePreviewPlaying_) {
        previewRangeButton_->setIcon(makePreviewPauseIcon(iconColor));
        previewRangeButton_->setToolTip(uiText("dialog.video_export.preview.pause", QStringLiteral("Pause")));
        previewRangeButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet(true));
    } else {
        previewRangeButton_->setIcon(makePreviewPlayIcon(iconColor));
        previewRangeButton_->setToolTip(uiText("dialog.video_export.preview.play", QStringLiteral("Play")));
        previewRangeButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet());
    }
}

void VideoExportDialog::onRangePreviewTick()
{
    // Embedded panel: there is no in-panel transport — the timer runs for the
    // panel's whole life and simply mirrors the main preview's authoritative
    // clock into the range tab's current-time readout (and the cursor that
    // seeds 设为起点/终点). Play/stop/seek all live on the preview-area
    // transport to the right.
    if (embeddedPanelMode_) {
        previewCursorSecond_ = qBound(0.0, currentPreviewSecond(), totalDurationSeconds_);
        if (rangeTrack_ != nullptr) {
            static_cast<ExportRangeTrack*>(rangeTrack_)->setPlayheadSeconds(previewCursorSecond_);
        }
        return;
    }
    if (!rangePreviewPlaying_) {
        return;
    }
    if (!isPreviewPlaying()) {
        stopRangePreview(false);
        return;
    }

    previewCursorSecond_ = qBound(0.0, currentPreviewSecond(), totalDurationSeconds_);
    if (previewCursorSecond_ >= totalDurationSeconds_) {
        previewCursorSecond_ = totalDurationSeconds_;
        stopRangePreview(false);
        seekPreview(previewCursorSecond_);
        syncRangeUi();
        return;
    }
    syncRangeUi();
}

void VideoExportDialog::syncRangeUi()
{
    if (syncingRangeUi_) {
        return;
    }
    syncingRangeUi_ = true;

    const double clampedStart = qBound(0.0, rangeStartSeconds(), totalDurationSeconds_);
    const double clampedEnd = qBound(clampedStart, rangeEndSeconds(), totalDurationSeconds_);
    previewCursorSecond_ = qBound(0.0, previewCursorSecond_, totalDurationSeconds_);

    if (startSecondSpin_ != nullptr) {
        const QSignalBlocker blocker(*startSecondSpin_);
        startSecondSpin_->setValue(clampedStart);
    }
    if (endSecondSpin_ != nullptr) {
        const QSignalBlocker blocker(*endSecondSpin_);
        endSecondSpin_->setValue(clampedEnd);
    }
    if (previewSlider_ != nullptr) {
        const QSignalBlocker blocker(*previewSlider_);
        previewSlider_->setValue(secondToSliderValue(previewCursorSecond_));
    }
    if (previewTimeLabel_ != nullptr) {
        previewTimeLabel_->setText(QStringLiteral("%1 / %2").arg(formatSecond(previewCursorSecond_), formatSecond(totalDurationSeconds_)));
    }
    if (rangeTrack_ != nullptr) {
        auto* track = static_cast<ExportRangeTrack*>(rangeTrack_);
        track->setRange(clampedStart, clampedEnd);
        track->setPlayheadSeconds(previewCursorSecond_);
        track->setIntroBannerShown(isAddIntroActiveForPreview());
    }
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(rangePreviewPlaying_ || previewCursorSecond_ > 0.0005);
    }

    syncingRangeUi_ = false;
}

void VideoExportDialog::seekPreview(double second)
{
    const double clamped = qBound(0.0, second, totalDurationSeconds_);
    previewCursorSecond_ = clamped;
    if (previewSlider_ != nullptr) {
        const QSignalBlocker blocker(*previewSlider_);
        previewSlider_->setValue(secondToSliderValue(clamped));
    }
    if (seekPreviewCallback_) {
        seekPreviewCallback_(clamped);
    }
}

void VideoExportDialog::playPreview(double second)
{
    previewCursorSecond_ = qBound(0.0, second, totalDurationSeconds_);
    if (playPreviewCallback_) {
        playPreviewCallback_(previewCursorSecond_);
        return;
    }
    seekPreview(previewCursorSecond_);
}

void VideoExportDialog::pausePreview()
{
    if (pausePreviewCallback_) {
        pausePreviewCallback_();
    }
}

bool VideoExportDialog::isPreviewPlaying() const
{
    if (isPreviewPlayingCallback_) {
        return isPreviewPlayingCallback_();
    }
    return false;
}

double VideoExportDialog::currentPreviewSecond() const
{
    if (currentPreviewSecondCallback_) {
        return currentPreviewSecondCallback_();
    }
    return previewCursorSecond_;
}

double VideoExportDialog::rangeStartSeconds() const
{
    return startSecondSpin_ != nullptr ? startSecondSpin_->value() : 0.0;
}

double VideoExportDialog::rangeEndSeconds() const
{
    return endSecondSpin_ != nullptr ? endSecondSpin_->value() : totalDurationSeconds_;
}

QString VideoExportDialog::formatSecond(double second) const
{
    const qint64 totalMs = qMax<qint64>(0, qRound64(second * 1000.0));
    const qint64 minutes = totalMs / 60000;
    const qint64 sec = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

void VideoExportDialog::onExportButtonClicked()
{
    // Embedded panel: while a worker run launched from here is active, the
    // export button doubles as the cancel affordance.
    if (embeddedPanelMode_ && embeddedExportRunning_) {
        emit exportCancelRequested();
        return;
    }
    startExport();
}

void VideoExportDialog::startExport()
{
    stopRangePreview(false);

    VideoExportTask task;
    QString errorMessage;
    if (!applyUiToTask(&task, &errorMessage)) {
        UiDialogs::showMessageBox(
            QMessageBox::Warning,
            this,
            uiText("dialog.video_export.title", QStringLiteral("Export Video")),
            errorMessage
        );
        return;
    }
    savePersistedSettings(task);
    requestedExportTask_ = task;
    exportRequested_ = true;
    if (embeddedPanelMode_) {
        // The panel stays open while the host launches the worker (progress
        // shows on the preview-area transport, A3 as amended 2026-06-11).
        emit exportConfirmed();
        return;
    }
    accept();
}

void VideoExportDialog::closeEvent(QCloseEvent* event)
{
    stopRangePreview(false);
    restoreLivePreviewState();
    QDialog::closeEvent(event);
}

void VideoExportDialog::done(int result)
{
    // Embedded panel: there is no window to close — Esc (QDialog's default
    // reject) and programmatic done() must NOT hide the panel or restore the
    // live-preview state mid-session. Teardown happens explicitly via
    // finalizeEmbeddedSession() when the host leaves the video sub-page.
    if (embeddedPanelMode_) {
        return;
    }
    stopRangePreview(false);
    restoreLivePreviewState();
    QDialog::done(result);
}

bool VideoExportDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event != nullptr && UiDialogs::dialogOwnsPreviewShortcutScope(this)) {
        if (event->type() == QEvent::ShortcutOverride) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (matchVideoExportShortcut(keyEvent) != VideoExportShortcutAction::None) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const VideoExportShortcutAction action = matchVideoExportShortcut(keyEvent);
            if (action != VideoExportShortcutAction::None) {
                if (keyEvent->isAutoRepeat()) {
                    event->accept();
                    return true;
                }
                switch (action) {
                case VideoExportShortcutAction::TogglePlayPause:
                    handlePreviewPlayPauseShortcut();
                    break;
                case VideoExportShortcutAction::StopOrPlay:
                    handlePreviewStopOrPlayShortcut();
                    break;
                case VideoExportShortcutAction::None:
                    break;
                }
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (matchVideoExportShortcut(keyEvent) != VideoExportShortcutAction::None) {
                event->accept();
                return true;
            }
        }
    }
    if (previewSlider_ != nullptr && watched == previewSlider_) {
        if (event->type() == QEvent::Wheel) {
            stopPreviewHeldSeek();
            if (handlePreviewSliderWheel(static_cast<QWheelEvent*>(event))) {
                return true;
            }
        } else if (event->type() == QEvent::KeyPress) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            int direction = 0;
            if (keyEvent->key() == Qt::Key_Left) {
                direction = -1;
            } else if (keyEvent->key() == Qt::Key_Right) {
                direction = 1;
            }
            if (direction != 0) {
                if (keyEvent->modifiers() != Qt::NoModifier) {
                    return QDialog::eventFilter(watched, event);
                }
                if (keyEvent->isAutoRepeat()) {
                    return true;
                }
                beginPreviewHeldSeek(direction, keyEvent->key());
                stepPreviewSliderBySeconds(
                    static_cast<double>(direction) * miacode::preview_interaction::kSeekSingleStepSeconds
                );
                return true;
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat()
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right)
                && previewSeekHeldArrowKey_ == keyEvent->key()) {
                stopPreviewHeldSeek(keyEvent->key());
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}
