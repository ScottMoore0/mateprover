// search.h -- The exact proof kernel, DFPN preconditioner, routes, and parallel search.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by echest.cpp; see docs/E_CHEST_ARCHITECTURE.md.

#ifndef ECHEST_SEARCH_H_INCLUDED
#define ECHEST_SEARCH_H_INCLUDED

namespace echest {

Proof prove_attacker(Search& s, const Board& b, int depth);

Proof prove_defender(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.defender_nodes;
    // Depth is not part of the key; it is supplied to probe/store instead.
    TTKey key = tt_key(b, 0, 'D', s.attacker);
    Proof exact_cached;
    if (probe_exact_proof_table(s, key, depth, exact_cached)) {
        return exact_cached;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'D', s.attacker);
            have_hint_key = true;
        }
        return hint_key;
    };

    // Lazy defender generation is only sound when ordering does not depend on
    // building the child board, which rules out --score-mates.
    const bool lazy = s.lazy_defender && !s.static_pseudo && !s.score_mates;
    bool replies_scored = false;
    auto replies = lazy ? pseudo_defender_moves(s, b)
                        : generate_ordered_moves(s, b, replies_scored);
    (void)replies_scored;
    ++s.stats.defender_move_lists;
    if (!lazy) {
        s.stats.defender_moves += replies.size();
    }
    if (replies.empty()) {
        store_exact_proof_table(s, key, depth, {});
        return {};
    }

    if (s.refutation_hints) {
        const TTKey& refutation_key = get_hint_key();
        ++s.stats.refutation_hint_probes;
        if (auto hint = s.defender_refutations.find(refutation_key); hint != s.defender_refutations.end()) {
            if (move_to_front(replies, hint->second)) {
                ++s.stats.refutation_hint_hits;
            }
        }
    }
    std::vector<Move> representative;
    std::vector<std::string> branch_certs;
    if (s.emit_proof) {
        branch_certs.reserve(replies.size());
    }
    bool any_legal = false;
    for (const Move& dmove : replies) {
        Board nb = make_move(b, dmove);
        if (lazy) {
            if (in_check(nb, other(nb.stm))) {
                ++s.stats.defender_lazy_skipped;
                continue; // pseudo-legal only: the defender left their king attacked
            }
            ++s.stats.defender_moves;
        }
        any_legal = true;
        ++s.stats.defender_replies_tried;
        Proof child = prove_attacker(s, nb, depth);
        if (s.aborted) {
            return {};
        }
        if (!child.ok) {
            ++s.stats.defender_refutations;
            if (s.debug) {
                std::cerr << "defender_refutes depth=" << depth << " move=" << move_uci(dmove)
                          << " fen=" << fen4(nb) << "\n";
            }
            if (s.refutation_hints) {
                ++s.stats.refutation_hint_stores;
                s.defender_refutations[get_hint_key()] = dmove;
            }
            store_exact_proof_table(s, key, depth, {});
            return {};
        }
        std::vector<Move> candidate;
        candidate.push_back(dmove);
        candidate.insert(candidate.end(), child.pv.begin(), child.pv.end());
        if (candidate.size() > representative.size()) {
            representative = std::move(candidate);
        }
        if (s.emit_proof) {
            branch_certs.push_back("{\"r\":" + json_quote(move_uci(dmove)) + ",\"p\":" + child.cert + "}");
        }
    }
    // With lazy generation the list held pseudo-legal moves, so an exhausted
    // loop does not by itself mean every legal reply was refuted -- it may mean
    // there were none. That is stalemate, not mate, and must be treated exactly
    // like the empty-list case above rather than reported as a proof.
    if (lazy && !any_legal) {
        store_exact_proof_table(s, key, depth, {});
        return {};
    }
    std::string cert = "[";
    if (s.emit_proof) {
        for (std::size_t i = 0; i < branch_certs.size(); ++i) {
            if (i) cert.push_back(',');
            cert += branch_certs[i];
        }
        cert.push_back(']');
    } else {
        cert.clear();
    }
    Proof proof{true, representative, cert};
    store_exact_proof_table(s, key, depth, proof);
    return proof;
}

