// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// routes.h -- Route implementations and the single guard that decides whether a route result may be reported.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_ROUTES_H_INCLUDED
#define MATEPROVER_ROUTES_H_INCLUDED

namespace mateprover {

// PROGRESS: publish a proven lower bound, mid-search.
//
// Called once per iterative-deepening depth that COMPLETED and found nothing.
// That establishes "no solution within d" exactly and permanently, so the line
// is a result in its own right and not a status guess.
//
// Three preconditions, and all three are load-bearing:
//
//   `progress_authority`  only the unrestricted lane may say this. A restricted
//                         lane's failure means nothing (98).
//   `!aborted`            a subtree abandoned on a node budget recorded no
//                         verdict, so the depth proved nothing.
//   `!timed_out`          the same for the wall clock.
//
// The last two are the abort invariant, and getting them wrong would publish a
// FALSE theorem -- the one class of bug this engine exists to prevent. They are
// tested by asserting that a timed-out depth emits nothing, rather than by
// asserting that the bounds look plausible.
//
// stderr, so the result format on stdout is untouched and existing consumers
// need not care that this exists.
inline void publish_proven_bound(const Search& s, const Board& b, int depth) {
    if (!s.progress || !s.progress_authority || s.aborted || s.timed_out) {
        return;
    }
    std::lock_guard<std::mutex> lock(progress_stream_mutex());
    std::cerr << "progress " << fen4(b) << "; proven no solution within " << depth
              << "; acn " << s.stats.nodes << "; acs "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - s.search_start).count()
              << ";\n";
    std::cerr.flush();
}

