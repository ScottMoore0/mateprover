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
#include <cstddef>
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
    int score = 0;
};

struct MoveList {
    std::array<Move, 256> moves{};
    std::size_t count = 0;
    bool overflow = false;

    void push_back(const Move& move) {
        if (count < moves.size()) {
            moves[count++] = move;
        } else {
            overflow = true;
        }
    }

    std::size_t size() const {
        return count;
    }

    const Move* begin() const {
        return moves.data();
    }

    const Move* end() const {
        return moves.data() + count;
    }
};

struct Proof {
    bool ok = false;
    std::vector<Move> pv;
    std::string cert;
};

struct Board {
    std::array<char, 64> sq{};
    std::array<std::uint64_t, 4> packed{};
    std::array<int, 2> king_sq{{-1, -1}};
    Color stm = WHITE;
    unsigned castling = 0; // 1 WK, 2 WQ, 4 BK, 8 BQ
    int ep = -1;
};

struct Stats {
    std::uint64_t nodes = 0;
    std::uint64_t attacker_nodes = 0;
    std::uint64_t defender_nodes = 0;
    std::uint64_t tt_probes = 0;
    std::uint64_t tt_hits = 0;
    std::uint64_t tt_stores = 0;
    std::uint64_t bound_tt_probes = 0;
    std::uint64_t bound_tt_hits = 0;
    std::uint64_t bound_tt_ok_hits = 0;
    std::uint64_t bound_tt_fail_hits = 0;
    std::uint64_t bound_tt_stores = 0;
    std::uint64_t attacker_move_lists = 0;
    std::uint64_t attacker_moves = 0;
    std::uint64_t attacker_candidates = 0;
    std::uint64_t defender_move_lists = 0;
    std::uint64_t defender_moves = 0;
    std::uint64_t defender_replies_tried = 0;
    std::uint64_t order_calls = 0;
    std::uint64_t order_moves = 0;
    std::uint64_t immediate_mate_tests = 0;
    std::uint64_t immediate_mates = 0;
    std::uint64_t refutation_hint_probes = 0;
    std::uint64_t refutation_hint_hits = 0;
    std::uint64_t refutation_hint_stores = 0;
    std::uint64_t proof_hint_probes = 0;
    std::uint64_t proof_hint_hits = 0;
    std::uint64_t proof_hint_stores = 0;
    std::uint64_t defender_refutations = 0;
};

struct TTKey {
    std::array<std::uint64_t, 4> board{};
    std::uint64_t context = 0;

    bool operator==(const TTKey& other) const {
        return board == other.board && context == other.context;
    }
};

struct TTKeyHash {
    std::size_t operator()(const TTKey& key) const noexcept {
        std::uint64_t h = 0x9e3779b97f4a7c15ull;
        auto mix64 = [&](std::uint64_t value) {
            value += 0x9e3779b97f4a7c15ull;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            value ^= value >> 31;
            h ^= value + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        };
        for (std::uint64_t word : key.board) {
            mix64(word);
        }
        mix64(key.context);
        return static_cast<std::size_t>(h);
    }
};

struct TTEntry {
    bool ok = false;
    std::vector<Move> pv;
    std::string cert;
};

struct BoundEntry {
    bool has_ok = false;
    int ok_depth = 0;
    std::vector<Move> ok_pv;
    std::string ok_cert;
    bool has_fail = false;
    int fail_depth = 0;
};

