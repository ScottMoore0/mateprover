// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// types.h -- Core value types: colours, moves, boards, proofs, statistics, table keys.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_TYPES_H_INCLUDED
#define MATEPROVER_TYPES_H_INCLUDED

namespace mateprover {


enum Color { WHITE = 0, BLACK = 1 };

// Index of the least/most significant set bit. Portable across the compilers
// the CI matrix builds with; the generic fallbacks keep this correct anywhere.
inline int lsb_index(std::uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#elif defined(_MSC_VER) && defined(_M_X64)
    unsigned long i;
    _BitScanForward64(&i, x);
    return static_cast<int>(i);
#else
    int n = 0;
    while ((x & 1ull) == 0ull) { x >>= 1; ++n; }
    return n;
#endif
}

inline int msb_index(std::uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return 63 - __builtin_clzll(x);
#elif defined(_MSC_VER) && defined(_M_X64)
    unsigned long i;
    _BitScanReverse64(&i, x);
    return static_cast<int>(i);
#else
    int n = 63;
    while ((x & (1ull << 63)) == 0ull) { x <<= 1; --n; }
    return n;
#endif
}

enum class RouteKind {
    DepthFirst,
    ShallowFast,
    Dfpn,
};

// What the attacker is trying to force. The AND/OR structure is identical for
// both -- attacker needs one move, defender must have every reply refuted --
// and only the terminal predicate differs. Stalemate is not a weaker mate: a
// position that is checkmate FAILS a stalemate goal, so the two are disjoint
// rather than nested, and a search cannot be reused between them.
// What the search must establish. The value is part of every table key, so a
// verdict recorded under one goal can never be read back under another.
//
// Three shapes, not six variations. The distinction decides which prover runs:
//
//   Mate, Stalemate            adversarial. Attacker maximises, defender
//                              resists. Goal tested AFTER each attacker move.
//   Selfmate, Selfstalemate    adversarial, inverted. The attacker forces the
//                              DEFENDER to end it. Goal tested at an attacker
//                              node BEFORE moving, and four degenerate cases
//                              each carry a distinct verdict.
//   Helpmate, Helpstalemate    COOPERATIVE. Both sides are OR nodes; there is
//                              no defender and no AND layer anywhere. Proof
//                              numbers, the restriction portfolio and the
//                              root split are all defined against an adversary
//                              and none of them apply. See section 58.
enum class Goal {
    Mate,
    Stalemate,
    Selfmate,
    Selfstalemate,
    Helpmate,
    Helpstalemate,
};

// Is this goal reached by the ATTACKER being unable to move, rather than the
// defender? True for the self- goals, which invert who is trapped.
inline bool goal_is_self(Goal g) {
    return g == Goal::Selfmate || g == Goal::Selfstalemate;
}

// Is this goal cooperative -- both sides working toward the same end?
inline bool goal_is_help(Goal g) {
    return g == Goal::Helpmate || g == Goal::Helpstalemate;
}

// Does the terminal position require the trapped side to be IN CHECK (mate) or
// NOT in check (stalemate)? The whole difference between each pair.
inline bool goal_wants_check(Goal g) {
    return g == Goal::Mate || g == Goal::Selfmate || g == Goal::Helpmate;
}

struct Move {
    int from = -1;
    int to = -1;
    char promo = 0;
    bool castle = false;
    bool ep = false;
    int score = 0;
};

struct MoveList {
    std::array<Move, 256> moves{};
    std::size_t count = 0;
    bool overflow = false;

    void push_back(const Move& move) {
        if (count < moves.size()) {
            moves[count++] = move;
        } else {
            overflow = true;
        }
    }

    std::size_t size() const {
        return count;
    }

    const Move* begin() const {
        return moves.data();
    }

