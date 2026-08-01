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

namespace {

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
};

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
    std::uint64_t bound_tt_probes = 0;
    std::uint64_t bound_tt_hits = 0;
    std::uint64_t bound_tt_ok_hits = 0;
    std::uint64_t bound_tt_fail_hits = 0;
    std::uint64_t bound_tt_stores = 0;
    std::uint64_t attacker_move_lists = 0;
    std::uint64_t attacker_moves = 0;
    std::uint64_t attacker_candidates = 0;
    std::uint64_t defender_move_lists = 0;
    std::uint64_t defender_moves = 0;
    std::uint64_t defender_replies_tried = 0;
    std::uint64_t defender_pseudo_moves = 0;
    std::uint64_t defender_lazy_skipped = 0;
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
        bound_tt_probes += o.bound_tt_probes;
        bound_tt_hits += o.bound_tt_hits;
        bound_tt_ok_hits += o.bound_tt_ok_hits;
        bound_tt_fail_hits += o.bound_tt_fail_hits;
        bound_tt_stores += o.bound_tt_stores;
        attacker_move_lists += o.attacker_move_lists;
        attacker_moves += o.attacker_moves;
        attacker_candidates += o.attacker_candidates;
        defender_move_lists += o.defender_move_lists;
        defender_moves += o.defender_moves;
        defender_replies_tried += o.defender_replies_tried;
        defender_pseudo_moves += o.defender_pseudo_moves;
        defender_lazy_skipped += o.defender_lazy_skipped;
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
static_assert(sizeof(Stats) == 41 * sizeof(std::uint64_t),
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

struct TTEntry {
    TTEntryKind kind = TTEntryKind::Disproof;
    std::vector<Move> pv;
    std::string cert;
    std::uint32_t gen = 0;
};

// A memo of exact verdicts with a bounded entry count.
//
// The safety argument for bounding is short: the table is a pure memo of
// verdicts that are themselves pure functions of the key. An absent entry only
// means the verdict is recomputed. Eviction therefore trades time for memory
// and can never trade correctness -- it cannot manufacture a false proof or a
// false disproof, only make the search slower.
//
// Replacement is generation-aged. Entries carry the pass in which they were
// last useful, refreshed on every probe hit, and a shard over capacity first
// sheds entries that no probe touched during the current pass.
struct BoundedTable {
    std::unordered_map<TTKey, TTEntry, TTKeyHash> map;
    std::size_t capacity = 0; // 0 means unbounded
    std::uint32_t generation = 1;
    std::uint64_t evictions = 0;

    bool probe(const TTKey& key, TTEntry& out) {
        auto it = map.find(key);
        if (it == map.end()) {
            return false;
        }
        it->second.gen = generation; // this entry is still earning its space
        out = it->second;
        return true;
    }

    void store(const TTKey& key, TTEntry entry) {
        entry.gen = generation;
        map[key] = std::move(entry);
        if (capacity != 0 && map.size() > capacity) {
            evict();
        }
    }

    void evict() {
        // First pass: drop anything untouched during the current generation.
        for (auto it = map.begin(); it != map.end();) {
            if (it->second.gen != generation) {
                it = map.erase(it);
                ++evictions;
            } else {
                ++it;
            }
        }
        // Everything is current, so age alone cannot choose a victim. Shed down
        // to a low-water mark to keep this from re-triggering on every store.
        const std::size_t low_water = capacity - capacity / 8;
        for (auto it = map.begin(); it != map.end() && map.size() > low_water;) {
            it = map.erase(it);
            ++evictions;
        }
    }

    void clear() {
        map.clear();
    }

    std::size_t size() const {
        return map.size();
    }
};

struct BoundEntry {
    bool has_ok = false;
    int ok_depth = 0;
    std::vector<Move> ok_pv;
    std::string ok_cert;
    bool has_fail = false;
    int fail_depth = 0;
};

// Exact proof table shared by every worker of a root-split search.
//
// Sharing is safe precisely because the key is exact and complete: an entry
// records the verdict for one board, side to move, attacker colour, node kind,
// castling and en-passant state, AT one exact remaining depth. That verdict is
// a pure function of the key, so it does not matter which worker computed it,
// and a reader cannot be misled by the writer's search context. This is the
// payoff for the conservative exact-key design.
//
// Storage is sharded so concurrent probes and stores on unrelated keys do not
// serialise. The shard is chosen from the high bits of the key hash, which are
// independent of the low bits an unordered_map uses for bucketing.
class SharedProofTable {
public:
    SharedProofTable(std::size_t shard_count, std::size_t reserve_total, std::size_t capacity_total) {
        std::size_t shards = 1;
        while (shards < shard_count) {
            shards <<= 1;
        }
        mask_ = shards - 1;
        shards_.reserve(shards);
        for (std::size_t i = 0; i < shards; ++i) {
            shards_.emplace_back(new Shard());
            if (reserve_total > 0) {
                shards_.back()->table.map.reserve(reserve_total / shards + 1);
            }
            // The budget is split evenly across shards. Hash spreading keeps
            // shard occupancy close enough that a per-shard cap is a faithful
            // proxy for a global cap, without a global counter on the hot path.
            shards_.back()->table.capacity = capacity_total == 0 ? 0 : std::max<std::size_t>(1, capacity_total / shards);
        }
    }

    bool probe(const TTKey& key, TTEntry& out) {
        Shard& shard = shard_for(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        return shard.table.probe(key, out);
    }

    void store(const TTKey& key, TTEntry entry) {
        Shard& shard = shard_for(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.table.store(key, std::move(entry));
    }

    // Adopt entries computed before the table existed, so the sequential
    // prelude of the cost gate is carried forward rather than redone.
    void import_from(const BoundedTable& src) {
        for (const auto& kv : src.map) {
            store(kv.first, kv.second);
        }
    }

    void clear() {
        for (auto& shard : shards_) {
            std::lock_guard<std::mutex> lock(shard->mutex);
            shard->table.clear();
        }
    }

    // Advance the aging generation. Called at depth boundaries, where no worker
    // is running, so no lock dance is needed beyond the per-shard guard.
    void next_generation() {
        for (auto& shard : shards_) {
            std::lock_guard<std::mutex> lock(shard->mutex);
            ++shard->table.generation;
        }
    }

    std::size_t size() const {
        std::size_t total = 0;
        for (const auto& shard : shards_) {
            std::lock_guard<std::mutex> lock(shard->mutex);
            total += shard->table.size();
        }
        return total;
    }

    std::uint64_t evictions() const {
        std::uint64_t total = 0;
        for (const auto& shard : shards_) {
            std::lock_guard<std::mutex> lock(shard->mutex);
            total += shard->table.evictions;
        }
        return total;
    }

private:
    struct Shard {
        mutable std::mutex mutex;
        BoundedTable table;
    };

    std::size_t shard_index(const TTKey& key) const {
        const std::size_t h = TTKeyHash{}(key);
        return (h >> 32) & mask_;
    }

    Shard& shard_for(const TTKey& key) {
        return *shards_[shard_index(key)];
    }

    std::vector<std::unique_ptr<Shard>> shards_;
    std::size_t mask_ = 0;
};

// Approximate resident cost of one table entry: the 40-byte exact key, the
// entry header, and the per-node and bucket overhead an unordered_map adds.
// This is an estimate, not an exact accounting -- the node-based container
// cannot give a hard byte bound -- so `-M` is honoured as a documented entry
// ceiling derived from this figure rather than as a guaranteed RSS limit.
constexpr std::size_t EST_BYTES_PER_ENTRY = 192;

inline std::size_t entry_capacity_for_mb(std::size_t megabytes) {
    if (megabytes == 0) {
        return 0; // explicit "unbounded"
    }
    return (megabytes * 1024u * 1024u) / EST_BYTES_PER_ENTRY;
}

// All tunable search options. This is the single value passed from the CLI to
// the search, and is also what each worker copies when a search is split
// across threads, so worker construction never has to re-parse or re-plumb
// individual flags.
struct SearchConfig {
    Color attacker = WHITE;
    RouteKind route = RouteKind::DepthFirst;
    bool debug = false;
    bool emit_proof = false;
    bool score_mates = false;
    bool score_checks = true;
    bool fast_check_score = false;
    bool refutation_hints = false;
    bool proof_hints = false;
    std::size_t tt_reserve = 0;
    bool move_reserve = false;
    std::size_t move_reserve_capacity = 64;
    bool inplace_order = false;
    bool static_pseudo = false;
    bool profile = false;
    std::size_t order_min_size = 2;
    bool bucket_order = false;
    bool keep_iter_tt = false;
    bool bound_tt_enabled = false;
    bool bound_tt_failures = false;
    bool ordered_check_shortcut = false;
    int threads = 1;
    bool fused_order = true;
    bool lazy_defender = false;
    bool shared_tt = true;
    std::size_t shared_tt_shards = 256;
    std::uint64_t parallel_min_nodes = 500;
    // -M megabytes. Converted to an entry ceiling via EST_BYTES_PER_ENTRY.
    std::size_t memory_mb = 64;
};

// Per-search mutable state. Inheriting the config keeps every existing
// `s.<option>` access valid while making the option set independently
// copyable.
struct Search : SearchConfig {
    Stats stats;
    BoundedTable tt;
    std::unordered_map<TTKey, BoundEntry, TTKeyHash> bound_tt;
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

    // When non-null, exact proof entries live in a table shared with the other
    // workers of this search instead of in the private `tt` map above.
    SharedProofTable* shared_table = nullptr;

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

struct SquareList {
    std::array<int, 8> sq{};
    int count = 0;
};

void add_square(SquareList& list, int sq) {
    list.sq[list.count++] = sq;
}

constexpr int DIRS[8][2] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
};

const std::array<SquareList, 64>& knight_table() {
    static const std::array<SquareList, 64> table = [] {
        std::array<SquareList, 64> out{};
        static const int delta[8][2] = {
            {1, 2}, {2, 1}, {2, -1}, {1, -2},
            {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
        };
        for (int sq = 0; sq < 64; ++sq) {
            int f = file_of(sq);
            int r = rank_of(sq);
            for (const auto& d : delta) {
                int tf = f + d[0];
                int tr = r + d[1];
                if (on_board(tf, tr)) {
                    add_square(out[sq], square_of(tf, tr));
                }
            }
        }
        return out;
    }();
    return table;
}

const std::array<SquareList, 64>& king_table() {
    static const std::array<SquareList, 64> table = [] {
        std::array<SquareList, 64> out{};
        for (int sq = 0; sq < 64; ++sq) {
            int f = file_of(sq);
            int r = rank_of(sq);
            for (int df = -1; df <= 1; ++df) {
                for (int dr = -1; dr <= 1; ++dr) {
                    if (df == 0 && dr == 0) continue;
                    int tf = f + df;
                    int tr = r + dr;
                    if (on_board(tf, tr)) {
                        add_square(out[sq], square_of(tf, tr));
                    }
                }
            }
        }
        return out;
    }();
    return table;
}

const std::array<std::array<SquareList, 64>, 2>& pawn_attacker_table() {
    static const std::array<std::array<SquareList, 64>, 2> table = [] {
        std::array<std::array<SquareList, 64>, 2> out{};
        for (int sq = 0; sq < 64; ++sq) {
            int f = file_of(sq);
            int r = rank_of(sq);
            int white_rank = r - 1;
            int black_rank = r + 1;
            for (int df : {-1, 1}) {
                int pf = f + df;
                if (on_board(pf, white_rank)) {
                    add_square(out[WHITE][sq], square_of(pf, white_rank));
                }
                if (on_board(pf, black_rank)) {
                    add_square(out[BLACK][sq], square_of(pf, black_rank));
                }
            }
        }
        return out;
    }();
    return table;
}

const std::array<std::array<SquareList, 64>, 8>& ray_table() {
    static const std::array<std::array<SquareList, 64>, 8> table = [] {
        std::array<std::array<SquareList, 64>, 8> out{};
        for (int dir = 0; dir < 8; ++dir) {
            for (int sq = 0; sq < 64; ++sq) {
                int f = file_of(sq) + DIRS[dir][0];
                int r = rank_of(sq) + DIRS[dir][1];
                while (on_board(f, r)) {
                    add_square(out[dir][sq], square_of(f, r));
                    f += DIRS[dir][0];
                    r += DIRS[dir][1];
                }
            }
        }
        return out;
    }();
    return table;
}

std::string sq_name(int sq) {
    std::string out;
    out.push_back(static_cast<char>('a' + file_of(sq)));
    out.push_back(static_cast<char>('1' + rank_of(sq)));
    return out;
}

std::string move_uci(const Move& m) {
    std::string out = sq_name(m.from) + sq_name(m.to);
    if (m.promo) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(m.promo))));
    }
    return out;
}

