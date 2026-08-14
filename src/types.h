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
    // "No solution at ANY depth", not merely "none within the depth searched".
    // See docs/GAP1_DERIVATION.md. This is the one field in the program whose
    // wrongness produces a wrong ANSWER rather than a slow one, so it has
    // exactly two sources: a gated static theorem, or composition from children
    // that carry it. Running out of depth, nodes or clock must never set it --
    // those are `ok == false && !refuted`, which is "unknown", not "impossible".
    bool refuted = false;
    // How much this failure actually PROVED, which is often more than was asked.
    // A disproof at depth d that composed from children carrying deeper failures
    // establishes absence for every depth up to fail_depth, and a caller may
    // start its next iteration past all of it. Zero means "nothing beyond the
    // depth requested"; it is never a claim, only ever extra strength.
    //
    // Unlike `refuted` this cannot produce a wrong answer on its own -- it is a
    // bound on a NEGATIVE result, so overstating it skips work that would have
    // failed anyway. It can produce one indirectly, by causing a level that
    // contains the real solution to be skipped, so it is composed only from
    // children that genuinely reported it.
    int fail_depth = 0;
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

// THE VARIANT RULES. Each is an event a side may be required to produce a fixed
// number of in order to win outright, and everything downstream is indexed by
// this enumerator rather than written once per rule.
//
// That indirection is the whole point of the type. x-check shipped first as a
// scalar, and the cost of the second rule would otherwise have been a third copy
// of the terminal logic, the mate-versus-quota tie-break, the list of shortcuts
// that must stand down, and -- worst -- a third hunt for the five separate
// places that decide "this move ends the game now". Adding a rule here should be
// adding a vocabulary entry.
// VR_ESCAPE is a rule of a DIFFERENT KIND from the first two, and the slot it
// occupies in this enumerator is the only thing it shares with them.
//
// A check quota and a capture quota count EVENTS a side produces, decrement
// toward zero, and are filled by the side that wins. An escape threshold counts
// nothing: E is recomputed from the position at every node, can rise as well as
// fall, and the number stored is a fixed LIMIT rather than a remaining balance.
// A side LOSES when its own king reaches it, so the winner is the other side --
// the reverse of both existing rules. The array is still called `quota` because
// renaming it would touch every rule; read the escape slots as "the E this side
// may not reach".
enum VariantRule { VR_CHECK = 0, VR_CAPTURE = 1, VR_ESCAPE = 2, VR_COUNT = 3 };

// E, the escape count, is defined in board.h -- it needs attack generation. The
// declaration lives here so the terminal predicate below can call it; one
// translation unit, so the definition arriving later is enough.
struct Board;
inline int escape_count(const Board& b, Color side);

// Seven bits a side per rule in the transposition key's context word, so 126 is
// the largest quota and 127 means the rule is not in force. Refused above that
// rather than clamped: two distinct states sharing a key is the one failure this
// engine exists to prevent, and folding 200 onto 126 would do exactly that.
// One slot per (colour, rule). Named rather than written as `2 * VR_COUNT` at
// each use: cppcheck evaluated that expression as 2 and reported every write to
// slot 2 or 3 as out of bounds -- a false positive, but `containerOutOfBounds`
// is not a class to wave through, and the honest fix is to give the analyser a
// single indexing site to reason about rather than an argument to lose.
constexpr std::size_t kQuotaSlots = 2 * static_cast<std::size_t>(VR_COUNT);

// The one place the (colour, rule) arithmetic happens.
inline std::size_t quota_index(int colour, int rule) {
    return static_cast<std::size_t>(colour) * VR_COUNT + static_cast<std::size_t>(rule);
}

constexpr std::uint8_t kNoQuota = 127;
constexpr int kMaxQuota = 126;

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
    // quota[colour * VR_COUNT + rule]: how many more of that event this side must
    // still produce to win outright. Flat rather than nested so it packs and so
    // the transposition key can walk it, and defaulted to "not in force" because
    // Boards are constructed as scratch probes in several places and every one of
    // them means standard chess.
    std::array<std::uint8_t, kQuotaSlots> quota{
        {kNoQuota, kNoQuota, kNoQuota, kNoQuota, kNoQuota, kNoQuota}};
};

