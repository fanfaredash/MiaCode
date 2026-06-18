#!/usr/bin/env python3
"""Subset a large CJK font down to the character set the HUD actually needs.

The bundled default HUD font (see src/core/scene/PreviewHudState.cpp ->
previewHudTimestampFont) only ever renders song titles, note labels and
timestamps, so we do not need the full ~45k-glyph source. This reuses the SAME
character-set recipe established for the intro/cover card fonts in
font_candidates/_subset/do_subset.py:

  * Latin + common punctuation / symbols / arrows / dingbats
  * Greek + Cyrillic (cheap, common in titles)
  * CJK symbols & punctuation, Japanese kana, fullwidth forms
  * Chinese: 通用规范汉字表 (Table of General Standard Chinese Characters, 8105)
    read from font_candidates/_subset/tongyong_guifan.txt
  * Japanese kanji: JIS X 0208 (anything encodable as shift_jis in the CJK range)

Usage:
    python scripts/subset_hud_font.py \
        --input  <Downloads>/XiaolaiMono-Regular.ttf \
        --output assets/fonts/XiaolaiMono-Regular.subset.ttf
"""

from __future__ import annotations

import argparse
import os
import sys

from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)


def _tongyong_txt() -> str:
    # 通用规范汉字表 (Table of General Standard Chinese Characters). Bundled next
    # to this script; falls back to the original copy under font_candidates/.
    local = os.path.join(HERE, "tongyong_guifan.txt")
    if os.path.isfile(local):
        return local
    return os.path.join(REPO_ROOT, "font_candidates", "_subset", "tongyong_guifan.txt")


TONGYONG_TXT = _tongyong_txt()

# Explicit Unicode ranges to always keep (start, end inclusive) -- mirrors the
# established do_subset.py recipe.
EXPLICIT_RANGES = [
    (0x0020, 0x024F),  # Basic Latin + Latin-1 Supplement + Latin Extended-A
    (0x0370, 0x03FF),  # Greek
    (0x0400, 0x052F),  # Cyrillic
    (0x2000, 0x206F),  # General Punctuation (dashes, smart quotes, ellipsis)
    (0x20A0, 0x20BF),  # Currency symbols (incl euro)
    (0x2100, 0x214F),  # Letterlike symbols (TM, No.)
    (0x2150, 0x218F),  # Number forms (Roman numerals, fractions)
    (0x2460, 0x24FF),  # Enclosed alphanumerics (circled numbers)
    (0x2600, 0x26FF),  # Misc symbols (stars, music notes, card suits)
    (0x2700, 0x27BF),  # Dingbats (checkmarks, etc.)
    (0x3000, 0x303F),  # CJK Symbols and Punctuation
    (0x3040, 0x30FF),  # Hiragana + Katakana
    (0x31F0, 0x31FF),  # Katakana phonetic extensions
    (0xFF00, 0xFFEF),  # Halfwidth and Fullwidth Forms
]


def build_unicode_set() -> set[int]:
    uni: set[int] = set()
    for start, end in EXPLICIT_RANGES:
        uni.update(range(start, end + 1))

    # Chinese: 通用规范汉字表 (8105). Same source file as do_subset.py.
    with open(TONGYONG_TXT, encoding="utf-8") as fh:
        ty = fh.read()
    ty_han = {ord(c) for c in ty if ord(c) >= 0x3400}
    uni |= ty_han

    # Japanese kanji: JIS X 0208 (anything encodable in shift_jis, CJK range).
    jis: set[int] = set()
    for cp in range(0x3000, 0xA000):
        try:
            chr(cp).encode("shift_jis")
        except Exception:
            continue
        jis.add(cp)
    uni |= jis

    print(f"requested codepoints: {len(uni)}  "
          f"(通用规范 han {len(ty_han)}, JIS-block {len(jis)})")
    return uni


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", required=True, help="source .ttf/.otf path")
    parser.add_argument("--output", required=True, help="subset output path")
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print(f"error: input font not found: {args.input}", file=sys.stderr)
        return 1
    if not os.path.isfile(TONGYONG_TXT):
        print(f"error: 通用规范汉字表 list not found: {TONGYONG_TXT}", file=sys.stderr)
        return 1

    uni = build_unicode_set()

    font = TTFont(args.input)
    orig_cmap = set(font.getBestCmap().keys())

    opt = Options()
    opt.name_IDs = ["*"]
    opt.name_legacy = True
    opt.glyph_names = False
    opt.layout_features = ["*"]
    opt.notdef_outline = True
    opt.recalc_timestamp = False

    ss = Subsetter(options=opt)
    ss.populate(unicodes=uni)
    ss.subset(font)
    font.save(args.output)

    name = font["name"]
    fam = name.getDebugName(1)
    sub = name.getDebugName(2)
    new = TTFont(args.output)
    kept = len(set(new.getBestCmap().keys()))

    sample = "测试ABC123 棗いつき Привет ★♪ β π № ① 北京 龘齉 ㊙"
    newcmap = set(new.getBestCmap().keys())
    missing = [f"{c}(U+{ord(c):04X})" for c in sample
               if ord(c) not in newcmap and c.strip()]

    osz = os.path.getsize(args.input)
    nsz = os.path.getsize(args.output)
    print(f"family='{fam}'  subfamily='{sub}'")
    print(f"glyphs kept: {kept} codepoints (font orig had {len(orig_cmap)})")
    print(f"size: {osz / 1048576:.2f} MB -> {nsz / 1048576:.2f} MB")
    print(f"sample missing chars: {missing if missing else 'none'}")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
