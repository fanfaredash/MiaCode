#include "MainEntrypoints.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/quick_scene/PreviewQuickGraphicsInfo.h"

#include <QQuickWindow>
#include <QRunnable>
#include <QString>

// P1 — actual GPU adapter / renderer visibility for GUI Quick surfaces.
//
// The startup/graphics_backend + preview/quick_scene rhi_backend logs name the
// RHI API. This render-thread probe records the physical device owned by each
// Quick window through the shared cross-platform RHI query.

namespace miacode::app::entry {

namespace {

void appendDeviceLog(const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/device"),
        payload);
}

// Runs on the QSG render thread (scheduled render job) where the RHI device
// and GL context are valid for the given window.
void probeAndLog(QQuickWindow* window, const QString& surfaceLabel)
{
    if (window == nullptr) {
        return;
    }
    const miacode::preview::quick_scene::QuickGraphicsInfo info =
        miacode::preview::quick_scene::queryQuickGraphicsInfo(window);
    appendDeviceLog(QStringLiteral("surface=%1 %2").arg(surfaceLabel, info.logFields));
}

class GpuAdapterProbeRenderJob final : public QRunnable
{
public:
    GpuAdapterProbeRenderJob(QQuickWindow* window, QString surfaceLabel)
        : window_(window), surfaceLabel_(std::move(surfaceLabel))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        // Qt drops queued render jobs (without running them) when the window
        // is torn down, so this only executes while `window_` is live and
        // mid-render — the raw pointer is safe.
        probeAndLog(window_, surfaceLabel_);
    }

private:
    QQuickWindow* window_ = nullptr;
    QString surfaceLabel_;
};

}  // namespace

void logQuickWindowGpuDevice(QQuickWindow* window, const QString& surfaceLabel)
{
    if (window == nullptr || !miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    // AfterRenderingStage runs on the render thread once the scene graph has a
    // device (and, for OpenGL, the context current) — the only safe moment to
    // read DeviceResource / GL strings.
    window->scheduleRenderJob(
        new GpuAdapterProbeRenderJob(window, surfaceLabel),
        QQuickWindow::AfterRenderingStage);
    window->update();
}

}  // namespace miacode::app::entry