inline std::uint8_t quota_of(const Board& b, int colour, int rule) {
    return b.quota[quota_index(colour, rule)];
}

inline void set_quota(Board& b, int colour, int rule, std::uint8_t value) {
    b.quota[quota_index(colour, rule)] = value;
}

// Can any variant quota still be FILLED within `moves` moves?
//
// A quota needs one qualifying event per unit and a side produces at most one
// per move, so a quota standing above the move budget cannot be reached however
// the game goes. When that is true of every live quota the position is a plain
// chess problem again, and every shortcut the variant forced off becomes sound.
//
// `moves` must be an UPPER bound on the moves either side still has. Over-
// estimating is the safe direction: the claim is only made when even a generous
// count falls short.
inline bool variant_reachable_within(const Board& b,
                                     const std::array<bool, VR_COUNT>& rule_wins,
                                     int moves) {
    for (int colour = 0; colour < 2; ++colour) {
        for (int rule = 0; rule < VR_COUNT; ++rule) {
            if (!rule_wins[static_cast<std::size_t>(rule)]) continue;
            const std::uint8_t q = quota_of(b, colour, rule);
            if (q == kNoQuota) continue;
            // ESCAPE HAS NO SUCH BOUND, and claiming one would be unsound.
            //
            // The whole argument here is that a side produces at most one
            // qualifying event per move, so a quota above the move budget is out
            // of reach. E is not produced, it is MEASURED: a single capture of a
            // piece shielding the king, or the withdrawal of a piece that was
            // covering three ring squares, can move it by several at once, and it
            // can move in either direction. There is no per-move ceiling to lean
            // on, so a live escape rule always reports "reachable" and every
            // shortcut resting on this stands down. Conservative and slow, which
            // is the only safe direction for a predicate whose false positives
            // discard real solutions.
            if (rule == VR_ESCAPE) return true;
            if (static_cast<int>(q) <= moves) return true;
        }
    }
    return false;
}

// Is ANY variant rule in force? The guard that keeps standard chess free.
inline bool variant_active(const Board& b) {
    for (std::uint8_t q : b.quota) {
        if (q != kNoQuota) return true;
    }
    return false;
}

// Is a variant win rule actually in play -- both enabled by the caller AND
// carrying a quota on this board? Either half alone is not enough: a rule
// switched on with every quota absent can never fire, and a quota sitting on
// the board is inert while the rule that reads it is off.
inline bool variant_win_live(const Board& b, const std::array<bool, VR_COUNT>& rule_wins) {
    for (int rule = 0; rule < VR_COUNT; ++rule) {
        if (rule_wins[static_cast<std::size_t>(rule)]) {
            return variant_active(b);
        }
    }
    return false;
}

// Who has already won outright, and under which rule. `side` is -1 when nobody
// has. Two quotas cannot both be spent: the game ends on the first.
struct VariantWin {
    int side = -1;
    int rule = VR_CHECK;
};

inline VariantWin variant_winner(const Board& b) {
    for (int colour = 0; colour < 2; ++colour) {
        for (int rule = 0; rule < VR_COUNT; ++rule) {
            if (rule == VR_ESCAPE) {
                continue;   // not a countdown; handled below
            }
            if (quota_of(b, colour, rule) == 0) return VariantWin{colour, rule};
        }
    }
    // Escape is checked last and reads the opposite way: the side whose own king
    // has reached its limit LOSES, so the winner is its opponent.
    //
    // The side to move is tested FIRST, which is the tie-break for the case both
    // kings are over their limit at once -- reachable, since one move can both
    // expose your king and stop covering the enemy's. A player is answerable for
    // the position after their own move, the same principle that makes leaving
    // your own king in check your problem rather than your opponent's. Note the
    // side to move here is the side that is ABOUT to move, so this attributes the
    // loss to whoever created the position: the previous mover's opponent is on
    // move, and their king being over the limit is the previous mover's doing.
    // `other()` is declared later in the unit, so the colour flip is written out.
    const int mover = 1 - static_cast<int>(b.stm);
    for (int i = 0; i < 2; ++i) {
        const int colour = (i == 0) ? mover : static_cast<int>(b.stm);
        const std::uint8_t limit = quota_of(b, colour, VR_ESCAPE);
        if (limit == kNoQuota) {
            continue;
        }
        if (escape_count(b, static_cast<Color>(colour)) >= static_cast<int>(limit)) {
            return VariantWin{1 - colour, VR_ESCAPE};
        }
    }
    return VariantWin{};
}

