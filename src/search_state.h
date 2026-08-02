// search_state.h -- Search configuration, per-search mutable state, and cancellation.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by echest.cpp; see docs/E_CHEST_ARCHITECTURE.md.

#ifndef ECHEST_SEARCH_STATE_H_INCLUDED
#define ECHEST_SEARCH_STATE_H_INCLUDED

namespace echest {

// All tunable search options. This is the single value passed from the CLI to
// the search, and is also what each worker copies when a search is split
// across threads, so worker construction never has to re-parse or re-plumb
// individual flags.
struct SearchConfig {
    Color attacker = WHITE;
    // DFPN is the default route.
    //
    // It was rejected in 8e as slower at every depth, which was measuring a
    // defect: its transposition key omitted the remaining depth, so every depth
    // shared one entry and the proof numbers were meaningless (21). Two further
    // fixes followed -- preconditioning only the deepest iteration (23) and
    // dropping the per-child work that computed a value already known (27),
    // which cut make_move calls per DFPN node from 24.3 to 4.34.
    //
    // Measured once on freshly minted evaluation sets, in the shipped
    // configuration: at mate-in-10, 54/60 against depth-first's 37/60, gaining
    // seventeen positions and losing none. At mate-in-8, 171/200 against
    // 167/200 -- a slight gain that is not significant on its own, but it is a
    // gain, so the deep result is not bought at the shallow one's expense.
    //
    // Still a preconditioner, never an output authority: every accepted proof,
    // PV and certificate comes from the exact prover.
    RouteKind route = RouteKind::Dfpn;
    bool debug = false;
    bool emit_proof = false;
    bool score_mates = false;
    bool score_checks = true;
    bool fast_check_score = false;
    bool refutation_hints = false;
    bool proof_hints = true;
    std::size_t tt_reserve = 0;
    bool move_reserve = true;
    std::size_t move_reserve_capacity = 96;
    bool inplace_order = true;
    bool static_pseudo = false;
    bool profile = false;
    std::size_t order_min_size = 2;
    bool bucket_order = false;
    bool keep_iter_tt = true;
    bool ordered_check_shortcut = true;
    // -1 means the user did not choose: main() resolves it to the same value
    // `--threads auto` computes. An explicit --threads N, including 1, is
    // honoured exactly.
    int threads = -1;
    bool fused_order = true;
    bool lazy_defender = false;
    // DFPN preconditioner. Disproofs it establishes are exact verdicts and are
    // shared with the exact table; proofs are shared only as ordering hints.
    bool dfpn_share_disproofs = true;
    std::uint64_t dfpn_node_limit = 0; // 0 = unlimited
    // Root moves searched sequentially before the split. 0 splits everything.
    int root_sequential_first = 0;
    // df-pn 1+epsilon: widen the proof threshold handed to a child so it keeps
    // working instead of bouncing straight back. Expressed in 1/64ths.
    // Precondition only the deepest iteration.
    //
    // Under iterative deepening the route runs every depth from 1, and DFPN
    // cannot carry work across depths because depth is part of its key (21, 22).
    // Every shallow pass is therefore paid for in full and thrown away, while
    // the exact prover disposes of those depths almost instantly by itself.
    // Preconditioning only the final depth measured 53/60 against 45/60 for
    // preconditioning every depth, and 49/60 for the depth-first route (23).
    //
    // Expressed as "the last iteration" rather than an absolute depth so it does
    // not depend on the problem depth: an absolute threshold tuned on mate-in-8
    // would silently disable preconditioning for shallower requests.
    bool dfpn_final_depth_only = true;
    // Absolute floor, retained for experiments. 1 means no floor.
    int dfpn_min_depth = 1;
    int dfpn_epsilon_64 = 0;
    bool dfpn_sort = false;
    // Wall-clock budget in seconds. 0 means unlimited.
    // A deterministic alternative to --time-limit. Wall-clock budgets make every
    // comparison noisy: the same configuration measured 49, 51, 52 and 53 of 60
    // across this session purely on where positions fell relative to the clock,
    // which is the same size as the effects being measured. A node cap gives the
    // same answer on every run and every machine.
    //
    // Kept separate from node_budget, which the parallel cost gate sets and
    // clears for its own purposes and would otherwise wipe a user's limit.
    // How many positions from stdin to solve concurrently. 1 keeps answers
    // streaming as they are produced, which the service mode depends on.
    int parallel_positions = 1;
    std::uint64_t node_limit = 0;
    double time_limit = 0.0;
    // Begin iterative deepening at the requested depth instead of 1. Only valid
    // when the caller wants "a mate within N" rather than "the shortest mate".
    bool direct_depth = false;
    // WinChest ChecksOnly, a bitmask over the attacker's move choice:
    //    1  only own check-moves (serial-check mate)
    //    2  no opponent checks
    //    4  no opponent captures
    //    8  no own capture moves
    //   16  no own check-moves, except the mating move itself
    // Each selects a different problem, not a faster route to the same one.
    int checks_mask = 0;
    // WinChest KingSquares: allow only attacker moves after which the defender
    // king has at most this many squares available, counting the one it stands
    // on. 0 means off; 1 means the king cannot move at all.
    int king_squares = 0;
    // WinChest PieceLimit: at most this many defender pieces may have a legal
    // move after the attacker's move. 0 means off.
    int piece_limit = 0;
    // WinChest MaxMoves: the defender may have at most this many legal moves in
    // total after the attacker's move. 0 means off.
    int max_defender_moves = 0;
    // WinChest ThreatDepth. A move is examined only if, after it and a null
    // move by the defender, the attacker can mate within |threat_depth|.
    // Positive means the threat search uses check-moves only ("check threats");
    // negative allows any move ("quiet threats"). 0 is off.
    int threat_depth = 0;
    // Root mate depth, needed because WinChest caps ThreatDepth at depth - 2.
    int root_depth = 0;
    // Try a sequence of restricted searches within the time budget, falling
    // back to the unrestricted one. Sound because a restriction only removes
    // attacker options: a mate found under one is a real forced mate.
    bool portfolio = true;
    // Run the portfolio entries concurrently instead of in sequence, so each
    // gets the whole budget rather than a slice. Sound for the same reason the
    // sequential portfolio is, and it uses cores that root splitting cannot:
    // the entries solve different restricted problems, so unlike root-split
    // workers they do not duplicate each other's search.
    bool portfolio_parallel = true;
    bool shared_tt = true;
    std::size_t shared_tt_shards = 256;
    std::uint64_t parallel_min_nodes = 500;
    // -M megabytes. Converted to an entry ceiling via EST_BYTES_PER_ENTRY.
    // 64 MB sat well below the knee: at mate-in-8 it evicts hard enough to cost
    // throughput, and the budget's fixed overhead is proportionally worst there
    // (a 64 MB request peaks near 91 MB RSS). See architecture 8l.
    //
    // This is the budget for every table alive at once, divided across the
    // portfolio's lanes and the batch's position workers. 2048 rather than 256
    // because the default portfolio runs eight lanes: the per-lane share stays
    // at the 256 MB the tuning above measured, so the default is unchanged
    // while `-M` now means what it says. See architecture 42.
    std::size_t memory_mb = 2048;
};

struct PnDnFwd {
    std::uint32_t pn = 1;
    std::uint32_t dn = 1;
};

// Per-search mutable state. Inheriting the config keeps every existing
// `s.<option>` access valid while making the option set independently
// copyable.
struct Search : SearchConfig {
    Stats stats;
    BoundedTable tt;
    std::unordered_map<TTKey, Move, TTKeyHash> defender_refutations;
    std::unordered_map<TTKey, Move, TTKeyHash> attacker_proofs;

