#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Audit the benchmark corpora, because every reach figure is a fraction and all
the work goes into the numerator.

What this checks, and why each one has bitten:

  structure    unparseable FEN, illegal position, more than one king a side.
               Four helpstalemate rows are fairy problems that orthodox chess
               cannot answer -- four guaranteed misses in any rate against 431.

  duplicates   within a file and across files. `selfmate_deep` has 904 rows and
               903 distinct positions; a duplicate is counted twice by every
               rate computed from it. `stalemate_reach` is entirely contained in
               `stalemate_pdb`, so "run every stalemate corpus" double-counts.

  contamination  train/eval and dev/eval overlap. This is the claim the reach
               figures rest on, so it is checked rather than assumed.

  status       the `solved`/`refuted`/`shorter` labels come from THIS ENGINE's
               verdicts at import time. They must never filter a comparison
               against another engine -- that would let the engine mark its own
               paper -- and they go stale, because they are not re-derived.

`refuted` entries are adjudicated separately by an independent prover; see
KNOWN_BAD.jsonl for the results and the adjudicator used.

Usage:
    python tools/audit_corpora.py [--benchmarks DIR]
"""

from __future__ import annotations

import argparse
import collections
import glob
import json
import os
import sys

try:
    import chess
except ImportError:
    print("audit_corpora.py needs python-chess", file=sys.stderr)
    raise SystemExit(2)

# The side that moves first, by goal. A helpmate is Black-first: the side being
# mated starts. Getting this backwards once measured the wrong problem for an
# entire corpus, so it is asserted here rather than assumed.
FIRST_MOVER = {"helpmate": "b", "helpstalemate": "b",
               "selfmate": "w", "selfstalemate": "w", "stalemate": "w"}


def audit(root: str) -> int:
    # KNOWN_BAD.jsonl lives here but is the audit's OUTPUT, not a corpus; left in
    # the glob it reports its own contents as cross-corpus duplicates.
    files = [p for p in sorted(glob.glob(os.path.join(root, "*.jsonl")))
             if os.path.basename(p) != "KNOWN_BAD.jsonl"]
    if not files:
        print(f"no corpora under {root}", file=sys.stderr)
        return 2

    everywhere: dict[str, list[str]] = collections.defaultdict(list)
    problems = 0

    print(f"{'corpus':<34}{'rows':>6}{'uniq':>6}{'badFEN':>8}{'illegal':>8}"
          f"{'kings':>7}{'stm':>5}")
    for path in files:
        rows = [json.loads(line) for line in open(path, encoding="utf-8")]
        name = os.path.basename(path)
        bad = illegal = kings = stm = 0
        here = collections.Counter()
        for row in rows:
            fen = row["fen4"]
            here[fen] += 1
            everywhere[fen].append(name)
            try:
                board = chess.Board(fen + " 0 1")
            except ValueError:
                bad += 1
                continue
            placement = fen.split()[0]
            if placement.count("K") != 1 or placement.count("k") != 1:
                kings += 1
                continue
            if not board.is_valid() and row.get("status") != "illegal":
                illegal += 1
            want = FIRST_MOVER.get(row.get("goal", ""))
            if want and fen.split()[1] != want:
                stm += 1
        problems += bad + illegal + kings + stm + (len(rows) - len(here))
        print(f"{name:<34}{len(rows):>6}{len(here):>6}{bad:>8}{illegal:>8}"
              f"{kings:>7}{stm:>5}")

    shared = {f: sorted(set(v)) for f, v in everywhere.items() if len(set(v)) > 1}
    pairs: collections.Counter = collections.Counter()
    for names in shared.values():
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                pairs[(names[i], names[j])] += 1
    print(f"\npositions in more than one corpus: {len(shared)}")
    for (a, b), n in pairs.most_common():
        # Containment is worth naming separately: a corpus wholly inside another
        # is not an independent corpus, whatever its filename suggests.
        print(f"    {n:5d}  {a}  &  {b}")

    # Contamination is the one that invalidates a reach figure rather than
    # merely inflating a denominator, so it is reported even when it is zero.
    print()
    for a, b in [("matetrack_d8_train60.jsonl", "matetrack_d8_eval200_r2.jsonl"),
                 ("matetrack_d10_train24.jsonl", "matetrack_d10_eval60_r2.jsonl"),
                 ("matetrack_d10_train24.jsonl", "matetrack_d20_eval40_r2.jsonl"),
                 ("stalemate_dev40.jsonl", "stalemate_pdb.jsonl"),
                 ("stalemate_dev40.jsonl", "stalemate_yacpdb.jsonl")]:
        pa, pb = os.path.join(root, a), os.path.join(root, b)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            continue
        fa = {json.loads(l)["fen4"] for l in open(pa, encoding="utf-8")}
        fb = {json.loads(l)["fen4"] for l in open(pb, encoding="utf-8")}
        overlap = len(fa & fb)
        problems += overlap
        print(f"  contamination {overlap:4d}   {a} & {b}")

    tally: collections.Counter = collections.Counter()
    for path in files:
        for row in (json.loads(l) for l in open(path, encoding="utf-8")):
            tally[row.get("status", "(unlabelled)")] += 1
    print(f"\nstatus labels (this engine's verdicts at import, NOT ground truth):")
    for key, n in tally.most_common():
        print(f"    {key:<14}{n:>6}")
    print("\n  `refuted` and `shorter` are falsifiable claims about the CORPUS.")
    print("  See KNOWN_BAD.jsonl for the ones an independent prover has settled.")

    print(f"\n{problems} structural findings")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--benchmarks", default=os.path.join(here, "benchmarks"))
    return audit(ap.parse_args().benchmarks)


if __name__ == "__main__":
    raise SystemExit(main())
