#!/usr/bin/env python3
"""Search for positions that break MateProver, rather than patches that improve it.

Three modes:

    hunt   evolve positions that maximise the node count -- pathological cases
    fuzz   check random positions against an INDEPENDENT oracle (python-chess)
    perft  differential move generation over random positions

WHY THIS IS THE HIGHER-VALUE HALF
=================================

Automation is far better at finding problems than at fixing them. A tuner
returns a few percent; a fuzzer returns a class of bug. And the bugs this engine
can have are asymmetric: a wrong "no solution" is invisible in the output, it
verifies against nothing, and it is the one outcome the whole design exists to
prevent.

So `fuzz` spends most of its effort on the direction that has no certificate
behind it. When MateProver says "mate in N" it emits a proof tree that
python-chess can check move by move -- that path is already self-defending. When
it says nothing, there is no artefact at all, and the only check available is an
independent search. That is what `brute_force_mate` is: slow, obviously correct,
and written against a different move generator.

A disagreement is interesting whichever way it points.
"""

import argparse
import json
import random
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

try:
    import chess
except ImportError:
    print("adversarial.py needs python-chess: pip install chess", file=sys.stderr)
    raise SystemExit(2)

DM_RE = re.compile(r"\bdm (\d+)")
ACN_RE = re.compile(r"\bacn (\d+)")
PV_RE = re.compile(r"\bpv ([^;]+)")
PROOF_RE = re.compile(r"\bproof (\{.*\})")


# --------------------------------------------------------------------------
# The independent oracle
#
# Deliberately naive. Its only job is to be obviously right, and it is written
# against python-chess's move generator rather than MateProver's, so a shared
# bug would have to exist in two independent implementations.

def brute_force_mate(board: chess.Board, depth: int) -> bool:
    """Can the side to move force mate in exactly `depth` moves or fewer?"""
    if depth <= 0:
        return False
    for move in board.legal_moves:
        board.push(move)
        try:
            if board.is_checkmate():
                board.pop()
                return True
            if depth > 1 and _defender_all_lose(board, depth - 1):
                board.pop()
                return True
        except Exception:
            board.pop()
            raise
        board.pop()
    return False


def _defender_all_lose(board: chess.Board, depth: int) -> bool:
    """Every legal reply leaves the attacker able to mate within `depth`."""
    any_reply = False
    for move in board.legal_moves:
        any_reply = True
        board.push(move)
        ok = brute_force_mate(board, depth)
        board.pop()
        if not ok:
            return False
    return any_reply


# --------------------------------------------------------------------------
# Random legal positions

PIECES = "QRBNP"


def random_position(rng: random.Random, men: int) -> chess.Board | None:
    """A random legal position with `men` pieces, or None if the draw failed.

    Legality is decided by python-chess, not by construction: it is the
    authority everywhere else in this tool and using it here too means the
    generator cannot disagree with the checker.
    """
    board = chess.Board(None)
    squares = rng.sample(range(64), men)
    board.set_piece_at(squares[0], chess.Piece(chess.KING, chess.WHITE))
    board.set_piece_at(squares[1], chess.Piece(chess.KING, chess.BLACK))
    for sq in squares[2:]:
        colour = rng.choice([chess.WHITE, chess.BLACK])
        kind = rng.choice(PIECES)
        if kind == "P" and chess.square_rank(sq) in (0, 7):
            kind = "Q"
        board.set_piece_at(sq, chess.Piece.from_symbol(
            kind if colour == chess.WHITE else kind.lower()))
    board.turn = chess.WHITE
    board.castling_rights = 0
    if not board.is_valid():
        return None
    return board


def fen4(board: chess.Board) -> str:
    parts = board.fen().split()
    return " ".join(parts[:4])


# --------------------------------------------------------------------------
# Talking to the engine

def solve(engine: Path, fen: str, depth: int, extra=(), node_limit=3_000_000):
    cmd = [str(engine), "--no-portfolio", "--threads", "1",
           "--node-limit", str(node_limit), "-z", str(depth), *extra, "-"]
    proc = subprocess.run(cmd, input=fen + "\n", capture_output=True, text=True,
                          timeout=600)
    line = next((l for l in proc.stdout.splitlines() if l.strip()), "")
    return line


