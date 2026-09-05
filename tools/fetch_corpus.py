#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (c) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Fetch the benchmark corpus that the position sets are drawn from.

MateProver does not distribute the corpus. It is a separate project under its
own licence, and shipping derived subsets of it would carry that licence's
obligations into this tree. Downloading it on your own machine, from its own
publisher, does not: this script only tells your machine where to look.

**The revision is pinned, and that is the point.** The corpus is maintained --
illegal positions removed, wrong mate values corrected -- so it changes over
time. Sampling seed 61 from this revision and seed 61 from a later one gives
DIFFERENT position sets, silently. A benchmark that drifts underneath its own
recorded seed is worse than one with no seed at all, because it still looks
reproducible. Hence the commit pin and the checksum below: a set is reproducible
only as (corpus revision, depth, count, seed), and all four are recorded.

    python tools/fetch_corpus.py

Writes benchmarks/corpus/matetrack.epd, which is gitignored. Re-running is a
no-op once the file is present and verifies.
"""

import argparse
import hashlib
import pathlib
import sys
import urllib.error
import urllib.request

HERE = pathlib.Path(__file__).resolve().parent
CORPUS_DIR = HERE.parent / "benchmarks" / "corpus"

# Pinned revision of the upstream corpus, and the digest of the file at it.
REPO = "vondele/matetrack"
COMMIT = "64914c01aa1f4deb2eb9774f4095e65bcfd61630"
FILENAME = "matetrack.epd"
SHA256 = "8ba5712234ba0cb2a1f1b1934872d1d30ba5e01c80482844dccccd4bc2eefd72"
POSITIONS = 6554

URL = f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/{FILENAME}"


def digest(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--force", action="store_true",
                    help="re-download even if a verified copy is present")
    args = ap.parse_args()

    target = CORPUS_DIR / FILENAME

    if target.exists() and not args.force:
        if digest(target) == SHA256:
            print(f"already present and verified: {target}")
            return 0
        print(f"{target} does not match the pinned digest; re-fetching",
              file=sys.stderr)

    CORPUS_DIR.mkdir(parents=True, exist_ok=True)
    print(f"fetching {FILENAME} at {COMMIT[:12]} ...")
    try:
        with urllib.request.urlopen(URL, timeout=60) as response:
            payload = response.read()
    except (urllib.error.URLError, TimeoutError) as exc:
        # An offline machine is an ordinary situation, not a crash.
        print(f"\ncould not fetch the corpus: {exc}\n\n"
              f"It is needed only to rebuild benchmark position sets; the test\n"
              f"suite does not use it. Fetch it manually if you prefer:\n\n"
              f"    {URL}\n\n"
              f"and save it as {target}", file=sys.stderr)
        return 1

    got = hashlib.sha256(payload).hexdigest()
    if got != SHA256:
        # Refuse rather than warn. A corpus that is not the pinned one produces
        # different sets from the same seeds, which is the exact failure this
        # pin exists to prevent.
        print(f"\ndigest mismatch -- refusing to write.\n"
              f"  expected {SHA256}\n  got      {got}\n\n"
              f"The pinned revision should be immutable, so this most likely\n"
              f"means the download was truncated or intercepted.", file=sys.stderr)
        return 2

    target.write_bytes(payload)
    lines = sum(1 for line in target.read_text(encoding="utf-8").splitlines()
                if line.strip())
    print(f"wrote {target} ({len(payload):,} bytes, {lines:,} positions)")
    if lines != POSITIONS:
        print(f"warning: expected {POSITIONS:,} positions", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