std::string json_quote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char ch : s) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

std::string trim(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string tok;
    while (in >> tok) {
        out.push_back(tok);
    }
    return out;
}

const char* route_name(RouteKind route) {
    switch (route) {
        case RouteKind::DepthFirst: return "depth-first";
        case RouteKind::ShallowFast: return "shallow-fast";
    }
    return "unknown";
}

std::optional<RouteKind> parse_route_kind(const std::string& name) {
    if (name == "depth-first" || name == "depth_first" || name == "df" || name == "dfs" || name == "default") {
        return RouteKind::DepthFirst;
    }
    if (name == "shallow-fast" || name == "shallow_fast" || name == "shallow" || name == "sf") {
        return RouteKind::ShallowFast;
    }
    return std::nullopt;
}

std::optional<Board> parse_fen4(const std::string& line) {
    auto tokens = split_ws(line);
    if (tokens.size() < 4) {
        return std::nullopt;
    }
    Board b;
    b.sq.fill('.');
    b.packed.fill(0);

    int rank = 7;
    int file = 0;
    for (char ch : tokens[0]) {
        if (ch == '/') {
            if (file != 8) {
                return std::nullopt;
            }
            --rank;
            file = 0;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            int n = ch - '0';
            if (n <= 0 || file + n > 8) {
                return std::nullopt;
            }
            file += n;
            continue;
        }
        if (std::string("PNBRQKpnbrqk").find(ch) == std::string::npos) {
            return std::nullopt;
        }
        if (!on_board(file, rank)) {
            return std::nullopt;
        }
        int sq = square_of(file, rank);
        set_square(b, sq, ch);
        if (ch == 'K') {
            b.king_sq[WHITE] = sq;
        } else if (ch == 'k') {
            b.king_sq[BLACK] = sq;
        }
        ++file;
    }
    if (rank != 0 || file != 8) {
        return std::nullopt;
    }

    b.stm = tokens[1] == "b" ? BLACK : WHITE;
    b.castling = 0;
    if (tokens[2].find('K') != std::string::npos) b.castling |= 1;
    if (tokens[2].find('Q') != std::string::npos) b.castling |= 2;
    if (tokens[2].find('k') != std::string::npos) b.castling |= 4;
    if (tokens[2].find('q') != std::string::npos) b.castling |= 8;

    b.ep = -1;
    if (tokens[3] != "-" && tokens[3].size() >= 2) {
        int ef = tokens[3][0] - 'a';
        int er = tokens[3][1] - '1';
        if (on_board(ef, er)) {
            b.ep = square_of(ef, er);
        }
    }
    return b;
}

std::string fen4(const Board& b) {
    std::ostringstream out;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            char p = b.sq[square_of(file, rank)];
            if (p == '.') {
                ++empty;
            } else {
                if (empty) {
                    out << empty;
                    empty = 0;
                }
                out << p;
            }
        }
        if (empty) out << empty;
        if (rank) out << '/';
    }
    out << (b.stm == WHITE ? " w " : " b ");
    std::string c;
    if (b.castling & 1) c.push_back('K');
    if (b.castling & 2) c.push_back('Q');
    if (b.castling & 4) c.push_back('k');
    if (b.castling & 8) c.push_back('q');
    out << (c.empty() ? "-" : c) << ' ';
    out << (b.ep >= 0 ? sq_name(b.ep) : "-");
    return out.str();
}

int king_square(const Board& b, Color c) {
    int cached = b.king_sq[c];
    char k = c == WHITE ? 'K' : 'k';
    if (cached >= 0 && cached < 64 && b.sq[cached] == k) {
        return cached;
    }
    for (int i = 0; i < 64; ++i) {
        if (b.sq[i] == k) {
            return i;
        }
    }
    return -1;
}

bool slider_attacker_matches(char p, bool diagonal) {
    char lp = static_cast<char>(std::tolower(static_cast<unsigned char>(p)));
    return diagonal ? (lp == 'b' || lp == 'q') : (lp == 'r' || lp == 'q');
}


// Bitboard forms of the leaper and ray tables, derived from the same square
// lists so the two representations cannot disagree.
struct AttackBitboards {
    std::array<std::uint64_t, 64> knight{};
    std::array<std::uint64_t, 64> king{};
    std::array<std::array<std::uint64_t, 64>, 2> pawn{}; // squares a pawn of [color] attacks target from
    std::array<std::array<std::uint64_t, 64>, 8> ray{};
    std::array<bool, 8> ray_ascending{};
};

const AttackBitboards& attack_bb() {
    static const AttackBitboards table = [] {
        AttackBitboards out{};
        auto pack = [](const SquareList& list) {
            std::uint64_t bb = 0;
            for (int i = 0; i < list.count; ++i) {
                bb |= 1ull << list.sq[i];
            }
            return bb;
        };
        for (int sq = 0; sq < 64; ++sq) {
            out.knight[sq] = pack(knight_table()[sq]);
            out.king[sq] = pack(king_table()[sq]);
            out.pawn[WHITE][sq] = pack(pawn_attacker_table()[WHITE][sq]);
            out.pawn[BLACK][sq] = pack(pawn_attacker_table()[BLACK][sq]);
            for (int dir = 0; dir < 8; ++dir) {
                out.ray[dir][sq] = pack(ray_table()[dir][sq]);
            }
        }
        // Whether a ray's squares ascend in index, which decides whether the
        // nearest blocker is the lowest or highest set bit. Derived from the
        // table rather than assumed from a direction convention.
        for (int dir = 0; dir < 8; ++dir) {
            for (int sq = 0; sq < 64; ++sq) {
                const SquareList& ray = ray_table()[dir][sq];
                if (ray.count > 0) {
                    out.ray_ascending[dir] = ray.sq[0] > sq;
                    break;
                }
            }
        }
        return out;
    }();
    return table;
}

bool is_attacked(const Board& b, int target, Color by) {
    const AttackBitboards& tb = attack_bb();
    const std::uint64_t them = b.by_color[by];

    if (tb.knight[target] & b.by_type[PT_KNIGHT] & them) {
        return true;
    }
    if (tb.king[target] & b.by_type[PT_KING] & them) {
        return true;
    }
    if (tb.pawn[by][target] & b.by_type[PT_PAWN] & them) {
        return true;
    }

    const std::uint64_t queens = b.by_type[PT_QUEEN] & them;
    const std::uint64_t diagonal = (b.by_type[PT_BISHOP] & them) | queens;
    const std::uint64_t straight = (b.by_type[PT_ROOK] & them) | queens;

    // Directions 0-3 are orthogonal and 4-7 diagonal, matching ray_table.
    for (int dir = 0; dir < 8; ++dir) {
        const std::uint64_t sliders = dir < 4 ? straight : diagonal;
        if (!sliders) {
            continue;
        }
        const std::uint64_t blockers = tb.ray[dir][target] & b.occ;
        if (!blockers) {
            continue;
        }
        // Only the nearest piece along the ray can attack the target.
        const int first = tb.ray_ascending[dir] ? lsb_index(blockers) : msb_index(blockers);
        if ((1ull << first) & sliders) {
            return true;
        }
    }
    return false;
}

bool in_check(const Board& b, Color c) {
    int k = king_square(b, c);
    return k < 0 || is_attacked(b, k, other(c));
}

void add_move(std::vector<Move>& moves, int from, int to, char promo = 0, bool castle = false, bool ep = false) {
    Move m;
    m.from = from;
    m.to = to;
    m.promo = promo;
    m.castle = castle;
    m.ep = ep;
    moves.push_back(m);
}

