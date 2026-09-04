# What Was Tried, And What Worked

The architecture document records every finding in the order it was made. This
one states the argument: what this engine's capability actually comes from, what
was measured and rejected, and how far to trust any of it.

Section numbers below point into `ARCHITECTURE.md`.

## The short version

**One idea produced essentially all of the capability, and it was not a search
idea.** Restricting the *attacker's* legal options -- the WinChest special-mate
restrictions, originally implemented for compatibility -- turns out to be a sound
fast path: a restriction only removes attacker options, so any mate found under
one is a real mate. Running several restrictions concurrently alongside the
unrestricted search covers problems no single search reaches.

Everything else that was tried failed, and most of it failed by measurement
rather than by argument.

## Where the capability came from

| change | effect |
|---|---|
| restriction portfolio, lanes derived by set cover (8f) | 39/60 → 44/60 on training; 47/60 → 49/60 held out |
| making the tuned settings the defaults (8n) | bare invocation 26/60 → 52/60 |
| portfolio at mate-in-10 (8x) | 13/24 → 18/24 |

Current measured reach. These figures come from evaluation sets used **once**,
for this measurement, and never consulted during development (14):

| measurement | result | 95% CI |
|---|---|---|
| mate-in-8, default configuration, 15 s (200 positions) | **156/200 = 78.0%** | 71.8-83.2 |
| mate-in-10, 30 s, 32 threads, `--direct-depth` (60 positions) | **58/60 = 96.7%** | 88.6-99.1 |
| mate-in-12, same conditions (40 positions) | 33/40 = 82.5% | 68.0-91.3 |
| mate-in-14, same conditions (40 positions) | 30/40 = 75.0% | 59.8-85.8 |
| mate-in-16, same conditions (40 positions) | 28/40 = 70.0% | 54.6-81.9 |
| mate-in-20, same conditions (40 positions) | 22/40 = 55.0% | 39.8-69.3 |

The mate-in-8, 10 and 20 sets were minted from the pinned corpus so that a
reader can rebuild them and check the digest. The mate-in-8 and mate-in-10 sets
draw only from positions no other set uses -- zero overlap -- so they are fresh
evidence as well as reproducible. The mate-in-20 set cannot be: the corpus holds
45 mate-in-20 problems, so it shares 35 of them with a development set and is
reproducible without being independent.

A set that informed development decisions overstates reach. The same
configuration scores 85.5% on a 200-position mate-in-8 set that had been
consulted during development and 78.0% on a fresh draw, and the first figure
sits outside the second's interval (14). Fresh draws are the figures quoted
here.

The DFPN default route is worth 54/60 against the depth-first route's 37/60 at
mate-in-10 on a 60-position set: seventeen positions gained, none lost.

Every certificate at every depth verified independently. The decline with depth is
gradual rather than a wall: this engine is not confined to the shallow end of
matetrack.

### Against Chest 3.19

The program this one reimplements. Same machine, same positions, same 2 GB table,
same 30-second cap, both single-threaded for the fair comparison:

| depth | Chest 3.19 | mateprover, one thread | mateprover, shipped default |
|---|---|---|---|
| mate-8 | 39/40, mean 4.3 s | **40/40, mean 1.0 s** | 40/40, mean 1.2 s |
| mate-10 | 17/40, mean 21.2 s | **37/40, mean 3.3 s** | 37/40, mean 3.3 s |
| mate-12 | 8/40, mean 26.3 s | **33/40, mean 6.4 s** | 33/40, mean 6.4 s |

At mate-in-8 the two are close in reach and mateprover is about four times faster. The
gap widens with depth: at mate-in-12 mateprover solves four times as many positions.
Mean times are censored by the 30-second cap, so they understate the speed
difference wherever Chest times out.

On the positions **both** engines solve -- the fair way to compare speed, since it
excludes everything Chest cannot do at all and therefore understates the gap:

| depth | paired positions | time, Chest → mateprover | nodes, Chest → mateprover |
|---|---|---|---|
| mate-8 | 39 | 3.61 s → 1.02 s (3.6x mean, 20x median) | 5.20M → 0.74M (7.1x mean, 25x median) |
| mate-10 | 17 | 9.40 s → 0.60 s (15.8x mean, 16x median) | 17.30M → 0.44M (39.8x mean, 38x median) |

Mean and median differ because a few slow positions dominate the mean; the median
is the typical case. Nodes-to-proof is the hardware-independent measure, and it
is where the difference is largest: at mate-in-10 mateprover proves the same mates
having examined about 2.5% of the positions Chest examines.

