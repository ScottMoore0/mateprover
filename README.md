# MateProver

An exact directmate prover that emits **machine-checkable proof certificates**.

Given a position and a depth N, mateprover either proves a forced mate in N and
emits a proof an independent checker can verify, or reports that it found none.
It does not estimate, search heuristically for a likely mate, or return a score
— every reported mate is a complete AND/OR proof in which every legal defender
reply is refuted.

```
$ echo "8/2Q5/R7/8/1k4K1/8/8/8 w - -" | mateprover -z 2 -
8/2Q5/R7/8/1k4K1/8/8/8 w - -; acn 99; acs 0.000272; bm a6b6; dm 2; pv a6b6 b4a3 c7a7;
```

## What makes it different

Most engines reporting `mate in N` are reporting a search result. mateprover
reports a **proof**, and with `--emit-proof` it hands you the whole thing:

```
proof {"a":"a6b6","d":[{"r":"b4a3","p":{"a":"c7a7","mate":true}}, ...]}
```

Every attacker node carries one proof move. Every defender node enumerates
**exactly** the legal replies — no sampling, no representative line — and each
leaf ends in real checkmate.

The checker ships with the engine, so the claim is yours to verify rather than
mine to assert:

```
mateprover --emit-proof -z 5 - < positions.epd | python tools/verify_proof.py
```

The reach figures are reproducible, with one step of setup. The position sets are
**not** distributed — they are drawn from a separately-licensed corpus — but they
are rebuildable:

```
python tools/fetch_corpus.py          # downloads the corpus, pinned and checksummed
python tools/mint_eval_set.py ...     # arguments from benchmarks/MANIFEST.json
python tools/reproduce_results.py --engine build/mateprover
```

A set is determined by its corpus *revision*, depth, count, seed **and the sets
minted before it**, all recorded in `benchmarks/MANIFEST.json`, which also holds
a `sha256` per set so a rebuild is verified rather than assumed.

**Every figure quoted below rests on a set you can rebuild and verify against a
recorded digest.** Older sets that could not be rebuilt were replaced rather than
excused: they are still listed, marked `"rebuildable": false` with the reason, but
nothing on this page depends on them. `benchmarks/README.md` explains what is and
is not reproducible, and why two of the retired sets never could be.

[CHANGELOG.md](CHANGELOG.md) records what each version is and what it measured.

[docs/RESULTS.md](docs/RESULTS.md) is the place to start if you want to know
where this engine's capability comes from, what was tried and rejected with
measurements, and how far to trust the numbers above. It is short.

The output line format is specified in
[docs/OUTPUT_FORMAT.md](docs/OUTPUT_FORMAT.md). Note especially that a completed
search finding no mate ends after `acs` with no marker, while a search that ran
out of budget says `timeout`; the two must not be conflated.

The certificate format is specified in [docs/PROOF_FORMAT.md](docs/PROOF_FORMAT.md),
precisely enough to write an independent verifier without reading the engine.
That is the point of it: the claim should not rest on trusting the prover.

`tools/verify_proof.py` re-derives every legal move itself and never consults
the engine, so it catches a certificate that omits an inconvenient defence,
marks a non-mating leaf as mate, or overstates the depth. The test suite exercises
exactly those forgeries against it.

That property drives the design: an optimisation is acceptable only if the
proof still verifies, and several plausible optimisations in this engine's
history were rejected for failing that bar rather than for being slow.

## Performance

Measured on a 32-core Windows host, GCC 15, `-O3`.

Single-threaded throughput is strongly position-dependent — **0.7M to 1.7M
nodes/sec** on hard positions, depending on how much of the tree is spent in the
preconditioner. A single headline figure would be misleading, and node rate is
the wrong thing to optimise anyway: this project measured that **halving the
nodes needed is worth about one extra position in forty**, the same as doubling
the speed (`docs/ARCHITECTURE.md` §43).