def verify_certificate(board: chess.Board, node) -> tuple[bool, str]:
    """Replay a proof tree against python-chess. Every branch, every reply."""
    move = chess.Move.from_uci(node["a"])
    if move not in board.legal_moves:
        return False, f"illegal attacker move {node['a']}"
    board.push(move)
    try:
        if node.get("mate"):
            return board.is_checkmate(), "" if board.is_checkmate() else "leaf is not mate"
        if node.get("stalemate"):
            return board.is_stalemate(), "" if board.is_stalemate() else "leaf is not stalemate"
        branches = node.get("d")
        if branches is None:
            return False, "interior attacker node has no defender branches"
        legal = sorted(m.uci() for m in board.legal_moves)
        if sorted(b["r"] for b in branches) != legal:
            return False, "defender branches do not match the legal replies"
        for branch in branches:
            board.push(chess.Move.from_uci(branch["r"]))
            ok, why = verify_certificate(board, branch["p"])
            board.pop()
            if not ok:
                return False, why
        return True, ""
    finally:
        board.pop()


# --------------------------------------------------------------------------
# fuzz: check the engine against the oracle

def mode_fuzz(args):
    rng = random.Random(args.seed)
    checked = failures = skipped = 0
    for _ in range(args.rounds):
        board = random_position(rng, rng.randint(args.min_men, args.max_men))
        if board is None or board.is_game_over():
            skipped += 1
            continue
        fen = fen4(board)
        line = solve(args.engine, fen, args.depth)
        if "timeout" in line:
            skipped += 1
            continue
        checked += 1
        claimed = DM_RE.search(line)

        truth = brute_force_mate(board.copy(), args.depth)

        if claimed and not truth:
            print(f"FALSE PROOF   {fen}  engine says {claimed.group(0)}, oracle says none")
            failures += 1
        elif truth and not claimed:
            # THE IMPORTANT DIRECTION. A missed mate is a false "no solution",
            # it carries no certificate, and nothing downstream would notice.
            print(f"MISSED MATE   {fen}  oracle finds a mate within {args.depth}")
            failures += 1
        elif claimed:
            # Depth minimality: the engine promises the SHORTEST mate.
            n = int(claimed.group(1))
            if n > 1 and brute_force_mate(board.copy(), n - 1):
                print(f"NON-MINIMAL   {fen}  engine says dm {n}, a shorter mate exists")
                failures += 1
            # And the certificate must replay.
            cert_line = solve(args.engine, fen, args.depth, extra=("--emit-proof",))
            cert = PROOF_RE.search(cert_line)
            if cert:
                ok, why = verify_certificate(board.copy(), json.loads(cert.group(1)))
                if not ok:
                    print(f"BAD PROOF     {fen}  {why}")
                    failures += 1
        if checked % 25 == 0:
            print(f"  ... {checked} checked, {failures} failures", file=sys.stderr)
    print(f"\nfuzz: {checked} positions checked, {skipped} skipped, {failures} FAILURES")
    return 1 if failures else 0


# --------------------------------------------------------------------------
# perft: differential move generation

def mode_perft(args):
    rng = random.Random(args.seed)
    checked = failures = 0
    for _ in range(args.rounds):
        board = random_position(rng, rng.randint(args.min_men, args.max_men))
        if board is None:
            continue
        fen = fen4(board)
        depth = rng.randint(1, 3)
        proc = subprocess.run([str(args.engine), "--perft", str(depth), "-"],
                              input=fen + "\n", capture_output=True, text=True, timeout=600)
        # `--perft N` prints one line per depth from 1 to N, so the count
        # wanted is the one tagged with the requested depth. Matching the first
        # `nodes` on the output compares depth 1 against depth N and reports a
        # mismatch on almost every position -- which is what the first run of
        # this tool did, and is a good reminder that a fuzzer is itself code
        # that has to be checked before its findings mean anything.
        m = re.search(r"perft %d; nodes (\d+)" % depth, proc.stdout)
        if not m:
            continue
        checked += 1
        theirs = int(m.group(1))
        ours = _perft(board, depth)
        if theirs != ours:
            print(f"PERFT MISMATCH {fen} depth {depth}: engine {theirs}, python-chess {ours}")
            failures += 1
    print(f"\nperft: {checked} positions checked, {failures} FAILURES")
    return 1 if failures else 0


def _perft(board: chess.Board, depth: int) -> int:
    if depth == 0:
        return 1
    if depth == 1:
        return board.legal_moves.count()
    total = 0
    for move in board.legal_moves:
        board.push(move)
        total += _perft(board, depth - 1)
        board.pop()
    return total


