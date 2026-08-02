// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// report.h -- Perft, profile counters, and the line-oriented output helpers.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_REPORT_H_INCLUDED
#define MATEPROVER_REPORT_H_INCLUDED

namespace mateprover {

// Perft: count leaf nodes of the legal move tree to a fixed depth.
//
// This is the engine's self-contained move-generation gate. Directmate proofs
// are only as trustworthy as legality, and perft against published reference
// counts exercises castling rights, en-passant capture and expiry, promotion,
// and pinned-piece legality far more thoroughly than the mate suites do -- a
// movegen bug usually shows up as a wrong perft number long before it shows up
// as a wrong mate.
std::uint64_t perft(const Board& b, int depth) {
    if (depth <= 0) {
        return 1;
    }
    auto moves = legal_moves(b);
    if (depth == 1) {
        return moves.size();
    }
    std::uint64_t total = 0;
    for (const Move& m : moves) {
        total += perft(make_move(b, m), depth - 1);
    }
    return total;
}

// Per-root-move perft breakdown: the standard tool for bisecting a movegen or
// make/unmake discrepancy against a reference implementation.
void perft_divide_line(const std::string& raw, int depth) {
    std::string line = trim(raw);
    if (line.empty()) {
        line = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; perft error input;\n" << std::flush;
        return;
    }
    const Board b = *parsed;
    auto moves = legal_moves(b);
    std::vector<std::pair<std::string, std::uint64_t>> rows;
    rows.reserve(moves.size());
    std::uint64_t total = 0;
    for (const Move& m : moves) {
        const std::uint64_t n = perft(make_move(b, m), depth - 1);
        rows.emplace_back(move_uci(m), n);
        total += n;
    }
    std::sort(rows.begin(), rows.end());
    for (const auto& row : rows) {
        std::cout << row.first << ' ' << row.second << '\n';
    }
    std::cout << "total " << total << '\n';
}

void perft_line(const std::string& raw, int depth) {
    std::string line = trim(raw);
    if (line.empty()) {
        line = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; perft error input;\n" << std::flush;
        return;
    }
    const Board b = *parsed;
    for (int d = 1; d <= depth; ++d) {
        const auto start = std::chrono::steady_clock::now();
        const std::uint64_t nodes = perft(b, d);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << fen4(b) << "; perft " << d << "; nodes " << nodes << "; acs " << seconds << ";\n"
                  << std::flush;
    }
}

// Read a mate depth out of an annotated input line.
//
// Two spellings are accepted. `#N` is the matetrack/EPD convention. `dm N` is
// the convention this repository's own corpora use (tests/mates.epd is written
// `<fen4> ; dm <depth>`) and is also the form mateprover itself prints, so a run's
// output can be fed straight back in. Only `#N` was recognised before, which
// meant piping the shipped corpus into the engine silently searched nothing.
int infer_mate_depth(const std::string& line) {
    auto read_digits = [&line](std::size_t pos) {
        int value = 0;
        bool any = false;
        for (; pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])); ++pos) {
            value = value * 10 + (line[pos] - '0');
            any = true;
        }
        return any ? value : 0;
    };

    if (auto pos = line.find('#'); pos != std::string::npos) {
        return read_digits(pos + 1);
    }

    // `dm` must stand as its own token, so that a FEN or a move never matches.
    for (std::size_t pos = line.find("dm"); pos != std::string::npos;
         pos = line.find("dm", pos + 1)) {
        const bool left_ok = pos == 0 || line[pos - 1] == ' ' || line[pos - 1] == ';' ||
                             line[pos - 1] == '	';
        std::size_t after = pos + 2;
        if (!left_ok || after >= line.size()) {
            continue;
        }
        if (line[after] != ' ' && line[after] != '	') {
            continue;
        }
        while (after < line.size() && (line[after] == ' ' || line[after] == '	')) {
            ++after;
        }
        if (const int value = read_digits(after); value > 0) {
            return value;
        }
    }
    return 0;
}

std::string pv_uci(const std::vector<Move>& pv) {
    std::ostringstream out;
    for (std::size_t i = 0; i < pv.size(); ++i) {
        if (i) out << ' ';
        out << move_uci(pv[i]);
    }
    return out.str();
}

