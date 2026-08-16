// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// table.h -- Bounded and shared proof tables, and the memory-budget conversion.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_TABLE_H_INCLUDED
#define MATEPROVER_TABLE_H_INCLUDED

namespace mateprover {

constexpr std::size_t EST_BYTES_PER_ENTRY = 192;

// The same budget, for the FLAT path, where an entry costs a great deal less.
//
// EST_BYTES_PER_ENTRY was sized for an `unordered_map` entry plus its separately
// allocated node. A `FlatSlot` is 56 bytes with nothing on the heap, so dividing
// a budget by 192 hands the flat table under a third of what was asked for: a
// `-M 8192` request became 44.7M slots occupying 2.51 GB. The engine was quietly
// running with a table two-thirds smaller than requested, and the only symptom
// was being slower than it needed to be.
//
// 64 rather than 56, because the side map holding lines for proved positions is
// real memory too and is not free. With `line_cap` at a thirty-second of the
// slot count, that map contributes roughly eight bytes per slot amortised; the
// rest is headroom, since this budget has always been an estimate rather than a
// ceiling (see the note on `memory_mb`).
//
// A constant that was right for a structure that no longer exists. That is the
// third one this session -- an eviction counter reading the wrong table, a
// fitness function counting the wrong outcome, and now a budget divided by the
// wrong entry size.
constexpr std::size_t EST_BYTES_PER_FLAT_ENTRY = 64;

// Convert a capacity expressed in hash-map entries into flat slots for the same
// memory. Callers size everything from entry_capacity_for_mb, so scaling here
// keeps one source of truth for the budget.
inline std::size_t flat_entries_for(std::size_t map_entries) {
    return map_entries * (EST_BYTES_PER_ENTRY / EST_BYTES_PER_FLAT_ENTRY);
}

inline std::size_t entry_capacity_for_mb(std::size_t megabytes) {
    if (megabytes == 0) {
        return 0; // explicit "unbounded"
    }
    return (megabytes * 1024u * 1024u) / EST_BYTES_PER_ENTRY;
}


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
// ONE SLOT OF THE FLAT TABLE.
//
// Fixed size and no owned storage, which is the whole point: an entry costs one
// contiguous 56-byte object, and replacing one is a write rather than an
// allocation, a free and a rehash. The principal variation and the certificate
// do NOT live here -- see BoundedTable::lines_ -- because they are variable
// length and only 7% of stores carry them.
//
// `gen == 0` marks a slot never written. Generations start at 1.
struct FlatSlot {
    TTKey key{};
    int max_disproved = TTEntry::NO_DISPROOF;
    int min_proved = TTEntry::NO_PROOF;
    std::uint32_t gen = 0;
    bool refuted = false;
};

struct BoundedTable {
    std::unordered_map<TTKey, TTEntry, TTKeyHash> map;
    // ---- The flat path.
    //
    // 121 measured that iterating this map to discard entries was ~42% of a deep
    // search even after evict() was made four times rarer. A direct-mapped array
    // removes the scan entirely rather than making it less frequent: a
    // collision is resolved by overwriting the slot, so eviction is a store and
    // costs nothing at all.
    //
    // DIRECT-MAPPED, with no probe chain. That is the standard chess-engine
    // design and it is what keeps the cost O(1) in the worst case as well as the
    // average. The price is that two positions hashing to one slot evict each
    // other repeatedly; the safety argument is unchanged and absolute -- the
    // table is a memo of verdicts that are pure functions of an exact key, so a
    // missing or overwritten entry costs time and can never change an answer.
    std::vector<FlatSlot> slots;
    // pv and cert for keys that have a PROOF, which the flat slots cannot hold.
    // Disproofs -- 93% of stores, measured -- carry neither, so the hot path
    // never touches this.
    std::unordered_map<TTKey, std::pair<std::vector<Move>, std::string>, TTKeyHash> lines_;
    std::size_t mask = 0;
    std::size_t used = 0;
    std::size_t line_cap = 0;
    bool flat = false;