Proof prove_attacker(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (depth <= 0 || b.stm != s.attacker) {
        return {};
    }
    // Depth is not part of the key; it is supplied to probe/store instead.
    TTKey key = tt_key(b, 0, 'A', s.attacker);
    Proof exact_cached;
    if (probe_exact_proof_table(s, key, depth, exact_cached)) {
        return exact_cached;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'A', s.attacker);
            have_hint_key = true;
        }
        return hint_key;
    };

    bool moves_scored = false;
    auto moves = generate_ordered_moves(s, b, moves_scored);
    restrict_attacker_moves(s, b, moves);
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();
    if (s.proof_hints) {
        const TTKey& proof_key = get_hint_key();
        ++s.stats.proof_hint_probes;
        if (auto hint = s.attacker_proofs.find(proof_key); hint != s.attacker_proofs.end()) {
            if (move_to_front(moves, hint->second)) {
                ++s.stats.proof_hint_hits;
            }
        }
    }
    const bool can_use_ordered_check_shortcut = s.ordered_check_shortcut && moves_scored && s.score_checks && !s.score_mates;
    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        Board nb = make_move(b, amove);
        ++s.stats.immediate_mate_tests;
        bool mate = false;
        if (can_use_ordered_check_shortcut) {
            ++s.stats.ordered_check_shortcut_uses;
            if (amove.score >= 50000) {
                ++s.stats.ordered_check_shortcut_checks;
                mate = !has_legal_move(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
            } else {
                ++s.stats.ordered_check_shortcut_skips;
            }
        } else {
            mate = is_checkmate(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
        }
        if (mate) {
            ++s.stats.immediate_mates;
            std::vector<Move> pv{amove};
            std::string cert;
            if (s.emit_proof) {
                cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"mate\":true}";
            }
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs[get_hint_key()] = amove;
            }
            Proof proof{true, pv, cert};
            store_exact_proof_table(s, key, depth, proof);
            return proof;
        }
        if (s.debug && depth == 1 && in_check(nb, nb.stm)) {
            auto replies = legal_moves(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
            std::cerr << "mate1_candidate_not_mate move=" << move_uci(amove)
                      << " defender_legal=" << replies.size()
                      << " fen=" << fen4(nb) << "\n";
            if (replies.size() <= 4) {
                std::cerr << "  replies:";
                for (const Move& r : replies) {
                    Board rb = make_move(nb, r);
                    Color moved = other(rb.stm);
                    int ksq = king_square(rb, moved);
                    std::cerr << ' ' << move_uci(r)
                              << "(k=" << (ksq >= 0 ? sq_name(ksq) : "none")
                              << ",chk=" << (in_check(rb, moved) ? "1" : "0")
                              << ",fen=" << fen4(rb);
                    if (ksq >= 0) {
                        std::cerr << ",left=";
                        for (int ff = file_of(ksq) - 1; ff >= 0; --ff) {
                            int rsq = square_of(ff, rank_of(ksq));
                            std::cerr << sq_name(rsq) << rb.sq[rsq];
                        }
                    }
                    std::cerr << ")";
                }
                std::cerr << "\n";
            }
        }
        if (depth > 1) {
            Proof all_replies = prove_defender(s, nb, depth - 1);
            if (s.aborted) {
                return {};
            }
            if (all_replies.ok) {
                std::vector<Move> pv{amove};
                pv.insert(pv.end(), all_replies.pv.begin(), all_replies.pv.end());
                std::string cert;
                if (s.emit_proof) {
                    cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"d\":" + all_replies.cert + "}";
                }
                if (s.proof_hints) {
                    ++s.stats.proof_hint_stores;
                    s.attacker_proofs[get_hint_key()] = amove;
                }
                Proof proof{true, pv, cert};
                store_exact_proof_table(s, key, depth, proof);
                return proof;
            }
            if (s.debug) {
                std::cerr << "attacker_move_failed depth=" << depth << " move=" << move_uci(amove)
                          << " fen=" << fen4(nb) << "\n";
            }
        }
    }
    store_exact_proof_table(s, key, depth, {});
    return {};
}

// One worker's coordination slot. `current_root` is the root move index the
// worker is presently proving; `cancel` is the flag its Search polls.
struct WorkerSlot {
    std::atomic<int> current_root{0};
    std::atomic<bool> cancel{false};
};