void add_move(MoveList& moves, int from, int to, char promo = 0, bool castle = false, bool ep = false) {
    Move m;
    m.from = from;
    m.to = to;
    m.promo = promo;
    m.castle = castle;
    m.ep = ep;
    moves.push_back(m);
}

template <typename MoveSink>
void gen_pseudo(const Board& b, MoveSink& moves) {
    Color us = b.stm;
    for (int from = 0; from < 64; ++from) {
        char p = b.sq[from];
        if (!is_piece_color(p, us)) continue;
        char lp = static_cast<char>(std::tolower(static_cast<unsigned char>(p)));
        int f = file_of(from);
        int r = rank_of(from);

        if (lp == 'p') {
            int dir = us == WHITE ? 1 : -1;
            int start_rank = us == WHITE ? 1 : 6;
            int promo_rank = us == WHITE ? 7 : 0;
            int one_r = r + dir;
            if (on_board(f, one_r)) {
                int one = square_of(f, one_r);
                if (b.sq[one] == '.') {
                    if (one_r == promo_rank) {
                        for (char pr : {'q', 'r', 'b', 'n'}) add_move(moves, from, one, pr);
                    } else {
                        add_move(moves, from, one);
                        int two_r = r + 2 * dir;
                        if (r == start_rank && on_board(f, two_r)) {
                            int two = square_of(f, two_r);
                            if (b.sq[two] == '.') add_move(moves, from, two);
                        }
                    }
                }
            }
            for (int df : {-1, 1}) {
                int cf = f + df;
                int cr = r + dir;
                if (!on_board(cf, cr)) continue;
                int to = square_of(cf, cr);
                if ((is_enemy_piece(b.sq[to], us) && !is_king_piece(b.sq[to])) || to == b.ep) {
                    bool is_ep = to == b.ep && b.sq[to] == '.';
                    if (cr == promo_rank) {
                        for (char pr : {'q', 'r', 'b', 'n'}) add_move(moves, from, to, pr, false, is_ep);
                    } else {
                        add_move(moves, from, to, 0, false, is_ep);
                    }
                }
            }
        } else if (lp == 'n') {
            const SquareList& targets = knight_table()[from];
            for (int i = 0; i < targets.count; ++i) {
                int to = targets.sq[i];
                if (!is_piece_color(b.sq[to], us) && !is_king_piece(b.sq[to])) add_move(moves, from, to);
            }
        } else if (lp == 'b' || lp == 'r' || lp == 'q') {
            int first = lp == 'b' ? 4 : 0;
            int last = lp == 'r' ? 4 : 8;
            const auto& rays = ray_table();
            for (int i = first; i < last; ++i) {
                const SquareList& ray = rays[i][from];
                for (int j = 0; j < ray.count; ++j) {
                    int to = ray.sq[j];
                    if (is_piece_color(b.sq[to], us)) break;
                    if (is_king_piece(b.sq[to])) break;
                    add_move(moves, from, to);
                    if (b.sq[to] != '.') break;
                }
            }
        } else if (lp == 'k') {
            const SquareList& targets = king_table()[from];
            for (int i = 0; i < targets.count; ++i) {
                int to = targets.sq[i];
                if (!is_piece_color(b.sq[to], us) && !is_king_piece(b.sq[to])) add_move(moves, from, to);
            }
            if (us == WHITE && from == square_of(4, 0) && !in_check(b, WHITE)) {
                if ((b.castling & 1) && b.sq[square_of(5, 0)] == '.' && b.sq[square_of(6, 0)] == '.' &&
                    !is_attacked(b, square_of(5, 0), BLACK) && !is_attacked(b, square_of(6, 0), BLACK)) {
                    add_move(moves, from, square_of(6, 0), 0, true);
                }
                if ((b.castling & 2) && b.sq[square_of(3, 0)] == '.' && b.sq[square_of(2, 0)] == '.' && b.sq[square_of(1, 0)] == '.' &&
                    !is_attacked(b, square_of(3, 0), BLACK) && !is_attacked(b, square_of(2, 0), BLACK)) {
                    add_move(moves, from, square_of(2, 0), 0, true);
                }
            }
            if (us == BLACK && from == square_of(4, 7) && !in_check(b, BLACK)) {
                if ((b.castling & 4) && b.sq[square_of(5, 7)] == '.' && b.sq[square_of(6, 7)] == '.' &&
                    !is_attacked(b, square_of(5, 7), WHITE) && !is_attacked(b, square_of(6, 7), WHITE)) {
                    add_move(moves, from, square_of(6, 7), 0, true);
                }
                if ((b.castling & 8) && b.sq[square_of(3, 7)] == '.' && b.sq[square_of(2, 7)] == '.' && b.sq[square_of(1, 7)] == '.' &&
                    !is_attacked(b, square_of(3, 7), WHITE) && !is_attacked(b, square_of(2, 7), WHITE)) {
                    add_move(moves, from, square_of(2, 7), 0, true);
                }
            }
        }
    }
}

Board make_move(Board b, const Move& m) {
    char p = b.sq[m.from];
    set_square(b, m.from, '.');

    if (m.ep) {
        int cap_sq = m.to + (b.stm == WHITE ? -8 : 8);
        set_square(b, cap_sq, '.');
    }

    char placed = p;
    if (m.promo) {
        placed = b.stm == WHITE ? static_cast<char>(std::toupper(static_cast<unsigned char>(m.promo))) : m.promo;
    }
    set_square(b, m.to, placed);
    if (p == 'K') {
        b.king_sq[WHITE] = m.to;
    } else if (p == 'k') {
        b.king_sq[BLACK] = m.to;
    }

    if (m.castle) {
        if (p == 'K' && m.to == square_of(6, 0)) {
            set_square(b, square_of(5, 0), 'R');
            set_square(b, square_of(7, 0), '.');
        } else if (p == 'K' && m.to == square_of(2, 0)) {
            set_square(b, square_of(3, 0), 'R');
            set_square(b, square_of(0, 0), '.');
        } else if (p == 'k' && m.to == square_of(6, 7)) {
            set_square(b, square_of(5, 7), 'r');
            set_square(b, square_of(7, 7), '.');
        } else if (p == 'k' && m.to == square_of(2, 7)) {
            set_square(b, square_of(3, 7), 'r');
            set_square(b, square_of(0, 7), '.');
        }
    }

    if (p == 'K') b.castling &= ~3u;
    if (p == 'k') b.castling &= ~12u;
    // Castling rights are revoked by SQUARE, never by captured piece type.
    //
    // Keying off the piece type is wrong once promotion exists: capturing a
    // promoted rook anywhere on the board would strip the owner's rights even
    // though both original corner rooks are untouched. The square tests below
    // are already complete -- a move from a corner covers the rook leaving, and
    // a move to a corner covers the rook being captured there (if some other
    // piece occupies that corner, the right was necessarily already gone).
    if (m.from == square_of(0, 0) || m.to == square_of(0, 0)) b.castling &= ~2u;
    if (m.from == square_of(7, 0) || m.to == square_of(7, 0)) b.castling &= ~1u;
    if (m.from == square_of(0, 7) || m.to == square_of(0, 7)) b.castling &= ~8u;
    if (m.from == square_of(7, 7) || m.to == square_of(7, 7)) b.castling &= ~4u;

    b.ep = -1;
    if (std::tolower(static_cast<unsigned char>(p)) == 'p' && std::abs(m.to - m.from) == 16) {
        b.ep = (m.from + m.to) / 2;
    }
    b.stm = other(b.stm);
    return b;
}

std::vector<Move> legal_moves_vector(const Board& b, bool move_reserve, std::size_t move_reserve_capacity) {
    std::vector<Move> pseudo;
    if (move_reserve) {
        pseudo.reserve(move_reserve_capacity);
    }
    gen_pseudo(b, pseudo);
    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    for (const Move& m : pseudo) {
        Board nb = make_move(b, m);
        if (!in_check(nb, other(nb.stm))) {
            legal.push_back(m);
        }
    }
    return legal;
}

std::vector<Move> legal_moves(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    if (static_pseudo) {
        MoveList pseudo;
        gen_pseudo(b, pseudo);
        if (!pseudo.overflow) {
            std::vector<Move> legal;
            legal.reserve(pseudo.size());
            for (const Move& m : pseudo) {
                Board nb = make_move(b, m);
                if (!in_check(nb, other(nb.stm))) {
                    legal.push_back(m);
                }
            }
            return legal;
        }
    }
    return legal_moves_vector(b, move_reserve, move_reserve_capacity);
}

bool has_legal_move_vector(const Board& b, bool move_reserve, std::size_t move_reserve_capacity) {
    std::vector<Move> pseudo;
    if (move_reserve) {
        pseudo.reserve(move_reserve_capacity);
    }
    gen_pseudo(b, pseudo);
    for (const Move& m : pseudo) {
        Board nb = make_move(b, m);
        if (!in_check(nb, other(nb.stm))) {
            return true;
        }
    }
    return false;
}

bool has_legal_move(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    if (static_pseudo) {
        MoveList pseudo;
        gen_pseudo(b, pseudo);
        if (!pseudo.overflow) {
            for (const Move& m : pseudo) {
                Board nb = make_move(b, m);
                if (!in_check(nb, other(nb.stm))) {
                    return true;
                }
            }
            return false;
        }
    }
    return has_legal_move_vector(b, move_reserve, move_reserve_capacity);
}

bool is_checkmate(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    return in_check(b, b.stm) && !has_legal_move(b, move_reserve, move_reserve_capacity, static_pseudo);
}

