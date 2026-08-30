# MateProver

An exact chess problem prover that emits **machine-checkable proof certificates**.

Given a position and a depth N, mateprover either proves the stipulation is
forced in N and emits a proof an independent checker can verify, or reports that
it found none. It does not estimate, search heuristically, or return a score —
every reported solution is a complete AND/OR proof in which every legal defence
is refuted.

**Six stipulations**, each a different problem rather than an easier or harder
one, and each with its own result token: directmate (`dm`), stalemate (`sm`),
selfmate (`sfm`), selfstalemate (`ssm`), helpmate (`hm`) and helpstalemate
(`hsm`). A checkmate *fails* a stalemate goal.

**Three variant rules**, orthogonal to all six goals and to each other:

| rule | flag | a side… |
|---|---|---|
| x-check | `--checks N` \| `W:B` | **wins** on giving its Nth check |
| x-capture | `--captures N` \| `W:B` | **wins** on making its Nth capture |
| x-escape | `--escape N` \| `W:B` | **loses** when its own king reaches escape count N |

Per-side quotas, so 5+2 is as ordinary as 3+3, and they compose — `--checks 3
--captures 5` is a legal game, and 3-check selfmate is a job you can run. Under
a mate goal a filled quota is the win being forced; under the other goals the
game ended without reaching the terminal the stipulation names.

x-escape is the odd one and worth reading twice. **E** is how many squares a king
could legally step to, computed with that king *removed from the board*, so it
measures the squares rather than the king's own blocking. It is not a countdown:
it is recomputed at every position, can rise and fall, and **reaching the limit
loses**, so the winner is the other side. The starting array is E 0 for both
sides. Range 1..8, since a king's ring holds at most eight squares.
`--escape-count` reports E for both kings and stops, without searching.

Six goals times three rules, each rule per-side and composable, is the whole
matrix — there is no goal that a rule does not apply to, and no pair of rules
that cannot be run together.

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

Solve rate on evaluation sets that were minted before the work they judge.
Wilson 95% intervals.

**Read the mate-in-8 row with its history.** It was measured once, at 78.0%,
against a set never consulted during the work it judged -- that was clean
holdout evidence. The set has since been used in engine-to-engine comparison
sweeps, and re-measuring it today gives 88.5%. The improvement is real and the
set was never TUNED against, but it is no longer virgin, so 88.5% is current
performance on a known set rather than a fresh holdout result. The distinction
costs nothing to state and is the whole difference between evidence and a
number.

| problems | solve rate | 95% CI |
|---|---:|---|
| mate-in-8, default configuration, 15 s (200 positions) | **88.5%** | 83.3–92.2 |
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

Build through CMake rather than invoking the compiler directly. **Do not add
`-march=native` to a hand-rolled MinGW build**: GCC then copies the board struct
through `ymm` registers and spills it with a 32-byte aligned store into a stack
frame it never realigned, and the binary faults inside move generation — perft
dies before it reaches depth 3. The build file disables AVX on MinGW to prevent
exactly this, so the supported invocation above is safe even with `-march=native`
in `CMAKE_CXX_FLAGS`. Nothing is lost by the restriction: the engine has no
hand-vectorised kernel, and AVX is worth 0.3% on perft and less than measurement
noise on search. `docs/ARCHITECTURE.md` section 79 has the disassembly.

Built and tested in CI on Linux/GCC, Linux/Clang, macOS/Clang, Windows/MSVC and
Windows/MinGW-w64. Every one of those is built a second time with
architecture-specific flags (`-march=native`, `-mcpu=native` on arm64,
`/arch:AVX2` on MSVC) and the full suite is run again against each, because the
fault in section 79 was a build-level defect that every binary-level gate passed.
CI additionally runs a bounds-checked build, a C++20/23 forward-compatibility
check, cppcheck, and `-Wall -Wextra -pedantic -Werror`.

## Test

```
ctest --test-dir build --output-on-failure
```

or directly:

```
python tests/run_tests.py --engine build/mateprover
```

