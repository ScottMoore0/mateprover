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

    # 3. corrupt the principal variation
    code, _ = run_verifier(re.sub(r"pv [^;]+;", "pv a1a2;", first) + "\n")
    res.check("verifier rejects a corrupted pv", code != 0)

    # 4. overstate the mate depth
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
    test_time_limit(args.engine, res)
    test_illegal_positions_rejected(args.engine, res)
    test_castling_needs_its_rook(args.engine, res)
    test_cli_contract(args.engine, res)
    test_help_documents_every_option(args.engine, res)
    test_checks_only_restriction(args.engine, res)
    test_defender_restrictions(args.engine, res)
    test_restriction_portfolio(args.engine, res)
    test_shipped_verifier(args.engine, res)
    test_pv_and_certificates(args.engine, res)
    test_corpus_ergonomics(args.engine, res)

    print(f"\n{res.passed} passed, {len(res.failed)} failed, {len(res.skipped)} skipped")
    for failure in res.failed:
        print(f"  FAILED: {failure}")
    return 1 if res.failed else 0


if __name__ == "__main__":
    sys.exit(main())