// Prove one depth by splitting the root attacker moves across workers.
//
// Workers claim root indices from a shared counter and prove their move in a
// private Search with a private table. The accepted answer is the successful
// move with the LOWEST root index, which is precisely the move the sequential
// attacker loop would have returned -- so splitting never changes which key
// move is reported, only how fast it is found.
//
// A worker whose index can no longer win (a lower index already succeeded) is
// cancelled and unwinds without recording a verdict, so an abandoned subtree
// is never mistaken for a disproof.
bool run_root_split_depth(Search& s, std::vector<std::unique_ptr<Search>>& workers,
                          std::vector<std::unique_ptr<WorkerSlot>>& slots,
                          const Board& b, int depth, Proof& out) {
    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    if (moves.empty()) {
        return false;
    }
    bool moves_scored = false;
    if (should_order(s, moves.size())) {
        order_moves(b, moves, s.score_mates, s.score_checks, s.fast_check_score,
                    s.move_reserve, s.move_reserve_capacity, s.static_pseudo,
                    s.inplace_order, s.bucket_order);
        moves_scored = true;
    }
    restrict_attacker_moves(s, b, moves);
    if (s.proof_hints) {
        TTKey hint_key = move_hint_key(b, 'A', s.attacker);
        if (auto hint = s.attacker_proofs.find(hint_key); hint != s.attacker_proofs.end()) {
            move_to_front(moves, hint->second);
        }
    }
    const bool shortcut = s.ordered_check_shortcut && moves_scored && s.score_checks && !s.score_mates;

    const int n = static_cast<int>(moves.size());
    const int worker_count = std::min<int>(static_cast<int>(workers.size()), n);

    // Young-brothers-wait at the root.
    //
    // The accepted answer is the lowest-index successful root move, so every
    // node spent on a higher index is discarded the moment a lower one
    // succeeds. Move ordering is good enough that the first root move is
    // frequently the proof, which makes an immediate full split maximally
    // wasteful: on the deep corpus, 8 threads explored 2.2x the sequential
    // node count.
    //
    // So try the first few moves sequentially. If one proves, the split never
    // happens and no work is wasted at all. If none does, the shared table is
    // warm before the workers start, which also cuts their duplication.
    const int seq_first = std::max(0, std::min(s.root_sequential_first, n));
    {
        Search& probe = *workers[0]; // already points at the shared table
        for (int i = 0; i < seq_first; ++i) {
            probe.aborted = false;
            slots[0]->cancel.store(false, std::memory_order_release);
            slots[0]->current_root.store(i, std::memory_order_release);
            ++probe.stats.root_sequential_tried;

            const Board nb = make_move(b, moves[static_cast<std::size_t>(i)]);
            bool mate = false;
            if (shortcut) {
                if (moves[static_cast<std::size_t>(i)].score >= 50000) {
                    mate = !has_legal_move(nb, probe.move_reserve, probe.move_reserve_capacity, probe.static_pseudo);
                }
            } else {
                mate = is_checkmate(nb, probe.move_reserve, probe.move_reserve_capacity, probe.static_pseudo);
            }

            Proof found;
            if (mate) {
                found.ok = true;
                found.pv.push_back(moves[static_cast<std::size_t>(i)]);
                if (probe.emit_proof) {
                    found.cert = "{\"a\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)])) + ",\"mate\":true}";
                }
            } else if (depth > 1) {
                Proof all_replies = prove_defender(probe, nb, depth - 1);
                if (!probe.aborted && all_replies.ok) {
                    found.ok = true;
                    found.pv.push_back(moves[static_cast<std::size_t>(i)]);
                    found.pv.insert(found.pv.end(), all_replies.pv.begin(), all_replies.pv.end());
                    if (probe.emit_proof) {
                        found.cert = "{\"a\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)]))
                                   + ",\"d\":" + all_replies.cert + "}";
                    }
                }
            }
            if (found.ok) {
                ++probe.stats.root_sequential_hits;
                s.stats += probe.stats;
                probe.stats = Stats{};
                out = std::move(found);
                return true;
            }
        }
    }

    std::atomic<int> next_index{seq_first};
    std::atomic<int> best_index{n}; // lowest root index proved so far
    std::mutex result_mutex;
    std::vector<Proof> results(static_cast<std::size_t>(n));

    for (int w = 0; w < worker_count; ++w) {
        slots[static_cast<std::size_t>(w)]->current_root.store(n, std::memory_order_relaxed);
        slots[static_cast<std::size_t>(w)]->cancel.store(false, std::memory_order_relaxed);
    }

    auto worker_body = [&](int w) {
        Search& ws = *workers[static_cast<std::size_t>(w)];
        WorkerSlot& slot = *slots[static_cast<std::size_t>(w)];
        for (;;) {
            int i = next_index.fetch_add(1, std::memory_order_relaxed);
            if (i >= n || i > best_index.load(std::memory_order_acquire)) {
                break;
            }
            slot.current_root.store(i, std::memory_order_release);
            slot.cancel.store(false, std::memory_order_release);
            ws.aborted = false;
            // Re-read after publishing our index: this closes the window where
            // a finishing worker scanned the slots before we announced this
            // move and so did not cancel us.
            if (i > best_index.load(std::memory_order_acquire)) {
                continue;
            }

            Board nb = make_move(b, moves[static_cast<std::size_t>(i)]);
            bool mate = false;
            if (shortcut) {
                if (moves[static_cast<std::size_t>(i)].score >= 50000) {
                    mate = !has_legal_move(nb, ws.move_reserve, ws.move_reserve_capacity, ws.static_pseudo);
                }
            } else {
                mate = is_checkmate(nb, ws.move_reserve, ws.move_reserve_capacity, ws.static_pseudo);
            }

            Proof found;
            if (mate) {
                found.ok = true;
                found.pv.push_back(moves[static_cast<std::size_t>(i)]);
                if (ws.emit_proof) {
                    found.cert = "{\"a\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)])) + ",\"mate\":true}";
                }
            } else if (depth > 1) {
                Proof all_replies = prove_defender(ws, nb, depth - 1);
                if (ws.aborted) {
                    continue; // abandoned: no verdict, nothing recorded
                }
                if (all_replies.ok) {
                    found.ok = true;
                    found.pv.push_back(moves[static_cast<std::size_t>(i)]);
                    found.pv.insert(found.pv.end(), all_replies.pv.begin(), all_replies.pv.end());
                    if (ws.emit_proof) {
                        found.cert = "{\"a\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)]))
                                   + ",\"d\":" + all_replies.cert + "}";
                    }
                }
            }

            if (found.ok) {
                std::lock_guard<std::mutex> lock(result_mutex);
                results[static_cast<std::size_t>(i)] = std::move(found);
                int prev = best_index.load(std::memory_order_acquire);
                while (i < prev && !best_index.compare_exchange_weak(prev, i, std::memory_order_acq_rel)) {
                }
                const int best = best_index.load(std::memory_order_acquire);
                for (auto& other : slots) {
                    if (other->current_root.load(std::memory_order_acquire) > best) {
                        other->cancel.store(true, std::memory_order_release);
                    }
                }
            }
        }
        slot.current_root.store(n, std::memory_order_release);
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(worker_count - 1));
    for (int w = 1; w < worker_count; ++w) {
        pool.emplace_back(worker_body, w);
    }
    worker_body(0);
    for (std::thread& t : pool) {
        t.join();
    }

    for (int w = 0; w < worker_count; ++w) {
        s.stats += workers[static_cast<std::size_t>(w)]->stats;
        workers[static_cast<std::size_t>(w)]->stats = Stats{};
        workers[static_cast<std::size_t>(w)]->aborted = false;
        if (workers[static_cast<std::size_t>(w)]->timed_out) {
            s.timed_out = true;
        }
    }

    const int best = best_index.load(std::memory_order_acquire);
    if (best < n && results[static_cast<std::size_t>(best)].ok) {
        out = std::move(results[static_cast<std::size_t>(best)]);
        return true;
    }
    return false;
}

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

