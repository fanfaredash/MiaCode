#!/usr/bin/env python3
"""
Run MiaCode CLI export and duplicate-frame analysis in one pipeline.

This script:
1. calls `MiaCode.exe --export-video ...`
2. enables export-side repeat diagnostics
3. runs `analyze_video_duplicate_frames.py`
4. prints key repeat diagnostics from export log
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Export video and analyze duplicate frames.")
    parser.add_argument("--miacode-exe", default=r"build\Debug\MiaCode.exe")
    parser.add_argument("--chart", default=r"D:\Files\les plumes")
    parser.add_argument("--difficulty", default="ESY")
    parser.add_argument("--start", type=float, default=0.0)
    parser.add_argument("--duration", type=float, default=4.0)
    parser.add_argument("--resolution", type=int, default=512)
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--crop-bottom", type=int, default=120)
    parser.add_argument(
        "--output-video",
        default=r"D:\Files\les plumes\maidata_ESY_0_4_cli.mp4",
    )
    parser.add_argument(
        "--output-csv",
        default=r"D:\Files\les plumes\maidata_ESY_0_4_dup_report.csv",
    )
    parser.add_argument(
        "--export-log",
        default=r"D:\Files\les plumes\esy_0_4_export.log",
    )
    parser.add_argument("--diag-max-lines", type=int, default=800)
    return parser.parse_args()


def run_export(args: argparse.Namespace) -> None:
    exe_path = Path(args.miacode_exe)
    if not exe_path.exists():
        raise FileNotFoundError(f"MiaCode executable not found: {exe_path}")

    export_log = Path(args.export_log)
    export_log.parent.mkdir(parents=True, exist_ok=True)
    if export_log.exists():
        export_log.unlink()

    env = os.environ.copy()
    env["MIACODE_EXPORT_LOG_PATH"] = str(export_log)
    env["MIACODE_EXPORT_DIAG_REPEAT"] = "1"
    env["MIACODE_EXPORT_DIAG_OBJECT_HASH"] = "1"
    env["MIACODE_EXPORT_DIAG_OBJECT_TRACE"] = "1"
    env["MIACODE_EXPORT_DIAG_MAX_LINES"] = str(max(0, args.diag_max_lines))

    cli_args = [
        str(exe_path),
        "--debug",
        "--export-video",
        "--chart",
        args.chart,
        "--difficulty",
        args.difficulty,
        "--resolution",
        str(args.resolution),
        "--fps",
        str(args.fps),
        "--start",
        f"{args.start:.6f}",
        "--duration",
        f"{args.duration:.6f}",
        "--output",
        args.output_video,
    ]
    print("[export] running:", " ".join(cli_args))
    proc = subprocess.run(cli_args, capture_output=True, text=True, env=env)
    if proc.stdout.strip():
        print("[export] stdout:")
        print(proc.stdout.strip())
    if proc.stderr.strip():
        print("[export] stderr:")
        print(proc.stderr.strip())
    if proc.returncode != 0:
        raise RuntimeError(f"Export failed with exit code {proc.returncode}")


def run_analyze(args: argparse.Namespace) -> None:
    analyze_script = Path(__file__).with_name("analyze_video_duplicate_frames.py")
    if not analyze_script.exists():
        raise FileNotFoundError(f"Analyzer script not found: {analyze_script}")

    cmd = [
        sys.executable,
        str(analyze_script),
        "--video",
        args.output_video,
        "--crop-bottom",
        str(args.crop_bottom),
        "--output-csv",
        args.output_csv,
    ]
    print("[analyze] running:", " ".join(cmd))
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.stdout.strip():
        print("[analyze] stdout:")
        print(proc.stdout.strip())
    if proc.stderr.strip():
        print("[analyze] stderr:")
        print(proc.stderr.strip())
    if proc.returncode != 0:
        raise RuntimeError(f"Analyzer failed with exit code {proc.returncode}")


def parse_export_repeat_diagnostics(export_log_path: Path) -> None:
    if not export_log_path.exists():
        print("[diag] export log missing:", export_log_path)
        return

    object_summary_line = None
    object_repeat_details: list[str] = []
    object_trace_details: list[str] = []
    object_trace_summary = None
    raw_summary_line = None
    with export_log_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if "object_repeat_summary" in line:
                object_summary_line = line.strip()
            elif "object_repeat_detail" in line:
                object_repeat_details.append(line.strip())
            elif "object_frame_trace" in line:
                object_trace_details.append(line.strip())
            elif "object_trace_summary" in line:
                object_trace_summary = line.strip()
            elif "raw_repeat_summary" in line:
                raw_summary_line = line.strip()

    print("[diag] object repeat summary:")
    if object_summary_line is None:
        print("  (none)")
    else:
        print(" ", object_summary_line)

    if not object_repeat_details:
        print("[diag] object repeat detail: none")
    else:
        print(f"[diag] object repeat detail lines: {len(object_repeat_details)}")
        pattern = re.compile(
            r"frame=(\d+)\s+t=([0-9.+-]+)\s+.*pixels=(\d+)\s+core=(\d+)\s+fx=(\d+)\s+(.*)$"
        )
        print("[diag] first 20 detail lines:")
        for line in object_repeat_details[:20]:
            m = pattern.search(line)
            if m is None:
                print(" ", line)
                continue
            frame = int(m.group(1))
            second = float(m.group(2))
            pixels = int(m.group(3))
            core = int(m.group(4))
            fx = int(m.group(5))
            payload = m.group(6)
            print(
                f"  frame={frame:4d} t={second:7.3f}s pixels={pixels:6d} "
                f"core={core} fx={fx} {payload}"
            )

    if raw_summary_line is not None:
        print("[diag] raw repeat summary (aux):")
        print(" ", raw_summary_line)

    if object_trace_summary is not None:
        print("[diag] object trace summary:")
        print(" ", object_trace_summary)
    if not object_trace_details:
        print("[diag] object frame trace: none")
    else:
        print(f"[diag] object frame trace lines: {len(object_trace_details)}")
        print("[diag] first 10 object frame traces:")
        for line in object_trace_details[:10]:
            print(" ", line)


def main() -> int:
    args = parse_args()
    run_export(args)
    run_analyze(args)
    parse_export_repeat_diagnostics(Path(args.export_log))
    print("[done] video:", args.output_video)
    print("[done] csv:", args.output_csv)
    print("[done] log:", args.export_log)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