    // Size the array to a power of two so the index is a mask rather than a
    // modulo, and reserve the line map to a fraction of it.
    void enable_flat(std::size_t entries) {
        std::size_t n = 1;
        while (n < entries) {
            n <<= 1;
        }
        slots.assign(n, FlatSlot{});
        mask = n - 1;
        used = 0;
        // A thirty-second, not an eighth. Proofs are 7% of stores (measured),
        // so this is still generous, and every byte here is a byte not spent on
        // slots -- which hold the verdicts the search actually reads.
        line_cap = std::max<std::size_t>(1024, n / 32);
        lines_.clear();
        flat = true;
    }

    std::size_t index_of(const TTKey& key) const {
        return TTKeyHash{}(key) & mask;
    }
    std::size_t capacity = 0; // 0 means unbounded
    // How much to shed per eviction, as a fraction 1/shed_divisor of capacity.
    //
    // THIS IS NOT A MEMORY KNOB, IT IS THE COST OF THE SCAN. evict() walks the
    // whole shard -- an unordered_map, so a pointer chase over nodes scattered
    // in allocation order -- and it does that once per call however little it
    // sheds. Shedding an eighth meant eight times as many full scans as
    // shedding a half.
    //
    // Measured at depth 8 on the capture quota, 24 threads, 8 GB: shedding an
    // eighth took 336.7 s, shedding a half took 172.9 s. Same search, same node
    // count to within 2%. Half the wall clock of a deep run was iteration over
    // a hash map, discarding entries.
    std::size_t shed_divisor = 2;
    std::uint32_t generation = 1;
    std::uint64_t evictions = 0;

    // Pull this key's slot toward the cache before anyone needs it.
    //
    // The flat table is gigabytes and the index is a hash, so every probe is a
    // cache miss and, on 4 KB pages, usually a TLB miss too. Measured: the same
    // search runs at 629K nodes/s against an 8 MB table that fits in cache and
    // 357K against the 8 GB working table -- so 1.76x of per-node throughput is
    // memory latency and nothing else. That is the ceiling for this and for
    // large pages together.
    //
    // A hint only. It cannot change what is found, only when it arrives, so no
    // verdict, depth or certificate depends on it.
    void prefetch(const TTKey& key) const {
        if (!flat || slots.empty()) {
            return;
        }
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(&slots[index_of(key)], 0, 1);
#else
        (void)key;
#endif
    }

    bool probe(const TTKey& key, TTEntry& out) {
        if (flat) {
            FlatSlot& slot = slots[index_of(key)];
            if (slot.gen == 0 || !(slot.key == key)) {
                return false;
            }
            slot.gen = generation;      // still earning its space
            out = TTEntry{};
            out.max_disproved = slot.max_disproved;
            out.min_proved = slot.min_proved;
            out.refuted = slot.refuted;
            out.gen = slot.gen;
            if (slot.min_proved != TTEntry::NO_PROOF) {
                auto line = lines_.find(key);
                if (line != lines_.end()) {
                    out.pv = line->second.first;
                    out.cert = line->second.second;
                }
            }
            return true;
        }
        auto it = map.find(key);
        if (it == map.end()) {
            return false;
        }
        it->second.gen = generation; // this entry is still earning its space
        out = it->second;
        return true;
    }

    void store(const TTKey& key, TTEntry entry) {
        if (flat) {
            merge(key, entry.min_proved != TTEntry::NO_PROOF ? entry.min_proved : entry.max_disproved,
                  entry.min_proved != TTEntry::NO_PROOF, entry.pv, entry.cert, entry.refuted);
            return;
        }
        entry.gen = generation;
        map[key] = std::move(entry);
        if (capacity != 0 && map.size() > capacity) {
            evict();
        }
    }


