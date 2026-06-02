# FFmpeg decode-only trim toolchain

Produces a **minimal, decode-only FFmpeg** (n7.1, LGPL, shared) for MiaCode's
QtAVPlayer preview backend, replacing the full ~110 MB BtbN DLL set with a
trimmed build (~15–20 MB target) without breaking playback.

This is the build-it-yourself counterpart to `scripts/ensure-windows-ffmpeg-dev.ps1`
(which just downloads the full upstream SDK). It installs into the same place —
`third_party/ffmpeg/windows/dev/` — so the rest of the build (CMake + packaging)
consumes it unchanged.

> **License:** decode-only ⇒ pure **LGPL v2.1+**, same obligations as the FFmpeg
> already shipped. The scripts pass **no** `--enable-gpl` / `--enable-nonfree` and
> never link libx264 — doing so would make MiaCode GPL. Export encoding stays in
> the separate GPL `ffmpeg.exe` (a different process) by design.

## Files

| File | Role |
|---|---|
| `trim-allowlist.psd1` | **Source of truth.** Which decoders/demuxers/parsers/filters/bsfs/hwaccels/protocols to keep. Conservative superset, hand-reviewed. |
| `survey-chart-codecs.ps1` | **Calibration engine.** Probes chart PVs (ffprobe, or `ffmpeg -i` fallback), tallies codecs/containers, writes `ffmpeg-codec-survey.txt`, and — if `trim-allowlist.psd1` is present — reports allowlist gaps. Unicode-correct. Runs standalone for devs (`-ChartRoots ...`) **and** is the engine behind the `.bat`. Finds `ffmpeg.exe`/`ffprobe.exe` next to itself first. |
| `survey-chart-codecs.bat` | **Test-user launcher.** Double-clickable; drives the `.ps1` via PowerShell, so it's **Unicode-safe** — Chinese / Japanese / emoji / bracketed folder names all probe correctly (the old pure-batch version mis-enumerated those). Scans its own folder (or a folder dragged onto it) and writes `ffmpeg-codec-survey.txt` to send back. |
| `build-trimmed-ffmpeg.ps1` | **Build.** Validates the allowlist against `./configure --list-*`, builds (MSYS2/MinGW), makes MSVC import libs, installs into the dev SDK (with backup), and verifies. Has `-PrintPlanOnly`. |

## Handing the survey to a test user

Give the tester **three files in one folder**:

1. `survey-chart-codecs.bat`
2. `survey-chart-codecs.ps1`
3. `ffmpeg.exe` — standalone (no extra DLLs); copy from a MiaCode package's
   `app\ffmpeg\ffmpeg.exe` (or `third_party\ffmpeg\windows\ffmpeg.exe`).

Then either: put that folder **at the root of their charts** and double-click the
`.bat`; **or** keep the three files anywhere and **drag their charts folder onto
the `.bat`**. It writes `ffmpeg-codec-survey.txt` next to the `.bat` — they send
that back. (The `.bat` drives the `.ps1`, so non-English / emoji folder names work.)

## How it avoids 误删 (cutting something a real PV needs)

Trimming by guesswork is the trap: omit one decoder/demuxer/filter a user's PV
needs and that video silently fails. Five guards, layered:

1. **Mandatory baseline never trimmed.** `trim-allowlist.psd1 → Mandatory` keeps
   the playback-critical plumbing that isn't a content codec:
   - QtAVPlayer's 4 filtergraph endpoints `buffer` / `buffersink` / `abuffer` /
     `abuffersink` (resolved by name via `avfilter_get_by_name()` — missing any
     one breaks *all* playback, not one video). Plus `format`/`scale`/`fps`/`aresample`.
   - `protocol=file` (else nothing local opens), the D3D11VA `hwaccel`s, and the
     mp4 `bsf`s (`h264_mp4toannexb`, …).
2. **Data-driven decoder/demuxer set.** Run `survey-chart-codecs.ps1` over a real
   corpus; it tells you exactly which codecs/containers appear and which are not
   yet in the allowlist. Add the gaps before building.
3. **Generous safety margin.** The default `Decoders`/`Demuxers` are a superset of
   common formats (h264/hevc/vp9/av1/… + mov/matroska/…). A spare decoder costs
   tens of KB; a missing one costs a black screen.