Two things that comparison does not capture. mateprover emits a machine-checkable
certificate for every proof and Chest does not, so the results are not merely
faster but independently checkable. And the parallel columns are nearly identical
to the single-thread ones because these positions resolve in seconds -- threads
matter at the budgets where they do not (8d).

The portfolio's contribution at mate-in-10 is **+15 positions of 60, losing
none** -- twenty-five points of solve rate from the one idea this engine has that
is not a conventional search technique.

### The stalemate and selfmate goals against Chest 3.19

60 composed problems a goal, 20 s cap, **run sequentially** -- one position at a
time, because a three-at-once harness puts 30 threads of a nine-lane portfolio
against 3 of a single-threaded engine on 16 cores and measures itself rather
than the engines. **Chest is given 2048 MB** against mateprover's shipped default
of 256 MB a lane, so Chest's one search has eight times the table of any single
mateprover lane:

| | Chest 3.19 | mateprover | shared positions | only Chest |
|---|---|---|---|---|
| stalemate, depth 10-16 | 31/60 | **54/60** | 2.23x total, 1.15x median | **1** |
| selfmate, s#5-s#10 | 35/60 | **49/60** | 4.71x total, 6.42x median | **0** |

**The same comparison at an equal 256 MB TOTAL** instead, which is the figure a
sceptic should be handed: stalemate 47/60 against 30/60 and selfmate 49/60
against 35/60, with three positions going to Chest rather than one. Nine lanes
sharing a total cannot each hold the whole of it, so an equal total does not ask
the two engines the same question -- but it is the stricter number and it is
reported rather than buried. Section 57 has the arithmetic.

**Where mateprover is still behind, stated exactly.** One stalemate position is
solved only by Chest (`8/p7/8/1k1K4/8/8/6P1/8`, depth 13). And "faster on the
median" is not "faster everywhere": mateprover is slower on 14 of 30 shared
stalemate positions and 5 of 35 shared selfmate positions. The stalemate margins
are small in absolute terms -- the worst is 1.45 s against 3.94 s -- but the
selfmate list contains one genuine outlier at 0.40 s against 13.90 s, the
position that needs the route lane's split to be reached at all.

#### The same comparison at MATCHED CLAIMS, which is the one to believe

Every figure above gives mateprover `--direct-depth`: prove a solution **within**
N. Chest does more than that: it proves the SHORTEST solution, establishing
minimal depth for interior sub-results as well as for the whole. So the tables
above compare two different questions, and the easier one is mateprover's.

Re-run with `--iterative-depth`, which is mateprover making the same claim Chest
makes:

| | Chest 3.19 | mateprover | shared positions | only Chest |
|---|---|---|---|---|
| stalemate, depth 10-16 | 33/60 | **46/60** | 1.41x total, **1.07x median** | 2 |
| selfmate, s#5-s#10 | 35/60 | **45/60** | 1.22x total, **1.09x median** | 2 |

The reach advantage survives; the speed advantage very largely does not. On
selfmate a **6.42x** median becomes **1.09x** once mateprover has to prove
minimality too, and each goal gains a position that only Chest solves. Roughly
five sixths of the headline selfmate speed margin was the weaker claim, not a
faster search.

Which number is "fair" depends on the question. `--direct-depth` is a legitimate
mode and the right one when any proof of a bound will do. But a reader comparing
two solvers is entitled to the matched-claims row, so it is stated here next to
the other rather than left to be discovered.

### The cooperative and inverted goals against Chest 3.19

All six of Chest's job types are implemented. Composed problems from YACPDB,
whose stipulation is the composer's ground truth, sequential, Chest given
2048 MB:

| | Chest 3.19 | mateprover | shared positions | only Chest |
|---|---|---|---|---|
| selfstalemate, s=2..4 | 23/35 | 23/35 | 2.01x total, 1.45x median | **0** |
| helpstalemate, h=2..4 | 45/60 | **47/60** | 1.00x total, 1.25x median | **0** |
| helpmate, h#2..4 | **53/60** | 49/60 | 0.50x total, 0.69x median | **4** |

**Helpmate is a loss.** Chest solves four h#4 positions this engine does not and
is faster on 33 of 49 shared positions. Pruning the final ply to candidate
moves -- only a checking move can mate, only a non-checking one can stalemate --
cut total time from 40.7 s to 33.6 s and moved coverage not at all. That is the
useful negative result: the gap is structural, not a constant factor.

