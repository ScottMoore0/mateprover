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

// An en-passant square that no pawn can legally use is not observable: no
// sequence of moves from here on can tell the two positions apart. Positions
// must therefore be compared, and predecessors emitted, on the LIVE ep square
// only.
//
// This is not a cosmetic normalisation. make_move records an ep square after
// every double push, whether or not a capture exists; the FEN convention in
// wide use (and the one the corpora are written in) records it only when the
// capture is legal. Comparing the raw field makes the two disagree, and the
// disagreement lands exactly on the predecessor "black just pushed two squares"
// -- which is most of the pawn endgame.
inline bool ep_is_live(const Board& b) {
    if (b.ep < 0) return false;
    std::vector<Move> pseudo;
    gen_pseudo(b, pseudo);
    for (const Move& m : pseudo) {
        if (m.ep && move_is_legal(b, m)) return true;
    }
    return false;
}

inline int effective_ep(const Board& b) {
    return ep_is_live(b) ? b.ep : -1;
}

// Two positions are the same position when everything a search can observe
// agrees: the pieces, the side to move, the castling rights and a usable
// en-passant square.
inline bool same_position(const Board& a, const Board& b) {
    return a.packed == b.packed && a.stm == b.stm &&
           (a.castling & 0x0fu) == (b.castling & 0x0fu) &&
           effective_ep(a) == effective_ep(b);
}

// The rights make_move would revoke when `m` is played on `pred`. Mirrors the
// forward rule exactly, including that rights go by SQUARE and never by the
// type of the captured piece.
inline unsigned rights_cleared_by(const Board& pred, const Move& m) {
    unsigned cleared = 0;
    const char p = pred.sq[static_cast<std::size_t>(m.from)];
    if (p == 'K') cleared |= 3u;
    if (p == 'k') cleared |= 12u;
    if (m.from == square_of(0, 0) || m.to == square_of(0, 0)) cleared |= 2u;
    if (m.from == square_of(7, 0) || m.to == square_of(7, 0)) cleared |= 1u;
    if (m.from == square_of(0, 7) || m.to == square_of(0, 7)) cleared |= 8u;
    if (m.from == square_of(7, 7) || m.to == square_of(7, 7)) cleared |= 4u;
    return cleared;
}

// A castling right only makes sense with the king and the rook still at home.
// Checked before a right is RESTORED, never against rights the position already
// carries: a caller-supplied FEN may claim a phantom right, and retraction is
// not the place to start rejecting the user's input.
inline bool castling_right_plausible(const Board& b, unsigned bit) {
    switch (bit) {
        case 1u: return b.sq[static_cast<std::size_t>(square_of(4, 0))] == 'K' &&
                        b.sq[static_cast<std::size_t>(square_of(7, 0))] == 'R';
        case 2u: return b.sq[static_cast<std::size_t>(square_of(4, 0))] == 'K' &&
                        b.sq[static_cast<std::size_t>(square_of(0, 0))] == 'R';
        case 4u: return b.sq[static_cast<std::size_t>(square_of(4, 7))] == 'k' &&
                        b.sq[static_cast<std::size_t>(square_of(7, 7))] == 'r';
        case 8u: return b.sq[static_cast<std::size_t>(square_of(4, 7))] == 'k' &&
                        b.sq[static_cast<std::size_t>(square_of(0, 7))] == 'r';
        default: return false;
    }
}

// Could this piece move from `from` to `to` at all, ignoring every other piece?
// A deliberately permissive filter -- it exists only to keep the candidate loop
// from calling the move generator sixty-four times per piece. Over-admitting
// costs time; under-admitting would cost completeness, so every case here is a
// superset of the real rule. Castling is not covered and is retracted
// separately.
inline bool geometry_allows(char piece, int from, int to, Color mover) {
    const int df = std::abs(file_of(to) - file_of(from));
    const int dr = rank_of(to) - rank_of(from);
    const int adr = std::abs(dr);
    switch (type_of(piece)) {
        case PT_KNIGHT: return (df == 1 && adr == 2) || (df == 2 && adr == 1);
        case PT_KING:   return df <= 1 && adr <= 1;
        case PT_BISHOP: return df == adr;
        case PT_ROOK:   return df == 0 || adr == 0;
        case PT_QUEEN:  return df == adr || df == 0 || adr == 0;
        case PT_PAWN: {
            const int fwd = (mover == WHITE) ? dr : -dr;
            if (df == 0) return fwd == 1 || fwd == 2;
            return df == 1 && fwd == 1;
        }
        default: return false;
    }
}

