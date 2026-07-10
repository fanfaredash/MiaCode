#include "VideoExportDialog.h"

#include "BusySpinner.h"
#include "DialogLocalization.h"
#include "EditableValueLabel.h"
#include "UiComponents.h"
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

#include "VideoExportDialogInternal.h"

using namespace miacode::video_export::dialog_detail;

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

// (The HUD-font library helpers + the font-settings dialog moved to
// tools/video_export/HudFontSettings.cpp on 2026-06-10, shared with the main
// window's 视频设置 dialog.)

double snappedFlowSpeed(double flowSpeed)
{
    const double flowSpeedMin = miacode::preview_gameplay::kPreviewTimingFlowSpeedMin;
    const double flowSpeedMax = miacode::preview_gameplay::kPreviewTimingFlowSpeedMax;
    const double flowSpeedStep = miacode::preview_gameplay::kPreviewTimingFlowSpeedStep;
    return qBound(
        flowSpeedMin,
        flowSpeedMin + qRound((flowSpeed - flowSpeedMin) / flowSpeedStep) * flowSpeedStep,
        flowSpeedMax
    );
}

QString flowSpeedValueLabel(double flowSpeed)
{
    const double snapped = snappedFlowSpeed(flowSpeed);
    const double roundedOneDecimal = qRound(snapped * 10.0) / 10.0;
    const bool useSingleDecimal = qAbs(snapped - roundedOneDecimal) < 0.001;
    return QString::number(snapped, 'f', useSingleDecimal ? 1 : 2);
}