The comparison worth making is against an established mate solver, run as the
reference implementation under matched conditions — same machine, positions,
memory and time cap, both single-threaded. On positions solved by both:

| | time | nodes |
|---|---|---|
| mate-8 (39 positions) | 3.6x mean, **20x median** | 7.1x mean, 25x median |
| mate-10 (17 positions) | 15.8x mean, **16x median** | 39.8x mean, 38x median |

Mean and median differ because a few slow positions dominate the mean. Full
methodology, the reference implementation's identity and version, and the
solve-rate comparison are in `docs/RESULTS.md`.

## Capability

Speed matters less than reach, so this is measured against
[matetrack](https://github.com/vondele/matetrack), a public mate benchmark,
rather than suites chosen by this project.

Solve rate on evaluation sets that were minted before the work they judge,
measured exactly once, and never consulted in between. Wilson 95% intervals:

| problems | solve rate | 95% CI |
|---|---:|---|
| mate-in-8, default configuration, 15 s (200 positions) | **78.0%** | 71.8–83.2 |
| mate-in-10, 30 s, 32 threads, `--direct-depth` (60) | **96.7%** | 88.6–99.1 |
| mate-in-12, same conditions (40) | 82.5% | 68.0–91.3 |
| mate-in-14, same conditions (40) | 75.0% | 59.8–85.8 |
| mate-in-16, same conditions (40) | 70.0% | 54.6–81.9 |
| mate-in-20, same conditions (40) | 55.0% | 39.8–69.3 |

Every row rests on a set you can rebuild and check against a recorded digest.
The mate-in-8, 10 and 20 sets were re-minted from the pinned corpus and measured
once; the mate-in-8 and mate-in-10 sets are drawn only from positions no earlier
set had used, so they are fresh evidence as well as reproducible.

Budget scaling at mate-in-8, measured on a now-retired set: 80.0% at 15 s, 90.5%
at 60 s, **96.0% at 240 s**. Sixteen times the budget converts four fifths into
all but eight — the shape is what matters, and that set cannot be rebuilt.

Switching the default route to DFPN was worth seventeen positions of sixty at
mate-in-10, gained with none lost.

**Read this honestly.** MateProver solves mate-in-5 and below essentially always,
and mate-in-6/7 reliably in seconds. Deeper, there is no wall: the solve rate
declines gradually and is still above half at mate-in-20. But the intervals are
wide because the sets are small — mate-in-20's 55.0% is consistent with anything
from 40% to 69%, and should be read as "roughly half", not as 55.0%. The
mate-in-10 set is only sixty positions, so 96.7% likewise means "nearly all of a
small sample", not a fourth significant figure.

Against the reference implementation, same machine, positions, memory and
30-second cap, both single-threaded: at mate-in-8 40/40 against 39/40; at
mate-in-10 37/40 against 17/40; at mate-in-12 33/40 against 8/40.

Where the reach comes from is worth stating plainly, because it is not speed. At
mate-in-10 the restriction portfolio — running several *soundly restricted*
searches concurrently, any of whose proofs is a real proof — is worth **+15
positions of 60, losing none**. Four times the time buys nothing there, and four
times the memory buys nothing. At mate-in-8 the engine is budget-limited
instead, which is why more time keeps converting there and more restriction
lanes do not.

`--direct-depth` proves "a mate within N" rather than "the shortest mate is N";
it is not the default.

## Build

### From a release

Each release attaches a self-contained binary for Linux, macOS and Windows. It
needs nothing else installed — `tools/verify_proof.py` needs python-chess, but
the prover itself has no runtime dependencies.

```
# Linux / macOS: download the asset for your platform, then
chmod +x mateprover-linux-x86_64
./mateprover-linux-x86_64 --version
echo "8/2Q5/R7/8/1k4K1/8/8/8 w - - dm 2" | ./mateprover-linux-x86_64 -
```

```
# Windows
.\mateprover-windows-x86_64.exe --version
echo 8/2Q5/R7/8/1k4K1/8/8/8 w - - dm 2 | .\mateprover-windows-x86_64.exe -
```

macOS may quarantine a downloaded binary; `xattr -d com.apple.quarantine
mateprover-macos-arm64` clears it. Every release asset is built by CI and must
report a version matching its tag and solve a mate-in-2 before it is published.

### From source

C++17 and CMake 3.16+. No third-party libraries.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binary lands at `build/mateprover` (`.exe` on Windows).

Built and tested in CI on Linux/GCC, Linux/Clang, macOS/Clang and
Windows/MSVC, plus Windows/MinGW-w64 GCC 15 locally. CI additionally runs a
bounds-checked build, a C++20/23 forward-compatibility check, cppcheck, and
`-Wall -Wextra -pedantic -Werror`.

## Test

```
ctest --test-dir build --output-on-failure
```

or directly:

```
python tests/run_tests.py --engine build/mateprover
```

333 automated checks covering:

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
mateprover [options] -            read EPD/FEN lines from stdin
mateprover [options] < file       read a single position from stdin
mateprover --help                 full option list
```

Input is a FEN (the first four fields are enough). The mate depth comes from
`-z N`, or is inferred from a `#N` token in an EPD line.

mateprover can also be kept running as a service: feed it positions one at a time on
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

- **Scope: directmates, stalemates and selfmates.** MateProver proves "White to
  move mates in N" (`dm N`), "White forces stalemate in N" (`--stalemate`, `sm
  N`) and "White forces Black to mate White in N" (`--selfmate`, `sfm N`).
  Chest, the program it is measured against, also solves self-stalemate,
  helpmate and help-stalemate. On those three families MateProver has no answer.
- **The selfmate goal is newer than the others and single-threaded.** It has its
  own preconditioner (worth +124 positions of 200 on hard problems), its own
  verified certificates, and a restriction portfolio that measurably helps
  (+17 of 200). It does use the root split, which is deterministic across thread
  counts but measurably worthless — 1.06x — exactly as for directmate. Measured
  reach at s#5–s#10 is 35.3% before the preconditioner; the tuned figure has not
  been re-measured across the full corpus.
- **The stalemate goal is measured, and it is at its ceiling.** On 579 composed
  problems from a public collection the engine solves 94.7%, with a real
  frontier: 100% through `=9`, 28.6% at `=12`, 9.1% at `=16`. Every restriction
  lane and every search setting was then measured against the 60 problems at
  depth 10-16, and **none beats the shipped default** — the restriction
  portfolio that gives the directmate mode +15 positions of 60 contributes
  exactly nothing here (architecture 49). The reach and speed figures elsewhere
  on this page are directmate figures and do not describe stalemate.
- Reach: see the table above. The two depths behave differently and the
  difference is measured, not assumed. At mate-in-8 the engine is
  budget-limited, so time buys reach. At mate-in-10 it is not: time and memory
  buy nothing and only the restriction portfolio helps. A result about one depth
  does not describe the other, and neither survives a change of configuration --
  both of these numbers replaced earlier ones that had quietly gone stale.
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
- Beyond the frontier, more hardware does not help: parallelism is worth about
  one extra position in forty per doubling, and root splitting nothing at all.
  The remaining engineering headroom is under half a depth notch.

## Project context

MateProver is a from-scratch implementation. It was developed inside a larger
workspace that also holds other mate solvers, which were run as behavioural
oracles and as benchmark references — that is how the restriction semantics were
established and how the comparative figures above were produced. **No code from
any of them appears here.** Every line is original, and the option semantics were
derived by observing what those programs do, not by copying how they do it.

`docs/ARCHITECTURE.md` records the design and, in more detail than is
usual, the optimisations that were measured and **rejected** — including why.

## Licence

MateProver is released under the MIT License. See `LICENSE` for the full
text.

`tools/verify_proof.py` requires python-chess, which is not distributed with
MateProver and must be installed separately. Benchmark position sets are not
distributed either; `benchmarks/README.md` explains how to regenerate them.
