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
    int max_depth = requested_depth > 0 ? requested_depth : infer_mate_depth(line);
    if (max_depth <= 0) {
        max_depth = 1;
    }

    const auto start = std::chrono::steady_clock::now();
    // The sequential portfolio slices wall-clock time, so it needs a time
    // limit. The parallel form gives every lane the whole budget and works with
    // either kind.
    const bool use_portfolio = config.portfolio &&
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
        }
        if (seconds > 0.0) {
            out.has_deadline = true;
            out.deadline = std::chrono::steady_clock::now() +
                           std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                               std::chrono::duration<double>(seconds));
        }
        // One lane at a time on this path, so it shares only with the batch.
        out.memory_mb = memory_share(1);
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
        const auto& entries = restriction_portfolio();
        const int lanes = static_cast<int>(entries.size());
        const int total_threads = std::max(1, config.threads);
        // Threads follow the same weights as the time slices did. Splitting
        // them equally starved the unrestricted lane, which is the most general
        // and the one whose answer is preferred: it dropped from 15 solved to
        // 13 while the restricted lanes added 4.
        std::vector<int> lane_threads(static_cast<std::size_t>(lanes), 1);
        int assigned = lanes;
        for (int i = 0; i < lanes && assigned < total_threads; ++i) {
            const int want = static_cast<int>(
                static_cast<double>(total_threads) * entries[static_cast<std::size_t>(i)].weight);
            const int extra = std::min(std::max(0, want - 1), total_threads - assigned);
            lane_threads[static_cast<std::size_t>(i)] += extra;
            assigned += extra;
        }

        std::vector<std::unique_ptr<Search>> searches;
        std::vector<std::unique_ptr<std::atomic<bool>>> cancels;
        std::vector<RouteResult> results(static_cast<std::size_t>(lanes));
        std::vector<char> proved(static_cast<std::size_t>(lanes), 0);
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
            t.threads = lane_threads[static_cast<std::size_t>(i)];
            t.checks_mask = entries[static_cast<std::size_t>(i)].checks_mask;
            t.king_squares = entries[static_cast<std::size_t>(i)].king_squares;
            t.max_defender_moves = entries[static_cast<std::size_t>(i)].max_defender_moves;
            t.threat_depth = entries[static_cast<std::size_t>(i)].threat_depth;
            t.cancel = cancels.back().get();
            t.memory_mb = memory_share(static_cast<std::size_t>(lanes));
            t.tt.capacity = entry_capacity_for_mb(t.memory_mb);
            if (config.time_limit > 0.0) {
                t.has_deadline = true;
                t.deadline = start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                         std::chrono::duration<double>(config.time_limit));
            }
        }

        auto lane = [&](int i) {
            Search& t = *searches[static_cast<std::size_t>(i)];
            const RouteResult r = run_route(t, b, max_depth);
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
    RouteResult route_result;
    const char* winning_entry = nullptr;

    if (use_portfolio && config.portfolio_parallel) {
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
                // parallel path for why the restricted lanes cannot.
                unrestricted_complete = !attempt_search.timed_out;
            }
        }
        if (!route_result_is_acceptable(route_result, max_depth)) {
            s.timed_out = !unrestricted_complete;
        }
    } else {
        route_result = attempt(nullptr, config.time_limit, s);
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
        out << "; bm " << move_uci(proof.pv.front())
                  << (config.goal == Goal::Stalemate ? "; sm "
                      : config.goal == Goal::Selfmate ? "; sfm " : "; dm ") << proved_depth
                  << "; pv " << pv_uci(proof.pv);
        if (s.emit_proof && !proof.cert.empty()) {
            out << "; proof " << proof.cert;
        }
    }
    if (accepted && winning_entry != nullptr && std::string(winning_entry) != "unrestricted") {
        // Say which restriction proved it: the mate is real but may not be the
        // shortest, and the caller is entitled to know a restriction was used.
        out << "; via " << winning_entry;
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
