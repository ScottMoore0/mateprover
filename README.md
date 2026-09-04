# MateProver

MateProver is an exact chess problem solver. Give it a position and a depth,
and it either proves that the stipulation is forced within that depth or
reports that it found no solution. When it succeeds it can print the whole
proof as a certificate, and a separate checker can confirm the proof without
trusting the engine.

```
$ echo "8/2Q5/R7/8/1k4K1/8/8/8 w - -" | mateprover -z 2 -
8/2Q5/R7/8/1k4K1/8/8/8 w - -; acn 99; acs 0.000272; bm a6b6; dm 2; pv a6b6 b4a3 c7a7;
```

There is no evaluation function and no heuristic score. A reported solution is
a complete AND/OR proof: one move at every attacker node, every legal reply at
every defender node, and checkmate (or the relevant terminal) at every leaf.

## Contents

- [What it solves](#what-it-solves)
- [Proof certificates](#proof-certificates)
- [Installing](#installing)
- [Using it](#using-it)
- [UCI](#uci)
- [Results](#results)
- [Reproducing the numbers](#reproducing-the-numbers)
- [Tests](#tests)
- [Tools](#tools)
- [Correctness](#correctness)
- [Limitations](#limitations)
- [Background](#background)
- [Licence](#licence)

## What it solves

Six stipulations, each with its own result token in the output:

| stipulation | flag | token | meaning |
|---|---|---|---|
| directmate | (default) | `dm` | White forces checkmate |
| stalemate | `--stalemate` | `sm` | White forces stalemate |
| selfmate | `--selfmate` | `sfm` | White forces Black to deliver checkmate |
| selfstalemate | `--selfstalemate` | `ssm` | White forces Black to deliver stalemate |
| helpmate | `--helpmate` | `hm` | both sides cooperate towards checkmate |
| helpstalemate | `--helpstalemate` | `hsm` | both sides cooperate towards stalemate |

These are different problems, not easier or harder versions of one problem. A
checkmate fails a stalemate goal, for example.

Three variant rules can be added to any goal, and to each other:

| rule | flag | effect |
|---|---|---|
| x-check | `--checks N` or `W:B` | a side wins by giving its Nth check |
| x-capture | `--captures N` or `W:B` | a side wins by making its Nth capture |
| x-escape | `--escape N` or `W:B` | a side loses when its king's escape count reaches N |

Quotas are per side, so `--checks 5:2` is as valid as `--checks 3`. The rules
compose: `--checks 3 --captures 5` is a legal game, and a 3-check selfmate is
an ordinary job. Under a mate goal, filling a quota is the win being forced.
Under the other goals it means the game ended before reaching the terminal the
stipulation asks for.

x-escape needs a word of explanation. The escape count E is the number of
squares a king could legally step to, computed with that king removed from the
board so that the king does not block lines through its own square. E is
recomputed at every position and can go up or down. Reaching the limit loses,
so the other side is the winner. In the starting position both kings have
E = 0. The range is 1 to 8, since a king's ring holds at most eight squares.
`--escape-count` prints E for both kings and exits without searching.

## Proof certificates

Most engines that report "mate in N" are reporting a search result. MateProver
reports a proof, and `--emit-proof` prints it:

```
proof {"a":"a6b6","d":[{"r":"b4a3","p":{"a":"c7a7","mate":true}}, ...]}
```

The format is specified in [docs/PROOF_FORMAT.md](docs/PROOF_FORMAT.md) in
enough detail to write an independent verifier without reading the engine.
One ships with the repository:

```
mateprover --emit-proof -z 5 - < positions.epd | python tools/verify_proof.py
```

`tools/verify_proof.py` uses python-chess to re-derive every legal move itself
and never consults the engine. It rejects a certificate that omits a defence,
marks a non-mating leaf as mate, or overstates the depth, and the test suite
feeds it exactly those forgeries to make sure it does.

This is the central design constraint. An optimisation is acceptable only if
the proof still verifies, and several plausible ones were rejected on that
ground rather than for being slow.

## Installing

### Release binaries

Each release attaches a self-contained binary for Linux, macOS and Windows.
The prover has no runtime dependencies. Only the verifier script needs
python-chess.

```
# Linux / macOS
chmod +x mateprover-linux-x86_64
./mateprover-linux-x86_64 --version
echo "8/2Q5/R7/8/1k4K1/8/8/8 w - - dm 2" | ./mateprover-linux-x86_64 -

# Windows
.\mateprover-windows-x86_64.exe --version
echo 8/2Q5/R7/8/1k4K1/8/8/8 w - - dm 2 | .\mateprover-windows-x86_64.exe -
```

macOS may quarantine a downloaded binary. `xattr -d com.apple.quarantine
mateprover-macos-arm64` clears it. Every release asset is built by CI, and the
workflow refuses to publish a binary whose `--version` disagrees with the tag
or which cannot solve a mate-in-2.

### From source

You need a C++17 compiler and CMake 3.16 or later. There are no third-party
libraries.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The binary is written to `build/mateprover` (`mateprover.exe` on Windows).

Please build through CMake. A hand-rolled MinGW build with `-march=native`
miscompiles the board copy (GCC spills a 32-byte aligned store into a stack
frame it never realigned) and the binary faults inside move generation. The
CMake build disables AVX on MinGW for this reason, so `-march=native` in
`CMAKE_CXX_FLAGS` is safe. Nothing is lost: the engine has no vectorised
kernel, and AVX is worth about 0.3% on perft and nothing measurable on
search. Section 79 of `docs/ARCHITECTURE.md` has the disassembly.

CI builds and tests on Linux (GCC and Clang), macOS (Clang), and Windows
(MSVC and MinGW-w64), each a second time with architecture-specific flags,
plus a bounds-checked build, a C++20/23 forward-compatibility check, cppcheck,
and `-Wall -Wextra -pedantic -Werror`.

## Using it

```
mateprover [options] -            read EPD/FEN lines from stdin, one answer per line
mateprover [options] < file       read a single position from stdin
mateprover --help                 full option list
```

Input is a FEN; the first four fields are enough. The depth comes from `-z N`
or from a `#N` token in an EPD line.

You can keep the process running as a service. Feed it positions one at a
time and each answer is flushed as soon as it is ready. Answers never depend
on order or batching, so a long-lived process and a fresh one give identical
results.

### Output

One line per position:

```
<fen>; acn <nodes>; acs <seconds>; bm <move>; dm <depth>; pv <moves...>;
```

`bm`, `dm` and `pv` are absent when nothing was proved. A search that
exhausted the tree and found no solution ends after `acs` with no marker. A
search that ran out of budget says `timeout`. The two mean different things
and the format keeps them apart; [docs/OUTPUT_FORMAT.md](docs/OUTPUT_FORMAT.md)
has the full specification.

### Defaults

The defaults are the tuned configuration. Everything that measured as a win is
on: multi-threaded root splitting, the search tunings, and, whenever a
`--time-limit` is given, the restriction portfolio. Supplying a time limit is
the single most valuable thing a caller can do, because the portfolio roughly
doubles reach at mate-in-8. Each default has an opt-out (`--single-thread`,
`--no-portfolio`, and the pairs listed under `--help`) for reproducing a
particular configuration.

The default proves that the *shortest* solution has the requested depth, by
iterative deepening. `--direct-depth` searches the requested depth only and
proves a solution *within* it. That gives up minimality and buys reach: on a
development set the default solves 52 of 60 mate-in-8 problems where
`--direct-depth` solves 59. If you already know the mate distance, from a
stipulation or an EPD token, there is no reason to pay for rediscovering it.

### Options worth knowing

| option | effect |
|---|---|
| `-z N` | requested depth |
| `-M N` | memory budget in MB. When set, it is the total for every table alive at once. When unset, each table gets 256 MB, and a cooperative search runs nine of them |
| `--threads N` or `auto` | root-split worker threads; `auto` is min(cores − 2, 8). Explicit values above that are honoured, and a depth-7 capture-quota search reaches 9.0x on 24 threads |
| `--time-limit S` | wall-clock budget in seconds; expiry reports `timeout`, never a mate |
| `--node-limit N` | deterministic budget: stop after N nodes, same on every machine |
| `--direct-depth` | prove "a mate within N" rather than "the shortest mate is N" |
| `--portfolio` | spend the time budget across restricted searches as well as the unrestricted one. Sound, since a restriction only removes attacker options |
| `--portfolio-parallel` | run those searches concurrently, each with the full budget |
| `--escape-count` | report E for both kings and stop |
| `--emit-proof` | append the JSON proof certificate |
| `--perft N`, `--perft-divide N` | move-generation self-check |
| `--profile` | per-position counters on stderr |

`--help` lists everything, including the search-tuning flags and their
opposites.

## UCI

```
mateprover --uci
```

The UCI interface is a convenience, and it loses information. `go mate <x>` is
in the UCI specification and maps exactly onto the engine's question:

```
position fen 8/2Q5/R7/8/1k4K1/8/8/8 w - -
go mate 2
info depth 2 nodes 99 time 2 nps 35855 score mate 2 pv a6b6 b4a3 c7a7
bestmove a6b6
```

UCI has no way to say "no solution exists". A GUI that sees no `score mate`
cannot tell an exhaustive disproof from a search that ran out of budget, so
the two are separated on `info string`, which a person can read and a GUI
cannot act on:

```
info string PROVED: no solution exists within the depth searched
info string no verdict: the search hit its budget or was stopped
bestmove 0000
```

`bestmove 0000` is the null move. There is no move to recommend, and naming
one would assert something the engine never proved.

Certificates cannot travel over UCI (a mate-in-8 proof runs to megabytes), so
`--emit-proof` is refused in this mode rather than silently ignored. The other
five goals and the three variant rules are reachable through `setoption`
(`Goal`, `Checks`, `Captures`, `Escape`), but no GUI knows to set them, so in
practice this is a directmate interface.

The UCI answer is a rendering of the same search the EPD interface runs, not a
second search, and the suite checks that the two agree on depth and key move.
The EPD line and the certificate are the record; nothing on this page rests on
the UCI output.

## Results

All figures were measured on a 32-core Windows machine with GCC 15 at `-O3`.
`docs/RESULTS.md` has the full methodology and every caveat; this section is
the summary.

### Against Chest

Chest 3.19 is the reference implementation: a specialist problem solver by
Heiner Marxen, Holger Pause and Thomas Rakovsky, copyright 1994, with version
3.16 dated 1999. The tables drive the solver itself (`WinChest.exe`) with a
job on stdin, 2048 MB of memory and its endgame databases disabled, which
matches MateProver's own lack of tablebases. MateProver ran at its defaults.

Both engines prove the shortest solution, 10 seconds per position, whole
corpora:

| goal | positions | Chest | MateProver | only Chest | only MateProver |
|---|---:|---:|---:|---:|---:|
| mate-in-8 | 200 | 146 | **167** | 9 | 30 |
| mate-in-10 | 60 | 20 | **52** | 2 | 34 |
| stalemate | 792 | 725 | **761** | 1 | 37 |
| selfmate | 903 | 407 | **643** | 19 | 255 |
| selfstalemate | 76 | 49 | **52** | 1 | 4 |
| helpmate | 546 | 491 | **513** | 2 | 24 |
| helpstalemate | 431 | 308 | **363** | 0 | 55 |

Speed on the positions both engines solved (a ratio that includes a timeout
would be comparing a number against a cap):

| goal | shared positions | total time ratio | median per position |
|---|---:|---:|---:|
| mate-in-8 | 137 | 2.50x | 8.7x |
| mate-in-10 | 18 | 6.09x | 136x |
| stalemate | 724 | 2.44x | 7.2x |
| selfmate | 388 | 3.02x | 3.5x |
| selfstalemate | 48 | 2.34x | 12.8x |
| helpmate | 489 | 1.83x | 3.4x |
| helpstalemate | 308 | 6.41x | 16.3x |

The median describes a typical position, which is usually easy. The total is
dominated by the hardest positions both engines solve and is the more
conservative aggregate. The mate-in-10 median rests on only 18 shared
positions, because Chest solves 20 of the 60.

"Faster than Chest" means faster than a mature 1999 specialist. It is not a
claim about the state of the art. For that, read on.

### Against Matefish

Matefish is a proof-number-search mate solver built on Stockfish, and
architecturally the closest thing to MateProver that exists. It answers "is
there a mate within N" and echoes the bound it was given, so it is comparable
to `--direct-depth` and has no counterpart for the default mode.

60 positions from mate-in-8 to mate-in-16, 10 seconds per engine per position,
single-threaded. Matefish was run with `ProofNumberSearch` on and a 4 GB
proof-number table, deliberately more than it needs. Its claims were verified
by re-proving them, because this family of engines over-claims:

| | MateProver `--direct-depth` | Matefish claimed | Matefish verified |
|---|---:|---:|---:|
| solved of 60 | 45 | 46 | 42 |

Paired, that is five discordant positions, sign test p = 0.375: parity.
MateProver does not beat Matefish at finding mates, and this page does not
claim it does. On the 41 positions both solved, MateProver was faster on 32
(p = 0.0004), median 1.49x, using 256 MB against 4 GB. And on minimality there
is no contest, because Matefish cannot be asked whether a shorter mate exists.
MateProver proves the shortest mate on 41 of the 60.

### Reach

Solve rate on [matetrack](https://github.com/vondele/matetrack) evaluation
sets, a public benchmark rather than one chosen by this project. Wilson 95%
intervals.

| problems | solve rate | 95% CI |
|---|---:|---|
| mate-in-8, defaults, 15 s (200 positions) | 88.5% | 83.3–92.2 |
| mate-in-10, 30 s, 32 threads, `--direct-depth` (60) | 96.7% | 88.6–99.1 |
| mate-in-12, same conditions (40) | 82.5% | 68.0–91.3 |
| mate-in-14, same conditions (40) | 75.0% | 59.8–85.8 |
| mate-in-16, same conditions (40) | 70.0% | 54.6–81.9 |
| mate-in-20, same conditions (40) | 55.0% | 39.8–69.3 |

Read the intervals, not the point estimates. The mate-in-20 set is small
enough that 55% means "roughly half", and the mate-in-10 figure means "nearly
all of a small sample". The mate-in-8 set also serves in the engine
comparisons, so it is a known set rather than a fresh holdout; the other rows
were measured once, on sets minted for the purpose.

MateProver solves mate-in-5 and below essentially always and mate-in-6 and 7
reliably in seconds. Beyond that the solve rate declines gradually rather
than hitting a wall.

Where the reach comes from is worth stating, because it is not speed. At
mate-in-10 the restriction portfolio, which runs several soundly restricted
searches concurrently, is worth 15 positions of 60 with none lost, and neither
four times the time nor four times the memory buys anything more. At mate-in-8
the engine is budget-limited instead: on one 200-position set the solve rate
rose from 80% at 15 s to 96% at 240 s, and extra restriction lanes contribute
nothing. Single-threaded throughput is 0.7 to 1.7 million nodes per second on
hard positions, depending on how much of the tree the preconditioner handles,
and halving the number of nodes needed is worth about one extra position in
forty, the same as doubling the speed.

## Reproducing the numbers

The position sets are not distributed, because they come from a separately
licensed corpus, but they can be rebuilt exactly:

```
python tools/fetch_corpus.py          # downloads the corpus, pinned and checksummed
python tools/mint_eval_set.py ...     # arguments from benchmarks/MANIFEST.json
python tools/reproduce_results.py --engine build/mateprover
```

A set is determined by the corpus revision, depth, count, seed, and the sets
minted before it. All of that is recorded in `benchmarks/MANIFEST.json` along
with a sha256 per set, so a rebuild is verified rather than assumed. Every
figure on this page rests on a set that can be rebuilt and checked against its
digest. `benchmarks/README.md` lists the retired sets that cannot be rebuilt
and why; nothing here depends on them.

`tools/reproduce_results.py --deterministic` runs a different comparison:
sequential, node budgets instead of a clock, development sets. The numbers are
lower and not comparable to the tables above, but they are identical on every
machine and every run.

## Tests

```
ctest --test-dir build --output-on-failure
```

or directly:

```
python tests/run_tests.py --engine build/mateprover
```

**Run the suite with an interpreter that has python-chess installed.** Without
it the suite still reports green (587 passed), but fourteen sections are
skipped, and they are the certificate checks that re-derive the engine's
proofs from scratch. A green run under a bare system Python has verified
nothing this engine exists to claim. The full run is 599 automated checks,
reported as 599 passed, 0 skipped.

They cover:

- perft against published counts for six standard positions, exercising
  castling rights, en passant capture and expiry, promotion including
  under-promotion, and pinned-piece legality;
- known directmates solved at the exact depth with a PV of the right length,
  and negative controls that must produce no mate;
- invariance: the key move and depth must not change with thread count, table
  sharing, the parallel cost gate, or the memory budget, including a budget
  tight enough to force heavy eviction;
- time-limit soundness: a budgeted search must stop on time and must never
  claim a mate it did not prove;
- the CLI contract: bad input is rejected, not silently ignored;
- illegal positions refused: wrong king counts, a side to move that is not
  `w` or `b`, malformed en passant, pawns on the back ranks, and positions
  where the side not to move is in check;
- castling requires its rook, checked against python-chess;
- PV replay and recursive certificate verification;
- the verifier itself, tested adversarially: it must accept genuine
  certificates and reject an omitted defence, a forged mate leaf, a corrupted
  PV and an overstated depth;
- the king-escape analysis, whose flight mask is compared square by square
  against move generation on every corpus position, and whose coverage table
  is cross-checked against a second, independent computation;
- differentials for every exact pruning switch: identical verdicts, moves and
  variations with each one on and off;
- the three variant rules against all six goals: the allowance round-trips
  through the FEN in both spellings, the win is found at every depth, a move
  that is both mate and the final check is certified as mate, the allowance is
  part of the transposition key, and a forged check-win certificate is
  rejected three ways;
- the sub-root parallel splits, which may change how long an answer takes and
  nothing else: verdict, line and certificate must be byte-identical with each
  split on and off;
- candidate pruning theorems, which are measured and never acted on: a
  candidate must leave the answer untouched, and a false one must be caught by
  counterexample;
- the measurement harness that produces every published number: strict
  result parsing, self-describing records, measurement identity, and the
  load-time invariants that make a shared resume file impossible.

The core tests have no third-party dependency.

## Tools

Three tools search for improvements automatically. All of them score on node
counts at one thread, never on wall clock: the measurement machine drifts
about 15% between identical runs while the effects worth finding are 3 to
10%, and node counts are exactly reproducible.

| tool | what it does |
|---|---|
| `tools/autotune.py tune` | coordinate descent over configuration knobs |
| `tools/autotune.py ordering` | genetic search over the five move-ordering weights (`--order-weights`) |
| `tools/adversarial.py hunt` | evolves positions that maximise the node count |
| `tools/adversarial.py fuzz` | verdicts against an independent brute-force oracle |
| `tools/adversarial.py perft` | move generation against python-chess |
| `tools/predicates.py` | candidate pruning theorems, by refuting the false ones |
| `tools/finder_lane.py` | asks an external engine for a mate and verifies the claim with `--direct-depth` |

Two design decisions matter here.

Every gate is lexicographic in correctness. Nothing is compared on speed until
every baseline verdict and depth is unchanged. A scalar objective would
happily accept a configuration that reports `dm 7` where the answer is `dm 5`,
and a score cannot see the difference.

Candidates that prune are measured, never obeyed. `--predicate` evaluates a
candidate theorem at every attacker node and then searches as though it had
said nothing, so a candidate that fired where a mate existed is refuted by
counterexample. Falsification is automatic. Promotion to a live prune is not:
it needs a proof written by a person, because soundness cannot be established
by testing. The candidate the search liked best appeared to save 48% of all
nodes and loses mates 4,034 times over ten mate-in-8 positions.

Neither tuner has produced a shipped improvement. Both produced findings about
measurement instead, recorded in sections 118 and 119 of the architecture
document.

The finder lane is sound (a false claim fails verification and costs only the
check) and tested against proposers that lie, that report a negative mate
score, and that find nothing. It does not improve coverage. Measured against a
`--direct-depth` baseline at full budget, with Matefish on 4 GB as the
proposer, it adds zero positions. If you want a mate found and certified, ask
`--direct-depth` directly.

## Correctness

Beyond the test suite:

- move generation matches python-chess on 294 benchmark positions with zero
  mismatches, and matches published perft counts on six standard positions;
- eviction from the proof table can never change an answer. The table is a
  memo of verdicts that are pure functions of an exact key, so a missing entry
  costs time and nothing else;
- parallel search returns the lowest-index successful root move, so the key
  move does not depend on thread scheduling;
- an aborted search, whether cancelled, out of budget or out of time, records
  no verdict, so it can never be mistaken for a disproof.

Perft is a permanent gate because of one bug. Castling rights were being
revoked by captured piece type, so capturing a promoted rook stripped rights
while the original rooks stood, removing a legal defender escape and opening
the door to a false mate. Roughly four thousand directmate positions in the
project's own suites never caught it. Six perft positions caught it
immediately.

## Limitations

- **The figures on this page are directmate figures** unless a table says
  otherwise. All six goals ship and all six are measured, but directmate is
  the most tuned by a wide margin. The cooperative goals (`--helpmate`,
  `--helpstalemate`) are a different search entirely: both sides are OR nodes,
  so there is no defender, no proof-number preconditioner, no restriction
  portfolio and no adversarial root split.
- **Stalemate is at its ceiling.** On 579 composed problems the engine solves
  94.7%, essentially everything through `=9`, then 28.6% at `=12` and 9.1% at
  `=16`. Every restriction lane and search setting was measured against the
  60 problems at depth 10 to 16, and none beats the shipped default. The
  portfolio that is worth 15 positions of 60 at mate-in-10 contributes nothing
  here.
- **Depth changes what helps.** At mate-in-8 the engine is budget-limited, so
  time buys reach. At mate-in-10 it is not: time and memory buy nothing and
  only the restriction portfolio helps. A result about one depth does not
  describe the other.
- **Minimality is expensive by nature.** Proving the shortest mate means
  proving absence at every shorter distance, which is about 99.3% of the work,
  so knowing the answer in advance saves almost none of it. Past 41 plies
  there is nothing to find at all: the engine scores 0 of 20 with
  `--direct-depth` as well as with the default.
- **`-M` is an entry-count ceiling**, computed from an estimated size per
  entry, not a hard cap on process memory, and it excludes fixed overhead.
  Small budgets overshoot most: a 64 MB request peaks near 91 MB resident,
  while 512 MB and 2 GB requests stay under. Size it with headroom.
- **WinChest's special-mate restrictions** `-C` (checks only, all five bits),
  `-R` (threat depth, both signs), `-K` (defender king squares), `-P`
  (defender pieces able to move) and `-X` (defender moves in total) are
  implemented and validated against the WinChest binary: 666 comparisons, 0
  disagreements. `-I` (threat flags) has no entry in the manual and is
  rejected rather than ignored, because a constrained request must never
  return an unconstrained answer. Chest 3.19 itself has none of these
  options; they are WinChest extensions.
- **Parallel speed-up depends on the position.** On a depth-7 capture-quota
  search the root split reaches 9.0x on 24 threads. Most of that comes from
  two things outside the search: standing the proof-number preconditioner
  down under a variant rule, and freeing the shared transposition table
  concurrently, which at a 4 GB budget is otherwise 44% of the run spent
  handing memory back after the answer is known. What remains is Amdahl's
  law: the parallel region is over 95% busy and the ceiling is the largest
  single subtree. Sections 109 to 115 of the architecture document have the
  measurements.
- **Speed and reach are different problems.** A doubling of speed is worth
  roughly one extra position in forty, so a 9x parallel speed-up is worth
  about three mate-in-8 positions in forty, not a depth notch. Reach comes
  from restriction and from search shape, and the project has measured
  repeatedly that it does not follow from throughput.

## Background

MateProver is a from-scratch implementation. It was developed alongside
other mate solvers, which were run as behavioural oracles and benchmark
references. That is how the restriction semantics were established and how
the comparative figures above were produced. No code from any of them appears
here. Every line is original, and the option semantics were derived by
observing what those programs do rather than by reading how they do it.

`docs/ARCHITECTURE.md` records the design and, in more detail than usual, the
optimisations that were measured and rejected, with the reasons.
[docs/RESULTS.md](docs/RESULTS.md) is the shorter document: where the
capability comes from, what was tried, and how far to trust the numbers.
[CHANGELOG.md](CHANGELOG.md) records what each version is and what it
measured.

## Licence

MateProver is released under the MIT License. See `LICENSE` for the full
text.

`tools/verify_proof.py` requires python-chess, which is not distributed with
MateProver. Benchmark position sets are not distributed either;
`benchmarks/README.md` explains how to regenerate them.
