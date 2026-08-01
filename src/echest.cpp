// E Chest initial exact directmate prover.
//
// This is a conservative first checkpoint for the E rewrite line. It favors
// auditable correctness over final performance. Later E milestones should
// replace the array board with a bitboard/incremental engine while preserving
// this proof interface and verifier behavior.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

// Modules are included in their original order into this single translation
// unit, so the preprocessed result is textually equivalent to the previous
// single-file build. Compilation stays a unity build deliberately: the search
// depends on cross-module inlining in its hottest paths.
#include "types.h"
#include "table.h"
#include "search_state.h"
#include "board.h"
#include "movegen.h"
#include "ordering.h"
#include "prooftable.h"
#include "search.h"

using namespace echest;


// Ceiling applied to `--threads auto`. See the comment at its use site: root
// split saturates, so detected-core-count parallelism wastes cores above this.
constexpr int AUTO_THREAD_CAP = 16;

#ifndef ECHEST_VERSION
#define ECHEST_VERSION "0.1.0-dev"
#endif

void print_version() {
    std::cout << "echest " << ECHEST_VERSION << "\n";
}

void print_usage() {
    std::cout <<
"echest " ECHEST_VERSION " - exact directmate prover with machine-checkable proofs\n"
"\n"
"Usage:\n"
"  echest [options] -            read EPD/FEN lines from stdin\n"
"  echest [options] < file       read a single position from stdin\n"
"\n"
"Problem:\n"
"  -z N                          requested mate depth (else inferred from #N)\n"
"  --route NAME                  depth-first (default) | shallow-fast | dfpn\n"
"  --direct-depth                prove \"a mate within N\" by searching at N\n"
"                                directly; better solve rate at a fixed\n"
"                                budget, but not guaranteed shortest\n"
"  --iterative-depth             default; prove \"the shortest mate is N\"\n"
"\n"
"Resources:\n"
"  -M N                          table budget in MB, honoured as an entry\n"
"                                ceiling; 0 means unbounded (default 64)\n"
"  --threads N | auto            root-split worker threads (default 1;\n"
"                                registry promotes 8). auto = min(cores,16):\n"
"                                the split saturates above that, so extra\n"
"                                cores add no capability. Explicit N is not\n"
"                                capped.\n"
"  --single-thread               force sequential search\n"
"  --parallel-min-nodes N        run a depth sequentially until it exceeds\n"
"                                N nodes, then split (default 500)\n"
"  --no-parallel-gate            always split, never probe sequentially\n"
"  --time-limit S                wall-clock budget in seconds; on expiry the\n"
"                                search reports \"timeout\", never a mate\n"
"  --root-sequential-first N     search N root moves sequentially before\n"
"                                splitting; cuts wasted parallel work\n"
"  --root-split-all              default; split every root move\n"
"  --shared-tt | --private-tt    share one exact proof table across workers\n"
"  --shared-tt-shards N          shards for the shared table (default 256)\n"
"  --tt-reserve N                pre-reserve N table buckets\n"
"\n"
"Output:\n"
"  -5                            UCI-style coordinate moves (compatibility)\n"
"  --emit-proof                  append a recursive JSON proof certificate\n"
"  --profile                     emit per-position counters to stderr\n"
"  --no-profile                  default; no counters\n"
"  --debug                       verbose search diagnostics on stderr\n"
"  --list-legal                  list legal moves instead of solving\n"
"  --perft N                     perft counts for depths 1..N\n"
"  --perft-divide N              per-root-move perft breakdown at depth N\n"
"\n"
"Search tuning (all preserve exactness; see docs/E_CHEST_ARCHITECTURE.md):\n"
"  --proof-hints | --no-proof-hints\n"
"  --refutation-hints | --no-refutation-hints\n"
"  --keep-iter-tt | --clear-iter-tt\n"
"  --ordered-check-shortcut | --no-ordered-check-shortcut\n"
"  --inplace-order | --scored-vector-order\n"
"  --fused-order | --split-order\n"
"  --lazy-defender | --eager-defender\n"
"  --move-reserve-cap N          pseudo-move vector capacity (default 96)\n"
"  --no-move-reserve             disable pseudo-move vector preallocation\n"
"  --order-min-size N | --order-all\n"
"  --bucket-order | --stable-sort-order\n"
"  --score-mates | --no-mate-score\n"
"  --static-pseudo | --vector-pseudo\n"
"\n"
"DFPN route (unpromoted; slower than the default at every measured depth):\n"
"  --dfpn-sort | --dfpn-no-sort  sort moves at DFPN nodes (default: no)\n"
"  --dfpn-epsilon-64 N           1+epsilon threshold widening, in 1/64ths\n"
"  --dfpn-node-limit N           cap preconditioner nodes (0 = unlimited)\n"
"  --dfpn-share-disproofs        default; publish exact disproofs\n"
"  --dfpn-hints-only             contribute ordering hints only\n"
"\n"
"Compatibility:\n"
"  -b, -1                        accepted and ignored\n"
"  --fast-check-score, --exact-check-score, --score-checks, --no-check-score\n"
"                                accepted; check scoring is a single shared\n"
"                                plane query, so these select nothing\n"
"  WinChest special-mate variants, NOT IMPLEMENTED. Each selects a different\n"
"  problem rather than a tuning knob, so they are rejected, not ignored:\n"
"    -C N                        ChecksOnly bitmask; -C 1 IS implemented\n"
"    -R N                        examine threats only\n"
"    -K N                        limit defender king mobility\n"
"    -P N                        limit the set of moving pieces\n"
"    -X N                        limit maximum moves\n"
"    -I N                        threat flags\n"
"    -n N, -N N                  accepted with a value, same treatment\n"
"  Pass --allow-unimplemented to search the unrestricted problem instead.\n"
"  -C 1 restricts the attacker to check-moves (serial-check mate) and is\n"
"  supported. ChecksOnly bits 2/4/8/16 and the other variants are not.\n"
"  Chest 3.19 has none of these; they are WinChest extensions.\n"
"\n"
"  -h, --help                    this message\n"
"  -V, --version                 version\n"
"\n"
"Exit codes: 0 success, 2 usage error.\n";
}

