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

// ---------------------------------------------------------------------------
// The second ply of the split.
//
// Root splitting alone stops paying at 4.75x however many threads are added,
// and the reason is arithmetic rather than mechanical: one root move owns about
// 21% of the tree, 1/0.21 = 4.76, and at the tail of the search one worker is
// grinding through that move while every other worker has run out of root
// indices to claim. Adding threads adds idlers.
//
// A defender node is the right place to put them, and it is the ONLY place in
// an AND/OR search where extra threads are not speculative. It is a
// conjunction: the attacker must reach the goal after EVERY reply, so every
// reply has to be proved whatever order they are taken in. Nothing a helper
// computes is wasted unless some reply REFUTES the node, and a refutation ends
// the node for every worker at once.
//
// The contract this structure exists to keep is stronger than the root split's.
// The root split is allowed to change the reported line (100), because a worker
// may walk into a subtree a sibling proved. This one is not allowed to change
// ANYTHING: the composition below reads the per-reply results back in index
// order, so the branch certificates, the representative PV and the refuting
// reply are all identical to what the sequential loop in prove_defender would
// have produced. That is what `test_reply_split_is_bit_identical` checks, and
// it is what makes this safe to default on -- a mistake here would forge a
// proof rather than crash, so "the output does not move" is the only gate
// worth having.
enum : std::uint8_t {
    kReplyUnresolved = 0,
    kReplyProved = 1,
    kReplyRefuted = 2,
    kReplyIllegal = 3,      // lazy generation: pseudo-legal move, king left en prise
    kReplyAbandoned = 4,    // cancelled or out of time: NO verdict, by the abort invariant
};

// One defender node opened up for helpers. Every mutable field is guarded by
// the registry mutex; the critical sections are a few instructions each and are
// entered once per reply SUBTREE, so the lock is never on a hot path.
struct ReplySplit {
    Board board{};              // position after the attacker's root move
    int depth = 0;              // depth handed to prove_attacker for each reply
    int root_index = 0;
    bool lazy = false;          // replies are pseudo-legal and need filtering
    std::vector<Move> replies;
    std::vector<Proof> results;
    std::vector<std::uint8_t> state;
    int next = 0;               // lowest unclaimed reply
    int active = 0;             // claims in flight, owner's included
    int refuted_at = -1;        // LOWEST refuting reply, which is the one the
                                // sequential loop would have stopped at
    bool refuted_flag = false;  // that child's GAP-1 `refuted`
    bool refuted_is_check = false;  // fatal-anti-check observer only
    bool open = false;
};

// Where owners publish and helpers look. One entry per root move index, so a
// helper can prefer the lowest -- the same priority the sequential search has.
struct SplitRegistry {
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::shared_ptr<ReplySplit>> open_splits;
    int live_roots = 0;         // workers still inside a root move
    const std::atomic<int>* best_index = nullptr;
    // How many replies the owner must have proved before helpers may join.
    // See claim_any_reply: this is what separates a node that needs all its
    // replies from one that is about to die on the next one.
    int min_replies_before_help = 2;
};

// Counts a worker in for as long as it is inside a root move. A helper that
// finds no work parks rather than leaving while this is non-zero, because an
// owner that has not yet finished generating its replies has nothing to offer
// yet and will in a moment.
//
// A guard rather than a pair of calls: the root loop leaves its body by
// `continue` from three places, and an undercount here would send every helper
// home at the exact moment the tail work appears.
struct LiveRootGuard {
    SplitRegistry& reg;
    explicit LiveRootGuard(SplitRegistry& r) : reg(r) {
        std::lock_guard<std::mutex> lock(reg.m);
        ++reg.live_roots;
    }
    ~LiveRootGuard() {
        std::lock_guard<std::mutex> lock(reg.m);
        --reg.live_roots;
        reg.cv.notify_all();
    }
    LiveRootGuard(const LiveRootGuard&) = delete;
    LiveRootGuard& operator=(const LiveRootGuard&) = delete;
};

// A claim: which node, and which of its replies. The shared_ptr is what makes
// the lifetime safe -- an owner may withdraw its split and return while a
// helper is still deep inside a reply, and the helper's copy keeps the node
// alive until it has somewhere to write the answer.
struct ReplyClaim {
    std::shared_ptr<ReplySplit> split;
    int index = -1;
};