    // Cooperative cancellation. `cancel` is null for an ordinary
    // single-threaded search, so the per-node check costs one null test and
    // the abort path is unreachable. When a search is abandoned, `aborted`
    // records that its result means "gave up", not "disproved" -- an aborted
    // result must never be stored in a proof table or read as a refutation.
    const std::atomic<bool>* cancel = nullptr;
    bool aborted = false;

    // Absolute node ceiling for this search. Zero means unbounded. Used by the
    // sequential prelude of the parallel cost gate: exceeding the ceiling is an
    // abort, not a verdict, so nothing false is recorded.
    std::uint64_t node_budget = 0;

    // Wall-clock deadline. Expiry is an abort, which by the abort invariant
    // records no verdict, so a timed-out search reports "not proved" rather
    // than anything false. Shared by value with every parallel worker, so all
    // of them stop at the same instant.
    bool has_deadline = false;
    std::chrono::steady_clock::time_point deadline{};
    bool timed_out = false;
    std::uint32_t deadline_countdown = 0;

    // When non-null, exact proof entries live in a table shared with the other
    // workers of this search instead of in the private `tt` map above.
    SharedProofTable* shared_table = nullptr;

    // Proof/disproof numbers for the DFPN route. Purely a heuristic cache:
    // discarding it costs search effort and cannot change a verdict.
    std::unordered_map<TTKey, PnDnFwd, TTKeyHash> dfpn_tt;
    std::size_t dfpn_capacity = 1u << 21;

