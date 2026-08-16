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
inline bool position_is_refuted_axiomatically(const Search& s, const Board& b, int moves);

// The shared reachability/coverage bound. Defined below, next to the helpmate
// caller; declared here because the selfmate node routines come first.
inline bool mate_out_of_reach(const Board& b, Color mating,
                              Color mated, int our_moves, int their_moves);

inline bool piece_attacks_square(const Board& b, int from, int target);

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
        s.threat_ctx->attacker_proofs.resize(s.hint_entries);
        s.threat_ctx->defender_refutations.resize(s.hint_entries);
        s.threat_ctx->tt.shed_divisor = s.tt_shed_divisor;
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
    const int forcing = s.forcing_mode;
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
    if (cm == 0 && !needs_child && threat == 0 && forcing == 0) {
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
        // Forcing lanes for a capture quota. Like bits 8 and 16, a move that
        // ENDS the game is exempt: discarding the winning move because it is
        // quiet is the bug that cost six call sites when x-capture landed.
        if (forcing != 0) {
            const bool is_capture = b.sq[m.to] != '.' || m.ep;
            const bool forced = is_capture ||
                                (forcing == 2 && move_gives_check_fast(b, m));
            if (!forced) {
                const Board probe = make_move(b, m);
                if (has_legal_move(probe, s.move_reserve, s.move_reserve_capacity, s.static_pseudo)) {
                    return true;
                }
            }
        }
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

// A failure that holds at every depth, not merely the one requested. Large
// enough that no real stipulation reaches it, small enough that the repeated
// +1 as it propagates upward cannot overflow an int.
constexpr int kFailAnyDepth = 1 << 20;


// The variant terminal, read against the goal in force.
//
// The rule ends the GAME. Whether that ending SATISFIES THE STIPULATION is a
// separate question, and the answer differs by goal -- which is the whole reason
// x-check is a variant here rather than a seventh goal:
//
//   mate            The attacker is asked to force a win, and under these rules
//                   filling his quota is a win. So his last check -- or last
//                   capture -- proves the goal. `--no-check-win` and
//                   `--no-capture-win` ask for checkmate specifically instead,
//                   for the problemist who means mate.
//
//   everything else Stalemate, selfmate, selfstalemate and the cooperative goals
//                   each name a terminal POSITION. A game that ended by check
//                   count did not reach it. The line is DEAD rather than merely
//                   out of depth -- nothing follows a finished game at any
//                   depth -- so it reports kFailAnyDepth, exactly as the
//                   "attacker has no legal move and this is not the goal" case
//                   already does a few lines below.
//
// Deliberately NOT setting `refuted`. That field is GAP-1's, it composes upward,
// and it means "no solution from this POSITION at any depth". This is a
// statement about a finished game, which is stronger but differently shaped, and
// the depth field already carries everything the parent needs.
inline bool variant_terminal(const Search& s, const Board& b, Proof& out) {
    if (!variant_active(b)) {
        return false;
    }
    const VariantWin won = variant_winner(b);
    if (won.side < 0) {
        return false;
    }
    // THE ORDINARY TERMINAL WINS THE TIE. A move can be checkmate and the final
    // check at once, and when it is, the game ended by MATE -- which is the
    // question every stipulation here actually asks. Firing the check-count
    // terminal first would call that line dead and lose a real solution, silently
    // and only on the positions where both rules bite.
    //
    // Every stipulated terminal in this engine is "the side to move has no legal
    // move, and is or is not in check". So it is enough to defer whenever there
    // is no legal move and let the routine that owns the goal decide. The test
    // is paid only when someone has already exhausted an allowance, which is the
    // last move of a check-win line and nowhere else.
    if (!has_legal_move(b, s.move_reserve, s.move_reserve_capacity,
                        s.static_pseudo)) {
        return false;
    }
    out = Proof{};
    if (s.goal == Goal::Mate && s.rule_wins[won.rule] &&
        won.side == static_cast<int>(s.attacker)) {
        out.ok = true;
        if (s.emit_proof) {
            out.cert = std::string("{\"") + variant_win_key(won.rule) + "\":true}";
        }
        return true;
    }
    out.fail_depth = kFailAnyDepth;
    return true;
}


// The selfmate attacker-rejection test.
//
// The claim: an attacker move at selfmate depth 1 is refuted if, after it, the
// defender KING has a legal move that does not give check -- a king move is
// never itself a check, so such a move is a legal non-mating reply and the
// "every defender move mates" requirement fails immediately.
//
// Two implementations of one predicate, and they must agree exactly.
//
// The reference below makes each king move and looks. It was written as an
// observer, where being obviously right mattered more than being fast, and it
// shipped in that form because it won six positions in that form. Then the
// counters said what it costs: 320 MILLION calls across sixty selfmate
// positions, rejecting 84.9% of them. A predicate consulted that often and
// answering yes that often is not a heuristic any more, it is the inner loop.
//
// So the fast path answers the same question with attack queries instead of
// board copies, and falls back to the reference whenever it cannot be certain:
//
//   no legal king move        exact, and the answer is no. One attack query per
//                             neighbour, no move executed.
//   no discovery available    lifting the defender king off the board leaves the
//                             attacker king unattacked, so NO king move can give
//                             check, so any legal one is a witness. Exact, and
//                             one further attack query.
//   otherwise                 a discovery is possible for some king move but not
//                             necessarily for the one we would use -- a move
//                             ALONG the discovered line discovers nothing -- so
//                             defer to the reference rather than guess.
//
// The fallback is what keeps this honest. Guessing in the yes direction rejects
// an attacker move that may be a solution, and that failure is silent.
inline bool defender_has_quiet_king_move_reference(const Board& nb, Color attacker) {
    const Color defender = other(attacker);
    const int dk = nb.king_sq[defender];
    if (dk < 0) return false;
    const SquareList& l = king_table()[dk];
    for (int i = 0; i < l.count; ++i) {
        const int to = l.sq[i];
        if (nb.by_color[defender] & (1ull << to)) continue;   // own piece
        Move km;
        km.from = dk;
        km.to = to;
        if (!move_is_legal(nb, km)) continue;
        const Board rb = make_move(nb, km);
        if (!in_check(rb, attacker)) {
            return true;            // legal, and does not mate -- a witness
        }
    }
    return false;
}

inline bool defender_has_quiet_king_move(const Board& nb, Color attacker,
                                         Stats& stats, bool fast) {
    const Color defender = other(attacker);
    if (nb.king_sq[defender] < 0) return false;
    if (fast) {
        if (!king_has_legal_move(nb, defender)) {
            ++stats.d1_reject_fast;
            return false;
        }
        if (!king_move_could_discover_check(nb, defender)) {
            ++stats.d1_reject_fast;
            return true;
        }
        ++stats.d1_reject_slow;
    }
    return defender_has_quiet_king_move_reference(nb, attacker);
}


Proof prove_selfmate_attacker(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    Proof check_end;
    if (variant_terminal(s, b, check_end)) {
        return check_end;
    }
    ++s.stats.attacker_nodes;
    if (position_is_refuted_axiomatically(s, b, depth)) {
        Proof out;
        out.refuted = true;
        // A refutation holds at EVERY depth, so it is the strongest failure
        // there is. Reporting it as an ordinary depth-limited failure -- which
        // is what a zero here means -- discards that at the first parent.
        out.fail_depth = kFailAnyDepth;
        return out;
    }
    if (b.stm != s.attacker) {
        return {};
    }
    // PREFETCH the table slot this node will probe, ~50 lines below, so the miss
    // overlaps the move generation in has_legal_move rather than stalling after
    // it. Issued here rather than beside the probe because a prefetch with no
    // work behind it hides nothing.
    //
    // The cost is a wasted key construction at nodes that return before the
    // probe -- a mated node, or one at depth 0. Whether that costs more than the
    // overlap saves is a question for a measurement, not an argument; see 127.
    if (s.tt_prefetch) {
        s.tt.prefetch(tt_key(b, 0, 'A', s.attacker, s.goal));
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
        // The attacker has no legal move and the position is not the goal, so
        // this line is OVER -- not merely out of depth. Nothing follows it at any
        // depth, which makes this one of the two places in the selfmate
        // recursion where a failure is genuinely unbounded. Both reported zero,
        // and because the attacker node takes the MINIMUM over its moves, a
        // single zero at the bottom pins every ancestor to exactly the depth it
        // was asked for. That is why the disproof-excess histogram sat at 100%
        // in bucket zero while the propagation above it was already correct.
        Proof dead;
        dead.fail_depth = kFailAnyDepth;
        return dead;
    }
    // See docs/SELFMATE_REACH_DERIVATION.md. The roles invert here: the side
    // that must DELIVER mate is the defender, and the side mated is the
    // attacker. At an attacker node both still have `depth` moves.
    //
    // Placed after the terminal test above, so a position that is ALREADY
    // selfmate is reported before any bound can look at it.
    if (s.goal == Goal::Selfmate && s.selfmate_bound &&
        !variant_reachable_static(b, s.rule_wins, depth) &&
        mate_out_of_reach(b, other(s.attacker), s.attacker, depth, depth)) {
        ++s.stats.selfmate_unreachable_prunes;
        return {};
    }
    // Proven disproof depth for this node: the MINIMUM over the attacker's
    // moves, since the attacker needs only one of them to work and the node
    // survives to whatever depth its best move does. The selfmate recursion
    // decrements across the DEFENDER edge, not this one, so no increment
    // belongs here -- the defender node it reaches carries the same depth.
    int sm_node_fail = 1 << 29;
    if (depth <= 0) {
        return {};
    }

    TTKey key = tt_key(b, 0, 'A', s.attacker, s.goal);
    Proof cached;
    if (probe_exact_proof_table(s, key, depth, cached)) {
        return cached;
    }

    // THE SELFMATE NODE EXIT, the same analysis as the direct-mate coverage
    // exit read the other way round. Where the per-move rejection test below
    // asks "does THIS move leave the defender king a quiet step" once per move
    // and pays a board copy to answer, this asks "does EVERY move leave it one"
    // once per node and pays nothing per move. When it holds, the node is
    // finished before a move exists.
    // Stood down while its observer runs, for the same reason as the coverage
    // exit above: an exit that consumes the nodes its observer counts makes the
    // observer report zero.
    if (s.selfmate_node_exit && !s.selfmate_node_observer && depth == 1 &&
        s.goal == Goal::Selfmate) {
        ++s.stats.selfmate_node_probes;
        if (selfmate_node_refuted_by_escape(b, s.attacker)) {
            ++s.stats.selfmate_node_exits;
            return {};
        }
    }

    bool scored = false;
    auto moves = generate_ordered_moves(s, b, scored);
    // OBSERVER for the exit above, sited after generation so it can report the
    // moves that would never have been produced. Q1 without Q2 is what section
    // 88 measured, and section 88 is why both are counted here.
    if (s.selfmate_node_observer && depth == 1 && s.goal == Goal::Selfmate) {
        ++s.stats.selfmate_node_probes;
        if (selfmate_node_refuted_by_escape(b, s.attacker)) {
            ++s.stats.selfmate_node_exits;
            s.stats.selfmate_node_moves_saved += moves.size();
        }
    }
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
        Move hint_move;
if (s.attacker_proofs.probe(hint_key, hint_move)) {
            if (move_to_front(moves, hint_move)) {
                ++s.stats.proof_hint_hits;
            }
        }
    }

    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        const Board nb = make_move(b, amove);
        // ATTACKER REJECTION at selfmate depth 1.
        //
        // A selfmate in one requires EVERY legal defender reply to checkmate the
        // attacker. So one legal reply that does not mate refutes the move, and
        // the cheapest witness is a defender KING move: a king move is never
        // itself a check, so any flight square the king can reach without
        // discovering check is a legal non-mating reply.
        //
        // Exact rather than approximate, so it is sound by construction --
        // unlike the board-free geometry, which must UNDER-estimate the flight
        // set because over-estimating it rejects a real solution silently.
        // What it saves is the defender node: generating the whole reply list to
        // consume one entry of it, replaced by a walk of eight king neighbours.
        // SELFMATE ONLY. The witness is "a king move cannot be checkmate", which
        // is true for a mate goal and false for a stalemate one -- a quiet king
        // move is exactly what MIGHT stalemate the attacker, so under
        // selfstalemate this rejects real solutions. The suite caught it on
        // b7/8/8/6p1/6P1/1RQ3PK/k6P/8 within seconds of the default being
        // flipped, which is the failure mode the specification warns about:
        // silent loss of a solution, correct-looking everywhere else.
        if ((s.reject_observer || s.attacker_reject) && depth == 1 &&
            s.goal == Goal::Selfmate) {
            ++s.stats.d1_attacker_moves;
            const bool witness = defender_has_quiet_king_move(
                nb, s.attacker, s.stats, s.fast_reject);
            if (witness) {
                ++s.stats.d1_would_reject;
                if (s.attacker_reject) {
                    continue;             // refuted without touching the subtree
                }
            }
        }
        Proof replies = prove_selfmate_defender(s, nb, depth);
        if (!replies.ok && replies.fail_depth > 0 && replies.fail_depth < sm_node_fail) {
            sm_node_fail = replies.fail_depth;
        }
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
                s.attacker_proofs.store(hint_key, amove);
            }
            return proof;
        }
    }
    Proof failed;
    failed.fail_depth = (sm_node_fail >= (1 << 29)) ? depth : std::max(depth, sm_node_fail);
    store_exact_proof_table(s, key, depth, failed);
    return failed;
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
inline bool selfmate_perpetual_check(const Search& s, const Board& root) {
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


// Two-ply attacker-width estimate, used to order the defender's replies.
//
// The defender prefers the reply leaving the ATTACKER least room. That is not
// about refuting sooner -- profiling showed the first reply already refutes at
// essentially every node -- but about WHICH refutation is taken. A reply that
// walks into a position hopeless for the attacker several levels shallower
// returns a much larger proven failure depth, and that surplus is what level
// skipping and the levels above consume. Two replies that both refute at the
// current depth are not equally valuable, and nothing in the old node
// distinguished them.
//
// This is a heuristic and nothing else depends on it: it changes the order
// replies are tried, never which of them refutes. The differential test is that
// verdicts are identical with it on and off while node counts are not, which is
// what `--no-answer-order` exists for.
//
// Deliberately NOT special-cased for checking replies. A check narrows the
// attacker's next level so sharply that a naive width would rank every check as
// excellent, and many checks are bad. Counting all surviving attacker units,
// without asking which are pinned by the check, over-estimates the width of a
// checking reply -- the conservative direction, and the one that avoids the
// failure mode.
inline int attacker_width_estimate(const Board& rb, Color attacker) {
    // A mobility proxy per surviving unit. Exact move counts would cost a
    // generation per reply, which is the expense this path exists to avoid; the
    // weights only have to rank, not to measure.
    static constexpr int kWeight[5] = {2, 5, 6, 7, 9};   // P N B R Q
    int width = 0;
    std::uint64_t men = rb.by_color[attacker];
    while (men) {
        const int sq = lsb_index(men);
        men &= men - 1;
        const std::uint64_t bit = 1ull << sq;
        for (int t = 0; t < 5; ++t) {
            if (rb.by_type[t] & bit) {
                width += kWeight[t];
                break;
            }
        }
    }
    // King escapes, counted exactly rather than proxied. This is the term that
    // decides whether the attacker is close to having no choice at all, which is
    // exactly the state a selfmate attacker is trying to reach and a defender is
    // trying to avoid, so it is worth eight attack queries.
    const int k = rb.king_sq[attacker];
    if (k >= 0) {
        const int kf = k % 8;
        const int kr = k / 8;
        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0) continue;
                const int nf = kf + df;
                const int nr = kr + dr;
                if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
                const int to = nf + nr * 8;
                if (rb.by_color[attacker] & (1ull << to)) continue;   // self-blocked
                if (!is_attacked(rb, to, other(attacker))) width += 3;
            }
        }
    }
    return width < 1 ? 1 : width;
}


