#!/usr/bin/env python3
"""Reproduce the reach figures quoted in docs/RESULTS.md.

The numbers in the documentation are worth exactly as much as a reader's ability
to check them, so this runs the same measurements on the same held-out positions
and prints what it gets beside what is claimed.

    python tools/reproduce_results.py --engine build/echest
    python tools/reproduce_results.py --engine build/echest --quick

Expect the full run to take on the order of half an hour: unsolved positions
consume their whole budget, which is the point of a budgeted measurement.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
SUITES = HERE.parent / "benchmarks"
DM = re.compile(r"\bdm \d+")


def measure(engine, positions, args, budget):
    solved = 0
    for row in positions:
        result = subprocess.run(
            [str(engine), "-5", "-z", str(row["mate"]), "--time-limit", str(budget), *args, "-"],
            input=(row["fen4"] + "\n").encode(),
            capture_output=True, timeout=budget * 8 + 120)
        if DM.search(result.stdout.decode()):
            solved += 1
    return solved


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", type=pathlib.Path, required=True)
    parser.add_argument("--quick", action="store_true",
                        help="a third of the positions and a third of the budget; "
                             "indicative only, and not comparable to the documented figures")
    args = parser.parse_args()

    if not args.engine.exists():
        print(f"engine not found: {args.engine}", file=sys.stderr)
        return 2

    def load(name):
        rows = [json.loads(line) for line in (SUITES / name).read_text().splitlines() if line.strip()]
        return rows[::3] if args.quick else rows

    scale = 3 if args.quick else 1
    checks = [
        ("mate-in-8, default configuration, 15s",
         load("matetrack_d8_holdout60.jsonl"), [], 15 // scale, "52/60"),
        ("mate-in-10, 32 threads, --direct-depth, 30s",
         load("matetrack_d10_holdout24.jsonl"),
         ["-M", "2048", "--threads", "32", "--direct-depth"], 30 // scale, "18/24"),
        ("mate-in-10, no portfolio (the comparison)",
         load("matetrack_d10_holdout24.jsonl"),
         ["-M", "2048", "--threads", "32", "--direct-depth", "--no-portfolio"], 30 // scale, "13/24"),
    ]

    if args.quick:
        print("QUICK MODE: reduced positions and budget. Indicative only.\n")
    print(f"{'measurement':<46} {'measured':>10}   {'documented':>10}")
    for label, positions, extra, budget, documented in checks:
        got = measure(args.engine, positions, extra, max(budget, 1))
        print(f"{label:<46} {f'{got}/{len(positions)}':>10}   {documented:>10}", flush=True)

    print("\nHeld-out positions: no tuning used them. See benchmarks/README.md.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
