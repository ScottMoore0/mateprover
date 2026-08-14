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
    // The off switch exists for one test: that verdicts and reported depths are
    // identical with the table on and off, over the whole corpus. The table is
    // keyed by position with no depth in the key, so its correctness rests
    // entirely on two properties of the search -- a disproof is a bound over
    // every smaller depth, and a proof depth is minimal. Nothing else in the
    // suite can detect a violation of either; it would surface as a non-minimal
    // mate on some position nobody happened to test.
    if (!s.exact_tt) {
        return false;
    }
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
        // Hand back the FULL strength of the stored disproof, not the depth that
        // happened to be asked for. This is what lets a caller skip levels.
        out.fail_depth = entry.max_disproved;
        return true;
    }
    // The position is known, but not at a bound that settles this depth. This
    // is precisely the re-entry a PER-MOVE disproof array would exploit: the
    // node must be searched again at a greater depth, and without per-move
    // bounds every one of its moves is re-executed from scratch. Counting it
    // says whether that array can pay before it is built.
    ++s.stats.tt_known_weaker;
    return false;
}

void store_exact_proof_table(Search& s, const TTKey& key, int depth, const Proof& proof) {
    if (!s.exact_tt) {
        return;
    }
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
    // Store the STRONGEST bound proven, not the depth that was asked for. For a
    // proof `depth` is the length of the mate; for a disproof it is a bound, and
    // the search often proves more than it was asked because a child's disproof
    // propagates upward. Recording only the request throws that away.
    const int bound = proof.ok ? depth : std::max(depth, proof.fail_depth);
    // The line and the certificate are the largest thing an entry carries and
    // the only part of it the SEARCH never reads: probes consume the bounds and
    // the refuted flag, while the line is wanted once, at the end, for the
    // answer. Suppressing them trades a truncated principal variation for more
    // verdicts per megabyte -- worth measuring, because 110 found node counts
    // acutely sensitive to how many verdicts fit.
    static const std::vector<Move> kNoLine;
    static const std::string kNoCert;
    const std::vector<Move>& line = s.tt_lines ? proof.pv : kNoLine;
    const std::string& cert = s.tt_lines ? proof.cert : kNoCert;
    if (s.shared_table != nullptr) {
        s.shared_table->merge(key, bound, proof.ok, line, cert, proof.refuted);
    } else {
        s.tt.merge(key, bound, proof.ok, line, cert, proof.refuted);
    }
}



} // namespace mateprover

#endif // MATEPROVER_PROOFTABLE_H_INCLUDED