int secondToSliderValue(double second)
{
    return qMax(0, qRound(second * kPreviewSliderScale));
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
    SharedSettingsSnapshotCallback sharedSettingsSnapshotCallback,
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
    , sharedSettingsSnapshotCallback_(std::move(sharedSettingsSnapshotCallback))
    , totalDurationSeconds_(qMax(0.0, baseTask.contentDurationSeconds))
{
    setWindowTitle(UiText::text(QStringLiteral("dialog.video_export.title")));
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

    // Gameplay page — Tap/Touch flow-speed rows live here, and owner-wired
    // controls (judge effect / slide stack / center display) are injected
    // post-construction via injectOwnerWiredSettings().
    gameplayPage_ = new QWidget(settingsTabs_);
    gameplayPageLayout_ = new QVBoxLayout(gameplayPage_);
    gameplayPageLayout_->setContentsMargins(4, 6, 4, 6);
    gameplayPageLayout_->setSpacing(8);

    // 皮肤 page — owner-wired skin / judge line / HUD font injected
    // post-construction via injectOwnerWiredSettings().
    skinPage_ = new QWidget(settingsTabs_);
    skinPageLayout_ = new QVBoxLayout(skinPage_);
    skinPageLayout_->setContentsMargins(4, 6, 4, 6);
    skinPageLayout_->setSpacing(8);

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
    auto* outputLabel = new QLabel(UiText::text(QStringLiteral("video_export.output")), outputRow);
    outputColumn->addWidget(outputLabel, 0);
    auto* outputControlRow = new QWidget(outputRow);
    auto* outputControlLayout = new QHBoxLayout(outputControlRow);
    outputControlLayout->setContentsMargins(0, 0, 0, 0);
    outputControlLayout->setSpacing(kFormRowSpacing);
    outputPathEdit_ = new QLineEdit(outputControlRow);
    outputPathEdit_->setText(displayOutputPathForDialog(baseTask_.outputPath, exportBaseDirectory(baseTask_)));
    outputPathEdit_->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet(UiTheme::colors().windowAltBg));
    auto* browseButton = miacode::ui::createDialogAuxiliaryButton(
        outputRow, UiText::text(QStringLiteral("dialog.video_export.browse")));
    outputBrowseButton_ = browseButton;
    connect(browseButton, &QPushButton::clicked, this, &VideoExportDialog::browseOutputPath);
    outputControlLayout->addWidget(outputPathEdit_, 1);
    outputControlLayout->addWidget(browseButton, 0);
    outputColumn->addWidget(outputControlRow, 0);
    outputPageLayout->addWidget(outputRow, 0);
    miacode::ui::busyTick();  // keep the export-switch spinner turning during the build
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
    resolutionCombo_ = miacode::ui::createDialogComboBox(optionsGrid, 12);
    for (const ResolutionPreset& preset : kResolutionPresets) {
        resolutionCombo_->addItem(QString::fromLatin1(preset.label), QSize(preset.width, preset.height));
    }
    resolutionCombo_->setCurrentIndex(currentPresetIndex);
    miacode::ui::applyDialogComboBoxStyle(resolutionCombo_, 12);
    connect(resolutionCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) {
            return;
        }
        selectedResolution_ = resolutionCombo_->itemData(index).toSize();
        applySelectedAspectRatioToPreview(true);
        persistExportOnlySettings();
    });
    addOptionField(0, 0, UiText::text(QStringLiteral("dialog.video_export.resolution")), resolutionCombo_);

    // FPS dropdown (row 0, col 1).
    selectedFps_ = baseTask_.fps >= 90 ? 120 : 60;
    fpsCombo_ = miacode::ui::createDialogComboBox(optionsGrid, 12);
    for (int fps : kFpsOptions) {
        fpsCombo_->addItem(QStringLiteral("%1 FPS").arg(fps), fps);
    }
    fpsCombo_->setCurrentIndex(qMax(0, fpsCombo_->findData(selectedFps_)));
    miacode::ui::applyDialogComboBoxStyle(fpsCombo_, 12);
    connect(fpsCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) {
            return;
        }
        selectedFps_ = fpsCombo_->itemData(index).toInt();
        persistExportOnlySettings();
    });
    addOptionField(0, 1, UiText::text(QStringLiteral("dialog.video_export.fps")), fpsCombo_);

    // Audio quality dropdown (row 1, col 0) — picks AAC bitrate forwarded
    // to ffmpeg as `-b:a <kbps>k`. Default 192 is a step above the previous
    // hard-coded 160k baseline; 320k matches the AAC LC stereo ceiling for
    // users who care about bgm fidelity in the exported clip.
    selectedAudioBitrateKbps_ = normaliseAudioBitrateKbps(baseTask_.audioBitrateKbps);
    const auto formatAudioBitrateLabel = [](int kbps) -> QString {
        return QStringLiteral("%1 kbps").arg(kbps);
    };
    audioBitrateCombo_ = miacode::ui::createDialogComboBox(optionsGrid, 12);
    for (int kbps : kAudioBitrateOptionsKbps) {
        audioBitrateCombo_->addItem(formatAudioBitrateLabel(kbps), kbps);
    }
    audioBitrateCombo_->setCurrentIndex(
        qMax(0, audioBitrateCombo_->findData(selectedAudioBitrateKbps_)));
    miacode::ui::applyDialogComboBoxStyle(audioBitrateCombo_, 12);
    connect(audioBitrateCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) {
            return;
        }
        selectedAudioBitrateKbps_ = audioBitrateCombo_->itemData(index).toInt();
        persistExportOnlySettings();
    });
    addOptionField(
        1,
        0,
        UiText::text(QStringLiteral("dialog.video_export.audio_bitrate")),
        audioBitrateCombo_
    );

    // Export Settings dropdown (row 1, col 1).
    selectedPreset_ = baseTask_.preset;
    presetCombo_ = miacode::ui::createDialogComboBox(optionsGrid, 12);
    presetCombo_->addItem(
        UiText::text(QStringLiteral("dialog.video_export.preset.fast")),
        static_cast<int>(VideoExportPreset::Fast));
    presetCombo_->addItem(
        UiText::text(QStringLiteral("dialog.video_export.preset.high_quality")),
        static_cast<int>(VideoExportPreset::HighQuality));
    presetCombo_->setCurrentIndex(
        qMax(0, presetCombo_->findData(static_cast<int>(selectedPreset_))));
    miacode::ui::applyDialogComboBoxStyle(presetCombo_, 12);
    connect(presetCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index < 0) {
            return;
        }
        selectedPreset_ = static_cast<VideoExportPreset>(presetCombo_->itemData(index).toInt());
        persistExportOnlySettings();
    });
    addOptionField(
        1,
        1,
        UiText::text(QStringLiteral("dialog.video_export.preset")),
        presetCombo_
    );

    outputPageLayout->addWidget(optionsGrid, 0);

    rangeContent_ = new QWidget(rangePage);
    auto* rangeLayout = new QVBoxLayout(rangeContent_);
    rangeLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    rangeLayout->setSpacing(6);

    auto* rangeTitleLabel = new QLabel(
        UiText::text(QStringLiteral("dialog.video_export.section.range")),
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
    track->setIntroBannerText(UiText::text(QStringLiteral("dialog.video_export.range.intro_tag")));
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
    auto* startLabel = new QLabel(UiText::text(QStringLiteral("dialog.video_export.range.start")), startRow);
    startLabel->setFixedWidth(kRangeLabelWidth);
    startLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* setStartButton = miacode::ui::createDialogAuxiliaryButton(
        startRow, UiText::text(QStringLiteral("dialog.video_export.range.set_current")));
    setStartButton_ = setStartButton;
    setStartButton->setToolTip(
        UiText::text(QStringLiteral("dialog.video_export.range.set_current.tip")));
    startRowLayout->addWidget(startLabel, 0);
    startRowLayout->addWidget(startSecondSpin_, 1);
    startRowLayout->addWidget(setStartButton, 0);
    rangeLayout->addWidget(startRow, 0);

    auto* endRow = new QWidget(rangeContent_);
    auto* endRowLayout = new QHBoxLayout(endRow);
    endRowLayout->setContentsMargins(0, 0, 0, 0);
    endRowLayout->setSpacing(kSetButtonLeftGap);
    auto* endLabel = new QLabel(UiText::text(QStringLiteral("dialog.video_export.range.end")), endRow);
    endLabel->setFixedWidth(kRangeLabelWidth);
    endLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto* setEndButton = miacode::ui::createDialogAuxiliaryButton(
        endRow, UiText::text(QStringLiteral("dialog.video_export.range.set_current")));
    setEndButton_ = setEndButton;
    setEndButton->setToolTip(
        UiText::text(QStringLiteral("dialog.video_export.range.set_current.tip")));
    endRowLayout->addWidget(endLabel, 0);
    endRowLayout->addWidget(endSecondSpin_, 1);
    endRowLayout->addWidget(setEndButton, 0);
    rangeLayout->addWidget(endRow, 0);

    // Caption under the rows: a live "当前导出区间：[start, end]" summary in the
    // same MM:SS:mmm format as the spin boxes, small + muted. Kept in sync by
    // syncRangeUi() and re-themed in applyThemeStyles().
    rangeSummaryLabel_ = new QLabel(rangeContent_);
    {
        QFont summaryFont = rangeSummaryLabel_->font();
        summaryFont.setPointSizeF(qMax(7.0, summaryFont.pointSizeF() - 1.0));
        rangeSummaryLabel_->setFont(summaryFont);
    }
    rangeSummaryLabel_->setStyleSheet(
        QStringLiteral("color: %1;").arg(UiTheme::colors().textMuted.name(QColor::HexRgb)));
    rangeLayout->addWidget(rangeSummaryLabel_, 0);
    refreshRangeSummaryLabel();

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
    stopPreviewButton_->setToolTip(UiText::text(QStringLiteral("dialog.video_export.preview.stop")));
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
    miacode::ui::busyTick();

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
        UiText::text(QStringLiteral("video_export.show_bottom_left_timestamp")),
        optionsContent_
    );
    showTimestampCheck_->setChecked(baseTask_.showTimestamp);
    showTimestampCheck_->setText(UiText::text(QStringLiteral("video_export.show_bottom_left_timestamp")));
    showObjectStatsCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("dialog.video_export.option.show_object_stats")),
        optionsContent_
    );
    showObjectStatsCheck_->setChecked(baseTask_.showObjectStatsHud);
    showChartInfoCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("dialog.video_export.option.show_chart_info")),
        optionsContent_
    );
    showChartInfoCheck_->setChecked(baseTask_.showChartInfoHud);
    clockCountCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("video_export.enable_clock_count_1"))
            .arg(baseTask_.clockCount),
        optionsContent_
    );
    // Opt-in count-in: OFF by default. When checked, applyUiToTask bakes the
    // chart's clock_count beats (the value shown in the label, carried in
    // baseTask_) into the export; unchecked exports with no clock.wav ticks.
    clockCountCheck_->setChecked(false);
    // Live WYSIWYG: re-seed the export-page audition count-in when toggled (the
    // host listens via clockCountEnabledChanged). Export baking is separate
    // (applyUiToTask sets task.clockCountEnabled).
    connect(clockCountCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        emit clockCountEnabledChanged(checked);
    });
    addIntroCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("video_export.add_intro")),
        optionsContent_
    );
    addIntroCheck_->setChecked(baseTask_.intro.enabled);
    smoothBrightnessCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("video_export.smooth_brightness")),
        optionsContent_
    );
    smoothBrightnessCheck_->setChecked(baseTask_.smoothBrightness);
    smoothBrightnessCheck_->setText(UiText::text(QStringLiteral("video_export.smooth_brightness")));
    brightnessOuterSlider_ = new QSlider(Qt::Horizontal, optionsContent_);
    brightnessOuterSlider_->setRange(0, 100);
    brightnessOuterSlider_->setSingleStep(1);
    brightnessOuterSlider_->setPageStep(1);
    brightnessOuterSlider_->setTickInterval(1);
    brightnessOuterSlider_->setValue(qRound(qBound(0.0, baseTask_.backgroundBrightnessOuter, 1.0) * 100.0));
    QWidget* outerBrightnessOption = miacode::ui::createDialogSliderOption(
        UiText::text(QStringLiteral("dialog.video_export.option.brightness_outer")),
        brightnessOuterSlider_,
        &brightnessOuterValueLabel_,
        QStringLiteral("%"),
        optionsContent_,
        miacode::ui::DialogSliderOptionLayout::Stacked
    );
    brightnessInnerSlider_ = new QSlider(Qt::Horizontal, optionsContent_);
    brightnessInnerSlider_->setRange(0, 100);
    brightnessInnerSlider_->setSingleStep(1);
    brightnessInnerSlider_->setPageStep(1);
    brightnessInnerSlider_->setTickInterval(1);
    brightnessInnerSlider_->setValue(qRound(qBound(0.0, baseTask_.backgroundBrightnessInner, 1.0) * 100.0));
    QWidget* innerBrightnessOption = miacode::ui::createDialogSliderOption(
        UiText::text(QStringLiteral("dialog.video_export.option.brightness_inner")),
        brightnessInnerSlider_,
        &brightnessInnerValueLabel_,
        QStringLiteral("%"),
        optionsContent_,
        miacode::ui::DialogSliderOptionLayout::Stacked
    );
    optionsLayout->addWidget(outerBrightnessOption, 1, 0, 1, 1);
    optionsLayout->addWidget(innerBrightnessOption, 1, 1, 1, 1);
    layoutSquareScaleSlider_ = new QSlider(Qt::Horizontal, optionsContent_);
    layoutSquareScaleSlider_->setRange(
        qRound(miacode::preview_video::kLayoutSquareScaleMin * 100.0),
        qRound(miacode::preview_video::kLayoutSquareScaleMax * 100.0));
    layoutSquareScaleSlider_->setSingleStep(qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0));
    layoutSquareScaleSlider_->setPageStep(qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0));
    layoutSquareScaleSlider_->setTickInterval(qRound(miacode::preview_video::kLayoutSquareScaleStep * 100.0));
    layoutSquareScaleSlider_->setValue(
        qRound(miacode::preview_video::normalizedLayoutSquareScale(baseTask_.layoutSquareScale) * 100.0));
    QWidget* layoutSquareScaleOption = miacode::ui::createDialogSliderOption(
        UiText::text(QStringLiteral("video_export.layout_size")),
        layoutSquareScaleSlider_,
        &layoutSquareScaleValueLabel_,
        QStringLiteral("%"),
        optionsContent_,
        miacode::ui::DialogSliderOptionLayout::Stacked
    );
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
    auto* gameplayFlowControls = new QWidget(gameplayPage_);
    auto* gameplayFlowLayout = new QGridLayout(gameplayFlowControls);
    gameplayFlowLayout->setContentsMargins(kSectionContentLeftInset, 0, kSectionContentLeftInset, 0);
    gameplayFlowLayout->setHorizontalSpacing(10);
    gameplayFlowLayout->setVerticalSpacing(8);
    gameplayFlowLayout->setColumnStretch(0, 1);
    gameplayFlowLayout->setColumnStretch(1, 1);
    const auto createFlowSpeedEdit = [this, snapFlowSpeed, flowSpeedMin, flowSpeedMax](
        QWidget* parent,
        double* selectedFlowSpeed,
        const std::function<void(double)>& applyFlowSpeed
    ) {
        auto* flowSpeedEdit = new QLineEdit(parent);
        flowSpeedEdit->setAlignment(Qt::AlignCenter);
        flowSpeedEdit->setText(flowSpeedValueLabel(selectedFlowSpeed != nullptr
            ? *selectedFlowSpeed
            : miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed));
        flowSpeedEdit->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet());
        flowSpeedEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        flowSpeedEdit->ensurePolished();
        flowSpeedEdit->setMinimumHeight(qMax(flowSpeedEdit->sizeHint().height(), 30) + 4);
        auto* flowSpeedValidator = new QDoubleValidator(flowSpeedMin, flowSpeedMax, 2, flowSpeedEdit);
        flowSpeedValidator->setNotation(QDoubleValidator::StandardNotation);
        flowSpeedEdit->setValidator(flowSpeedValidator);
        QObject::connect(flowSpeedEdit, &QLineEdit::editingFinished, this,
            [flowSpeedEdit, selectedFlowSpeed, snapFlowSpeed, applyFlowSpeed]() {
                if (flowSpeedEdit == nullptr || selectedFlowSpeed == nullptr) {
                    return;
                }
                bool ok = false;
                const double typedSpeed = flowSpeedEdit->text().trimmed().toDouble(&ok);
                if (!ok) {
                    flowSpeedEdit->setText(flowSpeedValueLabel(*selectedFlowSpeed));
                    return;
                }
                *selectedFlowSpeed = snapFlowSpeed(typedSpeed);
                flowSpeedEdit->setText(flowSpeedValueLabel(*selectedFlowSpeed));
                if (applyFlowSpeed) {
                    applyFlowSpeed(*selectedFlowSpeed);
                }
            });
        return flowSpeedEdit;
    };
    const auto addGameplayFlowField = [](
        QWidget* parent,
        QGridLayout* layout,
        int row,
        int column,
        const QString& labelText,
        QWidget* control
    ) {
        auto* field = new QWidget(parent);
        auto* fieldLayout = new QVBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 3);
        fieldLayout->setSpacing(6);
        auto* label = new QLabel(labelText, field);
        fieldLayout->addWidget(label, 0);
        control->setMinimumHeight(qMax(control->minimumHeight(), control->sizeHint().height() + 4));
        fieldLayout->addWidget(control, 0);
        layout->addWidget(field, row, column);
    };
    tapFlowSpeedEdit_ = createFlowSpeedEdit(
        gameplayFlowControls,
        &selectedTapFlowSpeed_,
        [this](double flowSpeed) {
            if (previewTapFlowSpeedCallback_) {
                previewTapFlowSpeedCallback_(flowSpeed);
            }
        });
    touchFlowSpeedEdit_ = createFlowSpeedEdit(
        gameplayFlowControls,
        &selectedTouchFlowSpeed_,
        [this](double flowSpeed) {
            if (previewTouchFlowSpeedCallback_) {
                previewTouchFlowSpeedCallback_(flowSpeed);
            }
        });
    addGameplayFlowField(
        gameplayFlowControls,
        gameplayFlowLayout,
        0,
        0,
        UiText::text(QStringLiteral("dialog.video_export.option.tap_flow_speed")),
        tapFlowSpeedEdit_);
    addGameplayFlowField(
        gameplayFlowControls,
        gameplayFlowLayout,
        0,
        1,
        UiText::text(QStringLiteral("dialog.video_export.option.touch_flow_speed")),
        touchFlowSpeedEdit_);
    gameplayPageLayout_->addWidget(gameplayFlowControls, 0, Qt::AlignTop);
    gameplayPageLayout_->addStretch(1);
    // 皮肤 tab carries only the injected owner-wired panel (buildSkinSettings).
    skinPageLayout_->addStretch(1);
    const QString scaleFillLabel = UiText::text(QStringLiteral("dialog.video_export.option.scale.fill"));
    const QString scaleFitLabel = UiText::text(QStringLiteral("dialog.video_export.option.scale.fit"));
    const QString scaleSquareFitLabel = UiText::text(QStringLiteral("dialog.video_export.option.scale.square_fit"));
    const QString scaleInnerCircleFitOuterFillLabel = UiText::text(QStringLiteral("dialog.video_export.option.scale.inner_circle_fit_outer_fill"));
    selectedBackgroundScaleMode_ = baseTask_.backgroundScaleMode;
    backgroundScaleModeCombo_ = miacode::ui::createDialogComboBox(optionsContent_, 12);
    backgroundScaleModeCombo_->addItem(
        scaleFillLabel, static_cast<int>(PreviewBackgroundScaleMode::FillCrop));
    backgroundScaleModeCombo_->addItem(
        scaleFitLabel, static_cast<int>(PreviewBackgroundScaleMode::FitContain));
    backgroundScaleModeCombo_->addItem(
        scaleSquareFitLabel, static_cast<int>(PreviewBackgroundScaleMode::SquareFitContain));
    backgroundScaleModeCombo_->addItem(
        scaleInnerCircleFitOuterFillLabel,
        static_cast<int>(PreviewBackgroundScaleMode::InnerCircleFitOuterFill));
    backgroundScaleModeCombo_->setCurrentIndex(qMax(
        0, backgroundScaleModeCombo_->findData(static_cast<int>(selectedBackgroundScaleMode_))));
    miacode::ui::applyDialogComboBoxStyle(backgroundScaleModeCombo_, 12);
    connect(backgroundScaleModeCombo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this,
            [this](int index) {
                if (index < 0) {
                    return;
                }
                selectedBackgroundScaleMode_ = static_cast<PreviewBackgroundScaleMode>(
                    backgroundScaleModeCombo_->itemData(index).toInt());
                if (previewScaleModeCallback_) {
                    previewScaleModeCallback_(selectedBackgroundScaleMode_);
                }
            });
    auto* backgroundScaleModeRow = new QWidget(optionsContent_);
    auto* backgroundScaleModeLayout = new QHBoxLayout(backgroundScaleModeRow);
    backgroundScaleModeLayout->setContentsMargins(0, 0, 0, 0);
    backgroundScaleModeLayout->setSpacing(6);
    auto* backgroundScaleModeLabel = new QLabel(
        UiText::text(QStringLiteral("dialog.video_export.option.scale_mode")),
        backgroundScaleModeRow
    );
    backgroundScaleModeLayout->addWidget(backgroundScaleModeLabel, 0);
    backgroundScaleModeLayout->addWidget(backgroundScaleModeCombo_, 1);
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
    miacode::ui::busyTick();

    // HUD font moved to the shared 皮肤 tab (buildSkinSettings). The former
    // standalone 字体 tab is gone.
    miacode::ui::busyTick();

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

    // "添加片头" — THE master switch of this tab; bold + one size up, wrapped in
    // an accent-tinted capsule so it reads as the prominent toggle that gates
    // the tab. The pill is styled via exportDialogStyleSheet()'s
    // QFrame#AddIntroCapsule rule (so it re-themes with applyThemeStyles).
    // (Moved from the Video tab.)
    {
        QFont introCheckFont = addIntroCheck_->font();
        introCheckFont.setBold(true);
        introCheckFont.setPointSizeF(introCheckFont.pointSizeF() + 1.0);
        addIntroCheck_->setFont(introCheckFont);
    }
    auto* addIntroCapsule = new QFrame(introControls);
    addIntroCapsule->setObjectName(QStringLiteral("AddIntroCapsule"));
    auto* addIntroCapsuleLayout = new QHBoxLayout(addIntroCapsule);
    // Horizontal padding hugs the checkbox+label into a pill; the vertical
    // padding sets the capsule height (and thus how round the radius reads).
    addIntroCapsuleLayout->setContentsMargins(14, 7, 18, 7);
    addIntroCapsuleLayout->setSpacing(0);
    addIntroCapsuleLayout->addWidget(addIntroCheck_, 0, Qt::AlignVCenter);   // reparents into the capsule
    // AlignLeft so the capsule sizes to its content instead of stretching to
    // the full controls-column width.
    introControlsLayout->addWidget(addIntroCapsule, 0, Qt::AlignLeft);

    // 背景 group — backdrop source + blur (size follows the export resolution).
    auto* introBgGroup = new QGroupBox(
        UiText::text(QStringLiteral("cover.background")), introControls);
    auto* introBgForm = new QFormLayout(introBgGroup);
    introBgForm->setSpacing(8);
    introBgForm->setLabelAlignment(Qt::AlignLeft);
    introBgForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    introBackgroundCombo_ =
        miacode::ui::createDialogComboBox(introBgGroup, 12, Qt::AlignLeft | Qt::AlignVCenter);
    introBackgroundCombo_->addItem(
        UiText::text(QStringLiteral("cover.chart_jacket")), QStringLiteral("jacket"));
    introBackgroundCombo_->addItem(
        UiText::text(QStringLiteral("cover.custom_image")), QStringLiteral("custom"));
    miacode::ui::applyDialogComboBoxStyle(introBackgroundCombo_, 12);
    introBgForm->addRow(UiText::text(QStringLiteral("cover.background")), introBackgroundCombo_);
    auto* introBgPathRow = new QWidget(introBgGroup);
    auto* introBgPathLayout = new QHBoxLayout(introBgPathRow);
    introBgPathLayout->setContentsMargins(0, 0, 0, 0);
    introBgPathLayout->setSpacing(8);
    introBackgroundPathEdit_ = new QLineEdit(introBgPathRow);
    introBackgroundPathEdit_->setPlaceholderText(
        UiText::text(QStringLiteral("cover.custom_background_image_path")));
    introBackgroundPathEdit_->setStyleSheet(UiTheme::dialogMenuLineEditStyleSheet(UiTheme::colors().windowAltBg));
    introBackgroundBrowse_ = miacode::ui::createDialogAuxiliaryButton(
        introBgPathRow, UiText::text(QStringLiteral("cover.browse")));
    introBgPathLayout->addWidget(introBackgroundPathEdit_, 1);
    introBgPathLayout->addWidget(introBackgroundBrowse_, 0);
    introBgForm->addRow(QString(), introBgPathRow);
    introBlurCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("cover.blur_background")), introBgGroup);
    introBlurCheck_->setChecked(baseTask_.intro.blurBackground);
    introBgForm->addRow(QString(), introBlurCheck_);
    introControlsLayout->addWidget(introBgGroup, 0);

    // 难度卡 group — DX/SD type + shadow + level-text render. (No card toggle:
    // an intro always carries the difficulty card.)
    auto* introCardGroup = new QGroupBox(
        UiText::text(QStringLiteral("cover.difficulty_card")), introControls);
    auto* introCardForm = new QFormLayout(introCardGroup);
    introCardForm->setSpacing(8);
    introCardForm->setLabelAlignment(Qt::AlignLeft);
    introCardForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    // DX / SD chart type. "Standard" is the QML-side mode value: the card shows
    // the スタンダード plate top-right and mirrors the tab shoulder under it.
    introCardModeCombo_ =
        miacode::ui::createDialogComboBox(introCardGroup, 12, Qt::AlignLeft | Qt::AlignVCenter);
    introCardModeCombo_->addItem(QStringLiteral("DX"), QStringLiteral("DX"));
    introCardModeCombo_->addItem(QStringLiteral("SD"), QStringLiteral("Standard"));
    miacode::ui::applyDialogComboBoxStyle(introCardModeCombo_, 12);
    {
        const int modeIdx = introCardModeCombo_->findData(baseTask_.intro.mode);
        if (modeIdx >= 0) {
            introCardModeCombo_->setCurrentIndex(modeIdx);
        }
    }
    introCardForm->addRow(
        UiText::text(QStringLiteral("cover.chart_type")), introCardModeCombo_);
    introCardShadowCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("cover.card_drop_shadow")), introCardGroup);
    introCardShadowCheck_->setChecked(baseTask_.intro.cardShadow);
    introCardForm->addRow(QString(), introCardShadowCheck_);
    introLevelTextCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("cover.render_level_as_text")), introCardGroup);
    introLevelTextCheck_->setChecked(baseTask_.intro.lvRenderMode == QStringLiteral("text"));
    // Opt past the app-wide tooltip suppression (MainWindow installs a global
    // event filter that hides tooltips outside the preview area).
    introLevelTextCheck_->setProperty("miacodeAllowTooltip", true);
    introLevelTextCheck_->setToolTip(UiText::text(QStringLiteral("video_export.level_text_tooltip")));
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
        UiText::text(QStringLiteral("video_export.output"))
    );
    settingsTabs_->addTab(
        visualsPage,
        UiText::text(QStringLiteral("video_export.video"))
    );
    settingsTabs_->addTab(
        gameplayPage_,
        UiText::text(QStringLiteral("video_export.gameplay"))
    );
    settingsTabs_->addTab(
        skinPage_,
        UiText::text(QStringLiteral("video_export.skin"))
    );
    settingsTabs_->addTab(
        introPage,
        UiText::text(QStringLiteral("video_export.intro"))
    );
    settingsTabs_->addTab(
        rangePage,
        UiText::text(QStringLiteral("video_export.export_range"))
    );
    connect(settingsTabs_, &QTabWidget::currentChanged, this, [this]() {
        refreshSharedSettingsFromCallback();
    });

    buttonBox_ = new QDialogButtonBox(this);
    QDialogButtonBox* buttonBox = buttonBox_;
    exportButton_ = miacode::ui::createDialogPushButton(
        UiText::text(QStringLiteral("dialog.video_export.button.export")), this, true);
    buttonBox->addButton(exportButton_, QDialogButtonBox::AcceptRole);
    exportButton_->setMinimumWidth(kDialogActionButtonMinWidth);
    cancelButton_ = miacode::ui::createDialogPushButton(
        UiText::text(QStringLiteral("dialog.video_export.button.cancel")), this);
    cancelButton_->setMinimumWidth(kDialogActionButtonMinWidth);
    buttonBox->addButton(cancelButton_, QDialogButtonBox::RejectRole);
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
    miacode::ui::busyTick();
    initialResolutionAspectRatio_ = selectedResolutionAspectRatio();
    syncRangeUi();
    updatePreviewPlayPauseUi();
    refreshDialogGeometry();
    miacode::ui::busyTick();
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

