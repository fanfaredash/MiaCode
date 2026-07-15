#!/usr/bin/env python3
"""Expand an outline PNG canvas from the external 980 layout to MiaCode's 1080 layout.

External outline assets are commonly authored on a 980x980 transparent canvas,
while MiaCode maps custom outlines as 1080x1080 playfield overlays. This tool
keeps the original pixels unchanged, centers them, and expands the transparent
canvas by the fixed 1080 / 980 ratio.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

SOURCE_CANVAS_SIZE = 980
TARGET_CANVAS_SIZE = 1080


@dataclass(frozen=True)
class ImageMetrics:
    path: Path
    width: int
    height: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Expand a transparent outline PNG canvas by the fixed 980:1080 ratio "
            "without scaling the visible pixels."
        )
    )
    parser.add_argument("input", type=Path, help="Input outline PNG, usually authored on a 980x980 canvas.")
    parser.add_argument("-o", "--output", type=Path, required=True, help="Output PNG path.")
    return parser.parse_args()


def load_rgba(path: Path) -> Image.Image:
    if not path.is_file():
        raise FileNotFoundError(path)
    return Image.open(path).convert("RGBA")


def expanded_extent(extent: int) -> int:
    if extent <= 0:
        raise ValueError(f"image extent must be positive, got {extent}")
    return max(extent, (extent * TARGET_CANVAS_SIZE + SOURCE_CANVAS_SIZE // 2) // SOURCE_CANVAS_SIZE)


def expand_canvas(image: Image.Image) -> Image.Image:
    output_width = expanded_extent(image.width)
    output_height = expanded_extent(image.height)
    offset_x = (output_width - image.width) // 2
    offset_y = (output_height - image.height) // 2

    expanded = Image.new("RGBA", (output_width, output_height), (0, 0, 0, 0))
    expanded.paste(image, (offset_x, offset_y))
    return expanded


def format_metrics(label: str, metrics: ImageMetrics) -> str:
    return f"{label}: path={metrics.path}\n  size={metrics.width}x{metrics.height}"


def main() -> int:
    args = parse_args()
    input_path = args.input.resolve()
    output_path = args.output.resolve()

    image = load_rgba(input_path)
    expanded = expand_canvas(image)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    expanded.save(output_path)

    print(format_metrics("input", ImageMetrics(input_path, image.width, image.height)))
    print()
    print(format_metrics("output", ImageMetrics(output_path, expanded.width, expanded.height)))
    print(f"  canvas_ratio={SOURCE_CANVAS_SIZE}:{TARGET_CANVAS_SIZE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
