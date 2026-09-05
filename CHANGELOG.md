# Changelog

Versions follow [semantic versioning](https://semver.org). The version lives in
`src/mateprover.cpp`; `CMakeLists.txt` parses it from there so the two cannot drift.

The two external contracts carry their own version numbers, documented in
`docs/OUTPUT_FORMAT.md` and `docs/PROOF_FORMAT.md`. Fields may be *added* to
either without a major bump; the meaning of an existing field will not change
without one.

## 0.1.0 — 2026-09-04

**Released under the MIT License.** The engine is an independent
implementation and carries no third-party code, so the licence was free to be
chosen rather than inherited. MIT is the permissive end of that choice: the
prover can be embedded anywhere, including in closed software, and the only
obligation is to keep the copyright notice. `tools/verify_proof.py` calls
python-chess, which is separately licensed and not distributed here.

**A finder lane, which does not improve coverage.**
`tools/finder_lane.py` has an external proposer suggest a mate and verifies it
with `--direct-depth`, reporting only what checks out. Sound by construction: a
false claim fails verification and costs only the check. Output is EPD carrying
the usual `proof` opcode plus a `lane` opcode saying whether the line was proved
shortest or verified within a claimed distance; the two are never merged.

Measured against `--direct-depth` at the same budget the lane adds **+0**: the
proposer, Matefish with a 4 GB proof-number table, claims 5 of the 24 positions
the baseline misses, and none of the five verifies. A comparison that pairs a
`--direct-depth` lane against an `--iterative-depth` baseline moves two
variables at once and credits the cost of minimality to the proposer; the
control for that is in docs/RESULTS.md. The tool is kept for its soundness
property, not for coverage.

**The verifier rejects empty input.** `verify_proof.py --require-proof` exits
non-zero when its input holds nothing, so a crashed producer in a pipeline
cannot report success. `--expect N` covers the related case of a producer that
died part way and left a valid prefix.

**Restriction portfolio flags.** `--restrict-checks`, `--restrict-king`,
`--restrict-maxdef`, `--restrict-threat` and `--lane0-weight` apply one
restriction or one budget split from the command line, so candidates can be
swept without a rebuild. Each removes attacker options only, so any mate found
under one is real.

**The version number goes DOWN here, deliberately.** This is a new repository
holding the current line of development, and it restarts at 0.1.0 to say that
the interfaces are not yet promised: the output format, the certificate schema
and the CLI are all still free to change. The 1.0.0 entry below is a real
release and its record is kept as it was written; it was published from a
different repository, and nothing in it is retracted by the renumbering.

Read 0.1.0 as "first release of this line", not as a step backwards from 1.0.0.

Six stipulations rather than one, two variant rules, a verified measurement
harness, and the first execution of continuous integration in the project's
life. Every addition is backwards compatible: existing output lines, existing
certificates and existing corpora read exactly as they did under 1.0.0, which is
why this is a minor version and not a major one.

**x-capture chess, and the variant framework that makes a third rule cheap**
(`--captures N` or `--captures W:B`). A side wins outright on its Nth capture,
with independent per-side quotas, composable with `--checks`. The fifth Forsyth
field is now tagged -- `chk3+3,cap5+2` -- while a bare `3+3` still means checks,
so existing corpora and the `checkwin` certificate token are untouched.

x-check shipped as a scalar; adding the second rule generalised it into a quota
vector indexed by rule, with one terminal, one win predicate, and one gating
list. x-check became rule 0 with no behaviour change, which is what made the
refactor safe: its thirty-two existing checks were the regression test.

Room for the quotas came from the transposition key's DEPTH field, which held
thirty-two bits for a value that never exceeds the requested depth. Narrowed to
eight, freeing twenty-four, with eleven still spare after the two rules. `-z` is
now refused above 127, because the cooperative key carries plies -- twice the
requested depth -- and a depth that cannot be keyed exactly must not be searched.

**The two rules do not need the same soundness gates, and that is the finding.**
GAP-1's "a lone king cannot mate" survives x-check, because a lone king cannot
give check either -- but a lone king CAN capture. The last-ply prune "a winning
move must be a check" survives x-check for the same shape of reason -- but a
quiet capture fills a capture quota and wins. Both are now gated, the second
across six call sites that were discarding the winning move before executing it.

That bug survived the first capture test, which used a move that happened to be
a check as well as a capture and so slipped through the very filter that was
broken. It took a lone king capturing a pawn to expose it.

**x-check chess, as a variant orthogonal to all six goals** (`--checks N` or
`--checks W:B`). A side wins outright on giving its Nth check. It is a variant
rather than a seventh goal because a way for the game to END is not the same as a
thing to force: every stipulation still names what must be forced, so 3-check
selfmate and 3-check helpmate are ordinary jobs. Allowances are per side and
independent, so 5+2 is as ordinary as 3+3. A fifth Forsyth field states checks
remaining (`3+3`) or, Lichess-style, checks already given (`+1+0`), and overrides
the flag.

The rule ends the game; the goal decides what that means. Under `--goal mate` the
final check IS the win being forced, and `--no-check-win` demands checkmate
specifically. Under every other goal the stipulation names a terminal POSITION, so
a game that ended by check count did not reach it and the line is dead at any
depth. A move that is checkmate and the final check at once counts as mate: the
stipulated terminal wins the tie, or a real solution is lost silently on exactly
the positions where both rules bite.

The allowance is part of the transposition key -- two positions identical on the
board but differing in checks remaining are different positions -- and it fit in
fifteen bits the context word already had spare. The limit is capped at 126 and
refused above it rather than clamped.

Certificates carry a `checkwin` leaf and `tools/verify_proof.py` checks it,
tracking the allowance itself because python-chess has no notion of one. It
rejects a claimed check win that gives no check, one with the allowance unspent,
and one with no allowance stated.

Standard chess is untouched byte for byte: no fifth field is emitted unless the
rule is in force, and a corpus annotation sitting where that field would go is
not mistaken for one.

**The measurement harness is now verified, and it was the least-verified thing
here.** Four measurement defects arrived in one session and none was in the
engine, which carried 414 checks of its own against `tools/paired_corpus.py`'s
zero -- and every published number passes through that file. All four were the
same bug, a value attached to the wrong thing: a per-lane budget passed as a
total, a stalemate line parsed as a selfmate result, a truncated stream shifting
rows against results, and a resume file keyed by goal alone so mate-in-10
reported mate-in-8's numbers under its own heading.

Records are now self-describing -- position digest, goal, requested depth,
engine, engine digest, both budgets, corpus digest, harness commit, schema --
so nothing is positional. Resume state is keyed by a hash of the entire
measurement definition, and reopening it under a different one is a hard error.
Invariants are asserted at load: no position twice, every depth inside its
requested bound, every record inside the corpus, and a ledger that refuses a
result set some *different* measurement already produced, which is the signature
of the shared-state-file defect and of nothing else. Parsing is strict -- result
lines are split into fields and matched on their own FEN and an exact goal
token, so a foreign token and a shifted row are both errors rather than quiet
non-solutions. And the harness no longer invents tuning parameters: a flag
reaches an engine only if the definition names it, and then it is recorded.

Twenty-nine checks, in the same suite and gate as the engine's.

**`src/kingescape.h`: one analysis, costed against two residue classes.** The
per-square attacker sets around a king were rejected twice when priced against
one residue class at a time. The same five sets feed the direct-mate coverage
exit and the selfmate rejection test -- 36 of the 38 residue positions -- and
that is what makes them worth building.

`flights` is exact; everything derived from it is conservative. The direction of
error is the opposite of the usual instinct: *over*-estimating what a king can do
makes a mate look impossible and throws the node away with the answer still in
it. `--self-check` compares the flight mask square by square against move
generation over every position in every corpus, 4,571 of them, with zero
mismatches, and cross-checks the 256-entry coverage table against a second,
naive computation of the same answers.

**The direct-mate coverage exit, built sound (`--coverage-exit`, default on).**
At depth 1 the whole node fails before a move is generated when no single piece
could deny the enemy king every escape square it unconditionally has. The
observed 15.2% fire rate reported earlier was an upper bound in three separate
places -- it used the full escape set rather than the unconditional one, excluded
kings from the coverage table, and ignored occupation. The last two were unsound
as a mechanism rather than merely loose as a measurement, and building the thing
is what forced them out. Castling and en passant are refused outright: both break
the premise that one move moves one piece.

**The selfmate node exit is rejected, and named the real target instead**
(`--selfmate-node-exit`, default off). Requiring a quiet king step to survive
*every* attacker move rather than one fires on 0.1% of selfmate depth-1 nodes and
saves 0.1% of moves -- three orders of magnitude weaker than the per-move test.
The same run found the per-move test being called 320 million times across sixty
positions and answering yes 84.9% of the time, which is not a heuristic but an
inner loop. It now answers with attack queries instead of board copies
(`--fast-reject`, default on), falling back to the exact form whenever a
discovered check might be available: 98.9% of 347 million calls resolved without
a board copy, identical verdicts on all sixty positions, **1.049x total and
1.013x median**. Recorded at that size deliberately -- the call count answered
how often the predicate runs, not what share of the search it is, and the honest
answer to the second question is an order of magnitude smaller than the first
implied.
On the mate-in-8 corpora at 20 s the exit solves 230 of 260 against 228 without
it, with **zero positions lost and zero answers changed** -- the direction that
matters for a mechanism whose failure mode is a silently missing mate.

**Two more mechanisms measured out before being written.** The fatal-anti-check
family was the next candidate, and the useful question about it turned out not to
be its fire rate. What it can save at a defender node is whatever the search
spends *before* reaching the refuting reply, and this engine already orders
replies to put refutations first. Over 260 positions, **98.7% of refutations
arrive on the very first reply tried**, mean 0.04 replies before one; the entire
pool such a test could remove is 3.9% of defender replies, and only 41.2% of
refutations are checks at all. Variants 3 and 2 rejected; variant 1 sits at a
different filter point and is now last rather than first, on the same evidence
that made it look attractive being about placement rather than payoff.

**All seven corpora re-measured against Chest 3.19 under one protocol** -- 5 s and
2 GB a position for each engine, same machine, same session -- and re-run again
through the hardened harness, so every row now carries a measurement identity and
a result fingerprint in `docs/measurement_ledger.jsonl`.

| goal | MateProver | Chest | only MP | only Chest |
| --- | ---: | ---: | ---: | ---: |
| mate d8 (200) | **158** | 126 | 42 | 10 |
| mate d10 (60) | **49** | 17 | 34 | 2 |
| stalemate (792) | **756** | 720 | 38 | 2 |
| selfmate (903) | **589** | 351 | 261 | 23 |
| selfstalemate (76) | **49** | 48 | 2 | 1 |
| helpmate (546) | **513** | 482 | 31 | 0 |
| helpstalemate (431) | **353** | 296 | 57 | 0 |
| **total (3,008)** | **2,467** | **2,040** | **465** | **38** |

Ahead on coverage and on speed on all seven; exclusive wins 465 to 38. Median
per-position speedups 5.3x to 82.5x, totals 2.21x to 6.73x. Helpmate and
helpstalemate have zero positions Chest solves and MateProver does not.

Against the previous run of the same protocol: MateProver -8, **Chest -14**,
margin 427 against 421, exclusive wins 465 against 459. No change here can lower
Chest's score, so the absolute dips are the machine rather than either engine;
the paired figures are measured within a session and both moved in MateProver's
favour. The coverage exit is **not** visible in this table -- mate-in-8 went down
three -- and the table is not offered as evidence for it. Its only clean evidence
is the same-session A/B: 230 against 228 at 20 s with nothing lost.

73 positions where Chest returns a definitive "no solution" are evidence about the
corpora rather than either engine.

Also fixes a harness defect found mid-run: the paired driver keyed its resumable
state by GOAL, so the two directmate corpora shared a state file and the d10 run
resumed d8's completed state instead of running, reporting d8's numbers under
d10's heading. Re-run with its own state.

**The selfmate residue is now nine positions, characterised, and two more
mechanisms are measured out.** After the rejection test the Chest-only residue is
12 at 30 s and **9 at 120 s** -- three fell to budget alone, so part of what has
been called structural since the 10 s paired run never was. Eight of the nine are
6-8 man miniatures at sfm 8 or 9 whose defender holds one strong unit (K+Q four
times, K+R twice), the material that most resists being forced to mate; the ninth
is a 24-man sfm 6.

Strengthening the rejection test will not move them: it already fires at 94.1% on
a failing miniature, and extending its witness beyond king moves gains no
coverage, because a defender node already returns on its first non-mating reply.
The king-move test is a cheaper route to the same verdict, not a route to more of
them -- a cost optimisation, not a coverage one.

The defender transposition table is rejected a second time, now on the class
picked to favour it. A lone shuffling queen is the textbook case for memoising
the defender side; it gives 13% fewer nodes and no time, exactly as on the whole
corpus at section 78. Two independent measurements, question closed.

No instrument remains for the nine. Their shape argues for backward analysis, but
a 7-man selfmate table is ~200 GB per material class and they span seven classes;
only one position is six-man and buildable. See `docs/ARCHITECTURE.md` section 87.

**The selfmate attacker-rejection test, and the first mechanism to move the
residue.** At selfmate depth 1 an attacker move is refuted without searching it
whenever the defender king has a legal move that gives no check: a selfmate in
one needs EVERY reply to mate, and a king move can never be one. Measured as a
read-only observer first -- it would reject 92.8% of depth-1 attacker moves, and
those are 91.2% of all attacker candidates, so it covers 84.7% of attacker work.

    sfm 5 disproof  11,059,528 nodes / 2.87 s  ->  1,867,551 / 1.37 s

250 selfmates at 4 s: 208 against 205, zero depth disagreements. The 28-position
Chest-only residue at 30 s: **16 against 10**, including **4 of the 14
miniatures**, which every previous mechanism had scored zero on. Default on,
`--no-attacker-reject` is the differential test, `--reject-observer` counts
without acting.

Implemented exactly -- make the move, walk the king's eight neighbours -- rather
than by the specification's board-free geometry, which needs per-square attacker
sets this board does not maintain and which is unsound if it over-estimates the
flight set. The exact form cannot over-estimate anything and still skips the
defender node, so the representation change is deferred and may never be needed.

Gated to selfmate. Defaulting it on broke two selfstalemate checks within
seconds: a quiet king move cannot be checkmate but is exactly what might
STALEMATE the attacker, and the two goals share the routine. See
`docs/ARCHITECTURE.md` section 86.

**Internal iterative deepening is rejected, and with it the whole
graded-failure-depth line.** Built together with the per-move disproof array,
since section 83's objection to the array -- consultable at only 6.24% of
expansions -- is specific to root deepening, and a node that deepens itself makes
the array a local variable instead. It fires at **42 of 242,780 attacker
expansions** and skips 0.014% of candidates, for 9.5% more nodes. The cause is
structural: internal deepening needs levels to iterate and only the top few plies
have any, while the mass of an AND/OR tree is at the bottom where remaining depth
is 1 or 2. Reverted.

That 0.014% is the same fact as section 82's disproof-excess histogram (99.98% in
bucket zero) and section 83's 6.24% ceiling, reached from three unrelated
directions: MateProver's disproofs prove exactly what they are asked, so nothing
downstream of a graded failure depth can fire. That is correct behaviour for an
exhaustive AND/OR search, not a defect -- over-proof requires depth-independent
knowledge, and the only candidates are material theory (measured at zero
applicability in section 80) and a tablebase (excluded). The line is closed for a
reason rather than left unfinished. See `docs/ARCHITECTURE.md` section 85.

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
sharing no code with the engine. 599 automated checks cover perft against
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

