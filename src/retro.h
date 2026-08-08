// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// retro.h -- Retrograde move generation: the predecessors of a position.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_RETRO_H_INCLUDED
#define MATEPROVER_RETRO_H_INCLUDED

namespace mateprover {

// set_square maintains the occupancy planes but not the king squares, because
// no forward path ever moves a king by rewriting a square. Retraction does.
inline void refresh_kings(Board& b) {
    b.king_sq[WHITE] = -1;
    b.king_sq[BLACK] = -1;
    for (int sq = 0; sq < 64; ++sq) {
        if (b.sq[static_cast<std::size_t>(sq)] == 'K') b.king_sq[WHITE] = sq;
        if (b.sq[static_cast<std::size_t>(sq)] == 'k') b.king_sq[BLACK] = sq;
    }
}

// Two positions are the same position when everything a search can observe
// agrees: the pieces, the side to move, the castling rights and the en-passant
// square. This is the relation the round-trip test is written against.
inline bool same_position(const Board& a, const Board& b) {
    return a.packed == b.packed && a.stm == b.stm &&
           (a.castling & 0x0fu) == (b.castling & 0x0fu) && a.ep == b.ep;
}

// The predecessors of `b`: every position P and legal move m with
// make_move(P, m) == b.
//
// GENERATE A SUPERSET, THEN VERIFY. Reverse move generation is the fiddly half
// of retrograde analysis -- uncaptures, unpromotions, un-castling,
// un-en-passant, and the fact that a sliding piece's origin depends on an
// occupancy that does not exist yet. Getting all of that exactly right by
// construction is where retrograde code goes wrong, and it goes wrong silently.
//
// So this proposes candidates cheaply and then CONFIRMS each one by playing the
// forward move and comparing. A candidate that does not reproduce `b` exactly is
// discarded. That makes soundness a property of the verification rather than of
// the geometry: the generator may over-propose without harm, and can only fail
// by under-proposing, which is what the round-trip test measures.
//
// Castling rights are not reconstructed. A predecessor is emitted with the same
// rights `b` carries, which is correct whenever the retracted move did not
// forfeit any -- and a move that DID forfeit rights cannot be verified by the
// comparison above, so such predecessors are simply not produced. That is a
// deliberate incompleteness: it never invents a position, and helpmate diagrams
// with castling rights are rare enough that the cost is small and the
// alternative is a second source of silent error. See section 66.
inline void generate_unmoves(const Board& b, std::vector<Board>& out) {
    out.clear();
    const Color mover = b.stm == WHITE ? BLACK : WHITE;   // whoever just moved
    const Color waiting = b.stm;

    // What a retracted capture may restore. Never a king, and never a pawn on
    // its own back rank.
    static const char white_units[] = {'Q', 'R', 'B', 'N', 'P'};
    static const char black_units[] = {'q', 'r', 'b', 'n', 'p'};

    auto try_candidate = [&](const Board& pred, const Move& m) {
        if (pred.king_sq[WHITE] < 0 || pred.king_sq[BLACK] < 0) return;
        if (pred.stm != mover) return;
        // The side not to move may not be in check in a legal position.
        if (in_check(pred, waiting)) return;
        if (in_check(pred, mover)) {
            // Legal, but only if the move being retracted escapes it; the
            // comparison below decides that.
        }
        const Board after = make_move(pred, m);
        if (same_position(after, b)) {
            out.push_back(pred);
        }
    };

    for (int to = 0; to < 64; ++to) {
        const char piece = b.sq[static_cast<std::size_t>(to)];
        if (type_of(piece) == PT_NONE) continue;
        if ((is_white_piece(piece) ? WHITE : BLACK) != mover) continue;

        for (int from = 0; from < 64; ++from) {
            if (from == to) continue;
            if (type_of(b.sq[static_cast<std::size_t>(from)]) != PT_NONE) continue;

            // The piece that stood on `from` before the move. Normally the same
            // piece; for an unpromotion it was a pawn.
            std::vector<std::pair<char, char>> origins;   // (piece on from, promo char)
            origins.emplace_back(piece, 0);
            const int to_rank = rank_of(to);
            const bool promo_rank = (mover == WHITE && to_rank == 7) ||
                                    (mover == BLACK && to_rank == 0);
            if (promo_rank && type_of(piece) != PT_PAWN && type_of(piece) != PT_KING) {
                origins.emplace_back(mover == WHITE ? 'P' : 'p',
                                     static_cast<char>(std::tolower(
                                         static_cast<unsigned char>(piece))));
            }

            for (const auto& origin : origins) {
                // Case 1: the move captured nothing.
                {
                    Board pred = b;
                    set_square(pred, to, '.');
                    set_square(pred, from, origin.first);
                    pred.stm = mover;
                    pred.ep = -1;
                    refresh_kings(pred);
                    Move m; m.from = from; m.to = to; m.promo = origin.second;
                    try_candidate(pred, m);

                    // A double pawn push leaves an en-passant square behind, so
                    // the predecessor must not have had one and `b` must.
                    if (b.ep >= 0) {
                        Board ep_pred = pred;
                        ep_pred.ep = -1;
                        try_candidate(ep_pred, m);
                    }
                }

                // Case 2: the move captured something on `to`.
                const char* units = (waiting == WHITE) ? white_units : black_units;
                for (int u = 0; u < 5; ++u) {
                    const char captured = units[u];
                    const int cap_rank = rank_of(to);
                    if (type_of(captured) == PT_PAWN && (cap_rank == 0 || cap_rank == 7)) {
                        continue;               // no pawn stands on a back rank
                    }
                    Board pred = b;
                    set_square(pred, to, captured);
                    set_square(pred, from, origin.first);
                    pred.stm = mover;
                    pred.ep = -1;
                    refresh_kings(pred);
                    Move m; m.from = from; m.to = to; m.promo = origin.second;
                    try_candidate(pred, m);
                }
            }
        }
    }
}

}  // namespace mateprover

#endif  // MATEPROVER_RETRO_H_INCLUDED
