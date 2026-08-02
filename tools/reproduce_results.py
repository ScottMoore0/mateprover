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


def measure(engine, positions, args, budget, deterministic=False):
    solved = 0
    for row in positions:
        limit = ["--node-limit", str(budget)] if deterministic else ["--time-limit", str(budget)]
        result = subprocess.run(
            [str(engine), "-5", "-z", str(row["mate"]), *limit, *args, "-"],
            input=(row["fen4"] + "\n").encode(),
            capture_output=True, timeout=1800 if deterministic else budget * 8 + 120)
        if DM.search(result.stdout.decode()):
            solved += 1
    return solved


# Sequential and node-budgeted, so identical on every machine. This measures a
# different configuration from the headline figures -- one thread, no portfolio --
# so the numbers are lower and are not comparable to them. What they are is
# exactly reproducible: a wall-clock run of the same engine varies by several
# positions between runs on one machine, which is the size of most of the
# effects this project measures.
DETERMINISTIC = [
    ("mate-8 dev set, depth-first, 2M nodes", "matetrack_d8_train60.jsonl", [], 2000000, "10/60"),
    ("mate-8 dev set, dfpn, 2M nodes", "matetrack_d8_train60.jsonl",
     ["--route", "dfpn"], 2000000, "16/60"),
    ("mate-10 dev set, depth-first, 4M nodes", "matetrack_d10_train24.jsonl",
     ["--direct-depth"], 4000000, "4/24"),
    ("mate-10 dev set, dfpn, 4M nodes", "matetrack_d10_train24.jsonl",
     ["--direct-depth", "--route", "dfpn"], 4000000, "18/24"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", type=pathlib.Path, required=True)
    parser.add_argument("--deterministic", action="store_true",
                        help="node budgets instead of wall clock: slower, lower numbers, "
                             "and identical on every machine")
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
         load("matetrack_d8_eval200.jsonl"), [], 15 // scale, "159/200 = 79.5%"),
        ("mate-in-10, 32 threads, --direct-depth, 30s",
         load("matetrack_d10_eval60.jsonl"),
         ["-M", "2048", "--threads", "32", "--direct-depth"], 30 // scale, "44/60 = 73.3%"),
        ("mate-in-10, no portfolio (the comparison)",
         load("matetrack_d10_eval60.jsonl"),
         ["-M", "2048", "--threads", "32", "--direct-depth", "--no-portfolio"], 30 // scale,
         "29/60 = 48.3%"),
    ]

    if args.deterministic:
        print("DETERMINISTIC MODE: sequential, node-budgeted.")
        print("Identical on every machine, and NOT comparable to the wall-clock "
              "figures -- a different configuration.\n")
        print(f"{'measurement':<44} {'measured':>10}   {'documented':>12}")
        for label, suite, extra, budget, documented in DETERMINISTIC:
            rows = load(suite)
            got = measure(args.engine, rows, ["--single-thread", "--no-portfolio", *extra],
                          budget // (3 if args.quick else 1), deterministic=True)
            print(f"{label:<44} {f'{got}/{len(rows)}':>10}   {documented:>12}", flush=True)
        return 0

    if args.quick:
        print("QUICK MODE: reduced positions and budget. Indicative only.\n")
    print(f"{'measurement':<46} {'measured':>10}   {'documented':>16}")
    for label, positions, extra, budget, documented in checks:
        got = measure(args.engine, positions, extra, max(budget, 1))
        print(f"{label:<46} {f'{got}/{len(positions)}':>10}   {documented:>16}", flush=True)

    print("\nEvaluation positions: used once, never consulted during development. See benchmarks/README.md.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