### Two-ply decomposition: built, measured, rejected

`--reply-split` takes the parallel split a second ply down. A worker that runs
out of root moves helps prove another root move's defender replies, on the
argument that a defender node is a conjunction -- every reply must be proved --
so the extra threads cannot be speculative.

The argument is wrong, and the measurement says so plainly: 24 threads on a
depth-7 capture quota went from 7.08 s to 31.70 s, and from 21.1M nodes to
56.9M. A defender node that ends up REFUTED stops at its first refuting reply,
which the reply ordering puts first most of the time, so helpers prove twenty
subtrees the sequential search never visits. On a position with no solution
every node is refuted -- and that is the workload the 4.75x ceiling was measured
on in the first place.

Ships OFF, with the mechanism, the gate (`--reply-split-min-proved`) and a
differential test that pins the one property worth keeping: the composition
reads results back in reply-index order, so the verdict, the line and the
certificate are byte-identical to the sequential loop's. See architecture 111
for the general form -- the node type that is safe to split depends on the
verdict, which is what the search is trying to find out.

### Young brothers wait on the OR nodes below the root

`--or-split` (on by default) takes the parallel split past the root. A worker
that runs out of root moves helps refute the ATTACKER moves below another root
move's refuting reply -- the node type 111 established is the safe one to split
in a failing search, since disproving an OR node needs every child.

