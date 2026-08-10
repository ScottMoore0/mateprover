# Changelog

Versions follow [semantic versioning](https://semver.org). The version lives in
`src/mateprover.cpp`; `CMakeLists.txt` parses it from there so the two cannot drift.

The two external contracts carry their own version numbers, documented in
`docs/OUTPUT_FORMAT.md` and `docs/PROOF_FORMAT.md`. Fields may be *added* to
either without a major bump; the meaning of an existing field will not change
without one.

## Unreleased

**The exact proof table's soundness is now testable, and tested.** The table is
keyed by position with no depth in the key, which is sound only because a
disproof bounds every smaller depth and a proof depth is minimal. Nothing in the
suite could detect a violation of either -- it would surface as a non-minimal
mate on some untested position. `--no-exact-tt` exists for that one check:
running four corpora with the table on and off and comparing reported DEPTHS
gives 275 positions solved both ways and **zero mismatches**. Both preconditions
hold on every goal. The same run prices the table at 35 solved against 18 on
directmate.

**The 28 remaining Chest-only selfmates are two classes, and mostly not
disproof-bound.** Fourteen miniatures of 6-8 men with a single-unit mating force,
fourteen heavy positions of 13-25 men. Given `--direct-depth`, which skips the
shallow root disproofs entirely, coverage goes from 2 of 28 to 5 -- so the whole
graded-failure-depth line has a ceiling of about three positions here. Section 81
was right that the cost concentrates in disproof; it does not follow that the
residue is unlocked by cheapening it.

**Answer ordering has no headroom left.** The depth-2 additive scorer was ported
with re-derived constants -- 1.73x fewer nodes on a hard disproof -- and ties the
width estimator on the corpus: 205 against 205, with ordering off at 202 and zero
depth disagreements between any pair. Any ordering is worth three positions;
sophistication is worth none of it. Kept behind `--depth2-scorer`, default off.
That closes the specification's check refinement, which was explicitly
conditional on this port leaving room, and its sole-attacker tracking, which
would need incremental per-square attack sets this board does not have. See
`docs/ARCHITECTURE.md` section 84.

**Selfmate against Chest 3.19, re-measured over all 903 positions at 10 s each:
626 to 416.** 238 positions only MateProver solves, 28 only Chest; on the 388
both solve, 2.24x in total time and a 6.14x median per position. Section 81's
389-against-318 came from a run at a different budget and is not comparable --
Chest itself scores 318 there and 416 here. The Chest-only residue is now 28
against a much larger denominator, and that is the baseline for what comes next.
Chest also returned a definitive "no solution" on 15 positions, which is evidence
about the corpus rather than either engine.

**The answer-ordering band was swept rather than assumed.** `--answer-order-min-depth`
defaults to 2; on 200 selfmates 2 solves 166, 3 and 4 solve 164, off solves 163.
Most of the heuristic's value is at remaining depth exactly two.

**Two follow-ups were measured out before being built.** A per-move disproof
array can be consulted at only 6.24% of attacker expansions, because MateProver
deepens at the root rather than per node, so its table usually settles a position
outright instead of re-entering it at a greater depth -- it is the second half of
an architecture this engine does not have, and internal iterative deepening is
the prerequisite, not the array. And replacing the estimator's static weights
with occupancy-aware mobility made it worse (11.1M nodes to 14.0M), because the
larger totals swamp the king-escape term that carries the signal and the
"improvement" approximated captures the old version had exactly. See
`docs/ARCHITECTURE.md` section 83.

**The selfmate defender now orders its replies, and scans them lazily.** Two
independent changes to the same node, together worth +3 positions on 250
selfmates at a 4 s budget (198 to 201) and 4.6x on a hard depth-5 disproof.

The lazy scan stops building a fully legality-filtered reply list it then
consumed one entry of: 809 million legality tests become 60.3 million, worth
1.35x. The ordering heuristic estimates how much room each reply leaves the
attacker and tries the narrowest first, taking a depth-5 disproof from 52.3M
nodes and 14.55 s to 11.1M and 3.18 s while returning a byte-identical best move
and principal variation. Neither can change a verdict; `--no-answer-order` is the
differential test and is retained for it.

Also fixes two leaves of the selfmate recursion that reported a depth-limited
failure where the failure was in fact unbounded -- an axiomatic refutation, and
an attacker with no legal move in a position that is not the goal. Both are
any-depth results and both reported zero.

The disproof-depth histogram added here explains section 78's zero conclusively:
99.98% of defender disproofs prove exactly what was asked, so level skipping
never had a ply to skip. It still does not fire, and section 82 now gives the
structural reason -- an OR node's failure depth is the minimum over its moves, so
over-proof requires every one of ~41 moves to over-prove. The per-move disproof
array is therefore not an addition to level skipping but the only workable
consumer of graded failure depth. See `docs/ARCHITECTURE.md` section 82.

**The remaining selfmate gap is a disproof gap, not a search-quality gap.** A
differential investigation against Chest, run as a black box, locates all 15
remaining misses precisely: MateProver's search for a *solution* is within 3x of
Chest and ahead on coverage (389 solved against 318, with 86 Chest cannot do at
all), while its proof of *absence* is one to three orders of magnitude slower.
Asked the strictly easier question one ply short of each real solution, so that
both engines are disproving, Chest settles 14 of 15 in 5.75 s total and
MateProver needs 401.70 s and times out on 12. Median ratio 90x, peak 2600x.
Every one of the 15 is a position whose solution sits at depth N and whose depth
N-1 disproof MateProver cannot finish. Two hypotheses were tested and rejected:
the reach bound of sections 75 and 77 fires zero times here, and three positions
cannot establish whether the gap is a widening ratio or a large constant one. No
code change; this redirects the work. See `docs/ARCHITECTURE.md` section 81.

**A sixteenth wrong corpus entry**, found in passing and the first of its kind:
`1R2nkb1/p3p1R1/4Q2B/p5P1/Bp6/1Kp1PP2/2P5/8 w - -` is stipulated at selfmate in
7 and is a selfmate in 6. Both provers agree and MateProver proved minimality by
exhausting depths 1 to 5, so it is recorded as a wrong stipulation rather than a
missing solution.

**Material knowledge is rejected, and the attractive form of it is unsound.**
Section 78 named material knowledge as the prerequisite for level skipping, so
it was measured before being built. Its main lever — a minimum-depth table for a
bare defender king against king-plus-one — applies to 2 positions out of 2093.
Dropping the bare-king requirement and testing the mating side's material alone
would fire on 96, but it is wrong: `6rk/5Npp/8/8/8/8/8/3K4 b - -` is checkmate
with White holding only king and knight, and a smothered mate needs the mated
side to have men to be entombed by. Shipping it would have produced 33 false
refutations, all selfmates, where the attacker is piece-laden and *wants* to be
mated. Exhaustive checks confirm the spec's underlying claims — no stalemate
exists between two bare kings (7,224 positions), and neither K+B nor K+N mates a
bare king (417,228 and 429,440) — but the sound theorem's applicability across
the corpora is zero. Nothing implemented; see `docs/ARCHITECTURE.md` section 80.

This closes the level-skipping line from section 78 rather than deferring it:
the prerequisite exists, was measured, and cannot fire here.

**Fixed: the engine faulted in move generation when built with AVX enabled.**
`g++ -O3 -march=native` produced a binary that segfaulted before perft reached
depth 3. GCC copies the board struct through `ymm` registers and spills it with
`vmovdqa` into a stack slot it assumed was 32-byte aligned, without emitting the
prologue that would make that true; Windows guarantees 16, so the store faulted
whenever the frame landed on the wrong half. The engine requests nothing unusual
— `Board` is a plain aggregate of alignment 8, and there is no `alignas` in the
source — so this is a MinGW-w64 GCC 15.2.0 codegen defect rather than a bug here.
`-mstackrealign`, `-mpreferred-stack-boundary=5` and `-fno-tree-vectorize` all
fail to avoid it; only disabling AVX does. The build file now does that on MinGW,
appended so it wins over a user's `-march=native`, with `MATEPROVER_ALLOW_AVX=ON`
to override. It costs nothing measurable: AVX is worth 0.3% on perft and less
than run-to-run noise on search, because the engine has no hand-vectorised kernel.
Release builds were never affected — the shipped CMake configuration adds no
`-march` — so this was a footgun for people building from source, not a defect in
any published binary. See `docs/ARCHITECTURE.md` section 79.

The same section records the gap that let it through: every existing gate tests a
*binary*, never a *build*, so no amount of self-testing could have caught it. The
CI matrix wants a native-flags entry per platform.

`mate_out_of_reach` no longer takes a `const Search&` it never read, restoring a
warning-free build under `-Wall -Wextra -pedantic`.

**Level skipping and the graded failure depth are rejected on measurement, and
the reason is that they cannot be measured alone.** Both were implemented in
full: a failed search reporting the largest depth it actually disproved, that
bound propagating through both node pairs and surviving in the proof table, and
the three iterative-deepening loops advancing past it rather than by one. The
counter says how often it fired across 420 positions, four corpora, both goal
families and 555 million nodes: **zero times.** Level skipping consumes
over-proof, and MateProver never over-proves — every disproof it makes comes from
exhausting a move tree at one depth, which says nothing about the next. The
mechanisms that would supply over-proof (material knowledge that holds for all
depths at once; an anti-mate test failing at the sentinel) are not present, so
the feature's value belongs to them and not to itself. Reverted, with the
condition for revisiting stated: build the material knowledge first. See
`docs/ARCHITECTURE.md` section 78.

The same work found that **`prove_selfmate_defender` had no transposition table**
while its directmate counterpart has always had one. Adding it cut a hard `sfm 6`
by 12% of nodes and still lost two positions across 400 selfmates at a 5 s cap,
flat across four depth bands — the third mechanism in this project that is sound,
fires constantly, and converts nothing. Also reverted, also documented.

**The cooperative split now runs two plies deep.** Splitting on the root move
alone made as many parallel tasks as there were root moves — around thirty — and
cooperative subtrees are wildly uneven, so one task held most of the work and the
other threads idled. Sixteen threads were SLOWER than four. Pairing each root move
with each reply gives hundreds of tasks instead of tens: 20.1 s to 6.2 s on a hard
h#4 at the default thread count, which is 6.0x against a single thread and matches
the scaling the same search already achieved on workloads with no early exit.

It also brings the answer closer to the sequential one, since lexicographic
(first, second) order is the order a sequential depth-first search visits those
subtrees in.

**Meet-in-the-middle for the cooperative goals is rejected**, on measurement
rather than on taste. A backward frontier has to start from an explicit goal set,
and checkmate is a predicate rather than a state, so the mate positions must be
enumerated first: 1.3x10^10 placements at six men against a forward search of
2.3x10^7 states, and roughly 10^13 at the corpus median for h#4. Where the goal
set is cheap to enumerate the forward search is already instant, and where the
forward search needs help the goal set cannot be enumerated. `docs/ARCHITECTURE.md`
§66 has the numbers. The retrograde generator added below keeps its value on its
own terms; it has no bidirectional search to carry.


**Retrograde move generation** (`--list-unmoves`). Given a position, lists every
position from which one legal move reaches it. This is groundwork for a
bidirectional cooperative search, and is shipped separately from it because the
generator has to be measured before anything rests on it.

The guarantee is exactly one ply and it holds in both directions: every emitted
predecessor is a legal position with a legal move to the target, and every such
predecessor is emitted. Both are gated by tests against an independent adjudicator
(`python-chess`) rather than by the engine agreeing with itself. It does not
decide whether a predecessor is reachable from the initial array — 0.7% of output
is legal one ply back and impossible overall — which `docs/ARCHITECTURE.md` §65
states as the contract rather than leaving to be discovered.

The first version of this passed a 96% completeness round-trip while emitting
predecessors that a knight had reached by sliding down a file: it replayed the
retracted move without first checking the move was legal, and a completeness
test is structurally blind to a spurious predecessor. Both properties are now
measured. Castling, castling-rights forfeiture, en-passant capture, and
en-passant squares that no pawn can use are handled; the last of these is a
disagreement about what a position is, and is resolved in favour of the FEN
convention the corpora use.

## 1.0.0 — 2026-08-02

Published as **MateProver**. The project was developed under the working name
"E Chest" because it began as a reimplementation line measured against Heiner
Marxen's Chest, which it is still benchmarked against below. That name was never
suitable for release: it borrows the identity of a separate program that this
one publishes head-to-head results against. `mateprover` names what the tool is
— a prover, not a solver — which is the distinction the certificates make real.

One wire-format token moved with the name: the `--profile` diagnostic line is
prefixed `% mateprover_profile` rather than `% e_profile`. Nothing had been
released, so no consumer existed to break; the position, PV, `dm`/`acn`/`acs`
and certificate formats are untouched.

First complete version. Exact directmate prover: given a position and a depth,
it either proves a forced mate and emits a machine-checkable certificate, or
reports that no mate exists, or reports that it ran out of budget — three
outcomes it never conflates.

**Capability.** Measured on evaluation positions used once and never consulted
during development:

- mate-in-8, default configuration: **78.0%** at 15 s (200 positions). Budget
  scaling, measured under the previous default route: 80.0% at 15 s, 90.5% at
  60 s, 96.0% at 240 s.
- mate-in-10, 30 s, 32 threads, `--direct-depth`: **90.0%** (60 positions), and
  the decline with depth is gradual rather than a wall: 82.5% at mate-in-12,
  75.0% at mate-in-14, 70.0% at mate-in-16, 57.5% at mate-in-20.
- Against Chest 3.19 on the same machine, positions, memory and time cap, both
  single-threaded: 40/40 against 39/40 at mate-in-8 and about four times faster,
  37/40 against 17/40 at mate-in-10, 33/40 against 8/40 at mate-in-12.

`docs/RESULTS.md` explains where that capability comes from and what was measured
and rejected; `tools/reproduce_results.py` re-runs the figures.

**Correctness.** Every proof is a certificate verifiable by a separate program
sharing no code with the engine. 414 automated checks cover perft against
reference counts, negative controls, restriction soundness, the abort invariant
under stress, order and batching independence, the CLI contract, and six ways of
forging a certificate.

**Interfaces.** Output format and proof format are specified. Defaults are the
measured-best configuration, so a bare invocation performs like a tuned one.
`--print-config` reports the effective settings for reproducibility. An explicit
`-M` is the budget for every table alive at once, split across portfolio lanes
and `--parallel-positions` workers; it previously applied per table, so a stated
256 MB cost 1994 MB at four workers. Left unset it stays per table at the tuned
256 MB, so raising `--parallel-positions` cannot shrink it. Input
tolerates a leading UTF-8 BOM: Notepad and PowerShell's `Set-Content -Encoding
utf8` both emit one, and without this the first position of such a file failed
as `error input` while the rest of the file succeeded. `tools/verify_proof.py`
reads certificates as `utf-8-sig` for the same reason.

**Also included.** A DFPN preconditioner behind `--route dfpn`. It was long
recorded here as rejected for being slower at every depth; that was measuring a
defect, not the algorithm -- its transposition key omitted the remaining depth, so
it burned ten million nodes on a mate-in-2. Repaired -- and with two further
fixes, preconditioning only the deepest iteration and dropping per-child work
that computed a value already known -- it is now the **default route**. On freshly
minted positions it solved 90.0% of mate-in-10 against the previous default's
61.7%, gaining seventeen and losing none, and 85.5% against 83.5% at mate-in-8.
(Those are the figures from the evaluation sets current at the time. The headline
reach numbers above were later re-measured on re-minted sets; this route
comparison is left as it was measured, on one set, which is what makes it a
comparison.)

`--node-limit N` gives a deterministic budget. Wall-clock limits made every
comparison noisy at the scale of the effects being measured; a node cap gives the
same answer on every run and machine.

**Not included.** Endgame tablebases (measured: they would reach 1% of proof
nodes, and near the leaves where the subtree beneath is already almost free) and
a bitboard rewrite (measured: no concentrated hotspot to justify it). Each is
recorded with its numbers in `docs/ARCHITECTURE.md` rather than left as
an implied roadmap.
