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
import importlib.util
import json
import os
import random
import re
import subprocess
import threading
import sys
import time
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

    # These positions are disproved exhaustively in a handful of nodes, so the
    # engine must report a completed search, not a timeout. Checking only for
    # the absence of a mate is not enough: when the portfolio became the default
    # it began marking every one of these as a timeout, because a lane failing
    # to prove a mate was being read as the search running out of time. A caller
    # cannot tell "there is no mate" from "I gave up" if both look the same.
    generous = solve(engine, cases, ["--time-limit", "30"])
    for (fen, _), line in zip(cases, generous):
        res.check(
            f"exhaustive disproof is not reported as a timeout {fen[:24]}",
            "timeout" not in line,
            f"completed disproof marked timeout: {line.strip()}",
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


def test_time_limit(engine: Path, res: Results) -> None:
    """A budgeted search must stop on time, and must never claim a false mate.

    Expiry is an abort, and by the abort invariant an aborted search records no
    verdict. So a timed-out run reports "not proved" -- never a mate, and never
    a disproof it did not establish.
    """
    print("\n[time] the search honours a wall-clock budget")

    # A mate-in-8 that does not resolve quickly, so the budget actually binds.
    hard = "5k2/q2ppP1p/4P2P/p1P1PbN1/P1pR2N1/2K4B/6P1/8 w - -"

    for limit in (0.5, 2.0):
        started = time.monotonic()
        out = run(engine, [*BASE_ARGS, "-M", "64", "--single-thread",
                           "--time-limit", str(limit), "-z", "8", "-"],
                  hard + "\n", timeout=limit + 30.0)
        elapsed = time.monotonic() - started
        res.check(f"budget {limit}s is honoured",
                  elapsed < limit + 5.0, f"took {elapsed:.1f}s")
        res.check(f"budget {limit}s reports timeout, not a mate",
                  "timeout" in out and DM_RE.search(out) is None,
                  f"output {out.strip()[:80]!r}")

    # A generous budget must not disturb positions that solve quickly.
    cases = load_epd(HERE / "mates.epd")
    baseline = solve(engine, cases, ["-M", "64", "--single-thread"])
    budgeted = solve(engine, cases, ["-M", "64", "--single-thread", "--time-limit", "120"])
    same = all(
        (BM_RE.search(a) is None) == (BM_RE.search(b) is None)
        and (BM_RE.search(a) is None or BM_RE.search(a).group(1) == BM_RE.search(b).group(1))
        for a, b in zip(baseline, budgeted)
    )
    res.check("a generous budget changes no answer", same)

    # Under a budget too small to prove anything, no mate may be claimed.
    nomate = load_epd(HERE / "nomate.epd")
    tight = solve(engine, nomate, ["-M", "64", "--time-limit", "0.02", "--threads", "4", "--shared-tt"])
    res.check("no false mate under a tight budget",
              all(DM_RE.search(l) is None for l in tight))


def test_cli_contract(engine: Path, res: Results) -> None:
    """The CLI must diagnose bad input rather than quietly doing something else.

    This suite exists because a flag that silently disabled move ordering cost
    30x and hid behind a "rejected as slower" label, and because a silently
    ignored restriction option would answer a constrained question with a
    confident unconstrained result.
    """
    print("\n[cli] bad input is rejected, not silently ignored")

    def run_raw(args: list[str], stdin: str = "") -> tuple[int, str]:
        proc = subprocess.run([str(engine), *args], input=stdin.encode(),
                              capture_output=True, timeout=60)
        return proc.returncode, (proc.stdout + proc.stderr).decode()

    checks = [
        ("unknown option rejected", ["--thredas", "8", "-"], 2, "unknown option"),
        ("missing value rejected", ["-M"], 2, "requires a size"),
        ("missing depth rejected", ["-z"], 2, "requires a mate depth"),
        ("bad thread count rejected", ["--threads", "zero", "-"], 2, "positive number"),
        ("unimplemented restriction rejected", ["-I", "2", "-"], 2, "special-mate"),
    ]
    for name, args, want_code, want_text in checks:
        code, out = run_raw(args)
        res.check(name, code == want_code and want_text in out,
                  f"exit={code} output={out.strip()[:90]!r}")

    code, out = run_raw(["--help"])
    res.check("--help exits 0 and lists options", code == 0 and "--threads" in out,
              f"exit={code}")
    code, out = run_raw(["--version"])
    res.check("--version exits 0", code == 0 and "echest" in out, f"exit={code}")

    # The escape hatch must still work, for harness compatibility.
    code, out = run_raw(["-R", "2", "--allow-unimplemented", "-z", "1", "-"],
                        "8/8/8/8/8/8/5k2/7K w - -\n")
    res.check("--allow-unimplemented restores acceptance", code == 0, f"exit={code}")

    # Compatibility flags must be accepted, not rejected by the new strictness.
    code, out = run_raw(["-b", "-1", "-5", "-z", "1", "-"],
                        "8/8/8/8/8/8/5k2/7K w - -\n")
    res.check("Chest compatibility flags accepted", code == 0, f"exit={code}")


def test_illegal_positions_rejected(engine: Path, res: Results) -> None:
    """Positions that are not legal chess must be refused, not searched.

    A prover whose output is a proof has to refuse questions that are not well
    posed. Before these checks existed, "8/8/8/8/8/8/8/KKKKKKKK w - -" -- eight
    white kings and no black king -- was accepted and reported "dm 1", a mate
    claim in a position with no king to mate.
    """
    print("\n[input] illegal positions are refused")

    illegal = [
        ("no kings", "8/8/8/8/8/8/8/8 w - -"),
        ("no black king", "8/8/8/8/8/8/8/KKKKKKKK w - -"),
        ("two white kings", "k7/8/8/8/8/8/8/K5K1 w - -"),
        ("bad side to move", "k7/8/8/8/8/8/8/K7 x - -"),
        ("bad en passant", "K7/8/8/8/8/8/8/k7 w - zz"),
        ("en passant off rank", "K7/8/8/8/8/8/8/k7 w - e4"),
        ("adjacent kings", "kK6/8/8/8/8/8/8/8 w - -"),
        ("pawn on first rank", "P7/8/8/8/8/8/8/K6k w - -"),
        ("side not to move in check", "4r2k/8/8/8/8/8/8/4K3 b - -"),
        ("too few ranks", "8/8/8/8/8/8/8 w - -"),
        ("too many ranks", "8/8/8/8/8/8/8/8/8 w - -"),
        ("bad piece letter", "zzzz/8/8/8/8/8/8/8 w - -"),
    ]
    for name, fen in illegal:
        out = run(engine, [*BASE_ARGS, "-M", "64", "-z", "2", "-"], fen + "\n")
        res.check(f"refuses {name}", "error input" in out,
                  f"accepted: {out.strip()[:80]!r}")

    # Legal positions must still be accepted.
    for name, fen in [("startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"),
                      ("bare kings", "k7/8/8/8/8/8/8/7K w - -"),
                      ("real en passant", "4k3/8/8/3pP3/8/8/8/4K3 w - d6")]:
        out = run(engine, [*BASE_ARGS, "-M", "64", "-z", "1", "-"], fen + "\n")
        res.check(f"accepts {name}", "error input" not in out,
                  f"rejected: {out.strip()[:80]!r}")


def test_castling_needs_its_rook(engine: Path, res: Results) -> None:
    """Castling must require the rook to be present.

    A FEN can claim a castling right whose rook is absent. make_move writes the
    rook onto f1/d1 unconditionally, so generating that castling would
    materialise a piece from nothing. Every standard perft position has
    consistent rights, so perft alone never exercised this.
    """
    print("\n[movegen] castling requires the rook to be there")

    def legal_set(fen: str) -> set[str]:
        out = run(engine, ["--list-legal"], fen + "\n")
        match = re.search(r"; legal ([^;]*);", out)
        return set(match.group(1).split()) if match and match.group(1).strip() else set()

    phantom = "4k3/8/8/8/8/8/8/4K3 w K -"
    res.check("no castling without a rook", "e1g1" not in legal_set(phantom),
              "generated e1g1 with an empty h1")

    real = "4k3/8/8/8/8/8/8/4K2R w K -"
    res.check("castling still generated when the rook is there",
              "e1g1" in legal_set(real))

    if HAVE_CHESS:
        for fen in (phantom, real):
            mine = legal_set(fen)
            theirs = {m.uci() for m in chess.Board(fen + " 0 1").legal_moves}
            res.check(f"matches python-chess on {fen[:24]}", mine == theirs,
                      f"engine-only {sorted(mine - theirs)}, missing {sorted(theirs - mine)}")


def test_checks_only_restriction(engine: Path, res: Results) -> None:
    """-C 1 must actually restrict the attacker to checking moves.

    This is not a pruning heuristic. Removing a legal attacker move would be
    unsound for an ordinary directmate; it is correct only because -C 1 asks a
    different question, so the removed moves are not candidates for it. The
    test therefore checks the restriction is real (every attacker move in a
    solution gives check) and that it is off unless asked for.
    """
    print("\n[restriction] -C 1 restricts the attacker to checking moves")

    cases = load_epd(HERE / "mates.epd")
    baseline = solve(engine, cases, ["-M", "64"])
    off = solve(engine, cases, ["-M", "64", "-C", "0"])
    res.check("-C 0 leaves the search unrestricted",
              [BM_RE.search(l) is not None for l in baseline] ==
              [BM_RE.search(l) is not None for l in off])

    restricted = solve(engine, cases, ["-M", "64", "-C", "1"])
    solved = sum(1 for l in restricted if DM_RE.search(l))
    res.check("-C 1 is at least as restrictive as no restriction",
              solved <= sum(1 for l in baseline if DM_RE.search(l)),
              f"restricted solved {solved}")

    if HAVE_CHESS:
        offenders = 0
        checked = 0
        for (fen, _), line in zip(cases, restricted):
            pv = PV_RE.search(line)
            if not pv:
                continue
            checked += 1
            board = chess.Board(fen + " 0 1")
            for i, token in enumerate(pv.group(1).split()):
                board.push(chess.Move.from_uci(token))
                if i % 2 == 0 and not board.is_check():
                    offenders += 1
        res.check(f"every attacker move gives check in {checked} restricted solution(s)",
                  offenders == 0, f"{offenders} non-checking attacker moves")

    # All ChecksOnly bits are implemented now, but contradictory and
    # out-of-range masks must still be refused.
    for name, value, expect in (("contradictory 1|16", "17", "contradictory"),
                                ("out of range", "99", "expects 0..31")):
        proc = subprocess.run([str(engine), "-C", value, "-z", "1", "-"],
                              input=b"", capture_output=True, timeout=60)
        out = (proc.stdout + proc.stderr).decode()
        res.check(f"-C {value} ({name}) is refused",
                  proc.returncode == 2 and expect in out,
                  f"exit={proc.returncode} {out.strip()[:70]!r}")


def test_defender_restrictions(engine: Path, res: Results) -> None:
    """-K, -P and -X must bound the defender exactly as WinChest defines them.

    KingSquares counts the square the king stands on, PieceLimit counts distinct
    defender pieces that can move, and MaxMoves counts total defender replies.
    Each is checked against the position reached after every attacker move in a
    solution, so a restriction that quietly failed to apply would be caught.
    """
    print("\n[restriction] -K/-P/-X bound the defender as specified")

    if not HAVE_CHESS:
        res.skip("defender restrictions", "python-chess not installed")
        return

    cases = load_epd(HERE / "mates.epd")
    baseline = sum(1 for l in solve(engine, cases, ["-M", "64"]) if DM_RE.search(l))

    def check(flag: str, value: int, measure) -> None:
        lines = solve(engine, cases, ["-M", "64", flag, str(value)])
        solved = sum(1 for l in lines if DM_RE.search(l))
        res.check(f"{flag} {value} is no less restrictive than none",
                  solved <= baseline, f"{solved} > {baseline}")
        violations = 0
        for (fen, _), line in zip(cases, lines):
            pv = PV_RE.search(line)
            if not pv:
                continue
            board = chess.Board(fen + " 0 1")
            for i, token in enumerate(pv.group(1).split()):
                board.push(chess.Move.from_uci(token))
                if i % 2 == 0 and measure(board) > value:
                    violations += 1
        res.check(f"{flag} {value} holds after every attacker move",
                  violations == 0, f"{violations} violation(s)")

    def king_squares(board) -> int:
        king = board.king(board.turn)
        return 1 + sum(1 for m in board.legal_moves if m.from_square == king)

    def piece_count(board) -> int:
        return len({m.from_square for m in board.legal_moves})

    def move_count(board) -> int:
        return board.legal_moves.count()

    check("-K", 2, king_squares)
    check("-P", 2, piece_count)
    check("-X", 3, move_count)

    # Off values must leave the search unrestricted, as WinChest defines them.
    for flag, off in (("-K", 9), ("-P", 16), ("-X", 222)):
        n = sum(1 for l in solve(engine, cases, ["-M", "64", flag, str(off)]) if DM_RE.search(l))
        res.check(f"{flag} {off} is off", n == baseline, f"{n} != {baseline}")


def test_restriction_portfolio(engine: Path, res: Results) -> None:
    """--portfolio must be sound, opt-in, and still emit verifiable proofs.

    A restriction only removes attacker options, so a mate found under one is a
    real forced mate. That makes the portfolio a sound fast path rather than a
    gamble -- but only if the proof it returns still verifies, which is what is
    checked here.
    """
    print("\n[portfolio] restricted searches are a sound fast path")

    cases = load_epd(HERE / "mates.epd")
    plain = solve(engine, cases, ["-M", "64", "--time-limit", "20", "--no-portfolio"])
    pf = solve(engine, cases, ["-M", "64", "--time-limit", "20", "--portfolio"])

    res.check("portfolio solves at least as much",
              sum(1 for l in pf if DM_RE.search(l)) >= sum(1 for l in plain if DM_RE.search(l)))

    # --no-portfolio must still restore the single unrestricted search: no line
    # may claim a restriction was used. The portfolio became the default once it
    # was measured to double bare-invocation reach, so this now checks the
    # opt-out rather than the opt-in.
    res.check("--no-portfolio restores the single unrestricted search",
              all("; via " not in l for l in plain))

    # The default must engage the portfolio whenever a time limit is set, which
    # is the contract the help text now advertises.
    default_lines = solve(engine, cases, ["-M", "64", "--time-limit", "20"])
    res.check("portfolio is on by default with a time limit",
              sum(1 for l in default_lines if DM_RE.search(l))
              >= sum(1 for l in plain if DM_RE.search(l)))

    if not HAVE_CHESS:
        res.skip("portfolio proof verification", "python-chess not installed")
        return

    # Every portfolio proof must replay legally and end in mate, including any
    # found under a restriction.
    proofs = solve(engine, cases, ["-M", "64", "--time-limit", "20",
                                   "--portfolio", "--emit-proof"])
    bad = 0
    checked = 0
    for (fen, _), line in zip(cases, proofs):
        pv = PV_RE.search(line)
        if not pv:
            continue
        checked += 1
        board = chess.Board(fen + " 0 1")
        ok = True
        for token in pv.group(1).split():
            move = chess.Move.from_uci(token)
            if move not in board.legal_moves:
                ok = False
                break
            board.push(move)
        if not (ok and board.is_checkmate()):
            bad += 1
    res.check(f"all {checked} portfolio proofs replay to mate", bad == 0, f"{bad} bad")

    # The parallel portfolio must be sound too. Which lane wins can vary, so
    # this checks the property every result must have rather than a fixed
    # answer: solved positions replay to a real mate at the stated depth.
    par = solve(engine, cases, ["-M", "64", "--time-limit", "20", "--threads", "8",
                                "--portfolio-parallel"])
    bad_par = 0
    checked_par = 0
    for (fen, _), line in zip(cases, par):
        pv = PV_RE.search(line)
        dm = DM_RE.search(line)
        if not pv or not dm:
            continue
        checked_par += 1
        board = chess.Board(fen + " 0 1")
        ok = True
        for token in pv.group(1).split():
            move = chess.Move.from_uci(token)
            if move not in board.legal_moves:
                ok = False
                break
            board.push(move)
        if not (ok and board.is_checkmate() and
                len(pv.group(1).split()) == 2 * int(dm.group(1)) - 1):
            bad_par += 1
    res.check(f"all {checked_par} parallel-portfolio proofs replay to mate",
              bad_par == 0, f"{bad_par} bad")
    res.check("parallel portfolio solves at least as much as none",
              sum(1 for l in par if DM_RE.search(l)) >=
              sum(1 for l in plain if DM_RE.search(l)))


def test_shipped_verifier(engine: Path, res: Results) -> None:
    """The shipped certificate verifier must accept real proofs and reject fakes.

    echest's headline claim is that a mate is a proof rather than a search
    result, so `tools/verify_proof.py` is the tool that makes the claim
    checkable by someone who does not trust the engine. A verifier that accepts
    everything would make the claim worthless, so it is tested adversarially
    against deliberately forged certificates.
    """
    print("\n[verifier] tools/verify_proof.py accepts proofs and rejects forgeries")

    tool = HERE.parent / "tools" / "verify_proof.py"
    if not tool.exists():
        res.skip("shipped verifier", "tools/verify_proof.py not present")
        return
    if not HAVE_CHESS:
        res.skip("shipped verifier", "python-chess not installed")
        return

    cases = load_epd(HERE / "mates.epd")[:6]
    output = "\n".join(solve(engine, cases, ["-M", "64", "--emit-proof"])) + "\n"

    def run_verifier(text: str) -> tuple[int, str]:
        proc = subprocess.run([sys.executable, str(tool), "--quiet", "-"],
                              input=text.encode(), capture_output=True, timeout=120)
        return proc.returncode, proc.stdout.decode() + proc.stderr.decode()

    code, out = run_verifier(output)
    res.check("verifier accepts genuine certificates", code == 0,
              f"exit={code} {out.strip()[:120]!r}")

    # Forge each certificate in a distinct way; every one must be rejected.
    first = output.splitlines()[0]
    fen = first.split(";", 1)[0].strip()
    proof = PROOF_RE.search(first)
    if not proof:
        res.check("engine emitted a certificate to forge", False, "no proof token")
        return
    node = json.loads(proof.group(1))

    def replaced(new_node) -> str:
        return first.replace(proof.group(1), json.dumps(new_node)) + "\n"

    # 1. omit a legal defence
    dropped = json.loads(json.dumps(node))
    if dropped.get("d"):
        dropped["d"].pop()
        code, _ = run_verifier(replaced(dropped))
        res.check("verifier rejects an omitted defence", code != 0)

    # 2. claim a non-mating leaf is mate
    forged = json.loads(json.dumps(node))
    board = chess.Board(fen + " 0 1")
    board.push(chess.Move.from_uci(forged["a"]))
    branch = forged["d"][0]
    board.push(chess.Move.from_uci(branch["r"]))
    real = branch["p"].get("a")
    alt = None
    for mv in board.legal_moves:
        board.push(mv)
        mates = board.is_checkmate()
        board.pop()
        if not mates and mv.uci() != real:
            alt = mv.uci()
            break
    if alt and branch["p"].get("mate"):
        branch["p"]["a"] = alt
        code, _ = run_verifier(replaced(forged))
        res.check("verifier rejects a forged mate leaf", code != 0)

    # 3. invent a defence that is not legal
    invented = json.loads(json.dumps(node))
    if invented.get("d"):
        invented["d"].append({"r": "a1a2", "p": {"a": "a2a3", "mate": True}})
        code, _ = run_verifier(replaced(invented))
        res.check("verifier rejects an invented illegal defence", code != 0)

    # 4. list a legal defence twice. The reply multiset must match exactly, so a
    #    duplicate is as wrong as an omission even though nothing is missing.
    doubled = json.loads(json.dumps(node))
    if doubled.get("d"):
        doubled["d"].append(json.loads(json.dumps(doubled["d"][0])))
        code, detail = run_verifier(replaced(doubled))
        res.check("verifier rejects a duplicated defence", code != 0)
        res.check("duplicate rejection explains itself", "duplicate" in detail.lower(),
                  detail.strip()[:120])

    # 5. make the attacker's own move illegal
    illegal = json.loads(json.dumps(node))
    illegal["a"] = "a1a2"
    code, _ = run_verifier(replaced(illegal))
    res.check("verifier rejects an illegal attacker move", code != 0)

    # 6. empty the reply list. A non-leaf node claiming no defences exist is
    #    claiming stalemate or mate without saying so.
    emptied = json.loads(json.dumps(node))
    if emptied.get("d"):
        emptied["d"] = []
        code, _ = run_verifier(replaced(emptied))
        res.check("verifier rejects an empty reply list", code != 0)

    # 7. corrupt the principal variation
    code, _ = run_verifier(re.sub(r"pv [^;]+;", "pv a1a2;", first) + "\n")
    res.check("verifier rejects a corrupted pv", code != 0)

    # 8. overstate the mate depth
    dm = DM_RE.search(first)
    if dm:
        wrong = first.replace(f"dm {dm.group(1)};", f"dm {int(dm.group(1)) + 1};")
        code, _ = run_verifier(wrong + "\n")
        res.check("verifier rejects an overstated depth", code != 0)


def test_help_documents_every_option(engine: Path, res: Results) -> None:
    """Every option the parser accepts must appear in --help.

    Help text drifts silently: a flag gets added during development and the
    usage block is not updated, so a released tool accepts options nobody can
    discover. This caught 14 undocumented options when first written, including
    --time-limit and --direct-depth.
    """
    print("\n[docs] --help covers every accepted option")

    source = HERE.parent / "src" / "echest.cpp"
    if not source.exists():
        res.skip("help coverage", "source not alongside tests")
        return

    text = source.read_text(encoding="utf-8", errors="replace")
    try:
        start = text.index("for (int i = 1; i < argc; ++i)")
        end = text.index("if (read_stdin)", start)
    except ValueError:
        res.skip("help coverage", "could not locate the argument parser")
        return

    accepted = sorted(set(re.findall(r'arg == "(-{1,2}[a-zA-Z0-9][a-zA-Z0-9-]*)"',
                                     text[start:end])))
    proc = subprocess.run([str(engine), "--help"], capture_output=True, timeout=60)
    help_text = proc.stdout.decode()

    missing = [flag for flag in accepted if flag not in help_text]
    res.check(f"--help documents all {len(accepted)} accepted options",
              not missing, f"missing: {missing}")


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


def test_corpus_ergonomics(engine: Path, res: Results) -> None:
    """The shipped corpus must work when simply piped in.

    Both halves of this were broken. The engine inferred a depth only from the
    matetrack `#N` spelling, not the `dm N` spelling its own corpora and its own
    output use, and it reported "error input" for the corpus's comment lines.
    Together that meant `echest - < tests/mates.epd` searched nothing and
    printed an error for every comment.
    """
    print("\n[corpus] the shipped corpus pipes in cleanly")

    raw = (HERE / "mates.epd").read_text(encoding="utf-8")
    out = run(engine, ["-5", "--time-limit", "10", "-"], raw)

    res.check("no error lines when piping the shipped corpus",
              "error input" not in out)
    res.check("comment lines produce no output",
              not any(l.lstrip().startswith("#") for l in out.splitlines()))
    res.check("depth inferred from 'dm N' without -z",
              len(DM_RE.findall(out)) >= 4)

    # The engine prints `dm N`, so a run's output can be fed straight back in.
    once = run(engine, ["-5", "-z", "2", "--time-limit", "10", "-"],
               "2brrb2/8/p7/7Q/1p1kpPp1/1P1pN1K1/3P4/8 w - -\n")
    twice = run(engine, ["-5", "--time-limit", "10", "-"], once)
    res.check("engine output round-trips as input", bool(DM_RE.search(twice)))


def test_memory_budget_is_a_total(engine: Path, res: Results) -> None:
    """`-M` covers every table alive at once, and dividing it must not reach 0.

    It used to apply per table, so the default's eight portfolio lanes turned a
    stated 256 MB into a measured 615 MB, and `--parallel-positions 4` into
    1994 MB -- wrong by 8x in the direction that ends a batch run with an
    allocation failure. Splitting it introduces the opposite hazard: a small
    budget over many consumers rounding to a zero-entry ceiling, which would
    evict on every store. The share is floored at 1 MB.
    """
    print("\n[memory] -M is a total and its share never rounds to zero")

    pos = "2brrb2/8/p7/7Q/1p1kpPp1/1P1pN1K1/3P4/8 w - - dm 2\n"
    expected = DM_RE.findall(run(engine, ["--time-limit", "10", "-"], pos))

    # 8 MB over four workers and eight lanes is 0.25 MB per table before the
    # floor. It must still solve, not divide by zero or thrash to a halt.
    tiny = run(engine, ["-M", "8", "--parallel-positions", "4",
                        "--time-limit", "20", "-"], pos)
    res.check("a budget smaller than its consumers still solves",
              DM_RE.findall(tiny) == expected)

    res.check("-M 0 still means unbounded",
              DM_RE.findall(run(engine, ["-M", "0", "--time-limit", "10", "-"], pos)) == expected)

    # The reported figure is the total the user asked for, not a share of it.
    cfg = run(engine, ["--print-config", "-M", "512", "--time-limit", "10", "-"], pos)
    res.check("--print-config reports the total, not the per-table share",
              '"memory_mb":512' in cfg.replace(" ", ""))


def test_bom_tolerated_on_input(engine: Path, res: Results) -> None:
    """A UTF-8 BOM must not swallow the first position.

    Windows is the primary platform, and both Notepad and PowerShell's
    `Set-Content -Encoding utf8` prepend EF BB BF. Those three bytes made the
    first line fail as "error input" while every later line succeeded -- so a
    multi-position file quietly lost one position, and a single-position file
    lost the only one and reported an error with nothing to explain it. This
    harness tripped over it twice while measuring something else, which is the
    best evidence available that users would too.
    """
    print("\n[input] a UTF-8 BOM does not swallow the first position")

    pos = "2brrb2/8/p7/7Q/1p1kpPp1/1P1pN1K1/3P4/8 w - - dm 2\n"

    plain = run(engine, ["--time-limit", "10", "-"], pos)
    bom = run(engine, ["--time-limit", "10", "-"], "﻿" + pos)
    res.check("BOM + position solves", "error input" not in bom)
    res.check("BOM run matches the plain run",
              DM_RE.findall(bom) == DM_RE.findall(plain))
    res.check("the BOM is not echoed back", "﻿" not in bom)

    # A BOM landed on the first non-space character, so it also stopped the
    # leading '#' from being recognised and turned a comment into an error.
    commented = run(engine, ["--time-limit", "10", "-"], "﻿# header\n" + pos)
    res.check("BOM before a comment line still comments it out",
              "error input" not in commented and "header" not in commented)

    # Degenerate input: a file containing nothing but a BOM must exit cleanly.
    res.check("a BOM-only input is not an error",
              run(engine, ["--time-limit", "10", "-"], "﻿\n").strip() == "")


def test_docs_reference_shipped_files(engine: Path, res: Results) -> None:
    """Documentation must not point at files the published tree does not contain.

    The docs were written while chest-e was a subdirectory of a larger private
    workspace, so several of them referenced the benchmark harness alongside it.
    Extracted for publication, those became instructions to run scripts that are
    not there -- including one telling the reader to verify proofs with a script
    that the engine actually ships under a different name.

    Deliberately external references are allowed by name; everything else that
    looks like a repository path must resolve inside the tree.
    """
    print("\n[docs] every referenced file ships with the engine")

    root = HERE.parent
    # Deliberate citations of things that exist outside this repository.
    external = {
        "Options.txt",      # WinChest's own documentation
        "matetrack.epd",    # the public corpus the held-out positions are drawn from
    }
    pattern = re.compile(
        r"`([^`\s]+\.(?:py|cpp|h|md|txt|epd|ps1|yml|json))`"
        r"|(?:^|\s)((?:benchmarks|tools|tests|src|docs)[/\\\\][^\s`,;)]+)")

    dangling = []
    for path in sorted(root.rglob("*.md")):
        if "build" in path.parts:
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for match in pattern.finditer(line):
                ref = (match.group(1) or match.group(2) or "").strip()
                if not ref or ref.startswith("http") or ref in external:
                    continue
                norm = ref.replace(chr(92), "/")
                tail = norm.split("/")[-1]
                if any((root / norm).exists() or (base / norm).exists() or (base / tail).exists()
                       for base in (path.parent, root, root / "src", root / "tools", root / "tests")):
                    continue
                dangling.append(f"{path.relative_to(root)}:{number} -> {ref}")

    res.check("no documentation reference points outside the shipped tree",
              not dangling, "; ".join(dangling[:4]))


def test_verifier_rejects_stalemate_as_mate(engine: Path, res: Results) -> None:
    """A stalemate must never verify as a forced mate.

    Found while specifying the certificate format. A node of the form
    {"a": <move>, "d": []} was accepted whenever the move left the defender with
    no legal reply: the listed-equals-legal check is vacuously true when both
    sides are empty, so the recursion added a ply and returned success. That
    accepts a STALEMATE as a mate.

    A full output line was still rejected, but only by the separate pv check, so
    the hole was invisible from outside and would have opened up for any
    stalemate buried in a branch the pv does not follow. This exercises
    verify_node directly for that reason.
    """
    print("\n[verifier] a stalemate is not a mate")

    if not HAVE_CHESS:
        res.skip("stalemate rejection", "python-chess not installed")
        return

    spec = importlib.util.spec_from_file_location(
        "verify_proof", HERE.parent / "tools" / "verify_proof.py")
    verifier = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verifier)

    # f1f7 leaves Black stalemated: no legal reply, and not in check.
    board = chess.Board("7k/8/8/8/8/8/8/1K3Q2 w - - 0 1")
    probe = board.copy()
    probe.push(chess.Move.from_uci("f1f7"))
    res.check("the probe position really is stalemate, not mate",
              probe.is_stalemate() and not probe.is_checkmate())

    try:
        depth = verifier.verify_node(board.copy(), {"a": "f1f7", "d": []}, [])
        res.check("verifier rejects a stalemate claimed as mate", False,
                  f"accepted, returned depth {depth}")
    except verifier.Failure:
        res.check("verifier rejects a stalemate claimed as mate", True)


