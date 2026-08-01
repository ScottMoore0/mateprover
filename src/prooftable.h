// prooftable.h -- Centralised exact proof-table probe and store.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by echest.cpp; see docs/E_CHEST_ARCHITECTURE.md.

#ifndef ECHEST_PROOFTABLE_H_INCLUDED
#define ECHEST_PROOFTABLE_H_INCLUDED

namespace echest {

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

} // namespace echest

#endif // ECHEST_PROOFTABLE_H_INCLUDED
