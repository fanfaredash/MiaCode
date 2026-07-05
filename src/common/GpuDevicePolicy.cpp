#include "common/GpuDevicePolicy.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QCoreApplication>
#include <QProcessEnvironment>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#endif

namespace miacode::gpu {

QString gpuPolicyKindName(GpuPolicyKind kind)
{
    switch (kind) {
    case GpuPolicyKind::AutoHighPerformance: return QStringLiteral("auto_high_performance");
    case GpuPolicyKind::PlatformDefault:     return QStringLiteral("platform_default");
    case GpuPolicyKind::AdapterLuid:         return QStringLiteral("adapter_luid");
    case GpuPolicyKind::Software:            return QStringLiteral("software");
    }
    return QStringLiteral("platform_default");
}

std::optional<GpuPolicyKind> parseGpuPolicyKind(const QString& raw)
{
    const QString v = raw.trimmed().toLower();
    if (v.isEmpty()) {
        return std::nullopt;
    }
    if (v == QStringLiteral("auto_high_performance") || v == QStringLiteral("auto")
        || v == QStringLiteral("high_performance") || v == QStringLiteral("hp")) {
        return GpuPolicyKind::AutoHighPerformance;
    }
    if (v == QStringLiteral("platform_default") || v == QStringLiteral("platform")
        || v == QStringLiteral("default")) {
        return GpuPolicyKind::PlatformDefault;
    }
    if (v == QStringLiteral("adapter_luid") || v == QStringLiteral("luid")) {
        return GpuPolicyKind::AdapterLuid;
    }
    if (v == QStringLiteral("software") || v == QStringLiteral("sw")) {
        return GpuPolicyKind::Software;
    }
    return std::nullopt;
}

QString GpuAdapterLuid::toString() const
{
    if (!valid) {
        return QStringLiteral("(none)");
    }
    return QStringLiteral("0x%1:0x%2").arg(high, 0, 16).arg(low, 0, 16);
}

std::optional<GpuAdapterLuid> parseAdapterLuid(const QString& raw)
{
    const QString trimmed = raw.trimmed();
    const int sep = trimmed.indexOf(QLatin1Char(':'));
    if (sep <= 0 || sep >= trimmed.size() - 1) {
        return std::nullopt;
    }
    const auto parseU32 = [](QString token, bool* ok) -> uint32_t {
        token = token.trimmed();
        int base = 10;
        if (token.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            token = token.mid(2);
            base = 16;
        }
        return token.toUInt(ok, base);
    };
    bool okHigh = false;
    bool okLow = false;
    const uint32_t high = parseU32(trimmed.left(sep), &okHigh);
    const uint32_t low = parseU32(trimmed.mid(sep + 1), &okLow);
    if (!okHigh || !okLow) {
        return std::nullopt;
    }
    GpuAdapterLuid luid;
    luid.high = high;
    luid.low = low;
    luid.valid = true;
    return luid;
}

QList<GpuAdapterInfo> enumerateHighPerformanceAdapters(QString* diagnostic)
{
#ifdef Q_OS_WIN
    QList<GpuAdapterInfo> result;
    QStringList trace;

    IDXGIFactory1* factory1 = nullptr;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory1)))
        || factory1 == nullptr) {
        if (diagnostic != nullptr) {
            *diagnostic = QStringLiteral("create_dxgi_factory_failed");
        }
        return result;
    }

    IDXGIFactory6* factory6 = nullptr;
    const bool haveFactory6 =
        SUCCEEDED(factory1->QueryInterface(__uuidof(IDXGIFactory6),
                                           reinterpret_cast<void**>(&factory6)))
        && factory6 != nullptr;
    trace << QStringLiteral("factory6=%1").arg(haveFactory6 ? 1 : 0);

    for (UINT index = 0; index < 64; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        HRESULT hr = E_FAIL;
        if (haveFactory6) {
            hr = factory6->EnumAdapterByGpuPreference(
                index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                __uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&adapter));
        } else {
            hr = factory1->EnumAdapters1(index, &adapter);
        }
        if (hr == DXGI_ERROR_NOT_FOUND || FAILED(hr) || adapter == nullptr) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            GpuAdapterInfo info;
            info.name = QString::fromWCharArray(desc.Description);
            info.vendorId = desc.VendorId;
            info.deviceId = desc.DeviceId;
            info.luid.high = static_cast<uint32_t>(desc.AdapterLuid.HighPart);
            info.luid.low = static_cast<uint32_t>(desc.AdapterLuid.LowPart);
            info.luid.valid = true;
            info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
            info.sharedSystemMemory = desc.SharedSystemMemory;
            info.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

            if (info.software) {
                trace << QStringLiteral("skip[%1]=software:%2").arg(index).arg(info.name);
            } else {
                result.append(info);
                trace << QStringLiteral("hw[%1]=%2(luid=%3,vram_mb=%4)")
                             .arg(index)
                             .arg(info.name)
                             .arg(info.luid.toString())
                             .arg(info.dedicatedVideoMemory / (1024ull * 1024ull));
            }
        }
        adapter->Release();
    }

    if (factory6 != nullptr) {
        factory6->Release();
    }
    factory1->Release();

    if (diagnostic != nullptr) {
        *diagnostic = trace.join(QStringLiteral("; "));
    }
    return result;