def test_output_format_conformance(engine: Path, res: Results) -> None:
    """The four documented outcomes must be distinguishable and well formed.

    docs/OUTPUT_FORMAT.md is the contract consumers parse. The distinction that
    matters most is disproof (line ends after acs) versus timeout (explicit
    marker): confusing them turns "I do not know" into "there is no mate".
    """
    print("\n[format] output lines conform to OUTPUT_FORMAT.md")

    hard = "3R4/pk6/p1pp4/B1pqPp2/3P3n/1N4N1/KR1P1p2/5r2 w - -"

    # 1. proved
    proved = run(engine, ["-5", "-z", "2", "--time-limit", "20", "--emit-proof", "-"],
                 "2brrb2/8/p7/7Q/1p1kpPp1/1P1pN1K1/3P4/8 w - -" + chr(10)).strip()
    res.check("proved line carries bm, dm and pv",
              all(f"; {tok} " in proved for tok in ("bm", "dm", "pv")), proved[:100])
    order = [proved.index(f"; {tok} ") for tok in ("bm", "dm", "pv", "proof")]
    res.check("proved line field order is bm, dm, pv, proof", order == sorted(order))
    res.check("proved line ends with a semicolon", proved.endswith(";"))

    # 2. disproved: nothing after acs
    disproved = run(engine, ["-5", "-z", "1", "--time-limit", "30", "-"],
                    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -" + chr(10)).strip()
    res.check("disproved line ends after acs",
              re.fullmatch(r"[^;]+; acn \d+; acs [\d.e+-]+;", disproved) is not None,
              disproved[:100])
    res.check("disproved line is not marked timeout", "timeout" not in disproved)

    # 3. gave up
    gave_up = run(engine, ["-5", "-z", "8", "--time-limit", "0.2", "-"],
                  hard + chr(10)).strip()
    res.check("exhausted budget is marked timeout", gave_up.endswith("timeout;"), gave_up[:100])
    res.check("timeout line claims no mate", DM_RE.search(gave_up) is None)

    # 4. bad input, echoed unchanged
    junk = "not a position at all"
    bad = run(engine, ["-5", "-"], junk + chr(10)).strip()
    res.check("bad input echoes the original line", bad.startswith(junk), bad[:100])
    res.check("bad input is marked error input", bad.endswith("error input;"), bad[:100])

    # An illegal-but-parseable position is rejected the same way: adjacent kings.
    illegal = run(engine, ["-5", "-"], "8/8/8/8/8/8/6k1/6KQ w - -" + chr(10)).strip()
    res.check("illegal position is rejected as bad input",
              illegal.endswith("error input;"), illegal[:100])


def test_documented_defaults_are_real(engine: Path, res: Results) -> None:
    """Every default --help advertises must be the one actually in force.

    Checked against `--print-config`, which reports the effective configuration
    after all defaults and sentinels resolve. A purely behavioural check is not
    enough: most tuning flags preserve exactness AND node counts on small
    positions, so passing the non-default value changes nothing observable and
    a false claim in the help would pass unnoticed. Measured on a mate-in-2,
    none of seven non-default flags altered the output at all.

    8n found the shipped defaults were the untuned ones while every gate passed
    explicit flags, so nothing had ever compared documentation to reality.
    """
    print("\n[cli] documented defaults match actual defaults")

    help_text = run(engine, ["--help"], "")
    config = json.loads(run(engine, ["--print-config"], ""))

    # 1. Every "A | B" pair must say which side is the default.
    undocumented = []
    for block in re.finditer(r"^  (--[\w-]+(?: N)? \| --[\w-]+(?: N)?)\s*$(.*?)(?=^  --|^\w|\Z)",
                             help_text, re.M | re.S):
        if "default" not in block.group(2):
            undocumented.append(block.group(1))
    res.check("every paired option documents its default",
              not undocumented, "; ".join(undocumented[:4]))

    # 2. Each documented default flag must match the effective configuration.
    #    The flag names the side that is on, so a "--no-" or negative-sense name
    #    means the underlying setting is off.
    flag_settings = {
        "--proof-hints": ("proof_hints", True),
        "--no-refutation-hints": ("refutation_hints", False),
        "--keep-iter-tt": ("keep_iter_tt", True),
        "--ordered-check-shortcut": ("ordered_check_shortcut", True),
        "--inplace-order": ("inplace_order", True),
        "--fused-order": ("fused_order", True),
        "--eager-defender": ("lazy_defender", False),
        "--stable-sort-order": ("bucket_order", False),
        "--no-mate-score": ("score_mates", False),
        "--vector-pseudo": ("static_pseudo", False),
    }
    documented = set(re.findall(r"default: (--[\w-]+)(?=\s*(?:\(|$))",
                                help_text, re.M))
    res.check("every documented default flag is covered by this test",
              documented <= set(flag_settings), str(sorted(documented - set(flag_settings))))
    for flag in sorted(documented):
        key, expected = flag_settings[flag]
        res.check(f"help says {flag} is default and it is",
                  config.get(key) is expected,
                  f"{key}={config.get(key)}, help claims {expected}")

    # 3. Documented default VALUES must match too, read out of the help text so
    #    that drift on either side is caught rather than hard-coded here.
    value_settings = [
        (r"-M N.*?\(default (\d+)\)", "memory_mb"),
        (r"--parallel-min-nodes.*?\(default (\d+)\)", "parallel_min_nodes"),
        (r"--shared-tt-shards.*?\(default (\d+)\)", "shared_tt_shards"),
        (r"--move-reserve-cap.*?\(default (\d+)\)", "move_reserve_capacity"),
        (r"default: --order-min-size (\d+)", "order_min_size"),
    ]
    for pattern, key in value_settings:
        match = re.search(pattern, help_text, re.S)
        if not match:
            res.check(f"help documents a default for {key}", False, "no default found")
            continue
        res.check(f"help's default for {key} is the real one",
                  config.get(key) == int(match.group(1)),
                  f"{key}={config.get(key)}, help says {match.group(1)}")

    # 4. --threads defaults to the auto value rather than to 1.
    res.check("help documents --threads default as auto", "(default: auto)" in help_text)
    res.check("effective thread count is the auto value",
              config.get("threads") == min(os.cpu_count() or 1, 16),
              f"threads={config.get('threads')}, cores={os.cpu_count()}")

    # 5. The portfolio default the help claims must hold.
    res.check("help says the portfolio is the default with a time limit",
              "is the default whenever --time-limit is set" in help_text)
    res.check("portfolio is enabled in the effective configuration",
              config.get("portfolio") is True and config.get("portfolio_parallel") is True)

    # 6. --direct-depth must NOT be default: it weakens the advertised claim
    #    from "the shortest mate is N" to "a mate within N".
    res.check("iterative depth remains the default", config.get("direct_depth") is False)


def test_abort_invariant_under_stress(engine: Path, res: Results) -> None:
    """An abandoned search must never produce a verdict, under any settings.

    The invariant underpins cancellation, time limits and eviction: an aborted
    subtree records nothing, so it can never be read back as a disproof. It was
    asserted in comments throughout and never exercised adversarially.

    Doing so found a crash rather than an unsoundness. A restriction can remove
    every attacker move at the root, and the empty-move-list guard ran BEFORE
    the restriction was applied, so the worker count reached zero and the thread
    pool reserved (size_t)(0 - 1). std::length_error escaped a portfolio lane
    and terminated the process mid-batch, losing every remaining position --
    reachable with default threads plus one documented flag.
    """
    print("\n[soundness] abandoned searches never claim a verdict")

    mates = load_epd(HERE / "mates.epd")
    nomates = load_epd(HERE / "nomate.epd")
    mixed = []
    for i in range(max(len(mates), len(nomates))):
        if i < len(mates):
            mixed.append((mates[i][0], mates[i][1], True))
        if i < len(nomates):
            mixed.append((nomates[i][0], nomates[i][1], False))
    stdin = "".join(f"{fen} bm #{dm};" + chr(10) for fen, dm, _ in mixed)

    configs = [
        ["--threads", "16", "--parallel-min-nodes", "1", "--time-limit", "0.005"],
        ["--threads", "16", "--parallel-min-nodes", "1", "--time-limit", "0.05", "--shared-tt"],
        ["-M", "1", "--threads", "16", "--parallel-min-nodes", "1", "--time-limit", "0.01"],
        ["--threads", "8", "--time-limit", "0.5"],
        ["--single-thread", "--time-limit", "0.01"],
    ]
    for config in configs:
        label = " ".join(config)
        out = run(engine, ["-5", *config, "-"], stdin)
        lines = [l for l in out.splitlines() if l.strip()]
        res.check(f"one line per position [{label}]", len(lines) == len(mixed),
                  f"got {len(lines)} of {len(mixed)}")
        if len(lines) != len(mixed):
            continue
        for (fen, dm, is_mate), line in zip(mixed, lines):
            claimed = DM_RE.search(line)
            if claimed and not is_mate:
                res.check(f"no false mate {fen[:22]} [{label}]", False, line.strip())
                break
            if is_mate and claimed and int(claimed.group(1)) != dm:
                res.check(f"depth correct {fen[:22]} [{label}]", False, line.strip())
                break
            # The invariant made visible: a truncated search must say so rather
            # than fall through to the silent "no mate" form.
            if is_mate and not claimed and "timeout" not in line:
                res.check(f"abandoned search is not a disproof {fen[:22]} [{label}]",
                          False, line.strip())
                break
        else:
            res.check(f"no false verdicts [{label}]", True)


def test_restriction_soundness_and_nesting(engine: Path, res: Results) -> None:
    """The property the restriction portfolio's soundness actually rests on.

    A restriction only removes attacker options, so a mate found under one must
    be a real mate, and cannot be shorter than the unrestricted answer. The
    restrictions had been validated against the WinChest oracle for AGREEMENT,
    which is a different claim: agreeing with another engine about which
    positions a restricted search solves says nothing about whether those
    answers are sound with respect to the unrestricted problem. That is what the
    portfolio depends on, and it was untested.

    Searches here are untimed and sequential so that every one completes; the
    subset properties hold for completed searches, not for truncated ones.
    """
    print("\n[restriction] restricted answers are sound and nest")

    cases = load_epd(HERE / "mates.epd")

    def solve_depths(extra):
        lines = solve(engine, cases, ["--no-portfolio", "--single-thread", *extra])
        out = {}
        for (fen, _), line in zip(cases, lines):
            found = DM_RE.search(line)
            out[fen] = int(found.group(1)) if found else None
        return out

    base = solve_depths([])
    res.check("unrestricted search solves the corpus",
              sum(1 for v in base.values() if v) >= 10)

    named = {
        "K2": ["-K", "2"], "K3": ["-K", "3"], "K4": ["-K", "4"],
        "X2": ["-X", "2"], "X4": ["-X", "4"], "X6": ["-X", "6"],
        "R1": ["-R", "1"], "R2": ["-R", "2"],
        "C1": ["-C", "1"], "C2": ["-C", "2"], "C3": ["-C", "3"],
        "C4": ["-C", "4"], "C6": ["-C", "6"],
    }
    depths = {name: solve_depths(flags) for name, flags in named.items()}

    # 1. Soundness: anything a restriction proves is a real mate, and never
    #    shorter than the unrestricted answer.
    for name, found in depths.items():
        unsound = [fen for fen, value in found.items()
                   if value is not None and (base[fen] is None or base[fen] > value)]
        res.check(f"{name} proves nothing the unrestricted search cannot",
                  not unsound, "; ".join(f[:26] for f in unsound[:2]))

    # 2. Numeric bounds nest: a tighter bound must solve a subset.
    for tight, loose in [("K2", "K3"), ("K3", "K4"), ("X2", "X4"), ("X4", "X6"), ("R1", "R2")]:
        a = {f for f, v in depths[tight].items() if v}
        b = {f for f, v in depths[loose].items() if v}
        res.check(f"{tight} solves a subset of {loose}", a <= b,
                  "; ".join(f[:26] for f in sorted(a - b)[:2]))

    # 3. ChecksOnly is a BITMASK, not a ladder: C1, C2 and C4 are independent
    #    conditions, so C2 is not comparable to C4. Adding bits is what makes a
    #    mask stricter, so a mask must solve a subset of every mask whose bits
    #    it contains.
    masks = {1: "C1", 2: "C2", 3: "C3", 4: "C4", 6: "C6"}
    for bits, name in masks.items():
        for other_bits, other_name in masks.items():
            if bits != other_bits and (bits & other_bits) == other_bits:
                a = {f for f, v in depths[name].items() if v}
                b = {f for f, v in depths[other_name].items() if v}
                res.check(f"{name} solves a subset of {other_name}", a <= b,
                          "; ".join(f[:26] for f in sorted(a - b)[:2]))

    # 4. A proof found under a restriction must verify like any other. The
    #    portfolio can return one, so this is the path a caller actually sees.
    if not HAVE_CHESS:
        res.skip("restricted certificates verify", "python-chess not installed")
        return
    tool = HERE.parent / "tools" / "verify_proof.py"
    for name in ("K3", "C2", "R2"):
        text = "" + chr(10).join(
            solve(engine, cases, ["--no-portfolio", "--single-thread",
                                  "--emit-proof", *named[name]])) + chr(10)
        proc = subprocess.run([sys.executable, str(tool), "--quiet", "-"],
                              input=text.encode(), capture_output=True, timeout=180)
        res.check(f"certificates produced under {name} verify",
                  proc.returncode == 0,
                  (proc.stdout + proc.stderr).decode().strip()[:120])


def test_order_and_scheduling_independence(engine: Path, res: Results) -> None:
    """An answer must not depend on batching, input order, or thread scheduling.

    Two separate claims. Batch independence matters because state could in
    principle leak between positions in one run -- the iterative table is kept
    across depths, and a shared table exists -- so a position's answer could
    depend on what preceded it. Scheduling independence is a documented design
    property: the root split accepts the LOWEST-index successful move precisely
    so the key move does not depend on which worker happens to finish first.

    Both were asserted and neither was gated.
    """
    print("\n[invariance] answers ignore order, batching and scheduling")

    cases = load_epd(HERE / "mates.epd") + load_epd(HERE / "nomate.epd")

    def answers(order, extra):
        lines = solve(engine, order, extra)
        out = {}
        for (fen, dm), line in zip(order, lines):
            normalised = re.sub(r"acs [\d.e+-]+", "acs X",
                                re.sub(r"acn \d+", "acn N", line)).strip()
            out[(fen, dm)] = normalised
        return out

    deterministic = ["--no-portfolio", "--single-thread"]
    forward = answers(cases, deterministic)
    res.check("batch run produces an answer per case", len(forward) == len(cases))

    reverse = answers(list(reversed(cases)), deterministic)
    res.check("answers are identical in reverse order",
              reverse == forward,
              next((str(k) for k in forward if forward[k] != reverse.get(k)), ""))

    shuffled = cases[:]
    random.Random(20260801).shuffle(shuffled)
    res.check("answers are identical in shuffled order",
              answers(shuffled, deterministic) == forward, "")

    # One position per process: no shared state of any kind can survive.
    solo = {}
    for case in cases:
        solo.update(answers([case], deterministic))
    res.check("answers are identical one position per process",
              solo == forward,
              next((str(k) for k in forward if forward[k] != solo.get(k)), ""))

    # Scheduling: the key move must be the same at every thread count.
    #
    # Honest limitation: this arm has NOT been shown to discriminate. Injecting
    # the opposite rule -- accept whichever root move finishes first instead of
    # the lowest index -- changes nothing observable, on shallow mates, on hard
    # mate-in-8 positions, with forced splitting, or on a constructed dual where
    # two root moves both mate. The rule only bites when two root moves mate AND
    # workers race, and the ordered-first move always resolves first. Treat this
    # as a consistency check against gross breakage, not as evidence for the
    # lowest-index property. See architecture 8v.
    baseline = answers(cases, ["--no-portfolio", "--single-thread"])
    for threads in ("2", "4", "8", "16", "32"):
        got = answers(cases, ["--no-portfolio", "--threads", threads])
        differing = [k for k in baseline if baseline[k] != got.get(k)]
        res.check(f"answers at {threads} threads match the sequential answer",
                  not differing,
                  f"{differing[0][0][:30] if differing else ''}")


def test_persistent_service_mode(engine: Path, res: Results) -> None:
    """The engine must answer each position before the next one arrives.

    Feeding positions on stdin and reading answers as they come is the whole of
    the "persistent service" mode: one process, many positions, no restart cost
    and no protocol beyond the documented line format. That only works if each
    result line reaches the client immediately.

    It did so before the flush was made explicit, but only because std::cin is
    tied to std::cout so the next read flushes it. sync_with_stdio(false) or
    cin.tie(nullptr) -- both routine throughput tweaks -- would have silently
    turned this into output that appears only at exit. This pins the behaviour
    against that.
    """
    print("\n[service] positions are answered as they arrive")

    mate = "2brrb2/8/p7/7Q/1p1kpPp1/1P1pN1K1/3P4/8 w - - bm #2;"
    nomate = "4k3/8/8/8/8/8/8/4K3 w - - bm #1;"

    process = subprocess.Popen(
        [str(engine), "-5", "--no-portfolio", "-"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
    answers = []
    try:
        for request in (mate, nomate, mate):
            process.stdin.write((request + chr(10)).encode())
            process.stdin.flush()
            captured = {}

            def read_one():
                captured["line"] = process.stdout.readline()

            reader = threading.Thread(target=read_one, daemon=True)
            reader.start()
            reader.join(timeout=30)
            if "line" not in captured:
                break
            answers.append(captured["line"].decode())
    finally:
        try:
            process.stdin.close()
            process.wait(timeout=30)
        except Exception:
            process.kill()

    res.check("every position was answered while the process stayed open",
              len(answers) == 3, f"got {len(answers)} of 3")
    if len(answers) != 3:
        return
    res.check("the mate is solved in service mode", "dm 2" in answers[0], answers[0].strip()[:80])
    res.check("the negative control is disproved, not timed out",
              DM_RE.search(answers[1]) is None and "timeout" not in answers[1],
              answers[1].strip()[:80])
    res.check("repeating a position gives the same answer",
              re.sub(r"ac[ns] [\d.e+-]+", "X", answers[0])
              == re.sub(r"ac[ns] [\d.e+-]+", "X", answers[2]),
              answers[2].strip()[:80])
    res.check("the process exits cleanly at end of input", process.returncode == 0,
              str(process.returncode))


def test_version_is_single_sourced(engine: Path, res: Results) -> None:
    """Every build of the same source must report the same version.

    It did not: CMake declared 0.1.0 and passed it in, while a direct g++ build
    fell back to a hardcoded "0.1.0-dev". Two builds of identical code reported
    different versions and neither string said which was which -- so a version in
    a bug report identified the build system, not the code.

    The source now holds the version and CMake parses it out.
    """
    print("\n[release] the version has one source of truth")

    root = HERE.parent
    source = (root / "src" / "echest.cpp").read_text(encoding="utf-8")
    declared = re.search(r'#define ECHEST_VERSION "([^"]+)"', source)
    res.check("the source declares a version", declared is not None)
    if not declared:
        return

    reported = run(engine, ["--version"], "").strip()
    res.check("--version matches the source declaration",
              reported == f"echest {declared.group(1)}",
              f"{reported!r} vs source {declared.group(1)!r}")

    banner = run(engine, ["--help"], "").splitlines()[0]
    res.check("--help banner carries the same version",
              declared.group(1) in banner, banner)

    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    res.check("CMakeLists declares no competing version",
              not re.search(r"\bVERSION\s+\d+\.\d+\.\d+", cmake),
              "a hardcoded version in CMakeLists will drift from the source")

    changelog = root / "CHANGELOG.md"
    res.check("a changelog exists", changelog.exists())
    if changelog.exists():
        res.check("the changelog documents this version",
                  declared.group(1) in changelog.read_text(encoding="utf-8"),
                  f"no entry for {declared.group(1)}")


def test_alternate_routes_and_modes(engine: Path, res: Results) -> None:
    """Everything the CLI can reach must be exercised, including what lost.

    Coverage measurement found dfpn.h at **0%**: the DFPN route ships, is
    reachable with --route dfpn, and no test had ever run a line of it. That is
    also where static analysis found its only two substantive defects (19), which
    is not a coincidence -- untested code is where defects survive.

    A route being unpromoted is a reason to warn about its speed, not a reason to
    ship it unverified. These use shallow positions because DFPN is slow enough
    at depth to dominate the suite's runtime, which is itself why it lost (8e).
    """
    print("\n[routes] unpromoted routes and auxiliary modes still work")

    shallow = [(fen, dm) for fen, dm in load_epd(HERE / "mates.epd") if dm <= 3]
    res.check("shallow cases available for the slow routes", len(shallow) >= 4)

    for route in ("dfpn", "shallow-fast", "depth-first"):
        lines = solve(engine, shallow, ["--route", route, "--single-thread", "--no-portfolio"])
        solved = 0
        for (fen, dm), line in zip(shallow, lines):
            found = DM_RE.search(line)
            if found:
                solved += 1
                res.check(f"{route}: correct depth for {fen[:22]}",
                          int(found.group(1)) == dm, line.strip()[:80])
        res.check(f"{route} solves every shallow case", solved == len(shallow),
                  f"{solved} of {len(shallow)}")

    # A proof from an unpromoted route must verify like any other.
    if HAVE_CHESS:
        tool = HERE.parent / "tools" / "verify_proof.py"
        text = chr(10).join(solve(engine, shallow,
                                  ["--route", "dfpn", "--single-thread",
                                   "--no-portfolio", "--emit-proof"])) + chr(10)
        proc = subprocess.run([sys.executable, str(tool), "--quiet", "-"],
                              input=text.encode(), capture_output=True, timeout=300)
        res.check("certificates from the DFPN route verify", proc.returncode == 0,
                  (proc.stdout + proc.stderr).decode().strip()[:120])
    else:
        res.skip("DFPN certificates verify", "python-chess not installed")

    # Auxiliary output modes: reachable from the CLI, so they are contract too.
    start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"
    divide = run(engine, ["--perft-divide", "2", "-"], start + chr(10))
    res.check("perft-divide totals the reference count",
              "total 400" in divide, divide.strip()[-60:])
    res.check("perft-divide lists every root move",
              len([l for l in divide.splitlines() if l and not l.startswith("total")]) == 20,
              divide.strip()[:60])

    legal = run(engine, ["--list-legal", "-"], start + chr(10))
    res.check("list-legal counts the opening moves", "legal_count 20" in legal,
              legal.strip()[:80])

    profile = run(engine, ["-5", "-z", "2", "--profile", "--single-thread",
                           "--no-portfolio", "-"],
                  "2brrb2/8/p7/7Q/1p1kpPp1/1P1pN1K1/3P4/8 w - -" + chr(10))
    res.check("a solved position still solves under --profile", "dm 2" in profile,
              profile.strip()[:80])


def test_node_limit_is_deterministic(engine: Path, res: Results) -> None:
    """--node-limit must be reproducible and must never look like a disproof.

    Wall-clock budgets make comparisons noisy: one configuration measured 49,
    51, 52 and 53 of 60 across this session purely on where positions fell
    relative to the clock, which is the size of the effects being measured.

    The soundness requirement is the one from 8r: a bare line means "searched
    exhaustively, no mate exists". An exhausted node budget has settled nothing,
    so it must report the "gave up" outcome, exactly as a wall-clock expiry does.
    """
    print("\n[budget] --node-limit is deterministic and claims nothing")

    hard = "3R4/pk6/p1pp4/B1pqPp2/3P3n/1N4N1/KR1P1p2/5r2 w - -" + chr(10)
    args = ["-5", "-z", "8", "--single-thread", "--no-portfolio", "--node-limit", "200000", "-"]

    runs = {re.sub(r"acs [\d.e+-]+", "acs X", run(engine, args, hard).strip())
            for _ in range(3)}
    res.check("three runs at the same node limit are identical", len(runs) == 1,
              "; ".join(sorted(runs))[:160])

    only = next(iter(runs))
    res.check("an exhausted node budget reports the gave-up outcome",
              only.endswith("timeout;"), only[:100])
    res.check("an exhausted node budget claims no mate", DM_RE.search(only) is None, only[:100])
    res.check("the node count stops at the limit", "acn 200000;" in only, only[:100])

    # A genuine disproof must still be reported as one, not as a budget expiry.
    disproved = run(engine, ["-5", "-z", "1", "--node-limit", "1000000", "-"],
                    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -" + chr(10)).strip()
    res.check("a completed disproof under a node limit carries no marker",
              re.fullmatch(r"[^;]+; acn \d+; acs [\d.e+-]+;", disproved) is not None,
              disproved[:100])

    # And a solvable position still solves when the budget is ample.
    solved = run(engine, ["-5", "-z", "2", "--single-thread", "--no-portfolio",
                          "--node-limit", "1000000", "-"],
                 "2brrb2/8/p7/7Q/1p1kpPp1/1P1pN1K1/3P4/8 w - -" + chr(10))
    res.check("an ample node budget still solves", "dm 2" in solved, solved.strip()[:80])


def test_parallel_positions(engine: Path, res: Results) -> None:
    """Solving several positions at once must not change any answer or its place.

    Root-split parallelism inside a position now contributes nothing (32), so on
    a larger machine most cores are idle during a batch. Positions carry no state
    between them -- gated by test_order_and_scheduling_independence -- so they can
    be solved concurrently. The results must still appear in input order.

    Note this compares verdicts, not whole lines: under --portfolio-parallel the
    particular proof returned varies between runs of the same configuration, so
    two identical invocations already differ textually.
    """
    print("\n[batch] positions solved concurrently keep their order")

    cases = load_epd(HERE / "mates.epd") + load_epd(HERE / "nomate.epd")
    stdin = "".join(f"{fen} bm #{dm};" + chr(10) for fen, dm in cases)

    def verdicts(width):
        # A node budget rather than a clock: under a wall-clock limit, positions
        # sharing cores each do slightly less work, so a wider batch can lose a
        # position for reasons that have nothing to do with ordering. With a node
        # budget the answers are identical at every width, which is the property
        # worth pinning (34).
        out = run(engine, ["-5", "--node-limit", "300000",
                           "--parallel-positions", str(width), "-"], stdin)
        lines = [l for l in out.splitlines() if l.strip()]
        res.check(f"one line per position at width {width}", len(lines) == len(cases),
                  f"got {len(lines)} of {len(cases)}")
        marks = []
        for line in lines:
            found = DM_RE.search(line)
            marks.append(found.group(1) if found else ("timeout" if "timeout" in line else "none"))
        return marks, lines

    sequential, seq_lines = verdicts(1)
    for width in (2, 5):
        concurrent, con_lines = verdicts(width)
        res.check(f"width {width} gives the same verdicts, in the same order",
                  concurrent == sequential,
                  next((f"position {i}: {a} vs {b}"
                        for i, (a, b) in enumerate(zip(sequential, concurrent)) if a != b), ""))
        res.check(f"width {width} keeps each result on its own position",
                  [l.split(";")[0] for l in con_lines] == [l.split(";")[0] for l in seq_lines],
                  "positions came back against the wrong input lines")

    # The default must remain 1, because streaming service mode depends on it.
    config = json.loads(run(engine, ["--print-config"], ""))
    res.check("parallel-positions defaults to 1", config.get("parallel_positions", 1) == 1,
              str(config.get("parallel_positions")))


def test_comparisons_actually_differ(engine: Path, res: Results) -> None:
    """Anything presented as a comparison must compare two different things.

    The reproduction tool's "depth-first" rows once passed no --route and relied
    on depth-first being the default. When dfpn was promoted they began running
    dfpn, so the tool compared a route with itself and still printed the old
    expectations beside it -- and passed (37). A gate that compares a
    configuration against itself passes forever.

    --print-config reports the effective configuration, so this is checkable
    mechanically rather than by remembering which defaults have moved.
    """
    print("\n[audit] configurations presented as different are different")

    def effective(args):
        return run(engine, ["--print-config", *args], "").strip()

    routes = {name: effective(["--route", name])
              for name in ("depth-first", "dfpn", "shallow-fast")}
    res.check("the three routes give three different configurations",
              len(set(routes.values())) == 3,
              "two routes resolve to the same configuration")

    # Read the reproduction tool's table rather than re-describing it here: each
    # row is quoted with its own expected result, so two identical rows would
    # mean two of those results describe the same run.
    spec = importlib.util.spec_from_file_location(
        "reproduce_results", HERE.parent / "tools" / "reproduce_results.py")
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)

    configs = {}
    for label, _suite, extra, _budget, _documented in tool.DETERMINISTIC:
        configs[label] = effective(list(extra))
    res.check("the deterministic table has entries to check", len(configs) >= 4,
              str(len(configs)))
    duplicates = sorted(a for a in configs if list(configs.values()).count(configs[a]) > 1)
    res.check("every deterministic-table row is a distinct configuration",
              not duplicates, "; ".join(duplicates[:3]))

    # A flag that has become a default is no longer a comparison. Spot-check the
    # ones 8n moved: passing them must be a no-op against the plain default.
    plain = effective([])
    for flag in ("--proof-hints", "--keep-iter-tt", "--inplace-order"):
        res.check(f"{flag} is a default now, so passing it changes nothing",
                  effective([flag]) == plain, f"{flag} still alters the configuration")


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
    test_alternate_routes_and_modes(args.engine, res)
    test_no_mate(args.engine, res)
    test_abort_invariant_under_stress(args.engine, res)
    test_invariance(args.engine, res)
    test_order_and_scheduling_independence(args.engine, res)
    test_time_limit(args.engine, res)
    test_node_limit_is_deterministic(args.engine, res)
    test_illegal_positions_rejected(args.engine, res)
    test_castling_needs_its_rook(args.engine, res)
    test_cli_contract(args.engine, res)
    test_version_is_single_sourced(args.engine, res)
    test_help_documents_every_option(args.engine, res)
    test_documented_defaults_are_real(args.engine, res)
    test_comparisons_actually_differ(args.engine, res)
    test_checks_only_restriction(args.engine, res)
    test_defender_restrictions(args.engine, res)
    test_restriction_soundness_and_nesting(args.engine, res)
    test_restriction_portfolio(args.engine, res)
    test_shipped_verifier(args.engine, res)
    test_verifier_rejects_stalemate_as_mate(args.engine, res)
    test_pv_and_certificates(args.engine, res)
    test_corpus_ergonomics(args.engine, res)
    test_bom_tolerated_on_input(args.engine, res)
    test_memory_budget_is_a_total(args.engine, res)
    test_persistent_service_mode(args.engine, res)
    test_parallel_positions(args.engine, res)
    test_output_format_conformance(args.engine, res)
    test_docs_reference_shipped_files(args.engine, res)

    print(f"\n{res.passed} passed, {len(res.failed)} failed, {len(res.skipped)} skipped")
    for failure in res.failed:
        print(f"  FAILED: {failure}")
    return 1 if res.failed else 0


if __name__ == "__main__":
    sys.exit(main())
