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

    // Stalemate spellings first: `=N` is the problemist convention and `sm N`
    // mirrors this engine's own output. Checked before `#` so a line carrying
    // both cannot be read as a mate job.
    // `sfm N` is this engine's own selfmate spelling, so its own output round
    // trips as input. Checked before `sm` and `#`: a selfmate line carries an
    // `s#N` too, and reading that as a directmate would search the wrong goal.
    // Longest token first. `ssm ` and `hsm ` both CONTAIN `sm `, and `hm ` is
    // contained in nothing but matches no other rule, so a shorter probe run
    // first would read a selfstalemate or a helpstalemate as a stalemate. The
    // digits happen to agree in every case today, which is exactly why this
    // ordering has to be deliberate rather than lucky: it stops agreeing the
    // moment any token grows a second field.
    if (auto pos = line.find("hsm "); pos != std::string::npos) {
        if (int d = read_digits(pos + 4); d > 0) return d;
    }
    if (auto pos = line.find("ssm "); pos != std::string::npos) {
        if (int d = read_digits(pos + 4); d > 0) return d;
    }
    if (auto pos = line.find("sfm "); pos != std::string::npos) {
        if (int d = read_digits(pos + 4); d > 0) return d;
    }
    if (auto pos = line.find("hm "); pos != std::string::npos) {
        if (int d = read_digits(pos + 3); d > 0) return d;
    }
    if (auto pos = line.find("sm "); pos != std::string::npos) {
        if (int d = read_digits(pos + 3); d > 0) return d;
    }
    if (auto pos = line.find('='); pos != std::string::npos) {
        if (int d = read_digits(pos + 1); d > 0) return d;
    }
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
              << ",\"defender_legality_tests\":" << st.defender_legality_tests
              << ",\"disproof_excess_0\":" << st.disproof_excess_0
              << ",\"disproof_excess_1\":" << st.disproof_excess_1
              << ",\"disproof_excess_2\":" << st.disproof_excess_2
              << ",\"disproof_excess_3\":" << st.disproof_excess_3
              << ",\"disproof_excess_4\":" << st.disproof_excess_4
              << ",\"disproof_excess_5plus\":" << st.disproof_excess_5plus
              << ",\"levels_skipped\":" << st.levels_skipped
              << ",\"answer_orderings\":" << st.answer_orderings
              << ",\"tt_known_weaker\":" << st.tt_known_weaker
              << ",\"mate1_generator_skips\":" << st.mate1_generator_skips
              << ",\"d1_attacker_moves\":" << st.d1_attacker_moves
              << ",\"d1_would_reject\":" << st.d1_would_reject
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
              << ",\"defender_kq_nodes\":" << st.defender_kq_nodes
              << ",\"perpetual_refutations\":" << st.perpetual_refutations
              << ",\"help_unreachable_prunes\":" << st.help_unreachable_prunes
              << ",\"selfmate_unreachable_prunes\":" << st.selfmate_unreachable_prunes
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
    out << ",\"any_depth_refutations\":" << (c.any_depth_refutations ? "true" : "false");
    out << ",\"help_bound\":" << (c.help_bound ? "true" : "false");
    out << ",\"selfmate_bound\":" << (c.selfmate_bound ? "true" : "false");
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


// ---------------------------------------------------------------------------
// Human-readable solution output: short algebraic notation and the proof tree.
//
// The certificate is the machine's record and is deliberately terse. A
// problemist reads a solution TREE -- key, then each defence and its refutation,
// indented -- and reads it in algebraic, not in coordinates. Both are output
// concerns rather than search concerns, so both live here and neither can
// affect a verdict.
// ---------------------------------------------------------------------------

