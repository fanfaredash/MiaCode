---
lifecycle: archive-legacy
---

> 历史资料：保留当时的设计与实施背景，不代表当前产品路径。当前入口见 [架构与契约](../specs/ui/CURRENT_ARCHITECTURE_ZH.md) 和 [文档索引](../INDEX.md)。

# Video Export Memory Analysis

Status: Legacy archive.

This file is kept so older discussions about export memory pressure remain reachable after the Qt Quick migration.

It is not the current design document for the active export backend.

Use these files for current behavior:

- `../specs/preview/PREVIEW_RUNTIME_EXPORT_ARCHITECTURE_SPEC.md`
- `../ops/DEBUG_INDEX.md`

Legacy topics covered by the old analysis and still worth remembering:

- frame-sized RGBA buffers dominate export memory
- pipe backpressure can accumulate buffered frames quickly
- offscreen readback strategy strongly affects both peak memory and throughput
- ffmpeg pipe sizing and batching matter as much as renderer-side throughput

Current caveat:

- The active export path no longer uses the removed legacy preview renderer.
- Headless export now runs through the Qt Quick offscreen session and may use direct readback or PBO readback depending on backend support.
