---
name: miacode-concurrent-build
description: Repository-specific MiaCode concurrent build policy. Use when building, rebuilding, compiling, validating, or refreshing MiaCode binaries in this repo; when the user mentions build slowness, MSBuild /MP, --parallel, ALL_BUILD, build-devtools, Release builds, desktop shortcut executable, or build output being occupied.
---

# MiaCode Concurrent Build Policy

Use this skill whenever running a local MiaCode build or advising how to build this repo.

## README Build Routes

The README documents two general Windows build routes:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\build-win.ps1
```

and, when Qt is already installed and the FFmpeg runtime/dev SDK must be prepared first:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\ffmpeg\ensure-windows-ffmpeg.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\ffmpeg\ensure-windows-ffmpeg-dev.ps1
cmake --preset vs2022-qt6
cmake --build --preset release
.\build\Release\MiaCode.exe
```

For packaging an existing build, README points to:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build\package-win.ps1 -QtRoot <QtRoot>
```

Use those routes when the user explicitly asks for first-time dependency setup, a normal packaged Windows build, or packaging. For routine Codex verification in this local workspace, use the `build-devtools` developer tree below instead of the README preset/package flow.

## Default Build Rule

Always use concurrent builds for routine local builds and verification. Build the full developer target graph with CMake/MSBuild parallelism:

```powershell
cmake --build "D:\STUDY\Project_Work\MiaCode_dev2\MiaCodeDev\build-devtools" --target ALL_BUILD --config Release --parallel
```

Use this by default when the user asks to build the current project, rebuild the current files, refresh the desktop executable, or verify the app after changes.

## Before Every Build

- Enforce one build at a time for this repository. Before starting any configure, compile,
  test-build, package, or rebuild command, inspect running `cmake`, `MSBuild`, `cl`, and
  `link` process command lines for either
  `D:\STUDY\Project_Work\MiaCode_dev2\MiaCodeDev` or its `build-devtools` tree.
- If a matching build exists, terminate its build-root process tree first. Then terminate
  any orphaned compiler/linker processes that can still be attributed to this repository,
  and re-check until zero matching build processes remain. Do not start the requested
  build while an earlier MiaCode build is still running.
- Never terminate an unrelated repository's build. If a process cannot be attributed to
  MiaCode from its command line or parent process tree, leave it running.
- Check whether `D:\STUDY\Project_Work\MiaCode_dev2\MiaCodeDev\build-devtools\Release\MiaCode.exe` is occupied by a running `MiaCode` process. If it is, directly close that process before building.
- Confirm `C:\Users\kanago\Desktop\MiaCode.exe.lnk` targets `D:\STUDY\Project_Work\MiaCode_dev2\MiaCodeDev\build-devtools\Release\MiaCode.exe` and uses working directory `D:\STUDY\Project_Work\MiaCode_dev2\MiaCodeDev\build-devtools\Release`; update the shortcut if it does not.
- Confirm the executable reached through `C:\Users\kanago\Desktop\MiaCode.exe.lnk` is a fresh build for the current code. If the target exe's `LastWriteTime` is older than the latest commit time or relevant source file updates, rebuild before treating the desktop shortcut as current.
- Treat `D:\STUDY\Project_Work\MiaCode_dev2\MiaCodeDev\build-devtools` as the local developer build tree. Do not switch to README's `build\Release` output unless the user explicitly asks for the preset route.

## If The Build Hits Output Occupancy

- If linking fails because `build-devtools\Release\MiaCode.exe` cannot be opened, directly close the running `MiaCode` process whose `Path` is that executable.
- After closing the occupied process, rerun the same concurrent build command once.

## Build Artifact Cleanup Requires Approval

- Obtain the user's explicit approval immediately before running any command that cleans or
  deletes developer build artifacts. This includes `cmake --build ... --target clean`, MSBuild
  `Clean` / `Rebuild`, deleting files under `build-devtools`, and removing or recreating the
  `build-devtools` directory.
- A request to build, rebuild, fix a compile error, stop a build, or verify the app does **not**
  authorize cleanup. A previous approval does not carry over to a later cleanup operation.
- If incremental build artifacts appear corrupt or locked, first stop attributable MiaCode build
  processes and retry the same incremental build when appropriate. If cleanup is still needed,
  explain the exact paths/target and reason, then wait for approval before proceeding.
- Stopping an active build process is not artifact cleanup and does not require this additional
  approval when the user asks to stop or when the one-build-at-a-time precheck requires it.

## Guardrails

- Use `ALL_BUILD` concurrent builds for normal local verification.
- Never run a clean/rebuild target or delete development build output without the explicit cleanup
  approval required above.
- Do not switch to low-concurrency build commands as a fallback.
- Do not add a fixed outer parallelism such as `--parallel 8` unless the user asks for a specific job count; plain `--parallel` lets CMake/MSBuild choose.
- Keep `--config Release` for routine compile/test/verification work, matching `miacode-dev-guide`.
- For a named helper/spec target, build that exact target with concurrent CMake/MSBuild parallelism.
- For packaging scripts, expect them to run their own prechecks; if the user only needs a fresh desktop shortcut executable, build `MiaCode` or `ALL_BUILD` in `build-devtools` directly instead of packaging.

## Explain The Cause Briefly

When asked why builds are slow or memory-heavy, mention that the Visual Studio generator has two levels of parallelism:

- outer MSBuild project parallelism from `cmake --build --parallel`
- inner compiler parallelism from MSVC `/MP`

Also mention that touching high-fanout MainWindow include files can trigger many translation units to recompile.
