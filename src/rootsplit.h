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

// Prove a defender node with its replies shared out.
//
// The prologue and the composition are prove_defender's, line for line, and
// they have to stay that way: this function and that one must agree on every
// observable, since either may run at this ply depending on the thread count.
// The differential test is the guarantee, not the reading.
//
// With no helpers this claims 0, 1, 2, ... in order and stops at the first
// refutation, which IS the sequential loop. That is deliberate: there is one
// code path here, exercised by every single-threaded run, rather than a fast
// path and a rarely-taken parallel one that could drift apart unnoticed.
Proof prove_defender_split(Search& s, SplitRegistry& reg, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    Proof check_end;
    if (variant_terminal(s, b, check_end)) {
        return check_end;
    }
    ++s.stats.defender_nodes;
    const TTKey key = tt_key(b, 0, 'D', s.attacker, s.goal);
    Proof exact_cached;
    if (probe_exact_proof_table(s, key, depth, exact_cached)) {
        return exact_cached;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'D', s.attacker, s.goal);
            have_hint_key = true;
        }
        return hint_key;
    };

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

    auto sp = std::make_shared<NodeSplit>();
    sp->board = b;
    sp->depth = depth;
    sp->slot = s.split_slot;
    sp->priority = s.split_priority;
    sp->conjunctive = true;
    sp->min_before_help = s.reply_split_min_proved;
    sp->lazy = lazy;
    sp->moves = std::move(replies);
    sp->results.resize(sp->moves.size());
    sp->state.assign(sp->moves.size(), kChildUnresolved);
    {
        std::lock_guard<std::mutex> lock(reg.m);
        sp->open = true;
        reg.open_splits[static_cast<std::size_t>(s.split_slot)] = sp;
        reg.cv.notify_all();
    }

    // The owner works the queue like anyone else. It never blocks while it has
    // replies left to take, so a node with no helpers costs exactly one atomic
    // claim per reply on top of the sequential loop.
    for (;;) {
        const int j = claim_child(reg, *sp);
        if (j < 0) {
            break;
        }
        run_child(reg, *sp, j, s);
        if (s.aborted) {
            break;      // this worker is unwinding; the composition will see it
        }
    }

    // Withdraw before waiting, so no further claim can be made against this
    // node, and then wait for the helpers already inside it. Their shared_ptr
    // copies keep the node alive regardless, but their ANSWERS are needed:
    // an AND node is not proved until every branch has come back.
    std::unique_lock<std::mutex> lock(reg.m);
    sp->open = false;
    reg.open_splits[static_cast<std::size_t>(s.split_slot)].reset();
    reg.cv.notify_all();
    reg.cv.wait(lock, [&] { return sp->active == 0; });

    const int n = static_cast<int>(sp->moves.size());
    const int refuted_at = sp->settled_at;
    const bool refuted_flag = sp->settle_refuted;
    const bool refutation_is_check = sp->settle_is_check;
    // Everything read out of the node from here is stable: it is closed, and
    // no claim is outstanding. Drop the lock so the composition -- which
    // builds strings -- is not done under it.
    lock.unlock();

    if (refuted_at >= 0) {
        // One reply the attacker can never win from is a permanent escape, and
        // the defender will take it. ONE witness settles the node, so replies
        // that were abandoned when this one landed do not matter -- which is
        // why the cancellation above is free rather than merely tolerable.
        if (s.fac_observer) {
            ++s.stats.fac_refuted_nodes;
            s.stats.fac_replies_before += static_cast<std::uint64_t>(refuted_at);
            if (refuted_at == 0) {
                ++s.stats.fac_first_reply_refutes;
            }
            if (refutation_is_check) {
                ++s.stats.fac_refutation_is_check;
            }
        }
        if (s.refutation_hints) {
            ++s.stats.refutation_hint_stores;
            s.defender_refutations[get_hint_key()] = sp->moves[static_cast<std::size_t>(refuted_at)];
        }
        Proof out;
        out.refuted = refuted_flag;
        store_exact_proof_table(s, key, depth, out);
        return out;
    }

    // No refutation, so the node is proved only if EVERY reply came back. One
    // abandoned branch and there is no verdict here at all -- not a proof, not
    // a disproof, and above all nothing to store.
    bool any_legal = false;
    for (int j = 0; j < n; ++j) {
        const std::uint8_t st = sp->state[static_cast<std::size_t>(j)];
        if (st == kChildProved) {
            any_legal = true;
        } else if (st != kChildIllegal) {
            s.aborted = true;
            return {};
        }
    }
    if (lazy && !any_legal) {
        // The list held pseudo-legal moves, so an exhausted loop can mean there
        // were no legal replies at all. That is stalemate, not mate.
        store_exact_proof_table(s, key, depth, {});
        return {};
    }

    std::vector<Move> representative;
    std::string cert = "[";
    bool first_cert = true;
    for (int j = 0; j < n; ++j) {
        if (sp->state[static_cast<std::size_t>(j)] != kChildProved) {
            continue;
        }
        const Move& dmove = sp->moves[static_cast<std::size_t>(j)];
        const Proof& child = sp->results[static_cast<std::size_t>(j)];
        std::vector<Move> candidate;
        candidate.push_back(dmove);
        candidate.insert(candidate.end(), child.pv.begin(), child.pv.end());
        // Strictly greater, so the FIRST longest wins -- prove_defender's rule,
        // and the reason this composition is index-ordered rather than
        // completion-ordered.
        if (candidate.size() > representative.size()) {
            representative = std::move(candidate);
        }
        if (s.emit_proof) {
            if (!first_cert) {
                cert.push_back(',');
            }
            first_cert = false;
            cert += "{\"r\":" + json_quote(move_uci(dmove)) + ",\"p\":" + child.cert + "}";
        }
    }
    if (s.emit_proof) {
        cert.push_back(']');
    } else {
        cert.clear();
    }
    Proof proof{true, representative, cert};
    store_exact_proof_table(s, key, depth, proof);
    return proof;
}

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
// Determinism is kept the same way the other splits keep it: workers claim task
// indices from a shared counter, and the LOWEST index that proves wins, whoever
// found it. So the reported line does not depend on thread timing even though
// the work does. Higher indices are cancelled once a lower one succeeds, since
// their answers can no longer be preferred.
//
// THE SPLIT IS TWO PLIES DEEP, not one, and that is the whole of its
// parallelism. Splitting on the root move alone makes as many tasks as there are
// root moves -- around thirty -- and cooperative subtrees are wildly uneven, so
// one task routinely holds most of the work and every other thread finishes
// early and idles. Measured on a six-man h#4: 32.7 s on one thread, 20.1 s on
// sixteen, a speedup of 1.6 while the same engine on an exhaustive cooperative
// workload scaled 5.9. The threads were not contending, they were starved.
//
// Pairing each root move with each reply gives roughly n^2 tasks -- hundreds
// rather than tens -- and the imbalance averages out.
//
// It also brings the answer CLOSER to the sequential one rather than further
// away. Lexicographic (first, second) order is exactly the order a sequential
// depth-first search visits these subtrees in, so the lowest-index-wins rule now
// picks the same subtree sequential search would reach first. As before, which
// line comes back from inside a subtree depends on what the shared table already
// holds, and that is not promised.
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

    // One task per (root move, reply) pair, in lexicographic order. Below four
    // plies there is no second ply worth splitting on and the task is the root
    // move itself.
    struct HelpTask {
        Move first;
        Move second;
        bool paired = false;
        Board board;
    };
    std::vector<HelpTask> tasks;
    const int rest_plies = plies >= 4 ? plies - 2 : plies - 1;
    if (plies >= 4) {
        for (const Move& first : moves) {
            const Board nb = make_move(b, first);
            auto replies = legal_moves(nb, s.move_reserve, s.move_reserve_capacity,
                                       s.static_pseudo);
            // A position with no reply cannot reach a goal that still needs
            // moves made, so it contributes no task rather than an empty one.
            if (should_order(s, replies.size())) {
                order_moves(nb, replies, s.score_mates, s.score_checks, s.goal,
                            s.fast_check_score, s.move_reserve, s.move_reserve_capacity,
                            s.static_pseudo, s.inplace_order, s.bucket_order);
            }
            for (const Move& second : replies) {
                tasks.push_back({first, second, true, make_move(nb, second)});
            }
        }
    } else {
        for (const Move& first : moves) {
            tasks.push_back({first, Move{}, false, make_move(b, first)});
        }
    }
    if (tasks.empty()) {
        return false;
    }

    const int n = static_cast<int>(tasks.size());
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

            const HelpTask& task = tasks[static_cast<std::size_t>(i)];
            Proof rest = prove_help(ws, task.board, rest_plies);
            if (ws.timed_out) {
                break;              // deadline passed: stop taking work
            }
            if (ws.aborted || !rest.ok) {
                continue;           // abandoned or refuted: no verdict recorded
            }
            Proof found;
            found.ok = true;
            found.pv.push_back(task.first);
            if (task.paired) {
                found.pv.push_back(task.second);
            }
            found.pv.insert(found.pv.end(), rest.pv.begin(), rest.pv.end());
            if (ws.emit_proof) {
                found.cert = rest.cert;
                if (task.paired) {
                    found.cert = "{\"h\":" + json_quote(move_uci(task.second))
                               + ",\"n\":" + found.cert + "}";
                }
                found.cert = "{\"h\":" + json_quote(move_uci(task.first))
                           + ",\"n\":" + found.cert + "}";
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
                if (!last_ply_win_needs_check(probe.rule_wins, b, probe.attacker) ||
                    move_can_reach_goal(moves[static_cast<std::size_t>(i)].score, probe.goal)) {
                    mate = !has_legal_move(nb, probe.move_reserve, probe.move_reserve_capacity, probe.static_pseudo);
                }
            } else {
                mate = is_goal(nb, probe.goal, probe.move_reserve, probe.move_reserve_capacity, probe.static_pseudo) ||
                       variant_win_reached(nb, probe.goal, probe.attacker, probe.rule_wins) >= 0;
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

    // ONE REGISTRY SLOT PER WORKER, not per root move. A worker publishes at
    // most one split at a time, which is what keeps the arrangement flat: an
    // owner waiting for its helpers is never itself a helper, so no owner can
    // ever be waiting on another owner and the classical YBWC blocked-owner
    // question does not arise here.
    SplitRegistry registry;
    registry.open_splits.resize(static_cast<std::size_t>(worker_count));
    registry.best_index = &best_index;
    // Every worker that has claimed a root move and not yet finished it. A
    // helper that finds no work parks until this reaches zero, because until
    // then an owner may still be about to publish.
    registry.live_roots = 0;
    const bool split_replies = s.reply_split && worker_count > 1 && depth > 1;
    const bool split_moves = s.or_split && worker_count > 1 && depth > 1;
    const bool any_split = split_replies || split_moves;

    for (int w = 0; w < worker_count; ++w) {
        slots[static_cast<std::size_t>(w)]->current_root.store(n, std::memory_order_relaxed);
        slots[static_cast<std::size_t>(w)]->cancel.store(false, std::memory_order_relaxed);
        Search& ws = *workers[static_cast<std::size_t>(w)];
        // `iteration_depth` is how prove_attacker recognises which plies are
        // near enough the root to be worth splitting; workers copy the config
        // but not the per-pass state, so it is set here rather than inherited.
        ws.iteration_depth = depth;
        ws.split_registry = split_moves ? &registry : nullptr;
        ws.split_slot = w;
    }

    auto worker_body = [&](int w) {
        Search& ws = *workers[static_cast<std::size_t>(w)];
        WorkerSlot& slot = *slots[static_cast<std::size_t>(w)];
        for (;;) {
            int i = next_index.fetch_add(1, std::memory_order_relaxed);
            if (i >= n) {
                // Out of root moves. This is the thread that used to stop here
                // and idle for the rest of the search, and it is the whole of
                // the 4.75x ceiling. Go and help whoever is still working.
                if (any_split) {
                    help_splits(registry, ws, slot.current_root, slot.cancel);
                }
                break;
            }
            if (i > best_index.load(std::memory_order_acquire)) {
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
            // Indices are claimed from a shared counter, so these arrive out of
            // order and interleaved across workers. That is honest reporting: it
            // is what the search is actually doing.
            publish_root_move(ws, b, depth, i + 1, n, moves[static_cast<std::size_t>(i)]);
            // Re-read after publishing our index: this closes the window where
            // a finishing worker scanned the slots before we announced this
            // move and so did not cancel us.
            if (i > best_index.load(std::memory_order_acquire)) {
                continue;
            }
            LiveRootGuard live(registry);
            ws.split_priority = i;

            Board nb = make_move(b, moves[static_cast<std::size_t>(i)]);
            bool mate = false;
            if (shortcut) {
                if (!last_ply_win_needs_check(ws.rule_wins, b, ws.attacker) ||
                    move_can_reach_goal(moves[static_cast<std::size_t>(i)].score, ws.goal)) {
                    mate = !has_legal_move(nb, ws.move_reserve, ws.move_reserve_capacity, ws.static_pseudo);
                }
            } else {
                mate = is_goal(nb, ws.goal, ws.move_reserve, ws.move_reserve_capacity, ws.static_pseudo) ||
                       variant_win_reached(nb, ws.goal, ws.attacker, ws.rule_wins) >= 0;
            }

            Proof found;
            if (mate) {
                found.ok = true;
                found.pv.push_back(moves[static_cast<std::size_t>(i)]);
                if (ws.emit_proof) {
                    found.cert = "{\"a\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)])) + (s.goal == Goal::Stalemate ? ",\"stalemate\":true}" : ",\"mate\":true}");
                }
            } else if (depth > 1) {
                Proof all_replies = split_replies
                                        ? prove_defender_split(ws, registry, nb, depth - 1)
                                        : prove_defender(ws, nb, depth - 1);
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
