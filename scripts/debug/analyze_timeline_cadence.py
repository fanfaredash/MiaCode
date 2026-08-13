#!/usr/bin/env python3
"""Measure timeline playback smoothness from a --debug run.

Reads the per-frame `content_transform_update scroll=<int>` lines that
TimelineQuickItem emits on the render thread, and reports how many delivered frames
were drawn at the WRONG position rather than how many frames were dropped. Those are
different failures and only the first one reads as judder:

  - a frame delivered late  -> the step scales with the gap; position was still right
  - a frame delivered on time with the wrong step -> the sample was taken at a moment
    that had drifted away from the frame it landed in. This is the judder.

Capture with:
    MIACODE_TIMELINE_HOTPATH_DIAG=1 <MiaCode binary> --debug
then play a chart with timeline follow on, and point this at the run's
miacode_runtime_debug.log.

Usage:
    python3 scripts/analyze_timeline_cadence.py <miacode_runtime_debug.log>
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from datetime import datetime

# The scroll is sub-pixel, so this MUST accept a fraction. Matching only \d+ silently
# truncates 33742.375 to 33742 and makes a smooth run look exactly like the old quantised
# one — i.e. it would hide the very thing this script exists to measure.
LINE_RE = re.compile(r"^(\S+) .*content_transform_update scroll=(-?\d+(?:\.\d+)?)")
# A present slower than this is treated as a late/dropped frame rather than a
# mis-positioned one, so the two failure modes stay separated in the report.
ON_TIME_MS = 25.0
# A run has to be long enough for the fitted rate to mean anything.
MIN_RUN_FRAMES = 120
# An integer scroll can never be closer than half a pixel to the true position, so
# that is the bar for "right". Do NOT score against a hardcoded vsync-derived ideal:
# being off by even 0.001 px flips which integers look acceptable and silently
# inflates the score (16.67 vs 16.667 turns an ideal of 4.000 into 4.0008 and starts
# accepting 5px steps as correct).
POSITION_TOLERANCE_PX = 0.5 + 1e-9


def parse(path: str) -> list[tuple[float, float]]:
    rows: list[tuple[datetime, float]] = []
    with open(path, errors="replace") as handle:
        for line in handle:
            match = LINE_RE.match(line)
            if match:
                stamp = datetime.strptime(match.group(1), "%Y-%m-%dT%H:%M:%S.%fZ")
                rows.append((stamp, float(match.group(2))))
    if not rows:
        return []
    origin = rows[0][0]
    return [((s - origin).total_seconds(), v) for s, v in rows]


def split_runs(points: list[tuple[float, float]]) -> list[list[tuple[float, float]]]:
    """Split on playback stops: a long gap, or the scroll jumping (a seek)."""
    runs: list[list[tuple[float, float]]] = []
    current = [points[0]]
    for prev, nxt in zip(points, points[1:]):
        if nxt[0] - prev[0] > 0.2 or abs(nxt[1] - prev[1]) > 200:
            if len(current) >= MIN_RUN_FRAMES:
                runs.append(current)
            current = [nxt]
        else:
            current.append(nxt)
    if len(current) >= MIN_RUN_FRAMES:
        runs.append(current)
    return runs


def report(index: int, run: list[tuple[float, float]]) -> None:
    duration = run[-1][0] - run[0][0]
    span = run[-1][1] - run[0][1]
    if duration <= 0:
        return
    pps = span / duration

    gaps_ms = [(tb - ta) * 1000.0 for (ta, _), (tb, _) in zip(run, run[1:])]
    on_time_gaps = sorted(g for g in gaps_ms if g < ON_TIME_MS)
    if not on_time_gaps:
        print(f"--- run {index}: no on-time frames to score ---")
        return
    # Measured, not assumed: the display may not be 60.000Hz and the log stamps are
    # millisecond-resolution.
    frame_ms = on_time_gaps[len(on_time_gaps) // 2]
    ideal = pps * frame_ms / 1000.0

    steps: list[float] = []
    late = 0
    lost_vsyncs = 0
    for gap_ms, ((_, sa), (_, sb)) in zip(gaps_ms, zip(run, run[1:])):
        if gap_ms >= ON_TIME_MS:
            late += 1
            lost_vsyncs += max(0, round(gap_ms / frame_ms) - 1)
        else:
            steps.append(sb - sa)

    print(f"--- run {index}: {duration:.1f}s, {len(run)} frames, "
          f"{len(run) / duration:.1f} fps ---")
    print(f"    scroll {pps:.1f} px/s, frame {frame_ms:.1f} ms "
          f"-> ideal {ideal:.2f} px/frame (zoom ~{pps / 120:.2f})")
    if not steps:
        print("    no on-time frames to score")
        return

    quantised = all(float(s).is_integer() for s in steps)
    errors = sorted(abs(s - ideal) for s in steps)
    mean_err = sum(errors) / len(errors)
    p95 = errors[int(len(errors) * 0.95)]
    rms = (sum(e * e for e in errors) / len(errors)) ** 0.5

    print(f"    scroll is {'WHOLE-PIXEL (pre-sub-pixel build)' if quantised else 'sub-pixel'}")
    if quantised:
        # Only meaningful while the scroll is an integer; the histogram is the story there.
        histogram = Counter(int(s) for s in steps)
        print("    step histogram: "
              + "  ".join(f"{k}px:{v}" for k, v in sorted(histogram.items())))
    print(f"    per-frame velocity error: mean {mean_err:.3f} px, "
          f"p95 {p95:.3f} px, rms {rms:.3f} px, max {errors[-1]:.3f} px")
    print(f"      as a share of one step: mean {mean_err / ideal * 100:.1f}%, "
          f"p95 {p95 / ideal * 100:.1f}%")
    # Kept so sub-pixel runs stay directly comparable to whole-pixel captures: half a pixel
    # was the best an integer scroll could ever do, so this is the old "mis-positioned" number.
    over = sum(1 for e in errors if e > POSITION_TOLERANCE_PX)
    print(f"    frames off by >0.5px (old quantisation floor): "
          f"{over}/{len(steps)} = {over / len(steps) * 100:.1f}%")
    print(f"    late/dropped presents: {late} "
          f"({late / max(1, len(run) - 1) * 100:.2f}%), {lost_vsyncs} lost vsyncs")


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__)
        return 2
    points = parse(argv[1])
    if not points:
        print("No content_transform_update lines found. Was the run captured with "
              "MIACODE_TIMELINE_HOTPATH_DIAG=1 and --debug, with playback started?")
        return 1
    runs = split_runs(points)
    print(f"{len(points)} scroll samples, {len(runs)} playback run(s) "
          f"of >={MIN_RUN_FRAMES} frames\n")
    if not runs:
        print("No run was long enough to score; play for a few seconds without seeking.")
        return 1
    for index, run in enumerate(runs, start=1):
        report(index, run)
        print()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
