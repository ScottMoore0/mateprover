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
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <cstddef>
#include <iostream>
#include <limits>
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

// The single source of truth for the version. CMakeLists.txt parses this line
// rather than declaring its own: it used to declare 0.1.0 and pass it in, while
// a direct g++ build of the same source fell back to "0.1.0-dev". Two builds of
// identical code reported different versions, and neither string said which was
// which.
//
// Above the includes rather than below them, because uci.h reports it in the
// `id name` line the protocol requires.
#define MATEPROVER_VERSION "0.1.0"

// Modules are included in their original order into this single translation
// unit, so the preprocessed result is textually equivalent to the previous
// single-file build. Compilation stays a unity build deliberately: the search
// depends on cross-module inlining in its hottest paths.
#include "types.h"
#include "table.h"
#include "search_state.h"
#include "board.h"
#include "movegen.h"
#include "kingescape.h"
#include "predicate.h"
#include "ordering.h"
#include "prooftable.h"
#include "prove.h"
#include "rootsplit.h"
#include "dfpn.h"
#include "routes.h"
#include "retro.h"
#include "report.h"
#include "solve.h"
#include "uci.h"

using namespace mateprover;


// Ceiling applied to `--threads auto`, and the cores held back from it.
//
// Measured on a 16-core machine, d12, 20 seeded positions, 10s, portfolio on:
// 1, 2, 4 and 8 threads all solve 20/20 at a ~0.16s median; 16 solves 14/20 at
// 0.38s. Flat, then a cliff at exactly the core count. With the portfolio
// disabled the same cliff is far steeper (18/20 -> 5/20), so what breaks is
// root splitting itself rather than contention with the lanes - capping lanes
// made it worse, not better (1/2/4/all lanes -> 5/10/12/14 solved).
//
// The previous cap of 16 came from a sweep of 16, 24 and 32 threads that found
// them flat and concluded 16 was the knee. All three sit past the cliff; the
// sweep never included 1, 2, 4 or 8.
//
// RESERVE keeps the coordinator and the portfolio lanes off the root-split
// threads' cores, which is the mechanism of the cliff. CAP is the largest
// thread count measured safe.
constexpr int AUTO_THREAD_CAP = 8;
constexpr int AUTO_THREAD_RESERVE = 2;