    const Move* end() const {
        return moves.data() + count;
    }
};

struct Proof {
    bool ok = false;
    std::vector<Move> pv;
    std::string cert;
};

struct RouteResult {
    Proof proof;
    int proved_depth = 0;
};

// Piece-type indices for the bitboard planes.
enum PieceType { PT_PAWN = 0, PT_KNIGHT, PT_BISHOP, PT_ROOK, PT_QUEEN, PT_KING, PT_NONE };

inline PieceType type_of(char p) {
    switch (std::tolower(static_cast<unsigned char>(p))) {
        case 'p': return PT_PAWN;
        case 'n': return PT_KNIGHT;
        case 'b': return PT_BISHOP;
        case 'r': return PT_ROOK;
        case 'q': return PT_QUEEN;
        case 'k': return PT_KING;
        default: return PT_NONE;
    }
}

struct Board {
    std::array<char, 64> sq{};
    std::array<std::uint64_t, 4> packed{};
    // Occupancy planes maintained incrementally by set_square. Attack queries
    // read these instead of walking squares one at a time.
    std::uint64_t occ = 0;
    std::array<std::uint64_t, 2> by_color{};
    std::array<std::uint64_t, 6> by_type{};
    std::array<int, 2> king_sq{{-1, -1}};
    Color stm = WHITE;
    unsigned castling = 0; // 1 WK, 2 WQ, 4 BK, 8 BQ
    int ep = -1;
};

struct Stats {
    std::uint64_t nodes = 0;
    std::uint64_t attacker_nodes = 0;
    std::uint64_t defender_nodes = 0;
    std::uint64_t tt_probes = 0;
    std::uint64_t tt_hits = 0;
    std::uint64_t tt_stores = 0;
    std::uint64_t exact_tt_proof_hits = 0;
    std::uint64_t exact_tt_disproof_hits = 0;
    std::uint64_t exact_tt_proof_stores = 0;
    std::uint64_t exact_tt_disproof_stores = 0;
    std::uint64_t shallow_fast_attempts = 0;
    std::uint64_t shallow_fast_hits = 0;
    std::uint64_t shallow_fast_fallbacks = 0;
    std::uint64_t attacker_move_lists = 0;
    std::uint64_t attacker_moves = 0;
    std::uint64_t attacker_candidates = 0;
    std::uint64_t defender_move_lists = 0;
    std::uint64_t defender_moves = 0;
    std::uint64_t defender_replies_tried = 0;
    std::uint64_t defender_pseudo_moves = 0;
    std::uint64_t defender_lazy_skipped = 0;
    std::uint64_t dfpn_nodes = 0;
    std::uint64_t dfpn_proved = 0;
    std::uint64_t dfpn_disproved = 0;
    std::uint64_t dfpn_table_size = 0;
    std::uint64_t dfpn_abandoned = 0;
    std::uint64_t root_sequential_tried = 0;
    std::uint64_t root_sequential_hits = 0;
    std::uint64_t dfpn_movegen = 0;
    std::uint64_t dfpn_mate_tests = 0;
    std::uint64_t deadline_checks = 0;
    std::uint64_t order_calls = 0;
    std::uint64_t order_moves = 0;
    std::uint64_t immediate_mate_tests = 0;
    std::uint64_t ordered_check_shortcut_uses = 0;
    std::uint64_t ordered_check_shortcut_checks = 0;
    std::uint64_t ordered_check_shortcut_skips = 0;
    std::uint64_t immediate_mates = 0;
    std::uint64_t refutation_hint_probes = 0;
    std::uint64_t refutation_hint_hits = 0;
    std::uint64_t refutation_hint_stores = 0;
    // Defender nodes at which the DEFENDER holds exactly king + queen.
    // Instrumentation for sizing the lone-defender refutation: root
    // material is a weak proxy because the configuration is also reached
    // mid-search once the defender's other units are captured.
    std::uint64_t defender_kq_nodes = 0;
    std::uint64_t proof_hint_probes = 0;
    std::uint64_t proof_hint_hits = 0;
    std::uint64_t proof_hint_stores = 0;
    std::uint64_t route_rejections = 0;
    std::uint64_t defender_refutations = 0;

