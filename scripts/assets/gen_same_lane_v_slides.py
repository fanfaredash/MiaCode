#!/usr/bin/env python3
"""Add same-lane v-slide entries (1v1..8v8) to assets/reference/slide_data.json.

The original slide table (merged from the MajdataPlay data dump) covers XvY for
Y != X and Y != opposite(X) only.  XvX ("go to center, come back") is a valid
extension shape this editor wants to accept; Xv(X+4) stays unsupported because
it is geometrically identical to the straight slide X-(X+4).

Every XvX entry is spliced from two existing donors that share its legs:
  inbound leg  (X -> center) = first half of XvY   (any Y; we use Y = X+1)
  outbound leg (center -> X) = second half of YvX
All v-family entries share thresholds / checkpoint / cut-index / arrow-count
patterns (asserted below), so the splice is exact, not approximate.

Idempotent: re-running overwrites the eight XvX keys and leaves the rest of the
file byte-for-byte semantically identical (the file is re-serialized compactly;
floats round-trip exactly under Python's shortest-repr).
"""

import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "assets" / "reference" / "slide_data.json"

# Sample-array midpoints: v paths are sampled symmetrically, the center sample
# sits exactly halfway (verified for every v key below).
MID_SAMPLES = 96        # of 193 "samples"
MID_REAL = 128          # of 257 "real_path_samples"
# judge areas: [A_start, B_start, C, B_end, A_end] -> splice after C
MID_JUDGE = 3
# pad_enter_times: [B_start, C, B_end, A_end] -> splice after C
MID_PADS = 2


def is_center(sample):
    return sample["x"] == 0.0 and sample["y"] == 0.0


def c_area_split(arrows):
    """The C judge area's arrows straddle the corner. Arrow arrays are stored
    in REVERSE travel order, so the OUTBOUND arrows (sharing one rotation)
    come first and the inbound arrows (sharing another) after."""
    first_rot = arrows[0]["rotation"]
    count = 0
    for arrow in arrows:
        if arrow["rotation"] != first_rot:
            break
        count += 1
    assert 0 < count < len(arrows), "C area arrows are not two rotation runs"
    return count


