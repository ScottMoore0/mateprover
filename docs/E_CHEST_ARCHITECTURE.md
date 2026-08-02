# E Chest Architecture

## Purpose

E Chest is a from-scratch exact directmate prover designed to exceed the current D line in core single-position speed, hard-position proof speed, and batch/service throughput while reducing the chance of proof errors.

The current D line wins mostly through orchestration: batching, route selection, memory sizing, load balancing, and worker fanout. E aims to improve the core prover itself and then regain or exceed D's orchestration advantages.

> **Start with [RESULTS.md](RESULTS.md)** if you want the argument rather than
> the record. This document is the record: every finding in the order it was
> made, including the ones that went nowhere and why. It is deliberately
> chronological, because several conclusions here were later overturned by
> re-measurement, and the sequence is part of the evidence.

## Contents

Sections are numbered by the order they were written, not the order they
appear, and the numbers are stable: they are cited across commit messages
and by each other, so they are never renumbered. This index is the way to
navigate them.

- [1. Exact Proof Kernel](#1-exact-proof-kernel)
- [2. Modern Board And Move Engine](#2-modern-board-and-move-engine)
- [2b. Move Generation Gate: Perft](#2b-move-generation-gate-perft)
- [3. Context-Safe Proof Table / TT](#3-context-safe-proof-table--tt)
- [3e. The Bound Table Is Retired](#3e-the-bound-table-is-retired)
- [3d. Shared Proof/Disproof Table](#3d-shared-proofdisproof-table)
- [3c. Bounded Tables And Honoured `-M`](#3c-bounded-tables-and-honoured--m)
- [4. Native DFPN / Proof-Number Search](#4-native-dfpn--proof-number-search)
- [5. Proof-Safe Move Ordering](#5-proof-safe-move-ordering)
- [5b. Fused Legality And Ordering](#5b-fused-legality-and-ordering)
- [6. Defender Refutation Memory](#6-defender-refutation-memory)
- [7. Typed Restrictions](#7-typed-restrictions)
- [7b. What The Restriction Options Actually Are](#7b-what-the-restriction-options-actually-are)
- [7c. ChecksOnly Implemented](#7c-checksonly-implemented)
- [7h. Portfolio Parallelism](#7h-portfolio-parallelism)
- [7g. The Restriction Portfolio](#7g-the-restriction-portfolio)
- [7f. ChecksOnly Completed](#7f-checksonly-completed)
- [7d. KingSquares, PieceLimit and MaxMoves Implemented](#7d-kingsquares-piecelimit-and-maxmoves-implemented)
- [7e. ThreatDepth Implemented](#7e-threatdepth-implemented)
- [8. Internal Parallelism](#8-internal-parallelism)
- [8f. The Scaling Cliff](#8f-the-scaling-cliff)
- [8e. Resource Scaling Is Exhausted](#8e-resource-scaling-is-exhausted)
- [8d. Parallelism Saturates At About 16 Threads](#8d-parallelism-saturates-at-about-16-threads)
- [8c. Root Young-Brothers-Wait (measured, not promoted)](#8c-root-young-brothers-wait-measured-not-promoted)
- [8b. Parallel Cost Gate](#8b-parallel-cost-gate)
- [3b. Shared Exact Proof Table](#3b-shared-exact-proof-table)
- [9. Persistent Service Mode](#9-persistent-service-mode)
- [10. Memory And Locality](#10-memory-and-locality)
- [10b. Input Validation And A Castling Bug It Uncovered](#10b-input-validation-and-a-castling-bug-it-uncovered)
- [11. Verification Harness](#11-verification-harness)
- [12. Benchmark And Mining System](#12-benchmark-and-mining-system)
- [4b. Direct-Depth Search](#4b-direct-depth-search)
- [12b. Wall-Clock Budget](#12b-wall-clock-budget)
- [12c. Source Layout](#12c-source-layout)
- [13. CLI Contract](#13-cli-contract)
- [8e. Rejected Routes Are Not Worth A Portfolio Lane (measured, not promoted)](#8e-rejected-routes-are-not-worth-a-portfolio-lane-measured-not-promoted)
- [8f. The Restriction Portfolio, Derived Rather Than Hand-Picked (promoted)](#8f-the-restriction-portfolio-derived-rather-than-hand-picked-promoted)
- [8g. Compound Restrictions (measured, not promoted)](#8g-compound-restrictions-measured-not-promoted)
- [8h. Mate-8 Is Budget-Limited, Not Capability-Limited (diagnostic)](#8h-mate-8-is-budget-limited-not-capability-limited-diagnostic)
- [8i. Legality Without A Child Board, And What It Says About Bitboards (promoted)](#8i-legality-without-a-child-board-and-what-it-says-about-bitboards-promoted)
- [8j. Allocator Traffic Is Not The Bottleneck Either (promoted on resources, not speed)](#8j-allocator-traffic-is-not-the-bottleneck-either-promoted-on-resources-not-speed)
- [8k. Node Cost Is Diffuse, So Micro-Optimisation Cannot Reach The Target](#8k-node-cost-is-diffuse-so-micro-optimisation-cannot-reach-the-target)
- [8l. Transposition Density Is Low, And The Memory Default Was Below The Knee](#8l-transposition-density-is-low-and-the-memory-default-was-below-the-knee)
- [8m. The Defender Side Is Already Tight, And Refutation Hints Cost More Than They Save](#8m-the-defender-side-is-already-tight-and-refutation-hints-cost-more-than-they-save)
- [8n. The Shipped Defaults Were The Untuned Ones (promoted)](#8n-the-shipped-defaults-were-the-untuned-ones-promoted)
- [8o. The Corpus Did Not Round-Trip, And The Registry Measured A Dead Configuration](#8o-the-corpus-did-not-round-trip-and-the-registry-measured-a-dead-configuration)


## Impact-Ordered Architecture

### 1. Exact Proof Kernel

Directmate is represented as an AND/OR problem:

- attacker-to-move nodes are OR nodes;
- defender-to-move nodes are AND nodes;
- a proof succeeds only when every legal defender reply has a continuing proof;
- every returned success must contain a legal representative PV ending in checkmate.
- proof-carrying mode must include every legal defender branch, not just the representative PV.

The proof kernel must be small enough to audit. It is the highest-correctness component and must remain independent of move-ordering or neural guidance.

Current E checkpoint:

- normal benchmark output remains `bm` / `dm` / `pv`;
- `--emit-proof` emits a recursive JSON proof certificate;
- proof JSON is built only when `--emit-proof` is requested, so normal search does not pay certificate allocation cost;
- attacker nodes contain one selected proof move;
- immediate mate leaves use `mate:true`;
- defender nodes contain exactly one branch per legal defender reply;
- `tools/verify_proof.py` independently verifies certificates with python-chess. It ships with the engine, so a reader can check a proof without trusting the prover that produced it.

### 2. Modern Board And Move Engine

The final target is bitboards, compact undo, incremental attacks, pins/check state, and branch-light legal move generation.

The first E checkpoint uses a simpler array board to establish correctness. Bitboard occupancy planes have now been added beside it; the array board remains the source of truth for piece-at-square queries.

#### Stale Rejections Re-Tested After Unification

`--fast-check-score` and `--lazy-defender` were both rejected on measurements taken before the plane representation existed, so both rejections were stale by construction. Re-tested at 8 threads over 4 interleaved trials, together with the proof-hint question left open by the earlier profile:

| probe | geomean vs promoted | outcome |
|---|---:|---|
| `--lazy-defender` | 1.010x | still neutral, rejection stands |
| `--fast-check-score` | 0.081x | **exposed a bug**, see below |
| `--no-proof-hints` | 0.972x | proof hints confirmed worth keeping |

**`--fast-check-score` was a 30x cliff, not a slow option.** In the fused generator `want_scores` was `!cfg.fast_check_score`, so passing the flag returned *unscored* moves; `generate_ordered_moves` then skipped the sort and the entire search ran unordered. A single flag silently disabled move ordering and cost roughly 2700-3100% on every suite.

Now that `move_gives_check_fast` shares this same plane path, "fast" and "exact" check scoring are the identical computation by identical means, so the flag has nothing left to select. Scoring is now unconditional and `--fast-check-score` / `--exact-check-score` are retained only as no-op CLI aliases. Verified: with the flag set, output and node counts are identical to the promoted default and timing matches.

**Proof hints earn their place after all.** The iteration-6 profile measured a 0.7% hint hit rate (9624 of 1296036 probes) and flagged them as possibly not worth their overhead. Removing them is *slower*: `+5.9%` on regression controls, `+4.8%` on the hard holdout, `+1.1%` on negative controls, `-0.1%` on smoke, geomean 0.972x. A low hit rate is not the same as a low value -- when a hint does fire it moves a proven move to the front of an attacker node, which prunes a subtree rather than saving a probe. The open question from that profile is now closed: keep them.

#### One Attack Implementation, Not Two

E carried two independent implementations of the same predicate, "is square X attacked after move M":

- the plane path used by move generation;
- `is_attacked_after_move` plus `attacked_by_slider_after_move` plus `piece_after_move`, 84 lines walking the attack geometry against a virtual mailbox, reached only through `move_gives_check_fast`.

Two implementations of one predicate is the exact shape of hazard that hid the castling-rights soundness bug: a rule fixed in one place stays broken in the other, and no test that exercises only the first will ever notice.

`move_gives_check_fast` now shares the single plane path, and the 84 duplicated lines plus a newly orphaned helper are deleted. The promoted configuration does not call `move_gives_check_fast` at all, so this is behaviour-neutral there -- verified by identical node counts, key moves and PVs against the previous binary on all four suites, 294/294 movegen parity, and all six perft positions still exact.

Its real effect is on the probes that *do* use it, `--fast-check-score` and `--lazy-defender`, which were both rejected on measurements taken before the plane primitive existed.

#### Copy-Free Move Generation

The bitboard slice above returned only 1.018x because it *added* a
representation rather than replacing one. This increment collects the rest by
using the planes to remove the copies they were competing with.

Move generation asks exactly two questions of each child position: is the
mover's king attacked (legality), and is the opponent's king attacked (check
ordering). Neither needs a mailbox, packed TT words, castling rights or side to
move. Both are answered by occupancy planes.

- `planes_after_move` applies a move to occupancy planes alone, mirroring
  `make_move`'s piece movement exactly: source vacated, en-passant victim
  removed, ordinary capture removed, promotion piece substituted, destination
  occupied, castling rook relocated;
- generation now builds a 72-byte plane set with a handful of bit operations
  instead of copying a 184-byte `Board` and running scatter writes through
  `set_square`;
- `--score-mates` still needs `is_checkmate` on a real child board, so it keeps
  the materialising path; every other configuration never builds a child
  `Board` during generation;
- roughly 46M `make_move` calls per hard-suite run are removed. The ~20M copies
  for moves actually searched remain, because recursion needs a real board;
- this is an implementation change only: identical node counts and PVs on all
  four suites, all six perft positions exact, 294/294 movegen parity with
  python-chess.

Measured over 5 interleaved trials:

| suite | seq gain | par8 gain |
|---|---:|---:|
| negative controls | -20.9% | -11.3% |
| regression controls | -26.2% | -21.3% |
| smoke | -25.0% | -16.7% |
| hard holdout | -23.6% | -20.2% |

Geometric mean **1.316x sequential** and **1.212x parallel**. Hard-holdout
throughput rose from 473 to **659 knps** at identical node counts, a 39%
improvement.

This closes the loop opened three increments ago. The lazy-defender measurement
said board copies were not dominant; the bitboard measurement said adding
planes made copies worse; together they said the two costs had to be attacked
at once. Removing the copy that the planes made redundant is what paid.

#### Bitboard Attack Detection

Prompted by the measured finding that attack scanning, not board copying, dominates. `is_attacked` previously walked pawn, knight and king square lists and stepped along slider rays one square at a time.

Current checkpoint:

- `Board` maintains `occ`, `by_color[2]` and `by_type[6]` incrementally in `set_square`, which was already the single mutation choke point for FEN parsing, ordinary moves, en passant, promotion and castling;
- leaper attacks are single mask-and-test operations; slider attacks intersect a precomputed ray mask with occupancy and take the nearest blocker by bit scan, replacing a square-by-square walk;
- bitboard tables are derived from the existing square-list tables at startup, so the two representations cannot disagree;
- ray direction sign is derived from the table rather than assumed, so the nearest blocker is correctly the lowest or highest set bit per direction;
- `lsb_index`/`msb_index` are portable across GCC, Clang and MSVC with a generic fallback, since the CI matrix builds all three;
- `is_attacked` is a pure predicate, so the search is unchanged: identical node counts and PVs on all four suites, all six perft positions still exact, and 294/294 movegen parity against python-chess with zero mismatches.

**The gain was much smaller than the bottleneck finding predicted.** Five interleaved trials at 8 threads: hard holdout `-3.8%`, negative controls `-6.5%`, regression controls `+1.2%` and smoke `+2.4%` (both inside one standard deviation), geometric mean **1.018x**.

The reason is that this slice *added* a representation rather than replacing one. `Board` grew from roughly 112 to 184 bytes, so every `make_move` copy became about 64% more expensive, and `set_square` now maintains the packed TT words *and* three bitboard planes on every write. Cheaper attack queries are paying for more expensive copies and mutations.

Promoted anyway, because it is positive on the suites that matter and it is the prerequisite for collecting the rest: the remaining win needs the redundancy removed -- make/unmake in place instead of copy-on-move, and a single representation rather than `sq[]` plus `packed[]` plus planes. That is the next board increment, and it is now a concrete target rather than a generic aspiration.

Current board checkpoint:

- board state caches both king squares after FEN parsing;
- `make_move` updates the relevant cached king square when a king moves;
- board state also maintains four packed 64-bit board words, updated by the same square-write helper used for FEN parsing, ordinary moves, en passant, promotions, and castling;
- TT key construction now copies those packed words instead of scanning and repacking all 64 squares at every search node;
- check detection uses the cached king square and keeps a full-board fallback scan if the cache is invalid;
- this removes repeated full-board king scans from the common `in_check` path while preserving a defensive correctness fallback;
- knight targets, king targets, pawn-attack origins, and slider rays are precomputed once and reused by attack detection and pseudo-legal move generation;
- this removes repeated coordinate/on-board geometry from the hot path without changing legal move semantics;
- checkmate testing has a separate early-exit legal-reply probe so it does not allocate and materialize all replies when one legal escape is enough;
- move-vector reserve capacity 96 is promoted in the benchmark registry default args after paired smoke, regression-control, and balanced-no-EP cap sweeps showed a small net speed gain with clean validation;
- `--move-reserve-cap N` is a locality-only tuning knob that changes the pseudo-move vector preallocation capacity while preserving identical legal move generation and search semantics; the previous `--move-reserve` path remains equivalent to capacity 64 for A/B checks;
- `--static-pseudo` is an opt-in move-generation locality probe that builds pseudo-legal moves in a fixed stack buffer and falls back to the vector path on overflow;
- `--vector-pseudo` restores the promoted heap-backed pseudo-vector path and remains the default;
- the first stack-buffer probe was correctness-clean on smoke, regression-control, and balanced-no-EP suites, but it was slightly slower than the promoted vector path and is therefore not promoted;
- the checkpoint was validated with python-chess movegen comparisons, proof-tree verification, PV replay, and D-oracle directmate verification.

### 2b. Move Generation Gate: Perft

Directmate proofs are only as trustworthy as move legality. A missing legal defender reply does not produce a slow search, it produces a **false mate**, so movegen needs a gate stronger than the mate corpus itself.

Current E checkpoint:

- `--perft N` prints leaf counts for depths 1..N; `--perft-divide N` prints the per-root-move breakdown, which is the standard tool for bisecting a discrepancy against a reference implementation;
- six standard reference positions are checked in the in-repo test suite, exercising castling rights, en-passant capture and expiry, promotion including under-promotion, and pinned-piece legality;
- **this immediately found a real soundness bug.** Castling rights were being revoked by captured *piece type*: `captured == 'R'` stripped both of White's rights whenever any White rook was captured anywhere. Once promotion exists that is wrong, because capturing a promoted rook on an arbitrary square would strip rights while both original corner rooks stood untouched;
- the failure was localized by divide to `d7c8r` then `d8c8` in the standard position 5: a pawn promotes to a rook on c8, Black's queen captures it, and White silently loses the right to castle kingside. The missing `O-O` was exactly the one absent node;
- rights are now revoked by **square** only. The square tests are already complete: a move *from* a corner covers the rook leaving, and a move *to* a corner covers the rook being captured there, since any other piece occupying that corner implies the right was already gone;
- this class of bug is invisible to the mate suites. Solvedness, mate depth and key move were unchanged on every frozen suite after the fix, because no benchmark position happened to involve a promoted-rook capture that mattered. Roughly four thousand directmate positions did not catch what six perft positions caught immediately;
- perft is therefore a permanent promotion gate, not a one-off check.

### 3. Context-Safe Proof Table / TT

TT keys must include all proof-relevant context:

- board occupancy/pieces;
- side to move;
- mate depth remaining;
- attacker/defender node kind;
- castling rights;
- en-passant square;
- restriction/stipulation context;
- threat/check/null-probe context.

Unsafe cache reuse can create false proofs. Therefore E starts with conservative exact keys before compact high-performance keys are promoted.

Current E checkpoint:

- TT keys are exact packed values, not ad hoc strings;
- the board is packed as 4-bit piece codes, 16 squares per 64-bit word;
- context packs side to move, castling, en-passant, node kind, attacker color, and depth;
- hash collisions are safe because `unordered_map` equality still compares the full key;
- `--tt-reserve N` allows controlled experiments that pre-reserve TT hash buckets before iterative deepening;
- `--keep-iter-tt` is a promoted exact-TT mode that retains entries across iterative-depth passes instead of clearing them before each pass;
- this is proof-safe because depth is part of the TT key, so a shallower proof result cannot satisfy a deeper lookup unless the exact keyed depth matches;
- `--clear-iter-tt` restores the previous per-depth clear behavior for rollback and A/B checks;
- bound reuse is monotonic: a proven entry may satisfy only equal-or-greater requested depth, and if failed-node bounds are enabled a failed entry may refute only equal-or-smaller requested depth;
- the rejection nonetheless stands, for a different reason. On that position the prune removes 8.6% of nodes (268,260 to 245,083) and costs 14.5% more time, because it is paid for with a *second table*: 126k extra probes, 116k extra stores, and a 96k-entry structure competing for cache. Capability is unchanged at 13/24 mate-in-8 and 3/20 mate-in-10;
- the correct summary is "the prune fires and the bookkeeping costs more than the search it saves", which points directly at the shared proof/disproof table: keying by position without depth and storing `max_disproved_depth` and `min_proved_depth` in one entry makes the same prune free, since one probe would answer both questions and the second table would disappear;
- the first guarded positive-bound probe stayed correctness-clean but was not promoted because the balanced no-EP suite slowed down despite smoke/regression average improvements;
- `--profile` emits stderr JSON counters for TT probes, hits, stores, table size, node split, move-list sizes, and ordering/refutation activity, and a collector in the private benchmark harness (not shipped with the engine) stores those rows as case-labelled JSONL;
- this is correctness groundwork for later packed/bucketed TT work, not yet the final high-performance TT design.

### 3e. The Bound Table Is Retired

`--bound-tt` existed to recover the monotone depth implications that a
depth-keyed table threw away. The shared proof/disproof table provides them
directly, so the second table had nothing left to contribute.

It was not merely dead: measured after the shared table landed, `--bound-tt`
produced an **identical node count** (245,083 — it changed nothing) while
costing **25% more time**, because it still allocated a table and performed its
own probes and stores. A flag that can only make the engine slower is a trap,
not an option.

Removed: `BoundEntry`, the bound map, `probe_bound_tt`, `store_bound_tt`, five
counters, nine guard blocks in the search, and the flags `--bound-tt`,
`--exact-tt-only`, `--bound-tt-failures`, `--bound-tt-ok-only`. 121 lines gone
from the proof path, which is the component where less code is worth most.

The `Stats` size guard did its job during this work: removing five counters
tripped the `static_assert` immediately rather than letting the merge silently
drop fields.

### 3d. Shared Proof/Disproof Table

Depth is no longer part of the table key. One entry per position and context carries `max_disproved` (no mate within that depth, hence none within any smaller) and `min_proved` (a mate within that depth, hence within any larger, with the payload). A single probe answers both questions.

Both implications are unconditional and hold for attacker and defender nodes alike, so this is exact. Stores **merge** rather than overwrite, letting one entry accumulate both bounds as the search revisits a position at several depths; for the shared table the read-modify-write happens under the shard lock so concurrent folds cannot lose one. The abort invariant is unchanged -- an aborted search still writes nothing.

Measured on a hard matetrack mate-in-8 position with `--direct-depth`:

| config | nodes | time |
|---|---:|---:|
| exact table only | 268,260 | 0.124 s |
| plus separate bound table | 245,083 (-8.6%) | 0.142 s (+14.5%) |
| **one shared table** | **245,083 (-8.6%)** | **0.110 s (-11.3%)** |

The node count is *identical* to the two-table arrangement, confirming the mechanism is exactly equivalent; the whole difference is bookkeeping. Frozen suites, 3 interleaved trials: regression controls `-2.7%` sequential and `-8.6%` at 8 threads, smoke `-2.5%`/`-6.4%`, hard holdout `-0.4%`/`-7.0%`, negative controls `+0.6%` in both. Gains are larger in parallel because workers searching at different depths now share bounds through one table.

Capability is unchanged at 13/24 mate-in-8 and 3/20 mate-in-10, consistent with the scaling cliff: at these depths no speed improvement of this size moves problems across the line.

This was the highest-stakes change available -- depth in the key is what made `--keep-iter-tt` safe, and an error in the monotone comparison yields false proofs rather than slow searches. Identical `bm` and `dm` on every suite, with certificates still verifying independently, is the evidence that exactness survived.

`--bound-tt` is now redundant: the shared table subsumes it and does the same work for free.

### 3c. Bounded Tables And Honoured `-M`

Until this checkpoint every table was an unbounded `unordered_map`, and `-M` was parsed and discarded. A sufficiently deep problem could grow the table until the process died, and the CLI advertised a memory option it did not implement.

The safety argument for bounding is short. The table is a pure memo of verdicts that are themselves pure functions of the exact key, so an absent entry only means the verdict is recomputed. **Eviction trades time for memory and can never trade correctness**: it cannot manufacture a false proof or a false disproof, only a slower search.

Current E checkpoint:

- `-M N` is honoured as a megabyte budget, converted to an entry ceiling through the documented constant `EST_BYTES_PER_ENTRY = 192`; `-M 0` restores unbounded behaviour;
- this is an **entry ceiling, not a guaranteed RSS limit**. A node-based container cannot give a hard byte bound, so the figure is an estimate covering the 40-byte exact key, the entry header, and per-node plus bucket overhead. Documented as such rather than overclaimed;
- replacement is generation-aged. Entries record the pass in which they were last useful, refreshed on every probe hit, and a shard over capacity first sheds entries that no probe touched during the current pass, then shed to a low-water mark if age alone cannot choose a victim;
- one `BoundedTable` implementation backs both the sequential table and every shard of the shared table, so the two paths cannot drift apart in replacement behaviour;
- the budget is split evenly across shards; hash spreading keeps occupancy close enough that a per-shard cap is a faithful proxy for a global cap without a global counter on the hot path;
- degradation is graceful and was measured on the 40-case hard holdout, where the working set is about 157k entries:

| `-M` | entry ceiling | max table | evictions | avg ms | nodes |
|---:|---:|---:|---:|---:|---:|
| 0 | unbounded | 156836 | 0 | 191.5 | 3342710 |
| 64 | 349525 | 156836 | 0 | 203.0 | 3342710 |
| 8 | 43690 | 43690 | 1398226 | 270.7 | 3500392 |
| 1 | 5461 | 5453 | 3967962 | 326.9 | 4863130 |

- a 28x smaller budget costs 71% more time and 45% more nodes, and changes no answer;
- correctness was verified under eviction pressure, not only at the comfortable default: at `-M 1`, with a 5461-entry ceiling against a 157k working set and roughly 4M evictions, solvedness, mate depth and key move stayed identical to the unbounded run, and proof-tree verification stayed 9/9 on smoke, 11/11 on regression controls, and 0 false proofs on negative controls;
- the promoted default `-M 64` does not bind on any frozen suite, and a paired 3-trial comparison against the previous binary showed no regression in either sequential or parallel mode.

### 4. Native DFPN / Proof-Number Search

DFPN should become a native search mode, not a fallback wrapper. E should have:

- AND/OR proof/disproof numbers;
- DFPN thresholds;
- a DFPN TT;
- PV reconstruction through exact replay;
- deterministic fallbacks for debugging.

Current E checkpoint:

- `--route depth-first` selects the current exact iterative depth-first directmate route, which remains the promoted default;
- `--route shallow-fast` selects an unpromoted exact route that tries direct mate-in-1 and mate-in-2 before falling back to depth-first from the first untried depth for deeper requests;
- `run_route` is now the common dispatch point for future exact routes, so shallow, DFPN-first, defender-refutation, and threat/mating-net routes can be added without changing output acceptance or proof verification;
- `--route dfpn` implements native depth-first proof-number search as a **preconditioner, never an output authority**: it contributes exact disproofs through the audited store helper and proof moves through the ordering-hint table only, so a numeric bug can cost time or a missed proof but cannot manufacture a mate;
- **DFPN is implemented and rejected, but the margin is much smaller than first reported.** The original "at least 10x slower" was a property of an unoptimised implementation. df-pn selects children by proof/disproof number, not by move score -- every child starts at pn=dn=1, so ordering only breaks ties -- yet every node re-entry was paying for a full scoring pass and a stable sort. `dfpn_moves` now skips the sort, keeps the check bit (free from the fused generator), and uses it so the attacker mate scan skips moves that cannot be mate. On the *same* four mate-6/7 positions this took DFPN from 0/4 solved (280 s timeout) to 3/4 in 125.6 s, against depth-first 4/4 in 28.0 s;
- **final verdict: DFPN is rejected on capability, not merely on speed.** At a 5 s budget single-threaded it solves 0/20 mate-in-10 (matching depth-first) and 4/24 mate-in-8 against depth-first 7/24. The wall-clock gap converts directly into fewer problems crossing the budget line;
- `run_dfpn_route` calls `prove_attacker` directly and never enters the parallel root split, so **DFPN is single-threaded regardless of `--threads`**. To match the promoted engine it would have to close the remaining ~1.8x *and* gain parallelism, since the promoted engine reaches 10/24 on 8 threads;
- the trajectory argument was real and is now spent: the gap narrows with depth but never reaches parity inside the range where E solves anything, and below that range the comparison is 0 against 0 and carries no information;
- the gap narrows with depth, measured with the implementation held fixed: **4.5x slower at mate-6/7, about 1.8x at mate-8**. Two points is a trend and not a curve, but it is the direction predicted when the rejection was recorded, and the crossover is plausibly near mate-9/10;
- `--dfpn-epsilon-64 N` adds standard 1+epsilon threshold widening; it made no measurable difference and stays off;
- **the original claim, retained for the record:** On the four hardest deep-corpus positions the exact route solves 4/4 in 26.8 s while DFPN solves 0/4 and times out at 280 s -- at least 10x slower. On easy suites it is ~2x slower;
- it loses for identifiable reasons: E's static move ordering already approximates what DFPN would discover dynamically; DFPN's defining re-descent makes each internal node cost O(branching) per visit where the exact search visits once; depth-bounded directmate has cheap decisive terminal tests that a single pass exploits directly; and the exact table already captures transpositions, so the number table adds a second cache without new information;
- a measurement trap was caught during this work and is worth recording: DFPN node visits did not initially increment `acn`, so the first run appeared to cut smoke from 260,157 nodes to 491. Any DFPN comparison that does not count preconditioner nodes flatters itself by hiding its entire cost;
- unsupported route names are not treated as proof mechanisms; E falls back to the current exact route and reports the unsupported name on stderr.

### 5. Proof-Safe Move Ordering

Move ordering can never remove a legal move. It may only change order.

High-impact ordering signals:

- checking moves;
- mating net/cage moves;
- captures;
- promotions;
- king confinement;
- hash/PV moves;
- killer/history/refutation moves;
- known hard defender replies.

Current E checkpoint:

- existing move scores are computed once per move before stable sorting;
- the previous sort order is preserved, but expensive score recomputation during comparator calls is removed;
- expensive immediate-mate detection is skipped during ordering by default because the attacker loop still checks every candidate exactly before accepting it;
- `--score-mates` restores the older, more expensive mate-first score path for comparison;
- checking-move scoring remains promoted; `--no-check-score` was tested and rejected on the smoke suite because it slowed search substantially;
- delta-based `--fast-check-score` was also tested and rejected because the simpler exact child-board check path was faster on the measured suites;
- `--inplace-order` writes computed scores into the existing move vector and stable-sorts it in place instead of building a separate scored vector and copying moves back;
- the benchmark registry promotes `--inplace-order` after paired smoke, regression-control, and balanced-no-EP runs showed speed gains with clean PV/directmate validation;
- `--scored-vector-order` restores the previous scored-vector ordering path for future reversions and A/B checks;
- `--order-min-size N` is an ordering-volume probe that avoids scoring and sorting small move lists; the promoted default remains `N=2` until a larger threshold proves faster without increasing search enough to lose the gain;
- the first broad skip-small-lists probe, `--order-min-size 16`, was PV-clean but much slower on the smoke suite, so future ordering work should preserve cheap fronting for small lists rather than simply leaving them unordered;
- `--bucket-order` is an implementation probe that keeps the same score values as the promoted in-place ordering path, then emits stable descending score buckets instead of using comparison sort;
- the first bucket-order timing run was PV-clean but slightly slower on the smoke suite, suggesting the expensive part of current ordering is mostly scoring and proof-order quality rather than comparison-sort overhead;
- a temporary ordinary `std::sort` probe was PV-clean but slower than the promoted stable sort on the smoke suite, so no unstable-sort flag is retained;
- this is proof-safe because it changes only move ordering, not legal move generation, proof tests, or pruning.

### 5b. Fused Legality And Ordering

A profile of the 40-case hard holdout found a structural redundancy rather than a hot loop to micro-tune.

Legal move generation builds each child board and asks whether the mover left **their own** king in check. Move-order check scoring then rebuilt the *same* child board to ask whether the **opponent** is now in check. Every ordered move therefore paid two `make_move` board copies where one would serve. On that suite it was 46.3M ordered moves, so tens of millions of redundant ~115-byte board copies.

Current E checkpoint:

- scoring is split into `static_move_terms` (capture, promotion, moving piece -- needs no child board) and `child_move_terms` (check and mate -- needs the child board), shared by both paths so they cannot drift;
- `legal_moves_fused` generates legal moves and their ordering scores in one pass, reusing the child board it already built for the legality test;
- `--fused-order` is promoted and default; `--split-order` is the rollback and A/B control;
- this is an **evaluation-order change only**. The scores produced are identical, so move order and therefore the entire search is unchanged. Verified: identical node counts and identical PVs on smoke, regression controls, negative controls, and the hard holdout;
- measured gains over 3 interleaved trials: hard holdout `-6.7%` sequential and `-7.1%` at 8 threads, regression controls `-4.7%`, negative controls `-4.3%`, smoke `-4.0%` sequential;
- throughput on the hard holdout rose from 452 to 473 knps at identical node counts;
- the fused path is skipped under `--static-pseudo` and `--fast-check-score`, which keep the split path.

#### Lazy Defender Legality: Measured And Not Promoted

The defender-node waste identified above was implemented and measured rather than left as an argument.

`--lazy-defender` orders **pseudo-legal** replies using `move_gives_check_fast`, which answers the check-ordering term by querying the post-move board virtually and so needs no child board at all. Legality is then established only for replies actually reached.

- the ordering predicate is identical to the eager path and the sort is stable, so deleting the illegal moves from the sequence leaves the order of the legal ones unchanged. Node counts and PVs are **identical** on all four suites, confirming the argument empirically;
- a lazy defender node whose loop ends without finding a legal reply is stalemate, not a proof, and is handled exactly like the empty-list case. Getting this wrong would turn stalemate into a false mate;
- it works as designed: **89.6% of defender legality tests are avoided**, 25.0M child boards down to 3.39M, a 7.4x reduction;
- it costs virtual check detection on all 32.7M pseudo moves and sorts 16.7% more moves (54.1M vs 46.3M), because pseudo lists are longer than legal ones;
- sequential gain is real: `-3.4%` to `-5.6%` across suites. **Parallel is neutral**: over 5 interleaved trials at 8 threads the deltas were `+2.2%`, `-4.1%`, `-2.1%`, all within one standard deviation;
- **not promoted**, because the promoted configuration is parallel and it does not improve there. Retained as `chest_E_lazy_defender_probe`, and useful for single-core deployment.

**The important finding is the ratio.** Eliminating 7.4x of defender child-board construction bought only 3-5%. That says the ~115-byte board copy is *not* the dominant cost -- the attack scan is, since `move_gives_check_fast` walks eight slider rays plus the knight, pawn and king tables and costs nearly as much as `make_move` plus `in_check`.

This is direct evidence for prioritising the bitboard/incremental-attack board in section 2 over further copy-avoidance work. Cheap attack queries, not cheap board copies, are what E is short of.

#### Profile Findings Still Open

The same profile identified a larger inefficiency that is **not** yet addressed:

- defender (AND) nodes generate on average 18.6 legal replies but try only **1.07**. Only 5.7% of generated defender moves are ever searched, because most defender nodes are refutations that end at the first surviving reply;
- attacker nodes are far better balanced, trying 87.9% of what they generate;
- the naive fix -- lazy legality with ordering deferred -- is not obviously correct as an optimisation. Ordering is what makes the 1.07 figure so low, and `--no-check-score` was already tested and rejected as substantially slower. Ordering by static terms alone would trade cheap move generation for expensive extra subtree searches;
- a real fix needs ordering signal that does not require the child board, so legality can be evaluated lazily in score order. That is a bitboard/incremental-attack concern, and belongs with section 2's board rewrite;
- proof-hint probes hit only 0.7% of the time on this suite (9624 hits from 1296036 probes) and are worth re-examining on a larger corpus.

### 6. Defender Refutation Memory

The prover should learn which defender replies refute candidate keys and try those replies early in later equivalent contexts. This is exact, handcrafted guidance, not probabilistic proof.

Current E checkpoint:

- `--refutation-hints` enables an ordering-only defender refutation table;
- when a defender reply refutes an attacker move, E records that legal move against the defender-position context without depth;
- on later visits, E moves that hinted reply to the front only if it is still present in the legal move list;
- `--proof-hints` enables the symmetric ordering-only attacker proof table;
- when an attacker move proves a position, E records that legal move against the attacker-position context without depth;
- on later visits, E moves that hinted proof move to the front only if it is still present in the legal move list;
- stale hints cannot remove moves, skip replies, or prove anything by themselves;
- attacker proof hints are promoted through the benchmark registry after clean smoke, regression-control, and balanced-no-EP validation;
- defender refutation hints remain disabled by default until they show a net benefit on the benchmark suites;
- `--ordered-check-shortcut` is promoted through the benchmark registry after clean smoke, regression-control, balanced-no-EP, PV, and proof-tree validation; it relies on already scored attacker move lists: when check scoring is enabled and mate scoring is disabled, moves below the check-score threshold cannot be immediate mates, so E can skip their immediate checkmate test;
- checking candidates under that mode still run the exact "no legal defender reply" test before a mate is claimed, and the shortcut is disabled automatically when the list was not scored; `--no-ordered-check-shortcut` is the rollback path.

### 7. Typed Restrictions

WinChest/Chest options such as `-C`, `-R`, `-K`, `-P`, `-X`, and `-I` must become typed search constraints. They must be part of TT keys and verifier fixtures.

### 7b. What The Restriction Options Actually Are

Section 7 called `-C`, `-R`, `-K`, `-P`, `-X` and `-I` "Chest/WinChest options" that must become typed search constraints. Checking the source settled two things.

**Chest 3.19 accepts none of them.** Its option set is `? 2 A a b c d D f g l L m p r s S t T u U V x y o h E O H M G z Z Q`, and the six are absent from both its usage text and its parser. They are **WinChest extensions**, and this document and the CLI had been mis-attributing them.

Their meanings, from WinChest's own `Options.txt`:

| option | meaning |
|---|---|
| `-C N` | special mate: examine checking moves only |
| `-R N` | special mate: examine threats only |
| `-K N` | special mate: limit defender king mobility |
| `-P N` | special mate: limit the set of moving pieces |
| `-X N` | special mate: limit maximum moves |
| `-I N` | special mate: threat flags |

Each selects a **different problem**, not a tuning knob on the same one. That is why rejecting them is right and why implementing them is a feature rather than a refinement: `-C` restricted to checking attacker moves is a distinct composition genre, not a faster route to the same answer.

The rejection message now names the semantics, so a caller can tell whether they asked for something meaningful or made a typo. Implementing any of them remains open, and would need differential validation against the WinChest binary since the semantics are documented only as one-line summaries.

### 7c. ChecksOnly Implemented

`-C 1` is now implemented: the attacker may play only checking moves, so a solution is a serial-check mate.

Getting there needed the semantics, and the one-line summary in `Options.txt` was actively misleading. Black-box probing of the WinChest binary showed **even values solving and odd values failing**, with node counts falling as the value rose -- which fits no reading of "examine checks only" as a count. The manual resolves it: `ChecksOnly` is a **bitmask**, not a count.

| bit | meaning |
|---:|---|
| +1 | only own check-moves |
| +2 | no opponent checks |
| +4 | no opponent captures |
| +8, +16 | further restrictions |

Every odd value sets bit 1, and the probe position's first move is not a check, so every odd value failed. The mystery was bit 0.

Implementation and validation:

- `restrict_attacker_moves` filters the attacker's list at every attacker site -- the depth-first prover, the DFPN preconditioner, and the parallel root split -- so the restriction cannot be evaded by route or by thread count;
- this is **not** a pruning heuristic. Removing a legal attacker move would be unsound for an ordinary directmate; it is correct only because `-C 1` asks a different question, under which the removed moves are not candidates. The code says so at the definition, because the distinction is exactly the kind that erodes;
- **validated differentially against the WinChest oracle**: 23 positions, 23 agreements on solvedness, 0 disagreements;
- self-consistency checked independently: in every restricted solution found, every attacker move in the PV gives check;
- `-C 0` and `-C -1` are off, matching WinChest, and leave the unrestricted search bit-identical;
- ChecksOnly bits 2/4/8/16 remain refused with a message naming what was asked for, as do `-R -K -P -X -I`.

The manual also records that under restriction "it can not be guaranteed that there will be found any solution at all, or that a given solution is the best (i.e. shortest) one" -- which matches how E behaves: a restricted search may legitimately report nothing.

### 7h. Portfolio Parallelism

The sequential portfolio gives each entry a *slice* of the budget. `--portfolio-parallel` runs every entry concurrently instead, so each gets the **whole** budget.

This works for a reason the root split cannot exploit. Root-split parallelism saturates around 16 threads because extra workers contribute duplicated nodes -- 2.2x the sequential node count at 8 threads, 2.9x at 16. Portfolio entries do **not** duplicate each other: they are searching genuinely different restricted problems. So the lanes scale where the root split does not, and they use cores the root split cannot.

matetrack mate-in-8, 24 positions, **equal 15-second wall clock**:

| configuration | solved | proved by |
|---|---:|---|
| unrestricted, 8 threads | 15/24 | unrestricted 15 |
| portfolio sequential, 8 threads | 15/24 | unrestricted 13, K2 1, R1 1 |
| **portfolio parallel, 32 threads** | **17/24** | unrestricted 13, K3 3, R1 1 |

The parallel portfolio reaches at 15 seconds what the sequential one needed 45 seconds for -- a threefold budget reduction for the same capability.

Threads follow the same weights as the time slices. Splitting them equally starved the unrestricted lane -- the most general one, and the one whose answer is preferred -- which dropped it from 15 solved to 13 while the restricted lanes added 4. With weighted allocation the unrestricted lane recovers to 15 and the restricted lanes add 2:

| lane allocation | total | unrestricted lane | via restriction |
|---|---:|---:|---:|
| equal (4 threads each) | 17/24 | 13 | 4 |
| **weighted** | 17/24 | **15** | 2 |

The headline is unchanged but the structure is strictly better. Under equal allocation the portfolio *traded* two problems away to gain four; under weighted allocation it gives up nothing relative to running unrestricted alone and adds two on top. A mode that can only help is easier to recommend than one that helps on balance.

**Determinism is traded deliberately.** Which lane wins first can vary between runs, so the *proof* returned is not deterministic. Every proof returned is still valid and verifiable, the lane used is reported, and the unrestricted lane is preferred whenever it also succeeds. Making the choice deterministic would mean waiting for lower-index lanes that may never finish, which is the cost this mode exists to avoid. The suite therefore tests the property every result must have -- solved positions replay to a real mate at the stated depth -- rather than a fixed expected answer.

All 17 certificates from a parallel-portfolio run verify with the shipped checker, including the four found under restriction.

### 7g. The Restriction Portfolio

Implementing the restrictions turned out to buy something the unrestricted engine could not get on its own.

**A restriction only removes attacker options, so any mate found under one is a real forced mate.** Restrictions are incomplete, never unsound. That makes a restricted search a *sound fast path* for the ordinary problem, not a gamble: if it finds a mate in N, a mate in N exists, and the certificate verifies exactly as any other.

`--portfolio` spends the time budget across a sequence of configurations -- unrestricted first and largest, then the restrictions that solved the most problems the others could not -- and stops at the first proof. The winning entry is reported as `; via K3` so a caller knows a restriction was used and that the mate may not be the shortest.

Measured on matetrack mate-in-8, 24 positions, at **equal total budget**:

| configuration | 15 s | 45 s |
|---|---:|---:|
| unrestricted | 15/24 | 15/24 |
| **portfolio** | 15/24 | **17/24** |

At 45 s the portfolio solves two problems the unrestricted search cannot, proved via `K3` and `R1`. At 15 s it ties: the unrestricted slice is only 34% of the budget, losing two, and the restrictions gain two back.

Note the unrestricted column: 15/24 at both 15 s and 45 s. That is the scaling cliff again -- tripling the budget solves nothing more -- which is precisely why a *different search* rather than *more of the same search* is what helps.

This is the first change in this line to improve capability at fixed budget by something other than speed.

Two defects were caught by the existing gates while building it, both worth recording:

- the reporting `Search` was given the caller's configuration only on the failure path, so a portfolio run that *succeeded* reported with a default-constructed config and silently dropped `--emit-proof`. The shipped verifier caught it immediately by refusing output that should have carried certificates;
- the help-coverage test caught `--portfolio` and `--no-portfolio` missing from `--help` before they could ship undocumented.

All 15 mate-in-8 proofs from a portfolio run verify, including the two found under restriction.

### 7f. ChecksOnly Completed

All five ChecksOnly bits are implemented:

| bit | meaning |
|---:|---|
| 1 | only own check-moves |
| 2 | no opponent checks |
| 4 | no opponent captures |
| 8 | no own captures, mating move exempt |
| 16 | no own check-moves, mating move exempt |

Bits 1 and 16 are contradictory and refused, as the manual requires; values outside 0..31 are refused.

**The mating-move exemption on bit 8 is not in the documentation.** The manual states it only for bit 16 ("no own check moves, with the exception of the last mating move"). Implementing bit 8 literally produced six disagreements with WinChest, all of the same shape: positions where WinChest solved and E did not, including a mate-in-1 whose only move is a capture. Extending the exemption to bit 8 removed every disagreement.

That is the third time the written specification for these options has been incomplete or misleading, after the ChecksOnly bitmask itself and the ThreatDepth cap. Each was caught only because the oracle was consulted rather than the prose trusted -- black-box differential testing has been worth more here than the manual.

**Differentially validated**: 14 mask values (1..9, 12, 16, 20, 24, 28), **322 comparisons, 0 disagreements**. The bits bind and bind differently: against 13 solved unrestricted, `-C 1` gives 3, `-C 2` gives 11, `-C 4` gives 5, `-C 8` gives 9, `-C 16` gives 9, `-C 28` gives 4.

Running total across all restriction work: **666 oracle comparisons, 0 disagreements**.

`-I` (threat flags) is the only variant still unimplemented, and confirmed unimplementable from the available documentation: it appears in `Options.txt` as a one-line label with a numeric range, and has **no entry at all** in the English manual, unlike every other option which has a numbered section. There is no semantic content to implement against, and unlike the ChecksOnly bitmask, probing would not distinguish between plausible interpretations because there is no candidate interpretation to test.

### 7d. KingSquares, PieceLimit and MaxMoves Implemented

Three more WinChest variants are implemented, all per-move filters on the attacker's list:

| option | condition on the position after the attacker's move |
|---|---|
| `-K N` | the defender king has at most N squares available, **counting the one it stands on** |
| `-P N` | at most N defender pieces have a legal move |
| `-X N` | the defender has at most N legal moves in total |

Off values follow WinChest exactly: `-K 0` or `9`, `-P 0` or `16`, `-X 0` or `222`. Negative values select automatic-mode lower bounds, which this engine has no automatic mode for, so they are refused rather than silently reinterpreted.

All three ask about the defender's replies, so they share one child materialisation and one move generation per candidate; the cost is paid only when a restriction is active.

**Differentially validated against the WinChest binary**: 23 positions x 9 option settings, **207 comparisons, 0 disagreements** on solvedness.

Agreement alone would be weak if the restrictions never bound, so that was measured too. Against 13 solved unrestricted:

| setting | solved |
|---|---:|
| unrestricted | 13/23 |
| `-C 1` | 3/23 |
| `-K 1` / `-K 2` | 7/23 / 11/23 |
| `-P 1` / `-P 2` | 3/23 / 6/23 |
| `-X 1` / `-X 2` / `-X 4` | 2/23 / 3/23 / 4/23 |

Every setting binds substantially, so the agreement is informative rather than vacuous.

The suite also checks the property directly rather than only through the oracle: for every solution found under a restriction, the condition is re-derived with python-chess at the position after *every* attacker move in the PV. A restriction that applied at the root but leaked deeper would fail that check.

`-R` (ThreatDepth) and `-I` (threat flags) remain unimplemented. Both need null-move threat machinery that the exact kernel does not have, which is a larger change than a per-move filter.

### 7e. ThreatDepth Implemented

`-R N` is implemented. A move is examined only if, after it and a **null move by the defender**, the attacker can mate within `|N|`. The sign selects the threat search: positive uses check-moves only ("check threats"), negative allows any move ("quiet threats").

Two things made this harder than the per-move filters.

**The threat probe answers a different question.** After the null move the position is unreachable by legal play, which is the point -- it measures what the attacker *threatens* rather than what is forced -- and when `N > 0` the probe is additionally restricted to checks. Sharing a table between that and the enclosing search would let a check-restricted disproof answer an unrestricted question. The probe therefore gets its own `Search` and its own table, created lazily per search object so parallel workers do not share one.

**"Higher values are ignored" means off, not clamped.** The manual caps ThreatDepth at *matenumber - 2*. Clamping an over-large value to the maximum seemed the natural reading and was wrong: differential testing found a mate-in-3 with `-R 2` that WinChest solved and E did not, because clamping to 1 imposes a restriction where WinChest imposes none. Switching the option off above the cap makes them agree. That is the second time these options' one-line documentation has been misleading and the oracle has settled it.

WinChest also disables ThreatDepth when ChecksOnly is 1, "because in this case threats are not useful any longer". E does the same, and the suite checks `-C 1` and `-C 1 -R 1` agree.

**Differentially validated**: 137 comparisons across `-R` 1, 2, 3, -1, -2, -3, **zero disagreements**. The restriction binds: against 13 solved unrestricted, `-R 1` and `-R -1` each give 10.

Five of the six WinChest variants are now implemented and oracle-validated: `-C 1`, `-R`, `-K`, `-P`, `-X`. Only `-I` remains, and its one-line description gives no usable semantics.

### 8. Internal Parallelism

E should support root split and deeper work stealing with:

- per-thread board stacks;
- shared or sharded TT;
- cancellation after a proof;
- deterministic debug mode;
- no shared mutable proof corruption.

Current E checkpoint:

- `--threads N` enables an unpromoted root-split search; `--threads auto` uses the hardware concurrency count; `--single-thread` and the promoted default `N=1` keep the exact sequential path;
- workers claim root attacker move indices from a shared counter and prove their move in a private `Search` with a private table, so no proof state is shared mutably;
- the accepted answer is the successful root move with the **lowest index**, which is exactly the move the sequential attacker loop would have returned, so splitting changes search speed and node counts but never the reported key move;
- this was verified directly: at 2, 4, 8, 16, and 32 threads the smoke, regression-control, negative-control, and hard-holdout suites returned identical solvedness and identical `bm` for all 126 rows;
- cancellation is cooperative. `Search` carries an optional atomic cancel flag polled once per node and an `aborted` bit. A worker whose root index can no longer win is cancelled, unwinds, and records nothing;
- **an aborted subtree has no verdict.** Both proof-table store helpers refuse to write while `aborted` is set, and the attacker/defender loops refuse to read an aborted empty result as a refutation. This is the core safety property of the split: without it, an abandoned search would cache a false disproof;
- a worker re-reads the best index after publishing its own, closing the race where a finishing worker scanned the slots before the new index was announced;
- worker construction is lazy, so positions that resolve without ever splitting pay nothing;
- depth 1 is never split; it is a flat immediate-mate scan where thread setup costs more than it saves;
- per-worker counters are folded back into the reported totals by `Stats::operator+=`, guarded by a `static_assert` on `sizeof(Stats)` so a new counter cannot silently escape the merge;
- measured on the 40-case hard holdout: `197.8 ms` average sequential versus `72.7 ms` at 16 threads, a 2.72x speedup, with p95 `454.8 ms` to `155.8 ms`;
- measured on smoke: `60.2 ms` average sequential versus `20.3 ms` at 8 threads;
- **the parallel route is now promoted as the default** through the benchmark registry: `--threads 8 --shared-tt`, gated by `--parallel-min-nodes 500`. `chest_E_sequential_probe` is the retained rollback control;
- key move, mate depth and solvedness are identical to sequential E on every validated row; only node counts differ, because workers explore concurrently;
- `--threads 8` beat 16, 24 and `auto` (32 here) once the cost gate was in place, geometric mean 2.35x versus 2.22x/2.19x/2.16x. With cheap depths no longer split, these suites' root branching does not keep more than about eight workers usefully busy, and extra threads only add contention and duplicated search. This is a suite- and machine-specific tuning that should be re-swept on a larger corpus and on smaller machines;

### 8f. The Scaling Cliff

The decisive question after this line's performance work: is E slow at mate-in-10, or unable to reach it?

10 sampled matetrack mate-in-10 problems, 8 threads, `--direct-depth`, `-M 1024`:

| time budget | solved |
|---:|---:|
| 5 s | 3/10 |
| 20 s | 3/10 |
| 60 s | **3/10** |

Flat across a **12x** increase in time. The three solved problems resolve inside 5 s; the other seven do not resolve in 60. That is a cliff, not a slope.

Together with the other axes the picture is complete: threads 8 to 32, memory 64 MB to unbounded, and time 5 s to 60 s all leave mate-in-10 unchanged. **No resource axis extends reach.**

This bounds the value of the performance work in this document. The 1.44x sequential gain and the parallel speedup are real and verified, and they move problems across a budget boundary at mate-in-8 — but at mate-in-10 a further 12x would solve nothing, because 12x more time is equivalent to a 12x faster engine at a fixed budget. The unsolved problems are likely orders of magnitude away, which is what exhaustive AND/OR search at depth 10 with branching near 39 implies.

Every avenue that redistributes or accelerates the same search is therefore closed: more cores, more memory, more time, faster nodes, DFPN, and root scheduling have each been measured and each changed nothing at this depth. What remains is **reducing the size of the tree** — proof-safe pruning, meaning exact tests that discharge subtrees without traversing them.

### 8e. Resource Scaling Is Exhausted

Solve rate at a 5 s budget with `--direct-depth`, varying memory at 8 threads:

| problems | 64 MB | 256 MB | 1024 MB | 4096 MB | unbounded |
|---|---:|---:|---:|---:|---:|
| matetrack mate-in-8 | 12/24 | 12/24 | 13/24 | 13/24 | 13/24 |
| matetrack mate-in-10 | 3/20 | 3/20 | 3/20 | 3/20 | **3/20** |

**mate-in-10 is 3/20 under every resource configuration tested** -- 8 to 32 threads, 64 MB to unbounded memory. Removing the memory bound entirely changes nothing.

This closes off two whole classes of improvement. More cores do not help, because root split saturates on duplicated nodes. More memory does not help, because eviction was never throttling reach -- the bounded-table work was necessary for robustness and was costing no capability.

That is a useful negative: it localises the limitation to the search itself and makes any further resource-scaling work dead. `-M 64` stays the default since `-M 1024` gains roughly one problem in twenty four at mate-in-8 and nothing at mate-in-10; the guidance is documented rather than imposed.

### 8d. Parallelism Saturates At About 16 Threads

Solve rate at a 5 s budget with `--direct-depth` on a 32-core host:

| problems | 8 | 16 | 24 | 32 |
|---|---:|---:|---:|---:|
| matetrack mate-in-8 | 13/24 | **14/24** | 14/24 | 14/24 |
| matetrack mate-in-10 | 3/20 | 3/20 | 3/20 | 3/20 |

Capability is flat from 16 threads upward, and completely flat at mate-in-10. Four times the CPU buys one extra mate-in-8 problem and nothing deeper.

This is the same effect the deep corpus showed as 2.2x node inflation at 8 threads and 2.9x at 16: extra workers contribute **duplicated nodes, not new search**. It is a property of the root-split decomposition rather than of this host.

Consequently `--threads auto` no longer resolves to the detected core count, which on a large machine burned cores for no capability. It resolves to `min(cores, 16)`. An explicit `--threads N` is never capped.

Honest limits: measured on one host at one budget. A much longer budget might differentiate higher thread counts; short budgets are the practical regime for a mate solver and are what was measured. The cap is where the curve flattens here, a defensible default rather than a universal constant.

What would help is not more cores but less duplication: in-progress markers in the shared table so a worker beginning a node warns siblings off it, or work stealing below the root. Both are larger than anything promoted this session, and both must be judged on solve rate rather than wall time.

### 8c. Root Young-Brothers-Wait (measured, not promoted)

The deep corpus showed 2.2x node inflation at 8 threads and 2.9x at 16. That follows from the acceptance rule rather than being incidental overhead: the accepted answer is the lowest-index successful root move, so every node spent on a higher index is discarded the moment a lower one succeeds. With median root branching of 39, and ordering good enough that the first move is often the proof, an immediate full split puts most workers on results that can never be used.

`--root-sequential-first N` searches the first N root moves sequentially before splitting the rest; `--root-split-all` is the promoted default. Answers are unchanged by construction and verified identical.

- it works as predicted: **node waste falls ~29%** at both thread counts, 151.8M to 107.0M at 8 threads and 198.7M to 142.2M at 16;
- it does not convert to wall time. Serialising the first root move adds critical-path latency, so the effects cancel at 8 threads (+0.6%) and the saving only wins at 16 (-3.7%);
- on the easy suites it is a clear loss, 10-19% slower, since those resolve in milliseconds and had little waste to recover;
- **not promoted**: an efficiency gain rather than a speed gain, and it costs time on the suites representing typical use;
- what it settles: roughly 29 points of the inflation are speculative root work superseded by a lower-index proof. The residue is genuine duplication *within* sibling subtrees, which root-level scheduling cannot reach. That is the argument for work stealing on interior nodes rather than further root-level scheduling.

### 8b. Parallel Cost Gate

Thread setup is pure overhead on work that would have finished in microseconds, but search cost is not knowable in advance. A gate that only inspects completed depths is useless here: cost grows exponentially with depth, so by the time a shallow depth shows the position is expensive, the expensive depth is the one already running.

So E probes instead of predicting. Each depth first runs sequentially under a node ceiling. Blowing the ceiling *is* the definition of an expensive position, and the depth is then re-run split.

- the probe is not wasted work. Exceeding the ceiling is an **abort**, which by the abort invariant records no verdict but leaves every genuinely completed subtree in the table, and that table is imported into the shared table before the workers start;
- `--parallel-min-nodes N` sets the ceiling, default `500`; `--no-parallel-gate` disables it and always splits;
- threshold sweep at 16 threads, geometric mean across the four suites: no gate `2.15x`, 250 `2.24x`, **500 `2.26x`**, 750 `2.20x`, 5000 `2.17x`... and 10000 only `1.89x-2.03x` per suite, because with hard-holdout positions totalling as few as 16.6k nodes a 10k serial prelude becomes an Amdahl bottleneck;
- the gate does more than avoid harm. Ungated, negative controls ran at `0.97x-1.00x` of sequential; gated at 500 they run at **`1.36x`**. Trivial positions now stay sequential and pay nothing, while the merely-small no-mate positions still escalate and genuinely benefit from splitting root refutations. This confirms the earlier no-mate regression was fixed overhead on tiny work, not anything structural about proving no-mate.

### 3b. Shared Exact Proof Table

Sharing exact proof entries between workers is safe *because* the key is exact and complete. An entry records the verdict for one board, side to move, attacker colour, node kind, castling state and en-passant state, at one exact remaining depth. That verdict is a pure function of the key, so it does not matter which worker computed it and a reader cannot be misled by the writer's search context. This is the payoff for the conservative exact-key design in section 3.

Current E checkpoint:

- `--shared-tt` is the promoted table mode whenever `--threads N` with `N > 1` is active; `--private-tt` is the rollback/A-B path; `--shared-tt-shards N` tunes shard count;
- the table is sharded with one mutex per shard, selected from the high bits of the key hash so the shard choice is independent of the low bits `unordered_map` uses for bucketing;
- the abort invariant is unchanged: an aborted subtree still stores nothing, so no worker can publish a false disproof into the shared table;
- shard count is not sensitive: `64` through `16384` shards all land within measurement noise at 16 threads, so lock contention is not the bottleneck and `256` is the default;
- sharing cuts duplicated work by 20-24%: 40-case hard-holdout nodes drop from 6.56M to 5.09M at 16 threads;
- more importantly it cuts *variance*. Over three interleaved trials the private-table path measured 79.1/100.5/75.4 ms while the shared path measured 69.3/70.9/68.0 ms;
- three-trial interleaved speedups versus sequential at 16 threads, private versus shared: hard holdout 2.24x vs **2.74x**, smoke 2.62x vs **2.86x**, regression controls 2.54x vs **2.68x**;
- the shared table also largely heals the no-mate regression, from 0.70x to 0.92x of sequential on negative controls, because workers now reuse each other's disproofs, which is exactly what a position with no mate needs;
- validated with proof-tree verification at 16 threads on smoke (9/9 valid), regression controls (11/11 valid, 33 skipped) and negative controls (0 false proofs), plus identical solvedness and identical key moves against sequential on all rows.

### 9. Persistent Service Mode

E should run as a long-lived worker:

- warm TT;
- input queue;
- batch protocol;
- no process startup per position;
- route metadata and timing counters.

### 10. Memory And Locality

Final E should use:

- packed move arrays;
- arena allocators;
- fixed-size TT buckets;
- cache-line-aware entries;
- compact undo records;
- minimal heap allocation in search.

Current E checkpoint:

- `chest-e\build.ps1` uses the linker no-timestamp option so repeated builds from identical source produce a stable executable hash for benchmark registry pinning.

### 10b. Input Validation And A Castling Bug It Uncovered

The architecture listed "malformed FEN/EPD tests" as part of the verification harness and had never implemented them. Probing the parser with malformed input found no crashes, but three real defects.

**A prover must refuse questions that are not well posed.** `8/8/8/8/8/8/8/KKKKKKKK w - -` -- eight white kings, no black king -- was accepted and reported `dm 1`: a mate claim in a position with no king to mate. An invalid side-to-move letter was silently treated as white, and a malformed en-passant field was silently treated as absent.

`parse_fen4` now rejects: a side other than `w`/`b`, an en-passant field that is malformed or off the third/sixth rank, a pawn on the first or last rank, any king count other than one per side, and a position where the side *not* to move is in check, which is unreachable.

**A castling bug fell out of the same probe.** Castling generation checked the rights bit and the empty squares between, but never that the rook was actually on its corner -- while `make_move` writes a rook onto f1/d1 unconditionally. A FEN claiming a right whose rook is absent therefore generated a castling move that **materialised a piece from nothing**: `4k3/8/8/8/8/8/8/4K3 w K -` produced six legal moves where python-chess gives five.

Every standard perft position has consistent castling rights, which is why six reference positions at depth 4-5 never exercised it. This is the second castling defect in this engine that perft alone could not reach; the first, revoking rights by captured piece type, was found *by* perft. Both were only visible from a direction the existing gates did not cover.

Thirteen illegal-position checks and four castling checks are now part of the suite, including a direct comparison against python-chess on both the phantom-rook and real-rook positions. All 86 positions in the deep-mined and matetrack corpora are still accepted, so the validation rejects only what is genuinely illegal.

### 11. Verification Harness

A certificate format is only worth having if someone other than the engine can check it. `chest-e/tools/verify_proof.py` ships with the prover for that reason: it reads engine output, re-derives every legal move with python-chess, and never consults the engine.

It accepts a certificate only when the attacker move is legal at each node, a leaf marked `mate` really is checkmate, a defender node lists **exactly** the legal replies, and every listed reply has a valid sub-proof. It independently replays the reported PV and checks its length against the reported depth.

The verifier is tested adversarially, because one that accepts everything would make the engine's headline claim worthless. The suite forges four distinct attacks on a genuine certificate -- an omitted defence, a non-mating leaf marked as mate, a corrupted PV, and an overstated depth -- and requires each to be rejected.

Writing those tests caught a flaw in the *test*, not the tool: the first forged-leaf attempt mutated a leaf that already read `{"a": ..., "mate": true}`, so it changed nothing and passed vacuously. A forgery test that does not actually forge anything is worse than no test, since it reports confidence it has not earned.



Every E success is checked by:

- legal PV replay;
- final checkmate verification;
- recursive proof-tree verification where available;
- A/B/C/D differential tests;
- restriction-mode sweeps;
- no-mate controls;
- malformed FEN/EPD tests;
- random legal-position stress.

### 12. Benchmark And Mining System

E needs a larger corpus than the current suite:

- thousands of directmates;
- mate depths 1-5+;
- composed and game-derived positions;
- no-mate controls;
- promotions, castling, en-passant;
- high-branching hard cases;
- restrictions/stipulations;
- frozen train/dev/test/holdout splits.



Current E checkpoint:

- a deep-mate miner in the private benchmark harness (not shipped with the engine) finds directmates deeper than the frozen suites, using the engine's own iterative deepening to establish the exact mate distance and grading positions by measured node cost;
- `benchmarks/suites/e_deep_mined_20260622.jsonl` holds 42 verified positions, 35 mate-in-6 and 7 mate-in-7, with a maximum cost of 25.6M nodes -- **138x** the previous corpus maximum of 185k, and a median 13x it;
- every position is PV-replay verified (42/42) and the mate-6 subset is proof-certificate verified (35/35, 0 invalid);
- **this corpus immediately corrected a headline claim.** Parallel speedup measured 3.51x on the easy suites but only **1.36x** here, and 16 threads is slower than 8. Node counts reveal why: 8 threads explores 2.2x the sequential nodes and 16 threads 2.9x, because sibling root subtrees share far more structure deep in the tree than shallow, so root splitting duplicates increasing amounts of work as depth grows;
- the frozen easy suites remain the correctness and regression gates. They are not informative about scaling, and search-algorithm work must be measured here instead.

### 4b. Direct-Depth Search

E ran iterative deepening from depth 1 to the requested depth N, so a mate-in-10 cost nine passes that cannot succeed before the one that can. Iterative deepening earns that two ways -- it finds the *shortest* mate, and it warms the table for the next pass -- and whether the warming pays for the wasted passes is empirical.

- `--direct-depth` starts at N; `--iterative-depth` is the default and the rollback;
- solve rate at budget, 8 threads: mate-8 goes 7/24 to **9/24** at 2 s and 11/24 to **12/24** at 5 s; mate-10 goes **0/20 to 3/20** at 2 s and 2/20 to 3/20 at 5 s;
- at mate-in-10 with a 2 s budget iterative solves nothing and direct-depth solves three. This is the first change in this line that improves capability where E was failing outright, rather than shifting the curve of what it already solved;
- **the semantics differ**: searching at depth N proves "a mate within N", iterative deepening proves "the shortest mate is N". They diverge when more than one first move works at different depths, since the attacker loop returns the first proving move in ordering order;
- measured on 72 solved positions with known exact depths across four suites there were **0 mismatches**, which is reassuring and not a guarantee -- composed problems usually admit only one working first move, so ordering has nothing wrong to pick;
- **not promoted as the default.** Promoting it would silently change what `dm` means for any caller relying on shortest-mate semantics, and that class of silent behavioural change is exactly what the `--fast-check-score` cliff and the ignored restriction options were. The default stays conservative and the flag is documented;
- recommended for "verify a mate in exactly N" workloads, which is what EPD `bm #N` supplies.

### 12b. Wall-Clock Budget

A released mate solver that cannot be told to stop is unusable: E solves 2 of 8 matetrack mate-in-8 positions, so on the other six it simply ran forever. And once reach rather than throughput is the limiting factor, *solve rate within a budget* is the metric that matters, which requires a budget to exist.

Current E checkpoint:

- `--time-limit S` sets a wall-clock budget in seconds; 0 means unlimited, which remains the default;
- expiry reuses the existing cancellation path rather than adding a mechanism. It is an **abort**, and by the abort invariant an aborted search records no verdict -- so a timed-out run reports "not proved", never a mate and never a disproof it did not establish;
- iterative deepening stops on expiry rather than treating an abandoned depth as a failed one, which would otherwise convert a timeout into a false "no mate at this depth";
- every parallel worker inherits the same deadline by value, so all stop at the same instant, and a worker timeout propagates back to the driver;
- the deadline is polled on a 2048-node countdown, because reading the clock at every node would cost more than the search it guards. Granularity affects only promptness, never correctness;
- output carries an explicit `timeout` marker, so a caller can distinguish "gave up" from "proved there is no mate". Without it a released tool reports the same thing for both;
- soundness was verified under pressure rather than only at rest: across three budgets and both threading modes on 24 mate-in-8 positions, 16 proofs were emitted and **0 were invalid**, with 128 timeout markers, and negative controls under a 0.05 s budget produced no false mates.

### 12c. Source Layout

The prover had grown to 3,534 lines in one translation unit. It is now split into eight modules, each with a stated purpose:

| module | lines | purpose |
|---|---:|---|
| `types.h` | 305 | colours, moves, boards, proofs, statistics, table keys |
| `table.h` | 219 | bounded and shared proof tables, memory-budget conversion |
| `search_state.h` | 273 | search configuration, per-search state, cancellation |
| `board.h` | 458 | geometry, attack tables, FEN parsing, attack queries |
| `movegen.h` | 357 | pseudo-legal generation, make_move, legality, planes |
| `ordering.h` | 331 | ordering scores and ordered move-list generators |
| `prooftable.h` | 64 | centralised exact proof-table probe and store |
| `prove.h` | 408 | the exact AND/OR kernel: attacker and defender nodes, threats, restrictions |
| `rootsplit.h` | 222 | root-split parallel search and worker coordination |
| `dfpn.h` | 281 | DFPN preconditioner, never an output authority |
| `routes.h` | 375 | route implementations and the output-acceptance guard |
| `report.h` | 237 | perft, profile counters, line-oriented output helpers |
| `solve.h` | 287 | restriction portfolio and the per-position driver |

**Compilation remains a unity build, deliberately.** The modules are headers included in their original order by one translation unit, so the preprocessed result is textually equivalent to the previous single file. Two reasons: the search depends on cross-module inlining in its hottest paths, and preserving textual order makes the refactor behaviour-preserving *by construction* rather than by inspection.

That was verified rather than assumed: node counts, key moves and PVs are identical to the pre-split binary on all four suites, and paired 3-trial timing shows -1.5% and -0.3%, both inside noise. 92/92 in-repo, ctest green, Linux green, `-Werror` clean.

The split was performed by a script that cuts on declaration boundaries and backs up over preceding comment blocks and `template` headers, so no cut orphans a comment from the code it documents. The first attempt did exactly that -- it separated `template <typename MoveSink>` from `gen_pseudo` -- which is why the boundary logic exists.

The search module was that remaining monolith, at 1,745 lines by the time it was
split, and no longer exists as a single file.
It is now six modules, cut on the seams the code already had: the kernel, the
root-split scheduler, the DFPN preconditioner, the routes, the reporting
helpers, and the portfolio driver. No module now exceeds 400 lines.

The cut points were chosen by checking, mechanically, that no earlier module
references a symbol defined in a later one -- the only candidate was a mention of
`perft` inside two comments in the routes section, which is not a reference at
all. Textual order within each module is unchanged, so the preprocessed
translation unit is identical to before and the refactor is behaviour-preserving
by construction.

Verified rather than assumed, again: with wall-clock timing fields normalised
away, the pre-split and post-split binaries produce **byte-identical output** on
the mate corpus, on the same corpus with `--emit-proof`, on the no-mate controls,
and on perft to depths 4 and 5. Timing fields were the only difference, and
noticing that they were the *entire* difference required stripping them -- the
first comparison reported every case as differing, including perft, which cannot
vary.

### 13. CLI Contract

A released prover is a tool other people drive from scripts, so the argument parser is part of the correctness surface, not decoration.

The parser previously had **no else branch**: any unrecognised token was silently discarded. `--thredas 8` ran single-threaded without complaint, a trailing `-M` vanished, and `--help` was unrecognised so it fell through to reading stdin and hung forever. Chest restriction options `-C -R -K -P -X -I -n -N` consumed their value and were ignored, which is the most dangerous case of all: the caller asks a constrained question and receives a confident answer to a different, unconstrained one.

This is the same failure mode as the `--fast-check-score` cliff, where one flag silently disabled move ordering and cost 30x while hiding behind a "rejected as slower" label. Silent acceptance of input the engine does not honour is a recurring hazard here, so it is now closed by construction.

Current checkpoint:

- unknown options, missing values and non-numeric values exit **2** with a diagnostic naming the option;
- unimplemented Chest restrictions are **rejected**, with `--allow-unimplemented` as an explicit opt-out for harness compatibility. The opt-out is pre-scanned so it works regardless of argument order;
- `-b`, `-1`, `-5` are explicitly accepted as documented compatibility no-ops rather than falling through;
- `--help` and `--version` exist and exit 0; the version is defined by `project(VERSION)` in CMake with a source fallback for plain compiler builds;
- nine CLI-contract tests are part of the in-repo suite, including that the escape hatch works and that compatibility flags are not caught by the new strictness.

The promoted search path is unchanged: identical node counts, key moves and PVs on all four suites.

### 8e. Rejected Routes Are Not Worth A Portfolio Lane (measured, not promoted)

The parallel portfolio does not require a lane to be *good*, only to be
*different*: a lane earns its threads if it solves anything the other lanes
miss, however badly it does overall. That is a much weaker bar than standalone
promotion, so the two routes previously rejected on capability -- `dfpn` and
`shallow-fast` -- were re-tested against it.

Measuring this exposed a defect first. `--direct-depth` was honoured only by
the depth-first route; the DFPN route hardcoded a start depth of 1 and the
shallow-fast fallback hardcoded 3. The flag therefore did nothing on two of
three routes, and the original DFPN comparison had been scoring iterative
deepening against a direct search -- not the same question. Both routes now
honour the flag, and the measurement below is the post-fix one.

matetrack mate-in-8, 24 positions, 15s, 4 threads, `--direct-depth`:

| route                    | solved | solves that the default does not |
|--------------------------|--------|----------------------------------|
| depth-first (default)    | 14/24  | --                               |
| shallow-fast             | 14/24  | none                             |
| dfpn                     |  0/24  | none                             |

Against the full 32-thread portfolio (17/24) the marginal contribution of each
is likewise empty.

Rejected. `shallow-fast` matches the default's count but its solution set is a
strict subset -- it finds the same positions by a different order, so a lane
would spend threads re-deriving results the unrestricted lane already has.
`dfpn` solves nothing at all at this depth even given a fair start depth, which
closes the question the earlier 4/24-vs-7/24 result had left open: DFPN is not
merely weaker here, it is not competitive at any margin, and no scheduling or
lane-level reuse rescues it.

The useful general result is that **route diversity is not the same as solution
diversity**. The restriction lanes pay off because a restriction changes which
mates are reachable at all; an alternative *search order* over the same move set
cannot, so it has no marginal value no matter how it is scheduled. Only lanes
that change the problem are worth parallelising over.

### 8f. The Restriction Portfolio, Derived Rather Than Hand-Picked (promoted)

The portfolio's eight restrictions were chosen early by judgement and their
weights tuned once -- on the same 24 positions used to evaluate them. With lane
membership now the only axis that still scales, that table was the highest-value
thing left to get right, and it had never been measured properly.

Method. Twenty candidate restrictions (the five `ChecksOnly` bits and useful
combinations, `KingSquares` 2-5, `MaxMoves` 2-6, `ThreatDepth` +-1/+-2) were
each run standalone over 60 matetrack mate-in-8 positions drawn from the 996
that no suite here had previously used, then the lane set was chosen by greedy
set cover. A second, disjoint 60 positions were held back and used only once,
to decide promotion.

Standalone coverage on the training half (5s, 2 threads):

| lane | solved | | lane | solved |
|---|---|---|---|---|
| K2 | 26/60 | | X6 | 13/60 |
| K3 | 26/60 | | X3, X4 | 11/60 |
| unrestricted | 22/60 | | X2 | 10/60 |
| K4 | 21/60 | | R2 | 8/60 |
| K5 | 18/60 | | C8, C16 | 7/60 |
| C2 | 17/60 | | C1, C3 | 1/60 |

Two results stand out. `KingSquares` restrictions **beat the unrestricted search
outright** -- K2 and K3 each solve 26 where it solves 22 -- so the strongest
lanes are restricted ones, not the general one. And `C1`/`C3` (serial-check
mate) solve 1/60: near-useless as a lane despite being the most famous of the
WinChest restrictions.

Greedy cover, with the unrestricted lane forced in first as the only complete
one:

| pick | adds | cumulative |
|---|---|---|
| unrestricted | -- | 22 |
| K2 | +11 | 33 |
| R2 | +5 | 38 |
| R1 | +2 | 40 |
| C4, K3, X2, Rq2 | +1 each | 44 |

The hand-picked table reached 39/60. The derived one reaches **44/60, which is
exactly the union of all twenty candidates** -- eight lanes already capture
everything this candidate pool can reach, so no further restriction of these
kinds can add anything. `R2` had never been in the table at all despite being
the second-best pick; `C2`, previously joint-second by weight, was dropped along
with `C1` and `X4`.

Held out, at the real operating point (32 threads, 15s, parallel portfolio):

| table | solved | certificates |
|---|---|---|
| hand-picked | 47/60 | -- |
| derived | **49/60** | 49 verified, 0 failed |

Two gained, none lost, on positions neither table was derived from. Promoted.

The lesson worth keeping is that the table had been *plausible* and was
measurably wrong in both directions at once: it spent its second-largest weight
on a lane worth dropping while omitting the second-best lane entirely. Judgement
picked the famous restrictions; measurement picked the ones that cover disjoint
problems. The gap between 39 and 44 was free, and had been sitting there since
the portfolio was introduced.

### 8g. Compound Restrictions (measured, not promoted)

8f left the portfolio at a hard ceiling: eight lanes already covered the union
of all twenty single restrictions, so nothing of that kind could add more. The
obvious way past it is compounds -- `KingSquares 2` *and* `ThreatDepth 2`
together. A compound cannot reach a mate its components cannot, since it only
removes more attacker options; but it searches a smaller space, so within a
fixed budget it can finish where the looser lane times out. Coverage under a
time limit is not monotone in permissiveness, which is what makes this worth
testing at all.

Twenty-five compounds were swept on the same training half. The effect is real
but small: the singles' 44/60 rises to **46/60**, from exactly two positions,
both won by a `KingSquares + ThreatDepth 2` lane. Greedy cover over the combined
pool needs nine lanes to bank both.

At the operating point it vanishes entirely. On a **third** disjoint holdout of
60 positions -- the 8f holdout had already been spent on one promotion decision,
and reusing it would have quietly converted it into a tuning set -- at 32
threads and 15s:

| table | solved |
|---|---|
| promoted 8-lane | 51/60 |
| compound 9-lane | 51/60 |

Not two gained and one lost, or any trade: the same 51 positions, identically.
Rejected.

The reason is the point worth keeping. A compound's whole advantage is finishing
inside a budget that defeats its looser parent. Give every lane 15s and 32
threads instead of 5s and 2, and the parent finishes too -- so the compound
contributes nothing it did not contribute *only because the parent was starved*.
The 46/60 was never a capability gain; it was a measurement of the training
budget.

That generalises into a standing caution about the 8f method: **set cover run at
a cheap training budget can manufacture lanes whose entire value is the cheapness
of the budget.** The derivation in 8f survives it because promotion there was
decided at the operating point on held-out data, not on the training sweep --
and the discipline of deciding at the operating point is exactly what caught
this. Training-budget coverage selects candidates; it must never promote them.

### 8h. Mate-8 Is Budget-Limited, Not Capability-Limited (diagnostic)

8e and 8g closed two directions by measurement, which left an unexamined
assumption underneath both: that the positions the portfolio misses are missed
because the engine *cannot* do them. Section 8d had measured exactly that at
mate-in-10 -- threads 8->32 flat, memory 64MB->unbounded flat, time 5s->60s flat
at 3/10 -- and concluded the engine was incapable rather than slow. It was easy
to carry that conclusion to mate-8 without checking.

It does not hold. Escalating the budget on the 60-position holdout, 32 threads,
parallel portfolio:

| budget | solved |
|---|---|
| 15s | 51/60 |
| 60s | 56/60 |
| 300s | **60/60** |

Every position falls. Not one is structurally out of reach; the nine misses at
15s are nine positions that needed more time. Mate-8 and mate-10 are in
qualitatively different regimes, and the mate-10 result does not describe mate-8.

This reprioritises the backlog. At mate-10, faster nodes buy nothing -- 8d
measured that directly. At mate-8 the solve curve is still climbing steeply at
the operating point, so **efficiency work is capability work here**: any
constant-factor speedup converts directly into coverage at a fixed budget. The
efficiency items that looked like polish next to a capability wall -- bitboards,
memory and locality, cheaper node costs -- are the items with a measurable
target, and the target is quantified: 4x buys +5/60, and roughly 20x would buy
the remaining 9 at 15s.

The general error worth recording is that 8d's conclusion was correct *at the
depth it was measured* and quietly wrong one depth down. A saturation result is
a statement about an operating point, not about the engine, and it expires the
moment either moves.

### 8i. Legality Without A Child Board, And What It Says About Bitboards (promoted)

8h established that constant-factor speed converts into coverage at mate-8, which
made "bitboard the board" the obvious next item. It is the wrong item, and
measuring rather than assuming is what showed it.

gprof is unusable here -- the sampling timer yields no data on this mingw
toolchain, with or without inlining -- so cost was attributed by counting
operations in an instrumented build and pricing them against perft, which
performs generation and `make_move` with no search overhead.

Baseline, one hard holdout position, single-threaded, 25s: 11.0M search nodes,
14.0M `gen_pseudo`, **124.7M `make_move`** (11.3 per node) and 62.2M
`is_attacked`. The cause was that `legal_moves`, `legal_moves_vector` and both
`has_legal_move` paths decided legality by building an entire child `Board` --
64 squares, nine occupancy planes, castling rights, en-passant state -- and
asking `in_check`, then discarding all of it. The only question being asked is
whether the mover's king is attacked, and `planes_after_move` plus
`attacked_on_planes` already answered exactly that: the fused ordering path had
used them for some time, while the plain generation paths had not.

Rewiring the four loops to the plane path:

| measure | before | after |
|---|---|---|
| perft(5) node count | 10,819,001 | 10,819,001 (identical) |
| perft(5) rate | 17.7 M/s | **28.7 M/s (1.62x)** |
| search rate, 1 thread | 448 k/s | 467 k/s (1.04x) |
| `make_move` calls | 124.7M | 53.9M |
| holdout @15s | 51/60 | 52/60 |

Promoted: strictly faster, semantically identical, and it deletes a second way
of asking a question the codebase already had one way to ask -- the same class
of duplication that once hid the castling-rights bug.

The result that matters is the discrepancy. Movegen-bound work got 1.62x; the
search got 1.04x. Removing 57% of all `make_move` calls and the entire per-move
`Board` copy bought four percent, so **the search is not movegen-bound**, and
bitboarding the board -- a much larger and riskier change aimed at the same
work -- cannot pay off either. The remaining cost is elsewhere: transposition
table traffic, move ordering and scoring, certificate and PV construction, and
allocation. That is where the next efficiency increment has to look.

Two cautions on the numbers. The `is_attacked` collapse from 62.2M to 0.22M is
mostly re-routing -- the attack test now runs inside `attacked_on_planes`, which
is not instrumented -- not work eliminated. And the +1 position at the operating
point is a single position on a wall-clock-limited parallel search, which is
inside the noise; the trustworthy claims here are the perft speedup and the
identical node counts, not the coverage delta.

### 8j. Allocator Traffic Is Not The Bottleneck Either (promoted on resources, not speed)

8i left the search's cost unaccounted for and named the suspects: TT traffic,
ordering and scoring, certificate construction, allocation. Certificates were
cleared immediately -- they are already gated behind `emit_proof` and cost
nothing when unused. Allocation was not.

Counting with a replaced global `operator new` (single-threaded, same hard
position, 25s): **39.3M allocations, 28.2 GB, for 11.8M nodes** -- 3.3
allocations and roughly 2.4 KB churned per node. The average allocation was
~717 bytes, which is a move-list vector at `--move-reserve-cap 96`.
`legal_moves_fused` was allocating two heap vectors per call, at nearly every
node.

Fixed by generating into the fixed-capacity `MoveList` already used by
`legal_moves` under `--static-pseudo`, keeping the heap path as an overflow
spill:

| measure | before | after |
|---|---|---|
| allocations | 39.3M | 28.7M (-27%) |
| bytes allocated | 28.2 GB | **12.7 GB (-55%)** |
| perft(5) rate | 28.7 M/s | 31.3 M/s |
| perft(5) node count | 10,819,001 | identical |
| search rate, 1 thread | 467 k/s | 459 k/s |
| holdout @15s, 32 threads | 52/60 | 52/60 |

Halving the allocation volume changed the search rate by -1.7%, which is noise,
and coverage not at all. **Allocator traffic is not the bottleneck.** That is the
second efficiency hypothesis to die this way: 8i removed 57% of `make_move`
calls for 4%, and this removes 55% of allocated bytes for nothing.

Kept, but on narrower grounds than intended, and it is worth being precise about
which: it is semantically identical (perft counts unchanged, 135/135), it makes
the fused path consistent with the idiom the rest of the file already uses, and
15.5 GB less churn per 25-second search is a real resource property for a
released engine. It is **not** a speed improvement and is not claimed as one.

Where the time actually goes, by elimination and arithmetic rather than by
measurement: a node costs ~2.2us at 459 k/s, and generates ~30 moves, each of
which builds a `Planes` copy and runs one or two `attacked_on_planes` scans in
the fused legality-and-scoring loop. Thirty such operations plausibly account
for most of the node. That is movegen-adjacent work, but it is not `make_move`
and not allocation, which is why both previous attempts missed it.

If that estimate is right, the target is legality that does not touch the planes
per move at all: compute checkers and absolute pins once per position, after
which most moves are legal by inspection and only king moves and en passant need
a real test. This is the standard technique and it attacks the one cost that has
survived elimination. It is also a genuine movegen rewrite with real correctness
risk, which is exactly what the perft gate exists for.

### 8k. Node Cost Is Diffuse, So Micro-Optimisation Cannot Reach The Target

8j ended by proposing a pin- and checker-based legality rewrite, on the estimate
that per-move plane construction dominates the node. That estimate was arithmetic,
not measurement, and two such estimates had already failed. It was checked before
being built on.

The first attempt to check it failed instructively. Disabling check scoring
removes one `attacked_on_planes` call per move, so the node rate should rise; it
*fell*, 466 to 439 k/s. Scoring drives move ordering, ordering changes which
nodes get visited, and the node mix changes with it. **Ablations inside the
search are confounded whenever the ablation changes the tree** -- nodes/sec moves
for reasons unrelated to the cost of what was removed. That rules out the whole
family of in-search ablations for cost attribution.

`tools/bench_movegen.cpp` measures instead by timing nested stages on a fixed
position set, where there is no tree to perturb. Twelve holdout positions, 20,000
repetitions:

| stage | cost | share |
|---|---|---|
| generation only | 186 ns | 25% |
| + legality (planes per move) | 500 ns | 43% |
| + scoring and list build (fused) | 734 ns | 32% |

No stage dominates. A pin-based rewrite attacks the 43%, and cannot take all of
it -- king moves, en passant and the check-scoring scan still need real tests --
so on a node that is roughly half fused-generation the realistic ceiling is
15-20% of node time. Set against 8h's measured curve, where **4x buys +5
positions**, 15-20% is worth a fraction of one position, for a genuine movegen
rewrite with the correctness risk that implies. Not worth building; rejected on
its measured value rather than on its difficulty.

The wider conclusion is the useful one. Three efficiency hypotheses have now been
measured and all three found the cost spread thin: `make_move` removal bought 4%
(8i), halving allocation bought nothing (8j), and the remaining candidate caps
out near 15%. **There is no concentrated hotspot left, so constant-factor work
cannot deliver the 4-20x that 8h says coverage requires.** Speed is a spent
direction at this depth.

What remains is node-count reduction: solving the same positions by visiting
fewer nodes -- ordering quality, transposition effectiveness, and restrictions.
That is not a new idea here, it is a re-derivation of why the restriction
portfolio worked. The portfolio's gains (8f: 39/60 to 44/60) came from searching
different, smaller problems, not from searching the same problem faster, and this
is the measurement that explains why that was the axis that moved.

The benchmark is kept in-tree as a `bench_movegen` CMake target, excluded from
the default build: attribution by nested timing is the only method here that has
produced a trustworthy answer, and it should not have to be rebuilt from scratch
next time.

### 8l. Transposition Density Is Low, And The Memory Default Was Below The Knee

8k closed constant-factor speed and pointed at node-count reduction. The first
candidate there is the transposition table, whose effectiveness had never been
measured. `--profile` already reported the counters.

On the hard holdout position, single-threaded, 20s at `-M 512`: 9.1M nodes, 9.1M
probes, **1.45M hits -- a 15.9% hit rate** -- 7.7M stores, the table 95% full
with **4.9M evictions**. Two-thirds of stores evicted something, which looks like
straightforward thrashing.

It is not. Sweeping the budget:

| budget | nodes in 20s | hit rate | full | evictions |
|---|---|---|---|---|
| 256 MB | 8.97M | 15.6% | 98% | 6.12M |
| 512 MB | 9.12M | 15.9% | 95% | 4.89M |
| 1 GB | 9.57M | 16.1% | 91% | 2.80M |
| 2 GB | 10.49M | 16.6% | 76% | **0** |

Eliminating eviction *entirely* moves the hit rate by one point. **The ~16% hit
rate is intrinsic**: at these depths the search tree barely transposes, and 94%
of the hits it does get are disproofs. No replacement policy, table sizing or
sharing scheme can do much, because the entries are not being lost -- they are
not being asked for. That closes the transposition table as a lever, and it is
the same shape of answer as 8i and 8j: the cost is not where it looked.

What memory does buy is throughput, by not doing eviction work: 17% more nodes
at 2 GB than 256 MB. Held out at the operating point (32 threads, 15s) that is
52/60 at 512 MB against **53/60 at 2 GB**, gaining one position and losing none.
One position is normally noise, but here it has a mechanism behind it and a
monotone curve, so it is reported as weak-positive rather than dismissed.

Two consequences, both promoted. The default budget was **64 MB**, well below the
knee of that curve for the problems this engine targets; it is now 256 MB.
And the budget is an entry-count ceiling derived from an estimated bytes-per-entry,
excluding fixed overhead, so it is not a hard cap on process memory: a 64 MB
request peaks near **91 MB** resident, 42% over, while 512 MB and 2 GB requests
stay under because the search does not fill them. Small budgets overshoot
proportionally most. That is now stated in the README rather than left for a user
to discover.

The README's standing claim that resources do not buy reach was also corrected.
It was true as measured -- at mate-in-10 -- and 8h had already shown it fails one
depth down. It now says which depth it describes.

### 8m. The Defender Side Is Already Tight, And Refutation Hints Cost More Than They Save

With speed (8i-8k) and transpositions (8l) closed, the remaining candidate for
node-count reduction was the defender side: `prove_defender` must enumerate
*exactly* the legal replies for the certificate, so any avoidable work there is
paid at every AND node.

Four counters needed for this -- `defender_refutations` and the three
`refutation_hint_*` -- were being collected and never emitted. `--profile` now
reports them.

Measured on the hard holdout position, single-threaded, 20s:

| quantity | value |
|---|---|
| defender nodes | 4.89M |
| legal replies generated | 69.9M (14.3 per node) |
| replies actually tried | 4.47M (0.9 per node) |
| nodes refuted | 78% |
| replies tried per refutation | **1.18** |

The ordering is already about as good as it can get: where a refutation exists,
the first reply examined is nearly always it. There is no headroom in *which*
reply is tried first.

There is apparent headroom in the other number -- 14.3 replies generated against
0.9 tried, so 93% of defender generation is discarded. `--lazy-defender` exists
for exactly that and is not in the promoted arguments. Enabling it cuts defender
move work from 59.6M to 3.9M, a 93% reduction, and changes the node rate by
**0.5%**. It also leaves refutation quality untouched at 1.19 tries per
refutation -- which is its own result: the eager path scores and orders every
defender reply, the lazy path orders nothing, and the first move refutes just as
often either way. **Defender move ordering buys nothing measurable.**

That is the fourth time work-reduction has failed to become time-reduction (8i
4%, 8j nothing, 8k capped, and now 93% of defender generation for 0.5%). The
consistent explanation is the one 8k measured directly: no single stage of a node
dominates, so removing any one of them moves the total very little.

Refutation hints were then measured for the first time and are **net harmful**:

| configuration | node rate | tries per refutation | hint hit rate |
|---|---|---|---|
| baseline | 466 k/s | 1.177 | -- (inactive) |
| `--refutation-hints` | 423 k/s | 1.180 | 4% |
| `--lazy-defender --refutation-hints` | 435 k/s | 1.178 | 5% |

A 4-5% hit rate, no improvement in tries per refutation, and a 9% throughput
cost. The feature cannot help for a structural reason: it exists to move a known
refutation to the front, and the front is already right 1.18 times out of 1.18.
It is correctly defaulted off, and stays off -- but it was off by inheritance
rather than by evidence, and is now off on measurement. Rejected.

The defender side is therefore closed as a lever, and with it the last item on
the node-count list that did not require changing the problem being searched.
Every remaining avenue for capability at this depth runs back through the
restriction portfolio.

### 8n. The Shipped Defaults Were The Untuned Ones (promoted)

Every measured win in this document was reachable only through an explicit flag.
The defaults were the configuration each of those measurements had been run
*against*: `proof_hints`, `move_reserve`, `inplace_order`, `keep_iter_tt` and
`ordered_check_shortcut` all off, `threads` 1, and the restriction portfolio --
the single largest source of capability in the engine -- off.

The benchmark registry carried a fourteen-token invocation, so no benchmark had
ever exercised what a user actually gets. Measured on the held-out 60, 15
seconds:

| invocation | solved |
|---|---|
| `echest -z 8 --time-limit 15 -` | 26/60 |
| the promoted incantation | 52/60 |

**Exactly half.** A reader following the Usage section got half the engine, and
no gate would ever have caught it, because every gate ran the incantation.

The defaults are now the measured-best configuration: the five tuning flags on,
`--threads` resolved to the `auto` value (`min(cores, 16)`) via a sentinel so an
explicit `--threads 1` is still honoured exactly, and the portfolio on. The same
bare invocation now solves **52/60**.

Two soundness points, since defaults are load-bearing in a way flags are not.
The portfolio is safe as a default because a restriction only removes attacker
options, so a mate it finds is real, and under iterative deepening it cannot
report a non-minimal depth either: no lane can prove a mate at a depth where none
exists, so the first depth at which *any* lane succeeds is still the true
minimum. And `--direct-depth` was deliberately **not** made default despite
appearing in every benchmark here -- it proves "a mate within N" instead of "the
shortest mate is N", and weakening the advertised guarantee is not a default's
decision to make. The new default reaches 52/60 without it.

The in-repo test suite caught the change, which is the system working: a check
asserting "portfolio is off by default" failed. It now asserts the opt-out
(`--no-portfolio` restores the single unrestricted search) and the new contract
(the portfolio engages by default with a time limit), so the behaviour stays
pinned in both directions rather than merely being re-pointed at whatever the
code does.

Also corrected: the README stated that waiting longer never helps, generalising
the mate-in-10 saturation result across all depths. That is the second such
correction after 8l, from the same root cause -- a result true at one operating
point written down as though it described the engine.

### 8o. The Corpus Did Not Round-Trip, And The Registry Measured A Dead Configuration

8n moved the tuned settings into the defaults, which left the benchmark registry
pinning `-M 64` and `--threads 8` -- both now *worse* than what ships -- and no
portfolio. Every benchmark was therefore characterising a configuration the
engine no longer has. The promoted entry now runs the shipped defaults
(`-5 -z {mate} -`), and the previous invocation is preserved verbatim as
`chest_E_legacy_args` so reports dated before 2026-08-01 stay reproducible
rather than merely being contradicted.

Smoke-testing that entry turned up two input-handling defects that no gate could
have caught, because every gate fed the engine input the engine had generated:

**Depth was inferred only from `#N`.** That is the matetrack spelling. This
repository's own corpora use `dm N` -- `tests/mates.epd` is literally documented
as `<fen4> ; dm <depth>` -- and so does echest's own output. Piping the shipped
corpus in searched nothing at all, silently, because a missing depth is not an
error. Both spellings are now accepted, which also makes a run's output valid
input to another run.

**Comment lines were reported as errors.** `tests/mates.epd` opens with two `#`
comment lines and each produced `error input`. Lines whose first non-blank
character is `#` are now skipped; a FEN cannot begin with `#`, so this cannot
mask a real position.

Together these meant the single most natural invocation a new user would try --
`echest - < tests/mates.epd` -- printed two errors and solved nothing. Both are
now pinned by `test_corpus_ergonomics`, including the round-trip property, and
the suite is at 140 checks.

The pattern is worth naming, because it is the third instance. 8n found the
defaults untested because every gate passed explicit flags. This finds input
handling untested because every gate constructed its own input. A test suite
that builds its own inputs and its own configuration validates the engine
against itself, and is blind in exactly the direction a first-time user
approaches from. The fix in both cases was to make a gate consume the shipped
artefact as shipped.

### 8p. The Published Tree, Checked As Published

8o established that gates which build their own inputs validate the engine
against itself. The same argument applies to the tree: every check so far ran
against the working copy, inside a larger private workspace, which is not what a
reader receives.

Checked as published, by extracting the tracked files with `git archive` into an
empty directory and running the documented build:

| step | result |
|---|---|
| extracted tree | 22 files, no build artefacts, no absolute paths |
| `cmake -S . -B build` | configures |
| `cmake --build build` | builds |
| `ctest` | 1/1, the full self-test, 11.4 s against a 900 s timeout |

The build path a reader is told to follow works, and the CI workflow's exact
sequence reproduces locally. (The workflow itself still cannot run: GitHub reads
workflows only from a repository root, and it sits at `chest-e/.github/` awaiting
extraction. That is deliberate and documented, but it does mean CI is staged
rather than active.)

What extraction *did* break was documentation. The docs were written while
chest-e was a subdirectory beside a private benchmark harness, and several
referenced it:

- two places, including the status document, told the reader that certificates
  are verified by a proof-tree script in the private harness. The engine ships
  exactly that capability as `tools/verify_proof.py`. A reader following the
  instruction would conclude the verifier was missing, when the most important
  claim the project makes -- that proofs are independently checkable -- is the
  one thing it does ship a tool for;
- two more cited harness scripts for profiling and mining, which genuinely are
  not shipped and now say so.

`test_docs_reference_shipped_files` now fails on any documentation reference
that does not resolve inside the tree, with an allowlist for deliberately
external citations (WinChest's own `Options.txt`). The gate was verified by
injecting a dangling reference and confirming it fails with the file and line,
because a gate that has never failed has not been shown to work.

The architecture document also passed 1,300 lines and 46 subsections with no way
to navigate it. It now opens with a generated index. The section numbers are
deliberately **not** renumbered despite being out of order: they are cited by
each other and by commit messages, so their stability is worth more than their
tidiness.

### 8q. Specifying The Certificate Found A Hole In The Verifier

The certificate is the project's central claim: that you need not trust the
prover. Until now it existed only as emitter code, a reference verifier, and
prose. Anyone writing an independent checker -- the entire point of the format --
had to reverse-engineer it. `docs/PROOF_FORMAT.md` now specifies the grammar, the
obligations a verifier must discharge, and, as importantly, what the certificate
does **not** claim.

Two non-claims are worth stating because both are easy to assume. A certificate
proves "the attacker can force mate in at most N", not that N is minimal:
minimality follows from iterative deepening refuting every shorter depth first,
which is a property of the search discipline and cannot be confirmed from the
certificate alone -- and under `--direct-depth` it is not claimed at all. And a
certificate is not canonical: under `--portfolio-parallel` the same position may
yield different valid proofs on different runs.

Writing obligation 3 -- that a defender node lists *exactly* the legal replies --
exposed a hole in the shipped verifier. A node `{"a": <move>, "d": []}` was
accepted whenever the move left the defender with no legal reply, because
"listed equals legal" is vacuously true when both are empty; the recursion then
added a ply and returned success. That accepts a **stalemate as a forced mate**.

The reason it had never been caught is instructive. A whole output line carrying
such a certificate *was* rejected -- by the principal-variation check, which
independently requires the pv to end in checkmate. From outside the tool the
behaviour looked correct. The hole was reachable only for a stalemate in a branch
the pv does not follow, and confirming it required calling `verify_node`
directly. A second, unrelated check was masking a soundness bug in the first.

Fixed by rejecting an empty reply list outright, which is never legitimate: the
engine emits mate as a leaf. The adversarial suite now forges certificates six
ways rather than two -- omitted defence, invented illegal defence, duplicated
defence, non-mating leaf, illegal attacker move, empty reply list -- plus a
direct check that the node logic itself rejects stalemate-as-mate. The verifier
also now explains a duplicate rejection instead of failing with an empty reason,
which it previously did because it compared sorted lists but diagnosed with sets.

148 checks. The general lesson is that a format with one implementation has no
specification, only behaviour; writing the specification is what turns "what it
does" into "what it must do", and the gap between those two was a soundness bug.

### 8r. Specifying The Output Line Found A Regression I Had Introduced

8q's lesson -- that a format with one implementation has only behaviour, not a
specification -- applies equally to the output line, which the benchmark
harness, the verifier and the test suite all parse without any of them agreeing
on a written definition. `docs/OUTPUT_FORMAT.md` now defines it.

The contract has exactly four outcomes, and one of them is encoded by silence:

| outcome | line |
|---|---|
| proved | `...; bm M; dm D; pv ...[; proof J][; via NAME];` |
| disproved | `...; acs S;` -- **nothing follows** |
| gave up | `...; timeout;` |
| bad input | `<original>; acn 0; acs 0; error input;` |

Writing that table exposed a regression I had introduced in 8n. Making the
portfolio the default meant that *every genuine disproof was being reported as a
timeout*: the parallel portfolio set `timed_out` whenever no lane proved a mate,
and the sequential one OR-ed the flag across all lanes. The starting position,
disproved exhaustively at mate-in-1 in eight nodes, came back marked `timeout`.

The distinction destroyed is the one the emitting code's own comment calls out --
"distinguish gave up from proved there is no mate. Without this a released tool
would report the same thing for both". It survived because the negative-control
gate only asserted that no `dm` token appears, which is true of both readings.

The fix follows from soundness rather than from taste. Restricted lanes are
sound but **incomplete**: a mate found under a restriction is real, but failing
to find one under a restriction proves nothing, so a restricted lane's timeout
carries no information about whether a mate exists. Only the unrestricted lane
can settle a disproof, so only its completion decides the outcome. Both
portfolio paths now ask exactly that.

The negative-control gate now checks both directions -- no false mate *and* no
false timeout -- and a conformance test pins all four outcomes, their field
order, and that an illegal-but-parseable position is rejected as bad input
rather than answered. 170 checks.

Twice now, writing down a contract has found a defect in it: 8q a stalemate
accepted as a mate, 8r a disproof reported as a timeout. Both were invisible to
gates that tested the thing rather than the claim about the thing.

### 8s. Documented Defaults, Checked Against Real Ones

8n found the shipped defaults were the untuned ones, undetected because every
gate passed explicit flags. The obvious follow-up is a gate that compares what
`--help` claims against what the engine does. Building it turned up two things.

First, eleven paired options -- `--proof-hints | --no-proof-hints` and the rest
of the tuning section -- **documented no default at all**. After 8n flipped five
of them on, a reader could not tell from the help which side was active. Each
now states it, including `--no-refutation-hints (measured harmful)`, which
records 8m's result where a user will actually see it.

Second, and more interesting, the natural behavioural check does not work.
"Passing the documented default must change nothing" is exactly right in
principle, and on a mate-in-2 **none of seven non-default flags changed the
output either** -- these settings preserve exactness and, on small positions,
node counts too. At mate-in-5 only two of six discriminated. Such a gate passes
whether the documentation is true or false, which is the same failure mode as a
gate that has never failed.

So the engine now reports its own resolved configuration. `--print-config`
prints the effective settings as JSON after every default and sentinel has been
applied -- the configuration that *would run*, not a restatement of the flags
given. It earns its place independently of testing: a run becomes reproducible
from its own report, which matters for an engine whose output is a proof.

The gate compares the help text against that JSON, reading documented values out
of the help rather than hard-coding them so drift on either side is caught. It
covers all eleven paired defaults, five numeric defaults, the `auto` thread
count, the portfolio default from 8n, and that `--direct-depth` is *not* default
-- the last because defaulting it would silently weaken the advertised claim from
"the shortest mate is N" to "a mate within N".

Verified by flipping `proof_hints` to false in the source and confirming the
suite fails with `proof_hints=False, help claims True`, then restoring it. 192
checks.

### 8t. Stress-Testing The Abort Invariant Found A Crash

The abort invariant -- an abandoned search records no verdict, so it can never be
read back as a disproof -- is what makes cancellation, time limits and eviction
safe. It is asserted in comments across the codebase and had never been attacked.

Its observable consequence, now that 8r makes disproof and timeout distinct, is
sharp: on a *known mate*, a truncated search must say `timeout` and must never
produce the silent no-mate form. Running the mate and no-mate corpora
interleaved, under tiny budgets, heavy eviction, forced splitting and many
threads, tests exactly that -- a poisoned entry surfaces either as a false mate
on a no-mate line or as a false disproof on a mate line.

The invariant held: 255 runs, no false mates, no false disproofs, no wrong
depths. What the stress found instead was a **crash**.

`--threads 16 --parallel-min-nodes 1 --time-limit 0.005` terminated the process
mid-batch, emitting 11 lines of 29 and losing every remaining position. The
mechanism: `restrict_attacker_moves` can remove *every* attacker move at the
root, but the empty-move-list guard runs **before** the restriction is applied.
With no moves left, the worker count is zero, and the thread pool computed
`reserve(static_cast<std::size_t>(worker_count - 1))` -- an unsigned `-1`, so a
reserve of `SIZE_MAX`. The resulting `std::length_error` escaped a portfolio lane
and called `std::terminate`.

Reachable with the default thread count plus one documented flag, and only
through a restricted lane, which is why nothing had hit it: restrictions became
default-on only in 8n, and no gate ran the portfolio under stress.

Two diagnostic missteps are worth recording. The first hypothesis was thread
creation failing under churn; making both spawn sites exception-safe changed
nothing, because the exception came from `reserve`, not from `std::thread`. The
instrumentation that identified it caught nothing either -- the throw was outside
the worker body. Only bisecting to a single position and a single flag located
it. The exception-safety around spawning was kept anyway: it is correct on its
own terms, and sound because root moves are claimed from a shared atomic counter
rather than statically partitioned, so running with fewer workers loses speed,
never coverage.

Fixed by re-checking for an empty move list after the restriction, where "no
permitted attacker move" is simply "no mate under this restriction". Both
reserve sites now clamp with `max(0, n - 1)` so a recurrence degrades to a
missing reservation rather than a crash. The stress matrix is now a permanent
gate across five configurations, and it fails against the pre-fix binary with
the original `std::length_error`. 202 checks.

### 8u. The Property The Portfolio Actually Rests On

The restrictions were validated against the WinChest binary for **agreement**.
That is a weaker and different claim than the one the portfolio depends on.
Agreeing with another engine about which positions a restricted search solves
says nothing about whether those answers are sound with respect to the
*unrestricted* problem -- and the portfolio's entire justification is that a mate
found under a restriction is a real mate. That was never tested.

Stated properly, and now gated: a restriction only removes attacker options, so
for every restriction R, anything R proves must also be provable unrestricted,
and never at a *shorter* depth. Measured over the corpus with untimed sequential
searches, so every search completes: **zero violations** across thirteen
restrictions.

The nesting properties hold too -- a tighter numeric bound solves a subset of a
looser one (K2 ⊆ K3 ⊆ K4, X2 ⊆ X4 ⊆ X6, R1 ⊆ R2) -- but establishing that
corrected a misconception of mine. I first checked C1 ⊆ C2 ⊆ C4 and it failed.
That is not a defect: `-C` is a **bitmask**, not a strictness ladder. C1 ("only
own check moves"), C2 ("no opponent checks") and C4 ("no opponent captures") are
independent conditions and are not comparable at all. What makes a mask stricter
is *adding bits*, so the real property is that a mask solves a subset of every
mask whose bits it contains: C3 ⊆ C1, C3 ⊆ C2, C6 ⊆ C2, C6 ⊆ C4. All hold. The
portfolio's C-lanes are therefore unordered with respect to each other, which is
consistent with 8f having selected them by set cover rather than by strength.

The gate also requires that certificates produced *under* a restriction verify,
since the portfolio can return one and that is the path a caller sees.

Verified adversarially rather than assumed: injecting a fault that drops one
defender reply when a KingSquares restriction is active produces ten failures,
and the shipped certificate verifier independently rejects the resulting proofs
with `missing defences ['f8c5']`. Two unrelated mechanisms catch the same
injected unsoundness, which is the arrangement worth having. 228 checks.

### 8v. Order Independence Gated; The Lowest-Index Rule Is Unobservable

Two invariance claims, one gated successfully and one that turned out not to be
testable at all.

**Order and batching.** State could in principle leak between positions in a run:
the iterative table is kept across depths and a shared table exists. It does not
-- `solve_line` builds a fresh `Search` per position and the only `static` is the
immutable portfolio table -- and answers are now checked identical across
forward, reversed and shuffled order, and against one-position-per-process.

That gate discriminates, verified by injecting a counter that shortens every
second position's depth: the shuffled and solo arms both catch it. **The reversed
arm does not**, because reversing an even-length list preserves parity, so a
parity-based leak survives it. Reversal is the weakest of the three and is kept
only for the cheap cases the others might miss.

**Scheduling.** The root split accepts the *lowest-index* successful root move
specifically so the key move does not depend on which worker finishes first.
Answers are identical at 1, 2, 4, 8, 16 and 32 threads -- but that check could
not be shown to discriminate. Injecting the opposite rule, accepting whichever
root move finishes first, changes nothing observable:

| corpus | result |
|---|---|
| shallow mates, 16 threads | identical |
| shallow mates, forced splitting, 32 threads, no gate | identical |
| hard mate-in-8 holdout positions | identical |
| a constructed dual (`c1c8` and `d1d8` both mate) | identical |

The reason is structural. The rule only matters when **two root moves mate and
workers genuinely race**, and in practice the ordered-first move resolves before
any race can occur -- on a dual mate-in-1 the whole search finishes before a
second worker reports. So the lowest-index rule is currently doing no work: it is
insurance against a case the search never reaches, not an active mechanism.

That is worth stating plainly rather than leaving the thread-count check to imply
more than it establishes. It is a consistency check that would catch gross
breakage; it is **not** evidence for the lowest-index property, which rests on
being correct by construction. A gate that cannot fail proves nothing, and this
is the second time that trap has appeared -- 8s hit it with defaults, where the
fix was to make the engine report its own configuration. There is no analogous
escape here: the property has no observable consequence to report.

**Corpus hygiene.** `mates.epd` held 17 lines but only 11 distinct positions, six
being exact duplicates, so every coverage count it produced overstated the
corpus by half. Deduplicated. `nomate.epd` also repeats FENs, but those are *not*
duplicates -- the same position at depth 1 and depth 2 are different claims -- and
the file now says so, since deduplicating it would silently delete test cases.

### 8w. Persistent Service Mode Already Existed, By Accident

The last untouched backlog item was a persistent service or batch mode. It turned
out to be almost entirely present already: one process reads positions from
stdin, answers each, and carries no state between them -- which 8v had just
established and gated. Nothing needed building.

What did need fixing was the part holding it up. Each answer reached the client
promptly, but nothing in the code flushed: it worked because `std::cin` is tied
to `std::cout`, so the *next* read flushed the previous answer. That is guaranteed
by the standard, not platform luck -- but it is a load-bearing subtlety with no
mention anywhere in the source. `sync_with_stdio(false)` and `std::cin.tie(nullptr)`
are the two most routine I/O throughput tweaks in C++, and either one silently
converts a streaming service into output that appears only at exit.

Result lines are now flushed explicitly. Verified both ways: removing the flush
*and* applying those two tweaks makes the new gate report **0 of 3 positions
answered** while the process stays open, and the cost of the flush is
unmeasurable -- 1,320 positions in 42.41s against 42.47s without it, a search
per position dwarfing a flush per position.

The service contract is now stated in `OUTPUT_FORMAT.md`, including the part
users would otherwise have to discover: a long-lived process gains nothing from
prior work, because there is no cross-position cache to warm. Keeping the engine
resident saves process startup and nothing else. Presenting that honestly seemed
better than implying a warm service is faster than a cold one.

This is the fourth backlog item to close by measurement rather than
implementation -- after DFPN (8e), bitboards (8i, 8k) and the transposition table
(8l). The pattern is consistent enough to be worth naming: items on that list
were guesses about where value lay, made before any of it was measured, and most
have not survived contact with measurement.

### 8x. Mate-In-10 Re-Measured: 3/20 Was Obsolete, It Is Now 18/24

8d concluded that mate-in-10 is capability-limited, on a measured 3/20 that was
flat across threads, memory and time. That number has been quoted ever since,
including in the README. It was measured before the derived portfolio (8f), before
the memory default moved off the knee (8l), and before the tuned settings became
the defaults (8n) -- that is, on a configuration the engine no longer has. 8h
already caught this class of error once, where a saturation result true at
mate-in-10 was silently describing mate-in-8. This is the same error at the depth
it was originally measured.

Re-measured on 24 fresh mate-in-10 positions, none previously used, at 30 s and
32 threads with `--direct-depth`:

| axis varied | solved |
|---|---|
| unrestricted search alone | 13/24 |
| **with the derived portfolio** | **18/24** |
| 30 s → 120 s | 18/24 (+0) |
| 8 → 32 threads | 17 → 18 (+1) |
| 256 MB → 2 GB | 18/24 (+0) |

All 18 certificates verify independently against the shipped checker.

Two conclusions, and they point in opposite directions from 8d's.

**The coverage figure was badly stale.** 3/20 (15%) has become 18/24 (75%) --
mostly from work that was never aimed at mate-in-10 at all. The restriction
portfolio was derived on mate-in-8 (8f) and is the single largest contributor
here too, adding five positions the unrestricted search cannot reach.

**The saturation conclusion was right, and remains right.** Four times the time
buys nothing. Four times the memory buys nothing. Four times the threads buys one
position, which is noise. At this depth the portfolio is the only axis that pays,
exactly as at mate-in-8 (8f) and for the same reason: it changes *which problem*
is searched rather than how fast the same problem is searched. Every other axis
this project has measured -- faster nodes (8i, 8j, 8k), a bigger table (8l),
alternative routes (8e), stricter restrictions (8g) -- has failed, and they have
now failed at both depths.

The practical statement for a reader, now in the README, is that mate-in-8 is
budget-limited and mate-in-10 is not. Spending more time helps at one depth and
does nothing at the other, and there is no configuration in which hardware
substitutes for the portfolio.

### 8y. Lane Strength Is Depth-Dependent; The Lane Set Is Not

8x showed the portfolio is the only axis that buys reach at mate-in-10, but the
table it uses was derived entirely on mate-in-8 (8f). Whether the best lane set
is depth-specific was the obvious question, and mate-in-10 is where an answer
would matter most, since nothing else works there.

Swept twenty restrictions standalone over 24 fresh mate-in-10 positions, disjoint
from the held-out 24. The per-lane ranking is **completely different** from
mate-in-8:

| lane | mate-8 (of 60) | mate-10 (of 24) |
|---|---|---|
| unrestricted | 22 | 6 |
| K3 | 26 | **10** |
| K4 | 21 | **9** |
| K2 | 26 | 6 |
| X4 | 11 | 6 |
| R2 | 8 | 2 |
| R1 | 5 | 1 |

At mate-in-10, `K3` and `K4` beat the unrestricted search outright, while the
threat-depth lanes -- which hold three slots in the promoted table -- solve one to
three positions between them. Greedy cover on this data reaches 16/24 against the
promoted table's 13/24, and 16 is the oracle union of all twenty candidates.

A merged table maximising coverage across both depths (`unrestricted, K2, K3, K4,
R2, C6, R1, Rq2`) scores 43/60 and 15/24 on training, against the promoted
table's 44/60 and 13/24: one mate-in-8 position traded for two at mate-in-10.

At the operating point it is **exactly identical**. 32 threads, 30 s, held-out
mate-in-10: 18/24 for both tables, the same 18 positions, nothing gained and
nothing lost. Rejected. The mate-in-8 regression arm was never needed, since a
change that cannot show a gain on its target depth has nothing to trade against.

The explanation matters more than the result. Under `--portfolio-parallel` every
lane runs concurrently with the **whole** budget, so a lane being individually
weak costs nothing -- it occupies a few threads that root splitting could not have
used anyway (8d). What matters is only whether the union of lanes covers the
problem, and 8f already saturated that union. Per-lane strength is therefore a
property of the *training* conditions -- 2 threads and a 20 s slice -- and has
almost no bearing on the operating point, where the question is coverage alone.

That also explains why training coverage understates reality: the twenty-candidate
oracle reaches 16/24 under training conditions while the promoted eight lanes
reach 18/24 at the operating point. Training numbers rank candidates; they do not
predict outcomes. This is the second table-tuning attempt to die at exactly this
step, after 8g, and for the same reason.

The practical conclusion is that the portfolio needs no depth awareness. One
table, derived once at one depth, is not merely adequate at another depth -- it is
indistinguishable from a table derived for that depth specifically.

### 8z. A Record Is Not An Argument

This document had reached fifty-six sections in the order they were discovered.
That is the right shape for evidence -- several conclusions here were later
overturned by re-measurement, and the sequence is part of what makes the
corrections legible -- but it is the wrong shape for a reader deciding whether to
trust or use the engine. Fifty-six chronological findings do not state what the
capability comes from.

`RESULTS.md` now does, in about a hundred lines: the one idea that produced
essentially all of the capability, the measured reach, a table of everything
implemented and rejected with its numbers, the defects found and what found them,
and an explicit statement of what would actually move the needle. It says plainly
that nothing on the original backlog would.

Two things were added so that a reader need not take any of it on trust.

The held-out position sets now **ship with the engine**, in `benchmarks/`, with a
note on why they are worth quoting: no tuning, lane selection or parameter choice
used them, the portfolio having been derived on a disjoint training set. Re-tuning
against them would destroy the only property that makes them evidence.

`tools/reproduce_results.py` re-runs the documented measurements and prints what
it gets beside what is claimed. Verified from a clean extraction, so a reader who
clones the published tree can check the headline numbers rather than believe them.
The mate-in-8 figure was re-measured while writing this and reproduced exactly at
52/60.

Writing it also caught a conflation of my own. The mate-in-8 escalation series
(51/60, 56/60, 60/60 at 15 s, 60 s and 300 s) was measured at 32 threads with
`--direct-depth`, while the headline 52/60 is the plain default configuration.
Presenting them as one series would have implied the default reaches 60/60 given
time, which is not what was measured. They are now separate lines, each with its
configuration -- the fourth instance in this document of a number that meant less
than it appeared to until its configuration was attached to it.

### 14. Held-Out Sets Decay Into Development Sets

Every reach figure in this project rested on two position sets totalling 84
positions. Both were held out from *tuning* in the strict sense -- no parameter
was fitted to them -- and both were nonetheless consulted repeatedly: the
mate-in-8 set informed roughly ten promote-or-reject decisions across 8g, 8i, 8j,
8n, 8s and 8y. Each of those decisions kept the engine on one branch rather than
another **because of how that set responded**. That is fitting, conducted by hand
and one bit at a time.

Two fresh sets were minted from positions no suite had ever contained -- 200 at
mate-in-8, 60 at mate-in-10 -- and measured once:

| measurement | development set | evaluation set | 95% CI |
|---|---|---|---|
| mate-in-8, default, 15 s | 52/60 = 86.7% | **159/200 = 79.5%** | 73.4-84.5 |
| mate-in-10, portfolio, 30 s | 18/24 = 75.0% | **44/60 = 73.3%** | 61.0-82.9 |
| mate-in-10, no portfolio | 13/24 = 54.2% | 29/60 = 48.3% | 36.2-60.7 |

The mate-in-8 figure fell by seven points, and the old value lies *outside* the
new interval. The mate-in-10 figures barely moved. The difference between them is
how often each set was consulted: about ten times against two or three. The size
of the optimism tracks the number of decisions taken while looking.

Nothing here was cheating, which is what makes it worth recording. No parameter
was fitted to those 60 positions. They were simply the yardstick every close call
was measured against, and a yardstick consulted often enough stops being
independent of the thing it measures.

Three consequences, all applied:

- The published figures are now the evaluation numbers, with confidence
  intervals, and `benchmarks/` ships both kinds of set clearly labelled: the
  evaluation sets marked *used once, do not tune against them*, the development
  sets marked as no longer evidence of absolute reach though still fine for
  comparing two builds, which is what they were used for.
- The portfolio's value is now stated far more precisely than before: **+15
  positions of 60 at mate-in-10, losing none**, twenty-five points of solve rate.
  A larger sample sharpened the one claim that matters rather than softening it.
- Quoting a bare fraction like 52/60 without an interval implied a precision the
  sample never supported. Every reach figure now carries one.

The general form of this, and the reason it sits alongside the stale-measurement
corrections in 8h, 8l and 8x: a number is only evidence about the engine if
nothing about the engine was chosen by looking at that number. Freshness is
consumed by use, and it is consumed silently.

### 15. The Mate-In-8 Budget Curve, On Fresh Positions

8h established that mate-in-8 is budget-limited, on the 60-position development
set: 51/60 at 15 s, 56/60 at 60 s, 60/60 at 300 s. "Nothing at this depth is out
of reach" has been quoted since. Measured on the 200 fresh evaluation positions:

| budget | cumulative | 95% CI | newly solved |
|---|---|---|---|
| 15 s | 160/200 = 80.0% | 73.9-85.0 | -- |
| 60 s | 181/200 = 90.5% | 85.6-93.8 | +21 |
| 240 s | **192/200 = 96.0%** | 92.3-98.0 | +11 |

The qualitative claim survives and is now much better quantified: sixteen times
the budget converts 80% into 96%, and the curve is still climbing at 240 s. That
is the strongest statement this engine can make about itself, and it is the exact
opposite of the mate-in-10 picture, where four times the budget buys nothing.

The absolute claim does not survive. **Eight positions resist 240 s**, so
"nothing at this depth is out of reach" was an artefact of a 60-position sample
in which all sixty happened to fall. A 100% observation on 60 positions is
consistent with a true rate anywhere above about 94%, and the measured rate is
96% -- the two are not in conflict, but only one of them should have been written
down as a property of the engine.

This is the third quantity in this project that shrank when measured on fresh
positions, after the mate-in-8 solve rate and the mate-in-10 figures (14). In
each case the direction was the same: the development set flattered the engine,
by an amount that scaled with how often it had been consulted. Recording it here
rather than quietly restating the number, because the pattern is the useful part.

The evaluation protocol is now mechanical rather than a matter of discipline.
`tools/mint_eval_set.py` draws a fresh set while excluding every position any
existing set contains, records the draw in `benchmarks/MANIFEST.json`, and
refuses to overwrite a set that already exists -- a minted set is spent the first
time it is measured and is never regenerated. `benchmarks/README.md` states the
protocol: mint before the work, do not look during, measure once after.

### 16. The Frontier Has No Structure To Exploit

`RESULTS.md` says the only remaining route to more reach is a restriction family
the WinChest set does not contain. Such a family would have to exploit some
property the unreachable positions share, so the obvious first step is to ask
what they do share. Splitting the 200 fresh mate-in-8 positions at a 15 s budget
gives 159 solved and 41 not, and comparing the two groups:

| feature | solved (159) | unsolved (41) | ratio |
|---|---|---|---|
| attacker moves at the root | 24.4 | 30.4 | 1.25x |
| mean defender replies | 14.0 | 21.1 | **1.51x** |
| worst-case defender replies | 18.1 | 26.1 | 1.44x |
| defender pieces | 8.0 | 10.0 | 1.26x |
| attacker pieces | 6.5 | 8.7 | 1.35x |
| defender king mobility | 0.58 | 0.71 | 1.21x |

**There is no structural signature, only size.** Every feature moves in the same
direction by a similar factor: the unsolved positions have more of everything.
Compounding the branching over eight plies -- four attacker moves and four
defender replies -- estimates `1.25^4 x 1.51^4`, about **twelve times the tree**.
That is consistent with what the budget curve measured independently: sixteen
times the budget converts 80% into 96% (15).

This is a negative result for the new-restriction idea, and a fairly strong one.
A restriction earns its place by removing attacker options in positions where the
remaining options still suffice. The unreachable positions do not differ in kind
from the reachable ones -- no distinctive material, no distinctive king
confinement, no distinctive tactical shape -- so there is nothing for a new
restriction to key on. They are the same problems, an order of magnitude larger.

One incidental finding explains something the portfolio work never accounted for.
**Defender king mobility is already near zero in both groups**, 0.58 and 0.71
legal king moves on average. That is why `KingSquares` restrictions are the
strongest lanes at both depths (8f, 8y): they cost almost nothing, because the
defending king is already confined in nearly every position in this corpus, while
pruning every attacker move that would release it. The lane set found by set
cover was exploiting a property of the problem domain that nobody had noticed.

What this leaves: closing the remaining 4% at mate-in-8 needs roughly another
order of magnitude, from budget or from a faster search. 8i, 8j and 8k measured
the available constant factors and found none worth having. The honest position
is that the frontier moves with hardware and with nothing else this project has
found.

### 17. Tablebase Termination Would Reach One Percent Of The Wrong Nodes

16 closed the new-restriction idea and left tablebase termination as the last
named candidate from outside this project's methods. Endgame tablebases give an
exact verdict for positions with few enough pieces, which would let the search
stop early wherever a line reduces that far. Whether that helps depends entirely
on how often proof trees get there.

Walking 107,965 nodes of real certificates from 25 solved mate-in-8 positions:

| tablebase | approximate size | proof nodes reached |
|---|---|---|
| <= 5 pieces | 1 GB | **1.01%** |
| <= 6 pieces | 150 GB | 1.27% |
| <= 7 pieces | 18 TB | 5.02% |

A shippable tablebase reaches one node in a hundred. The complete 7-man set,
eighteen terabytes, reaches one in twenty.

The count is not even the main problem. Those nodes sit at **mean ply 11.5 of a
15-ply proof** -- deep, near the leaves, because pieces only leave the board by
capture and a mating attack does not have many to spare. A node that deep has a
handful of plies left beneath it, so terminating it early saves a subtree that
was nearly free. Had the hits clustered near the root, 1% of nodes could have
meant a large fraction of the work; near the leaves it means almost none.

That is the general point worth keeping: **the value of an early-termination
oracle is set by where its hits fall, not by how many there are.** Counting
reachable nodes without asking where they sit would have made a 5% figure look
like a 5% saving, and it would have been wrong by whatever the branching factor
does over the plies below.

Rejected on measurement, without implementing it. With 16 this closes both
externally-suggested ideas. What remains genuinely unexamined is a search that
reasons about *why* a defence fails rather than enumerating that it does -- which
is a different engine, not an increment to this one.

## Promotion Rule

No E search feature is promoted by intuition. A feature is promoted only after:

1. semantic correctness is unchanged;
2. PV validation is clean;
3. no-mate controls remain clean;
4. speed improves on the relevant frozen suite;
5. regressions are documented and intentionally accepted or rejected.