bool dfpn_budget_exhausted(Search& s) {
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
        const std::uint32_t child_thpn =
            thpn >= here.pn ? std::min<std::uint32_t>(DFPN_INF, thpn - here.pn + dfpn_lookup(s, child_keys[best]).pn)
                            : 0;
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
        const std::uint32_t child_thdn =
            thdn >= here.dn ? std::min<std::uint32_t>(DFPN_INF, thdn - here.dn + dfpn_lookup(s, child_keys[best]).dn)
                            : 0;
        dfpn_defender(s, child_boards[best], depth - 1, child_thpn, child_thdn);
    }
}

RouteResult run_depth_first_route_from(Search& s, const Board& b, int start_depth, int max_depth) {
    RouteResult result;
    start_depth = std::max(1, start_depth);

    // Workers are built once for the whole route so their tables survive
    // across iterative-deepening passes exactly as the sequential table does,
    // and lazily so that positions resolved without ever splitting -- shallow
    // mates and quickly refuted no-mate controls -- pay nothing for threads
    // they never use.
    std::vector<std::unique_ptr<Search>> workers;
    std::vector<std::unique_ptr<WorkerSlot>> slots;
    std::unique_ptr<SharedProofTable> shared_table;
    bool prelude_imported = false;
    const int thread_count = std::max(1, s.threads);
    auto ensure_workers = [&]() {
        if (!workers.empty()) {
            return;
        }
        if (s.shared_tt) {
            shared_table.reset(new SharedProofTable(s.shared_tt_shards, s.tt_reserve, entry_capacity_for_mb(s.memory_mb)));
        }
        workers.reserve(static_cast<std::size_t>(thread_count));
        slots.reserve(static_cast<std::size_t>(thread_count));
        for (int w = 0; w < thread_count; ++w) {
            slots.emplace_back(new WorkerSlot());
            auto ws = std::unique_ptr<Search>(new Search());
            static_cast<SearchConfig&>(*ws) = static_cast<const SearchConfig&>(s);
            ws->has_deadline = s.has_deadline;
            ws->deadline = s.deadline;
            ws->cancel = &slots.back()->cancel;
            ws->shared_table = shared_table.get();
            if (ws->shared_table == nullptr) {
                ws->tt.capacity = entry_capacity_for_mb(ws->memory_mb);
                if (ws->tt_reserve > 0) {
                    ws->tt.map.reserve(ws->tt_reserve);
                }
            }
            workers.push_back(std::move(ws));
        }
    };

    for (int depth = start_depth; depth <= max_depth; ++depth) {
        if (s.timed_out) {
            break;
        }
        // Advance the aging generation so entries that go untouched during this
        // pass become the first candidates for eviction if the table is full.
        ++s.tt.generation;
        if (shared_table) {
            shared_table->next_generation();
        }
        if (!s.keep_iter_tt) {
            s.tt.clear();
            for (auto& ws : workers) {
                ws->tt.clear();
            }
            if (shared_table) {
                shared_table->clear();
            }
        }
        // Depth 1 is a flat scan for immediate mates; the split would cost more
        // in thread setup than it saves.
        const bool splittable = thread_count > 1 && depth > 1;

        // Parallel cost gate.
        //
        // Thread setup is pure overhead on work that was going to finish in
        // microseconds, but cost is not knowable in advance, and a gate that
        // only looks at completed depths is useless here: search cost grows
        // exponentially with depth, so by the time a shallow depth proves the
        // position expensive, the expensive depth is the one already running.
        //
        // So probe instead of predict. Run the depth sequentially under a node
        // ceiling; if it blows the ceiling the position is expensive by
        // definition, and the depth is re-run split. The probe is not wasted
        // work: exceeding the ceiling is an abort, which by the abort
        // invariant records no verdict but leaves every genuinely completed
        // subtree in the table, and that table is handed to the workers.
        bool escalate = false;
        if (splittable && s.parallel_min_nodes > 0 && s.stats.nodes < s.parallel_min_nodes) {
            s.node_budget = s.parallel_min_nodes;
            s.aborted = false;
            Proof probe = prove_attacker(s, b, depth);
            s.node_budget = 0;
            if (s.aborted) {
                s.aborted = false;
                escalate = true;
            } else {
                result.proof = std::move(probe);
            }
        } else {
            escalate = splittable;
        }

        if (escalate) {
            ensure_workers();
            if (shared_table && !prelude_imported) {
                shared_table->import_from(s.tt);
                prelude_imported = true;
            }
            Proof proof;
            if (run_root_split_depth(s, workers, slots, b, depth, proof)) {
                result.proof = std::move(proof);
            }
        } else if (!splittable) {
            result.proof = prove_attacker(s, b, depth);
        }
        if (result.proof.ok) {
            result.proved_depth = static_cast<int>((result.proof.pv.size() + 1) / 2);
            break;
        }
        if (s.timed_out) {
            break; // the depth was abandoned, so its failure is not a verdict
        }
    }
    return result;
}

