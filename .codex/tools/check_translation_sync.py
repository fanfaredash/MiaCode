#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOT = REPO_ROOT / ".codex" / "skills" / "miacode-dev-guide"
TRANSLATION_ROOT = REPO_ROOT / ".codex" / "i18n" / "zh-CN" / "miacode-dev-guide"
SOURCE_MARKER_RE = re.compile(r"^<!--\s*translation-source:\s*(.*?)\s*-->\s*$")
HASH_MARKER_RE = re.compile(r"^<!--\s*translation-source-hash:\s*([0-9a-fA-F]+|pending)\s*-->\s*$")


def sha256_text(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_files() -> list[Path]:
    return sorted(path for path in SOURCE_ROOT.rglob("*.md") if path.is_file())


def parse_header_markers(text: str) -> tuple[str | None, str | None]:
    source = None
    source_hash = None
    for line in text.splitlines()[:8]:
        source_match = SOURCE_MARKER_RE.match(line)
        if source_match:
            source = source_match.group(1).strip()
        hash_match = HASH_MARKER_RE.match(line)
        if hash_match:
            source_hash = hash_match.group(1).strip().lower()
    return source, source_hash


def ensure_markers(text: str, source_rel: str, source_hash: str) -> str:
    lines = text.splitlines()
    source_line = f"<!-- translation-source: {source_rel} -->"
    hash_line = f"<!-- translation-source-hash: {source_hash} -->"

    source_index = None
    hash_index = None
    for index, line in enumerate(lines[:8]):
        if SOURCE_MARKER_RE.match(line):
            source_index = index
        if HASH_MARKER_RE.match(line):
            hash_index = index

    if source_index is not None:
        lines[source_index] = source_line
    else:
        lines.insert(0, source_line)
        if hash_index is not None:
            hash_index += 1

    if hash_index is not None:
        lines[hash_index] = hash_line
    else:
        insert_at = 1 if source_index is None else source_index + 1
        lines.insert(insert_at, hash_line)

    return "\n".join(lines) + "\n"


def check_translation_file(source_path: Path, stamp: bool) -> tuple[str, str]:
    rel_path = source_path.relative_to(SOURCE_ROOT)
    translation_path = TRANSLATION_ROOT / rel_path
    source_rel = source_path.relative_to(REPO_ROOT).as_posix()
    source_hash = sha256_text(source_path)

    if not translation_path.exists():
        return "missing", rel_path.as_posix()

    text = translation_path.read_text(encoding="utf-8")
    recorded_source, recorded_hash = parse_header_markers(text)
    expected_source = source_rel
    status = "ok"
    if recorded_source != expected_source:
        status = "source-mismatch"
    elif recorded_hash != source_hash:
        status = "stale"

    if stamp:
        translation_path.write_text(
            ensure_markers(text, expected_source, source_hash),
            encoding="utf-8",
        )
        status = "ok"

    return status, rel_path.as_posix()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check or stamp the Chinese mirror for miacode-dev-guide.")
    parser.add_argument("--stamp", action="store_true", help="Rewrite translation source hashes to the current source hash.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    problems = 0
    for source_path in source_files():
        status, rel_path = check_translation_file(source_path, stamp=args.stamp)
        print(f"{status:16} {rel_path}")
        if status != "ok":
            problems += 1

    if problems and not args.stamp:
        print("\nTranslation mirror is out of sync.")
        return 1

    if args.stamp:
        print("\nStamped translation metadata.")
    else:
        print("\nTranslation mirror is in sync.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
