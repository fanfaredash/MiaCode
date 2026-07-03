// P2 — high-performance GPU hint symbols.
//
// On hybrid-graphics laptops (Intel iGPU + NVIDIA/AMD dGPU) the vendor driver
// shims look for these specially-named, exported globals in the running
// executable's export table and, when present and non-zero, prefer the
// discrete GPU for the process. Compiled into BOTH MiaCode.exe (the real
// renderer / export worker) and MiaCodeLauncher.exe so the preference is
// declared regardless of which binary the OS inspects.
//
// Caveats (see the plan's P2 risks): this is a process-level PREFERENCE, not a
// precise adapter binding. It only raises the probability of the dGPU being
// selected; Windows Graphics Settings, the vendor control panel, power policy,
// or an external-monitor topology can still override it. Precise adapter
// selection is P3+ (DXGI high-performance enumeration + LUID).
#if defined(_WIN32)
#include <windows.h>

extern "C" {
// NVIDIA Optimus: any non-zero value requests the high-performance GPU.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
// AMD PowerXpress / Enduro: 1 requests the high-performance GPU.
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