// Resolve `--threads auto` (and the unspecified-threads sentinel) identically.
// These were two copies of the same expression; a fix applied to one and not
// the other would silently give `auto` and the default different meanings.
int resolve_auto_threads() {
    const unsigned hw = std::thread::hardware_concurrency();
    const int detected = hw > 0 ? static_cast<int>(hw) : 1;
    const int usable = std::max(1, detected - AUTO_THREAD_RESERVE);
    return std::min(usable, AUTO_THREAD_CAP);
}


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
"  --checks N | W:B              x-check chess: a side wins outright on giving\n"
"                                its Nth check. A VARIANT, not a goal -- it\n"
"                                composes with every --goal, so 3-check selfmate\n"
"                                and 3-check helpmate are ordinary jobs. W:B\n"
"                                gives the two sides different allowances. A\n"
"                                fifth Forsyth field on the input line states\n"
"                                the checks REMAINING ('3+3') or, Lichess-style,\n"
"                                those already given ('+1+0'), and overrides\n"
"                                this. Range 1..126\n"
"  --captures N | W:B            x-capture chess: a side wins outright on making\n"
"                                its Nth capture. The second variant rule, and\n"
"                                composable with the first, so --checks 3\n"
"                                --captures 5 is a legal game. W:B differs the\n"
"                                two sides. Range 1..126. A fifth Forsyth field\n"
"                                states quotas tagged, 'chk3+3,cap5+2'; an\n"
"                                untagged '3+3' still means checks\n"
"  --escape N | W:B              x-escape chess: a side LOSES when its own king\n"
"                                reaches an escape count of N. E is how many\n"
"                                squares that king could legally step to, so a\n"
"                                king walled in by its own men has E 0, and the\n"
"                                starting array is 0 for both sides. The THIRD\n"
"                                variant rule, and unlike the first two it is not\n"
"                                a countdown: E is measured afresh at every\n"
"                                position, can rise or fall, and reaching the\n"
"                                limit LOSES rather than wins -- so the winner is\n"
"                                the other side. W:B differs the two sides.\n"
"                                Range 1..8; a king's ring holds at most 8\n"
"                                squares, and a limit of 0 would end every game\n"
"                                at once. See also --escape-count\n"
"  --escape-win | --no-escape-win\n"
"                                default: on. Whether reaching the escape limit\n"
"                                ends the game at all\n"
"  --check-win | --no-check-win  default: on. Under --goal mate, whether filling\n"
"                                the CHECK quota counts as forcing the win. Off\n"
"                                demands checkmate specifically\n"
"  --capture-win | --no-capture-win\n"
"                                default: on. Under --goal mate, whether filling\n"
"                                a quota counts as forcing the win. Off demands\n"
"                                checkmate specifically. Only the mate goal has\n"
"                                the choice: the other goals each name a terminal\n"
"                                position, and a game that ended on a quota did\n"
"                                not reach it\n"
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
"                                is the default whenever --time-limit is set.\n"
"                                USE THIS WHEN YOU EXPECT NO SOLUTION. A\n"
"                                restricted lane can only ever FIND a mate; its\n"
"                                failure proves nothing, because it never looked\n"
"                                at the moves the restriction removed. Only the\n"
"                                unrestricted lane can answer 'there is none',\n"
"                                and while it works the other lanes compete with\n"
"                                it for cores to no purpose. On a measured\n"
"                                opening position the same question took 2.3 s\n"
"                                with this flag and had not finished in 90\n"
"                                minutes without it\n"
"  --restrict-checks N | --restrict-king N | --restrict-maxdef N |\n"
"  --restrict-threat N           apply ONE portfolio restriction directly, so\n"
"                                candidates can be swept without a rebuild. The\n"
"                                shipped portfolio was set-covered on mate-in-8\n"
"                                and never revisited for depth. Combine with\n"
"                                --no-portfolio to measure a restriction alone.\n"
"                                A restriction only removes ATTACKER options, so\n"
"                                any mate found under one is real.\n"
"  --beam-defender K             FINDING ONLY, and UNSOUND. At every defender\n"
"                                node examine only the first K replies in the\n"
"                                engine's own ordering and accept the node\n"
"                                once those K are refuted. A reply outside\n"
"                                the K is never examined, so a mate found\n"
"                                this way may be FALSE: the line carries\n"
"                                `beam K` and never a proof, and it must be\n"
"                                verified by a plain --direct-depth run.\n"
"                                Refused with --iterative-depth, --emit-proof\n"
"                                and every goal but mate. It exists because\n"
"                                pruning defender replies shrinks the tree\n"
"                                exponentially in depth, which no ordering\n"
"                                or speedup can. An experiment, not a mode\n"
"                                for answers\n"
"  --portfolio-lanes N           cap concurrent lanes (0 = all, the default).\n"
"                                Lanes contend for cores, so fewer can be\n"
"                                faster on a small machine\n"
"  --lane0-weight PCT            override the share of the portfolio budget\n"
"                                given to lane 0, the UNRESTRICTED search, as\n"
"                                a percent 1-100. The remaining lanes are\n"
"                                rescaled to divide what is left, keeping\n"
"                                their relative proportions. 0, the default,\n"
"                                leaves the shipped weights untouched. Lane 0\n"
"                                is the only COMPLETE lane, so this is the\n"
"                                knob for trading its coverage against the\n"
"                                restricted ones\n"
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
"                                (default 256). DEPTH decides whether this\n"
"                                matters at all: a plain directmate corpus\n"
"                                searches an IDENTICAL node count from 64 MB to\n"
"                                4 GB, while a capture-quota search at depth 7\n"
"                                costs SEVEN TIMES the nodes at 64 MB as at\n"
"                                4 GB. Past depth 6 under a quota this is the\n"
"                                largest single knob the engine offers\n"
"  --threads N | auto            root-split worker threads (default: auto).\n"
"                                auto = min(cores-2,8):\n"
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
"  --no-or-split                 stop after the first ply of the split. On by\n"
"                                default: splitting root moves alone saturates\n"
"                                at 4.75x because one root move owns about a\n"
"                                fifth of the tree. Workers that run out of root\n"
"                                moves help refute the ATTACKER moves below the\n"
"                                dominant move's refuting reply -- an OR node,\n"
"                                whose children must ALL be visited to disprove\n"
"                                it. Young brothers wait: the eldest child is\n"
"                                searched alone first, so a node that is going\n"
"                                to succeed shares nothing out, and a node only\n"
"                                splits at all while a worker is idle to take\n"
"                                the children -- supply follows demand, at\n"
"                                whatever depth the pool happens to run dry\n"
"  --predicate EXPR              measure a CANDIDATE pruning theorem without\n"
"                                ever acting on it. EXPR is a conjunction of\n"
"                                threshold tests joined by &, over depth, men,\n"
"                                amen, dmen, amat, dmat, aqueens, arooks,\n"
"                                aminors, apawns, dflights and aincheck --\n"
"                                for example 'amen<=1' or 'depth<=2&dflights>=5'.\n"
"                                The engine reports how often it fired, how\n"
"                                many nodes it would have skipped, and how many\n"
"                                COUNTEREXAMPLES it hit -- nodes where it said\n"
"                                there was no solution and there was one. One\n"
"                                counterexample kills the candidate; none is\n"
"                                evidence and never a proof. See tools/\n"
"                                predicates.py and architecture 119\n"
"  --order-weights c,p,q,r,m     move-ordering bonuses for a capture, a\n"
"                                promotion, and a queen, rook or minor move\n"
"                                (default 10000,8000,50,40,30). Ordering changes\n"
"                                the ORDER moves are tried and never the SET, so\n"
"                                no setting can change a verdict, a depth or a\n"
"                                certificate -- only the node count. They must\n"
"                                sum below 50000, the magnitude that marks a\n"
"                                checking move. Intended for tools/autotune.py\n"
"  --or-split-plies N            plies below the root at which an OR node may\n"
"                                split (default 99: demand decides, not depth)\n"
"  --or-split-min-depth N        remaining depth below which a node is not worth\n"
"                                the coordination (default 2)\n"
"  --ybw-first N                 children searched alone before the rest are\n"
"                                shared out (default 1)\n"
"  --reply-split                 take the split a second ply down: a worker\n"
"                                that runs out of root moves helps prove\n"
"                                another root move's DEFENDER replies. OFF by\n"
"                                default because it LOSES -- a defender node\n"
"                                that ends up refuted stops at its first\n"
"                                refuting reply, so helpers prove replies the\n"
"                                sequential search never visits. Measured at\n"
"                                4.4x slower on a depth-7 capture quota. The\n"
"                                answer, the line and the certificate are\n"
"                                unchanged either way. See architecture 111\n"
"  --reply-split-min-proved N    replies a node must have proved before helpers\n"
"                                may join it (default 2). 0 is the ungated\n"
"                                mechanism the measurement above rejected\n"
"  --shared-tt | --private-tt    share one exact proof table across workers\n"
"  --shared-tt-shards N          shards for the shared table (default 256)\n"
"  --hint-entries N              move-ordering hint slots per worker (default\n"
"                                262144, about 17 MB each). A hint only\n"
"                                reorders moves, so losing one costs ordering\n"
"                                quality and can never change a verdict\n"
"  --heartbeat S                 print a STATUS line every S seconds while a\n"
"                                depth is still running, showing nodes so far.\n"
"                                Implies --progress. A depth can run for an\n"
"                                hour and the proven-bound stream says nothing\n"
"                                until it completes, so without this there is\n"
"                                no way to tell a healthy long search from a\n"
"                                stuck one. The line says \"searching\", never\n"
"                                \"proven\": it asserts nothing about the\n"
"                                position, unlike every other line here\n"
"  --tt-shed-divisor N          shed 1/N of the table per eviction (default 2).\n"
"                                NOT a memory setting: evict() scans a whole\n"
"                                shard however little it sheds, so a small\n"
"                                fraction means many more full scans. Worth\n"
"                                2.4x on a deep capture-quota search\n"
"  --tt-reserve N                pre-reserve N table buckets\n"
"\n"
"  --uci                         speak the UCI protocol instead of reading EPD.\n"
"                                A LOSSY CONVENIENCE INTERFACE: `go mate N` is\n"
"                                exact and maps to `score mate N`, but UCI has\n"
"                                no way to say \"proved no solution exists\", so\n"
"                                that verdict and a timeout are separated on\n"
"                                `info string` and nowhere else. Certificates\n"
"                                cannot travel and --emit-proof is refused.\n"
"                                The EPD line remains the record\n"
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
"  --list-unmoves                every PREDECESSOR of the position, as FENs.\n"
"                                Retrograde generation, for bidirectional\n"
"                                search. Incomplete where castling rights were\n"
"                                forfeited by the retracted move\n"
"\n"
"Output:\n"
"  -5                            UCI-style coordinate moves (compatibility)\n"
"  --emit-proof                  append a recursive JSON proof certificate\n"
"  --print-config                print the effective configuration as JSON\n"
"                                and exit; every default already resolved\n"
"  --profile                     emit per-position counters to stderr\n"
"  --no-profile                  default; no counters\n"
"  --debug                       verbose search diagnostics on stderr\n"
"  --self-check                  check the king-escape analysis against move\n"
"                                generation on each position, and the escape\n"
"                                coverage table against a second computation\n"
"  --list-legal                  list legal moves instead of solving\n"
"  --perft N                     perft counts for depths 1..N\n"
"  --perft-divide N              per-root-move perft breakdown at depth N\n"
"\n"
"Search tuning (all preserve exactness; see docs/ARCHITECTURE.md):\n"
"  --proof-hints | --no-proof-hints\n"
"                                default: --proof-hints\n"
"  --refutation-hints | --no-refutation-hints\n"
"                                default: --no-refutation-hints (measured harmful)\n"
"  --defender-history | --no-defender-history\n"
"                                order defender replies by from/to refutation\n"
"                                history. The refutation hints above are keyed by\n"
"                                POSITION; this generalises across positions.\n"
"                                Ordering only, never which reply refutes.\n"
"                                default: --no-defender-history. The first reply\n"
"                                already refutes at 97%% of refuted defender\n"
"                                nodes, so perfect ordering is worth 1.05x per\n"
"                                ply and this is not perfect. See 126.\n"
"  --answer-order | --no-answer-order\n"
"                                default: --answer-order. Orders the selfmate\n"
"                                defender's replies by how little room they\n"
"                                leave the attacker, so that the refutation\n"
"                                taken is the one proving the most. Cannot\n"
"                                change a verdict; the off switch is the\n"
"                                differential test\n"
"  --attacker-reject | --no-attacker-reject\n"
"                                default: on. At selfmate depth 1, refute an\n"
"                                attacker move without searching it when the\n"
"                                defender king has a legal move giving no check.\n"
"                                A selfmate in one needs EVERY reply to mate, so\n"
"                                one quiet king move refutes it. Exact, so it\n"
"                                cannot change a verdict; the off switch is the\n"
"                                differential test\n"
"  --coverage-exit | --no-coverage-exit\n"
"                                default: on. At direct-mate depth 1, fail the\n"
"                                whole node before generating a move when no\n"
"                                single piece could deny the enemy king every\n"
"                                escape square it unconditionally has. Exact, so\n"
"                                it cannot change a verdict; the off switch is\n"
"                                the differential test\n"
"  --fac-observer                count, at every defender node, how many replies\n"
"                                the search tries before the one that refutes,\n"
"                                and whether that refutation is a check. What a\n"
"                                fatal-anti-check test could save is exactly what\n"
"                                precedes the refutation, so this measures the\n"
"                                mechanism against the ordering already in place.\n"
"                                A measurement aid, not a tuning knob\n"
"  --fast-reject | --no-fast-reject\n"
"                                default: on. Answer the selfmate rejection\n"
"                                test with attack queries instead of board\n"
"                                copies, deferring to the exact form whenever a\n"
"                                discovered check might be available. Cannot\n"
"                                change a verdict; the off switch is the\n"
"                                differential test\n"
"  --selfmate-node-exit | --no-selfmate-node-exit\n"
"                                default: off. At selfmate depth 1, refute the\n"
"                                whole node before generating a move when no\n"
"                                single piece could deny the defender king every\n"
"                                escape square it unconditionally has and no\n"
"                                king move could discover a check. Exact, so it\n"
"                                cannot change a verdict; the off switch is the\n"
"                                differential test\n"
"  --selfmate-node-observer      count what that node exit would refute, and how\n"
"                                many moves it would save, without acting on it.\n"
"                                A measurement aid, not a tuning knob\n"
"  --coverage-observer           count how often a mate in one is provably\n"
"                                impossible at a direct-mate depth-1 node, and\n"
"                                how many moves that would save, without acting\n"
"                                on it. A measurement aid, not a tuning knob\n"
"  --reject-observer             count what that test would reject without acting\n"
"                                on it. A measurement aid, not a tuning knob\n"
"  --depth2-scorer | --no-depth2-scorer\n"
"                                default: off. At remaining depth 2, order\n"
"                                replies with an additive scorer -- checks,\n"
"                                material, own-king room -- instead of the width\n"
"                                estimator. A different shape, not a cheaper\n"
"                                approximation, and its constants are derived\n"
"                                here rather than adopted. Measured a tie with\n"
"                                the estimator on coverage, so it is off\n"
"  --exact-tt | --no-exact-tt    default: on. Off disables the exact proof table\n"
"                                entirely. A diagnostic, not a tuning knob: the\n"
"                                table is keyed by position with no depth, so it\n"
"                                is sound only because a disproof bounds every\n"
"                                smaller depth and a proof depth is minimal.\n"
"                                Running a corpus both ways and comparing the\n"
"                                reported depths is the only check of those two\n"
"                                properties\n"
"  --answer-order-min-depth N    remaining depth at or above which replies are\n"
"                                ordered (default 2). Below it the lazy scan\n"
"                                runs instead. Swept, not assumed: on 200\n"
"                                selfmates 2 solves 166, 3 and 4 solve 164,\n"
"                                and ordering off solves 163\n"
"  --selfmate-bound | --no-selfmate-bound\n"
"                                default: OFF. The same bound on a selfmate,\n"
"                                where the roles invert and the DEFENDER is the\n"
"                                side that must deliver mate. Sound, but measured\n"
"                                a net loss of three positions over the 903: it\n"
"                                fires constantly and converts nothing\n"
"  --help-bound | --no-help-bound\n"
"                                default: --help-bound. Prunes helpmate subtrees\n"
"                                where no mate can be reached in the moves left.\n"
"                                The off switch exists so the bound can be\n"
"                                differentially tested; see docs/\n"
"                                HELPMATE_COVERAGE_DERIVATION.md\n"
"  --any-depth-refutations | --no-any-depth-refutations\n"
"                                default: OFF. Static theorems that prove no\n"
"                                solution exists at ANY depth, so iterative\n"
"                                deepening stops instead of re-searching. This\n"
"                                is the one option that can make a WRONG answer\n"
"                                rather than a slow one; off means inert, not\n"
"                                merely unused. See docs/GAP1_DERIVATION.md\n"
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
"  --dfpn-min-men N              skip the preconditioner when the position has\n"
"                                fewer than N men (0 = no floor). On sparse\n"
"                                material it costs more than its guidance is\n"
"                                worth; see --dfpn-min-depth for the other axis\n"
"  --dfpn-min-depth N            skip the preconditioner below depth N; under\n"
"                                iterative deepening its work cannot carry\n"
"                                across depths (default 1, no skipping)\n"
"  --escape-count                report E, the escape count, for both kings and\n"
"                                stop. E is how many squares a king could legally\n"
"                                step to, counted with the king itself removed\n"
"                                from the board so that sliding attacks THROUGH\n"
"                                its square are seen. A diagnostic, not a search\n"
"  --progress-moves | --no-progress-moves\n"
"                                default: off. Also report WHICH root move is\n"
"                                being searched, as it is taken up. A STATUS\n"
"                                line, not a theorem: it asserts nothing about\n"
"                                the position and is superseded by the next move,\n"
"                                so it reads 'searching' where a bound reads\n"
"                                'proven'. Independent of --progress\n"
"  --progress | --no-progress    default: off. While the search runs, write a\n"
"                                line to STDERR each time a depth completes\n"
"                                without finding a solution. Every such line is a\n"
"                                PROVEN bound -- 'no solution within N' -- never\n"
"                                a guess, so nothing emitted is ever withdrawn.\n"
"                                Only the unrestricted lane publishes, and only\n"
"                                for a depth that COMPLETED: one abandoned on the\n"
"                                clock or a node budget proved nothing. Needs\n"
"                                --iterative-depth to have anything to say\n"
"  --dfpn-under-variant | --no-dfpn-under-variant\n"
"                                default: --no-dfpn-under-variant. Proof numbers\n"
"                                measure the MATE and know nothing about a\n"
"                                capture or check quota, so under a live variant\n"
"                                rule they steer by the wrong game: measured 9x\n"
"                                to 30x SLOWER on the x-capture bench, verdicts\n"
"                                unchanged. On plain directmates it pays, and\n"
"                                there it still runs\n"
"  --dfpn-sort | --dfpn-no-sort  sort moves at DFPN nodes (default: no)\n"
"  --dfpn-child-init | --dfpn-no-child-init\n"
"                                estimate an unvisited child's proof number\n"
"                                from the defender king's flight count, rather\n"
"                                than letting every unvisited child tie at 1\n"
"                                and selection fall back to generator order\n"
"                                (default: off). Distinct from --dfpn-check-bias,\n"
"                                which weights an AND node's own guess and so\n"
"                                cannot discriminate between siblings\n"
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

