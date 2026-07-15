#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

class QProcessEnvironment;

// P3 — internal high-performance GPU device policy skeleton.
//
// This is a diagnostic / plumbing layer only. It resolves a "which adapter
// should MiaCode prefer" request (default = the DXGI high-performance hardware
// adapter, with hidden CLI/env overrides for A/B testing) into a concrete
// adapter LUID and logs it, and can serialize the request for the export
// worker process. It does NOT yet bind any Qt Quick / D3D11 device to the
// resolved adapter — that is P4 (GUI) / P5 (export). Enumeration is read-only
// (no device creation, no UMD driver load) so it is safe to run at startup.
namespace miacode::gpu {

enum class GpuPolicyKind {
    AutoHighPerformance,  // default: prefer the DXGI HIGH_PERFORMANCE hardware adapter
    PlatformDefault,      // fallback: let Qt / the OS pick the RHI device
    AdapterLuid,          // hidden diagnostic: force a specific adapter by LUID
    Software,             // diagnostic / compatibility fallback
};

QString gpuPolicyKindName(GpuPolicyKind kind);
std::optional<GpuPolicyKind> parseGpuPolicyKind(const QString& raw);

struct GpuAdapterLuid {
    uint32_t high = 0;  // LUID.HighPart bits
    uint32_t low = 0;   // LUID.LowPart bits
    bool valid = false;

    QString toString() const;  // "0x<high>:0x<low>", or "(none)" when invalid
    bool equals(const GpuAdapterLuid& other) const
    {
        return valid && other.valid && high == other.high && low == other.low;
    }
};

// Parse "<high>:<low>" (each hex with optional 0x prefix, or decimal).
std::optional<GpuAdapterLuid> parseAdapterLuid(const QString& raw);

struct GpuAdapterInfo {
    QString name;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    GpuAdapterLuid luid;
    uint64_t dedicatedVideoMemory = 0;
    uint64_t sharedSystemMemory = 0;
    bool software = false;
};

// Raw request parsed from argv + environment, before adapter enumeration.
struct GpuPolicyRequest {
    GpuPolicyKind kind = GpuPolicyKind::AutoHighPerformance;
    std::optional<GpuAdapterLuid> explicitLuid;  // only meaningful for AdapterLuid
    QString source;  // provenance, e.g. "default" / "cli:--gpu-policy" / "env:MIACODE_GPU_POLICY"
};

// Resolved policy after (Windows) high-performance adapter enumeration.
struct ResolvedGpuPolicy {
    GpuPolicyRequest request;
    GpuPolicyKind resolvedKind = GpuPolicyKind::PlatformDefault;
    std::optional<GpuAdapterLuid> adapterLuid;  // the chosen adapter, if any
    QString adapterName;
    QString fallbackReason;  // empty when the request resolved without fallback
};

// Parse the request from argv + environment. CLI (`--gpu-policy=`,
// `--gpu-adapter-luid=`) wins over env (MIACODE_GPU_POLICY,
// MIACODE_GPU_ADAPTER_LUID). Absent everything => AutoHighPerformance.
GpuPolicyRequest parseGpuPolicyRequest(const QStringList& args);

// Enumerate hardware adapters ordered by DXGI HIGH_PERFORMANCE preference.
// Windows-only (empty elsewhere). Software adapters are skipped. `diagnostic`,
// when non-null, receives a human-readable enumeration trace.
QList<GpuAdapterInfo> enumerateHighPerformanceAdapters(QString* diagnostic = nullptr);

// LUID of the DXGI default-order adapter 0 — the adapter Qt's D3D11 RHI would
// pick by default. Windows-only (nullopt elsewhere / on failure). Used to skip
// redundant high-performance binding when the high-perf adapter already IS the
// default (single-GPU machines, or dGPU-is-primary).
std::optional<GpuAdapterLuid> defaultAdapterLuid();

// Resolve a request into a concrete policy, running enumeration as needed.
ResolvedGpuPolicy resolveGpuPolicy(const GpuPolicyRequest& request);

// Process-wide resolved policy, computed once from this process's argv +
// environment and cached. Cheap on subsequent calls.
const ResolvedGpuPolicy& resolveGpuPolicyOnce();

// Log the resolved policy to the runtime startup log (gated on --debug).
// processRole is "gui" / "cli_export" / "export_worker".
void logResolvedGpuPolicy(const ResolvedGpuPolicy& resolved, const QString& processRole);

// Propagate this request into a child process environment (the export worker,
// whose CLI parser is strict about unknown options, so env is more robust than
// argv). Sets MIACODE_GPU_POLICY (+ MIACODE_GPU_ADAPTER_LUID when a LUID was
// requested). No-op for the plain default policy.
void applyGpuPolicyToChildEnvironment(QProcessEnvironment& environment,
                                      const ResolvedGpuPolicy& resolved);

}  // namespace miacode::gpu
