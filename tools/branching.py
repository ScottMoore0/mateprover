#!/usr/bin/env python3
"""Measure what a growth factor is actually made of.

Section 125 records a search whose cost jumped 206x in one depth after four
depths at 12x. The first explanation written for it modelled the growth as
`R = W x B` -- attacker width times defender width -- inferred both from R
itself, and named table capacity as a rival hypothesis it could not separate
without two three-hour re-runs.

None of that was necessary. The engine counts every term directly:

    W         attacker_moves / attacker_move_lists
    B         defender_replies_tried / defender_move_lists
    discount  W x B / R, the factor by which the transposition table collapses
              those children into fewer real expansions

and all three come out of a `--profile` run. The two-term model was wrong twice
over: W was 29.6 rather than the inferred 12, and the missing third term is
worth 2-3x and moving.

TWO MODES.

  ladder    one row per depth, each searched standalone against a fresh table.
            This is the series in 125.

  sweep     one depth at many table sizes. Node count against table size is the
            instrument for capacity questions -- NOT hit rate, which moved 31.8%
            to 17.9% across a 4096x range while node count moved 5.7x, and would
            have said capacity was irrelevant when it was worth 3x.

The sweep is what makes a deep regime cheap to study: a search too expensive to
run holds some small fraction of its nodes in the table, and a shallower search
throttled to the same fraction pays the same penalty. Depth 9's 0.197% coverage
was reproduced at depth 7 with `-M 8` in ninety-five seconds.

Note when comparing B across configurations: B FALLS as the table shrinks (1.30
to 1.07 across the sweep) because re-searched nodes find their refutation sooner.
Hold -M fixed or the comparison flatters itself.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - -"
FIELDS = ["nodes", "tt_probes", "tt_hits", "tt_evictions", "attacker_move_lists",
          "attacker_moves", "defender_move_lists", "defender_replies_tried"]
BYTES_PER_ENTRY = 64  # see 123: the flat slot, not the map entry it replaced


def measure(engine, fen, depth, mb, captures, threads):
    proc = subprocess.run(
        [str(engine), "--captures", str(captures), "--threads", str(threads),
         "--no-portfolio", "-M", str(mb), "--profile", "--direct-depth", "-"],
        input=f"{fen} ; dm {depth}\n", capture_output=True, text=True, timeout=86400)
    blob = proc.stdout + proc.stderr
    out = {}
    for f in FIELDS:
        m = re.search(r'"%s":(\d+)' % f, blob)
        if m is None:
            raise SystemExit(f"engine reported no {f!r}; is --profile still emitting it?\n{blob[:400]}")
        out[f] = int(m.group(1))
    m = re.search(r"acs ([\d.]+)", blob)
    out["secs"] = float(m.group(1)) if m else 0.0
    out["entries"] = mb * 1024 * 1024 // BYTES_PER_ENTRY
    out["B"] = out["defender_replies_tried"] / max(1, out["defender_move_lists"])
    out["W"] = out["attacker_moves"] / max(1, out["attacker_move_lists"])
    out["hit"] = 100.0 * out["tt_hits"] / max(1, out["tt_probes"])
    out["coverage"] = 100.0 * out["entries"] / max(1, out["nodes"])
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=["ladder", "sweep"])
    p.add_argument("--engine", type=Path, default=ROOT / "build" / "mateprover.exe")
    p.add_argument("--fen", default=START)
    p.add_argument("--captures", default="3")
    p.add_argument("--threads", type=int, default=1,
                   help="1 by default: parallel node counts are not comparable")
    p.add_argument("--depths", default="4,5,6,7", help="ladder mode")
    p.add_argument("--depth", type=int, default=7, help="sweep mode")
    p.add_argument("--sizes", default="8192,2048,512,128,32,8,2", help="sweep mode, MB")
    args = p.parse_args()

    if args.mode == "ladder":
        print(f"{'d':>2s} {'nodes':>14s} {'R':>7s} {'B':>5s} {'W':>6s} {'WxB':>7s} {'disc':>6s} {'secs':>7s}")
        prev = None
        for d in [int(x) for x in args.depths.split(",")]:
            r = measure(args.engine, args.fen, d, 8192, args.captures, args.threads)
            R = r["nodes"] / prev if prev else 0.0
            disc = (r["W"] * r["B"] / R) if R else 0.0
            print(f"{d:>2d} {r['nodes']:>14,} {R:>6.2f}x {r['B']:>5.2f} {r['W']:>6.2f} "
                  f"{r['W'] * r['B']:>6.1f}x {disc:>5.2f}x {r['secs']:>7.1f}")
            prev = r["nodes"]
        return 0

    base = None
    print(f"depth {args.depth}\n")
    print(f"{'-M':>6s} {'entries':>12s} {'nodes':>13s} {'coverage':>10s} "
          f"{'penalty':>8s} {'B':>5s} {'hit%':>6s} {'evictions':>13s}")
    for mb in [int(x) for x in args.sizes.split(",")]:
        r = measure(args.engine, args.fen, args.depth, mb, args.captures, args.threads)
        base = base or r["nodes"]
        print(f"{mb:>6d} {r['entries']:>12,} {r['nodes']:>13,} {r['coverage']:>9.3f}% "
              f"{r['nodes'] / base:>7.2f}x {r['B']:>5.2f} {r['hit']:>6.1f} "
              f"{r['tt_evictions']:>13,}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