RouteResult run_depth_first_route(Search& s, const Board& b, int max_depth) {
    // Iterative deepening exists to find the SHORTEST mate and to warm the
    // table for the next pass. When the caller only needs "a mate within N",
    // the shallower passes are optional. Whether they pay for themselves is an
    // empirical question, so this is a flag rather than an assumption.
    return run_depth_first_route_from(s, b, s.direct_depth ? max_depth : 1, max_depth);
}

Proof prove_shallow_mate1(Search& s, const Board& b) {
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (b.stm != s.attacker) {
        return {};
    }

    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();
    bool moves_scored = false;
    if (should_order(s, moves.size())) {
        ++s.stats.order_calls;
        s.stats.order_moves += moves.size();
        order_moves(b, moves, s.score_mates, s.score_checks, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
        moves_scored = true;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'A', s.attacker);
            have_hint_key = true;
        }
        return hint_key;
    };
    if (s.proof_hints) {
        const TTKey& proof_key = get_hint_key();
        ++s.stats.proof_hint_probes;
        if (auto hint = s.attacker_proofs.find(proof_key); hint != s.attacker_proofs.end()) {
            if (move_to_front(moves, hint->second)) {
                ++s.stats.proof_hint_hits;
            }
        }
    }

    const bool can_use_ordered_check_shortcut = s.ordered_check_shortcut && moves_scored && s.score_checks && !s.score_mates;
    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        Board nb = make_move(b, amove);
        ++s.stats.immediate_mate_tests;
        bool mate = false;
        if (can_use_ordered_check_shortcut) {
            ++s.stats.ordered_check_shortcut_uses;
            if (amove.score >= 50000) {
                ++s.stats.ordered_check_shortcut_checks;
                mate = !has_legal_move(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
            } else {
                ++s.stats.ordered_check_shortcut_skips;
            }
        } else {
            mate = is_checkmate(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
        }
        if (mate) {
            ++s.stats.immediate_mates;
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs[get_hint_key()] = amove;
            }
            std::string cert;
            if (s.emit_proof) {
                cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"mate\":true}";
            }
            return {true, {amove}, cert};
        }
    }
    return {};
}

