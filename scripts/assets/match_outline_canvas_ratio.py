#!/usr/bin/env python3
"""Pad or crop a transparent outline PNG so its visible-area ratio matches a reference.

MiaCode's built-in outline canvases are 1080x1080 with transparent space around
the visible judge-line art. External outline PNGs are often 980x980 and can sit
too large or too small if used directly. This tool preserves the candidate art
pixels, then adds or removes transparent canvas space around the center so the
candidate's alpha bounds occupy the same canvas proportion as a reference image.
"""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path

from PIL import Image


@dataclass(frozen=True)
class AlphaBounds:
    left: int
    top: int
    right: int
    bottom: int

    @property
    def width(self) -> int:
        return self.right - self.left

    @property
    def height(self) -> int:
        return self.bottom - self.top

    @property
    def center_x(self) -> float:
        return (self.left + self.right) / 2.0

    @property
    def center_y(self) -> float:
        return (self.top + self.bottom) / 2.0


@dataclass(frozen=True)
class ImageMetrics:
    path: Path
    width: int
    height: int
    bounds: AlphaBounds

    @property
    def bounds_width_ratio(self) -> float:
        return self.bounds.width / self.width

    @property
    def bounds_height_ratio(self) -> float:
        return self.bounds.height / self.height


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Pad or crop a transparent outline PNG so its alpha-bounds proportion "
            "matches a reference PNG."
        )
    )
    parser.add_argument("reference", type=Path, help="Reference PNG, usually MiaCode's 1080x1080 outline.")
    parser.add_argument("candidate", type=Path, help="Candidate PNG to adjust, often a 980x980 external outline.")
    parser.add_argument("-o", "--output", type=Path, required=True, help="Output PNG path.")
    parser.add_argument(
        "--alpha-threshold",
        type=int,
        default=1,
        help="Ignore pixels with alpha below this value when measuring visible bounds.",
    )
    parser.add_argument(
        "--force-square",
        action="store_true",
        help="Use one square output extent instead of independent width/height extents.",
    )
    return parser.parse_args()


def load_rgba(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(path)
    return Image.open(path).convert("RGBA")


def alpha_bounds(image: Image.Image, alpha_threshold: int) -> AlphaBounds:
    alpha = image.getchannel("A")
    mask = alpha.point(lambda value: 255 if value >= alpha_threshold else 0)
    bbox = mask.getbbox()
    if bbox is None:
        raise ValueError("image has no visible pixels above the alpha threshold")
    left, top, right, bottom = bbox
    return AlphaBounds(left=left, top=top, right=right, bottom=bottom)


def collect_metrics(path: Path, alpha_threshold: int) -> ImageMetrics:
    image = load_rgba(path)
    return ImageMetrics(path=path, width=image.width, height=image.height, bounds=alpha_bounds(image, alpha_threshold))


def choose_output_extent(subject_extent: int, target_ratio: float) -> int:
    if target_ratio <= 0.0 or target_ratio > 1.0:
        raise ValueError(f"target ratio must be within (0, 1], got {target_ratio!r}")
    raw_extent = subject_extent / target_ratio
    candidates = {max(subject_extent, math.floor(raw_extent)), max(subject_extent, math.ceil(raw_extent))}
    return min(candidates, key=lambda extent: (abs(subject_extent / extent - target_ratio), extent))


def centered_adjusted_image(candidate: Image.Image, bounds: AlphaBounds, output_width: int, output_height: int) -> Image.Image:
    offset_x = round(output_width / 2.0 - bounds.center_x)
    offset_y = round(output_height / 2.0 - bounds.center_y)
    adjusted = Image.new("RGBA", (output_width, output_height), (0, 0, 0, 0))

    src_left = max(0, -offset_x)
    src_top = max(0, -offset_y)
    src_right = min(candidate.width, output_width - offset_x)
    src_bottom = min(candidate.height, output_height - offset_y)
    if src_right <= src_left or src_bottom <= src_top:
        raise ValueError("calculated crop produced no overlapping pixels")

    visible_region = candidate.crop((src_left, src_top, src_right, src_bottom))
    adjusted.paste(visible_region, (max(0, offset_x), max(0, offset_y)))
    return adjusted


def adjust_candidate(
    reference_metrics: ImageMetrics,
    candidate_path: Path,
    output_path: Path,
    alpha_threshold: int,
    force_square: bool,
) -> ImageMetrics:
    candidate = load_rgba(candidate_path)
    candidate_bounds = alpha_bounds(candidate, alpha_threshold)

    output_width = choose_output_extent(candidate_bounds.width, reference_metrics.bounds_width_ratio)
    output_height = choose_output_extent(candidate_bounds.height, reference_metrics.bounds_height_ratio)
    if force_square:
        square_extent = max(output_width, output_height)
        output_width = square_extent
        output_height = square_extent

    adjusted = centered_adjusted_image(candidate, candidate_bounds, output_width, output_height)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    adjusted.save(output_path)

    return ImageMetrics(
        path=output_path,
        width=adjusted.width,
        height=adjusted.height,
        bounds=alpha_bounds(adjusted, alpha_threshold),
    )


def format_metrics(label: str, metrics: ImageMetrics) -> str:
    return (
        f"{label}: path={metrics.path}\n"
        f"  size={metrics.width}x{metrics.height}\n"
        f"  alpha_bounds=({metrics.bounds.left}, {metrics.bounds.top}, {metrics.bounds.right}, {metrics.bounds.bottom})\n"
        f"  alpha_ratio={metrics.bounds_width_ratio:.6f} x {metrics.bounds_height_ratio:.6f}"
    )


def main() -> int:
    args = parse_args()
    alpha_threshold = max(0, min(255, args.alpha_threshold))
    reference_path = args.reference.resolve()
    candidate_path = args.candidate.resolve()
    output_path = args.output.resolve()

    reference_metrics = collect_metrics(reference_path, alpha_threshold)
    candidate_metrics = collect_metrics(candidate_path, alpha_threshold)
    adjusted_metrics = adjust_candidate(
        reference_metrics,
        candidate_path,
        output_path,
        alpha_threshold,
        args.force_square,
    )

    print(format_metrics("reference", reference_metrics))
    print()
    print(format_metrics("candidate", candidate_metrics))
    print()
    print(format_metrics("adjusted", adjusted_metrics))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
