#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Certificates for positions mateprover cannot prove alone: propose, then verify.

mateprover proves the SHORTEST mate and can prove ABSENCE, but it is a weak
finder -- 92 solved against 131 and 140 for two Stockfish forks over the same
180 positions. Those forks find quickly but routinely report a LONGER mate than
the shortest, so their output is a claim, not a result.

This lane turns those complementary failures into a combination. Where
mateprover fails within its budget, an external finder proposes a mate and
mateprover *verifies* it with --direct-depth, which asks only "is there a mate
within N" and so costs a fraction of proving the shortest. Measured over 60
positions at depths 12-18:

    mateprover alone                     7/60
    with a verified finder lane         31/60      (+24, 4.4x)
    claims made 41, verified 24, REJECTED 17

Seventeen rejected claims is the point, not a defect. The lane is **sound by
construction**: an unreliable proposer cannot produce a wrong answer, only a
wasted check. Nothing is trusted -- every position reported here carries a
certificate that tools/verify_proof.py will re-derive from scratch.

It replicates. On an independent sample of 40 positions the finder claimed on
77% of what the prover missed, against 77% before, and 53% of those verified
against 59% before.

The verification ceiling is measured rather than assumed. Cost is bimodal --
median 90K nodes, maximum 20.4M -- so the ceiling is irrelevant to a typical
claim and only ever buys the tail. Because a ceiling is spent only on
FAILURES, 1M verifies exactly as many claims as 4M did while wasting a quarter
as much work, which is why that is the default.

WHAT THIS DOES NOT DO
---------------------
A verified find is **not** a minimality proof. Stage 1 proves "the shortest
mate is N". Stage 3 proves only "a mate exists within N". Both are certified
and neither is a guess, but they are different claims, and the `lane` opcode on
every output line says which one you have. Do not aggregate them into a single
"solved" number without saying so.

Minimality coverage is unchanged by this tool and cannot be improved by it:
proving the shortest mate requires proving ABSENCE at every distance below it,
which is 99.3% of the work, and knowing the answer in advance saves none of it.

The gain is also band-limited, though not as sharply as first assumed. It is
largest at depths 12-18 (+24 of 60) and still present but roughly ten times
weaker across d20-40 (+3 of 47), where what fails is the PROPOSER -- it claims
on 32% of missed positions against 77% shallow -- rather than verification. Past
41 plies nothing is left: mateprover scores 0/20 with --direct-depth as well as
--iterative-depth, so there is nothing for verification to confirm.

USAGE
-----
    python tools/finder_lane.py --finder ./hunt18 positions.epd > proofs.epd
    python tools/verify_proof.py --require-proof proofs.epd

The second line is not optional if you intend to publish the results. This tool
verifies with mateprover; verify_proof.py re-derives every move independently
with python-chess, and is what makes the output checkable by someone who does
not trust either engine.

Input is EPD with a `dm N` opcode giving the target depth, the format used by
tests/mates.epd. Output is EPD with mateprover's own `proof` opcode appended.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import re
import subprocess
import sys
import threading

# A mate score from a UCI engine is signed. A NEGATIVE one means the side to
# move is BEING mated, which is the opposite of a solution. Accepting abs()
# here silently converts losses into claimed solves.
UCI_MATE = re.compile(r"\bscore\s+mate\s+(-?\d+)\b")
EPD_DM = re.compile(r"\bdm\s+(\d+)")
OUT_DM = re.compile(r"\bdm\s+(\d+)")