Proof prove_shallow_mate2(Search& s, const Board& b) {
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (b.stm != s.attacker) {
        return {};
    }

    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();
    if (should_order(s, moves.size())) {
        ++s.stats.order_calls;
        s.stats.order_moves += moves.size();
        order_moves(b, moves, s.score_mates, s.score_checks, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
    }

    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        Board defender_board = make_move(b, amove);
        ++s.stats.nodes;
        ++s.stats.defender_nodes;
        auto replies = legal_moves(defender_board, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
        ++s.stats.defender_move_lists;
        s.stats.defender_moves += replies.size();
        if (replies.empty()) {
            continue;
        }
        if (should_order(s, replies.size())) {
            ++s.stats.order_calls;
            s.stats.order_moves += replies.size();
            order_moves(defender_board, replies, s.score_mates, s.score_checks, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
        }

        std::vector<Move> representative;
        std::vector<std::string> branch_certs;
        if (s.emit_proof) {
            branch_certs.reserve(replies.size());
        }
        bool all_replies_mate = true;
        for (const Move& dmove : replies) {
            ++s.stats.defender_replies_tried;
            Board attacker_board = make_move(defender_board, dmove);
            Proof child = prove_shallow_mate1(s, attacker_board);
            if (!child.ok) {
                ++s.stats.defender_refutations;
                all_replies_mate = false;
                break;
            }
            std::vector<Move> candidate;
            candidate.push_back(dmove);
            candidate.insert(candidate.end(), child.pv.begin(), child.pv.end());
            if (candidate.size() > representative.size()) {
                representative = std::move(candidate);
            }
            if (s.emit_proof) {
                branch_certs.push_back("{\"r\":" + json_quote(move_uci(dmove)) + ",\"p\":" + child.cert + "}");
            }
        }
        if (all_replies_mate) {
            std::vector<Move> pv{amove};
            pv.insert(pv.end(), representative.begin(), representative.end());
            std::string cert;
            if (s.emit_proof) {
                cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"d\":[";
                for (std::size_t i = 0; i < branch_certs.size(); ++i) {
                    if (i) cert.push_back(',');
                    cert += branch_certs[i];
                }
                cert += "]}";
            }
            return {true, pv, cert};
        }
    }
    return {};
}

RouteResult run_shallow_fast_route(Search& s, const Board& b, int max_depth) {
    ++s.stats.shallow_fast_attempts;
    RouteResult result;
    if (max_depth >= 1) {
        result.proof = prove_shallow_mate1(s, b);
        if (result.proof.ok) {
            ++s.stats.shallow_fast_hits;
            result.proved_depth = 1;
            return result;
        }
    }
    if (max_depth >= 2) {
        result.proof = prove_shallow_mate2(s, b);
        if (result.proof.ok) {
            ++s.stats.shallow_fast_hits;
            result.proved_depth = static_cast<int>((result.proof.pv.size() + 1) / 2);
            return result;
        }
    }
    if (max_depth > 2) {
        ++s.stats.shallow_fast_fallbacks;
        return run_depth_first_route_from(s, b, 3, max_depth);
    }
    return {};
}

// DFPN route: precondition each depth with proof-number search, then let the
// exact prover produce the answer. The exact pass is unchanged and remains the
// sole source of accepted proofs; DFPN only makes its job cheaper by having
// already settled subtrees and identified proof moves.
RouteResult run_dfpn_route(Search& s, const Board& b, int max_depth) {
    RouteResult result;
    for (int depth = 1; depth <= max_depth; ++depth) {
        if (s.timed_out) {
            break;
        }
        if (!s.keep_iter_tt) {
            s.tt.clear();
            s.dfpn_tt.clear();
        }
        // The preconditioner may stop early on its own node budget; that is not
        // a verdict, so the exact pass still runs. But a wall-clock expiry ends
        // the whole search, and clearing  past it would restart a
        // search that has no time left.
        s.aborted = false;
        dfpn_attacker(s, b, depth, DFPN_INF, DFPN_INF);
        s.stats.dfpn_table_size = s.dfpn_tt.size();
        if (s.timed_out) {
            break;
        }
        s.aborted = false;

        result.proof = prove_attacker(s, b, depth);
        if (result.proof.ok) {
            result.proved_depth = static_cast<int>((result.proof.pv.size() + 1) / 2);
            break;
        }
        if (s.timed_out) {
            break; // the depth was abandoned, so its failure is not a verdict
        }
    }
    return result;
}

RouteResult run_route(Search& s, const Board& b, int max_depth) {
    switch (s.route) {
        case RouteKind::DepthFirst:
            return run_depth_first_route(s, b, max_depth);
        case RouteKind::ShallowFast:
            return run_shallow_fast_route(s, b, max_depth);
        case RouteKind::Dfpn:
            return run_dfpn_route(s, b, max_depth);
    }
    return {};
}

bool route_result_is_acceptable(const RouteResult& result, int max_depth) {
    if (!result.proof.ok || result.proof.pv.empty()) {
        return false;
    }
    const int pv_depth = static_cast<int>((result.proof.pv.size() + 1) / 2);
    return result.proved_depth == pv_depth && result.proved_depth > 0 && result.proved_depth <= max_depth;
}

// Perft: count leaf nodes of the legal move tree to a fixed depth.
//
// This is the engine's self-contained move-generation gate. Directmate proofs
// are only as trustworthy as legality, and perft against published reference
// counts exercises castling rights, en-passant capture and expiry, promotion,
// and pinned-piece legality far more thoroughly than the mate suites do -- a
// movegen bug usually shows up as a wrong perft number long before it shows up
// as a wrong mate.
std::uint64_t perft(const Board& b, int depth) {
    if (depth <= 0) {
        return 1;
    }
    auto moves = legal_moves(b);
    if (depth == 1) {
        return moves.size();
    }
    std::uint64_t total = 0;
    for (const Move& m : moves) {
        total += perft(make_move(b, m), depth - 1);
    }
    return total;
}

// Per-root-move perft breakdown: the standard tool for bisecting a movegen or
// make/unmake discrepancy against a reference implementation.
void perft_divide_line(const std::string& raw, int depth) {
    std::string line = trim(raw);
    if (line.empty()) {
        line = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; perft error input;\n";
        return;
    }
    const Board b = *parsed;
    auto moves = legal_moves(b);
    std::vector<std::pair<std::string, std::uint64_t>> rows;
    rows.reserve(moves.size());
    std::uint64_t total = 0;
    for (const Move& m : moves) {
        const std::uint64_t n = perft(make_move(b, m), depth - 1);
        rows.emplace_back(move_uci(m), n);
        total += n;
    }
    std::sort(rows.begin(), rows.end());
    for (const auto& row : rows) {
        std::cout << row.first << ' ' << row.second << '\n';
    }
    std::cout << "total " << total << '\n';
}

void perft_line(const std::string& raw, int depth) {
    std::string line = trim(raw);
    if (line.empty()) {
        line = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; perft error input;\n";
        return;
    }
    const Board b = *parsed;
    for (int d = 1; d <= depth; ++d) {
        const auto start = std::chrono::steady_clock::now();
        const std::uint64_t nodes = perft(b, d);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << fen4(b) << "; perft " << d << "; nodes " << nodes << "; acs " << seconds << ";\n";
    }
}

int infer_mate_depth(const std::string& line) {
    auto pos = line.find('#');
    if (pos == std::string::npos) {
        return 0;
    }
    int value = 0;
    for (++pos; pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])); ++pos) {
        value = value * 10 + (line[pos] - '0');
    }
    return value;
}