void VideoExportDialog::refreshSharedSettingsFromCallback()
{
    if (sharedSettingsSnapshotCallback_) {
        refreshSharedSettingsFromTask(sharedSettingsSnapshotCallback_());
    }
    if (ownerWiredSettingsRefreshCallback_) {
        ownerWiredSettingsRefreshCallback_();
    }
}

void VideoExportDialog::refreshSharedSettingsFromTask(const VideoExportTask& task)
{
    const auto setCheckBoxSilently = [](QCheckBox* checkbox, bool checked) {
        if (checkbox == nullptr) {
            return;
        }
        const QSignalBlocker blocker(checkbox);
        checkbox->setChecked(checked);
    };
    setCheckBoxSilently(showTimestampCheck_, task.showTimestamp);
    setCheckBoxSilently(showObjectStatsCheck_, task.showObjectStatsHud);
    setCheckBoxSilently(showChartInfoCheck_, task.showChartInfoHud);
    setCheckBoxSilently(smoothBrightnessCheck_, task.smoothBrightness);
    initialShowTimestamp_ = task.showTimestamp;
    initialShowObjectStats_ = task.showObjectStatsHud;
    initialShowChartInfo_ = task.showChartInfoHud;

    const int outerBrightness = qRound(qBound(0.0, task.backgroundBrightnessOuter, 1.0) * 100.0);
    if (brightnessOuterSlider_ != nullptr) {
        const QSignalBlocker blocker(brightnessOuterSlider_);
        brightnessOuterSlider_->setValue(outerBrightness);
    }
    if (brightnessOuterValueLabel_ != nullptr) {
        brightnessOuterValueLabel_->setText(QStringLiteral("%1%").arg(outerBrightness));
    }

    const int innerBrightness = qRound(qBound(0.0, task.backgroundBrightnessInner, 1.0) * 100.0);
    if (brightnessInnerSlider_ != nullptr) {
        const QSignalBlocker blocker(brightnessInnerSlider_);
        brightnessInnerSlider_->setValue(innerBrightness);
    }
    if (brightnessInnerValueLabel_ != nullptr) {
        brightnessInnerValueLabel_->setText(QStringLiteral("%1%").arg(innerBrightness));
    }

    const int layoutScale = qRound(miacode::preview_video::normalizedLayoutSquareScale(task.layoutSquareScale) * 100.0);
    if (layoutSquareScaleSlider_ != nullptr) {
        const QSignalBlocker blocker(layoutSquareScaleSlider_);
        layoutSquareScaleSlider_->setValue(layoutScale);
    }
    if (layoutSquareScaleValueLabel_ != nullptr) {
        layoutSquareScaleValueLabel_->setText(QStringLiteral("%1%").arg(layoutScale));
    }

    selectedBackgroundScaleMode_ = task.backgroundScaleMode;
    if (backgroundScaleModeCombo_ != nullptr) {
        const QSignalBlocker blocker(backgroundScaleModeCombo_);
        backgroundScaleModeCombo_->setCurrentIndex(qMax(
            0,
            backgroundScaleModeCombo_->findData(static_cast<int>(selectedBackgroundScaleMode_))));
    }

    selectedTapFlowSpeed_ = snappedFlowSpeed(task.tapFlowSpeed);
    if (tapFlowSpeedEdit_ != nullptr && !tapFlowSpeedEdit_->hasFocus()) {
        const QSignalBlocker blocker(tapFlowSpeedEdit_);
        tapFlowSpeedEdit_->setText(flowSpeedValueLabel(selectedTapFlowSpeed_));
    }
    selectedTouchFlowSpeed_ = snappedFlowSpeed(task.touchFlowSpeed);
    if (touchFlowSpeedEdit_ != nullptr && !touchFlowSpeedEdit_->hasFocus()) {
        const QSignalBlocker blocker(touchFlowSpeedEdit_);
        touchFlowSpeedEdit_->setText(flowSpeedValueLabel(selectedTouchFlowSpeed_));
    }
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
    if (rangePreviewPlaying_ || isPreviewPlaying()) {
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
    const bool previewPlaying = rangePreviewPlaying_ || isPreviewPlaying();
    if (previewPlaying) {
        previewRangeButton_->setIcon(makePreviewPauseIcon(iconColor));
        previewRangeButton_->setToolTip(UiText::text(QStringLiteral("dialog.video_export.preview.pause")));
        previewRangeButton_->setStyleSheet(UiTheme::dialogIconToolButtonStyleSheet(true));
    } else {
        previewRangeButton_->setIcon(makePreviewPlayIcon(iconColor));
        previewRangeButton_->setToolTip(UiText::text(QStringLiteral("dialog.video_export.preview.play")));
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
    refreshRangeSummaryLabel();
    if (stopPreviewButton_ != nullptr) {
        stopPreviewButton_->setEnabled(rangePreviewPlaying_ || previewCursorSecond_ > 0.0005);
    }

    syncingRangeUi_ = false;
}

void VideoExportDialog::refreshRangeSummaryLabel()
{
    if (rangeSummaryLabel_ == nullptr) {
        return;
    }
    const double start = qBound(0.0, rangeStartSeconds(), totalDurationSeconds_);
    const double end = qBound(start, rangeEndSeconds(), totalDurationSeconds_);
    const double durationSeconds = qMax(0.0, end - start);
    rangeSummaryLabel_->setText(
        UiText::text(QStringLiteral("video_export.current_export_range_1_2"))
            .arg(formatSecond(start), formatSecond(end),
                 QString::number(durationSeconds, 'f', 3)));
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
