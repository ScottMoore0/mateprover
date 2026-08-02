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
| mate-in-8, default configuration, 15 s (200 positions) | **171/200 = 85.5%** | 80.0-89.7 |
| mate-in-10, 30 s, 32 threads, `--direct-depth` (60 positions) | **54/60 = 90.0%** | 79.9-95.3 |
| mate-in-10, same with the previous default route | 37/60 = 61.7% | 49.0-72.9 |
| mate-in-12, same conditions (40 positions) | 33/40 = 82.5% | 68.0-91.3 |
| mate-in-14, same conditions (40 positions) | 30/40 = 75.0% | 59.8-85.8 |
| mate-in-16, same conditions (40 positions) | 28/40 = 70.0% | 54.6-81.9 |
| mate-in-20, same conditions (40 positions) | 23/40 = 57.5% | 42.2-71.5 |

Every certificate at every depth verified independently. The decline with depth is
gradual rather than a wall: this engine is not confined to the shallow end of
matetrack, and earlier versions of this document said it was because nothing had
looked past mate-in-10 (31).

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

**Mate-in-8 is budget-limited.** Measured on a different 200-position evaluation
set, and **with the previous default route**, since it predates DFPN's promotion
(29) -- the shape is the claim here, not the absolute numbers, which are now
higher:

| budget | solved | 95% CI |
|---|---|---|
| 15 s | 160/200 = 80.0% | 73.9-85.0 |
| 60 s | 181/200 = 90.5% | 85.6-93.8 |
| 240 s | **192/200 = 96.0%** | 92.3-98.0 |

Sixteen times the budget converts 80% into 96%. Eight positions resist 240 s, so
the earlier claim that *nothing* at this depth is out of reach -- taken from a
60-position development set where all 60 fell at 300 s -- is stronger than fresh
data supports (15).

**Mate-in-10 is not budget-limited.** Four times the time buys nothing there, and
four times the memory buys nothing; only the portfolio buys reach (8x). The two
depths behave in opposite ways and each result describes only its own depth.

### A caution about the older figures

Earlier versions of this document quoted **52/60 = 86.7%** for mate-in-8. That
came from a 60-position set which, over the course of development, informed about
ten promote-or-reject decisions. Re-measured on 200 positions that had informed
none, the same configuration scores 79.5%, and the old figure sits *outside* the
new interval. Nothing was tuned against that set directly; consulting it
repeatedly was enough (14).

The mate-in-10 figures did not move: a set consulted two or three times gave
75%, and a fresh one gives 73.3%. The size of the effect tracks how often a set
was used.

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
- 299 automated checks, including perft, negative controls, restriction
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

Nothing on the original backlog. Every item is now implemented, measured and
rejected, or found to already exist. The one axis that has ever produced
capability -- changing *which problem* is searched rather than how fast the same
problem is searched -- is saturated at eight lanes over the restriction set the
engine implements.

A new restriction family was the most promising of those, and it now looks
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
quarter of a 32-core machine. It is now measured and rejected in every form the
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

Root splitting, re-measured under DFPN after being found inert under the previous
default route, is still inert: eight threads against one scored 0.96x and 1.04x
on two runs and not one extra position (43). A proof must refute every sibling,
so dividing the siblings between threads removes no work.

What remains genuinely unexamined is a search that reasons about *why* a defence
fails rather than enumerating that it does. That is a different engine, not an
increment to this one, and this document should not pretend otherwise.

One thing the frontier analysis did explain: the defending king already has
essentially no mobility in this corpus -- 0.6 legal king moves on average -- which
is why `KingSquares` restrictions are the strongest lanes at both depths. They
cost almost nothing and prune every attacker move that would free the king. The
set-cover derivation found that without anyone realising why it worked.
