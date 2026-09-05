// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (c) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// uci.h -- A lossy convenience interface speaking the UCI protocol.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_UCI_H_INCLUDED
#define MATEPROVER_UCI_H_INCLUDED

namespace mateprover {

// ---------------------------------------------------------------------------
// UCI IS A CONVENIENCE INTERFACE AND MUST NEVER BE THE AUTHORITY.
//
// The protocol was designed for engines that choose moves, and it can carry the
// half of this program's answer that resembles one. It cannot carry the other
// half, and the difference is not cosmetic:
//
//   "mate in N"          maps exactly. `go mate <x>` is in the specification --
//                        "search for a mate in x moves" -- and `score mate N`
//                        means what `dm N` means.
//
//   "NO mate exists"     has no representation at all. A GUI that sees no
//                        `score mate` cannot tell an exhaustive disproof from a
//                        search that ran out of time, and conflating those two
//                        is the single thing docs/OUTPUT_FORMAT.md exists to
//                        prevent. Here they are separated on `info string`,
//                        which is visible to a human and legible to nothing
//                        else. That is the cost, and it is stated rather than
//                        hidden.
//
//   the certificate      cannot travel. A mate-in-8 proof tree runs to
//                        megabytes. --emit-proof is refused in this mode rather
//                        than silently doing nothing.
//
// THE ANSWER IS RENDERED, NOT RECOMPUTED. Everything below drives `solve_line`
// -- the same entry point the EPD interface uses, with the same portfolio, the
// same routes and the same gates -- and then reformats its result line. There
// is deliberately no second search path: a UCI answer is provably a rendering
// of the canonical one, so this file cannot drift from the engine's real
// behaviour however long it goes unexamined.

struct UciResult {
    bool solved = false;
    bool timed_out = false;
    std::string best;          // `bm` token, empty when nothing was proved
    std::string verdict_token; // "dm", "sfm", ... as reported
    int depth = 0;
    std::string pv;
    std::uint64_t nodes = 0;
    double seconds = 0.0;
};

// Pull one field out of an EPD result line. The line format is specified in
// docs/OUTPUT_FORMAT.md and exercised by the suite, so parsing it is parsing a
// documented contract rather than guessing at output.
inline bool uci_field(const std::string& line, const std::string& key, std::string& out) {
    const std::string needle = "; " + key + " ";
    const std::size_t at = line.find(needle);
    if (at == std::string::npos) {
        return false;
    }
    const std::size_t start = at + needle.size();
    const std::size_t end = line.find(';', start);
    out = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return true;
}

inline UciResult parse_result_line(const std::string& line) {
    UciResult r;
    std::string value;
    if (uci_field(line, "acn", value)) r.nodes = std::strtoull(value.c_str(), nullptr, 10);
    if (uci_field(line, "acs", value)) r.seconds = std::strtod(value.c_str(), nullptr);
    if (uci_field(line, "bm", value)) r.best = value;
    if (uci_field(line, "pv", value)) r.pv = value;
    // A `timeout` marker means the search gave up. Its ABSENCE on a line with no
    // verdict is the strong statement -- searched exhaustively, none exists --
    // and the two must not be conflated.
    r.timed_out = line.find("; timeout;") != std::string::npos;
    for (const char* token : {"dm", "sm", "sfm", "ssm", "hm", "hsm"}) {
        if (uci_field(line, token, value)) {
            r.verdict_token = token;
            r.depth = std::atoi(value.c_str());
            r.solved = true;
            break;
        }
    }
    return r;
}

// One UCI session. Single instance, owned by run_uci_loop.
class UciSession {
public:
    explicit UciSession(const SearchConfig& base) : base_(base) {
        base_.progress = false;         // `info` replaces the bespoke stream
        base_.progress_moves = false;
        base_.emit_proof = false;       // refused in this mode; see the header note
        position_ = kStartFen;
    }

    ~UciSession() { join(); }

    // Returns false when the session should end.
    bool command(const std::string& raw) {
        std::istringstream in(raw);
        std::string word;
        if (!(in >> word)) {
            return true;
        }
        if (word == "uci") return handle_uci();
        if (word == "isready") return reply("readyok");
        if (word == "ucinewgame") return true;
        if (word == "setoption") return handle_setoption(in);
        if (word == "position") return handle_position(in);
        if (word == "go") return handle_go(in);
        if (word == "stop") { stop(); return true; }
        if (word == "quit") { stop(); return false; }
        // Unknown commands are IGNORED, which the specification requires: a GUI
        // may send anything and an engine that died on it would be unusable.
        // Note the asymmetry with the EPD interface, which rejects bad input
        // loudly -- there, silence could be mistaken for an answer.
        return true;
    }

private:
    static constexpr const char* kStartFen =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -";