// Short algebraic for one move, in the position before it is played.
//
// Disambiguation is the whole difficulty. Two knights that can both reach the
// square make "Nf3" ambiguous, and the rule is file first, then rank, then
// both. The candidates must be filtered to LEGAL moves rather than pseudo-legal
// ones: a piece pinned against its own king cannot really go there, so including
// it would disambiguate against a move that does not exist.
std::string move_san(const Board& b, const Move& m) {
    const char piece = b.sq[static_cast<std::size_t>(m.from)];
    const int type = type_of(piece);
    std::string out;

    if (m.castle) {
        out = (file_of(m.to) > file_of(m.from)) ? "O-O" : "O-O-O";
    } else if (type == PT_PAWN) {
        // An empty square is '.', not 0. Testing against 0 made every move a
        // capture, so a quiet key printed as "Qxa5".
        const bool capture = m.ep || type_of(b.sq[static_cast<std::size_t>(m.to)]) != PT_NONE;
        if (capture) {
            out.push_back(static_cast<char>('a' + file_of(m.from)));
            out.push_back('x');
        }
        out += sq_name(m.to);
        if (m.promo) {
            out.push_back('=');
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(m.promo))));
        }
    } else {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(piece))));
        bool same_file = false, same_rank = false, ambiguous = false;
        for (const Move& other : legal_moves(b, false, 64, false)) {
            if (other.from == m.from || other.to != m.to) {
                continue;
            }
            if (type_of(b.sq[static_cast<std::size_t>(other.from)]) != type) {
                continue;
            }
            ambiguous = true;
            if (file_of(other.from) == file_of(m.from)) same_file = true;
            if (rank_of(other.from) == rank_of(m.from)) same_rank = true;
        }
        if (ambiguous) {
            // File alone unless another candidate shares it; then rank; then
            // both, which is the case two pieces on the same file and rank
            // cannot produce but three pieces can.
            if (!same_file) {
                out.push_back(static_cast<char>('a' + file_of(m.from)));
            } else if (!same_rank) {
                out.push_back(static_cast<char>('1' + rank_of(m.from)));
            } else {
                out += sq_name(m.from);
            }
        }
        if (type_of(b.sq[static_cast<std::size_t>(m.to)]) != PT_NONE) {
            out.push_back('x');
        }
        out += sq_name(m.to);
    }

    const Board nb = make_move(b, m);
    if (in_check(nb, nb.stm)) {
        out.push_back(has_legal_move(nb, false, 64, false) ? '+' : '#');
    }
    return out;
}

std::string move_text(const Board& b, const Move& m, bool short_notation) {
    return short_notation ? move_san(b, m) : move_uci(m);
}

// A minimal reader for the certificate shapes this engine emits.
//
// Not a general JSON parser and not trying to be: it understands exactly the
// objects `emit_proof` writes, and anything else makes it stop. That is the
// right trade for output -- a printer that guesses at unknown input would
// present a tree that is not the one the search proved.
struct CertReader {
    const std::string& s;
    std::size_t i = 0;
    explicit CertReader(const std::string& text) : s(text) {}

    void skip() { while (i < s.size() && (s[i] == ' ' || s[i] == ',')) ++i; }
    bool eat(char c) { skip(); if (i < s.size() && s[i] == c) { ++i; return true; } return false; }
    bool peek(char c) { skip(); return i < s.size() && s[i] == c; }
    std::string str() {
        skip();
        if (i >= s.size() || s[i] != '"') return {};
        ++i;
        std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) ++i;
            out.push_back(s[i++]);
        }
        ++i;
        return out;
    }
    // Reads `"key":` and returns the key, or empty at the end of an object.
    std::string key() {
        skip();
        if (i >= s.size() || s[i] != '"') return {};
        std::string k = str();
        eat(':');
        return k;
    }
    void skip_value() {
        skip();
        if (i >= s.size()) return;
        if (s[i] == '"') { str(); return; }
        if (s[i] == '{' || s[i] == '[') {
            const char open = s[i], close = open == '{' ? '}' : ']';
            int depth = 0;
            for (; i < s.size(); ++i) {
                if (s[i] == open) ++depth;
                else if (s[i] == close && --depth == 0) { ++i; return; }
            }
            return;
        }
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') ++i;
    }
};

