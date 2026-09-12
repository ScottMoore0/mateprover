// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (c) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// refute.h -- Hunt for a refutation of a claimed mating line.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_REFUTE_H_INCLUDED
#define MATEPROVER_REFUTE_H_INCLUDED

namespace mateprover {

// A ONE-SIDED TEST, and the name says so.
//
// Given a position, a claimed mate distance and a claimed principal variation,
// look for something that breaks the claim. There are two answers:
//
//   refuted     here is the defect, and the evidence for it
//   unrefuted   nothing was found within the budget
//
// "unrefuted" is NOT "verified". Verifying a forced mate means refuting every
// defender reply at every defender node, which is the whole proof: the line
// being checked is the cheap part, and its siblings are nearly all of the cost.
// So no amount of walking one line confirms it, and this mode never says so.
//
// What it CAN find cheaply are the common defects:
//
//   illegal    a move in the line is not legal where it is played
//   not-mate   the line does not end with the defender checkmated
//   length     the line is not as long as the claimed distance implies
//   escape     a defender reply the line did not consider survives past the
//              claimed distance -- proved, by an exhaustive search
//   shorter    a mate in fewer moves exists where the line claims more
//
// The two search-based checks run from the END of the line back to the root.
// Near the end the remaining distance is small, each search is cheap, and that
// is where broken lines usually break. The root check -- "is the claimed
// distance itself too long?" -- is the deepest search, so it runs last.

// Each search check gets the caller's --node-limit or --time-limit. With
// neither, this: a refutation hunt that could run forever by default is not a
// triage tool.
constexpr std::uint64_t kRefuteDefaultNodes = 10000000;

struct RefuteSearch {
    enum Kind { Proved, Disproved, Timeout, Error } kind = Error;
    int depth = 0;
    std::uint64_t nodes = 0;
    std::string line;  // the sub-search's own result line, verbatim
};

inline std::string refute_field(const std::string& line, const std::string& key) {
    const std::string needle = "; " + key + " ";
    const std::size_t at = line.find(needle);
    if (at == std::string::npos) {
        return std::string();
    }
    const std::size_t start = at + needle.size();
    const std::size_t end = line.find(';', start);
    return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Every search here goes through solve_line -- the one entry point the EPD
// interface itself uses -- and its answer is read back off the documented
// result line. There is no second search path to drift from the engine.
//
// The portfolio is OFF for all of them. A restricted lane cannot prove absence,
// and an `escape` refutation IS an absence proof. `--direct-depth` throughout,
// because "is there a mate within r?" is exactly the question each check asks.
inline RefuteSearch refute_search(const Board& b, int depth, const SearchConfig& base,
                                  bool want_proof) {
    SearchConfig c = base;
    c.portfolio = false;
    c.direct_depth = true;
    c.emit_proof = want_proof;
    c.progress = false;
    c.progress_moves = false;
    c.all_solutions = false;
    c.successors = false;
    c.print_tree = false;
    c.profile = false;
    std::ostringstream sink;
    solve_line(fen4(b), depth, c, sink);
    RefuteSearch r;
    r.line = sink.str();
    const std::string acn = refute_field(r.line, "acn");
    if (!acn.empty()) {
        r.nodes = std::strtoull(acn.c_str(), nullptr, 10);
    }
    const std::string dm = refute_field(r.line, "dm");
    if (!dm.empty()) {
        r.kind = RefuteSearch::Proved;
        r.depth = std::atoi(dm.c_str());
    } else if (r.line.find("; timeout") != std::string::npos) {
        r.kind = RefuteSearch::Timeout;
    } else if (r.line.find("; acs ") != std::string::npos &&
               r.line.find("error") == std::string::npos) {
        // The line ends after `acs`: searched to the end, no mate within depth.
        r.kind = RefuteSearch::Disproved;
    }
    return r;
}

// A glued move number (`1.e4`, `12...Kh8`), and trailing check, mate and
// annotation marks. None of them changes which move is meant.
inline std::string refute_normalise(std::string t) {
    std::size_t i = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
        ++i;
    }
    if (i > 0 && i < t.size() && t[i] == '.') {
        while (i < t.size() && t[i] == '.') {
            ++i;
        }
        t = t.substr(i);
    }
    while (!t.empty() && (t.back() == '+' || t.back() == '#' || t.back() == '!' || t.back() == '?')) {
        t.pop_back();
    }
    if (t == "0-0") t = "O-O";
    if (t == "0-0-0") t = "O-O-O";
    return t;
}

inline bool refute_is_move_number(const std::string& t) {
    std::size_t i = 0;
    while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i]))) {
        ++i;
    }
    if (i == 0) {
        return false;
    }
    while (i < t.size() && t[i] == '.') {
        ++i;
    }
    return i == t.size();
}

