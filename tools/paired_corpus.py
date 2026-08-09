#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Paired coverage AND speed over a whole corpus: this engine against Chest 3.19.

The point of this file is that it runs a CORPUS. On every goal where a sample was
replaced by the full set, the two disagreed -- mate-in-8's sample invented a loss,
selfmate's understated the misses twelvefold, helpmate's overstated a loss that
was really a dead heat. Samples of 30 to 40 against effects of 1 to 4% are the
wrong instrument.

Coverage and timing come from ONE pass. Every mateprover result line carries its
own `acs`, so a batched run still yields per-position times; Chest is invoked per
position and timed directly. Two passes would cost twice as much and let the two
tables drift apart.

Three scoring rules, each of which has been got wrong here at least once:

  presence, not equality   51 corpus rows have a true solution shallower than
                           stipulated. Demanding the reported depth equal the
                           stipulated one scores BOTH engines as failing on all
                           51, for correctly proving the shorter mate.

  refusal is not timeout   Chest reporting "No solution" and Chest running out of
                           clock are different events. A position both engines
                           refuse is evidence about the CORPUS, not either
                           engine. See benchmarks/KNOWN_BAD.jsonl.

  the goal must be set     Fed an EPD stipulation like `=2`, Chest silently
                           solves the position as an orthodox DIRECTMATE and
                           reports `dm 1`. The job type has to be set with
                           j<letter> or the run measures the wrong goal.

Resumable. State is written after every chunk, because long runs here get killed
and three earlier sweeps were lost whole.

Usage:
    python tools/paired_corpus.py --corpus benchmarks/stalemate_pdb.jsonl \
        --goal stalemate --seconds 10 --state run.json
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import statistics
import subprocess
import time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(HERE)
CHEST_DIR = os.path.join(ROOT, "chest-3.19-original", "build")
CHEST = os.path.join(CHEST_DIR, "dchest_original.exe")

GOALS = {
    "mate":          ("mate",          r"\bdm\s+(\d+)\b",  "jo"),
    "stalemate":     ("stalemate",     r"\bsm\s+(\d+)\b",  "jO"),
    "selfmate":      ("selfmate",      r"\bsfm\s+(\d+)\b", "js"),
    "selfstalemate": ("selfstalemate", r"\bssm\s+(\d+)\b", "jS"),
    "helpmate":      ("helpmate",      r"\bhm\s+(\d+)\b",  "jh"),
    "helpstalemate": ("helpstalemate", r"\bhsm\s+(\d+)\b", "jH"),
}
ACS = re.compile(r"\bacs\s+([0-9.eE+-]+)")


def mateprover_chunk(engine, goal, rows, seconds, memory_mb):
    """{fen: seconds} for the rows this engine solved."""
    name, token, _ = GOALS[goal]
    pattern = re.compile(token)
    out = {}
    by_depth = collections.defaultdict(list)
    for row in rows:
        by_depth[row["mate"]].append(row)
    for depth, group in by_depth.items():
        proc = subprocess.run(
            # No -M. An explicit -M is the budget for every table alive at
            # once; passing the per-lane figure of 256 starves the engine of the
            # default it ships with, and on a cooperative search that was worth
            # 12.8x on a measured position (2.9 s became 37 s). Handicapping the
            # engine under test with its own tuning knob is exactly the defect
            # this project already fixed once, in the CLI, and I reintroduced it
            # in the harness.
            [engine, "--goal", name, "-z", str(depth),
             "--time-limit", str(seconds), "-"],
            input="".join(r["fen4"] + "\n" for r in group).encode(),
            capture_output=True, timeout=seconds * len(group) + 900)
        for row, line in zip(group, proc.stdout.decode().splitlines()):
            if pattern.search(line):
                found = ACS.search(line)
                out[row["fen4"]] = float(found.group(1)) if found else 0.0
    return out