// `<fen>; san <uci>=<san> ...` -- every legal move in both notations.
//
// A diagnostic, and the hook the test suite uses to compare this engine's SAN
// against python-chess move for move. SAN is full of edge cases -- three kinds
// of disambiguation, castling, promotion, en passant, the check and mate
// suffixes -- and an earlier version of move_san passed a three-move eyeball
// while marking every quiet move as a capture.
// `<fen4>; unmoves N; pred <fen4> ...` -- every predecessor of the position.
//
// The hook the round-trip test drives. Retrograde generation is verified by a
// property rather than by inspection: for every predecessor P this prints, some
// legal move from P must reproduce the input, and the generator establishes that
// itself by construction. What the external test adds is the other direction --
// that no predecessor is MISSING -- which it checks by playing every legal move
// from a large sample of positions and confirming the resulting position lists
// the position it came from.
void list_unmoves_line(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; unmoves 0; error input;\n" << std::flush;
        return;
    }
    std::vector<Board> preds;
    generate_unmoves(*parsed, preds);
    std::vector<std::string> fens;
    fens.reserve(preds.size());
    for (const Board& p : preds) {
        fens.push_back(fen4(p));
    }
    std::sort(fens.begin(), fens.end());
    fens.erase(std::unique(fens.begin(), fens.end()), fens.end());
    std::cout << fen4(*parsed) << "; unmoves " << fens.size() << "; pred";
    for (const std::string& f : fens) {
        std::cout << " [" << f << "]";
    }
    std::cout << ";\n" << std::flush;
}

void list_san_line(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; san_count 0; error input;\n" << std::flush;
        return;
    }
    Board b = *parsed;
    auto moves = legal_moves(b);
    std::vector<std::string> pairs;
    pairs.reserve(moves.size());
    for (const Move& move : moves) {
        pairs.push_back(move_uci(move) + "=" + move_san(b, move));
    }
    std::sort(pairs.begin(), pairs.end());
    std::cout << fen4(b) << "; san_count " << pairs.size() << "; san";
    for (const std::string& pair : pairs) {
        std::cout << ' ' << pair;
    }
    std::cout << ";\n" << std::flush;
}

void print_cert_tree(std::ostream& out, CertReader& r, Board b, int indent,
                     bool short_notation);

// One `{...}` node of a certificate, with `b` the position it describes.
void print_cert_node(std::ostream& out, CertReader& r, Board b, int indent,
                     bool short_notation) {
    if (!r.eat('{')) {
        return;
    }
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    for (;;) {
        if (r.eat('}')) return;
        const std::string k = r.key();
        if (k.empty()) { r.eat('}'); return; }
        if (k == "a" || k == "h") {
            const std::string uci = r.str();
            for (const Move& m : legal_moves(b, false, 64, false)) {
                if (move_uci(m) == uci) {
                    out << pad << move_text(b, m, short_notation) << "\n";
                    b = make_move(b, m);
                    break;
                }
            }
        } else if (k == "d") {
            print_cert_tree(out, r, b, indent + 1, short_notation);
        } else if (k == "n" || k == "p") {
            print_cert_node(out, r, b, indent + 1, short_notation);
        } else if (k == "r") {
            const std::string uci = r.str();
            for (const Move& m : legal_moves(b, false, 64, false)) {
                if (move_uci(m) == uci) {
                    out << pad << move_text(b, m, short_notation) << "\n";
                    b = make_move(b, m);
                    break;
                }
            }
        } else {
            // A terminal marker: mate, stalemate, selfmated, helpmated, ...
            r.skip_value();
        }
    }
}

// A `[...]` array of defender branches.
void print_cert_tree(std::ostream& out, CertReader& r, Board b, int indent,
                     bool short_notation) {
    if (!r.eat('[')) {
        // A single nested object rather than an array.
        print_cert_node(out, r, b, indent, short_notation);
        return;
    }
    while (!r.eat(']')) {
        if (r.i >= r.s.size()) return;
        print_cert_node(out, r, b, indent, short_notation);
        r.skip();
    }
}
} // namespace mateprover

#endif // MATEPROVER_REPORT_H_INCLUDED
