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

    // Read-modify-write of the depth bounds for one key. Merging rather than
    // overwriting is what lets a single entry accumulate both a disproof bound
    // and a proof bound as the search visits the position at several depths.
    void merge(const TTKey& key, int depth, bool proved,
               const std::vector<Move>& pv, const std::string& cert,
               bool refuted = false) {
        TTEntry& entry = map[key];
        entry.absorb(depth, proved, pv, cert, refuted);
        entry.gen = generation;
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
    }

    std::size_t size() const {
        return map.size();
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

} // namespace mateprover

#endif // MATEPROVER_TABLE_H_INCLUDED
