#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Tests for tools/finder_lane.py -- the proposer must never be trusted.

The lane's whole justification is that an unreliable proposer cannot produce a
wrong answer, only a wasted check. That is a claim about what happens when the
proposer is WRONG, so testing it with a good engine tests the wrong thing: a
proposer that happens to be right proves nothing about the guard.

So the important cases here drive the lane with deliberately broken proposers,
written as tiny UCI scripts whose answers are known in advance:

    liar         claims `score mate 2` for every position, whatever it is
    mated        reports `score mate -3` -- the side to move is BEING mated,
                 which is not a solution and must never be read as one
    silent       never reports a mate at all

The liar is the load-bearing test. Fed positions whose mates are far deeper
than 2, every one of its claims must fail verification and the lane must
certify nothing. If that test passes with a bad proposer, the soundness
argument holds for any proposer.

`mated` exists because taking the absolute value of a UCI mate score is the
single most expensive mistake made in this project: a negative score means the
side to move is being mated, and reading it as a solution voided an entire
workstream. It is encoded here so it cannot come back silently.

Run directly:

    python tests/test_finder_lane.py --engine ./mateprover

Standalone by design, so it does not collide with the main suite while
run_tests.py is being edited. To fold it in later, call `run(res, engine)` from
run_tests.py with its Results object; the signature already matches.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
LANE = REPO / "tools" / "finder_lane.py"
VERIFIER = REPO / "tools" / "verify_proof.py"

try:
    import chess  # noqa: F401
    HAVE_CHESS = True
except ImportError:
    # Without python-chess the independent re-derivation cannot run. It is
    # SKIPPED rather than silently passed: the suite's count guard counts
    # skips, so a missing dependency stays visible instead of shrinking the
    # total. Run under ~/.venv-mateprover/bin/python to get the real check.
    HAVE_CHESS = False

# Positions with a known, and deliberately not-2, mate distance.
DEEP = [
    ("8/2k5/8/4K1R1/8/8/6Q1/8 w - -", 3),
    ("8/8/4Q3/8/2NK4/8/2k5/8 w - -", 3),
    ("6k1/4n2R/8/8/8/3R4/8/6K1 w - -", 3),
    ("8/6K1/6R1/7R/8/8/3k4/8 w - -", 4),
]

FAKE = '''import sys
def emit(s):
    sys.stdout.write(s + "\\n")
    sys.stdout.flush()
for line in sys.stdin:
    line = line.strip()
    if line == "uci":
        emit("id name fake"); emit("uciok")
    elif line == "isready":
        emit("readyok")
    elif line.startswith("go"):
        %s
        emit("bestmove a1a2")
    elif line == "quit":
        break
'''
BEHAVIOUR = {
    # Always claims a mate in 2, whatever the position really is.
    "liar": 'emit("info depth 4 score mate 2 pv a1a2")',
    # Reports that the side to move is being mated. Not a solution.
    "mated": 'emit("info depth 4 score mate -3 pv a1a2")',
    # Searches and finds nothing.
    "silent": 'emit("info depth 4 score cp 15 pv a1a2")',
    # Echoes back the distance it was asked for. Since the lane asks for the
    # depth on the EPD line and every position here really is mate in that
    # many, this is a proposer that is always RIGHT -- which exercises the
    # verified lane end to end without needing a real engine, and keeps the
    # check count deterministic for the suite's own count guard.
    "honest": 'emit("info depth 4 score mate %s pv a1a2" % line.split()[2])',
}


class Results:
    """Minimal stand-in for run_tests.py's Results, so this can be folded in."""

    def __init__(self) -> None:
        self.passed = self.failed = self.skipped = 0

    def ok(self, name: str) -> None:
        self.passed += 1
        print(f"  ok    {name}")

    def fail(self, name: str, detail: str) -> None:
        self.failed += 1
        print(f"  FAIL  {name}: {detail}")

    def skip(self, name: str, why: str) -> None:
        self.skipped += 1
        print(f"  skip  {name} ({why})")

    def check(self, name: str, cond: bool, detail: str = "") -> None:
        self.ok(name) if cond else self.fail(name, detail)