// A move is matched against the engine's own legal moves, never trusted: UCI
// coordinates first, then SAN. A token that matches nothing is the `illegal`
// refutation, not a parse error, because in a claimed line it is exactly that.
inline std::optional<Move> refute_match(const Board& b, const std::string& raw) {
    const std::string t = refute_normalise(raw);
    if (t.empty()) {
        return std::nullopt;
    }
    const std::vector<Move> moves = legal_moves(b);
    for (const Move& m : moves) {
        if (move_uci(m) == t) {
            return m;
        }
    }
    for (const Move& m : moves) {
        std::string san = move_san(b, m);
        while (!san.empty() && (san.back() == '+' || san.back() == '#')) {
            san.pop_back();
        }
        if (san == t) {
            return m;
        }
    }
    return std::nullopt;
}

// The `pv` opcode: as this engine writes it (`; pv m1 m2 ...;`), as an EPD line
// carries it (`... bm #3; pv m1 m2 ...;`), or directly after the position. The
// four position fields can never contain the word.
inline std::string refute_pv_text(const std::string& line) {
    std::size_t i = 0;
    int fields = 0;
    while (i < line.size() && fields < 4) {
        while (i < line.size() && line[i] == ' ') {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }
        while (i < line.size() && line[i] != ' ' && line[i] != ';') {
            ++i;
        }
        ++fields;
    }
    const std::size_t prefix_end = i;
    for (std::size_t pos = line.find("pv ", prefix_end); pos != std::string::npos;
         pos = line.find("pv ", pos + 3)) {
        std::size_t k = pos;
        while (k > prefix_end && line[k - 1] == ' ') {
            --k;
        }
        if (k == prefix_end || line[k - 1] == ';') {
            const std::size_t start = pos + 3;
            const std::size_t end = line.find(';', start);
            return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        }
    }
    return std::string();
}

inline bool refute_mated(const Board& b) {
    return in_check(b, b.stm) && legal_moves(b).empty();
}

