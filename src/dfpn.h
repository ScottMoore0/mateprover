// dfpn.h -- Native DFPN preconditioner. A preconditioner only, never an output authority.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by echest.cpp; see docs/E_CHEST_ARCHITECTURE.md.

#ifndef ECHEST_DFPN_H_INCLUDED
#define ECHEST_DFPN_H_INCLUDED

namespace echest {

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
        store_exact_proof_table(s, tt_key(b, 0, kind, s.attacker), depth, {});
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
    const TTKey key = tt_key(b, 0, 'D', s.attacker);

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
        child_keys.push_back(tt_key(child_boards.back(), depth, 'A', s.attacker));
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
    const TTKey key = tt_key(b, 0, 'A', s.attacker);

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

    // Immediate-mate scan, reusing the child boards that the selection loop
    // below also needs, so each child is built exactly once per node visit.
    std::vector<Board> child_boards;
    child_boards.reserve(moves.size());
    for (const Move& m : moves) {
        child_boards.push_back(make_move(b, m));
    }
    // A move that does not give check cannot be mate, and the fused generator
    // already computed that bit, so most children never need the full
    // checkmate test.
    const bool mate_shortcut = scored && s.score_checks && !s.score_mates;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const Board& nb = child_boards[i];
        const Move& m = moves[i];
        if (mate_shortcut && m.score < 50000) {
            continue;
        }
        ++s.stats.dfpn_mate_tests;
        if (is_checkmate(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo)) {
            const PnDn v{0, DFPN_INF};
            dfpn_store(s, key, v);
            ++s.stats.dfpn_proved;
            if (s.proof_hints) {
                s.attacker_proofs[move_hint_key(b, 'A', s.attacker)] = m;
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

    std::vector<TTKey> child_keys;
    child_keys.reserve(moves.size());
    for (const Board& nb : child_boards) {
        child_keys.push_back(tt_key(nb, depth - 1, 'D', s.attacker));
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
                s.attacker_proofs[move_hint_key(b, 'A', s.attacker)] = moves[best];
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

} // namespace echest

#endif // ECHEST_DFPN_H_INCLUDED