char piece_after_move(const Board& b, const Move& m, int sq) {
    char p = b.sq[m.from];
    char placed = p;
    if (m.promo) {
        placed = b.stm == WHITE ? static_cast<char>(std::toupper(static_cast<unsigned char>(m.promo))) : m.promo;
    }

    if (sq == m.from) {
        return '.';
    }
    if (m.ep) {
        int cap_sq = m.to + (b.stm == WHITE ? -8 : 8);
        if (sq == cap_sq) {
            return '.';
        }
    }
    if (sq == m.to) {
        return placed;
    }
    if (m.castle) {
        if (p == 'K' && m.to == square_of(6, 0)) {
            if (sq == square_of(7, 0)) return '.';
            if (sq == square_of(5, 0)) return 'R';
        } else if (p == 'K' && m.to == square_of(2, 0)) {
            if (sq == square_of(0, 0)) return '.';
            if (sq == square_of(3, 0)) return 'R';
        } else if (p == 'k' && m.to == square_of(6, 7)) {
            if (sq == square_of(7, 7)) return '.';
            if (sq == square_of(5, 7)) return 'r';
        } else if (p == 'k' && m.to == square_of(2, 7)) {
            if (sq == square_of(0, 7)) return '.';
            if (sq == square_of(3, 7)) return 'r';
        }
    }
    return b.sq[sq];
}

bool attacked_by_slider_after_move(const Board& b, const Move& m, int target, Color by, int first_dir, int last_dir, bool diagonal) {
    const auto& rays = ray_table();
    for (int dir = first_dir; dir < last_dir; ++dir) {
        const SquareList& ray = rays[dir][target];
        for (int i = 0; i < ray.count; ++i) {
            char p = piece_after_move(b, m, ray.sq[i]);
            if (p != '.') {
                if (is_piece_color(p, by)) {
                    if (slider_attacker_matches(p, diagonal)) {
                        return true;
                    }
                }
                break;
            }
        }
    }
    return false;
}

bool is_attacked_after_move(const Board& b, const Move& m, int target, Color by) {
    const SquareList& pawns = pawn_attacker_table()[by][target];
    for (int i = 0; i < pawns.count; ++i) {
        char p = piece_after_move(b, m, pawns.sq[i]);
        if (p == (by == WHITE ? 'P' : 'p')) {
            return true;
        }
    }

    const SquareList& knights = knight_table()[target];
    for (int i = 0; i < knights.count; ++i) {
        char p = piece_after_move(b, m, knights.sq[i]);
        if (p == (by == WHITE ? 'N' : 'n')) {
            return true;
        }
    }

    if (attacked_by_slider_after_move(b, m, target, by, 4, 8, true)) {
        return true;
    }
    if (attacked_by_slider_after_move(b, m, target, by, 0, 4, false)) {
        return true;
    }

    const SquareList& kings = king_table()[target];
    for (int i = 0; i < kings.count; ++i) {
        char p = piece_after_move(b, m, kings.sq[i]);
        if (p == (by == WHITE ? 'K' : 'k')) {
            return true;
        }
    }
    return false;
}

bool move_gives_check_fast(const Board& b, const Move& m) {
    int enemy_king = king_square(b, other(b.stm));
    return enemy_king < 0 || is_attacked_after_move(b, m, enemy_king, b.stm);
}

// Move-ordering terms that need no child board: capture, promotion, and moving
// piece. Shared by the fused and split scoring paths so the two cannot drift.
int static_move_terms(const Board& b, const Move& m) {
    int score = 0;
    if (b.sq[m.to] != '.' || m.ep) score += 10000;
    if (m.promo) score += 8000;
    char p = std::tolower(static_cast<unsigned char>(b.sq[m.from]));
    if (p == 'q') score += 50;
    if (p == 'r') score += 40;
    if (p == 'b' || p == 'n') score += 30;
    return score;
}

// Ordering terms that require the child board, given that board.
int child_move_terms(const Board& nb, bool score_mates, bool score_checks,
                     bool move_reserve, std::size_t move_reserve_capacity, bool static_pseudo) {
    int score = 0;
    if (score_mates) {
        if (is_checkmate(nb, move_reserve, move_reserve_capacity, static_pseudo)) score += 1000000;
        if (score_checks && in_check(nb, nb.stm)) score += 50000;
    } else if (score_checks) {
        if (in_check(nb, nb.stm)) score += 50000;
    }
    return score;
}

int move_score(const Board& b, const Move& m, bool score_mates, bool score_checks, bool fast_check_score, bool move_reserve, std::size_t move_reserve_capacity, bool static_pseudo) {
    int score = 0;
    if (score_mates) {
        Board nb = make_move(b, m);
        if (is_checkmate(nb, move_reserve, move_reserve_capacity, static_pseudo)) score += 1000000;
        if (score_checks && in_check(nb, nb.stm)) score += 50000;
    } else if (score_checks) {
        bool gives_check = false;
        if (fast_check_score) {
            gives_check = move_gives_check_fast(b, m);
        } else {
            Board nb = make_move(b, m);
            gives_check = in_check(nb, nb.stm);
        }
        if (gives_check) score += 50000;
    }
    return score + static_move_terms(b, m);
}

void stable_bucket_order(std::vector<Move>& moves) {
    std::vector<int> scores;
    scores.reserve(moves.size());
    for (const Move& move : moves) {
        auto it = std::find(scores.begin(), scores.end(), move.score);
        if (it != scores.end()) {
            continue;
        }
        auto insert_at = std::find_if(scores.begin(), scores.end(), [&](int score) {
            return move.score > score;
        });
        scores.insert(insert_at, move.score);
    }

    std::vector<Move> ordered;
    ordered.reserve(moves.size());
    for (int score : scores) {
        for (const Move& move : moves) {
            if (move.score == score) {
                ordered.push_back(move);
            }
        }
    }
    moves.swap(ordered);
}

void order_moves(const Board& b, std::vector<Move>& moves, bool score_mates, bool score_checks, bool fast_check_score, bool move_reserve, std::size_t move_reserve_capacity, bool static_pseudo, bool inplace_order, bool bucket_order) {
    if (moves.size() < 2) {
        return;
    }
    if (inplace_order) {
        for (Move& move : moves) {
            move.score = move_score(b, move, score_mates, score_checks, fast_check_score, move_reserve, move_reserve_capacity, static_pseudo);
        }
        if (bucket_order) {
            stable_bucket_order(moves);
            return;
        }
        std::stable_sort(moves.begin(), moves.end(), [](const Move& a, const Move& c) {
            return a.score > c.score;
        });
        return;
    }
    struct ScoredMove {
        Move move;
        int score = 0;
    };
    std::vector<ScoredMove> scored;
    scored.reserve(moves.size());
    for (const Move& move : moves) {
        scored.push_back({move, move_score(b, move, score_mates, score_checks, fast_check_score, move_reserve, move_reserve_capacity, static_pseudo)});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const ScoredMove& a, const ScoredMove& c) {
        return a.score > c.score;
    });
    for (std::size_t i = 0; i < scored.size(); ++i) {
        moves[i] = scored[i].move;
    }
}

TTKey tt_key(const Board& b, int depth, char kind, Color attacker) {
    TTKey k;
    k.board = b.packed;
    std::uint64_t ep = static_cast<std::uint64_t>(b.ep + 1);
    k.context = static_cast<std::uint64_t>(static_cast<std::uint32_t>(depth))
        | (static_cast<std::uint64_t>(b.stm) << 32)
        | (static_cast<std::uint64_t>(attacker) << 33)
        | (static_cast<std::uint64_t>(kind == 'D' ? 1 : 0) << 34)
        | (static_cast<std::uint64_t>(b.castling & 0x0fu) << 35)
        | (ep << 39);
    return k;
}

TTKey move_hint_key(const Board& b, char kind, Color attacker) {
    return tt_key(b, 0, kind, attacker);
}

bool same_move(const Move& a, const Move& b) {
    return a.from == b.from
        && a.to == b.to
        && a.promo == b.promo
        && a.castle == b.castle
        && a.ep == b.ep;
}

bool move_to_front(std::vector<Move>& moves, const Move& hint) {
    for (std::size_t i = 0; i < moves.size(); ++i) {
        if (same_move(moves[i], hint)) {
            if (i != 0) {
                std::rotate(moves.begin(), moves.begin() + static_cast<std::ptrdiff_t>(i), moves.begin() + static_cast<std::ptrdiff_t>(i + 1));
            }
            return true;
        }
    }
    return false;
}

// Generate legal moves and their ordering scores in a single pass.
//
// Legality already requires building the child board and asking whether the
// mover left their own king in check. The check-scoring term asks whether the
// OPPONENT is now in check -- the same child board, the other king. Computing
// the two separately builds every child board twice. Fusing them removes one
// full board copy per move from every ordered move list, which on a hard-suite
// run is tens of millions of copies.
//
// This is an evaluation-order change only. The scores produced are identical to
// the split path, so the resulting move order, and therefore the search, is
// unchanged.
std::vector<Move> legal_moves_fused(const Board& b, const SearchConfig& cfg, bool& out_scored) {
    out_scored = false;
    std::vector<Move> pseudo;
    if (cfg.move_reserve) {
        pseudo.reserve(cfg.move_reserve_capacity);
    }
    gen_pseudo(b, pseudo);

    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    const bool want_scores = !cfg.fast_check_score;
    for (Move m : pseudo) {
        Board nb = make_move(b, m);
        if (in_check(nb, other(nb.stm))) {
            continue; // illegal: the mover left their own king attacked
        }
        if (want_scores) {
            m.score = child_move_terms(nb, cfg.score_mates, cfg.score_checks,
                                       cfg.move_reserve, cfg.move_reserve_capacity,
                                       cfg.static_pseudo)
                    + static_move_terms(b, m);
        }
        legal.push_back(m);
    }
    out_scored = want_scores;
    return legal;
}

// Obtain the move list for a search node, ordered if the node warrants it.
// Returns whether the returned moves carry ordering scores.
std::vector<Move> generate_ordered_moves(Search& s, const Board& b, bool& moves_scored) {
    moves_scored = false;
    if (s.fused_order && !s.static_pseudo) {
        bool scored = false;
        std::vector<Move> moves = legal_moves_fused(b, s, scored);
        if (scored && moves.size() >= std::max<std::size_t>(2, s.order_min_size)) {
            ++s.stats.order_calls;
            s.stats.order_moves += moves.size();
            if (s.bucket_order) {
                stable_bucket_order(moves);
            } else {
                std::stable_sort(moves.begin(), moves.end(),
                                 [](const Move& a, const Move& c) { return a.score > c.score; });
            }
            moves_scored = true;
        }
        return moves;
    }
    std::vector<Move> moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    if (moves.size() >= std::max<std::size_t>(2, s.order_min_size)) {
        ++s.stats.order_calls;
        s.stats.order_moves += moves.size();
        order_moves(b, moves, s.score_mates, s.score_checks, s.fast_check_score,
                    s.move_reserve, s.move_reserve_capacity, s.static_pseudo,
                    s.inplace_order, s.bucket_order);
        moves_scored = true;
    }
    return moves;
}