The eldest four children are searched alone first. A node that is going to
SUCCEED stops at its first working move, so sharing the rest out speculates, and
one eldest child is not enough of a wait: at `--ybw-first 1` the split cost 3.5%
on deep directmates, and at 4 it gives all of it back while costing nothing on
the disproof workload it exists for.

Written inside `prove_attacker`'s own move loop rather than as a second copy of
it. The composition accepts the LOWEST-indexed child that settles the node --
exactly where the sequential loop would have stopped -- so the answer, the line
and the certificate are byte-identical to the single-threaded search. Twelve
differential checks pin that, at the most aggressive settings of both splits.

Depth 7 under a capture quota, 24 threads: 6.22 s to 5.44 s, lifting the ceiling
from 5.87x to 6.72x over one thread. That is 1.14x, not the 2x predicted -- the
ply-3 children turn out to be as unequal as the root moves were, so Amdahl
applies again one ply down. See architecture 112.

### The split follows demand, at whatever depth the pool runs dry

Young brothers wait has two conditions -- wait for the eldest child, and split
only if another worker is idle -- and the previous release shipped one. A fixed
ply publishes whether or not anyone is free to help, which is why splitting two
plies down had measured worse: it created a split point inside every ply-3 task
against a handful of idle workers, and most of the pool became owners blocked at
their own tails. That was read as coordination cost. It was supply with no
demand.

