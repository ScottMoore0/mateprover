# echest

An exact directmate prover that emits **machine-checkable proof certificates**.

Given a position and a depth N, echest either proves a forced mate in N and
emits a proof an independent checker can verify, or reports that it found none.
It does not estimate, search heuristically for a likely mate, or return a score
— every reported mate is a complete AND/OR proof in which every legal defender
reply is refuted.

```
$ echo "2brrb2/8/p7/7Q/1p1kpPp1/1P1pN1K1/3P4/8 w - -" | echest -z 2 -
2brrb2/8/p7/7Q/1p1kpPp1/1P1pN1K1/3P4/8 w - -; acn 43; acs 0.0002; bm h5a5; dm 2; pv h5a5 d8d7 e3f5;
```

## What makes it different

Most engines reporting `mate in N` are reporting a search result. echest
reports a **proof**, and with `--emit-proof` it hands you the whole thing:

```
proof {"a":"h5a5","d":[{"r":"d8d7","p":{"a":"e3f5","mate":true}}, ...]}
```

Every attacker node carries one proof move. Every defender node enumerates
**exactly** the legal replies — no sampling, no representative line — and each
leaf ends in real checkmate.

The checker ships with the engine, so the claim is yours to verify rather than
mine to assert:

```
echest --emit-proof -z 5 - < positions.epd | python tools/verify_proof.py
```

The output line format is specified in
[docs/OUTPUT_FORMAT.md](docs/OUTPUT_FORMAT.md). Note especially that a completed
search finding no mate ends after `acs` with no marker, while a search that ran
out of budget says `timeout`; the two must not be conflated.

The certificate format is specified in [docs/PROOF_FORMAT.md](docs/PROOF_FORMAT.md),
precisely enough to write an independent verifier without reading the engine.
That is the point of it: the claim should not rest on trusting the prover.

It re-derives every legal move itself with python-chess and never consults the
engine, so it catches a certificate that omits an inconvenient defence, marks a
non-mating leaf as mate, or overstates the depth. The test suite exercises
exactly those forgeries against it.

That property drives the design: an optimisation is acceptable only if the
proof still verifies, and several plausible optimisations in this engine's
history were rejected for failing that bar rather than for being slow.

## Performance

Measured on a 32-core Windows host, GCC 15, `-O3`.

Single-threaded throughput on a 40-position hard suite: **659k nodes/sec**.

Speed relative to the same suites earlier in development, with identical
answers and identical node counts:

| | speedup |
|---|---:|
| sequential (efficiency only) | 1.44x |
| default (8 threads) | 2.93x |

## Capability