#else
    if (diagnostic != nullptr) {
        *diagnostic = QStringLiteral("non_windows");
    }
    return {};
#endif
}

std::optional<GpuAdapterLuid> defaultAdapterLuid()
{
#ifdef Q_OS_WIN
    IDXGIFactory1* factory1 = nullptr;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory1)))
        || factory1 == nullptr) {
        return std::nullopt;
    }
    std::optional<GpuAdapterLuid> result;
    IDXGIAdapter1* adapter = nullptr;
    // Default DXGI order (NOT the high-performance order) — adapter 0 is what
    // Qt's D3D11 RHI selects when no adapter LUID is supplied.
    if (SUCCEEDED(factory1->EnumAdapters1(0, &adapter)) && adapter != nullptr) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            GpuAdapterLuid luid;
            luid.high = static_cast<uint32_t>(desc.AdapterLuid.HighPart);
            luid.low = static_cast<uint32_t>(desc.AdapterLuid.LowPart);
            luid.valid = true;
            result = luid;
        }
        adapter->Release();
    }
    factory1->Release();
    return result;
#else
    return std::nullopt;
#endif
}

namespace {

QString cliFlagValue(const QStringList& args, const QString& flag)
{
    const QString flagEq = flag + QStringLiteral("=");
    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args.at(i);
        if (arg.startsWith(flagEq)) {
            return arg.mid(flagEq.size());
        }
        if (arg == flag && (i + 1) < args.size()) {
            return args.at(i + 1);
        }
    }
    return QString();
}

}  // namespace

GpuPolicyRequest parseGpuPolicyRequest(const QStringList& args)
{
    GpuPolicyRequest request;
    request.source = QStringLiteral("default");

    // Policy value: CLI wins over env.
    QString policyRaw;
    QString policySource;
    const QString cliPolicy = cliFlagValue(args, QStringLiteral("--gpu-policy"));
    if (!cliPolicy.trimmed().isEmpty()) {
        policyRaw = cliPolicy.trimmed();
        policySource = QStringLiteral("cli:--gpu-policy");
    } else {
        const QString envPolicy = miacode::debug_options::gpuPolicyRequestRaw();
        if (!envPolicy.isEmpty()) {
            policyRaw = envPolicy;
            policySource = QStringLiteral("env:MIACODE_GPU_POLICY");
        }
    }

    // Adapter LUID override: CLI wins over env.
    QString luidRaw;
    QString luidSource;
    const QString cliLuid = cliFlagValue(args, QStringLiteral("--gpu-adapter-luid"));
    if (!cliLuid.trimmed().isEmpty()) {
        luidRaw = cliLuid.trimmed();
        luidSource = QStringLiteral("cli:--gpu-adapter-luid");
    } else {
        const QString envLuid = miacode::debug_options::gpuAdapterLuidRaw();
        if (!envLuid.isEmpty()) {
            luidRaw = envLuid;
            luidSource = QStringLiteral("env:MIACODE_GPU_ADAPTER_LUID");
        }
    }

    if (!luidRaw.isEmpty()) {
        request.explicitLuid = parseAdapterLuid(luidRaw);
    }

    if (!policyRaw.isEmpty()) {
        if (const std::optional<GpuPolicyKind> parsed = parseGpuPolicyKind(policyRaw)) {
            request.kind = *parsed;
            request.source = policySource;
        } else {
            // Unrecognized value — stay on the default but record why.
            request.source = QStringLiteral("default(invalid_policy='%1')").arg(policyRaw);
        }
    } else if (!luidRaw.isEmpty()) {
        // A LUID with no explicit policy implies the adapter_luid override.
        request.kind = GpuPolicyKind::AdapterLuid;
        request.source = luidSource;
    }

    return request;
}