A node now publishes only while there is an idle worker not already spoken for
by an open split point, re-checked before every child, at any depth with at
least `--or-split-min-depth` plies of work left. The floor is absolute rather
than relative to the root, because the cost of publishing is fixed while the
benefit scales with the work underneath.

Depth 7 under a capture quota, 24 threads, three interleaved repetitions:
6.60 s root split only, 6.19 s at one level, 5.98 s demand-driven -- 5.71x to
6.30x over one thread. Helpers now take 3,645 of 4,659 children rather than 613
of 2,125. Deep directmates do not regress.

The previous release's conclusion that "the real lever is a different
decomposition, not a deeper one" was wrong. A deeper decomposition pays; it
could not pay while publishing work nobody was waiting for, into nodes too small
to be worth publishing. See architecture 113.

### Fixed: the eviction counter was pointed at the wrong table

`--profile` reported 0 evictions at 64 MB, 128 MB and 4 GB while node counts
moved 60% with memory. The shared proof table is handed to the root-split
WORKERS and never to the enclosing search, so the report was reading the main
search's own table, which barely gets used once the root splits. Folding the
shared table's count in at the end of each route gives 26.6M evictions at 64 MB
and 0 at 4 GB -- which explains the memory curve exactly, and is the number
anyone tuning `-M` actually needs.

### Three rejections, kept as switches

- `--no-tt-lines` suppresses the line and certificate in table entries. No
  effect: 93% of entries are disproofs, which store neither already.
- `--tt-depth-evict` chooses eviction victims by depth bound rather than hash
  order. Consistently slightly worse -- depth says what an entry cost, not
  whether it will be wanted again, and a cache is for the second.
- `--owner-helps` lets a worker blocked on its own split help others, with a
  written termination argument (splits carry a monotonic sequence number and a
  worker may only claim from splits newer than its own, so the wait relation
  strictly increases and cannot cycle). Sound, and it fires 98 times in a whole
  depth-7 search: demand gating had already stopped creating blocked owners.

All three off by default. See architecture 114.

### 2.3x on deep searches, by freeing the table in parallel

The remaining parallel loss was not in the search. Instrumenting worker time
showed workers are busy 93-98% of the time they are alive, with parking and
waiting together under four worker-seconds out of a hundred and fifty -- there
was no pool of idle time to reclaim.

What there was, at every thread count alike, was a constant two and a half
seconds of wall clock outside the parallel region. It is the shared
transposition table's destructor: twenty million entries across 256 hash maps,
handed back one at a time when the route returns, inside the reported `acs`,
after the verdict is already known. It scales with `-M` and not with the search
-- 0.41 s at 256 MB, 0.87 s at 1 GB, 2.65 s at 4 GB, on the same node counts.

The shards share nothing, so they are now freed concurrently. Depth 7 under a
capture quota at 24 threads and 4 GB: 6.02 s to 3.92 s, moving the ceiling over
one thread from 6.00x to 9.01x. The gain is proportional to `-M` and there is
almost nothing to recover at 256 MB.

New `--profile` counters: `worker_micros`, `root_work_micros`,
`split_work_micros`, `split_park_micros`, `owner_wait_micros`. See
architecture 115.

### Portfolio lanes now share their proofs

The lanes solve different problems, so they cannot share a table in general.
They can share proofs: a restriction only removes attacker options, so a mate
forced with fewer options available is a mate forced with more. A restricted
lane's proof is therefore valid unrestricted, while its disproof is valid for
nothing but its own problem. One table carries proofs between every lane;
nothing on the disproof side is written to it or read from it, enforced at both
call sites rather than assumed.

The hazard is minimality rather than validity -- a restricted lane can prove a
longer mate than exists, and a wrong `dm N` is a wrong answer even when the mate
is real -- so the gate compares (position, depth) pairs and permits only one
kind of difference: a position that was unsolved becoming solved. Over 72
positions no depth moved and none stopped being solved; 30 mate-in-8s at a 5 s
cap went from 18 to 19 solved. On by default.

### The rest of the teardown

115 left 0.58 s of freeing unaccounted for. The workers' own DFPN and hint maps
were the other half; they free concurrently now too, sharing one helper with the
shard destructor. Depth 7 at 4 GB and 24 threads: 6.01 s to 3.89 s, with
teardown down to 13% of the run from 44%. The single-threaded case is not fixed
and is not instrumented -- with no root split there is no shared table, and the
private one is freed outside the route.

### The cross-lane table now carries disproofs, and crosses jobs

Working out what may cross a lane boundary turned up a fourth direction the
previous release missed. A restriction removes attacker options, so an
UNRESTRICTED lane's disproof is valid for restricted lanes too: "no mate within
d with every move available" implies "no mate within d with fewer". Three of the
four directions are sound; only a restricted lane's disproof travels nowhere.
That matters because disproofs are 93% of what the table holds, so the
proofs-only version was nearly empty.

`--cross-job-proofs` carries the same table between the jobs of a `--successors`
run, where the positions are siblings and their subtrees overlap. Sound because
the key is a complete description of a position rather than of a root -- as
tt_key's own comment anticipated. It stands down when an escape rule is live,
because the escape LIMIT is deliberately unkeyed on the argument that it is
constant for one search, and carrying a table between jobs is what could make
that false.

Worth 4% on a depth-6 successors job (29.95 s to 28.79 s), with a 1% hit rate --
the cross table is a fallback, probed after the lane's own table misses. Nothing
measurable elsewhere. 30 mate-in-8s: 19/30 either way, every verdict and depth
identical.

Two corrections to the previous release's notes. The 18-to-19 gain reported for
cross-lane sharing did not reproduce; it was a borderline position moving on
timing at a 5 s cap. And reuse buys nothing from avoiding table allocation --
a 64-fold change in `-M` moves a trivial batch by 0.01 s, because the table is
lazy and grows only to what is stored. See architecture 117.

