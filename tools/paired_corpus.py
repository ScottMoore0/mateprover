#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Paired coverage of a whole corpus: this engine against Chest 3.19.

The point of this file is that it runs a CORPUS. On every goal where a sample
was replaced by the full set, the two disagreed -- mate-in-8's sample invented a
loss, selfmate's understated the misses twelvefold, helpmate's overstated a loss
that was really a dead heat. Samples of 30 to 40 against effects of 1 to 4% are
the wrong instrument, and this project used them repeatedly before the
arithmetic was taken seriously.

Scoring is PRESENCE, not equality with the stipulated depth. 51 corpus rows have
a true solution shallower than stipulated; a harness demanding an exact match
scores both engines as failing on all of them, for correctly proving the shorter
one.

A refusal is not a timeout. Chest reporting "No solution" and Chest running out
of clock are different events, and this records them separately, because a
position both engines refuse is evidence about the CORPUS rather than about
either engine. See benchmarks/KNOWN_BAD.jsonl.

Chest is given more memory than mateprover, deliberately.

Usage:
    python tools/paired_corpus.py --corpus benchmarks/stalemate_pdb.jsonl \
        --goal stalemate --seconds 10
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(HERE)
CHEST_DIR = os.path.join(ROOT, "chest-3.19-original", "build")
CHEST = os.path.join(CHEST_DIR, "dchest_original.exe")

# mateprover flag, its result token, and Chest's job-type letter.
GOALS = {
    "mate":          ("--goal", "mate",          r"\bdm\s+(\d+)\b",  "jo"),
    "stalemate":     ("--goal", "stalemate",     r"\bsm\s+(\d+)\b",  "jO"),
    "selfmate":      ("--goal", "selfmate",      r"\bsfm\s+(\d+)\b", "js"),
    "selfstalemate": ("--goal", "selfstalemate", r"\bssm\s+(\d+)\b", "jS"),
    "helpmate":      ("--goal", "helpmate",      r"\bhm\s+(\d+)\b",  "jh"),
    "helpstalemate": ("--goal", "helpstalemate", r"\bhsm\s+(\d+)\b", "jH"),
}


def run_mateprover(engine, goal, rows, seconds, memory_mb):
    """Solved set. Batched by depth: the engine takes a whole batch on stdin and
    applies the limit per position, which is how every other corpus run here is
    done."""
    _, name, token, _ = GOALS[goal]
    pattern = re.compile(token)
    by_depth = collections.defaultdict(list)
    for row in rows:
        by_depth[row["mate"]].append(row)
    solved = set()
    for depth in sorted(by_depth):
        group = by_depth[depth]
        proc = subprocess.run(
            [engine, "--goal", name, "-z", str(depth), "-M", str(memory_mb),
             "--time-limit", str(seconds), "-"],
            input="".join(r["fen4"] + "\n" for r in group).encode(),
            capture_output=True, timeout=seconds * len(group) + 600)
        for row, line in zip(group, proc.stdout.decode().splitlines()):
            if pattern.search(line):
                solved.add(row["fen4"])
    return solved


def run_chest(goal, row, seconds, memory_mb):
    """'solved' | 'refused' | 'timeout'.

    Chest takes a job rather than a stipulated EPD: fed `=2` it silently solves
    the position as an orthodox DIRECTMATE and reports dm 1, so the job type has
    to be set with j<letter> or the comparison measures the wrong goal.
    """
    board, stm, castling, ep = row["fen4"].split()
    # The board-only Forsyth line carries no castling rights, and dropping them
    # changes the position rather than the presentation: five of the 200
    # mate-in-8 evaluation positions have rights, and a rook that cannot castle
    # is a different problem. Rights are enabled one at a time, colour then side,
    # long meaning queenside.
    extra = ""
    for flag, code in (("K", "cws"), ("Q", "cwl"), ("k", "cbs"), ("q", "cbl")):
        if flag in castling:
            extra += code + "\n"
    if ep != "-":
        extra += "e" + ep + "\n"
    job = f"LE\nf {board}\n{extra}{GOALS[goal][3]}\nz{row['mate']}{stm}\n..\n"
    try:
        proc = subprocess.run([CHEST, "-r", "-M", str(memory_mb)], cwd=CHEST_DIR,
                              input=job.encode(), capture_output=True,
                              timeout=seconds)
    except subprocess.TimeoutExpired:
        return "timeout"
    out = proc.stdout.decode("latin-1")
    if "No solution" in out:
        return "refused"
    return "solved" if "Solution" in out else "timeout"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--goal", required=True, choices=sorted(GOALS))
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--engine", default=os.path.join(HERE, "build", "mateprover.exe"))
    ap.add_argument("--mateprover-mb", type=int, default=256)
    ap.add_argument("--chest-mb", type=int, default=2048)
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    rows = [json.loads(l) for l in open(args.corpus, encoding="utf-8") if l.strip()]
    seen, unique = set(), []
    for row in rows:
        if row["fen4"] not in seen:
            seen.add(row["fen4"])
            unique.append(row)
    print(f"{os.path.basename(args.corpus)}: {len(rows)} rows, {len(unique)} distinct, "
          f"goal {args.goal}, {args.seconds:g}s a position, single trial", flush=True)
    print(f"  mateprover {args.mateprover_mb} MB   chest {args.chest_mb} MB", flush=True)

    t0 = time.time()
    mine = run_mateprover(args.engine, args.goal, unique, args.seconds, args.mateprover_mb)
    print(f"  mateprover: {len(mine)}/{len(unique)}   ({time.time()-t0:.0f}s)", flush=True)

    t0 = time.time()
    theirs, refused = set(), set()
    for i, row in enumerate(unique, 1):
        verdict = run_chest(args.goal, row, args.seconds, args.chest_mb)
        if verdict == "solved":
            theirs.add(row["fen4"])
        elif verdict == "refused":
            refused.add(row["fen4"])
        if i % 100 == 0:
            print(f"    chest {i}/{len(unique)} ({time.time()-t0:.0f}s)", flush=True)
    print(f"  chest:      {len(theirs)}/{len(unique)}   ({time.time()-t0:.0f}s)", flush=True)

    only_mine, only_theirs = sorted(mine - theirs), sorted(theirs - mine)
    print(f"\n  only mateprover: {len(only_mine)}    only chest: {len(only_theirs)}")
    print(f"  chest refused definitively (evidence about the CORPUS): {len(refused)}")
    for fen in only_theirs[:20]:
        print(f"    only chest: {fen}")
    if args.out:
        json.dump({"corpus": args.corpus, "goal": args.goal, "seconds": args.seconds,
                   "distinct": len(unique), "mateprover": sorted(mine),
                   "chest": sorted(theirs), "chest_refused": sorted(refused)},
                  open(args.out, "w"))
        print(f"  wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