def main():
    root = json.loads(DATA.read_text(encoding="utf-8"))
    slides = root["slides"]

    for x in range(1, 9):
        y = x % 8 + 1
        a = slides[f"{x}v{y}"]  # inbound donor: X -> C -> Y
        b = slides[f"{y}v{x}"]  # outbound donor: Y -> C -> X

        for donor in (a, b):
            assert len(donor["samples"]) == 2 * MID_SAMPLES + 1
            assert len(donor["real_path_samples"]) == 2 * MID_REAL + 1
            assert is_center(donor["samples"][MID_SAMPLES])
            assert is_center(donor["real_path_samples"][MID_REAL])
            assert len(donor["judge_sequence"]) == 5
            assert len(donor["pad_enter_times"]) == 4
            assert not donor["is_l"] and not donor["is_special_l"]
        # the timing/threshold pattern is shared by the whole v family
        for field in ("track_thresholds", "track_checkpoints",
                      "track_cut_indices", "critical_proportion"):
            assert a[field] == b[field], f"{field} differs between donors"

        split_a = c_area_split(a["track_arrows"][2])
        split_b = c_area_split(b["track_arrows"][2])
        assert split_a == split_b, "C-area inbound arrow counts differ"

        # Track arrows: the inbound leg keeps donor a's arrows (areas 0-1 and
        # the inbound part of its C area — all on the start lane's ray).  The
        # return leg does NOT reuse donor b's equidistantly generated arrows:
        # on the shared segment they would interleave at half-spacing with
        # the inbound ones, opposite-pointing, and read as a broken track.
        # Instead every return arrow mirrors an inbound arrow IN PLACE (same
        # position, rotation flipped to the return donor's outbound value).
        # As the star passes, inbound areas disappear and what remains is a
        # clean chain pointing back out.
        #
        # Conventions (verified by the asserts): arrow arrays are stored in
        # reverse travel order, so a C area lists its outbound arrows first;
        # rotation is uniform per leg.
        rot_out = b["track_arrows"][3][0]["rotation"]
        for area in (b["track_arrows"][2][:split_b], *b["track_arrows"][3:]):
            assert all(arrow["rotation"] == rot_out for arrow in area), \
                "return-leg arrow rotations are not uniform"
        in_c = a["track_arrows"][2][split_a:]  # inbound C arrows, reverse-travel order
        rot_in = in_c[0]["rotation"]
        for area in (a["track_arrows"][0], a["track_arrows"][1], in_c):
            assert all(arrow["rotation"] == rot_in for arrow in area), \
                "inbound-leg arrow rotations are not uniform"

        def mirrored(arrows):
            return [{"x": s["x"], "y": s["y"], "rotation": rot_out} for s in arrows]

        arrows = [
            a["track_arrows"][0],
            a["track_arrows"][1],
            # C area, reverse travel order: the return mirrors first (their
            # travel order is the reverse of the inbound arrows'), then the
            # inbound arrows as the donor stored them.
            mirrored(reversed(in_c)) + in_c,
            # B/A return areas: mirrors of the inbound B/A arrows; reversing
            # keeps reverse-travel order on the outbound leg.
            mirrored(reversed(a["track_arrows"][1])),
            mirrored(reversed(a["track_arrows"][0])),
        ]
        # cut indices follow the donor convention: per-area arrow count,
        # last area empty
        cuts = [[len(area)] for area in arrows[:-1]] + [[]]
        assert cuts[:2] == a["track_cut_indices"][:2]

        entry = {
            "start": x,
            "end": x,
            "samples": a["samples"][:MID_SAMPLES] + b["samples"][MID_SAMPLES:],
            "track_arrows": arrows,
            "track_thresholds": a["track_thresholds"],
            "track_checkpoints": a["track_checkpoints"],
            "track_cut_indices": cuts,
            "critical_proportion": a["critical_proportion"],
            "pad_enter_times": a["pad_enter_times"][:MID_PADS]
                + b["pad_enter_times"][MID_PADS:],
            "judge_sequence": a["judge_sequence"][:MID_JUDGE]
                + b["judge_sequence"][MID_JUDGE:],
            "is_l": False,
            "is_special_l": False,
            "real_path_samples": a["real_path_samples"][:MID_REAL]
                + b["real_path_samples"][MID_REAL:],
        }
        # sanity: path is closed (ends where it starts) and judges A_x twice
        assert entry["samples"][0]["x"] == entry["samples"][-1]["x"] or \
            abs(entry["samples"][0]["x"] - entry["samples"][-1]["x"]) < 1e-3
        assert entry["judge_sequence"][0] == entry["judge_sequence"][-1] == [f"A{x}"]
        assert entry["judge_sequence"][1] == entry["judge_sequence"][-2] == [f"B{x}"]
        slides[f"{x}v{x}"] = entry

    before = json.loads(DATA.read_text(encoding="utf-8"))
    DATA.write_text(json.dumps(root, separators=(",", ":")), encoding="utf-8")

    # round-trip guard: every pre-existing key survives unchanged
    after = json.loads(DATA.read_text(encoding="utf-8"))
    for section, table in before.items():
        for key, value in table.items():
            if section == "slides" and len(key) == 3 and key[0] == key[2] and key[1] == "v":
                continue
            assert after[section][key] == value, f"{section}/{key} drifted"
    print(f"ok: {DATA} now has {len(after['slides'])} slide keys "
          f"({sum(1 for k in after['slides'] if len(k) == 3 and k[1] == 'v' and k[0] == k[2])} same-lane v)")


if __name__ == "__main__":
    sys.exit(main())
