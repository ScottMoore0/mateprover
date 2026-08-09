#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Turn an exported problem collection into a verified MateProver corpus.

Why this exists rather than a generator: a corpus built by solving positions
contains only positions the engine can already solve, so its solve rate is 100%
by construction and **reach cannot be measured from it at all**. An externally
sourced corpus carries a composer's stipulation as ground truth, so the problems
the engine FAILS are still known to be sound -- and those are the ones that make
a reach figure mean anything.

Everything here follows from that. Positions are classified, never filtered:

    solved      the engine proved the stipulation at the stated depth
    unsolved    the engine ran out of budget. KEPT -- this is the evidence
    shorter     the engine proved it at a SHALLOWER depth than stipulated
    refuted     the engine proved no solution exists at the stated depth

`shorter` and `refuted` are not discarded either. Published problem collections
do contain errors -- matetrack's own documentation notes its predecessor had
"positions with a sub-optimal or likely incorrect value for the fastest known
mate" -- and a corpus that silently drops whatever disagrees with it is a corpus
that cannot be wrong.

Input: one problem per line, as `<FEN> <stipulation>`, where the stipulation is
`#N` (mate), `=N` (stalemate) or `dm N`/`sm N`. EPD lines work as-is; comment
lines starting with # or ; are ignored. `.olv` exports from YACPDB are YAML --
convert them with --olv if PyYAML is available.

    python tools/import_problems.py --engine build/mateprover \\
        --input stalemates.epd --out benchmarks/stalemate_pdb.jsonl \\
        --time-limit 30
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

DM_RE = re.compile(r"\bdm\s+(\d+)\b")
SM_RE = re.compile(r"\bsm\s+(\d+)\b")
SFM_RE = re.compile(r"\bsfm\s+(\d+)\b")
SSM_RE = re.compile(r"\bssm\s+(\d+)\b")
HM_RE = re.compile(r"\bhm\s+(\d+)\b")
HSM_RE = re.compile(r"\bhsm\s+(\d+)\b")
# Stipulation as written by problem databases: #3, =2, s#4, s=3, h#2, h=2 ...
#
# The two-character forms must come FIRST in the alternation. Python's `|` takes
# the leftmost match that succeeds, so putting `=` before `s=` makes every
# selfstalemate import as a plain stalemate -- silently, and with a stipulation
# the engine would then fail to prove.
STIP_RE = re.compile(r"(?:^|\s)(h#|h=|s#|s=|#|=)\s*(\d+)", re.IGNORECASE)
ALT_RE = re.compile(r"\b(dm|sm)\s+(\d+)\b")


def parse_line(line: str):
    """Return (fen4, goal, depth) or None."""
    line = line.strip()
    if not line or line.startswith(";"):
        return None
    fields = line.split()
    if len(fields) < 4:
        return None
    fen4 = " ".join(fields[:4])
    if "/" not in fen4 or fields[1] not in ("w", "b"):
        return None
    rest = " ".join(fields[4:])

    got = ALT_RE.search(rest)
    if got:
        return fen4, ("stalemate" if got.group(1) == "sm" else "mate"), int(got.group(2))
    got = STIP_RE.search(rest)
    if not got:
        return None
    kind = got.group(1).lower()
    goal = {"h#": "helpmate", "h=": "helpstalemate", "s#": "selfmate",
            "s=": "selfstalemate", "=": "stalemate", "#": "mate"}[kind]
    return fen4, goal, int(got.group(2))


