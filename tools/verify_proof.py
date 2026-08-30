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
Under --require-proof an EMPTY input is a failure, not a vacuous pass: in a
pipeline a producer that crashes emits nothing, and calling that success
defeats the point of checking. Pass --expect N to also catch a producer
that died part way through and left a valid prefix.
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
# `sfm N` is a forced SELFMATE: the attacker compels the defender to mate him.
SFM_RE = re.compile(r"\bsfm\s+(\d+)\b")
SSM_RE = re.compile(r"\bssm\s+(\d+)\b")
HM_RE = re.compile(r"\bhm\s+(\d+)\b")
HSM_RE = re.compile(r"\bhsm\s+(\d+)\b")
PV_RE = re.compile(r"\bpv\s+([^;]+);")
PROOF_RE = re.compile(r"\bproof\s+(\{.*\})\s*;", re.DOTALL)


class Failure(Exception):
    pass


def split_variant_field(fen: str) -> tuple[str, dict[str, list[int]] | None]:
    """('<four Forsyth fields>', {rule: [white_left, black_left]} or None).

    python-chess has no notion of a variant quota, so the verifier tracks them
    itself -- which is the right division anyway: a proof that ends "and this was
    my third check" must be checked against the counter the ENGINE claimed, not
    against one the verifier inferred.

    Accepts the same spellings the engine emits: a bare `3+3` for checks alone,
    the Lichess `+1+0`, and the tagged `chk3+3,cap5+2`.
    """
    fields = fen.split()
    if len(fields) < 5:
        return " ".join(fields[:4]), None
    quotas: dict[str, list[int]] = {}
    for term in fields[4].split(","):
        rule, body = "chk", term
        if term.startswith("chk"):
            body = term[3:]
        elif term.startswith("cap"):
            rule, body = "cap", term[3:]
        delivered = body.startswith("+")
        if delivered:
            body = body[1:]
        if body.count("+") != 1:
            return " ".join(fields[:4]), None
        left, _, right = body.partition("+")
        if not (left.isdigit() and right.isdigit()):
            return " ".join(fields[:4]), None
        if delivered:
            if rule != "chk":
                return " ".join(fields[:4]), None
            quotas[rule] = [3 - int(left), 3 - int(right)]
        else:
            quotas[rule] = [int(left), int(right)]
    return " ".join(fields[:4]), (quotas or None)


# The quota is spent going down the tree and refunded coming back up, exactly as
# the board is. A leaf's claim is only meaningful against the counters as they
# stand AT that leaf, not as they stood at the root.
def spend_quotas(quotas, mover: int, after: chess.Board, was_capture: bool) -> list[str]:
    if quotas is None:
        return []
    spent = []
    if "chk" in quotas and after.is_check():
        quotas["chk"][mover] -= 1
        spent.append("chk")
    if "cap" in quotas and was_capture:
        quotas["cap"][mover] -= 1
        spent.append("cap")
    return spent


def refund_quotas(quotas, mover: int, spent: list[str]) -> None:
    for rule in spent:
        quotas[rule][mover] += 1


VARIANT_LEAF = {"checkwin": "chk", "capturewin": "cap"}