// Does the piece standing on `from` attack `target` on this board? Geometry plus
// occupancy; used by the depth-2 scorer to identify checking pieces and to ask
// whether a checker can be taken.
inline bool piece_attacks_square(const Board& b, int from, int target) {
    if (from == target) return false;
    const std::uint64_t bit = 1ull << from;
    int pt = PT_NONE;
    for (int i = 0; i < 6; ++i) {
        if (b.by_type[i] & bit) { pt = i; break; }
    }
    if (pt == PT_NONE) return false;
    if (pt == PT_KNIGHT || pt == PT_KING) {
        const SquareList& l = (pt == PT_KNIGHT) ? knight_table()[from] : king_table()[from];
        for (int i = 0; i < l.count; ++i) if (l.sq[i] == target) return true;
        return false;
    }
    if (pt == PT_PAWN) {
        const Color us = (b.by_color[WHITE] & bit) ? WHITE : BLACK;
        const int f = file_of(from), r = rank_of(from);
        const int dr = (us == WHITE) ? 1 : -1;
        for (int df = -1; df <= 1; df += 2) {
            if (on_board(f + df, r + dr) && square_of(f + df, r + dr) == target) return true;
        }
        return false;
    }
    const int first = (pt == PT_BISHOP) ? 4 : 0;
    const int last = (pt == PT_ROOK) ? 4 : 8;
    const auto& rays = ray_table();
    for (int dir = first; dir < last; ++dir) {
        const SquareList& ray = rays[dir][from];
        for (int i = 0; i < ray.count; ++i) {
            const int sq = ray.sq[i];
            if (sq == target) return true;
            if (b.occ & (1ull << sq)) break;
        }
    }
    return false;
}

