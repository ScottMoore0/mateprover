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
import hashlib
import json
import pathlib
import random
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
BENCH = HERE.parent / "benchmarks"
MANIFEST = BENCH / "MANIFEST.json"


def positions_in(paths):
    seen = set()
    for path in sorted(paths):
        for line in pathlib.Path(path).read_text().splitlines():
            line = line.strip()
            if line.startswith("{"):
                fen = json.loads(line).get("fen4")
                if fen:
                    seen.add(fen)
    return seen


def already_used(explicit):
    """Positions to exclude, and the names they came from.

    Exclusion used to be read implicitly from whatever happened to be sitting in
    benchmarks/. That made a set depend on directory state rather than on its
    recorded parameters, and the dependence is decisive rather than marginal:
    rebuilding d16_dev40 with its sibling present reproduces it exactly, and
    without the sibling gives a set sharing NOT ONE position of forty.

    So the sets used are recorded in the manifest, and `--exclude` states them
    explicitly on a rebuild. The directory scan remains the default because it
    is the right behaviour when minting something new.
    """
    if explicit is not None:
        paths = [BENCH / name for name in explicit]
        missing = [p.name for p in paths if not p.exists()]
        if missing:
            raise SystemExit(f"--exclude names sets that are not present: {missing}")
        return positions_in(paths), sorted(p.name for p in paths)
    paths = sorted(BENCH.glob("*.jsonl"))
    return positions_in(paths), sorted(p.name for p in paths)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--corpus", type=pathlib.Path, required=True)
    parser.add_argument("--depth", type=int, required=True)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--name", required=True, help="file stem, written under benchmarks/")
    parser.add_argument("--seed", type=int, default=None,
                        help="defaults to the count, so a run is reproducible from the manifest")
    parser.add_argument("--exclude-hashes", type=pathlib.Path, default=None, metavar="FILE",
                        help="also exclude positions whose SHA-256 appears in FILE. Lets a "
                             "set exclude everything previously seen without those earlier "
                             "sets being present, or shipped")
    parser.add_argument("--exclude", action="append", default=None, metavar="SET.jsonl",
                        help="exclude exactly these sets rather than whatever is in "
                             "benchmarks/. Repeatable. Use the manifest's `excludes` "
                             "list to rebuild a recorded set faithfully")
    args = parser.parse_args()

    if not args.corpus.exists():
        print(f"corpus not found: {args.corpus}", file=sys.stderr)
        return 2

    target = BENCH / f"{args.name}.jsonl"
    if target.exists():
        print(f"refusing to overwrite {target.name}: a minted set is spent the "
              f"first time it is measured, so it is never regenerated", file=sys.stderr)
        return 2

    used, excluded_names = already_used(args.exclude)

    # A digest fingerprints a position without containing one, so a list of them
    # can ship where the positions themselves cannot. That is what lets a fresh
    # set exclude everything previously consulted and still be rebuildable by
    # someone who holds none of the earlier sets.
    excluded_hashes = set()
    if args.exclude_hashes:
        excluded_hashes = {line.strip() for line
                           in args.exclude_hashes.read_text(encoding="utf-8").splitlines()
                           if line.strip() and not line.startswith("#")}
    pool = []
    for line in args.corpus.read_text(encoding="utf-8", errors="replace").splitlines():
        found = re.search(r"bm #(-?\d+)", line)
        if not found or int(found.group(1)) != args.depth:
            continue
        fen4 = " ".join(line.split()[:4])
        if fen4 in used:
            continue
        if excluded_hashes and hashlib.sha256(
                fen4.encode("utf-8")).hexdigest() in excluded_hashes:
            continue
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
        "excludes": excluded_names,
        "exclude_hashes": args.exclude_hashes.name if args.exclude_hashes else None,
        "sha256": hashlib.sha256(
            "".join(json.dumps(row) + "\n" for row in rows).encode("utf-8")).hexdigest(),
        "status": "unused",
    }
    manifest = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else []
    manifest.append(entry)
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"minted {target.name}: {len(rows)} positions at mate-in-{args.depth}, "
          f"drawn from {len(pool)} unused")
    print(f"excluded {len(used)} positions from {len(excluded_names)} set(s): "
          f"{', '.join(excluded_names) or 'none'}")

    # If the manifest already records a digest under this name, the rebuild can
    # be checked rather than trusted. This is what makes a set reproducible in
    # practice: not that the recipe looks complete, but that the result matches.
    for previous in manifest[:-1]:
        if previous.get("name") == target.name and previous.get("sha256"):
            if previous["sha256"] == entry["sha256"]:
                print("MATCHES the digest recorded for this name: rebuild is faithful")
            else:
                print("WARNING: does NOT match the digest recorded for this name.\n"
                      "         Check --exclude and the corpus revision "
                      "(tools/fetch_corpus.py pins one).")
            break
    print()
    print("This set is now evidence. Measure it once, after the work it judges is")
    print("finished, and mark it spent in MANIFEST.json. Consulting it during")
    print("development destroys the only property that makes it worth quoting.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
