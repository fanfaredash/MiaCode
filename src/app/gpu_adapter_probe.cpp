#include "MainEntrypoints.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QRunnable>
#include <QSGRendererInterface>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#endif

// P1 — actual GPU adapter / renderer visibility for GUI Quick surfaces.
//
// The startup/graphics_backend + preview/quick_scene rhi_backend logs already
// say WHICH RHI API is in use, but not which physical adapter Qt Quick's RHI
// actually bound. This probe answers that: on Direct3D11 it pulls the live
// ID3D11Device from QSGRendererInterface::DeviceResource (valid only on the
// render thread) and reads the DXGI adapter desc (name / vendor / device /
// subsys / revision / LUID / memory). On OpenGL it records GL_VENDOR /
// GL_RENDERER / GL_VERSION — a renderer string only, NOT a guaranteed adapter
// on hybrid-graphics Windows. Other RHIs log "unsupported" rather than guess.

namespace miacode::app::entry {

namespace {

QString rhiApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Software:    return QStringLiteral("Software");
    case QSGRendererInterface::OpenGL:      return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11:  return QStringLiteral("Direct3D11");
    case QSGRendererInterface::Direct3D12:  return QStringLiteral("Direct3D12");
    case QSGRendererInterface::Vulkan:      return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal:       return QStringLiteral("Metal");
    case QSGRendererInterface::Unknown:
    default:                                return QStringLiteral("Unknown");
    }
}

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
    QSGRendererInterface* ri = window->rendererInterface();
    const QSGRendererInterface::GraphicsApi api =
        ri != nullptr ? ri->graphicsApi() : QSGRendererInterface::Unknown;

#ifdef Q_OS_WIN
    if (api == QSGRendererInterface::Direct3D11 && ri != nullptr) {
        void* devicePtr = ri->getResource(window, QSGRendererInterface::DeviceResource);
        if (devicePtr != nullptr) {
            auto* device = reinterpret_cast<ID3D11Device*>(devicePtr);
            IDXGIDevice* dxgiDevice = nullptr;
            if (SUCCEEDED(device->QueryInterface(
                    __uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))
                && dxgiDevice != nullptr) {
                IDXGIAdapter* adapter = nullptr;
                if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter != nullptr) {
                    DXGI_ADAPTER_DESC desc{};
                    if (SUCCEEDED(adapter->GetDesc(&desc))) {
                        const quint64 dedicatedMb =
                            static_cast<quint64>(desc.DedicatedVideoMemory) / (1024ull * 1024ull);
                        const quint64 sharedMb =
                            static_cast<quint64>(desc.SharedSystemMemory) / (1024ull * 1024ull);
                        const quint64 dedicatedSysMb =
                            static_cast<quint64>(desc.DedicatedSystemMemory) / (1024ull * 1024ull);
                        appendDeviceLog(
                            QStringLiteral(
                                "surface=%1 rhi_api=Direct3D11 adapter=\"%2\" vendor_id=0x%3 "
                                "device_id=0x%4 subsys_id=0x%5 revision=%6 luid=0x%7:0x%8 "
                                "dedicated_video_mb=%9 shared_system_mb=%10 dedicated_system_mb=%11")
                                .arg(surfaceLabel)
                                .arg(QString::fromWCharArray(desc.Description))
                                .arg(desc.VendorId, 4, 16, QLatin1Char('0'))
                                .arg(desc.DeviceId, 4, 16, QLatin1Char('0'))
                                .arg(desc.SubSysId, 8, 16, QLatin1Char('0'))
                                .arg(desc.Revision)
                                .arg(static_cast<quint32>(desc.AdapterLuid.HighPart), 0, 16)
                                .arg(static_cast<quint32>(desc.AdapterLuid.LowPart), 0, 16)
                                .arg(dedicatedMb)
                                .arg(sharedMb)
                                .arg(dedicatedSysMb));
                    }
                    adapter->Release();
                }
                dxgiDevice->Release();
            }
            return;
        }
    }
#endif

    if (api == QSGRendererInterface::OpenGL) {
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        QOpenGLFunctions* fns = ctx != nullptr ? ctx->functions() : nullptr;
        if (fns != nullptr) {
            const auto glStr = [fns](GLenum name) -> QString {
                const GLubyte* value = fns->glGetString(name);
                return value != nullptr
                    ? QString::fromLatin1(reinterpret_cast<const char*>(value))
                    : QStringLiteral("(null)");
            };
            appendDeviceLog(
                QStringLiteral(
                    "surface=%1 rhi_api=OpenGL gl_vendor=\"%2\" gl_renderer=\"%3\" "
                    "gl_version=\"%4\" note=renderer_string_only_adapter_not_guaranteed")
                    .arg(surfaceLabel)
                    .arg(glStr(GL_VENDOR))
                    .arg(glStr(GL_RENDERER))
                    .arg(glStr(GL_VERSION)));
            return;
        }
    }

    appendDeviceLog(
        QStringLiteral("surface=%1 rhi_api=%2 note=adapter_probe_unsupported_for_this_rhi")
            .arg(surfaceLabel)
            .arg(rhiApiName(api)));
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
