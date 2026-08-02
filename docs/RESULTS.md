# What Was Tried, And What Worked

The architecture document records every finding in the order it was made. This
one states the argument: what this engine's capability actually comes from, what
was measured and rejected, and how far to trust any of it.

Section numbers below point into `E_CHEST_ARCHITECTURE.md`.

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
| mate-in-8, default configuration, 15 s (200 positions) | **159/200 = 79.5%** | 73.4-84.5 |
| mate-in-10 with the portfolio, 30 s, 32 threads, `--direct-depth` (60 positions) | **44/60 = 73.3%** | 61.0-82.9 |
| mate-in-10, same but `--no-portfolio` | 29/60 = 48.3% | 36.2-60.7 |

The portfolio's contribution at mate-in-10 is **+15 positions of 60, losing
none** -- twenty-five points of solve rate from the one idea this engine has that
is not a conventional search technique.

**Mate-in-8 is budget-limited**, measured on the same 200 evaluation positions:

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
python tools/reproduce_results.py --engine build/echest
```

re-runs the measurements above and prints what it gets beside what is claimed.
`--quick` gives an indicative run in a few minutes. The positions are in
`benchmarks/`, and were held out from all tuning -- the portfolio was derived on
a disjoint training set, and these were spent only on promotion decisions.

## What was tried and rejected

Each of these was implemented or measured, not merely considered.

| idea | result | section |
|---|---|---|
| native DFPN route | 0/24 at mate-in-8, even after fixing a flag that had made the comparison unfair | 8e |
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
- 224 automated checks, including perft, negative controls, restriction
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

Extending reach further needs a genuinely different idea: a restriction family
the WinChest set does not contain, endgame tablebase termination, or a search
that reasons about *why* a defence fails rather than enumerating that it does.
Those are open questions, not backlog items, and this document should not
pretend otherwise.
