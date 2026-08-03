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
    // Honour --direct-depth like the depth-first route does. This was hardcoded
    // to 1, so the flag silently did nothing here: a comparison against the
    // other routes under --direct-depth was measuring iterative deepening
    // against a direct search, which is not the same question.
    for (int depth = s.direct_depth ? max_depth : 1; depth <= max_depth; ++depth) {
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
        const bool precondition = depth >= s.dfpn_min_depth &&
                                  (!s.dfpn_final_depth_only || depth >= max_depth);
        if (precondition) {
            dfpn_attacker(s, b, depth, DFPN_INF, DFPN_INF);
        }
        s.stats.dfpn_table_size = s.dfpn_tt.size();
        if (s.timed_out) {
            break;
        }
        s.aborted = false;

        result.proof = goal_is_self(s.goal)
                         ? prove_selfmate_attacker(s, b, depth)
                         : prove_attacker(s, b, depth);
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
    const int start = s.direct_depth ? max_depth : 1;
    for (int depth = std::max(1, start); depth <= max_depth; ++depth) {
        Proof p = prove_help(s, b, depth * 2);
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
    for (int depth = std::max(1, start); depth <= max_depth; ++depth) {
        // Precondition with the selfmate walker, on the same terms as the
        // directmate route: it only warms the tables and the ordering hints,
        // and the verdict below is still the exact prover's.
        if (s.route == RouteKind::Dfpn && depth >= s.dfpn_min_depth &&
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
                for (auto& ws : workers) {
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
    }
    return result;
}

RouteResult run_route(Search& s, const Board& b, int max_depth) {
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
