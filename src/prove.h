// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// prove.h -- The exact AND/OR proof kernel: attacker and defender nodes, threat probes, and attacker restrictions.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_PROVE_H_INCLUDED
#define MATEPROVER_PROVE_H_INCLUDED

namespace mateprover {

Proof prove_attacker(Search& s, const Board& b, int depth);

// GAP-1's axioms. Defined below with their proofs; declared here because the
// selfmate node routines come first in this file.
inline bool position_is_refuted_axiomatically(const Search& s, const Board& b);

// Is `m` a threat move? WinChest defines one as a move after which, if the
// defender were allowed to pass, the attacker could mate within ThreatDepth.
//
// The null move makes the position unreachable by legal play, which is the
// point: it measures what the attacker is threatening, not what is forced. The
// probe therefore answers a different question from the enclosing search and is
// given its own Search and its own table -- sharing one would let a
// check-restricted disproof answer an unrestricted question.
bool move_is_threat(Search& s, const Board& b, const Move& m, int depth_budget) {
    if (s.threat_ctx == nullptr) {
        s.threat_ctx.reset(new Search());
        static_cast<SearchConfig&>(*s.threat_ctx) = static_cast<const SearchConfig&>(s);
        // The probe is a plain mate search: no threat filter (which would
        // recurse), no defender-side limits, and checks-only exactly when
        // WinChest's sign says so.
        s.threat_ctx->threat_depth = 0;
        s.threat_ctx->king_squares = 0;
        s.threat_ctx->piece_limit = 0;
        s.threat_ctx->max_defender_moves = 0;
        s.threat_ctx->checks_mask = s.threat_depth > 0 ? 1 : 0;
        s.threat_ctx->emit_proof = false;
        s.threat_ctx->tt.capacity = entry_capacity_for_mb(s.memory_mb);
    }
    Search& t = *s.threat_ctx;
    t.attacker = s.attacker;
    t.aborted = false;
    t.has_deadline = s.has_deadline;
    t.deadline = s.deadline;

    Board nb = make_move(b, m);
    // The defender passes: side to move returns to the attacker and any
    // en-passant right created by `m` lapses.
    nb.stm = s.attacker;
    nb.ep = -1;

    const Proof p = prove_attacker(t, nb, depth_budget);
    if (t.timed_out) {
        s.timed_out = true;
    }
    return p.ok;
}

void restrict_attacker_moves(Search& s, const Board& b, std::vector<Move>& moves) {
    const int cm = s.checks_mask;
    const bool needs_child = s.king_squares > 0 || s.piece_limit > 0 ||
                             s.max_defender_moves > 0 || (cm & 2) != 0 || (cm & 4) != 0;
    // WinChest disables ThreatDepth internally when ChecksOnly is 1 (or 3),
    // because threats add nothing once every move must already be a check.
    int threat = (cm & 1) ? 0 : s.threat_depth;
    if (threat != 0) {
        // "The maximum value for this parameter is the current matenumber-2,
        // higher values are ignored."
        //
        // "Ignored" means the option switches off, not that it clamps to the
        // maximum. Differential testing settled this: clamping made a mate-in-3
        // with -R 2 unsolvable for E while WinChest solved it, because clamping
        // to 1 imposes a restriction where WinChest imposes none.
        const int cap = s.root_depth - 2;
        const int magnitude = threat < 0 ? -threat : threat;
        if (cap < 1 || magnitude > cap) {
            threat = 0;
        }
    }
    if (cm == 0 && !needs_child && threat == 0) {
        return;
    }
    const Color them = other(b.stm);
    moves.erase(std::remove_if(moves.begin(), moves.end(), [&](const Move& m) {
        // The attacker-side bits need no child position.
        const bool gives_check = (cm & (1 | 16)) ? move_gives_check_fast(b, m) : false;
        if ((cm & 1) && !gives_check) {
            return true;                       // 1: only own check-moves
        }
        // Bits 8 and 16 both exempt the mating move. The manual states the
        // exception only for 16, but differential testing showed WinChest
        // applying it to 8 as well: with -C 8 it solves mate-in-1 positions
        // whose only move is a capture, which a literal "no own captures"
        // reading forbids.
        const bool bans_capture = (cm & 8) && (b.sq[m.to] != '.' || m.ep);
        const bool bans_check = (cm & 16) && gives_check;
        if (bans_capture || bans_check) {
            const Board probe = make_move(b, m);
            if (has_legal_move(probe, s.move_reserve, s.move_reserve_capacity, s.static_pseudo)) {
                return true;                   // not mate, so the ban applies
            }
        }
        if (threat != 0 && !move_is_threat(s, b, m, threat < 0 ? -threat : threat)) {
            return true;
        }
        if (!needs_child) {
            return false;
        }
        // The remaining three all ask about the defender's replies, so they
        // share one materialisation and one move generation.
        const Board nb = make_move(b, m);
        const std::vector<Move> replies =
            legal_moves(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);

        if (s.max_defender_moves > 0 &&
            static_cast<int>(replies.size()) > s.max_defender_moves) {
            return true;
        }
        if (cm & 2) {
            // 2: the defender must have no move that checks the attacker.
            for (const Move& r : replies) {
                if (move_gives_check_fast(nb, r)) {
                    return true;
                }
            }
        }
        if (cm & 4) {
            // 4: the defender must have no capture of an attacker piece.
            for (const Move& r : replies) {
                if (nb.sq[r.to] != '.' || r.ep) {
                    return true;
                }
            }
        }
        if (s.king_squares > 0) {
            // "KingSquares" counts the square the king stands on as well, so a
            // value of 1 permits no king move at all.
            const int king_sq = nb.king_sq[them];
            int reachable = 1;
            for (const Move& r : replies) {
                if (r.from == king_sq) {
                    ++reachable;
                }
            }
            if (reachable > s.king_squares) {
                return true;
            }
        }
        if (s.piece_limit > 0) {
            std::array<bool, 64> movable{};
            int distinct = 0;
            for (const Move& r : replies) {
                if (!movable[static_cast<std::size_t>(r.from)]) {
                    movable[static_cast<std::size_t>(r.from)] = true;
                    ++distinct;
                }
            }
            if (distinct > s.piece_limit) {
                return true;
            }
        }
        return false;
    }), moves.end());
}



// ---------------------------------------------------------------------------
// Selfmate: the attacker forces the defender to mate him.
//
// A separate recursion rather than another terminal predicate, because the
// shape differs. For a directmate the goal is tested after each ATTACKER move;
// here it is tested at an attacker node before moving, since "the attacker is
// mated" is a statement about the side to move.
//
// Four degenerate cases follow directly from that definition, and none collapses
// into another. Each is checkable at a board:
//
//     attacker is mated      -> solution, possibly shorter than asked
//     attacker is stalemated -> no solution: he is not mated, merely stuck
//     defender is mated      -> no solution: he must mate US, not be mated
//     defender is stalemated -> no solution: he cannot move, so cannot mate
//
// Kept out of the directmate path deliberately. That path is validated by 314
// checks and a corpus of 264 compositions; threading a third goal with a
// different control flow through it would put all of that at risk to save some
// duplication. The preconditioner is not used here either -- DFPN's proof
// numbers are defined against the directmate terminal, and reusing them would
// be unsound rather than merely unhelpful.
Proof prove_selfmate_defender(Search& s, const Board& b, int depth);

Proof prove_selfmate_attacker(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (position_is_refuted_axiomatically(s, b)) {
        Proof out;
        out.refuted = true;
        return out;
    }
    if (b.stm != s.attacker) {
        return {};
    }
    const bool have_move = has_legal_move(b, s.move_reserve, s.move_reserve_capacity,
                                          s.static_pseudo);
    if (!have_move) {
        // No legal move, so the attacker is mated or stalemated -- and which of
        // those is the goal and which is failure is the ONLY difference between
        // a selfmate and a selfstalemate. The two verdicts are exact opposites
        // here, so reading the wrong one proves the wrong problem while every
        // output stays well-formed.
        if (in_check(b, b.stm) == goal_wants_check(s.goal)) {
            std::string cert;
            if (s.emit_proof) {
                cert = s.goal == Goal::Selfmate ? "{\"selfmated\":true}"
                                                : "{\"selfstalemated\":true}";
            }
            return Proof{true, {}, cert};
        }
        return {};
    }
    if (depth <= 0) {
        return {};
    }

    TTKey key = tt_key(b, 0, 'A', s.attacker, s.goal);
    Proof cached;
    if (probe_exact_proof_table(s, key, depth, cached)) {
        return cached;
    }

    bool scored = false;
    auto moves = generate_ordered_moves(s, b, scored);
    restrict_attacker_moves(s, b, moves);
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();

    // Consult the ordering hint. Without this the selfmate preconditioner was
    // inert: it computed proof numbers and stored the move they favoured, and
    // nothing ever read it. Hints cannot change a verdict -- they only decide
    // which move is tried first -- so this is the safe half of what the
    // preconditioner offers.
    const TTKey hint_key = move_hint_key(b, 'A', s.attacker, s.goal);
    if (s.proof_hints) {
        ++s.stats.proof_hint_probes;
        if (auto hint = s.attacker_proofs.find(hint_key); hint != s.attacker_proofs.end()) {
            if (move_to_front(moves, hint->second)) {
                ++s.stats.proof_hint_hits;
            }
        }
    }

    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        const Board nb = make_move(b, amove);
        Proof replies = prove_selfmate_defender(s, nb, depth);
        if (s.aborted) {
            return {};
        }
        if (replies.ok) {
            std::vector<Move> pv{amove};
            pv.insert(pv.end(), replies.pv.begin(), replies.pv.end());
            std::string cert;
            if (s.emit_proof) {
                cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"d\":" + replies.cert + "}";
            }
            Proof proof{true, pv, cert};
            store_exact_proof_table(s, key, depth, proof);
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs[hint_key] = amove;
            }
            return proof;
        }
    }
    store_exact_proof_table(s, key, depth, {});
    return {};
}