    // Accumulate another search's counters into this one. Used to fold
    // per-worker statistics back into the reported totals after a root
    // split, so acn stays an honest whole-search node count.
    Stats& operator+=(const Stats& o) {
        nodes += o.nodes;
        attacker_nodes += o.attacker_nodes;
        defender_nodes += o.defender_nodes;
        tt_probes += o.tt_probes;
        tt_hits += o.tt_hits;
        tt_stores += o.tt_stores;
        exact_tt_proof_hits += o.exact_tt_proof_hits;
        exact_tt_disproof_hits += o.exact_tt_disproof_hits;
        exact_tt_proof_stores += o.exact_tt_proof_stores;
        exact_tt_disproof_stores += o.exact_tt_disproof_stores;
        shallow_fast_attempts += o.shallow_fast_attempts;
        shallow_fast_hits += o.shallow_fast_hits;
        shallow_fast_fallbacks += o.shallow_fast_fallbacks;
        attacker_move_lists += o.attacker_move_lists;
        attacker_moves += o.attacker_moves;
        attacker_candidates += o.attacker_candidates;
        defender_move_lists += o.defender_move_lists;
        defender_moves += o.defender_moves;
        defender_replies_tried += o.defender_replies_tried;
        defender_pseudo_moves += o.defender_pseudo_moves;
        defender_lazy_skipped += o.defender_lazy_skipped;
        dfpn_nodes += o.dfpn_nodes;
        dfpn_proved += o.dfpn_proved;
        dfpn_disproved += o.dfpn_disproved;
        dfpn_table_size += o.dfpn_table_size;
        dfpn_abandoned += o.dfpn_abandoned;
        root_sequential_tried += o.root_sequential_tried;
        root_sequential_hits += o.root_sequential_hits;
        dfpn_movegen += o.dfpn_movegen;
        dfpn_mate_tests += o.dfpn_mate_tests;
        deadline_checks += o.deadline_checks;
        order_calls += o.order_calls;
        order_moves += o.order_moves;
        immediate_mate_tests += o.immediate_mate_tests;
        ordered_check_shortcut_uses += o.ordered_check_shortcut_uses;
        ordered_check_shortcut_checks += o.ordered_check_shortcut_checks;
        ordered_check_shortcut_skips += o.ordered_check_shortcut_skips;
        immediate_mates += o.immediate_mates;
        refutation_hint_probes += o.refutation_hint_probes;
        refutation_hint_hits += o.refutation_hint_hits;
        refutation_hint_stores += o.refutation_hint_stores;
        defender_kq_nodes += o.defender_kq_nodes;
        proof_hint_probes += o.proof_hint_probes;
        proof_hint_hits += o.proof_hint_hits;
        proof_hint_stores += o.proof_hint_stores;
        route_rejections += o.route_rejections;
        defender_refutations += o.defender_refutations;
        return *this;
    }
};

// Guard: every Stats member is a counter folded by operator+=. If a field is
// added without extending the merge, this assertion fails at compile time.
static_assert(sizeof(Stats) == 47 * sizeof(std::uint64_t),
              "Stats gained a field; extend Stats::operator+= to match.");

struct TTKey {
    std::array<std::uint64_t, 4> board{};
    std::uint64_t context = 0;

    bool operator==(const TTKey& other) const {
        return board == other.board && context == other.context;
    }
};

struct TTKeyHash {
    std::size_t operator()(const TTKey& key) const noexcept {
        std::uint64_t h = 0x9e3779b97f4a7c15ull;
        auto mix64 = [&](std::uint64_t value) {
            value += 0x9e3779b97f4a7c15ull;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            value ^= value >> 31;
            h ^= value + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        };
        for (std::uint64_t word : key.board) {
            mix64(word);
        }
        mix64(key.context);
        return static_cast<std::size_t>(h);
    }
};

enum class TTEntryKind {
    Disproof,
    Proof,
};

// One entry per position and context, with depth deliberately NOT part of the
// key. Instead the entry carries monotone depth bounds:
//
//   depth <= max_disproved  =>  no mate within that depth
//   depth >= min_proved     =>  mate within that depth; pv/cert give it
//
// Both directions are exact. A mate within d is also a mate within any larger
// bound, and the absence of a mate within d also means its absence within any
// smaller bound. Keying by depth, as this table previously did, threw both
// implications away and made a separate bound table necessary to recover them.
struct TTEntry {
    static constexpr int NO_DISPROOF = -1;
    static constexpr int NO_PROOF = 1 << 29;

    int max_disproved = NO_DISPROOF;
    int min_proved = NO_PROOF;
    std::vector<Move> pv;
    std::string cert;
    std::uint32_t gen = 0;

    bool empty() const {
        return max_disproved == NO_DISPROOF && min_proved == NO_PROOF;
    }

    // Fold a new verdict in, keeping the strongest bound seen.
    void absorb(int depth, bool proved, const std::vector<Move>& new_pv,
                const std::string& new_cert) {
        if (proved) {
            if (depth < min_proved) {
                min_proved = depth;
                pv = new_pv;
                cert = new_cert;
            }
        } else if (depth > max_disproved) {
            max_disproved = depth;
        }
    }
};

} // namespace mateprover

#endif // MATEPROVER_TYPES_H_INCLUDED