// The token each rule contributes to a proof certificate. Named per rule rather
// than shared, because docs/PROOF_FORMAT.md promises an existing field's meaning
// will not change -- so `checkwin` stays what it was and captures get their own.
inline const char* variant_win_key(int rule) {
    if (rule == VR_CAPTURE) return "capturewin";
    if (rule == VR_ESCAPE) return "escapewin";
    return "checkwin";
}

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
    // Legality tests performed at defender nodes. `defender_moves` counts the
    // legal replies the lazy scan actually reached; this counts what it cost to
    // reach them, and the ratio is what the scan was written to reduce.
    std::uint64_t defender_legality_tests = 0;
    // Disproof-depth histogram, bucketed by how far the proven failure depth
    // exceeded the requested depth at a defender node. Bucket 0 is "proved
    // exactly what was asked", which is the signature of a search with no
    // ordering preference among refutations; the higher buckets are the
    // over-proof that level skipping and the per-move disproof array consume.
    std::uint64_t disproof_excess_0 = 0;
    std::uint64_t disproof_excess_1 = 0;
    std::uint64_t disproof_excess_2 = 0;
    std::uint64_t disproof_excess_3 = 0;
    std::uint64_t disproof_excess_4 = 0;
    std::uint64_t disproof_excess_5plus = 0;
    std::uint64_t levels_skipped = 0;
    std::uint64_t answer_orderings = 0;
    std::uint64_t tt_known_weaker = 0;
    std::uint64_t mate1_generator_skips = 0;
    std::uint64_t coverage_nodes = 0;
    std::uint64_t coverage_exits = 0;
    std::uint64_t coverage_moves_saved = 0;
    std::uint64_t selfmate_node_probes = 0;
    std::uint64_t selfmate_node_exits = 0;
    std::uint64_t selfmate_node_moves_saved = 0;
    std::uint64_t d1_attacker_moves = 0;
    std::uint64_t d1_would_reject = 0;
    std::uint64_t d1_reject_fast = 0;
    std::uint64_t d1_reject_slow = 0;
    std::uint64_t fac_refuted_nodes = 0;
    std::uint64_t fac_replies_before = 0;
    std::uint64_t fac_first_reply_refutes = 0;
    std::uint64_t fac_refutation_is_check = 0;
    std::uint64_t defender_pseudo_moves = 0;
    std::uint64_t defender_lazy_skipped = 0;
    std::uint64_t dfpn_nodes = 0;
    std::uint64_t dfpn_proved = 0;
    std::uint64_t dfpn_disproved = 0;
    std::uint64_t dfpn_table_size = 0;
    std::uint64_t dfpn_abandoned = 0;
    std::uint64_t root_sequential_tried = 0;
    std::uint64_t root_sequential_hits = 0;
    // Second-ply split: defender replies claimed in total, and the share of
    // them claimed by a worker that did not own the node. The second number is
    // the whole point -- it is work that would otherwise have been done by one
    // thread while the rest idled -- and if it is zero the mechanism did not
    // engage, whatever the wall clock says.
    std::uint64_t split_claims = 0;
    std::uint64_t split_helped = 0;
    // WHERE THE WALL CLOCK GOES, in microseconds of worker time. The four are
    // disjoint, and together with the clock they say what each worker thread
    // was doing; the denominator is threads x wall clock. Counted per child
    // SUBTREE and per park, so the clock reads are coarse and cost nothing.
    std::uint64_t split_work_micros = 0;    // inside run_child
    std::uint64_t split_park_micros = 0;    // helper loop, nothing to take
    std::uint64_t owner_wait_micros = 0;    // owner blocked on its own helpers
    std::uint64_t root_work_micros = 0;     // inside a root move of one's own
    // Time a worker was ALIVE inside worker_body, which is the denominator the
    // others are fractions of. Without it there is no honest utilisation
    // figure: threads x wall clock over-counts, because a worker that has left
    // worker_body is not idle, it is gone.
    std::uint64_t worker_micros = 0;
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
    std::uint64_t perpetual_refutations = 0;
    std::uint64_t help_unreachable_prunes = 0;
    std::uint64_t selfmate_unreachable_prunes = 0;
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
        defender_legality_tests += o.defender_legality_tests;
        disproof_excess_0 += o.disproof_excess_0;
        disproof_excess_1 += o.disproof_excess_1;
        disproof_excess_2 += o.disproof_excess_2;
        disproof_excess_3 += o.disproof_excess_3;
        disproof_excess_4 += o.disproof_excess_4;
        disproof_excess_5plus += o.disproof_excess_5plus;
        levels_skipped += o.levels_skipped;
        answer_orderings += o.answer_orderings;
        tt_known_weaker += o.tt_known_weaker;
        mate1_generator_skips += o.mate1_generator_skips;
        coverage_nodes += o.coverage_nodes;
        coverage_exits += o.coverage_exits;
        coverage_moves_saved += o.coverage_moves_saved;
        selfmate_node_probes += o.selfmate_node_probes;
        selfmate_node_exits += o.selfmate_node_exits;
        selfmate_node_moves_saved += o.selfmate_node_moves_saved;
        d1_attacker_moves += o.d1_attacker_moves;
        d1_would_reject += o.d1_would_reject;
        d1_reject_fast += o.d1_reject_fast;
        d1_reject_slow += o.d1_reject_slow;
        fac_refuted_nodes += o.fac_refuted_nodes;
        fac_replies_before += o.fac_replies_before;
        fac_first_reply_refutes += o.fac_first_reply_refutes;
        fac_refutation_is_check += o.fac_refutation_is_check;
        defender_pseudo_moves += o.defender_pseudo_moves;
        defender_lazy_skipped += o.defender_lazy_skipped;
        dfpn_nodes += o.dfpn_nodes;
        dfpn_proved += o.dfpn_proved;
        dfpn_disproved += o.dfpn_disproved;
        dfpn_table_size += o.dfpn_table_size;
        dfpn_abandoned += o.dfpn_abandoned;
        root_sequential_tried += o.root_sequential_tried;
        root_sequential_hits += o.root_sequential_hits;
        split_claims += o.split_claims;
        split_helped += o.split_helped;
        split_work_micros += o.split_work_micros;
        split_park_micros += o.split_park_micros;
        owner_wait_micros += o.owner_wait_micros;
        root_work_micros += o.root_work_micros;
        worker_micros += o.worker_micros;
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
        perpetual_refutations += o.perpetual_refutations;
        help_unreachable_prunes += o.help_unreachable_prunes;
        selfmate_unreachable_prunes += o.selfmate_unreachable_prunes;
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
static_assert(sizeof(Stats) == 82 * sizeof(std::uint64_t),
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
    // Top of the disproof chain: Unknown < Disproved(d) < Disproved(d') < Refuted.
    // Absorbing, so a deeper search may never downgrade it to a depth bound.
    bool refuted = false;

    bool empty() const {
        return max_disproved == NO_DISPROOF && min_proved == NO_PROOF && !refuted;
    }

    // Fold a new verdict in, keeping the strongest bound seen.
    void absorb(int depth, bool proved, const std::vector<Move>& new_pv,
                const std::string& new_cert, bool now_refuted = false) {
        if (now_refuted) {
            refuted = true;         // absorbing: nothing downgrades it
        }
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