// Men on the board. The preconditioner's material floor reads this; it is not
// on any hot path.
inline int men_on_board(const Board& b) {
    std::uint64_t occ = b.occ;
    int n = 0;
    while (occ) { occ &= occ - 1; ++n; }
    return n;
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
            // The lane's own cancel, so a worker stops when the SEARCH is
            // abandoned and not only when a sibling beats its root move.
            ws->external_cancel = s.cancel;
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
        s.iteration_depth = depth;
        // Advance the aging generation so entries that go untouched during this
        // pass become the first candidates for eviction if the table is full.
        ++s.tt.generation;
        if (shared_table) {
            shared_table->next_generation();
        }
        if (!s.keep_iter_tt) {
            s.tt.clear();
            for (const auto& ws : workers) {
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
        // The depth completed and found nothing: that is a theorem, so say so.
        publish_proven_bound(s, b, depth);
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
        order_moves(b, moves, s.score_mates, s.score_checks, s.goal, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
        moves_scored = true;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'A', s.attacker, s.goal);
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
        order_moves(b, moves, s.score_mates, s.score_checks, s.goal, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
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
            order_moves(defender_board, replies, s.score_mates, s.score_checks, s.goal, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
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

// NOT available under x-check. Both shallow provers decide a position by
// testing for mate in one and mate in two directly, and neither knows that a
// final check also ends the game -- so under the variant they would answer "no"
// to a position that is won. The caller falls back to the general route, which
// is slower and correct.
RouteResult run_shallow_fast_route(Search& s, const Board& b, int max_depth) {
    ++s.stats.shallow_fast_attempts;
    RouteResult result;
    if (variant_reachable_static(b, s.rule_wins, max_depth)) {
        ++s.stats.shallow_fast_fallbacks;
        return result;
    }
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
        // The shallow probes above already covered depths 1 and 2, so the
    // fallback starts at 3 -- or at the requested depth under
    // --direct-depth, which asks for that depth specifically.
    return run_depth_first_route_from(s, b, s.direct_depth ? max_depth : 3, max_depth);
    }
    return {};
}

// DFPN route: precondition each depth with proof-number search, then let the
// exact prover produce the answer. The exact pass is unchanged and remains the
// sole source of accepted proofs; DFPN only makes its job cheaper by having
// already settled subtrees and identified proof moves.
RouteResult run_dfpn_route(Search& s, const Board& b, int max_depth) {
    RouteResult result;
    // Root split for the DEFAULT route.
    //
    // This route had none, and it is the one almost every search takes: the
    // depth-first route owned the only call to run_root_split_depth, so
    // `--threads` on a default run allocated nothing and split nothing. A
    // twelve-hour quota-3 search at `--threads 30` ran on ONE OS thread for its
    // whole life, and section 32's "root-split parallelism contributes nothing"
    // was measured against this same route -- identical times at 1, 8 and 32
    // threads because the threads were never engaged, not because splitting
    // fails to pay.
    //
    // The exact pass below is `prove_attacker`, which is precisely what
    // run_root_split_depth parallelises, so this is a wiring change rather than
    // a new algorithm. Workers are built lazily, so a position that resolves
    // without splitting pays nothing.
    std::vector<std::unique_ptr<Search>> workers;
    std::vector<std::unique_ptr<WorkerSlot>> slots;
    std::unique_ptr<SharedProofTable> shared_table;
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
            ws->external_cancel = s.cancel;
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
    // Honour --direct-depth like the depth-first route does. This was hardcoded
    // to 1, so the flag silently did nothing here: a comparison against the
    // other routes under --direct-depth was measuring iterative deepening
    // against a direct search, which is not the same question.
    for (int depth = s.direct_depth ? max_depth : 1; depth <= max_depth; ++depth) {
        if (s.timed_out) {
            break;
        }
        s.iteration_depth = depth;
        if (!s.keep_iter_tt) {
            s.tt.clear();
            s.dfpn_tt.clear();
        }
        // The preconditioner may stop early on its own node budget; that is not
        // a verdict, so the exact pass still runs. But a wall-clock expiry ends
        // the whole search, and clearing  past it would restart a
        // search that has no time left.
        s.aborted = false;
        // A live variant win rule stands the preconditioner down. Proof numbers
        // estimate how hard the MATE is to prove and know nothing about a
        // capture or check quota, so under a quota they steer the search by a
        // measure of the wrong game. This was the whole of the missing
        // parallelism: on the x-capture bench the preconditioner was 96% of the
        // wall clock and every second of it single-threaded, which is why
        // splitting the exact pass underneath it appeared to buy nothing.
        const bool precondition = depth >= s.dfpn_min_depth &&
                                  (s.dfpn_min_men == 0 || men_on_board(b) >= s.dfpn_min_men) &&
                                  (!s.dfpn_final_depth_only || depth >= max_depth) &&
                                  (s.dfpn_under_variant || !variant_win_live(b, s.rule_wins));
        if (precondition) {
            dfpn_attacker(s, b, depth, DFPN_INF, DFPN_INF);
        }
        s.stats.dfpn_table_size = s.dfpn_tt.size();
        if (s.timed_out) {
            break;
        }
        s.aborted = false;

        // Selfmate keeps its own exact route, which has no root split of this
        // shape; everything else splits when there is more than one worker and
        // more than one ply to split.
        if (goal_is_self(s.goal)) {
            result.proof = prove_selfmate_attacker(s, b, depth);
        } else if (s.root_split && thread_count > 1 && depth > 1) {
            ensure_workers();
            if (shared_table != nullptr) {
                shared_table->import_from(s.tt);
            }
            Proof split;
            if (run_root_split_depth(s, workers, slots, b, depth, split)) {
                result.proof = std::move(split);
            }
        } else {
            result.proof = prove_attacker(s, b, depth);
        }
        if (result.proof.ok) {
            result.proved_depth = static_cast<int>((result.proof.pv.size() + 1) / 2);
            break;
        }
        if (s.timed_out) {
            break; // the depth was abandoned, so its failure is not a verdict
        }
        // The depth completed and found nothing: that is a theorem, so say so.
        publish_proven_bound(s, b, depth);
    }
    // THE SHARED TABLE'S EVICTION COUNT DIES WITH IT UNLESS IT IS FOLDED HERE.
    //
    // `shared_table` is handed to the WORKERS and never to the enclosing search,
    // so the report -- which reads `s.shared_table ? shared->evictions() :
    // s.tt.evictions` -- was reading the main search's own table, which barely
    // gets used once the root splits. It printed 0 evictions at 64 MB, 128 MB
    // and 4 GB while the node count moved from 34.6M to 22.0M with memory,
    // which cannot both be true. The instrument was pointed at the wrong object,
    // and no measurement of the replacement policy was possible until it was
    // pointed at the right one.
    if (shared_table != nullptr) {
        s.tt.evictions += shared_table->evictions();
    }
    return result;
}

// A selfmate gets its own route, deliberately plain.
//
// The first attempt reused the depth-first route and only redirected the two
// `result.proof = prove_attacker(...)` call sites. That was not enough: the
// route also reaches the exact prover through the ROOT SPLIT, which calls
// prove_attacker directly from its workers. The selfmate goal therefore ran the
// DIRECTMATE search without saying so -- 260 composed selfmates came back
// "refuted", and the one that came back solved was a position that happens to
// contain a mate in 1, certificate `{"a":"d6f6","mate":true}` and all.
//
// A silently wrong search is worse than a missing feature, so this route does
// not share machinery it cannot yet be shown to share safely: no root split,
// no preconditioner. Both are open work rather than impossibilities.
// Every root move that solves, not merely the first one.
//
// This is the composition question, and it is a different question from the one
// the rest of the engine answers. A prover stops at the first proof, because one
// proof settles "is there a mate in N". A problemist needs to know whether the
// intended key move is the ONLY one: a second solution at the root is a DUAL,
// and a directmate with one is cooked -- unsound as a composition, however
// genuine the mate. Chest computes top-level duals unconditionally for this
// reason and documents that even -U cannot suppress them.
//
// So this deliberately defeats the short-circuit the whole engine is built on,
// at the root and only at the root. Below the root the ordinary provers run
// unchanged and stop at their first proof, which is sound here: a dual in a
// sub-line does not make the KEY ambiguous, and enumerating them all would cost
// the entire search's worth of pruning to answer a question nobody asked.
//
// The cost is real and unavoidable: N root moves means N searches where the
// prover needed one, and no early exit. That is the price of the answer, not an
// inefficiency to be tuned away.
struct RootSolution {
    Move move;
    Proof proof;
};

// Why each root move that does NOT solve fails.
//
// Chest prints this by default and needs -r to suppress it, which is the right
// emphasis: a solver's answer is the key, but an ANALYST's question is usually
// "why doesn't my move work?". A refutation names the defence that survives it.
//
// The refutation reported is one surviving defence, not all of them, and not
// necessarily the most testing. One is enough to refute, and finding the others
// costs a full search per move to answer a question the analyst did not ask.
struct RootRefutation {
    Move move;
    std::string reason;   // "gives no check" style summary, or a defending move
};

std::vector<RootSolution> run_all_root_solutions(Search& s, const Board& b, int depth,
                                                 std::vector<RootRefutation>* refutations = nullptr) {
    std::vector<RootSolution> found;
    bool scored = false;
    auto moves = generate_ordered_moves(s, b, scored);
    restrict_attacker_moves(s, b, moves);

    for (const Move& m : moves) {
        if (search_cancelled(s)) {
            break;
        }
        const Board nb = make_move(b, m);
        Proof sub;
        if (goal_is_help(s.goal)) {
            // A cooperative root move is followed by 2N-1 further plies.
            sub = prove_help(s, nb, depth * 2 - 1);
        } else if (goal_is_self(s.goal)) {
            // The self- goals test their terminal at an attacker node, so the
            // root move hands straight to the defender at the SAME depth.
            sub = prove_selfmate_defender(s, nb, depth);
        } else if (is_goal(nb, s.goal, s.move_reserve, s.move_reserve_capacity, s.static_pseudo) ||
                   variant_win_reached(nb, s.goal, s.attacker, s.rule_wins) >= 0) {
            sub = Proof{true, {}, s.emit_proof
                ? (s.goal == Goal::Stalemate ? std::string("{\"stalemate\":true}")
                                             : std::string("{\"mate\":true}"))
                : std::string()};
        } else if (depth > 1) {
            sub = prove_defender(s, nb, depth - 1);
        }
        if (s.aborted) {
            // An abandoned search records no verdict, so a partial enumeration
            // must not be reported as a complete one. See the abort invariant.
            return {};
        }
        if (sub.ok) {
            std::vector<Move> pv{m};
            pv.insert(pv.end(), sub.pv.begin(), sub.pv.end());
            std::string cert;
            if (s.emit_proof) {
                cert = goal_is_help(s.goal)
                    ? "{\"h\":" + json_quote(move_uci(m)) + ",\"n\":" + sub.cert + "}"
                    : (goal_is_self(s.goal)
                        ? "{\"a\":" + json_quote(move_uci(m)) + ",\"d\":" + sub.cert + "}"
                        : (sub.cert.empty() || sub.pv.empty()
                            ? "{\"a\":" + json_quote(move_uci(m)) + ","
                              + (s.goal == Goal::Stalemate ? "\"stalemate\"" : "\"mate\"") + ":true}"
                            : "{\"a\":" + json_quote(move_uci(m)) + ",\"d\":" + sub.cert + "}"));
            }
            found.push_back(RootSolution{m, Proof{true, pv, cert}});
        } else if (refutations != nullptr) {
            // Name a defence that survives. For the adversarial goals that is a
            // defender reply the attacker cannot answer; searching each reply to
            // depth-1 finds the first such. For the cooperative goals there is
            // no defender to name, so the honest report is that the line simply
            // does not reach the goal rather than an invented refuter.
            std::string why = "no solution";
            if (!goal_is_help(s.goal) && !goal_is_self(s.goal) && depth > 1) {
                for (const Move& r : legal_moves(nb, s.move_reserve,
                                                 s.move_reserve_capacity, s.static_pseudo)) {
                    const Board rb = make_move(nb, r);
                    Proof after = prove_attacker(s, rb, depth - 1);
                    if (s.aborted) {
                        return {};
                    }
                    if (!after.ok) {
                        why = move_uci(r);
                        break;
                    }
                }
            }
            refutations->push_back(RootRefutation{m, why});
        }
    }
    return found;
}

// The cooperative route.
//
// No preconditioner, no root split, no restriction lanes -- see prove_help for
// why none of the three has a meaning without an adversary. What is left is the
// iterative-deepening frame the other routes use, which does still apply: a
// shorter cooperative line is a better answer than a longer one, and
// --direct-depth still means "search the asked length only".
//
// A help problem's length is counted in MOVES BY EACH SIDE, so `h#3` is six
// plies. Passing the move count where the prover wants plies would have
// searched half the problem and reported no solution, which reads exactly like
// a hard position rather than like a bug.
RouteResult run_help_route(Search& s, const Board& b, int max_depth) {
    RouteResult result;

    // Workers are built once and lazily, as the other routes do: a position
    // resolved without ever splitting pays nothing for threads it never uses.
    std::vector<std::unique_ptr<Search>> workers;
    std::vector<std::unique_ptr<WorkerSlot>> slots;
    std::unique_ptr<SharedProofTable> shared_table;
    // Fewer workers than threads -- unless the table is shared.
    //
    // With PRIVATE tables the split's value (cores) and the table's value
    // (transposition reuse) compete, because workers divide the budget. At
    // sixteen workers each table is a sixteenth, and the one helpmate position
    // Chest still won goes from solved to unsolved: it needs the table more than
    // the cores. Four is the compromise there.
    //
    // A SHARED table removes the competition entirely -- one table of the whole
    // budget, every worker reusing every other worker's work -- so the cap comes
    // off and the split can use what it was given.
    // Shared is the DEFAULT here, not an opt-in.
    //
    // It reaches all three helpmate positions Chest solved and this engine did
    // not -- positions previously recorded as reachable by nothing, which was
    // true only because the flag was a no-op when they were probed. A default
    // that leaves the three losses on the table to preserve a flag's opt-in
    // status is the wrong way round.
    // `shared_tt` already defaults to true (--private-tt is the opt-out), so
    // wiring it here was the whole change: the default now shares.
    const int thread_count = s.shared_tt ? std::max(1, s.threads)
                                         : std::min(4, std::max(1, s.threads));
    auto ensure_workers = [&]() {
        if (!workers.empty()) {
            return;
        }
        // --shared-tt was accepted here and did NOTHING: this route never set
        // shared_table, so every worker used a private table whatever the flag
        // said. Not a wrong answer, but a flag that silently fails to do what it
        // promises -- the same class of fault as 55 and 56.
        //
        // Sharing is what the cooperative split most wants. The split's value is
        // cores and the table's value is transposition reuse, and with private
        // tables those compete: four workers means four quarter-sized tables and
        // no reuse between them. One shared table of the whole budget gives both.
        //
        // Sound here for the same reason the private path is: `plies` is part of
        // the key, so the table's "proved within N implies proved within any
        // larger N" bound reduces to an exact match and cannot leak a result
        // across lengths.
        if (s.shared_tt) {
            shared_table.reset(new SharedProofTable(s.shared_tt_shards, s.tt_reserve,
                                                    entry_capacity_for_mb(s.memory_mb)));
        }
        workers.reserve(static_cast<std::size_t>(thread_count));
        slots.reserve(static_cast<std::size_t>(thread_count));
        for (int w = 0; w < thread_count; ++w) {
            slots.emplace_back(new WorkerSlot());
            auto ws = std::unique_ptr<Search>(new Search());
            static_cast<SearchConfig&>(*ws) = static_cast<const SearchConfig&>(s);
            ws->attacker = s.attacker;
            ws->has_deadline = s.has_deadline;
            ws->deadline = s.deadline;
            ws->cancel = &slots.back()->cancel;
            ws->external_cancel = s.cancel;
            ws->shared_table = shared_table.get();
            // Workers DIVIDE the budget rather than each taking all of it.
            //
            // A cooperative search now holds a lane-equivalent table (59), and
            // sixteen workers each claiming the whole of it is a ceiling of tens
            // of gigabytes. The tables grow lazily, so nothing fails outright --
            // it simply pages, and a run overshot its --time-limit by a factor
            // of two while the deadline checks sat behind slow memory. The
            // symptom looked like a cancellation defect and was an allocation
            // one.
            //
            // Only when the tables are PRIVATE. A shared table holds the whole
            // budget once, so there is nothing to divide and dividing would
            // shrink it for no reason.
            if (ws->shared_table == nullptr) {
                ws->memory_mb = std::max<std::size_t>(
                    1, s.memory_mb / static_cast<std::size_t>(std::max(1, thread_count)));
                ws->tt.capacity = entry_capacity_for_mb(ws->memory_mb);
            }
            workers.push_back(std::move(ws));
        }
    };

    const int start = s.direct_depth ? max_depth : 1;
    for (int depth = std::max(1, start); depth <= max_depth; ++depth) {
        // Never begin a depth whose budget has already gone. The other routes
        // are bounded by their preconditioner's own checks; this one is not, and
        // without this a cooperative run could start a fresh iteration on an
        // expired clock and hand back an answer well past its --time-limit --
        // which, in a comparison where the other engine is hard-killed at the
        // cap, is not a small unfairness.
        if (s.has_deadline && std::chrono::steady_clock::now() >= s.deadline) {
            s.timed_out = true;
            break;
        }
        Proof p;
        // Depth 1 is two plies and a flat scan; the split would cost more in
        // thread setup than it saves, exactly as on the other routes.
        if (thread_count > 1 && depth > 1) {
            ensure_workers();
            Proof split;
            if (run_help_root_split(s, workers, slots, b, depth * 2, split)) {
                p = std::move(split);
            }
        } else {
            p = prove_help(s, b, depth * 2);
        }
        if (s.aborted) {
            return result; // abandoned, so its failure is not a verdict
        }
        if (p.ok) {
            result.proof = p;
            result.proved_depth = static_cast<int>((p.pv.size() + 1) / 2);
            break;
        }
        if (s.timed_out) {
            break;
        }
    }
    return result;
}

RouteResult run_selfmate_route(Search& s, const Board& b, int max_depth) {
    RouteResult result;

    // Workers are built once for the route and lazily, exactly as the directmate
    // route does: a position resolved without ever splitting pays nothing for
    // threads it never uses.
    std::vector<std::unique_ptr<Search>> workers;
    std::vector<std::unique_ptr<WorkerSlot>> slots;
    std::unique_ptr<SharedProofTable> shared_table;
    const int thread_count = std::max(1, s.threads);
    auto ensure_workers = [&]() {
        if (!workers.empty()) {
            return;
        }
        if (s.shared_tt) {
            shared_table.reset(new SharedProofTable(s.shared_tt_shards, s.tt_reserve,
                                                    entry_capacity_for_mb(s.memory_mb)));
        }
        workers.reserve(static_cast<std::size_t>(thread_count));
        slots.reserve(static_cast<std::size_t>(thread_count));
        for (int w = 0; w < thread_count; ++w) {
            slots.emplace_back(new WorkerSlot());
            auto ws = std::unique_ptr<Search>(new Search());
            static_cast<SearchConfig&>(*ws) = static_cast<const SearchConfig&>(s);
            ws->attacker = s.attacker;
            ws->has_deadline = s.has_deadline;
            ws->deadline = s.deadline;
            ws->cancel = &slots.back()->cancel;
            // The lane's own cancel, so a worker stops when the SEARCH is
            // abandoned and not only when a sibling beats its root move.
            ws->external_cancel = s.cancel;
            ws->shared_table = shared_table.get();
            if (ws->shared_table == nullptr) {
                ws->tt.capacity = entry_capacity_for_mb(ws->memory_mb);
            }
            workers.push_back(std::move(ws));
        }
    };

    const int start = s.direct_depth ? max_depth : 1;
    for (int depth = std::max(1, start); depth <= max_depth; ) {
        // Precondition with the selfmate walker, on the same terms as the
        // directmate route: it only warms the tables and the ordering hints,
        // and the verdict below is still the exact prover's.
        if (s.route == RouteKind::Dfpn && depth >= s.dfpn_min_depth &&
            (s.dfpn_min_men == 0 || men_on_board(b) >= s.dfpn_min_men) &&
            (!s.dfpn_final_depth_only || depth >= max_depth)) {
            s.dfpn_tt.clear();
            dfpn_selfmate_attacker(s, b, depth, DFPN_INF, DFPN_INF);
            s.aborted = false;
        }
        Proof p;
        // Depth 1 is a flat scan; the split would cost more in thread setup
        // than it saves, as on the directmate route.
        if (thread_count > 1 && depth > 1) {
            ensure_workers();
            // Hand the preconditioner's ordering hints to the workers.
            //
            // They are fresh Search objects, so without this they start blind
            // and the split discards the single largest contributor this goal
            // has: hints are worth +124 positions of 200 (51). Measured before
            // the handoff, eight threads solved 9 of 60 where one thread solved
            // 37 -- parallelism making the engine four times worse, because the
            // work it parallelised was the work the hints made unnecessary.
            if (s.proof_hints) {
                for (const auto& ws : workers) {
                    ws->attacker_proofs = s.attacker_proofs;
                }
            }
            Proof split;
            if (run_selfmate_root_split(s, workers, slots, b, depth, split)) {
                p = std::move(split);
            }
        } else {
            p = prove_selfmate_attacker(s, b, depth);
        }
        if (s.aborted) {
            return result;
        }
        if (p.ok) {
            result.proof = p;
            result.proved_depth = static_cast<int>((p.pv.size() + 1) / 2);
            return result;
        }
        // LEVEL SKIPPING. A failed depth reports how much of the space it
        // actually disproved, which can exceed what was asked once the defender
        // prefers refutations that prove more; the next iteration then starts
        // past all of it rather than at depth+1. Bounded below by depth+1 so it
        // can only move forwards, and fail_depth stays at the requested depth
        // unless something genuinely established more.
        if (p.fail_depth > depth) {
            s.stats.levels_skipped += p.fail_depth - depth;
        }
        depth = (p.fail_depth > depth) ? p.fail_depth + 1 : depth + 1;
    }
    return result;
}

RouteResult run_route(Search& s, const Board& b, int max_depth) {
    // GAP-1: a root the theorems refute outright needs no search at all, and --
    // the whole point -- no iteration either. Under iterative deepening a
    // position provably unsolvable at every depth is otherwise re-searched to
    // exhaustion at depth 1, 2, 3, ... until the clock runs out, and the engine
    // reports "not found within budget" rather than "no solution".
    //
    // Inert when the gate is off: the predicate returns false immediately.
    if (position_is_refuted_axiomatically(s, b, max_depth)) {
        RouteResult refuted;
        refuted.proof.refuted = true;
        return refuted;
    }
    // A selfmate always takes the exact route, whatever --route asked for.
    //
    // DFPN's proof and disproof numbers are defined against the directmate
    // terminal -- "the defender is mated" -- and a selfmate inverts who must be
    // mated. Running it here does not merely waste effort: it publishes
    // disproofs into the exact table that answer a different question, so the
    // search silently finds nothing. That is exactly what it did before this
    // guard, while --route depth-first solved the same positions.
    if (goal_is_self(s.goal)) {
        return run_selfmate_route(s, b, max_depth);
    }
    // The same argument applies with more force to the cooperative goals: DFPN
    // measures what an adversary can force, and a helpmate has no adversary.
    if (goal_is_help(s.goal)) {
        return run_help_route(s, b, max_depth);
    }
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

} // namespace mateprover

#endif // MATEPROVER_ROUTES_H_INCLUDED