// Pseudo-legal defender replies, ordered WITHOUT building any child board.
//
// A defender (AND) node stops at the first reply that survives, so it typically
// searches ~1 of ~19 generated replies. Eagerly proving legality for all of
// them builds eighteen child boards that are then discarded.
//
// `move_gives_check_fast` answers the check-ordering term by querying the
// post-move board virtually, so ordering needs no child board at all. Legality
// is then established lazily, only for replies actually reached.
//
// The ordering predicate is identical to the eager path -- both ask whether the
// move gives check -- and the sort is stable, so removing the illegal moves
// from the sequence leaves the order of the legal ones unchanged. The sequence
// of legal replies searched is therefore the same as the eager path's, and so
// is the resulting search.
std::vector<Move> pseudo_defender_moves(Search& s, const Board& b) {
    std::vector<Move> pseudo;
    if (s.move_reserve) {
        pseudo.reserve(s.move_reserve_capacity);
    }
    gen_pseudo(b, pseudo);
    s.stats.defender_pseudo_moves += pseudo.size();
    if (pseudo.size() >= std::max<std::size_t>(2, s.order_min_size)) {
        ++s.stats.order_calls;
        s.stats.order_moves += pseudo.size();
        for (Move& m : pseudo) {
            int score = static_move_terms(b, m);
            if (s.score_checks && move_gives_check_fast(b, m)) {
                score += 50000;
            }
            m.score = score;
        }
        if (s.bucket_order) {
            stable_bucket_order(pseudo);
        } else {
            std::stable_sort(pseudo.begin(), pseudo.end(),
                             [](const Move& a, const Move& c) { return a.score > c.score; });
        }
    }
    return pseudo;
}

bool should_order(const Search& s, std::size_t move_count) {
    return move_count >= std::max<std::size_t>(2, s.order_min_size);
}

bool probe_exact_proof_table(Search& s, const TTKey& key, Proof& out) {
    ++s.stats.tt_probes;
    TTEntry entry;
    if (s.shared_table != nullptr) {
        if (!s.shared_table->probe(key, entry)) {
            return false;
        }
    } else if (!s.tt.probe(key, entry)) {
        return false;
    }
    ++s.stats.tt_hits;
    if (entry.kind == TTEntryKind::Proof) {
        ++s.stats.exact_tt_proof_hits;
        out = {true, std::move(entry.pv), std::move(entry.cert)};
    } else {
        ++s.stats.exact_tt_disproof_hits;
        out = {};
    }
    return true;
}

void store_exact_proof_table(Search& s, const TTKey& key, const Proof& proof) {
    // An aborted subtree produced no verdict. Storing its empty result would
    // cache a false disproof, so nothing is written once the search unwinds.
    if (s.aborted) {
        return;
    }
    ++s.stats.tt_stores;
    TTEntry entry;
    if (proof.ok) {
        ++s.stats.exact_tt_proof_stores;
        entry = {TTEntryKind::Proof, proof.pv, proof.cert};
    } else {
        ++s.stats.exact_tt_disproof_stores;
        entry = {TTEntryKind::Disproof, {}, ""};
    }
    if (s.shared_table != nullptr) {
        s.shared_table->store(key, std::move(entry));
    } else {
        s.tt.store(key, std::move(entry));
    }
}

bool probe_bound_tt(Search& s, const TTKey& key, int depth, Proof& out) {
    if (!s.bound_tt_enabled) {
        return false;
    }
    ++s.stats.bound_tt_probes;
    auto it = s.bound_tt.find(key);
    if (it == s.bound_tt.end()) {
        return false;
    }
    const BoundEntry& entry = it->second;
    if (entry.has_ok && entry.ok_depth <= depth) {
        ++s.stats.bound_tt_hits;
        ++s.stats.bound_tt_ok_hits;
        out = {true, entry.ok_pv, entry.ok_cert};
        return true;
    }
    if (s.bound_tt_failures && entry.has_fail && entry.fail_depth >= depth) {
        ++s.stats.bound_tt_hits;
        ++s.stats.bound_tt_fail_hits;
        out = {};
        return true;
    }
    return false;
}

void store_bound_tt(Search& s, const TTKey& key, int depth, const Proof& proof) {
    if (!s.bound_tt_enabled) {
        return;
    }
    // Same invariant as the exact table: an abandoned search has no verdict.
    if (s.aborted) {
        return;
    }
    if (!proof.ok && !s.bound_tt_failures) {
        return;
    }
    ++s.stats.bound_tt_stores;
    BoundEntry& entry = s.bound_tt[key];
    if (proof.ok) {
        if (!entry.has_ok || depth < entry.ok_depth) {
            entry.has_ok = true;
            entry.ok_depth = depth;
            entry.ok_pv = proof.pv;
            entry.ok_cert = proof.cert;
        }
    } else if (s.bound_tt_failures && (!entry.has_fail || depth > entry.fail_depth)) {
        entry.has_fail = true;
        entry.fail_depth = depth;
    }
}

Proof prove_attacker(Search& s, const Board& b, int depth);

Proof prove_defender(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.defender_nodes;
    TTKey key = tt_key(b, depth, 'D', s.attacker);
    Proof exact_cached;
    if (probe_exact_proof_table(s, key, exact_cached)) {
        return exact_cached;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'D', s.attacker);
            have_hint_key = true;
        }
        return hint_key;
    };
    if (s.bound_tt_enabled) {
        Proof cached;
        if (probe_bound_tt(s, get_hint_key(), depth, cached)) {
            return cached;
        }
    }

    // Lazy defender generation is only sound when ordering does not depend on
    // building the child board, which rules out --score-mates.
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
        store_exact_proof_table(s, key, {});
        if (s.bound_tt_enabled) {
            store_bound_tt(s, get_hint_key(), depth, {});
        }
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
    std::vector<Move> representative;
    std::vector<std::string> branch_certs;
    if (s.emit_proof) {
        branch_certs.reserve(replies.size());
    }
    bool any_legal = false;
    for (const Move& dmove : replies) {
        Board nb = make_move(b, dmove);
        if (lazy) {
            if (in_check(nb, other(nb.stm))) {
                ++s.stats.defender_lazy_skipped;
                continue; // pseudo-legal only: the defender left their king attacked
            }
            ++s.stats.defender_moves;
        }
        any_legal = true;
        ++s.stats.defender_replies_tried;
        Proof child = prove_attacker(s, nb, depth);
        if (s.aborted) {
            return {};
        }
        if (!child.ok) {
            ++s.stats.defender_refutations;
            if (s.debug) {
                std::cerr << "defender_refutes depth=" << depth << " move=" << move_uci(dmove)
                          << " fen=" << fen4(nb) << "\n";
            }
            if (s.refutation_hints) {
                ++s.stats.refutation_hint_stores;
                s.defender_refutations[get_hint_key()] = dmove;
            }
            store_exact_proof_table(s, key, {});
            if (s.bound_tt_enabled) {
                store_bound_tt(s, get_hint_key(), depth, {});
            }
            return {};
        }
        std::vector<Move> candidate;
        candidate.push_back(dmove);
        candidate.insert(candidate.end(), child.pv.begin(), child.pv.end());
        if (candidate.size() > representative.size()) {
            representative = std::move(candidate);
        }
        if (s.emit_proof) {
            branch_certs.push_back("{\"r\":" + json_quote(move_uci(dmove)) + ",\"p\":" + child.cert + "}");
        }
    }
    // With lazy generation the list held pseudo-legal moves, so an exhausted
    // loop does not by itself mean every legal reply was refuted -- it may mean
    // there were none. That is stalemate, not mate, and must be treated exactly
    // like the empty-list case above rather than reported as a proof.
    if (lazy && !any_legal) {
        store_exact_proof_table(s, key, {});
        if (s.bound_tt_enabled) {
            store_bound_tt(s, get_hint_key(), depth, {});
        }
        return {};
    }
    std::string cert = "[";
    if (s.emit_proof) {
        for (std::size_t i = 0; i < branch_certs.size(); ++i) {
            if (i) cert.push_back(',');
            cert += branch_certs[i];
        }
        cert.push_back(']');
    } else {
        cert.clear();
    }
    Proof proof{true, representative, cert};
    store_exact_proof_table(s, key, proof);
    if (s.bound_tt_enabled) {
        store_bound_tt(s, get_hint_key(), depth, proof);
    }
    return proof;
}

