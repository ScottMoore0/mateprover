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

// ---------------------------------------------------------------------------
// CONTACT DISTANCE, and why it is only ever a feature
//
// A capture needs an attacker man and a defender man one legal move apart. So a
// natural bound suggests itself: if no attacker man can reach any defender man
// inside the remaining depth, no capture can happen, and a node needing one can
// be cut without searching.
//
// The relaxation is admissible in one direction. Ignoring obstruction can only
// SHORTEN a piece's journey, so a distance computed on an empty board never
// overstates the real one, and `contact > depth` really does mean the attacker
// cannot reach anybody -- IF the defender stands still.
//
// The defender does not stand still. A defender who walks a man into an
// attacker's reach creates contact the empty-board distance never saw, and the
// bound would then prune a node where a capture was available. That is not a
// theoretical worry: it is the shape of section 74's unsound bound, which
// reported solvable problems as unsolvable and shipped.
//
// A sound version has to argue that the defender always HAS a move keeping its
// men out of reach, which is a statement about zugzwang, not about geometry.
// Nobody has proved it. So this ships as a feature the observer can measure and
// falsify, exactly as predicate.h says: `--predicate contact>depth` will report
// its fire rate, what it would have saved, AND any counterexample. One
// counterexample kills it, and finding none does not make it true.
using EmptyBoardDist = std::array<std::array<std::array<std::uint8_t, 64>, 64>, 6>;

inline const EmptyBoardDist& empty_board_distance() {
    // BFS from every square for every piece type, on a board with nothing on it.
    // Built once; 6 x 64 x 64 bytes.
    static const auto table = [] {
        EmptyBoardDist d{};
        for (auto& per_type : d) {
            for (auto& per_from : per_type) {
                per_from.fill(63);
            }
        }
        auto on = [](int f, int r) { return f >= 0 && f < 8 && r >= 0 && r < 8; };
        static const int kN[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
        static const int kK[8][2] = {{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
        for (int type = 0; type < 6; ++type) {
            for (int from = 0; from < 64; ++from) {
                std::array<std::uint8_t, 64>& dist = d[static_cast<std::size_t>(type)]
                                                      [static_cast<std::size_t>(from)];
                dist[static_cast<std::size_t>(from)] = 0;
                std::vector<int> frontier{from}, next;
                for (int step = 1; step <= 8 && !frontier.empty(); ++step) {
                    next.clear();
                    for (int sq : frontier) {
                        const int f = sq % 8, r = sq / 8;
                        auto reach = [&](int tf, int tr) {
                            if (!on(tf, tr)) return;
                            const std::size_t t = static_cast<std::size_t>(tr * 8 + tf);
                            if (dist[t] > step) {
                                dist[t] = static_cast<std::uint8_t>(step);
                                next.push_back(static_cast<int>(t));
                            }
                        };
                        if (type == PT_KNIGHT) {
                            for (auto& m : kN) reach(f + m[0], r + m[1]);
                        } else if (type == PT_KING) {
                            for (auto& m : kK) reach(f + m[0], r + m[1]);
                        } else if (type == PT_PAWN) {
                            // A pawn CAPTURES diagonally, so contact for a pawn
                            // means a diagonal step. Colour-agnostic on purpose:
                            // taking both directions can only shorten, which is
                            // the safe side of this relaxation.
                            reach(f - 1, r + 1); reach(f + 1, r + 1);
                            reach(f - 1, r - 1); reach(f + 1, r - 1);
                        } else {
                            const bool diag = (type == PT_BISHOP || type == PT_QUEEN);
                            const bool orth = (type == PT_ROOK || type == PT_QUEEN);
                            for (int dir = 0; dir < 8; ++dir) {
                                const bool is_diag = (dir % 2) == 1;
                                if (is_diag ? !diag : !orth) continue;
                                for (int n = 1; n < 8; ++n) {
                                    reach(f + kK[dir][0] * n, r + kK[dir][1] * n);
                                }
                            }
                        }
                    }
                    frontier.swap(next);
                }
            }
        }
        return d;
    }();
    return table;
}

// Fewest moves any attacker man needs to stand where a defender man stands.
inline int contact_distance(const Board& b, Color attacker) {
    const EmptyBoardDist& d = empty_board_distance();
    const std::uint64_t theirs = b.by_color[other(attacker)];
    if (!theirs) {
        return 63;
    }
    int best = 63;
    std::uint64_t mine = b.by_color[attacker];
    while (mine) {
        const int from = lsb_index(mine);
        mine &= mine - 1;
        int type = -1;
        for (int t = 0; t < 6; ++t) {
            if (b.by_type[t] & (1ull << from)) { type = t; break; }
        }
        if (type < 0) {
            continue;
        }
        std::uint64_t targets = theirs;
        while (targets) {
            const int to = lsb_index(targets);
            targets &= targets - 1;
            const int v = d[static_cast<std::size_t>(type)][static_cast<std::size_t>(from)]
                           [static_cast<std::size_t>(to)];
            if (v < best) {
                best = v;
            }
        }
        if (best <= 1) {
            break;      // cannot do better than "capturable right now"
        }
    }
    return best;
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
        case PF_CONTACT: return contact_distance(b, attacker);
        // Relational: how much contact exists RIGHT NOW, in each direction.
        // These are the cheapest statements that involve both sides at once.
        case PF_DATTACKED: {
            int n = 0;
            std::uint64_t men = b.by_color[other(attacker)];
            while (men) {
                const int sq = lsb_index(men);
                men &= men - 1;
                if (is_attacked(b, sq, attacker)) ++n;
            }
            return n;
        }
        case PF_AATTACKED: {
            int n = 0;
            std::uint64_t men = b.by_color[attacker];
            while (men) {
                const int sq = lsb_index(men);
                men &= men - 1;
                if (is_attacked(b, sq, other(attacker))) ++n;
            }
            return n;
        }
        case PF_MATE1IMP: return mate1_impossible_by_coverage(b) ? 1 : 0;
        case PF_ACAPTURES: {
            int n = 0;
            std::uint64_t men = b.by_color[other(attacker)];
            while (men) {
                const int sq = lsb_index(men);
                men &= men - 1;
                if (is_attacked(b, sq, attacker)) ++n;
            }
            // En passant: the captured pawn is NOT on the destination square,
            // so no square-attack test can see it. A capturing pawn sits one
            // rank behind the ep square, on either adjacent file.
            if (b.ep >= 0) {
                const int back = (attacker == WHITE) ? -8 : 8;
                const int f = b.ep % 8;
                for (int df = -1; df <= 1; df += 2) {
                    const int from = b.ep + back + df;
                    if (f + df < 0 || f + df > 7 || from < 0 || from > 63) continue;
                    const std::uint64_t bit = 1ull << from;
                    if ((b.by_color[attacker] & b.by_type[PT_PAWN] & bit) != 0) {
                        ++n;
                        break;
                    }
                }
            }
            return n;
        }
        case PF_DKINGRING: {
            const int k = king_square(b, other(attacker));
            if (k < 0) return 0;
            int n = 0;
            const int kf = k % 8, kr = k / 8;
            for (int df = -1; df <= 1; ++df) {
                for (int dr = -1; dr <= 1; ++dr) {
                    if (!df && !dr) continue;
                    const int f = kf + df, r = kr + dr;
                    if (f < 0 || f > 7 || r < 0 || r > 7) continue;
                    if (is_attacked(b, r * 8 + f, attacker)) ++n;
                }
            }
            return n;
        }
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
