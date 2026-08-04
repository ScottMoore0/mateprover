// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// rootsplit.h -- Root-split parallel search and the worker coordination it needs.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_ROOTSPLIT_H_INCLUDED
#define MATEPROVER_ROOTSPLIT_H_INCLUDED

namespace mateprover {

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
// Root split for the selfmate goal.
//
// A separate function rather than a branch inside the directmate splitter. That
// splitter's body tests whether the DEFENDER is mated after the attacker's move
// and then calls prove_defender; both are wrong here, and the last time this
// goal borrowed directmate machinery it ran the directmate search in silence
// (50). The parts worth sharing -- worker construction, the shared table, the
// atomic claim counter, lowest-index acceptance -- are shared; the body is not.
//
// Lowest-index acceptance is what keeps the answer deterministic: whichever
// worker finishes first, the proof reported is the one from the lowest root
// index that proved, so the result does not depend on thread timing.
bool run_selfmate_root_split(Search& s, std::vector<std::unique_ptr<Search>>& workers,
                             std::vector<std::unique_ptr<WorkerSlot>>& slots,
                             const Board& b, int depth, Proof& out) {
    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    if (moves.empty()) {
        return false;
    }
    if (should_order(s, moves.size())) {
        order_moves(b, moves, s.score_mates, s.score_checks, s.goal, s.fast_check_score,
                    s.move_reserve, s.move_reserve_capacity, s.static_pseudo,
                    s.inplace_order, s.bucket_order);
    }
    restrict_attacker_moves(s, b, moves);
    if (moves.empty()) {
        return false;               // no permitted move: no proof here, not a disproof
    }
    if (s.proof_hints) {
        const TTKey hint_key = move_hint_key(b, 'A', s.attacker, s.goal);
        if (auto hint = s.attacker_proofs.find(hint_key); hint != s.attacker_proofs.end()) {
            move_to_front(moves, hint->second);
        }
    }

    const int n = static_cast<int>(moves.size());
    const int worker_count = std::min<int>(static_cast<int>(workers.size()), n);
    if (worker_count <= 0) {
        return false;
    }

    std::atomic<int> next_index{0};
    std::atomic<int> best_index{n};
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
            // Stop taking new root moves once the enclosing search is
            // abandoned. Without this the split ran to its deadline, which is
            // the run's time limit, so a portfolio lane that had already lost
            // the race still held the whole process until the limit expired.
            if (ws.external_cancel != nullptr &&
                ws.external_cancel->load(std::memory_order_relaxed)) {
                break;
            }
            slot.current_root.store(i, std::memory_order_release);
            slot.cancel.store(false, std::memory_order_release);
            ws.aborted = false;
            if (i > best_index.load(std::memory_order_acquire)) {
                continue;
            }

            const Board nb = make_move(b, moves[static_cast<std::size_t>(i)]);
            Proof replies = prove_selfmate_defender(ws, nb, depth);
            if (ws.timed_out) {
                break;              // deadline passed: stop taking work
            }
            if (ws.aborted) {
                continue;           // abandoned: no verdict, nothing recorded
            }
            if (!replies.ok) {
                continue;
            }
            Proof found;
            found.ok = true;
            found.pv.push_back(moves[static_cast<std::size_t>(i)]);
            found.pv.insert(found.pv.end(), replies.pv.begin(), replies.pv.end());
            if (ws.emit_proof) {
                found.cert = "{\"a\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)]))
                           + ",\"d\":" + replies.cert + "}";
            }

            std::lock_guard<std::mutex> lock(result_mutex);
            results[static_cast<std::size_t>(i)] = std::move(found);
            int prev = best_index.load(std::memory_order_acquire);
            while (i < prev && !best_index.compare_exchange_weak(prev, i, std::memory_order_acq_rel)) {
            }
            const int best = best_index.load(std::memory_order_acquire);
            for (const auto& other : slots) {
                if (other->current_root.load(std::memory_order_acquire) > best) {
                    other->cancel.store(true, std::memory_order_release);
                }
            }
        }
        slot.current_root.store(n, std::memory_order_release);
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(std::max(0, worker_count - 1)));
    for (int w = 1; w < worker_count; ++w) {
        // A refused thread must not escape: coverage is unaffected because root
        // moves are claimed from a shared counter, so worker 0 alone still
        // visits every index. Only speed is lost, never a verdict.
        try {
            pool.emplace_back(worker_body, w);
        } catch (const std::system_error&) {
            break;
        }
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

// The cooperative root split.
//
// A help node is a pure disjunction: any root move whose continuation reaches
// the goal answers the whole question, and nothing has to hold against a reply.
// That makes this the simplest split in the program -- no defender layer, no
// conjunction to keep intact -- and it is the last search here to get one, which
// is why helpmate was the one goal Chest still won.
//
// Determinism is kept the same way the other splits keep it: workers claim root
// indices from a shared counter, and the LOWEST index that proves wins, whoever
// found it. So the reported line does not depend on thread timing even though
// the work does. Higher indices are cancelled once a lower one succeeds, since
// their answers can no longer be preferred.
bool run_help_root_split(Search& s, std::vector<std::unique_ptr<Search>>& workers,
                         std::vector<std::unique_ptr<WorkerSlot>>& slots,
                         const Board& b, int plies, Proof& out) {
    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    if (moves.empty() || plies <= 1) {
        return false;
    }
    if (should_order(s, moves.size())) {
        order_moves(b, moves, s.score_mates, s.score_checks, s.goal, s.fast_check_score,
                    s.move_reserve, s.move_reserve_capacity, s.static_pseudo,
                    s.inplace_order, s.bucket_order);
    }
    // No restriction. Both sides are helping, so removing a mover's options
    // removes solutions rather than pruning an adversary (58).

    const int n = static_cast<int>(moves.size());
    const int worker_count = std::min<int>(static_cast<int>(workers.size()), n);
    if (worker_count <= 0) {
        return false;
    }

    std::atomic<int> next_index{0};
    std::atomic<int> best_index{n};
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
            if (ws.external_cancel != nullptr &&
                ws.external_cancel->load(std::memory_order_relaxed)) {
                break;
            }
            slot.current_root.store(i, std::memory_order_release);
            slot.cancel.store(false, std::memory_order_release);
            ws.aborted = false;
            if (i > best_index.load(std::memory_order_acquire)) {
                continue;
            }

            const Board nb = make_move(b, moves[static_cast<std::size_t>(i)]);
            Proof rest = prove_help(ws, nb, plies - 1);
            if (ws.timed_out) {
                break;              // deadline passed: stop taking work
            }
            if (ws.aborted || !rest.ok) {
                continue;           // abandoned or refuted: no verdict recorded
            }
            Proof found;
            found.ok = true;
            found.pv.push_back(moves[static_cast<std::size_t>(i)]);
            found.pv.insert(found.pv.end(), rest.pv.begin(), rest.pv.end());
            if (ws.emit_proof) {
                found.cert = "{\"h\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)]))
                           + ",\"n\":" + rest.cert + "}";
            }

            std::lock_guard<std::mutex> lock(result_mutex);
            results[static_cast<std::size_t>(i)] = std::move(found);
            int prev = best_index.load(std::memory_order_acquire);
            while (i < prev && !best_index.compare_exchange_weak(prev, i, std::memory_order_acq_rel)) {
            }
            const int best = best_index.load(std::memory_order_acquire);
            for (const auto& other : slots) {
                if (other->current_root.load(std::memory_order_acquire) > best) {
                    other->cancel.store(true, std::memory_order_release);
                }
            }
        }
        slot.current_root.store(n, std::memory_order_release);
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(std::max(0, worker_count - 1)));
    for (int w = 1; w < worker_count; ++w) {
        try {
            pool.emplace_back(worker_body, w);
        } catch (const std::system_error&) {
            break;      // coverage is unaffected: worker 0 still visits every index
        }
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

bool run_root_split_depth(Search& s, std::vector<std::unique_ptr<Search>>& workers,
                          std::vector<std::unique_ptr<WorkerSlot>>& slots,
                          const Board& b, int depth, Proof& out) {
    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    if (moves.empty()) {
        return false;
    }
    bool moves_scored = false;
    if (should_order(s, moves.size())) {
        order_moves(b, moves, s.score_mates, s.score_checks, s.goal, s.fast_check_score,
                    s.move_reserve, s.move_reserve_capacity, s.static_pseudo,
                    s.inplace_order, s.bucket_order);
        moves_scored = true;
    }
    restrict_attacker_moves(s, b, moves);
    if (moves.empty()) {
        // A restriction can remove every attacker move, which the empty check
        // above cannot see because it runs before the restriction is applied.
        // Without this, n is 0, worker_count is 0, and the thread-pool reserve
        // below computes worker_count - 1 as an unsigned -1: a reserve of
        // SIZE_MAX, throwing std::length_error out of a portfolio lane and
        // terminating the process mid-batch.
        //
        // No permitted attacker move means no mate under this restriction. That
        // is a legitimate "no proof here", not a disproof of the unrestricted
        // problem -- restricted lanes never settle disproofs.
        return false;
    }
    if (s.proof_hints) {
        TTKey hint_key = move_hint_key(b, 'A', s.attacker, s.goal);
        if (auto hint = s.attacker_proofs.find(hint_key); hint != s.attacker_proofs.end()) {
            move_to_front(moves, hint->second);
        }
    }
    // Mate goal only; see the note in prove.h.
    const bool shortcut = s.ordered_check_shortcut && moves_scored && s.score_checks && !s.score_mates && s.goal == Goal::Mate;

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
            // Goal-aware, and it was not. This tested is_checkmate whatever the
            // goal while the certificate beside it already said "stalemate",
            // so a root-split stalemate search would report a CHECKMATE as
            // `sm 1` -- a false proof. Unreachable until a depth-first lane
            // was added for that goal, and caught immediately by the test that
            // a mate-in-1 must not be accepted as a stalemate.
            bool mate = false;
            if (shortcut) {
                if (move_can_reach_goal(moves[static_cast<std::size_t>(i)].score, probe.goal)) {
                    mate = !has_legal_move(nb, probe.move_reserve, probe.move_reserve_capacity, probe.static_pseudo);
                }
            } else {
                mate = is_goal(nb, probe.goal, probe.move_reserve, probe.move_reserve_capacity, probe.static_pseudo);
            }

            Proof found;
            if (mate) {
                found.ok = true;
                found.pv.push_back(moves[static_cast<std::size_t>(i)]);
                if (probe.emit_proof) {
                    found.cert = "{\"a\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)])) + (s.goal == Goal::Stalemate ? ",\"stalemate\":true}" : ",\"mate\":true}");
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
            // Stop taking new root moves once the enclosing search is
            // abandoned. Without this the split ran to its deadline, which is
            // the run's time limit, so a portfolio lane that had already lost
            // the race still held the whole process until the limit expired.
            if (ws.external_cancel != nullptr &&
                ws.external_cancel->load(std::memory_order_relaxed)) {
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
                if (move_can_reach_goal(moves[static_cast<std::size_t>(i)].score, ws.goal)) {
                    mate = !has_legal_move(nb, ws.move_reserve, ws.move_reserve_capacity, ws.static_pseudo);
                }
            } else {
                mate = is_goal(nb, ws.goal, ws.move_reserve, ws.move_reserve_capacity, ws.static_pseudo);
            }

            Proof found;
            if (mate) {
                found.ok = true;
                found.pv.push_back(moves[static_cast<std::size_t>(i)]);
                if (ws.emit_proof) {
                    found.cert = "{\"a\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)])) + (s.goal == Goal::Stalemate ? ",\"stalemate\":true}" : ",\"mate\":true}");
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
                for (const auto& other : slots) {
                    if (other->current_root.load(std::memory_order_acquire) > best) {
                        other->cancel.store(true, std::memory_order_release);
                    }
                }
            }
        }
        slot.current_root.store(n, std::memory_order_release);
    };

    std::vector<std::thread> pool;
    // max(): a zero worker count would make this reserve SIZE_MAX and throw.
    pool.reserve(static_cast<std::size_t>(std::max(0, worker_count - 1)));
    for (int w = 1; w < worker_count; ++w) {
        // The OS can refuse a thread, and does under churn: always-split plus a
        // short time limit spawns and joins a full set per depth per position,
        // and Windows starts failing. An escaping std::system_error would
        // destroy the already-started threads unjoined, which calls
        // std::terminate -- the observed crash was "terminate called
        // recursively", mid-batch, losing every remaining position.
        //
        // Running with fewer workers is sound, not merely convenient: root
        // moves are claimed from a shared atomic counter rather than statically
        // partitioned, so any surviving worker still covers every index and
        // worker 0 alone would cover all of them. Coverage cannot be lost, only
        // speed, so this can never turn into a false disproof.
        try {
            pool.emplace_back(worker_body, w);
        } catch (const std::system_error&) {
            break;
        }
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

} // namespace mateprover

#endif // MATEPROVER_ROOTSPLIT_H_INCLUDED
