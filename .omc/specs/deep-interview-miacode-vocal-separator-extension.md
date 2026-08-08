# Deep Interview Spec: MiaCode Vocal Separator Extension

## Metadata

- Interview ID: `6e6cb5a4-de48-4c5d-a1fc-f3934220645a`
- Rounds: 5
- Final Ambiguity Score: 8.75%
- Type: brownfield
- Generated: 2026-07-30T14:24:36.0059324Z
- Threshold: 0.20
- Threshold Source: default
- Initial Context Summarized: no
- Status: PASSED / PENDING EXECUTION APPROVAL

## Clarity Breakdown

| Dimension | Score | Weight | Weighted |
|---|---:|---:|---:|
| Goal Clarity | 0.95 | 0.35 | 0.3325 |
| Constraint Clarity | 0.85 | 0.25 | 0.2125 |
| Success Criteria | 0.90 | 0.25 | 0.2250 |
| Context Clarity | 0.95 | 0.15 | 0.1425 |
| **Total Clarity** | | | **0.9125** |
| **Ambiguity** | | | **0.0875** |

## Topology

| Component | Status | Description | Coverage |
|---|---|---|---|
| Separation engine and model delivery | active | Extension launches a companion app that downloads and manages one curated two-stem runtime/model package. | Fixed model version/checksum, CPU baseline, optional GPU later. |
| MiaCode workflow and UI | active | Installing the extension adds a vocal-separation command; it opens a separate companion window. | Default current chart `track.*`, arbitrary audio selection, download/separation progress, cancel, errors. |
| Output and chart integration | active | Generate sidecar vocal and BGM files beside the selected input. | `track_Vocal.wav` and `track_BGM.wav`; source file is never changed. |
| Distribution, licensing, and quality | active | Windows x64 MVP with pinned notices and measurable acceptance checks. | Windows-first; macOS deferred until built/signed/tested on a real Mac. |

## Goal

Create a standalone MiaCode extension project at `C:\Users\kanago\Desktop\MiaCodeVocalSeparatorExtension`. Installing the extension adds a vocal/accompaniment separation command to MiaCode. The command launches a separate Windows x64 companion application that defaults to the current chart folder's `track.*`, permits arbitrary supported audio selection, prepares a pinned local separation runtime and one curated two-stem model on first use, and generates `track_Vocal.wav` plus `track_BGM.wav` beside the selected input without changing the source file.

## Constraints

- Do not modify MiaCode product source code for the MVP.
- Use the existing extension manifest, command, `tools/menu`, workspace-read, and detached-process surfaces.
- The companion application owns file selection, component download, hash verification, extraction, progress, cancellation, error reporting, inference, and cleanup.
- Declare only required extension permissions, including `workspace.read`, `ui.message` and `process.manage`; document why the high-risk process permission is needed.
- Do not use the current host `net.download` for large model/runtime archives because it buffers the full response and caps requests at 60 seconds.
- Windows x64 is the only release-blocking platform for the MVP.
- Keep protocols and paths portable so macOS can be added later, but do not claim macOS support without real-device build, signing, and verification.
- Ship one curated two-stem model only. Do not expose the full UVR model selector.
- Priority order: BGM quality, stability, CPU speed, download size, vocal quality.
- Pin runtime/model version, URL, SHA-256, size, and license metadata in a machine-readable manifest.
- Audio stays local; no source audio upload or remote inference.
- Treat extension installation as a trust boundary and avoid shell-command string construction. Launch an explicit executable with an argument list.

## Non-Goals

- No MiaCode core or extension-host API changes in the MVP.
- No embedded progress panel inside MiaCode.
- No full UVR GUI or complete model catalog.
- No automatic replacement of `track.*`.
- No batch-folder processing in the MVP.
- No macOS release in the Windows MVP milestone.
- No cloud inference.

## Functional Design

1. The extension contributes one command under MiaCode's Tools extension menu.
2. On activation, it obtains the chart folder and available media through the stable workspace API.
3. On command execution, it launches the platform companion executable with the chart folder and resolved default track path as separate arguments.
4. The companion validates the provided default path and otherwise opens with no selection.
5. The companion lets the user select MP3, WAV, FLAC, or OGG input.
6. On first use, it displays component size/license information, downloads the pinned runtime/model with resumable streaming, verifies SHA-256, and atomically promotes verified files into the component store.
7. It performs local two-stem separation and writes temporary outputs first.
8. It validates both outputs, then atomically promotes them to `track_Vocal.wav` and `track_BGM.wav` beside the input.
9. Existing outputs require explicit overwrite confirmation.
10. Cancellation terminates active work, removes temporary files, and leaves existing final files untouched.

## Acceptance Criteria

