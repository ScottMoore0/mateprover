# MateProver Architecture

## Purpose

MateProver is a from-scratch exact directmate prover designed to exceed the current D line in core single-position speed, hard-position proof speed, and batch/service throughput while reducing the chance of proof errors.

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

This document is a measurement log as much as an architecture description.
Most sections record something that was tried, measured, and then promoted
or rejected -- the rejections are kept deliberately, because the reason a
plausible optimisation did not work is usually worth more than the ones that
did. If you are reading it for the first time:

- **What the engine is and why it is exact** -- sections 1 to 5.
- **Where the capability actually comes from** -- 7g (the restriction
  portfolio) and 29 (promoting DFPN), which between them account for most of
  the reach.
- **What is measured out and will not repay more work** -- 16, 17, 30 to 32,
  and 41 to 47.
- **How a change is allowed to ship** -- the Promotion Rule at the end.

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
- [8p. The Published Tree, Checked As Published](#8p-the-published-tree-checked-as-published)
- [8q. Specifying The Certificate Found A Hole In The Verifier](#8q-specifying-the-certificate-found-a-hole-in-the-verifier)
- [8r. Specifying The Output Line Found A Regression I Had Introduced](#8r-specifying-the-output-line-found-a-regression-i-had-introduced)
- [8s. Documented Defaults, Checked Against Real Ones](#8s-documented-defaults-checked-against-real-ones)
- [8t. Stress-Testing The Abort Invariant Found A Crash](#8t-stress-testing-the-abort-invariant-found-a-crash)
- [8u. The Property The Portfolio Actually Rests On](#8u-the-property-the-portfolio-actually-rests-on)
- [8v. Order Independence Gated; The Lowest-Index Rule Is Unobservable](#8v-order-independence-gated-the-lowest-index-rule-is-unobservable)
- [8w. Persistent Service Mode Already Existed, By Accident](#8w-persistent-service-mode-already-existed-by-accident)
- [8x. Mate-In-10 Re-Measured: 3/20 Was Obsolete, It Is Now 18/24](#8x-mate-in-10-re-measured-320-was-obsolete-it-is-now-1824)
- [8y. Lane Strength Is Depth-Dependent; The Lane Set Is Not](#8y-lane-strength-is-depth-dependent-the-lane-set-is-not)
- [8z. A Record Is Not An Argument](#8z-a-record-is-not-an-argument)
- [14. Held-Out Sets Decay Into Development Sets](#14-held-out-sets-decay-into-development-sets)
- [15. The Mate-In-8 Budget Curve, On Fresh Positions](#15-the-mate-in-8-budget-curve-on-fresh-positions)
- [16. The Frontier Has No Structure To Exploit](#16-the-frontier-has-no-structure-to-exploit)
- [17. Tablebase Termination Would Reach One Percent Of The Wrong Nodes](#17-tablebase-termination-would-reach-one-percent-of-the-wrong-nodes)
- [18. Two Builds, Two Versions](#18-two-builds-two-versions)
- [19. An Independent Opinion On The Source](#19-an-independent-opinion-on-the-source)
- [20. Substitutes For The Compilers And Sanitisers That Are Not Here](#20-substitutes-for-the-compilers-and-sanitisers-that-are-not-here)
- [21. Coverage Measurement Found DFPN Was Broken, Not Slow](#21-coverage-measurement-found-dfpn-was-broken-not-slow)
- [22. DFPN Promoted, Then Rejected By The Set Minted To Judge It](#22-dfpn-promoted-then-rejected-by-the-set-minted-to-judge-it)
- [23. Preconditioning Only The Deepest Iteration](#23-preconditioning-only-the-deepest-iteration)
- [24. A Deterministic Budget, And What It Says About DFPN](#24-a-deterministic-budget-and-what-it-says-about-dfpn)
- [25. Where A DFPN Node's Cost Goes, And What That Implies](#25-where-a-dfpn-nodes-cost-goes-and-what-that-implies)
- [26. Reproduction That Does Not Depend On The Reader's Machine](#26-reproduction-that-does-not-depend-on-the-readers-machine)
- [27. Making A DFPN Node Cheap](#27-making-a-dfpn-node-cheap)
- [28. The DFPN Question, Settled As Far As The Evidence Allows](#28-the-dfpn-question-settled-as-far-as-the-evidence-allows)
- [29. DFPN Promoted: Decisive At Mate-In-10](#29-dfpn-promoted-decisive-at-mate-in-10)
- [30. What Promoting DFPN Invalidated](#30-what-promoting-dfpn-invalidated)
- [31. How Deep It Actually Goes, And How It Compares](#31-how-deep-it-actually-goes-and-how-it-compares)
- [32. Root-Split Parallelism Now Contributes Nothing](#32-root-split-parallelism-now-contributes-nothing)
- [33. Parallelism Across Positions](#33-parallelism-across-positions)
- [34. Two Budgets, Two Different Batch Trades](#34-two-budgets-two-different-batch-trades)
- [35. The Third Explanation Was The Right One](#35-the-third-explanation-was-the-right-one)
- [36. Batch Results Stream Instead Of Arriving In Blocks](#36-batch-results-stream-instead-of-arriving-in-blocks)
- [37. The Reproduction Tool Had Stopped Comparing Anything](#37-the-reproduction-tool-had-stopped-comparing-anything)
- [38. Auditing For Comparisons That Compare Nothing](#38-auditing-for-comparisons-that-compare-nothing)
- [39. Memory Is Still Not A Lever, And The Work Has Moved Into DFPN](#39-memory-is-still-not-a-lever-and-the-work-has-moved-into-dfpn)
- [40. Threshold Widening Does Not Help Here (measured, not promoted)](#40-threshold-widening-does-not-help-here-measured-not-promoted)
- [41. Bounding Parallel DFPN Before Building It](#41-bounding-parallel-dfpn-before-building-it)
- [42. `-M` Was A Per-Table Budget Wearing A Total's Name](#42--m-was-a-per-table-budget-wearing-a-totals-name)
- [43. What A Speedup Is Worth, And Root Splitting Under DFPN](#43-what-a-speedup-is-worth-and-root-splitting-under-dfpn)
- [44. The Default Budget Is Per Table, Not A Split Total](#44-the-default-budget-is-per-table-not-a-split-total)
- [45. Two Ceilings: Tables Are Finished, Ordering Is Not](#45-two-ceilings-tables-are-finished-ordering-is-not)
- [46. The Waste Is Per-Ply, Not At The Root](#46-the-waste-is-per-ply-not-at-the-root)
- [47. Heuristic Proof-Number Initialisation: Tried And Rejected](#47-heuristic-proof-number-initialisation-tried-and-rejected)
- [48. Stalemate Goal](#48-stalemate-goal)
- [49. Stalemate Tuning: The Levers Are Already Pulled](#49-stalemate-tuning-the-levers-are-already-pulled)
- [50. Selfmate Goal](#50-selfmate-goal)
- [51. Selfmate Preconditioner: +124 Positions Of 200](#51-selfmate-preconditioner-124-positions-of-200)
- [52. The Restriction Portfolio Transfers To Selfmate](#52-the-restriction-portfolio-transfers-to-selfmate)
- [53. Root Splitting For Selfmate: Correct, Deterministic, And Inert](#53-root-splitting-for-selfmate-correct-deterministic-and-inert)
- [54. A False Proof In The Stalemate Shortcut, And A Route Lane](#54-a-false-proof-in-the-stalemate-shortcut-and-a-route-lane)
- [55. The Starved Lane: A Flag That Cost A Factor Of Nine, Silently](#55-the-starved-lane-a-flag-that-cost-a-factor-of-nine-silently)
- [56. The Sixteenth Thread: A 30x Loss Hidden Below A Threshold](#56-the-sixteenth-thread-a-30x-loss-hidden-below-a-threshold)
- [57. The Worker That Would Not Stop](#57-the-worker-that-would-not-stop)
- [58. The Cooperative Goals, And A Benchmark That Flattered Its Own Engine](#58-the-cooperative-goals-and-a-benchmark-that-flattered-its-own-engine)
- [59. The Composition Features, And An Endgame Fix Refuted By Measurement](#59-the-composition-features-and-an-endgame-fix-refuted-by-measurement)

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

What each selects, as established by probing the binary's behaviour (the
one-line summaries in its own documentation proved misleading twice over, and
are paraphrased rather than reproduced here):

| option | restriction it imposes |
|---|---|
| `-C N` | restricts the attacker to checking moves, under a bitmask |
| `-R N` | restricts to moves carrying a threat, to a given depth |
| `-K N` | admits a move only if it leaves the defending king at most N squares |
| `-P N` | caps how many defender pieces may retain a legal move |
| `-X N` | caps the defender's total legal replies |
| `-I N` | flags governing how threats are counted; semantics never established |

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

- `mateprover\build.ps1` uses the linker no-timestamp option so repeated builds from identical source produce a stable executable hash for benchmark registry pinning.

### 10b. Input Validation And A Castling Bug It Uncovered

The architecture listed "malformed FEN/EPD tests" as part of the verification harness and had never implemented them. Probing the parser with malformed input found no crashes, but three real defects.

**A prover must refuse questions that are not well posed.** `8/8/8/8/8/8/8/KKKKKKKK w - -` -- eight white kings, no black king -- was accepted and reported `dm 1`: a mate claim in a position with no king to mate. An invalid side-to-move letter was silently treated as white, and a malformed en-passant field was silently treated as absent.

`parse_fen4` now rejects: a side other than `w`/`b`, an en-passant field that is malformed or off the third/sixth rank, a pawn on the first or last rank, any king count other than one per side, and a position where the side *not* to move is in check, which is unreachable.

**A castling bug fell out of the same probe.** Castling generation checked the rights bit and the empty squares between, but never that the rook was actually on its corner -- while `make_move` writes a rook onto f1/d1 unconditionally. A FEN claiming a right whose rook is absent therefore generated a castling move that **materialised a piece from nothing**: `4k3/8/8/8/8/8/8/4K3 w K -` produced six legal moves where python-chess gives five.

Every standard perft position has consistent castling rights, which is why six reference positions at depth 4-5 never exercised it. This is the second castling defect in this engine that perft alone could not reach; the first, revoking rights by captured piece type, was found *by* perft. Both were only visible from a direction the existing gates did not cover.

Thirteen illegal-position checks and four castling checks are now part of the suite, including a direct comparison against python-chess on both the phantom-rook and real-rook positions. All 86 positions in the deep-mined and matetrack corpora are still accepted, so the validation rejects only what is genuinely illegal.

### 11. Verification Harness

A certificate format is only worth having if someone other than the engine can check it. `mateprover/tools/verify_proof.py` ships with the prover for that reason: it reads engine output, re-derives every legal move with python-chess, and never consults the engine.

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
| `mateprover -z 8 --time-limit 15 -` | 26/60 |
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
as `<fen4> ; dm <depth>` -- and so does mateprover's own output. Piping the shipped
corpus in searched nothing at all, silently, because a missing depth is not an
error. Both spellings are now accepted, which also makes a run's output valid
input to another run.

**Comment lines were reported as errors.** `tests/mates.epd` opens with two `#`
comment lines and each produced `error input`. Lines whose first non-blank
character is `#` are now skipped; a FEN cannot begin with `#`, so this cannot
mask a real position.

Together these meant the single most natural invocation a new user would try --
`mateprover - < tests/mates.epd` -- printed two errors and solved nothing. Both are
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
workflows only from a repository root, and it sits at `mateprover/.github/` awaiting
extraction. That is deliberate and documented, but it does mean CI is staged
rather than active.)

What extraction *did* break was documentation. The docs were written while
mateprover was a subdirectory beside a private benchmark harness, and several
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

### 18. Two Builds, Two Versions

`CMakeLists.txt` declared `VERSION 0.1.0` and passed it to the compiler, while
`src/mateprover.cpp` carried `#define MATEPROVER_VERSION "0.1.0-dev"` as a fallback for
builds that did not define it. Both build paths are supported and documented, so
the same source reported **0.1.0** or **0.1.0-dev** depending on how it was
compiled, and neither string said which. A version in a bug report identified the
build system rather than the code.

Two declarations of one fact drift; there is no way to keep them honest by
attention. The source is now the only declaration and `CMakeLists.txt` parses it
out with a regex, failing the configure step if it cannot find one. The
compile-definition path is gone, so there is nothing left to disagree with.

Version set to **1.0.0**, which the state of the work supports: both external
formats are specified and independently versioned, the defaults are the
measured-best configuration, the capability figures come from evaluation sets
used once, and 230 checks cover the contracts. `CHANGELOG.md` records what the
version *is* rather than a list of commits -- what it can do, measured on which
positions, what was deliberately left out and why.

The gate checks four things: that the source declares a version, that
`--version` and the `--help` banner agree with it, that `CMakeLists.txt` contains
no competing hardcoded version, and that the changelog has an entry for the
current one. Verified by bumping the source to 1.0.1 and confirming the changelog
check fails.

That last check is the one worth having. The first three catch drift between
files; the fourth catches a version bumped without anyone saying what changed,
which is the failure that actually happens.

### 19. An Independent Opinion On The Source

Every correctness claim so far rests on the engine's own behaviour: tests,
certificates, differential runs. All of it is one compiler's view of one
platform. The CI matrix names clang, MSVC, Linux and macOS, and **none of them
has ever compiled this code** -- the workflow sits at `mateprover/.github/` and
GitHub reads workflows only from a repository root, so CI is staged, not active.
No second compiler or sanitiser is available here either.

What is available is `cppcheck`, which is a genuinely different analysis rather
than another opinion from the same front end. At `warning`, `performance` and
`portability` it reports **nothing** across all thirteen modules. At `style`,
exhaustive analysis found twenty items, of which two were worth acting on:

**Two dead conditions in the DFPN threshold logic.** `knownConditionTrueFalse` on
`thpn >= here.pn` and `thdn >= here.dn`. Both are correct findings. The function
returns early when `here.pn >= thpn`, so by the time the ternary is evaluated the
condition cannot be false and the `: 0` branch is unreachable. Harmless, but
actively misleading: the guard implies the subtraction below it could underflow,
when the control flow above has already made that impossible. Replaced with the
direct expression and a comment stating the invariant.

**Three const-correctness slips**, all trivial and all applied.

The rest were `useStlAlgorithm` suggestions -- replace a raw loop with
`std::any_of` and similar. Declined: these loops sit in the search's hot paths,
carry early exits and side effects that the algorithm forms obscure, and 8k
measured where node time actually goes rather than guessing. Declining is
recorded in `.cppcheck-suppressions` with the reason, so the analyser run is
silent and any *new* finding is a real one.

Verified behaviour-preserving the way a refactor should be: with timing
normalised away, the pre-change and post-change binaries produce byte-identical
output on the mate corpus for the default route, the DFPN route and the
shallow-fast route. The DFPN comparison needed the shallow subset -- DFPN is slow
enough on the deeper entries to exceed a ten-minute tool budget, which is itself
consistent with 8e rejecting it.

The finding worth keeping is that the analyser's substantive hits were both in
`dfpn.h` -- the one module that is unpromoted, unused by default, and therefore
the least exercised by every other gate in this project. Code that no test path
runs is where static analysis earns its keep, and it is exactly the code a
reader of a published repository is most likely to read and least likely to
trust.

### 20. Substitutes For The Compilers And Sanitisers That Are Not Here

19 left the standing risk unaddressed: one compiler, one platform, no sanitiser,
and a CI matrix that has never run. Three substitutes are available without
installing anything.

**Bounds-checked containers.** `-D_GLIBCXX_ASSERTIONS` bounds-checks
`std::array` and `std::vector` `operator[]`, and `-D_GLIBCXX_DEBUG` adds iterator
validity checking. This matters more here than it would in most codebases: the
search indexes a `std::array<char, 64>` with plain `int` values everywhere, and a
stricter warning set reports **117 sign-conversions**, nearly all of that form.
Whether those are safe is not a matter of argument -- it is a matter of whether
any index ever leaves `[0, 64)`.

The full suite passes **230/230 under bounds checking**. No out-of-range access,
no invalid iterator, on any path the tests exercise. That is direct evidence, and
it is why the 117 sign-conversions are being *declined* rather than fixed:
rewriting them would be churn across the hottest code in the engine, carrying a
real chance of introducing the very error the warning speculates about, for a
risk the bounds-checked run says is not present.

**A stricter warning set.** `-Wshadow -Wconversion -Wsign-conversion
-Wold-style-cast -Wcast-qual -Wdouble-promotion -Wformat=2 -Wnull-dereference
-Wredundant-decls -Wmissing-declarations` produces three groups: the 117
sign-conversions above; 84 `-Wmissing-declarations`, which are an artefact of the
unity build, where every function is defined in a header with no prior
declaration; and exactly **one** `-Wconversion`, an implicit `int` to `char`
narrowing of `std::tolower`'s result. That one is now an explicit cast -- the
other four `tolower` sites already had one, so this was an inconsistency as much
as a warning.

**Newer standards.** C++20 and C++23 both compile clean with `-Werror`. Not a
support promise; a canary. Newer standards reject constructs C++17 tolerates
quietly, which is the cheapest approximation available of a second front end.

All three are now CI jobs, so they run when the workflow does. The bounds-checked
one is the valuable one, and it is worth being clear about what it does and does
not establish: it proves those indices are in range on every path **the suite
exercises**, which is a claim about test coverage as much as about the code.

Verified behaviour-preserving as usual: with timing normalised, pre- and
post-change binaries produce byte-identical output on the mate corpus including
certificates.

### 21. Coverage Measurement Found DFPN Was Broken, Not Slow

20 ended by noting that the bounds-checked run proves safety only on paths the
suite exercises. Which paths those are had never been measured. Building with
`--coverage` and running the suite:

| module | line coverage |
|---|---|
| **dfpn.h** | **0.0% (0/155)** |
| routes.h | 30.9% |
| report.h | 47.3% |
| ordering.h | 52.7% |
| movegen.h / board.h / search_state.h | 94% / 92% / 99% |

The DFPN route **ships**, is reachable with `--route dfpn`, and no test had ever
executed a line of it -- which is exactly why 19's static analysis found its only
two substantive defects there. Writing the missing test immediately found a
third, and it was not minor: **DFPN could not solve a mate-in-2 in 120 seconds**,
while the default route did all six shallow positions in 0.04s. Ten million nodes
on a mate-in-2, with a transposition table holding two entries.

The cause was one argument. `dfpn_attacker` and `dfpn_defender` built their table
key as `tt_key(b, 0, ...)`, passing **0 where the remaining depth belongs**. The
module's own banner comment states the invariant it was violating: *"Depth is
part of the key, so a result at one remaining depth can never satisfy a query at
another."* With every depth sharing one entry, a position stored as
`{DFPN_INF, 0}` at depth 0 read back as unprovable at every depth, and the proof
numbers stopped meaning anything.

Passing `depth` turns the mate-in-2 from **10,417,116 nodes into 76**.

That overturns 8e. Re-measured on the development set at mate-in-8,
single-threaded, 5 s, `--direct-depth`:

| route | solved |
|---|---|
| depth-first (the promoted default) | 17/60 |
| **dfpn** | **54/60** |

DFPN solves 37 positions the default cannot and misses none that it can. All 54
certificates verify independently, none over-deep. 8e recorded that DFPN was
"slower than the default at every measured depth" -- it was measuring a defect,
and the conclusion drawn from it was wrong.

The default path is untouched: with timing normalised, the pre- and post-fix
binaries are byte-identical on the mate corpus with certificates, on the
shallow-fast route, and on perft. This iteration changes what `--route dfpn`
does and nothing else.

**Promotion is deliberately not decided here.** The measurement above used a
development set at one budget on one thread, which is the right instrument for
"is this worth pursuing" and the wrong one for "should this be the default".
That decision needs the operating point, the portfolio interaction, and a freshly
minted evaluation set per the protocol in `benchmarks/README.md`.

The lesson is about coverage rather than DFPN. Three separate defects lived in
the one module no test touched, and two of the project's own gates -- the
architecture's promotion rule and the benchmark suite -- had both been applied to
it and recorded a confident conclusion. A measurement of a broken implementation
is not a measurement of the idea it implements, and nothing distinguishes the two
except looking.

### 22. DFPN Promoted, Then Rejected By The Set Minted To Judge It

21 fixed DFPN and deliberately left promotion undecided, on the grounds that a
development set at one budget on one thread answers "worth pursuing" and not
"should this be the default". The promotion was then run properly -- design on
development sets, mint a fresh evaluation set, spend it once -- and the fresh set
**rejected the change**.

Design, on the mate-in-8 development set at 32 threads and 15 s **with
`--direct-depth`**:

| configuration | solved |
|---|---|
| depth-first + portfolio (the default) | 52/60 |
| **dfpn + portfolio** | **59/60** |
| dfpn alone | 57/60 |
| depth-first alone | 43/60 |

Mate-in-10 agreed: 22/24 against 18/24, four gained and none lost. DFPN composes
with the portfolio rather than replacing it. On that basis DFPN was made the
default route.

Confirmation, on 150 freshly minted positions, run **once**, in the default
configuration:

| configuration | solved | 95% CI |
|---|---|---|
| previous default (depth-first) | 117/150 = 78.0% | 70.7-83.9 |
| new default (dfpn) | 108/150 = 72.0% | 64.3-78.6 |

Four gained, thirteen lost. **Reverted.**

The design measurement did not answer the question the promotion asked of it,
and the flaw was mine: every design run passed `--direct-depth`, which is *not*
the default. Isolating it on the development set:

| route | mode | solved |
|---|---|---|
| depth-first | iterative deepening (the default) | 51/60 |
| dfpn | iterative deepening | 45/60 |
| depth-first | `--direct-depth` | 52/60 |
| **dfpn** | **`--direct-depth`** | **59/60** |

DFPN wins by seven under `--direct-depth` and loses by six under iterative
deepening. The mechanism is a direct consequence of 21's fix: iterative deepening
re-runs the search at every depth from 1, and now that depth is correctly part of
the DFPN key, nothing computed at one depth is reusable at the next. DFPN pays
full price at every depth, and the exact route -- which keeps its table across
depths -- does not.

So the fix stands and the promotion does not. `--route dfpn --direct-depth` is
now the strongest configuration this engine has, and it is documented as such;
the default is unchanged because the default mode is iterative deepening, where
DFPN is worse.

Two things worth recording beyond the result. First, the protocol worked exactly
as designed: a change that looked like a clear +7 on development data was caught
by a set that had never been consulted, and caught *because* it was run in the
shipped configuration rather than the one the design used. Second, that
evaluation set is now spent on rejecting a change rather than confirming one,
which is the correct use of it and the reason it existed -- the cost of my
measuring the wrong configuration during design was 150 positions of evidence,
not a shipped regression.

### 23. Preconditioning Only The Deepest Iteration

22 explained why DFPN loses under iterative deepening even though it wins under
`--direct-depth`: the route runs every depth from 1, and since 21 put depth back
into the DFPN key, nothing computed at one depth is reusable at the next. Every
shallow pass is paid for in full and discarded, while the exact route's bounded
table does carry information forward.

That explanation predicts a fix. The shallow iterations are the ones the exact
prover disposes of almost instantly by itself, so preconditioning them buys
nothing and costs a full DFPN search each. Skipping them should recover DFPN's
advantage without giving up minimality.

Measured on the mate-in-8 development set, 15 s, default configuration otherwise:

| configuration | solved |
|---|---|
| depth-first, iterative (the default route) | 51/60 |
| dfpn, preconditioning every depth | 45/60 |
| **dfpn, preconditioning only the deepest iteration** | **53/60** |

An absolute threshold sweep located it: skipping below depth 5 or 7 changed
almost nothing (46/60), and skipping everything below the requested depth gave
53/60. The gain is not gradual -- it is entirely in the last iteration, exactly as
the mechanism predicts.

Promoted as the default behaviour of `--route dfpn`, expressed as **"only the
final iteration"** rather than as the absolute depth that the sweep found. An
absolute threshold tuned on a mate-in-8 corpus would silently disable
preconditioning altogether for shallower requests; the relative form states the
actual finding, which is about the last depth rather than about depth 8.
`--dfpn-every-depth` restores the old behaviour.

The scope is deliberately narrow. **The default route is unchanged** and the
shipped default path is byte-identical, verified on the mate corpus with
certificates, on perft and on the shallow-fast route. This improves a route a
user must opt into, which is a lower bar than changing what everyone gets, and
the evidence is a development set plus a mechanism -- appropriate for that bar and
not for a higher one.

Whether DFPN should now *become* the default is a real question and is left open:
53/60 against 51/60 is inside the run-to-run variance seen across this session
(the same depth-first configuration measured 49, 51 and 52 on three occasions).
Answering it needs a freshly minted evaluation set, and 22 spent one on exactly
this question two iterations ago. That is the cost of having measured the wrong
configuration then, and it is not a reason to lower the bar now.

### 24. A Deterministic Budget, And What It Says About DFPN

23 left a question open -- whether DFPN should be the default -- and noted the
margin was inside run-to-run variance. That variance is worth attacking directly:
the same configuration measured **49, 51, 52 and 53 of 60** across this session,
purely on where positions fell relative to a wall clock, which is the size of the
effects being measured. Every comparison in this project has been made through
that noise.

`--node-limit N` stops after N nodes and reports the same "gave up" outcome a
wall-clock expiry does. Sequentially it is exactly reproducible: three runs of a
hard position at the same limit are byte-identical, stopping at `acn 200000` each
time. With threads the node totals vary because lanes race, but the answers do
not.

The soundness requirement is the one 8r established, and it is why the internal
`node_budget` could not simply be exposed: exceeding it sets `aborted` **without**
`timed_out`, and a line with no marker means "searched exhaustively, no mate
exists". A user-facing node limit reusing that path would have reported every
exhausted budget as a **disproof**. The new limit sets `timed_out`, so it lands
in the documented gave-up outcome rather than inventing a fifth one.

Used on the open question, at an equal 20M-node budget, sequential, no portfolio:

| route | solved |
|---|---|
| depth-first | 40/60 |
| **dfpn, final-depth-only** | **51/60** |

Eleven positions, with no noise at all, against the +2 that wall-clock measurement
suggested. The two numbers are both right and they measure different things.
Per **node** DFPN is far ahead: its search is much better directed. Per **second**
it is roughly break-even, which means its nodes cost proportionally more --
proof-number bookkeeping, a second table, and threshold arithmetic at every node.

That resolves the open question in a more useful way than choosing a winner. A
user paying in seconds should not expect much from `--route dfpn` at mate-in-8
today; the reason is node cost rather than search quality, and node cost is the
kind of thing that can be optimised. The default stays where it is, and the
deterministic budget is now available for whoever tries.

### 25. Where A DFPN Node's Cost Goes, And What That Implies

24 established that DFPN wins per node and breaks even per second, and named node
cost as the thing to attack. Measuring it:

| quantity | value |
|---|---|
| node rate, depth-first route | 503 k/s |
| node rate, dfpn route | 350 k/s |
| DFPN nodes as a share of all nodes in a dfpn run | **9%** |
| move generations per DFPN node | 1.00 |
| checkmate tests per DFPN node | 0.60 |
| distinct table entries per node visit | 0.80 |

Two things follow, and the second is more interesting than the first.

**A DFPN node costs about three times an exact node.** Holding the exact route's
rate fixed, 3.31M exact nodes account for ~6.6s of a 12s run, leaving ~5.4s for
901k DFPN nodes. The reason is per-child work: a DFPN node generates its moves
once but then builds a child board, computes a transposition key and performs a
lookup **for every child**, to pick the cheapest one to descend into. An exact
node builds roughly one child. With ~25 legal moves that is the whole 3x.

**The preconditioner is only 9% of the nodes.** It is not doing most of the work;
it is redirecting the 91% that the exact prover does. Spending 9% of nodes to
gain eleven positions of reach at a fixed node budget is a very high return -- and
it is exactly why the wall-clock result is break-even rather than a loss: that 9%
of nodes costs about 27% of the time, which is roughly what the gain is worth.

That makes the arithmetic unusually clear for once. **If a DFPN node cost what an
exact node costs, the route would win outright on wall clock too.** The gap is
not algorithmic, and it is not spread thin the way 8k found the exact search's
cost to be -- it is concentrated in one identifiable place, per-child board and
key construction.

The obvious attack is to stop building children whose values are not yet known.
On a node's first visit nearly every child is unvisited and its lookup returns
the default `{1, 1}`, so the board and key were built to learn nothing. Visits are
only 1.25 per distinct node here, so first visits dominate. Skipping that work
would change DFPN's search order, which is sound -- the proof numbers are a
heuristic and every verdict still comes from the exact prover -- but it would need
measuring in both currencies, since it trades search quality for node cost and
`--node-limit` now separates those cleanly.

Not attempted here. It is a real change to the one module that had 0% test
coverage a few iterations ago, and this iteration's contribution is the
measurement that makes it worth attempting and says what success would look
like: a DFPN node rate approaching the exact route's, with the equal-node result
holding at 51/60.

Also corrected: `CHANGELOG.md` still listed DFPN under "not included", as
"measured: slower at every depth". That was written before 21 found the defect
and is exactly the sort of stale claim the fresh-measurement discipline exists to
catch. It now says what the route is and why it is not the default.

### 26. Reproduction That Does Not Depend On The Reader's Machine

`tools/reproduce_results.py` let a reader check the published figures, but only
approximately: it used wall-clock budgets, so the numbers depend on the machine,
its load, and luck. This project has measured the size of that: one configuration
scored 49, 51, 52 and 53 of 60 on the same hardware in the same session. A reader
seeing 74% where the document says 79.5% could not tell whether the engine or
their laptop was the difference.

`--deterministic` uses the node budgets from 24 instead. It measures a different
configuration -- sequential, no portfolio, since the parallel portfolio's node
totals vary as lanes race -- and says so, because the numbers are lower and not
comparable to the headline figures:

| measurement (sequential, equal node budget) | solved |
|---|---|
| mate-8 dev set, depth-first, 2M nodes | 10/60 |
| mate-8 dev set, dfpn, 2M nodes | 16/60 |
| mate-10 dev set, depth-first, `--direct-depth`, 4M nodes | 4/24 |
| **mate-10 dev set, dfpn, `--direct-depth`, 4M nodes** | **18/24** |

It deliberately uses the **development** sets. Reproduction is not a promotion
decision, so it consumes no evidence: a reader can run it as often as they like
without spending anything, which is the opposite of what the evaluation sets are
for. Using those here would have burned a set on a check that decides nothing.

The last row is worth noting on its own. At an equal node budget the repaired
DFPN route reaches four and a half times as many mate-in-10 positions as the
default route. That is the cleanest available statement of what 21's one-argument
fix was worth, and it is invisible in wall-clock measurement because 25 showed a
DFPN node costs about three times an exact one.

### 27. Making A DFPN Node Cheap

25 measured a DFPN node at about three times an exact one and located the cost
in per-child work. Counting it precisely, by subtracting the exact prover's own
rate from a DFPN run: **~24 `make_move` and ~24 `tt_key` calls per DFPN node**,
one per child, each followed by a hash lookup in a 40-byte-keyed map.

The decisive measurement was of the selection loop. It runs **149,379 times
across 149,380 node entries** -- essentially once per entry, never repeating. So
those 24 boards, keys and probes were built to compute a single number and then
discarded, and when that number already exceeded a threshold the node returned
without ever descending into a child.

For a node being visited for the first time, every child is unvisited and every
probe returns the default `{1, 1}`. The value is therefore known in advance:
`(N, 1)` at an AND node, `(1, N)` at an OR node, for `N` legal moves. Both node
functions now compute that directly and, if it already crosses a threshold, store
and return **without building any children at all**.

A transposed child could genuinely be known, so this is an estimate rather than a
lookup. That is sound for the same reason DFPN is allowed to exist here at all:
the proof numbers only steer the search, and every verdict, PV and certificate
still comes from the exact prover.

The attacker's immediate-mate scan was fixed separately and is the same mistake
in miniature. Only checking moves can mate, and at ~0.6 mate tests per node the
scan was pre-building all ~24 child boards to examine one. It now builds each
board inside the test.

| measure | before | after |
|---|---|---|
| dfpn node rate | 350 k/s | **465 k/s** |
| depth-first node rate, for scale | 503 k/s | 503 k/s |
| mate-10 dev, dfpn, equal 4M-node budget | 18/24 | **18/24** |
| mate-8 dev, wall clock 15 s | 53/60 | **54/60** |
| depth-first, same conditions | 51/60 | 52/60 |

The two rows that matter together: the node rate rose 33% and the equal-node
capability did **not** move. The estimate costs nothing in search quality; it
only stops paying for information the algorithm already had.

Promoted, scope limited to `--route dfpn`. The default path is byte-identical on
the mate corpus with certificates, on perft and on the shallow-fast route, and
all 54 proofs verify independently.

Whether DFPN should now be the default is *still* not settled. 54 against 52 is
inside the run-to-run variance this project has measured repeatedly, and 22 spent
an evaluation set discovering that a development-set lead can reverse. The honest
position is that DFPN has gone from clearly worse to probably slightly better,
and "probably slightly" is not a standard this project promotes on.

### 28. The DFPN Question, Settled As Far As The Evidence Allows

27 cut DFPN's per-node cost and left the default unchanged, on the grounds that a
two-position lead is inside measured variance. With the work finished, the
protocol says to mint a set and spend it once. Counting what 27 actually
achieved first, by subtracting the exact prover's share from a DFPN run:

| per DFPN node | before 27 | after 27 |
|---|---|---|
| `make_move` calls | 24.3 | **4.34** |
| `tt_key` calls | 23.6 | **4.60** |

An 82% reduction, and DFPN nodes now do *less* board work than exact nodes,
which do 5.16 `make_move` each. The remaining node-rate gap is 8% (465 k/s
against 503 k/s), so there is little left to win here and this line of
optimisation is finished.

Fresh evaluation set, 200 positions minted after the work and measured once, in
the shipped configuration:

| route | solved | 95% CI |
|---|---|---|
| default (depth-first) | 167/200 = 83.5% | 77.7-88.0 |
| `--route dfpn` | **171/200 = 85.5%** | 80.0-89.7 |

DFPN gains six positions and loses two. It did **not** reverse the way 22's
promotion did, and the direction agrees with both development sets.

**The default is still unchanged.** The intervals overlap heavily, and the paired
comparison -- eight positions decided differently, six favouring DFPN -- gives a
sign-test p of about 0.29. That is "probably slightly better", which is exactly
the standard 27 declined to promote on. Having written that down before seeing
this number, lowering the bar now because the number came out favourable would
make the standard meaningless.

What the evidence does support is stated plainly in the documentation: the DFPN
route is now at least as good as the default and probably a little better, it is
markedly better where the depth is known (`--direct-depth`), and it is
dramatically better per node. A user choosing `--route dfpn` is not taking a
risk; they are simply not getting a guarantee.

Settling it properly would need a much larger sample -- detecting a two-point
difference at this variance takes on the order of a thousand positions -- or a
larger effect. Both are available: 550 unused mate-in-8 positions remain, and the
node-cost work suggests the effect could grow if DFPN's remaining 8% node-rate
gap were closed. Neither is worth another iteration now, because the practical
advice to a user does not change either way.

### 29. DFPN Promoted: Decisive At Mate-In-10

28 settled the mate-in-8 question -- DFPN slightly ahead, not significantly -- and
left the default alone. It did not ask about depth. That was the omission worth
fixing, because 22's mate-in-10 comparison predates both 23 and 27, and mate-in-10
is where this engine is weakest.

Development sets first, 48 mate-in-10 positions at the operating point:
depth-first + portfolio 34/48, dfpn + portfolio **44/48**, gaining ten and losing
none. Then a freshly minted set of 60, measured once:

| configuration | solved | 95% CI | certificates |
|---|---|---|---|
| depth-first + portfolio | 37/60 = 61.7% | 49.0-72.9 | 37 verified |
| **dfpn + portfolio** | **54/60 = 90.0%** | 79.9-95.3 | 54 verified |

**Seventeen gained, none lost.** All seventeen discordant positions favour DFPN,
a sign test of p ~ 8e-6, and the intervals do not overlap. Twenty-eight points of
solve rate at the depth where the engine had the least to offer.

**Promoted: DFPN is now the default route.** The bar declined in 27 and 28 was
"probably slightly better"; this is not that. The mate-in-8 result matters here
only as a guard -- 171/200 against 167/200, a slight gain rather than a loss -- so
the deep improvement is not bought at the shallow one's expense.

The whole gain came from three fixes to a route this project had already
measured, rejected and recorded a confident conclusion about:

1. **21**: depth was missing from the DFPN transposition key, so every depth
   shared one entry. Ten million nodes on a mate-in-2 became 76.
2. **23**: preconditioning every iterative-deepening depth, when its work cannot
   carry across depths, so only the deepest iteration repays the cost.
3. **27**: building 24 child boards and keys per node to compute a value already
   known, cut to 4.3.

None of the three is an algorithmic insight about proof-number search. They are
an argument in the wrong place, an unnecessary repetition, and a redundant
computation -- and together they were the difference between "slower at every
measured depth" and "the default". 8e's rejection was not a wrong judgement given
its evidence; the evidence was of a broken implementation, and nothing
distinguished that from the idea except looking, which took a coverage
measurement finding `dfpn.h` at 0% to prompt.

### 30. What Promoting DFPN Invalidated

Changing the default route makes every measurement taken under the old one a
statement about a configuration the engine no longer has -- the failure mode this
document has recorded three times already (8h, 8l, 8x). Two things needed
checking straight away.

**The published mate-in-8 headline was stale.** It read 79.5%, measured on an
evaluation set under the previous default route. The shipped configuration's
figure is the one 28 measured with `--route dfpn`, which is now simply the
default: **85.5%** [80.0-89.7] on 200 fresh positions. README, RESULTS and the
changelog now carry it.

The budget curve (80.0% / 90.5% / 96.0% at 15 s / 60 s / 240 s) was **not**
re-measured. It predates the promotion and is now labelled with the route it used.
Re-running it costs about two hours and would not change what it is quoted for --
that mate-in-8 is budget-limited, which is a claim about shape rather than about
absolute solve rate.

**The portfolio still earns its place.** This was the more interesting question:
DFPN improves the search so much that it might have subsumed the restriction
portfolio, which would have been a real simplification -- eight lanes, weighted
thread allocation, and a table derived by set cover, all removable.

| configuration | mate-10 (48 positions) | mate-8 (60 positions) |
|---|---|---|
| dfpn + portfolio (shipped) | **44/48** | **60/60** |
| dfpn alone | 40/48 | 58/60 |
| portfolio contributes | +4, costs 0 | +2, costs 0 |

It does not subsume it. The two mechanisms are orthogonal in exactly the way the
architecture predicts: DFPN changes *how* the tree is searched, the portfolio
changes *which problem* is searched, and a restriction that makes a mate
reachable at all is not something a better search order can substitute for. They
stack, and neither costs the other anything.

Worth noting the mate-in-8 development row: **60/60**. That set is exhausted as a
discriminator for the shipped configuration -- it cannot show an improvement any
more, only a regression. Its usefulness from here is as a canary, not a
comparison.

### 31. How Deep It Actually Goes, And How It Compares

Promoting DFPN (29) raised mate-in-10 from 61.7% to 90%. Nothing had been measured
deeper, so the documentation's claim to address "the shallow end of matetrack" was
an assumption rather than a finding. Freshly minted evaluation sets, 30 s, shipped
configuration with `--direct-depth`, every certificate independently verified:

| depth | solved | 95% CI |
|---|---|---|
| mate-10 | 54/60 = 90.0% | 79.9-95.3 |
| mate-12 | 33/40 = 82.5% | 68.0-91.3 |
| mate-14 | 30/40 = 75.0% | 59.8-85.8 |
| mate-16 | 28/40 = 70.0% | 54.6-81.9 |
| mate-20 | 23/40 = 57.5% | 42.2-71.5 |

There is no wall, only a gradual decline. The engine solves more than half of
mate-in-20 positions within thirty seconds, which nothing in this document
previously suggested.

**Against Chest 3.19**, the program this one reimplements, on the same machine,
positions, memory and time cap:

| depth | Chest 3.19 | mateprover, one thread | mateprover, default |
|---|---|---|---|
| mate-8 | 39/40, 4.3 s | 40/40, 1.0 s | 40/40, 1.2 s |
| mate-10 | 17/40, 21.2 s | 37/40, 3.3 s | 37/40, 3.3 s |
| mate-12 | 8/40, 26.3 s | 33/40, 6.4 s | 33/40, 6.4 s |

Comparable at mate-in-8 with about a fourfold speed advantage; four times the
reach at mate-in-12. Both single-threaded in the fair column, so this is an
algorithmic difference, not a hardware one -- the parallel column is nearly
identical because these positions resolve in seconds.

A methodological note, because the first attempt at this comparison was wrong.
It reported Chest solving **0/40 at every depth**, at 0.0 seconds per position,
which was a harness fault: the binary was wrapped in an external `timeout`
command that failed silently in the background environment. The giveaway was the
timing, not the score -- an engine that searches and fails consumes its budget,
and one that consumes none never ran. Publishing that would have libelled a
program that in fact solves 39 of 40 mate-in-8 positions unaided. A comparison
against someone else's work needs its failures to look like failures before its
numbers mean anything.

### 32. Root-Split Parallelism Now Contributes Nothing

31 noted in passing that the 32-thread column matched the single-thread one.
Those positions resolve in seconds, so the comparison proved little. At the
frontier it proves a great deal. Forty fresh mate-in-16 development positions,
30 s, 2 GB:

| configuration | solved | mean time |
|---|---|---|
| portfolio, 1 root thread | 31/40 | 7.9 s |
| portfolio, 8 root threads | 31/40 | 7.9 s |
| portfolio, 32 root threads | 31/40 | 7.9 s |
| no portfolio, 1 root thread | 26/40 | 12.1 s |
| no portfolio, 8 root threads | 26/40 | 12.2 s |
| no portfolio, 32 root threads | 26/40 | 12.2 s |

**Root-split threading contributes nothing at all** -- identical solve counts and
identical mean times at 1, 8 and 32 threads, with the portfolio on *or* off. The
portfolio contributes five to six positions and cuts mean time by a third.

Note that `--single-thread` sets the root-split width to one but leaves the
portfolio running, so even that row uses eight cores, one per lane. The engine is
parallel; the parallelism that matters is lane-level, and root splitting inside a
lane is now inert.

8d measured root splitting saturating around sixteen threads and still worth
something against one. That is no longer true, and DFPN is why: a search that
resolves a position in eight seconds instead of exhausting a thirty-second budget
gives the extra workers nothing to do but duplicate. The mechanism that made
splitting pay -- lots of independent root moves, each expensive -- is precisely
what a better-directed search removes.

Not acted on. `--threads` is inert rather than harmful, removing root splitting is
a large change to working code, and the case for keeping it is that it costs
nothing and may matter on hardware or problems unlike these. But the
documentation should not imply it buys anything here, and it now does not.

### 33. Parallelism Across Positions

32 established that root splitting inside a position contributes nothing: the
engine uses about one core per portfolio lane whatever `--threads` says, so on a
32-core machine most cores idle through a batch. The remaining axis is the one
the architecture already guaranteed and never used -- positions are independent,
with no state crossing between them (8v, 26).

`--parallel-positions N` solves N at a time, each into its own buffer, and emits
the buffers in input order. Measured on 40 mate-in-12 positions with a 5 s cap:

| width | wall clock | solved |
|---|---|---|
| 1 | 74.1 s | 31/40 |
| 2 | 59.8 s | 30/40 |
| 4 | 40.9 s | 30/40 |
| 8 | **30.4 s** | 29/40 |

**2.4x throughput at width 8** as first implemented, since raised to 4.1x. This
section originally blamed the shortfall on oversubscription; 34 blamed lane
imbalance; 35 found the actual cause, a barrier in the batching code, and removed
it. The numbers in the table above are the barrier version's.

The solve count drifts down slightly, 31 to 29. That is the honest cost: under a
*wall-clock* limit, positions sharing cores each get less work done, so a wider
batch trades a little per-position reach for throughput. Under `--node-limit` the
trade would not exist. Anyone running a corpus wants the throughput; anyone
solving one position wants the reach, and gets it by leaving the default alone.

Two contracts had to survive. Results appear in input order, so a caller can
still match answers to inputs positionally. And the default stays at 1, because
service mode depends on each answer streaming as it is produced (8w) -- batching
by definition delays that.

Verified by comparing *verdicts* rather than whole lines: under
`--portfolio-parallel` the proof returned already varies between two identical
runs, so a textual diff between widths would have shown a difference that has
nothing to do with batching. Widths 1, 2 and 5 give the same depths, the same
timeouts, against the same input lines.

### 34. Two Budgets, Two Different Batch Trades

33 made two claims in passing that were not tested. Both are now, and one was
wrong.

**Oversubscription is not why batching scales sub-linearly.** Width 8 runs eight
positions of eight lanes each, 64 threads on 32 cores, which looked like the
obvious culprit. Removing the inert root-split threads with `--single-thread`
should then have helped substantially. It gives 28.5 s against 29.3 s and one
extra position -- about 3%, inside noise. The sub-linearity is lane-length
imbalance, not thread contention: a position finishes when its slowest lane does,
and lanes differ.

**Under a node budget the reach/throughput trade disappears entirely**, which 33
asserted without checking. Forty mate-in-12 positions, 3M nodes each:

| width | wall clock | solved | verdicts |
|---|---|---|---|
| 1 | 257.6 s | 33/40 | -- |
| 4 | 166.6 s | 33/40 | identical |
| 8 | **152.1 s** | **33/40** | identical |

Identical verdict sequences at every width, and 1.7x throughput. So the two
budget types give genuinely different batch behaviour:

| budget | throughput at width 8 | reach | reproducible |
|---|---|---|---|
| `--time-limit` | 2.4x | drifts, 31 → 29 | no |
| `--node-limit` | 1.7x | **unchanged** | yes |

The reason is direct. A wall clock is a shared resource, so positions competing
for cores each get less of it; a node budget is per-position and indifferent to
what else is running. Whoever wants a corpus processed quickly and cares about
the answers should use a node budget; whoever wants the most reach per second on
one position should use a clock and leave the batch width alone.

That also makes the batch test deterministic. It compared verdicts under a
wall-clock limit, where a wider batch can legitimately lose a position for
reasons unrelated to ordering -- a test that could fail without a defect. It now
uses a node budget, where any difference is a real one.

### 35. The Third Explanation Was The Right One

33 blamed sub-linear batch scaling on thread oversubscription. 34 tested that,
found it worth about 3%, and blamed lane-length imbalance instead. That was also
wrong, and for a reason that should have been obvious: when a lane proves a mate
every other lane is cancelled, so a position finishes when its *fastest* lane
succeeds, not its slowest.

Memory was the next candidate -- eight concurrent positions each holding a table.
Measured with a fixed node budget so only throughput varies, across a 32x range
of table sizes: 2.82x speedup at 64 MB, 2.71x at 512 MB, 2.67x at 2 GB. A 32x
change in memory moves scaling by 5%. Not that either.

The cause was in the code I had written the iteration before. `flush_pending`
spawned one thread per position and joined them all -- **a barrier**. A chunk took
as long as its slowest member while every other core idled, and positions differ
enormously: some resolve instantly, some run to the whole budget.

Replaced with a work queue. Workers pull the next position as they finish, and
chunks accumulate four positions per worker so the queue has imbalance to
absorb. Same 40 positions, same fixed node budget:

| version | width 1 | width 8 | scaling |
|---|---|---|---|
| barrier | 196.1 s | 107.5 s | 1.82x |
| **work queue** | 187.9 s | **84.4 s** | **2.23x** |

21% faster at width 8, with identical solve counts.

The wall-clock arm improves more and costs more: 72.9 s to 18.0 s is **4.06x**,
against 2.4x for the barrier, but reach falls 31 to 27 rather than 31 to 29.
That is consistent rather than contradictory. Keeping every core busy means each
position gets a smaller share of a budget that is *shared*; a node budget is
per-position and indifferent, so it stays lossless. The guidance from 34 holds
and gets sharper: corpus work should use `--node-limit`, where batching is free.

Three wrong explanations before the right one, each plausible and each cheap to
test. What distinguished the last is that it was a property of code written two
iterations earlier rather than of the machine -- the kind of cause that is easy to
overlook precisely because it is the newest thing in the picture.

### 36. Batch Results Stream Instead Of Arriving In Blocks

35's work queue fixed the throughput but introduced a regression I had not
flagged. To give the queue imbalance to absorb, chunks grew to four positions per
worker -- 32 at width 8 -- and nothing was emitted until the whole chunk finished.
On a corpus of hard problems that is minutes of silence, and worse than the
barrier version it replaced, which at least emitted every 8.

Results are now emitted **in input order as each becomes ready**, while workers
carry on with later positions. A condition variable wakes the emitting thread
when the next index in sequence completes.

Measured on 40 mate-in-12 positions, width 8, 5 s each, timestamping arrivals:

| version | first result | eighth | last |
|---|---|---|---|
| original barrier, chunk of 8 | 6.5 s | 6.5 s | 30.3 s |
| **streaming work queue** | **2.7 s** | 6.9 s | **17.1 s** |

First result 2.4x sooner, whole batch 1.8x sooner. The eighth arrives at about
the same moment either way, which is the honest comparison: the barrier version
delivers its first eight simultaneously, so it is only behind on the *first*
result, and ahead of nothing.

Streaming costs about 4% of throughput against emitting the chunk in one go
(88.1 s against 84.4 s under a fixed node budget, same 32 solved). That is worth
paying. A user watching a long corpus run needs to see it working, and the
ordering contract -- results in input order, one line per input line -- is
preserved either way, which the batch test checks at widths 1, 2 and 5.

The general point is that the barrier in 35 and the chunk silence here are the
same mistake in two forms: making the whole batch wait on its slowest member.
Removing it for scheduling was worth 21% of throughput; removing it for output
was worth 2.4x on the latency a user actually perceives.

### 37. The Reproduction Tool Had Stopped Comparing Anything

`--deterministic` runs one position per subprocess. Under a node budget batching
is free (34), so it should use `--parallel-positions`. Making that change cut the
full run from about twenty minutes to **84 seconds**, and identical output at
width 1 and width 8 confirmed the property holds where it is now relied upon.

Doing it exposed two staleness bugs in the tool itself, both of the same kind:
values that were correct when written and quietly stopped being so.

**The comparison had collapsed.** Its "depth-first" rows passed no `--route` and
relied on depth-first being the default. When DFPN was promoted (29) those rows
silently began running dfpn, so the tool compared a route against itself while
printing the old depth-first expectations beside it. The quick-mode numbers gave
it away: the depth-first row had gone from 1/8 to 6/8, matching the dfpn row
exactly. Every entry now names its route explicitly.

**One expected value was stale in the other direction.** With the routes distinct
again, mate-in-8 dfpn measures 17/60 against a documented 16/60. That is not a
regression -- 27 cut DFPN's per-node cost by 82%, so at a *fixed node budget* it
now reaches one position further. The reference value has been corrected upward.

Both are the failure this document keeps recording: a number written down as a
property of the engine when it was a property of the engine at a moment. What is
different here is that the tool is the thing that catches such drift for readers,
so it was drifting in the one place that is supposed to be the check. A gate that
compares a configuration against itself passes forever.

### 38. Auditing For Comparisons That Compare Nothing

37 found the reproduction tool comparing a route with itself, and found it by
accident -- a quick-mode number looked wrong. That is not a detection method, and
the same failure can hide anywhere a comparison names one side implicitly.

`--print-config` reports the effective configuration, so the question "do these
two runs actually differ?" is now answerable mechanically. `test_comparisons_
actually_differ` checks that the three routes resolve to three distinct
configurations, that every row of the reproduction tool's table differs from
every other row -- reading the table by importing the tool rather than restating
it, so the two cannot drift apart -- and that the flags 8n turned into defaults
are now no-ops.

Verified by reintroducing the exact bug 37 fixed: with the depth-first row's
`--route` removed, the gate fails naming both colliding rows. It would have
caught it.

An audit of the benchmark registry found five more instances, all harmless and
all the same shape. `chest_E_proof_hints_probe`, `keep_iter_tt`, `move_reserve`,
`inplace_order` and `move_reserve_cap96` each pass a flag that 8n made the
default, so each now runs the shipped configuration under a different name. They
are historical records rather than live probes, so they are annotated rather than
deleted -- but a reader comparing them against the promoted entry would otherwise
be comparing a thing with itself and concluding the setting makes no difference,
which is true only because it is already on.

The general shape is worth naming, because this project has now hit it three
times (8n, 37, here). **Defaults are load-bearing in comparisons.** Any
measurement that says "with X" against "without X" is really saying "with X"
against "whatever the default is", and the second half of that sentence changes
without anyone editing the comparison.

### 39. Memory Is Still Not A Lever, And The Work Has Moved Into DFPN

8l concluded that memory buys nothing. It measured mate-in-8 under the
depth-first route, so it described a configuration the engine no longer has, at a
depth where trees are small. Re-tested at the frontier -- 40 mate-in-16 positions,
30 s, shipped configuration:

| table | solved | evictions |
|---|---|---|
| 256 MB | 31/40 | **0** |
| 1 GB | 31/40 | 0 |
| 4 GB | 31/40 | 0 |

The conclusion holds and is now stronger than 8l could state it: at this depth
the table never fills at all, so memory is not merely unhelpful, it is not a
constraint. The 256 MB default is ample and nothing above it can matter.

**The work has moved.** 25 measured a DFPN run as 9% preconditioner nodes and 91%
exact-prover nodes, and reasoned from that. 27 then cut DFPN's per-node cost by
82%, so it explores far more nodes in the same time. Measured now:

| depth | DFPN | exact prover |
|---|---|---|
| mate-8 | 97.1% | 2.9% |
| mate-10 | 99.7% | 0.3% |
| mate-16 | 85.6% | 14.4% |

The ratio has inverted. This matters for what is worth optimising next: the exact
prover, which every efficiency measurement in 8i, 8j and 8k examined, is now
between 0.3% and 14% of the work. Optimising it cannot pay. DFPN is 86-99% of it.

That also retires a proposal. The obvious reading of 25 was that the exact pass
re-derives what DFPN already found, and eliminating that re-derivation would be
worth most of the runtime. At 0.3% of nodes at mate-in-10 there is nothing there
to eliminate -- the exact pass is already almost free, because DFPN hands it a
proof structure that verifies immediately.

### 40. Threshold Widening Does Not Help Here (measured, not promoted)

39 established that DFPN is 86-99% of the work, so anything that cuts its node
count attacks nearly all of the runtime. The cheapest such lever was already
implemented and switched off: `--dfpn-epsilon-64` widens the child thresholds by
1+epsilon, a standard df-pn technique whose purpose is to stop the search
oscillating between siblings and re-expanding the same subtrees.

Forty mate-in-16 positions at a fixed 4M-node budget, so only search quality
varies:

| epsilon (64ths) | solved |
|---|---|
| 0 (off, the default) | 24/40 |
| 8 | 25/40 |
| 16 | 24/40 |
| 32 | 24/40 |
| 64 (double) | 22/40 |

One position at epsilon 8, nothing at 16 or 32, and two positions lost at 64.
A single position on forty is not a result. Rejected; the default stays off.

The likely reason it does not pay here is 27. Threshold widening exists to reduce
*re-expansion*, and this DFPN barely re-expands: 35 measured the selection loop
running 149,379 times across 149,380 node entries, essentially once per entry.
A technique that reduces repeated visits has little to work on when there are
almost no repeated visits. What that also says is that the remaining node count
is not waste to be squeezed out -- it is the search actually exploring, which is a
harder thing to improve.

### 41. Bounding Parallel DFPN Before Building It

DFPN is 86-99% of the work (39) and a single position uses about 8 of 32 cores,
so parallelising DFPN is the largest remaining lever by resource argument. It is
also the most expensive thing on the list -- thread-safe tables, a divergence
strategy, and real tuning -- and this project has a direct warning against
assuming idle cores convert into speed: root splitting is completely inert (32).

There are two families to parallelise a search like this. **Portfolio**: run
several differently-parameterised searches at once and take the first to finish.
**Shared tree**: several threads cooperating on one search through a shared
transposition table. The first is a few lines here; the second is weeks. So the
first question is whether the portfolio form would pay, and that is measurable
today with knobs the engine already has.

Six DFPN variants -- three threshold-widening settings crossed with move sorting
on and off -- on 40 mate-in-16 positions at a fixed 2M-node budget:

| variant | solved |
|---|---|
| eps 0 | 22/40 |
| eps 0 + sort | 21/40 |
| eps 8 | 22/40 |
| eps 8 + sort | 22/40 |
| eps 24 | **23/40** |
| eps 24 + sort | 21/40 |
| **union of all six** | **25/40** |

An **oracle** that picked the best variant for each position separately would gain
**two positions of forty** over simply always using the best single variant. A
real portfolio cannot beat its own oracle, so that is the ceiling, and it is
worth a fifth of what the restriction portfolio delivers (+5 to +6, section 30)
for six times the cores.

**Portfolio-style parallel DFPN is rejected on that ceiling**, without building
it. The variants solve nearly the same positions, which says the search is robust
to these parameters: it is not taking a wrong path that a differently-tuned
sibling would avoid. That is consistent with 16, which found the unreachable
positions are not structurally different, merely about twelve times larger.

This does **not** settle shared-tree parallel df-pn. That form makes one search
faster rather than making several searches diverse, and a tree twelve times too
large is exactly the case where raw speed helps. But it does mean the cheap form
of the idea is spent, and anyone attempting the expensive form should expect to
be fighting tree size rather than search quality.

## Promotion Rule

No E search feature is promoted by intuition. A feature is promoted only after:

1. semantic correctness is unchanged;
2. PV validation is clean;
3. no-mate controls remain clean;
4. speed improves on the relevant frozen suite;
5. regressions are documented and intentionally accepted or rejected.


### 42. `-M` Was A Per-Table Budget Wearing A Total's Name

Peak working set, all lanes driven to a full 20 s budget:

| configuration | stated `-M` | measured peak |
|---|---|---|
| default, 8 lanes | 256 MB | 615 MB |
| `--no-portfolio`, 1 lane | 256 MB | 217 MB |
| default, 8 lanes | 64 MB | 232 MB |
| 8 positions, `--parallel-positions 4` | 256 MB | **1994 MB** |

The flag bounded each table, and tables are held per portfolio lane and per
batch worker. So the default multiplied it by about 2.4x -- not the full 8x,
because lanes are weighted and the restricted ones search less -- and
`--parallel-positions` multiplied it again. A user asking for 256 MB across
four workers got two gigabytes, and the error runs in the direction that ends a
long batch with an allocation failure rather than a warning.

`-M` is now the budget for every table alive at once, divided by lanes and
workers. The default rises 256 -> 2048 so the default portfolio's per-lane
share stays at exactly the 256 MB the tuning in 8l measured: the shipped
configuration is unchanged, and the flag now means what it says. 1.0.0 is
unreleased, so nothing depended on the old reading.

After: default 609 MB (was 615, unchanged as designed); `-M 256` 120 MB;
`--parallel-positions 4` at `-M 256` 533 MB, down from 1994.

That last figure is over its stated budget, and the reason is worth stating
rather than hiding: `-M` bounds table *entries*, not resident bytes. Thirty-two
concurrent searches carry fixed per-search structure that no entry ceiling
covers, and the allocator does not return freed pages between positions. The
claim this change earns is that `-M` now scales the right way and is the right
order of magnitude -- not that it is a hard RSS bound. `EST_BYTES_PER_ENTRY`
carried that caveat already; it now applies to the aggregate too.

A share is floored at 1 MB. A budget divided below one entry would evict on
every store, which is slower than having no table at all.


### 43. What A Speedup Is Worth, And Root Splitting Under DFPN

41 rejected the portfolio form of parallel DFPN and left the shared-tree form
open, with an estimate of two to four weeks. Two cheap measurements price that
estimate before anyone spends it.

### The speedup-to-solves curve

A parallel search does not think better, it thinks more, so a perfect Kx
speedup is a K-times larger node budget in the same wall clock. Node budgets
are deterministic where wall clock is not. 40 mate-16 development positions,
single-threaded, no portfolio, `--direct-depth`:

| speedup | budget | solved | gain |
|---|---|---|---|
| 1x | 2,000,000 | 22/40 | -- |
| 2x | 4,000,000 | 24/40 | +2 |
| 4x | 8,000,000 | 24/40 | +2 |
| 8x | 16,000,000 | 25/40 | +3 |
| 16x | 32,000,000 | 26/40 | +4 |

**Four doublings buy four positions: about one position per doubling of
speed.** The curve is logarithmic, which is what an exponentially growing tree
implies -- and it reads as an upper bound, because real parallel search
duplicates work and K threads deliver less than K times the useful nodes.

This prices every remaining parallelism proposal. Shared-tree df-pn at a
realistic 2-5x buys **+1 to +2 positions of forty** for two to four weeks. The
restriction portfolio already delivers +5 to +6 and is already built. Where
DFPN is 86% of the work, Amdahl caps the total speedup at 7.1x however many
threads are thrown at it -- under three doublings, so under +3 positions, at
infinite hardware.

It also explains 41's result rather than merely agreeing with it. The
six-variant oracle scored 25/40 at the 2M budget; a single variant needs 16M --
three doublings -- to reach the same 25. The entire diversity ceiling of a
six-way portfolio is worth about a 8x speedup. Two very different experiments
agree on the exchange rate between cores and positions, which is the useful
thing to know about this search.

### Root splitting under DFPN

Root splitting was measured inert (32) under the depth-first route, and never
re-measured after DFPN became the default. Same 40 positions, 20 s cap, no
portfolio, 1 thread against 8:

| | solved | paired total | speedup |
|---|---|---|---|
| run 1 | 24/40 both | 18.60 s vs 19.30 s | 0.96x |
| run 2 | 24/40 both | 20.30 s vs 19.44 s | 1.04x |

Median per position 0.99x and 1.03x; ranges 0.89-1.02x and 0.94-1.18x. Two runs
straddling 1.0 is the answer: **eight threads change nothing, and the effect is
smaller than the run-to-run noise.** Not one extra position either.

The reason is the same one 32 gave, and DFPN does not change it. Root splitting
divides the attacker's first moves between threads, but a proof needs *every*
sibling refuted, so the whole set must be searched regardless -- there is no
early cut-off for the split to exploit. The threads finish the same total work
in the same total time.

Both rows of the parallelism backlog are now measured rather than estimated.
The honest summary is that this engine's remaining problem is tree size, and
parallelism is a poor instrument against it: a doubling of hardware buys a
single position, while the 16 finding -- unreached positions are about twelve
times too large -- needs roughly three and a half doublings just to pull one
notch of depth into range.


### 44. The Default Budget Is Per Table, Not A Split Total

42 made `-M` a total and raised the default 256 -> 2048 so that the default
portfolio's eight lanes would still see 256 MB each. That reasoning holds only
at one worker count. At `--parallel-positions 8` the same total splits eight
ways again -- 2048 / (8 workers x 8 lanes) = **32 MB per table**, half of the
64 MB that 8l already records as sitting well below the knee.

The regression would have been invisible: batch runs would simply have solved
slightly less, in exactly the mode people reach for when they have a lot of
work to do.

The fix is not a better constant. A default expressed as a total has to be
divided by a table count that is not known until the flags are parsed, so any
constant is wrong at some worker count. The default is therefore expressed the
other way round -- **256 MB per table, total scaling with the number of
tables** -- which is the tuned figure at every lane and worker count, and cannot
be eroded by raising `--parallel-positions`. An explicit `-M` keeps 42's
meaning: a total, divided.

Same shape as `--threads auto`, and the same justification: the default adapts
to the machine and the workload, an explicit value is obeyed literally.
`--print-config` reports `memory_is_total` so which rule is in force is
visible rather than inferred.

The measurement 42 rests on is unaffected -- an explicit `-M 256` still costs
533 MB at four workers rather than 1994 -- and the default is now the tuned
configuration by construction rather than by a benchmark that would have had to
be re-run at every worker count.


### 45. Two Ceilings: Tables Are Finished, Ordering Is Not

43 established that every uniform improvement is worth about one position per
doubling, and that this applies to node-count work exactly as it does to speed
-- halving the nodes needed *is* doubling the budget. So the only things worth
ranking are large multipliers. Two of them were measured directly.

### Transposition rate: 1.25 to 1.38

DFPN nodes visited over distinct positions stored, from the existing counters:

| set | median | range |
|---|---|---|
| mate-10, 20 solved | 1.25 | 1.00 - 1.67 |
| mate-16, 31 solved | 1.38 | 1.00 - 5.67 |

The search visits about 1.3 nodes per distinct position. **There is almost no
duplication left to harvest**, so every remaining table, DAG and sharing idea is
capped at about 1.4x -- well under a single doubling, therefore under one
position of forty. The exact table already reuses verdicts across depths: its
key excludes depth and entries carry `min_proved` / `max_disproved` bounds, so
the obvious cross-depth win was collected long ago.

Table and memory work is finished. Not "low priority" -- finished.

### Ordering ceiling: 330x to 400x

The certificate *is* the proof tree: the attacker move chosen at each OR node
and every legal defender reply at each AND node, recursively. A search that
always tried the winning move first at every OR node would stop at the first
success and never touch a losing sibling, so it would visit exactly that tree.
Searched nodes over certificate nodes is therefore a true upper bound on what
all ordering, hint and selectivity work could ever save.

| set | median | mean | range |
|---|---|---|---|
| mate-10 | 330.8x | 860.1x | 3.0x - 7,121.8x |
| mate-16 | 402.3x | 1,334.3x | 5.2x - 13,568.5x |

Two orders of magnitude, against 1.4x for everything else measured. This is the
only category in the engine that is not already near its floor.

Three things temper it. The bound is an oracle: perfect ordering means knowing
the winning move without searching for it, so no implementation approaches it --
realistic ordering gains in search are single-digit multiples, and by 43 a 4x
gain is two positions. The distribution is heavy-tailed, mean far above median,
so a summary statistic hides most of the story. And the ratio is the ordinary
inefficiency of proof search, not evidence of a defect.

What the tail *does* say is where to look. At the good end the engine is within
3-5x of the minimal proof and cannot be meaningfully improved; at the bad end it
spends thirteen thousand nodes per proof node. Those are the frontier positions,
and they are the same ones 16 found to be twelve times too large. The two
findings are the same phenomenon seen from different sides: the frontier is not
hard because the trees are big, it is hard because the search wanders in them.

Ordering is where the remaining headroom is, and it is the last category with
any.


### 46. The Waste Is Per-Ply, Not At The Root

45 found a 330-400x gap between nodes searched and nodes in the proof, with a
tail past 13,000x, but a whole-search ratio cannot say *where* the search
wanders. If the waste sat at the root -- losing first moves tried before the
winning one -- root move ordering would be the fix and a small change. So the
worst positions were descended: play the certificate's own first move and each
defender reply, then re-measure the ratio on the resulting position at N-1.

| parent | children, median |
|---|---|
| 2,288x | 1,278x |
| 2,165x | 2,189x |
| 1,038x | 996x |
| 5,662x | 2,123x |
| 5,129x | 3,191x |
| 13,513x | 49.7x |

**The ratio does not collapse when you descend.** Two plies down the subtree is
enormously smaller but the inefficiency is the same order -- a median factor of
about 1.6 per two plies. Each ply contributes its own roughly constant
multiplier and the total is that multiplier compounded over the depth, which is
what produces 400x at mate-16 without any single ply being at fault.

So root ordering is not the fix, and neither is any localised change. There is
no defect here to find; the search is uniformly, mildly unselective at every
ply. This is suggestive rather than settled: most defender replies lead
straight to mate and cannot be re-searched, so this is one or two children per
position across seven positions.

Compounding cuts both ways, and that is the useful half. A per-ply multiplier
raised to the power of the depth means a *small* per-ply improvement is a large
total one: trimming the per-ply factor from about 1.27 to 1.20 is roughly a
3x node reduction over a mate-16 tree, which by 43 is +1.5 positions. Nothing
else measured offers that, because nothing else compounds.

Which names the one remaining experiment. In DFPN the per-ply selectivity signal
is the proof and disproof number initialisation, and this engine uses the
crudest form there is -- the move count:

    AND node:  guess = {replies.size(), 1}
    OR node:   guess = {1, moves.size()}

A position with twelve legal replies and a position with twelve legal replies
that are all forced king moves into a mating net look identical to this. Every
other search decision -- which child to descend, when to stop -- is driven by
these two numbers, so they are the per-ply multiplier. Heuristic initialisation
from cheap domain features (king mobility, checking replies, whether the reply
is forced) is the standard remedy in the df-pn literature and is not tried here.

That is the last unexplored lever with a compounding payoff. If it fails, the
performance work is finished and the remaining gap belongs to a different
engine.


### 47. Heuristic Proof-Number Initialisation: Tried And Rejected

46 identified DFPN's proof-number initialisation as the last lever with a
compounding payoff, and named the obvious first heuristic: the move count cannot
tell a free choice from a forced one, so weight an AND node's proof estimate up
when the defender is *not* in check and the search will prefer forcing lines.

Implemented behind `--dfpn-check-bias N`, deterministic node budgets:

| bias | mate-10 solved | nodes vs off | mate-16 solved | nodes vs off |
|---|---|---|---|---|
| 1 (off) | 18/24 | 1.00x | 24/40 | 1.00x |
| 2 | 18/24 | 0.63x | 25/40 | 0.73x |
| 3 | 18/24 | 0.54x | 24/40 | 0.84x |
| 5 | 18/24 | 0.48x | 23/40 | 0.52x |
| 8 | 17/24 | 0.39x | 22/40 | 0.46x |

Ratios below 1.00x mean *more* nodes. Every setting is worse, monotonically so,
and solve counts are flat or declining. The single 25/40 at bias 2 is one
position against a paired node count that got worse and a second set that did
not reproduce it.

**Rejected**, and the reason is more useful than the result. The move count
already encodes forcing-ness: a checking move is exactly one that leaves the
defender few legal replies, so a forced position already receives a low proof
estimate through the count itself. The bias re-applied a signal that was
present, double-counting it, and the distortion grew with the weight -- which is
the monotone degradation in the table.

This qualifies 46's claim. Twelve quiet replies and twelve forced king moves are
*not* actually indistinguishable to the count, because the forced case rarely
has twelve replies. The count is a cruder signal than it could be, but it is not
the blind one that section implied.

The flag is kept at its measured-off default, as `--refutation-hints` is, so the
negative result stays reproducible rather than becoming folklore.

This rejects one heuristic, not the category: initialisation from king mobility,
distance-to-mate estimates or threat structure is still untried. But the prior
should now be lower. The cheapest and most obvious domain feature turned out to
be redundant with what the count already measures, and the same objection --
that move counts are already a proxy for confinement -- applies to king mobility
and to most cheap features anyone would reach for next.


### 48. Stalemate Goal

The attacker forces a stalemate rather than a mate. Chest solves six job types;
this is the second of them, and the cheapest, because the AND/OR structure is
identical -- attacker needs one move, defender must have every reply refuted --
and only the terminal predicate changes.

**The goals are disjoint, not nested.** A checkmate FAILS a stalemate goal and a
stalemate FAILS a mate goal, so nothing computed for one is valid for the other.
The goal is therefore part of the transposition key, in a spare bit of the
context word. Tables are per-search and a run has one goal, so nothing can mix
them today -- but a stalemate verdict satisfying a mate query is a false proof,
which is the one class of bug this engine exists to make impossible.

**The check term carries the goal in its sign.** Ordering scores a checking move
+50000 under a mate goal and -50000 under a stalemate goal, because a check is
progress toward one and disqualifying for the other. The magnitude is
load-bearing beyond ordering: both terminal scans read |score| >= 50000 as "this
move gives check" and skip a test they can already decide.

That coupling produced two bugs during implementation, in opposite directions,
and both are now covered by tests:

- The DFPN scan tested `score < 50000` to mean "cannot mate". Under a stalemate
  goal the check term is negative, so **every** move was skipped and the search
  never saw a stalemate at all. Symptom: positions with a verified stalemate in
  one returned nothing on the default route while `--route depth-first` solved
  them.
- The exact prover's shortcut had the mirror error: it would have treated a
  checking move as goal-compatible and asked only whether replies existed --
  **accepting a checkmate as a stalemate**. A false proof, caught before it
  could produce one.

Both now call one predicate, `move_can_reach_goal(score, goal)`, so the test
exists in a single place rather than twice with opposite polarity.

Results are reported as `sm N`, never `dm N`, and `verify_proof.py` checks a
stalemate certificate against `is_stalemate` and a mate certificate against
`is_checkmate` -- a leaf claiming the wrong one is rejected even if the position
satisfies the other.

**Optimisation status: implemented and validated, not yet tuned.** 40 generated
positions at depths 1 to 5 all solve, and all 40 certificates verify
independently. But the check-term inversion, measured against disabling the
term entirely, gives **identical node counts -- 27,353 either way**. The corpus
is too easy to discriminate: 275 random attempts produced 40 positions, so
forced stalemates are common and shallow in sparse endgames, and ordering
barely matters at depth 5.

So the inversion is justified by correctness, not by measurement, and this
section should not pretend otherwise. Tuning stalemate to the standard the
directmate mode reached needs a corpus with hard, deep problems, and generating
one is a different problem from generating easy ones. No public stalemate corpus
was usable: YACPDB holds the problems but exposes no reachable API, and its
compositions carry their own rights.


### 49. Stalemate Tuning: The Levers Are Already Pulled

48 shipped the stalemate goal untuned, because the generated corpus could not
tell two orderings apart. A real corpus now exists, and the answer it gives is
that there is nothing left to tune.

**The corpus.** PDB holds 579 direct stalemate problems -- not the 20,000 a
naive query suggests, because its `STIP` field is a substring match and `=8`
also selects `ser-=8`, `h=8` and retro reconstructions like `Orthorek. n=81,5`.
Filtering those leaves a distribution concentrated at the shallow end: 329 at
`=2`, and 60 at depth 10 or more.

Measured across all 579 at a 10 s cap: 505 solved, 33 shorter, 30 unsolved,
10 refuted, 1 illegal -- **538/568 = 94.7%**. The shape is what matters:

| depth | solve rate |
|---|---|
| `=2` to `=9` | 100% |
| `=10`, `=11` | 87.5%, 90.9% |
| `=12` | 28.6% |
| `=13` | 50.0% |
| `=15`, `=16` | 25.0%, 9.1% |

So a frontier exists, and the 60 problems at depth 10-16 sit at 51.7% -- the
mid-range that discriminates between builds.

**The restriction portfolio contributes nothing.** On those 60 problems at a
10M-node budget, every lane was measured against the unrestricted search:

| lane | solved | adds over unrestricted |
|---|---|---|
| unrestricted | 48/60 | -- |
| `-P 2` | 48/60 | +0 |
| `-X 4` | 15/60 | +0 |
| `-K 3` | 12/60 | +0 |
| `-R 1` | 1/60 | +0 |
| `-C 1` | **0/60** | +0 |

The union of every lane is 48/60: exactly the unrestricted set. This is the
single largest difference from directmate, where the portfolio is worth +15 of
60 (8f). The reason is visible in the `-C 1` row: restrictions prune toward
forcing play, and a stalemate is reached by quiet moves. `-C 1` admits only
checking moves and therefore solves nothing at all, since a checked king is
never stalemated.

**No search setting beats the shipped default either.** Same 60 problems:

| configuration | solved | nodes |
|---|---|---|
| default (dfpn, check term inverted) | 48/60 | 1.00x |
| check term disabled | 48/60 | 1.00x |
| dfpn every depth | 48/60 | 1.00x |
| scored-vector order / order-all / eager defender | 48/60 | 1.00x |
| route depth-first | 42/60 | 1.30x |
| route shallow-fast | 42/60 | 1.30x |
| **no proof hints** | **27/60** | 0.68x |

Every alternative is equal or worse. Two results are worth keeping:

- **Proof hints are worth +21 positions of 60.** By far the largest single
  contributor for this goal, and already on by default.
- **The check-term inversion is neutral for ordering, on hard positions as well
  as easy ones.** 47 identical solves and identical node counts. It stays
  because it is required for CORRECTNESS -- both terminal scans read its sign to
  decide which moves can reach the goal (48) -- but the claim that it improves
  the search is now measured and false. 48 said the inversion was justified by
  correctness rather than measurement; that was right, and it remains right for
  a stronger reason than caution.

**Generated positions cannot substitute for composed ones -- depth is not
difficulty.** The plan was a hybrid corpus: composed problems for an honest
reach figure, generated blocked-pawn positions for volume and discrimination.
120 were generated, and they came out at depths 6-9 rather than the 6-12 band
targeted. That alone was survivable; the cost measurement was not:

| corpus | solved | median nodes |
|---|---|---|
| generated, depths 6-9 | 120/120 | **740** |
| composed, depths 10-16 | 48/60 | **1,928,706** |

Two and a half thousand times apart. Random construction produces stalemates
that are *long* but narrow -- the search walks straight down them. A composed
problem of the same nominal depth is hard because a composer deliberately made
the tree wide and the key move unobvious. Nominal depth measures neither.

So there is no hybrid. The only corpus that discriminates for this goal is the
60 composed problems at depth 10-16, and there is no way to enlarge it.

**Conclusion.** Stalemate is not untuned. It is tuned, in the sense that every
knob this engine exposes has been measured against a real corpus and the shipped
configuration is the best of them. What it lacks is not tuning but headroom:
directmate could be improved because matetrack has thousands of problems it
failed and a portfolio that converted; stalemate has 30 unsolved problems in the
world's collection and no lever that moves them.


### 50. Selfmate Goal

The attacker forces the DEFENDER to mate him. `--goal selfmate` / `--selfmate`,
reported as `sfm N`. The third of Chest's six job types.

Structurally different from the other two goals, not just another terminal
predicate. For a directmate or a stalemate the goal is tested after each
*attacker* move; here it is "the attacker is mated", which is a statement about
the side to move, so it is tested at an attacker node before moving. Chest's
four degenerate cases stay distinct: attacker mated is a solution, attacker
stalemated is not, defender mated is not, defender stalemated is not.

**It has its own recursion and its own route, deliberately.** The first attempt
reused the depth-first route and redirected the two
`result.proof = prove_attacker(...)` call sites. That was not enough, because
the route also reaches the exact prover through the ROOT SPLIT, whose workers
call `prove_attacker` directly. So the selfmate goal ran the DIRECTMATE search
without saying so.

The symptom is worth recording. All 260 composed selfmates came back `refuted`,
and the single one that came back solved was a position that happens to contain
a mate in one -- reported as `sfm 1` with certificate
`{"a":"d6f6","mate":true}`, the directmate leaf format. A goal running the wrong
search and reporting the wrong goal's certificate is the worst failure this
engine can have, and it was caught only because a corpus of real problems
disagreed with it 260 times.

So `run_selfmate_route` shares nothing it cannot be shown to share safely: no
root split, no preconditioner. DFPN's proof numbers are defined against the
directmate terminal and inverting who must be mated makes them meaningless, so
running it would publish disproofs answering a different question. Both are open
work rather than impossibilities.

A second and quieter bug followed: the engine could not infer a depth from its
own `sfm N` token, so every problem was searched at depth 1 and refuted again.
`infer_mate_depth` now reads `sfm` before `sm` and `#`, because a selfmate line
also carries an `s#N` and reading that as a directmate searches the wrong goal.

**Measured: 259 of 260 composed selfmates from a public collection, at s#2 to
s#4, solved within 20 s. One refuted.** For comparison, a random position
contains a selfmate in one about once in 71,000 -- this genre cannot be
generated, only sourced, which is why the corpus mattered before the code did.

Not yet done: certificate verification (`verify_proof.py` does not know the
`selfmated` leaf), root splitting, and the preconditioner. Reach at greater
depths is unmeasured.


### 51. Selfmate Preconditioner: +124 Positions Of 200

Selfmate is the first goal since the directmate work with real headroom. 904
composed problems at s#5 to s#10, measured at a 10 s cap: **319/903 = 35.3%**,
with 584 unsolved. Stalemate had 30 unsolved problems in the world's entire
collection; this has 584 in one export.

The AND/OR structure survives the goal change, so proof and disproof numbers
still mean what they mean -- what moves is the terminal, from "after an attacker
move, is the defender mated" to "at an attacker node, is the attacker mated".
`dfpn_selfmate_attacker` and `dfpn_selfmate_defender` are that walker.

**Measured on 200 sampled problems at a 3M-node budget:**

| configuration | solved |
|---|---|
| exact search only | 7/200 = 3.5% |
| with the preconditioner | **131/200 = 65.5%** |

+124 positions. The largest single improvement measured anywhere in this
project -- larger than promoting DFPN for directmate, which was worth +17 of 60.

**It was inert on first measurement, and the reason is worth keeping.** The
first run scored 7/200 both ways, identical. The preconditioner was running --
645 DFPN nodes, 14 proved -- and storing the move its numbers favoured, but
`prove_selfmate_attacker` never read the hint table. A preconditioner whose only
consumer does not consult it is an expensive no-op, and it looked exactly like
"measured, no effect".

**The verifier then caught a reporting bug the search could not see.**
Certificates failed with "certificate proves mate in 6, reported 2": the
defender node built its PV from the FIRST reply, but a selfmate in N is a claim
about EVERY defence, so the depth is the worst line and not the first one. The
search was correct and the number it printed was not, which is precisely the
class of error an independent checker exists to find. The PV is now the longest
branch, and 26 of 26 sampled certificates verify.


### 52. The Restriction Portfolio Transfers To Selfmate

For stalemate the portfolio was worth nothing: every lane's solutions were a
subset of the unrestricted search's, and `-C 1` scored 0 of 60 because a checked
king is never stalemated (49). Selfmate is the opposite, and for the reason that
prediction rested on -- selfmates are reached by FORCING play, so restrictions
that prune toward force prune toward the goal.

200 sampled problems from s#5-s#10, 3M-node budget:

| lane | solved | adds over unrestricted |
|---|---|---|
| unrestricted | 131/200 | -- |
| `-K 3` | **136/200** | +6 |
| `-C 1` | 27/200 | **+7** |
| `-X 3` | 97/200 | +6 |
| `-R 1` | 59/200 | +2 |
| **union** | **148/200** | **+17** |

`-K 3` beats the unrestricted search outright, and `-C 1` earns its place by
solving seven problems nothing else solves while solving only 27 overall --
which is exactly what a portfolio lane is for and why coverage, not individual
score, is the right selection criterion. Greedy set cover orders them
`C1(+7) X3(+6) K3(+3) R1(+1)`.

+17 of 200 is the same shape of result as the directmate portfolio's +15 of 60
(8f), on a goal where the mechanism was in genuine doubt after stalemate.

**Ordering, by contrast, is measured out.** Disabling the check term and forcing
`--order-all` both leave the count at exactly 131/200. As with stalemate, the
check term stays for correctness rather than for speed, and there is no ordering
lever here that this engine exposes.

**Proof hints remain the dominant single factor**: 131/200 with them, 7/200
without.


### 53. Root Splitting For Selfmate: Correct, Deterministic, And Inert

Selfmate now has a root split. `run_selfmate_root_split` is a separate function
rather than a branch in the directmate splitter, whose body tests whether the
DEFENDER is mated and then calls `prove_defender` -- both wrong here, and the
last time this goal borrowed directmate machinery it ran the directmate search
in silence (50). Worker construction, the shared table, the atomic claim counter
and lowest-index acceptance are shared; the body is not.

**It made the engine four times worse before it made it faster.** First
measurement: 9 of 60 at eight threads against 37 of 60 at one. The workers are
fresh `Search` objects, so they never received the preconditioner's ordering
hints -- and hints are worth +124 positions of 200 on this goal (51). The split
was parallelising precisely the work the hints exist to make unnecessary.
Handing `attacker_proofs` to each worker before the split fixed it.

After the handoff, on 60 sampled problems at a 3M-node budget:

| | solved | identical result |
|---|---|---|
| 1 thread | 37/60 | -- |
| 8 threads | 37/60 | **60/60** |

Every position returns the same move and the same depth at both thread counts,
which is what lowest-index acceptance is for.

**And it buys nothing.** Wall clock on 24 problems, 20 s cap, the 15 solved by
both: 18.1 s against 17.1 s, **1.06x total and 1.04x median**.

That is the directmate result again (32), and for the same reason. A proof needs
every sibling refuted, so dividing the root moves between threads removes no
work -- and here the hints usually put the winning move first, which
concentrates the whole search in one root subtree that the split cannot divide
at all.

So it ships correct rather than useful: `--threads` no longer silently does
nothing for selfmate, and it still does nothing measurable. Parallelism for this
goal, as for the others, would have to happen inside the tree rather than at its
root.


### 54. A False Proof In The Stalemate Shortcut, And A Route Lane

Chasing the positions Chest solves and this engine did not turned up a false
proof: `8/4R3/8/1K6/8/8/2R5/k7` with `--stalemate` returned `bm e7e1; sm 1` and
the certificate `{"a":"e7e1","stalemate":true}`. Re1 is CHECKMATE. The engine
reported a mate as a forced stalemate.

The cause is the ordered-check shortcut, which infers "this move gives check"
from the move's ordering score rather than from a flag. Under a mate goal that
inference is safe: the check term is +50000 and every other term sums to at most
18050, so a score at or above 50000 means check, and an unscored move (0) simply
fails the test and pays for the full terminal check.

Under a stalemate goal the check term is NEGATIVE, so the safe direction
inverts. An unscored move scores 0, reads as "not a check", passes the test, and
is accepted the instant the defender has no reply -- which is exactly what a
checkmate looks like. No threshold repairs this, because 0 is a legitimate score
for a quiet move and an illegitimate one for an unscored check. Two attempts at
a better threshold both failed before that became clear.

The shortcut is now restricted to the mate goal in all three places that use it,
and the other goals pay for the terminal test. Verified rejected on every route
and both thread counts, with genuine stalemates still solved.

Two smaller fixes fell out of the same hunt. `run_root_split_depth` tested
`is_checkmate` whatever the goal, while the certificate string beside it was
already goal-aware -- latent, since the default route does not reach the root
split, and exposed the moment a depth-first lane was added. And `is_goal` needed
the same treatment for Selfmate (50).

**The route lane.** The losing positions are tiny-material endgames -- K+R vs K
at depth 13 -- where proof numbers from move counts carry no signal because
every branch looks alike. A plain depth-first search recovers 3 of 8 stalemate
losses and the only selfmate loss where no DFPN configuration recovers any, so
the portfolio gains a depth-first lane for the non-directmate goals. Directmate
keeps its measured lane set unchanged (8e).

**Head to head after these changes**, 60 problems each, 20 s, 256 MB:

| | Chest 3.19 | MateProver | on shared positions |
|---|---|---|---|
| stalemate, depth 10-16 | 31/60 | **36/60** | **1.57x median faster** (was 1.6x SLOWER) |
| selfmate, s#5-s#10 | 34/60 | **48/60** | **3.99x median faster** |

The speed regression on stalemate is gone. Nine stalemate positions and one
selfmate position are still solved only by Chest, so neither goal dominates it
outright; the portfolio lane helps in isolation but is memory-starved sharing a
budget nine ways, which is the next thing to test rather than a conclusion.

### 55. The Starved Lane: A Flag That Cost A Factor Of Nine, Silently

The memory-starvation hypothesis at the end of 54 was right, and the fault was
in the measurement as much as in the engine.

Those head-to-head runs passed `-M 256` to both engines to hold memory equal.
For Chest that is one table for one search. For this engine an explicit `-M` is
a TOTAL (42, 44), divided across the nine lanes -- 28 MB each. The flag that
looked like it granted a quarter gigabyte in fact took away a factor of nine.

The nine positions, at a fixed 20 s cap:

| per-lane budget | recovered (of 9) |
|---|---|
| `-M 256` as a total, 28 MB a lane | **0** |
| default, 256 MB a lane | **8** |

Nothing about the search changed between those two rows. Eight of the nine
"losses to Chest" were an artifact of how the benchmark invoked its own engine.

**The cliff is sharp, and 64 MB is the whole of it.** Sweeping per-lane memory
over the nine positions and over 40 easy ones, two workers so the harness's own
concurrency does not multiply table memory:

| per lane | recovered | easy solved | easy median | easy total |
|---|---|---|---|---|
| 16 MB | 0 | 36/40 | 2.80 s | 319.2 s |
| 32 MB | 0 | 36/40 | 2.73 s | 299.7 s |
| **64 MB** | **8** | 36/40 | 1.40 s | 296.2 s |
| 128 MB | 8 | 36/40 | 2.07 s | 291.8 s |
| 256 MB | 8 | 37/40 | 1.35 s | 301.4 s |

Recovery is a step function: nothing below 64 MB, everything at and above it.
Throughput on easy positions is flat across the whole range, so the floor is
free -- 64 MB is not a compromise between capability and speed, it is the point
where capability appears at no cost.

**A rejected fix, recorded because it measured well on the wrong axis.** The
first attempt funded the floor by DROPPING lanes: `kMinLaneMb` is 64, so
`-M 256` bought four lanes of 64 MB instead of nine of 28. It recovered the
eight positions and honoured the cap exactly. It was still wrong. Paying for
them cost five restriction lanes, and restrictions are what make selfmate fast
(52) -- the selfmate median went to 0.20x of Chest, five times SLOWER, buying
one position's coverage with a broad speed loss. Coverage and speed are not the
same axis and a fix may not be evaluated on only one of them.

**The shipped fix: charge the floor only where it buys something.** A lane is
UNRESTRICTED when it removes no attacker options -- lane 0 and the route-
diversity lanes. Those search the whole space, so their tables must hold a whole
search, and they are the lanes that fall off the 64 MB cliff. A restricted lane
searches a deliberately smaller space and does not need as much table to hold
it. So the unrestricted lanes take `kMinLaneMb` and the restricted lanes divide
what is left: at `-M 256`, two lanes of 64 MB and seven of 18 MB. Every lane
still runs, the cap is honoured exactly, and recovery is 8 of 9 at `-M 256`,
`-M 512` and the default alike.

The asymmetry is the whole point. Spreading a budget evenly over lanes with
unequal appetites is what created the cliff; spending it where it changes an
answer is what removes it.

**Why this one matters beyond its own numbers.** Every output stayed
well-formed throughout. No error, no warning, no wrong answer -- only a quieter
engine, losing eight positions it could solve, in response to a flag a careful
user would type precisely because they were being careful. A false proof (54)
announces itself to the verifier. This did not announce itself at all, and it
was found only by disbelieving a benchmark that flattered the comparison's
other side. The regression guard in `run_tests.py` is therefore a solving check
at `-M 256`, not a configuration check: nothing about the reported config was
ever wrong.

### 56. The Sixteenth Thread: A 30x Loss Hidden Below A Threshold

Fixing the memory (55) left selfmate still five to six times SLOWER than Chest
on the median shared position, which the memory work was supposed to explain
and did not. The restriction-lane hypothesis was wrong -- restoring every lane
changed nothing -- so the cause was elsewhere.

`--no-portfolio` solved the same set in **5.9 s against 254 s**: a factor of 43,
on identical positions, from a flag that only removes lanes. The portfolio's
median was 20.1 s, which is not a search time at all. It is the time limit.

The cause is thread assignment. Lane threads follow the entry weights, so at the
default 16 threads lanes 0-2 receive four, three and three. A selfmate lane with
more than one thread ROOT-SPLITS, and the selfmate root split -- recorded as
merely inert at 53 -- is ruinous here:

| threads | `--no-portfolio` | portfolio |
|---|---|---|
| 1 | 0.57 s | 0.68 s |
| 2 | 0.56 s | 0.67 s |
| 4 | 0.57 s | 0.71 s |
| 8 | 0.58 s | 0.69 s |
| **16** | **0.57 s** | **19.37 s** |

The cliff is at ten threads, and it is an artifact of the assignment arithmetic:
below ten, every lane already gets exactly one thread and the extra-thread loop
never runs. Every thread sweep this engine had been through stayed under that
threshold, so a 30x defect sat in the DEFAULT configuration, on a machine whose
default thread count is 16, without appearing in a single measurement.

Extra per-lane threads were then measured against what they cost, over 20
positions a goal:

| | solved | total | median |
|---|---|---|---|
| selfmate, default 16 threads | 16/20 | 249.5 s | 20.22 s |
| selfmate, one thread a lane | 16/20 | **16.1 s** | **0.29 s** |
| stalemate, default 16 threads | 20/20 | 39.6 s | 0.50 s |
| stalemate, one thread a lane | 20/20 | **34.5 s** | **0.43 s** |

Identical coverage at a fifteenth of the time. The extra threads are therefore
not handed out for the non-directmate goals at all; directmate keeps the tuned
weights, which were measured on a path that does not root-split this way.
Restoring them is gated on the selfmate root split earning its keep, which it
has never yet been measured to do.

**Head to head after 55 and 56**, 60 problems each, 20 s, 256 MB total to both:

| | Chest 3.19 | MateProver | on shared positions |
|---|---|---|---|
| stalemate, depth 10-16 | 30/60 | **52/60** | 1.51x total, **1.09x median** |
| selfmate, s#5-s#10 | 34/60 | **48/60** | 8.86x total, **6.35x median** |

Stalemate solves everything Chest solves -- **only-Chest is 0** -- and its median
has crossed from 0.91x (slower) to 1.09x. Selfmate is at its best measured
margin, 6.35x median against 3.99x before. One selfmate position remains solved
only by Chest.

The lesson is the same one as 55 wearing different clothes. Both defects were
invisible: correct answers, well-formed output, no warning. Both were found by
distrusting a comparison rather than by reading code. And both lived in exactly
the configuration a user gets by typing nothing at all.

### 57. The Worker That Would Not Stop

56 blamed the root split and withheld threads from it. That was treating a
symptom, and the evidence that it was a symptom had been on the page the whole
time: the slow runs did not take a long time, they took *exactly the time
limit*. Medians of 20.10 s and 20.08 s under a 20 s cap are not search times.
They are the sound of a process that already has its answer and cannot leave.

**The defect.** A root-split worker polls `cancel`, which is its own slot's
flag, set by a sibling that proved a better root move. Nothing pointed at the
flag belonging to the SEARCH the split serves. So when a portfolio lane lost the
race and was cancelled, its workers noticed nothing and kept taking root moves
until their own deadline -- and their deadline is the run's time limit. One
abandoned lane held the whole process to the cap, with the winning answer
already in hand.

The fix is a second flag, `external_cancel`, set to the lane's own cancel when a
worker is built, checked both in `search_cancelled` and at the top of the
worker's root loop -- the first so an in-flight subtree unwinds, the second so no
new root move is started.

Measured on the same 25 selfmate positions, one spare thread on the route lane:

| | solved | total | median |
|---|---|---|---|
| before | 22/25 | 353.6 s | 20.10 s |
| after | 22/25 | **49.8 s** | **0.38 s** |

A factor of seven in total time and fifty in the median, for identical coverage.
This also explains 56's numbers without needing the root split to be at fault:
the split was never intrinsically slow, it simply could not be stopped, and
every configuration that used it inherited the time limit as its floor.

**What the threads are actually for.** With cancellation fixed, the split can be
measured on its merits. It is what reaches positions single-threaded search
cannot: the last selfmate position Chest solved and this engine did not goes
26.0 s at one thread to 7.2 s at sixteen, while the dfpn route never solves it at
any thread count. But the threads are not free -- they come from the lanes
resolving everything else -- and the goals disagree about the trade:

| spare threads on the route lane | selfmate | stalemate |
|---|---|---|
| 1 | 49/60 | **47/60** |
| 2 | 49/60 | 44/60 |
| 4 | 49/60 | 43/60 |

Selfmate is flat; stalemate falls monotonically. So the route lane gets exactly
one spare thread: enough to reach what it alone can reach, not enough to starve
the lanes doing the ordinary work. `--route-lane-threads` exposes the number,
and `--portfolio-lanes` caps how many lanes run at once, for machines smaller
than the one this was tuned on.

**And a boundary worth stating plainly.** Two stalemate positions resisted every
thread count, and threads were never their problem -- they need 256 MB. Under
`-M 256` an unrestricted lane gets 64 MB, and at 64 MB one of them solves and the
other does not; at 256 MB both solve in 9.8 s and 2.7 s.

That is not a defect, it is the arithmetic of a portfolio. Nine lanes sharing a
total cannot each have the whole of it, so a nine-lane engine and a one-search
engine given the SAME total are not being asked the same question. Both framings
are reported rather than the flattering one being chosen: at its shipped default
of 256 MB a lane, MateProver solves every position Chest solves on both goals;
at an equal 256 MB total, three positions go the other way. The default is what
a user runs, and the equal-total figure is what a sceptic should be handed.

### 58. The Cooperative Goals, And A Benchmark That Flattered Its Own Engine

All six of Chest's job types are now implemented. Two findings dominate, and
neither is about search speed.

**A helpmate is not a mate search with a friendlier defender.** Every other goal
here is AND/OR: the attacker needs one move that works, the defender must have
none that escapes. A helpmate has no defender. Both sides want the same terminal,
so EVERY node is an OR node and the question is purely existential.

That removes three things by argument rather than by omission:

- Proof and disproof numbers measure what an ADVERSARY can force. With no
  adversary they are not merely unhelpful, they are undefined.
- The restriction portfolio is sound because removing attacker options cannot
  invent a forced mate. That argument needs an adversary. Remove a move from a
  HELPER and the solution may have run straight through it -- so a restricted
  lane is not an incomplete search for the same answer, it is a search for a
  different problem. The portfolio is therefore disabled outright for these
  goals rather than restricted more gently.
- The root split parallelises a disjunction whose children are conjunctions.

Exact length is the other trap. `h#3` means a mate ON move three, not by move
three, so the usual table bound -- proved within N implies proved within any
larger N -- is unsound here. Plies go into the key instead, reducing it to an
exact match.

**The benchmark was wrong before the engine was.** Two defects, both of which
reported success:

The YACPDB exporter hardcoded `w` as side to move. That is right for directmate,
stalemate, selfmate and selfstalemate, where the attacker moves first and is
White. It is wrong for helpmates, where BLACK moves first by the convention every
one of these diagrams is published under. The engine was being asked to help-mate
White, found unrelated cooperative sequences, and reported 80.4% against a
stipulation none of those solutions answered -- with every certificate verifying,
because each was a true statement about the wrong question. Corrected: 85.7%.

And `verify_proof.py` did not know the new tokens, so every `hm`/`hsm` line fell
through to "no solution reported" and was skipped. It printed **"0 certificates
verified"** beside a full run of solved positions and exited successfully. A
checker that passes by not looking is worse than no checker.

**The claim mismatch.** Chest's documentation says it *"always performs iterative
deepening"* at every recursive level, so *"the depth of the result, and of any
partial sub-result, is always minimal"*. Chest proves the SHORTEST solution.
Every head-to-head in this project had given MateProver `--direct-depth`, which
proves a solution WITHIN N and is explicitly not guaranteed minimal.

The two engines were answering different questions, and the easier one was ours.
Re-run at matched claims:

| | reported (`--direct-depth`) | matched (`--iterative-depth`) |
|---|---|---|
| stalemate | 54/60, 1.15x median, only-Chest 1 | 46/60, **1.07x** median, only-Chest 2 |
| selfmate | 49/60, 6.42x median, only-Chest 0 | 45/60, **1.09x** median, only-Chest 2 |

The reach advantage survives. The speed advantage very largely does not: about
five sixths of the headline selfmate margin was the weaker claim rather than a
faster search, and each goal gains a position only Chest solves.

Both figures are now in RESULTS.md, side by side. `--direct-depth` remains a
legitimate mode and the right one when any proof of a bound will do -- but a
reader comparing two solvers is owed the matched row, and choosing which to
publish is not a presentational decision.

**Helpmate is a loss, recorded as one.** Chest solves four h#4 positions this
engine does not. Pruning the final ply to candidate moves -- only a checking move
can mate, only a non-checking one can stalemate, gated as 54 requires -- cut
total time by a fifth and moved coverage not at all. The gap is structural: the
cooperative search is the only one in the program that is single-threaded and
unparallelised, while being the only one that is a pure disjunction and therefore
the easiest to parallelise. That is the next thing, not a better prune.

### 59. The Composition Features, And An Endgame Fix Refuted By Measurement

**Duals.** Every search in this engine until now answered the prover's question
-- is there a mate in N -- and stopped at the first proof, because one proof
settles it. The composition question is different: a second solution at the root
is a DUAL, and a directmate with one is cooked, unsound as a composition however
genuine the mate. `--all-solutions` enumerates every root move that solves.

Two design points, both load-bearing.

It defeats the short-circuit AT THE ROOT AND ONLY THERE. A dual in a sub-line
does not make the key ambiguous, so enumerating those would spend the whole
search's worth of pruning answering a question nobody asked.

And it forces an UNRESTRICTED search. The restriction portfolio is sound for
proving because removing attacker options cannot invent a mate. For counting it
is the opposite of sound: the moves a restriction removed are exactly the ones
that might have been second solutions, so a restricted enumeration undercounts
duals and reports a cooked problem as sound. Sound-for-proving and
sound-for-counting are different properties of the same mechanism, and the
portfolio has only the first.

Counts checked against python-chess: 1 key on a composed mate-in-2, 18 on K+Q
against a bare king, identical move sets.

**The tree is printed from the certificate**, not from a second walk of the
search. What is displayed is therefore exactly what was proved and independently
verified; there is no way for the display and the proof to disagree, which is a
property worth more than the few lines it costs.

**Algebraic notation was verified, not eyeballed.** 3551 moves across 150
positions from a random walk -- which reaches castling, promotion, en passant
and all three kinds of disambiguation that hand-picked positions miss -- against
python-chess, zero mismatches. The first version passed a three-move eyeball
while marking every quiet move as a capture, because an empty square is '.' and
the test was against 0.

**The endgame weakness: the obvious fix is wrong.** 54 diagnosed the losing
positions as tiny-material endgames where proof numbers carry no signal because
every branch looks alike, and a depth-first walk simply gets there. The obvious
consequence is to prefer depth-first when material is sparse. Measured on the
stalemate suite, bucketed by piece count:

| pieces | positions | dfpn (default) | depth-first standalone |
|---|---|---|---|
| <= 5 | 47 | **41/47** | 38/47 |
| 6-8 | 8 | **8/8** | 3/8 |
| > 8 | 5 | **5/5** | 3/5 |

DFPN wins every bucket, including the sparse one the hypothesis was about. A
material-aware route switch would LOSE positions. What is true is narrower than
what 54 suggested: depth-first reaches a few specific positions DFPN cannot, and
the portfolio already runs both and takes whichever finishes -- which is the
correct design and is already shipped.

**And the class is slow, not unreachable.** The one stalemate position Chest
solves and this engine did not, characterised properly:

| depth | budget | result |
|---|---|---|
| 11 | 2048 MB, 300 s | no stalemate at this depth (exhaustive, 27 s) |
| 12 | 2048 MB, 300 s | timeout |
| 13 | 256 MB, 60 s | timeout |
| 13 | 2048 MB, 60 s | timeout |
| **13** | **2048 MB, 300 s** | **SOLVED, 281 s** |

Time-bound, not memory-bound: eight times the table changed nothing at 60 s, and
five times the clock solved it. Against Chest's ~20 s that is roughly a factor of
fourteen on this class, which is a real gap and a smaller and more precise claim
than "cannot solve it".

The remaining fix is retrograde analysis -- backward induction over a whole
material class, which makes depth irrelevant because it never searches forward.
It fits the existing architecture as a PRECONDITIONER, on the same terms as DFPN:
it would guide the search while the exact prover still produced the verdict and
the certificate, so nothing about verifiability changes. It is scoped here and
deliberately not implemented: a tablebase generator that is wrong is worse than
none, and validating one is a project rather than an increment.