// GAP-2: perpetual check by a lone queen. See docs/GAP2_DERIVATION.md.
//
// True when the defender, holding exactly king and queen and to move, can check
// forever without ever mating -- which makes the selfmate unreachable at any
// depth, because the attacker needs the defender to deliver the mate.
//
// The specification offers local conditions on a single checking move. Those are
// NOT sufficient: the attacker's king may reach a square where his own men block
// every queen line, and then the defender cannot check and the attacker is free.
// The property is about a REGION, so this computes the closure directly.
//
// It is cheap because of an observation that makes the region finite: if the
// attacker is in check after every defender move, cannot block, and will not
// capture the queen, then every attacker move is a KING move -- so all his other
// pieces are frozen for the whole line, and the state is three squares.
//
// Selfmate ONLY. Under selfstalemate the argument is unsound: it rests on the
// capture of the queen being fatal because a bare king cannot mate, and a bare
// king can perfectly well stalemate.
inline bool selfmate_perpetual_check(Search& s, const Board& root) {
    if (!s.any_depth_refutations || s.goal != Goal::Selfmate) {
        return false;                       // goal scope first, before material
    }
    const Color defender = other(s.attacker);
    if (root.stm != defender) {
        return false;
    }
    const std::uint64_t men = root.by_color[defender];
    const std::uint64_t queens = men & root.by_type[PT_QUEEN];
    const std::uint64_t kings = men & root.by_type[PT_KING];
    if (queens == 0 || (men & ~(queens | kings)) != 0) {
        return false;                       // defender is not exactly king + queen
    }

    // A bound, not a budget. Hitting it means "no proof found", never "proved":
    // running out of room is not a theorem. GAP-1's rule, again.
    const std::size_t kMaxStates = 256;
    std::vector<std::array<std::uint64_t, 4>> seen;
    std::vector<Board> frontier{root};

    while (!frontier.empty()) {
        const Board p = frontier.back();
        frontier.pop_back();
        if (std::find(seen.begin(), seen.end(), p.packed) != seen.end()) {
            continue;
        }
        if (seen.size() >= kMaxStates) {
            return false;
        }
        seen.push_back(p.packed);

        // Does SOME queen check keep the whole closure alive? One witness per
        // state is enough -- the defender is choosing.
        bool state_ok = false;
        std::vector<Board> pending;
        for (const Move& dm : legal_moves(p, s.move_reserve, s.move_reserve_capacity,
                                          s.static_pseudo)) {
            if (type_of(p.sq[static_cast<std::size_t>(dm.from)]) != PT_QUEEN) {
                continue;
            }
            const Board q = make_move(p, dm);
            if (!in_check(q, q.stm)) {
                continue;                   // not a check: the attacker is not forced
            }
            const auto replies = legal_moves(q, s.move_reserve, s.move_reserve_capacity,
                                             s.static_pseudo);
            if (replies.empty()) {
                continue;                   // this check is MATE, which is what the
                                            // attacker wants; the defender declines it
            }
            pending.clear();
            bool all_ok = true;
            for (const Move& am : replies) {
                if (am.to == dm.to) {
                    continue;               // captures the queen: attacker loses
                                            // outright, since a bare king cannot mate
                }
                if (type_of(q.sq[static_cast<std::size_t>(am.from)]) != PT_KING) {
                    all_ok = false;         // a block, or some other unit moving:
                    break;                  // the frozen-pieces argument fails
                }
                const Board r = make_move(q, am);
                if (in_check(r, r.stm)) {
                    all_ok = false;         // the defender must answer a check
                    break;                  // instead of giving one
                }
                pending.push_back(r);
            }
            if (all_ok) {
                state_ok = true;
                for (const Board& r : pending) {
                    frontier.push_back(r);
                }
                break;
            }
        }
        if (!state_ok) {
            return false;
        }
    }
    return true;
}