int main(int argc, char** argv) {
    SearchConfig config;
    int requested_depth = 0;
    int perft_depth = 0;
    bool perft_divide = false;
    bool read_stdin = false;
    bool list_legal = false;

    // Pre-scan: this flag must work regardless of where it appears, otherwise
    // it only takes effect when written before the option it excuses.
    bool allow_unimplemented = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--allow-unimplemented") {
            allow_unimplemented = true;
            break;
        }
    }

    auto usage_error = [](const std::string& message) {
        std::cerr << "echest: " << message << "\n"
                  << "Try 'echest --help' for usage.\n";
        return 2;
    };

    auto parse_size = [&](const char* text, std::size_t& out) {
        char* end = nullptr;
        unsigned long value = std::strtoul(text, &end, 10);
        if (end != text) {
            out = static_cast<std::size_t>(value);
            return true;
        }
        return false;
    };

    // Options taking a value must actually have one. Previously the value check
    // lived in the match condition, so a trailing `-M` failed to match and then
    // fell out of the loop entirely, silently doing nothing.
    auto need_value = [&](int& idx) -> const char* {
        if (idx + 1 >= argc) {
            return nullptr;
        }
        return argv[++idx];
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else if (arg == "-V" || arg == "--version") {
            print_version();
            return 0;
        } else if (arg == "--allow-unimplemented") {
            allow_unimplemented = true;
        } else if (arg == "-b" || arg == "-1" || arg == "-5") {
            // Chest-compatible flags with no effect on this engine.
        } else if (arg == "-z") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '-z' requires a mate depth");
            requested_depth = std::atoi(v);
        } else if (arg == "--route") {
            const char* rv = need_value(i);
            if (!rv) return usage_error("option '--route' requires a route name");
            std::string route_arg = rv;
            if (auto parsed = parse_route_kind(route_arg)) {
                config.route = *parsed;
            } else {
                std::cerr << "unsupported route '" << route_arg << "', using " << route_name(config.route) << "\n";
            }
        } else if (arg == "-") {
            read_stdin = true;
        } else if (arg == "--debug") {
            config.debug = true;
        } else if (arg == "--list-legal") {
            list_legal = true;
        } else if (arg == "--perft" || arg == "--perft-divide") {
            const char* v = need_value(i);
            if (!v) return usage_error("option " + arg + " requires a depth");
            perft_depth = std::atoi(v);
            if (perft_depth <= 0) return usage_error("option " + arg + " requires a positive depth");
            perft_divide = (arg == "--perft-divide");
        } else if (arg == "--emit-proof") {
            config.emit_proof = true;
        } else if (arg == "--profile") {
            config.profile = true;
        } else if (arg == "--no-profile") {
            config.profile = false;
        } else if (arg == "--score-mates") {
            config.score_mates = true;
        } else if (arg == "--no-mate-score") {
            config.score_mates = false;
        } else if (arg == "--score-checks") {
            config.score_checks = true;
        } else if (arg == "--no-check-score") {
            config.score_checks = false;
        } else if (arg == "--fast-check-score") {
            config.fast_check_score = true;
        } else if (arg == "--exact-check-score") {
            config.fast_check_score = false;
        } else if (arg == "--refutation-hints") {
            config.refutation_hints = true;
        } else if (arg == "--no-refutation-hints") {
            config.refutation_hints = false;
        } else if (arg == "--proof-hints") {
            config.proof_hints = true;
        } else if (arg == "--no-proof-hints") {
            config.proof_hints = false;
        } else if (arg == "--tt-reserve") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--tt-reserve' requires a bucket count");
            if (!parse_size(v, config.tt_reserve)) return usage_error("option '--tt-reserve' expects a number");
        } else if (arg == "--move-reserve") {
            config.move_reserve = true;
        } else if (arg == "--no-move-reserve") {
            config.move_reserve = false;
        } else if (arg == "--move-reserve-cap") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--move-reserve-cap' requires a capacity");
            std::size_t value = 0;
            if (!parse_size(v, value) || value == 0) return usage_error("option '--move-reserve-cap' expects a positive number");
            config.move_reserve = true;
            config.move_reserve_capacity = value;
        } else if (arg == "--inplace-order") {
            config.inplace_order = true;
        } else if (arg == "--scored-vector-order") {
            config.inplace_order = false;
        } else if (arg == "--bucket-order") {
            config.bucket_order = true;
            config.inplace_order = true;
        } else if (arg == "--stable-sort-order") {
            config.bucket_order = false;
        } else if (arg == "--keep-iter-tt") {
            config.keep_iter_tt = true;
        } else if (arg == "--clear-iter-tt") {
            config.keep_iter_tt = false;
        } else if (arg == "--ordered-check-shortcut") {
            config.ordered_check_shortcut = true;
        } else if (arg == "--no-ordered-check-shortcut") {
            config.ordered_check_shortcut = false;
        } else if (arg == "--static-pseudo") {
            config.static_pseudo = true;
        } else if (arg == "--vector-pseudo") {
            config.static_pseudo = false;
        } else if (arg == "--order-min-size") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--order-min-size' requires a size");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--order-min-size' expects a number");
            config.order_min_size = std::max<std::size_t>(2, value);
        } else if (arg == "--threads") {
            const char* tv = need_value(i);
            if (!tv) return usage_error("option '--threads' requires a count or 'auto'");
            std::string value = tv;
            if (value == "auto") {
                // Root-split parallelism saturates. Measured solve rate at a
                // 5 s budget was flat from 16 threads upward -- mate-in-8 sat
                // at 14/24 for 16, 24 and 32 threads, and mate-in-10 at 3/20
                // across all of them -- because additional workers contribute
                // duplicated nodes rather than new search. Uncapped `auto` on a
                // large machine therefore burns cores for no capability.
                //
                // An explicit --threads N is never capped; only `auto` is.
                const unsigned hw = std::thread::hardware_concurrency();
                const int detected = hw > 0 ? static_cast<int>(hw) : 1;
                config.threads = std::min(detected, AUTO_THREAD_CAP);
            } else {
                std::size_t parsed = 0;
                if (!parse_size(value.c_str(), parsed) || parsed == 0) {
                    return usage_error("option '--threads' expects a positive number or 'auto'");
                }
                config.threads = static_cast<int>(parsed);
            }
        } else if (arg == "--single-thread") {
            config.threads = 1;
        } else if (arg == "--parallel-min-nodes") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--parallel-min-nodes' requires a node count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--parallel-min-nodes' expects a number");
            config.parallel_min_nodes = static_cast<std::uint64_t>(value);
        } else if (arg == "--no-parallel-gate") {
            config.parallel_min_nodes = 0;
        } else if (arg == "--root-sequential-first") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--root-sequential-first' requires a count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--root-sequential-first' expects a number");
            config.root_sequential_first = static_cast<int>(value);
        } else if (arg == "--root-split-all") {
            config.root_sequential_first = 0;
        } else if (arg == "--direct-depth") {
            config.direct_depth = true;
        } else if (arg == "--iterative-depth") {
            config.direct_depth = false;
        } else if (arg == "--time-limit") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--time-limit' requires seconds");
            char* end = nullptr;
            const double value = std::strtod(v, &end);
            if (end == v || value < 0.0) return usage_error("option '--time-limit' expects a non-negative number of seconds");
            config.time_limit = value;
        } else if (arg == "--dfpn-epsilon-64") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--dfpn-epsilon-64' requires a value");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--dfpn-epsilon-64' expects a number");
            config.dfpn_epsilon_64 = static_cast<int>(value);
        } else if (arg == "--dfpn-sort") {
            config.dfpn_sort = true;
        } else if (arg == "--dfpn-no-sort") {
            config.dfpn_sort = false;
        } else if (arg == "--dfpn-hints-only") {
            config.dfpn_share_disproofs = false;
        } else if (arg == "--dfpn-share-disproofs") {
            config.dfpn_share_disproofs = true;
        } else if (arg == "--dfpn-node-limit") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--dfpn-node-limit' requires a node count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--dfpn-node-limit' expects a number");
            config.dfpn_node_limit = static_cast<std::uint64_t>(value);
        } else if (arg == "--lazy-defender") {
            config.lazy_defender = true;
        } else if (arg == "--eager-defender") {
            config.lazy_defender = false;
        } else if (arg == "--fused-order") {
            config.fused_order = true;
        } else if (arg == "--split-order") {
            config.fused_order = false;
        } else if (arg == "--shared-tt") {
            config.shared_tt = true;
        } else if (arg == "--private-tt") {
            config.shared_tt = false;
        } else if (arg == "--shared-tt-shards") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--shared-tt-shards' requires a shard count");
            std::size_t value = 0;
            if (!parse_size(v, value) || value == 0) return usage_error("option '--shared-tt-shards' expects a positive number");
            config.shared_tt_shards = value;
        } else if (arg == "--order-all") {
            config.order_min_size = 2;
        } else if (arg == "-M") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '-M' requires a size in MB");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '-M' expects a number");
            config.memory_mb = value; // 0 means unbounded
        } else if (arg == "-C") {
            // WinChest ChecksOnly is a bitmask, not a count:
            //   +1 only own check-moves      +2 no opponent checks
            //   +4 no opponent captures      +8/+16 further restrictions
            // Bit 1 is implemented; the others select problems this engine
            // does not solve, so they are refused rather than ignored.
            const char* v = need_value(i);
            if (!v) return usage_error("option -C requires a value");
            const int mask = std::atoi(v);
            if (mask == 0 || mask == -1) {
                config.checks_only = false;
            } else if (mask == 1) {
                config.checks_only = true;
            } else if (!allow_unimplemented) {
                return usage_error("option -C " + std::string(v) + " selects WinChest "
                                   "ChecksOnly bits this engine does not implement. Only "
                                   "-C 1 (attacker plays check-moves only) is supported; "
                                   "bits 2, 4, 8 and 16 are not. Pass "
                                   "--allow-unimplemented to search unrestricted.");
            }
        } else if (arg == "-R" || arg == "-K" || arg == "-P" ||
                   arg == "-X" || arg == "-I" || arg == "-n" || arg == "-N") {
            if (!need_value(i)) {
                return usage_error("option " + arg + " requires a value");
            }
            if (!allow_unimplemented) {
                std::string meaning = "a restricted mate variant";
                if (arg == "-R") meaning = "examine threats only";
                else if (arg == "-K") meaning = "limit defender king mobility";
                else if (arg == "-P") meaning = "limit the set of moving pieces";
                else if (arg == "-X") meaning = "limit maximum moves";
                else if (arg == "-I") meaning = "threat flags";
                return usage_error("option " + arg + " selects a WinChest special-mate "
                                   "variant (" + meaning + ") that this engine does not "
                                   "implement. It solves ordinary directmates only, so "
                                   "the restriction would be silently ignored and the "
                                   "answer would be to a different problem. Pass "
                                   "--allow-unimplemented to search unrestricted.");
            }
        } else {
            return usage_error("unknown option " + arg);
        }
    }

    if (read_stdin) {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (perft_depth > 0) {
                if (perft_divide) perft_divide_line(line, perft_depth); else perft_line(line, perft_depth);
            } else if (list_legal) {
                list_legal_line(line);
            } else {
                solve_line(line, requested_depth, config);
            }
        }
    } else {
        std::ostringstream buffer;
        buffer << std::cin.rdbuf();
        if (perft_depth > 0) {
            if (perft_divide) perft_divide_line(buffer.str(), perft_depth); else perft_line(buffer.str(), perft_depth);
        } else if (list_legal) {
            list_legal_line(buffer.str());
        } else {
            solve_line(buffer.str(), requested_depth, config);
        }
    }
    return 0;
}
