// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// MateProver initial exact directmate prover.
//
// This is a conservative first checkpoint for the E rewrite line. It favors
// auditable correctness over final performance. Later E milestones should
// replace the array board with a bitboard/incremental engine while preserving
// this proof interface and verifier behavior.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
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
#include "prove.h"
#include "rootsplit.h"
#include "dfpn.h"
#include "routes.h"
#include "report.h"
#include "solve.h"

using namespace mateprover;


// Ceiling applied to `--threads auto`. See the comment at its use site: root
// split saturates, so detected-core-count parallelism wastes cores above this.
constexpr int AUTO_THREAD_CAP = 16;

// The single source of truth for the version. CMakeLists.txt parses this line
// rather than declaring its own: it used to declare 0.1.0 and pass it in, while
// a direct g++ build of the same source fell back to "0.1.0-dev". Two builds of
// identical code reported different versions, and neither string said which was
// which.
#define MATEPROVER_VERSION "1.0.0"

void print_version() {
    std::cout << "mateprover " << MATEPROVER_VERSION << "\n";
}

void print_usage() {
    std::cout <<
"mateprover " MATEPROVER_VERSION " - exact directmate prover with machine-checkable proofs\n"
"\n"
"Usage:\n"
"  mateprover [options] -            read EPD/FEN lines from stdin\n"
"  mateprover [options] < file       read a single position from stdin\n"
"\n"
"Problem:\n"
"  -z N                          requested mate depth. Without it the depth is\n"
"                                read from a #N or 'dm N' token on the input\n"
"                                line; lines beginning with # are comments\n"
"  --goal mate|stalemate|selfmate|selfstalemate|helpmate|helpstalemate\n"
"                                what must be forced (default mate). Each goal\n"
"                                is a DIFFERENT problem, not an easier or harder\n"
"                                one: a checkmate FAILS a stalemate goal. Each\n"
"                                has its own result token, never 'dm N'\n"
"  --stalemate                   shorthand for --goal stalemate    ('sm N')\n"
"  --selfmate                    shorthand for --goal selfmate: the attacker\n"
"                                forces the DEFENDER to mate him. Reported as\n"
"                                'sfm N'. Runs an exact route of its own, with\n"
"                                no root split and no preconditioner\n"
"  --selfstalemate               shorthand for --goal selfstalemate ('ssm N'):\n"
"                                as selfmate, but the defender must STALEMATE\n"
"                                the attacker\n"
"  --helpmate                    shorthand for --goal helpmate      ('hm N'):\n"
"                                COOPERATIVE. Both sides work toward the mate,\n"
"                                so there is no defender and no adversarial\n"
"                                search: no proof numbers, no restriction\n"
"                                portfolio, no root split. The side to move at\n"
"                                the root is the one that gets mated, and h#N\n"
"                                is N moves by EACH side\n"
"  --helpstalemate               shorthand for --goal helpstalemate ('hsm N')\n"
"  --route NAME                  dfpn (default) | depth-first | shallow-fast\n"
"  --direct-depth                prove \"a mate within N\" by searching at N\n"
"                                directly; better solve rate at a fixed\n"
"                                budget, but not guaranteed shortest\n"
"  --iterative-depth             default; prove \"the shortest mate is N\"\n"
"  --portfolio                   try restricted searches within the time\n"
"                                budget before giving up; a restriction only\n"
"                                removes attacker options, so any mate found\n"
"                                is real. Needs --time-limit\n"
"  --portfolio-parallel          run the portfolio entries concurrently, each\n"
"                                with the full budget; uses cores that root\n"
"                                splitting saturates on\n"
"  --no-portfolio                single unrestricted search. The portfolio\n"
"                                is the default whenever --time-limit is set\n"
"  --portfolio-lanes N           cap concurrent lanes (0 = all, the default).\n"
"                                Lanes contend for cores, so fewer can be\n"
"                                faster on a small machine\n"
"  --route-lane-threads N        threads for each route-diversity lane on the\n"
"                                non-directmate goals (default 1). The split\n"
"                                reaches positions a single thread cannot, but\n"
"                                takes cores from the lanes doing the rest\n"
"\n"
"Resources:\n"
"  -M N                          total table budget in MB across every table\n"
"                                alive at once, split between portfolio lanes\n"
"                                and --parallel-positions workers; honoured as\n"
"                                an entry ceiling, not a hard RSS bound;\n"
"                                0 = unbounded. Unset, the budget is per table\n"
"                                and the total scales with the table count\n"
"                                (default 256)\n"
"  --threads N | auto            root-split worker threads (default: auto).\n"
"                                auto = min(cores,16):\n"
"                                the split saturates above that, so extra\n"
"                                cores add no capability. Explicit N is not\n"
"                                capped.\n"
"  --single-thread               force sequential search\n"
"  --parallel-min-nodes N        run a depth sequentially until it exceeds\n"
"                                N nodes, then split (default 500)\n"
"  --no-parallel-gate            always split, never probe sequentially\n"
"  --parallel-positions N        solve N positions from stdin at once, emitting\n"
"                                results in input order (default 1). Uses the\n"
"                                cores that per-position parallelism cannot;\n"
"                                answers stop streaming as they are produced\n"
"  --node-limit N                deterministic budget: stop after N nodes and\n"
"                                report 'timeout', claiming nothing. Same\n"
"                                answer on every run and machine, unlike\n"
"                                --time-limit (0 = unlimited)\n"
"  --time-limit S                wall-clock budget in seconds; on expiry the\n"
"                                search reports \"timeout\", never a mate\n"
"  --root-sequential-first N     search N root moves sequentially before\n"
"                                splitting; cuts wasted parallel work\n"
"  --root-split-all              default; split every root move\n"
"  --shared-tt | --private-tt    share one exact proof table across workers\n"
"  --shared-tt-shards N          shards for the shared table (default 256)\n"
"  --tt-reserve N                pre-reserve N table buckets\n"
"\n"
"Analysis:\n"
"  --all-solutions | --duals     report EVERY root move that solves, not just\n"
"                                the first. A second solution at the root is a\n"
"                                dual and the problem is cooked; the line gains\n"
"                                'keys N', the list, and 'sound' or 'cooked'.\n"
"                                Forces one unrestricted search: a restriction\n"
"                                removes attacker options, which is sound for\n"
"                                proving and would UNDERCOUNT duals\n"
"  --tree | -L                   print the solution tree, indented, from the\n"
"                                certificate -- so what is shown is exactly what\n"
"                                was proved. With --all-solutions it also prints\n"
"                                the refutation table: for each root move that\n"
"                                fails, a defence that survives it\n"
"  --short-notation | -S         algebraic rather than coordinates in the tree\n"
"  --tree-extra N | -l           accepted for compatibility; has no effect. The\n"
"                                certificate is complete to the terminal and\n"
"                                there is nothing beyond it to print\n"
"  --suppress-duals | -u         accepted for compatibility; has no effect. A\n"
"  --suppress-all-duals | -U     certificate records ONE attacker move per node,\n"
"                                so a printed tree never contains a sub-line\n"
"                                dual to suppress. Top-level duals are never\n"
"                                suppressed by either engine; --all-solutions\n"
"                                reports them\n"
"  --default-depth N | -Z        depth for input lines that carry none (-z\n"
"                                OVERRIDES a depth the line does carry)\n"
"  --successors | -x             run the job on every position reachable in one\n"
"                                move instead of on this one (\"black moves but\n"
"                                loses in x moves\")\n"
"  --check-legal | -c            report legality and stop, without searching\n"
"  --list-san                    every legal move as '<uci>=<san>'\n"
"\n"
"Output:\n"
"  -5                            UCI-style coordinate moves (compatibility)\n"
"  --emit-proof                  append a recursive JSON proof certificate\n"
"  --print-config                print the effective configuration as JSON\n"
"                                and exit; every default already resolved\n"
"  --profile                     emit per-position counters to stderr\n"
"  --no-profile                  default; no counters\n"
"  --debug                       verbose search diagnostics on stderr\n"
"  --list-legal                  list legal moves instead of solving\n"
"  --perft N                     perft counts for depths 1..N\n"
"  --perft-divide N              per-root-move perft breakdown at depth N\n"
"\n"
"Search tuning (all preserve exactness; see docs/ARCHITECTURE.md):\n"
"  --proof-hints | --no-proof-hints\n"
"                                default: --proof-hints\n"
"  --refutation-hints | --no-refutation-hints\n"
"                                default: --no-refutation-hints (measured harmful)\n"
"  --keep-iter-tt | --clear-iter-tt\n"
"                                default: --keep-iter-tt\n"
"  --ordered-check-shortcut | --no-ordered-check-shortcut\n"
"                                default: --ordered-check-shortcut\n"
"  --inplace-order | --scored-vector-order\n"
"                                default: --inplace-order\n"
"  --fused-order | --split-order\n"
"                                default: --fused-order\n"
"  --lazy-defender | --eager-defender\n"
"                                default: --eager-defender\n"
"  --move-reserve-cap N          pseudo-move vector capacity (default 96)\n"
"  --no-move-reserve             disable pseudo-move vector preallocation\n"
"  --order-min-size N | --order-all\n"
"                                default: --order-min-size 2\n"
"  --bucket-order | --stable-sort-order\n"
"                                default: --stable-sort-order\n"
"  --score-mates | --no-mate-score\n"
"                                default: --no-mate-score\n"
"  --static-pseudo | --vector-pseudo\n"
"                                default: --vector-pseudo\n"
"\n"
"DFPN route (the default; a preconditioner, never an output authority):\n"
"  --dfpn-final-depth-only | --dfpn-every-depth\n"
"                                default: --dfpn-final-depth-only. Its work\n"
"                                cannot carry across depths, so only the\n"
"                                deepest iteration repays the cost\n"
"  --dfpn-check-bias N           weight an AND node's proof estimate when the\n"
"                                defender is not in check (default 1, off;\n"
"                                measured harmful -- see architecture 47)\n"
"  --dfpn-min-depth N            skip the preconditioner below depth N; under\n"
"                                iterative deepening its work cannot carry\n"
"                                across depths (default 1, no skipping)\n"
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
"WinChest special-mate variants. -C, -R, -K, -P and -X are implemented and\n"
"validated against the WinChest binary. Each selects a different problem, so\n"
"the unimplemented ones are rejected rather than ignored:\n"
"    -C N                        ChecksOnly bitmask (0..31, -1 = off):\n"
"                                  1 only own check-moves\n"
"                                  2 no opponent checks\n"
"                                  4 no opponent captures\n"
"                                  8 no own captures*\n"
"                                 16 no own check-moves*\n"
"                                (* the mating move is exempt; 1 and 16\n"
"                                are contradictory and refused)\n"
"    -K N                        KingSquares: defender king has at most N\n"
"                                squares, counting the one it stands on\n"
"    -P N                        PieceLimit: at most N defender pieces may\n"
"                                move\n"
"    -X N                        MaxMoves: at most N defender moves in total\n"
"    -R N                        ThreatDepth: only moves threatening mate\n"
"                                within |N| after a defender null move;\n"
"                                N>0 check-threats, N<0 quiet threats\n"
"    -I N                        threat flags (not implemented)\n"
"    -n N, -N N                  accepted with a value, same treatment\n"
"  Pass --allow-unimplemented to search the unrestricted problem instead.\n"
"  Off values follow WinChest: -C 0, -K 0 or 9, -P 0 or 16, -X 0 or 222.\n"
"  Negative values select automatic-mode bounds, which are not implemented.\n"
"  Chest 3.19 has none of these; they are WinChest extensions.\n"
"\n"
"  -h, --help                    this message\n"
"  -V, --version                 version\n"
"\n"
"Exit codes: 0 success, 2 usage error.\n";
}

