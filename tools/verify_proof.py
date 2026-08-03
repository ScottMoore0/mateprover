#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Independently verify mateprover proof certificates.

mateprover's central claim is that a reported mate is a *proof*, not a search
result. This checker is what makes that claim testable by someone who does not
trust the engine: it re-derives every legal move itself, using python-chess,
and accepts a certificate only if every step holds.

A certificate is valid only when, at every node:

  * the attacker's move is legal in the position reached so far;
  * a leaf marked `mate` really is checkmate;
  * a defender node lists **exactly** the legal replies — no more, no fewer,
    so a proof cannot quietly omit a defence that refutes it;
  * every listed reply leads to a valid sub-proof.

It also checks the reported principal variation independently: the PV must
replay legally, end in checkmate, and have the length the reported mate depth
implies.

Usage:

    mateprover --emit-proof -z 5 - < positions.epd | python verify_proof.py
    python verify_proof.py engine_output.txt

Exit status is 0 only if every certificate and PV in the input verified.
"""

from __future__ import annotations

import argparse
import json
import re
import sys

try:
    import chess
except ImportError:
    sys.stderr.write(
        "verify_proof.py needs python-chess:\n    pip install chess\n"
        "It is a deliberate dependency: verification must not reuse the\n"
        "engine's own move generator, or it would prove nothing.\n")
    raise SystemExit(2)

DM_RE = re.compile(r"\bdm\s+(\d+)\b")
# `sm N` is a forced STALEMATE in N -- a different claim about the position,
# checked against a different terminal predicate.
SM_RE = re.compile(r"\bsm\s+(\d+)\b")
PV_RE = re.compile(r"\bpv\s+([^;]+);")
PROOF_RE = re.compile(r"\bproof\s+(\{.*\})\s*;", re.DOTALL)


class Failure(Exception):
    pass


def verify_node(board: chess.Board, node: dict, path: list[str],
                goal: str = "mate") -> int:
    """Verify one attacker node. Returns the depth in attacker moves."""
    where = " ".join(path) if path else "<root>"

    move_uci = node.get("a")
    if not isinstance(move_uci, str):
        raise Failure(f"after {where}: attacker node has no move")
    try:
        move = chess.Move.from_uci(move_uci)
    except ValueError:
        raise Failure(f"after {where}: unparseable move {move_uci!r}")
    if move not in board.legal_moves:
        raise Failure(f"after {where}: illegal attacker move {move_uci}")

    board.push(move)
    try:
        # A leaf must claim the goal that was asked for, and be it. A stalemate
        # leaf in a mate proof -- or the reverse -- is exactly the confusion the
        # two goals make possible, so the claim and the test are both checked.
        if node.get("mate") or node.get("stalemate"):
            if goal == "stalemate":
                if not node.get("stalemate"):
                    raise Failure(f"after {where} {move_uci}: leaf claims mate in a "
                                  f"stalemate proof")
                if not board.is_stalemate():
                    raise Failure(f"after {where} {move_uci}: leaf is not stalemate")
            else:
                if not node.get("mate"):
                    raise Failure(f"after {where} {move_uci}: leaf claims stalemate in "
                                  f"a mate proof")
                if not board.is_checkmate():
                    raise Failure(f"after {where} {move_uci}: leaf is not checkmate")
            return 1

        branches = node.get("d")
        if branches is None:
            raise Failure(f"after {where} {move_uci}: non-leaf node has no replies")
        if not branches:
            # An empty reply list is never legitimate. If the position really
            # has no legal replies it is mate or stalemate, and mate must be
            # claimed with "mate": true so that it gets checked. Accepting an
            # empty list here would verify a STALEMATE as a forced mate: the
            # listed-equals-legal comparison below is vacuously true when both
            # are empty, and the recursion adds a ply and returns success.
            raise Failure(f"after {where} {move_uci}: empty reply list; a "
                          f"position with no legal replies is mate or "
                          f"stalemate and must be stated as a leaf")

        listed = sorted(b.get("r", "") for b in branches)
        legal = sorted(m.uci() for m in board.legal_moves)
        if listed != legal:
            missing = sorted(set(legal) - set(listed))
            extra = sorted(set(listed) - set(legal))
            detail = []
            if missing:
                detail.append(f"missing defences {missing}")
            if extra:
                detail.append(f"claims illegal defences {extra}")
            if not detail:
                # Same set, different multiset: a legal reply listed more than
                # once. Rejected either way, but say why rather than failing
                # with an empty explanation.
                seen = {}
                for uci in listed:
                    seen[uci] = seen.get(uci, 0) + 1
                dupes = sorted(u for u, n in seen.items() if n > 1)
                detail.append(f"lists duplicate defences {dupes}")
            raise Failure(f"after {where} {move_uci}: " + "; ".join(detail))

        worst = 0
        for branch in branches:
            reply = branch["r"]
            board.push(chess.Move.from_uci(reply))
            try:
                worst = max(worst, verify_node(board, branch["p"],
                                               path + [move_uci, reply], goal))
            finally:
                board.pop()
        return worst + 1
    finally:
        board.pop()


def verify_pv(board: chess.Board, pv: list[str], claimed_depth: int,
              goal: str = "mate") -> None:
    replay = board.copy()
    for token in pv:
        try:
            move = chess.Move.from_uci(token)
        except ValueError:
            raise Failure(f"unparseable pv move {token!r}")
        if move not in replay.legal_moves:
            raise Failure(f"illegal pv move {token}")
        replay.push(move)
    if goal == "stalemate":
        if not replay.is_stalemate():
            raise Failure("pv does not end in stalemate")
    elif not replay.is_checkmate():
        raise Failure("pv does not end in checkmate")
    expected = 2 * claimed_depth - 1
    if len(pv) != expected:
        raise Failure(f"pv has {len(pv)} plies, expected {expected} for mate in "
                      f"{claimed_depth}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", nargs="?", default="-",
                    help="engine output file, or - for stdin (default)")
    ap.add_argument("--quiet", action="store_true",
                    help="print only the summary and any failures")
    ap.add_argument("--require-proof", action="store_true",
                    help="fail if a solved position carries no certificate "
                         "(the engine emits them only under --emit-proof)")
    args = ap.parse_args()

    # utf-8-sig, not utf-8: a certificate saved on Windows may carry a BOM, and
    # the engine now tolerates one on its own input. Identical to utf-8 when no
    # BOM is present.
    stream = sys.stdin if args.input == "-" else open(args.input, encoding="utf-8-sig")

    checked = pv_only = skipped = 0
    failures: list[str] = []

    for raw in stream:
        line = raw.strip()
        if not line or line.startswith("%"):
            continue
        fen = line.split(";", 1)[0].strip()
        depth_match = DM_RE.search(line)
        goal = "mate"
        if not depth_match:
            depth_match = SM_RE.search(line)
            goal = "stalemate"
        if not depth_match:
            skipped += 1          # no mate reported: nothing to verify
            continue

        depth = int(depth_match.group(1))
        try:
            board = chess.Board(fen + " 0 1")
        except ValueError as exc:
            failures.append(f"{fen[:40]}: unusable FEN ({exc})")
            continue

        try:
            pv_match = PV_RE.search(line)
            if not pv_match:
                raise Failure("solved position has no pv")
            verify_pv(board, pv_match.group(1).split(), depth, goal)

            proof_match = PROOF_RE.search(line)
            if proof_match:
                node = json.loads(proof_match.group(1))
                proved = verify_node(board.copy(), node, [], goal)
                if proved != depth:
                    raise Failure(f"certificate proves mate in {proved}, "
                                  f"reported {depth}")
                checked += 1
                if not args.quiet:
                    print(f"  ok   {goal} in {depth}  {fen[:44]}")
            elif args.require_proof:
                raise Failure("no certificate (run the engine with --emit-proof)")
            else:
                pv_only += 1
                if not args.quiet:
                    print(f"  pv   mate in {depth}  {fen[:44]}  (no certificate)")
        except (Failure, json.JSONDecodeError, KeyError) as exc:
            failures.append(f"{fen[:44]}: {exc}")
            print(f"  FAIL mate in {depth}  {fen[:44]}: {exc}")

    print(f"\n{checked} certificate(s) verified, {pv_only} pv-only, "
          f"{len(failures)} failed, {skipped} line(s) with no mate reported")
    for failure in failures:
        print(f"  FAILED: {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