std::string pv_uci(const std::vector<Move>& pv) {
    std::ostringstream out;
    for (std::size_t i = 0; i < pv.size(); ++i) {
        if (i) out << ' ';
        out << move_uci(pv[i]);
    }
    return out.str();
}

void emit_profile_line(const Board& b, const Search& s, int requested_depth, int proved_depth, double seconds) {
    const Stats& st = s.stats;
    std::cerr << "% e_profile {"
              << "\"fen\":" << json_quote(fen4(b))
              << ",\"route\":" << json_quote(route_name(s.route))
              << ",\"requested_depth\":" << requested_depth
              << ",\"proved_depth\":" << proved_depth
              << ",\"seconds\":" << seconds
              << ",\"nodes\":" << st.nodes
              << ",\"attacker_nodes\":" << st.attacker_nodes
              << ",\"defender_nodes\":" << st.defender_nodes
              << ",\"tt_probes\":" << st.tt_probes
              << ",\"tt_hits\":" << st.tt_hits
              << ",\"tt_stores\":" << st.tt_stores
              << ",\"tt_size\":" << (s.shared_table != nullptr ? s.shared_table->size() : s.tt.size())
              << ",\"tt_capacity\":" << s.tt.capacity
              << ",\"tt_evictions\":" << (s.shared_table != nullptr ? s.shared_table->evictions() : s.tt.evictions)
              << ",\"memory_mb\":" << s.memory_mb
              << ",\"exact_tt_proof_hits\":" << st.exact_tt_proof_hits
              << ",\"exact_tt_disproof_hits\":" << st.exact_tt_disproof_hits
              << ",\"exact_tt_proof_stores\":" << st.exact_tt_proof_stores
              << ",\"exact_tt_disproof_stores\":" << st.exact_tt_disproof_stores
              << ",\"shallow_fast_attempts\":" << st.shallow_fast_attempts
              << ",\"shallow_fast_hits\":" << st.shallow_fast_hits
              << ",\"shallow_fast_fallbacks\":" << st.shallow_fast_fallbacks
              << ",\"attacker_move_lists\":" << st.attacker_move_lists
              << ",\"attacker_moves\":" << st.attacker_moves
              << ",\"attacker_candidates\":" << st.attacker_candidates
              << ",\"defender_move_lists\":" << st.defender_move_lists
              << ",\"defender_moves\":" << st.defender_moves
              << ",\"defender_replies_tried\":" << st.defender_replies_tried
              << ",\"defender_pseudo_moves\":" << st.defender_pseudo_moves
              << ",\"defender_lazy_skipped\":" << st.defender_lazy_skipped
              << ",\"dfpn_nodes\":" << st.dfpn_nodes
              << ",\"dfpn_proved\":" << st.dfpn_proved
              << ",\"dfpn_disproved\":" << st.dfpn_disproved
              << ",\"dfpn_table_size\":" << st.dfpn_table_size
              << ",\"root_sequential_tried\":" << st.root_sequential_tried
              << ",\"root_sequential_hits\":" << st.root_sequential_hits
              << ",\"dfpn_movegen\":" << st.dfpn_movegen
              << ",\"dfpn_mate_tests\":" << st.dfpn_mate_tests
              << ",\"timed_out\":" << (s.timed_out ? "true" : "false")
              << ",\"lazy_defender\":" << (s.lazy_defender ? "true" : "false")
              << ",\"order_calls\":" << st.order_calls
              << ",\"order_moves\":" << st.order_moves
              << ",\"immediate_mate_tests\":" << st.immediate_mate_tests
              << ",\"ordered_check_shortcut_uses\":" << st.ordered_check_shortcut_uses
              << ",\"ordered_check_shortcut_checks\":" << st.ordered_check_shortcut_checks
              << ",\"ordered_check_shortcut_skips\":" << st.ordered_check_shortcut_skips
              << ",\"immediate_mates\":" << st.immediate_mates
              << ",\"refutation_hint_probes\":" << st.refutation_hint_probes
              << ",\"refutation_hint_hits\":" << st.refutation_hint_hits
              << ",\"refutation_hint_stores\":" << st.refutation_hint_stores
              << ",\"proof_hint_probes\":" << st.proof_hint_probes
              << ",\"proof_hint_hits\":" << st.proof_hint_hits
              << ",\"proof_hint_stores\":" << st.proof_hint_stores
              << ",\"route_rejections\":" << st.route_rejections
              << ",\"defender_refutations\":" << st.defender_refutations
              << ",\"move_reserve\":" << (s.move_reserve ? "true" : "false")
              << ",\"move_reserve_capacity\":" << s.move_reserve_capacity
              << ",\"inplace_order\":" << (s.inplace_order ? "true" : "false")
              << ",\"bucket_order\":" << (s.bucket_order ? "true" : "false")
              << ",\"static_pseudo\":" << (s.static_pseudo ? "true" : "false")
              << ",\"order_min_size\":" << s.order_min_size
              << ",\"refutation_hints\":" << (s.refutation_hints ? "true" : "false")
              << ",\"proof_hints\":" << (s.proof_hints ? "true" : "false")
              << ",\"keep_iter_tt\":" << (s.keep_iter_tt ? "true" : "false")
              << ",\"ordered_check_shortcut\":" << (s.ordered_check_shortcut ? "true" : "false")
              << "}\n";
}