class Finder:
    """A UCI engine asked for a candidate mate. Its answer is never trusted.

    Driven over a live pipe rather than by redirecting a file into stdin: an
    engine whose stdin is already at EOF will emit an immediate `bestmove`
    without searching, which looks exactly like a fast failure to find.
    """

    def __init__(self, path: str, options: dict[str, str], timeout: float):
        self.path = path
        self.options = options
        self.timeout = timeout

    def claim(self, fen: str, depth: int, nodes: int) -> int | None:
        """Best mate distance the finder reports, or None. Bounded by `depth`."""
        proc = subprocess.Popen(
            [self.path], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, text=True, bufsize=1, errors="replace")
        lines: list[str] = []
        done = threading.Event()

        def reader() -> None:
            try:
                for line in proc.stdout:            # type: ignore[union-attr]
                    lines.append(line)
                    if line.startswith("bestmove"):
                        break
            finally:
                done.set()

        threading.Thread(target=reader, daemon=True).start()
        commands = ["uci", "isready"]
        commands += ["setoption name %s value %s" % kv
                     for kv in self.options.items()]
        commands += ["isready", "ucinewgame", "position fen %s 0 1" % fen,
                     "go mate %d nodes %d" % (depth, nodes)]
        try:
            proc.stdin.write("\n".join(commands) + "\n")   # type: ignore[union-attr]
            proc.stdin.flush()                             # type: ignore[union-attr]
            done.wait(timeout=self.timeout)
        except OSError:
            pass
        finally:
            try:
                proc.stdin.write("quit\n")                 # type: ignore[union-attr]
                proc.stdin.flush()                         # type: ignore[union-attr]
                proc.wait(timeout=5)
            except (OSError, subprocess.TimeoutExpired):
                proc.kill()

        best = None
        for line in lines:
            m = UCI_MATE.search(line)
            if not m:
                continue
            value = int(m.group(1))
            if 0 < value <= depth:      # positive only; see UCI_MATE above
                best = value if best is None else min(best, value)
        return best


class Prover:
    """mateprover, run for a verdict plus a certificate."""

    def __init__(self, path: str, extra: list[str], timeout: float):
        self.path = path
        self.extra = extra
        self.timeout = timeout

    def run(self, fen: str, depth: int, mode: str, nodes: int) -> tuple[int | None, str]:
        """Return (proved depth or None, the engine's EPD output line)."""
        argv = [self.path, "--emit-proof", mode, "-z", str(depth),
                "--node-limit", str(nodes), *self.extra, "-"]
        try:
            proc = subprocess.run(
                argv, input="%s bm #%d;\n" % (fen, depth), capture_output=True,
                text=True, errors="replace", timeout=self.timeout)
        except subprocess.TimeoutExpired:
            return None, ""
        out = (proc.stdout or "").strip()
        line = next((l for l in out.splitlines() if l.strip()
                     and not l.startswith("#")), "")
        m = OUT_DM.search(line)
        return (int(m.group(1)) if m else None), line


def annotate(line: str, lane: str, claim: int | None) -> str:
    """Tag an engine EPD line with its provenance, as an EPD opcode."""
    line = line.rstrip()
    if not line.endswith(";"):
        line += ";"
    tag = " lane %s;" % lane
    if claim is not None:
        tag += " claimed %d;" % claim
    return line + tag


def positions(handle) -> list[tuple[str, int]]:
    out = []
    for raw in handle:
        raw = raw.strip()
        if not raw or raw.startswith("#"):
            continue
        m = EPD_DM.search(raw)
        if not m:
            continue
        fen = raw.split(";")[0].strip()
        out.append((fen, int(m.group(1))))
    return out