Speed matters less than reach, so this is measured against
[matetrack](https://github.com/vondele/matetrack), a public mate benchmark,
rather than suites chosen by this project.

Solve rate within a wall-clock budget, 8 threads, `-M 256`:

| problems | 0.5 s | 2 s | 5 s |
|---|---:|---:|---:|
| mate-in-8 (24 sampled) | 5/24 | 6–7/24 | 10–11/24 |
| mate-in-10 (20 sampled) | 0/20 | 0/20 | 2/20 |

With `--direct-depth`, which proves "a mate within N" instead of "the shortest
mate is N":

| problems | 2 s | 5 s |
|---|---:|---:|
| mate-in-8 | 9/24 | 12/24 |
| mate-in-10 | 3/20 | 3/20 |

Ranges reflect run-to-run variation for problems sitting near the budget
boundary.

**Read this honestly:** echest solves mate-in-5 and below essentially always,
mate-in-6/7 reliably in seconds, and a small fraction of mate-in-10. It
addresses the shallow end of a standard benchmark.

At mate-in-8 the default configuration solves **52 of 60** held-out matetrack
positions in 15 seconds, 56 in 60 seconds and **all 60** in 300 seconds. There,
waiting longer does help: nothing at that depth is out of reach, it is a matter
of budget.

**At mate-in-10 waiting longer does not help.** The solve rate is identical at
5 s, 20 s and 60 s, a 12x increase in budget that solves nothing extra, and the
same is true across 8 to 32 threads and from 64 MB to unbounded memory. That
result is specific to that depth and does not describe mate-in-8. Where it
applies, problems echest does not solve quickly are far out of reach, and the
limitation is the search rather than the resources given to it.

## Build

C++17 and CMake 3.16+. No third-party libraries.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binary lands at `build/echest` (`.exe` on Windows).

Verified on Linux/GCC 13 and Windows/MinGW-w64 GCC 15; macOS/Clang and
Windows/MSVC are configured in CI. Builds clean under
`-Wall -Wextra -pedantic -Werror`.

## Test

```
ctest --test-dir build --output-on-failure
```

or directly:

```
python tests/run_tests.py --engine build/echest
```

135 checks covering:

- **perft** against published reference counts for six standard positions,
  exercising castling rights, en-passant capture and expiry, promotion
  including under-promotion, and pinned-piece legality;
- known directmates solved at the exact depth with a PV of the right length;
- negative controls that must produce no mate;
- **invariance** — the key move and mate depth must not change with thread
  count, table sharing, the parallel cost gate, or the memory budget, including
  a budget tight enough to force heavy eviction;
- **time-limit soundness** — a budgeted search must stop on time and must never
  claim a mate it did not prove;
- CLI contract — bad input is rejected, not silently ignored;
- **illegal positions refused** — wrong king counts, a side-to-move that is
  not `w`/`b`, malformed en passant, pawns on the back ranks, and positions
  where the side not to move is in check;
- **castling requires its rook**, checked against python-chess;
- PV replay and recursive certificate verification, enabled automatically when
  `python-chess` is installed and skipped cleanly when it is not;
- the shipped verifier itself, tested adversarially: it must accept genuine
  certificates and reject an omitted defence, a forged mate leaf, a corrupted
  PV and an overstated depth.

The core tests have no third-party dependency.

## Usage

```
echest [options] -            read EPD/FEN lines from stdin
echest [options] < file       read a single position from stdin
echest --help                 full option list
```

Input is a FEN (the first four fields are enough). The mate depth comes from
`-z N`, or is inferred from a `#N` token in an EPD line.

echest can also be kept running as a service: feed it positions one at a time on
stdin and each answer is flushed as soon as it is ready. Answers never depend on
order or batching, so a long-lived process and a fresh one give identical
results.

**The defaults are the tuned configuration.** Every setting measured as a win is
on by default: all cores up to 16, the search tunings, and, whenever
`--time-limit` is given, the restriction portfolio. Supplying a time limit is
therefore the single highest-value thing a caller can do, since it roughly
doubles reach at mate-in-8. Each default has an opt-out (`--single-thread`,
`--no-portfolio`, and the pairs listed under `--help`) for reproducing a
specific configuration.

Output is one line per position:

```
<fen>; acn <nodes>; acs <seconds>; bm <move>; dm <depth>; pv <moves...>;
```

`bm`, `dm` and `pv` are absent when no mate was proved. A `timeout` marker
appears when the search hit its budget, so "gave up" is distinguishable from
"proved there is no mate".

### Options worth knowing

| option | effect |
|---|---|
| `-z N` | requested mate depth |
| `-M N` | table budget in MB, honoured as an entry ceiling; `0` unbounded |
| `--threads N` \| `auto` | root-split parallel search; `auto` = min(cores, 16) because the split saturates there |
| `--time-limit S` | wall-clock budget; expiry reports `timeout`, never a mate |
| `--direct-depth` | prove "a mate within N" rather than the shortest mate; materially better solve rate at a fixed budget |
| `--portfolio` | spend the time budget across restricted searches as well as the unrestricted one; sound, since a restriction only removes attacker options |
| `--portfolio-parallel` | run those searches concurrently, each with the full budget; uses cores that root splitting saturates on |
| `--emit-proof` | append the recursive JSON proof certificate |
| `--perft N` / `--perft-divide N` | move-generation self-check |
| `--profile` | per-position counters on stderr |

`--help` lists the full set, including search-tuning flags and their rollback
counterparts.

## Correctness

The engine is exact. Beyond the test suite:

- move generation matches python-chess on 294 benchmark positions with zero
  mismatches, and matches published perft counts on six standard positions;
- eviction from the proof table can never change an answer — the table is a
  memo of verdicts that are pure functions of an exact key, so a missing entry
  costs time and nothing else;
- parallel search returns the lowest-index successful root move, so the key
  move is defined independently of thread scheduling;
- an aborted search — cancelled, out of budget, or out of time — records no
  verdict, so it can never be mistaken for a disproof.

A move-generation bug found by perft during development would have allowed a
**false mate**: castling rights were being revoked by captured piece type, so
capturing a promoted rook stripped rights while the original rooks stood,
removing a legal defender escape. Roughly four thousand directmate positions in
this project's own suites never caught it; six perft positions caught it
immediately. Perft is a permanent gate for that reason.

## Limitations

- Reach, as above: the shallow end of matetrack. **At mate-in-10** this is a
  limitation of the search rather than of resources: the solve rate is unchanged
  from 8 to 32 threads and from 64 MB to unbounded memory. That does not carry
  down a depth. At mate-in-8 the engine is budget-limited, not capability-limited
  -- 51/60 held-out positions at 15s, 56/60 at 60s and 60/60 at 300s -- so there,
  time and memory do buy reach.
- The `-M` budget is an entry-count ceiling computed from an estimated bytes per
  entry, not a hard cap on process memory, and it excludes fixed overhead. Small
  budgets overshoot proportionally most: a 64 MB request peaks near 91 MB
  resident, while 512 MB and 2 GB requests stay under. Size it with headroom.
- WinChest's special-mate variants are **implemented**, except one: `-C N`
  (ChecksOnly, all five bits), `-R N` (threat depth, both signs), `-K N`
  (defender king squares), `-P N` (defender pieces able to move) and `-X N`
  (defender moves in total). All are validated against the WinChest binary —
  **666 comparisons, 0 disagreements**. Only `-I` (threat flags) is missing: it
  has no entry in the manual at all, so there is nothing to implement against.
  It is rejected rather than ignored, since it selects a *different problem* —
  a constrained request never returns an unconstrained answer.
  `--allow-unimplemented` searches unrestricted instead. Chest 3.19 itself has
  none of these; they are WinChest extensions.
- `-M` is an entry ceiling derived from an estimated bytes-per-entry, not a
  hard RSS bound; a node-based container cannot give one.
- A DFPN route exists behind `--route dfpn` but is slower than the default at
  every depth measured, and is not recommended.

## Project context

echest is a from-scratch implementation. Other mate solvers are used as
behavioural oracles and differential references in testing; no code from any
of them is used here.

`docs/E_CHEST_ARCHITECTURE.md` records the design and, in more detail than is
usual, the optimisations that were measured and **rejected** — including why.
