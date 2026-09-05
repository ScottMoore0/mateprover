// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (c) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// movegen.h -- Pseudo-legal generation, make_move, legality, and occupancy-plane queries.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_MOVEGEN_H_INCLUDED
#define MATEPROVER_MOVEGEN_H_INCLUDED

namespace mateprover {

template <typename MoveSink>
void gen_pseudo(const Board& b, MoveSink& moves) {
    Color us = b.stm;
    for (int from = 0; from < 64; ++from) {
        char p = b.sq[from];
        if (!is_piece_color(p, us)) continue;
        char lp = piece_lower(p);
        int f = file_of(from);
        int r = rank_of(from);

        if (lp == 'p') {
            int dir = us == WHITE ? 1 : -1;
            int start_rank = us == WHITE ? 1 : 6;
            int promo_rank = us == WHITE ? 7 : 0;
            int one_r = r + dir;
            if (on_board(f, one_r)) {
                int one = square_of(f, one_r);
                if (b.sq[one] == '.') {
                    if (one_r == promo_rank) {
                        for (char pr : {'q', 'r', 'b', 'n'}) add_move(moves, from, one, pr);
                    } else {
                        add_move(moves, from, one);
                        int two_r = r + 2 * dir;
                        if (r == start_rank && on_board(f, two_r)) {
                            int two = square_of(f, two_r);
                            if (b.sq[two] == '.') add_move(moves, from, two);
                        }
                    }
                }
            }
            for (int df : {-1, 1}) {
                int cf = f + df;
                int cr = r + dir;
                if (!on_board(cf, cr)) continue;
                int to = square_of(cf, cr);
                if ((is_enemy_piece(b.sq[to], us) && !is_king_piece(b.sq[to])) || to == b.ep) {
                    bool is_ep = to == b.ep && b.sq[to] == '.';
                    if (cr == promo_rank) {
                        for (char pr : {'q', 'r', 'b', 'n'}) add_move(moves, from, to, pr, false, is_ep);
                    } else {
                        add_move(moves, from, to, 0, false, is_ep);
                    }
                }
            }
        } else if (lp == 'n') {
            const SquareList& targets = knight_table()[from];
            for (int i = 0; i < targets.count; ++i) {
                int to = targets.sq[i];
                if (!is_piece_color(b.sq[to], us) && !is_king_piece(b.sq[to])) add_move(moves, from, to);
            }
        } else if (lp == 'b' || lp == 'r' || lp == 'q') {
            int first = lp == 'b' ? 4 : 0;
            int last = lp == 'r' ? 4 : 8;
            const auto& rays = ray_table();
            for (int i = first; i < last; ++i) {
                const SquareList& ray = rays[i][from];
                for (int j = 0; j < ray.count; ++j) {
                    int to = ray.sq[j];
                    if (is_piece_color(b.sq[to], us)) break;
                    if (is_king_piece(b.sq[to])) break;
                    add_move(moves, from, to);
                    if (b.sq[to] != '.') break;
                }
            }
        } else if (lp == 'k') {
            const SquareList& targets = king_table()[from];
            for (int i = 0; i < targets.count; ++i) {
                int to = targets.sq[i];
                if (!is_piece_color(b.sq[to], us) && !is_king_piece(b.sq[to])) add_move(moves, from, to);
            }
            // The castling rook must actually be on its corner.
            //
            // A FEN can claim a right whose rook is absent, and nothing else
            // here would notice: make_move writes a rook onto f1/d1
            // unconditionally, so castling under a phantom right would
            // materialise a piece from nothing. Standard perft positions all
            // have consistent rights, which is why this survived those gates.
            if (us == WHITE && from == square_of(4, 0) && !in_check(b, WHITE)) {
                if ((b.castling & 1) && b.sq[square_of(7, 0)] == 'R' &&
                    b.sq[square_of(5, 0)] == '.' && b.sq[square_of(6, 0)] == '.' &&
                    !is_attacked(b, square_of(5, 0), BLACK) && !is_attacked(b, square_of(6, 0), BLACK)) {
                    add_move(moves, from, square_of(6, 0), 0, true);
                }
                if ((b.castling & 2) && b.sq[square_of(0, 0)] == 'R' &&
                    b.sq[square_of(3, 0)] == '.' && b.sq[square_of(2, 0)] == '.' && b.sq[square_of(1, 0)] == '.' &&
                    !is_attacked(b, square_of(3, 0), BLACK) && !is_attacked(b, square_of(2, 0), BLACK)) {
                    add_move(moves, from, square_of(2, 0), 0, true);
                }
            }
            if (us == BLACK && from == square_of(4, 7) && !in_check(b, BLACK)) {
                if ((b.castling & 4) && b.sq[square_of(7, 7)] == 'r' &&
                    b.sq[square_of(5, 7)] == '.' && b.sq[square_of(6, 7)] == '.' &&
                    !is_attacked(b, square_of(5, 7), WHITE) && !is_attacked(b, square_of(6, 7), WHITE)) {
                    add_move(moves, from, square_of(6, 7), 0, true);
                }
                if ((b.castling & 8) && b.sq[square_of(0, 7)] == 'r' &&
                    b.sq[square_of(3, 7)] == '.' && b.sq[square_of(2, 7)] == '.' && b.sq[square_of(1, 7)] == '.' &&
                    !is_attacked(b, square_of(3, 7), WHITE) && !is_attacked(b, square_of(2, 7), WHITE)) {
                    add_move(moves, from, square_of(2, 7), 0, true);
                }
            }
        }
    }
}

Board make_move(Board b, const Move& m) {
    char p = b.sq[m.from];
    // Latched HERE, before the destination is overwritten below. Reading it any
    // later reports every capture as a quiet move, silently and everywhere.
    const bool captured = (b.sq[m.to] != '.') || m.ep;
    set_square(b, m.from, '.');

    if (m.ep) {
        int cap_sq = m.to + (b.stm == WHITE ? -8 : 8);
        set_square(b, cap_sq, '.');
    }

    char placed = p;
    if (m.promo) {
        placed = b.stm == WHITE ? static_cast<char>(std::toupper(static_cast<unsigned char>(m.promo))) : m.promo;
    }
    set_square(b, m.to, placed);
    if (p == 'K') {
        b.king_sq[WHITE] = m.to;
    } else if (p == 'k') {
        b.king_sq[BLACK] = m.to;
    }

    if (m.castle) {
        if (p == 'K' && m.to == square_of(6, 0)) {
            set_square(b, square_of(5, 0), 'R');
            set_square(b, square_of(7, 0), '.');
        } else if (p == 'K' && m.to == square_of(2, 0)) {
            set_square(b, square_of(3, 0), 'R');
            set_square(b, square_of(0, 0), '.');
        } else if (p == 'k' && m.to == square_of(6, 7)) {
            set_square(b, square_of(5, 7), 'r');
            set_square(b, square_of(7, 7), '.');
        } else if (p == 'k' && m.to == square_of(2, 7)) {
            set_square(b, square_of(3, 7), 'r');
            set_square(b, square_of(0, 7), '.');
        }
    }

    if (p == 'K') b.castling &= ~3u;
    if (p == 'k') b.castling &= ~12u;
    // Castling rights are revoked by SQUARE, never by captured piece type.
    //
    // Keying off the piece type is wrong once promotion exists: capturing a
    // promoted rook anywhere on the board would strip the owner's rights even
    // though both original corner rooks are untouched. The square tests below
    // are already complete -- a move from a corner covers the rook leaving, and
    // a move to a corner covers the rook being captured there (if some other
    // piece occupies that corner, the right was necessarily already gone).
    if (m.from == square_of(0, 0) || m.to == square_of(0, 0)) b.castling &= ~2u;
    if (m.from == square_of(7, 0) || m.to == square_of(7, 0)) b.castling &= ~1u;
    if (m.from == square_of(0, 7) || m.to == square_of(0, 7)) b.castling &= ~8u;
    if (m.from == square_of(7, 7) || m.to == square_of(7, 7)) b.castling &= ~4u;

    b.ep = -1;
    if (piece_lower(p) == 'p' && std::abs(m.to - m.from) == 16) {
        b.ep = (m.from + m.to) / 2;
    }
    // Variant quotas: the event spends one, and spending the last ends the game
    // in the mover's favour. Each guard is a comparison against a constant that
    // standard play never passes, which is what lets this sit in the hottest
    // function in the engine -- in particular the check rule's attack query is
    // never issued under standard rules.
    //
    // The two rules cost very differently. A check is a property of the RESULTING
    // POSITION and needs an attack query; a capture is a property of the MOVE and
    // was already known before the move was made.
    std::uint8_t& checks = b.quota[quota_index(b.stm, VR_CHECK)];
    if (checks != kNoQuota && checks > 0 && in_check(b, other(b.stm))) {
        --checks;
    }
    std::uint8_t& captures = b.quota[quota_index(b.stm, VR_CAPTURE)];
    if (captures != kNoQuota && captures > 0 && captured) {
        --captures;
    }
    b.stm = other(b.stm);
    return b;
}

// Is this move legal -- that is, does it leave the mover's own king unattacked?
//
// Defined below, once the occupancy planes it needs are in scope. Declared here
// because the legality loops come first in the file.
inline bool move_is_legal(const Board& b, const Move& m);

std::vector<Move> legal_moves_vector(const Board& b, bool move_reserve, std::size_t move_reserve_capacity) {
    std::vector<Move> pseudo;
    if (move_reserve) {
        pseudo.reserve(move_reserve_capacity);
    }
    gen_pseudo(b, pseudo);
    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    for (const Move& m : pseudo) {
        if (move_is_legal(b, m)) {
            legal.push_back(m);
        }
    }
    return legal;
}

std::vector<Move> legal_moves(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    if (static_pseudo) {
        MoveList pseudo;
        gen_pseudo(b, pseudo);
        if (!pseudo.overflow) {
            std::vector<Move> legal;
            legal.reserve(pseudo.size());
            for (const Move& m : pseudo) {
                if (move_is_legal(b, m)) {
                    legal.push_back(m);
                }
            }
            return legal;
        }
    }
    return legal_moves_vector(b, move_reserve, move_reserve_capacity);
}

bool has_legal_move_vector(const Board& b, bool move_reserve, std::size_t move_reserve_capacity) {
    std::vector<Move> pseudo;
    if (move_reserve) {
        pseudo.reserve(move_reserve_capacity);
    }
    gen_pseudo(b, pseudo);
    for (const Move& m : pseudo) {
        if (move_is_legal(b, m)) {
            return true;
        }
    }
    return false;
}

bool has_legal_move(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    if (static_pseudo) {
        MoveList pseudo;
        gen_pseudo(b, pseudo);
        if (!pseudo.overflow) {
            for (const Move& m : pseudo) {
                if (move_is_legal(b, m)) {
                    return true;
                }
            }
            return false;
        }
    }
    return has_legal_move_vector(b, move_reserve, move_reserve_capacity);
}

bool is_checkmate(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    return in_check(b, b.stm) && !has_legal_move(b, move_reserve, move_reserve_capacity, static_pseudo);
}

// Stalemate: the side to move has no legal move and is NOT in check. The second
// clause is what makes this disjoint from checkmate rather than a superset of
// it, and it is why a stalemate goal cannot reuse a mate search's verdicts.
bool is_stalemate(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    return !in_check(b, b.stm) && !has_legal_move(b, move_reserve, move_reserve_capacity, static_pseudo);
}

// "Is the goal reached in the position after an attacker move?"
//
// Selfmate answers no, always, and that is not a stub. Its goal is that the
// ATTACKER is mated, which cannot be true in a position where the defender is
// to move. Falling through to is_checkmate() here asked whether the DEFENDER is
// mated -- the opposite of what a selfmate wants, and worth a +1000000 ordering
// bonus under --score-mates. Dormant, since that flag is off by default, but it
// would have scored exactly the wrong moves first.
// Has the mover just won outright, and under which rule? Returns the rule index
// or -1.
//
//
// `is_goal` answers the POSITION question -- is the side to move mated, or
// stalemated, as the stipulation demands. Under x-check there is a second way to
// end the game, and it is a win only under a mate goal, only with --check-win in
// force, and only for the attacker.
//
// It exists as a named predicate because FIVE places in this engine
// independently decide "this move reaches the goal immediately" -- the two node
// routines, the DFPN expander, the root-split probe and its worker, and the
// depth-first root. The first implementation of x-check taught the obvious
// lesson by fixing one of them: mate in one worked at every depth except the one
// where -z 1 took a different path to the same question.
inline int variant_win_reached(const Board& nb, Goal goal, Color attacker,
                               const std::array<bool, VR_COUNT>& rule_wins) {
    if (goal != Goal::Mate) {
        return -1;
    }
    for (int rule = 0; rule < VR_COUNT; ++rule) {
        if (rule == VR_ESCAPE) {
            continue;   // not a countdown; read the other way round below
        }
        if (rule_wins[rule] && quota_of(nb, attacker, rule) == 0) {
            return rule;
        }
    }
    // Escape reads inverted: the attacker wins when the DEFENDER's own king has
    // reached ITS limit, not when the attacker fills a quota of his own. Getting
    // this backwards would hand the attacker a win for exposing his own king,
    // which is the losing condition, so the asymmetry is written out rather than
    // folded into the loop above.
    if (rule_wins[VR_ESCAPE]) {
        const Color defender = other(attacker);
        const std::uint8_t limit = quota_of(nb, defender, VR_ESCAPE);
        if (limit != kNoQuota && escape_count(nb, defender) >= static_cast<int>(limit)) {
            return VR_ESCAPE;
        }
    }
    return -1;
}

bool is_goal(const Board& b, Goal goal, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    switch (goal) {
        case Goal::Stalemate:
            return is_stalemate(b, move_reserve, move_reserve_capacity, static_pseudo);
        // The self- goals want the ATTACKER trapped, which cannot hold in a
        // position where the defender is to move, and the help- goals test
        // their terminal at the end of the cooperative line rather than after
        // any one move. Answering "no" is the correct answer to the question
        // this function asks, not a stub for those goals.
        case Goal::Selfmate:
        case Goal::Selfstalemate:
        case Goal::Helpmate:
        case Goal::Helpstalemate:
            return false;
        case Goal::Mate:
            break;
    }
    return is_checkmate(b, move_reserve, move_reserve_capacity, static_pseudo);
}

// The terminal predicate for a goal, applied to the side to move.
//
// One place, so the mate/stalemate distinction cannot drift between the
// adversarial, inverted and cooperative provers that all need it.
bool goal_terminal(const Board& b, Goal goal, bool move_reserve = false,
                   std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    return goal_wants_check(goal)
        ? is_checkmate(b, move_reserve, move_reserve_capacity, static_pseudo)
        : is_stalemate(b, move_reserve, move_reserve_capacity, static_pseudo);
}

// Occupancy planes only: enough to answer attack queries, without the mailbox,
// packed TT words, castling rights or side-to-move that a full Board carries.
struct Planes {
    std::uint64_t occ = 0;
    std::array<std::uint64_t, 2> by_color{};
    std::array<std::uint64_t, 6> by_type{};
};

inline void plane_clear(Planes& pl, int sq, Color c, PieceType t) {
    const std::uint64_t bit = 1ull << sq;
    pl.occ &= ~bit;
    pl.by_color[c] &= ~bit;
    pl.by_type[t] &= ~bit;
}

inline void plane_set(Planes& pl, int sq, Color c, PieceType t) {
    const std::uint64_t bit = 1ull << sq;
    pl.occ |= bit;
    pl.by_color[c] |= bit;
    pl.by_type[t] |= bit;
}

// Apply a move to occupancy planes alone, mirroring make_move's piece movement
// exactly: source vacated, en-passant victim removed, ordinary capture removed,
// promotion piece substituted, destination occupied, castling rook relocated.
//
// This exists so that move generation never has to build a child Board. The
// only questions generation asks of the child position are "is the mover's king
// attacked" (legality) and "is the opponent's king attacked" (check ordering),
// and both are answered by these planes.
Planes planes_after_move(const Board& b, const Move& m, int& mover_king_sq) {
    Planes pl;
    pl.occ = b.occ;
    pl.by_color = b.by_color;
    pl.by_type = b.by_type;

    const Color us = b.stm;
    const Color them = other(us);
    const char moving = b.sq[m.from];
    const PieceType pt = type_of(moving);

    plane_clear(pl, m.from, us, pt);
    if (m.ep) {
        plane_clear(pl, m.to + (us == WHITE ? -8 : 8), them, PT_PAWN);
    } else {
        const char captured = b.sq[m.to];
        if (type_of(captured) != PT_NONE) {
            plane_clear(pl, m.to, them, type_of(captured));
        }
    }
    plane_set(pl, m.to, us, m.promo ? type_of(m.promo) : pt);

    if (m.castle && pt == PT_KING) {
        const int home = us == WHITE ? 0 : 7;
        if (m.to == square_of(6, home)) {
            plane_clear(pl, square_of(7, home), us, PT_ROOK);
            plane_set(pl, square_of(5, home), us, PT_ROOK);
        } else if (m.to == square_of(2, home)) {
            plane_clear(pl, square_of(0, home), us, PT_ROOK);
            plane_set(pl, square_of(3, home), us, PT_ROOK);
        }
    }

    mover_king_sq = (pt == PT_KING) ? m.to : b.king_sq[us];
    return pl;
}

// Legality without building a child Board.
//
// The predicate is only "is the mover's king attacked afterwards", and the
// occupancy planes answer that, so the square array, castling rights, en-passant
// state and side-to-move of a child position were all being computed and thrown
// away. This is the same test the fused ordering path already used; the plain
// generation paths were still copying a whole Board per pseudo-legal move.
//
// A missing king counts as illegal, matching in_check, which the make_move
// version delegated to. parse_fen4 rejects positions without exactly one king
// per side, so this arm is unreachable in practice and exists to keep the two
// paths answering identically rather than merely equivalently.
inline bool move_is_legal(const Board& b, const Move& m) {
    int mover_king_sq = -1;
    const Planes pl = planes_after_move(b, m, mover_king_sq);
    return mover_king_sq >= 0 &&
           !attacked_on_planes(pl.occ, pl.by_color, pl.by_type, mover_king_sq, other(b.stm));
}




// Does this move give check, without materialising the child board?
//
// This used to be a second, independent implementation of "is square X attacked
// after move M" -- 84 lines duplicating the attack logic against a virtual
// mailbox. Two implementations of the same predicate is the exact shape of
// hazard that hid the castling-rights bug, so it now shares the single plane
// path used by move generation.
bool move_gives_check_fast(const Board& b, const Move& m) {
    const int enemy_king = king_square(b, other(b.stm));
    if (enemy_king < 0) {
        return true;
    }
    int ignored = -1;
    const Planes pl = planes_after_move(b, m, ignored);
    return attacked_on_planes(pl.occ, pl.by_color, pl.by_type, enemy_king, b.stm);
}

} // namespace mateprover

#endif // MATEPROVER_MOVEGEN_H_INCLUDED