    // Context for ThreatDepth probes. The threat search asks "can the attacker
    // mate within R from this position", optionally restricted to checks, which
    // is a DIFFERENT predicate from the enclosing search. Sharing a table
    // between them would let a restricted disproof answer an unrestricted
    // question, so it gets its own Search and its own table.
    std::unique_ptr<Search> threat_ctx;

    void reset_for_new_search() {
        aborted = false;
    }
};

// Returns true if this search must unwind. Sets `aborted` so that every
// enclosing frame skips its table stores and refuses to interpret the empty
// result as a disproof.
inline bool search_cancelled(Search& s) {
    if (s.aborted) {
        return true;
    }
    if (s.node_budget != 0 && s.stats.nodes >= s.node_budget) {
        s.aborted = true;
        return true;
    }
    if (s.node_limit != 0 && s.stats.nodes >= s.node_limit) {
        // timed_out, not merely aborted. Section 8r: a line with no marker means
        // "searched exhaustively, no mate exists". An exhausted node budget has
        // settled nothing, so it must report the same "gave up" outcome a
        // wall-clock expiry does, or it would be read as a disproof.
        s.timed_out = true;
        s.aborted = true;
        return true;
    }
    // Reading the clock on every node would cost more than the search it
    // guards, so the deadline is polled on a countdown. The granularity only
    // affects how promptly the limit is honoured, never correctness.
    if (s.has_deadline) {
        if (s.deadline_countdown == 0) {
            s.deadline_countdown = 2048;
            ++s.stats.deadline_checks;
            if (std::chrono::steady_clock::now() >= s.deadline) {
                s.timed_out = true;
                s.aborted = true;
                return true;
            }
        } else {
            --s.deadline_countdown;
        }
    }
    if (s.cancel != nullptr && s.cancel->load(std::memory_order_relaxed)) {
        s.aborted = true;
        return true;
    }
    return false;
}

bool is_white_piece(char p) {
    return p >= 'A' && p <= 'Z';
}

bool is_black_piece(char p) {
    return p >= 'a' && p <= 'z';
}

bool is_piece_color(char p, Color c) {
    return c == WHITE ? is_white_piece(p) : is_black_piece(p);
}

bool is_enemy_piece(char p, Color c) {
    return p != '.' && !is_piece_color(p, c);
}

bool is_king_piece(char p) {
    return p == 'K' || p == 'k';
}

std::uint8_t piece_code(char p) {
    switch (p) {
        case '.': return 0;
        case 'P': return 1;
        case 'N': return 2;
        case 'B': return 3;
        case 'R': return 4;
        case 'Q': return 5;
        case 'K': return 6;
        case 'p': return 7;
        case 'n': return 8;
        case 'b': return 9;
        case 'r': return 10;
        case 'q': return 11;
        case 'k': return 12;
        default: return 0;
    }
}

void set_square(Board& b, int sq, char p) {
    const std::uint64_t bit = 1ull << sq;
    const char old = b.sq[sq];
    if (type_of(old) != PT_NONE) {
        b.occ &= ~bit;
        b.by_color[is_white_piece(old) ? WHITE : BLACK] &= ~bit;
        b.by_type[type_of(old)] &= ~bit;
    }
    if (type_of(p) != PT_NONE) {
        b.occ |= bit;
        b.by_color[is_white_piece(p) ? WHITE : BLACK] |= bit;
        b.by_type[type_of(p)] |= bit;
    }
    b.sq[sq] = p;
    int word = sq / 16;
    int shift = (sq % 16) * 4;
    std::uint64_t mask = 0xfull << shift;
    b.packed[word] = (b.packed[word] & ~mask) | (static_cast<std::uint64_t>(piece_code(p)) << shift);
}

Color other(Color c) {
    return c == WHITE ? BLACK : WHITE;
}

int file_of(int sq) {
    return sq & 7;
}

int rank_of(int sq) {
    return sq >> 3;
}

bool on_board(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

int square_of(int file, int rank) {
    return rank * 8 + file;
}

} // namespace echest

#endif // ECHEST_SEARCH_STATE_H_INCLUDED