// Take the next reply of `sp`, or -1 if there is none left to take. Returns -1
// once the node is settled, so a refutation stops new work immediately.
int claim_reply(SplitRegistry& reg, ReplySplit& sp) {
    std::lock_guard<std::mutex> lock(reg.m);
    if (!sp.open || sp.refuted_at >= 0 || sp.next >= static_cast<int>(sp.replies.size())) {
        return -1;
    }
    const int j = sp.next++;
    ++sp.active;
    return j;
}

// Find work for an idle worker: the lowest-indexed open node with a reply left.
// Lowest first because that is the order the sequential search would reach
// these nodes in, so helpers spend their effort where it is most likely to
// still matter.
ReplyClaim claim_any_reply(SplitRegistry& reg) {
    std::lock_guard<std::mutex> lock(reg.m);
    const int best = reg.best_index != nullptr
                         ? reg.best_index->load(std::memory_order_acquire)
                         : std::numeric_limits<int>::max();
    for (std::shared_ptr<ReplySplit>& sp : reg.open_splits) {
        if (!sp) {
            continue;
        }
        // A root move a lower index has already beaten cannot be the answer,
        // so helping it is pure waste.
        if (sp->root_index > best) {
            continue;
        }
        if (!sp->open || sp->refuted_at >= 0 ||
            sp->next >= static_cast<int>(sp->replies.size())) {
            continue;
        }
        // THE GATE, and the whole difference between this mechanism paying and
        // costing 2.7x the nodes.
        //
        // "Every reply must be proved, so no helper's work is speculative" is
        // true of a defender node that ends up PROVED and false of one that
        // ends up refuted -- and on a no-solution position every node is
        // refuted. Such a node early-exits at its first refuting reply, which
        // the reply ordering and the refutation-hint table between them make
        // the FIRST reply most of the time, so a sequential node costs one
        // subtree and helpers were charging in to prove twenty more that
        // sequential would never have looked at.
        //
        // The owner has already resolved replies 0..next-1 without refuting,
        // since a refutation closes the node. So `next` is direct evidence
        // about which kind of node this is: past a couple of proved replies it
        // is very likely to need all of them, and before that it is very likely
        // to die on the next one.
        if (sp->next < reg.min_replies_before_help) {
            continue;
        }
        ReplyClaim claim;
        claim.index = sp->next++;
        ++sp->active;
        claim.split = sp;
        return claim;
    }
    return {};
}