// Boolean options, as data rather than as control flow.
//
// This was 65 more links in main's `else if` chain, and that chain grew past a
// limit nobody knew was there: MSVC gives up at 128 nested blocks (C1061), and
// each `else if` nests one. The program stopped compiling on MSVC entirely --
// not slowly, not with a warning -- while GCC and Clang, which have no such
// limit, built it clean every time locally. Nothing said so until CI ran on a
// Windows runner for the first time.
//
// A table has no depth. It is also the better shape: adding a flag is adding a
// row, and the rows sit where they can be read together.
struct BoolOption {
    const char* name;
    bool SearchConfig::* field;
    bool value;
};

const BoolOption kBoolOptions[] = {
    {"--debug", &SearchConfig::debug, true},
    {"--emit-proof", &SearchConfig::emit_proof, true},
    {"--profile", &SearchConfig::profile, true},
    {"--no-profile", &SearchConfig::profile, false},
    {"--score-mates", &SearchConfig::score_mates, true},
    {"--no-mate-score", &SearchConfig::score_mates, false},
    {"--score-checks", &SearchConfig::score_checks, true},
    {"--no-check-score", &SearchConfig::score_checks, false},
    {"--fast-check-score", &SearchConfig::fast_check_score, true},
    {"--exact-check-score", &SearchConfig::fast_check_score, false},
    {"--tt-prefetch", &SearchConfig::tt_prefetch, true},
    {"--no-tt-prefetch", &SearchConfig::tt_prefetch, false},
    {"--refutation-hints", &SearchConfig::refutation_hints, true},
    {"--no-refutation-hints", &SearchConfig::refutation_hints, false},
    {"--defender-history", &SearchConfig::defender_history_order, true},
    {"--no-defender-history", &SearchConfig::defender_history_order, false},
    {"--proof-hints", &SearchConfig::proof_hints, true},
    {"--no-proof-hints", &SearchConfig::proof_hints, false},
    {"--attacker-reject", &SearchConfig::attacker_reject, true},
    {"--no-attacker-reject", &SearchConfig::attacker_reject, false},
    {"--fac-observer", &SearchConfig::fac_observer, true},
    {"--fast-reject", &SearchConfig::fast_reject, true},
    {"--no-fast-reject", &SearchConfig::fast_reject, false},
    {"--selfmate-node-exit", &SearchConfig::selfmate_node_exit, true},
    {"--no-selfmate-node-exit", &SearchConfig::selfmate_node_exit, false},
    {"--selfmate-node-observer", &SearchConfig::selfmate_node_observer, true},
    {"--coverage-exit", &SearchConfig::coverage_exit, true},
    {"--no-coverage-exit", &SearchConfig::coverage_exit, false},
    {"--coverage-observer", &SearchConfig::coverage_observer, true},
    {"--reject-observer", &SearchConfig::reject_observer, true},
    {"--depth2-scorer", &SearchConfig::depth2_scorer, true},
    {"--no-depth2-scorer", &SearchConfig::depth2_scorer, false},
    {"--no-exact-tt", &SearchConfig::exact_tt, false},
    {"--exact-tt", &SearchConfig::exact_tt, true},
    {"--answer-order", &SearchConfig::answer_order, true},
    {"--no-answer-order", &SearchConfig::answer_order, false},
    {"--selfmate-bound", &SearchConfig::selfmate_bound, true},
    {"--no-selfmate-bound", &SearchConfig::selfmate_bound, false},
    {"--help-bound", &SearchConfig::help_bound, true},
    {"--no-help-bound", &SearchConfig::help_bound, false},
    {"--any-depth-refutations", &SearchConfig::any_depth_refutations, true},
    {"--no-any-depth-refutations", &SearchConfig::any_depth_refutations, false},
    {"--move-reserve", &SearchConfig::move_reserve, true},
    {"--no-move-reserve", &SearchConfig::move_reserve, false},
    {"--inplace-order", &SearchConfig::inplace_order, true},
    {"--scored-vector-order", &SearchConfig::inplace_order, false},
    {"--stable-sort-order", &SearchConfig::bucket_order, false},
    {"--keep-iter-tt", &SearchConfig::keep_iter_tt, true},
    {"--clear-iter-tt", &SearchConfig::keep_iter_tt, false},
    {"--ordered-check-shortcut", &SearchConfig::ordered_check_shortcut, true},
    {"--no-ordered-check-shortcut", &SearchConfig::ordered_check_shortcut, false},
    {"--static-pseudo", &SearchConfig::static_pseudo, true},
    {"--vector-pseudo", &SearchConfig::static_pseudo, false},
    {"--portfolio", &SearchConfig::portfolio, true},
    {"--no-portfolio", &SearchConfig::portfolio, false},
    {"--direct-depth", &SearchConfig::direct_depth, true},
    {"--iterative-depth", &SearchConfig::direct_depth, false},
    {"--dfpn-final-depth-only", &SearchConfig::dfpn_final_depth_only, true},
    {"--dfpn-every-depth", &SearchConfig::dfpn_final_depth_only, false},
    {"--escape-count", &SearchConfig::escape_count_only, true},
    {"--progress-moves", &SearchConfig::progress_moves, true},
    {"--no-progress-moves", &SearchConfig::progress_moves, false},
    {"--progress-moves", &SearchConfig::progress_moves, true},
    {"--no-progress-moves", &SearchConfig::progress_moves, false},
    {"--progress", &SearchConfig::progress, true},
    {"--no-progress", &SearchConfig::progress, false},
    {"--dfpn-under-variant", &SearchConfig::dfpn_under_variant, true},
    {"--no-dfpn-under-variant", &SearchConfig::dfpn_under_variant, false},
    {"--dfpn-sort", &SearchConfig::dfpn_sort, true},
    {"--dfpn-no-sort", &SearchConfig::dfpn_sort, false},
    {"--dfpn-child-init", &SearchConfig::dfpn_child_init, true},
    {"--dfpn-no-child-init", &SearchConfig::dfpn_child_init, false},
    {"--dfpn-child-init-cheap", &SearchConfig::dfpn_child_init_cheap, true},
    {"--dfpn-no-child-init-cheap", &SearchConfig::dfpn_child_init_cheap, false},
    {"--dfpn-hints-only", &SearchConfig::dfpn_share_disproofs, false},
    {"--dfpn-share-disproofs", &SearchConfig::dfpn_share_disproofs, true},
    {"--lazy-defender", &SearchConfig::lazy_defender, true},
    {"--eager-defender", &SearchConfig::lazy_defender, false},
    {"--fused-order", &SearchConfig::fused_order, true},
    {"--split-order", &SearchConfig::fused_order, false},
    {"--root-split", &SearchConfig::root_split, true},
    {"--no-root-split", &SearchConfig::root_split, false},
    {"--reply-split", &SearchConfig::reply_split, true},
    {"--no-reply-split", &SearchConfig::reply_split, false},
    {"--cross-job-proofs", &SearchConfig::cross_job_proofs, true},
    {"--no-cross-job-proofs", &SearchConfig::cross_job_proofs, false},
    {"--cross-lane-proofs", &SearchConfig::cross_lane_proofs, true},
    {"--no-cross-lane-proofs", &SearchConfig::cross_lane_proofs, false},
    {"--owner-helps", &SearchConfig::owner_helps, true},
    {"--no-owner-helps", &SearchConfig::owner_helps, false},
    {"--flat-tt", &SearchConfig::flat_tt, true},
    {"--no-flat-tt", &SearchConfig::flat_tt, false},
    {"--tt-lines", &SearchConfig::tt_lines, true},
    {"--no-tt-lines", &SearchConfig::tt_lines, false},
    {"--tt-depth-evict", &SearchConfig::tt_depth_evict, true},
    {"--no-tt-depth-evict", &SearchConfig::tt_depth_evict, false},
    {"--or-split", &SearchConfig::or_split, true},
    {"--no-or-split", &SearchConfig::or_split, false},
    {"--shared-tt", &SearchConfig::shared_tt, true},
    {"--private-tt", &SearchConfig::shared_tt, false},
};