inline void refute_pv_line(const std::string& raw, int requested_depth, const SearchConfig& config,
                           std::ostream& out) {
    const std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        out << line << "; refute-pv; error input;\n";
        out.flush();
        return;
    }
    const Board root = *parsed;
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t nodes = 0;
    int checks = 0;
    int inconclusive = 0;

    SearchConfig budget = config;
    if (budget.node_limit == 0 && budget.time_limit <= 0.0) {
        budget.node_limit = kRefuteDefaultNodes;
    }

    std::ostringstream head;
    head << fen4(root) << "; refute-pv";

    auto finish = [&](const std::string& body) {
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        out << head.str() << body << "; checks " << checks << "; inconclusive " << inconclusive
            << "; acn " << nodes << "; acs " << seconds << ";\n";
        out.flush();
    };

    const std::string pv_text = refute_pv_text(line);
    std::vector<std::string> tokens;
    {
        std::istringstream ts(pv_text);
        for (std::string t; ts >> t;) {
            if (!refute_is_move_number(t)) {
                tokens.push_back(t);
            }
        }
    }
    if (tokens.empty()) {
        out << head.str() << "; error no pv;\n";
        out.flush();
        return;
    }

    // The claimed distance comes from -z, else the line's own `dm N` or `bm #N`
    // -- read with the PV removed, since a SAN line is full of `#` -- else the
    // length of the line itself.
    std::string without_pv = line;
    if (!pv_text.empty()) {
        const std::size_t at = without_pv.find(pv_text);
        if (at != std::string::npos) {
            without_pv.erase(at, pv_text.size());
        }
    }
    int claim = requested_depth > 0 ? requested_depth : infer_mate_depth(without_pv);
    const bool inferred = claim <= 0;
    if (inferred) {
        claim = static_cast<int>((tokens.size() + 1) / 2);
    }
    head << "; claim " << claim << (inferred ? "; claim from-pv" : "") << "; plies " << tokens.size();

    // 1. Replay, through the engine's own move generator.
    std::vector<Board> boards{root};
    std::vector<Move> moves;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto m = refute_match(boards.back(), tokens[i]);
        if (!m) {
            finish("; refuted illegal; ply " + std::to_string(i + 1) + "; token " + tokens[i]);
            return;
        }
        moves.push_back(*m);
        boards.push_back(make_move(boards.back(), *m));
    }
    const int plies = static_cast<int>(moves.size());
    const Board& last = boards.back();

    // 2. It must end with the DEFENDER checkmated.
    const bool mated = refute_mated(last);
    if (!mated || last.stm == root.stm) {
        std::string why = "game continues";
        if (mated) {
            why = "attacker mated";
        } else if (legal_moves(last).empty()) {
            why = "stalemate";
        }
        finish("; refuted not-mate; ply " + std::to_string(plies) + "; ends " + why);
        return;
    }

    // 3. It must be as long as the claim says.
    const int expected = 2 * claim - 1;
    if (plies != expected) {
        finish("; refuted length; expected " + std::to_string(expected) + "; mates-in " +
               std::to_string((plies + 1) / 2));
        return;
    }

    // Directmate convention ignores threefold repetition, so this is reported,
    // never counted as a refutation. A game-rules consumer will care.
    if (config.flag_repetition && max_position_repeats(root, moves) >= 3) {
        head << "; rep3";
    }

    // 4. From the end of the line back to the root.
    for (int ply = plies - 1; ply >= 0; --ply) {
        const Board& at = boards[static_cast<std::size_t>(ply)];
        const std::string played = move_uci(moves[static_cast<std::size_t>(ply)]);
        if (ply % 2 == 1) {
            // A defender node. Every reply the line did not play must still
            // allow mate within the attacker moves that remain after it.
            const int remaining = (plies - ply) / 2;
            for (const Move& reply : legal_moves(at)) {
                if (move_uci(reply) == played) {
                    continue;
                }
                const RefuteSearch r = refute_search(make_move(at, reply), remaining, budget, false);
                ++checks;
                nodes += r.nodes;
                if (r.kind == RefuteSearch::Disproved) {
                    finish("; refuted escape; ply " + std::to_string(ply + 1) + "; after " +
                           move_uci(moves[static_cast<std::size_t>(ply - 1)]) + "; reply " +
                           move_uci(reply) + "; survives " + std::to_string(remaining));
                    return;
                }
                if (r.kind != RefuteSearch::Proved) {
                    ++inconclusive;
                }
            }
        } else {
            // An attacker node. A mate in fewer moves than the line claims
            // from here means the claim -- or, below the root, the defender's
            // previous move -- is not the best play.
            const int remaining = (plies - ply + 1) / 2;
            if (remaining < 2) {
                continue;
            }
            const RefuteSearch r = refute_search(at, remaining - 1, budget, config.emit_proof);
            ++checks;
            nodes += r.nodes;
            if (r.kind == RefuteSearch::Proved) {
                finish("; refuted shorter; ply " + std::to_string(ply + 1) + "; claimed " +
                       std::to_string(remaining) + "; found " + std::to_string(r.depth));
                if (config.emit_proof) {
                    // The evidence is the sub-search's own result line, so
                    // tools/verify_proof.py checks it exactly as it checks any.
                    out << r.line;
                    out.flush();
                }
                return;
            }
            if (r.kind != RefuteSearch::Disproved) {
                ++inconclusive;
            }
        }
    }
    finish("; unrefuted");
}

}  // namespace mateprover

#endif  // MATEPROVER_REFUTE_H_INCLUDED
