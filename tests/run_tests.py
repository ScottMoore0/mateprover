#!/usr/bin/env python3
"""Self-contained test suite for the echest directmate prover.

The core tests deliberately have no third-party dependencies so they run
anywhere CMake/CTest does. If `python-chess` happens to be installed, an extra
layer of independent verification is enabled automatically: every reported PV is
replayed for legality and checked to end in real checkmate, and every emitted
proof certificate is verified to enumerate exactly the legal defender replies.

Usage:
    python run_tests.py --engine /path/to/echest
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

DM_RE = re.compile(r"\bdm\s+(\d+)\b")
BM_RE = re.compile(r"\bbm\s+([^;]+);")
PV_RE = re.compile(r"\bpv\s+([^;]+);")
NODES_RE = re.compile(r"\bnodes\s+(\d+)\b")
PROOF_RE = re.compile(r"\bproof\s+(\{.*\})\s*;", re.DOTALL)

# Reference perft counts. These are the standard published positions; they
# exercise castling rights, en-passant capture and expiry, promotion including
# under-promotion, and pinned-piece legality.
PERFT_CASES = [
    ("startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
     [20, 400, 8902, 197281, 4865609]),
    ("kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
     [48, 2039, 97862, 4085603]),
    ("position3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -",
     [14, 191, 2812, 43238, 674624]),
    ("position4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -",
     [6, 264, 9467, 422333]),
    ("position5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ -",
     [44, 1486, 62379, 2103487]),
    ("position6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - -",
     [46, 2079, 89890, 3894594]),
]

BASE_ARGS = [
    "-b", "-1", "-5",
    "--move-reserve-cap", "96",
    "--inplace-order",
    "--proof-hints",
    "--keep-iter-tt",
    "--ordered-check-shortcut",
]

try:
    import chess  # type: ignore
    HAVE_CHESS = True
except ImportError:  # pragma: no cover - depends on environment
    HAVE_CHESS = False


class Results:
    def __init__(self) -> None:
        self.passed = 0
        self.failed: list[str] = []
        self.skipped: list[str] = []

    def check(self, name: str, ok: bool, detail: str = "") -> None:
        if ok:
            self.passed += 1
            print(f"  PASS  {name}")
        else:
            self.failed.append(f"{name}: {detail}")
            print(f"  FAIL  {name}  {detail}")

    def skip(self, name: str, why: str) -> None:
        self.skipped.append(name)
        print(f"  SKIP  {name} ({why})")


def run(engine: Path, args: list[str], stdin: str, timeout: float = 600.0) -> str:
    proc = subprocess.run(
        [str(engine), *args],
        input=stdin.encode(),
        capture_output=True,
        timeout=timeout,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"engine exited {proc.returncode}: {proc.stderr.decode()[:500]}"
        )
    return proc.stdout.decode()


def load_epd(path: Path) -> list[tuple[str, int]]:
    cases = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        fen, _, rest = line.partition(";")
        match = DM_RE.search(rest)
        if not match:
            raise ValueError(f"{path.name}: line has no dm token: {line!r}")
        cases.append((fen.strip(), int(match.group(1))))
    return cases


def solve(engine: Path, cases: list[tuple[str, int]], extra: list[str]) -> list[str]:
    stdin = "".join(f"{fen} bm #{dm};\n" for fen, dm in cases)
    out = run(engine, [*BASE_ARGS, *extra, "-"], stdin)
    lines = [l for l in out.splitlines() if l.strip()]
    if len(lines) != len(cases):
        raise RuntimeError(f"expected {len(cases)} result lines, got {len(lines)}")
    return lines


def test_perft(engine: Path, res: Results) -> None:
    print("\n[movegen] perft against reference counts")
    for name, fen, expected in PERFT_CASES:
        out = run(engine, ["--perft", str(len(expected))], fen)
        got = [int(m) for m in NODES_RE.findall(out)]
        res.check(f"perft {name}", got == expected, f"got {got}, expected {expected}")


def test_known_mates(engine: Path, res: Results) -> None:
    print("\n[proof] known directmates are found at the exact depth")
    cases = load_epd(HERE / "mates.epd")
    lines = solve(engine, cases, [])
    for (fen, dm), line in zip(cases, lines):
        match = DM_RE.search(line)
        if not match:
            res.check(f"mate {fen[:28]}", False, "no dm reported")
            continue
        got = int(match.group(1))
        pv = PV_RE.search(line)
        pv_len = len(pv.group(1).split()) if pv else 0
        ok = got == dm and pv_len == 2 * dm - 1
        res.check(
            f"mate #{dm} {fen[:28]}",
            ok,
            f"dm={got} expected {dm}, pv has {pv_len} plies (expected {2 * dm - 1})",
        )


def test_no_mate(engine: Path, res: Results) -> None:
    print("\n[soundness] negative controls must not claim a mate")
    cases = load_epd(HERE / "nomate.epd")
    lines = solve(engine, cases, [])
    for (fen, dm), line in zip(cases, lines):
        res.check(
            f"no mate #{dm} {fen[:28]}",
            DM_RE.search(line) is None,
            f"false mate claimed: {line.strip()}",
        )


def test_invariance(engine: Path, res: Results) -> None:
    """Answers must not depend on thread count or memory budget.

    Parallel search returns the lowest-index successful root move, so the key
    move is defined independently of scheduling. Table eviction only discards
    memoised verdicts, so a tight budget may cost time but never an answer.
    """
    print("\n[invariance] answers are independent of threads and memory budget")
    cases = load_epd(HERE / "mates.epd") + load_epd(HERE / "nomate.epd")

    def digest(extra: list[str]) -> list[tuple[str | None, str | None]]:
        out = []
        for line in solve(engine, cases, extra):
            bm = BM_RE.search(line)
            dm = DM_RE.search(line)
            out.append((bm.group(1).strip() if bm else None,
                        dm.group(1) if dm else None))
        return out

    baseline = digest(["-M", "64", "--single-thread"])
    for label, extra in [
        ("threads=2", ["-M", "64", "--threads", "2", "--shared-tt"]),
        ("threads=8", ["-M", "64", "--threads", "8", "--shared-tt"]),
        ("threads=8 private tables", ["-M", "64", "--threads", "8", "--private-tt"]),
        ("threads=8 no cost gate", ["-M", "64", "--threads", "8", "--no-parallel-gate"]),
        ("tight memory -M 1", ["-M", "1", "--single-thread"]),
        ("tight memory + threads", ["-M", "1", "--threads", "8", "--shared-tt"]),
        ("unbounded memory -M 0", ["-M", "0", "--single-thread"]),
    ]:
        got = digest(extra)
        diffs = [i for i, (a, b) in enumerate(zip(baseline, got)) if a != b]
        res.check(f"invariant under {label}", not diffs,
                  f"{len(diffs)} rows differ (first at index {diffs[0]})" if diffs else "")


def test_pv_and_certificates(engine: Path, res: Results) -> None:
    print("\n[verify] PV replay and proof certificates (needs python-chess)")
    if not HAVE_CHESS:
        res.skip("PV replay and certificate verification", "python-chess not installed")
        return

    cases = load_epd(HERE / "mates.epd")
    lines = solve(engine, cases, ["--emit-proof"])

    def verify_node(board, node) -> tuple[bool, str]:
        move = chess.Move.from_uci(node["a"])
        if move not in board.legal_moves:
            return False, f"illegal attacker move {node['a']}"
        board.push(move)
        try:
            if node.get("mate"):
                if not board.is_checkmate():
                    return False, f"leaf after {node['a']} is not checkmate"
                return True, ""
            branches = node.get("d")
            if branches is None:
                return False, "non-leaf attacker node has no defender branches"
            listed = sorted(b["r"] for b in branches)
            legal = sorted(m.uci() for m in board.legal_moves)
            if listed != legal:
                return False, "defender branches do not match the legal replies"
            for branch in branches:
                board.push(chess.Move.from_uci(branch["r"]))
                ok, why = verify_node(board, branch["p"])
                board.pop()
                if not ok:
                    return False, why
            return True, ""
        finally:
            board.pop()

    for (fen, dm), line in zip(cases, lines):
        board = chess.Board(fen + " 0 1")
        pv = PV_RE.search(line)
        if not pv:
            res.check(f"pv {fen[:24]}", False, "no pv reported")
            continue
        replay = board.copy()
        ok, why = True, ""
        for token in pv.group(1).split():
            move = chess.Move.from_uci(token)
            if move not in replay.legal_moves:
                ok, why = False, f"illegal pv move {token}"
                break
            replay.push(move)
        if ok and not replay.is_checkmate():
            ok, why = False, "pv does not end in checkmate"
        res.check(f"pv replay #{dm} {fen[:24]}", ok, why)

        proof = PROOF_RE.search(line)
        if not proof:
            res.check(f"certificate {fen[:24]}", False, "no proof certificate emitted")
            continue
        ok, why = verify_node(board.copy(), json.loads(proof.group(1)))
        res.check(f"certificate #{dm} {fen[:24]}", ok, why)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--quick", action="store_true",
                        help="skip the deepest perft levels")
    args = parser.parse_args()

    if not args.engine.exists():
        print(f"engine not found: {args.engine}", file=sys.stderr)
        return 2

    if args.quick:
        for i, (name, fen, counts) in enumerate(PERFT_CASES):
            PERFT_CASES[i] = (name, fen, counts[:3])

    print(f"engine: {args.engine}")
    print(f"python-chess: {'available' if HAVE_CHESS else 'not installed'}")

    res = Results()
    test_perft(args.engine, res)
    test_known_mates(args.engine, res)
    test_no_mate(args.engine, res)
    test_invariance(args.engine, res)
    test_pv_and_certificates(args.engine, res)

    print(f"\n{res.passed} passed, {len(res.failed)} failed, {len(res.skipped)} skipped")
    for failure in res.failed:
        print(f"  FAILED: {failure}")
    return 1 if res.failed else 0


if __name__ == "__main__":
    sys.exit(main())