The likely reason is visible in the design. Every other goal gets a portfolio and
a root split; the cooperative search is a single-threaded depth-first walk. A
help node is a pure disjunction, which makes it the most trivially parallel
search in the program and currently the only one exploiting nothing.

Correctness was established before any of this was measured, and independently of
the engine: python-chess brute forces every cooperative sequence for a set of
small positions, giving 114 positives and **240 negatives**. The negatives carry
the weight -- an engine answering "yes" to everything passes a positives-only
suite, and an inverted terminal predicate produces exactly that. The engine
agreed on all 354, and every certificate it emitted verified independently.

### All six goals at matched claims: the figures to believe

Everything above this section gives mateprover `--direct-depth`. This table does
not. Both engines prove the SHORTEST solution, one position at a time, Chest with
2048 MB against mateprover's 256 MB a lane, **best of three trials per position**
so that a single unlucky run cannot decide a comparison at one-second scales:

| goal | corpus | Chest | mateprover | only Chest | only mateprover |
|---|---|---|---|---|---|
| mate-in-8 | **all 200** | 146 | **167** | 9 | 30 |
| mate-in-10 | **all 60** | 20 | **52** | 2 | 34 |
| stalemate | **all 792** | 725 | **761** | 1 | 37 |
| helpmate | **all 546** | 491 | **513** | 2 | 24 |
| helpstalemate | **all 431** | 308 | **363** | **0** | 55 |
| selfmate | **all 903** | 407 | **643** | 19 | 255 |
| selfstalemate | **all 76** | 49 | **52** | 1 | 4 |

