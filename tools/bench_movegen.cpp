// bench_movegen.cpp -- Where does a move-generation call actually spend its time?
//
// Node-rate ablations inside the search are confounded: turning a feature off
// changes move ordering, which changes which nodes get visited, so nodes/sec
// moves for reasons unrelated to the cost of the thing removed. Measuring one
// nested stage at a time on a fixed set of positions has no such confound.
//
// Build (or use the `bench_movegen` CMake target):
//   g++ -std=c++17 -O3 -DNDEBUG -I ../src -pthread -o bench_movegen bench_movegen.cpp
// Run, one FEN per line on stdin:
//   ./bench_movegen 20000 < positions.txt

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "types.h"
#include "table.h"
#include "search_state.h"
#include "board.h"
#include "movegen.h"
#include "ordering.h"

using namespace mateprover;

int main(int argc, char** argv) {
    std::vector<Board> boards;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (auto b = parse_fen4(line)) {
            boards.push_back(*b);
        }
    }
    if (boards.empty()) {
        std::fprintf(stderr, "no positions on stdin (expected one FEN per line)\n");
        return 2;
    }
    const int reps = argc > 1 ? std::atoi(argv[1]) : 20000;
    if (reps <= 0) {
        std::fprintf(stderr, "repetition count must be positive\n");
        return 2;
    }

    SearchConfig cfg;
    cfg.move_reserve = true;
    cfg.move_reserve_capacity = 96;

    // volatile so the optimiser cannot delete the work being timed.
    volatile std::size_t sink = 0;
    using clk = std::chrono::steady_clock;

    auto time_it = [&](const char* tag, auto&& fn) {
        const auto t0 = clk::now();
        for (int r = 0; r < reps; ++r) {
            for (const Board& b : boards) {
                sink += fn(b);
            }
        }
        const double elapsed = std::chrono::duration<double>(clk::now() - t0).count();
        const double per_call = elapsed / (static_cast<double>(reps) * static_cast<double>(boards.size()));
        std::printf("  %-36s %7.0f ns/call\n", tag, per_call * 1e9);
        return per_call;
    };

    std::printf("%zu positions, %d repetitions\n", boards.size(), reps);

    // Each stage is the previous one plus a layer, so the differences attribute
    // cost to the layer rather than to the whole call.
    const double gen = time_it("generation only", [](const Board& b) {
        MoveList ml;
        gen_pseudo(b, ml);
        return ml.count;
    });
    const double legal = time_it("+ legality (planes per move)", [](const Board& b) {
        MoveList ml;
        gen_pseudo(b, ml);
        std::size_t n = 0;
        for (std::size_t i = 0; i < ml.count; ++i) {
            int king_after = -1;
            const Planes pl = planes_after_move(b, ml.moves[i], king_after);
            if (king_after >= 0 &&
                !attacked_on_planes(pl.occ, pl.by_color, pl.by_type, king_after, other(b.stm))) {
                ++n;
            }
        }
        return n;
    });
    const double fused = time_it("+ scoring and list build (fused)", [&](const Board& b) {
        bool scored = false;
        return legal_moves_fused(b, cfg, scored).size();
    });

    std::printf("\n  generation                %5.0f%%\n", 100.0 * gen / fused);
    std::printf("  legality (planes/move)    %5.0f%%\n", 100.0 * (legal - gen) / fused);
    std::printf("  scoring and list build    %5.0f%%\n", 100.0 * (fused - legal) / fused);
    return sink == 123456789 ? 1 : 0;
}