Proof prove_attacker(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (depth <= 0 || b.stm != s.attacker) {
        return {};
    }
    TTKey key = tt_key(b, depth, 'A', s.attacker);
    Proof exact_cached;
    if (probe_exact_proof_table(s, key, exact_cached)) {
        return exact_cached;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'A', s.attacker);
            have_hint_key = true;
        }
        return hint_key;
    };
    if (s.bound_tt_enabled) {
        Proof cached;
        if (probe_bound_tt(s, get_hint_key(), depth, cached)) {
            return cached;
        }
    }

    bool moves_scored = false;
    auto moves = generate_ordered_moves(s, b, moves_scored);
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();
    if (s.proof_hints) {
        const TTKey& proof_key = get_hint_key();
        ++s.stats.proof_hint_probes;
        if (auto hint = s.attacker_proofs.find(proof_key); hint != s.attacker_proofs.end()) {
            if (move_to_front(moves, hint->second)) {
                ++s.stats.proof_hint_hits;
            }
        }
    }
    const bool can_use_ordered_check_shortcut = s.ordered_check_shortcut && moves_scored && s.score_checks && !s.score_mates;
    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        Board nb = make_move(b, amove);
        ++s.stats.immediate_mate_tests;
        bool mate = false;
        if (can_use_ordered_check_shortcut) {
            ++s.stats.ordered_check_shortcut_uses;
            if (amove.score >= 50000) {
                ++s.stats.ordered_check_shortcut_checks;
                mate = !has_legal_move(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
            } else {
                ++s.stats.ordered_check_shortcut_skips;
            }
        } else {
            mate = is_checkmate(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
        }
        if (mate) {
            ++s.stats.immediate_mates;
            std::vector<Move> pv{amove};
            std::string cert;
            if (s.emit_proof) {
                cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"mate\":true}";
            }
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs[get_hint_key()] = amove;
            }
            Proof proof{true, pv, cert};
            store_exact_proof_table(s, key, proof);
            if (s.bound_tt_enabled) {
                store_bound_tt(s, get_hint_key(), depth, proof);
            }
            return proof;
        }
        if (s.debug && depth == 1 && in_check(nb, nb.stm)) {
            auto replies = legal_moves(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
            std::cerr << "mate1_candidate_not_mate move=" << move_uci(amove)
                      << " defender_legal=" << replies.size()
                      << " fen=" << fen4(nb) << "\n";
            if (replies.size() <= 4) {
                std::cerr << "  replies:";
                for (const Move& r : replies) {
                    Board rb = make_move(nb, r);
                    Color moved = other(rb.stm);
                    int ksq = king_square(rb, moved);
                    std::cerr << ' ' << move_uci(r)
                              << "(k=" << (ksq >= 0 ? sq_name(ksq) : "none")
                              << ",chk=" << (in_check(rb, moved) ? "1" : "0")
                              << ",fen=" << fen4(rb);
                    if (ksq >= 0) {
                        std::cerr << ",left=";
                        for (int ff = file_of(ksq) - 1; ff >= 0; --ff) {
                            int rsq = square_of(ff, rank_of(ksq));
                            std::cerr << sq_name(rsq) << rb.sq[rsq];
                        }
                    }
                    std::cerr << ")";
                }
                std::cerr << "\n";
            }
        }
        if (depth > 1) {
            Proof all_replies = prove_defender(s, nb, depth - 1);
            if (s.aborted) {
                return {};
            }
            if (all_replies.ok) {
                std::vector<Move> pv{amove};
                pv.insert(pv.end(), all_replies.pv.begin(), all_replies.pv.end());
                std::string cert;
                if (s.emit_proof) {
                    cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"d\":" + all_replies.cert + "}";
                }
                if (s.proof_hints) {
                    ++s.stats.proof_hint_stores;
                    s.attacker_proofs[get_hint_key()] = amove;
                }
                Proof proof{true, pv, cert};
                store_exact_proof_table(s, key, proof);
                if (s.bound_tt_enabled) {
                    store_bound_tt(s, get_hint_key(), depth, proof);
                }
                return proof;
            }
            if (s.debug) {
                std::cerr << "attacker_move_failed depth=" << depth << " move=" << move_uci(amove)
                          << " fen=" << fen4(nb) << "\n";
            }
        }
    }
    store_exact_proof_table(s, key, {});
    if (s.bound_tt_enabled) {
        store_bound_tt(s, get_hint_key(), depth, {});
    }
    return {};
}

// One worker's coordination slot. `current_root` is the root move index the
// worker is presently proving; `cancel` is the flag its Search polls.
struct WorkerSlot {
    std::atomic<int> current_root{0};
    std::atomic<bool> cancel{false};
};

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
bool run_root_split_depth(Search& s, std::vector<std::unique_ptr<Search>>& workers,
                          std::vector<std::unique_ptr<WorkerSlot>>& slots,
                          const Board& b, int depth, Proof& out) {
    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    if (moves.empty()) {
        return false;
    }
    bool moves_scored = false;
    if (should_order(s, moves.size())) {
        order_moves(b, moves, s.score_mates, s.score_checks, s.fast_check_score,
                    s.move_reserve, s.move_reserve_capacity, s.static_pseudo,
                    s.inplace_order, s.bucket_order);
        moves_scored = true;
    }
    if (s.proof_hints) {
        TTKey hint_key = move_hint_key(b, 'A', s.attacker);
        if (auto hint = s.attacker_proofs.find(hint_key); hint != s.attacker_proofs.end()) {
            move_to_front(moves, hint->second);
        }
    }
    const bool shortcut = s.ordered_check_shortcut && moves_scored && s.score_checks && !s.score_mates;

    const int n = static_cast<int>(moves.size());
    const int worker_count = std::min<int>(static_cast<int>(workers.size()), n);

    std::atomic<int> next_index{0};
    std::atomic<int> best_index{n}; // lowest root index proved so far
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
            slot.current_root.store(i, std::memory_order_release);
            slot.cancel.store(false, std::memory_order_release);
            ws.aborted = false;
            // Re-read after publishing our index: this closes the window where
            // a finishing worker scanned the slots before we announced this
            // move and so did not cancel us.
            if (i > best_index.load(std::memory_order_acquire)) {
                continue;
            }

            Board nb = make_move(b, moves[static_cast<std::size_t>(i)]);
            bool mate = false;
            if (shortcut) {
                if (moves[static_cast<std::size_t>(i)].score >= 50000) {
                    mate = !has_legal_move(nb, ws.move_reserve, ws.move_reserve_capacity, ws.static_pseudo);
                }
            } else {
                mate = is_checkmate(nb, ws.move_reserve, ws.move_reserve_capacity, ws.static_pseudo);
            }

            Proof found;
            if (mate) {
                found.ok = true;
                found.pv.push_back(moves[static_cast<std::size_t>(i)]);
                if (ws.emit_proof) {
                    found.cert = "{\"a\":" + json_quote(move_uci(moves[static_cast<std::size_t>(i)])) + ",\"mate\":true}";
                }
            } else if (depth > 1) {
                Proof all_replies = prove_defender(ws, nb, depth - 1);
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
                for (auto& other : slots) {
                    if (other->current_root.load(std::memory_order_acquire) > best) {
                        other->cancel.store(true, std::memory_order_release);
                    }
                }
            }
        }
        slot.current_root.store(n, std::memory_order_release);
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(worker_count - 1));
    for (int w = 1; w < worker_count; ++w) {
        pool.emplace_back(worker_body, w);
    }
    worker_body(0);
    for (std::thread& t : pool) {
        t.join();
    }

    for (int w = 0; w < worker_count; ++w) {
        s.stats += workers[static_cast<std::size_t>(w)]->stats;
        workers[static_cast<std::size_t>(w)]->stats = Stats{};
        workers[static_cast<std::size_t>(w)]->aborted = false;
    }

    const int best = best_index.load(std::memory_order_acquire);
    if (best < n && results[static_cast<std::size_t>(best)].ok) {
        out = std::move(results[static_cast<std::size_t>(best)]);
        return true;
    }
    return false;
}

RouteResult run_depth_first_route_from(Search& s, const Board& b, int start_depth, int max_depth) {
    RouteResult result;
    start_depth = std::max(1, start_depth);

    // Workers are built once for the whole route so their tables survive
    // across iterative-deepening passes exactly as the sequential table does,
    // and lazily so that positions resolved without ever splitting -- shallow
    // mates and quickly refuted no-mate controls -- pay nothing for threads
    // they never use.
    std::vector<std::unique_ptr<Search>> workers;
    std::vector<std::unique_ptr<WorkerSlot>> slots;
    std::unique_ptr<SharedProofTable> shared_table;
    bool prelude_imported = false;
    const int thread_count = std::max(1, s.threads);
    auto ensure_workers = [&]() {
        if (!workers.empty()) {
            return;
        }
        if (s.shared_tt) {
            shared_table.reset(new SharedProofTable(s.shared_tt_shards, s.tt_reserve, entry_capacity_for_mb(s.memory_mb)));
        }
        workers.reserve(static_cast<std::size_t>(thread_count));
        slots.reserve(static_cast<std::size_t>(thread_count));
        for (int w = 0; w < thread_count; ++w) {
            slots.emplace_back(new WorkerSlot());
            auto ws = std::unique_ptr<Search>(new Search());
            static_cast<SearchConfig&>(*ws) = static_cast<const SearchConfig&>(s);
            ws->cancel = &slots.back()->cancel;
            ws->shared_table = shared_table.get();
            if (ws->shared_table == nullptr) {
                ws->tt.capacity = entry_capacity_for_mb(ws->memory_mb);
                if (ws->tt_reserve > 0) {
                    ws->tt.map.reserve(ws->tt_reserve);
                }
            }
            workers.push_back(std::move(ws));
        }
    };

    for (int depth = start_depth; depth <= max_depth; ++depth) {
        // Advance the aging generation so entries that go untouched during this
        // pass become the first candidates for eviction if the table is full.
        ++s.tt.generation;
        if (shared_table) {
            shared_table->next_generation();
        }
        if (!s.keep_iter_tt) {
            s.tt.clear();
            for (auto& ws : workers) {
                ws->tt.clear();
            }
            if (shared_table) {
                shared_table->clear();
            }
        }
        // Depth 1 is a flat scan for immediate mates; the split would cost more
        // in thread setup than it saves.
        const bool splittable = thread_count > 1 && depth > 1;

        // Parallel cost gate.
        //
        // Thread setup is pure overhead on work that was going to finish in
        // microseconds, but cost is not knowable in advance, and a gate that
        // only looks at completed depths is useless here: search cost grows
        // exponentially with depth, so by the time a shallow depth proves the
        // position expensive, the expensive depth is the one already running.
        //
        // So probe instead of predict. Run the depth sequentially under a node
        // ceiling; if it blows the ceiling the position is expensive by
        // definition, and the depth is re-run split. The probe is not wasted
        // work: exceeding the ceiling is an abort, which by the abort
        // invariant records no verdict but leaves every genuinely completed
        // subtree in the table, and that table is handed to the workers.
        bool escalate = false;
        if (splittable && s.parallel_min_nodes > 0 && s.stats.nodes < s.parallel_min_nodes) {
            s.node_budget = s.parallel_min_nodes;
            s.aborted = false;
            Proof probe = prove_attacker(s, b, depth);
            s.node_budget = 0;
            if (s.aborted) {
                s.aborted = false;
                escalate = true;
            } else {
                result.proof = std::move(probe);
            }
        } else {
            escalate = splittable;
        }

        if (escalate) {
            ensure_workers();
            if (shared_table && !prelude_imported) {
                shared_table->import_from(s.tt);
                prelude_imported = true;
            }
            Proof proof;
            if (run_root_split_depth(s, workers, slots, b, depth, proof)) {
                result.proof = std::move(proof);
            }
        } else if (!splittable) {
            result.proof = prove_attacker(s, b, depth);
        }
        if (result.proof.ok) {
            result.proved_depth = static_cast<int>((result.proof.pv.size() + 1) / 2);
            break;
        }
    }
    return result;
}