inline bool squares_adjacent(int a, int c) {
    const int df = file_of(a) - file_of(c);
    const int dr = rank_of(a) - rank_of(c);
    return a != c && df >= -1 && df <= 1 && dr >= -1 && dr <= 1;
}

// How many of a king's eight neighbours are neither self-occupied nor attacked.
inline int safe_neighbour_count(const Board& b, int king_sq, Color us) {
    if (king_sq < 0) return 0;
    int n = 0;
    const SquareList& l = king_table()[king_sq];
    for (int i = 0; i < l.count; ++i) {
        const int to = l.sq[i];
        if (b.by_color[us] & (1ull << to)) continue;
        if (!is_attacked(b, to, other(us))) ++n;
    }
    return n;
}

// The depth-2 answer scorer: additive, higher is better for the defender, no
// product and no subtree model. A different shape from the width estimator, not
// a cheaper approximation of it.
//
// EVERY constant here is mine. The specification records that the original's
// author marks his own values as underived, fitted empirically against 1990s
// search behaviour; adopting them would import a fit to a different engine. The
// relative ORDERING of the check bonuses is the specified part and is preserved:
// double-and-near > double > single-and-near > single. Magnitudes are set
// commensurate with the material scale, so that a double check outweighs winning
// a queen and a bare single check does not.
inline int depth2_answer_score(const Board& b, const Board& rb, const Move& r,
                               Color attacker, int defender_in_check_safe_origin) {
    static constexpr int kValue[6] = {100, 320, 330, 500, 900, 0};   // P N B R Q K
    int score = 0;

    const int aking = rb.king_sq[attacker];
    if (aking >= 0 && is_attacked(rb, aking, other(attacker))) {
        int checkers = 0;
        int checker_sq = -1;
        std::uint64_t men = rb.by_color[other(attacker)];
        while (men) {
            const int sq = lsb_index(men);
            men &= men - 1;
            if (piece_attacks_square(rb, sq, aking)) {
                ++checkers;
                checker_sq = sq;
            }
        }
        const bool near = checker_sq >= 0 && squares_adjacent(checker_sq, aking);
        if (checkers >= 2) {
            score += near ? 900 : 700;
        } else {
            score += near ? 400 : 250;
        }
        // A checker the attacker can simply take with something other than his
        // king is worth much less. The king's own capture is excluded because it
        // is already priced by the escape count.
        if (checkers == 1 && checker_sq >= 0) {
            std::uint64_t theirs = rb.by_color[attacker];
            while (theirs) {
                const int sq = lsb_index(theirs);
                theirs &= theirs - 1;
                if (sq == aking) continue;
                if (piece_attacks_square(rb, sq, checker_sq)) {
                    score -= 200;
                    break;
                }
            }
        }
    }

    // Material the reply removes.
    const std::uint64_t tobit = 1ull << r.to;
    if (b.by_color[attacker] & tobit) {
        for (int i = 0; i < 6; ++i) {
            if (b.by_type[i] & tobit) { score += kValue[i]; break; }
        }
    }

    // When the defender is himself in check, how the reply changes his own
    // king's room. The origin count is invariant across replies and is passed in
    // rather than recomputed.
    if (defender_in_check_safe_origin >= 0) {
        const Color defender = other(attacker);
        const int dking = rb.king_sq[defender];
        if (r.from == b.king_sq[defender]) {
            score += 60 * (safe_neighbour_count(rb, dking, defender) -
                           defender_in_check_safe_origin);
        } else {
            if (dking >= 0 && squares_adjacent(r.to, dking)) score += 40;
            if (dking >= 0 && squares_adjacent(r.from, dking)) score -= 40;
        }
    }
    return score;
}

