#!/usr/bin/env python3
"""Test whether one root move solves a position, without searching the rest.

Restricting the root is a RESTRICTED LANE in the sense of section 111, and the
asymmetry is the whole point:

    every reply loses   -> an upper bound is PROVED. Exhibiting a strategy is
                           enough, so this is as sound as the full search.
    one reply survives  -> the candidate is dead, and NOTHING is proved about
                           any other root move. A restricted lane that finds
                           nothing has found nothing.

That bought the exact value of d(3) under cap3:126 for a twentieth of the work:
`1.b3` against all twenty replies took 6h37m where the unrestricted depth-9
search would have been an estimated ~2 trillion nodes.

FOUR THINGS THIS DOES THAT THE FIRST ATTEMPT DID NOT
====================================================

RAW OUTPUT IS KEPT. The first version parsed `bm` and `dm` out of each result
line, printed those, and let the rest fall on the floor -- including the
principal variations, which then cost hours to recompute. Every engine
invocation's complete output is written to its own file BEFORE anything is
parsed out of it, and a run that yields no verdict line is reported as broken
rather than as a result.

ITERATIVE DEEPENING, not --direct-depth. Measured 12x: replies that lose in
seven never pay for eight, and under these rules depth 8 costs ~70x depth 7.

TWO PHASES. Reply cost is sharply bimodal -- 47 s against 3785 s, 81x apart,
with nothing in between -- so a short first pass resolves three quarters of them
and tells you exactly how many expensive ones are left. It also front-loads the
chance of finding the refuting reply, which is what kills a doomed candidate
early.

INDEPENDENT LANES. Measured 1.69x: four searches at six threads beat one at
twenty-four, and the node count FALLS 25% because a wide search on one problem
speculates where four narrow ones on four problems do not.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
START = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - -"
VERDICT = re.compile(r"acn (\d+); acs ([\d.]+)")
SOLVED = re.compile(r"bm (\S+); dm (\d+)")


def replies_after(fen: str, moves: list[str]) -> list[tuple[str, str]]:
    """Every legal reply to `moves` played from `fen`, as (san, child_fen4).

    python-chess is OPTIONAL here exactly as it is in tests/run_tests.py: it is
    a convenience for generating positions, never part of any verdict.
    """
    try:
        import chess  # type: ignore
    except ImportError:
        raise SystemExit(
            "this mode needs python-chess (pip install chess) to enumerate replies.\n"
            "Without it, pass positions directly with --epd.")
    board = chess.Board(fen + " 0 1" if len(fen.split()) == 4 else fen)
    for m in moves:
        board.push_san(m)
    out = []
    for mv in board.legal_moves:
        child = board.copy()
        san = board.san(mv)
        child.push(mv)
        out.append((san, child.fen().split()[0] + " w - -"
                    if child.turn else child.fen().split()[0] + " b - -"))
    return out


def run(engine: Path, fen: str, depth: int, rule: list[str], threads: int, mb: int,
        logdir: Path, tag: str, time_limit: float | None) -> dict:
    args = [str(engine), *rule, "--threads", str(threads), "-M", str(mb)]
    if time_limit:
        args += ["--time-limit", str(time_limit)]
    else:
        args += ["--no-portfolio"]
    t0 = time.time()
    p = subprocess.run(args + ["-"], input=f"{fen} ; dm {depth}\n",
                       capture_output=True, text=True, timeout=None)
    secs = time.time() - t0
    blob = p.stdout + p.stderr
    (logdir / f"{tag}.log").write_text(blob, encoding="utf-8")   # RAW, first
    lines = [l for l in blob.splitlines() if "acn " in l and "progress" not in l]
    if len(lines) != 1:
        return {"tag": tag, "broken": True, "secs": secs, "raw": blob[:300]}
    line = lines[0]
    won = SOLVED.search(line)
    nodes = int(VERDICT.search(line).group(1))
    return {"tag": tag, "broken": False, "secs": secs, "nodes": nodes,
            "won": bool(won), "dm": int(won.group(2)) if won else None,
            "pv": line.split("; pv ", 1)[1].rstrip(";") if "; pv " in line else None,
            "line": line}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", type=Path, default=ROOT / "build" / "mateprover.exe")
    ap.add_argument("--fen", default=START)
    ap.add_argument("--move", required=True, help="candidate root move in SAN, e.g. b3")
    ap.add_argument("--depth", type=int, required=True,
                    help="TOTAL depth being tested; each reply is searched at depth-1")
    ap.add_argument("--rule", nargs="+", default=["--captures", "3:126"])
    ap.add_argument("--lanes", type=int, default=4)
    ap.add_argument("--threads", type=int, default=6, help="per lane; 4x6 measured best")
    ap.add_argument("--mb", type=int, default=4000, help="per lane")
    ap.add_argument("--probe-seconds", type=float, default=120.0, help="0 disables phase 1")
    ap.add_argument("--logdir", type=Path, default=ROOT / "build" / "candidates")
    args = ap.parse_args()

    args.logdir.mkdir(parents=True, exist_ok=True)
    rs = replies_after(args.fen, [args.move])
    sub_depth = args.depth - 1
    print(f"1.{args.move}: {len(rs)} replies, each needs a White win in {sub_depth}")
    print(f"logs -> {args.logdir}\n")

    done: dict[str, dict] = {}
    pending = list(rs)

    if args.probe_seconds > 0:
        print(f"--- phase 1: {args.probe_seconds:.0f}s cap over all {len(pending)} ---")
        with ThreadPoolExecutor(max_workers=args.lanes) as ex:
            res = list(ex.map(lambda r: run(args.engine, r[1], sub_depth, args.rule,
                                            args.threads, args.mb, args.logdir,
                                            f"probe_{r[0]}", args.probe_seconds), pending))
        still = []
        for (san, fen), r in zip(pending, res):
            if r["broken"]:
                print(f"  {san:<6} BROKEN -- no verdict line; see {r['tag']}.log")
                return 2
            if r["won"]:
                done[san] = r
                print(f"  {san:<6} WIN dm {r['dm']}  ({r['nodes']:,} nodes, {r['secs']:.0f}s)")
            else:
                still.append((san, fen))
        print(f"  resolved {len(done)}/{len(rs)}; {len(still)} left\n")
        pending = still

    if pending:
        print(f"--- phase 2: full search on {len(pending)} ---")
        with ThreadPoolExecutor(max_workers=args.lanes) as ex:
            futs = {ex.submit(run, args.engine, fen, sub_depth, args.rule, args.threads,
                              args.mb, args.logdir, f"full_{san}", None): san
                    for san, fen in pending}
            for f in futs:
                san = futs[f]
                r = f.result()
                if r["broken"]:
                    print(f"  {san:<6} BROKEN -- no verdict line; see {r['tag']}.log")
                    return 2
                if not r["won"]:
                    print(f"  {san:<6} *** SURVIVES *** -- 1.{args.move} is REFUTED")
                    print(f"  This proves nothing about any other root move (111).")
                    return 1
                done[san] = r
                print(f"  {san:<6} WIN dm {r['dm']}  ({r['nodes']:,} nodes, {r['secs']:.0f}s)")

    total = sum(r["nodes"] for r in done.values())
    print(f"\nALL {len(rs)} replies lose. **1.{args.move} SOLVES depth {args.depth}.**")
    print(f"{total:,} nodes. Sound as an UPPER BOUND: a strategy is exhibited (111).")
    print("\nPrincipal variations:")
    for san in (s for s, _ in rs):
        r = done[san]
        print(f"  1...{san:<6} dm {r['dm']}  {r['pv'] or '(none reported)'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