struct Search {
    Color attacker = WHITE;
    Stats stats;
    std::unordered_map<TTKey, TTEntry, TTKeyHash> tt;
    std::unordered_map<TTKey, BoundEntry, TTKeyHash> bound_tt;
    std::unordered_map<TTKey, Move, TTKeyHash> defender_refutations;
    std::unordered_map<TTKey, Move, TTKeyHash> attacker_proofs;
    bool debug = false;
    bool emit_proof = false;
    bool score_mates = false;
    bool score_checks = true;
    bool fast_check_score = false;
    bool refutation_hints = false;
    bool proof_hints = false;
    std::size_t tt_reserve = 0;
    bool move_reserve = false;
    std::size_t move_reserve_capacity = 64;
    bool inplace_order = false;
    bool static_pseudo = false;
    bool profile = false;
    std::size_t order_min_size = 2;
    bool bucket_order = false;
    bool keep_iter_tt = false;
    bool bound_tt_enabled = false;
    bool bound_tt_failures = false;
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

std::uint8_t piece_code(char p) {
    switch (p) {
        case '.': return 0;
        case 'P': return 1;
        case 'N': return 2;
        case 'B': return 3;
        case 'R': return 4;
        case 'Q': return 5;
        case 'K': return 6;
        case 'p': return 7;
        case 'n': return 8;
        case 'b': return 9;
        case 'r': return 10;
        case 'q': return 11;
        case 'k': return 12;
        default: return 0;
    }
}

void set_square(Board& b, int sq, char p) {
    b.sq[sq] = p;
    int word = sq / 16;
    int shift = (sq % 16) * 4;
    std::uint64_t mask = 0xfull << shift;
    b.packed[word] = (b.packed[word] & ~mask) | (static_cast<std::uint64_t>(piece_code(p)) << shift);
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

struct SquareList {
    std::array<int, 8> sq{};
    int count = 0;
};

void add_square(SquareList& list, int sq) {
    list.sq[list.count++] = sq;
}

constexpr int DIRS[8][2] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1},
    {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
};

const std::array<SquareList, 64>& knight_table() {
    static const std::array<SquareList, 64> table = [] {
        std::array<SquareList, 64> out{};
        static const int delta[8][2] = {
            {1, 2}, {2, 1}, {2, -1}, {1, -2},
            {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
        };
        for (int sq = 0; sq < 64; ++sq) {
            int f = file_of(sq);
            int r = rank_of(sq);
            for (const auto& d : delta) {
                int tf = f + d[0];
                int tr = r + d[1];
                if (on_board(tf, tr)) {
                    add_square(out[sq], square_of(tf, tr));
                }
            }
        }
        return out;
    }();
    return table;
}

const std::array<SquareList, 64>& king_table() {
    static const std::array<SquareList, 64> table = [] {
        std::array<SquareList, 64> out{};
        for (int sq = 0; sq < 64; ++sq) {
            int f = file_of(sq);
            int r = rank_of(sq);
            for (int df = -1; df <= 1; ++df) {
                for (int dr = -1; dr <= 1; ++dr) {
                    if (df == 0 && dr == 0) continue;
                    int tf = f + df;
                    int tr = r + dr;
                    if (on_board(tf, tr)) {
                        add_square(out[sq], square_of(tf, tr));
                    }
                }
            }
        }
        return out;
    }();
    return table;
}

const std::array<std::array<SquareList, 64>, 2>& pawn_attacker_table() {
    static const std::array<std::array<SquareList, 64>, 2> table = [] {
        std::array<std::array<SquareList, 64>, 2> out{};
        for (int sq = 0; sq < 64; ++sq) {
            int f = file_of(sq);
            int r = rank_of(sq);
            int white_rank = r - 1;
            int black_rank = r + 1;
            for (int df : {-1, 1}) {
                int pf = f + df;
                if (on_board(pf, white_rank)) {
                    add_square(out[WHITE][sq], square_of(pf, white_rank));
                }
                if (on_board(pf, black_rank)) {
                    add_square(out[BLACK][sq], square_of(pf, black_rank));
                }
            }
        }
        return out;
    }();
    return table;
}

const std::array<std::array<SquareList, 64>, 8>& ray_table() {
    static const std::array<std::array<SquareList, 64>, 8> table = [] {
        std::array<std::array<SquareList, 64>, 8> out{};
        for (int dir = 0; dir < 8; ++dir) {
            for (int sq = 0; sq < 64; ++sq) {
                int f = file_of(sq) + DIRS[dir][0];
                int r = rank_of(sq) + DIRS[dir][1];
                while (on_board(f, r)) {
                    add_square(out[dir][sq], square_of(f, r));
                    f += DIRS[dir][0];
                    r += DIRS[dir][1];
                }
            }
        }
        return out;
    }();
    return table;
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
    b.packed.fill(0);

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
        int sq = square_of(file, rank);
        set_square(b, sq, ch);
        if (ch == 'K') {
            b.king_sq[WHITE] = sq;
        } else if (ch == 'k') {
            b.king_sq[BLACK] = sq;
        }
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
    int cached = b.king_sq[c];
    char k = c == WHITE ? 'K' : 'k';
    if (cached >= 0 && cached < 64 && b.sq[cached] == k) {
        return cached;
    }
    for (int i = 0; i < 64; ++i) {
        if (b.sq[i] == k) {
            return i;
        }
    }
    return -1;
}

bool slider_attacker_matches(char p, bool diagonal) {
    char lp = static_cast<char>(std::tolower(static_cast<unsigned char>(p)));
    return diagonal ? (lp == 'b' || lp == 'q') : (lp == 'r' || lp == 'q');
}

bool attacked_by_slider(const Board& b, int target, Color by, int first_dir, int last_dir, bool diagonal) {
    const auto& rays = ray_table();
    for (int dir = first_dir; dir < last_dir; ++dir) {
        const SquareList& ray = rays[dir][target];
        for (int i = 0; i < ray.count; ++i) {
            char p = b.sq[ray.sq[i]];
            if (p != '.') {
                if (is_piece_color(p, by)) {
                    if (slider_attacker_matches(p, diagonal)) {
                        return true;
                    }
                }
                break;
            }
        }
    }
    return false;
}

bool is_attacked(const Board& b, int target, Color by) {
    const SquareList& pawns = pawn_attacker_table()[by][target];
    for (int i = 0; i < pawns.count; ++i) {
        char p = b.sq[pawns.sq[i]];
        if (p == (by == WHITE ? 'P' : 'p')) {
            return true;
        }
    }

    const SquareList& knights = knight_table()[target];
    for (int i = 0; i < knights.count; ++i) {
        char p = b.sq[knights.sq[i]];
        if (p == (by == WHITE ? 'N' : 'n')) {
            return true;
        }
    }

    if (attacked_by_slider(b, target, by, 4, 8, true)) {
        return true;
    }
    if (attacked_by_slider(b, target, by, 0, 4, false)) {
        return true;
    }

    const SquareList& kings = king_table()[target];
    for (int i = 0; i < kings.count; ++i) {
        char p = b.sq[kings.sq[i]];
        if (p == (by == WHITE ? 'K' : 'k')) {
            return true;
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

void add_move(MoveList& moves, int from, int to, char promo = 0, bool castle = false, bool ep = false) {
    Move m;
    m.from = from;
    m.to = to;
    m.promo = promo;
    m.castle = castle;
    m.ep = ep;
    moves.push_back(m);
}

template <typename MoveSink>
void gen_pseudo(const Board& b, MoveSink& moves) {
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
            const SquareList& targets = knight_table()[from];
            for (int i = 0; i < targets.count; ++i) {
                int to = targets.sq[i];
                if (!is_piece_color(b.sq[to], us) && !is_king_piece(b.sq[to])) add_move(moves, from, to);
            }
        } else if (lp == 'b' || lp == 'r' || lp == 'q') {
            int first = lp == 'b' ? 4 : 0;
            int last = lp == 'r' ? 4 : 8;
            const auto& rays = ray_table();
            for (int i = first; i < last; ++i) {
                const SquareList& ray = rays[i][from];
                for (int j = 0; j < ray.count; ++j) {
                    int to = ray.sq[j];
                    if (is_piece_color(b.sq[to], us)) break;
                    if (is_king_piece(b.sq[to])) break;
                    add_move(moves, from, to);
                    if (b.sq[to] != '.') break;
                }
            }
        } else if (lp == 'k') {
            const SquareList& targets = king_table()[from];
            for (int i = 0; i < targets.count; ++i) {
                int to = targets.sq[i];
                if (!is_piece_color(b.sq[to], us) && !is_king_piece(b.sq[to])) add_move(moves, from, to);
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
    set_square(b, m.from, '.');

    if (m.ep) {
        int cap_sq = m.to + (b.stm == WHITE ? -8 : 8);
        captured = b.sq[cap_sq];
        set_square(b, cap_sq, '.');
    }

    char placed = p;
    if (m.promo) {
        placed = b.stm == WHITE ? static_cast<char>(std::toupper(static_cast<unsigned char>(m.promo))) : m.promo;
    }
    set_square(b, m.to, placed);
    if (p == 'K') {
        b.king_sq[WHITE] = m.to;
    } else if (p == 'k') {
        b.king_sq[BLACK] = m.to;
    }

    if (m.castle) {
        if (p == 'K' && m.to == square_of(6, 0)) {
            set_square(b, square_of(5, 0), 'R');
            set_square(b, square_of(7, 0), '.');
        } else if (p == 'K' && m.to == square_of(2, 0)) {
            set_square(b, square_of(3, 0), 'R');
            set_square(b, square_of(0, 0), '.');
        } else if (p == 'k' && m.to == square_of(6, 7)) {
            set_square(b, square_of(5, 7), 'r');
            set_square(b, square_of(7, 7), '.');
        } else if (p == 'k' && m.to == square_of(2, 7)) {
            set_square(b, square_of(3, 7), 'r');
            set_square(b, square_of(0, 7), '.');
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

std::vector<Move> legal_moves_vector(const Board& b, bool move_reserve, std::size_t move_reserve_capacity) {
    std::vector<Move> pseudo;
    if (move_reserve) {
        pseudo.reserve(move_reserve_capacity);
    }
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

std::vector<Move> legal_moves(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    if (static_pseudo) {
        MoveList pseudo;
        gen_pseudo(b, pseudo);
        if (!pseudo.overflow) {
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
    }
    return legal_moves_vector(b, move_reserve, move_reserve_capacity);
}

bool has_legal_move_vector(const Board& b, bool move_reserve, std::size_t move_reserve_capacity) {
    std::vector<Move> pseudo;
    if (move_reserve) {
        pseudo.reserve(move_reserve_capacity);
    }
    gen_pseudo(b, pseudo);
    for (const Move& m : pseudo) {
        Board nb = make_move(b, m);
        if (!in_check(nb, other(nb.stm))) {
            return true;
        }
    }
    return false;
}

bool has_legal_move(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    if (static_pseudo) {
        MoveList pseudo;
        gen_pseudo(b, pseudo);
        if (!pseudo.overflow) {
            for (const Move& m : pseudo) {
                Board nb = make_move(b, m);
                if (!in_check(nb, other(nb.stm))) {
                    return true;
                }
            }
            return false;
        }
    }
    return has_legal_move_vector(b, move_reserve, move_reserve_capacity);
}

bool is_checkmate(const Board& b, bool move_reserve = false, std::size_t move_reserve_capacity = 64, bool static_pseudo = false) {
    return in_check(b, b.stm) && !has_legal_move(b, move_reserve, move_reserve_capacity, static_pseudo);
}

char piece_after_move(const Board& b, const Move& m, int sq) {
    char p = b.sq[m.from];
    char placed = p;
    if (m.promo) {
        placed = b.stm == WHITE ? static_cast<char>(std::toupper(static_cast<unsigned char>(m.promo))) : m.promo;
    }

    if (sq == m.from) {
        return '.';
    }
    if (m.ep) {
        int cap_sq = m.to + (b.stm == WHITE ? -8 : 8);
        if (sq == cap_sq) {
            return '.';
        }
    }
    if (sq == m.to) {
        return placed;
    }
    if (m.castle) {
        if (p == 'K' && m.to == square_of(6, 0)) {
            if (sq == square_of(7, 0)) return '.';
            if (sq == square_of(5, 0)) return 'R';
        } else if (p == 'K' && m.to == square_of(2, 0)) {
            if (sq == square_of(0, 0)) return '.';
            if (sq == square_of(3, 0)) return 'R';
        } else if (p == 'k' && m.to == square_of(6, 7)) {
            if (sq == square_of(7, 7)) return '.';
            if (sq == square_of(5, 7)) return 'r';
        } else if (p == 'k' && m.to == square_of(2, 7)) {
            if (sq == square_of(0, 7)) return '.';
            if (sq == square_of(3, 7)) return 'r';
        }
    }
    return b.sq[sq];
}

bool attacked_by_slider_after_move(const Board& b, const Move& m, int target, Color by, int first_dir, int last_dir, bool diagonal) {
    const auto& rays = ray_table();
    for (int dir = first_dir; dir < last_dir; ++dir) {
        const SquareList& ray = rays[dir][target];
        for (int i = 0; i < ray.count; ++i) {
            char p = piece_after_move(b, m, ray.sq[i]);
            if (p != '.') {
                if (is_piece_color(p, by)) {
                    if (slider_attacker_matches(p, diagonal)) {
                        return true;
                    }
                }
                break;
            }
        }
    }
    return false;
}

bool is_attacked_after_move(const Board& b, const Move& m, int target, Color by) {
    const SquareList& pawns = pawn_attacker_table()[by][target];
    for (int i = 0; i < pawns.count; ++i) {
        char p = piece_after_move(b, m, pawns.sq[i]);
        if (p == (by == WHITE ? 'P' : 'p')) {
            return true;
        }
    }

    const SquareList& knights = knight_table()[target];
    for (int i = 0; i < knights.count; ++i) {
        char p = piece_after_move(b, m, knights.sq[i]);
        if (p == (by == WHITE ? 'N' : 'n')) {
            return true;
        }
    }

    if (attacked_by_slider_after_move(b, m, target, by, 4, 8, true)) {
        return true;
    }
    if (attacked_by_slider_after_move(b, m, target, by, 0, 4, false)) {
        return true;
    }

    const SquareList& kings = king_table()[target];
    for (int i = 0; i < kings.count; ++i) {
        char p = piece_after_move(b, m, kings.sq[i]);
        if (p == (by == WHITE ? 'K' : 'k')) {
            return true;
        }
    }
    return false;
}

bool move_gives_check_fast(const Board& b, const Move& m) {
    int enemy_king = king_square(b, other(b.stm));
    return enemy_king < 0 || is_attacked_after_move(b, m, enemy_king, b.stm);
}

int move_score(const Board& b, const Move& m, bool score_mates, bool score_checks, bool fast_check_score, bool move_reserve, std::size_t move_reserve_capacity, bool static_pseudo) {
    int score = 0;
    if (score_mates) {
        Board nb = make_move(b, m);
        if (is_checkmate(nb, move_reserve, move_reserve_capacity, static_pseudo)) score += 1000000;
        if (score_checks && in_check(nb, nb.stm)) score += 50000;
    } else if (score_checks) {
        bool gives_check = false;
        if (fast_check_score) {
            gives_check = move_gives_check_fast(b, m);
        } else {
            Board nb = make_move(b, m);
            gives_check = in_check(nb, nb.stm);
        }
        if (gives_check) score += 50000;
    }
    if (b.sq[m.to] != '.' || m.ep) score += 10000;
    if (m.promo) score += 8000;
    char p = std::tolower(static_cast<unsigned char>(b.sq[m.from]));
    if (p == 'q') score += 50;
    if (p == 'r') score += 40;
    if (p == 'b' || p == 'n') score += 30;
    return score;
}

void stable_bucket_order(std::vector<Move>& moves) {
    std::vector<int> scores;
    scores.reserve(moves.size());
    for (const Move& move : moves) {
        auto it = std::find(scores.begin(), scores.end(), move.score);
        if (it != scores.end()) {
            continue;
        }
        auto insert_at = std::find_if(scores.begin(), scores.end(), [&](int score) {
            return move.score > score;
        });
        scores.insert(insert_at, move.score);
    }

    std::vector<Move> ordered;
    ordered.reserve(moves.size());
    for (int score : scores) {
        for (const Move& move : moves) {
            if (move.score == score) {
                ordered.push_back(move);
            }
        }
    }
    moves.swap(ordered);
}

void order_moves(const Board& b, std::vector<Move>& moves, bool score_mates, bool score_checks, bool fast_check_score, bool move_reserve, std::size_t move_reserve_capacity, bool static_pseudo, bool inplace_order, bool bucket_order) {
    if (moves.size() < 2) {
        return;
    }
    if (inplace_order) {
        for (Move& move : moves) {
            move.score = move_score(b, move, score_mates, score_checks, fast_check_score, move_reserve, move_reserve_capacity, static_pseudo);
        }
        if (bucket_order) {
            stable_bucket_order(moves);
            return;
        }
        std::stable_sort(moves.begin(), moves.end(), [](const Move& a, const Move& c) {
            return a.score > c.score;
        });
        return;
    }
    struct ScoredMove {
        Move move;
        int score = 0;
    };
    std::vector<ScoredMove> scored;
    scored.reserve(moves.size());
    for (const Move& move : moves) {
        scored.push_back({move, move_score(b, move, score_mates, score_checks, fast_check_score, move_reserve, move_reserve_capacity, static_pseudo)});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const ScoredMove& a, const ScoredMove& c) {
        return a.score > c.score;
    });
    for (std::size_t i = 0; i < scored.size(); ++i) {
        moves[i] = scored[i].move;
    }
}

TTKey tt_key(const Board& b, int depth, char kind, Color attacker) {
    TTKey k;
    k.board = b.packed;
    std::uint64_t ep = static_cast<std::uint64_t>(b.ep + 1);
    k.context = static_cast<std::uint64_t>(static_cast<std::uint32_t>(depth))
        | (static_cast<std::uint64_t>(b.stm) << 32)
        | (static_cast<std::uint64_t>(attacker) << 33)
        | (static_cast<std::uint64_t>(kind == 'D' ? 1 : 0) << 34)
        | (static_cast<std::uint64_t>(b.castling & 0x0fu) << 35)
        | (ep << 39);
    return k;
}

TTKey move_hint_key(const Board& b, char kind, Color attacker) {
    return tt_key(b, 0, kind, attacker);
}

bool same_move(const Move& a, const Move& b) {
    return a.from == b.from
        && a.to == b.to
        && a.promo == b.promo
        && a.castle == b.castle
        && a.ep == b.ep;
}

bool move_to_front(std::vector<Move>& moves, const Move& hint) {
    for (std::size_t i = 0; i < moves.size(); ++i) {
        if (same_move(moves[i], hint)) {
            if (i != 0) {
                std::rotate(moves.begin(), moves.begin() + static_cast<std::ptrdiff_t>(i), moves.begin() + static_cast<std::ptrdiff_t>(i + 1));
            }
            return true;
        }
    }
    return false;
}

bool should_order(const Search& s, std::size_t move_count) {
    return move_count >= std::max<std::size_t>(2, s.order_min_size);
}

bool probe_bound_tt(Search& s, const TTKey& key, int depth, Proof& out) {
    if (!s.bound_tt_enabled) {
        return false;
    }
    ++s.stats.bound_tt_probes;
    auto it = s.bound_tt.find(key);
    if (it == s.bound_tt.end()) {
        return false;
    }
    const BoundEntry& entry = it->second;
    if (entry.has_ok && entry.ok_depth <= depth) {
        ++s.stats.bound_tt_hits;
        ++s.stats.bound_tt_ok_hits;
        out = {true, entry.ok_pv, entry.ok_cert};
        return true;
    }
    if (s.bound_tt_failures && entry.has_fail && entry.fail_depth >= depth) {
        ++s.stats.bound_tt_hits;
        ++s.stats.bound_tt_fail_hits;
        out = {};
        return true;
    }
    return false;
}

void store_bound_tt(Search& s, const TTKey& key, int depth, const Proof& proof) {
    if (!s.bound_tt_enabled) {
        return;
    }
    if (!proof.ok && !s.bound_tt_failures) {
        return;
    }
    ++s.stats.bound_tt_stores;
    BoundEntry& entry = s.bound_tt[key];
    if (proof.ok) {
        if (!entry.has_ok || depth < entry.ok_depth) {
            entry.has_ok = true;
            entry.ok_depth = depth;
            entry.ok_pv = proof.pv;
            entry.ok_cert = proof.cert;
        }
    } else if (s.bound_tt_failures && (!entry.has_fail || depth > entry.fail_depth)) {
        entry.has_fail = true;
        entry.fail_depth = depth;
    }
}

Proof prove_attacker(Search& s, const Board& b, int depth);

Proof prove_defender(Search& s, const Board& b, int depth) {
    ++s.stats.nodes;
    ++s.stats.defender_nodes;
    TTKey key = tt_key(b, depth, 'D', s.attacker);
    ++s.stats.tt_probes;
    if (auto it = s.tt.find(key); it != s.tt.end()) {
        ++s.stats.tt_hits;
        return {it->second.ok, it->second.pv, it->second.cert};
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
    if (s.bound_tt_enabled) {
        Proof cached;
        if (probe_bound_tt(s, get_hint_key(), depth, cached)) {
            return cached;
        }
    }

    auto replies = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    ++s.stats.defender_move_lists;
    s.stats.defender_moves += replies.size();
    if (replies.empty()) {
        ++s.stats.tt_stores;
        s.tt[key] = {false, {}, ""};
        if (s.bound_tt_enabled) {
            store_bound_tt(s, get_hint_key(), depth, {});
        }
        return {};
    }

    if (should_order(s, replies.size())) {
        ++s.stats.order_calls;
        s.stats.order_moves += replies.size();
        order_moves(b, replies, s.score_mates, s.score_checks, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
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
    for (const Move& dmove : replies) {
        ++s.stats.defender_replies_tried;
        Board nb = make_move(b, dmove);
        Proof child = prove_attacker(s, nb, depth);
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
            ++s.stats.tt_stores;
            s.tt[key] = {false, {}, ""};
            if (s.bound_tt_enabled) {
                store_bound_tt(s, get_hint_key(), depth, {});
            }
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
    ++s.stats.tt_stores;
    s.tt[key] = {true, representative, cert};
    Proof proof{true, representative, cert};
    if (s.bound_tt_enabled) {
        store_bound_tt(s, get_hint_key(), depth, proof);
    }
    return proof;
}

Proof prove_attacker(Search& s, const Board& b, int depth) {
    ++s.stats.nodes;
    ++s.stats.attacker_nodes;
    if (depth <= 0 || b.stm != s.attacker) {
        return {};
    }
    TTKey key = tt_key(b, depth, 'A', s.attacker);
    ++s.stats.tt_probes;
    if (auto it = s.tt.find(key); it != s.tt.end()) {
        ++s.stats.tt_hits;
        return {it->second.ok, it->second.pv, it->second.cert};
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
    if (s.bound_tt_enabled) {
        Proof cached;
        if (probe_bound_tt(s, get_hint_key(), depth, cached)) {
            return cached;
        }
    }

    auto moves = legal_moves(b, s.move_reserve, s.move_reserve_capacity, s.static_pseudo);
    ++s.stats.attacker_move_lists;
    s.stats.attacker_moves += moves.size();
    if (should_order(s, moves.size())) {
        ++s.stats.order_calls;
        s.stats.order_moves += moves.size();
        order_moves(b, moves, s.score_mates, s.score_checks, s.fast_check_score, s.move_reserve, s.move_reserve_capacity, s.static_pseudo, s.inplace_order, s.bucket_order);
    }
    if (s.proof_hints) {
        const TTKey& proof_key = get_hint_key();
        ++s.stats.proof_hint_probes;
        if (auto hint = s.attacker_proofs.find(proof_key); hint != s.attacker_proofs.end()) {
            if (move_to_front(moves, hint->second)) {
                ++s.stats.proof_hint_hits;
            }
        }
    }
    for (const Move& amove : moves) {
        ++s.stats.attacker_candidates;
        Board nb = make_move(b, amove);
        ++s.stats.immediate_mate_tests;
        if (is_checkmate(nb, s.move_reserve, s.move_reserve_capacity, s.static_pseudo)) {
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
            ++s.stats.tt_stores;
            s.tt[key] = {true, pv, cert};
            Proof proof{true, pv, cert};
            if (s.bound_tt_enabled) {
                store_bound_tt(s, get_hint_key(), depth, proof);
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
        if (depth > 1) {
            Proof all_replies = prove_defender(s, nb, depth - 1);
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
                ++s.stats.tt_stores;
                s.tt[key] = {true, pv, cert};
                Proof proof{true, pv, cert};
                if (s.bound_tt_enabled) {
                    store_bound_tt(s, get_hint_key(), depth, proof);
                }
                return proof;
            }
            if (s.debug) {
                std::cerr << "attacker_move_failed depth=" << depth << " move=" << move_uci(amove)
                          << " fen=" << fen4(nb) << "\n";
            }
        }
    }
    ++s.stats.tt_stores;
    s.tt[key] = {false, {}, ""};
    if (s.bound_tt_enabled) {
        store_bound_tt(s, get_hint_key(), depth, {});
    }
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

void emit_profile_line(const Board& b, const Search& s, int requested_depth, int proved_depth, double seconds) {
    const Stats& st = s.stats;
    std::cerr << "% e_profile {"
              << "\"fen\":" << json_quote(fen4(b))
              << ",\"requested_depth\":" << requested_depth
              << ",\"proved_depth\":" << proved_depth
              << ",\"seconds\":" << seconds
              << ",\"nodes\":" << st.nodes
              << ",\"attacker_nodes\":" << st.attacker_nodes
              << ",\"defender_nodes\":" << st.defender_nodes
              << ",\"tt_probes\":" << st.tt_probes
              << ",\"tt_hits\":" << st.tt_hits
              << ",\"tt_stores\":" << st.tt_stores
              << ",\"tt_size\":" << s.tt.size()
              << ",\"bound_tt_probes\":" << st.bound_tt_probes
              << ",\"bound_tt_hits\":" << st.bound_tt_hits
              << ",\"bound_tt_ok_hits\":" << st.bound_tt_ok_hits
              << ",\"bound_tt_fail_hits\":" << st.bound_tt_fail_hits
              << ",\"bound_tt_stores\":" << st.bound_tt_stores
              << ",\"bound_tt_size\":" << s.bound_tt.size()
              << ",\"attacker_move_lists\":" << st.attacker_move_lists
              << ",\"attacker_moves\":" << st.attacker_moves
              << ",\"attacker_candidates\":" << st.attacker_candidates
              << ",\"defender_move_lists\":" << st.defender_move_lists
              << ",\"defender_moves\":" << st.defender_moves
              << ",\"defender_replies_tried\":" << st.defender_replies_tried
              << ",\"order_calls\":" << st.order_calls
              << ",\"order_moves\":" << st.order_moves
              << ",\"immediate_mate_tests\":" << st.immediate_mate_tests
              << ",\"immediate_mates\":" << st.immediate_mates
              << ",\"refutation_hint_probes\":" << st.refutation_hint_probes
              << ",\"refutation_hint_hits\":" << st.refutation_hint_hits
              << ",\"refutation_hint_stores\":" << st.refutation_hint_stores
              << ",\"proof_hint_probes\":" << st.proof_hint_probes
              << ",\"proof_hint_hits\":" << st.proof_hint_hits
              << ",\"proof_hint_stores\":" << st.proof_hint_stores
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
              << ",\"bound_tt\":" << (s.bound_tt_enabled ? "true" : "false")
              << ",\"bound_tt_failures\":" << (s.bound_tt_failures ? "true" : "false")
              << "}\n";
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

void solve_line(const std::string& raw, int requested_depth, bool debug, bool emit_proof, bool profile, bool score_mates, bool score_checks, bool fast_check_score, bool refutation_hints, bool proof_hints, std::size_t tt_reserve, bool move_reserve, std::size_t move_reserve_capacity, bool inplace_order, bool static_pseudo, std::size_t order_min_size, bool bucket_order, bool keep_iter_tt, bool bound_tt_enabled, bool bound_tt_failures) {
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
    s.emit_proof = emit_proof;
    s.profile = profile;
    s.score_mates = score_mates;
    s.score_checks = score_checks;
    s.fast_check_score = fast_check_score;
    s.refutation_hints = refutation_hints;
    s.proof_hints = proof_hints;
    s.tt_reserve = tt_reserve;
    s.move_reserve = move_reserve;
    s.move_reserve_capacity = move_reserve_capacity;
    s.inplace_order = inplace_order;
    s.static_pseudo = static_pseudo;
    s.order_min_size = std::max<std::size_t>(2, order_min_size);
    s.bucket_order = bucket_order;
    s.keep_iter_tt = keep_iter_tt;
    s.bound_tt_enabled = bound_tt_enabled;
    s.bound_tt_failures = bound_tt_failures;
    if (s.tt_reserve > 0) {
        s.tt.reserve(s.tt_reserve);
    }
    auto start = std::chrono::steady_clock::now();

    Proof proof;
    int proved_depth = 0;
    for (int depth = 1; depth <= max_depth; ++depth) {
        if (!s.keep_iter_tt) {
            s.tt.clear();
        }
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
    if (s.profile) {
        emit_profile_line(b, s, requested_depth, proved_depth, seconds);
    }
}

} // namespace

int main(int argc, char** argv) {
    int requested_depth = 0;
    bool read_stdin = false;
    bool debug = false;
    bool list_legal = false;
    bool emit_proof = false;
    bool profile = false;
    bool score_mates = false;
    bool score_checks = true;
    bool fast_check_score = false;
    bool refutation_hints = false;
    bool proof_hints = false;
    std::size_t tt_reserve = 0;
    bool move_reserve = false;
    std::size_t move_reserve_capacity = 64;
    bool inplace_order = false;
    bool static_pseudo = false;
    std::size_t order_min_size = 2;
    bool bucket_order = false;
    bool keep_iter_tt = false;
    bool bound_tt_enabled = false;
    bool bound_tt_failures = false;
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
        } else if (arg == "--profile") {
            profile = true;
        } else if (arg == "--no-profile") {
            profile = false;
        } else if (arg == "--score-mates") {
            score_mates = true;
        } else if (arg == "--no-mate-score") {
            score_mates = false;
        } else if (arg == "--score-checks") {
            score_checks = true;
        } else if (arg == "--no-check-score") {
            score_checks = false;
        } else if (arg == "--fast-check-score") {
            fast_check_score = true;
        } else if (arg == "--exact-check-score") {
            fast_check_score = false;
        } else if (arg == "--refutation-hints") {
            refutation_hints = true;
        } else if (arg == "--no-refutation-hints") {
            refutation_hints = false;
        } else if (arg == "--proof-hints") {
            proof_hints = true;
        } else if (arg == "--no-proof-hints") {
            proof_hints = false;
        } else if (arg == "--tt-reserve" && i + 1 < argc) {
            char* end = nullptr;
            unsigned long value = std::strtoul(argv[++i], &end, 10);
            if (end != argv[i]) {
                tt_reserve = static_cast<std::size_t>(value);
            }
        } else if (arg == "--move-reserve") {
            move_reserve = true;
        } else if (arg == "--no-move-reserve") {
            move_reserve = false;
        } else if (arg == "--move-reserve-cap" && i + 1 < argc) {
            char* end = nullptr;
            unsigned long value = std::strtoul(argv[++i], &end, 10);
            if (end != argv[i] && value > 0) {
                move_reserve = true;
                move_reserve_capacity = static_cast<std::size_t>(value);
            }
        } else if (arg == "--inplace-order") {
            inplace_order = true;
        } else if (arg == "--scored-vector-order") {
            inplace_order = false;
        } else if (arg == "--bucket-order") {
            bucket_order = true;
            inplace_order = true;
        } else if (arg == "--stable-sort-order") {
            bucket_order = false;
        } else if (arg == "--keep-iter-tt") {
            keep_iter_tt = true;
        } else if (arg == "--clear-iter-tt") {
            keep_iter_tt = false;
        } else if (arg == "--bound-tt") {
            bound_tt_enabled = true;
        } else if (arg == "--exact-tt-only") {
            bound_tt_enabled = false;
        } else if (arg == "--bound-tt-failures") {
            bound_tt_failures = true;
        } else if (arg == "--bound-tt-ok-only") {
            bound_tt_failures = false;
        } else if (arg == "--static-pseudo") {
            static_pseudo = true;
        } else if (arg == "--vector-pseudo") {
            static_pseudo = false;
        } else if (arg == "--order-min-size" && i + 1 < argc) {
            char* end = nullptr;
            unsigned long value = std::strtoul(argv[++i], &end, 10);
            if (end != argv[i]) {
                order_min_size = std::max<std::size_t>(2, static_cast<std::size_t>(value));
            }
        } else if (arg == "--order-all") {
            order_min_size = 2;
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
                solve_line(line, requested_depth, debug, emit_proof, profile, score_mates, score_checks, fast_check_score, refutation_hints, proof_hints, tt_reserve, move_reserve, move_reserve_capacity, inplace_order, static_pseudo, order_min_size, bucket_order, keep_iter_tt, bound_tt_enabled, bound_tt_failures);
            }
        }
    } else {
        std::ostringstream buffer;
        buffer << std::cin.rdbuf();
        if (list_legal) {
            list_legal_line(buffer.str());
        } else {
            solve_line(buffer.str(), requested_depth, debug, emit_proof, profile, score_mates, score_checks, fast_check_score, refutation_hints, proof_hints, tt_reserve, move_reserve, move_reserve_capacity, inplace_order, static_pseudo, order_min_size, bucket_order, keep_iter_tt, bound_tt_enabled, bound_tt_failures);
        }
    }
    return 0;
}
