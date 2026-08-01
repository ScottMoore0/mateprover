# E Chest

E Chest is the architectural rewrite line for an exact directmate prover.

It is a new implementation. Other mate solvers are used for testing,
benchmarking and verification, but the prover core is written from scratch
and uses no code from any of them.

## Current Status

This initial checkpoint establishes:

- standalone source tree;
- standalone executable interface compatible with the benchmark harness;
- legal move generation and directmate proof scaffolding;
- incrementally maintained packed board words for exact TT key construction;
- proof-carrying output in UCI move format;
- exact PV replay compatibility with the existing validator;
- opt-in recursive proof-tree output verified independently with python-chess;
- documentation for the full rewrite path.

The first implementation is intentionally conservative. It prioritizes correctness and auditability over maximum speed while the proof kernel and board representation are brought up under tests.

## Build

Requires a C++17 compiler and CMake 3.16+. No third-party libraries.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binary lands at `build/echest` (`build/echest.exe` on Windows).

Verified on Linux/GCC 13, Windows/MinGW-w64 GCC 15, and configured for
macOS/Clang and Windows/MSVC in CI.

## Test

```
ctest --test-dir build --output-on-failure
```

or directly:

```
python tests/run_tests.py --engine build/echest
```

The suite covers:

- **perft** against published reference counts for six standard positions,
  exercising castling rights, en-passant capture and expiry, promotion
  including under-promotion, and pinned-piece legality;
- **known directmates** solved at the exact stated depth with a PV of the
  right length;
- **negative controls** that must produce no `dm` token;
- **invariance**: the key move and mate depth must not change with thread
  count, table sharing, the parallel cost gate, or the memory budget --
  including a budget tight enough to force heavy eviction;
- **PV replay and proof-certificate verification**, enabled automatically when
  `python-chess` is installed and skipped cleanly when it is not.

The core tests have no third-party dependency.

## CLI Shape

E accepts the Chest-style subset needed by the existing benchmark harness:

```text
echest.exe -b -1 -5 -M 64 -z 2 -
```

Supported now:

- `-b`: accepted for compatibility;
- `-1`: accepted for compatibility;
- `-5`: output UCI-style coordinate moves;
- `-M N`: table memory budget in megabytes, honoured as an entry ceiling; `-M 0` is unbounded;
- `-z N`: requested mate depth;
- `--route depth-first`: select the current exact depth-first route; this is the promoted default;
- `--route dfpn`: unpromoted proof-number search preconditioner; at least 10x slower than the default on deep positions, retained for reproducibility;
- `--route shallow-fast`: unpromoted exact route that tries mate-in-1 and mate-in-2 directly, then falls back to depth-first for deeper requests;
- `--emit-proof`: append a recursive JSON proof certificate for solved positions;
- `--profile`: emit one `% e_profile {...}` JSON counter row to stderr per input position;
- `--no-profile`: keep profiling disabled, which is the promoted default;
- `--score-mates`: restore the older, more expensive move-order score that detects immediate mates during sorting;
- `--no-mate-score`: keep the promoted default, included for explicitness in experiments;
- `--score-checks`: keep the promoted default, which scores checking moves during ordering;
- `--no-check-score`: experimental probe that disables checking-move scoring during ordering;
- `--fast-check-score`: no-op alias, retained for CLI compatibility; check scoring is now a single shared plane query;
- `--exact-check-score`: no-op alias, retained for CLI compatibility;
- `--order-min-size N`: experimental probe that skips scoring/sorting legal move lists smaller than `N`;
- `--order-all`: restore the promoted default, equivalent to ordering move lists with at least two moves;
- `--bucket-order`: experimental probe that groups moves by descending score while preserving stable order inside each score bucket;
- `--stable-sort-order`: restore the promoted stable-sort ordering implementation;
- `--refutation-hints`: experimental probe that moves known defender refutations to the front;
- `--no-refutation-hints`: keep defender refutation hints disabled, which is the promoted default;
- `--proof-hints`: promoted ordering-only mode that moves known attacker proof moves to the front;
- `--no-proof-hints`: disable attacker proof hints for rollback and A/B checks;
- `--keep-iter-tt`: promoted exact-TT mode that keeps entries across iterative-depth passes;
- `--clear-iter-tt`: restore the previous behavior, clearing exact TT entries before each iterative-depth pass;
- `--bound-tt`: experimental probe that adds a depth-bound TT beside exact-depth TT entries;
- `--exact-tt-only`: keep the promoted default, using exact-depth TT entries only;
- `--bound-tt-failures`: include failed-node bounds in the depth-bound TT probe;
- `--bound-tt-ok-only`: keep the probe default, storing only proven-node bounds;
- `--ordered-check-shortcut`: promoted attacker-loop mode that uses already computed check scores to skip immediate-mate tests for moves known not to give check;
- `--no-ordered-check-shortcut`: disable the ordered-check shortcut for rollback and A/B checks;
- `--lazy-defender`: unpromoted probe; order pseudo-legal defender replies and test legality only when reached;
- `--eager-defender`: promoted default; establish legality for all defender replies up front;
- `--fused-order`: promoted default; compute legality and ordering scores in one pass over the child boards;
- `--split-order`: rollback path that generates and scores in two passes;
- `--time-limit S`: wall-clock budget in seconds; on expiry the search stops and reports a `timeout` marker rather than a mate. 0 (default) is unlimited;
- `--threads N`: root-split parallel search across `N` worker threads; the benchmark registry promotes `8`;
- `--parallel-min-nodes N`: cost gate; a depth runs sequentially until it exceeds `N` nodes, then escalates to a split. Default `500`;
- `--no-parallel-gate`: always split, never probe sequentially first;
- `--shared-tt`: promoted table mode for the parallel path; workers share one sharded exact proof table;
- `--private-tt`: rollback path giving each worker its own exact table;
- `--shared-tt-shards N`: shard count for the shared table, default 256;
- `--threads auto`: use the detected hardware concurrency;
- `--single-thread`: keep the promoted default of one thread and the exact sequential path;
- `--perft N`: print perft counts for depths 1..N for the position on stdin;
- `--perft-divide N`: print the per-root-move perft breakdown at depth N;
- `-`: read EPD/FEN lines from stdin.

Unsupported options are currently ignored only when they are harmless compatibility flags. Native typed support for WinChest/Chest restriction options is a later E milestone.

## Output

Solved positions print:

```text
<fen4>; acn <nodes>; acs <seconds>; bm <uci>; dm <depth>; pv <uci ...>;
```

With `--emit-proof`, solved positions additionally print:

```text
proof {"a":"<attacker-move>","mate":true}
```

or, for non-leaf attacker nodes:

```text
proof {"a":"<attacker-move>","d":[{"r":"<defender-reply>","p":<child-proof>}, ...]}
```

The proof tree is verified by `benchmarks\scripts\e_verify_proof_tree.py`. A valid proof must enumerate exactly every legal defender reply at every defender node and each leaf must end in checkmate.

Proof-certificate construction is opt-in. Normal benchmark/search runs do not build recursive proof JSON internally unless `--emit-proof` is passed.

No-mate or unproved positions print no `dm` token, so the existing harness treats them as no mate.

Profile rows are stderr-only and are intended for search-design work, not for normal benchmark scoring. They include node counts, TT probes/hits/stores, attacker/defender move-list counts, ordering counts, immediate-mate tests, and refutation-hint counters. The helper script `benchmarks\scripts\collect_e_profiles.py` runs a registry engine over a suite and writes case-labelled JSONL profile rows.