    static bool reply(const std::string& text) {
        std::cout << text << "\n" << std::flush;
        return true;
    }

    bool handle_uci() {
        std::cout << "id name MateProver " MATEPROVER_VERSION "\n"
                  << "id author Scott Moore\n"
                  // Only options a GUI can meaningfully populate. The goals and
                  // the variant rules are reachable, but no GUI knows what to
                  // do with them, so they are declared rather than advertised
                  // as though they were ordinary settings.
                  << "option name Hash type spin default 256 min 1 max 1048576\n"
                  << "option name Threads type spin default 1 min 1 max 1024\n"
                  << "option name Goal type combo default mate var mate var stalemate"
                     " var selfmate var selfstalemate var helpmate var helpstalemate\n"
                  << "option name Checks type spin default 0 min 0 max 126\n"
                  << "option name Captures type spin default 0 min 0 max 126\n"
                  << "option name Escape type spin default 0 min 0 max 8\n"
                  << "uciok\n" << std::flush;
        return true;
    }

    bool handle_setoption(std::istringstream& in) {
        std::string word, name, value;
        while (in >> word) {
            if (word == "name") {
                name.clear();
                while (in >> word && word != "value") {
                    name += (name.empty() ? "" : " ") + word;
                }
                if (word == "value") {
                    std::getline(in, value);
                    value = trim(value);
                }
                break;
            }
        }
        const long n = value.empty() ? 0 : std::strtol(value.c_str(), nullptr, 10);
        if (name == "Hash") base_.memory_mb = static_cast<std::size_t>(std::max(1L, n));
        else if (name == "Threads") base_.threads = static_cast<int>(std::max(1L, n));
        else if (name == "Goal") {
            if (value == "mate") base_.goal = Goal::Mate;
            else if (value == "stalemate") base_.goal = Goal::Stalemate;
            else if (value == "selfmate") base_.goal = Goal::Selfmate;
            else if (value == "selfstalemate") base_.goal = Goal::Selfstalemate;
            else if (value == "helpmate") base_.goal = Goal::Helpmate;
            else if (value == "helpstalemate") base_.goal = Goal::Helpstalemate;
        } else if (name == "Checks" && n > 0) {
            base_.quota_limit[VR_CHECK] = static_cast<int>(n);
            base_.quota_limit[VR_COUNT + VR_CHECK] = static_cast<int>(n);
        } else if (name == "Captures" && n > 0) {
            base_.quota_limit[VR_CAPTURE] = static_cast<int>(n);
            base_.quota_limit[VR_COUNT + VR_CAPTURE] = static_cast<int>(n);
        } else if (name == "Escape" && n > 0) {
            base_.quota_limit[VR_ESCAPE] = static_cast<int>(n);
            base_.quota_limit[VR_COUNT + VR_ESCAPE] = static_cast<int>(n);
        }
        return true;
    }

    // `position [startpos | fen <fen>] [moves <m1> <m2> ...]`
    //
    // The move list is applied through the engine's own generator and rejected
    // if a move is not legal, rather than being trusted. A GUI that sent a
    // wrong move would otherwise silently change which position was analysed.
    bool handle_position(std::istringstream& in) {
        // Never change the position under a running search. A GUI waits for
        // `bestmove` before sending the next command, but nothing enforces
        // that, and a search whose board changed underneath it would report an
        // answer about a position nobody asked about.
        join();
        std::string word;
        std::string fen;
        if (!(in >> word)) return true;
        if (word == "startpos") {
            fen = kStartFen;
        } else if (word == "fen") {
            std::vector<std::string> fields;
            while (in >> word && word != "moves") {
                fields.push_back(word);
            }
            // The first four fields are the position; a full FEN's move
            // counters are accepted and discarded, since nothing here depends
            // on them.
            for (std::size_t i = 0; i < fields.size() && i < 4; ++i) {
                fen += (i ? " " : "") + fields[i];
            }
            if (word != "moves") {
                position_ = fen;
                return true;
            }
        } else {
            return true;
        }
        auto parsed = parse_fen4(fen);
        if (!parsed) {
            std::cout << "info string rejected: unparseable position\n" << std::flush;
            return true;
        }
        Board b = *parsed;
        if (word != "moves") {
            in >> word;
        }
        if (word == "moves") {
            while (in >> word) {
                auto moves = legal_moves(b);
                bool applied = false;
                for (const Move& m : moves) {
                    if (move_uci(m) == word) {
                        b = make_move(b, m);
                        applied = true;
                        break;
                    }
                }
                if (!applied) {
                    std::cout << "info string rejected: illegal move " << word << "\n" << std::flush;
                    return true;
                }
            }
        }
        position_ = fen4(b);
        return true;
    }