Proof prove_selfmate_defender(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.defender_nodes;
    if (selfmate_perpetual_check(s, b)) {
        ++s.stats.perpetual_refutations;
        Proof out;
        out.refuted = true;
        return out;
    }

    // Instrumentation only: how often does the DEFENDER reduce to king + queen?
    //
    // Root material is a weak predictor for any lone-defender refutation, because
    // the configuration is also reached mid-search once the defender's other
    // units are captured -- so the interesting number is this one, not a filter
    // over starting positions. Costs two bitboard reads and no branch on the hot
    // path's outcome.
    if (b.by_type[PT_QUEEN] & b.by_color[b.stm]) {
        std::uint64_t units = b.by_color[b.stm];
        int count = 0;
        while (units) { units &= units - 1; ++count; }
        if (count == 2) {
            ++s.stats.defender_kq_nodes;
        }
    }

    std::vector<Move> replies = legal_moves(b, s.move_reserve, s.move_reserve_capacity,
                                            s.static_pseudo);
    ++s.stats.defender_move_lists;
    s.stats.defender_moves += replies.size();
    if (replies.empty()) {
        // The defender is mated or stalemated. Either way he has not mated the
        // attacker, so this line fails.
        return {};
    }

    // The reported depth must be the WORST line, not the first one. Taking the
    // first reply's variation understated it whenever another defence held out
    // longer, and the independent verifier caught exactly that: "certificate
    // proves mate in 6, reported 2". A selfmate in N is a claim about every
    // defence, so the PV has to be the longest of them.
    std::vector<Move> pv;
    std::vector<std::string> branch_certs;
    for (const Move& r : replies) {
        ++s.stats.defender_replies_tried;
        const Board rb = make_move(b, r);
        Proof child = prove_selfmate_attacker(s, rb, depth - 1);
        if (s.aborted) {
            return {};
        }
        if (!child.ok) {
            return {};            // one surviving defence refutes the whole line
        }
        if (child.pv.size() + 1 > pv.size()) {
            pv.clear();
            pv.push_back(r);
            pv.insert(pv.end(), child.pv.begin(), child.pv.end());
        }
        if (s.emit_proof) {
            branch_certs.push_back("{\"r\":" + json_quote(move_uci(r)) + ",\"p\":" + child.cert + "}");
        }
    }
    std::string cert;
    if (s.emit_proof) {
        cert = "[";
        for (std::size_t i = 0; i < branch_certs.size(); ++i) {
            if (i) cert.push_back(',');
            cert += branch_certs[i];
        }
        cert.push_back(']');
    }
    return Proof{true, pv, cert};
}