int main(int argc, char** argv) {
    SearchConfig config;
    bool print_config = false;
    int requested_depth = 0;
    int perft_depth = 0;
    bool perft_divide = false;
    bool read_stdin = false;
    bool uci_mode = false;
    bool list_legal = false;
    bool list_san = false;
    bool list_unmoves = false;
    bool self_check = false;

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
        bool from_table = false;
        for (const BoolOption& option : kBoolOptions) {
            if (arg == option.name) {
                config.*option.field = option.value;
                from_table = true;
                break;
            }
        }
        if (from_table) {
            continue;
        }
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
            // Bounded because the transposition key carries eight bits of depth,
            // and the cooperative key carries PLIES -- twice this. A request
            // past the bound cannot be keyed correctly, so it is refused here
            // rather than asserted deep in the search. Mate in 120 is far beyond
            // anything the corpora contain.
            if (requested_depth < 1 || requested_depth > kMaxKeyDepth / 2) {
                return usage_error("option '-z' wants 1.." +
                                   std::to_string(kMaxKeyDepth / 2));
            }
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
        } else if (arg == "--self-check") {
            self_check = true;
            // Once, before any position: a table checked only against itself is
            // not checked, and this one decides whether nodes get thrown away.
            const int wrong = verify_escape_coverage_table();
            std::cout << "coverage-table; selfcheck "
                      << (wrong == 0 ? "ok" : "FAIL") << "; mismatches "
                      << wrong << ";\n";
        } else if (arg == "--list-legal") {
            list_legal = true;
        } else if (arg == "--list-san") {
            list_san = true;
        } else if (arg == "--list-unmoves") {
            list_unmoves = true;
        } else if (arg == "--perft" || arg == "--perft-divide") {
            const char* v = need_value(i);
            if (!v) return usage_error("option " + arg + " requires a depth");
            perft_depth = std::atoi(v);
            if (perft_depth <= 0) return usage_error("option " + arg + " requires a positive depth");
            perft_divide = (arg == "--perft-divide");
        } else if (arg == "--uci") {
            uci_mode = true;
        } else if (arg == "--print-config") {
            print_config = true;
        } else if (arg == "--answer-order-min-depth" && i + 1 < argc) {
            config.answer_order_min_depth = std::atoi(argv[++i]);
        } else if (arg == "--tt-reserve") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--tt-reserve' requires a bucket count");
            if (!parse_size(v, config.tt_reserve)) return usage_error("option '--tt-reserve' expects a number");
        } else if (arg == "--move-reserve-cap") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--move-reserve-cap' requires a capacity");
            std::size_t value = 0;
            if (!parse_size(v, value) || value == 0) return usage_error("option '--move-reserve-cap' expects a positive number");
            config.move_reserve = true;
            config.move_reserve_capacity = value;
        } else if (arg == "--bucket-order") {
            config.bucket_order = true;
            config.inplace_order = true;
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
                // Root-split parallelism does not merely saturate, it
                // reverses: at the full core count it solves FEWER positions
                // than a single root-split thread. See AUTO_THREAD_CAP.
                //
                // An explicit --threads N is never capped; only `auto` is.
                config.threads = resolve_auto_threads();
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
        } else if (arg == "--predicate") {
            // A CANDIDATE, never a prune. The engine measures it and reports
            // three counters; it does not act on it. See predicate.h.
            const char* v = need_value(i);
            if (!v) return usage_error("option '--predicate' requires an expression");
            std::string why;
            if (!parse_predicate(v, config.predicate, why)) {
                return usage_error("option '--predicate': " + why);
            }
        } else if (arg == "--order-weights") {
            // c,p,q,r,m -- one flag rather than five, because an automatic
            // search sets them together and a partial assignment is never
            // wanted. The invariant is checked HERE rather than at the use
            // site: the static terms must stay below the 50000 that prove.h
            // reads as "this move gives check", and a run that broke it would
            // not crash, it would quietly accept a checkmate as a stalemate.
            const char* v = need_value(i);
            if (!v) return usage_error("option '--order-weights' requires capture,promo,queen,rook,minor");
            OrderWeights w;
            int* fields[5] = {&w.capture, &w.promo, &w.queen, &w.rook, &w.minor};
            const std::string spec(v);
            std::size_t start = 0;
            int seen = 0;
            for (; seen < 5; ++seen) {
                const std::size_t comma = spec.find(',', start);
                const std::string part = comma == std::string::npos
                                             ? spec.substr(start)
                                             : spec.substr(start, comma - start);
                std::size_t parsed = 0;
                if (part.empty() || !parse_size(part.c_str(), parsed) || parsed > 40000) {
                    return usage_error("option '--order-weights' expects five numbers in 0..40000");
                }
                *fields[seen] = static_cast<int>(parsed);
                if (comma == std::string::npos) {
                    ++seen;
                    break;
                }
                start = comma + 1;
            }
            if (seen != 5) {
                return usage_error("option '--order-weights' expects five comma-separated numbers");
            }
            if (w.max_static() >= 50000) {
                return usage_error("option '--order-weights': the terms must sum below 50000, "
                                   "the magnitude that marks a checking move");
            }
            config.order_weights = w;
        } else if (arg == "--or-split-plies") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--or-split-plies' requires a count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--or-split-plies' expects a number");
            config.or_split_plies = static_cast<int>(value);
        } else if (arg == "--hint-entries") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--hint-entries' requires a count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--hint-entries' expects a number");
            config.hint_entries = value;
        } else if (arg == "--heartbeat") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--heartbeat' requires seconds");
            config.heartbeat_seconds = std::atof(v);
            if (config.heartbeat_seconds < 0.0) return usage_error("option '--heartbeat' expects a non-negative number");
            if (config.heartbeat_seconds > 0.0) config.progress = true;
        } else if (arg == "--tt-shed-divisor") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--tt-shed-divisor' requires a number");
            std::size_t value = 0;
            if (!parse_size(v, value) || value < 1) return usage_error("option '--tt-shed-divisor' expects a number >= 1");
            config.tt_shed_divisor = value;
        } else if (arg == "--or-split-min-depth") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--or-split-min-depth' requires a depth");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--or-split-min-depth' expects a number");
            config.or_split_min_depth = static_cast<int>(value);
        } else if (arg == "--ybw-first") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--ybw-first' requires a count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--ybw-first' expects a number");
            config.ybw_first = static_cast<int>(value);
        } else if (arg == "--reply-split-min-proved") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--reply-split-min-proved' requires a count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--reply-split-min-proved' expects a number");
            config.reply_split_min_proved = static_cast<int>(value);
        } else if (arg == "--root-split-all") {
            config.root_sequential_first = 0;
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
            if (!parse_size(v, value) || value == 0 ||
                value > static_cast<std::size_t>(kMaxKeyDepth / 2)) {
                return usage_error("option '-Z' wants 1.." +
                                   std::to_string(kMaxKeyDepth / 2));
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
        } else if (arg == "--checks" || arg == "--captures") {
            const int rule = (arg == "--captures") ? VR_CAPTURE : VR_CHECK;
            const char* v = need_value(i);
            if (v == nullptr) return usage_error("option " + arg + " requires a value");
            // "N" for both sides, "W:B" for an asymmetric allowance. The two
            // counters are wholly independent, so the asymmetric form costs
            // nothing beyond parsing it.
            std::string text(v);
            const std::size_t colon = text.find(':');
            const std::string left = text.substr(0, colon);
            const std::string right = colon == std::string::npos ? left
                                                                 : text.substr(colon + 1);
            for (const std::string* part : {&left, &right}) {
                if (part->empty() || part->find_first_not_of("0123456789") != std::string::npos) {
                    return usage_error("option " + arg + " wants N or W:B");
                }
            }
            const int w = std::atoi(left.c_str()), bl = std::atoi(right.c_str());
            // Capped rather than clamped: the allowance occupies seven bits of
            // the transposition key, and folding two distinct states onto one
            // key is the single failure this engine exists to prevent.
            if (w < 1 || w > kMaxQuota || bl < 1 || bl > kMaxQuota) {
                return usage_error("option " + arg + " wants 1.." +
                                   std::to_string(kMaxQuota) + " a side");
            }
            config.quota_limit[quota_index(WHITE, rule)] = w;
            config.quota_limit[quota_index(BLACK, rule)] = bl;
        } else if (arg == "--escape") {
            const char* v = need_value(i);
            if (v == nullptr) return usage_error("option '--escape' requires a value");
            std::string text(v);
            const std::size_t colon = text.find(':');
            const std::string left = text.substr(0, colon);
            const std::string right = colon == std::string::npos ? left
                                                                 : text.substr(colon + 1);
            for (const std::string* part : {&left, &right}) {
                if (part->empty() || part->find_first_not_of("0123456789") != std::string::npos) {
                    return usage_error("option '--escape' wants N or W:B");
                }
            }
            const int w = std::atoi(left.c_str()), bl = std::atoi(right.c_str());
            // 1..8, not 1..126. E counts a king's ring, which holds eight squares
            // in the interior and fewer at an edge, so a limit above 8 can never
            // be reached and a limit of 0 is reached by every position at once.
            // Refused rather than clamped, as the quotas are.
            if (w < 1 || w > 8 || bl < 1 || bl > 8) {
                return usage_error("option '--escape' wants 1..8 a side");
            }
            config.quota_limit[quota_index(WHITE, VR_ESCAPE)] = w;
            config.quota_limit[quota_index(BLACK, VR_ESCAPE)] = bl;
        } else if (arg == "--escape-win") {
            config.rule_wins[VR_ESCAPE] = true;
        } else if (arg == "--no-escape-win") {
            config.rule_wins[VR_ESCAPE] = false;
        } else if (arg == "--check-win") {
            config.rule_wins[VR_CHECK] = true;
        } else if (arg == "--no-check-win") {
            config.rule_wins[VR_CHECK] = false;
        } else if (arg == "--capture-win") {
            config.rule_wins[VR_CAPTURE] = true;
        } else if (arg == "--no-capture-win") {
            config.rule_wins[VR_CAPTURE] = false;
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
        } else if (arg == "--dfpn-min-men") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--dfpn-min-men' requires a count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--dfpn-min-men' expects a number");
            config.dfpn_min_men = static_cast<int>(value); // 0 disables the floor
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
        } else if (arg == "--lane0-weight") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--lane0-weight' requires a percent");
            std::size_t value = 0;
            if (!parse_size(v, value) || value > 100)
                return usage_error("option '--lane0-weight' expects 0-100");
            config.lane0_weight = static_cast<int>(value);
        } else if (arg == "--restrict-checks") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--restrict-checks' requires a number");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--restrict-checks' expects a number");
            config.checks_mask = static_cast<int>(value);
        } else if (arg == "--restrict-king") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--restrict-king' requires a number");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--restrict-king' expects a number");
            config.king_squares = static_cast<int>(value);
        } else if (arg == "--restrict-maxdef") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--restrict-maxdef' requires a number");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--restrict-maxdef' expects a number");
            config.max_defender_moves = static_cast<int>(value);
        } else if (arg == "--beam-defender") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--beam-defender' requires a number");
            std::size_t value = 0;
            if (!parse_size(v, value) || value == 0)
                return usage_error("option '--beam-defender' expects a number of replies, at least 1");
            config.beam_defender = static_cast<int>(value);
        } else if (arg == "--restrict-threat") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--restrict-threat' requires a number");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--restrict-threat' expects a number");
            config.threat_depth = static_cast<int>(value);
        } else if (arg == "--dfpn-node-limit") {
            const char* v = need_value(i);
            if (!v) return usage_error("option '--dfpn-node-limit' requires a node count");
            std::size_t value = 0;
            if (!parse_size(v, value)) return usage_error("option '--dfpn-node-limit' expects a number");
            config.dfpn_node_limit = static_cast<std::uint64_t>(value);
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
    // auto` computes.
    //
    // The note this replaces said the previous default of 1 "used a single
    // core on any machine". That was wrong, and it is why the default moved to
    // a value that measures worse: --threads is ROOT SPLIT only, and the
    // portfolio parallelises independently of it, so threads=1 already uses
    // the machine. Raising it to the core count did not add parallelism, it
    // took cores away from the lanes that were providing it.
    if (config.threads < 0) {
        config.threads = resolve_auto_threads();
    }

    // Report the configuration that would actually be used and stop. Printed
    // after every default and sentinel is resolved, so this is the effective
    // configuration rather than a restatement of the flags given.
    if (print_config) {
        emit_config_json(config);
        return 0;
    }
    if (config.beam_defender > 0) {
        // The beam is unsound. Every road from it to something that looks
        // like a proof is closed here, not documented and hoped for.
        if (!config.direct_depth)
            return usage_error("--beam-defender is a finder and cannot prove a shortest mate: "
                               "use it with --direct-depth");
        if (config.emit_proof)
            return usage_error("--beam-defender results are claims, not proofs: "
                               "no certificate exists for them, so --emit-proof is refused");
        if (config.goal != Goal::Mate)
            return usage_error("--beam-defender is implemented for the mate goal only");
        if (uci_mode)
            return usage_error("--beam-defender is not available in --uci mode: a bestmove "
                               "cannot carry the `beam` marker that says it is unverified");
    }
    if (uci_mode) {
        // --emit-proof is REFUSED here rather than silently ignored. A caller
        // who asked for a certificate and got a bare `bestmove` would have no
        // way to distinguish "not supported" from "no proof was found", and the
        // second is a claim about the position.
        if (config.emit_proof) {
            return usage_error("--emit-proof is not available in --uci mode: a proof tree "
                               "cannot travel over the protocol. Use the EPD interface");
        }
        return run_uci_loop(config);
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
            } else if (self_check) {
                self_check_line(line);
            } else if (list_legal) {
                list_legal_line(line);
            } else if (list_san) {
                list_san_line(line);
            } else if (list_unmoves) {
                list_unmoves_line(line);
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
        } else if (self_check) {
            self_check_line(buffer.str());
        } else if (list_legal) {
            list_legal_line(buffer.str());
        } else if (list_san) {
            list_san_line(buffer.str());
        } else if (list_unmoves) {
            list_unmoves_line(buffer.str());
        } else {
            solve_line(buffer.str(), requested_depth, config);
        }
    }
    return 0;
}
