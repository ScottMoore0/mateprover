// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (c) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// absproof.h -- Certificates for "no mate within k" and "the shortest mate is N".
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_ABSPROOF_H_INCLUDED
#define MATEPROVER_ABSPROOF_H_INCLUDED

namespace mateprover {

// --emit-proof certifies "the side to move forces mate within N". It cannot
// certify the two claims a prover makes that a finder cannot: "there is no mate
// within k", and "the shortest mate is N". Both are claims about EVERY attacker
// move, not about one winning line. This module builds certificates for them in
// the formats specified in docs/PROOF_FORMAT.md:
//
//   matebench-absence-1     no mate within k
//   matebench-minimality-1  a mate certificate of depth N, plus an absence
//                           certificate for N - 1
//
// CONSTRUCTION. At an attacker node with j moves left, every legal attacker move
// is refuted:
//
//   * a move that mates means a mate within the bound exists: no certificate;
//   * with j == 1, or when the defender has no legal move, the move is a leaf;
//   * otherwise the defender's replies are tried in turn. Each is put to this
//     engine's own search -- through solve_line, exactly as refute_search does --
//     and the first reply DISPROVED within j - 1 is refuted recursively.
//
// Only a Disproved search counts as evidence. A timeout can make a build
// inconclusive; it can never produce a certificate. And nothing here is trusted
// downstream: a checker written from docs/PROOF_FORMAT.md, such as the one in
// tests/run_tests.py, re-derives every move from the rules, so a bug
// in this builder can produce a certificate that fails, never a false result.
//
// SHARING. A position reached again with the same moves left is built once and
// referenced, which is what the format allows and what keeps
// transpositions from multiplying the certificate.
//
// SIZE. The certificate lists every attacker move at every attacker node, so it
// grows exponentially with the bound. --absence-max-nodes caps the number of
// shared nodes; a build that reaches the cap reports `too-large`, never a partial
// certificate.

constexpr std::uint64_t kAbsenceDefaultMaxNodes = 200000;

class AbsenceBuilder {
public:
    enum class Status { Ok, MateExists, Inconclusive, TooLarge };

    AbsenceBuilder(const SearchConfig& budget, std::uint64_t max_nodes)
        : budget_(budget), max_nodes_(max_nodes) {}

    // On Ok, `id` names a node proving the side to move cannot mate within j.
    Status build(const Board& b, int j, std::string& id) {
        const std::string key = fen4(b) + "|" + std::to_string(j);
        const auto done = ids_.find(key);
        if (done != ids_.end()) {
            id = done->second;
            return Status::Ok;
        }
        const auto known = failed_.find(key);
        if (known != failed_.end()) {
            return known->second;
        }

        std::string body = "{\"m\":[";
        bool first = true;
        for (const Move& m : legal_moves(b)) {
            const Board after = make_move(b, m);
            const std::vector<Move> replies = legal_moves(after);
            if (replies.empty() && in_check(after, after.stm)) {
                return fail(key, Status::MateExists);
            }
            body += first ? "" : ",";
            first = false;
            body += "{\"a\":\"" + move_uci(m) + "\"";
            if (j == 1 || replies.empty()) {
                body += "}";
                continue;
            }
            bool refuted = false;
            bool inconclusive = false;
            for (const Move& r : replies) {
                const Board child = make_move(after, r);
                const RefuteSearch s = refute_search(child, j - 1, budget_, false);
                ++searches_;
                nodes_searched_ += s.nodes;
                if (s.kind == RefuteSearch::Disproved) {
                    std::string sub;
                    const Status st = build(child, j - 1, sub);
                    if (st == Status::Ok) {
                        body += ",\"r\":\"" + move_uci(r) + "\",\"p\":{\"ref\":\"" + sub + "\"}}";
                        refuted = true;
                        break;
                    }
                    if (st == Status::TooLarge) {
                        return fail(key, st);
                    }
                    // The search found no mate and the build found one, or ran
                    // out: either way this reply is not evidence.
                    inconclusive = true;
                } else if (s.kind != RefuteSearch::Proved) {
                    inconclusive = true;
                }
            }
            if (!refuted) {
                return fail(key, inconclusive ? Status::Inconclusive : Status::MateExists);
            }
        }
        body += "]}";
        if (table_.size() >= max_nodes_) {
            return fail(key, Status::TooLarge);
        }
        id = "n" + std::to_string(table_.size());
        table_.emplace_back(id, std::move(body));
        ids_.emplace(key, id);
        return Status::Ok;
    }

    std::string certificate(const std::string& root, int k) const {
        std::string out = "{\"format\":\"matebench-absence-1\",\"k\":" + std::to_string(k) +
                          ",\"proof\":{\"ref\":\"" + root + "\"},\"nodes\":{";
        for (std::size_t i = 0; i < table_.size(); ++i) {
            out += (i ? ",\"" : "\"") + table_[i].first + "\":" + table_[i].second;
        }
        return out + "}}";
    }

    std::uint64_t searches() const { return searches_; }
    std::uint64_t nodes_searched() const { return nodes_searched_; }
    std::size_t size() const { return table_.size(); }