void list_legal_line(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; legal_count 0; error input;\n";
        return;
    }
    Board b = *parsed;
    auto moves = legal_moves(b);
    std::vector<std::string> uci;
    uci.reserve(moves.size());
    for (const Move& move : moves) {
        uci.push_back(move_uci(move));
    }
    std::sort(uci.begin(), uci.end());
    std::cout << fen4(b) << "; legal_count " << uci.size() << "; legal";
    for (const std::string& move : uci) {
        std::cout << ' ' << move;
    }
    std::cout << ";\n";
}

void solve_line(const std::string& raw, int requested_depth, const SearchConfig& config) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; acn 0; acs 0; error input;\n";
        return;
    }
    Board b = *parsed;
    int max_depth = requested_depth > 0 ? requested_depth : infer_mate_depth(line);
    if (max_depth <= 0) {
        max_depth = 1;
    }

    Search s;
    static_cast<SearchConfig&>(s) = config;
    s.attacker = b.stm;
    s.order_min_size = std::max<std::size_t>(2, config.order_min_size);
    if (s.time_limit > 0.0) {
        s.has_deadline = true;
        s.deadline = std::chrono::steady_clock::now() +
                     std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                         std::chrono::duration<double>(s.time_limit));
    }
    s.tt.capacity = entry_capacity_for_mb(s.memory_mb);
    if (s.tt_reserve > 0) {
        s.tt.map.reserve(s.tt_reserve);
    }
    auto start = std::chrono::steady_clock::now();

    RouteResult route_result = run_route(s, b, max_depth);
    const Proof& proof = route_result.proof;
    const bool accepted = route_result_is_acceptable(route_result, max_depth);
    if (!accepted && route_result.proof.ok) {
        ++s.stats.route_rejections;
    }
    int proved_depth = accepted ? route_result.proved_depth : 0;

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << fen4(b) << "; acn " << s.stats.nodes << "; acs " << seconds;
    if (accepted) {
        std::cout << "; bm " << move_uci(proof.pv.front())
                  << "; dm " << proved_depth
                  << "; pv " << pv_uci(proof.pv);
        if (s.emit_proof && !proof.cert.empty()) {
            std::cout << "; proof " << proof.cert;
        }
    }
    if (!accepted && s.timed_out) {
        // Distinguish "gave up" from "proved there is no mate". Without this a
        // released tool would report the same thing for both.
        std::cout << "; timeout";
    }
    std::cout << ";\n";
    if (s.profile) {
        emit_profile_line(b, s, requested_depth, proved_depth, seconds);
    }
}

} // namespace echest

#endif // ECHEST_SEARCH_H_INCLUDED