ResolvedGpuPolicy resolveGpuPolicy(const GpuPolicyRequest& request)
{
    ResolvedGpuPolicy resolved;
    resolved.request = request;

    switch (request.kind) {
    case GpuPolicyKind::PlatformDefault:
        resolved.resolvedKind = GpuPolicyKind::PlatformDefault;
        return resolved;
    case GpuPolicyKind::Software:
        resolved.resolvedKind = GpuPolicyKind::Software;
        return resolved;
    case GpuPolicyKind::AutoHighPerformance: {
        const QList<GpuAdapterInfo> adapters = enumerateHighPerformanceAdapters();
        if (!adapters.isEmpty()) {
            resolved.resolvedKind = GpuPolicyKind::AutoHighPerformance;
            resolved.adapterLuid = adapters.first().luid;
            resolved.adapterName = adapters.first().name;
        } else {
            resolved.resolvedKind = GpuPolicyKind::PlatformDefault;
            resolved.fallbackReason = QStringLiteral("no_hardware_adapter_enumerated");
        }
        return resolved;
    }
    case GpuPolicyKind::AdapterLuid: {
        if (!request.explicitLuid.has_value() || !request.explicitLuid->valid) {
            resolved.resolvedKind = GpuPolicyKind::PlatformDefault;
            resolved.fallbackReason = QStringLiteral("adapter_luid_missing_or_invalid");
            return resolved;
        }
        const QList<GpuAdapterInfo> adapters = enumerateHighPerformanceAdapters();
        for (const GpuAdapterInfo& adapter : adapters) {
            if (adapter.luid.equals(*request.explicitLuid)) {
                resolved.resolvedKind = GpuPolicyKind::AdapterLuid;
                resolved.adapterLuid = adapter.luid;
                resolved.adapterName = adapter.name;
                return resolved;
            }
        }
        // Illegal / unknown LUID must not block startup AND must not bind: the
        // requested LUID stays recorded in resolved.request.explicitLuid (it is
        // logged), but resolved.adapterLuid is left EMPTY so nothing downstream
        // binds to a non-existent adapter. Resolve to the platform default.
        resolved.resolvedKind = GpuPolicyKind::PlatformDefault;
        resolved.fallbackReason = QStringLiteral("adapter_luid_not_found_among_hw_adapters");
        return resolved;
    }
    }
    return resolved;
}

const ResolvedGpuPolicy& resolveGpuPolicyOnce()
{
    static const ResolvedGpuPolicy resolved =
        resolveGpuPolicy(parseGpuPolicyRequest(QCoreApplication::arguments()));
    return resolved;
}

void logResolvedGpuPolicy(const ResolvedGpuPolicy& resolved, const QString& processRole)
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("startup/gpu_policy"),
        QStringLiteral(
            "process_role=%1 requested_policy=%2 request_source=%3 requested_luid=%4 "
            "resolved_policy=%5 resolved_adapter=\"%6\" resolved_luid=%7 fallback_reason=%8 "
            "note=diagnostic_skeleton_no_device_binding_yet")
            .arg(processRole)
            .arg(gpuPolicyKindName(resolved.request.kind))
            .arg(resolved.request.source)
            .arg(resolved.request.explicitLuid ? resolved.request.explicitLuid->toString()
                                               : QStringLiteral("(none)"))
            .arg(gpuPolicyKindName(resolved.resolvedKind))
            .arg(resolved.adapterName.isEmpty() ? QStringLiteral("(none)") : resolved.adapterName)
            .arg(resolved.adapterLuid ? resolved.adapterLuid->toString() : QStringLiteral("(none)"))
            .arg(resolved.fallbackReason.isEmpty() ? QStringLiteral("(none)")
                                                   : resolved.fallbackReason));
}

void applyGpuPolicyToChildEnvironment(QProcessEnvironment& environment,
                                      const ResolvedGpuPolicy& resolved)
{
    // Forward the RAW request (not the resolved LUID) so the worker resolves
    // against its own enumeration and logs a matching request. The worker runs
    // on the same machine, so it resolves to the same adapter. Only propagate
    // when the request differs from the plain default (AutoHighPerformance, no
    // LUID) — a default child keeps its own defaults.
    if (resolved.request.kind == GpuPolicyKind::AutoHighPerformance
        && !resolved.request.explicitLuid.has_value()) {
        return;
    }
    environment.insert(QStringLiteral("MIACODE_GPU_POLICY"),
                       gpuPolicyKindName(resolved.request.kind));
    if (resolved.request.explicitLuid.has_value() && resolved.request.explicitLuid->valid) {
        environment.insert(QStringLiteral("MIACODE_GPU_ADAPTER_LUID"),
                           resolved.request.explicitLuid->toString());
    }
}

}  // namespace miacode::gpu
