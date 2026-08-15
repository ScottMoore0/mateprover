// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// dfpn.h -- Native DFPN preconditioner. A preconditioner only, never an output authority.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_DFPN_H_INCLUDED
#define MATEPROVER_DFPN_H_INCLUDED

namespace mateprover {

// ---------------------------------------------------------------------------
// Native DFPN (depth-first proof-number search)
//
// The exact route explores attacker moves left to right in a fixed order.
// DFPN instead repeatedly descends into the child that is currently cheapest to
// resolve, measured by proof and disproof numbers, so effort concentrates where
// the proof is actually likely to be settled.
//
// Depth-bounded directmate makes this simpler than general DFPN: the depth
// bound terminates every line, so the usual non-termination and
// graph-history-interaction hazards do not arise. Depth is part of the key, so
// a result at one remaining depth can never satisfy a query at another.
//
// SAFETY: DFPN is a preconditioner, never an output authority. It contributes
// exactly two things back to the exact search:
//
//   * disproofs, which are exact verdicts ("no mate within this remaining
//     depth from this exact position") and are written through the same
//     audited store_exact_proof_table helper;
//   * proof moves, written only into the ordering-hint table, which by
//     construction cannot change any verdict.
//
// The exact prover still produces every accepted proof, PV and certificate. A
// bug in the numbers below can therefore cost search time or, at worst, a
// missed proof caught by the corpus gates -- it cannot manufacture a mate.
constexpr std::uint32_t DFPN_INF = 1u << 30;

using PnDn = PnDnFwd;

inline std::uint32_t sat_add(std::uint32_t a, std::uint32_t b) {
    const std::uint64_t sum = static_cast<std::uint64_t>(a) + b;
    return sum >= DFPN_INF ? DFPN_INF : static_cast<std::uint32_t>(sum);
}

PnDn dfpn_attacker(Search& s, const Board& b, int depth, std::uint32_t thpn, std::uint32_t thdn);

// Move generation for DFPN nodes.
//
// df-pn selects the child to expand by proof/disproof number, not by move
// score: every child starts at pn=dn=1, so ordering influences nothing except
// which of several equal minima is picked first. A DFPN node is re-entered many
// times -- that repeated descent is the algorithm -- and paying for a stable
// sort on every entry buys nothing.
//
// Scores are still computed, because the fused generator produces the check bit
// while it is already building the child planes for the legality test, and the
// attacker's mate scan uses it to skip moves that cannot possibly be mate.
std::vector<Move> dfpn_moves(Search& s, const Board& b, bool& scored) {
    ++s.stats.dfpn_movegen;
    if (s.dfpn_sort) {
        return generate_ordered_moves(s, b, scored);
    }
    if (!s.static_pseudo && s.fused_order) {
        return legal_moves_fused(b, s, scored);
    }
    scored = false;
    return legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
}

bool dfpn_budget_exhausted(const Search& s) {
    return s.dfpn_node_limit != 0 && s.stats.dfpn_nodes >= s.dfpn_node_limit;
}

// Is this node already in the table? Distinguishes absent from stored-as-{1,1},
// which dfpn_lookup cannot, because a miss returns the default {1, 1}.
bool dfpn_seen(const Search& s, const TTKey& key) {
    return s.dfpn_tt.find(key) != s.dfpn_tt.end();
}

PnDn dfpn_lookup(Search& s, const TTKey& key) {
    auto it = s.dfpn_tt.find(key);
    return it == s.dfpn_tt.end() ? PnDn{} : it->second;
}

void dfpn_store(Search& s, const TTKey& key, const PnDn& v) {
    if (s.dfpn_tt.size() >= s.dfpn_capacity && s.dfpn_tt.find(key) == s.dfpn_tt.end()) {
        // Unbounded growth is not acceptable; the numbers are a heuristic
        // cache, so dropping them costs search effort and nothing else.
        s.dfpn_tt.clear();
    }
    s.dfpn_tt[key] = v;
}

// Publish a settled DFPN verdict to the exact search.
void dfpn_publish(Search& s, const Board& b, int depth, char kind, const PnDn& v) {
    if (v.dn == 0 && s.dfpn_share_disproofs) {
        ++s.stats.dfpn_disproved;
        store_exact_proof_table(s, tt_key(b, 0, kind, s.attacker, s.goal), depth, {});
    }
}

// ---------------------------------------------------------------------------
// Selfmate preconditioner.
//
// The AND/OR structure survives the goal change -- the attacker still needs one
// move to work and the defender still needs every reply refuted -- so proof and
// disproof numbers still mean what they mean. What changes is where the
// terminal sits and what the degenerate cases decide, and those cannot be
// shared with the directmate walker: its terminal asks whether the DEFENDER is
// mated, and this goal asks the opposite.
//
// Same safety property as the directmate preconditioner: it steers only. Every
// verdict still comes from prove_selfmate_attacker, and the goal is part of the
// transposition key, so a number computed here can never answer a directmate
// query.
PnDn dfpn_selfmate_attacker(Search& s, const Board& b, int depth,
                            std::uint32_t thpn, std::uint32_t thdn);

PnDn dfpn_selfmate_defender(Search& s, const Board& b, int depth,
                            std::uint32_t thpn, std::uint32_t thdn) {
    if (search_cancelled(s) || dfpn_budget_exhausted(s)) {
        return PnDn{1, 1};
    }
    ++s.stats.dfpn_nodes;
    ++s.stats.nodes;
    const TTKey key = tt_key(b, depth, 'D', s.attacker, s.goal);

    bool scored = false;
    std::vector<Move> replies = dfpn_moves(s, b, scored);
    if (replies.empty()) {
        // The defender is mated or stalemated, so he has not mated the
        // attacker. Disproved.
        const PnDn v{DFPN_INF, 0};
        dfpn_store(s, key, v);
        return v;
    }
    if (!dfpn_seen(s, key)) {
        const PnDn guess{static_cast<std::uint32_t>(
                             std::min<std::size_t>(replies.size(), DFPN_INF)), 1};
        if (guess.pn >= thpn || guess.dn >= thdn || dfpn_budget_exhausted(s) || s.aborted) {
            dfpn_store(s, key, guess);
            return guess;
        }
    }

    std::vector<Board> kids;
    std::vector<TTKey> keys;
    kids.reserve(replies.size());
    keys.reserve(replies.size());
    for (const Move& r : replies) {
        kids.push_back(make_move(b, r));
        keys.push_back(tt_key(kids.back(), depth - 1, 'A', s.attacker, s.goal));
    }

    for (;;) {
        PnDn total{0, DFPN_INF};
        std::size_t best = 0;
        std::uint32_t best_dn = DFPN_INF;
        std::uint32_t second_dn = DFPN_INF;
        for (std::size_t i = 0; i < kids.size(); ++i) {
            const PnDn v = dfpn_lookup(s, keys[i]);
            total.pn = sat_add(total.pn, v.pn);
            if (v.dn < best_dn) {
                second_dn = best_dn;
                best_dn = v.dn;
                best = i;
            } else if (v.dn < second_dn) {
                second_dn = v.dn;
            }
        }
        total.dn = best_dn;
        if (total.pn >= thpn || total.dn >= thdn || dfpn_budget_exhausted(s) || s.aborted) {
            dfpn_store(s, key, total);
            return total;
        }
        const PnDn chosen = dfpn_lookup(s, keys[best]);
        const std::uint32_t child_thpn =
            thpn > total.pn ? sat_add(chosen.pn, thpn - total.pn) : chosen.pn;
        const std::uint32_t child_thdn =
            std::min<std::uint32_t>(thdn, sat_add(second_dn, 1));
        dfpn_selfmate_attacker(s, kids[best], depth - 1, child_thpn, child_thdn);
        if (s.aborted) {
            return PnDn{1, 1};
        }
    }
}

PnDn dfpn_selfmate_attacker(Search& s, const Board& b, int depth,
                            std::uint32_t thpn, std::uint32_t thdn) {
    if (search_cancelled(s) || dfpn_budget_exhausted(s)) {
        return PnDn{1, 1};
    }
    ++s.stats.dfpn_nodes;
    ++s.stats.nodes;
    const TTKey key = tt_key(b, depth, 'A', s.attacker, s.goal);

    // The terminal is here rather than after an attacker move: "the attacker is
    // mated" is a statement about the side to move.
    const bool have_move = has_legal_move(b, s.move_reserve, s.move_reserve_capacity,
                                          s.static_pseudo);
    if (!have_move) {
        const bool mated = in_check(b, b.stm);
        const PnDn v = mated ? PnDn{0, DFPN_INF} : PnDn{DFPN_INF, 0};
        dfpn_store(s, key, v);
        if (mated) {
            ++s.stats.dfpn_proved;
        }
        return v;
    }
    if (depth <= 0) {
        const PnDn v{DFPN_INF, 0};
        dfpn_store(s, key, v);
        return v;
    }

    bool scored = false;
    std::vector<Move> moves = dfpn_moves(s, b, scored);
    restrict_attacker_moves(s, b, moves);
    if (moves.empty()) {
        const PnDn v{DFPN_INF, 0};
        dfpn_store(s, key, v);
        return v;
    }
    if (!dfpn_seen(s, key)) {
        const PnDn guess{1, static_cast<std::uint32_t>(
                                std::min<std::size_t>(moves.size(), DFPN_INF))};
        if (guess.pn >= thpn || guess.dn >= thdn || dfpn_budget_exhausted(s) || s.aborted) {
            dfpn_store(s, key, guess);
            return guess;
        }
    }

    std::vector<Board> kids;
    std::vector<TTKey> keys;
    kids.reserve(moves.size());
    keys.reserve(moves.size());
    for (const Move& m : moves) {
        kids.push_back(make_move(b, m));
        keys.push_back(tt_key(kids.back(), depth, 'D', s.attacker, s.goal));
    }

    for (;;) {
        PnDn total{DFPN_INF, 0};
        std::size_t best = 0;
        std::uint32_t best_pn = DFPN_INF;
        std::uint32_t second_pn = DFPN_INF;
        for (std::size_t i = 0; i < kids.size(); ++i) {
            const PnDn v = dfpn_lookup(s, keys[i]);
            total.dn = sat_add(total.dn, v.dn);
            if (v.pn < best_pn) {
                second_pn = best_pn;
                best_pn = v.pn;
                best = i;
            } else if (v.pn < second_pn) {
                second_pn = v.pn;
            }
        }
        total.pn = best_pn;
        if (total.pn >= thpn || total.dn >= thdn || dfpn_budget_exhausted(s) || s.aborted) {
            dfpn_store(s, key, total);
            if (total.pn == 0 && s.proof_hints) {
                s.attacker_proofs.store(move_hint_key(b, 'A', s.attacker, s.goal), moves[best]);
            }
            return total;
        }
        const PnDn chosen = dfpn_lookup(s, keys[best]);
        const std::uint32_t child_thpn =
            std::min<std::uint32_t>(thpn, sat_add(second_pn, 1));
        const std::uint32_t child_thdn =
            thdn > total.dn ? sat_add(chosen.dn, thdn - total.dn) : chosen.dn;
        dfpn_selfmate_defender(s, kids[best], depth, child_thpn, child_thdn);
        if (s.aborted) {
            return PnDn{1, 1};
        }
    }
}

PnDn dfpn_defender(Search& s, const Board& b, int depth, std::uint32_t thpn, std::uint32_t thdn) {
    if (search_cancelled(s) || dfpn_budget_exhausted(s)) {
        return PnDn{1, 1};
    }
    // Count DFPN work in the reported node total. Without this, acn would
    // report only the exact pass and every DFPN comparison would flatter
    // itself by hiding the preconditioner's cost entirely.
    ++s.stats.dfpn_nodes;
    ++s.stats.nodes;
    const TTKey key = tt_key(b, depth, 'D', s.attacker, s.goal);

    bool scored = false;
    std::vector<Move> replies = dfpn_moves(s, b, scored);
    if (replies.empty()) {
        // No legal reply means stalemate here: the attacker's previous move was
        // tested for mate before recursing, so this is not a proof.
        const PnDn v{DFPN_INF, 0};
        dfpn_store(s, key, v);
        dfpn_publish(s, b, depth, 'D', v);
        return v;
    }

    // On a first visit every child is unvisited, so every lookup would return
    // the default {1, 1} and this AND node's value is exactly (N, 1). Measured:
    // the selection loop below runs once per entry almost always (149,379
    // iterations across 149,380 entries), so the children were being built,
    // keyed and probed -- 24 of each per node -- to compute a number already
    // known. When that value already exceeds a threshold the node returns
    // without descending, and none of that work was ever needed.
    //
    // A transposed child could genuinely be known, so this is an estimate
    // rather than a lookup. That is sound: the proof numbers only steer the
    // search, and every verdict still comes from the exact prover.
    if (!dfpn_seen(s, key)) {
        // Move count alone cannot tell twelve quiet replies from twelve forced
        // king moves in a mating net, yet it is the only signal steering every
        // descent (architecture 46). A defender not in check is choosing freely
        // and is correspondingly harder to refute, so weight the proof estimate
        // against those nodes and the search prefers forcing lines. Sound for
        // the same reason the estimate itself is: proof numbers only steer, and
        // every verdict still comes from the exact prover.
        std::size_t estimate = replies.size();
        if (s.dfpn_check_bias > 1 && !in_check(b, b.stm)) {
            estimate *= static_cast<std::size_t>(s.dfpn_check_bias);
        }
        const PnDn guess{static_cast<std::uint32_t>(
                             std::min<std::size_t>(estimate, DFPN_INF)),
                         1};
        if (guess.pn >= thpn || guess.dn >= thdn || dfpn_budget_exhausted(s) || s.aborted) {
            dfpn_store(s, key, guess);
            dfpn_publish(s, b, depth, 'D', guess);
            return guess;
        }
    }

    // Child boards and keys are built once per node visit, not once per
    // selection-loop iteration. Recomputing them each pass made every internal
    // node cost O(branching) make_move calls per iteration, which dominated
    // everything the algorithm saved.
    std::vector<Board> child_boards;
    std::vector<TTKey> child_keys;
    child_boards.reserve(replies.size());
    child_keys.reserve(replies.size());
    for (const Move& r : replies) {
        child_boards.push_back(make_move(b, r));
        child_keys.push_back(tt_key(child_boards.back(), depth, 'A', s.attacker, s.goal));
    }

    for (;;) {
        // AND node: proving needs every reply proved, disproving needs one.
        std::uint32_t sum_pn = 0;
        std::uint32_t min_dn = DFPN_INF;
        std::size_t best = 0;
        std::uint32_t best_dn = DFPN_INF;
        std::uint32_t second_dn = DFPN_INF;
        for (std::size_t i = 0; i < replies.size(); ++i) {
            const PnDn c = dfpn_lookup(s, child_keys[i]);
            sum_pn = sat_add(sum_pn, c.pn);
            if (c.dn < best_dn) {
                second_dn = best_dn;
                best_dn = c.dn;
                best = i;
            } else if (c.dn < second_dn) {
                second_dn = c.dn;
            }
            min_dn = std::min(min_dn, c.dn);
        }
        const PnDn here{sum_pn, min_dn};
        if (here.pn >= thpn || here.dn >= thdn || dfpn_budget_exhausted(s) || s.aborted) {
            dfpn_store(s, key, here);
            dfpn_publish(s, b, depth, 'D', here);
            return here;
        }
        // thpn > here.pn here: the early return above fired if it were not, so
        // the subtraction cannot underflow. It used to be guarded by a ternary
        // whose false branch was unreachable, which implied a risk that the
        // control flow had already excluded.
        const std::uint32_t child_thpn = std::min<std::uint32_t>(
            DFPN_INF, thpn - here.pn + dfpn_lookup(s, child_keys[best]).pn);
        std::uint32_t child_thdn =
            std::min<std::uint32_t>(thdn, second_dn == DFPN_INF ? DFPN_INF : second_dn + 1);
        if (s.dfpn_epsilon_64 > 0 && child_thdn < DFPN_INF) {
            const std::uint64_t widened =
                static_cast<std::uint64_t>(child_thdn) * (64 + s.dfpn_epsilon_64) / 64;
            child_thdn = std::min<std::uint32_t>(thdn, static_cast<std::uint32_t>(
                std::min<std::uint64_t>(widened, DFPN_INF)));
        }
        dfpn_attacker(s, child_boards[best], depth, child_thpn, child_thdn);
    }
}

PnDn dfpn_attacker(Search& s, const Board& b, int depth, std::uint32_t thpn, std::uint32_t thdn) {
    if (search_cancelled(s) || dfpn_budget_exhausted(s)) {
        return PnDn{1, 1};
    }
    ++s.stats.dfpn_nodes;
    ++s.stats.nodes;
    // Depth belongs in the key. This module's own contract says so: a result
    // at one remaining depth must never satisfy a query at another. It was
    // passing 0, so every depth shared one entry -- a position stored as
    // {INF, 0} at depth 0 read back as unprovable at every depth, and the
    // proof numbers stopped meaning anything.
    const TTKey key = tt_key(b, depth, 'A', s.attacker, s.goal);

    if (depth <= 0 || b.stm != s.attacker) {
        const PnDn v{DFPN_INF, 0};
        dfpn_store(s, key, v);
        return v;
    }

    bool scored = false;
    std::vector<Move> moves = dfpn_moves(s, b, scored);
    restrict_attacker_moves(s, b, moves);
    if (moves.empty()) {
        const PnDn v{DFPN_INF, 0};
        dfpn_store(s, key, v);
        dfpn_publish(s, b, depth, 'A', v);
        return v;
    }

    // Immediate-mate scan. Only moves that give check can mate, and the fused
    // generator already computed that bit, so the child board is built inside
    // the test rather than for every move up front: at ~0.6 tests per node,
    // pre-building all of them meant ~24 make_move calls to examine one.
    // Mate goal only; see the note in prove.h. Here a wrong skip would cost
    // only search quality, but the flag is unreliable for the same reason.
    const bool mate_shortcut = scored && s.score_checks && !s.score_mates && s.goal == Goal::Mate;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const Move& m = moves[i];
        // Which moves can possibly reach the goal, read off the check term the
        // ordering pass already computed. Mate needs a check; stalemate forbids
        // one. Testing `score < 50000` unconditionally was correct for mate and
        // skipped EVERY move under a stalemate goal, since the check term is
        // negative there -- the search then never saw a stalemate at all.
        if (mate_shortcut && last_ply_win_needs_check(s.rule_wins, b, s.attacker) &&
            !move_can_reach_goal(m.score, s.goal)) {
            continue;
        }
        ++s.stats.dfpn_mate_tests;
        const Board nb = make_move(b, m);
        if (is_goal(nb, s.goal, s.move_reserve, s.move_reserve_capacity, s.static_pseudo) ||
            variant_win_reached(nb, s.goal, s.attacker, s.rule_wins) >= 0) {
            const PnDn v{0, DFPN_INF};
            dfpn_store(s, key, v);
            ++s.stats.dfpn_proved;
            if (s.proof_hints) {
                s.attacker_proofs.store(move_hint_key(b, 'A', s.attacker, s.goal), m);
            }
            return v;
        }
    }
    if (depth <= 1) {
        const PnDn v{DFPN_INF, 0};
        dfpn_store(s, key, v);
        dfpn_publish(s, b, depth, 'A', v);
        return v;
    }

