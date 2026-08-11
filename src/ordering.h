// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// ordering.h -- Move-ordering scores and the ordered move-list generators.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_ORDERING_H_INCLUDED
#define MATEPROVER_ORDERING_H_INCLUDED

namespace mateprover {

// Move-ordering terms that need no child board: capture, promotion, and moving
// piece. Shared by the fused and split scoring paths so the two cannot drift.
int static_move_terms(const Board& b, const Move& m) {
    int score = 0;
    if (b.sq[m.to] != '.' || m.ep) score += 10000;
    if (m.promo) score += 8000;
    // tolower returns int; the cast makes the narrowing explicit rather than
    // implicit. Safe for the ASCII piece letters this board stores.
    const char p = static_cast<char>(std::tolower(static_cast<unsigned char>(b.sq[m.from])));
    if (p == 'q') score += 50;
    if (p == 'r') score += 40;
    if (p == 'b' || p == 'n') score += 30;
    return score;
}

// The check term's SIGN carries the goal.
//
// For mate, a check is progress and scores +50000. For stalemate a check is
// disqualifying -- a checked side to move is either mated or has a reply, and
// neither is stalemate -- so it scores -50000 instead. Ordering then prefers
// quiet, mobility-removing moves without any extra machinery.
//
// The magnitude is load-bearing beyond ordering: the shortcut in prove.h reads
// |score| >= 50000 as "this move gives check" and skips a terminal test it can
// already decide. Keeping the same magnitude keeps that flag intact under both
// goals, which is why this is a sign flip rather than a separate field.
int check_term(bool gives_check, Goal goal) {
    if (!gives_check) return 0;
    // Negative wherever the terminal wants the trapped side NOT in check, so a
    // checking move sorts last instead of first. The sign is not cosmetic: with
    // it positive under a stalemate goal, an unscored move read as "not a
    // check" and a CHECKMATE was accepted as a stalemate (54).
    return goal_wants_check(goal) ? 50000 : -50000;
}

// Can a move with this ordering score possibly reach the goal?
//
// Mate requires the defender to be in check; stalemate requires that he is not.
// So the same check bit that admits a move under one goal excludes it under the
// other, and both terminal scans use this rather than testing the score's sign
// directly -- getting that test backwards once silently skipped every candidate,
// and getting it backwards the other way would accept a checkmate as a stalemate.
// Must a move that WINS at the last ply be a check?
//
// True for a mate goal, because a checkmate is a check, and true for a check
// quota for the obvious reason. FALSE under a capture quota: a quiet capture can
// fill the quota and win outright, and every caller of `move_can_reach_goal`
// below uses it to DISCARD moves before they are executed -- so under captures
// they would throw the winning move away unexecuted.
//
// This is the sixth shortcut to need gating for a variant rule, and the first
// that was not behind a named predicate. It is one now.
inline bool last_ply_win_needs_check(const std::array<bool, VR_COUNT>& rule_wins,
                                     const Board& b, Color attacker) {
    return !(rule_wins[VR_CAPTURE] &&
             quota_of(b, attacker, VR_CAPTURE) != kNoQuota);
}

bool move_can_reach_goal(int score, Goal goal) {
    // The thresholds must clear static_move_terms, which adds up to 18050
    // (capture 10000 + promotion 8000 + piece 50) on top of the check term.
    //
    // For a mate goal that is already true: a check scores at least 50000 and a
    // non-check at most 18050, so `>= 50000` separates them. For a stalemate
    // goal it was NOT: a check scores -50000 plus statics, so a checking rook
    // move scored -49960 -- above the -50000 threshold, read as "not a check",
    // and then accepted as a stalemate because the defender had no reply. That
    // is a CHECKMATE reported as `sm 1`, a false proof.
    //
    // Signs separate the two cleanly: with the check term negative, every
    // checking move scores below zero and every other move at or above it.
    return goal_wants_check(goal) ? score >= 50000 : score >= 0;
}

// Ordering terms that require the child board, given that board.
int child_move_terms(const Board& nb, bool score_mates, bool score_checks, Goal goal,
                     bool move_reserve, std::size_t move_reserve_capacity, bool static_pseudo) {
    int score = 0;
    if (score_mates) {
        if (is_goal(nb, goal, move_reserve, move_reserve_capacity, static_pseudo)) score += 1000000;
        if (score_checks) score += check_term(in_check(nb, nb.stm), goal);
    } else if (score_checks) {
        score += check_term(in_check(nb, nb.stm), goal);
    }
    return score;
}

int move_score(const Board& b, const Move& m, bool score_mates, bool score_checks, Goal goal, bool fast_check_score, bool move_reserve, std::size_t move_reserve_capacity, bool static_pseudo) {
    int score = 0;
    if (score_mates) {
        Board nb = make_move(b, m);
        if (is_goal(nb, goal, move_reserve, move_reserve_capacity, static_pseudo)) score += 1000000;
        if (score_checks) score += check_term(in_check(nb, nb.stm), goal);
    } else if (score_checks) {
        bool gives_check = false;
        if (fast_check_score) {
            gives_check = move_gives_check_fast(b, m);
        } else {
            Board nb = make_move(b, m);
            gives_check = in_check(nb, nb.stm);
        }
        score += check_term(gives_check, goal);
    }
    return score + static_move_terms(b, m);
}

void stable_bucket_order(std::vector<Move>& moves) {
    std::vector<int> scores;
    scores.reserve(moves.size());
    for (const Move& move : moves) {
        auto it = std::find(scores.begin(), scores.end(), move.score);
        if (it != scores.end()) {
            continue;
        }
        auto insert_at = std::find_if(scores.begin(), scores.end(), [&](int score) {
            return move.score > score;
        });
        scores.insert(insert_at, move.score);
    }

    std::vector<Move> ordered;
    ordered.reserve(moves.size());
    for (int score : scores) {
        for (const Move& move : moves) {
            if (move.score == score) {
                ordered.push_back(move);
            }
        }
    }
    moves.swap(ordered);
}

void order_moves(const Board& b, std::vector<Move>& moves, bool score_mates, bool score_checks, Goal goal, bool fast_check_score, bool move_reserve, std::size_t move_reserve_capacity, bool static_pseudo, bool inplace_order, bool bucket_order) {
    if (moves.size() < 2) {
        return;
    }
    if (inplace_order) {
        for (Move& move : moves) {
            move.score = move_score(b, move, score_mates, score_checks, goal, fast_check_score, move_reserve, move_reserve_capacity, static_pseudo);
        }
        if (bucket_order) {
            stable_bucket_order(moves);
            return;
        }
        std::stable_sort(moves.begin(), moves.end(), [](const Move& a, const Move& c) {
            return a.score > c.score;
        });
        return;
    }
    struct ScoredMove {
        Move move;
        int score = 0;
    };
    std::vector<ScoredMove> scored;
    scored.reserve(moves.size());
    for (const Move& move : moves) {
        scored.push_back({move, move_score(b, move, score_mates, score_checks, goal, fast_check_score, move_reserve, move_reserve_capacity, static_pseudo)});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const ScoredMove& a, const ScoredMove& c) {
        return a.score > c.score;
    });
    for (std::size_t i = 0; i < scored.size(); ++i) {
        moves[i] = scored[i].move;
    }
}