int main(int argc, char** argv) {
    SearchConfig config;
    bool print_config = false;
    int requested_depth = 0;
    int perft_depth = 0;
    bool perft_divide = false;
    bool read_stdin = false;
    bool list_legal = false;
    bool list_san = false;

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
        std::cerr << "mateprover: " << message << "\n"
                  << "Try 'mateprover --help' for usage.\n";
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
        } else if (arg == "--list-san") {
            list_san = true;
        } else if (arg == "--perft" || arg == "--perft-divide") {
            const char* v = need_value(i);
            if (!v) return usage_error("option " + arg + " requires a depth");
            perft_depth = std::atoi(v);
            if (perft_depth <= 0) return usage_error("option " + arg + " requires a positive depth");
            perft_divide = (arg == "--perft-divide");
        } else if (arg == "--emit-proof") {
            config.emit_proof = true;
        } else if (arg == "--print-config") {
            print_config = true;
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
        } else if (arg == "--portfolio") {
            config.portfolio = true;
        } else if (arg == "--portfolio-parallel") {
            config.portfolio = true;
            config.portfolio_parallel = true;
        } else if (arg == "-l" || arg == "--tree-extra") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '-l' requires a move count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '-l' expects a number");
            config.tree_extra_moves = static_cast<int>(value);
        } else if (arg == "-u" || arg == "--suppress-duals") {
            // Chest increments the level each time -u is given.
            ++config.dual_suppression;
        } else if (arg == "-U" || arg == "--suppress-all-duals") {
            config.suppress_all_duals = true;
        } else if (arg == "-Z" || arg == "--default-depth") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '-Z' requires a depth");
            std::size_t value = 0;
            if (!parse_size(v, value) || value == 0) {
                return usage_error("option '-Z' expects a positive depth");
            }
            config.default_depth = static_cast<int>(value);
        } else if (arg == "--all-solutions" || arg == "--duals") {
            config.all_solutions = true;
        } else if (arg == "--successors" || arg == "-x") {
            config.successors = true;
        } else if (arg == "--check-legal" || arg == "-c") {
            config.legality_only = true;
        } else if (arg == "--tree" || arg == "-L") {
            config.print_tree = true;
        } else if (arg == "--short-notation" || arg == "-S") {
            config.short_notation = true;
        } else if (arg == "--no-portfolio") {
            config.portfolio = false;
        } else if (arg == "--portfolio-lanes") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--portfolio-lanes' requires a count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--portfolio-lanes' expects a number");
            config.portfolio_lanes = static_cast<int>(value); // 0 means every lane
        } else if (arg == "--route-lane-threads") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--route-lane-threads' requires a count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--route-lane-threads' expects a number");
            config.route_lane_threads = static_cast<int>(value); // 0 means the default
        } else if (arg == "--direct-depth") {
            config.direct_depth = true;
        } else if (arg == "--iterative-depth") {
            config.direct_depth = false;
        } else if (arg == "--parallel-positions") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--parallel-positions' requires a count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--parallel-positions' expects a number");
            if (value < 1) return usage_error("option '--parallel-positions' expects at least 1");
            config.parallel_positions = static_cast<int>(value);
        } else if (arg == "--node-limit") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--node-limit' requires a node count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--node-limit' expects a number");
            config.node_limit = static_cast<std::uint64_t>(value);
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
        } else if (arg == "--dfpn-final-depth-only") {
            config.dfpn_final_depth_only = true;
        } else if (arg == "--dfpn-every-depth") {
            config.dfpn_final_depth_only = false;
        } else if (arg == "--goal") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--goal' requires mate or stalemate");
            const std::string g(v);
            if (g == "mate") {
                config.goal = Goal::Mate;
            } else if (g == "stalemate") {
                config.goal = Goal::Stalemate;
            } else if (g == "selfmate") {
                config.goal = Goal::Selfmate;
            } else if (g == "selfstalemate") {
                config.goal = Goal::Selfstalemate;
            } else if (g == "helpmate") {
                config.goal = Goal::Helpmate;
            } else if (g == "helpstalemate") {
                config.goal = Goal::Helpstalemate;
            } else {
                return usage_error("option '--goal' expects mate, stalemate, selfmate, "
                                   "selfstalemate, helpmate or helpstalemate");
            }
        } else if (arg == "--stalemate") {
            config.goal = Goal::Stalemate;
        } else if (arg == "--selfmate") {
            config.goal = Goal::Selfmate;
        } else if (arg == "--selfstalemate") {
            config.goal = Goal::Selfstalemate;
        } else if (arg == "--helpmate") {
            config.goal = Goal::Helpmate;
        } else if (arg == "--helpstalemate") {
            config.goal = Goal::Helpstalemate;
        } else if (arg == "--dfpn-check-bias") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--dfpn-check-bias' requires a weight");
            std::size_t value = 0;
            if (!parse_size(v, value) || value < 1) {
                return usage_error("option '--dfpn-check-bias' expects a number >= 1");
            }
            config.dfpn_check_bias = static_cast<int>(value);
        } else if (arg == "--dfpn-min-depth") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--dfpn-min-depth' requires a depth");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--dfpn-min-depth' expects a number");
            config.dfpn_min_depth = static_cast<int>(value);
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
            // An explicit budget is a total across every table alive at
            // once; the default is per table. See architecture 42, 44.
            config.memory_is_total = true;
        } else if (arg == "-C") {
            // ChecksOnly bitmask: 1 own checks only, 2 no opponent checks,
            // 4 no opponent captures, 8 no own captures, 16 no own checks
            // except the mating move. 0 and -1 are off.
            const char* v = need_value(i);
            if (!v) return usage_error("option -C requires a value");
            const int mask = std::atoi(v);
            if (mask == -1 || mask == 0) {
                config.checks_mask = 0;
            } else if (mask < 0 || mask > 31) {
                return usage_error("option -C expects 0..31 (a bitmask) or -1");
            } else if ((mask & 1) && (mask & 16)) {
                // The manual is explicit that these must not be combined:
                // one demands every move be a check, the other forbids it.
                return usage_error("option -C " + std::string(v) + " combines bit 1 "
                                   "(only check-moves) with bit 16 (no check-moves), "
                                   "which are contradictory.");
            } else {
                config.checks_mask = mask;
            }
        } else if (arg == "-K" || arg == "-P" || arg == "-X") {
            // KingSquares / PieceLimit / MaxMoves: each bounds what the
            // defender may do after an attacker move, so each is a
            // per-move filter rather than a search heuristic.
            //   -K N  king has at most N squares, counting its own
            //   -P N  at most N defender pieces can move
            //   -X N  at most N defender moves in total
            // Off values follow WinChest: K 0 or 9, P 0 or 16, X 0 or 222.
            const char* v = need_value(i);
            if (!v) return usage_error("option " + arg + " requires a value");
            const int n = std::atoi(v);
            if (n < 0) {
                // Negative values are lower bounds for WinChest's automatic
                // search, which this engine does not have.
                if (!allow_unimplemented) {
                    return usage_error("option " + arg + " with a negative value selects "
                                       "WinChest automatic-mode bounds, which this engine "
                                       "does not implement. Use a positive limit, or "
                                       "--allow-unimplemented to search unrestricted.");
                }
            } else if (arg == "-K") {
                config.king_squares = (n == 0 || n >= 9) ? 0 : n;
            } else if (arg == "-P") {
                config.piece_limit = (n == 0 || n >= 16) ? 0 : n;
            } else {
                config.max_defender_moves = (n == 0 || n >= 222) ? 0 : n;
            }
        } else if (arg == "-R") {
            // ThreatDepth: examine only moves that threaten mate within N
            // after a defender null move. The sign selects the threat
            // search: positive uses check-moves only, negative any move.
            const char* v = need_value(i);
            if (!v) return usage_error("option -R requires a value");
            const int n = std::atoi(v);
            config.threat_depth = (n == 0 || n >= 125 || n <= -125) ? 0 : n;
        } else if (arg == "-I" || arg == "-n" || arg == "-N") {
            if (!need_value(i)) {
                return usage_error("option " + arg + " requires a value");
            }
            if (!allow_unimplemented) {
                return usage_error("option " + arg + " selects a WinChest special-mate "
                                   "variant (threat flags) that this engine does not "
                                   "implement. It would be silently ignored and the "
                                   "answer would be to a different problem. Pass "
                                   "--allow-unimplemented to search unrestricted.");
            }
        } else {
            return usage_error("unknown option " + arg);
        }
    }

    // Resolve the unspecified-threads sentinel to the same value `--threads
    // auto` computes. The default was 1, which meant the shipped configuration
    // used a single core on any machine unless the user knew to ask otherwise.
    if (config.threads < 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        const int detected = hw > 0 ? static_cast<int>(hw) : 1;
        config.threads = std::min(detected, AUTO_THREAD_CAP);
    }

    // Report the configuration that would actually be used and stop. Printed
    // after every default and sentinel is resolved, so this is the effective
    // configuration rather than a restatement of the flags given.
    if (print_config) {
        emit_config_json(config);
        return 0;
    }

    // Solve several positions at once, each into its own buffer, emitting the
    // buffers in input order.
    //
    // Worth having because root-split parallelism inside one position now
    // contributes nothing (architecture 32): the engine uses about one core per
    // portfolio lane whatever --threads says, so on a larger machine most cores
    // sit idle during a batch. Positions are independent -- no state crosses
    // between them, which is gated -- so this is the one axis where more cores
    // still buy throughput.
    //
    // Off by default: with one position in flight the output streams as it is
    // produced, which is what makes the service mode work.
    std::vector<std::string> pending;
    auto flush_pending = [&]() {
        if (pending.empty()) {
            return;
        }
        // Workers pull the next position as they finish, rather than each taking
        // one and joining at a barrier.
        //
        // The barrier version made a chunk take as long as its slowest member
        // while every other core idled, and positions differ enormously -- some
        // resolve instantly, some run to the whole budget. Measured, that cost
        // most of the available speedup, more than thread oversubscription (~3%)
        // or table size (~5% across a 32x range) (34, 35).
        const std::size_t width = std::min<std::size_t>(
            pending.size(), static_cast<std::size_t>(config.parallel_positions));
        std::vector<std::ostringstream> buffers(pending.size());
        std::vector<char> done(pending.size(), 0);
        std::atomic<std::size_t> next{0};
        std::mutex done_mutex;
        std::condition_variable done_signal;

        auto worker = [&]() {
            for (;;) {
                const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= pending.size()) {
                    return;
                }
                solve_line(pending[i], requested_depth, config, buffers[i]);
                {
                    std::lock_guard<std::mutex> lock(done_mutex);
                    done[i] = 1;
                }
                done_signal.notify_all();
            }
        };

        std::vector<std::thread> pool;
        pool.reserve(width);
        for (std::size_t w = 0; w < width; ++w) {
            try {
                pool.emplace_back(worker);
            } catch (const std::system_error&) {
                break;      // the OS refused a thread; the rest carry the load
            }
        }
        if (pool.empty()) {
            worker();       // no threads at all: solve here
        }

        // Emit in input order as each result becomes ready, rather than waiting
        // for the whole chunk. A chunk holds four positions per worker so the
        // queue has imbalance to absorb, which would otherwise mean 32 positions
        // of silence at width 8 -- minutes, on a corpus of hard problems.
        for (std::size_t i = 0; i < pending.size(); ++i) {
            std::unique_lock<std::mutex> lock(done_mutex);
            done_signal.wait(lock, [&]() { return done[i] != 0; });
            lock.unlock();
            std::cout << buffers[i].str();
            std::cout.flush();
        }
        for (std::thread& t : pool) {
            t.join();
        }
        pending.clear();
    };

    if (read_stdin) {
        std::string line;
        bool first_line = true;
        while (std::getline(std::cin, line)) {
            // Strip a leading UTF-8 byte order mark. Windows is the primary
            // platform here, and both Notepad and PowerShell's `Set-Content
            // -Encoding utf8` prepend EF BB BF; without this the first position
            // of such a file fails as "error input" while every later line
            // succeeds, which for a single-position file means the only
            // position is lost with no indication why.
            if (first_line) {
                first_line = false;
                if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
                    line.erase(0, 3);
                }
            }
            // Skip comment lines. EPD corpora are routinely commented -- this
            // repository's own tests/mates.epd opens with two such lines -- and
            // reporting "error input" for each made piping a corpus in produce
            // an error for content that is not input at all. A FEN never begins
            // with '#', so this cannot mask a real position.
            const std::size_t first = line.find_first_not_of(" \t\r");
            if (first != std::string::npos && line[first] == '#') {
                continue;
            }
            if (perft_depth > 0) {
                if (perft_divide) perft_divide_line(line, perft_depth); else perft_line(line, perft_depth);
            } else if (list_legal) {
                list_legal_line(line);
            } else if (list_san) {
                list_san_line(line);
            } else if (config.parallel_positions > 1) {
                pending.push_back(line);
                // Four positions per worker before flushing: the queue can only
                // balance load it can see, and a chunk equal to the worker count
                // degenerates back into one position each.
                if (pending.size() >= static_cast<std::size_t>(config.parallel_positions) * 4) {
                    flush_pending();
                }
            } else {
                solve_line(line, requested_depth, config);
            }
        }
        flush_pending();
    } else {
        std::ostringstream buffer;
        buffer << std::cin.rdbuf();
        if (perft_depth > 0) {
            if (perft_divide) perft_divide_line(buffer.str(), perft_depth); else perft_line(buffer.str(), perft_depth);
        } else if (list_legal) {
            list_legal_line(buffer.str());
        } else if (list_san) {
            list_san_line(buffer.str());
        } else {
            solve_line(buffer.str(), requested_depth, config);
        }
    }
    return 0;
}