    static const char* name(Status s) {
        switch (s) {
            case Status::Ok: return "ok";
            case Status::MateExists: return "mate-exists";
            case Status::Inconclusive: return "inconclusive";
            case Status::TooLarge: return "too-large";
        }
        return "error";
    }

private:
    Status fail(const std::string& key, Status s) {
        failed_.emplace(key, s);
        return s;
    }

    const SearchConfig& budget_;
    std::uint64_t max_nodes_;
    std::unordered_map<std::string, std::string> ids_;
    std::unordered_map<std::string, Status> failed_;
    std::vector<std::pair<std::string, std::string>> table_;
    std::uint64_t searches_ = 0;
    std::uint64_t nodes_searched_ = 0;
};

inline SearchConfig absproof_budget(const SearchConfig& config) {
    SearchConfig budget = config;
    if (budget.node_limit == 0 && budget.time_limit <= 0.0) {
        budget.node_limit = kRefuteDefaultNodes;
    }
    // A certificate takes thousands of small, complete searches. Starting a thread
    // pool and sizing a full hint table for each one costs more than the search
    // itself, and neither changes what a completed search proves or disproves.
    budget.threads = 1;
    budget.hint_entries = std::min<std::size_t>(budget.hint_entries, 1u << 12);
    return budget;
}

// --absence-proof: for each position, a certificate that the side to move cannot
// force mate within k, where k is -z, else the line's own dm N or bm #N.
inline void absence_proof_line(const std::string& raw, int requested_depth, const SearchConfig& config,
                               std::uint64_t max_nodes, std::ostream& out) {
    const std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    const auto parsed = parse_fen4(line);
    if (!parsed) {
        out << line << "; absence; error input;\n";
        out.flush();
        return;
    }
    const int k = requested_depth > 0 ? requested_depth : infer_mate_depth(line);
    if (k <= 0) {
        out << fen4(*parsed) << "; absence; error no bound;\n";
        out.flush();
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    const SearchConfig budget = absproof_budget(config);
    AbsenceBuilder builder(budget, max_nodes);
    std::string root;
    const AbsenceBuilder::Status st = builder.build(*parsed, k, root);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    out << fen4(*parsed) << "; absence " << k << "; " << AbsenceBuilder::name(st) << "; nodes "
        << builder.size() << "; searches " << builder.searches() << "; acn " << builder.nodes_searched()
        << "; acs " << seconds;
    if (st == AbsenceBuilder::Status::Ok) {
        out << "; absproof " << builder.certificate(root, k);
    }
    out << ";\n";
    out.flush();
}

// --minimality-proof: for each position, the shortest mate N with its mate
// certificate, and an absence certificate for N - 1. The search is this
// engine's iterative deepening, capped at -z or the line's own dm/bm.
inline void minimality_proof_line(const std::string& raw, int requested_depth, const SearchConfig& config,
                                  std::uint64_t max_nodes, std::ostream& out) {
    const std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    const auto parsed = parse_fen4(line);
    if (!parsed) {
        out << line << "; minimality; error input;\n";
        out.flush();
        return;
    }
    const int cap = requested_depth > 0 ? requested_depth : infer_mate_depth(line);
    if (cap <= 0) {
        out << fen4(*parsed) << "; minimality; error no bound;\n";
        out.flush();
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    SearchConfig c = absproof_budget(config);
    c.portfolio = false;
    c.direct_depth = false;
    c.emit_proof = true;
    c.progress = false;
    c.progress_moves = false;
    c.all_solutions = false;
    c.successors = false;
    c.print_tree = false;
    c.profile = false;
    std::ostringstream sink;
    solve_line(fen4(*parsed), cap, c, sink);
    const std::string result = sink.str();
    const std::string dm = refute_field(result, "dm");
    const std::size_t proof_at = result.find("; proof ");
    const std::size_t proof_end = result.find_last_of(';');
    if (dm.empty() || proof_at == std::string::npos || proof_end == std::string::npos || proof_end <= proof_at + 8) {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        out << fen4(*parsed) << "; minimality; no mate proved within " << cap << "; acs " << seconds << ";\n";
        out.flush();
        return;
    }
    const int n = std::atoi(dm.c_str());
    const std::string mate = result.substr(proof_at + 8, proof_end - (proof_at + 8));

    // The absence half's own searches need no mate certificates.
    SearchConfig search = c;
    search.emit_proof = false;
    AbsenceBuilder builder(search, max_nodes);
    AbsenceBuilder::Status st = AbsenceBuilder::Status::Ok;
    std::string root;
    if (n > 1) {
        st = builder.build(*parsed, n - 1, root);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    out << fen4(*parsed) << "; dm " << n << "; minimality " << AbsenceBuilder::name(st) << "; nodes "
        << builder.size() << "; searches " << builder.searches() << "; acs " << seconds;
    if (st == AbsenceBuilder::Status::Ok) {
        out << "; minproof {\"format\":\"matebench-minimality-1\",\"n\":" << n << ",\"mate\":" << mate
            << ",\"no_shorter\":" << (n > 1 ? builder.certificate(root, n - 1) : std::string("null")) << "}";
    }
    out << ";\n";
    out.flush();
}

}  // namespace mateprover

#endif  // MATEPROVER_ABSPROOF_H_INCLUDED