4. **Build-time validation gate.** `build-trimmed-ffmpeg.ps1` intersects the
   allowlist with `./configure --list-decoders/--list-filters/…`, **warns** on any
   requested name that doesn't exist (typo / wrong name), and **hard-fails** if the
   4 mandatory endpoint filters didn't survive.
5. **Runtime verify + A/B fallback.** After building it runs the trimmed `ffmpeg`
   to assert the mandatory filters + allowlisted decoders are present, and it
   **backs up the previous full SDK** to `dev.full.bak`. If a PV later fails on the
   trimmed build, A/B against the backup to confirm it's a trim gap, add the codec,
   rebuild. The host's own `preview/stage_media action=video_software_fallback` +
   `video_frame_first` logs surface decode failures in the app.

## Prerequisites

- **MSYS2** at `C:\msys64` (the script installs the MinGW build deps via `pacman`:
  `make diffutils pkgconf git mingw-w64-x86_64-gcc mingw-w64-x86_64-nasm`).
- **VS 2022 BuildTools** (for `dumpbin`/`lib` → MSVC import libs; auto-located, or `-VcvarsPath`).
- Network (clone FFmpeg n7.1). The build tries a **CN-reachable Gitee mirror first**,
  then GitHub, then `git.ffmpeg.org` — each with a retry — so a reset GitHub clone
  (common behind the GFW) falls through automatically. Force a remote with
  `-FfmpegGitUrl 'https://gitee.com/mirrors/ffmpeg.git'`, or clone the `n7.1` tag
  yourself into `build/ffmpeg-trim/FFmpeg` and pass `-SkipSourceFetch`.

## Usage

```powershell
# 1. (Recommended) calibrate the allowlist against real charts:
scripts\ffmpeg-trim\survey-chart-codecs.ps1 -ChartRoots '<local-chart-root>','<chart-root>'
#    → add any reported gaps to trim-allowlist.psd1 (Decoders / Demuxers).

# 2. Review the build plan without building:
scripts\ffmpeg-trim\build-trimmed-ffmpeg.ps1 -PrintPlanOnly

# 3. Build + install into third_party/ffmpeg/windows/dev (~30–60 min):
scripts\ffmpeg-trim\build-trimmed-ffmpeg.ps1

# 4. Rebuild MiaCode against the trimmed SDK + re-verify:
cmake --preset vs2022-qt6
cmake --build build --config Release --target MiaCode
#    then launch + play a few real PVs (or re-run scripts\package-win.ps1 + smoke test).
```

To revert to the full SDK: delete `third_party/ffmpeg/windows/dev`, restore
`dev.full.bak`, or re-run `scripts\ensure-windows-ffmpeg-dev.ps1`.

## Notes / limits

- Windows-only (matches the QtAVPlayer-on-Windows backend). macOS/Linux would
  need their own toolchain + a `dev` layout; not built here.
- FFmpeg's MSVC-vs-MinGW build is finicky; this uses the robust **MinGW build +
  MSVC import-lib generation** route. The DLLs are C-ABI and link fine into the
  MSVC-built MiaCode (same as the BtbN builds we use today). First run on a fresh
  toolchain may surface a missing `pacman` package — the error names it.
- **Self-contained DLLs (`--extra-ldflags=-static`).** A naïve MinGW *shared* build
  leaves the produced `av*.dll` dynamically depending on MinGW runtime DLLs
  (`zlib1.dll`, `libwinpthread-1.dll`, `libgcc_s_*.dll`) that we don't ship next to
  `MiaCode.exe` — the loader then fails with `ERROR_MOD_NOT_FOUND` (126) and **the app
  won't open**. The build links those statically into the DLLs (and `--disable-iconv`
  drops `libiconv-2.dll`), matching the self-contained BtbN SDK. Step 8 asserts this
  with `objdump` and **hard-fails** if any external MinGW runtime dep remains, so a
  regression can't ship a DLL that crashes the app on a clean machine.
- **`dev.full.bak` preserves the ORIGINAL full SDK** across rebuilds: the first build
  backs it up there; later rebuilds discard the previous *trimmed* `dev/` without
  clobbering that baseline. (If it was already overwritten, re-fetch the full SDK via
  `scripts\ensure-windows-ffmpeg-dev.ps1` for an A/B baseline.)
- The trim scripts are saved **UTF-8 with BOM** so Windows PowerShell 5.1 (ANSI/GBK
  default codepage) renders their non-ASCII text correctly instead of mojibake.
- Keep `trim-allowlist.psd1` under review in PRs: it's the contract for "which
  videos can still play."