RouteResult run_depth_first_route(Search& s, const Board& b, int max_depth) {
    return run_depth_first_route_from(s, b, 1, max_depth);
}

Proof prove_shallow_mate1(Search& s, const Board& b) {
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (b.stm != s.attacker) {
        return {};
    }

    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();
    bool moves_scored = false;
    if (should_order(s, moves.size())) {
        ++s.stats.order_calls;
        s.stats.order_moves += moves.size();
        order_moves(b, moves, s.score_mates, s.score_checks, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
        moves_scored = true;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'A', s.attacker);
            have_hint_key = true;
        }
        return hint_key;
    };
    if (s.proof_hints) {
        const TTKey& proof_key = get_hint_key();
        ++s.stats.proof_hint_probes;
        if (auto hint = s.attacker_proofs.find(proof_key); hint != s.attacker_proofs.end()) {
            if (move_to_front(moves, hint->second)) {
                ++s.stats.proof_hint_hits;
            }
        }
    }

    const bool can_use_ordered_check_shortcut = s.ordered_check_shortcut && moves_scored && s.score_checks && !s.score_mates;
    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        Board nb = make_move(b, amove);
        ++s.stats.immediate_mate_tests;
        bool mate = false;
        if (can_use_ordered_check_shortcut) {
            ++s.stats.ordered_check_shortcut_uses;
            if (amove.score >= 50000) {
                ++s.stats.ordered_check_shortcut_checks;
                mate = !has_legal_move(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
            } else {
                ++s.stats.ordered_check_shortcut_skips;
            }
        } else {
            mate = is_checkmate(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
        }
        if (mate) {
            ++s.stats.immediate_mates;
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs[get_hint_key()] = amove;
            }
            std::string cert;
            if (s.emit_proof) {
                cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"mate\":true}";
            }
            return {true, {amove}, cert};
        }
    }
    return {};
}

Proof prove_shallow_mate2(Search& s, const Board& b) {
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (b.stm != s.attacker) {
        return {};
    }

    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();
    if (should_order(s, moves.size())) {
        ++s.stats.order_calls;
        s.stats.order_moves += moves.size();
        order_moves(b, moves, s.score_mates, s.score_checks, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
    }

    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        Board defender_board = make_move(b, amove);
        ++s.stats.nodes;
        ++s.stats.defender_nodes;
        auto replies = legal_moves(defender_board, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
        ++s.stats.defender_move_lists;
        s.stats.defender_moves += replies.size();
        if (replies.empty()) {
            continue;
        }
        if (should_order(s, replies.size())) {
            ++s.stats.order_calls;
            s.stats.order_moves += replies.size();
            order_moves(defender_board, replies, s.score_mates, s.score_checks, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
        }

        std::vector<Move> representative;
        std::vector<std::string> branch_certs;
        if (s.emit_proof) {
            branch_certs.reserve(replies.size());
        }
        bool all_replies_mate = true;
        for (const Move& dmove : replies) {
            ++s.stats.defender_replies_tried;
            Board attacker_board = make_move(defender_board, dmove);
            Proof child = prove_shallow_mate1(s, attacker_board);
            if (!child.ok) {
                ++s.stats.defender_refutations;
                all_replies_mate = false;
                break;
            }
            std::vector<Move> candidate;
            candidate.push_back(dmove);
            candidate.insert(candidate.end(), child.pv.begin(), child.pv.end());
            if (candidate.size() > representative.size()) {
                representative = std::move(candidate);
            }
            if (s.emit_proof) {
                branch_certs.push_back("{\"r\":" + json_quote(move_uci(dmove)) + ",\"p\":" + child.cert + "}");
            }
        }
        if (all_replies_mate) {
            std::vector<Move> pv{amove};
            pv.insert(pv.end(), representative.begin(), representative.end());
            std::string cert;
            if (s.emit_proof) {
                cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"d\":[";
                for (std::size_t i = 0; i < branch_certs.size(); ++i) {
                    if (i) cert.push_back(',');
                    cert += branch_certs[i];
                }
                cert += "]}";
            }
            return {true, pv, cert};
        }
    }
    return {};
}

RouteResult run_shallow_fast_route(Search& s, const Board& b, int max_depth) {
    ++s.stats.shallow_fast_attempts;
    RouteResult result;
    if (max_depth >= 1) {
        result.proof = prove_shallow_mate1(s, b);
        if (result.proof.ok) {
            ++s.stats.shallow_fast_hits;
            result.proved_depth = 1;
            return result;
        }
    }
    if (max_depth >= 2) {
        result.proof = prove_shallow_mate2(s, b);
        if (result.proof.ok) {
            ++s.stats.shallow_fast_hits;
            result.proved_depth = static_cast<int>((result.proof.pv.size() + 1) / 2);
            return result;
        }
    }
    if (max_depth > 2) {
        ++s.stats.shallow_fast_fallbacks;
        return run_depth_first_route_from(s, b, 3, max_depth);
    }
    return {};
}

RouteResult run_route(Search& s, const Board& b, int max_depth) {
    switch (s.route) {
        case RouteKind::DepthFirst:
            return run_depth_first_route(s, b, max_depth);
        case RouteKind::ShallowFast:
            return run_shallow_fast_route(s, b, max_depth);
    }
    return {};
}

bool route_result_is_acceptable(const RouteResult& result, int max_depth) {
    if (!result.proof.ok || result.proof.pv.empty()) {
        return false;
    }
    const int pv_depth = static_cast<int>((result.proof.pv.size() + 1) / 2);
    return result.proved_depth == pv_depth && result.proved_depth > 0 && result.proved_depth <= max_depth;
}

// Perft: count leaf nodes of the legal move tree to a fixed depth.
//
// This is the engine's self-contained move-generation gate. Directmate proofs
// are only as trustworthy as legality, and perft against published reference
// counts exercises castling rights, en-passant capture and expiry, promotion,
// and pinned-piece legality far more thoroughly than the mate suites do -- a
// movegen bug usually shows up as a wrong perft number long before it shows up
// as a wrong mate.
std::uint64_t perft(const Board& b, int depth) {
    if (depth <= 0) {
        return 1;
    }
    auto moves = legal_moves(b);
    if (depth == 1) {
        return moves.size();
    }
    std::uint64_t total = 0;
    for (const Move& m : moves) {
        total += perft(make_move(b, m), depth - 1);
    }
    return total;
}

// Per-root-move perft breakdown: the standard tool for bisecting a movegen or
// make/unmake discrepancy against a reference implementation.
void perft_divide_line(const std::string& raw, int depth) {
    std::string line = trim(raw);
    if (line.empty()) {
        line = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; perft error input;\n";
        return;
    }
    const Board b = *parsed;
    auto moves = legal_moves(b);
    std::vector<std::pair<std::string, std::uint64_t>> rows;
    rows.reserve(moves.size());
    std::uint64_t total = 0;
    for (const Move& m : moves) {
        const std::uint64_t n = perft(make_move(b, m), depth - 1);
        rows.emplace_back(move_uci(m), n);
        total += n;
    }
    std::sort(rows.begin(), rows.end());
    for (const auto& row : rows) {
        std::cout << row.first << ' ' << row.second << '\n';
    }
    std::cout << "total " << total << '\n';
}

void perft_line(const std::string& raw, int depth) {
    std::string line = trim(raw);
    if (line.empty()) {
        line = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; perft error input;\n";
        return;
    }
    const Board b = *parsed;
    for (int d = 1; d <= depth; ++d) {
        const auto start = std::chrono::steady_clock::now();
        const std::uint64_t nodes = perft(b, d);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << fen4(b) << "; perft " << d << "; nodes " << nodes << "; acs " << seconds << ";\n";
    }
}

int infer_mate_depth(const std::string& line) {
    auto pos = line.find('#');
    if (pos == std::string::npos) {
        return 0;
    }
    int value = 0;
    for (++pos; pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])); ++pos) {
        value = value * 10 + (line[pos] - '0');
    }
    return value;
}

std::string pv_uci(const std::vector<Move>& pv) {
    std::ostringstream out;
    for (std::size_t i = 0; i < pv.size(); ++i) {
        if (i) out << ' ';
        out << move_uci(pv[i]);
    }
    return out.str();
}

