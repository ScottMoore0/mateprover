// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// solve.h -- The restriction portfolio and the per-position driver.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_SOLVE_H_INCLUDED
#define MATEPROVER_SOLVE_H_INCLUDED

namespace mateprover {

// One entry in the restriction portfolio.
struct PortfolioEntry {
    const char* name;
    int checks_mask;
    int king_squares;
    int max_defender_moves;
    int threat_depth;
    double weight;      // share of the budget
    // Lanes differ by ROUTE as well as by restriction. 8e measured a rejected
    // route to be worth no lane for directmates; the stalemate and selfmate
    // goals disagree, and section 54 has the measurement.
    RouteKind route = RouteKind::Dfpn;
    // 0 = off, 1 = attacker plays only captures, 2 = only captures or checks.
    // The shipped eight lanes were derived by set cover over MATE problems and
    // none of them knows a capture from a quiet move, which is why they find
    // nothing under a capture quota.
    int forcing = 0;
};

// Derived by greedy set cover over a 20-candidate restriction sweep, measured
// on 60 matetrack mate-in-8 positions held disjoint from every suite the engine
// was previously tuned on (benchmarks/reports/portfolio_sweep_train_20260801.json).
// The hand-picked predecessor reached 39/60; these lanes reach 44/60, which is
// the union of all 20 candidates -- eight lanes capture everything the candidate
// pool can reach, so adding more of these restrictions cannot help.
//
// Entry order is the greedy pick order, so lane 0 is the unrestricted search and
// each later lane is the one adding most over those before it. Weights allocate
// threads and budget: the unrestricted lane is held at a deliberate 0.30 floor
// because it is the only complete lane, and the rest are proportional to their
// measured standalone coverage.
//
// This is sound, not a heuristic gamble. A restriction only removes attacker
// options, so any mate it finds is a real forced mate, verifiable by the same
// certificate checker. It is incomplete, which is why the unrestricted search
// runs too rather than being replaced.
const std::vector<PortfolioEntry>& restriction_portfolio() {
    static const std::vector<PortfolioEntry> entries = {
        //  name          checks  king  maxdef  threat  weight
        {"unrestricted",       0,    0,      0,      0,  0.300},
        {"K2",                 0,    2,      0,      0,  0.212},
        {"K3",                 0,    3,      0,      0,  0.212},
        {"X2",                 0,    0,      2,      0,  0.081},
        {"R2",                 0,    0,      0,      2,  0.065},
        {"C4",                 4,    0,      0,      0,  0.049},
        {"R1",                 0,    0,      0,      1,  0.041},
        {"Rq2",                0,    0,      0,     -2,  0.041},
    };
    return entries;
}

// The portfolio for a live CAPTURE quota.
//
// The eight shipped lanes were derived by greedy set cover over matetrack
// mate-in-8 problems, where the only win is mate. Not one of them can express
// "this move captures", so under a capture quota they search hard for the wrong
// thing: measured on four replies to 1.e3 at quota 3, all eight exhausted within
// minutes and found nothing.
//
// These lanes are the same idea aimed at the right target. They remain pure
// ATTACKER restrictions, so a win found under one is a real forced win and
// verifies against the same certificate checker; they are incomplete, which is
// why lane 0 is still the unrestricted search.
//
// `cap-or-check` is the shape of the only quota win found so far -- the quota-2
// solution 3.Bxf7+ Kxf7 4.Qh5+ Ke6 5.Qxh7 lies entirely inside it, every White
// move being a capture or a check.
const std::vector<PortfolioEntry>& capture_portfolio() {
    static const std::vector<PortfolioEntry> entries = {
        //  name            checks king maxdef threat  weight  route            forcing
        {"unrestricted",         0,   0,     0,     0,  0.300, RouteKind::Dfpn, 0},
        {"cap-or-check",         0,   0,     0,     0,  0.180, RouteKind::Dfpn, 2},
        {"cap-only",             0,   0,     0,     0,  0.130, RouteKind::Dfpn, 1},
        {"cap-or-check-K3",      0,   3,     0,     0,  0.100, RouteKind::Dfpn, 2},
        {"K2",                   0,   2,     0,     0,  0.100, RouteKind::Dfpn, 0},
        {"K3",                   0,   3,     0,     0,  0.080, RouteKind::Dfpn, 0},
        {"cap-or-check-X2",      0,   0,     2,     0,  0.060, RouteKind::Dfpn, 2},
        {"X2",                   0,   0,     2,     0,  0.050, RouteKind::Dfpn, 0},
    };
    return entries;
}

// Is a capture quota live for either side? Decides which portfolio to run.
inline bool capture_quota_live(const SearchConfig& c) {
    if (!c.rule_wins[VR_CAPTURE]) {
        return false;
    }
    for (int colour = 0; colour < 2; ++colour) {
        const int q = c.quota_limit[quota_index(colour, VR_CAPTURE)];
        if (q > 0 && q <= kMaxQuota) {
            return true;
        }
    }
    return false;
}

// What a cooperative goal's single table is worth in lanes.
//
// The restriction portfolio ships nine lanes, so a directmate's default budget
// buys nine tables. A help goal has no lanes and bought one. This makes the two
// comparable rather than leaving a goal starved by its own structure.
const std::size_t kHelpMemoryLanes = 9;

// How many cooperative solutions to enumerate before stopping.
//
// A bound is necessary rather than tidy: enumeration cannot use the table, so a
// wide position can hold more solutions than anyone wants printed. The output
// says when the cap bound the answer, so a capped count is never mistaken for a
// complete one.
const std::size_t kHelpSolutionCap = 64;

// The smallest table worth giving a lane, in MB.
//
// Measured on the eight stalemate positions that Chest solved and this engine
// did not (section 55): a lane recovers them at 64 MB and above and recovers
// none at 32 MB, so the cliff sits between the two and the floor is placed at
// the first size on the working side of it. This bounds how thin an explicit
// total budget may be spread, not how much memory a lane may have.
const std::size_t kMinLaneMb = 64;

// Which lanes need the floor, and which can run thin.
//
// A lane is UNRESTRICTED when it removes no attacker options: lane 0, and the
// route-diversity lanes. Those search the whole space, so their tables hold a
// whole search and they are the lanes that fall off the 64 MB cliff.
//
// A restricted lane searches a deliberately smaller space and does not need as
// much table to hold it. That asymmetry is what makes the floor affordable: it
// is charged only where it buys something.
bool lane_is_unrestricted(const PortfolioEntry& e) {
    return e.checks_mask == 0 && e.king_squares == 0 &&
           e.max_defender_moves == 0 && e.threat_depth == 0 && e.forcing == 0;
}

// Solve one position, writing the result line to `out`.
//
// The stream is a parameter so that several positions can be solved at once,
// each into its own buffer, and the buffers emitted in input order. Writing
// straight to std::cout would interleave them.
void solve_line(const std::string& raw, int requested_depth, const SearchConfig& config,
                std::ostream& out) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        out << line << "; acn 0; acs 0; error input;\n";
        out.flush();
        return;
    }
    Board b = *parsed;
    // x-check: --checks supplies the allowance for lines that do not carry a
    // fifth Forsyth field. The line wins when it has one, on the same principle
    // as -Z against a line's own depth token: what the position states about
    // itself beats what the invocation assumed about it.
    if (!variant_active(b)) {
        for (std::size_t i = 0; i < config.quota_limit.size(); ++i) {
            if (config.quota_limit[i] >= 0) {
                b.quota[i] = static_cast<std::uint8_t>(config.quota_limit[i]);
            }
        }
    }
    int max_depth = requested_depth > 0 ? requested_depth : infer_mate_depth(line);
    if (max_depth <= 0) {
        // -Z supplies a depth for lines that carry none, which is distinct from
        // -z overriding whatever the line does carry. Falling back to 1 without
        // it turned an unannotated file into a run of mate-in-1 searches that
        // found nothing and looked like a hard corpus.
        max_depth = config.default_depth > 0 ? config.default_depth : 1;
    }

    // Legality only. parse_fen4 has already rejected everything unusable -- a
    // board that reaches here IS legal -- so this reports rather than decides,
    // and exists so a caller can screen a file without paying for a search.
    if (config.legality_only) {
        out << fen4(b) << "; acn 0; acs 0; legal;\n";
        out.flush();
        return;
    }

    // ALREADY DECIDED BEFORE ANYONE MOVED.
    //
    // x-check and x-capture cannot reach this: their quotas count down from at
    // least one, so a root can never start filled. An escape limit can, because
    // E is measured rather than accumulated and a supplied position may simply
    // be over it. The engine has no way to say "mate in 0" -- a result line
    // needs a key move and there is none -- so without this the search stopped
    // after one node and printed no verdict at all, which reads as "nothing
    // found" when the truth is "decided before the first move".
    //
    // Reported rather than refused. The position is legal and its answer is
    // known; a caller screening a corpus wants to be told which side stands won.
    {
        const VariantWin decided = variant_winner(b);
        if (decided.side >= 0) {
            out << fen4(b) << "; acn 0; acs 0; " << variant_win_key(decided.rule) << ' '
                << (decided.side == WHITE ? 'w' : 'b') << "; decided at root;\n";
            out.flush();
            return;
        }
    }

    // E for both kings, no search. See board.h; this exists so the escape count
    // can be checked directly against hand-worked positions.
    if (config.escape_count_only) {
        out << fen4(b) << "; acn 0; acs 0; escape w " << escape_count(b, WHITE)
            << "; escape b " << escape_count(b, BLACK) << ";\n";
        out.flush();
        return;
    }

    // Successor analysis: run the job on every position reachable in one move.
    //
    // Chest's -x, and its stated purpose is the phrase "black moves but loses in
    // x moves" -- the analyst wants to know which of a side's moves lose and how
    // fast, not whether the diagram itself is a problem. Each child is a whole
    // independent job with the other colour to move, so this recurses into the
    // ordinary path with the flag cleared rather than trying to share a search.
    if (config.successors) {
        SearchConfig child_config = config;
        child_config.successors = false;
        const std::vector<Move> moves = legal_moves(b, false, 64, false);
        // CARRY ONE TABLE ACROSS THE WHOLE JOB.
        //
        // Successors of one position are the case where sharing between
        // jobs obviously pays: they are siblings, so their subtrees overlap
        // almost entirely, and without this each child throws away
        // everything the previous child learned. Unrelated positions in a
        // batch transpose essentially never, which is why this is scoped
        // here rather than made general.
        //
        // Stood down when an escape rule is live: the escape LIMIT is not
        // part of the table key -- deliberately, see tt_key -- on the
        // argument that it is constant for one search, and carrying a table
        // between jobs is exactly what could make that false.
        std::unique_ptr<SharedProofTable> job_table;
        const bool escape_live = quota_of(b, WHITE, VR_ESCAPE) != kNoQuota ||
                                 quota_of(b, BLACK, VR_ESCAPE) != kNoQuota;
        if (config.cross_job_proofs && !escape_live) {
            job_table.reset(new SharedProofTable(config.shared_tt_shards, 0,
                                                 entry_capacity_for_mb(config.memory_mb),
                                                 config.tt_shed_divisor));
            child_config.job_table = job_table.get();
        }
        out << "; successors " << moves.size() << "\n";
        for (const Move& m : moves) {
            const Board nb = make_move(b, m);
            out << move_uci(m) << " ";
            solve_line(fen4(nb), max_depth, child_config, out);
        }
        out.flush();
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    // The sequential portfolio slices wall-clock time, so it needs a time
    // limit. The parallel form gives every lane the whole budget and works with
    // either kind.
    // The cooperative goals get no portfolio.
    //
    // Every lane in it is a RESTRICTION on the attacker, sound because removing
    // attacker options cannot invent a forced mate: a restricted search that
    // finds one has found a real one. That argument needs an adversary. When
    // both sides cooperate, a "restriction" removes moves from a helper, and a
    // helpmate solution can run straight through the move it removed -- so a
    // restricted lane would not be an incomplete search for the same answer, it
    // would be a search for a different problem. Running one lane, unrestricted,
    // is the whole portfolio here. See section 58.
    // Dual enumeration must never run under a restriction.
    //
    // A restricted lane removes attacker options, which is sound for PROVING --
    // a mate found under one is a real mate -- and catastrophic for COUNTING.
    // The moves it removed are exactly the ones that might have been second
    // solutions, so a restricted enumeration undercounts duals and reports a
    // cooked problem as sound. That is a wrong answer to the only question this
    // mode exists to answer, so the portfolio is off whenever it is asked.
    const bool use_portfolio = config.portfolio && !goal_is_help(config.goal) &&
                               !config.all_solutions &&
                               (config.time_limit > 0.0 ||
                                (config.node_limit > 0 && config.portfolio_parallel));
    const char* winning_entry_name = nullptr;

    // `-M` is a ceiling on every table alive at once, not on each one
    // separately. Tables are held per portfolio lane and per batch position
    // worker, so the per-table share is the total divided by both. Measured
    // before this split: a stated 256 MB cost 615 MB with the default eight
    // lanes, and 1994 MB at --parallel-positions 4 -- a flag wrong by 8x in
    // the direction that ends a batch run with an allocation failure.
    //
    // 0 keeps its meaning of "unbounded", and a share never rounds to 0: a
    // table with a one-entry ceiling would evict on every store.
    //
    // Only an explicit -M is divided. The default is per-table, so raising
    // --parallel-positions cannot quietly shrink the tuned budget underneath
    // a batch run.
    const std::size_t memory_workers =
        static_cast<std::size_t>(std::max(1, config.parallel_positions));
    auto memory_share = [&](std::size_t consumers) -> std::size_t {
        if (config.memory_mb == 0 || !config.memory_is_total) {
            return config.memory_mb;
        }
        const std::size_t divisor = memory_workers * std::max<std::size_t>(1, consumers);
        return std::max<std::size_t>(1, config.memory_mb / divisor);
    };

    // Each attempt gets a fresh Search: tables from a restricted run answer a
    // different question and must not leak into the next attempt.
    auto attempt = [&](const PortfolioEntry* entry, double seconds, Search& out) {
        static_cast<SearchConfig&>(out) = config;
        out.attacker = b.stm;
        out.order_min_size = std::max<std::size_t>(2, config.order_min_size);
        out.root_depth = max_depth;
        out.portfolio = false;
        if (entry != nullptr) {
            out.checks_mask = entry->checks_mask;
            out.king_squares = entry->king_squares;
            out.max_defender_moves = entry->max_defender_moves;
            out.threat_depth = entry->threat_depth;
            out.forcing_mode = entry->forcing;
        }
        if (seconds > 0.0) {
            out.has_deadline = true;
            out.deadline = std::chrono::steady_clock::now() +
                           std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                               std::chrono::duration<double>(seconds));
        }
        // One lane at a time on this path, so it shares only with the batch.
        out.memory_mb = memory_share(1);
        // A cooperative goal runs ONE table where the portfolio runs nine.
        //
        // The default budget is per table, so a directmate takes nine lanes'
        // worth -- about 2.3 GB -- while a helpmate, which has no lanes by
        // construction (58), took 256 MB in total. Same flag, same nominal
        // number, a ninth of the memory, purely because of a structural
        // difference in the goal. The last helpmate position Chest solved and
        // this engine did not is reached at 2048 MB and not at 256, so this was
        // costing coverage and not merely consistency.
        //
        // An explicit total is left exactly as the user set it: they asked for a
        // cap, and this is the branch that must not quietly exceed it.
        if (goal_is_help(config.goal) && !config.memory_is_total && config.memory_mb != 0) {
            out.memory_mb = config.memory_mb * kHelpMemoryLanes;
        }
        // Not in a portfolio, so this search IS the unrestricted one.
        out.progress_authority = true;
        out.tt.capacity = entry_capacity_for_mb(out.memory_mb);
        if (out.tt_reserve > 0) {
            out.tt.map.reserve(out.tt_reserve);
        }
        return run_route(out, b, max_depth);
    };

    // Run every portfolio entry concurrently, each with the full budget.
    //
    // The first entry to prove a mate wins and the rest are cancelled. Which
    // one that is can vary between runs, so the *proof* returned is not
    // deterministic -- but every proof returned is valid and verifiable, and
    // the entry used is reported. Determinism was traded deliberately: making
    // it deterministic would mean waiting for lower-index entries that may
    // never finish, which is the whole cost this mode exists to avoid.
    auto run_parallel_portfolio = [&](Search& report) -> RouteResult {
        // Route diversity for the non-directmate goals.
        //
        // Measured on the positions Chest solves and this engine did not: a
        // plain depth-first lane recovers 3 of 8 stalemate losses and the only
        // selfmate loss, where every DFPN configuration recovers none. Those
        // positions are tiny-material endgames -- K+R vs K at depth 13 -- where
        // proof numbers derived from move counts carry no signal because every
        // branch looks alike, and a straight depth-first walk with a table
        // simply gets there.
        //
        // Directmate keeps the shipped lanes unchanged: 8e measured a rejected
        // route to be worth no lane there, and that measurement still stands.
        static const std::vector<PortfolioEntry> with_route = [] {
            std::vector<PortfolioEntry> v = restriction_portfolio();
            PortfolioEntry df{"depth-first", 0, 0, 0, 0, 0.100, RouteKind::DepthFirst};
            v.push_back(df);
            return v;
        }();
        // A live capture quota changes what a good restriction IS, so it
        // changes the lane set rather than merely reweighting the old one.
        const auto& mate_entries =
            capture_quota_live(config) ? capture_portfolio() : restriction_portfolio();
        const auto& all_entries = config.goal == Goal::Mate ? mate_entries : with_route;

        // An optional cap on how many lanes run at once.
        //
        // Lanes contend for cores and memory bandwidth, so more is not
        // monotonically better: the depth-first lane solves the last selfmate
        // holdout in 7.4 s alone and 27 s against eight others, at every memory
        // size, so the loss is contention and not table space. 0 means every
        // lane, which stays the default.
        //
        // Lanes are kept in a priority order, not list order. Lane 0 -- the
        // unrestricted search -- is the only lane that can settle "no mate
        // exists" and survives everything. For the non-directmate goals the
        // route-diversity lane comes second, ahead of every restriction,
        // because it reaches positions no DFPN configuration reaches at any
        // memory size (54). Restrictions then follow in greedy pick order,
        // which is already descending marginal coverage.
        std::vector<PortfolioEntry> capped;
        if (config.portfolio_lanes > 0 &&
            static_cast<std::size_t>(config.portfolio_lanes) < all_entries.size()) {
            std::vector<std::size_t> order{0};
            if (config.goal != Goal::Mate) {
                for (std::size_t i = 1; i < all_entries.size(); ++i) {
                    if (all_entries[i].route != RouteKind::Dfpn) order.push_back(i);
                }
            }
            for (std::size_t i = 1; i < all_entries.size(); ++i) {
                if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
            }
            for (std::size_t rank : order) {
                if (capped.size() >= static_cast<std::size_t>(config.portfolio_lanes)) break;
                capped.push_back(all_entries[rank]);
            }
        }
        const std::vector<PortfolioEntry>& entries = capped.empty() ? all_entries : capped;
        const int lanes = static_cast<int>(entries.size());
        const int total_threads = std::max(1, config.threads);
        // Threads follow the same weights as the time slices did. Splitting
        // them equally starved the unrestricted lane, which is the most general
        // and the one whose answer is preferred: it dropped from 15 solved to
        // 13 while the restricted lanes added 4.
        //
        // The weights apply to DIRECTMATE only, and on the other goals the
        // extra threads go somewhere else entirely -- because whether a second
        // thread helps or ruins a lane depends on its ROUTE, not its weight.
        //
        // A lane with more than one thread root-splits. On the non-directmate
        // goals, measured per route on the one position Chest still wins:
        //
        //   depth-first  1 thread 26.0 s -> 16 threads 7.2 s   (3.6x, scales)
        //   dfpn         never solves it at any thread count
        //
        // and spreading threads over the dfpn lanes by weight took selfmate's
        // median from 0.29 s to 20.22 s -- the time limit -- for identical
        // coverage (56). So the threads are not withheld, they are aimed: every
        // spare thread goes to the route-diversity lanes, which are the ones
        // that turn them into speed, and the dfpn lanes stay at one apiece.
        std::vector<int> lane_threads(static_cast<std::size_t>(lanes), 1);
        int assigned = lanes;
        if (config.goal == Goal::Mate) {
            for (int i = 0; i < lanes && assigned < total_threads; ++i) {
                const int want = static_cast<int>(
                    static_cast<double>(total_threads) * entries[static_cast<std::size_t>(i)].weight);
                const int extra = std::min(std::max(0, want - 1), total_threads - assigned);
                lane_threads[static_cast<std::size_t>(i)] += extra;
                assigned += extra;
            }
        } else {
            int splitters = 0;
            for (const auto& e : entries) {
                if (e.route != RouteKind::Dfpn) ++splitters;
            }
            if (splitters > 0 && assigned < total_threads) {
                // Bounded, not "all that is spare". The split scales this lane
                // but it competes with the eight lanes that resolve most
                // positions quickly, so an unbounded share wins one position
                // and loses many. See section 57 for the sweep.
                // One spare thread, not four. Selfmate is flat in this number
                // (49 of 60 at 1, 2 and 4) while stalemate falls monotonically
                // as the split takes cores from the lanes resolving everything
                // else: 47, 44, 43. The split is worth exactly enough threads
                // to reach the positions single-threaded search cannot, and no
                // more. See section 57.
                const int cap = config.route_lane_threads > 0 ? config.route_lane_threads : 1;
                const int share = std::min(cap, (total_threads - assigned) / splitters);
                for (int i = 0; i < lanes && share > 0; ++i) {
                    if (entries[static_cast<std::size_t>(i)].route != RouteKind::Dfpn) {
                        const int extra = std::min(share, total_threads - assigned);
                        lane_threads[static_cast<std::size_t>(i)] += extra;
                        assigned += extra;
                    }
                }
            }
        }

        // Spend a total budget where it changes the answer.
        //
        // An explicit -M is a total (42, 44), so it is divided among the lanes.
        // Divided EQUALLY, "-M 256" -- the obvious thing to type when matching
        // another engine's 256 MB -- left 28 MB a lane, and 28 MB solved none of
        // the eight stalemate positions that 64 MB solves. The flag that looked
        // like it granted a quarter gigabyte took away a factor of nine in
        // solving power, silently, with every output still well-formed.
        //
        // Funding the floor by dropping lanes instead was measured and rejected:
        // it recovered the eight, but paying for them with five restriction
        // lanes made selfmate 5x SLOWER than Chest on the median position, since
        // restrictions are exactly what make selfmate fast (52).
        //
        // So the floor is charged only to the lanes that fall off the cliff --
        // the unrestricted ones -- and the restricted lanes divide what is left.
        // Every lane still runs, the cap is still honoured exactly, and the
        // memory goes where it changes an answer rather than being spread evenly
        // over lanes that do not need it. See section 55.
        std::vector<std::size_t> lane_memory(static_cast<std::size_t>(lanes),
                                             memory_share(static_cast<std::size_t>(lanes)));
        if (config.memory_mb != 0 && config.memory_is_total) {
            std::size_t unrestricted = 0;
            for (const auto& e : entries) {
                if (lane_is_unrestricted(e)) ++unrestricted;
            }
            const std::size_t per_worker = std::max<std::size_t>(1, config.memory_mb / memory_workers);
            const std::size_t floored = unrestricted * kMinLaneMb;
            // Only worth doing if the floor both binds and leaves something over
            // for the restricted lanes; otherwise fall back to an equal split.
            if (unrestricted > 0 && unrestricted < static_cast<std::size_t>(lanes) &&
                floored < per_worker && kMinLaneMb > per_worker / static_cast<std::size_t>(lanes)) {
                const std::size_t rest = static_cast<std::size_t>(lanes) - unrestricted;
                const std::size_t thin = std::max<std::size_t>(1, (per_worker - floored) / rest);
                for (int i = 0; i < lanes; ++i) {
                    lane_memory[static_cast<std::size_t>(i)] =
                        lane_is_unrestricted(entries[static_cast<std::size_t>(i)]) ? kMinLaneMb : thin;
                }
            }
        }

        // ONE PROOF TABLE FOR EVERY LANE, holding proofs and nothing else.
        //
        // The lanes solve DIFFERENT problems -- each restriction is a different
        // question -- so they cannot share a table in general. They can share
        // this one, because a proof is the one statement that survives the
        // difference: a restriction only removes attacker options, so a mate
        // forced with fewer options available is a mate forced with more.
        //
        // Sized like a lane's own table rather than the whole budget: it holds
        // only the proved positions, which the counters put at about 7% of
        // stores.
        std::unique_ptr<SharedProofTable> owned_cross;
        SharedProofTable* cross_proofs = config.job_table;
        if (cross_proofs == nullptr && config.cross_lane_proofs) {
            owned_cross.reset(new SharedProofTable(
                config.shared_tt_shards, 0,
                entry_capacity_for_mb(memory_share(static_cast<std::size_t>(lanes))),
                config.tt_shed_divisor));
            cross_proofs = owned_cross.get();
        }
        std::vector<std::unique_ptr<Search>> searches;
        std::vector<std::unique_ptr<std::atomic<bool>>> cancels;
        std::vector<RouteResult> results(static_cast<std::size_t>(lanes));
        std::vector<char> proved(static_cast<std::size_t>(lanes), 0);
        // A lane's any-depth verdict, recorded separately because `results` is
        // only written for ACCEPTABLE results and a refutation is never one.
        std::vector<char> lane_refuted(static_cast<std::size_t>(lanes), 0);
        searches.reserve(static_cast<std::size_t>(lanes));
        cancels.reserve(static_cast<std::size_t>(lanes));

        std::atomic<bool> stop{false};
        std::mutex done_mutex;
        int winner = -1;

        for (int i = 0; i < lanes; ++i) {
            cancels.emplace_back(new std::atomic<bool>(false));
            searches.emplace_back(new Search());
            Search& t = *searches.back();
            static_cast<SearchConfig&>(t) = config;
            t.attacker = b.stm;
            t.order_min_size = std::max<std::size_t>(2, config.order_min_size);
            t.root_depth = max_depth;
            t.portfolio = false;
            t.portfolio_parallel = false;
            t.cross_proofs = cross_proofs;
            // Only an unrestricted lane may contribute disproofs. This is the
            // single flag standing between a sound optimisation and a false
            // "no solution": a restricted lane's failure is silent about every
            // move its restriction removed.
            t.cross_authoritative = lane_is_unrestricted(entries[static_cast<std::size_t>(i)]);
            t.attacker_proofs.resize(t.hint_entries);
            t.defender_refutations.resize(t.hint_entries);
            t.threads = lane_threads[static_cast<std::size_t>(i)];
            t.checks_mask = entries[static_cast<std::size_t>(i)].checks_mask;
            t.king_squares = entries[static_cast<std::size_t>(i)].king_squares;
            t.max_defender_moves = entries[static_cast<std::size_t>(i)].max_defender_moves;
            t.threat_depth = entries[static_cast<std::size_t>(i)].threat_depth;
            t.forcing_mode = entries[static_cast<std::size_t>(i)].forcing;
            // Only the unrestricted lane may publish a bound: a restricted
            // lane that fails has proved nothing, because it never looked at
            // the moves the restriction removed (98).
            t.progress_authority = lane_is_unrestricted(entries[static_cast<std::size_t>(i)]);
            t.route = entries[static_cast<std::size_t>(i)].route;
            // A RESTRICTED lane searches the final depth only, even under
            // iterative deepening.
            //
            // Iterative deepening exists to establish MINIMALITY: depth d is
            // searched only after d-1 has been shown to hold no solution. A
            // restricted lane cannot establish that. Its failure at d-1 means
            // "no solution among the moves I was allowed", which is silent
            // about the moves it was not -- only the unrestricted lane can
            // settle non-existence, and the code below already relies on that
            // when deciding timeout versus disproof.
            //
            // So every shallow pass a restricted lane makes is work that cannot
            // contribute to the claim. With nine lanes that multiplied the
            // wasted portion by eight, and it is why --iterative-depth collapsed
            // where --direct-depth did not: a selfmate position solved in 0.0 s
            // at a fixed depth timed out at 60 s when every lane re-walked the
            // shallow ones.
            //
            // What a restricted lane CAN contribute is a proof, and a proof at
            // the requested depth is the only one it is asked for. Its result is
            // already reported with `via <restriction>`, which the output
            // documents as "real but may not be the shortest", so nothing about
            // the engine's claims changes. See section 61.
            if (!lane_is_unrestricted(entries[static_cast<std::size_t>(i)])) {
                t.direct_depth = true;
            }
            t.cancel = cancels.back().get();
            t.memory_mb = lane_memory[static_cast<std::size_t>(i)];
            t.tt.capacity = entry_capacity_for_mb(t.memory_mb);
            t.tt.shed_divisor = t.tt_shed_divisor;
            if (config.time_limit > 0.0) {
                t.has_deadline = true;
                t.deadline = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                         std::chrono::duration<double>(config.time_limit));
            }
        }

        auto lane = [&](int i) {
            Search& t = *searches[static_cast<std::size_t>(i)];
            const RouteResult r = run_route(t, b, max_depth);
            lane_refuted[static_cast<std::size_t>(i)] = r.proof.refuted ? 1 : 0;
            if (route_result_is_acceptable(r, max_depth)) {
                std::lock_guard<std::mutex> lock(done_mutex);
                results[static_cast<std::size_t>(i)] = r;
                proved[static_cast<std::size_t>(i)] = 1;
                if (winner < 0) {
                    winner = i;
                    stop.store(true, std::memory_order_release);
                    for (auto& c : cancels) {
                        c->store(true, std::memory_order_release);
                    }
                }
            }
        };

        std::vector<std::thread> pool;
        pool.reserve(static_cast<std::size_t>(std::max(0, lanes - 1)));
        for (int i = 1; i < lanes; ++i) {
            // As in the root split: a refused thread must not escape and
            // terminate the process. Dropping a restricted lane is sound
            // because every lane is independent and the unrestricted lane --
            // the only one that can settle a disproof -- runs inline below.
            try {
                pool.emplace_back(lane, i);
            } catch (const std::system_error&) {
                break;
            }
        }
        lane(0);
        for (std::thread& th : pool) {
            th.join();
        }

        // Prefer the unrestricted lane when it also proved: it is the most
        // general answer and the one whose depth is the shortest found.
        int pick = -1;
        if (proved[0]) {
            pick = 0;
        } else {
            for (int i = 0; i < lanes; ++i) {
                if (proved[static_cast<std::size_t>(i)]) {
                    pick = i;
                    break;
                }
            }
        }
        for (int i = 0; i < lanes; ++i) {
            report.stats += searches[static_cast<std::size_t>(i)]->stats;
        }
        if (pick < 0) {
            // No lane proved a mate. Whether that is "no mate exists" or "we ran
            // out of time" is decided by the UNRESTRICTED lane alone: the
            // restricted lanes are sound but incomplete, so their failure --
            // and their timing out -- proves nothing either way. Reporting a
            // timeout whenever any lane failed would have marked every genuine
            // disproof as a timeout, which is exactly what happened once the
            // portfolio became the default.
            bool complete = false;
            for (int i = 0; i < lanes; ++i) {
                if (std::string(entries[static_cast<std::size_t>(i)].name) == "unrestricted") {
                    complete = !searches[static_cast<std::size_t>(i)]->timed_out;
                    break;
                }
            }
            report.timed_out = !complete;
            // Same rule for the any-depth verdict: only the unrestricted lane
            // may assert it, for exactly the reason it alone may assert "no
            // mate exists".
            for (int i = 0; i < lanes; ++i) {
                if (std::string(entries[static_cast<std::size_t>(i)].name) == "unrestricted") {
                    report.refuted_any_depth = lane_refuted[static_cast<std::size_t>(i)] != 0;
                    break;
                }
            }
            return {};
        }
        winning_entry_name = entries[static_cast<std::size_t>(pick)].name;
        return results[static_cast<std::size_t>(pick)];
    };

    Search s;
    // The reporting Search carries the caller's config from the start. It was
    // previously assigned only on the failure path, so a portfolio run that
    // succeeded reported with a default-constructed config and silently
    // dropped --emit-proof.
    static_cast<SearchConfig&>(s) = config;
    // The preconditioner runs on THIS search and writes its move guidance into
    // these, so leaving them unsized disables DFPN's whole contribution.
    s.attacker_proofs.resize(config.hint_entries);
    s.defender_refutations.resize(config.hint_entries);
    RouteResult route_result;
    const char* winning_entry = nullptr;

    std::vector<RootSolution> root_solutions;
    std::vector<RootRefutation> root_refutations;
    std::vector<std::vector<Move>> help_solutions;
    if (config.all_solutions) {
        // One unrestricted search, enumerating rather than short-circuiting.
        static_cast<SearchConfig&>(s) = config;
        s.attacker = b.stm;
        s.order_min_size = std::max<std::size_t>(2, config.order_min_size);
        s.root_depth = max_depth;
        s.portfolio = false;
        s.memory_mb = memory_share(1);
        s.tt.capacity = entry_capacity_for_mb(s.memory_mb);
        if (config.time_limit > 0.0) {
            s.has_deadline = true;
            s.deadline = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                     std::chrono::duration<double>(config.time_limit));
        }
        if (goal_is_help(config.goal)) {
            // Help goals enumerate whole SEQUENCES, because two solutions often
            // share a first move: counting root keys would report one where a
            // composer intends two. See prove.h.
            std::vector<Move> help_line;
            collect_help_solutions(s, b, max_depth * 2, help_line, help_solutions,
                                   kHelpSolutionCap);
            for (const std::vector<Move>& sol : help_solutions) {
                if (!sol.empty()) {
                    root_solutions.push_back(RootSolution{sol.front(), Proof{true, sol, {}}});
                }
            }
        } else {
            root_solutions = run_all_root_solutions(s, b, max_depth,
                                                    config.print_tree ? &root_refutations : nullptr);
        }
        if (!root_solutions.empty()) {
            route_result.proof = root_solutions.front().proof;
            route_result.proved_depth =
                static_cast<int>((route_result.proof.pv.size() + 1) / 2);
        }
    } else if (use_portfolio && config.portfolio_parallel) {
        route_result = run_parallel_portfolio(s);
        winning_entry = winning_entry_name;
    } else if (use_portfolio) {
        const auto& entries = restriction_portfolio();
        bool unrestricted_complete = false;
        const auto deadline = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                          std::chrono::duration<double>(config.time_limit));
        for (const PortfolioEntry& entry : entries) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                break;
            }
            const double remaining = std::chrono::duration<double>(deadline - now).count();
            const double slice = std::min(remaining, config.time_limit * entry.weight);
            Search attempt_search;
            const RouteResult r = attempt(&entry, slice, attempt_search);
            if (route_result_is_acceptable(r, max_depth)) {
                route_result = r;
                s.stats += attempt_search.stats;
                s.timed_out = false;
                winning_entry = entry.name;
                break;
            }
            s.stats += attempt_search.stats;
            if (std::string(entry.name) == "unrestricted") {
                // Only the complete lane can settle "no mate exists"; see the
                // parallel path for why the restricted lanes cannot. The
                // any-depth verdict travels on the same ticket.
                unrestricted_complete = !attempt_search.timed_out;
                s.refuted_any_depth = r.proof.refuted;
            }
        }
        if (!route_result_is_acceptable(route_result, max_depth)) {
            s.timed_out = !unrestricted_complete;
        }
    } else {
        route_result = attempt(nullptr, config.time_limit, s);
        s.refuted_any_depth = route_result.proof.refuted;
    }
    const Proof& proof = route_result.proof;
    const bool accepted = route_result_is_acceptable(route_result, max_depth);
    if (!accepted && route_result.proof.ok) {
        ++s.stats.route_rejections;
    }
    int proved_depth = accepted ? route_result.proved_depth : 0;

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    out << fen4(b) << "; acn " << s.stats.nodes << "; acs " << seconds;
    if (accepted) {
        // `sm N`, not `dm N`. A forced stalemate is a different claim about the
        // position, and every consumer that greps for `dm` -- this project's own
        // tools among them -- would otherwise read one as the other.
        // One token a goal, all distinct, none a prefix of another once the
        // trailing space is counted. Six goals now share this line and a
        // consumer that grepped for the wrong one would read a solved
        // helpstalemate as a solved directmate.
        const char* token = "; dm ";
        switch (config.goal) {
            case Goal::Stalemate:     token = "; sm ";  break;
            case Goal::Selfmate:      token = "; sfm "; break;
            case Goal::Selfstalemate: token = "; ssm "; break;
            case Goal::Helpmate:      token = "; hm ";  break;
            case Goal::Helpstalemate: token = "; hsm "; break;
            case Goal::Mate:          break;
        }
        out << "; bm " << move_uci(proof.pv.front()) << token << proved_depth
                  << "; pv " << pv_uci(proof.pv);
        if (s.emit_proof && !proof.cert.empty()) {
            out << "; proof " << proof.cert;
        }
    }
    if (config.print_tree && accepted && !proof.cert.empty()) {
        // The tree is the problemist's view of the same proof the certificate
        // records: key, then each defence indented under it. It is printed from
        // the certificate rather than from a second search, so what is displayed
        // is exactly what was proved and verified -- there is no way for the
        // two to disagree.
        out << "\n";
        CertReader reader(proof.cert);
        print_cert_node(out, reader, b, 0, config.short_notation);
        if (!root_refutations.empty()) {
            out << "refutations\n";
            for (const RootRefutation& r : root_refutations) {
                out << "  " << move_text(b, r.move, config.short_notation) << "  ";
                if (r.reason == "no solution") {
                    out << r.reason << "\n";
                } else {
                    // Print the refuting reply in the position it is played in.
                    const Board nb = make_move(b, r.move);
                    bool named = false;
                    for (const Move& rm : legal_moves(nb, false, 64, false)) {
                        if (move_uci(rm) == r.reason) {
                            out << move_text(nb, rm, config.short_notation) << "\n";
                            named = true;
                            break;
                        }
                    }
                    if (!named) out << r.reason << "\n";
                }
            }
        }
    }
    if (config.all_solutions && accepted) {
        // The composition verdict. `keys` lists every root move that solves, so
        // one key is a sound problem and more than one is a cook. Stating the
        // count separately from the list means a consumer can test soundness
        // without parsing moves.
        if (goal_is_help(config.goal)) {
            // Distinct first moves, and whole solutions. They differ whenever
            // two solutions share a key, which is common in helpmates, so
            // reporting only one of the two numbers would mislead either way.
            std::vector<std::string> distinct;
            for (const RootSolution& r : root_solutions) {
                const std::string u = move_uci(r.move);
                if (std::find(distinct.begin(), distinct.end(), u) == distinct.end()) {
                    distinct.push_back(u);
                }
            }
            out << "; keys " << distinct.size() << "; solutions " << help_solutions.size();
            if (help_solutions.size() >= kHelpSolutionCap) {
                // Never let a truncated enumeration read as a complete one.
                out << "; capped";
            }
            out << "; sols";
            for (const std::string& u : distinct) {
                out << " " << u;
            }
        } else {
            out << "; keys " << root_solutions.size() << "; sols";
            for (const RootSolution& r : root_solutions) {
                out << " " << move_uci(r.move);
            }
            out << (root_solutions.size() == 1 ? "; sound" : "; cooked");
        }
    }
    if (accepted && winning_entry != nullptr && std::string(winning_entry) != "unrestricted") {
        // Say which restriction proved it: the mate is real but may not be the
        // shortest, and the caller is entitled to know a restriction was used.
        out << "; via " << winning_entry;
    }
    if (!accepted && s.refuted_any_depth) {
        // The strongest negative this engine can make: no solution at ANY depth,
        // so no larger budget and no deeper search will change it. Distinct from
        // the bare no-solution line, which only says "none within the depth
        // searched". See docs/GAP1_DERIVATION.md.
        out << "; refuted";
    }
    if (!accepted && s.timed_out) {
        // Distinguish "gave up" from "proved there is no mate". Without this a
        // released tool would report the same thing for both.
        out << "; timeout";
    }
    // Flush at the line boundary so the engine works as a persistent service:
    // a client that writes one position and waits gets its answer immediately.
    //
    // This also happens implicitly today, because std::cin is tied to std::cout
    // and the next std::getline flushes it. That is easy to destroy:
    // std::cin.tie(nullptr) and sync_with_stdio(false) are routine throughput
    // tweaks, and either would silently turn streaming answers into a batch that
    // appears only at exit. Being explicit costs one flush per position, against
    // a search costing at least microseconds.
    out << ";\n";
    out.flush();
    if (s.profile) {
        emit_profile_line(b, s, requested_depth, proved_depth, seconds);
    }
}

// Convenience overload: solve straight to stdout.
inline void solve_line(const std::string& raw, int requested_depth, const SearchConfig& config) {
    solve_line(raw, requested_depth, config, std::cout);
}
} // namespace mateprover

#endif // MATEPROVER_SOLVE_H_INCLUDED