- [ ] Extension installation and refresh make the separation command appear; disable/remove makes it unavailable.
- [ ] The extension project is self-contained under `C:\Users\kanago\Desktop\MiaCodeVocalSeparatorExtension` and MiaCode source remains unchanged.
- [ ] With an open chart, the companion defaults to the folder's resolved `track.mp3`, `track.wav`, `track.flac`, or `track.ogg` according to MiaCode's candidate order.
- [ ] Without a valid chart track, the companion still opens and permits arbitrary audio selection.
- [ ] MP3, WAV, FLAC, and OGG inputs are accepted.
- [ ] Outputs are stereo 44.1 kHz WAV named `track_Vocal.wav` and `track_BGM.wav` in the selected input directory.
- [ ] Output duration differs from the source by no more than 100 ms.
- [ ] Source audio is byte-for-byte unchanged after success, failure, overwrite refusal, and cancellation.
- [ ] Existing output files are never overwritten without explicit confirmation.
- [ ] Runtime/model download supports retry, verifies pinned SHA-256 before use, and never activates a partial archive.
- [ ] Cancelling download or separation removes temporary artifacts and does not corrupt existing final outputs.
- [ ] UI remains responsive and displays current stage and progress throughout preparation and separation.
- [ ] Errors identify the failed stage and offer a recovery action without exposing raw stack traces as the primary message.
- [ ] On 10 legally usable representative tracks, at least 8 `track_BGM.wav` results are judged directly usable for chart authoring/preview.
- [ ] Windows x64 CPU inference works without requiring users to install Python, CUDA, or UVR manually.
- [ ] All copied code, runtime libraries, FFmpeg components, and model weights have reviewed licenses and shipped notices.

## Assumptions Exposed And Resolved

| Assumption | Challenge | Resolution |
|---|---|---|
| Separation should be built into MiaCode. | Existing extensions can register commands and launch a helper. | Use a pure extension plus companion app; no MiaCode source changes. |
| The large runtime should ship in MiaCode. | Official UVR bundles add roughly 0.5-2.0 GB. | Download pinned components on first use; remain offline afterward. |
| Full UVR model selection is useful. | It increases size, licensing work, QA combinations, and user confusion. | Ship one curated two-stem model in the MVP. |
| Windows and macOS should launch together. | Current environment cannot build/sign/verify macOS. | Deliver Windows x64 first and defer macOS. |
| Extension-host download/progress APIs are sufficient. | Current download buffers the whole body; process spawn is detached and unmanaged. | The companion app owns large downloads, job lifecycle, progress, and cancellation. |

## Technical Context

- Extension specification: `docs/specs/extensions/EXTENSION_SYSTEM_V1.md`.
- Manifest/template precedent: `templates/extensions/hello-world/miacode-extension.json`.
- Command/menu registration: `src/extensions/ExtensionManager.cpp`.
- Current chart folder/media APIs: `src/extensions/EmbeddedExtensionRuntime.cpp` and `src/app/mainwindow/sections/frame/MainWindow.ExtensionHostRequests.cpp`.
- Track candidate order: `src/common/ChartAssetPaths.h` (`track.mp3`, `track.wav`, `track.flac`, `track.ogg`).
- Detached helper launch: `ExtensionManager.cpp::startDetachedProcess`; it returns only started/PID and does not supervise stdout, exit, or cancellation.
- Current large-download limitation: `ExtensionManager.cpp` `net/download` buffers the full body and uses a maximum 60-second timeout.
- Reference implementation and algorithms: `leebufan/Ultimate-Vocal-Remover` / upstream `Anjok07/ultimatevocalremovergui`; copied code and model licensing require separate review.

## Ontology (Key Entities)

| Entity | Type | Key Fields | Relationships |
|---|---|---|---|
| MiaCodeExtension | integration | manifest, permissions, command | launches CompanionApp |
| CompanionApp | local application | platform, version, progress, cancellation | manages runtime and processes input |
| SeparationRuntime | inference engine | backend, version, checksum | loads ModelPackage |
| ModelPackage | downloaded asset | version, size, license, checksum | used by SeparationRuntime |
| AudioInput | user file | path, format | defaults from ChartFolder; produces outputs |
| StemOutput | generated file | vocal path, BGM path, sample format | written beside input; never replaces input |
| ChartFolder | MiaCode context | path, resolved track | provides default AudioInput |

## Ontology Convergence

| Round | Entity Count | New | Changed | Stable | Stability Ratio |
|---:|---:|---:|---:|---:|---:|
| 1 | 7 | 7 | - | - | N/A |
| 2 | 7 | 0 | 0 | 7 | 100% |
| 3 | 7 | 0 | 0 | 7 | 100% |
| 4 | 7 | 0 | 0 | 7 | 100% |
| 5 | 7 | 0 | 0 | 7 | 100% |

## Interview Transcript

### Round 0

**Decision:** Four active components: engine/model delivery, workflow/UI, output integration, and distribution/licensing/quality. Arbitrary audio is allowed, current chart `track.*` is the default, outputs are sidecars, and the source remains unchanged.

### Round 1

**Q:** How should the engine be delivered, and is a separate companion window acceptable?  
**A:** Use a pure MiaCode extension installed separately; launch an independent window; develop it in a desktop folder without modifying MiaCode core.  
**Ambiguity:** 55.25%.

### Round 2

**Q:** Is the priority BGM quality, stability, CPU speed, download size, then vocal quality?  
**A:** Yes.  
**Ambiguity:** 40.50%.

### Round 3

**Q:** Should the MVP support one curated model instead of the full UVR model selector?  
**A:** Yes.  
**Ambiguity:** 30.50%.

### Round 4

**Q:** Must Windows and macOS launch together?  
**A:** Windows first.  
**Ambiguity:** 21.75%.

### Round 5

**Q:** Accept the proposed Windows MVP quality, safety, download, cancellation, and output criteria?  
**A:** Accepted.  
**Ambiguity:** 8.75%.