def chest_one(goal, row, seconds, memory_mb):
    """('solved'|'refused'|'timeout', seconds)."""
    board, stm, castling, ep = row["fen4"].split()
    # The board-only Forsyth line carries no castling rights, and dropping them
    # changes the position rather than its presentation.
    extra = ""
    for flag, code in (("K", "cws"), ("Q", "cwl"), ("k", "cbs"), ("q", "cbl")):
        if flag in castling:
            extra += code + "\n"
    if ep != "-":
        extra += "e" + ep + "\n"
    job = f"LE\nf {board}\n{extra}{GOALS[goal][2]}\nz{row['mate']}{stm}\n..\n"
    start = time.time()
    try:
        proc = subprocess.run([CHEST, "-r", "-M", str(memory_mb)], cwd=CHEST_DIR,
                              input=job.encode(), capture_output=True, timeout=seconds)
    except subprocess.TimeoutExpired:
        return "timeout", seconds
    elapsed = time.time() - start
    text = proc.stdout.decode("latin-1")
    if "No solution" in text:
        return "refused", elapsed
    return ("solved", elapsed) if "Solution" in text else ("timeout", elapsed)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--goal", required=True, choices=sorted(GOALS))
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--engine", default=os.path.join(HERE, "build", "mateprover.exe"))
    ap.add_argument("--mateprover-mb", type=int, default=256)
    ap.add_argument("--chest-mb", type=int, default=2048)
    ap.add_argument("--chunk", type=int, default=20)
    ap.add_argument("--state", required=True)
    args = ap.parse_args()

    rows, seen = [], set()
    for line in open(args.corpus, encoding="utf-8"):
        if not line.strip():
            continue
        row = json.loads(line)
        if row["fen4"] not in seen:          # a duplicate is counted twice by any rate
            seen.add(row["fen4"])
            rows.append(row)
    chunks = [rows[i:i + args.chunk] for i in range(0, len(rows), args.chunk)]

    state = {"mine": {}, "theirs": {}, "refused": [], "done": []}
    if os.path.exists(args.state):
        state = json.load(open(args.state))
    print(f"{os.path.basename(args.corpus)}: {len(rows)} distinct, goal {args.goal}, "
          f"{args.seconds:g}s a position, {len(chunks)} chunks, "
          f"{len(state['done'])} done", flush=True)

    for index, chunk in enumerate(chunks):
        if index in state["done"]:
            continue
        start = time.time()
        state["mine"].update(mateprover_chunk(args.engine, args.goal, chunk,
                                              args.seconds, args.mateprover_mb))
        for row in chunk:
            verdict, elapsed = chest_one(args.goal, row, args.seconds, args.chest_mb)
            if verdict == "solved":
                state["theirs"][row["fen4"]] = elapsed
            elif verdict == "refused":
                state["refused"].append(row["fen4"])
        state["done"].append(index)
        json.dump(state, open(args.state, "w"))
        print(f"  chunk {index+1}/{len(chunks)}: mateprover {len(state['mine'])}  "
              f"chest {len(state['theirs'])}  ({time.time()-start:.0f}s)", flush=True)

    mine, theirs = set(state["mine"]), set(state["theirs"])
    both = sorted(mine & theirs)
    print(f"\nCOVERAGE over {len(rows)} distinct positions at {args.seconds:g}s")
    print(f"  mateprover {len(mine)}    chest {len(theirs)}")
    print(f"  only mateprover {len(mine - theirs)}    only chest {len(theirs - mine)}")
    print(f"  chest refused definitively (evidence about the CORPUS): "
          f"{len(set(state['refused']))}")
    for fen in sorted(theirs - mine)[:25]:
        print(f"    only chest: {fen}")

    # Speed is compared only on positions BOTH solved. A ratio that includes a
    # timeout is comparing a number against a cap.
    if both:
        mine_t = [max(state["mine"][f], 1e-6) for f in both]
        their_t = [max(state["theirs"][f], 1e-6) for f in both]
        ratios = [t / m for m, t in zip(mine_t, their_t)]
        print(f"\nSPEED on the {len(both)} both solved")
        print(f"  total   chest {sum(their_t):.1f}s  mateprover {sum(mine_t):.1f}s  "
              f"= {sum(their_t)/max(sum(mine_t),1e-9):.2f}x")
        print(f"  median per-position speedup: {statistics.median(ratios):.2f}x")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