void emit_profile_line(const Board& b, const Search& s, int requested_depth, int proved_depth, double seconds) {
    const Stats& st = s.stats;
    std::cerr << "% mateprover_profile {"
              << "\"fen\":" << json_quote(fen4(b))
              << ",\"route\":" << json_quote(route_name(s.route))
              << ",\"requested_depth\":" << requested_depth
              << ",\"proved_depth\":" << proved_depth
              << ",\"seconds\":" << seconds
              << ",\"nodes\":" << st.nodes
              << ",\"attacker_nodes\":" << st.attacker_nodes
              << ",\"defender_nodes\":" << st.defender_nodes
              << ",\"tt_probes\":" << st.tt_probes
              << ",\"tt_hits\":" << st.tt_hits
              << ",\"tt_stores\":" << st.tt_stores
              << ",\"tt_size\":" << (s.shared_table != nullptr ? s.shared_table->size() : s.tt.size())
              << ",\"tt_capacity\":" << s.tt.capacity
              << ",\"tt_evictions\":" << (s.shared_table != nullptr ? s.shared_table->evictions() : s.tt.evictions)
              << ",\"memory_mb\":" << s.memory_mb
              << ",\"exact_tt_proof_hits\":" << st.exact_tt_proof_hits
              << ",\"exact_tt_disproof_hits\":" << st.exact_tt_disproof_hits
              << ",\"exact_tt_proof_stores\":" << st.exact_tt_proof_stores
              << ",\"exact_tt_disproof_stores\":" << st.exact_tt_disproof_stores
              << ",\"shallow_fast_attempts\":" << st.shallow_fast_attempts
              << ",\"shallow_fast_hits\":" << st.shallow_fast_hits
              << ",\"shallow_fast_fallbacks\":" << st.shallow_fast_fallbacks
              << ",\"attacker_move_lists\":" << st.attacker_move_lists
              << ",\"attacker_moves\":" << st.attacker_moves
              << ",\"attacker_candidates\":" << st.attacker_candidates
              << ",\"defender_move_lists\":" << st.defender_move_lists
              << ",\"defender_moves\":" << st.defender_moves
              << ",\"defender_replies_tried\":" << st.defender_replies_tried
              << ",\"defender_refutations\":" << st.defender_refutations
              << ",\"refutation_hint_probes\":" << st.refutation_hint_probes
              << ",\"refutation_hint_hits\":" << st.refutation_hint_hits
              << ",\"refutation_hint_stores\":" << st.refutation_hint_stores
              << ",\"defender_pseudo_moves\":" << st.defender_pseudo_moves
              << ",\"defender_lazy_skipped\":" << st.defender_lazy_skipped
              << ",\"dfpn_nodes\":" << st.dfpn_nodes
              << ",\"dfpn_proved\":" << st.dfpn_proved
              << ",\"dfpn_disproved\":" << st.dfpn_disproved
              << ",\"dfpn_table_size\":" << st.dfpn_table_size
              << ",\"root_sequential_tried\":" << st.root_sequential_tried
              << ",\"root_sequential_hits\":" << st.root_sequential_hits
              << ",\"dfpn_movegen\":" << st.dfpn_movegen
              << ",\"dfpn_mate_tests\":" << st.dfpn_mate_tests
              << ",\"timed_out\":" << (s.timed_out ? "true" : "false")
              << ",\"lazy_defender\":" << (s.lazy_defender ? "true" : "false")
              << ",\"order_calls\":" << st.order_calls
              << ",\"order_moves\":" << st.order_moves
              << ",\"immediate_mate_tests\":" << st.immediate_mate_tests
              << ",\"ordered_check_shortcut_uses\":" << st.ordered_check_shortcut_uses
              << ",\"ordered_check_shortcut_checks\":" << st.ordered_check_shortcut_checks
              << ",\"ordered_check_shortcut_skips\":" << st.ordered_check_shortcut_skips
              << ",\"immediate_mates\":" << st.immediate_mates
              << ",\"refutation_hint_probes\":" << st.refutation_hint_probes
              << ",\"refutation_hint_hits\":" << st.refutation_hint_hits
              << ",\"refutation_hint_stores\":" << st.refutation_hint_stores
              << ",\"proof_hint_probes\":" << st.proof_hint_probes
              << ",\"proof_hint_hits\":" << st.proof_hint_hits
              << ",\"proof_hint_stores\":" << st.proof_hint_stores
              << ",\"route_rejections\":" << st.route_rejections
              << ",\"defender_refutations\":" << st.defender_refutations
              << ",\"move_reserve\":" << (s.move_reserve ? "true" : "false")
              << ",\"move_reserve_capacity\":" << s.move_reserve_capacity
              << ",\"inplace_order\":" << (s.inplace_order ? "true" : "false")
              << ",\"bucket_order\":" << (s.bucket_order ? "true" : "false")
              << ",\"static_pseudo\":" << (s.static_pseudo ? "true" : "false")
              << ",\"order_min_size\":" << s.order_min_size
              << ",\"refutation_hints\":" << (s.refutation_hints ? "true" : "false")
              << ",\"proof_hints\":" << (s.proof_hints ? "true" : "false")
              << ",\"keep_iter_tt\":" << (s.keep_iter_tt ? "true" : "false")
              << ",\"ordered_check_shortcut\":" << (s.ordered_check_shortcut ? "true" : "false")
              << "}\n";
}