### Automated search: two tools, and the move-ordering weights exposed

`tools/autotune.py` searches the configuration (coordinate descent over declared
knobs) and the five move-ordering weights (a genetic search).
`tools/adversarial.py` searches for POSITIONS instead: `perft` and `fuzz` check
the engine against python-chess and against an independent brute-force oracle,
and `hunt` evolves positions that maximise the node count.

The one source change is `--order-weights c,p,q,r,m`, which exposes the ordering
bonuses that were literals. Those five are the right ones to automate because
ordering is soundness-neutral -- it changes the ORDER moves are tried and never
the SET, so no setting can change a verdict, a depth or a certificate. The CLI
refuses any assignment reaching 50000, which is the magnitude prove.h reads as
"this move gives check".

Both tuners score on NODES rather than seconds -- this machine drifts 15% between
identical runs while the effects are 3-10% -- and gate lexicographically:
nothing is compared until every baseline verdict and depth is unchanged.

Neither found a shippable improvement. The ordering search produced a 5.6% gain
on its held-out set that REVERSED on a third corpus (10/25 solved became 9/25),
which is overfitting and is reported as such. Its soundness claim did hold:
across the positions solved by both weight sets, zero depths changed. `perft`
and `fuzz` found no engine bugs over 169 random positions -- though `perft`
found a bug in itself first, having compared depth 1 against depth N. See
architecture 118.

### Candidate pruning theorems, measured but never acted on

`--predicate EXPR` takes a conjunction of threshold tests over twelve cheap
board features and evaluates it at every attacker node -- then searches as though
it had said nothing. When the node returns, its true verdict is known, so a
candidate that fired where a mate existed is refuted by counterexample, exactly
and permanently, while one that fired where the search failed has its subtree
counted as what it would be worth if a person ever proved it.

Implemented as a wrapper around `prove_attacker` rather than an edit inside it,
so no candidate can affect a verdict even by accident. The suite pins both
halves: the answer is unchanged under every candidate, and a false candidate is
seen to be refuted.

`tools/predicates.py` runs a genetic search over candidates and checks its own
instrument first against GAP-1's Axiom 1 (proved) and `depth>=1` (false).

Nothing was promoted. 48 of 50 candidates were refuted in minutes; the two
survivors were refuted in seconds once shown harder positions. One of them
claimed an attacker in check can never mate, appeared to save 48% of all nodes,
and loses mates 4,034 times over ten mate-in-8 positions -- which is why the
tool's ranking is lexicographic in counterexamples and not a weighted sum.
See architecture 119.

### Documentation reconciled with what actually ships

The README had drifted several sessions behind the code, and two of its claims
were not merely stale but wrong.

- It announced **two** variant rules; there are three. x-escape — a side loses
  when its own king's escape count reaches N — was undocumented, along with the
  `--escape-count` diagnostic. The goals-by-rules matrix is now stated: six
  goals, three rules, each rule per-side and composable.
- Its Limitations section said MateProver "has no answer" for selfstalemate,
  helpmate and helpstalemate. All three ship, are measured against the reference
  implementation, and the same README said so eleven lines from the top. It now
  describes how the cooperative goals differ instead — both sides are OR nodes,
  so the preconditioner, the portfolio and the adversarial split are all
  undefined there.
- It said parallelism "is worth about one extra position in forty per doubling,
  and root splitting nothing at all". The split is worth 9.0x on 24 threads. The
  exchange rate between speed and reach is unchanged and still the point: 9.0x is
  about three positions of forty, not a depth notch.
- `docs/RESULTS.md` carried the same inert-root-split claim, with an argument
  attached that was backwards. Refuting every sibling is what makes the work
  divisible; it is PROVING that does not divide.
- The three automation tools were entirely undocumented and now have a section.

### UCI mode, deliberately lossy

`--uci` speaks the protocol. `go mate N` is in the UCI specification and is
exactly this engine's question, so it maps to `score mate N` with the same N and
the same key move the EPD interface reports.

What the protocol cannot express is "no solution exists" — a GUI seeing no
`score mate` cannot distinguish an exhaustive disproof from a timeout. Those are
separated on `info string`, which a human can read and a GUI cannot act on, and
`bestmove 0000` is returned rather than a legal move the engine never proved
anything about. Certificates cannot travel, so `--emit-proof` is refused rather
than silently ignored. The other five goals and three variant rules ride on
`setoption`, which no GUI populates.

The UCI answer is a RENDERING of the canonical result line, not a second search
path — same `solve_line`, same portfolio, same gates — so it cannot drift from
the engine. The suite checks the two interfaces agree on depth and key move.

`stop` needed one line: `SearchConfig` is copied wholesale into every worker and
lane, so a stop flag on the config is already visible everywhere, and
`search_cancelled` marks it `timed_out` as well as `aborted` so a stopped search
can never be read as a disproof.

Beyond GUIs, this lets MateProver run under matetrack's own UCI harness — an
independent measurement path this project did not write. See architecture 120.

### 2.4x on deep searches: eviction was scanning, not shedding

`evict()` walks an entire table shard — an `unordered_map`, so a pointer chase
over scattered nodes — and it did that once per call while shedding only an
eighth of capacity. Eight times as many full scans as shedding a half needs.

Depth 8 on the capture quota, 24 threads, 8 GB: **373.3 s to 156.4 s**, with the
node count moving 1.2%. Half the wall clock of a deep search was iteration over a
hash map, discarding entries. It is not a trade against table quality either — on
a mate-in-10 at 64 MB, where the table matters most, shedding a half does 46%
more nodes in the same 40 s.

`--tt-shed-divisor N` exposes it, default 2. The name says what it is: the cost of
the scan, not a memory setting.

The projection this came from was also wrong. Depth 8 of the `d(3)` search was
quoted at hours and takes **six minutes**; depth 9 is a few hours, not the 35–90
this project had been repeating. That figure was scaled from a pre-optimisation
measurement and never re-taken.

Re-tuning the move-ordering weights for x-capture was tried and **rejected**: 21%
fewer nodes on the training set, 4.5% worse on held-out positions. 108's principle
holds from a new angle — ordering ranks moves by how fast they resolve a subtree,
not by how much they advance the goal.