def verify_node(board: chess.Board, node: dict, path: list[str],
                goal: str = "mate", quotas: dict[str, list[int]] | None = None) -> int:
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

    mover = int(board.turn == chess.BLACK)
    was_capture = board.is_capture(move)
    board.push(move)
    spent = spend_quotas(quotas, mover, board, was_capture)
    try:
        # A leaf must claim the goal that was asked for, and be it. A stalemate
        # leaf in a mate proof -- or the reverse -- is exactly the confusion the
        # two goals make possible, so the claim and the test are both checked.
        # A variant leaf: the mover's final check, or final capture, ended the
        # game. Checked, not taken on trust -- the move must actually be the
        # event claimed, and the quota must actually reach zero on it. A
        # certificate that claims a quota win without one is exactly as wrong as
        # a forged mate leaf, and would otherwise be accepted unlooked at.
        for claim, rule in VARIANT_LEAF.items():
            if not node.get(claim):
                continue
            if quotas is None or rule not in quotas:
                raise Failure(f"after {where} {move_uci}: leaf claims {claim} but "
                              f"the position states no quota for it")
            if goal != "mate":
                raise Failure(f"after {where} {move_uci}: {claim} cannot satisfy "
                              f"a {goal} stipulation")
            if rule not in spent:
                raise Failure(f"after {where} {move_uci}: leaf claims {claim} but "
                              f"the move is not that event")
            if quotas[rule][mover] != 0:
                raise Failure(f"after {where} {move_uci}: leaf claims {claim} with "
                              f"{quotas[rule][mover]} still owed")
            return 1

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
            reply_mover = int(board.turn == chess.BLACK)
            reply_move = chess.Move.from_uci(reply)
            reply_capture = board.is_capture(reply_move)
            board.push(reply_move)
            reply_spent = spend_quotas(quotas, reply_mover, board, reply_capture)
            try:
                worst = max(worst, verify_node(board, branch["p"],
                                               path + [move_uci, reply], goal,
                                               quotas))
            finally:
                board.pop()
                refund_quotas(quotas, reply_mover, reply_spent)
        return worst + 1
    finally:
        board.pop()
        refund_quotas(quotas, mover, spent)


def verify_help_node(board: chess.Board, node: dict, path: list[str], goal: str) -> int:
    """Verify one cooperative node. Returns the number of PLIES verified.

    A helpmate certificate is a chain, not a tree: `{"h": move, "n": next}`
    ending in `{"helpmated": true}`. There is no branching to check because
    there is no adversary -- nothing has to hold against every reply, so the
    obligation here is only that every move is legal and the terminal really is
    what it claims.

    That makes the LENGTH the load-bearing check. `h#3` means a mate on move
    three, exactly, so a chain that reaches mate early answers a different
    stipulation; the caller compares the ply count this returns against the
    depth the engine reported.
    """
    where = " ".join(path) if path else "<root>"
    want = "helpmated" if goal == "helpmate" else "helpstalemated"
    other = "helpstalemated" if goal == "helpmate" else "helpmated"

    if node.get(other):
        raise Failure(f"after {where}: leaf claims {other} inside a {goal} proof")
    if node.get(want):
        if goal == "helpmate":
            if not board.is_checkmate():
                raise Failure(f"after {where}: leaf claims helpmate, but the side "
                              f"to move is not checkmated")
        elif not board.is_stalemate():
            raise Failure(f"after {where}: leaf claims helpstalemate, but the side "
                          f"to move is not stalemated")
        return 0
    if node.get("mate") or node.get("stalemate") or node.get("selfmated") \
            or node.get("selfstalemated"):
        raise Failure(f"after {where}: non-cooperative leaf inside a {goal} proof")

    move_uci = node.get("h")
    if not isinstance(move_uci, str):
        raise Failure(f"after {where}: cooperative node has no move")
    try:
        move = chess.Move.from_uci(move_uci)
    except ValueError:
        raise Failure(f"after {where}: unparseable move {move_uci!r}")
    if move not in board.legal_moves:
        raise Failure(f"after {where}: illegal move {move_uci}")
    board.push(move)
    try:
        nxt = node.get("n")
        if nxt is None:
            raise Failure(f"after {where} {move_uci}: chain ends without a terminal")
        return verify_help_node(board, nxt, path + [move_uci], goal) + 1
    finally:
        board.pop()


