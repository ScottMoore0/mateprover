// E Chest initial exact directmate prover.
//
// This is a conservative first checkpoint for the E rewrite line. It favors
// auditable correctness over final performance. Later E milestones should
// replace the array board with a bitboard/incremental engine while preserving
// this proof interface and verifier behavior.

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

enum Color { WHITE = 0, BLACK = 1 };

struct Move {
    int from = -1;
    int to = -1;
    char promo = 0;
    bool castle = false;
    bool ep = false;
};

struct Proof {
    bool ok = false;
    std::vector<Move> pv;
    std::string cert;
};

struct Board {
    std::array<char, 64> sq{};
    Color stm = WHITE;
    unsigned castling = 0; // 1 WK, 2 WQ, 4 BK, 8 BQ
    int ep = -1;
};

struct Stats {
    std::uint64_t nodes = 0;
    std::uint64_t tt_hits = 0;
};

struct TTEntry {
    bool ok = false;
    std::vector<Move> pv;
    std::string cert;
};

struct Search {
    Color attacker = WHITE;
    Stats stats;
    std::unordered_map<std::string, TTEntry> tt;
    bool debug = false;
};

bool is_white_piece(char p) {
    return p >= 'A' && p <= 'Z';
}

bool is_black_piece(char p) {
    return p >= 'a' && p <= 'z';
}

bool is_piece_color(char p, Color c) {
    return c == WHITE ? is_white_piece(p) : is_black_piece(p);
}

bool is_enemy_piece(char p, Color c) {
    return p != '.' && !is_piece_color(p, c);
}

bool is_king_piece(char p) {
    return p == 'K' || p == 'k';
}

Color other(Color c) {
    return c == WHITE ? BLACK : WHITE;
}

int file_of(int sq) {
    return sq & 7;
}

int rank_of(int sq) {
    return sq >> 3;
}

bool on_board(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

int square_of(int file, int rank) {
    return rank * 8 + file;
}

std::string sq_name(int sq) {
    std::string out;
    out.push_back(static_cast<char>('a' + file_of(sq)));
    out.push_back(static_cast<char>('1' + rank_of(sq)));
    return out;
}

std::string move_uci(const Move& m) {
    std::string out = sq_name(m.from) + sq_name(m.to);
    if (m.promo) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(m.promo))));
    }
    return out;
}

std::string json_quote(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char ch : s) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

std::string trim(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string tok;
    while (in >> tok) {
        out.push_back(tok);
    }
    return out;
}

std::optional<Board> parse_fen4(const std::string& line) {
    auto tokens = split_ws(line);
    if (tokens.size() < 4) {
        return std::nullopt;
    }
    Board b;
    b.sq.fill('.');

    int rank = 7;
    int file = 0;
    for (char ch : tokens[0]) {
        if (ch == '/') {
            if (file != 8) {
                return std::nullopt;
            }
            --rank;
            file = 0;
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            int n = ch - '0';
            if (n <= 0 || file + n > 8) {
                return std::nullopt;
            }
            file += n;
            continue;
        }
        if (std::string("PNBRQKpnbrqk").find(ch) == std::string::npos) {
            return std::nullopt;
        }
        if (!on_board(file, rank)) {
            return std::nullopt;
        }
        b.sq[square_of(file, rank)] = ch;
        ++file;
    }
    if (rank != 0 || file != 8) {
        return std::nullopt;
    }

    b.stm = tokens[1] == "b" ? BLACK : WHITE;
    b.castling = 0;
    if (tokens[2].find('K') != std::string::npos) b.castling |= 1;
    if (tokens[2].find('Q') != std::string::npos) b.castling |= 2;
    if (tokens[2].find('k') != std::string::npos) b.castling |= 4;
    if (tokens[2].find('q') != std::string::npos) b.castling |= 8;

    b.ep = -1;
    if (tokens[3] != "-" && tokens[3].size() >= 2) {
        int ef = tokens[3][0] - 'a';
        int er = tokens[3][1] - '1';
        if (on_board(ef, er)) {
            b.ep = square_of(ef, er);
        }
    }
    return b;
}