Two defects fixed in `tools/autotune.py`: its saturation guard counted *solved*
positions, so it refused any disproof corpus outright, and it did not check the
engine's exit code, so a truncated run was reported as a changed verdict — the
loudest alarm it has, for the most benign cause. See architecture 121.

### Both re-measurements confirm the defaults

The shed-divisor curve is monotone from 1/8 down to 1/2 (373.3 s → 156.4 s) and
then falls off a cliff at 1/1 (>560 s, did not finish): with a divisor of 1 the
low-water mark is zero, so eviction erases entries stored moments earlier and the
table can never accumulate anything. A half is the corner of the curve, not a
compromise, and the shipped default is right.

Memory, re-measured now that eviction scanning no longer dominates: 2 GB 173.6 s,
8 GB 156.4 s, 16 GB 154.0 s. Removing the confound flipped the sign — 2 GB had
measured *faster* than 8 GB — and left the magnitude small. The knee is 8 GB and
doubling again buys 1.6%.

### The flat proof table: 1.76x, and the worker cap lifted

`--flat-tt` (on by default) replaces the hash map with a direct-mapped array.
121 measured eviction *scanning* as still ~42% of a deep search even after
`--tt-shed-divisor` made scans four times rarer; a direct-mapped table removes
the scan rather than making it rarer, because a collision is resolved by
overwriting the slot and `evict()` becomes a no-op.

Depth 8 under the capture quota, 32 threads, 8 GB: **136.1 s to 77.3 s**. The
node count rises 37% — direct mapping discards harder than an aged hash map —
and the per-node cost falls far enough that it does not matter.

The safety argument is unchanged and absolute: the table is a memo of verdicts
that are pure functions of an exact key, every slot stores its key and probe
compares it, so a collision is a miss and never a wrong answer. Lines live in a
small side map for the 7% of stores that are proofs, and clearing it can only
shorten a reported variation, never change a verdict. The whole suite runs
through the flat path and a new gate compares entire output lines, certificate
included.

`worker_count = min(threads, n)` no longer caps the pool at the root branching
factor. It was right when the root split was the only split; sub-root splitting
gave a spare worker somewhere to go, and the cap was never revisited. It bit
hardest where branching was smallest — the chess starting array has twenty legal
moves, so the `d(3)` search could never build more than twenty workers. Depth 8:
166.1 s at 20 threads, 153.5 s at 32.

Together with 121, depth 8 has gone 366 s → 156 s → 77 s. `d(3)` at depth 9
should be **20–40 minutes**, against the 35–90 hours this project was quoting a
day ago. See architecture 122.

### The memory budget was being divided by the wrong entry size

`entry_capacity_for_mb` divides by `EST_BYTES_PER_ENTRY` = 192, sized for an
`unordered_map` entry plus its heap node. A flat slot is **56 bytes**. So since
the flat table landed, `-M 8192` had been producing 2.51 GB of table — under a
third of what was asked for — with no symptom but being slower than necessary.

Depth 8, 32 threads, `-M 8192`: **74.6 s to 60.9 s**, with the node count falling
23%. Every figure in the previous release, including the flat table's 1.76×, was
measured with a table two-thirds too small, so that gain was understated.

The knee moved *down*: 8 GB now beats 16 GB, because a megabyte buys three times
the entries it used to. Fourth time a memory conclusion here has needed re-taking
after something else moved beneath it, which is now stated as a rule — a memory
finding is only valid for the table it was measured on.

Depth 8 across this line of work: 366 s → 156 s → 77 s → **61 s**. See
architecture 123.

### `--heartbeat S`: node counts while a depth is still running

`--progress` publishes a proven bound when a depth *completes*. Between the last
completed depth and the answer, the stream said nothing — so a depth-9
capture-quota search ran for an hour, twice, in total silence, and the only
honest answer to "how much longer?" was that the engine provided nothing to
estimate from.

`--heartbeat 30` prints nodes-so-far every thirty seconds. Each worker publishes
its count on a 16,384-node countdown; a monitor thread sums the slots and prints.
It takes no lock the search uses and reads only relaxed atomics, so it cannot
perturb what it reports on.

The line says **searching**, never *proven*. Every other line in this stream is
permanent; this one asserts nothing about the position, and the wording keeps
them apart on sight.

It immediately showed something invisible before — throughput decaying *within* a
depth as the table fills: 120M nodes in the first ten seconds, then 80M, 77M,
59M, 53M, 53M.

### Fixed: the move-ordering hint maps were unbounded

`attacker_proofs` and `defender_refutations` were `unordered_map`s with no
capacity, no eviction and no periodic clear — one pair per worker, thirty-two of
them, growing for the entire search.

At depth 9 of the `d(3)` capture-quota problem, with `-M 3072` set, the process
reached **30.84 GB of commit on a 29.7 GB machine** and spent thirty-six minutes
servicing **124,000 hard page faults per second** instead of searching.
Throughput had collapsed 24×, from 8.2M nodes/sec to 300K. The transposition
table beside it was bounded and behaving perfectly, which is why shrinking `-M`
from 8192 to 3072 changed nothing: the leak was elsewhere and does not scale
with `-M` at all.

Replaced with `HintTable`, a fixed-size direct-mapped array that overwrites on
collision. A hint only reorders moves — losing one costs ordering quality and
can never change a verdict, a depth or a certificate — so discarding on
collision is the right shape rather than a compromise. `--hint-entries N` tunes
it; the default is 2^18 slots per worker, about 17 MB each.

Same search, `-M 3072`, depth 8: commit **30.84 GB → 4.83 GB**, timing unchanged.

The suite caught a second, quieter fault immediately: an unsized `HintTable`
accepts every store and returns nothing, and the enclosing search's tables were
not being sized — so the DFPN preconditioner, which writes its guidance there,
was silently contributing nothing. `HintTable` now carries a size from
construction, so "forgot to size it" cannot be a silent behaviour change.

### `d(3)` says which problem it means, and the alternative is measured