def verify_selfmate_node(board: chess.Board, node: dict, path: list[str],
                         goal: str = "selfmate") -> int:
    """Verify one selfmate attacker node. `board` has the ATTACKER to move.

    The shape differs from a directmate certificate. A leaf is `{"selfmated":
    true}` and carries no move, because the goal -- the attacker is mated -- is
    a statement about the side to move, reached when it is already his turn.
    Everything else is the same obligation: the attacker's move must be legal,
    and the defender node must list EXACTLY his legal replies, so a proof cannot
    omit a defence that lets him escape mating the attacker.
    """
    where = " ".join(path) if path else "<root>"

    want = "selfmated" if goal == "selfmate" else "selfstalemated"
    other = "selfstalemated" if goal == "selfmate" else "selfmated"
    if node.get(other):
        # The two are exact opposites at the terminal -- in check versus not --
        # so accepting the wrong one would certify the wrong problem.
        raise Failure(f"after {where}: leaf claims {other} inside a {goal} proof")
    if node.get(want):
        if goal == "selfmate":
            if not board.is_checkmate():
                raise Failure(f"after {where}: leaf claims the attacker is mated, "
                              f"but he is not")
        elif not board.is_stalemate():
            raise Failure(f"after {where}: leaf claims the attacker is stalemated, "
                          f"but he is not")
        return 0
    if node.get("mate") or node.get("stalemate"):
        # This is the bug that shipped: a selfmate search emitting a directmate
        # leaf because it had silently run the directmate code path.
        raise Failure(f"after {where}: directmate/stalemate leaf inside a "
                      f"{goal} proof")

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
        branches = node.get("d")
        if not branches:
            raise Failure(f"after {where} {move_uci}: no defender replies listed; "
                          f"a defender with no legal move has not mated anyone")
        listed = sorted(b.get("r", "") for b in branches)
        legal = sorted(m.uci() for m in board.legal_moves)
        if listed != legal:
            missing = sorted(set(legal) - set(listed))
            extra = sorted(set(listed) - set(legal))
            raise Failure(f"after {where} {move_uci}: defender replies do not match "
                          f"the legal moves (missing {missing}, extra {extra})")
        worst = 0
        for branch in branches:
            reply = branch["r"]
            board.push(chess.Move.from_uci(reply))
            try:
                worst = max(worst, verify_selfmate_node(board, branch["p"],
                                                        path + [move_uci, reply], goal))
            finally:
                board.pop()
        return worst + 1
    finally:
        board.pop()