// The cooperative prover: helpmate and helpstalemate.
//
// Every other search in this engine is AND/OR -- the attacker picks a move that
// works and the defender must have none that escapes. A helpmate has no
// defender at all. Both sides want the same terminal, so EVERY node is an OR
// node and the whole question is existential: does some sequence of `plies`
// legal moves end in the goal?
//
// That is why none of the adversarial machinery is reused rather than adapted.
// Proof and disproof numbers measure how much work an ADVERSARY can force; with
// no adversary they carry no meaning. The restriction portfolio is sound only
// because removing attacker options cannot invent a mate, an argument that says
// nothing about a side which is helping. The root split parallelises a
// disjunction over root moves whose children are conjunctions. None of the
// three has a cooperative analogue, so this is a plain depth-first search with
// a table, which is also what the shape of the problem wants: helpmates are
// short and wide, not long and narrow.
//
// EXACT LENGTH, not "within". `h#3` asks for a mate on move three, not by move
// three, so a solution at a shorter length does not answer it. The table key
// therefore carries `plies`, which makes each length its own entry and reduces
// the usual "proved within N implies proved within M>N" bound to an exact
// match. Reusing the monotone bound directly would have been unsound.
Proof prove_help(Search& s, const Board& b, int plies) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    if (plies <= 0) {
        // The line is complete. The side to move is the one the stipulation
        // requires to be mated or stalemated; both sides cooperated to get here,
        // but the terminal is still an ordinary mate or stalemate.
        if (goal_terminal(b, s.goal, s.move_reserve, s.move_reserve_capacity, s.static_pseudo)) {
            std::string cert;
            if (s.emit_proof) {
                cert = s.goal == Goal::Helpmate ? "{\"helpmated\":true}"
                                                : "{\"helpstalemated\":true}";
            }
            return Proof{true, {}, cert};
        }
        return {};
    }

    // An admissible lower bound: is the mate simply out of reach from here?
    //
    // A helpmate ends with one side checkmated, so at the final position some
    // unit of the MATING side attacks the mated king's square. Two relaxations
    // make that checkable in a few bitboard ANDs, and both only ever widen the
    // possibilities, so the bound can rule a subtree out but never rule one in:
    //
    //   the mated king ends within `their_moves` king-steps of where it stands,
    //   measured on an empty board;
    //   a mating unit attacks that square after at most `our_moves` of its own
    //   moves, again on an empty board, promotion included.
    //
    // If no unit of the mating side can attack ANY square the king could reach,
    // no mate exists down this line at any continuation, and the whole subtree
    // is dead. This prunes before the move list is built, so it saves the
    // generation as well as the recursion.
    //
    // Only for helpmate: a helpstalemate needs no check, so the argument has
    // nothing to stand on. Only when the mating side has three moves or fewer,
    // because the table stops at three and using it beyond that would
    // UNDERSTATE reach -- the one way this could become unsound.
    if (s.goal == Goal::Helpmate) {
        const Color mated = (plies % 2 == 0) ? b.stm : other(b.stm);
        const Color mating = other(mated);
        const int our_moves = (mating == b.stm) ? (plies + 1) / 2 : plies / 2;
        const int their_moves = plies - our_moves;
        if (our_moves >= 1 && our_moves <= 3) {
            const int king_sq = b.king_sq[mated];
            if (king_sq >= 0) {
                const std::uint64_t reachable =
                    king_disc_table()[static_cast<std::size_t>(king_sq)]
                                     [static_cast<std::size_t>(their_moves > 8 ? 8 : their_moves)];
                const auto& within = attack_within_table();
                // The mating KING is excluded: a king cannot give check, so it
                // can never be the unit attacking the mated king at the end.
                // Free, and strictly tighter -- with it included, a mating king
                // anywhere near the enemy king kept every subtree alive.
                std::uint64_t men = b.by_color[mating] & ~b.by_type[PT_KING];
                bool possible = false;
                while (men) {
                    const int from = lsb_index(men);
                    men &= men - 1;
                    int pt = PT_NONE;
                    for (int t = 0; t < 6; ++t) {
                        if (b.by_type[static_cast<std::size_t>(t)] & (1ull << from)) { pt = t; break; }
                    }
                    if (pt == PT_NONE) continue;
                    if (within[static_cast<std::size_t>(mating * 6 + pt)]
                             [static_cast<std::size_t>(from)]
                             [static_cast<std::size_t>(our_moves)] & reachable) {
                        possible = true;
                        break;
                    }
                }
                if (!possible) {
                    ++s.stats.help_unreachable_prunes;
                    return {};
                }
            }
        }
    }

    // `plies` is in the key, so this is an exact-length entry. See above.
    TTKey key = tt_key(b, plies, 'H', s.attacker, s.goal);
    Proof cached;
    if (probe_exact_proof_table(s, key, plies, cached)) {
        return cached;
    }

    bool scored = false;
    auto moves = generate_ordered_moves(s, b, scored);
    // No restriction is applied. `restrict_attacker_moves` exists to remove
    // options from an adversary's opponent; here both sides are on the same
    // side, and a restriction that removed a helper's move would remove
    // solutions without the soundness argument that justifies it elsewhere.
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();

    // On the LAST ply, only a move that can reach the terminal is worth making.
    //
    // A helpmate ends in checkmate, and only a checking move can deliver one; a
    // helpstalemate ends in stalemate, which no checking move can. The ordering
    // pass already computed that bit, so the whole final ply reduces to a scan
    // of the candidates instead of make-move-and-test on every legal move --
    // and the final ply is the widest one, since it is the deepest.
    //
    // Gated exactly as 54 requires. The score only means "gives check" when the
    // moves were actually SCORED and the check term was actually applied; an
    // unscored move scores 0, which reads as "not a check" and would accept a
    // checkmate as a stalemate. `moves_scored && s.score_checks` is that gate,
    // and `!s.score_mates` keeps the +1000000 mate bonus from swamping the sign
    // the test depends on.
    const bool last_ply_prune = plies == 1 && scored && s.score_checks && !s.score_mates;
    for (const Move& m : moves) {
        ++s.stats.attacker_candidates;
        if (last_ply_prune && !move_can_reach_goal(m.score, s.goal)) {
            continue;
        }
        const Board nb = make_move(b, m);
        Proof child = prove_help(s, nb, plies - 1);
        if (s.aborted) {
            return {};
        }
        if (child.ok) {
            std::vector<Move> pv{m};
            pv.insert(pv.end(), child.pv.begin(), child.pv.end());
            std::string cert;
            if (s.emit_proof) {
                cert = "{\"h\":" + json_quote(move_uci(m)) + ",\"n\":" + child.cert + "}";
            }
            Proof proof{true, pv, cert};
            store_exact_proof_table(s, key, plies, proof);
            return proof;
        }
    }
    store_exact_proof_table(s, key, plies, {});
    return {};
}