def solve(engine, fen4, goal, depth, seconds, emit_proof=False):
    args = [str(engine), "--direct-depth", "--time-limit", str(seconds)]
    flag = {"stalemate": "--stalemate", "selfmate": "--selfmate",
            "selfstalemate": "--selfstalemate", "helpmate": "--helpmate",
            "helpstalemate": "--helpstalemate"}.get(goal)
    if flag:
        args.append(flag)
    if emit_proof:
        args.append("--emit-proof")
    args.append("-")
    token = {"stalemate": "sm", "selfmate": "sfm", "selfstalemate": "ssm",
             "helpmate": "hm", "helpstalemate": "hsm"}.get(goal, "dm")
    out = subprocess.run(args, input=f"{fen4} {token} {depth}\n".encode(),
                         capture_output=True, timeout=seconds * 8).stdout.decode()
    # An unparseable position makes the engine echo the WHOLE input line back,
    # and that line ends in the stipulation -- "... sm 3". Searching the output
    # for `sm N` therefore matched the echo and scored illegal positions as
    # solved. Check for the error marker before looking for a result.
    if "error input" in out:
        return False, False, out, True
    rx = {"stalemate": SM_RE, "selfmate": SFM_RE, "selfstalemate": SSM_RE,
          "helpmate": HM_RE, "helpstalemate": HSM_RE}.get(goal, DM_RE)
    return ("timeout" in out), bool(rx.search(out)), out, False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", type=pathlib.Path, required=True)
    ap.add_argument("--input", type=pathlib.Path, required=True)
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--time-limit", type=float, default=30.0)
    ap.add_argument("--workers", type=int, default=8,
                    help="positions in flight; each is independent, so this "
                         "changes wall clock and nothing else")
    ap.add_argument("--verify", action="store_true",
                    help="re-check every solved problem with tools/verify_proof.py")
    args = ap.parse_args()

    if not args.engine.exists():
        print(f"engine not found: {args.engine}", file=sys.stderr)
        return 2

    problems, skipped = [], 0
    for raw in args.input.read_text(encoding="utf-8-sig").splitlines():
        parsed = parse_line(raw)
        if parsed is None:
            skipped += 1
            continue
        problems.append(parsed)

    tally = {"solved": 0, "unsolved": 0, "shorter": 0, "refuted": 0,
             "unsupported": 0, "illegal": 0}

    def classify(item):
        fen4, goal, depth = item
        timed_out, found, out, illegal = solve(args.engine, fen4, goal, depth,
                                               args.time_limit, args.verify)
        if illegal:
            # Not orthodox chess -- kingless diagrams and the like. The database
            # holds them legitimately; this engine cannot express them.
            return {"fen4": fen4, "goal": goal, "mate": depth,
                    "status": "illegal"}, None
        proof = None
        if found:
            status = "solved"
            # Is the stipulation right? A shallower proof means the published
            # depth is not the shortest, which is a real and known failure mode
            # of problem collections.
            if depth > 1:
                _, shallower, _, _ = solve(args.engine, fen4, goal, depth - 1,
                                           args.time_limit)
                if shallower:
                    status = "shorter"
            if args.verify:
                proof = out.strip()
        elif timed_out:
            status = "unsolved"     # kept: this is what makes reach measurable
        else:
            status = "refuted"      # searched to completion, no solution exists
        return {"fen4": fen4, "goal": goal, "mate": depth, "status": status}, proof

    rows, proofs = [], []
    with ThreadPoolExecutor(max_workers=max(1, args.workers)) as pool:
        for row, proof in pool.map(classify, problems):
            rows.append(row)
            tally[row["status"]] += 1
            if proof:
                proofs.append(proof)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", newline="\n") as fh:
        for row in rows:
            fh.write(json.dumps(row) + "\n")

    total = len(rows)
    print(f"read {total} problems ({skipped} lines skipped)")
    for key in ("solved", "shorter", "unsolved", "refuted", "unsupported", "illegal"):
        if tally[key]:
            print(f"  {key:<12} {tally[key]:>5}"
                  f"{'   <-- kept as evidence of reach' if key == 'unsolved' else ''}"
                  f"{'   <-- stipulation disagrees, check it' if key in ('shorter', 'refuted') else ''}")
    solved_total = tally["solved"] + tally["shorter"]
    measurable = solved_total + tally["unsolved"]
    if measurable:
        print(f"\nsolve rate over sound problems: {solved_total}/{measurable} "
              f"= {100.0 * solved_total / measurable:.1f}%")
        print("That figure is only meaningful because the unsolved problems stayed in.")

    if args.verify and proofs:
        tool = pathlib.Path(__file__).with_name("verify_proof.py")
        proc = subprocess.run([sys.executable, str(tool), "--quiet", "-"],
                              input=("\n".join(proofs) + "\n").encode(),
                              capture_output=True, timeout=1800)
        sys.stdout.write(proc.stdout.decode())
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr.decode())
            print("VERIFICATION FAILED", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