def verify_pv(board: chess.Board, pv: list[str], claimed_depth: int,
              goal: str = "mate", quotas: dict[str, list[int]] | None = None) -> None:
    replay = board.copy()
    for token in pv:
        try:
            move = chess.Move.from_uci(token)
        except ValueError:
            raise Failure(f"unparseable pv move {token!r}")
        if move not in replay.legal_moves:
            raise Failure(f"illegal pv move {token}")
        replay.push(move)
    # Which side must be trapped, and in check or not, is the whole difference
    # between the three pairs of goals.
    if goal in ("stalemate", "selfstalemate", "helpstalemate"):
        if not replay.is_stalemate():
            raise Failure(f"pv does not end in stalemate ({goal})")
    elif not replay.is_checkmate():
        # Under a variant rule a won line may end on the final CHECK or the
        # final CAPTURE rather than on mate. Accepted only when the position
        # actually carried a quota and the line actually fills it -- so an
        # ordinary directmate proof that simply fails to mate is still rejected,
        # which is the case this branch must not become a hole for.
        if quotas is None:
            raise Failure(f"pv does not end in checkmate ({goal})")
        left = {rule: list(pair) for rule, pair in quotas.items()}
        walk = board.copy()
        for token in pv:
            mover = int(walk.turn == chess.BLACK)
            move = chess.Move.from_uci(token)
            capture = walk.is_capture(move)
            walk.push(move)
            spend_quotas(left, mover, walk, capture)
        if not any(pair[side] <= 0 for pair in left.values() for side in (0, 1)):
            raise Failure(f"pv ends in neither checkmate nor a filled quota "
                          f"({goal})")
    # Ply counts differ by goal and are load-bearing, not bookkeeping: a line of
    # the wrong length answers a different stipulation even when every move in
    # it is legal and the terminal is real.
    #   directmate     2N-1  the attacker's mating move is last
    #   self- goals    2N    the DEFENDER's move is last
    #   help- goals    2N    N moves by each side, cooperatively
    if goal == "mate":
        expected = 2 * claimed_depth - 1
    elif goal == "stalemate":
        expected = 2 * claimed_depth - 1
    else:
        expected = 2 * claimed_depth
    if len(pv) != expected:
        raise Failure(f"pv has {len(pv)} plies, expected {expected} for {goal} in "
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
                         "(the engine emits them only under --emit-proof). "
                         "Also fails on empty input, which otherwise passes "
                         "vacuously when the producer has crashed")
    ap.add_argument("--expect", type=int, default=None, metavar="N",
                    help="fail unless exactly N certificates verify. Use when "
                         "the caller knows the count: it is the only check "
                         "that catches a producer which died PART WAY through "
                         "and left a valid prefix")
    args = ap.parse_args()

    # utf-8-sig, not utf-8: a certificate saved on Windows may carry a BOM, and
    # the engine now tolerates one on its own input. Identical to utf-8 when no
    # BOM is present.
    stream = sys.stdin if args.input == "-" else open(args.input, encoding="utf-8-sig")

    checked = pv_only = skipped = seen = 0
    failures: list[str] = []

    for raw in stream:
        line = raw.strip()
        if not line or line.startswith("%"):
            continue
        seen += 1
        if "error input" in line:
            # The engine echoes an unparseable line back verbatim, so the echo
            # still carries whatever goal token the input had. That is not a
            # claim about a position and must not be verified as one -- doing so
            # reported the engine's own rejection as a verifier failure.
            skipped += 1
            continue
        fen = line.split(";", 1)[0].strip()
        # Every goal must be tried. Leaving one out does not make the checker
        # lenient, it makes it SILENT: an unrecognised token falls through to
        # "no mate reported" and the line is skipped, so a whole mode can report
        # success against a verifier that never looked at it. That is what
        # happened when hm/hsm were added to the engine and not to this list.
        #
        # The \b in each pattern is what keeps them disjoint: there is no word
        # boundary inside "hsm" or "ssm", so \bsm cannot match within them.
        goal = None
        for pattern, name in ((DM_RE, "mate"), (SM_RE, "stalemate"),
                              (SFM_RE, "selfmate"), (SSM_RE, "selfstalemate"),
                              (HM_RE, "helpmate"), (HSM_RE, "helpstalemate")):
            depth_match = pattern.search(line)
            if depth_match:
                goal = name
                break
        if goal is None:
            skipped += 1          # no solution reported: nothing to verify
            continue

        depth = int(depth_match.group(1))
        # python-chess parses four Forsyth fields and would reject a fifth, so
        # the allowance is taken off here and carried alongside.
        fen, quotas = split_variant_field(fen)
        try:
            board = chess.Board(fen + " 0 1")
        except ValueError as exc:
            failures.append(f"{fen[:40]}: unusable FEN ({exc})")
            continue

        try:
            pv_match = PV_RE.search(line)
            if not pv_match:
                raise Failure("solved position has no pv")
            verify_pv(board, pv_match.group(1).split(), depth, goal, quotas)

            proof_match = PROOF_RE.search(line)
            if proof_match:
                node = json.loads(proof_match.group(1))
                if goal in ("selfmate", "selfstalemate"):
                    proved = verify_selfmate_node(board.copy(), node, [], goal)
                elif goal in ("helpmate", "helpstalemate"):
                    # The cooperative chain is counted in plies; the stipulation
                    # is in moves by each side, so 2N plies is N.
                    plies = verify_help_node(board.copy(), node, [], goal)
                    if plies % 2:
                        raise Failure(f"cooperative certificate has {plies} plies, "
                                      f"which is not a whole number of moves")
                    proved = plies // 2
                else:
                    proved = verify_node(
                        board.copy(), node, [], goal,
                        {r: list(p) for r, p in quotas.items()} if quotas else None)
                if proved != depth:
                    raise Failure(f"certificate proves {goal} in {proved}, "
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
    # Zero of everything is not a pass. The documented workflow is a pipe, so
    # a producer that dies emits nothing, every counter above stays at 0, and
    # this returned success. Requiring proof means asserting there is
    # something here to prove.
    if args.require_proof and seen == 0:
        print("  FAILED: --require-proof, but the input held no engine output.")
        print("          An empty stream is a failed producer, not a clean")
        print("          verification.")
        return 1
    if args.expect is not None and checked != args.expect:
        print(f"  FAILED: expected {args.expect} certificate(s), "
              f"verified {checked}.")
        return 1
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
