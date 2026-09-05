// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (c) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// kingescape.h -- What one move can and cannot do to a king's escape squares.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.
//
// WHY THIS IS ONE MODULE AND NOT TWO. Two separate mechanisms wanted the same
// analysis and each looked unaffordable while it was paying for the analysis
// alone: the direct-mate coverage early exit, and the selfmate attacker
// rejection test. They want the same five sets about a king. Costed against one
// residue class the infrastructure was rejected twice; costed against both it is
// the cheapest remaining work. That is the whole argument for this file.
//
// THE ONE THING TO GET RIGHT. Every set here feeds a predicate that DISCARDS
// work, so an error in the generous direction silently loses solutions on
// positions nobody will think to test. The escape set must therefore be an
// UNDER-estimate: claiming a flight the king does not have makes a mate look
// impossible and the node gets thrown away with the answer still in it. Every
// individual function below states which way it errs, because "conservative"
// means opposite things on the two sides of this file.

#ifndef MATEPROVER_KINGESCAPE_H_INCLUDED
#define MATEPROVER_KINGESCAPE_H_INCLUDED

namespace mateprover {

// Defined in prove.h, next to the rest of the exact-geometry helpers. Only the
// coverage table calls it, and only once, when its static initialiser runs.
inline bool piece_attacks_square(const Board& b, int from, int target);

// The eight king-adjacent directions, in the fixed order every mask in this
// file uses: file-major from (-1,-1) to (+1,+1), skipping the centre. The
// coverage table is indexed by masks in this order and nothing else may
// renumber them.
constexpr int kEscapeDirs = 8;

inline int escape_dir_index(int df, int dr) {
    // (df+1)*3 + (dr+1), with the centre removed and everything after it
    // shifted down by one.
    const int raw = (df + 1) * 3 + (dr + 1);
    return raw < 4 ? raw : raw - 1;
}

// Squares reachable from `sq` in each of the eight directions, or -1.
inline const std::array<std::array<int, kEscapeDirs>, 64>& escape_square_table() {
    static const std::array<std::array<int, kEscapeDirs>, 64> table = [] {
        std::array<std::array<int, kEscapeDirs>, 64> out{};
        for (int sq = 0; sq < 64; ++sq) {
            out[sq].fill(-1);
            for (int df = -1; df <= 1; ++df) {
                for (int dr = -1; dr <= 1; ++dr) {
                    if (df == 0 && dr == 0) continue;
                    const int nf = file_of(sq) + df, nr = rank_of(sq) + dr;
                    if (on_board(nf, nr)) {
                        out[sq][escape_dir_index(df, dr)] = square_of(nf, nr);
                    }
                }
            }
        }
        return out;
    }();
    return table;
}

// Pieces of `by` that would attack `target` if exactly one intervening piece
// were removed -- the indirect, or latent, attackers.
//
// Errs GENEROUSLY on purpose. Every consumer uses this to REMOVE a square from
// an escape set, and removing too much only costs a missed pruning. The blocker
// is not required to belong to either side: an attacker can move its own
// blocker, and it can also capture the defender's.
inline std::uint64_t indirect_attackers(const Board& b, int target, Color by) {
    const AttackBitboards& tb = attack_bb();
    const std::uint64_t them = b.by_color[by];
    const std::uint64_t queens = b.by_type[PT_QUEEN] & them;
    const std::uint64_t diagonal = (b.by_type[PT_BISHOP] & them) | queens;
    const std::uint64_t straight = (b.by_type[PT_ROOK] & them) | queens;
    std::uint64_t out = 0;
    for (int dir = 0; dir < 8; ++dir) {
        const std::uint64_t sliders = dir < 4 ? straight : diagonal;
        if (!sliders) continue;
        std::uint64_t blockers = tb.ray[dir][target] & b.occ;
        if (!blockers) continue;
        // Skip the nearest piece; the one behind it is the indirect attacker.
        const int first = tb.ray_ascending[dir] ? lsb_index(blockers) : msb_index(blockers);
        blockers &= ~(1ull << first);
        if (!blockers) continue;
        const int second = tb.ray_ascending[dir] ? lsb_index(blockers) : msb_index(blockers);
        if ((1ull << second) & sliders) {
            out |= 1ull << second;
        }
    }
    return out;
}

// Pieces of `by` directly attacking `target`, as a bitboard rather than a bool.
// The singleton test the rejection heuristic needs is a popcount on this.
//
// Takes the planes apart from a Board so a caller can ask the question against a
// hypothetical occupancy -- the king lifted off, a captured man removed -- which
// is what every caller here actually wants.
inline std::uint64_t direct_attackers_on_planes(
        std::uint64_t occ, const std::array<std::uint64_t, 2>& by_color,
        const std::array<std::uint64_t, 6>& by_type, int target, Color by) {
    const AttackBitboards& tb = attack_bb();
    const std::uint64_t them = by_color[by];
    std::uint64_t out = 0;
    out |= tb.knight[target] & by_type[PT_KNIGHT] & them;
    out |= tb.king[target] & by_type[PT_KING] & them;
    out |= tb.pawn[by][target] & by_type[PT_PAWN] & them;
    const std::uint64_t queens = by_type[PT_QUEEN] & them;
    const std::uint64_t diagonal = (by_type[PT_BISHOP] & them) | queens;
    const std::uint64_t straight = (by_type[PT_ROOK] & them) | queens;
    for (int dir = 0; dir < 8; ++dir) {
        const std::uint64_t sliders = dir < 4 ? straight : diagonal;
        if (!sliders) continue;
        const std::uint64_t blockers = tb.ray[dir][target] & occ;
        if (!blockers) continue;
        const int first = tb.ray_ascending[dir] ? lsb_index(blockers) : msb_index(blockers);
        if ((1ull << first) & sliders) {
            out |= 1ull << first;
        }
    }
    return out;
}

inline std::uint64_t direct_attackers(const Board& b, int target, Color by) {
    return direct_attackers_on_planes(b.occ, b.by_color, b.by_type, target, by);
}


// The five sets about one king that both residue mechanisms need.
//
// `flights` is what the king can actually do NOW. `unconditional` is the part of
// it that a single enemy move cannot take away except by covering or occupying
// the square -- which is the only part a coverage argument may reason about,
// because a square with a latent attacker behind a blocker can be shut without
// ever being attacked from a new square. `openable` is the mirror image: squares
// that are denied now but by so thin a margin that one move restores them.
struct KingEscape {
    int king_sq = -1;
    unsigned flights = 0;         // directions the king may legally step to
    unsigned unconditional = 0;   // flights no single move can close indirectly
    unsigned closeable = 0;       // flights \ unconditional
    unsigned openable = 0;        // denied by exactly one attacker, no backup
    std::uint64_t closers = 0;    // pieces that could close a `closeable`
    std::uint64_t openers = 0;    // the sole deniers of an `openable`
};

// Compute the five sets for the king of `side`, against the pieces of the other
// colour.
//
// `flights` is EXACT and must stay exact. Two ways to get it wrong, and both
// have already been made once in this project:
//
//   x-ray through the king   A king stepping directly away from a checking
//                            slider is still on the line. The square looks
//                            unattacked because the king itself is blocking the
//                            ray, so it is tested with the king lifted off the
//                            board -- which is the whole reason this uses
//                            attacked_on_planes with a doctored occupancy
//                            instead of is_attacked.
//
//   capture of the attacker  A neighbouring square holding an UNDEFENDED enemy
//                            piece is a flight, by capture. Treating occupancy
//                            as denial understates nothing and overstates the
//                            mate, which is the unsound direction.
inline KingEscape king_escape(const Board& b, Color side) {
    KingEscape out;
    const Color them = other(side);
    const int king = b.king_sq[side];
    out.king_sq = king;
    if (king < 0) {
        return out;
    }
    const std::array<int, kEscapeDirs>& neighbours = escape_square_table()[king];
    // The king cannot shield the square it is about to leave.
    const std::uint64_t lifted = b.occ & ~(1ull << king);

    for (int dir = 0; dir < kEscapeDirs; ++dir) {
        const int to = neighbours[dir];
        if (to < 0) continue;
        const std::uint64_t bit = 1ull << to;
        if (b.by_color[side] & bit) continue;         // blocked by our own man

        // Occupancy as the king would leave it: itself gone, and the captured
        // piece gone if this is a capture.
        std::array<std::uint64_t, 2> colors = b.by_color;
        std::array<std::uint64_t, 6> types = b.by_type;
        std::uint64_t occ = lifted;
        if (b.by_color[them] & bit) {
            occ &= ~bit;
            colors[them] &= ~bit;
            for (std::uint64_t& plane : types) plane &= ~bit;
        }
        // Both queries use the SAME doctored occupancy. Asking `direct` against
        // the real board would miss a slider standing behind the king and call a
        // two-piece denial a singleton, which is the one thing `openable` must
        // not do.
        const std::uint64_t direct = direct_attackers_on_planes(occ, colors, types,
                                                                to, them);
        const bool denied = attacked_on_planes(occ, colors, types, to, them);
        const std::uint64_t latent = indirect_attackers(b, to, them);

        if (denied) {
            // Not a flight now. It becomes one if the single piece denying it
            // moves away or is captured, and nothing stands behind it.
            if (latent == 0 && direct != 0 && (direct & (direct - 1)) == 0) {
                out.openable |= 1u << dir;
                out.openers |= direct;
            }
            continue;
        }
        out.flights |= 1u << dir;
        if (latent != 0) {
            out.closeable |= 1u << dir;
            out.closers |= latent;
        } else {
            out.unconditional |= 1u << dir;
        }
    }
    return out;
}

// Can ANY single piece deny every direction in `mask` at once, standing
// somewhere -- by attacking those squares, or by occupying one of them?
//
// Indexed by the eight-bit mask, derived by enumeration over the movement rules.
// Nothing empirical, nothing to tune, so it is derived here rather than
// transcribed, which also makes it exactly equivalent to its definition.
//
// SOUNDNESS DIRECTION. The table must never say "no piece can do this" when one
// can, because a false "no" throws away a node with a mate in it. Over-claiming
// merely declines the early exit, so everything doubtful is marked capable:
//
//   the king counts    An attacker king two files from the defender king covers
//                      squares beside it. Excluding kings, as the first cut of
//                      this table did, is unsound for discovered-check mates
//                      delivered by the king itself.
//   occupation counts  A piece standing ON an escape square denies it, without
//                      attacking it. Whether the king can capture it back is not
//                      decidable from the mask, so it is assumed it cannot.
inline const std::array<bool, 256>& escape_coverage_table() {
    static const std::array<bool, 256> table = [] {
        std::array<bool, 256> out{};
        out.fill(false);
        const int ksq = square_of(4, 4);     // central, so every offset exists
        const std::array<int, kEscapeDirs>& neighbours = escape_square_table()[ksq];
        for (int pt = 0; pt < 6; ++pt) {                  // P N B R Q K
            for (int from = 0; from < 64; ++from) {
                if (from == ksq) continue;
                unsigned covered = 0;
                for (int dir = 0; dir < kEscapeDirs; ++dir) {
                    const int to = neighbours[dir];
                    if (to < 0) continue;
                    if (from == to) {
                        covered |= 1u << dir;             // occupation
                        continue;
                    }
                    Board probe;
                    // Empty board: the most generous reach a piece of this type
                    // can have, which is the safe direction here.
                    probe.by_type[pt] |= 1ull << from;
                    probe.by_color[WHITE] |= 1ull << from;
                    probe.occ = 1ull << from;
                    if (piece_attacks_square(probe, from, to)) {
                        covered |= 1u << dir;
                    }
                }
                // Every subset of what this piece covers is coverable by it.
                // Enumerating subsets beats testing 256 masks per placement.
                for (unsigned m = covered; ; m = (m - 1) & covered) {
                    out[m] = true;
                    if (m == 0) break;
                }
            }
        }
        return out;
    }();
    return table;
}


// Does a single move move a single piece here? Castling moves two, and en
// passant vacates two squares, and both premises are load-bearing for every
// coverage argument below. Promotion needs no exception: the coverage table
// enumerates every piece type at every offset, so the promoted piece is in it.
inline bool one_move_moves_one_piece(const Board& b) {
    if (b.ep >= 0) {
        return false;
    }
    const unsigned mine = (b.stm == WHITE) ? 0x3u : 0xCu;
    return (b.castling & mine) == 0;
}

// Every square on a queen line from `sq`, ignoring what stands on them.
// Occupancy-free on purpose: a caller reasoning about the position AFTER some
// unknown move cannot rely on today's blockers.
inline std::uint64_t queen_lines_from(int sq) {
    static const std::array<std::uint64_t, 64> table = [] {
        std::array<std::uint64_t, 64> out{};
        const AttackBitboards& tb = attack_bb();
        for (int s = 0; s < 64; ++s) {
            for (int dir = 0; dir < 8; ++dir) {
                out[s] |= tb.ray[dir][s];
            }
        }
        return out;
    }();
    return table[sq];
}

// Is a mate in one impossible here, for the side to move, by the coverage
// argument alone? A `true` disproves the WHOLE node before a move is generated.
//
// THE ARGUMENT. To mate, the enemy king must end in check with nowhere to go. A
// single move moves a single piece. The directions in `unconditional` are ones
// no move can take away except by covering or occupying them from the square it
// lands on -- squares closeable by a discovery are excluded from the set for
// exactly this reason. So if no piece type, from any square, can deny that whole
// set at once, no single move mates and the node fails.
//
// The premise is "one move, one piece", so the two move types that break it are
// refused rather than modelled:
//
//   castling      moves a king and a rook, and the rook may cover flights the
//                 king cannot.
//   en passant    vacates the origin AND the captured pawn's square. Two
//                 vacancies open lines that a one-blocker x-ray test does not
//                 see, and the excluded-flight set would be too small.
//
// Promotion needs no special case: the table enumerates every piece type at
// every offset, so the promoted piece is already in it.
//
// Errs towards `false`. Every doubtful case answers "a mate might exist", which
// costs a node that would have been pruned and never costs a solution.
inline bool mate1_impossible_by_coverage(const Board& b) {
    if (!one_move_moves_one_piece(b)) {
        return false;
    }
    const KingEscape escape = king_escape(b, other(b.stm));
    if (escape.king_sq < 0) {
        return false;
    }
    return !escape_coverage_table()[escape.unconditional & 0xffu];
}


// Has the king of `side` any legal move at all, without executing one?
//
// The same legality test `move_is_legal` applies, expressed directly: a king may
// step to a square that holds none of its own men and that the enemy does not
// attack once the king has left the square it is standing on -- and once the man
// it captures, if any, is off the board. Both removals matter and both have been
// got wrong here before.
//
// EXACT, not conservative, and it must stay that way: callers use it to conclude
// a king is trapped.
inline bool king_has_legal_move(const Board& b, Color side) {
    const Color them = other(side);
    const int king = b.king_sq[side];
    if (king < 0) {
        return false;
    }
    const std::array<int, kEscapeDirs>& neighbours = escape_square_table()[king];
    const std::uint64_t lifted = b.occ & ~(1ull << king);
    for (int dir = 0; dir < kEscapeDirs; ++dir) {
        const int to = neighbours[dir];
        if (to < 0) continue;
        const std::uint64_t bit = 1ull << to;
        if (b.by_color[side] & bit) continue;
        if (b.by_color[them] & bit) {
            std::array<std::uint64_t, 2> colors = b.by_color;
            std::array<std::uint64_t, 6> types = b.by_type;
            colors[them] &= ~bit;
            for (std::uint64_t& plane : types) plane &= ~bit;
            if (!attacked_on_planes(lifted & ~bit, colors, types, to, them)) return true;
        } else if (!attacked_on_planes(lifted, b.by_color, b.by_type, to, them)) {
            return true;
        }
    }
    return false;
}

// Could ANY move of `side`'s king expose its enemy's king to a discovered check?
//
// A king cannot give check itself, so a discovery is the only way a king move
// checks anything. A discovery needs a slider of `side` aimed through this king
// at the other one -- so lifting the king off the board and asking whether the
// enemy king is attacked answers it in one query.
//
// Conservative in the direction that costs time rather than truth: it says
// "maybe" for a king move that stays ON the discovered line, which discovers
// nothing. The caller falls back to an exact test on a maybe.
inline bool king_move_could_discover_check(const Board& b, Color side) {
    const int mover = b.king_sq[side];
    const int target = b.king_sq[other(side)];
    if (mover < 0 || target < 0) {
        return false;
    }
    return attacked_on_planes(b.occ & ~(1ull << mover), b.by_color, b.by_type,
                              target, side);
}

// The selfmate counterpart, and the second thing the shared analysis buys.
//
// At a selfmate depth-1 node the attacker needs a move after which EVERY
// defender reply mates him. A defender KING move that is legal and gives no
// check is not a mate, so it refutes that attacker move by itself. The shipped
// per-move test looks for one by making each attacker move and trying the eight
// king steps -- exact, and it is what won the six positions it won, but it pays
// per move.
//
// This one refutes the whole node instead, and it needs two facts:
//
//   1. No single piece can deny every direction in `unconditional`. Then
//      whatever the attacker plays, the defender king still has somewhere legal
//      to step -- the moving piece cannot cover them all from where it lands,
//      and the directions a discovery could shut were excluded from the set.
//
//   2. No defender king move can give check. A king cannot check by itself, so
//      the only way is a discovery: a defender slider behind the king, aimed
//      through it at the attacker king. Impossible unless the two kings are on a
//      queen line -- so it is enough that the attacker king is not on one from
//      the defender king and cannot step onto one. That is tested against
//      occupancy-free lines, because the attacker's move may change blockers.
//
// Both hold => every attacker move is refuted => the node fails, with no move
// generated and no defender analysis begun.
//
// SELFMATE ONLY. Under selfstalemate the defender must stalemate the attacker,
// and a quiet king move does not disturb that at all -- the same trap that made
// the per-move version unsound on `b7/8/8/6p1/6P1/1RQ3PK/k6P/8 w - -` before it
// was gated. The goal check is not conservatism; it is the correctness argument.
inline bool selfmate_node_refuted_by_escape(const Board& b, Color attacker) {
    if (!one_move_moves_one_piece(b)) {
        return false;
    }
    const Color defender = other(attacker);
    const int dk = b.king_sq[defender];
    const int ak = b.king_sq[attacker];
    if (dk < 0 || ak < 0) {
        return false;
    }
    const std::uint64_t lines = queen_lines_from(dk);
    const std::uint64_t attacker_king_reach = attack_bb().king[ak] | (1ull << ak);
    if (attacker_king_reach & lines) {
        return false;                    // a discovered check might be available
    }
    const KingEscape escape = king_escape(b, defender);
    return escape.unconditional != 0 &&
           !escape_coverage_table()[escape.unconditional & 0xffu];
}

}  // namespace mateprover

#endif  // MATEPROVER_KINGESCAPE_H_INCLUDED