# --------------------------------------------------------------------------
# hunt: evolve positions that cost the most nodes
#
# Fitness is the node count, so this finds where the search is WEAK rather than
# where it is wrong. Every pathological case this turns up is a candidate for
# the corpus, and the corpus is what every other measurement rests on.

def mode_hunt(args):
    rng = random.Random(args.seed)

    def cost(board):
        line = solve(args.engine, fen4(board), args.depth, node_limit=args.node_limit)
        m = ACN_RE.search(line)
        return (int(m.group(1)) if m else 0), line

    def mutate(board):
        child = board.copy()
        for _ in range(rng.randint(1, 3)):
            occupied = list(child.piece_map().keys())
            action = rng.random()
            if action < 0.5 and occupied:
                # Move a non-king piece somewhere empty.
                movable = [s for s in occupied
                           if child.piece_at(s).piece_type != chess.KING]
                empty = [s for s in range(64) if child.piece_at(s) is None]
                if movable and empty:
                    src = rng.choice(movable)
                    piece = child.piece_at(src)
                    child.remove_piece_at(src)
                    child.set_piece_at(rng.choice(empty), piece)
            elif action < 0.75 and len(occupied) > 2:
                victim = [s for s in occupied
                          if child.piece_at(s).piece_type != chess.KING]
                if victim:
                    child.remove_piece_at(rng.choice(victim))
            else:
                empty = [s for s in range(64) if child.piece_at(s) is None]
                if empty and len(occupied) < args.max_men:
                    kind = rng.choice(PIECES)
                    sq = rng.choice(empty)
                    if kind == "P" and chess.square_rank(sq) in (0, 7):
                        kind = "Q"
                    colour = rng.choice([chess.WHITE, chess.BLACK])
                    child.set_piece_at(sq, chess.Piece.from_symbol(
                        kind if colour == chess.WHITE else kind.lower()))
        child.turn = chess.WHITE
        child.castling_rights = 0
        return child if child.is_valid() else None

    population = []
    while len(population) < args.population:
        board = random_position(rng, rng.randint(args.min_men, args.max_men))
        if board is not None and not board.is_game_over():
            population.append(board)

    hall_of_fame = []
    for generation in range(args.generations):
        scored = []
        for board in population:
            nodes, line = cost(board)
            scored.append((nodes, board, line))
        scored.sort(key=lambda t: -t[0])
        best = scored[0]
        print(f"gen {generation + 1}: worst case {best[0]:,} nodes  {fen4(best[1])}")
        hall_of_fame.extend((n, fen4(b)) for n, b, _ in scored[:3])
        elite = [b for _, b, _ in scored[:max(2, args.population // 3)]]
        population = list(elite)
        while len(population) < args.population:
            child = mutate(rng.choice(elite))
            if child is not None and not child.is_game_over():
                population.append(child)

    hall_of_fame.sort(key=lambda t: -t[0])
    seen, out = set(), []
    for nodes, fen in hall_of_fame:
        if fen not in seen:
            seen.add(fen)
            out.append({"fen4": fen, "nodes": nodes, "depth": args.depth})
    print(f"\nhunt: {len(out)} distinct hard positions, worst {out[0]['nodes']:,} nodes")
    if args.out:
        args.out.write_text("\n".join(json.dumps(r) for r in out[:args.keep]) + "\n",
                            encoding="utf-8")
        print(f"wrote {min(len(out), args.keep)} to {args.out}")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=["hunt", "fuzz", "perft"])
    p.add_argument("--engine", type=Path, default=ROOT / "build" / "mateprover.exe")
    p.add_argument("--rounds", type=int, default=200)
    p.add_argument("--depth", type=int, default=3)
    p.add_argument("--min-men", type=int, default=4)
    p.add_argument("--max-men", type=int, default=7)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--population", type=int, default=12)
    p.add_argument("--generations", type=int, default=8)
    p.add_argument("--node-limit", type=int, default=3_000_000)
    p.add_argument("--keep", type=int, default=20)
    p.add_argument("--out", type=Path)
    args = p.parse_args()
    return {"hunt": mode_hunt, "fuzz": mode_fuzz, "perft": mode_perft}[args.mode](args)


if __name__ == "__main__":
    raise SystemExit(main())