// Every cooperative solution, not just the first.
//
// Chest defines a helpmate as "find ALL sequences of 2N legal moves", and it is
// right to: helpmates conventionally HAVE several intended solutions, so the set
// is the answer rather than a soundness footnote the way a directmate's duals
// are. Enumerating root moves is not enough either -- two solutions often share
// a first move and differ later, and counting keys would report one.
//
// No transposition table. A cached "solved" entry records ONE line, so reusing
// it would silently drop every other solution through that position; and a
// cached "not solved" is still usable but not worth the risk of the two paths
// diverging. Enumeration therefore pays full price, which is why it is capped.
void collect_help_solutions(Search& s, const Board& b, int plies,
                            std::vector<Move>& line,
                            std::vector<std::vector<Move>>& out, std::size_t limit) {
    if (search_cancelled(s) || out.size() >= limit) {
        return;
    }
    ++s.stats.nodes;
    if (plies <= 0) {
        if (goal_terminal(b, s.goal, s.move_reserve, s.move_reserve_capacity, s.static_pseudo)) {
            out.push_back(line);
        }
        return;
    }
    bool scored = false;
    auto moves = generate_ordered_moves(s, b, scored);
    const bool last_ply_prune = plies == 1 && scored && s.score_checks && !s.score_mates;
    for (const Move& m : moves) {
        if (out.size() >= limit) {
            return;
        }
        if (last_ply_prune && !move_can_reach_goal(m.score, s.goal)) {
            continue;
        }
        line.push_back(m);
        collect_help_solutions(s, make_move(b, m), plies - 1, line, out, limit);
        line.pop_back();
        if (s.aborted) {
            return;
        }
    }
}

