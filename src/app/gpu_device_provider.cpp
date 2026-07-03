#include "MainEntrypoints.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/GpuDevicePolicy.h"

#include <QQuickGraphicsDevice>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>

#if defined(Q_OS_WIN)
#include "preview/runtime/PreviewSharedD3D11Device.h"  // H2 single-device video-share
#endif

// P4.1 / P4.2 — high-performance Quick graphics device provider.
//
// Given the P3 resolved GPU policy, bind a Quick window/view to the resolved
// high-performance DXGI adapter using QQuickGraphicsDevice::fromAdapter — Qt
// creates and owns the D3D11 device (simple lifetime; the primary approach in
// the plan's P4.2). We do NOT create an ID3D11Device ourselves for the root
// window; the H2 single-device path (fromDeviceAndContext) stays reserved for
// the preview composite's video-decode sharing.
//
// Safety: binding only happens on Windows, only when the RHI is D3D11 (or the
// Windows platform default, still Unknown at bind time), only when the resolved
// policy yields a real hardware adapter LUID, and only when that adapter is NOT
// already the DXGI default adapter (otherwise Qt would pick it anyway, so
// binding is pure redundant cross-adapter machinery). Every miss falls through
// to Qt's default adapter — never blocks startup. The A/B off switch is the P3
// policy itself (MIACODE_GPU_POLICY=platform_default).

namespace miacode::app::entry {

namespace {

QString providerRhiApiName(QSGRendererInterface::GraphicsApi api)
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

void logProvider(const QString& surfaceLabel, const QString& payload)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("startup/gpu_provider"),
        QStringLiteral("surface=%1 %2").arg(surfaceLabel).arg(payload));
}

}  // namespace