    // As on the defender side: a first visit has every child unvisited, so this
    // OR node's value is exactly (1, N) and building the children to discover
    // that is wasted. See the comment there for why estimating is sound.
    if (!dfpn_seen(s, key)) {
        const PnDn guess{1, static_cast<std::uint32_t>(
                                std::min<std::size_t>(moves.size(), DFPN_INF))};
        if (guess.pn >= thpn || guess.dn >= thdn || dfpn_budget_exhausted(s) || s.aborted) {
            dfpn_store(s, key, guess);
            dfpn_publish(s, b, depth, 'A', guess);
            return guess;
        }
    }

    std::vector<Board> child_boards;
    std::vector<TTKey> child_keys;
    child_boards.reserve(moves.size());
    child_keys.reserve(moves.size());
    for (const Move& m : moves) {
        child_boards.push_back(make_move(b, m));
        child_keys.push_back(tt_key(child_boards.back(), depth - 1, 'D', s.attacker, s.goal));
    }

    for (;;) {
        // OR node: proving needs one move proved, disproving needs all.
        std::uint32_t min_pn = DFPN_INF;
        std::uint32_t sum_dn = 0;
        std::size_t best = 0;
        std::uint32_t best_pn = DFPN_INF;
        std::uint32_t second_pn = DFPN_INF;
        for (std::size_t i = 0; i < moves.size(); ++i) {
            const PnDn c = dfpn_lookup(s, child_keys[i]);
            sum_dn = sat_add(sum_dn, c.dn);
            if (c.pn < best_pn) {
                second_pn = best_pn;
                best_pn = c.pn;
                best = i;
            } else if (c.pn < second_pn) {
                second_pn = c.pn;
            }
            min_pn = std::min(min_pn, c.pn);
        }
        const PnDn here{min_pn, sum_dn};
        if (here.pn >= thpn || here.dn >= thdn || dfpn_budget_exhausted(s) || s.aborted) {
            dfpn_store(s, key, here);
            dfpn_publish(s, b, depth, 'A', here);
            if (here.pn == 0 && s.proof_hints) {
                s.attacker_proofs.store(move_hint_key(b, 'A', s.attacker, s.goal), moves[best]);
            }
            return here;
        }
        std::uint32_t child_thpn =
            std::min<std::uint32_t>(thpn, second_pn == DFPN_INF ? DFPN_INF : second_pn + 1);
        if (s.dfpn_epsilon_64 > 0 && child_thpn < DFPN_INF) {
            const std::uint64_t widened =
                static_cast<std::uint64_t>(child_thpn) * (64 + s.dfpn_epsilon_64) / 64;
            child_thpn = std::min<std::uint32_t>(thpn, static_cast<std::uint32_t>(
                std::min<std::uint64_t>(widened, DFPN_INF)));
        }
        // thdn > here.dn, for the same reason as child_thpn above.
        const std::uint32_t child_thdn = std::min<std::uint32_t>(
            DFPN_INF, thdn - here.dn + dfpn_lookup(s, child_keys[best]).dn);
        dfpn_defender(s, child_boards[best], depth - 1, child_thpn, child_thdn);
    }
}

} // namespace mateprover

#endif // MATEPROVER_DFPN_H_INCLUDED