Architecture 104 recorded `d(3) ≥ 9` without stating the rule it was measured
under. Every figure there is `cap3+3` — **both** sides win outright on their third
capture — so `d(N)` is "how fast can White make N captures *without letting Black
make N first*", a race rather than a one-sided count. The section read as though
it described the asymmetric problem, and nobody had run that one.

Now measured. `--captures 3:126` gives Black an allowance it cannot reach, so
Black can only win by mate:

| depth | race `cap3+3` | alternative `cap3+126` |
|---|---|---|
| 7 | no win, 22,427,185 | no win, 21,742,346 |
| 8 | no win, 352,010,893 | no win, **1,533,755,964** |

`d(3) ≥ 9` holds under both, as do `d(1) = 3` and `d(2) = 5` with identical
principal variations — so the headline survives the ambiguity. The race stays as
the headline rule and the alternative is recorded beside it.

The two diverge at depth 8, and opposite to expectation: the alternative is
weakly *easier to solve* (fewer Black resources) but **4.4× more expensive to
search**, because in the race a line where Black reaches three captures is cut
immediately. Black's counter-quota was doing substantial pruning.

104 also gains the `d(1)` and `d(2)` tables it never had — they were a single
prose sentence with no node counts beside a `d(3)` result carrying four rows and
two independent configurations.


### `d(3) ≥ 10`, proven

Depth 9 of the capture-quota search from the starting array returned
`proven no solution within 9` after **3 h 04 min** and 68,315,107,395 nodes.
White cannot force three captures in nine moves. All nine depths ran in one
process against one table, so the series is a controlled measurement rather than
a comparison between builds:

| depth | plies | nodes at this depth | ratio | wall clock |
|---:|---:|---:|---:|---:|
| 6 | 11 | 1,713,416 | 11.7× | 2.2 s |
| 7 | 13 | 20,106,922 | 11.7× | 4.2 s |
| 8 | 15 | 330,029,716 | 16.4× | 46.8 s |
| 9 | 17 | **67,963,096,502** | **205.9×** | 11,037.7 s |

The bound is `d(3) ≥ 10` under the race rule `cap3+3`. Under the alternative
`cap3+126`, where Black cannot win by captures, depth 9 has not been run and what
stands is `d(3) ≥ 9`.

**Two projections in the documentation were wrong and are corrected.** Depth 9
was quoted at 90–240 hours in architecture 104 and at 20–40 minutes in 122; it
took 3.07 hours. Both extrapolated a growth factor across builds that were
changing underneath them by a factor of six.

### Depth 9 cost 206× the depth before it, and the reason is not the machine

A twelve-fold break from a trend that had held for four depths usually means a
bug. It does not here, and the first measurement rules out every hardware
explanation: the node **rate was flat**, 7.92 M/s early against 5.35 M/s late and
6.19 M/s overall, versus depth 8's 5.77 M/s. Depth 9 ran *faster per node* than
depth 8. No memory wall, no table thrash — 206× more nodes at an unchanged cost
each.

What changed is the tree, and it decomposes into three terms — attacker width
`W`, defender width `B`, and the transposition discount, the factor by which the
table collapses `W × B` children into fewer real expansions. All three are
measured, not inferred: `--profile` has always reported the counters
(`defender_replies_tried / defender_move_lists` is `B`), which makes each depth a
thirty-second measurement rather than a three-hour one.

| d | nodes | R | B | W | discount |
|---:|---:|---:|---:|---:|---:|
| 6 | 1,642,739 | 11.75× | 1.08 | 25.31 | 2.33× |
| 7 | 19,620,832 | 11.94× | 1.30 | 27.76 | **3.02×** |
| 8 | 306,627,206 | 15.63× | 1.52 | 29.60 | 2.88× |

Depth 9's memory regime — 0.197% of its nodes held in the table — was reproduced
by shrinking the table until *depth 7* matched it, which costs 95 seconds instead
of three hours. At `-M 8` (0.218% coverage) depth 7 pays **3.06×**. The curve is
very flat, `penalty ~ coverage^-0.237`: a 23,586× range of table sizes costs only
5.69×, because nearly all of the table's value is local transpositions a tiny
table still catches.

The break then closes exactly:

```
R(9)/R(8) = 13.2× observed
  attacker widening  W    1.06×
  defender widening  B    4.21×
  table losing power      2.94×
  product                13.18×
```

**`B(9) ≈ 6.4`** — the defender is the largest single factor, so the direction was
right. Capacity and defender widening were never rival explanations: the discount
*is* the table's power, so capacity multiplies into the same equation rather than
competing with it. Both are real, and `discount(9) ≈ 0.98` says the table's
ability to collapse the tree is already gone by depth 9.

This supersedes an earlier reading in the same release that inferred `W ≈ 12` and
`B ≈ 17` from a two-term model, and priced the discriminator at two three-hour
runs. Depth 10 projects to **139–277 days**, not the 26–38 that a flat
continuation of `R` suggested.

### Fixed: `--heartbeat` was inert on every single-threaded search

The monitor was written inside `run_root_split_depth`, so it existed only when
the root split did. A `--threads 1` search printed nothing however long it ran,
and so did **every portfolio lane**, since dividing eight threads over nine lanes
gives each of them one. The flag accepted its argument, reported no error, and
did nothing, in the two configurations most likely to be left unattended.

It is now `HeartbeatMonitor<Count>`, a scoped object shared by both paths whose
destructor joins the thread — which the split needed a hand-rolled guard for,
because that function returns from two places. Verified on both: 38 lines on a
38-second single-threaded depth 7 that previously printed nothing.

Found by testing the instrument on a workload that finishes in seconds, after an
evening spent watching a three-hour run through it. `--progress-moves` was
checked at the same time and was always sound, being published from
`prove_attacker` rather than from the split.

### Defender move ordering: measured, built, and rejected

Architecture 125 named defender ordering the top lever for depth 10 and put
5–30× on it, reasoning that `B ≈ 6.4` against a floor of 1.0 compounds over nine
defender plies. The engine had been counting the answer since the
fatal-anti-check work, and it says otherwise.

