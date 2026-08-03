#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Export composed problems from YACPDB into EPD, for tools/import_problems.py.

Why an external collection at all: a corpus built by solving positions contains
only positions the engine can already solve, so its solve rate is 100% by
construction and reach cannot be measured from it. Composed problems carry a
stipulation as ground truth, so the ones the engine FAILS are still known to be
sound -- and those are the ones a reach figure is made of.

The problems are other people's compositions. This writes an EPD file for local
use; nothing fetched here is redistributed by this project, exactly as with
tools/fetch_corpus.py.

Requests are paced deliberately. YACPDB is a community-run database, and a
corpus is not worth hammering someone's server for.

    python tools/export_yacpdb.py --stip "=" --depths 2 3 4 --out stalemates.epd
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
import time
import urllib.parse
import urllib.request

GATEWAY = "https://yacpdb.org/gateway/ql"
UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120 Safari/537.36")

# Problem notation uses S for the knight (Springer). Anything outside this set
# is a fairy piece and the entry is skipped rather than guessed at.
PIECES = {"K": "k", "Q": "q", "R": "r", "B": "b", "S": "n", "P": "p"}


def fetch(query: str, page: int, timeout: float = 60.0):
    url = f"{GATEWAY}?q={urllib.parse.quote(query)}&p={page}"
    req = urllib.request.Request(url, headers={"User-Agent": UA,
                                               "Referer": "https://yacpdb.org/"})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8", "replace"))


def to_fen(algebraic) -> str | None:
    """Convert {'white': ['Kb6', ...], 'black': [...]} into a board FEN field."""
    board = {}
    for colour, men in (("w", algebraic.get("white") or []),
                        ("b", algebraic.get("black") or [])):
        for man in men:
            if len(man) < 3:
                return None
            piece, square = man[0].upper(), man[1:].lower()
            if piece not in PIECES or len(square) != 2:
                return None            # fairy piece or odd notation: skip entry
            if not ("a" <= square[0] <= "h" and "1" <= square[1] <= "8"):
                return None
            sym = PIECES[piece]
            board[square] = sym.upper() if colour == "w" else sym
    if not board:
        return None
    rows = []
    for rank in range(8, 0, -1):
        row, gap = "", 0
        for f in "abcdefgh":
            sym = board.get(f"{f}{rank}")
            if sym is None:
                gap += 1
            else:
                if gap:
                    row += str(gap)
                    gap = 0
                row += sym
        if gap:
            row += str(gap)
        rows.append(row)
    return "/".join(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stip", default="=", help='stipulation prefix: "=" stalemate, "#" mate')
    ap.add_argument("--depths", type=int, nargs="+", required=True)
    ap.add_argument("--out", type=pathlib.Path, required=True)
    ap.add_argument("--max-pages", type=int, default=3,
                    help="pages per depth (100 problems per page)")
    ap.add_argument("--delay", type=float, default=1.5,
                    help="seconds between requests; be kind to a community server")
    args = ap.parse_args()

    lines, stats = [], {}
    for depth in args.depths:
        query = f'Stip("^{args.stip}{depth}$")'
        kept = skipped = 0
        for page in range(1, args.max_pages + 1):
            try:
                data = fetch(query, page)
            except Exception as exc:                       # noqa: BLE001
                print(f"  {query} page {page}: {exc}", file=sys.stderr)
                break
            if not data.get("success"):
                print(f"  {query}: {str(data.get('error'))[:120]}", file=sys.stderr)
                break
            entries = (data.get("result") or {}).get("entries") or []
            if not entries:
                break
            for e in entries:
                # Twins are several problems sharing a diagram; the stipulation
                # applies to the first only, so the rest would be mislabelled.
                if e.get("twins"):
                    skipped += 1
                    continue
                # `options` carries fairy conditions (Circe) and problem-type
                # modifiers (Duplex, SetPlay, Defence N). Exporting those as
                # orthodox problems made the engine refuse them correctly and
                # the import blame the database -- a mislabelling of ours
                # dressed up as someone else's error.
                if e.get("options") or e.get("conditions"):
                    skipped += 1
                    continue
                kw = [k.lower() for k in (e.get("keywords") or [])]
                sol = (e.get("solution") or "").strip().lower()
                # Fairy problems are not always flagged in `options`; some carry
                # only the keyword, and their solutions use rebirth notation
                # like [+wBc1]. Found by investigating a refuted problem that
                # turned out to be Circe.
                if "fairy" in kw or "[+" in sol:
                    skipped += 1
                    continue
                # An en-passant key cannot be expressed: the diagram gives no
                # ep square, and this exporter writes "w - -". The engine would
                # correctly refuse the composer's first move. Two problems were
                # refuted for exactly this reason before it was filtered.
                if "ep." in sol or "e.p." in sol:
                    skipped += 1
                    continue
                if "no solution" in kw or sol in ("none", "", "{no solution\n}"):
                    skipped += 1          # the database itself says it is unsound
                    continue
                alg = e.get("algebraic") or {}
                # Kingless diagrams are a real composition genre and a real part
                # of this database; they are simply not orthodox chess, so they
                # cannot be expressed as a FEN this engine will accept.
                if not any(m[:1].upper() == "K" for m in (alg.get("white") or []))                    or not any(m[:1].upper() == "K" for m in (alg.get("black") or [])):
                    skipped += 1
                    continue
                fen = to_fen(alg)
                if fen is None:
                    skipped += 1
                    continue
                lines.append(f"{fen} w - - {args.stip}{depth}; id \"yacpdb-{e.get('id')}\";")
                kept += 1
            time.sleep(args.delay)
        stats[depth] = (kept, skipped)
        print(f"  {args.stip}{depth}: kept {kept}, skipped {skipped}", flush=True)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    header = ("; Composed problems exported from YACPDB for local verification.\n"
              "; Not redistributed by this project -- see benchmarks/README.md.\n")
    args.out.write_text(header + "\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"\nwrote {len(lines)} problems to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
