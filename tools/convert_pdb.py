#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Convert a PDB LaTeX piece list into EPD for tools/import_problems.py.

PDB exports diagrams as `\\pieces{wKe5, wDf4, wTd3c4, ...}` in German notation:
w/s for weiss/schwarz, and K D T L S B for King, Queen (Dame), Rook (Turm),
Bishop (Laeufer), Knight (Springer), Pawn (Bauer). A single letter group can
carry several squares -- `wTd3c4` is two rooks.

Anything outside those six letters is a fairy piece and the entry is dropped
rather than guessed at. The stipulation `=N` is a *direct* stalemate; PDB's
STIP field is a substring match, so `ser-=N`, `h=N` and retro reconstructions
all match a naive query and must be excluded before this point.

Input lines: `<depth>|<pdb-id>|<piecelist>`
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

GERMAN = {"K": "k", "D": "q", "T": "r", "L": "b", "S": "n", "B": "p"}
GROUP = re.compile(r"([ws])([A-Z][A-Za-z]*)((?:[a-h][1-8])+)")


def to_fen(pieces: str):
    board, seen = {}, 0
    for colour, kind, squares in (m.groups() for m in GROUP.finditer(pieces)):
        if kind not in GERMAN:
            return None                     # fairy piece
        sym = GERMAN[kind]
        for i in range(0, len(squares), 2):
            sq = squares[i:i + 2]
            board[sq] = sym.upper() if colour == "w" else sym
            seen += 1
    # A comma-separated group that did not match means notation we do not model.
    if seen != len(re.findall(r"[a-h][1-8]", pieces)):
        return None
    if not any(v == "K" for v in board.values()) or not any(v == "k" for v in board.values()):
        return None                          # kingless: not orthodox chess
    rows = []
    for rank in range(8, 0, -1):
        row, gap = "", 0
        for f in "abcdefgh":
            sym = board.get(f"{f}{rank}")
            if sym is None:
                gap += 1
            else:
                if gap:
                    row, gap = row + str(gap), 0
                row += sym
        rows.append(row + (str(gap) if gap else ""))
    return "/".join(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", type=pathlib.Path, required=True)
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--min-depth", type=int, default=1)
    args = ap.parse_args()

    kept, dropped = [], 0
    for line in args.input.read_text(encoding="utf-8").splitlines():
        parts = line.strip().split("|")
        if len(parts) != 3:
            continue
        depth, pid, pieces = parts
        if int(depth) < args.min_depth:
            continue
        fen = to_fen(pieces)
        if fen is None:
            dropped += 1
            continue
        kept.append(f'{fen} w - - ={depth}; id "{pid}";')

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("; PDB direct-stalemate problems, for local verification only.\n"
                        + "\n".join(kept) + "\n", encoding="utf-8", newline="\n")
    print(f"converted {len(kept)}, dropped {dropped} (fairy pieces or kingless)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
