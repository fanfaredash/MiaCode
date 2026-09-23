# BASS position-sync probes

Standalone programs that measure, against the bundled BASS / BASSmix binaries, the
semantics the preview SFX scheduler (`src/audio/BassPreviewAudioBackend_EventDrain.cpp`)
depends on. They run without Qt or the app; build one, run it with the default output
device present, and read the printed verdicts. Results on macOS with BASS 2.4.18.3 /
BASSmix 2.4.12 are recorded in `docs/audit/PREVIEW_AUDIO_MASTER_MIXER_STALL_REVIEW_ZH.md`
§7.9; rerun on Windows (BASS 2.4.18 / BASSmix 2.4.13) when a field report suggests the
library behaves differently there.

| Program | Question it answers |
| --- | --- |
| `sync_semantics_probe.cpp` | Does a `POS|MIXTIME|ONETIME` sync at / behind / ahead of the decode cursor fire? Do syncs armed inside a callback fire within the current block? Does a stalled callback break the chain? What is the mix block size? Args: `<mixer_threads> <buffer_seconds>` (production: `4 0`). |
| `long_uptime_probe.cpp` | Are positions and sync targets 64-bit across the 2^32-byte boundary? Does BASS validate far-future sync positions? |
| `voice_retrigger_probe.cpp` | Does the SFX voice graph (memory decode stream → NONSTOP resampler mixer → master) retrigger reliably from the worker thread and from inside a mixer sync callback, after idle, after `stop()`, with other sources attached? |
| `dev_default_probe.cpp` | After which BASS calls does `BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE)` stop being accepted? MiaCode makes that call once per process (`disableBassDefaultDeviceEntry`), at the top of `main()` since `4d7c274e`; before that the preview engine made it lazily, and a waveform decode that initialized device 0 first made it fail until a restart. Arg: one case per fresh process (`fresh`, `init0`, `init0free`, `init0thread`, `enum`, `getdevice`, `plugin`, `setfirst`). macOS 2.4.18.3 result: `init0`, `init0free`, `init0thread` and `enum` fail with err 37; `getdevice` and `plugin` do not. Windows 2.4.18.3 and 2.4.17.0 match on every case the wrapper covers (it has no `getdevice`). Windows testers run the compiler-free wrapper in `../first_play_probe/`. |

## Build

macOS (from the repository root, after a `build-macos` app build so the dylibs exist):

```bash
FW=build-macos/MiaCode.app/Contents/Frameworks
clang++ -std=c++17 -O2 -I third_party/bass/include scripts/debug/bass_sync_probe/sync_semantics_probe.cpp -o /tmp/sync_probe "$FW/libbass.dylib" "$FW/libbassmix.dylib" -Wl,-rpath,"$FW" && /tmp/sync_probe 4 0
```

Windows (Developer PowerShell, x64):

```powershell
cl /std:c++17 /O2 /I third_party\bass\include scripts\debug\bass_sync_probe\sync_semantics_probe.cpp third_party\bass\lib\win64\bass.lib third_party\bass\lib\win64\bassmix.lib /Fe:sync_probe.exe
copy third_party\bass\bin\win64\bass.dll . ; copy third_party\bass\bin\win64\bassmix.dll . ; .\sync_probe.exe 4 0
```

The voice probe plays 120 ms tones at 2% master volume; the others are silent. Run `dev_default_probe` once per case, because the setting it probes is process-global:

```bash
clang++ -std=c++17 -O2 -I third_party/bass/include scripts/debug/bass_sync_probe/dev_default_probe.cpp -o /tmp/dev_default_probe "$FW/libbass.dylib" -Wl,-rpath,"$FW" && (cd "$FW" && for c in fresh init0 init0free init0thread enum getdevice plugin setfirst; do /tmp/dev_default_probe $c; done)
```
