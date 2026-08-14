// MateProver -- an exact directmate prover with machine-checkable proofs.
// Copyright (C) 2026 Scott Moore
//
// Released under the MIT License. See LICENSE for the full text.

// board.h -- Board geometry, attack tables, FEN parsing and formatting, attack queries.
//
// Part of a header-based split of a single translation unit. The modules are
// included in order by mateprover.cpp; see docs/ARCHITECTURE.md.

#ifndef MATEPROVER_BOARD_H_INCLUDED
#define MATEPROVER_BOARD_H_INCLUDED

namespace mateprover {

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

// Squares within Chebyshev distance d -- everywhere a king can stand after at
// most d moves on an empty board.
const std::array<std::array<std::uint64_t, 9>, 64>& king_disc_table() {
    static const std::array<std::array<std::uint64_t, 9>, 64> table = [] {
        std::array<std::array<std::uint64_t, 9>, 64> out{};
        for (int sq = 0; sq < 64; ++sq) {
            for (int d = 0; d <= 8; ++d) {
                std::uint64_t mask = 0;
                for (int t = 0; t < 64; ++t) {
                    const int df = std::abs(file_of(t) - file_of(sq));
                    const int dr = std::abs(rank_of(t) - rank_of(sq));
                    if (df > dr ? df <= d : dr <= d) mask |= 1ull << t;
                }
                out[sq][d] = mask;
            }
        }
        return out;
    }();
    return table;
}

// What a piece standing on `sq` can reach, on an EMPTY board, after at most d of
// its own moves: `attack` is what it then ATTACKS, `reach` is where it can
// STAND. Indexed [colour * 6 + type][square][d], d up to 5.
//
// Both are supersets of the truth on a real board, which is the only property
// that matters: blockers shorten a slider's reach and obstruct a route, they
// never create an attack line or a path that emptiness did not already offer.
//
// Built together, deliberately. The two differ only in what they record from the
// same walk, and the pawn modelling below is subtle enough that two copies of it
// would eventually disagree -- which is precisely how the unsound version of 74
// arose.
//
// PAWNS INCLUDE THEIR DIAGONALS. An empty board offers nothing to capture, but
// this models what a pawn may do on a REAL board, where it captures sideways and
// changes file. Omitting the diagonals makes the pawn sets an UNDERestimate,
// which is the one direction that makes any bound built on them unsound. It
// promotes to a queen on the last rank for the same reason.
//
// d is capped at 5; a caller with more moves must not use the table, because
// entry [5] is the FIVE-move set and would be an underestimate.
struct EmptyBoardReach {
    std::array<std::array<std::array<std::uint64_t, 6>, 64>, 12> attack{};
    std::array<std::array<std::array<std::uint64_t, 6>, 64>, 12> reach{};
};

const EmptyBoardReach& empty_board_reach() {
    static const EmptyBoardReach tables = [] {
        EmptyBoardReach out;
        auto attacks_from = [](int pt, int c, int s) -> std::uint64_t {
            std::uint64_t mask = 0;
            const int f = file_of(s), r = rank_of(s);
            if (pt == PT_PAWN) {
                const int dr = (c == WHITE) ? 1 : -1;
                for (int df = -1; df <= 1; df += 2) {
                    if (on_board(f + df, r + dr)) mask |= 1ull << square_of(f + df, r + dr);
                }
                return mask;
            }
            if (pt == PT_KNIGHT || pt == PT_KING) {
                const SquareList& l = (pt == PT_KNIGHT) ? knight_table()[s] : king_table()[s];
                for (int i = 0; i < l.count; ++i) mask |= 1ull << l.sq[i];
                return mask;
            }
            const int first = (pt == PT_BISHOP) ? 4 : 0;
            const int last = (pt == PT_ROOK) ? 4 : 8;
            const auto& rays = ray_table();
            for (int dir = first; dir < last; ++dir) {
                const SquareList& ray = rays[dir][s];
                for (int i = 0; i < ray.count; ++i) mask |= 1ull << ray.sq[i];
            }
            return mask;
        };
        for (int c = 0; c < 2; ++c) {
            for (int pt = 0; pt < 6; ++pt) {
                for (int sq = 0; sq < 64; ++sq) {
                    std::vector<std::pair<int, int>> cur{{sq, pt}};
                    std::uint64_t acc_attack = 0;
                    std::uint64_t acc_reach = 1ull << sq;
                    const std::size_t slot = static_cast<std::size_t>(c * 6 + pt);
                    for (int d = 0; d <= 5; ++d) {
                        for (const auto& st : cur) {
                            acc_attack |= attacks_from(st.second, c, st.first);
                            acc_reach |= 1ull << st.first;
                        }
                        out.attack[slot][static_cast<std::size_t>(sq)]
                                  [static_cast<std::size_t>(d)] = acc_attack;
                        out.reach[slot][static_cast<std::size_t>(sq)]
                                 [static_cast<std::size_t>(d)] = acc_reach;
                        if (d == 5) break;
                        std::vector<std::pair<int, int>> next;
                        for (const auto& st : cur) {
                            if (st.second == PT_PAWN) {
                                const int dr = (c == WHITE) ? 1 : -1;
                                const int start = (c == WHITE) ? 1 : 6;
                                const int back = (c == WHITE) ? 7 : 0;
                                const int f = file_of(st.first), r = rank_of(st.first);
                                const int steps = (r == start) ? 2 : 1;
                                // On the last rank a pawn becomes a queen OR A
                                // KNIGHT, and both must be modelled. A knight's
                                // attacks are not a subset of a queen's -- a
                                // knight on f8 attacks e6 and a queen does not --
                                // so queen-only promotion UNDERSTATES what a pawn
                                // can attack, which is the unsound direction.
                                // Rook and bishop need no state of their own:
                                // their attacks are subsets of the queen's.
                                //
                                // Found by a helpmate whose solution ends
                                // g7xf8=N. Same class as the missing diagonals:
                                // a modelling shortcut that is true of the common
                                // case and false in general.
                                auto arrive = [&](int nf, int nr) {
                                    const int to = square_of(nf, nr);
                                    if (nr == back) {
                                        next.emplace_back(to, PT_QUEEN);
                                        next.emplace_back(to, PT_KNIGHT);
                                    } else {
                                        next.emplace_back(to, PT_PAWN);
                                    }
                                };
                                for (int step = 1; step <= steps; ++step) {
                                    const int nr = r + dr * step;
                                    if (!on_board(f, nr)) break;
                                    arrive(f, nr);
                                }
                                for (int df = -1; df <= 1; df += 2) {
                                    const int nf = f + df, nr = r + dr;
                                    if (!on_board(nf, nr)) continue;
                                    arrive(nf, nr);
                                }
                                continue;
                            }
                            std::uint64_t m = attacks_from(st.second, c, st.first);
                            while (m) {
                                const int to = lsb_index(m);
                                m &= m - 1;
                                next.emplace_back(to, st.second);
                            }
                        }
                        std::sort(next.begin(), next.end());
                        next.erase(std::unique(next.begin(), next.end()), next.end());
                        cur = std::move(next);
                    }
                }
            }
        }
        return out;
    }();
    return tables;
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

const char* route_name(RouteKind route) {
    switch (route) {
        case RouteKind::DepthFirst: return "depth-first";
        case RouteKind::ShallowFast: return "shallow-fast";
        case RouteKind::Dfpn: return "dfpn";
    }
    return "unknown";
}

std::optional<RouteKind> parse_route_kind(const std::string& name) {
    if (name == "dfpn") {
        return RouteKind::Dfpn;
    }
    if (name == "depth-first" || name == "depth_first" || name == "df" || name == "dfs" || name == "default") {
        return RouteKind::DepthFirst;
    }
    if (name == "shallow-fast" || name == "shallow_fast" || name == "shallow" || name == "sf") {
        return RouteKind::ShallowFast;
    }
    return std::nullopt;
}

// Defined below; parse_fen4 uses it to reject positions in which the side
// that just moved is left in check.
bool in_check(const Board& b, Color c);

// The optional fifth Forsyth field, carrying the variant state.
//
// Two spellings, told apart by the leading '+':
//
//   3+3        checks REMAINING for white and black. The primary spelling,
//              because it states the position rather than its history and it
//              expresses an asymmetric allowance -- 5+2 -- without a limit
//              alongside. Kept bare and untagged for compatibility: it is the
//              spelling x-check shipped with, and corpora hold it.
//   +1+2       checks DELIVERED, the Lichess spelling. Only well defined against
//              a limit, and Lichess only uses three, so it is read as three.
//   chk3+3     the same, tagged.
//   cap5+2     capture quotas, remaining, for white and black.
//   chk3+3,cap5+2   both rules at once.
//
// TAGGED RATHER THAN POSITIONAL. A sixth Forsyth field for the second rule would
// be brittle and would have to be extended again for the third; a tagged list
// extends by vocabulary, which is the same reason the counters themselves are
// indexed by rule.
//
// Returns false for anything else, INCLUDING plain junk, because a fifth token
// is not necessarily a check field: corpora here routinely carry annotations
// after the four Forsyth fields, and tests/smoke.epd puts `bm #1` exactly there.
// A parser that claimed those would break every existing corpus, so an
// unrecognised token is ignored precisely as it is today.
inline bool parse_quota_pair(const std::string& token, int rule,
                             std::array<std::uint8_t, kQuotaSlots>& out) {
    const bool delivered = !token.empty() && token[0] == '+';
    const std::string body = delivered ? token.substr(1) : token;
    const std::size_t plus = body.find('+');
    if (plus == std::string::npos || plus == 0 || plus + 1 >= body.size()) {
        return false;
    }
    const std::string left = body.substr(0, plus), right = body.substr(plus + 1);
    for (const std::string& part : {left, right}) {
        if (part.empty() || part.size() > 3) return false;
        for (char ch : part) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        }
    }
    const int a = std::atoi(left.c_str()), c = std::atoi(right.c_str());
    if (delivered) {
        const int limit = 3;                 // the only limit the spelling has
        if (rule != VR_CHECK || a > limit || c > limit) return false;
        out[quota_index(WHITE, rule)] = static_cast<std::uint8_t>(limit - a);
        out[quota_index(BLACK, rule)] = static_cast<std::uint8_t>(limit - c);
        return true;
    }
    if (a > kMaxQuota || c > kMaxQuota) return false;
    out[quota_index(WHITE, rule)] = static_cast<std::uint8_t>(a);
    out[quota_index(BLACK, rule)] = static_cast<std::uint8_t>(c);
    return true;
}

inline bool parse_variant_field(const std::string& token,
                                std::array<std::uint8_t, kQuotaSlots>& out) {
    out.fill(kNoQuota);
    std::size_t start = 0;
    bool any = false;
    while (start <= token.size()) {
        const std::size_t comma = token.find(',', start);
        const std::string term = token.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        if (term.empty()) return false;
        int rule = VR_CHECK;
        std::string body = term;
        if (term.rfind("chk", 0) == 0) {
            body = term.substr(3);
        } else if (term.rfind("cap", 0) == 0) {
            rule = VR_CAPTURE;
            body = term.substr(3);
        }
        if (!parse_quota_pair(body, rule, out)) return false;
        any = true;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return any;
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

    if (tokens[1] != "w" && tokens[1] != "b") {
        return std::nullopt; // side to move must be w or b, not silently white
    }
    b.stm = tokens[1] == "b" ? BLACK : WHITE;
    b.castling = 0;
    if (tokens[2].find('K') != std::string::npos) b.castling |= 1;
    if (tokens[2].find('Q') != std::string::npos) b.castling |= 2;
    if (tokens[2].find('k') != std::string::npos) b.castling |= 4;
    if (tokens[2].find('q') != std::string::npos) b.castling |= 8;

    b.ep = -1;
    if (tokens[3] != "-") {
        if (tokens[3].size() < 2) {
            return std::nullopt;
        }
        const int ef = tokens[3][0] - 'a';
        const int er = tokens[3][1] - '1';
        // An en-passant square is only meaningful on the third or sixth rank.
        // Accepting anything else silently would let a malformed field through.
        if (!on_board(ef, er) || (er != 2 && er != 5)) {
            return std::nullopt;
        }
        b.ep = square_of(ef, er);
    }

    // Reject positions that are not legal chess before any search runs.
    //
    // Without this an input such as "8/8/8/8/8/8/8/KKKKKKKK w - -" -- eight
    // white kings and no black king -- was accepted and reported "dm 1", a
    // mate claim in a position with no king to mate. A prover whose output is
    // a proof must refuse to answer questions that are not well posed.
    int kings[2] = {0, 0};
    for (int sq = 0; sq < 64; ++sq) {
        const char p = b.sq[sq];
        if (p == 'K') ++kings[WHITE];
        if (p == 'k') ++kings[BLACK];
        // Pawns cannot stand on the first or last rank.
        if ((p == 'P' || p == 'p') && (rank_of(sq) == 0 || rank_of(sq) == 7)) {
            return std::nullopt;
        }
    }
    if (kings[WHITE] != 1 || kings[BLACK] != 1) {
        return std::nullopt;
    }
    b.king_sq[WHITE] = -1;
    b.king_sq[BLACK] = -1;
    for (int sq = 0; sq < 64; ++sq) {
        if (b.sq[sq] == 'K') b.king_sq[WHITE] = sq;
        if (b.sq[sq] == 'k') b.king_sq[BLACK] = sq;
    }
    // The side that just moved cannot be left in check; such a position is
    // unreachable, and searching it would answer a question about a game that
    // could not have occurred.
    if (in_check(b, other(b.stm))) {
        return std::nullopt;
    }
    if (tokens.size() >= 5) {
        std::array<std::uint8_t, kQuotaSlots> quota{};
        if (parse_variant_field(tokens[4], quota)) {
            b.quota = quota;
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
    // Emitted ONLY when some rule is in force, so every standard-chess result
    // line stays byte-identical. The corpora, the suite's differentials and the
    // harness's strict parser all compare these strings.
    //
    // A check-only position emits the bare `3+3` it always did, for the same
    // reason: that spelling is already in corpora and in the suite.
    if (variant_active(b)) {
        const bool checks_only =
            quota_of(b, WHITE, VR_CAPTURE) == kNoQuota &&
            quota_of(b, BLACK, VR_CAPTURE) == kNoQuota;
        const char* tags[VR_COUNT] = {"chk", "cap"};
        out << ' ';
        bool first = true;
        for (int rule = 0; rule < VR_COUNT; ++rule) {
            if (quota_of(b, WHITE, rule) == kNoQuota &&
                quota_of(b, BLACK, rule) == kNoQuota) {
                continue;
            }
            if (!first) out << ',';
            if (!checks_only) out << tags[rule];
            out << static_cast<int>(quota_of(b, WHITE, rule)) << '+'
                << static_cast<int>(quota_of(b, BLACK, rule));
            first = false;
        }
    }
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



// Bitboard forms of the leaper and ray tables, derived from the same square
// lists so the two representations cannot disagree.
struct AttackBitboards {
    std::array<std::uint64_t, 64> knight{};
    std::array<std::uint64_t, 64> king{};
    std::array<std::array<std::uint64_t, 64>, 2> pawn{}; // squares a pawn of [color] attacks target from
    std::array<std::array<std::uint64_t, 64>, 8> ray{};
    std::array<bool, 8> ray_ascending{};
};

const AttackBitboards& attack_bb() {
    static const AttackBitboards table = [] {
        AttackBitboards out{};
        auto pack = [](const SquareList& list) {
            std::uint64_t bb = 0;
            for (int i = 0; i < list.count; ++i) {
                bb |= 1ull << list.sq[i];
            }
            return bb;
        };
        for (int sq = 0; sq < 64; ++sq) {
            out.knight[sq] = pack(knight_table()[sq]);
            out.king[sq] = pack(king_table()[sq]);
            out.pawn[WHITE][sq] = pack(pawn_attacker_table()[WHITE][sq]);
            out.pawn[BLACK][sq] = pack(pawn_attacker_table()[BLACK][sq]);
            for (int dir = 0; dir < 8; ++dir) {
                out.ray[dir][sq] = pack(ray_table()[dir][sq]);
            }
        }
        // Whether a ray's squares ascend in index, which decides whether the
        // nearest blocker is the lowest or highest set bit. Derived from the
        // table rather than assumed from a direction convention.
        for (int dir = 0; dir < 8; ++dir) {
            for (int sq = 0; sq < 64; ++sq) {
                const SquareList& ray = ray_table()[dir][sq];
                if (ray.count > 0) {
                    out.ray_ascending[dir] = ray.sq[0] > sq;
                    break;
                }
            }
        }
        return out;
    }();
    return table;
}

bool attacked_on_planes(std::uint64_t occ,
                        const std::array<std::uint64_t, 2>& by_color,
                        const std::array<std::uint64_t, 6>& by_type,
                        int target, Color by) {
    const AttackBitboards& tb = attack_bb();
    const std::uint64_t them = by_color[by];

    if (tb.knight[target] & by_type[PT_KNIGHT] & them) {
        return true;
    }
    if (tb.king[target] & by_type[PT_KING] & them) {
        return true;
    }
    if (tb.pawn[by][target] & by_type[PT_PAWN] & them) {
        return true;
    }

    const std::uint64_t queens = by_type[PT_QUEEN] & them;
    const std::uint64_t diagonal = (by_type[PT_BISHOP] & them) | queens;
    const std::uint64_t straight = (by_type[PT_ROOK] & them) | queens;

    // Directions 0-3 are orthogonal and 4-7 diagonal, matching ray_table.
    for (int dir = 0; dir < 8; ++dir) {
        const std::uint64_t sliders = dir < 4 ? straight : diagonal;
        if (!sliders) {
            continue;
        }
        const std::uint64_t blockers = tb.ray[dir][target] & occ;
        if (!blockers) {
            continue;
        }
        // Only the nearest piece along the ray can attack the target.
        const int first = tb.ray_ascending[dir] ? lsb_index(blockers) : msb_index(blockers);
        if ((1ull << first) & sliders) {
            return true;
        }
    }
    return false;
}

// E, THE ESCAPE COUNT. How confined is a king, counted as if it were the only
// mover on the board?
//
// The ring is the up-to-eight squares adjacent to the king: eight in the
// interior, five on an edge, three in a corner. A ring square counts when the
// king could legally step onto it.
//
// THE KING IS REMOVED FIRST, and that is the whole subtlety. Left on the board
// it blocks sliding lines that pass through its own square, so a rook checking
// along a file would leave the square directly BEHIND the king looking safe --
// which is exactly the square the king cannot legally run to. Removing it makes
// the sliding attacks pass through, and the count comes out right.
//
// The two conditions in the specification collapse into one query, and it is
// worth saying why rather than leaving it as a coincidence. An occupied square
// needs the enemy man on it to be UNDEFENDED; an empty square needs to be
// unattacked by the enemy. "Defended by the opponent" and "attacked by the
// opponent" are the same predicate, so one `attacked_on_planes` call settles
// both cases. Attack generation does not care whether the target square is
// occupied -- occupancy matters only for sliders passing BEYOND it -- so the
// enemy man is left in place for the query, which is what makes the recapture
// reading correct. A pinned defender still defends, matching normal chess.
//
// En passant is ignored: it cannot bear on a king step. Castling is ignored:
// E counts single steps only.
inline int escape_count(const Board& b, Color side) {
    const int k = b.king_sq[side];
    if (k < 0) {
        return 0;
    }
    const Color foe = other(side);
    const std::uint64_t king_bit = 1ull << k;

    // The king-removed position, built once and reused for every ring square.
    const std::uint64_t occ = b.occ & ~king_bit;
    std::array<std::uint64_t, 2> by_color = b.by_color;
    std::array<std::uint64_t, 6> by_type = b.by_type;
    by_color[side] &= ~king_bit;
    by_type[PT_KING] &= ~king_bit;

    std::uint64_t ring = attack_bb().king[k];
    int escapes = 0;
    while (ring) {
        const int sq = lsb_index(ring);
        ring &= ring - 1;
        const std::uint64_t bit = 1ull << sq;
        // Occupied by one of our own men: blocked, and no query needed.
        if ((occ & bit) && !(by_color[foe] & bit)) {
            continue;
        }
        if (attacked_on_planes(occ, by_color, by_type, sq, foe)) {
            continue;
        }
        ++escapes;
    }
    return escapes;
}

bool is_attacked(const Board& b, int target, Color by) {
    return attacked_on_planes(b.occ, b.by_color, b.by_type, target, by);
}

// Does this side attack anything to capture RIGHT NOW?
//
// The first half of the capture-distance estimator. If a side attacks nothing,
// its first capture cannot be this move, so q captures need at least q+1 moves.
//
// Contact is read off the attack map rather than by generating moves, so it
// ignores pins and legality and OVER-states what is available. Over-stating is
// the safe direction here: it can only make the estimator concede reachability
// it might otherwise have denied, never claim an unreachability that is false.
inline bool side_has_capture_contact(const Board& b, Color by) {
    std::uint64_t enemy = b.by_color[other(by)];
    while (enemy) {
        const int sq = lsb_index(enemy);
        enemy &= enemy - 1;
        if (is_attacked(b, sq, by)) {
            return true;
        }
    }
    // An en-passant target may be capturable; assume it is rather than prove it.
    return b.ep >= 0;
}

// `variant_reachable_within` sharpened by contact.
//
// The arithmetic test asks only whether the quota fits in the move budget. This
// adds the move it takes to REACH something, and only where that extra move can
// change the verdict -- when a capture quota exactly equals the budget. Anywhere
// else the answer is already settled, so the attack-map scan never runs.
inline bool variant_reachable_static(const Board& b,
                                     const std::array<bool, VR_COUNT>& rule_wins,
                                     int moves) {
    if (!variant_reachable_within(b, rule_wins, moves)) {
        return false;
    }
    if (!rule_wins[VR_CAPTURE]) {
        return true;
    }
    // Is a capture quota the ONLY thing keeping this reachable, and is it exactly
    // at the budget? Only then does one more move of setup decide it.
    bool decided_by_capture_at_limit = false;
    for (int colour = 0; colour < 2; ++colour) {
        for (int rule = 0; rule < VR_COUNT; ++rule) {
            if (!rule_wins[static_cast<std::size_t>(rule)]) continue;
            const std::uint8_t q = quota_of(b, colour, rule);
            if (q == kNoQuota) continue;
            if (static_cast<int>(q) > moves) continue;
            if (rule != VR_CAPTURE || static_cast<int>(q) != moves) {
                return true;   // something else reaches with room to spare
            }
            if (!side_has_capture_contact(b, static_cast<Color>(colour))) {
                decided_by_capture_at_limit = true;
            } else {
                return true;
            }
        }
    }
    return !decided_by_capture_at_limit;
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

} // namespace mateprover

#endif // MATEPROVER_BOARD_H_INCLUDED