void emit_profile_line(const Board& b, const Search& s, int requested_depth, int proved_depth, double seconds) {
    const Stats& st = s.stats;
    std::cerr << "% e_profile {"
              << "\"fen\":" << json_quote(fen4(b))
              << ",\"route\":" << json_quote(route_name(s.route))
              << ",\"requested_depth\":" << requested_depth
              << ",\"proved_depth\":" << proved_depth
              << ",\"seconds\":" << seconds
              << ",\"nodes\":" << st.nodes
              << ",\"attacker_nodes\":" << st.attacker_nodes
              << ",\"defender_nodes\":" << st.defender_nodes
              << ",\"tt_probes\":" << st.tt_probes
              << ",\"tt_hits\":" << st.tt_hits
              << ",\"tt_stores\":" << st.tt_stores
              << ",\"tt_size\":" << (s.shared_table != nullptr ? s.shared_table->size() : s.tt.size())
              << ",\"tt_capacity\":" << s.tt.capacity
              << ",\"tt_evictions\":" << (s.shared_table != nullptr ? s.shared_table->evictions() : s.tt.evictions)
              << ",\"memory_mb\":" << s.memory_mb
              << ",\"exact_tt_proof_hits\":" << st.exact_tt_proof_hits
              << ",\"exact_tt_disproof_hits\":" << st.exact_tt_disproof_hits
              << ",\"exact_tt_proof_stores\":" << st.exact_tt_proof_stores
              << ",\"exact_tt_disproof_stores\":" << st.exact_tt_disproof_stores
              << ",\"shallow_fast_attempts\":" << st.shallow_fast_attempts
              << ",\"shallow_fast_hits\":" << st.shallow_fast_hits
              << ",\"shallow_fast_fallbacks\":" << st.shallow_fast_fallbacks
              << ",\"bound_tt_probes\":" << st.bound_tt_probes
              << ",\"bound_tt_hits\":" << st.bound_tt_hits
              << ",\"bound_tt_ok_hits\":" << st.bound_tt_ok_hits
              << ",\"bound_tt_fail_hits\":" << st.bound_tt_fail_hits
              << ",\"bound_tt_stores\":" << st.bound_tt_stores
              << ",\"bound_tt_size\":" << s.bound_tt.size()
              << ",\"attacker_move_lists\":" << st.attacker_move_lists
              << ",\"attacker_moves\":" << st.attacker_moves
              << ",\"attacker_candidates\":" << st.attacker_candidates
              << ",\"defender_move_lists\":" << st.defender_move_lists
              << ",\"defender_moves\":" << st.defender_moves
              << ",\"defender_replies_tried\":" << st.defender_replies_tried
              << ",\"defender_pseudo_moves\":" << st.defender_pseudo_moves
              << ",\"defender_lazy_skipped\":" << st.defender_lazy_skipped
              << ",\"lazy_defender\":" << (s.lazy_defender ? "true" : "false")
              << ",\"order_calls\":" << st.order_calls
              << ",\"order_moves\":" << st.order_moves
              << ",\"immediate_mate_tests\":" << st.immediate_mate_tests
              << ",\"ordered_check_shortcut_uses\":" << st.ordered_check_shortcut_uses
              << ",\"ordered_check_shortcut_checks\":" << st.ordered_check_shortcut_checks
              << ",\"ordered_check_shortcut_skips\":" << st.ordered_check_shortcut_skips
              << ",\"immediate_mates\":" << st.immediate_mates
              << ",\"refutation_hint_probes\":" << st.refutation_hint_probes
              << ",\"refutation_hint_hits\":" << st.refutation_hint_hits
              << ",\"refutation_hint_stores\":" << st.refutation_hint_stores
              << ",\"proof_hint_probes\":" << st.proof_hint_probes
              << ",\"proof_hint_hits\":" << st.proof_hint_hits
              << ",\"proof_hint_stores\":" << st.proof_hint_stores
              << ",\"route_rejections\":" << st.route_rejections
              << ",\"defender_refutations\":" << st.defender_refutations
              << ",\"move_reserve\":" << (s.move_reserve ? "true" : "false")
              << ",\"move_reserve_capacity\":" << s.move_reserve_capacity
              << ",\"inplace_order\":" << (s.inplace_order ? "true" : "false")
              << ",\"bucket_order\":" << (s.bucket_order ? "true" : "false")
              << ",\"static_pseudo\":" << (s.static_pseudo ? "true" : "false")
              << ",\"order_min_size\":" << s.order_min_size
              << ",\"refutation_hints\":" << (s.refutation_hints ? "true" : "false")
              << ",\"proof_hints\":" << (s.proof_hints ? "true" : "false")
              << ",\"keep_iter_tt\":" << (s.keep_iter_tt ? "true" : "false")
              << ",\"bound_tt\":" << (s.bound_tt_enabled ? "true" : "false")
              << ",\"bound_tt_failures\":" << (s.bound_tt_failures ? "true" : "false")
              << ",\"ordered_check_shortcut\":" << (s.ordered_check_shortcut ? "true" : "false")
              << "}\n";
}

void list_legal_line(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; legal_count 0; error input;\n";
        return;
    }
    Board b = *parsed;
    auto moves = legal_moves(b);
    std::vector<std::string> uci;
    uci.reserve(moves.size());
    for (const Move& move : moves) {
        uci.push_back(move_uci(move));
    }
    std::sort(uci.begin(), uci.end());
    std::cout << fen4(b) << "; legal_count " << uci.size() << "; legal";
    for (const std::string& move : uci) {
        std::cout << ' ' << move;
    }
    std::cout << ";\n";
}

void solve_line(const std::string& raw, int requested_depth, const SearchConfig& config) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; acn 0; acs 0; error input;\n";
        return;
    }
    Board b = *parsed;
    int max_depth = requested_depth > 0 ? requested_depth : infer_mate_depth(line);
    if (max_depth <= 0) {
        max_depth = 1;
    }

    Search s;
    static_cast<SearchConfig&>(s) = config;
    s.attacker = b.stm;
    s.order_min_size = std::max<std::size_t>(2, config.order_min_size);
    s.tt.capacity = entry_capacity_for_mb(s.memory_mb);
    if (s.tt_reserve > 0) {
        s.tt.map.reserve(s.tt_reserve);
    }
    auto start = std::chrono::steady_clock::now();

    RouteResult route_result = run_route(s, b, max_depth);
    const Proof& proof = route_result.proof;
    const bool accepted = route_result_is_acceptable(route_result, max_depth);
    if (!accepted && route_result.proof.ok) {
        ++s.stats.route_rejections;
    }
    int proved_depth = accepted ? route_result.proved_depth : 0;

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << fen4(b) << "; acn " << s.stats.nodes << "; acs " << seconds;
    if (accepted) {
        std::cout << "; bm " << move_uci(proof.pv.front())
                  << "; dm " << proved_depth
                  << "; pv " << pv_uci(proof.pv);
        if (s.emit_proof && !proof.cert.empty()) {
            std::cout << "; proof " << proof.cert;
        }
    }
    std::cout << ";\n";
    if (s.profile) {
        emit_profile_line(b, s, requested_depth, proved_depth, seconds);
    }
}

} // namespace

int main(int argc, char** argv) {
    SearchConfig config;
    int requested_depth = 0;
    int perft_depth = 0;
    bool perft_divide = false;
    bool read_stdin = false;
    bool list_legal = false;

    auto parse_size = [&](const char* text, std::size_t& out) {
        char* end = nullptr;
        unsigned long value = std::strtoul(text, &end, 10);
        if (end != text) {
            out = static_cast<std::size_t>(value);
            return true;
        }
        return false;
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-z" && i + 1 < argc) {
            requested_depth = std::atoi(argv[++i]);
        } else if (arg == "--route" && i + 1 < argc) {
            std::string route_arg = argv[++i];
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
        } else if (arg == "--perft" && i + 1 < argc) {
            perft_depth = std::atoi(argv[++i]);
        } else if (arg == "--perft-divide" && i + 1 < argc) {
            perft_depth = std::atoi(argv[++i]);
            perft_divide = true;
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
        } else if (arg == "--tt-reserve" && i + 1 < argc) {
            parse_size(argv[++i], config.tt_reserve);
        } else if (arg == "--move-reserve") {
            config.move_reserve = true;
        } else if (arg == "--no-move-reserve") {
            config.move_reserve = false;
        } else if (arg == "--move-reserve-cap" && i + 1 < argc) {
            std::size_t value = 0;
            if (parse_size(argv[++i], value) && value > 0) {
                config.move_reserve = true;
                config.move_reserve_capacity = value;
            }
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
        } else if (arg == "--bound-tt") {
            config.bound_tt_enabled = true;
        } else if (arg == "--exact-tt-only") {
            config.bound_tt_enabled = false;
        } else if (arg == "--bound-tt-failures") {
            config.bound_tt_failures = true;
        } else if (arg == "--bound-tt-ok-only") {
            config.bound_tt_failures = false;
        } else if (arg == "--ordered-check-shortcut") {
            config.ordered_check_shortcut = true;
        } else if (arg == "--no-ordered-check-shortcut") {
            config.ordered_check_shortcut = false;
        } else if (arg == "--static-pseudo") {
            config.static_pseudo = true;
        } else if (arg == "--vector-pseudo") {
            config.static_pseudo = false;
        } else if (arg == "--order-min-size" && i + 1 < argc) {
            std::size_t value = 0;
            if (parse_size(argv[++i], value)) {
                config.order_min_size = std::max<std::size_t>(2, value);
            }
        } else if (arg == "--threads" && i + 1 < argc) {
            std::string value = argv[++i];
            if (value == "auto") {
                unsigned hw = std::thread::hardware_concurrency();
                config.threads = hw > 0 ? static_cast<int>(hw) : 1;
            } else {
                std::size_t parsed = 0;
                if (parse_size(value.c_str(), parsed) && parsed > 0) {
                    config.threads = static_cast<int>(parsed);
                }
            }
        } else if (arg == "--single-thread") {
            config.threads = 1;
        } else if (arg == "--parallel-min-nodes" && i + 1 < argc) {
            std::size_t value = 0;
            if (parse_size(argv[++i], value)) {
                config.parallel_min_nodes = static_cast<std::uint64_t>(value);
            }
        } else if (arg == "--no-parallel-gate") {
            config.parallel_min_nodes = 0;
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
        } else if (arg == "--shared-tt-shards" && i + 1 < argc) {
            std::size_t value = 0;
            if (parse_size(argv[++i], value) && value > 0) {
                config.shared_tt_shards = value;
            }
        } else if (arg == "--order-all") {
            config.order_min_size = 2;
        } else if (arg == "-M" && i + 1 < argc) {
            std::size_t value = 0;
            if (parse_size(argv[++i], value)) {
                config.memory_mb = value; // 0 means unbounded
            }
        } else if ((arg == "-C" || arg == "-R" || arg == "-K" || arg == "-P" || arg == "-X" || arg == "-I" || arg == "-n" || arg == "-N") && i + 1 < argc) {
            ++i; // accepted for CLI compatibility; not yet semantically implemented
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