std::string fen4(const Board& b) {
    std::ostringstream out;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            char p = b.sq[square_of(file, rank)];
            if (p == '.') {
                ++empty;
            } else {
                if (empty) {
                    out << empty;
                    empty = 0;
                }
                out << p;
            }
        }
        if (empty) out << empty;
        if (rank) out << '/';
    }
    out << (b.stm == WHITE ? " w " : " b ");
    std::string c;
    if (b.castling & 1) c.push_back('K');
    if (b.castling & 2) c.push_back('Q');
    if (b.castling & 4) c.push_back('k');
    if (b.castling & 8) c.push_back('q');
    out << (c.empty() ? "-" : c) << ' ';
    out << (b.ep >= 0 ? sq_name(b.ep) : "-");
    return out.str();
}

int king_square(const Board& b, Color c) {
    char k = c == WHITE ? 'K' : 'k';
    for (int i = 0; i < 64; ++i) {
        if (b.sq[i] == k) {
            return i;
        }
    }
    return -1;
}

bool attacked_by_slider(const Board& b, int target, Color by, const int* dirs, int ndirs, const std::string& pieces) {
    int tf = file_of(target);
    int tr = rank_of(target);
    for (int i = 0; i < ndirs; ++i) {
        int df = dirs[i * 2];
        int dr = dirs[i * 2 + 1];
        int f = tf + df;
        int r = tr + dr;
        while (on_board(f, r)) {
            char p = b.sq[square_of(f, r)];
            if (p != '.') {
                if (is_piece_color(p, by)) {
                    char lp = static_cast<char>(std::tolower(static_cast<unsigned char>(p)));
                    if (pieces.find(lp) != std::string::npos) {
                        return true;
                    }
                }
                break;
            }
            f += df;
            r += dr;
        }
    }
    return false;
}