inline bool board_less(const Board& a, const Board& b) {
    if (a.packed != b.packed) return a.packed < b.packed;
    if (a.stm != b.stm) return a.stm < b.stm;
    const unsigned ca = a.castling & 0x0fu, cb = b.castling & 0x0fu;
    if (ca != cb) return ca < cb;
    return a.ep < b.ep;
}

// The predecessors of `b`: every position P and legal move m with
// make_move(P, m) == b.
//
// GENERATE A SUPERSET, THEN VERIFY. Reverse move generation is the fiddly half
// of retrograde analysis -- uncaptures, unpromotions, un-castling,
// un-en-passant, restored castling rights, and the fact that a sliding piece's
// origin depends on an occupancy that does not exist yet. Getting all of that
// right by construction is where retrograde code goes wrong, and it goes wrong
// silently.
//
// So this proposes candidates cheaply and then CONFIRMS each one: the retracted
// move must appear in the candidate's own LEGAL move list, and playing it must
// reproduce `b` exactly. Both halves are load-bearing. Replaying the move
// without first finding it among the legal moves proves nothing at all --
// make_move will happily carry a knight down a file, and the result still
// compares equal to `b`, so every empty square becomes a "predecessor".
//
// THE CONTRACT IS ONE PLY, and it is exact in both directions:
//
//   sound        -- every emitted P is a legal position with a legal move to b
//   complete     -- every P with a legal move to b is emitted
//
// What it does NOT decide is whether P is reachable from the initial array.
// Material bounds are enforced because they are free (see below), but a
// position can respect them and still be retro-impossible: three pieces giving
// check at once, or two checkers on one line through the king, cannot be
// produced by any single move, and nothing here rejects them. Measured on the
// mixed random-walk and helpmate corpus that is 0.7% of what this emits.
//
// That residue costs a backward search time, not correctness. A bidirectional
// search meets when a backward node EQUALS a forward node, and forward nodes
// are reachable by construction, so an unreachable backward node simply never
// matches. A caller that needs retro-legality in its own right must add it.
inline void generate_unmoves(const Board& b, std::vector<Board>& out) {
    out.clear();
    const Color mover = b.stm == WHITE ? BLACK : WHITE;   // whoever just moved
    const Color waiting = b.stm;
    const int b_ep = effective_ep(b);   // ep_is_live runs a move generator

    // An uncapture hands a man back, and an unpromotion hands a pawn back.
    // Neither may take a side past sixteen men or eight pawns -- the one thing
    // about a predecessor that can be settled without knowing how the game got
    // here. Counted from `b` and applied only where a unit is ADDED, so a
    // caller-supplied FEN that is already over strength is retracted, not
    // rejected: this is a move generator, not a validator.
    int men[2] = {0, 0};
    int pawns[2] = {0, 0};
    for (int sq = 0; sq < 64; ++sq) {
        const char p = b.sq[static_cast<std::size_t>(sq)];
        if (type_of(p) == PT_NONE) continue;
        const Color c = is_white_piece(p) ? WHITE : BLACK;
        ++men[c];
        if (type_of(p) == PT_PAWN) ++pawns[c];
    }
    const bool can_uncapture = men[waiting] < 16;
    const bool can_uncapture_pawn = can_uncapture && pawns[waiting] < 8;
    const bool can_unpromote = pawns[mover] < 8;

    // What a retracted capture may restore. Never a king, and never a pawn on
    // its own back rank.
    static const char white_units[] = {'Q', 'R', 'B', 'N', 'P'};
    static const char black_units[] = {'q', 'r', 'b', 'n', 'p'};

    // Does `pred` reach `b` by exactly the move `want`, legally?
    auto verified = [&](const Board& pred, const Move& want) {
        if (pred.king_sq[WHITE] < 0 || pred.king_sq[BLACK] < 0) return false;
        if (pred.stm != mover) return false;
        // The side not to move may not be in check in a legal position. This
        // also rules out adjacent kings.
        if (in_check(pred, waiting)) return false;
        std::vector<Move> pseudo;
        gen_pseudo(pred, pseudo);
        for (const Move& m : pseudo) {
            if (m.from != want.from || m.to != want.to) continue;
            if (m.castle != want.castle || m.ep != want.ep) continue;
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(m.promo)));
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(want.promo)));
            if (a != c) continue;
            if (!move_is_legal(pred, m)) return false;
            const Board after = make_move(pred, m);
            return after.packed == b.packed && after.stm == b.stm &&
                   (after.castling & 0x0fu) == (b.castling & 0x0fu) &&
                   effective_ep(after) == b_ep;
        }
        return false;
    };

    // The ep squares the WAITING side could have left behind before `pred`.
    // Only live ones are collected: a dead ep square is the same position as
    // none, and emitting it would be a duplicate under same_position.
    auto live_ep_squares = [&](const Board& pred, std::vector<int>& sqs) {
        sqs.clear();
        const int ep_rank = (waiting == WHITE) ? 2 : 5;
        const int push_from = (waiting == WHITE) ? -8 : 8;   // relative to ep
        const int push_to = -push_from;
        const char pawn = (waiting == WHITE) ? 'P' : 'p';
        for (int f = 0; f < 8; ++f) {
            const int e = square_of(f, ep_rank);
            if (pred.sq[static_cast<std::size_t>(e)] != '.') continue;
            if (pred.sq[static_cast<std::size_t>(e + push_from)] != '.') continue;
            if (pred.sq[static_cast<std::size_t>(e + push_to)] != pawn) continue;
            Board probe = pred;
            probe.ep = e;
            if (ep_is_live(probe)) sqs.push_back(e);
        }
    };

    std::vector<int> eps;

    // Emit `pred` and every variant of it that also reaches `b` by `want`:
    // each plausible restoration of a right this move would have forfeited,
    // and each ep square the waiting side could have left unused.
    auto emit = [&](const Board& pred, const Move& want) {
        const unsigned restorable = rights_cleared_by(pred, want) & ~b.castling & 0x0fu;
        unsigned bits[4];
        int nbits = 0;
        for (unsigned bit : {1u, 2u, 4u, 8u}) {
            if (restorable & bit) bits[nbits++] = bit;
        }
        for (int mask = 0; mask < (1 << nbits); ++mask) {
            Board cand = pred;
            bool ok = true;
            for (int i = 0; i < nbits; ++i) {
                if (!(mask & (1 << i))) continue;
                if (!castling_right_plausible(cand, bits[i])) { ok = false; break; }
                cand.castling |= bits[i];
            }
            if (!ok) continue;
            if (!verified(cand, want)) continue;
            out.push_back(cand);
            if (want.ep) continue;      // an ep capture pins pred.ep to want.to
            live_ep_squares(cand, eps);
            for (int e : eps) {
                Board with_ep = cand;
                with_ep.ep = e;
                if (verified(with_ep, want)) out.push_back(with_ep);
            }
        }
    };

    // ---- Ordinary moves: one piece from an empty square to where it stands,
    // with or without an uncapture, with or without an unpromotion.
    for (int to = 0; to < 64; ++to) {
        const char piece = b.sq[static_cast<std::size_t>(to)];
        if (type_of(piece) == PT_NONE) continue;
        if ((is_white_piece(piece) ? WHITE : BLACK) != mover) continue;

        for (int from = 0; from < 64; ++from) {
            if (from == to) continue;
            if (type_of(b.sq[static_cast<std::size_t>(from)]) != PT_NONE) continue;

            // The piece that stood on `from` before the move. Normally the same
            // piece; for an unpromotion it was a pawn.
            std::pair<char, char> origins[2];   // (piece on from, promo char)
            int norigins = 0;
            origins[norigins++] = {piece, 0};
            const int to_rank = rank_of(to);
            const bool promo_rank = (mover == WHITE && to_rank == 7) ||
                                    (mover == BLACK && to_rank == 0);
            if (promo_rank && can_unpromote &&
                type_of(piece) != PT_PAWN && type_of(piece) != PT_KING) {
                origins[norigins++] = {mover == WHITE ? 'P' : 'p',
                                       static_cast<char>(std::tolower(
                                           static_cast<unsigned char>(piece)))};
            }

            for (int oi = 0; oi < norigins; ++oi) {
                const std::pair<char, char>& origin = origins[oi];
                if (!geometry_allows(origin.first, from, to, mover)) continue;
                // A pawn never stood on a back rank -- neither the one this
                // move restores nor the one it retracts.
                if (type_of(origin.first) == PT_PAWN &&
                    (rank_of(from) == 0 || rank_of(from) == 7)) {
                    continue;
                }

                Move m;
                m.from = from;
                m.to = to;
                m.promo = origin.second;

                // Case 1: the move captured nothing.
                {
                    Board pred = b;
                    set_square(pred, to, '.');
                    set_square(pred, from, origin.first);
                    pred.stm = mover;
                    pred.ep = -1;
                    refresh_kings(pred);
                    emit(pred, m);
                }

                // Case 2: the move captured something on `to`.
                const char* units = (waiting == WHITE) ? white_units : black_units;
                for (int u = 0; can_uncapture && u < 5; ++u) {
                    const char captured = units[u];
                    if (type_of(captured) == PT_PAWN) {
                        if (!can_uncapture_pawn) continue;
                        if (to_rank == 0 || to_rank == 7) {
                            continue;           // no pawn stands on a back rank
                        }
                    }
                    Board pred = b;
                    set_square(pred, to, captured);
                    set_square(pred, from, origin.first);
                    pred.stm = mover;
                    pred.ep = -1;
                    refresh_kings(pred);
                    emit(pred, m);
                }
            }
        }
    }

    // ---- Retracting an en-passant capture. The restored pawn does not go back
    // on `to`, so the loop above cannot express this move at all.
    {
        const char my_pawn = (mover == WHITE) ? 'P' : 'p';
        const char their_pawn = (mover == WHITE) ? 'p' : 'P';
        const int land_rank = (mover == WHITE) ? 5 : 2;
        const int behind = (mover == WHITE) ? -8 : 8;    // captured pawn, from `to`
        for (int f = 0; can_uncapture_pawn && f < 8; ++f) {
            const int to = square_of(f, land_rank);
            if (b.sq[static_cast<std::size_t>(to)] != my_pawn) continue;
            if (b.sq[static_cast<std::size_t>(to + behind)] != '.') continue;
            if (b.sq[static_cast<std::size_t>(to - behind)] != '.') continue;  // its origin
            for (int df : {-1, 1}) {
                const int ff = f + df;
                if (ff < 0 || ff > 7) continue;
                const int from = square_of(ff, rank_of(to + behind));
                if (b.sq[static_cast<std::size_t>(from)] != '.') continue;
                Board pred = b;
                set_square(pred, to, '.');
                set_square(pred, from, my_pawn);
                set_square(pred, to + behind, their_pawn);
                pred.stm = mover;
                pred.ep = to;
                refresh_kings(pred);
                Move m;
                m.from = from;
                m.to = to;
                m.ep = true;
                emit(pred, m);
            }
        }
    }

    // ---- Un-castling. Two pieces move, so this too is outside the loop above,
    // and the right that made it possible has been cleared from `b` and must be
    // put back before the move generator will admit it.
    {
        struct Castle {
            Color side;
            int king_to, rook_to, king_from, rook_from;
            unsigned right;
        };
        static const Castle table[4] = {
            {WHITE, square_of(6, 0), square_of(5, 0), square_of(4, 0), square_of(7, 0), 1u},
            {WHITE, square_of(2, 0), square_of(3, 0), square_of(4, 0), square_of(0, 0), 2u},
            {BLACK, square_of(6, 7), square_of(5, 7), square_of(4, 7), square_of(7, 7), 4u},
            {BLACK, square_of(2, 7), square_of(3, 7), square_of(4, 7), square_of(0, 7), 8u},
        };
        const char king = (mover == WHITE) ? 'K' : 'k';
        const char rook = (mover == WHITE) ? 'R' : 'r';
        for (const Castle& c : table) {
            if (c.side != mover) continue;
            if (b.sq[static_cast<std::size_t>(c.king_to)] != king) continue;
            if (b.sq[static_cast<std::size_t>(c.rook_to)] != rook) continue;
            if (b.sq[static_cast<std::size_t>(c.king_from)] != '.') continue;
            if (b.sq[static_cast<std::size_t>(c.rook_from)] != '.') continue;
            Board pred = b;
            set_square(pred, c.king_to, '.');
            set_square(pred, c.rook_to, '.');
            set_square(pred, c.king_from, king);
            set_square(pred, c.rook_from, rook);
            pred.stm = mover;
            pred.ep = -1;
            pred.castling |= c.right;
            refresh_kings(pred);
            Move m;
            m.from = c.king_from;
            m.to = c.king_to;
            m.castle = true;
            emit(pred, m);
        }
    }

    // A predecessor can be proposed by more than one route -- a rights subset
    // that adds nothing, an unpromotion that lands on the same board. Distinct
    // positions, not distinct derivations, are what a backward frontier wants.
    std::sort(out.begin(), out.end(), board_less);
    out.erase(std::unique(out.begin(), out.end(),
                          [](const Board& x, const Board& y) {
                              return !board_less(x, y) && !board_less(y, x);
                          }),
              out.end());
}

}  // namespace mateprover

#endif  // MATEPROVER_RETRO_H_INCLUDED