// Prove one reply and record the outcome. Runs on whichever worker claimed it;
// `ws` is that worker's own Search, so its node counts land in its own Stats
// and are folded back with everyone else's at the end of the split.
//
// The three outcomes are exactly prove_defender's, in the same order and with
// the same precedence. ABANDONED FIRST: a search that gave up has proved
// nothing, and reading its empty result as a refutation is the single mistake
// that would turn this into a false proof.
void run_reply(SplitRegistry& reg, ReplySplit& sp, int j, Search& ws) {
    const Move& dmove = sp.replies[static_cast<std::size_t>(j)];
    const Board nb = make_move(sp.board, dmove);
    std::uint8_t st = kReplyAbandoned;
    Proof child;
    bool refuted_flag = false;
    bool is_check = false;

    ++ws.stats.reply_split_claims;
    if (sp.lazy && in_check(nb, other(nb.stm))) {
        ++ws.stats.defender_lazy_skipped;
        st = kReplyIllegal;
    } else {
        if (sp.lazy) {
            ++ws.stats.defender_moves;
        }
        ++ws.stats.defender_replies_tried;
        child = prove_attacker(ws, nb, sp.depth);
        if (ws.aborted) {
            st = kReplyAbandoned;
        } else if (!child.ok) {
            st = kReplyRefuted;
            refuted_flag = child.refuted;
            ++ws.stats.defender_refutations;
            if (ws.fac_observer) {
                is_check = in_check(nb, other(sp.board.stm));
            }
            if (ws.debug) {
                std::cerr << "defender_refutes depth=" << sp.depth << " move=" << move_uci(dmove)
                          << " fen=" << fen4(nb) << "\n";
            }
        } else {
            st = kReplyProved;
        }
    }

    std::lock_guard<std::mutex> lock(reg.m);
    sp.state[static_cast<std::size_t>(j)] = st;
    if (st == kReplyProved) {
        sp.results[static_cast<std::size_t>(j)] = std::move(child);
    } else if (st == kReplyRefuted && (sp.refuted_at < 0 || j < sp.refuted_at)) {
        sp.refuted_at = j;
        sp.refuted_flag = refuted_flag;
        sp.refuted_is_check = is_check;
    }
    --sp.active;
    reg.cv.notify_all();
}

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
Proof prove_defender_split(Search& s, SplitRegistry& reg, const Board& b, int depth,
                           int root_index) {
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

    auto sp = std::make_shared<ReplySplit>();
    sp->board = b;
    sp->depth = depth;
    sp->root_index = root_index;
    sp->lazy = lazy;
    sp->replies = std::move(replies);
    sp->results.resize(sp->replies.size());
    sp->state.assign(sp->replies.size(), kReplyUnresolved);
    {
        std::lock_guard<std::mutex> lock(reg.m);
        sp->open = true;
        reg.open_splits[static_cast<std::size_t>(root_index)] = sp;
        reg.cv.notify_all();
    }

    // The owner works the queue like anyone else. It never blocks while it has
    // replies left to take, so a node with no helpers costs exactly one atomic
    // claim per reply on top of the sequential loop.
    for (;;) {
        const int j = claim_reply(reg, *sp);
        if (j < 0) {
            break;
        }
        run_reply(reg, *sp, j, s);
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
    reg.open_splits[static_cast<std::size_t>(root_index)].reset();
    reg.cv.notify_all();
    reg.cv.wait(lock, [&] { return sp->active == 0; });

    const int n = static_cast<int>(sp->replies.size());
    const int refuted_at = sp->refuted_at;
    const bool refuted_flag = sp->refuted_flag;
    const bool refutation_is_check = sp->refuted_is_check;
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
            s.defender_refutations[get_hint_key()] = sp->replies[static_cast<std::size_t>(refuted_at)];
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
        if (st == kReplyProved) {
            any_legal = true;
        } else if (st != kReplyIllegal) {
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
        if (sp->state[static_cast<std::size_t>(j)] != kReplyProved) {
            continue;
        }
        const Move& dmove = sp->replies[static_cast<std::size_t>(j)];
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

// An idle worker's loop: take replies from whatever nodes are still open until
// there is nothing left anywhere. Called only when the ROOT queue is empty, so
// this thread has no work of its own to displace and cannot be the owner of a
// node someone is waiting on -- which is also why the waiting above can never
// deadlock.
void help_reply_splits(SplitRegistry& reg, Search& ws, WorkerSlot& slot) {
    for (;;) {
        if (ws.timed_out ||
            (ws.external_cancel != nullptr &&
             ws.external_cancel->load(std::memory_order_relaxed))) {
            return;
        }
        ReplyClaim claim = claim_any_reply(reg);
        if (claim.index < 0) {
            // Nothing to take. If every root move is finished there never will
            // be; otherwise an owner is still generating its replies and this
            // waits to be told. The timeout is a backstop, not the mechanism.
            std::unique_lock<std::mutex> lock(reg.m);
            if (reg.live_roots == 0) {
                return;
            }
            reg.cv.wait_for(lock, std::chrono::milliseconds(2));
            continue;
        }
        // Announce which root move this thread is now serving, so the root
        // split's existing cancellation reaches helpers too: when a lower root
        // index proves, everyone above it is told to stop, owners and helpers
        // alike. Order matters -- publish, then clear, then re-check -- which
        // closes the same window the root loop closes.
        slot.current_root.store(claim.split->root_index, std::memory_order_release);
        slot.cancel.store(false, std::memory_order_release);
        ws.aborted = false;
        ++ws.stats.reply_split_helped;
        run_reply(reg, *claim.split, claim.index, ws);
        slot.current_root.store(std::numeric_limits<int>::max(), std::memory_order_release);
    }
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

    SplitRegistry registry;
    registry.open_splits.resize(static_cast<std::size_t>(n));
    registry.best_index = &best_index;
    // Every worker that has claimed a root move and not yet finished it. A
    // helper that finds no work parks until this reaches zero, because until
    // then an owner may still be about to publish.
    registry.live_roots = 0;
    registry.min_replies_before_help = s.reply_split_min_proved;
    const bool split_replies = s.reply_split && worker_count > 1 && depth > 1;

    for (int w = 0; w < worker_count; ++w) {
        slots[static_cast<std::size_t>(w)]->current_root.store(n, std::memory_order_relaxed);
        slots[static_cast<std::size_t>(w)]->cancel.store(false, std::memory_order_relaxed);
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
                if (split_replies) {
                    help_reply_splits(registry, ws, slot);
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
                                        ? prove_defender_split(ws, registry, nb, depth - 1, i)
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