    // Read-modify-write of the depth bounds for one key. Merging rather than
    // overwriting is what lets a single entry accumulate both a disproof bound
    // and a proof bound as the search visits the position at several depths.
    void merge(const TTKey& key, int depth, bool proved,
               const std::vector<Move>& pv, const std::string& cert,
               bool refuted = false) {
        if (flat) {
            FlatSlot& slot = slots[index_of(key)];
            if (slot.gen == 0) {
                ++used;
            } else if (!(slot.key == key)) {
                // A different position owned this slot. Overwriting it IS the
                // eviction, and it is a store rather than a scan.
                ++evictions;
                slot = FlatSlot{};
            }
            slot.key = key;
            slot.gen = generation;
            if (refuted) {
                slot.refuted = true;    // absorbing: nothing downgrades it
            }
            if (proved) {
                if (depth < slot.min_proved) {
                    slot.min_proved = depth;
                    if (!pv.empty() || !cert.empty()) {
                        // Bounded, and clearing it can only SHORTEN a reported
                        // line -- never change a verdict, which lives entirely
                        // in the slot above.
                        if (lines_.size() >= line_cap) {
                            lines_.clear();
                        }
                        lines_[key] = {pv, cert};
                    }
                }
            } else if (depth > slot.max_disproved) {
                slot.max_disproved = depth;
            }
            return;
        }
        TTEntry& entry = map[key];
        entry.absorb(depth, proved, pv, cert, refuted);
        entry.gen = generation;
        if (capacity != 0 && map.size() > capacity) {
            evict();
        }
    }

    void evict() {
        if (flat) {
            return;     // replacement happened in place; there is nothing to scan
        }
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
        const std::size_t low_water = capacity - capacity / std::max<std::size_t>(1, shed_divisor);
        if (map.size() <= low_water) {
            return;
        }
        // DEPTH DECIDES, not map order.
        //
        // The old second pass erased whatever the iterator reached first, which
        // is hash order -- so a verdict that cost a hundred million nodes was
        // exactly as likely to be dropped as one that cost fifty. Every entry is
        // equally correct to discard (the safety argument above is untouched by
        // this: eviction trades time for memory and never correctness) but they
        // are wildly unequal in what they cost to recompute, and that cost grows
        // with the depth bound the entry carries.
        //
        // A histogram rather than a sort: depths are small integers, so one
        // O(n) pass finds the cut and a second applies it, against O(n log n)
        // and an allocation for the general form. Amortised over the capacity/8
        // stores before the next eviction, this is a few operations per store.
        constexpr int kMaxRank = 96;
        std::array<std::size_t, kMaxRank + 1> histogram{};
        for (const auto& kv : map) {
            ++histogram[static_cast<std::size_t>(entry_rank(kv.second))];
        }
        // Walk up from the cheapest until enough have been marked for removal.
        const std::size_t excess = map.size() - low_water;
        std::size_t counted = 0;
        int cut = 0;
        for (; cut <= kMaxRank; ++cut) {
            counted += histogram[static_cast<std::size_t>(cut)];
            if (counted >= excess) {
                break;
            }
        }
        // Entries strictly below the cut always go; entries AT the cut go until
        // the quota is met, so the pass sheds what was asked for and no more.
        std::size_t at_cut_to_drop = (counted > excess) ? histogram[static_cast<std::size_t>(cut)] - (counted - excess)
                                                        : histogram[static_cast<std::size_t>(cut)];
        for (auto it = map.begin(); it != map.end() && map.size() > low_water;) {
            const int rank = entry_rank(it->second);
            bool drop = rank < cut;
            if (!drop && rank == cut && at_cut_to_drop > 0) {
                drop = true;
                --at_cut_to_drop;
            }
            if (drop) {
                it = map.erase(it);
                ++evictions;
            } else {
                ++it;
            }
        }
    }

    // How expensive this entry would be to recompute, as a small integer.
    //
    // A refuted entry ranks top and is shed last: `refuted` is depth
    // INDEPENDENT, so it answers every future query at every depth, and no
    // amount of later searching can produce a stronger statement about that
    // position. Everything else ranks by its depth bound, since the work behind
    // a verdict grows with the depth it was established at.
    static int entry_rank(const TTEntry& e) {
        if (e.refuted) {
            return 96;
        }
        int rank = 0;
        if (e.min_proved != TTEntry::NO_PROOF) {
            rank = std::max(rank, e.min_proved);
        }
        if (e.max_disproved != TTEntry::NO_DISPROOF) {
            rank = std::max(rank, e.max_disproved);
        }
        return std::min(95, std::max(0, rank));
    }