// The goal is part of the key. Tables are per-search and a run has one goal, so
// nothing can mix them today -- but a stalemate verdict satisfying a mate query
// would be a false proof, which is the one class of bug this engine exists to
// make impossible. It costs a bit of an already-spare word.
// Field layout of the context word. Depth used to occupy bits 0-31 -- thirty-two
// bits for a value that never exceeds the requested search depth. Reading a
// field's WIDTH as its REQUIREMENT is how this engine came to believe it was out
// of key space; narrowing depth to eight bits freed twenty-four, which is room
// for the variant quotas and the next several rules after them.
//
// Eight bits is a real bound, not a hopeful one, so it is asserted rather than
// masked: a depth that wrapped would give two distinct nodes one key.
constexpr int kKeyDepthBits = 8;
constexpr int kMaxKeyDepth = (1 << kKeyDepthBits) - 1;

TTKey tt_key(const Board& b, int depth, char kind, Color attacker, Goal goal) {
    TTKey k;
    k.board = b.packed;
    std::uint64_t ep = static_cast<std::uint64_t>(b.ep + 1);
    // A search deeper than this cannot be keyed correctly, so it must not be
    // keyed at all. Callers cap the requested depth well below it.
    assert(depth >= 0 && depth <= kMaxKeyDepth);
    k.context = static_cast<std::uint64_t>(depth & kMaxKeyDepth)
        | (static_cast<std::uint64_t>(b.stm) << 8)
        | (static_cast<std::uint64_t>(attacker) << 9)
        | (static_cast<std::uint64_t>(kind == 'D' ? 1 : 0) << 10)
        | (static_cast<std::uint64_t>(b.castling & 0x0fu) << 11)
        | (ep << 15)
        // THREE bits, not one. This used to be `goal == Stalemate ? 1 : 0`,
        // which gave Mate and Selfmate the same encoding -- harmless only
        // because no table has ever spanned two goals, since a Search fixes its
        // goal for life and every portfolio lane shares it. With six goals that
        // latent collision would have become a live one, and the symptom would
        // have been a verdict proved under one goal returned as another: a
        // false proof with nothing wrong in the output to see. ep occupies bits
        // 39-45, so 47-49 are free.
        | (static_cast<std::uint64_t>(goal) << 22);
    // Variant quotas, seven bits each, starting at bit 25 and leaving 53-63 for
    // the rules after these. Two positions identical on the board but differing
    // in what either side still owes are DIFFERENT positions, and a key that
    // cannot tell them apart returns a verdict proved under one state as though
    // it held under another -- the same class of false proof the goal bits were
    // widened to prevent, and just as invisible in the output.
    //
    // Capture quotas are in fact derivable from material, since captures by one
    // side are exactly the men the other has lost since the root. They are keyed
    // anyway: that derivation holds only while a table never spans two roots,
    // which is true today and enforced nowhere.
    for (std::size_t i = 0; i < b.quota.size(); ++i) {
        k.context |= static_cast<std::uint64_t>(b.quota[i] & 0x7fu) << (25 + 7 * i);
    }
    return k;
}