bool bindHighPerformanceQuickGraphicsDevice(
    QQuickWindow* window, const QString& surfaceLabel, bool preferVideoShareDevice)
{
    if (window == nullptr) {
        return false;
    }

    // Only D3D11 can honor a DXGI adapter LUID. `Unknown` is the Windows
    // platform-default case (no --rhi forced), which resolves to D3D11 at
    // runtime — the primary intended path. An explicitly-forced OpenGL /
    // Software / Vulkan / D3D12 backend returns a concrete value and is skipped
    // so we respect the user's explicit RHI choice (plan P4.4).
    const QSGRendererInterface::GraphicsApi api = QQuickWindow::graphicsApi();
    const bool d3d11Capable = api == QSGRendererInterface::Direct3D11
        || api == QSGRendererInterface::Unknown;
    if (!d3d11Capable) {
        logProvider(surfaceLabel,
            QStringLiteral("action=skip source=qt_default reason=explicit_non_d3d11_rhi api=%1")
                .arg(providerRhiApiName(api)));
        return false;
    }

#if defined(Q_OS_WIN)
    // Video surface (preview composite / any surface that renders decoded video):
    // try the H2 single-device video-sharing device first (default OFF) so the
    // decoder and QRhi share one ID3D11Device. If that is off/unavailable, keep
    // Qt's DEFAULT adapter — never fromAdapter to a non-default adapter — because
    // the QtAVPlayer D3D11VA two-device keyed-mutex bridge is same-adapter only,
    // and FFmpeg decodes on the default adapter. Binding this surface elsewhere
    // would break video-background playback.
    if (preferVideoShareDevice) {
        const QQuickGraphicsDevice shared =
            miacode::preview::sharedPreviewQuickGraphicsDevice();
        if (!shared.isNull()) {
            window->setGraphicsDevice(shared);
            logProvider(surfaceLabel,
                QStringLiteral("action=bound source=h2_shared reason=single_device_video_share"));
            return true;
        }
        logProvider(surfaceLabel,
            QStringLiteral("action=skip source=qt_default "
                           "reason=video_surface_keeps_default_for_decode_bridge"));
        return false;
    }

    // Non-video surface (root window). fromAdapter binding is opt-in until it is
    // validated on a dual-GPU machine — the root window also embeds decoded
    // video via the same two-device bridge, so binding it to a non-default
    // adapter carries the same risk for video-background charts (plan P4.4).
    if (!miacode::debug_options::gpuBindHighPerformanceEnabled()) {
        logProvider(surfaceLabel,
            QStringLiteral("action=skip source=qt_default "
                           "reason=bind_disabled_default flag=MIACODE_GPU_BIND_HIGH_PERFORMANCE"));
        return false;
    }

    const miacode::gpu::ResolvedGpuPolicy& resolved = miacode::gpu::resolveGpuPolicyOnce();
    if (resolved.resolvedKind == miacode::gpu::GpuPolicyKind::Software) {
        logProvider(surfaceLabel,
            QStringLiteral("action=skip source=qt_default reason=policy_software"));
        return false;
    }
    // Only bind for policies that resolved to a CONCRETE adapter. Any fallback
    // to platform_default (e.g. an illegal / unknown LUID) keeps Qt's default
    // adapter — binding with a stale/unresolved LUID would log a misleading
    // action=bound while Qt silently falls back to the default anyway.
    if (resolved.resolvedKind != miacode::gpu::GpuPolicyKind::AutoHighPerformance
        && resolved.resolvedKind != miacode::gpu::GpuPolicyKind::AdapterLuid) {
        logProvider(surfaceLabel,
            QStringLiteral("action=skip source=qt_default reason=policy_not_adapter_binding "
                           "resolved_policy=%1 fallback=%2")
                .arg(miacode::gpu::gpuPolicyKindName(resolved.resolvedKind))
                .arg(resolved.fallbackReason.isEmpty() ? QStringLiteral("(none)")
                                                       : resolved.fallbackReason));
        return false;
    }
    if (!resolved.adapterLuid.has_value() || !resolved.adapterLuid->valid) {
        logProvider(surfaceLabel,
            QStringLiteral("action=skip source=qt_default reason=no_high_perf_luid "
                           "resolved_policy=%1 fallback=%2")
                .arg(miacode::gpu::gpuPolicyKindName(resolved.resolvedKind))
                .arg(resolved.fallbackReason.isEmpty() ? QStringLiteral("(none)")
                                                       : resolved.fallbackReason));
        return false;
    }

    // Skip when the high-performance adapter already IS the DXGI default adapter
    // (single-GPU machine, or the dGPU already drives the primary display). Qt
    // would pick it anyway, so binding adds nothing but cross-adapter risk.
    const std::optional<miacode::gpu::GpuAdapterLuid> defaultLuid =
        miacode::gpu::defaultAdapterLuid();
    if (defaultLuid.has_value() && defaultLuid->equals(*resolved.adapterLuid)) {
        logProvider(surfaceLabel,
            QStringLiteral("action=skip source=qt_default reason=high_perf_equals_default_adapter "
                           "luid=%1").arg(resolved.adapterLuid->toString()));
        return false;
    }

    const QQuickGraphicsDevice device = QQuickGraphicsDevice::fromAdapter(
        resolved.adapterLuid->low, static_cast<qint32>(resolved.adapterLuid->high));
    if (device.isNull()) {
        logProvider(surfaceLabel,
            QStringLiteral("action=skip source=qt_default reason=from_adapter_null luid=%1")
                .arg(resolved.adapterLuid->toString()));
        return false;
    }

    window->setGraphicsDevice(device);
    logProvider(surfaceLabel,
        QStringLiteral("action=bound source=from_adapter resolved_policy=%1 adapter=\"%2\" "
                       "luid=%3 default_luid=%4")
            .arg(miacode::gpu::gpuPolicyKindName(resolved.resolvedKind))
            .arg(resolved.adapterName.isEmpty() ? QStringLiteral("(unknown)")
                                                : resolved.adapterName)
            .arg(resolved.adapterLuid->toString())
            .arg(defaultLuid.has_value() ? defaultLuid->toString() : QStringLiteral("(unknown)")));
    return true;
#else
    Q_UNUSED(preferVideoShareDevice);
    logProvider(surfaceLabel,
        QStringLiteral("action=skip source=qt_default reason=non_windows"));
    return false;
#endif
}

}  // namespace miacode::app::entry