    bool handle_go(std::istringstream& in) {
        join();
        int depth = 0;
        double movetime = 0.0;
        std::uint64_t nodes = 0;
        std::string word;
        while (in >> word) {
            if (word == "mate" || word == "depth") in >> depth;
            else if (word == "movetime") { long ms = 0; in >> ms; movetime = ms / 1000.0; }
            else if (word == "nodes") in >> nodes;
            // wtime/btime/winc/binc/movestogo describe a game clock, which a
            // prover has no use for: it is not choosing between moves under
            // time pressure, it is answering a question. Read and discarded.
            else if (word == "wtime" || word == "btime" || word == "winc" ||
                     word == "binc" || word == "movestogo") { long ignored = 0; in >> ignored; }
        }
        if (depth <= 0) {
            // `go infinite`, or a game-clock `go`. Neither names a depth, and a
            // prover cannot answer without one, so the deepest supported search
            // is run and the caller is told what was assumed.
            depth = kDefaultDepth;
            std::cout << "info string no depth given; assuming mate in " << depth
                      << " (use 'go mate N')\n" << std::flush;
        }
        SearchConfig cfg = base_;
        cfg.time_limit = movetime;
        cfg.node_limit = nodes;
        cfg.stop_flag = &stop_;
        stop_.store(false, std::memory_order_relaxed);
        const std::string fen = position_;
        searching_ = true;
        worker_ = std::thread([this, cfg, fen, depth] { search(cfg, fen, depth); });
        return true;
    }

    void search(const SearchConfig& cfg, const std::string& fen, int depth) {
        std::ostringstream sink;
        solve_line(fen, depth, cfg, sink);
        const UciResult r = parse_result_line(sink.str());

        std::ostringstream out;
        out << "info depth " << depth << " nodes " << r.nodes
            << " time " << static_cast<long>(r.seconds * 1000.0);
        if (r.nodes > 0 && r.seconds > 0.0) {
            out << " nps " << static_cast<std::uint64_t>(r.nodes / r.seconds);
        }
        if (r.solved && r.verdict_token == "dm") {
            // The one exact translation in the protocol.
            out << " score mate " << r.depth;
        }
        if (!r.pv.empty()) {
            out << " pv " << r.pv;
        }
        out << "\n";

        // THE PART UCI CANNOT SAY, said on the only channel that can carry it.
        if (r.solved && r.verdict_token != "dm") {
            out << "info string " << r.verdict_token << " " << r.depth
                << " (this goal has no UCI score; see the EPD interface)\n";
        } else if (!r.solved) {
            out << (r.timed_out
                        ? "info string no verdict: the search hit its budget or was stopped\n"
                        : "info string PROVED: no solution exists within the depth searched\n");
        }
        // `bestmove` is mandatory. 0000 is the null move: there is no move to
        // recommend, because the engine was never asked to recommend one, and
        // naming a legal move here would assert something it never proved.
        out << "bestmove " << (r.best.empty() ? "0000" : r.best) << "\n";
        std::cout << out.str() << std::flush;
        searching_ = false;
    }

    void stop() { stop_.store(true, std::memory_order_relaxed); join(); }

    void join() {
        if (worker_.joinable()) {
            worker_.join();
        }
        searching_ = false;
    }

    static constexpr int kDefaultDepth = 10;

    SearchConfig base_;
    std::string position_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> searching_{false};
};

// The command loop. Reads on this thread while the search runs on another, so
// `stop` is answerable while a search is in flight -- which the protocol
// requires and a read-search-print loop cannot provide.
inline int run_uci_loop(const SearchConfig& config) {
    UciSession session(config);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!session.command(trim(line))) {
            break;
        }
    }
    return 0;
}

} // namespace mateprover

#endif // MATEPROVER_UCI_H_INCLUDED