TTKey move_hint_key(const Board& b, char kind, Color attacker, Goal goal) {
    return tt_key(b, 0, kind, attacker, goal);
}

bool same_move(const Move& a, const Move& b) {
    return a.from == b.from
        && a.to == b.to
        && a.promo == b.promo
        && a.castle == b.castle
        && a.ep == b.ep;
}

bool move_to_front(std::vector<Move>& moves, const Move& hint) {
    for (std::size_t i = 0; i < moves.size(); ++i) {
        if (same_move(moves[i], hint)) {
            if (i != 0) {
                std::rotate(moves.begin(), moves.begin() + static_cast<std::ptrdiff_t>(i), moves.begin() + static_cast<std::ptrdiff_t>(i + 1));
            }
            return true;
        }
    }
    return false;
}

// Generate legal moves and their ordering scores in a single pass.
//
// Legality already requires building the child board and asking whether the
// mover left their own king in check. The check-scoring term asks whether the
// OPPONENT is now in check -- the same child board, the other king. Computing
// the two separately builds every child board twice. Fusing them removes one
// full board copy per move from every ordered move list, which on a hard-suite
// run is tens of millions of copies.
//
// This is an evaluation-order change only. The scores produced are identical to
// the split path, so the resulting move order, and therefore the search, is
// unchanged.
std::vector<Move> legal_moves_fused(const Board& b, const SearchConfig& cfg, bool& out_scored) {
    out_scored = false;
    // Generate into a stack buffer.
    //
    // This runs at nearly every node, and the heap move list it allocated per
    // call was the largest single source of allocator traffic in the search:
    // measured at 3.3 allocations and ~2.4 KB per node, 28 GB churned in a
    // 25-second search. MoveList is fixed-capacity and reports overflow rather
    // than truncating, so the heap path is kept as a spill for positions that
    // exceed it -- the same fallback shape legal_moves uses under
    // --static-pseudo.
    MoveList buf;
    gen_pseudo(b, buf);
    std::vector<Move> spill;
    if (buf.overflow) {
        if (cfg.move_reserve) {
            spill.reserve(cfg.move_reserve_capacity);
        }
        gen_pseudo(b, spill);
    }
    const Move* const pseudo_data = buf.overflow ? spill.data() : buf.moves.data();
    const std::size_t pseudo_n = buf.overflow ? spill.size() : buf.count;

    std::vector<Move> legal;
    legal.reserve(pseudo_n);
    // Always score. `--fast-check-score` used to select a delta-based check
    // test; now that move_gives_check_fast shares this same plane path, "fast"
    // and "exact" check scoring are the identical computation, so the flag has
    // nothing left to select. It previously suppressed scoring entirely here,
    // which silently disabled move ordering and cost ~30x on every suite.
    const bool want_scores = true;
    const Color us = b.stm;
    const Color them = other(us);
    const int enemy_king = b.king_sq[them];

    // --score-mates needs is_checkmate on a real child board, so it keeps the
    // materialising path. Every other configuration answers both of its
    // questions from occupancy planes and never builds a child Board here.
    if (cfg.score_mates) {
        for (std::size_t pi = 0; pi < pseudo_n; ++pi) {
        Move m = pseudo_data[pi];
            Board nb = make_move(b, m);
            if (in_check(nb, other(nb.stm))) {
                continue;
            }
            if (want_scores) {
                m.score = child_move_terms(nb, cfg.score_mates, cfg.score_checks, cfg.goal,
                                           cfg.move_reserve, cfg.move_reserve_capacity,
                                           cfg.static_pseudo)
                        + static_move_terms(b, m);
            }
            legal.push_back(m);
        }
        out_scored = want_scores;
        return legal;
    }

    for (std::size_t pi = 0; pi < pseudo_n; ++pi) {
        Move m = pseudo_data[pi];
        int king_after = -1;
        const Planes pl = planes_after_move(b, m, king_after);
        if (king_after >= 0 && attacked_on_planes(pl.occ, pl.by_color, pl.by_type, king_after, them)) {
            continue; // illegal: the mover left their own king attacked
        }
        if (want_scores) {
            int score = static_move_terms(b, m);
            if (cfg.score_checks && enemy_king >= 0 &&
                attacked_on_planes(pl.occ, pl.by_color, pl.by_type, enemy_king, us)) {
                score += 50000;
            }
            m.score = score;
        }
        legal.push_back(m);
    }
    out_scored = want_scores;
    return legal;
}

