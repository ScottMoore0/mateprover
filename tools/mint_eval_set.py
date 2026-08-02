#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Mint a fresh evaluation set from an EPD corpus.

Reach figures are only evidence about the engine if nothing about the engine was
chosen by looking at them, and that property is consumed by use -- silently. A
set consulted for ten promote-or-reject decisions overstated this engine's
mate-in-8 reach by seven points, enough to put the old figure outside the new
confidence interval (architecture 14).

So: mint a set BEFORE the work it will judge, measure it ONCE afterwards, and
never consult it in between. This tool enforces the mechanical part -- excluding
every position any existing set already contains -- and records what was drawn,
when, and with which seed.

    python tools/mint_eval_set.py --corpus matetrack.epd --depth 8 --count 200 \
        --name d8_eval_round2

The corpus is not shipped with the engine; matetrack.epd is the public
mate-tracking benchmark this project draws from.
"""

import argparse
import datetime
import json
import pathlib
import random
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
BENCH = HERE.parent / "benchmarks"
MANIFEST = BENCH / "MANIFEST.json"


def already_used():
    seen = set()
    for path in sorted(BENCH.glob("*.jsonl")):
        for line in path.read_text().splitlines():
            line = line.strip()
            if line.startswith("{"):
                fen = json.loads(line).get("fen4")
                if fen:
                    seen.add(fen)
    return seen


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--corpus", type=pathlib.Path, required=True)
    parser.add_argument("--depth", type=int, required=True)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--name", required=True, help="file stem, written under benchmarks/")
    parser.add_argument("--seed", type=int, default=None,
                        help="defaults to the count, so a run is reproducible from the manifest")
    args = parser.parse_args()

    if not args.corpus.exists():
        print(f"corpus not found: {args.corpus}", file=sys.stderr)
        return 2

    target = BENCH / f"{args.name}.jsonl"
    if target.exists():
        print(f"refusing to overwrite {target.name}: a minted set is spent the "
              f"first time it is measured, so it is never regenerated", file=sys.stderr)
        return 2

    used = already_used()
    pool = []
    for line in args.corpus.read_text(encoding="utf-8", errors="replace").splitlines():
        found = re.search(r"bm #(-?\d+)", line)
        if not found or int(found.group(1)) != args.depth:
            continue
        fen4 = " ".join(line.split()[:4])
        if fen4 not in used:
            pool.append({"fen4": fen4, "mate": args.depth})

    if len(pool) < args.count:
        print(f"only {len(pool)} unused mate-in-{args.depth} positions available, "
              f"{args.count} requested", file=sys.stderr)
        return 2

    seed = args.seed if args.seed is not None else args.count
    random.Random(seed).shuffle(pool)
    rows = pool[:args.count]
    with target.open("w", encoding="utf-8", newline="\n") as handle:
        for row in rows:
            handle.write(json.dumps(row) + "\n")

    entry = {
        "name": target.name,
        "minted": datetime.date.today().isoformat(),
        "depth": args.depth,
        "count": len(rows),
        "seed": seed,
        "corpus": args.corpus.name,
        "pool_available": len(pool),
        "status": "unused",
    }
    manifest = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else []
    manifest.append(entry)
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"minted {target.name}: {len(rows)} positions at mate-in-{args.depth}, "
          f"drawn from {len(pool)} unused")
    print("excluded every position already present in benchmarks/")
    print()
    print("This set is now evidence. Measure it once, after the work it judges is")
    print("finished, and mark it spent in MANIFEST.json. Consulting it during")
    print("development destroys the only property that makes it worth quoting.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