bool is_attacked(const Board& b, int target, Color by) {
    int tf = file_of(target);
    int tr = rank_of(target);

    int pawn_rank = by == WHITE ? tr - 1 : tr + 1;
    for (int df : {-1, 1}) {
        int f = tf + df;
        if (on_board(f, pawn_rank)) {
            char p = b.sq[square_of(f, pawn_rank)];
            if (p == (by == WHITE ? 'P' : 'p')) {
                return true;
            }
        }
    }

    static const int knight_delta[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
    };
    for (auto& d : knight_delta) {
        int f = tf + d[0];
        int r = tr + d[1];
        if (on_board(f, r)) {
            char p = b.sq[square_of(f, r)];
            if (p == (by == WHITE ? 'N' : 'n')) {
                return true;
            }
        }
    }

    static const int bishop_dirs[8] = {1, 1, 1, -1, -1, 1, -1, -1};
    static const int rook_dirs[8] = {1, 0, -1, 0, 0, 1, 0, -1};
    if (attacked_by_slider(b, target, by, bishop_dirs, 4, "bq")) {
        return true;
    }
    if (attacked_by_slider(b, target, by, rook_dirs, 4, "rq")) {
        return true;
    }

    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) continue;
            int f = tf + df;
            int r = tr + dr;
            if (on_board(f, r)) {
                char p = b.sq[square_of(f, r)];
                if (p == (by == WHITE ? 'K' : 'k')) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool in_check(const Board& b, Color c) {
    int k = king_square(b, c);
    return k < 0 || is_attacked(b, k, other(c));
}

void add_move(std::vector<Move>& moves, int from, int to, char promo = 0, bool castle = false, bool ep = false) {
    Move m;
    m.from = from;
    m.to = to;
    m.promo = promo;
    m.castle = castle;
    m.ep = ep;
    moves.push_back(m);
}

void gen_pseudo(const Board& b, std::vector<Move>& moves) {
    Color us = b.stm;
    for (int from = 0; from < 64; ++from) {
        char p = b.sq[from];
        if (!is_piece_color(p, us)) continue;
        char lp = static_cast<char>(std::tolower(static_cast<unsigned char>(p)));
        int f = file_of(from);
        int r = rank_of(from);

        if (lp == 'p') {
            int dir = us == WHITE ? 1 : -1;
            int start_rank = us == WHITE ? 1 : 6;
            int promo_rank = us == WHITE ? 7 : 0;
            int one_r = r + dir;
            if (on_board(f, one_r)) {
                int one = square_of(f, one_r);
                if (b.sq[one] == '.') {
                    if (one_r == promo_rank) {
                        for (char pr : {'q', 'r', 'b', 'n'}) add_move(moves, from, one, pr);
                    } else {
                        add_move(moves, from, one);
                        int two_r = r + 2 * dir;
                        if (r == start_rank && on_board(f, two_r)) {
                            int two = square_of(f, two_r);
                            if (b.sq[two] == '.') add_move(moves, from, two);
                        }
                    }
                }
            }
            for (int df : {-1, 1}) {
                int cf = f + df;
                int cr = r + dir;
                if (!on_board(cf, cr)) continue;
                int to = square_of(cf, cr);
                if ((is_enemy_piece(b.sq[to], us) && !is_king_piece(b.sq[to])) || to == b.ep) {
                    bool is_ep = to == b.ep && b.sq[to] == '.';
                    if (cr == promo_rank) {
                        for (char pr : {'q', 'r', 'b', 'n'}) add_move(moves, from, to, pr, false, is_ep);
                    } else {
                        add_move(moves, from, to, 0, false, is_ep);
                    }
                }
            }
        } else if (lp == 'n') {
            static const int kd[8][2] = {
                {1, 2}, {2, 1}, {2, -1}, {1, -2},
                {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
            };
            for (auto& d : kd) {
                int tf = f + d[0], tr = r + d[1];
                if (!on_board(tf, tr)) continue;
                int to = square_of(tf, tr);
                if (!is_piece_color(b.sq[to], us) && !is_king_piece(b.sq[to])) add_move(moves, from, to);
            }
        } else if (lp == 'b' || lp == 'r' || lp == 'q') {
            static const int dirs[8][2] = {
                {1, 0}, {-1, 0}, {0, 1}, {0, -1},
                {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
            };
            int first = lp == 'b' ? 4 : 0;
            int last = lp == 'r' ? 4 : 8;
            for (int i = first; i < last; ++i) {
                int tf = f + dirs[i][0];
                int tr = r + dirs[i][1];
                while (on_board(tf, tr)) {
                    int to = square_of(tf, tr);
                    if (is_piece_color(b.sq[to], us)) break;
                    if (is_king_piece(b.sq[to])) break;
                    add_move(moves, from, to);
                    if (b.sq[to] != '.') break;
                    tf += dirs[i][0];
                    tr += dirs[i][1];
                }
            }
        } else if (lp == 'k') {
            for (int df = -1; df <= 1; ++df) {
                for (int dr = -1; dr <= 1; ++dr) {
                    if (df == 0 && dr == 0) continue;
                    int tf = f + df, tr = r + dr;
                    if (!on_board(tf, tr)) continue;
                    int to = square_of(tf, tr);
                    if (!is_piece_color(b.sq[to], us) && !is_king_piece(b.sq[to])) add_move(moves, from, to);
                }
            }
            if (us == WHITE && from == square_of(4, 0) && !in_check(b, WHITE)) {
                if ((b.castling & 1) && b.sq[square_of(5, 0)] == '.' && b.sq[square_of(6, 0)] == '.' &&
                    !is_attacked(b, square_of(5, 0), BLACK) && !is_attacked(b, square_of(6, 0), BLACK)) {
                    add_move(moves, from, square_of(6, 0), 0, true);
                }
                if ((b.castling & 2) && b.sq[square_of(3, 0)] == '.' && b.sq[square_of(2, 0)] == '.' && b.sq[square_of(1, 0)] == '.' &&
                    !is_attacked(b, square_of(3, 0), BLACK) && !is_attacked(b, square_of(2, 0), BLACK)) {
                    add_move(moves, from, square_of(2, 0), 0, true);
                }
            }
            if (us == BLACK && from == square_of(4, 7) && !in_check(b, BLACK)) {
                if ((b.castling & 4) && b.sq[square_of(5, 7)] == '.' && b.sq[square_of(6, 7)] == '.' &&
                    !is_attacked(b, square_of(5, 7), WHITE) && !is_attacked(b, square_of(6, 7), WHITE)) {
                    add_move(moves, from, square_of(6, 7), 0, true);
                }
                if ((b.castling & 8) && b.sq[square_of(3, 7)] == '.' && b.sq[square_of(2, 7)] == '.' && b.sq[square_of(1, 7)] == '.' &&
                    !is_attacked(b, square_of(3, 7), WHITE) && !is_attacked(b, square_of(2, 7), WHITE)) {
                    add_move(moves, from, square_of(2, 7), 0, true);
                }
            }
        }
    }
}

Board make_move(Board b, const Move& m) {
    char p = b.sq[m.from];
    char captured = b.sq[m.to];
    b.sq[m.from] = '.';

    if (m.ep) {
        int cap_sq = m.to + (b.stm == WHITE ? -8 : 8);
        captured = b.sq[cap_sq];
        b.sq[cap_sq] = '.';
    }

    char placed = p;
    if (m.promo) {
        placed = b.stm == WHITE ? static_cast<char>(std::toupper(static_cast<unsigned char>(m.promo))) : m.promo;
    }
    b.sq[m.to] = placed;

    if (m.castle) {
        if (p == 'K' && m.to == square_of(6, 0)) {
            b.sq[square_of(5, 0)] = 'R';
            b.sq[square_of(7, 0)] = '.';
        } else if (p == 'K' && m.to == square_of(2, 0)) {
            b.sq[square_of(3, 0)] = 'R';
            b.sq[square_of(0, 0)] = '.';
        } else if (p == 'k' && m.to == square_of(6, 7)) {
            b.sq[square_of(5, 7)] = 'r';
            b.sq[square_of(7, 7)] = '.';
        } else if (p == 'k' && m.to == square_of(2, 7)) {
            b.sq[square_of(3, 7)] = 'r';
            b.sq[square_of(0, 7)] = '.';
        }
    }

    if (p == 'K') b.castling &= ~3u;
    if (p == 'k') b.castling &= ~12u;
    if (m.from == square_of(0, 0) || m.to == square_of(0, 0) || captured == 'R') b.castling &= ~2u;
    if (m.from == square_of(7, 0) || m.to == square_of(7, 0) || captured == 'R') b.castling &= ~1u;
    if (m.from == square_of(0, 7) || m.to == square_of(0, 7) || captured == 'r') b.castling &= ~8u;
    if (m.from == square_of(7, 7) || m.to == square_of(7, 7) || captured == 'r') b.castling &= ~4u;

    b.ep = -1;
    if (std::tolower(static_cast<unsigned char>(p)) == 'p' && std::abs(m.to - m.from) == 16) {
        b.ep = (m.from + m.to) / 2;
    }
    b.stm = other(b.stm);
    return b;
}

std::vector<Move> legal_moves(const Board& b) {
    std::vector<Move> pseudo;
    gen_pseudo(b, pseudo);
    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    for (const Move& m : pseudo) {
        Board nb = make_move(b, m);
        if (!in_check(nb, other(nb.stm))) {
            legal.push_back(m);
        }
    }
    return legal;
}

bool is_checkmate(const Board& b) {
    return in_check(b, b.stm) && legal_moves(b).empty();
}

int move_score(const Board& b, const Move& m) {
    Board nb = make_move(b, m);
    int score = 0;
    if (is_checkmate(nb)) score += 1000000;
    if (in_check(nb, nb.stm)) score += 50000;
    if (b.sq[m.to] != '.' || m.ep) score += 10000;
    if (m.promo) score += 8000;
    char p = std::tolower(static_cast<unsigned char>(b.sq[m.from]));
    if (p == 'q') score += 50;
    if (p == 'r') score += 40;
    if (p == 'b' || p == 'n') score += 30;
    return score;
}

void order_moves(const Board& b, std::vector<Move>& moves) {
    std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& c) {
        return move_score(b, a) > move_score(b, c);
    });
}

std::string tt_key(const Board& b, int depth, char kind, Color attacker) {
    std::string k;
    k.reserve(90);
    for (char p : b.sq) k.push_back(p);
    k.push_back(b.stm == WHITE ? 'w' : 'b');
    k.push_back(static_cast<char>('A' + b.castling));
    k += std::to_string(b.ep);
    k.push_back(kind);
    k.push_back(attacker == WHITE ? 'W' : 'B');
    k += std::to_string(depth);
    return k;
}

Proof prove_attacker(Search& s, const Board& b, int depth);

Proof prove_defender(Search& s, const Board& b, int depth) {
    ++s.stats.nodes;
    std::string key = tt_key(b, depth, 'D', s.attacker);
    if (auto it = s.tt.find(key); it != s.tt.end()) {
        ++s.stats.tt_hits;
        return {it->second.ok, it->second.pv, it->second.cert};
    }

    auto replies = legal_moves(b);
    if (replies.empty()) {
        s.tt[key] = {false, {}, ""};
        return {};
    }

    order_moves(b, replies);
    std::vector<Move> representative;
    std::vector<std::string> branch_certs;
    branch_certs.reserve(replies.size());
    for (const Move& dmove : replies) {
        Board nb = make_move(b, dmove);
        Proof child = prove_attacker(s, nb, depth);
        if (!child.ok) {
            if (s.debug) {
                std::cerr << "defender_refutes depth=" << depth << " move=" << move_uci(dmove)
                          << " fen=" << fen4(nb) << "\n";
            }
            s.tt[key] = {false, {}, ""};
            return {};
        }
        std::vector<Move> candidate;
        candidate.push_back(dmove);
        candidate.insert(candidate.end(), child.pv.begin(), child.pv.end());
        if (candidate.size() > representative.size()) {
            representative = std::move(candidate);
        }
        branch_certs.push_back("{\"r\":" + json_quote(move_uci(dmove)) + ",\"p\":" + child.cert + "}");
    }
    std::string cert = "[";
    for (std::size_t i = 0; i < branch_certs.size(); ++i) {
        if (i) cert.push_back(',');
        cert += branch_certs[i];
    }
    cert.push_back(']');
    s.tt[key] = {true, representative, cert};
    return {true, representative, cert};
}

Proof prove_attacker(Search& s, const Board& b, int depth) {
    ++s.stats.nodes;
    if (depth <= 0 || b.stm != s.attacker) {
        return {};
    }
    std::string key = tt_key(b, depth, 'A', s.attacker);
    if (auto it = s.tt.find(key); it != s.tt.end()) {
        ++s.stats.tt_hits;
        return {it->second.ok, it->second.pv, it->second.cert};
    }

    auto moves = legal_moves(b);
    order_moves(b, moves);
    for (const Move& amove : moves) {
        Board nb = make_move(b, amove);
        if (is_checkmate(nb)) {
            std::vector<Move> pv{amove};
            std::string cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"mate\":true}";
            s.tt[key] = {true, pv, cert};
            return {true, pv, cert};
        }
        if (s.debug && depth == 1 && in_check(nb, nb.stm)) {
            auto replies = legal_moves(nb);
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
            if (all_replies.ok) {
                std::vector<Move> pv{amove};
                pv.insert(pv.end(), all_replies.pv.begin(), all_replies.pv.end());
                std::string cert = "{\"a\":" + json_quote(move_uci(amove)) + ",\"d\":" + all_replies.cert + "}";
                s.tt[key] = {true, pv, cert};
                return {true, pv, cert};
            }
            if (s.debug) {
                std::cerr << "attacker_move_failed depth=" << depth << " move=" << move_uci(amove)
                          << " fen=" << fen4(nb) << "\n";
            }
        }
    }
    s.tt[key] = {false, {}, ""};
    return {};
}

int infer_mate_depth(const std::string& line) {
    auto pos = line.find('#');
    if (pos == std::string::npos) {
        return 0;
    }
    int value = 0;
    for (++pos; pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])); ++pos) {
        value = value * 10 + (line[pos] - '0');
    }
    return value;
}