| depth | refuted defender nodes | FIRST reply refutes |
|---:|---:|---:|
| 6 | 599,915 | **99.15%** |
| 7 | 6,452,586 | **97.75%** |
| 8 | 102,697,792 | **97.16%** |

`B` is above 1 mostly because of defender nodes where the defender *loses* and
every reply must be searched by construction — 3.6% of nodes at depth 8 but
**33% of all replies**, at 14 each. No ordering touches those. Perfect,
unattainable ordering is worth 1.050× per ply, **1.55× over nine plies**. That is
the ceiling, not the estimate.

`--defender-history` implements the one genuinely missing mechanism: a from/to
refutation history, generalising across positions where the existing hint table
is keyed to one position. Measured:

| | depth 6 | depth 7 |
|---|---:|---:|
| off | 1,642,739 | 19,620,832 |
| on | 2,043,369 | **31,168,364** |

24% and 59% worse — while *improving* its own metric, first-reply-refutes rising
97.75% → 98.23%. It displaced an answer ordering that was never trying to refute
sooner: that ordering picks *which* refutation is taken, because one reaching a
hopeless position several levels shallower returns a larger proven failure depth,
and the surplus is what level skipping consumes. Cheaper refutations, found
sooner, cost 59%.

Kept as a switch, defaulted off. Every proven depth is identical across the
corpus with it on and off — only PVs differ, where duals exist — so it is exactly
as soundness-neutral as claimed, and useless.

### `d(3) = 9` exactly, under the rule where Black can only win by mate

Depth 8 was already a proven refusal under `cap3:126`. Depth 9 is solved by
**`1.b3`**, which wins against all twenty Black replies — 68,409,851,876 nodes
over 6h37m. An exhibited strategy proves an upper bound (architecture 111) and
the depth-8 refusal supplies the lower one, so this is exact: **`d(3) = 9` moves
= 17 plies**. The first exact third-capture value in the project.

Under the race rule `cap3+3` the bound remains `d(3) ≥ 10`. Not a contradiction —
removing Black's winning condition removes Black *resources*, so the asymmetric
problem is weakly easier to solve.

Only five replies force all nine moves and they take **97% of the time**;
`1...b5` collapses in three seconds.

### Seven optimisation proposals, measured: one worked

| proposal | estimated | measured |
|---|---:|---:|
| batch for a shared table | 1.5–3× | **0.97×** |
| `reply_split` | 2–4× | **1.00×** |
| independent lanes | 1.5–2× | **1.69×** |
| retune `OrderWeights` | "possibly large" | **nothing beats the default** |
| TT prefetch (`--tt-prefetch`, new) | 1.1–1.2× | **1.00×** |

Six of seven estimates were wrong. The survivor is the only one that had a
measurement behind it beforehand — 37% parallel efficiency at 24 threads, which
correctly predicted that four lanes of six threads beat one of twenty-four.
Node count *falls* 25% when they do: a wide search on one problem speculates,
four narrow searches on four problems don't.

`-M 12000` measured **15% slower than `-M 4000` at identical node counts** —
pure memory latency. And the ceiling for all memory work is now bounded: the
same search runs at 629K nodes/s against an 8 MB table that fits in cache
against 357K on the working 8 GB table, so **1.76×** is everything prefetch,
large pages and layout can ever return between them. Prefetch collected none of
it and is defaulted off.

`tools/candidates.py` ships the restricted-root candidate test: two-phase
scheduling for the bimodal cost distribution, four lanes, iterative deepening,
and raw engine output written to disk *before* anything is parsed — after a
driver discarded 6h37m worth of principal variations by keeping only its own
summary.

### The reachability bound, measured and dead

Architecture 127 named a contact-distance prune the largest remaining lever: no
attacker man within reach of a defender man means no capture, so a node needing
one could be cut unsearched. It was the only candidate that cuts *node count*
rather than time per node.

`contact` is now a predicate feature — empty-board move distance from any
attacker man to any defender-occupied square, BFS tables built once. Measured
over 3,053,854 attacker nodes:

| predicate | fires |
|---|---:|
| `contact>=2` | **0** |
| `contact>=1` | 3,053,854 |

Exactly 1 everywhere. Admissibility demanded ignoring obstruction — a cleared
board can only shorten a journey, never lengthen it — but a queen on an empty
board reaches nearly every square in one move. **The obstruction was the whole
bound.** Keeping it restores the power and loses the soundness, since blockers
move; a version that survives has to reason about which blockers can vacate in
time, which is a search rather than a bound.

Kept as a feature, inert unless `--predicate` names it, so the result is
reproducible and stronger formulations can be tested the same way.

### Relational predicate features, and the measured price of soundness

The predicate language counted each side in isolation and could not express what
the two sides were doing to each other. Added `dattacked`, `aattacked`,
`dkingring`, `acaptures` and `mate1imp` (exposing the coverage test from 107).

The ceiling first: `depth<=1` measures what a *perfect* depth-1 lemma could
return — **26.8% of the run, a 1.37× ceiling** — and **47% of its fires land on
nodes that were proved**. Half the nodes at any depth are winnable, which is why
every previous generator run found only accidents.

Relational features change that by three orders of magnitude:

| predicate | fires | counterexamples |
|---|---:|---:|
| `depth<=1` | 2,720,038 | 1,289,312 (47%) |
| `acaptures<=0&depth<=1` | 198,253 | 62 (0.031%) |
| `acaptures<=0&depth<=1&mate1imp>=1` | 485 | **0** |

The first zero-counterexample candidate in the project. Two defects found by the
observer: `dattacked` misses **en passant**, where the captured pawn is not on
the destination square (91 → 62 once `acaptures` handled it), and the residue is
the other winning route, mate.

**Soundness costs 400× the fire rate** — the clause removing the last 62
counterexamples takes the saving from 3.7% of the run to 0.009%. At depth 1,
deciding "no win here" *is* the depth-1 search, so a lemma must be cheaper than
one move generation, and the cheap tests are the conservative ones: `mate1imp`
fires at 6.7% of eligible nodes.

Generator re-run with the new features: **0 unrefuted of 28**, instrument check
passing. It remains a falsifier rather than a discoverer.
