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



Proof prove_defender(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.defender_nodes;
    // Depth is not part of the key; it is supplied to probe/store instead.
    TTKey key = tt_key(b, 0, 'D', s.attacker);
    Proof exact_cached;
    if (probe_exact_proof_table(s, key, depth, exact_cached)) {
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
            store_exact_proof_table(s, key, depth, {});
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

Proof prove_attacker(Search& s, const Board& b, int depth) {
    if (search_cancelled(s)) {
        return {};
    }
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (depth <= 0 || b.stm != s.attacker) {
        return {};
    }
    // Depth is not part of the key; it is supplied to probe/store instead.
    TTKey key = tt_key(b, 0, 'A', s.attacker);
    Proof exact_cached;
    if (probe_exact_proof_table(s, key, depth, exact_cached)) {
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
            store_exact_proof_table(s, key, depth, proof);
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
                store_exact_proof_table(s, key, depth, proof);
                return proof;
            }
            if (s.debug) {
                std::cerr << "attacker_move_failed depth=" << depth << " move=" << move_uci(amove)
                          << " fen=" << fen4(nb) << "\n";
            }
        }
    }
    store_exact_proof_table(s, key, depth, {});
    return {};
}

} // namespace mateprover

#endif // MATEPROVER_PROVE_H_INCLUDED