Proof prove_selfmate_defender(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    // Variant: has the game already ended outright? Tested at every node
    // entry rather than only where a move is made, so it also catches a ROOT
    // position handed in already finished.
    Proof check_end;
    if (variant_terminal(s, b, check_end)) {
        return check_end;
    }
    ++s.stats.defender_nodes;
    if (selfmate_perpetual_check(s, b)) {
        ++s.stats.perpetual_refutations;
        Proof out;
        out.refuted = true;
        return out;
    }
    // The mated side has depth-1 moves here: the attacker has already spent his
    // to arrive at this node. Overstating it would be safe, understating it
    // would not, so it is written out rather than approximated.
    if (s.goal == Goal::Selfmate && s.selfmate_bound &&
        !variant_reachable_static(b, s.rule_wins, depth) &&
        mate_out_of_reach(b, other(s.attacker), s.attacker, depth, depth - 1)) {
        ++s.stats.selfmate_unreachable_prunes;
        return {};
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

    // LAZY REPLY SCAN. This used to build a fully legality-filtered reply list
    // and then, on a disproof, consume exactly one entry of it. Profiled on a
    // depth-5 selfmate disproof: 31.0 legal replies generated per node against
    // 1.00 searched -- 809 million generations to use 26 million. Legality is a
    // make-move plus an in-check test, so that discarded filter was the largest
    // single cost in the search.
    //
    // The scan below interleaves instead: test one pseudo-legal move for
    // legality, search it, stop the moment it refutes. Legal replies are still
    // visited in gen_pseudo order with illegal ones skipped, which is exactly
    // the sequence the filtered list produced, so this cannot change which reply
    // refutes first, the PV, or any verdict. It is a pure cost change.
    ++s.stats.defender_move_lists;

    bool any_legal = false;
    bool defence_survives = false;
    int survived_to = depth;

    // The reported depth must be the WORST line, not the first one. Taking the
    // first reply's variation understated it whenever another defence held out
    // longer, and the independent verifier caught exactly that: "certificate
    // proves mate in 6, reported 2". A selfmate in N is a claim about every
    // defence, so the PV has to be the longest of them.
    std::vector<Move> pv;
    std::vector<std::string> branch_certs;

    // One reply's worth of work, shared by the ordered and unordered paths.
    // Returns true when the scan should stop.
    auto try_reply = [&](const Move& r) -> bool {
        any_legal = true;
        ++s.stats.defender_moves;
        ++s.stats.defender_replies_tried;
        const Board rb = make_move(b, r);
        Proof child = prove_selfmate_attacker(s, rb, depth - 1);
        if (s.aborted || !child.ok) {
            // The decrement lives on this edge, so the increment does too: the
            // subtree sat at depth-1 and reaching it cost this move.
            defence_survives = true;   // one surviving defence refutes the line
            survived_to = std::max(depth, child.fail_depth + 1);
            return true;
        }
        if (child.pv.size() + 1 > pv.size()) {
            pv.clear();
            pv.push_back(r);
            pv.insert(pv.end(), child.pv.begin(), child.pv.end());
        }
        if (s.emit_proof) {
            branch_certs.push_back("{\"r\":" + json_quote(move_uci(r)) + ",\"p\":" + child.cert + "}");
        }
        return false;
    };

    auto scan = [&](const auto& pseudo) {
        for (const Move& r : pseudo) {
            ++s.stats.defender_legality_tests;
            if (!move_is_legal(b, r)) {
                continue;
            }
            if (try_reply(r)) {
                return;
            }
        }
    };

    // Ordering needs the whole list, and the lazy scan exists precisely to avoid
    // building it, so the two are banded rather than combined: below remaining
    // depth 2 there is no subtree left for ordering to shape and the lazy scan
    // runs, at 2 and above the list is materialised and sorted. That band is
    // also where the lazy scan was worth least, so little is given up.
    if (s.answer_order && depth >= s.answer_order_min_depth) {
        std::vector<Move> pseudo;
        if (s.move_reserve) pseudo.reserve(s.move_reserve_capacity);
        gen_pseudo(b, pseudo);
        std::vector<std::pair<int, Move>> scored;
        scored.reserve(pseudo.size());
        // At remaining depth exactly 2 an additive scorer may run instead of the
        // width estimator -- a different shape, not a cheaper approximation, and
        // what the original dispatches to in that band. Scores there are "higher
        // is better", so they are negated to keep one ascending selection below.
        const bool use_depth2 = s.depth2_scorer && depth == 2;
        int safe_origin = -1;
        if (use_depth2 && in_check(b, b.stm)) {
            safe_origin = safe_neighbour_count(b, b.king_sq[b.stm], b.stm);
        }
        for (const Move& r : pseudo) {
            ++s.stats.defender_legality_tests;
            if (!move_is_legal(b, r)) {
                continue;
            }
            const Board rb = make_move(b, r);
            scored.emplace_back(
                use_depth2 ? -depth2_answer_score(b, rb, r, s.attacker, safe_origin)
                           : attacker_width_estimate(rb, s.attacker),
                r);
        }
        if (scored.size() >= 2) {
            ++s.stats.answer_orderings;
        }
        // SELECTION, NOT SORT. The scan consumes 1.00 replies per node on
        // measurement, so sorting the whole list and then taking its head does
        // n log n work to use one element. Taking the minimum, and looking for
        // the next only when the current one fails to refute, is linear in the
        // common case and identical in outcome: scanning ascending with a strict
        // comparison resolves ties to the earliest generated reply, which is
        // what a stable sort did and what keeps the search deterministic.
        std::vector<bool> taken(scored.size(), false);
        for (std::size_t n = 0; n < scored.size(); ++n) {
            std::size_t best = scored.size();
            for (std::size_t i = 0; i < scored.size(); ++i) {
                if (taken[i]) continue;
                if (best == scored.size() || scored[i].first < scored[best].first) {
                    best = i;
                }
            }
            if (best == scored.size()) break;
            taken[best] = true;
            if (try_reply(scored[best].second)) break;
        }
    } else if (s.static_pseudo) {
        MoveList fixed;
        gen_pseudo(b, fixed);
        if (!fixed.overflow) {
            scan(fixed);
        } else {
            std::vector<Move> spill;
            if (s.move_reserve) spill.reserve(s.move_reserve_capacity);
            gen_pseudo(b, spill);
            scan(spill);
        }
    } else {
        std::vector<Move> pseudo;
        if (s.move_reserve) pseudo.reserve(s.move_reserve_capacity);
        gen_pseudo(b, pseudo);
        scan(pseudo);
    }

    if (defence_survives) {
        // The diagnostic the selfmate answer specification asks for first: how
        // much MORE than the request this refutation proved. Bucket 0 dominating
        // means every refutation is worth exactly what was asked for, which is
        // the signature of an AND node with no preference among refutations.
        const int excess = survived_to - depth;
        if (excess <= 0)      ++s.stats.disproof_excess_0;
        else if (excess == 1) ++s.stats.disproof_excess_1;
        else if (excess == 2) ++s.stats.disproof_excess_2;
        else if (excess == 3) ++s.stats.disproof_excess_3;
        else if (excess == 4) ++s.stats.disproof_excess_4;
        else                  ++s.stats.disproof_excess_5plus;
        Proof out;
        out.fail_depth = survived_to;
        return out;
    }
    if (!any_legal) {
        // The defender is mated or stalemated. Either way he has not mated the
        // attacker, so this line fails.
        return {};
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
// Is a mate simply out of reach from here? Shared by the helpmate and selfmate
// bounds -- ONE implementation, because the two differ only in who mates and how
// many moves each side has left, and two copies of this reasoning would drift.
// See docs/HELPMATE_COVERAGE_DERIVATION.md and docs/SELFMATE_REACH_DERIVATION.md.
//
// A mate ends with the mated king in check and every flight handled. Two
// relaxations make that checkable in a few bitboard ANDs, and both only WIDEN
// what is allowed, so this can rule a subtree out but never rule one in:
//
//   the mated king ends within `their_moves` king-steps of where it stands,
//   measured on an empty board;
//   a mating unit attacks or occupies a square within `our_moves` of its own
//   moves, again on an empty board, captures and promotion included.
//
// Returns true when NO mate is possible, so the caller may drop the subtree.
// It is depth-bounded and must never be turned into an any-depth refutation: the
// whole argument rests on the moves remaining, and a deeper search has more.
inline bool mate_out_of_reach(const Board& b, Color mating,
                              Color mated, int our_moves, int their_moves) {
    // The table stops at three of the mating side's moves. Beyond that it would
    // UNDERSTATE reach, which is the one way this becomes unsound. Extending it
    // to five was measured at 74 and rejected: the nodes it newly covers are
    // shallow, which are few, and testing them costs more than the prune returns.
    if (our_moves < 1 || our_moves > 3 || their_moves < 0) {
        return false;
    }
    const int king_sq = b.king_sq[mated];
    if (king_sq < 0) {
        return false;
    }
    const auto& tab = empty_board_reach();
    const auto& disc = king_disc_table();
    const std::size_t w = static_cast<std::size_t>(our_moves);
    const bool their_reach_unbounded = their_moves > 5;
    const std::size_t bm = static_cast<std::size_t>(their_moves > 5 ? 5 : their_moves);

    auto piece_type_at = [&](int sq) {
        for (int t = 0; t < 6; ++t) {
            if (b.by_type[static_cast<std::size_t>(t)] & (1ull << sq)) return t;
        }
        return static_cast<int>(PT_NONE);
    };

    // A(w): only a NON-KING unit can give the check. U(w) and the occupancy
    // reaches go into `handled`, where the mating king DOES belong -- it cannot
    // check but it can cover a flight square.
    std::uint64_t check_sources = 0;
    std::uint64_t handled = b.occ;
    std::uint64_t men = b.by_color[mating];
    while (men) {
        const int from = lsb_index(men);
        men &= men - 1;
        const int pt = piece_type_at(from);
        if (pt == PT_NONE) continue;
        const std::size_t slot = static_cast<std::size_t>(mating * 6 + pt);
        const std::uint64_t atk = tab.attack[slot][static_cast<std::size_t>(from)][w];
        handled |= atk;
        handled |= tab.reach[slot][static_cast<std::size_t>(from)][w];
        if (pt != PT_KING) check_sources |= atk;
    }
    // The mated side can self-block a flight by standing on it -- occupancy only,
    // since what it attacks says nothing about where its own king may step.
    std::uint64_t theirs = b.by_color[mated];
    while (theirs) {
        const int from = lsb_index(theirs);
        theirs &= theirs - 1;
        const int pt = piece_type_at(from);
        if (pt == PT_NONE) continue;
        handled |= their_reach_unbounded
                       ? ~0ull
                       : tab.reach[static_cast<std::size_t>(mated * 6 + pt)]
                                  [static_cast<std::size_t>(from)][bm];
    }

    std::uint64_t candidates =
        disc[static_cast<std::size_t>(king_sq)]
            [static_cast<std::size_t>(their_moves > 8 ? 8 : their_moves)] & check_sources;
    while (candidates) {
        const int k = lsb_index(candidates);
        candidates &= candidates - 1;
        const std::uint64_t flights = disc[static_cast<std::size_t>(k)][1] & ~(1ull << k);
        if ((flights & ~handled) == 0) {
            return false;               // this square could still be a mate
        }
    }
    return true;
}

Proof prove_help(Search& s, const Board& b, int plies) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    // Variant: has the game already ended outright? Tested at every node
    // entry rather than only where a move is made, so it also catches a ROOT
    // position handed in already finished.
    Proof check_end;
    if (variant_terminal(s, b, check_end)) {
        return check_end;
    }
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

    // See mate_out_of_reach. In a helpmate the side to move at the END is the
    // one mated, which parity decides from here.
    if (s.goal == Goal::Helpmate && s.help_bound) {
        const Color mated = (plies % 2 == 0) ? b.stm : other(b.stm);
        const Color mating = other(mated);
        const int our_moves = (mating == b.stm) ? (plies + 1) / 2 : plies / 2;
        if (!variant_reachable_static(b, s.rule_wins, plies) &&
            mate_out_of_reach(b, mating, mated, our_moves, plies - our_moves)) {
            ++s.stats.help_unreachable_prunes;
            return {};
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
        if (last_ply_prune && last_ply_win_needs_check(s.rule_wins, b, s.attacker) &&
            !move_can_reach_goal(m.score, s.goal)) {
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
        if (last_ply_prune && last_ply_win_needs_check(s.rule_wins, b, s.attacker) &&
            !move_can_reach_goal(m.score, s.goal)) {
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

// ---------------------------------------------------------------------------
// Split points: one node's children, opened up so that idle workers can take
// them. Shared by both split shapes, because the COORDINATION is identical and
// only the composition differs -- and the coordination is the part that is
// difficult to get right twice.
//
// The two shapes, and the one rule that separates them:
//
//   AND node (defender replies). The attacker needs the goal after every
//   reply, so a PROOF needs every child and a DISPROOF needs one. The node is
//   settled by the first child that FAILS.
//
//   OR node (attacker moves). The attacker needs one move that works, so a
//   PROOF needs one child and a DISPROOF needs every one. The node is settled
//   by the first child that SUCCEEDS.
//
// "Settled" always means the lowest such index, which is exactly where the
// sequential loop would have stopped. Children above it are never claimed and
// children below it are still worth finishing, since a lower index would be
// preferred if it also settles. That single rule makes both splits report the
// same move, the same line and the same certificate as the sequential search,
// whatever order the workers happen to finish in. See architecture 111 and 112.
enum : std::uint8_t {
    kChildUnresolved = 0,
    kChildProved = 1,
    kChildFailed = 2,
    kChildIllegal = 3,      // lazy generation: pseudo-legal move, king left en prise
    kChildAbandoned = 4,    // cancelled or out of time: NO verdict, by the abort invariant
    kChildImmediate = 5,    // OR node: won outright, `results` already composed
};

// Every mutable field is guarded by the registry mutex. The critical sections
// are a few instructions and are entered once per child SUBTREE, so the lock is
// never on a hot path.
struct NodeSplit {
    Board board{};
    int depth = 0;              // the splitting node's own remaining depth
    int slot = 0;               // registry slot, which is the owner's worker index
    int priority = 0;           // root move being served; helpers prefer the lowest
    bool conjunctive = true;    // AND node (replies) or OR node (attacker moves)
    bool lazy = false;          // AND only: children are pseudo-legal
    std::vector<Move> moves;
    std::vector<Proof> results;
    std::vector<std::uint8_t> state;
    int next = 0;               // lowest unclaimed child
    int active = 0;             // claims in flight, the owner's included
    int settled_at = -1;
    bool settle_refuted = false;    // AND: the settling child's GAP-1 `refuted`
    bool settle_is_check = false;   // AND: fatal-anti-check observer only
    bool open = false;
    int min_before_help = 0;    // AND: replies the owner must have proved first
    // Publication order, from a monotonic counter. This is what makes it safe
    // for a BLOCKED OWNER to go and help somebody else; see claim_any_child.
    std::uint64_t seq = 0;
};

struct SplitRegistry {
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::shared_ptr<NodeSplit>> open_splits;   // one slot per worker
    int live_roots = 0;         // workers still inside a root move
    const std::atomic<int>* best_index = nullptr;
    // DEMAND. Read without the lock by every eligible node in the search, so
    // both are atomics rather than guarded fields: `idle` counts workers sitting
    // in the helper loop with nothing to do, `open` counts split points already
    // published and still offering children.
    //
    // These exist because 112 shipped the wrong half of young brothers wait. The
    // concept has two conditions -- wait for the eldest child, AND split only if
    // another processor is idle -- and only the first was implemented. Splitting
    // at a fixed ply satisfies neither: it publishes whether or not anyone is
    // free to help, so `--or-split-plies 2` created a split point inside every
    // ply-3 task, twenty of them against a handful of idle workers, and turned
    // most of the pool into owners blocked at their own tails. That measured
    // 7.07 s against 6.25 s and was read as coordination cost. It was not
    // coordination cost. It was supply with no demand.
    std::atomic<int> idle{0};
    std::atomic<int> open{0};
    // Hands out NodeSplit::seq. Monotonic, and the ordering it induces is the
    // whole of the termination argument for owners helping while they wait.
    std::atomic<std::uint64_t> next_seq{1};
};

// May a node publish its remaining children? Only if somebody is waiting for
// them, and only if the workers already waiting are not already spoken for by
// split points that exist. Two relaxed loads, on a path taken once per child at
// eligible depths.
inline bool split_is_wanted(const SplitRegistry& reg) {
    return reg.idle.load(std::memory_order_relaxed) >
           reg.open.load(std::memory_order_relaxed);
}

struct NodeClaim {
    std::shared_ptr<NodeSplit> split;
    int index = -1;
};

// Take the next child of `sp`, or -1 if there is none worth taking.
//
// `next >= settled_at` rather than `settled_at >= 0`: a child BELOW the settling
// index would still be preferred if it settled too, so it is real work, while
// one above it can never be chosen and is not. Claims ascend, so this one test
// covers both.
int claim_child(SplitRegistry& reg, NodeSplit& sp) {
    std::lock_guard<std::mutex> lock(reg.m);
    if (!sp.open || sp.next >= static_cast<int>(sp.moves.size()) ||
        (sp.settled_at >= 0 && sp.next >= sp.settled_at)) {
        return -1;
    }
    const int j = sp.next++;
    ++sp.active;
    return j;
}

// Find work for an idle worker: the open node with the lowest priority -- the
// root move the sequential search would have reached first -- that still has a
// child to give.
// `min_seq` is the sequence number of the newest split the CALLER owns, or 0
// for a worker that owns none. A caller may only take children from splits
// published AFTER its own, and that one restriction is what makes it safe for
// an owner to help rather than idle while it waits for its own helpers.
//
// THE TERMINATION ARGUMENT, written out because the failure mode is a hang, and
// a hang in a prover is worse than most programs' wrong answers.
//
// Let worker X be blocked on the split S it owns, with seq(S) = x. Every
// outstanding child of S is held by some worker Y, and at the instant Y claimed
// it, Y owned nothing newer than S. If Y is in turn blocked, it is blocked on a
// split S' that it published AFTER that claim, so seq(S') > x -- strictly,
// because the counter only increases. Following "is waiting for" from any
// blocked worker therefore walks a strictly increasing sequence of integers.
//
// A strictly increasing walk over a finite set cannot revisit, so the chain has
// no cycle and must end; and it can only end at a worker that is NOT blocked,
// which is one that is computing. So some worker is always making progress and
// no set of workers can wait on one another in a ring. Stack depth is bounded
// by the same fact: each nested help owns a strictly newer split than its
// caller, and splits are finite in any one search.
NodeClaim claim_any_child(SplitRegistry& reg, std::uint64_t min_seq) {
    std::lock_guard<std::mutex> lock(reg.m);
    const int best = reg.best_index != nullptr
                         ? reg.best_index->load(std::memory_order_acquire)
                         : std::numeric_limits<int>::max();
    NodeSplit* chosen = nullptr;
    std::shared_ptr<NodeSplit>* chosen_ptr = nullptr;
    for (std::shared_ptr<NodeSplit>& sp : reg.open_splits) {
        if (!sp || !sp->open) {
            continue;
        }
        // A root move a lower index has already beaten cannot be the answer.
        if (sp->priority > best) {
            continue;
        }
        if (sp->next >= static_cast<int>(sp->moves.size()) ||
            (sp->settled_at >= 0 && sp->next >= sp->settled_at)) {
            continue;
        }
        // THE GATE, and on an AND node it is the whole difference between the
        // mechanism paying and costing 2.7x the nodes (111).
        //
        // "Every reply must be proved, so no helper's work is speculative" is
        // true of a defender node that ends up PROVED and false of one that ends
        // up refuted -- and on a no-solution position every node is refuted.
        // Such a node stops at its first refuting reply, which the reply
        // ordering and the refutation-hint table between them make the first
        // reply most of the time. `next` is direct evidence about which kind of
        // node this is, because a settling child closes the node: past a couple
        // of proved replies it very likely needs all of them.
        //
        // An OR node needs no such gate. Young brothers wait already searched
        // the eldest child alone, which is where a proof would be if there were
        // one, so the rest are being shared out precisely when the node looks
        // like it is going to fail -- and a failing OR node needs every child.
        if (sp->next < sp->min_before_help) {
            continue;
        }
        // Strictly newer than anything the caller owns. A pure helper passes 0
        // and is unrestricted; a blocked owner is confined to splits that
        // cannot be waiting on it, which is what keeps the wait acyclic.
        if (sp->seq <= min_seq) {
            continue;
        }
        if (chosen == nullptr || sp->priority < chosen->priority) {
            chosen = sp.get();
            chosen_ptr = &sp;
        }
    }
    if (chosen == nullptr) {
        return {};
    }
    NodeClaim claim;
    claim.index = chosen->next++;
    ++chosen->active;
    claim.split = *chosen_ptr;
    return claim;
}

// Counts a worker in for as long as it is inside a root move. A helper that
// finds no work parks rather than going home while this is non-zero, because an
// owner that has not finished generating its children has nothing to offer yet
// and will in a moment.
//
// A guard rather than a pair of calls: the root loop leaves its body by
// `continue` from three places, and an undercount here would send every helper
// away at the exact moment the tail work appeared.
struct LiveRootGuard {
    SplitRegistry& reg;
    explicit LiveRootGuard(SplitRegistry& r) : reg(r) {
        std::lock_guard<std::mutex> lock(reg.m);
        ++reg.live_roots;
    }
    ~LiveRootGuard() {
        std::lock_guard<std::mutex> lock(reg.m);
        --reg.live_roots;
        reg.cv.notify_all();
    }
    LiveRootGuard(const LiveRootGuard&) = delete;
    LiveRootGuard& operator=(const LiveRootGuard&) = delete;
};

// Prove one child and record the outcome. Defined after prove_attacker, since
// it calls both searches; declared here because prove_attacker calls it.
void run_child(SplitRegistry& reg, NodeSplit& sp, int j, Search& ws);

Proof prove_defender(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    // Variant: has the game already ended outright? Tested at every node
    // entry rather than only where a move is made, so it also catches a ROOT
    // position handed in already finished.
    Proof check_end;
    if (variant_terminal(s, b, check_end)) {
        return check_end;
    }
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

    // History first, so that a position-keyed hint still wins the front slot:
    // an exact match on THIS position outranks a move that refuted elsewhere.
    if (s.defender_history_order && replies.size() > 1) {
        std::stable_sort(replies.begin(), replies.end(),
                         [&s](const Move& a, const Move& c) {
                             const std::size_t ia = static_cast<std::size_t>(a.from) * 64
                                                  + static_cast<std::size_t>(a.to);
                             const std::size_t ic = static_cast<std::size_t>(c.from) * 64
                                                  + static_cast<std::size_t>(c.to);
                             return s.defender_history[ia] > s.defender_history[ic];
                         });
    }
    if (s.refutation_hints) {
        const TTKey& refutation_key = get_hint_key();
        ++s.stats.refutation_hint_probes;
        Move hint_move;
if (s.defender_refutations.probe(refutation_key, hint_move)) {
            if (move_to_front(replies, hint_move)) {
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
    // OBSERVER for the fatal-anti-check family, and specifically for the term
    // that killed section 88: work an existing mechanism already avoids.
    //
    // Variant 3 finds a refuting reply by scanning the reply list for a CHECK
    // the attacker cannot answer, instead of searching replies one by one. What
    // it can save is whatever the search spends BEFORE it reaches the refuting
    // reply -- and this engine already orders replies to put refutations first,
    // via answer ordering and the refutation-hint table. So the question is not
    // "how often does a fatal check exist" but "how many replies does the search
    // actually try before it finds its refutation, and is that refutation a
    // check anyway". Both are counted here, at no cost when the flag is off.
    //
    // Counting the position of the refuting reply is the whole measurement. If
    // it is almost always the first, the mechanism has nothing left to harvest,
    // whatever its fire rate -- which is section 88 stated in advance rather
    // than discovered afterwards.
    std::size_t tried_before_refutation = 0;
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
            if (s.defender_history_order) {
                s.bump_defender_history(dmove, depth);
            }
            if (s.fac_observer) {
                ++s.stats.fac_refuted_nodes;
                s.stats.fac_replies_before += tried_before_refutation;
                if (tried_before_refutation == 0) {
                    ++s.stats.fac_first_reply_refutes;
                }
                // Is the refutation a check on the attacker? That is what the
                // fatal-anti-check test looks for; a refutation that is a quiet
                // move is outside the mechanism's reach however early it comes.
                if (in_check(nb, other(b.stm))) {
                    ++s.stats.fac_refutation_is_check;
                }
            }
            if (s.debug) {
                std::cerr << "defender_refutes depth=" << depth << " move=" << move_uci(dmove)
                          << " fen=" << fen4(nb) << "\n";
            }
            if (s.refutation_hints) {
                ++s.stats.refutation_hint_stores;
                s.defender_refutations.store(get_hint_key(), dmove);
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
        ++tried_before_refutation;
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
inline bool position_is_refuted_axiomatically(const Search& s, const Board& b, int moves) {
    if (!s.any_depth_refutations) {
        return false;                 // inert by construction
    }
    if (s.goal == Goal::Mate) {
        // NOT under a capture quota. The axiom is "a lone king cannot mate",
        // which held for x-check because a lone king cannot give check either.
        // A lone king CAN capture, so under capture-win this would report a won
        // position as having no solution -- silently, which is the third time a
        // goal-specific shortcut has had to be gated for exactly this reason.
        // A lone king cannot mate -- but it CAN capture, so the axiom stands
        // down only while the capture quota is still REACHABLE. Past that point
        // the king is back to needing a mate it cannot deliver.
        if (s.rule_wins[VR_CAPTURE] &&
            static_cast<int>(quota_of(b, s.attacker, VR_CAPTURE)) <= moves) {
            return false;
        }
        // ESCAPE stands the axiom down outright. "A lone king cannot mate" is
        // true and irrelevant here: a lone king can capture the man shielding
        // the enemy king, and raising the enemy's E is the win. No move-budget
        // test can rescue it either, because E has no per-move ceiling to bound
        // it with -- see variant_reachable_within.
        if (s.rule_wins[VR_ESCAPE] &&
            (quota_of(b, WHITE, VR_ESCAPE) != kNoQuota ||
             quota_of(b, BLACK, VR_ESCAPE) != kNoQuota)) {
            return false;
        }
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

// THE CANDIDATE-PREDICATE OBSERVER.
//
// A wrapper rather than an edit to the body below, for two reasons. The body has
// nine return statements and an observer that missed one would under-count
// silently; and this keeps the measurement completely outside the search, so
// there is no path by which a candidate can affect a verdict even by accident.
// With no predicate configured it is one predictable branch per node.
//
// The arithmetic is the whole mechanism:
//
//   fired and the node was PROVED    a counterexample. The candidate claimed
//                                    there was no solution below here and there
//                                    was one. That is a disproof of the
//                                    candidate, it is exact, and one is enough.
//   fired and the node FAILED        the subtree is added to what the candidate
//                                    would have saved -- but only if the node
//                                    was not abandoned, because an abandoned
//                                    subtree settled nothing and pruning it
//                                    would have been a guess rather than a save.
Proof prove_attacker_observed(Search& s, const Board& b, int depth);

Proof prove_attacker(Search& s, const Board& b, int depth) {
    if (!s.predicate.active()) {
        return prove_attacker_observed(s, b, depth);
    }
    const bool fired = depth > 0 && b.stm == s.attacker &&
                       predicate_fires(s.predicate, b, s.attacker, depth);
    const std::uint64_t before = s.stats.nodes;
    Proof out = prove_attacker_observed(s, b, depth);
    if (fired) {
        ++s.stats.pred_fires;
        if (out.ok) {
            ++s.stats.pred_counterexamples;
        } else if (!s.aborted) {
            s.stats.pred_nodes_saved += s.stats.nodes - before;
        }
    }
    return out;
}

Proof prove_attacker_observed(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    // Variant: has the game already ended outright? Tested at every node
    // entry rather than only where a move is made, so it also catches a ROOT
    // position handed in already finished.
    Proof check_end;
    if (variant_terminal(s, b, check_end)) {
        return check_end;
    }
    ++s.stats.attacker_nodes;
    if (position_is_refuted_axiomatically(s, b, depth)) {
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

    // THE COVERAGE EARLY EXIT, at position 4 of the filter ladder: before
    // generation, node-level, so what it saves is the entire node rather than
    // one execution. This is the distinction section 88 got wrong -- that filter
    // moved from position 1 to position 2, fired on 87.1% of candidates, and
    // converted none of it, because an existing shortcut had already reduced the
    // work behind it to a bit read. Nothing in this engine has a node-level
    // "could a mate in one exist at all" predicate, so nothing has harvested
    // this.
    //
    // Held off when any-depth refutations are on: GAP-1 composition needs to
    // have LOOKED at the successors to conclude the node is refuted at every
    // depth, and returning early would report a plain depth-limited failure
    // where a refutation was available. Wrong direction for a cache.
    //
    // Stood down while the observer runs. Otherwise the exit consumes exactly
    // the nodes the observer exists to count, and the observer reports zero --
    // a measurement that describes the instrument rather than the engine.
    //
    // Also stood down under x-check. It proves "no mate in one exists here",
    // which is not the same statement as "no WIN in one exists here" once the
    // final check also wins -- and the node it would discard could hold one.
    if (s.coverage_exit && !s.coverage_observer && depth == 1 &&
        s.goal == Goal::Mate && !s.any_depth_refutations &&
        !variant_reachable_static(b, s.rule_wins, 1)) {
        ++s.stats.coverage_nodes;
        if (mate1_impossible_by_coverage(b)) {
            ++s.stats.coverage_exits;
            return {};
        }
    }

    bool moves_scored = false;
    auto moves = generate_ordered_moves(s, b, moves_scored);
    restrict_attacker_moves(s, b, moves);
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();
    if (s.proof_hints) {
        const TTKey& proof_key = get_hint_key();
        ++s.stats.proof_hint_probes;
        Move hint_move;
if (s.attacker_proofs.probe(proof_key, hint_move)) {
            if (move_to_front(moves, hint_move)) {
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

    // Root-move status. `iteration_depth` is set by the route once per pass, and
    // the root attacker node is the only one that can still have that much depth
    // remaining, so this identifies the root without threading a flag down the
    // recursion. Zero when no route set it, which no real depth ever equals.
    const bool at_root_for_progress =
        s.progress_moves && s.progress_authority && depth == s.iteration_depth && depth > 0;
    int root_move_index = 0;

    // OBSERVER for the same predicate the mechanism above uses, sited after
    // generation so it can report Q2 -- the moves that would never have been
    // produced -- alongside Q1. Two flags for one predicate because they answer
    // different questions: the mechanism must run before generation to save
    // anything, and the observer must run after it to count what was saved.
    //
    // The first cut of this observer used the FULL escape set and the fire rate
    // it reported, 15.2%, was an upper bound three ways over: it counted squares
    // a discovery could close, it excluded kings from the coverage table, and it
    // ignored occupation. All three are fixed in the shipped predicate and all
    // three moved the same way, so the honest figure is the one below and not
    // that one.
    if (s.coverage_observer && depth == 1 && s.goal == Goal::Mate &&
        !variant_reachable_static(b, s.rule_wins, 1)) {
        ++s.stats.coverage_nodes;
        if (mate1_impossible_by_coverage(b)) {
            ++s.stats.coverage_exits;
            s.stats.coverage_moves_saved += moves.size();
        }
    }

    // YOUNG BROTHERS WAIT. The eldest child is searched alone; if it settles the
    // node nothing was shared out and nothing was wasted, and if it does not,
    // the node is probably going to need all its children and the rest go to
    // whoever is idle. `searched` counts children actually handed to a subsearch,
    // not moves stepped over, so a run of moves the depth-1 generator skips does
    // not consume the wait.
    //
    // Gated to the plies just below the root: that is where the work the root
    // split cannot reach lives (109), and keeping it there keeps the split FLAT.
    // One slot per worker, one split per slot, so no owner is ever waiting on a
    // node another owner is waiting on -- which disposes of the blocked-owner
    // problem a general YBWC implementation has to reason about.
    const bool may_split = s.or_split && s.split_registry != nullptr && s.split_slot >= 0 &&
                           depth > 1 && depth >= s.or_split_min_depth &&
                           s.iteration_depth > 0 &&
                           depth >= s.iteration_depth - s.or_split_plies;
    int searched = 0;
    std::size_t move_index = 0;

    for (; move_index < moves.size(); ++move_index) {
        // The second condition of young brothers wait, and 112 shipped without
        // it: split only if another worker is actually idle. Re-read before
        // every child rather than decided once, so a node deep in the tree that
        // becomes the last thing running will open up the moment the pool
        // empties out around it -- which is exactly when it should, and never
        // before.
        if (may_split && searched >= s.ybw_first && split_is_wanted(*s.split_registry)) {
            break;
        }
        const Move& amove = moves[move_index];
        if (at_root_for_progress) {
            publish_root_move(s, b, depth, ++root_move_index,
                              static_cast<int>(moves.size()), amove);
        }
        // RESTRICTED MATING GENERATOR, in its cheapest sound form. A checkmate
        // is a check, so at depth 1 a move that gives no check cannot be a
        // solution -- which is chess, not a borrowed heuristic, and trivially
        // satisfies the superset guarantee the idea rests on.
        //
        // The check bit was already computed by the ordering pass and already
        // consulted below; it was just consulted AFTER the move was made. Moving
        // the test in front of make_move turns a skipped mate test into a
        // skipped move execution, and when no move passes, the node fails with
        // no executions at all -- the whole-node disproof that is the larger of
        // this idea's two payoffs.
        //
        // Guarded on all_moves_refuted because the GAP-1 composition below needs
        // the child board; with any-depth refutations off, which is the default,
        // nothing else at depth 1 does.
        if (depth == 1 && can_use_ordered_check_shortcut && !all_moves_refuted &&
            last_ply_win_needs_check(s.rule_wins, b, s.attacker) &&
            !move_can_reach_goal(amove.score, s.goal)) {
            ++s.stats.mate1_generator_skips;
            continue;
        }
        ++s.stats.attacker_candidates;
        Board nb = make_move(b, amove);
        // x-check: the attacker's final check ends the game in his favour, and
        // under the mate goal that IS the win being forced. Tested here beside
        // the mate test rather than left to the child node, because at depth 1
        // there is no child -- the loop returns or continues without recursing.
        const int win_rule = variant_win_reached(nb, s.goal, s.attacker, s.rule_wins);
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
        if (mate || win_rule >= 0) {
            ++s.stats.immediate_mates;
            std::vector<Move> pv{amove};
            std::string cert;
            if (s.emit_proof) {
                std::string how = (s.goal == Goal::Stalemate) ? ",\"stalemate\":true}"
                                                             : ",\"mate\":true}";
                if (win_rule >= 0 && !mate) {
                    how = std::string(",\"") + variant_win_key(win_rule) + "\":true}";
                }
                cert = "{\"a\":" + json_quote(move_uci(amove)) + how;
            }
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs.store(get_hint_key(), amove);
            }
            Proof proof{true, pv, cert};
            store_exact_proof_table(s, key, depth, proof);
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs.store(hint_key, amove);
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
        if (all_moves_refuted && !position_is_refuted_axiomatically(s, nb, depth)) {
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
            ++searched;
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
                    s.attacker_proofs.store(get_hint_key(), amove);
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

    if (may_split && move_index < moves.size()) {
        SplitRegistry& reg = *s.split_registry;
        auto sp = std::make_shared<NodeSplit>();
        sp->board = b;
        sp->depth = depth;
        sp->slot = s.split_slot;
        sp->priority = s.split_priority;
        sp->conjunctive = false;
        sp->moves.assign(moves.begin() + static_cast<std::ptrdiff_t>(move_index), moves.end());
        const int n = static_cast<int>(sp->moves.size());
        sp->results.resize(static_cast<std::size_t>(n));
        sp->state.assign(static_cast<std::size_t>(n), kChildUnresolved);

        // The part of the loop body that CANNOT be shared out, run here in move
        // order. A move that wins outright is a settling child like any other,
        // recorded at its own index rather than returned -- because a LOWER
        // index that also settles, by searching successfully, would still be
        // preferred, and that is precisely what the sequential loop would do.
        // Nothing after it can be preferred, so nothing after it is tested.
        for (int j = 0; j < n; ++j) {
            const Move& amove = sp->moves[static_cast<std::size_t>(j)];
            if (at_root_for_progress) {
                publish_root_move(s, b, depth, ++root_move_index,
                                  static_cast<int>(moves.size()), amove);
            }
            ++s.stats.attacker_candidates;
            const Board nb = make_move(b, amove);
            const int win_rule = variant_win_reached(nb, s.goal, s.attacker, s.rule_wins);
            ++s.stats.immediate_mate_tests;
            bool mate = false;
            if (can_use_ordered_check_shortcut) {
                ++s.stats.ordered_check_shortcut_uses;
                if (move_can_reach_goal(amove.score, s.goal)) {
                    ++s.stats.ordered_check_shortcut_checks;
                    mate = !has_legal_move(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
                } else {
                    ++s.stats.ordered_check_shortcut_skips;
                }
            } else {
                mate = is_goal(nb, s.goal, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
            }
            if (mate || win_rule >= 0) {
                ++s.stats.immediate_mates;
                std::string how = (s.goal == Goal::Stalemate) ? ",\"stalemate\":true}"
                                                             : ",\"mate\":true}";
                if (win_rule >= 0 && !mate) {
                    how = std::string(",\"") + variant_win_key(win_rule) + "\":true}";
                }
                Proof won{true, std::vector<Move>{amove},
                          s.emit_proof ? "{\"a\":" + json_quote(move_uci(amove)) + how : std::string()};
                sp->results[static_cast<std::size_t>(j)] = std::move(won);
                sp->state[static_cast<std::size_t>(j)] = kChildImmediate;
                sp->settled_at = j;
                break;
            }
        }

        const std::uint64_t saved_seq = s.max_owned_seq;
        {
            std::lock_guard<std::mutex> lock(reg.m);
            sp->seq = reg.next_seq.fetch_add(1, std::memory_order_relaxed);
            sp->open = true;
            reg.open_splits[static_cast<std::size_t>(s.split_slot)] = sp;
            reg.open.fetch_add(1, std::memory_order_relaxed);
            reg.cv.notify_all();
        }
        s.max_owned_seq = sp->seq;
        // The owner works the queue like anyone else, so a node nobody helps
        // costs one atomic claim per child on top of the sequential loop.
        for (;;) {
            const int j = claim_child(reg, *sp);
            if (j < 0) {
                break;
            }
            run_child(reg, *sp, j, s);
            if (s.aborted) {
                break;      // unwinding; the composition below will see it
            }
        }
        // Withdraw before waiting, so no further claim can be made, and then
        // wait for the helpers already inside. Their shared_ptr copies keep the
        // node alive regardless; it is their ANSWERS that are needed.
        {
            std::lock_guard<std::mutex> lock(reg.m);
            sp->open = false;
            reg.open_splits[static_cast<std::size_t>(s.split_slot)].reset();
            reg.open.fetch_sub(1, std::memory_order_relaxed);
            reg.cv.notify_all();
        }
        // WAIT BY WORKING. A blocked owner used to be a lost thread: out of its
        // own children, sitting on the condition variable until its helpers came
        // back. That is why splitting more than one ply down lost -- every extra
        // split point converted a worker into a waiter, so supply of split
        // points became consumption of workers.
        //
        // Now it goes and helps, confined to splits published after its own so
        // the wait relation stays acyclic (see claim_any_child). Not while
        // unwinding: a search that has given up should unwind, not take on work.
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(reg.m);
                if (sp->active == 0) {
                    break;
                }
            }
            NodeClaim help = (s.owner_helps && !s.aborted)
                                 ? claim_any_child(reg, s.max_owned_seq)
                                 : NodeClaim{};
            if (help.index >= 0) {
                // The helped child's outcome belongs to ITS node, not this one.
                // Letting an abort leak across would poison a node this worker
                // merely lent a hand to, and every real termination condition --
                // deadline, node ceiling, cancellation -- is re-derived by
                // search_cancelled from flags this does not touch, so restoring
                // it cannot mask one.
                const bool was_aborted = s.aborted;
                ++s.stats.split_helped;
                run_child(reg, *help.split, help.index, s);
                s.aborted = was_aborted;
                continue;
            }
            const auto wait_start = std::chrono::steady_clock::now();
            std::unique_lock<std::mutex> lock(reg.m);
            if (sp->active == 0) {
                break;
            }
            reg.cv.wait_for(lock, std::chrono::milliseconds(1));
            s.stats.owner_wait_micros += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - wait_start).count());
        }
        s.max_owned_seq = saved_seq;
        std::unique_lock<std::mutex> lock(reg.m);
        const int settled_at = sp->settled_at;
        lock.unlock();

        if (settled_at >= 0) {
            const Move& amove = sp->moves[static_cast<std::size_t>(settled_at)];
            Proof& child = sp->results[static_cast<std::size_t>(settled_at)];
            Proof proof;
            if (sp->state[static_cast<std::size_t>(settled_at)] == kChildImmediate) {
                proof = std::move(child);
            } else {
                proof.ok = true;
                proof.pv.push_back(amove);
                proof.pv.insert(proof.pv.end(), child.pv.begin(), child.pv.end());
                if (s.emit_proof) {
                    proof.cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"d\":" + child.cert + "}";
                }
            }
            if (s.proof_hints) {
                ++s.stats.proof_hint_stores;
                s.attacker_proofs.store(get_hint_key(), amove);
            }
            store_exact_proof_table(s, key, depth, proof);
            return proof;
        }
        // Nothing settled the node, so it fails only if EVERY child came back.
        // One abandoned branch and there is no verdict here at all -- not a
        // proof, not a disproof, and above all nothing to store. This is the
        // abort invariant, and it is the line that decides whether a parallel
        // prover is sound.
        for (int j = 0; j < n; ++j) {
            const std::uint8_t st = sp->state[static_cast<std::size_t>(j)];
            if (st != kChildProved && st != kChildFailed) {
                s.aborted = true;
                return {};
            }
            if (!sp->results[static_cast<std::size_t>(j)].refuted) {
                all_moves_refuted = false;
            }
        }
    }

    Proof failed;
    failed.refuted = all_moves_refuted;
    store_exact_proof_table(s, key, depth, failed);
    return failed;
}

// Prove one child of a split node and record the outcome. Runs on whichever
// worker claimed it, so its node counts land in that worker's own Stats and are
// folded back with everyone else's when the root split finishes.
//
// The outcomes are exactly the sequential loops', in the same order and with
// the same precedence. ABANDONED FIRST, in both shapes: a search that gave up
// has proved nothing, and reading its empty result as a verdict is the single
// mistake that would turn this machinery into a forged proof.
void run_child(SplitRegistry& reg, NodeSplit& sp, int j, Search& ws) {
    const auto child_start = std::chrono::steady_clock::now();
    const Move& m = sp.moves[static_cast<std::size_t>(j)];
    const Board nb = make_move(sp.board, m);
    std::uint8_t st = kChildAbandoned;
    Proof child;
    bool settle_refuted = false;
    bool is_check = false;

    ++ws.stats.split_claims;
    if (sp.conjunctive && sp.lazy && in_check(nb, other(nb.stm))) {
        ++ws.stats.defender_lazy_skipped;
        st = kChildIllegal;
    } else if (sp.conjunctive) {
        if (sp.lazy) {
            ++ws.stats.defender_moves;
        }
        ++ws.stats.defender_replies_tried;
        child = prove_attacker(ws, nb, sp.depth);
        if (ws.aborted) {
            st = kChildAbandoned;
        } else if (!child.ok) {
            st = kChildFailed;              // a failing reply SETTLES an AND node
            settle_refuted = child.refuted;
            ++ws.stats.defender_refutations;
            if (ws.fac_observer) {
                is_check = in_check(nb, other(sp.board.stm));
            }
            if (ws.debug) {
                std::cerr << "defender_refutes depth=" << sp.depth << " move=" << move_uci(m)
                          << " fen=" << fen4(nb) << "\n";
            }
        } else {
            st = kChildProved;
        }
    } else {
        child = prove_defender(ws, nb, sp.depth - 1);
        if (ws.aborted) {
            st = kChildAbandoned;
        } else if (child.ok) {
            st = kChildProved;              // a succeeding move SETTLES an OR node
        } else {
            st = kChildFailed;
            if (ws.debug) {
                std::cerr << "attacker_move_failed depth=" << sp.depth << " move=" << move_uci(m)
                          << " fen=" << fen4(nb) << "\n";
            }
        }
    }

    ws.stats.split_work_micros += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - child_start).count());

    const bool settles = sp.conjunctive ? (st == kChildFailed) : (st == kChildProved);
    std::lock_guard<std::mutex> lock(reg.m);
    sp.state[static_cast<std::size_t>(j)] = st;
    sp.results[static_cast<std::size_t>(j)] = std::move(child);
    if (settles && (sp.settled_at < 0 || j < sp.settled_at)) {
        sp.settled_at = j;
        sp.settle_refuted = settle_refuted;
        sp.settle_is_check = is_check;
    }
    --sp.active;
    reg.cv.notify_all();
}

// An idle worker's loop: take children from whatever nodes are still open until
// there is nothing left anywhere. Entered only when the ROOT queue is empty, so
// this thread has no work of its own to displace and owns no split anyone is
// waiting on -- which is why the waits above cannot deadlock.
void help_splits(SplitRegistry& reg, Search& ws, std::atomic<int>& current_root,
                 std::atomic<bool>& cancel) {
    // THE DEMAND SIGNAL. Nothing in the tree splits unless this is raised, so a
    // worker that leaves without lowering it would make every eligible node
    // publish for a helper that does not exist. A guard rather than a pair of
    // calls, because the loop below returns from four places.
    struct IdleMark {
        std::atomic<int>& n;
        explicit IdleMark(std::atomic<int>& c) : n(c) { n.fetch_add(1, std::memory_order_relaxed); }
        ~IdleMark() { n.fetch_sub(1, std::memory_order_relaxed); }
        IdleMark(const IdleMark&) = delete;
        IdleMark& operator=(const IdleMark&) = delete;
    } idle_mark(reg.idle);

    for (;;) {
        if (ws.timed_out ||
            (ws.external_cancel != nullptr &&
             ws.external_cancel->load(std::memory_order_relaxed))) {
            return;
        }
        NodeClaim claim = claim_any_child(reg, ws.max_owned_seq);
        if (claim.index < 0) {
            // Nothing to take. If every root move is finished there never will
            // be; otherwise an owner is still generating its children and this
            // waits to be told. The timeout is a backstop, not the mechanism.
            const auto park_start = std::chrono::steady_clock::now();
            std::unique_lock<std::mutex> lock(reg.m);
            if (reg.live_roots == 0) {
                return;
            }
            reg.cv.wait_for(lock, std::chrono::milliseconds(2));
            ws.stats.split_park_micros += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - park_start).count());
            continue;
        }
        // Announce which root move this thread is now serving, so the root
        // split's existing cancellation reaches helpers too: when a lower root
        // index proves, everyone above it is told to stop, owners and helpers
        // alike. Publish, then clear, then let run_child re-check -- the same
        // order, and the same window closed, as the root loop's.
        current_root.store(claim.split->priority, std::memory_order_release);
        cancel.store(false, std::memory_order_release);
        ws.aborted = false;
        ++ws.stats.split_helped;
        // Not idle while actually working, or a pool that is fully employed
        // would still read as demand and every node in it would split.
        reg.idle.fetch_sub(1, std::memory_order_relaxed);
        run_child(reg, *claim.split, claim.index, ws);
        reg.idle.fetch_add(1, std::memory_order_relaxed);
        current_root.store(std::numeric_limits<int>::max(), std::memory_order_release);
    }
}

} // namespace mateprover

#endif // MATEPROVER_PROVE_H_INCLUDED