587 automated checks covering:

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
  PV and an overstated depth;
- **the king-escape analysis**, whose flight mask is compared square by square
  against move generation on every position in every corpus, and whose coverage
  table is cross-checked against a second, independent computation of the same
  256 answers — a table checked only against itself is not checked;
- **differentials for every exact pruning switch**: the same verdicts, moves and
  variations with each one on and off, run single-threaded so the comparison can
  be on the whole line rather than the depth alone;
- **the three variant rules** -- x-check, x-capture and x-escape -- orthogonal to
  all six goals: the allowance round-trips through the FEN in both accepted
  spellings, the win is found at every depth, a move that is both mate and the
  final check is certified as mate, the allowance is part of the transposition
  key, and a forged check-win certificate is rejected three ways;
- **the sub-root parallel splits**, which are allowed to change how long an
  answer takes and nothing else: the verdict, the reported line and the whole
  certificate must be byte-identical with each split on and off;
- **candidate pruning theorems**, which are measured and never acted on: any
  candidate must leave the answer untouched, and a candidate that is FALSE must
  be caught by counterexample rather than quietly reported as a survivor;
- **the measurement harness**, which produces every published number and is
  checked like the engine: strict result parsing, self-describing records,
  measurement identity, and the load-time invariants that make a shared resume
  file impossible rather than merely unlikely.

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
| `--threads N` \| `auto` | parallel search; `auto` = min(cores, 16). That cap dates from when the split saturated at 4.75x; it now reaches **9.0x at 24 threads**, so an explicit `--threads N` above 16 is worth trying on a big machine |
| `--time-limit S` | wall-clock budget; expiry reports `timeout`, never a mate |
| `--direct-depth` | prove "a mate within N" rather than the shortest mate; materially better solve rate at a fixed budget |
| `--portfolio` | spend the time budget across restricted searches as well as the unrestricted one; sound, since a restriction only removes attacker options |
| `--portfolio-parallel` | run those searches concurrently, each with the full budget; uses cores that root splitting saturates on |
| `--escape-count` | report E for both kings and stop, without searching |
| `--emit-proof` | append the recursive JSON proof certificate |
| `--perft N` / `--perft-divide N` | move-generation self-check |
| `--profile` | per-position counters on stderr |

`--help` lists the full set, including search-tuning flags and their rollback
counterparts.

## UCI

```
mateprover --uci
```

A **lossy convenience interface**, and worth understanding before relying on it.

`go mate <x>` is in the UCI specification — *"search for a mate in x moves"* —
and it is exactly this engine's question, so that half maps cleanly:

```
position fen 8/2Q5/R7/8/1k4K1/8/8/8 w - -
go mate 2
info depth 2 nodes 99 time 2 nps 35855 score mate 2 pv a6b6 b4a3 c7a7
bestmove a6b6
```

What UCI cannot express is **"no solution exists"**. A GUI that sees no
`score mate` cannot tell an exhaustive disproof from a search that ran out of
budget, and conflating those two is the one thing `docs/OUTPUT_FORMAT.md` exists
to prevent. So they are separated on `info string`, which a human can read and a
GUI cannot act on:

```
info string PROVED: no solution exists within the depth searched
info string no verdict: the search hit its budget or was stopped
bestmove 0000
```

`bestmove 0000` is the null move: there is no move to recommend, because the
engine was never asked to recommend one, and naming a legal move there would
assert something it never proved.

Certificates cannot travel — a mate-in-8 proof tree runs to megabytes — so
`--emit-proof` is **refused** in this mode rather than silently doing nothing.
The other five goals and the three variant rules are reachable through
`setoption` (`Goal`, `Checks`, `Captures`, `Escape`) but no GUI knows to set
them, so in practice this is a directmate interface.

**The UCI answer is a rendering, never a second search.** It drives the same
`solve_line` the EPD interface does — same portfolio, same routes, same gates —
and reformats the result line, so it cannot drift from the engine's real
behaviour. The suite checks that the mate depth and key move agree with the EPD
answer on the same position.