// Print the effective configuration as JSON.
//
// Exists so that a run is reproducible from its own report, and so that the
// defaults --help advertises can be checked against the ones actually in
// force. Documentation and behaviour had already drifted apart once.
void emit_config_json(const SearchConfig& c) {
    std::ostream& out = std::cout;
    out << "{\"route\":\"" << route_name(c.route) << "\"";
    out << ",\"threads\":" << c.threads;
    out << ",\"parallel_positions\":" << c.parallel_positions;
    out << ",\"memory_mb\":" << c.memory_mb;
    out << ",\"memory_is_total\":" << (c.memory_is_total ? "true" : "false");
    out << ",\"time_limit\":" << c.time_limit;
    out << ",\"parallel_min_nodes\":" << c.parallel_min_nodes;
    out << ",\"shared_tt_shards\":" << c.shared_tt_shards;
    out << ",\"move_reserve_capacity\":" << c.move_reserve_capacity;
    out << ",\"order_min_size\":" << c.order_min_size;
    out << ",\"direct_depth\":" << (c.direct_depth ? "true" : "false");
    out << ",\"portfolio\":" << (c.portfolio ? "true" : "false");
    out << ",\"portfolio_parallel\":" << (c.portfolio_parallel ? "true" : "false");
    out << ",\"shared_tt\":" << (c.shared_tt ? "true" : "false");
    out << ",\"move_reserve\":" << (c.move_reserve ? "true" : "false");
    out << ",\"proof_hints\":" << (c.proof_hints ? "true" : "false");
    out << ",\"refutation_hints\":" << (c.refutation_hints ? "true" : "false");
    out << ",\"keep_iter_tt\":" << (c.keep_iter_tt ? "true" : "false");
    out << ",\"ordered_check_shortcut\":" << (c.ordered_check_shortcut ? "true" : "false");
    out << ",\"inplace_order\":" << (c.inplace_order ? "true" : "false");
    out << ",\"fused_order\":" << (c.fused_order ? "true" : "false");
    out << ",\"lazy_defender\":" << (c.lazy_defender ? "true" : "false");
    out << ",\"bucket_order\":" << (c.bucket_order ? "true" : "false");
    out << ",\"score_mates\":" << (c.score_mates ? "true" : "false");
    out << ",\"static_pseudo\":" << (c.static_pseudo ? "true" : "false");
    out << ",\"emit_proof\":" << (c.emit_proof ? "true" : "false");
    out << ",\"profile\":" << (c.profile ? "true" : "false");
    out << "}\n";
}
void list_legal_line(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; legal_count 0; error input;\n" << std::flush;
        return;
    }
    Board b = *parsed;
    auto moves = legal_moves(b);
    std::vector<std::string> uci;
    uci.reserve(moves.size());
    for (const Move& move : moves) {
        uci.push_back(move_uci(move));
    }
    std::sort(uci.begin(), uci.end());
    std::cout << fen4(b) << "; legal_count " << uci.size() << "; legal";
    for (const std::string& move : uci) {
        std::cout << ' ' << move;
    }
    std::cout << ";\n" << std::flush;
}

} // namespace mateprover

#endif // MATEPROVER_REPORT_H_INCLUDED