Proof prove_defender(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.defender_nodes;
    // Depth is not part of the key; it is supplied to probe/store instead.
    TTKey key = tt_key(b, 0, 'D', s.attacker, s.goal);
    Proof exact_cached;
    if (probe_exact_proof_table(s, key, depth, exact_cached)) {
        return exact_cached;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'D', s.attacker, s.goal);
            have_hint_key = true;
        }
        return hint_key;
    };

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
        store_exact_proof_table(s, key, depth, {});
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
            // AND-node composition (GAP-1): the attacker needs the goal after
            // EVERY reply, so one reply he can never win from is a permanent
            // escape and the defender will take it. ONE witness suffices here --
            // the OR node needs all of them. Carried up only when this child was
            // Refuted; a child that merely failed within depth leaves the node
            // depth-bounded, exactly as before.
            Proof out;
            out.refuted = child.refuted;
            store_exact_proof_table(s, key, depth, out);
            return out;
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
        store_exact_proof_table(s, key, depth, {});
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
    store_exact_proof_table(s, key, depth, proof);
    return proof;
}

// ---- The axioms: the only way `Refuted` enters the lattice.
//
// Each is a theorem about the position, not an observation about the search, and
// each must be sound in the strong sense: a single false positive makes the
// engine declare a sound problem unsolvable. See docs/GAP1_DERIVATION.md §4.
//
// Axiom 1 -- a bare attacker king cannot mate. In a directmate the attacker must
// deliver checkmate; a lone king can never give check, so it can never mate, at
// any depth. Material only ever decreases in the attacker's favour by promotion,
// which needs a pawn he does not have. P-CHESS-THEORY, and about as safe as a
// chess theorem gets.
//
// Deliberately NOT applied to the other goals. Under a stalemate goal a bare
// king can absolutely force stalemate, and under selfmate the attacker is trying
// to be mated rather than to mate, so the argument does not transfer. Goal
// scope is the first thing to get wrong here.
inline bool position_is_refuted_axiomatically(const Search& s, const Board& b) {
    if (!s.any_depth_refutations) {
        return false;                 // inert by construction
    }
    if (s.goal == Goal::Mate) {
        const std::uint64_t attacker_men = b.by_color[s.attacker];
        return attacker_men == (attacker_men & b.by_type[PT_KING]);
    }
    // Axiom 2 -- a bare DEFENDER king cannot deliver a selfmate. The attacker
    // needs the defender to mate him; a lone king cannot give check, so cannot
    // mate, and the defender's material never increases (he has no pawn to
    // promote). This is the lemma GAP-2's perpetual argument rests on, so the
    // engine holds it directly rather than only implicitly.
    //
    // No root in the corpus has this shape, which makes it look worthless. It is
    // reached constantly MID-SEARCH -- every line that captures the defender's
    // last unit -- and each such node was otherwise searched to the depth limit
    // at every iteration.
    //
    // Selfmate only, never selfstalemate: a bare king cannot mate, but it can
    // certainly stalemate. See docs/GAP2_DERIVATION.md section 4.
    if (s.goal == Goal::Selfmate) {
        const std::uint64_t defender_men = b.by_color[other(s.attacker)];
        return defender_men == (defender_men & b.by_type[PT_KING]);
    }
    return false;
}