**The EPD line and the certificate remain the record.** UCI is for plugging the
directmate mode into a GUI or a standard harness; it is not the interface any
claim on this page rests on.

## Searching for improvements automatically

Three tools search for changes rather than waiting to be told about them. All
three score on **node counts at one thread**, never on wall clock: this machine
drifts 15% between identical runs while the effects worth having are 3-10%, and
node counts are exactly reproducible, so one repetition is enough.

| tool | what it searches |
|---|---|
| `tools/autotune.py tune` | coordinate descent over configuration knobs |
| `tools/autotune.py ordering` | a genetic search over the five move-ordering weights (`--order-weights`) |
| `tools/adversarial.py hunt` | evolves POSITIONS that maximise the node count |
| `tools/adversarial.py fuzz` | verdicts against an independent brute-force oracle |
| `tools/adversarial.py perft` | move generation against python-chess |
| `tools/predicates.py` | candidate pruning theorems, by refuting the false ones |

Two design points carry all the weight.

**Every gate is lexicographic in correctness.** Nothing is compared on speed
until every baseline verdict and depth is unchanged. A scalar objective would
happily accept a configuration reporting `dm 7` where the answer is `dm 5`, and
that difference is invisible in a score.

**Candidates that prune are measured, never obeyed.** `--predicate` evaluates a
candidate theorem at every attacker node and then searches as though it had said
nothing, so when the node returns its true verdict is known and a candidate that
fired where a mate existed is refuted by counterexample. Falsification is exact
and automatic; **promotion to a live prune is not, and needs a proof written by a
person.** Soundness cannot be established by testing, and the one candidate this
search liked best appeared to save 48% of all nodes and loses mates 4,034 times
over ten mate-in-8 positions.

Neither tuner has produced a shipped improvement. Both have produced findings
about the process — a fitness function that silently carried no signal because
every position hit its budget and scored the cap, and a tuning result that won on
its held-out set and reversed on a third. Architecture 118 and 119 record both.

## Certifying what it cannot prove

The engine proves the *shortest* mate, and it is a weak finder: over 180
positions it solves 92 where two Stockfish forks solve 131 and 140. Those forks
find quickly but routinely report a **longer** mate than the shortest, so what
they produce is a claim, not a result.

`tools/finder_lane.py` puts the two failures together. Where the engine cannot
finish within its budget, an external finder proposes a mate and the engine
verifies it with `--direct-depth`, which asks only whether a mate exists within
N and so costs a fraction of proving the shortest. Measured over 60 positions at
depths 12-18, 20M nodes to prove and 4M to verify:

| | positions |
|---|---|
| engine alone | 7/60 |
| with the finder lane | **31/60** |
| claims made | 41 |
| claims verified | 24 |
| **claims rejected** | **17** |

The seventeen rejections are the mechanism working, not a defect. The lane is
**sound by construction**: an unreliable proposer cannot produce a wrong answer,
only a wasted check. Nothing is taken on trust — every position reported carries
a certificate, and `tools/verify_proof.py` re-derives all of them from scratch.

**It replicates**, which five other positives in this project did not. On an
independent sample of 40 positions the finder claimed on 77% of what the engine
missed, against 77% before, and 53% of those verified, against 59% before.

The verification ceiling is measured, not guessed. Cost is bimodal — median
**90K nodes**, maximum 20.4M — so the ceiling is irrelevant to a typical claim
and only ever buys the tail. A ceiling is spent only on *failures*, so raising
it is paid for by every rejected claim:

| ceiling | claims verified | nodes wasted on failures |
|---|---|---|
| **1M** | **13 of 30** | **17M** |
| 4M | 13 of 30 | 68M |
| 32M | 16 of 30 | 448M |

1M is therefore the default: identical yield to the 4M used above, at a quarter
of the waste.

### Where it stops

The lane is not confined to the band it was tuned on, but it decays sharply.
Measured across six deeper bands:

| band | n | engine alone | with lane | claims | verified |
|---|---|---|---|---|---|
| d20 | 10 | 2 | 2 | 3 | 0 |
| d24 | 10 | 6 | 6 | 1 | 0 |
| d28 | 8 | 0 | **2** | 3 | 2 |
| d32 | 10 | 1 | 1 | 2 | 0 |
| d36 | 5 | 0 | **1** | 3 | 1 |
| d40 | 4 | 0 | 0 | 0 | 0 |
| **total** | **47** | **9** | **12** | **12** | **3** |

Three extra positions across 47, against twenty-four across sixty at d12-18.
Those three are a hard count rather than an estimate — each carries a
certificate — but the effect is roughly a tenth of the shallow one.

**What fails is the proposer, not the prover.** The finder produces a candidate
on 77% of missed positions at d12-18 and on only 32% here; of the claims it does
make, 25% verify against 53-59% shallow. So the lane runs out because there is
nothing to check, not because checking stops working — which is the opposite of
what closes d41+, and it means a stronger proposer is the thing that would
extend it.

Read the deepest bands with care: the corpus thins with depth, so d36 is five
positions and d40 is four. The d40 zero is close to no evidence at all.

```
python tools/finder_lane.py --finder ./hunt18 positions.epd > proofs.epd
python tools/verify_proof.py --require-proof proofs.epd
```

**A verified find is not a minimality proof, and the two are never merged.**
Stage one proves "the shortest mate is N". Stage three proves only "a mate
exists within N". Both are certified and neither is a guess, but they are
different claims, and the `lane` opcode on every output line says which one you
have.

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

- **The finder lane extends finding, not minimality, and only in a band.**
  Proving the shortest mate means proving absence at every distance below it,
  which is 99.3% of the work — so knowing the answer in advance saves none of
  it, and `tools/finder_lane.py` leaves minimality coverage exactly where it
  was. The gain is largest at depths 12-18 (+24 of 60) and decays to roughly a
  tenth of that by d20-40 (+3 of 47), where the proposer stops producing
  candidates rather than the prover failing to check them. Past 41 plies there
  is nothing left: the engine scores 0/20 with `--direct-depth` as well as with
  `--iterative-depth`, so verification has nothing to confirm.
- **The goals differ in maturity, not in availability.** All six ship and all
  six are measured against the reference implementation in `docs/RESULTS.md`.
  Directmate is the most tuned by a wide margin; the cooperative goals
  (`--helpmate`, `--helpstalemate`) are a different search entirely — both sides
  are OR nodes, so there is no defender, no proof-number preconditioner, no
  restriction portfolio and no adversarial root split, none of which are defined
  against a partner. The figures elsewhere on this page are **directmate**
  figures and do not describe the others.
- **The selfmate goal is newer than the directmate one.** It has its own
  preconditioner (worth +124 positions of 200 on hard problems), its own
  verified certificates, and a restriction portfolio that measurably helps
  (+17 of 200). Measured reach at s#5–s#10 is 35.3% before the preconditioner;
  the tuned figure has not been re-measured across the full corpus.
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
- **Parallelism helps on one position far more than it used to, and the old
  claim on this page was wrong.** It read "root splitting nothing at all", which
  was measured against a route the split was never wired into. Corrected and
  re-measured: on a depth-7 capture-quota search the split reaches **9.0x on 24
  threads**. Most of that came from two fixes rather than from the search --
  standing the proof-number preconditioner down under a variant rule, which had
  been consuming 96% of the wall clock single-threaded, and freeing the shared
  transposition table concurrently, which at a 4 GB budget was **44% of the run**
  spent handing memory back after the answer was already known. What remains is
  Amdahl: the parallel region is 95%+ busy, and the ceiling is the largest single
  subtree. Architecture 109-115 has the whole sequence, including the two
  conclusions it had to retract.
- **Converting speed into reach is still the hard part.** A doubling of speed is
  worth roughly one extra position in forty, so the 9.0x above is worth about
  three depth-8 positions in forty and not a depth notch. Speed and reach are
  different problems and this project has repeatedly measured that the second
  does not follow from the first.

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