    void clear() {
        map.clear();
        if (flat) {
            slots.assign(slots.size(), FlatSlot{});
            lines_.clear();
            used = 0;
        }
    }

    std::size_t size() const {
        return flat ? used : map.size();
    }
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
// Run `body(i)` for i in [0, n) across up to `hardware_concurrency` threads.
//
// Only ever used for FREEING, which is why it can be this blunt: there is no
// ordering, no result, no verdict and nothing to cancel, so a refused thread
// costs time and nothing else. Deallocation is the one part of this program
// where parallelism is embarrassing rather than speculative.
template <typename Body>
inline void parallel_for_teardown(std::size_t n, Body body) {
    const unsigned hw = std::thread::hardware_concurrency();
    const std::size_t lanes = std::min<std::size_t>(n, hw == 0 ? 1u : hw);
    if (lanes <= 1 || n <= 1) {
        for (std::size_t i = 0; i < n; ++i) {
            body(i);
        }
        return;
    }
    std::atomic<std::size_t> next{0};
    auto worker = [&] {
        for (;;) {
            const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= n) {
                return;
            }
            body(i);
        }
    };
    std::vector<std::thread> pool;
    pool.reserve(lanes - 1);
    for (std::size_t i = 1; i < lanes; ++i) {
        try {
            pool.emplace_back(worker);
        } catch (const std::system_error&) {
            break;      // this thread finishes the rest; only speed is lost
        }
    }
    worker();
    for (std::thread& t : pool) {
        t.join();
    }
}

class SharedProofTable {
public:
    // FREE THE SHARDS IN PARALLEL.
    //
    // Destroying this table is the single largest non-search cost the engine
    // has, and it is paid AFTER the answer is known. At 4 GB on a depth-7
    // capture quota it was 2.65 s of a 6.01 s run -- 44% of the wall clock
    // spent handing memory back, inside the reported `acs`, with the verdict
    // already in hand. It scales with the table and not with the search: 0.41 s
    // at 256 MB, 0.87 s at 1 GB, 2.65 s at 4 GB, on 23.1M, 21.5M and 21.1M
    // nodes respectively.
    //
    // Twenty million entries in 256 independent hash maps is twenty million
    // individual frees, and the shards share nothing, so the work divides
    // perfectly. This is the one part of the program where parallelism is
    // embarrassing rather than speculative: no ordering, no cutoffs, no
    // verdicts, just deallocation.
    //
    // A refused thread is not an error here -- the remaining shards are freed
    // on this thread, exactly as before, and the only cost is time.
    ~SharedProofTable() {
        parallel_for_teardown(shards_.size(), [this](std::size_t i) { shards_[i].reset(); });
    }

    SharedProofTable(std::size_t shard_count, std::size_t reserve_total, std::size_t capacity_total,
                     std::size_t shed_divisor = 2, bool flat = false) {
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
            shards_.back()->table.shed_divisor = shed_divisor;
            if (flat && capacity_total != 0) {
                shards_.back()->table.enable_flat(
                    std::max<std::size_t>(1024, flat_entries_for(capacity_total) / shards));
            }
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

    // The read-modify-write happens under the shard lock, so two workers
    // folding different depth bounds into the same position cannot lose one.
    void merge(const TTKey& key, int depth, bool proved,
               const std::vector<Move>& pv, const std::string& cert,
               bool refuted = false) {
        Shard& shard = shard_for(key);
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.table.merge(key, depth, proved, pv, cert, refuted);
    }

    // Adopt entries computed before the table existed, so the sequential
    // prelude of the cost gate is carried forward rather than redone.
    void import_from(const BoundedTable& src) {
        // Only the hash-map path is ever imported from: `import_from` carries the
        // enclosing search's private table into the shared one, and that private
        // table is only populated on the sequential prelude, which never runs
        // flat. Stated rather than assumed, because a silent no-op here would
        // lose a warm table and only show up as a slower search.
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
} // namespace mateprover

#endif // MATEPROVER_TABLE_H_INCLUDED