Proof prove_attacker(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (position_is_refuted_axiomatically(s, b)) {
        Proof out;
        out.refuted = true;
        return out;
    }
    if (depth <= 0 || b.stm != s.attacker) {
        return {};
    }
    // Depth is not part of the key; it is supplied to probe/store instead.
    TTKey key = tt_key(b, 0, 'A', s.attacker, s.goal);
    Proof exact_cached;
    if (probe_exact_proof_table(s, key, depth, exact_cached)) {
        return exact_cached;
    }
    TTKey hint_key;
    bool have_hint_key = false;
    auto get_hint_key = [&]() -> const TTKey& {
        if (!have_hint_key) {
            hint_key = move_hint_key(b, 'A', s.attacker, s.goal);
            have_hint_key = true;
        }
        return hint_key;
    };

    bool moves_scored = false;
    auto moves = generate_ordered_moves(s, b, moves_scored);
    restrict_attacker_moves(s, b, moves);
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
    // Mate goal only, and that restriction is not conservatism.
    //
    // The shortcut infers "this move gives check" from the ordering score. For
    // a mate goal that is safe: the check term is +50000 and every other term
    // sums to at most 18050, so a score of 50000 or more means a check and an
    // unscored move (0) simply fails the test and gets the full check.
    //
    // Under a stalemate goal the check term is NEGATIVE, so the safe direction
    // inverts and an unscored move -- score 0 -- reads as "not a check", passes
    // the test, and is accepted the moment the defender has no reply. That is a
    // CHECKMATE reported as `sm 1`: a false proof, produced from a position the
    // suite already tests. No threshold fixes it, because 0 is a legitimate
    // score for a quiet move and an illegitimate one for an unscored check.
    //
    // So the goals that cannot carry the flag safely pay for the terminal test.
    const bool can_use_ordered_check_shortcut = s.ordered_check_shortcut && moves_scored && s.score_checks && !s.score_mates && s.goal == Goal::Mate;
    // OR-node composition (GAP-1): the attacker reaches the goal only by
    // choosing SOME move, so if every move leads to a position he can never win
    // from, he can never win here. All successors must be Refuted -- contrast
    // the AND node, where one suffices. Swapping the two is the likely bug and
    // it is invisible on positives.
    //
    // `moves.empty()` must not count as "all refuted": a node with no moves is
    // terminal and the goal decides it, not composition.
    bool all_moves_refuted = s.any_depth_refutations && !moves.empty();
    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        Board nb = make_move(b, amove);
        ++s.stats.immediate_mate_tests;
        bool mate = false;
        if (can_use_ordered_check_shortcut) {
            // The shortcut reads the check bit the ordering pass already
            // computed. It inverts cleanly for a stalemate goal: mate needs
            // check, stalemate forbids it, so the same bit decides which side
            // of the test can be skipped outright.
            ++s.stats.ordered_check_shortcut_uses;
            if (move_can_reach_goal(amove.score, s.goal)) {
                // Goal-compatible: the only remaining question is whether the
                // defender has a reply.
                ++s.stats.ordered_check_shortcut_checks;
                mate = !has_legal_move(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
            } else {
                ++s.stats.ordered_check_shortcut_skips;
            }
        } else {
            mate = is_goal(nb, s.goal, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
        }
        if (mate) {
            ++s.stats.immediate_mates;
            std::vector<Move> pv{amove};
            std::string cert;
            if (s.emit_proof) {
                cert = "{\"a\":" + json_quote(move_uci(amove)) + (s.goal == Goal::Stalemate ? ",\"stalemate\":true}" : ",\"mate\":true}");
            }
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs[get_hint_key()] = amove;
            }
            Proof proof{true, pv, cert};
            store_exact_proof_table(s, key, depth, proof);
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs[hint_key] = amove;
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
        if (all_moves_refuted && !position_is_refuted_axiomatically(s, nb)) {
            // Only a child that is itself Refuted keeps the claim alive. At
            // depth 1 no child is searched, so nothing below is Refuted and the
            // claim lapses -- which is correct: "no mate in 1" is not "no mate".
            if (depth <= 1) {
                all_moves_refuted = false;
            }
        }
        if (depth > 1) {
            Proof all_replies = prove_defender(s, nb, depth - 1);
            if (s.aborted) {
                return {};
            }
            if (!all_replies.refuted) {
                all_moves_refuted = false;
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
                store_exact_proof_table(s, key, depth, proof);
                return proof;
            }
            if (s.debug) {
                std::cerr << "attacker_move_failed depth=" << depth << " move=" << move_uci(amove)
                          << " fen=" << fen4(nb) << "\n";
            }
        }
    }
    Proof failed;
    failed.refuted = all_moves_refuted;
    store_exact_proof_table(s, key, depth, failed);
    return failed;
}

} // namespace mateprover

#endif // MATEPROVER_PROVE_H_INCLUDED
