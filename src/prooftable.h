// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// prooftable.h -- Centralised exact proof-table probe and store.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_PROOFTABLE_H_INCLUDED
#define MATEPROVER_PROOFTABLE_H_INCLUDED

namespace mateprover {

// Probe for a verdict at `depth`. The key excludes depth; the entry's bounds
// decide whether what is stored answers this particular question.
bool probe_exact_proof_table(Search& s, const TTKey& key, int depth, Proof& out) {
    ++s.stats.tt_probes;
    TTEntry entry;
    if (s.shared_table != nullptr) {
        if (!s.shared_table->probe(key, entry)) {
            return false;
        }
    } else if (!s.tt.probe(key, entry)) {
        return false;
    }
    // Refuted is depth-independent by construction, so it answers any query
    // regardless of the depth asked for. It is checked FIRST: it is the
    // strongest statement in the lattice and no depth bound can improve on it.
    if (entry.refuted) {
        ++s.stats.tt_hits;
        ++s.stats.exact_tt_disproof_hits;
        out = {};
        out.refuted = true;
        return true;
    }
    // A mate within min_proved is a mate within any larger bound.
    if (depth >= entry.min_proved) {
        ++s.stats.tt_hits;
        ++s.stats.exact_tt_proof_hits;
        out = {true, std::move(entry.pv), std::move(entry.cert)};
        return true;
    }
    // No mate within max_disproved means none within any smaller bound.
    if (depth <= entry.max_disproved) {
        ++s.stats.tt_hits;
        ++s.stats.exact_tt_disproof_hits;
        out = {};
        return true;
    }
    // The position is known, but not at a bound that settles this depth.
    return false;
}

void store_exact_proof_table(Search& s, const TTKey& key, int depth, const Proof& proof) {
    // An aborted subtree produced no verdict. Storing its empty result would
    // cache a false disproof, so nothing is written once the search unwinds.
    if (s.aborted) {
        return;
    }
    ++s.stats.tt_stores;
    if (proof.ok) {
        ++s.stats.exact_tt_proof_stores;
    } else {
        ++s.stats.exact_tt_disproof_stores;
    }
    if (s.shared_table != nullptr) {
        s.shared_table->merge(key, depth, proof.ok, proof.pv, proof.cert, proof.refuted);
    } else {
        s.tt.merge(key, depth, proof.ok, proof.pv, proof.cert, proof.refuted);
    }
}



} // namespace mateprover

#endif // MATEPROVER_PROOFTABLE_H_INCLUDED