def write_fake(tmp: Path, kind: str) -> Path:
    """A UCI engine whose answer is known in advance."""
    src = tmp / f"fake_{kind}.py"
    src.write_text(FAKE % BEHAVIOUR[kind], encoding="utf-8")
    sh = tmp / f"fake_{kind}"
    sh.write_text(f'#!/bin/sh\nexec "{sys.executable}" "{src}"\n', encoding="utf-8")
    sh.chmod(0o755)
    return sh


def run_lane(engine: str, epd: Path, extra: list[str]) -> tuple[int, str, str]:
    proc = subprocess.run(
        [sys.executable, str(LANE), "--mateprover", engine, str(epd), *extra],
        capture_output=True, text=True, errors="replace", timeout=1800)
    return proc.returncode, proc.stdout, proc.stderr


def lanes(stderr: str) -> dict[str, int]:
    tally = {"minimal": 0, "verified": 0, "rejected": 0, "unsolved": 0}
    for line in stderr.splitlines():
        m = re.match(r"^\s+d\d+\s+(minimal|verified|rejected|unsolved)\s", line)
        if m:
            tally[m.group(1)] += 1
    return tally


def run(res: Results, engine: str) -> None:
    print("\n[finder lane] tools/finder_lane.py never trusts its proposer")
    if not LANE.exists():
        res.skip("finder lane", "tools/finder_lane.py not present")
        return

    tmp = Path(tempfile.mkdtemp(prefix="finder_lane_test_"))
    try:
        epd = tmp / "deep.epd"
        epd.write_text("".join(f"{fen} ; dm {d}\n" for fen, d in DEEP),
                       encoding="utf-8")

        # --- 1. prover alone: every one of these is provable ---------------
        code, out, err = run_lane(engine, epd, ["--prove-nodes", "5000000"])
        t = lanes(err)
        res.check("prover-only mode certifies what it can prove",
                  code == 0 and t["minimal"] == len(DEEP),
                  f"exit {code}, tally {t}")
        res.check("proved lines are tagged `lane minimal`",
                  out.count("lane minimal;") == len(DEEP),
                  f"{out.count('lane minimal;')} of {len(DEEP)} tagged")
        res.check("a proved line carries a certificate",
                  out.count("proof {") == len(DEEP),
                  f"{out.count('proof {')} certificates")

        # --- 2. THE LOAD-BEARING TEST: a proposer that lies ----------------
        # Every position here has a mate deeper than 2, so every claim of
        # "mate in 2" is false and must be refused.
        liar = write_fake(tmp, "liar")
        code, out, err = run_lane(engine, epd, [
            "--prove-nodes", "100", "--finder", str(liar),
            "--verify-nodes", "2000000"])
        t = lanes(err)
        # Assertions are relative to how many positions actually REACHED the
        # proposer. A small --prove-nodes does not guarantee stage 1 fails --
        # these mates are cheap and the prover may still get some -- and a test
        # that assumed otherwise would fail for a reason unrelated to what it
        # is checking.
        offered = len(DEEP) - t["minimal"]
        res.check("a lying proposer certifies NOTHING",
                  t["verified"] == 0, f"tally {t}")
        res.check("every false claim is recorded as rejected",
                  offered > 0 and t["rejected"] == offered,
                  f"tally {t}, {offered} offered")
        res.check("no `lane verified` line survives a lying proposer",
                  "lane verified;" not in out, "a false claim was certified")

        # --- 3. a negative mate score is not a solution --------------------
        mated = write_fake(tmp, "mated")
        code, out, err = run_lane(engine, epd, [
            "--prove-nodes", "100", "--finder", str(mated)])
        t = lanes(err)
        offered = len(DEEP) - t["minimal"]
        res.check("`score mate -N` is never read as a claim",
                  t["rejected"] == 0 and t["verified"] == 0
                  and t["unsolved"] == offered,
                  f"tally {t} -- a negative score was treated as a solution")

        # --- 4. no mate reported at all ------------------------------------
        silent = write_fake(tmp, "silent")
        code, out, err = run_lane(engine, epd, [
            "--prove-nodes", "100", "--finder", str(silent)])
        t = lanes(err)
        offered = len(DEEP) - t["minimal"]
        res.check("a proposer that finds nothing yields nothing",
                  t["unsolved"] == offered and t["verified"] == 0,
                  f"tally {t}")

        # --- 5. malformed option is refused, not ignored -------------------
        code, out, err = run_lane(engine, epd, [
            "--finder", str(silent), "--finder-option", "NoEqualsSign"])
        res.check("a malformed --finder-option is refused", code == 2,
                  f"exit {code}")

        # --- 6. an honest proposer: the verified lane, end to end ----------
        honest = write_fake(tmp, "honest")
        code, out, err = run_lane(engine, epd, [
            "--prove-nodes", "100", "--finder", str(honest),
            "--verify-nodes", "5000000"])
        t = lanes(err)
        res.check("an honest proposer produces verified finds",
                  t["verified"] > 0, f"tally {t}")
        res.check("verified lines are tagged `lane verified`",
                  out.count("lane verified;") == t["verified"],
                  f"{out.count('lane verified;')} tags, {t['verified']} finds")

        # --- 7. both lanes re-derive under the independent checker ---------
        # --expect pins the count, so a producer that died part way through
        # and left a valid prefix cannot pass this.
        if not VERIFIER.exists() or not HAVE_CHESS:
            res.skip("every certificate re-derives independently",
                     "verifier or python-chess absent")
        else:
            got = tmp / "out.epd"
            got.write_text(out, encoding="utf-8")
            total = t["minimal"] + t["verified"]
            proc = subprocess.run(
                [sys.executable, str(VERIFIER), "--quiet", "--require-proof",
                 "--expect", str(total), str(got)],
                capture_output=True, text=True, errors="replace", timeout=600)
            res.check("every certificate re-derives independently",
                      proc.returncode == 0,
                      (proc.stdout.strip().splitlines() or ["no output"])[-1])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def run_with_real_finder(res: Results, engine: str, finder: str) -> None:
    """Standalone extra: the same path driven by an actual engine.

    Deliberately NOT part of run(), because it needs a binary the suite cannot
    assume and would make the check count depend on what is installed.
    """
    print("\n[finder lane] a real proposer, for standalone use")
    tmp = Path(tempfile.mkdtemp(prefix="finder_lane_real_"))
    try:
        epd = tmp / "deep.epd"
        epd.write_text("".join(f"{fen} ; dm {d}\n" for fen, d in DEEP),
                       encoding="utf-8")
        code, out, err = run_lane(engine, epd, [
            "--prove-nodes", "100", "--finder", finder,
            "--finder-option", "Threads=1",
            "--find-nodes", "2000000", "--verify-nodes", "2000000"])
        t = lanes(err)
        res.check("a real proposer produces verified finds",
                  t["verified"] > 0, f"tally {t}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", default=os.environ.get("MATEPROVER", "mateprover"),
                    help="path to the mateprover binary")
    ap.add_argument("--finder", default=None,
                    help="optional real UCI engine, to exercise the honest path")
    args = ap.parse_args()

    if not shutil.which(args.engine) and not Path(args.engine).exists():
        print(f"engine not found: {args.engine}")
        return 2

    res = Results()
    run(res, args.engine)
    if args.finder and Path(args.finder).exists():
        run_with_real_finder(res, args.engine, args.finder)
    print(f"\n{res.passed} passed, {res.failed} failed, {res.skipped} skipped")
    return 1 if res.failed else 0


if __name__ == "__main__":
    sys.exit(main())