Every row is measured at **engine defaults on both sides**. That matters for
mateprover's `-M`: an explicit `-M` is the budget for every table at once,
while unset it is 256 MB *per table*, and a cooperative search runs nine lanes.
Passing `-M 256` in the belief that it is the default costs 12.8x on a measured
position and turns the helpmate row into a loss (480 against Chest's 491). See
`docs/ARCHITECTURE.md` §70.

**10 s a position**, mateprover against Chest on 2048 MB, both proving the
SHORTEST solution, scored on presence rather than on matching the stipulated
depth. Every row is a whole corpus. Stalemate is the externally sourced union of
`stalemate_pdb` and `stalemate_yacpdb`, 792 distinct after removing 47
duplicates and the one position already tagged illegal. The selfmate row is a
single trial measured on 2026-09-03 (`WinChest.exe` at 2048 MB, mateprover at
its defaults); Chest refused 15 of its positions outright, which is evidence
about the corpus rather than about either engine.

Speed, on the positions **both** engines solved -- a ratio that includes a timeout
is comparing a number against a cap:

| goal | paired | total | median per position |
|---|---|---|---|
| selfmate | 388 | 3.02x | **3.5x** |
| mate-in-8 | 137 | 2.50x | **8.7x** |
| mate-in-10 | 18 | 6.09x | **136.3x** |
| stalemate | 724 | 2.44x | **7.2x** |
| helpmate | 489 | 1.83x | 3.4x |
| helpstalemate | 308 | 6.41x | **16.3x** |
| selfstalemate | 48 | 2.34x | **12.8x** |

The median and the total say different things and both are true. The median is
the typical position, usually easy, where mateprover is near-instant; the total
is dominated by the hardest positions both engines solve, and it is the honest
aggregate. mate-in-10's median rests on only 18 shared positions, because Chest
solves 20 of 60 — quote it with that in mind.


Every row is a whole corpus rather than a sample, and that is not cosmetic:
on every goal where both were measured, a draw of 30 to 60 positions disagreed
with the corpus. Mate-in-8 reads as a 4-position loss on a 30-position draw and
is a 21-position win over 200. Helpmate reads as a 3-position loss over 40 and
is a 22-position win over 546. Small draws cannot resolve margins of this size.

**The DFPN preconditioner stays on for sparse selfmates.** `--dfpn-min-men N`
skips it below N men, and on the 377 selfmates of nine men or fewer -- the whole
population the gate can affect, since it is checked at the root -- a gate of 9
solves 185 against the default's 232: 12 gained, 59 lost, net -47 at 20 s a
position. The twelve are real, so the effect a single observation pointed at
exists; material count simply cannot tell those twelve from the 220 that want the
preconditioner kept. See `docs/ARCHITECTURE.md` §68.

**Neither engine can reach 546/546, and the corpus is why.**
`r3k3/8/8/2K5/1P6/8/8/8 b - -` is recorded as h#4. It is not: an exhaustive
python-chess search, independent of this engine, puts the shortest cooperative
mate at h#5, and mateprover REFUSES the h#4 in 0.037 s rather than timing out. A
definitive refusal and a timeout are different events that a solve-rate percentage
renders identically, so any cooperative rate quoted here is a rate against a
corpus with at least one known-wrong entry. The helpstalemate corpus additionally
carries 4 positions of 431 with more than one king a side -- fairy problems that
orthodox chess cannot answer at all.

**Mate-in-8 is the narrowest adversarial margin**, and it has to be quoted
from the whole 200-position set: 167 against 146, with 9 positions only Chest
solves and 30 only mateprover solves. The nine are a real class; no
configuration tried reaches more than two of them.

Depth is what separates the two directmate rows. At mate-in-10 the margin
widens to 52 against 20 with two positions only Chest solves, because the
portfolio's advantage arrives with depth, while at shallow depth Chest's
iterative deepening at every recursive level is competitive with what this
engine does.

The selfstalemate `0.72x median` is noise, not a finding: those positions resolve
in 0.00-0.01 s, where the numbers are timer granularity.

### Against Matefish: parity on finding, and why that matters

Every comparison above is against Chest 3.19 -- a program whose copyright is
1994 and whose 3.16 release is dated 16 June 1999. Beating it is worth
reporting, but a reader is entitled to ask what happens against something
modern. Matefish is a proof-number-search mate solver, architecturally the
closest thing to this engine that exists.

**Claims have to be matched first, and by default they are not.** Probed over
nine cases, `go mate N` on a position whose shortest mate is 2, 3 or 4 returns
`score mate N` for N = true, true+5 and true+12 every time. Matefish echoes the
bound. It answers "is there a mate within N" and never "the shortest mate is
N", so it is comparable to `--direct-depth` and has no counterpart at all for
`--iterative-depth`.

60 positions, d8-d16, 10 s per engine per position, single-threaded, idle
machine. Matefish's claims are **verified** by re-proving them rather than
counted, because this family of engines over-claims:

| band | n | mateprover `--direct-depth` | matefish claimed | matefish **verified** |
|---|---|---|---|---|
| d8 | 12 | 10 | 10 | 10 |
| d10 | 12 | 10 | 11 | 10 |
| d12 | 12 | 10 | 10 | 9 |
| d14 | 12 | 7 | 8 | 6 |
| d16 | 12 | 8 | 7 | 7 |
| **total** | **60** | **45** | 46 | **42** |

Paired, matefish is **+1/-4** against mateprover: five discordant pairs, sign
test **p = 0.375**. **This is parity. mateprover does not beat Matefish at
finding, and this document does not claim it does.** Four of the 46 claims did
not verify within the budget, which means unconfirmed rather than false.

Speed is a different answer. On the 41 positions both solved, mateprover was
faster on **32 of 41, p = 0.0004**, median **1.49x** and mean 2.13x. Process
startup is about 5 ms for both engines and was checked explicitly, so this is
search rather than launch overhead.

Minimality has no contest, because there is no opponent:

| band | n | mateprover `--iterative-depth` | matefish |
|---|---|---|---|
| **total** | **60** | **41** | **n/a** |

Not zero -- **n/a**. Proving that no shorter mate exists is a question Matefish
cannot be asked.

**Matefish was given 4 GB** for its proof-number table against mateprover's
256 MB per-table default, deliberately more than it needs, so that no result
here can be attributed to under-resourcing it. That matters more than it
sounds: `ProofNumberSearch` defaults to **false**, and the `PNS Hash` table is
**separate from `Hash`** and defaults to 32 MB, at which Matefish abandons a
d14 search in 0.20 s. Benchmarking it at its defaults measures a crippled
engine and produces a flattering, wrong result.

**So the honest summary is narrower than the Chest tables alone suggest.**
Against a 1999-era specialist this engine wins broadly. Against a modern
proof-number solver it is **level on finding**, **significantly faster**, needs
**a fraction of the memory**, and does two things its rival cannot do at any
budget: prove minimality, and emit a certificate a third party can re-derive.
Those last two are the claim worth making.

### The composition features

Everything above answers the prover's question. These answer the problemist's.

| feature | mateprover | Chest 3.19 |
|---|---|---|
| duals at the root, sound/cooked verdict | `--all-solutions` | always on, cannot be suppressed |
| solution tree | `-L`, printed from the certificate | `-L` |
| short algebraic | `-S`, verified against python-chess | `-S` |
| refutation table | with `-L` | default, `-r` suppresses |
| successor analysis | `-x` | `-x` |
| legality check only | `-c` | `-c` |
| machine-checkable certificate | **yes** | no |

Dual counts are checked against python-chess: 1 key on a composed mate-in-2, 18
on K+Q against a bare king, identical move sets. Algebraic notation is checked
move for move -- 3551 moves over 150 positions from a random walk, zero
mismatches.

Dual enumeration forces an unrestricted search, and that is a soundness
requirement rather than a default. A restriction removes attacker options, which
is sound for PROVING and unsound for COUNTING: the removed moves are exactly the
candidates for a second solution, so a restricted enumeration undercounts duals
and would report a cooked problem as sound.

**The endgame gap, characterised.** The one stalemate position Chest solves and
this engine did not is solvable after all -- 281 s at depth 13 with 2048 MB. It
is time-bound and not memory-bound: eight times the table changed nothing at
60 s, five times the clock solved it. Against Chest's ~20 s that is about a
factor of fourteen on tiny-material endgames.

The obvious fix was measured and **refuted**: preferring depth-first when
material is sparse loses positions, because DFPN wins every piece-count bucket
including the sparse one (41/47 against 38/47 at five pieces or fewer). Section
59 has the table. The remaining candidate is retrograde analysis, scoped there
and not implemented.

**Mate-in-8 is budget-limited.** Measured on a 200-position evaluation set
with the depth-first route rather than the DFPN default (29), so the shape is
the claim here, not the absolute numbers:

| budget | solved | 95% CI |
|---|---|---|
| 15 s | 160/200 = 80.0% | 73.9-85.0 |
| 60 s | 181/200 = 90.5% | 85.6-93.8 |
| 240 s | **192/200 = 96.0%** | 92.3-98.0 |

Sixteen times the budget converts 80% into 96%. Eight positions resist 240 s.
A 60-position development set on which all 60 fell at 300 s suggests that
nothing at this depth is out of reach; the 200-position set shows otherwise
(15).

**Mate-in-10 is not budget-limited.** Four times the time buys nothing there, and
four times the memory buys nothing; only the portfolio buys reach (8x). The two
depths behave in opposite ways and each result describes only its own depth.

### Why development sets are not quoted

A 60-position mate-in-8 set that informed about ten promote-or-reject decisions
over the course of development scores **52/60 = 86.7%**. The same configuration
on 200 positions that had informed none scores 79.5%, and the first figure sits
*outside* the second's interval. Nothing was tuned against that set directly;
consulting it repeatedly was enough (14).

The effect scales with use. At mate-in-10 a set consulted two or three times
gives 75% and a fresh one 73.3%. That is why every figure in this document
comes from a set used once.

### How Chest was configured, and what it is

Two facts a reader is entitled to have without having to ask.

**Chest 3.19 is an old program.** Its copyright is 1994 (Heiner Marxen, Holger
Pause, Thomas Rakovsky) and its own documentation dates version 3.16 to 16 June
1999. The tables in this document drive the solver itself, `WinChest.exe`, with
a job on stdin (the `ChestUCI.exe` wrapper is a UCI front end for GUIs and is
not what answered here); the wrapper is recent, the solver
underneath is not. It remains a serious and well-regarded special-mate solver,
and the margins above are real -- but "faster than Chest" should be read as
"faster than a mature 1999 specialist", not as a claim about the state of the
art. For that, see the Matefish comparison above, which is a much narrower
result.

**Chest ran with its endgame databases disabled** -- `UseDatabase = false`, and
the `EgtbPath` entries in its `.ini` point at directories that do not exist on
the measurement machine. This is defensible and not an accident: mateprover has
no tablebase support either, so the condition is matched, and this project
measured that even the full 7-man set reaches only about 1% of proof nodes on
this kind of position (composed problems mostly carry too many pieces for a
tablebase to apply). It is disclosed because "did you enable Chest's
databases?" is the first question anyone familiar with it will ask, and the
answer should not have to be reconstructed from a configuration file.

Chest was given 2048 MB throughout; mateprover ran at its own defaults.

### Checking these numbers yourself

The held-out position sets ship with the engine, and

```
python tools/reproduce_results.py --engine build/mateprover
```

re-runs the measurements above and prints what it gets beside what is claimed.
`--quick` gives an indicative run in a few minutes. The positions are in
`benchmarks/`, and were held out from all tuning -- the portfolio was derived on
a disjoint training set, and these were spent only on promotion decisions.

`--deterministic` runs a different comparison: sequential, with node budgets
instead of a clock, on the development sets. The numbers are lower, because the
configuration is one thread with no portfolio, and they are **not** comparable to
the figures above. What they are is exactly reproducible -- the same on every
machine and every run:

| measurement (sequential, equal node budget) | solved |
|---|---|
| mate-8 dev set, depth-first, 2M nodes | 10/60 |
| mate-8 dev set, dfpn, 2M nodes | 17/60 |
| mate-10 dev set, depth-first, `--direct-depth`, 4M nodes | 4/24 |
| **mate-10 dev set, dfpn, `--direct-depth`, 4M nodes** | **18/24** |

The last pair is the clearest statement of what the repaired DFPN route is worth
where its cost is not being charged: four and a half times the reach at an equal
node budget.

### If you know the depth, say so

The default proves "the shortest mate is N" by iterative deepening, which means
searching every depth from 1. `--direct-depth` proves "a mate within N" by
searching N directly. It gives up minimality and buys reach: on a development set
the default configuration solves 52/60 where `--direct-depth` reaches 59/60.

Both mate-in-10 figures above use it, which is why they are labelled. If you
already know the mate distance -- from a problem's stipulation, or from an EPD
token -- there is no reason to pay for rediscovering it.

### The finder lane, and the control that bounds it

`tools/finder_lane.py` has an external engine propose a mate and verifies the
proposal with `--direct-depth`, reporting only what checks out. It is sound by
construction and its certificates are real. **It does not improve coverage.**

The lane is scored with `--direct-depth`, which asks whether a mate exists
within N. Scoring it against an `--iterative-depth` baseline, which proves the
SHORTEST mate, would move two variables at once and credit the cost of
minimality to the proposer. The control separates them: same 60 positions,
same seed, same 20M nodes, same `--no-portfolio`, only the mode changed:

| | solved |
|---|---|
| `--iterative-depth --no-portfolio` | 7/60 |
| `--direct-depth --no-portfolio` (the question the lane answers) | **36/60** |
| lane composite: iterative baseline plus proposer plus verification | 31/60 |

**Dropping minimality alone is worth +29.** The composite scores 31 where the
engine asking directly scores 36, and it spends 44M nodes (20M proving, 20M
finding, 4M verifying) against the control's 20M.

Measured with the right baseline -- `--direct-depth` at full budget, a proposer
consulted only on the 24 positions it cannot do, verification at the same
budget:

| | |
|---|---|
| `--direct-depth` alone | 36/60 |
| proposer claimed on | 5 of the 24 missed |
| **verified** | **0** |
| **lane total** | **36/60 (+0)** |

The proposer was Matefish with a 4 GB proof-number table -- the best of the
three measured, and better resourced than the engine it was helping. Zero of its
claims survived verification.

**The lane adds nothing.** It is retained because it is correct and tested, and
because verifying an untrusted claim is a useful primitive, but it is not a
coverage result. To find and certify a mate, `--direct-depth` alone is faster
and reaches further.

The control is recorded because a comparison between two configurations that
differ in two ways is the most common way a measurement of this kind goes
wrong, and this one is a clean example of how to separate the variables.

## What was tried and rejected

Each of these was implemented or measured, not merely considered.

| idea | result | section |
|---|---|---|
| native DFPN route **as the default** | **promoted**: 90.0% against 61.7% at mate-in-10 on 60 fresh positions, gaining 17 and losing none. Rejected twice before, while its implementation was broken | 21, 23, 27, 29 |
| DFPN or shallow-fast as extra lanes | add nothing the portfolio does not already reach | 8e |
| compound restrictions (`K2`+`R2` etc.) | +2 on training, **exactly zero** at the operating point | 8g |
| bitboard board representation | removing 57% of `make_move` calls bought 4%; no concentrated hotspot exists | 8i, 8k |
| pin- and checker-based legality | ceiling of 15-20% of node time for a rewrite with false-mate risk | 8k |
| reducing allocator traffic | 28.2 GB → 12.7 GB churn changed the node rate by −1.7% | 8j |
| bigger transposition table | eliminating eviction entirely moves the hit rate 15.6% → 16.6% | 8l |
| refutation hints | 4-5% hit rate, no ordering gain, −9% throughput | 8m |
| depth-aware portfolio | per-lane strength differs sharply by depth; the lane *set* does not | 8y |

The pattern is consistent: **work-reduction did not become time-reduction**, four
separate times, because no single stage of a node dominates -- generation 25%,
legality 43%, scoring and list-building 32% (8k). Constant-factor work cannot
reach the 4-20× that coverage would require.

## Defects found, and how

Most were found by testing a *claim* rather than the code implementing it.

| defect | found by |
|---|---|
| false mates from castling rights revoked by captured piece type | perft against reference counts |
| castling generated with no rook on the corner | malformed-input probing |
| illegal positions answered (`dm 1` for eight kings) | adversarial input testing |
| a stalemate accepted as a forced mate by the verifier | writing the proof-format specification (8q) |
| every genuine disproof reported as a timeout | writing the output-format specification (8r) |
| crash losing a whole batch, on default threads plus one flag | stress-testing the abort invariant (8t) |
| shipped defaults were the untuned ones, halving reach | gating documented defaults against real ones (8s) |
| the shipped corpus searched nothing when piped in | running the artefact as shipped (8o) |

Two of those were introduced by earlier iterations of this same work and caught
only because the contract was later written down. That is the strongest argument
here for specifications: a format with one implementation has no specification,
only behaviour.

## How far to trust this

- Every proof is a machine-checkable certificate, verified by a separate program
  sharing no code with the engine (`tools/verify_proof.py`), specified in
  `PROOF_FORMAT.md`. The test suite forges certificates six ways and requires
  each to be rejected.
- 599 automated checks, including perft, negative controls, restriction
  soundness, the abort invariant under stress, order and batching independence,
  and the CLI contract.
- Where a gate could not be shown to discriminate, that is stated rather than
  implied: the thread-count invariance check does not catch an injected
  scheduling fault, because the property it tests has no observable consequence
  on any corpus tried (8v).

## If you extend this

Both evaluation sets are spent. Any future change that could affect reach needs a
set minted *before* the work starts, using `tools/mint_eval_set.py`, and measured
once when it finishes; `benchmarks/README.md` states the protocol and
`benchmarks/MANIFEST.json` tracks which sets remain unused. The development sets
are still the right tool for deciding whether one build beats another -- that use
survives repetition; quoting an absolute reach figure does not.

## What would actually move the needle

Nothing on the original backlog. Every item is implemented, measured and
rejected, or found to already exist. The one axis that has ever produced
capability -- changing *which problem* is searched rather than how fast the same
problem is searched -- is saturated at eight lanes over the restriction set the
engine implements.

Since that was written the engine gained three orthogonal **variant rules**
(x-check, x-capture, x-escape), which are new problems rather than faster
searches of the old one, and a set of tools that search for improvements
automatically (118, 119). The tools have produced no shipped improvement and two
useful findings about measurement: a fitness function that carried no signal
because every position hit its budget and scored the cap, and a candidate pruning
theorem that appeared to save 48% of all nodes and loses mates 4,034 times. The
axis above is unchanged.

A new restriction family was the most promising of those, and it looks
unlikely. Comparing the positions the engine cannot reach against the ones it can
shows **no structural difference at all** -- not in material, not in king
confinement, not in tactical shape. They differ only in size: about 1.5x the
defender branching, which compounds to roughly twelve times the tree over eight
plies, matching the sixteen-times budget the curve above requires (16). A
restriction needs something to key on, and there is nothing.

Endgame tablebase termination was the other candidate, and it is also measured
and rejected (17). Walking 108,000 nodes of real certificates, a shippable 5-man
tablebase reaches **1.01%** of proof nodes and the full 18 TB 7-man set reaches
5.02% -- and those nodes sit at mean ply 11.5 of a 15-ply proof, near the leaves
where the subtree beneath them is already almost free. The value of an
early-termination oracle is set by where its hits land, not how many there are.

Parallelising the DFPN search itself looked like the largest remaining lever by
resource argument -- it is 86-99% of the work and a single position uses about a
quarter of a 32-core machine. It is measured and rejected in every form the
engine could take, on the exchange rate between speed and positions rather than
on any implementation difficulty.

A perfect Kx speedup is a K-times larger node budget in the same wall clock, and
that curve is flat: 22, 24, 24, 25, 26 solved of forty at one, two, four, eight
and sixteen times the budget. **One position per doubling of speed** (43). So
shared-tree df-pn at a realistic 2-5x buys one or two positions of forty for two
to four weeks, against a restriction portfolio that already delivers five or six.
Where DFPN is 86% of the work, Amdahl caps the whole idea at 7.1x -- under three
doublings -- at infinite hardware.

The cheap portfolio form was rejected first, on its own oracle: six
differently-tuned DFPN searches solve nearly the same positions, so picking the
best per position gains two of forty (41). The two experiments agree on the
exchange rate, which is the useful part -- that six-way diversity ceiling is
worth about the same as an eight-times speedup.

**Root splitting was reported inert here twice, and both reports were wrong.**
The first measured a route the split had never been wired into; the second
measured it underneath a proof-number preconditioner that was consuming 96% of
the wall clock single-threaded, so the split was scaling a twenty-fifth of the
run. With both fixed it is worth **9.0x on 24 threads** on a depth-7
capture-quota search (109-115). The argument attached to those numbers -- "a
proof must refute every sibling, so dividing the siblings between threads removes
no work" -- was exactly backwards: refuting every sibling is precisely what makes
the work divisible, and it is proving that does not divide, because the first
sibling that succeeds ends the node.

Two thirds of the eventual gain came from outside the search entirely. Standing
the preconditioner down under a live variant rule was one; freeing the shared
transposition table concurrently was the other, and at a 4 GB budget that was
**44% of a deep run** spent returning memory after the verdict was already known.
Both had been invisible because the instrument that would have shown the second
-- an eviction counter -- was reading a table the search never used, and reported
zero at every memory size while node counts moved 60%.

The exchange rate below is unaffected, and it is the part that matters: 9.0x is
about three positions of forty at mate-in-8, not a depth notch.

What remains genuinely unexamined is a search that reasons about *why* a defence
fails rather than enumerating that it does. That is a different engine, not an
increment to this one, and this document should not pretend otherwise.

One thing the frontier analysis did explain: the defending king already has
essentially no mobility in this corpus -- 0.6 legal king moves on average -- which
is why `KingSquares` restrictions are the strongest lanes at both depths. They
cost almost nothing and prune every attacker move that would free the king. The
set-cover derivation found that without anyone realising why it worked.

## Unified comparison against Chest 3.19

One protocol for every goal, which the previous figures did not have: **5 s and
2 GB a position for each engine**, full corpora, same machine, same session. And
since the harness hardening of section 90, every row below carries a
**measurement identity** and a **result fingerprint**, recorded in
`docs/measurement_ledger.jsonl` — a hash over the corpus digest, goal, depth
bound, both budgets and both engine digests, and a second hash over what was
found. These are the first published numbers here that can be told apart from a
different measurement by anything other than the heading above them.

| goal (corpus size) | MateProver | Chest | only MP | only Chest | Chest refused |
| --- | ---: | ---: | ---: | ---: | ---: |
| mate (d8, 200) | **158** | 126 | 42 | 10 | 0 |
| mate (d10, 60) | **49** | 17 | 34 | 2 | 0 |
| stalemate (792) | **756** | 720 | 38 | 2 | 25 |
| selfstalemate (76) | **49** | 48 | 2 | 1 | 14 |
| helpmate (546) | **513** | 482 | 31 | 0 | 14 |
| helpstalemate (431) | **353** | 296 | 57 | 0 | 9 |
| selfmate (903) | **589** | 351 | 261 | 23 | 11 |
| **total (3,008)** | **2,467** | **2,040** | **465** | **38** | **73** |

Speed, on the positions both engines solve:

| goal | total | median per position |
| --- | ---: | ---: |
| mate d8 | 4.42x | 11.5x |
| mate d10 | 3.74x | **82.5x** |
| stalemate | 2.23x | 7.4x |
| selfstalemate | 6.73x | 14.6x |
| helpmate | 3.30x | 5.3x |
| helpstalemate | 6.05x | 15.8x |
| selfmate | 2.21x | 5.5x |

Ahead on coverage on all seven, ahead on speed on all seven, and 465 exclusive
wins against 38.

**"Chest refused" counts positions where Chest returns a definitive "no
solution".** That is evidence about the CORPUS rather than about either engine,
and 73 such positions want the same two-prover adjudication that produced
`KNOWN_BAD.jsonl`.

Two goals — helpmate and helpstalemate — have **zero** positions Chest solves
and MateProver does not.

The medians exceed the totals nearly everywhere because the totals are dominated
by a few positions near the budget where both engines spend all of it; the median
is the better description of ordinary behaviour.

Selfmate is the widest margin of the seven: 589 to 351, with 261 exclusive
wins against 23. Both engines score higher under the 10 s protocol above; the
margin is of the same size.
