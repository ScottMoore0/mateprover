#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (c) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Reproduce the reach figures quoted in docs/RESULTS.md.

The numbers in the documentation are worth exactly as much as a reader's ability
to check them, so this runs the same measurements on the same held-out positions
and prints what it gets beside what is claimed.

    python tools/reproduce_results.py --engine build/mateprover
    python tools/reproduce_results.py --engine build/mateprover --quick

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


def measure_batched(engine, positions, args, budget, width):
    """Measure a node-budgeted run with several positions in flight.

    Only safe for node budgets. Under a wall clock, positions sharing cores each
    get less of it, so a wider batch quietly loses a few deep positions; under a
    node budget the answers are identical at any width (architecture 34).

    Depth comes from a per-line `bm #N` token rather than -z, so a suite with
    mixed depths would still work.
    """
    stdin = "".join(f"{row['fen4']} bm #{row['mate']};" + chr(10) for row in positions)
    result = subprocess.run(
        [str(engine), "-5", "--node-limit", str(budget),
         "--parallel-positions", str(width), *args, "-"],
        input=stdin.encode(), capture_output=True, timeout=7200)
    return len(DM.findall(result.stdout.decode()))


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
# Reference values re-measured 2026-08-02 on re-minted development sets. The
# originals were drawn without a recorded seed and could not be rebuilt, so
# their numbers described sets that no longer exist. The new draw is slightly
# easier across the board; what the table exists to show -- dfpn ahead of
# depth-first at both depths -- is unchanged.
# Every entry names its route explicitly. These originally relied on the default
# being depth-first; when dfpn was promoted (architecture 29) the "depth-first"
# rows silently began running dfpn, so the tool compared a route with itself and
# still printed the old expected values beside it.
DETERMINISTIC = [
    ("mate-8 dev set, depth-first, 2M nodes", "matetrack_d8_train60.jsonl",
     ["--route", "depth-first"], 2000000, "12/60"),
    ("mate-8 dev set, dfpn, 2M nodes", "matetrack_d8_train60.jsonl",
     ["--route", "dfpn"], 2000000, "19/60"),
    ("mate-10 dev set, depth-first, 4M nodes", "matetrack_d10_train24.jsonl",
     ["--direct-depth", "--route", "depth-first"], 4000000, "5/24"),
    ("mate-10 dev set, dfpn, 4M nodes", "matetrack_d10_train24.jsonl",
     ["--direct-depth", "--route", "dfpn"], 4000000, "22/24"),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", type=pathlib.Path, required=True)
    parser.add_argument("--deterministic", action="store_true",
                        help="node budgets instead of wall clock: slower, lower numbers, "
                             "and identical on every machine")
    parser.add_argument("--width", type=int, default=8,
                        help="positions in flight in --deterministic mode (default 8). "
                             "A node budget makes batching free, so this only changes how "
                             "long the run takes, not what it reports")
    parser.add_argument("--quick", action="store_true",
                        help="a third of the positions and a third of the budget; "
                             "indicative only, and not comparable to the documented figures")
    args = parser.parse_args()

    if not args.engine.exists():
        print(f"engine not found: {args.engine}", file=sys.stderr)
        return 2

    def load(name):
        # Position sets are regenerated, not shipped. They are drawn from a
        # third-party corpus under a copyleft licence, and redistributing
        # derived subsets would carry that licence's obligations into this
        # tree; deriving them locally does not. benchmarks/MANIFEST.json keeps
        # the seed and selection parameters for every set, so a regenerated set
        # is the same set.
        path = SUITES / name
        if not path.exists():
            raise SystemExit(
                f"position set not found: {path}\n\n"
                "Position sets are not shipped. Rebuild them with the seeds\n"
                "recorded in benchmarks/MANIFEST.json:\n\n"
                "    python tools/mint_eval_set.py --corpus <corpus.epd> \\\n"
                "        --depth <N> --count <N> --seed <N> --name <stem>\n\n"
                "See benchmarks/README.md.")
        rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        return rows[::3] if args.quick else rows

    scale = 3 if args.quick else 1
    # Sets are loaded lazily. Building this list eagerly made --deterministic
    # demand the evaluation sets it never reads, so a deterministic run failed
    # on a missing file belonging to the other mode entirely.
    # THE SETS HERE MUST BE REBUILDABLE ONES. This list named the original
    # eval200 and eval60, which benchmarks/MANIFEST.json marks
    # `"rebuildable": false` -- so a reader following the README could fetch the
    # corpus, fail to mint the sets, and never reach a comparison at all. It also
    # quoted those retired sets' figures, which disagree with the ones the README
    # now documents. A reproduction script that cannot be run, printing numbers
    # that are not the published ones, is worse than no reproduction script.
    #
    # The _r2 sets are the current, rebuildable ones, and the figures below are
    # what THEY produced.
    checks = [
        ("mate-in-8, default configuration, 15s",
         "matetrack_d8_eval200_r2.jsonl", [], 15 // scale, "177/200 = 88.5%"),
        ("mate-in-10, 32 threads, --direct-depth, 30s",
         "matetrack_d10_eval60_r2.jsonl",
         ["-M", "2048", "--threads", "32", "--direct-depth"], 30 // scale, "58/60 = 96.7%"),
        ("mate-in-10, no portfolio (the comparison)",
         "matetrack_d10_eval60_r2.jsonl",
         ["-M", "2048", "--threads", "32", "--direct-depth", "--no-portfolio"], 30 // scale,
         "54/60 = 90.0%"),
    ]

    if args.deterministic:
        print("DETERMINISTIC MODE: sequential, node-budgeted.")
        print("Identical on every machine, and NOT comparable to the wall-clock "
              "figures -- a different configuration.")
        print(f"Running {args.width} positions at a time; a node budget makes that free.\n")
        print(f"{'measurement':<44} {'measured':>10}   {'documented':>12}")
        for label, suite, extra, budget, documented in DETERMINISTIC:
            rows = load(suite)
            got = measure_batched(args.engine, rows, ["--single-thread", "--no-portfolio", *extra],
                                  budget // (3 if args.quick else 1), args.width)
            print(f"{label:<44} {f'{got}/{len(rows)}':>10}   {documented:>12}", flush=True)
        return 0

    if args.quick:
        print("QUICK MODE: reduced positions and budget. Indicative only.\n")
    print(f"{'measurement':<46} {'measured':>10}   {'documented':>16}")
    for label, suite, extra, budget, documented in checks:
        positions = load(suite)
        got = measure(args.engine, positions, extra, max(budget, 1))
        print(f"{label:<46} {f'{got}/{len(positions)}':>10}   {documented:>16}", flush=True)

    print("\nEvaluation positions: used once, never consulted during development. See benchmarks/README.md.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