std::string pv_uci(const std::vector<Move>& pv) {
    std::ostringstream out;
    for (std::size_t i = 0; i < pv.size(); ++i) {
        if (i) out << ' ';
        out << move_uci(pv[i]);
    }
    return out.str();
}

void list_legal_line(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; legal_count 0; error input;\n";
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
    std::cout << ";\n";
}

void solve_line(const std::string& raw, int requested_depth, bool debug, bool emit_proof) {
    std::string line = trim(raw);
    if (line.empty()) {
        return;
    }
    auto parsed = parse_fen4(line);
    if (!parsed) {
        std::cout << line << "; acn 0; acs 0; error input;\n";
        return;
    }
    Board b = *parsed;
    int max_depth = requested_depth > 0 ? requested_depth : infer_mate_depth(line);
    if (max_depth <= 0) {
        max_depth = 1;
    }

    Search s;
    s.attacker = b.stm;
    s.debug = debug;
    auto start = std::chrono::steady_clock::now();

    Proof proof;
    int proved_depth = 0;
    for (int depth = 1; depth <= max_depth; ++depth) {
        s.tt.clear();
        proof = prove_attacker(s, b, depth);
        if (proof.ok) {
            proved_depth = static_cast<int>((proof.pv.size() + 1) / 2);
            break;
        }
    }

    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();

    std::cout << fen4(b) << "; acn " << s.stats.nodes << "; acs " << seconds;
    if (proof.ok && !proof.pv.empty()) {
        std::cout << "; bm " << move_uci(proof.pv.front())
                  << "; dm " << proved_depth
                  << "; pv " << pv_uci(proof.pv);
        if (emit_proof && !proof.cert.empty()) {
            std::cout << "; proof " << proof.cert;
        }
    }
    std::cout << ";\n";
}

} // namespace

int main(int argc, char** argv) {
    int requested_depth = 0;
    bool read_stdin = false;
    bool debug = false;
    bool list_legal = false;
    bool emit_proof = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-z" && i + 1 < argc) {
            requested_depth = std::atoi(argv[++i]);
        } else if (arg == "-") {
            read_stdin = true;
        } else if (arg == "--debug") {
            debug = true;
        } else if (arg == "--list-legal") {
            list_legal = true;
        } else if (arg == "--emit-proof") {
            emit_proof = true;
        } else if ((arg == "-M" || arg == "-C" || arg == "-R" || arg == "-K" || arg == "-P" || arg == "-X" || arg == "-I" || arg == "-n" || arg == "-N") && i + 1 < argc) {
            ++i; // accepted for CLI compatibility in the initial E checkpoint
        }
    }

    if (read_stdin) {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (list_legal) {
                list_legal_line(line);
            } else {
                solve_line(line, requested_depth, debug, emit_proof);
            }
        }
    } else {
        std::ostringstream buffer;
        buffer << std::cin.rdbuf();
        if (list_legal) {
            list_legal_line(buffer.str());
        } else {
            solve_line(buffer.str(), requested_depth, debug, emit_proof);
        }
    }
    return 0;
}