def solve(fen: str, depth: int, prover: Prover, finder: Finder | None,
          args) -> tuple[str, str, int | None]:
    """Three stages. Returns (lane, output line, finder claim if any)."""
    got, line = prover.run(fen, depth, "--iterative-depth", args.prove_nodes)
    if got is not None:
        return "minimal", line, None
    if finder is None:
        return "unsolved", "", None

    claim = finder.claim(fen, depth, args.find_nodes)
    if claim is None:
        return "unsolved", "", None

    got, line = prover.run(fen, claim, "--direct-depth", args.verify_nodes)
    if got is not None:
        return "verified", line, claim
    return "rejected", "", claim


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", nargs="?", default="-",
                    help="EPD file with `dm N` opcodes (default: stdin)")
    ap.add_argument("--mateprover", default="mateprover",
                    help="path to the mateprover binary")
    ap.add_argument("--finder", default=None,
                    help="path to a UCI engine to propose mates. Without one "
                         "this degrades to a plain mateprover run, which is a "
                         "useful baseline but not the point of the tool")
    ap.add_argument("--finder-option", action="append", default=[],
                    metavar="NAME=VALUE",
                    help="UCI option for the finder; repeatable")
    ap.add_argument("--prove-nodes", type=int, default=20_000_000,
                    help="stage 1 budget, proving the shortest mate")
    ap.add_argument("--find-nodes", type=int, default=20_000_000,
                    help="stage 2 budget, the finder's search")
    ap.add_argument("--verify-nodes", type=int, default=1_000_000,
                    help="stage 3 ceiling. Measured, not guessed: verification "
                         "cost is bimodal (median 90K nodes, max 20.4M), so "
                         "this buys nothing for a typical claim and only ever "
                         "reaches the tail. A ceiling is spent only on "
                         "FAILURES, and 1M verifies exactly as many claims as "
                         "4M for a quarter of the wasted work. Raise it to "
                         "trade a lot of work for a few tail positions")
    ap.add_argument("--mateprover-arg", action="append", default=[],
                    help="extra argument passed through to mateprover; "
                         "repeatable")
    ap.add_argument("--timeout", type=float, default=900.0,
                    help="per-stage wall-clock guard, seconds. Budgets are in "
                         "NODES so runs stay reproducible; this only stops a "
                         "hang")
    ap.add_argument("--jobs", type=int, default=1,
                    help="positions to process concurrently. Each job is "
                         "single-threaded, so this trades cores for wall time "
                         "without changing any result")
    ap.add_argument("--quiet", action="store_true",
                    help="suppress the per-position progress on stderr")
    args = ap.parse_args()

    options = {}
    for item in args.finder_option:
        if "=" not in item:
            sys.stderr.write("bad --finder-option %r, expected NAME=VALUE\n" % item)
            return 2
        name, value = item.split("=", 1)
        options[name] = value

    handle = sys.stdin if args.input == "-" else open(args.input, encoding="utf-8")
    try:
        cases = positions(handle)
    finally:
        if handle is not sys.stdin:
            handle.close()
    if not cases:
        sys.stderr.write("no positions with a `dm N` opcode in %s\n" % args.input)
        return 2

    prover = Prover(args.mateprover, args.mateprover_arg, args.timeout)
    finder = Finder(args.finder, options, args.timeout) if args.finder else None

    tally = {"minimal": 0, "verified": 0, "rejected": 0, "unsolved": 0}

    def work(item):
        fen, depth = item
        return (fen, depth) + solve(fen, depth, prover, finder, args)

    # as_completed, not map: map yields in submission order, so one slow
    # position holds back every line behind it and a run killed halfway leaves
    # nothing. Output is therefore unordered, which costs nothing -- each line
    # carries its own FEN.
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [pool.submit(work, item) for item in cases]
        for future in concurrent.futures.as_completed(futures):
            fen, depth, lane, line, claim = future.result()
            tally[lane] += 1
            if line:
                print(annotate(line, lane, claim), flush=True)
            if not args.quiet:
                note = "" if claim is None else " (claimed %d)" % claim
                sys.stderr.write("  d%-3d %-9s %s%s\n" % (depth, lane, fen, note))
                sys.stderr.flush()

    total = len(cases)
    certified = tally["minimal"] + tally["verified"]
    sys.stderr.write(
        "\n  %d positions: %d certified (%d proved shortest, %d verified finds)\n"
        % (total, certified, tally["minimal"], tally["verified"]))
    sys.stderr.write(
        "  %d claims rejected, %d unsolved\n" % (tally["rejected"], tally["unsolved"]))
    if tally["verified"]:
        sys.stderr.write(
            "  the %d verified finds prove a mate WITHIN the claimed distance,\n"
            "  not that it is the shortest. Check the `lane` opcode before\n"
            "  combining them with the %d minimal proofs.\n"
            % (tally["verified"], tally["minimal"]))
    if finder is None:
        sys.stderr.write("  no --finder given, so stages 2 and 3 did not run.\n")
    sys.stderr.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
