#pragma once

#include <QWidget>

class QQuickItem;
class QQuickView;

#include "tools/video_export/VideoExportController.h"   // IntroBannerSpec

// Read-only live preview of the maimai track-start intro for the export
// dialog's "片头" tab. Hosts the SAME IntroOverlay.qml the export session
// mounts (same banner template injection, same track/style contract via
// introBannerTrackMap/introBannerStyleMap), pinned at a representative
// card-hold frame — so the preview can never diverge from the export.
//
// The widget is a FIXED box: switching the export resolution/aspect never
// resizes it (so the dialog geometry stays put). Inside it, a letterboxed
// clip item at the output aspect plays the EXPORT FRAMEBUFFER's role, and the
// overlay root is composed inside it with the exact transform the export uses
// (PreviewQuickExportSession::applyIntroGeometry: native 16:9 1920x1080,
// scaled to the frame height around centre, centre-cropped) — so 1:1 / 4:3
// outputs preview the same centre-cut backdrop band the exported frame gets,
// not a re-layout at the output aspect.
//
// HOSTING CONSTRAINT — native QQuickView via createWindowContainer, NOT
// QQuickWidget: this panel is embedded in the Export page, whose widget tree
// lives inside a quick-shell bridge surface that the QML shell embeds via
// QWindow::fromWinId. Introducing a QQuickWidget (texture-based child) there
// forces Qt to DESTROY + RECREATE the surface's top-level HWND to switch the
// backingstore to RHI-composited flushing — the shell's WindowContainer keeps
// wrapping the dead HWND and the whole workspace pops out as a floating
// frameless window (the 2026-06-12 导出页/谱面信息设置 整列错位 bug). A native
// child window has no such backingstore interaction. The QQuickView's key
// events are forwarded to the host window by an event filter so a click on
// the preview can't swallow the dialog's Esc/arrows (same concern
// ExportCoverDialog's previewWindowObject filter handles).
class IntroPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit IntroPreviewWidget(QWidget* parent = nullptr);

    // Push the current dialog state into the overlay. Cheap (property writes
    // on the live scene); call freely on every control change.
    void applySpec(const IntroBannerSpec& spec);

    // Letterbox the overlay at this output aspect inside the fixed box.
    void setOutputAspectRatio(double ratio);

protected:
    // Forwards the QQuickView's key events to the host window (see class
    // comment).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateRootGeometry();
    QQuickItem* rootObject() const;

    QQuickView* view_ = nullptr;

    // Letterboxed clip container standing in for the export framebuffer
    // viewport (clips the centre-crop overhang of the 16:9 overlay).
    QQuickItem* clipItem_ = nullptr;

    // Mid card-alone hold (frames 36..194 @60fps): wipe retracted, every
    // staggered reveal stage done — the canonical "片头的样子" still.
    static constexpr int kPreviewFrame = 120;
    // Fixed preview box: 16:9 output gets thin top/bottom bars, square /
    // vertical outputs pillarbox inside the same footprint.
    static constexpr int kBoxWidth = 320;
    static constexpr int kBoxHeight = 220;

    double outputAspectRatio_ = 16.0 / 9.0;
};
