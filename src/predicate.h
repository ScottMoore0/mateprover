// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// predicate.h -- Candidate pruning predicates, as OBSERVERS only.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_PREDICATE_H_INCLUDED
#define MATEPROVER_PREDICATE_H_INCLUDED

namespace mateprover {

// ---------------------------------------------------------------------------
// A CANDIDATE PRUNING THEOREM, AND WHY IT NEVER PRUNES ANYTHING
//
// A pruning theorem says "no solution below here" without searching. Getting one
// wrong does not make the engine slow, it makes it report a sound problem as
// unsolvable -- silently, with nothing in the output to see. Section 74 shipped
// an unsound bound and this is the class of bug the whole design exists to
// prevent.
//
// Soundness cannot be established by testing. A predicate that survives every
// test in this repository may be false on the next position, and no number of
// passing runs turns a conjecture into a theorem.
//
// But FALSIFICATION can be automated completely, and that is what this is. The
// predicate is evaluated at an attacker node and the search then proceeds as if
// it had said nothing. When the node returns, its true verdict is known:
//
//     the predicate fired and the node was PROVED   -> a counterexample, and the
//                                                      candidate is dead. One is
//                                                      enough, and it is exact.
//     the predicate fired and the node failed       -> the subtree it would have
//                                                      skipped is counted, which
//                                                      is what the candidate is
//                                                      worth if it is ever proved.
//
// So an automatic search can generate candidates and kill the false ones at
// scale, and what survives is a conjecture with a measured fire rate, a measured
// saving, and no known counterexample. Promoting it to a live prune still needs
// a proof written by a person. That is not a limitation of the method; it is
// what "exact prover" means.
//
// This file therefore contains no pruning. It contains a language, an evaluator,
// and the arithmetic for an observer.

inline int pred_popcount(std::uint64_t x) {
    int n = 0;
    while (x) { x &= x - 1; ++n; }
    return n;
}

// One feature's value at this node. Only ever called when a predicate is
// active, so nothing here is on a hot path in a normal run.
inline int predicate_feature(const Board& b, Color attacker, int depth, int f) {
    const std::uint64_t a = b.by_color[attacker];
    const std::uint64_t d = b.by_color[other(attacker)];
    switch (f) {
        case PF_DEPTH: return depth;
        case PF_MEN: return pred_popcount(b.occ);
        case PF_AMEN: return pred_popcount(a);
        case PF_DMEN: return pred_popcount(d);
        case PF_AMAT:
        case PF_DMAT: {
            const std::uint64_t side = (f == PF_AMAT) ? a : d;
            return 9 * pred_popcount(side & b.by_type[PT_QUEEN])
                 + 5 * pred_popcount(side & b.by_type[PT_ROOK])
                 + 3 * pred_popcount(side & (b.by_type[PT_BISHOP] | b.by_type[PT_KNIGHT]))
                 + 1 * pred_popcount(side & b.by_type[PT_PAWN]);
        }
        case PF_AQUEENS: return pred_popcount(a & b.by_type[PT_QUEEN]);
        case PF_AROOKS: return pred_popcount(a & b.by_type[PT_ROOK]);
        case PF_AMINORS: return pred_popcount(a & (b.by_type[PT_BISHOP] | b.by_type[PT_KNIGHT]));
        case PF_APAWNS: return pred_popcount(a & b.by_type[PT_PAWN]);
        // E, computed with the king REMOVED from the board, which is what makes
        // it a statement about the squares rather than about the king's own
        // blocking. Reused from the x-escape rule rather than reimplemented.
        case PF_DFLIGHTS: return escape_count(b, other(attacker));
        case PF_AINCHECK: return in_check(b, attacker) ? 1 : 0;
        default: return 0;
    }
}

inline bool pred_clause_holds(const PredClause& c, int value) {
    switch (c.op) {
        case PO_LE: return value <= c.value;
        case PO_GE: return value >= c.value;
        case PO_LT: return value < c.value;
        case PO_GT: return value > c.value;
        case PO_EQ: return value == c.value;
        case PO_NE: return value != c.value;
        default: return false;
    }
}

// Does the candidate CLAIM there is no solution below this node?
//
// Note the name: it fires, it does not prune. Nothing reads this except the
// observer in prove_attacker.
inline bool predicate_fires(const Predicate& p, const Board& b, Color attacker, int depth) {
    for (int i = 0; i < p.count; ++i) {
        const PredClause& c = p.clauses[static_cast<std::size_t>(i)];
        if (!pred_clause_holds(c, predicate_feature(b, attacker, depth, c.feature))) {
            return false;
        }
    }
    return true;
}

// Parse `depth<=2&amen<=3&dflights>=5`. Returns false on anything it does not
// fully understand, because a predicate that silently parsed to something other
// than what was written would make every measurement about it meaningless.
inline bool parse_predicate(const std::string& text, Predicate& out, std::string& error) {
    out = Predicate{};
    std::size_t pos = 0;
    while (pos < text.size()) {
        if (out.count >= static_cast<int>(out.clauses.size())) {
            error = "at most four clauses";
            return false;
        }
        const std::size_t amp = text.find('&', pos);
        const std::string term = text.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        pos = (amp == std::string::npos) ? text.size() : amp + 1;
        if (term.empty()) {
            error = "empty clause";
            return false;
        }
        // Longest operators first, so "<=" is never read as "<" plus junk.
        static const struct { const char* text; int op; } kOps[] = {
            {"<=", PO_LE}, {">=", PO_GE}, {"!=", PO_NE},
            {"==", PO_EQ}, {"<", PO_LT}, {">", PO_GT}, {"=", PO_EQ},
        };
        std::size_t op_at = std::string::npos;
        int op = -1;
        std::size_t op_len = 0;
        for (const auto& cand : kOps) {
            const std::size_t at = term.find(cand.text);
            if (at != std::string::npos && (op_at == std::string::npos || at < op_at ||
                                            (at == op_at && std::strlen(cand.text) > op_len))) {
                op_at = at;
                op = cand.op;
                op_len = std::strlen(cand.text);
            }
        }
        if (op < 0 || op_at == 0) {
            error = "clause '" + term + "' has no comparison";
            return false;
        }
        const std::string name = term.substr(0, op_at);
        const std::string number = term.substr(op_at + op_len);
        int feature = -1;
        for (int f = 0; f < PF_COUNT; ++f) {
            if (name == pred_feature_name(f)) {
                feature = f;
                break;
            }
        }
        if (feature < 0) {
            error = "unknown feature '" + name + "'";
            return false;
        }
        if (number.empty()) {
            error = "clause '" + term + "' has no value";
            return false;
        }
        char* end = nullptr;
        const long value = std::strtol(number.c_str(), &end, 10);
        if (end == number.c_str() || *end != '\0' || value < -1000 || value > 1000) {
            error = "clause '" + term + "' has a bad value";
            return false;
        }
        PredClause clause;
        clause.feature = static_cast<std::uint8_t>(feature);
        clause.op = static_cast<std::uint8_t>(op);
        clause.value = static_cast<int>(value);
        out.clauses[static_cast<std::size_t>(out.count++)] = clause;
    }
    if (out.count == 0) {
        error = "empty predicate";
        return false;
    }
    return true;
}

} // namespace mateprover

#endif // MATEPROVER_PREDICATE_H_INCLUDED