// Obtain the move list for a search node, ordered if the node warrants it.
// Returns whether the returned moves carry ordering scores.
std::vector<Move> generate_ordered_moves(Search& s, const Board& b, bool& moves_scored) {
    moves_scored = false;
    if (s.fused_order && !s.static_pseudo) {
        bool scored = false;
        std::vector<Move> moves = legal_moves_fused(b, s, scored);
        if (scored && moves.size() >= std::max<std::size_t>(2, s.order_min_size)) {
            ++s.stats.order_calls;
            s.stats.order_moves += moves.size();
            if (s.bucket_order) {
                stable_bucket_order(moves);
            } else {
                std::stable_sort(moves.begin(), moves.end(),
                                 [](const Move& a, const Move& c) { return a.score > c.score; });
            }
            moves_scored = true;
        }
        return moves;
    }
    std::vector<Move> moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    if (moves.size() >= std::max<std::size_t>(2, s.order_min_size)) {
        ++s.stats.order_calls;
        s.stats.order_moves += moves.size();
        order_moves(b, moves, s.score_mates, s.score_checks, s.goal, s.fast_check_score,
                    s.move_reserve, s.move_reserve_capacity, s.static_pseudo,
                    s.inplace_order, s.bucket_order);
        moves_scored = true;
    }
    return moves;
}

// Pseudo-legal defender replies, ordered WITHOUT building any child board.
//
// A defender (AND) node stops at the first reply that survives, so it typically
// searches ~1 of ~19 generated replies. Eagerly proving legality for all of
// them builds eighteen child boards that are then discarded.
//
// `move_gives_check_fast` answers the check-ordering term by querying the
// post-move board virtually, so ordering needs no child board at all. Legality
// is then established lazily, only for replies actually reached.
//
// The ordering predicate is identical to the eager path -- both ask whether the
// move gives check -- and the sort is stable, so removing the illegal moves
// from the sequence leaves the order of the legal ones unchanged. The sequence
// of legal replies searched is therefore the same as the eager path's, and so
// is the resulting search.
std::vector<Move> pseudo_defender_moves(Search& s, const Board& b) {
    std::vector<Move> pseudo;
    if (s.move_reserve) {
        pseudo.reserve(s.move_reserve_capacity);
    }
    gen_pseudo(b, pseudo);
    s.stats.defender_pseudo_moves += pseudo.size();
    if (pseudo.size() >= std::max<std::size_t>(2, s.order_min_size)) {
        ++s.stats.order_calls;
        s.stats.order_moves += pseudo.size();
        for (Move& m : pseudo) {
            int score = static_move_terms(b, m);
            if (s.score_checks && move_gives_check_fast(b, m)) {
                score += 50000;
            }
            m.score = score;
        }
        if (s.bucket_order) {
            stable_bucket_order(pseudo);
        } else {
            std::stable_sort(pseudo.begin(), pseudo.end(),
                             [](const Move& a, const Move& c) { return a.score > c.score; });
        }
    }
    return pseudo;
}

// Apply attacker-side restrictions that change which problem is being solved.
//
// This is not a pruning heuristic: removing a legal attacker move would be
// unsound for an ordinary directmate. It is only correct because `-C 1` asks a
// different question -- "is there a mate in which every attacker move gives
// check" -- and under that question the removed moves are not candidates.
void restrict_attacker_moves(Search& s, const Board& b, std::vector<Move>& moves);

bool should_order(const Search& s, std::size_t move_count) {
    return move_count >= std::max<std::size_t>(2, s.order_min_size);
}

} // namespace mateprover

#endif // MATEPROVER_ORDERING_H_INCLUDED
