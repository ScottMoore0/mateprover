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
  41 to 47, and the later rejections 68, 71, 77 and 78.
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
- [32a. Retracted: The Threads Were Never Engaged](#32a-retracted-the-threads-were-never-engaged)
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
- [60. Parallelising The Cooperative Search, And The Mate-In-8 Regression](#60-parallelising-the-cooperative-search-and-the-mate-in-8-regression)
- [61. A Restricted Lane Has No Business Deepening Iteratively](#61-a-restricted-lane-has-no-business-deepening-iteratively)
- [62. Two Corrections, One Of Them To A Result Bought Past The Deadline](#62-two-corrections-one-of-them-to-a-result-bought-past-the-deadline)
- [63. The Three Helpmate Misses, Characterised Before Being Chased](#63-the-three-helpmate-misses-characterised-before-being-chased)
- [64. Wiring The Shared Table, And Four Corpora That Contradicted Their Samples](#64-wiring-the-shared-table-and-four-corpora-that-contradicted-their-samples)
- [65. Retrograde Generation: A Round-Trip That Could Not See The Real Defect](#65-retrograde-generation-a-round-trip-that-could-not-see-the-real-defect)
- [66. GAP-11: The Bidirectional Search That Cannot Exist, And The Starvation It Was Hiding](#66-gap-11-the-bidirectional-search-that-cannot-exist-and-the-starvation-it-was-hiding)
- [67. Auditing The Corpora: Fifteen Wrong Entries, And A Denominator That Cannot Be Trusted To Itself](#67-auditing-the-corpora-fifteen-wrong-entries-and-a-denominator-that-cannot-be-trusted-to-itself)
- [68. The Preconditioner Gate: A Real Effect, Measured, And Rejected](#68-the-preconditioner-gate-a-real-effect-measured-and-rejected)
- [69. Six Corpora, Measured Together: Where Chest Still Wins, And Where It Is Wrong](#69-six-corpora-measured-together-where-chest-still-wins-and-where-it-is-wrong)
- [70. The Harness Handicapped The Engine With Its Own Tuning Knob](#70-the-harness-handicapped-the-engine-with-its-own-tuning-knob)
- [71. GAP-2 Implemented, Sound, And Worth Nothing Yet](#71-gap-2-implemented-sound-and-worth-nothing-yet)
- [72. Closing The Cheap Half: One Win, One Loss Found, And Five Budgets](#72-closing-the-cheap-half-one-win-one-loss-found-and-five-budgets)
- [73. The Cooperative Search: One Idea Rejected, One Kept](#73-the-cooperative-search-one-idea-rejected-one-kept)
- [74. Tightening The Reachability Bound Found It Was Unsound](#74-tightening-the-reachability-bound-found-it-was-unsound)
- [75. The Flight-Square Bound, And A Mate Delivered By Underpromotion](#75-the-flight-square-bound-and-a-mate-delivered-by-underpromotion)
- [76. Characterising The Cooperative Residue: There Is No Class](#76-characterising-the-cooperative-residue-there-is-no-class)
- [77. There Is No King+Pawn Theorem, And The Bound That Replaces It Does Not Pay](#77-there-is-no-kingpawn-theorem-and-the-bound-that-replaces-it-does-not-pay)
- [78. Level Skipping Is Correct, Fires Zero Times, And Cannot Be Evaluated Alone](#78-level-skipping-is-correct-fires-zero-times-and-cannot-be-evaluated-alone)
- [79. The Engine Faults Under AVX, And It Is Not The Engine's Fault](#79-the-engine-faults-under-avx-and-it-is-not-the-engines-fault)
- [80. Material Knowledge: The Useful Form Is Unsound And The Sound Form Fires Zero Times](#80-material-knowledge-the-useful-form-is-unsound-and-the-sound-form-fires-zero-times)
- [81. The Selfmate Gap Is A Disproof Gap, And The Solution Search Was Never The Problem](#81-the-selfmate-gap-is-a-disproof-gap-and-the-solution-search-was-never-the-problem)
- [82. Answer Ordering: A Real Win Through The Wrong Mechanism](#82-answer-ordering-a-real-win-through-the-wrong-mechanism)
- [83. Three Follow-Ups, Two Measured Out Before They Were Built](#83-three-follow-ups-two-measured-out-before-they-were-built)
- [84. The Cache Is Provably Sound, The Residue Is Two Classes, And Ordering Has No Headroom Left](#84-the-cache-is-provably-sound-the-residue-is-two-classes-and-ordering-has-no-headroom-left)
- [85. Internal Iterative Deepening, And The Closure Of The Graded-Failure-Depth Line](#85-internal-iterative-deepening-and-the-closure-of-the-graded-failure-depth-line)
- [86. The Attacker Rejection Test: The Mechanism Six Investigations Missed](#86-the-attacker-rejection-test-the-mechanism-six-investigations-missed)
- [87. The Residue After The Rejection Test: Nine Positions, No Instrument](#87-the-residue-after-the-rejection-test-nine-positions-no-instrument)
- [88. Counter Evidence On The Direct-Mate Residue, And A Skip That Fires Constantly For Nothing](#88-counter-evidence-on-the-direct-mate-residue-and-a-skip-that-fires-constantly-for-nothing)
- [89. The Coverage-Table Early Exit, Measured Both Ways Before Building](#89-the-coverage-table-early-exit-measured-both-ways-before-building)
- [90. The Harness Becomes The Least-Verified Component, And Stops Being One](#90-the-harness-becomes-the-least-verified-component-and-stops-being-one)
- [91. The King-Escape Module: One Analysis, Costed Against Two Residue Classes](#91-the-king-escape-module-one-analysis-costed-against-two-residue-classes)
- [92. The Coverage Exit, Built Sound: Three Over-Estimates Removed](#92-the-coverage-exit-built-sound-three-over-estimates-removed)
- [93. The Selfmate Node Exit, Rejected; And Where Selfmate Time Actually Goes](#93-the-selfmate-node-exit-rejected-and-where-selfmate-time-actually-goes)
- [94. The Fatal-Anti-Check Family, Measured Against The Ordering It Would Replace](#94-the-fatal-anti-check-family-measured-against-the-ordering-it-would-replace)
- [95. The First Auditable Table, And What A Re-Run Costs You](#95-the-first-auditable-table-and-what-a-re-run-costs-you)
- [96. x-check As A Variant, Not A Seventh Goal](#96-x-check-as-a-variant-not-a-seventh-goal)
- [97. The Second Variant Rule, And What The First One Cost To Generalise](#97-the-second-variant-rule-and-what-the-first-one-cost-to-generalise)
- [98. A Disproof Wants The Opposite Configuration To A Proof](#98-a-disproof-wants-the-opposite-configuration-to-a-proof)
- [99. Proof Numbers Measure The Wrong Game Under A Quota](#99-proof-numbers-measure-the-wrong-game-under-a-quota)
- [100. A Shared Table Makes The PV Stop Being A Function Of The Position](#100-a-shared-table-makes-the-pv-stop-being-a-function-of-the-position)
- [101. Finding-Only Mode Is A Frontier Tool, And Costs On Everything Else](#101-finding-only-mode-is-a-frontier-tool-and-costs-on-everything-else)
- [102. Quota Dominance Is Sound, And Cannot Pay](#102-quota-dominance-is-sound-and-cannot-pay)
- [103. The Quota Ladder: The Premise Was Right And The Arithmetic Was Not](#103-the-quota-ladder-the-premise-was-right-and-the-arithmetic-was-not)
- [104. d(3) >= 9, Confirmed Twice](#104-d3--9-confirmed-twice)
- [105. A Progress Stream That Publishes Theorems, Not Estimates](#105-a-progress-stream-that-publishes-theorems-not-estimates)
- [106. The One Line In The Stream That Behaves Like Stockfish's](#106-the-one-line-in-the-stream-that-behaves-like-stockfishs)
- [107. x-escape: The Rule That Is Measured Rather Than Counted](#107-x-escape-the-rule-that-is-measured-rather-than-counted)
- [108. What x-escape Actually Costs, And One Hypothesis That Was Wrong](#108-what-x-escape-actually-costs-and-one-hypothesis-that-was-wrong)
- [109. The Root Split Saturates Because One Move Owns The Tree](#109-the-root-split-saturates-because-one-move-owns-the-tree)
- [110. Memory Is Not A Lever Until The Depth Makes It One](#110-memory-is-not-a-lever-until-the-depth-makes-it-one)
- [111. Two-Ply Decomposition, And Why The Conjunction Argument Is Wrong](#111-two-ply-decomposition-and-why-the-conjunction-argument-is-wrong)
- [112. Young Brothers Wait, On The Node Type 111 Says To Split](#112-young-brothers-wait-on-the-node-type-111-says-to-split)
- [113. Supply Follows Demand, And The Deeper Decomposition Was Right After All](#113-supply-follows-demand-and-the-deeper-decomposition-was-right-after-all)
- [114. Four Leads, One Bug, And Three Rejections](#114-four-leads-one-bug-and-three-rejections)
- [115. The Idle Time Was Not Idle Time. It Was The Free.](#115-the-idle-time-was-not-idle-time-it-was-the-free)
- [116. Sharing Proofs Between Lanes, And The Last Of The Free](#116-sharing-proofs-between-lanes-and-the-last-of-the-free)

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

#### 32a. Retracted: The Threads Were Never Engaged

Every number above was measured on the DFPN route, and **the DFPN route had no
root split**. `run_root_split_depth` had exactly one caller, in the depth-first
route, and the DFPN route is the default. So `--threads 32` on a default run
allocated no workers and split nothing: the three identical rows in each half of
the table were not evidence that splitting fails to pay, they were the same
sequential search reported three times. A later twelve-hour capture-quota search
at `--threads 30` ran its whole life on one OS thread.

The reasoning here was therefore correct about a mechanism that was not running.
The paragraph blaming DFPN for leaving the workers nothing to do named the right
culprit for the wrong reason: DFPN was not starving the workers of work, it was
standing between them and the work.

Wiring the split into the DFPN route did not, on its own, change anything -- the
node count moved by 19 and the wall clock did not move at all. That looked like
confirmation of the original finding, and it was not. The preconditioner runs
*before* the exact pass and is single-threaded, so where it dominates,
parallelising the exact pass parallelises the cheap half. Under a capture quota
it dominates completely: 96% of the wall clock. Standing it down there (99)
exposes the rest.

| x-capture bench, geometric mean over 14 positions | wall clock |
|---|---|
| preconditioner on, 1 thread (as shipped before) | baseline |
| preconditioner off, 1 thread | **23.6x faster** |
| preconditioner off, `--root-split --threads 16` | **83.4x faster** |

Verdicts are identical across all fourteen positions in all three
configurations. The split contributes the 3.5x between the second row and the
third, so **root-split parallelism does not contribute nothing** -- it
contributes a factor of three and a half, once it is both wired up and not
hidden behind a sequential preconditioner.

What survives is narrower and still true: on plain directmates at the mate-in-16
frontier, where the preconditioner earns its keep, root splitting is worth
little, and lane-level and position-level parallelism (33) remain the
parallelism that pays there. What does not survive is the general claim. The
table above does not cover variant search and must not be read as if it does.

`--root-split` is nonetheless still **off by default**, for a reason unrelated to
speed: it changes the reported PV. See 100.

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

**The claim mismatch.** Chest proves the SHORTEST solution, establishing minimal
depth for interior sub-results as well as for the whole, which is observable in
its output and was confirmed here by measurement.
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

### 60. Parallelising The Cooperative Search, And The Mate-In-8 Regression

**The cooperative split.** A help node is a pure disjunction, which makes it the
simplest split in the program -- no defender layer, no conjunction to preserve --
and it was the last search here to get one. That is why helpmate was the single
goal Chest still won.

Determinism is kept exactly as the other splits keep it: workers claim root
indices from a shared counter and the LOWEST proving index wins, whoever finds
it. Verified rather than assumed -- identical PVs across 1, 2, 8 and 16 threads,
two runs each, on fifteen composed problems.

Measured against Chest, best of three, matched claims: helpmate goes from 49/60
with four positions solved only by Chest and a 0.69x median, to 34/40 level on
coverage with ONE only-Chest position and a 1.02x median. The gap was structural
and the structure was the fix.

**All solutions, not all keys.** Chest defines a helpmate as "find all sequences
of 2N legal moves", and helpmates conventionally have several intended solutions.
Enumerating root moves is not the same thing: two solutions often share a first
move. `8/6P1/6nP/8/4K1k1/5Nr1/8/8` has ONE key and TWO solutions, differing by
underpromotion -- g8=B and g8=N. Counting keys reports one and loses the point of
the problem.

So the cooperative enumerator collects whole sequences, and it cannot use the
table: a cached "solved" entry records one line, and reusing it would silently
drop every other solution through that position. Enumeration therefore pays full
price and is capped, and the output says `capped` when the cap bound the answer
rather than letting a truncated count read as complete.

**And the headline did not survive.** The directmate figures in RESULTS.md --
40/40 against Chest's 39/40 at mate-in-8, a 20x median -- were measured with
`--direct-depth`, and had never been re-run after the memory, thread and
cancellation fixes. Re-measured at matched claims, best of three:

| | Chest | mateprover | only Chest |
|---|---|---|---|
| mate-in-8 | **27/30** | 22/30 | **5** |
| mate-in-10 | 13/30 | **23/30** | 0 |

Mate-in-8 is a LOSS. Asked to prove the mate is no shorter -- the question Chest
answers at every recursive level -- this engine solves 22 of 30 and misses five
Chest finds. The earlier figure was not wrong about what it measured; it was the
easier question presented as though it were the same one.

Mate-in-10 reverses completely, which locates the effect: the portfolio's
advantage is real and arrives WITH DEPTH, while at shallow depth Chest's
iterative deepening at every recursive level beats it. That is a more useful
statement than the headline it replaces, and it is the fourth time in this
project that tightening a measurement has shrunk a margin. At mate-in-8 it went
negative.

### 61. A Restricted Lane Has No Business Deepening Iteratively

`--iterative-depth` collapsed where `--direct-depth` did not, and the size of it
was hard to miss: a selfmate position solved in 0.0 s at a fixed depth timed out
at 60 s when asked for the shortest solution. `--no-portfolio` was FASTER than
the portfolio on the same positions, which is the wrong way round and was the
clue.

Iterative deepening exists to establish MINIMALITY: depth d is searched only
after d-1 has been shown to hold no solution. A restricted lane cannot establish
that. Its failure at d-1 means "no solution among the moves I was allowed",
which is silent about the moves it was not -- and the portfolio already relies
on exactly this asymmetry when it decides that only the unrestricted lane can
tell a disproof from a timeout.

So every shallow pass a restricted lane made was work that could not contribute
to the claim being made, and with nine lanes that multiplied the wasted portion
by eight. Restricted lanes now search the requested depth directly. What such a
lane can contribute is a proof, and a proof at the requested depth is the only
thing it is ever asked for; its result is already reported with `via
<restriction>`, documented as "real but may not be the shortest", so no claim
this engine makes changes.

Measured at matched claims, 30 or 40 positions, Chest at 2048 MB:

| | before | after | Chest |
|---|---|---|---|
| mate-in-10 | 23/30 | **30/30** | 13/30 |
| selfmate | 22/40 | **32/40** | 24/40 |
| mate-in-8 | 22/30 | 24/30 | **27/30** |

Mate-in-10 goes to a clean sweep and selfmate gains ten positions. The three
selfmate holdouts that prompted this went from 0 of 3 to 2 of 3 solved.

**Mate-in-8 remains a loss, and the earlier diagnosis of it was wrong.** 60
attributed it to the claim mismatch -- proving minimality rather than a bound.
That is not what the positions say: `--direct-depth` fails three of the four
Chest-only positions too, and at aggregate scale direct reaches 26/30 against
Chest's 27/30. The claim mismatch costs about two positions; the rest is a plain
search-quality gap at shallow depth, where Chest's iterative deepening at every
recursive level is simply better than this engine's portfolio. Depth remains the
dividing line -- at mate-in-10 the ordering reverses completely -- but the
mate-in-8 deficit is real and is not an artifact of how it was asked.

### 62. Two Corrections, One Of Them To A Result Bought Past The Deadline

**Mate-in-8 was never a loss.** 60 and 61 reported it as one, from 30-position
samples giving 4 and 5 only-Chest positions. A 60-position draw then gave ZERO,
which is not a result so much as a warning that the sample was the instrument.
Run over the entire 200-position evaluation set at matched claims:

| | solved | only that engine |
|---|---|---|
| Chest 3.19 | 171/200 | 17 |
| mateprover | **178/200** | **10** |

Ahead on net, and neither engine dominating. The ten mateprover misses are a
real class -- across ten configurations nothing reached more than two of them --
but the deficit reported twice in this document did not exist. At this effect
size a 30-position sample cannot tell 4 from 0, and it was used anyway.

**And the helpmate result had been bought past the deadline.** Chasing the last
helpmate position turned up something worse than the position: the cooperative
route was systematically exceeding `--time-limit`, by a FACTOR OF TWO at small
budgets -- 9.4 s on a 5 s limit, 19.9 s on 10 s. Every other goal honoured its
budget exactly.

In a comparison where the other engine is hard-killed at the cap, that is not a
small unfairness. It means 60's helpmate figures -- level on coverage, only-Chest
1 -- were partly obtained by running longer than Chest was allowed to.

The cause was not cancellation, which is where 57 taught me to look. Each of
sixteen workers inherited the WHOLE table budget rather than a share, so a
cooperative search declared a ceiling of tens of gigabytes; the tables grow
lazily so nothing failed, it simply paged, and the deadline polls sat behind slow
memory. Workers now divide the budget, and a between-depth deadline check
guarantees no iteration starts on an expired clock.

That fixes the contract and costs coverage, which is the right way round:

| | before (over budget) | after (within budget) | Chest |
|---|---|---|---|
| helpmate | 34/40, only-Chest 1 | 32/40, only-Chest 3 | 35/40 |
| helpstalemate | 35/40, only-Chest 0 | 34/40, only-Chest 0 | 33/40 |
| runs exceeding the limit | frequent | **0 of 80** | n/a |

Helpmate is a loss again, by three positions, honestly measured. The split also
now uses four workers rather than sixteen: the split wants cores and the table
wants memory, they compete, and at sixteen the tables are too thin to hold the
positions that need them.

### 63. The Three Helpmate Misses, Characterised Before Being Chased

62 left helpmate three positions behind Chest and proposed a shared worker table
as the fix, on the strength of ONE of the three having been examined. The other
two had not been, so the proposal rested on a third of the evidence. Looking at
all three first, which cost minutes:

| position | pieces | Chest | reached by |
|---|---|---|---|
| `1qb4r/1p1kb1pr/Pp1n1p1P/1p3p2/8/8/1np2p1K/8` | 19 | 8.9 s | `-M 4096`, `-M 8192`, 60 s |
| `2q5/5p2/1p6/bk3p2/rn3P2/2ppp3/P2pp1K1/R7` | 17 | 14.8 s | **nothing** |
| `8/5p1B/2p1p3/2pp4/r3P2K/8/1pkq4/1nr5` | 14 | 9.3 s | **nothing** |

All three are h#4. Only the first is table-bound. The other two resist every
configuration tried -- sixty seconds against a twenty second cap, eight gigabytes,
single-threaded, direct depth, and a shared table -- so no amount of memory or
time in the range that matters reaches them.

That reprices the shared-table work honestly. It would recover one position and
leave helpmate at 33 of 40 against Chest's 35: still a loss, for hours of work on
a concurrent data structure. Worth doing on its merits, not worth doing as a way
to win helpmate.

Both unreachable positions are dense h#4 diagrams where Chest itself takes nine
to fifteen seconds, so this is the tail of Chest's range as well as past the end
of ours.

**A defect found while probing.** `--shared-tt` is accepted on the cooperative
route and does NOTHING: the help route's `ensure_workers` never sets
`shared_table`, so every worker uses a private table whatever the flag says. It
is not a wrong answer, but it is a flag that silently fails to do what it
promises, which is the same class of fault as 55 and 56 -- an option whose effect
cannot be seen in any output. Wiring it is the same change as the shared-table
work above and should be done with it rather than separately.

### 64. Wiring The Shared Table, And Four Corpora That Contradicted Their Samples

**`--shared-tt` now does something on the cooperative route.** It was accepted
and ignored (63). Wiring it is the whole change, since `shared_tt` already
defaults to true and `--private-tt` is the opt-out -- so the default now shares,
and the four-worker cap comes off, because the cap existed only to stop private
tables dividing the budget into uselessly thin slices.

It reached all three helpmate positions recorded at 63 as reachable by nothing.
That record was wrong in a specific and instructive way: the probe that produced
it DID try `--shared-tt`, and the flag was a no-op at the time. A dead option
does not merely fail to help; it silently contaminates every conclusion drawn
from a sweep that includes it.

**Then four corpora were run whole instead of sampled, and every one disagreed
with its sample.**

| goal | corpus | Chest | mateprover | only Chest | sample had said |
|---|---|---|---|---|---|
| mate-in-8 | 200 | 171 | **178** | 10 | a 4-position LOSS |
| selfmate | 904 | 475 | **646** | 36 | 3 behind |
| helpmate | 546 | **511** | 510 | 7 | a 3-position loss |
| helpstalemate | 431 | 331 | **379** | 1 | 4 ahead |

Every direction of error appears here. Mate-in-8's sample invented a loss.
Selfmate's understated the misses by a factor of twelve while also understating
the wins. Helpmate's overstated a loss that is really a one-position dead heat.

The samples were 30 to 40 positions and the effects are 1-4%. That is simply the
wrong instrument, and it was used repeatedly in this project before the
arithmetic was taken seriously. **Any comparative figure here drawn from a sample
should be treated as provisional until the corpus has been run.**

**What the residue looks like.** Selfmate's 36 misses cluster in sparse endgames
at s#8-s#10 -- `7k/6R1/5Q2/...`, `q7/8/8/7Q/...` -- which is the same
tiny-material class as the stalemate weakness of 59, arriving from a third
direction. Helpmate's 7 are all h#4. And the ten mate-in-8 misses survived a
fourteen-configuration sweep (restrictions C, K, X, R, the shared table, 32
threads, the shallow-fast route) with a UNION of one: nine of them are reached by
nothing this engine can currently be told to do.

### 65. Retrograde Generation: A Round-Trip That Could Not See The Real Defect

`retro.h` answers one question: what are the predecessors of this position?
Every position P and legal move m with `make_move(P, m) == b`. It is the
groundwork for a bidirectional cooperative search (the backward frontier has
nothing to stand on without it), and it is measured before anything is built on
it.

**The design is generate-a-superset-then-verify.** Reverse move generation is
the fiddly half of retrograde analysis -- uncaptures, unpromotions, un-castling,
un-en-passant, restored castling rights, and the fact that a sliding piece's
origin depends on an occupancy that does not exist yet. Getting all of that
right by construction is where retrograde code goes wrong, and it goes wrong
silently. So candidates are proposed cheaply and confirmed afterwards.

**The first version confirmed the wrong thing.** It played the candidate move
forward with `make_move` and compared the result against `b`. That reads like
verification and is not: `make_move` does not check that the move is legal, or
even that the piece moves the way its type moves. It rewrites two squares. So
for a knight on `to` and ANY empty square `from`, the replay reproduces `b`
exactly, and every empty square on the board became a "predecessor". The
generator was emitting positions a knight had reached by sliding down a file.

The round-trip harness measured 96.19% and said nothing about this, because a
round-trip is a completeness test:

> for each position P and each legal move m,
> the predecessors of `make_move(P, m)` must include P.

Every spurious predecessor is invisible to that question. Completeness and
soundness are separate measurements and the harness that finds one is
structurally blind to the other -- the same shape as the dead `--shared-tt`
flag at 64 and the verifier that skipped `hm`/`hsm` at 57: a gate reporting a
number for a property it was not testing.

Verification now requires the retracted move to appear in the candidate's own
**legal** move list before it is replayed. That is the whole soundness
argument, and it is one line.

**What the round-trip was right about.** 96.19% was a real gap, and it broke
down as 67 castling-rights misses and 35 undiagnosed. Both are now closed:

| Miss class | Why it was missed | Fix |
| --- | --- | --- |
| castling rights forfeited | the predecessor needs rights `b` no longer carries | enumerate plausible restorations of exactly the bits this move would clear |
| castling itself | two pieces move; the single-piece loop cannot express it | a separate four-entry retraction table |
| en-passant capture | the restored pawn does not go back on `to` | a separate retraction, with `pred.ep` pinned to the capture square |
| double push, ep square absent | see below | compare positions on the LIVE ep square |
| unused ep square in the predecessor | only `ep = -1` was ever proposed | emit each live ep square the waiting side could have left |

The fourth of those is worth stating plainly, because it is a disagreement about
what a position IS. `make_move` records an ep square after every double push,
whether or not a capture exists; the FEN convention in wide use -- and the one
the corpora are written in -- records it only when the capture is legal.
Comparing the raw field makes the two disagree, and the disagreement lands
exactly on the predecessor "the opponent just pushed two squares", which is most
of the pawn endgame. An ep square no pawn can use is not observable: no
continuation tells the two positions apart. So positions are compared, and
predecessors emitted, on the live ep square only.

**Material bounds, because they are free.** An uncapture hands a man back and an
unpromotion hands a pawn back; neither may take a side past sixteen men or eight
pawns. That is the one thing about a predecessor that can be settled without
knowing how the game got here, and it removed 90% of the unreachable output. It
is applied only where a unit is ADDED, so a caller-supplied FEN that is already
over strength is retracted rather than rejected -- this is a move generator, not
a validator. The same rule governs restored castling rights (checked for
plausibility only when restored, never against rights the input already carries)
and the back-rank pawn guard, which was present for the pawn an uncapture
restores and absent for the pawn the retraction itself moves.

**Where it stands.** On a mixed corpus of a random walk and 40 composed
helpmates -- 2,676 (parent, move) pairs, 16,146 emitted predecessors:

| Property | Before | After |
| --- | --- | --- |
| complete: every predecessor generated | 96.19% | **100%** |
| sound: every emitted predecessor really is one | not measured | **100%** |
| of which reachable from the initial array | -- | 99.3% |

The contract is exactly one ply, in both directions. It does not decide whether
a predecessor is reachable from the initial array: a position can respect the
material bounds and still be retro-impossible -- three pieces giving check at
once, or two checkers on one line through the king, cannot be produced by any
single move, and nothing here rejects them. That is 0.7% of output.

Chasing it would mean a second attack generator duplicating `board.h` for a
0.7% return, and it costs a backward search time rather than correctness: a
bidirectional search meets when a backward node EQUALS a forward node, forward
nodes are reachable by construction, and an unreachable backward node therefore
never matches. Documented, bounded by a test, and left. A caller that needs
retro-legality in its own right must add it.

### 66. GAP-11: The Bidirectional Search That Cannot Exist, And The Starvation It Was Hiding

GAP-11 was specified as meet-in-the-middle for the cooperative goals: enumerate
forward half the plies from the root, enumerate backward half the plies from the
set of mating positions, intersect. Roughly b⁸ becomes 2·b⁴ — a change of
complexity class rather than of constant, and the only such item in either
document.

It does not survive contact with a measurement. What follows is the measurement,
because "we tried it and it was slow" is not a finding.

**A backward frontier needs an explicit goal set, and checkmate is a predicate.**
That is the whole difficulty, and it is not a detail of chess. Bidirectional
search requires states to expand backward FROM. A helpmate's goal is not a state,
it is a property, so the goal set has to be enumerated before the backward half
can begin. Enumerating it means walking the placements of that material:

| Material | Placements to walk | Mate positions among them |
| --- | --- | --- |
| K+Q v K+R (4 men) | 15,249,024 | ~10,100 |
| K+N+N v K+R+R (6 men) | 13,495,386,240 | ~270,000 |
| 8 men (the h#4 corpus median) | ~10¹³ | — |
| 25 men (the largest h#4 in the corpus) | ~10³⁰ | — |

Mate counts are sampled (300,000 uniform placements each, filtered to legal
positions with Black to move); placement counts are exact.

Now put that beside the forward search it would replace. The six-man h#4
`8/7r/5Nk1/6N1/4r3/8/2K5/8` costs **23,288,236 distinct (position, plies) states**
— the profile records exactly that many stores against 83,214,104 table hits and
zero evictions, so the search already visits each state once and is at frontier
cost, with no slack for a better forward algorithm to recover.

So at six men, merely FINDING the seeds of the backward frontier costs 1.3×10¹⁰
placement tests against a forward search of 2.3×10⁷ states: the enumeration alone
is some 600× the work it was meant to save. At four men the walk is cheap, but
there the forward search is already instant. Above about seven men the walk is
not merely expensive, it is beyond any machine. **The crossover is on the wrong
side everywhere**: where the goal set is cheap to enumerate the forward search
does not need help, and where the forward search needs help the goal set cannot
be enumerated.

The obvious repair fails too. Constrain the goal set to what the root can
actually reach — in h#N each side makes exactly N moves, so at most N units per
side have left their diagram squares. That constraint is real, but the set it
defines is precisely the forward frontier at ply 2N, so computing it costs the
forward search it was supposed to replace.

**The signature measurement gave a false green light, and it is worth saying why.**
The suggested cheap probe was to count distinct material signatures at the
meeting ply and proceed if the set was small. It was small — median 14. But the
binding constraint is not how MANY signatures need backward frontiers, it is what
ONE of them costs, and that probe cannot see it. A discriminator has to be able
to return the answer you do not want.

GAP-11 is therefore **rejected**, and it collapses into GAP-4b: a backward
frontier from an enumerable goal set with fixed material is a retrograde
tablebase, which is already demoted on its own evidence. The retrograde generator
of 65 keeps its value — it was always independently useful and independently
tested — but it has no bidirectional search to carry.

#### What was actually wrong

Rejecting the mechanism does not release the target, which was the cooperative
misses and "a large reduction in time on cooperative positions generally". So the
same profile was read for what it did say.

The cooperative split scaled like this, and the middle column is the tell:

| Threads | h#4 solve | Exhaustive h#3 sweep |
| --- | --- | --- |
| 1 | 32.7 s | 1.56 s |
| 4 | 13.9 s | 0.53 s |
| 8 | 12.8 s | 0.34 s |
| 16 | **20.1 s** | 0.27 s |

The right-hand column is the same engine on the same position with the early exit
removed, so every thread must work to the end: it scales 5.9× at sixteen threads.
The threads were not contending. On the real solve they were **starved** — and at
sixteen the search was slower than at four.

The cause is granularity. The split ran one ply deep, so there were as many tasks
as root moves, around thirty, and cooperative subtrees are wildly uneven. One task
routinely holds most of the work; everything else finishes early and idles. Adding
threads past that point cannot help, and hurts, because more of them start
speculative work that a lower-indexed task then invalidates.

**The split now runs two plies deep**: one task per (root move, reply) pair,
hundreds rather than tens, and the imbalance averages out.

| Threads | Before | After |
| --- | --- | --- |
| 4 | 13.9 s | 13.2 s |
| 16 | 20.1 s | **6.2 s** |
| 32 | — | 5.6 s |

At the default sixteen threads that is 3.2× on the position, and 6.0× against one
thread — which is the 5.9× the exhaustive column said was available all along.

It also moves the answer TOWARD the sequential one. Lexicographic (first, second)
order is the order a sequential depth-first search visits these subtrees in, so
lowest-index-wins now selects the subtree sequential search would reach first.
Which line comes back from within a subtree still depends on what the shared table
already holds, and that was never promised.

#### What it bought, stated plainly

On the full 546-position helpmate corpus at an unchanged 10 s cap: **500 → 501
solved**, with total wall time 744 s → 615 s. Since the timeouts account for a
fixed 450 s, the time spent on positions that were actually solved fell from about
284 s to about 165 s — 1.7× on aggregate solve time, and 3.2× where it was
measured directly.

One converted position is a thin return for a 3.2× speedup, and the reason is in
the miss list rather than in the fix: the remaining misses are dominated by 14-to-25
man positions that are one or two orders of magnitude beyond a 10 s cap, not a
factor of three. A speedup converts what sits near the boundary, and little sits
near this one.

**And the corpus is not a clean denominator.** `r3k3/8/8/2K5/1P6/8/8/8 b - -` is
recorded as h#4 and appears in the miss list, but the engine REFUSES it in 0.037 s
rather than timing out. An exhaustive python-chess search, independent of this
engine, agrees: there is no h#4 there, and the shortest cooperative mate is h#5.
The corpus entry is wrong. A definitive refusal against a corpus that expects a
solution is either a corpus error or a soundness bug, and the two look identical
in a solve-rate table — so a refusal must never be counted as a timeout, which is
what a bare percentage does.

### 67. Auditing The Corpora: Fifteen Wrong Entries, And A Denominator That Cannot Be Trusted To Itself

Every reach figure in this project is a fraction, and the work has all gone into
the numerator. This is the denominator.

**Structural sweep, all 3,584 rows across fourteen files.** No unparseable FEN
anywhere. One position in `stalemate_pdb` is illegal (the side not to move is in
check) and is already carried as `"status": "illegal"`, so it is tagged rather
than silent. Four of 431 helpstalemate rows have more than one king a side --
fairy problems that orthodox chess cannot answer, and therefore four guaranteed
misses in any rate quoted against 431. `selfmate_deep` has 904 rows and **903
distinct positions**: one is duplicated, and a duplicate is counted twice by
every rate computed from it.

**Train and evaluation sets are clean.** Zero shared positions across every
train/eval and dev/corpus pair checked. That is the claim the reach figures rest
on and it holds.

**The stalemate corpora overlap each other heavily.** `stalemate_reach` is 60
positions and **all 60 are also in `stalemate_pdb`**; 46 of `stalemate_yacpdb`'s
260 are too. The four files hold 1,019 rows and 913 distinct positions, so a
"run every stalemate corpus" figure double-counts 106. They are also outside
`benchmarks/MANIFEST.json`, which covers only the matetrack sets -- the provenance
discipline of 55 was never extended to the goals added later.

#### The status labels cannot filter a comparison against Chest

Each imported row carries `solved`, `unsolved`, `shorter`, `refuted` or
`illegal`. It is tempting to quote rates against `solved` only. That is
circular: `import_problems.py` assigns those labels **from this engine's own
verdicts**. Excluding what MateProver refuted, from a table comparing MateProver
against Chest, would let the engine mark its own paper.

What the labels are good for is the opposite. `refuted` is a falsifiable claim --
"searched to completion, no solution exists at the stated depth" -- and a claim
like that must be adjudicated by something that shares no code with the engine
making it.

**All 15 `refuted` entries were put to Chest 3.19.** Chest agrees with every one:

| goal | count | Chest's verdict |
| --- | --- | --- |
| stalemate, s=2..5 | 13 | no solution, 13 of 13 |
| selfmate, s#3 and s#6 | 2 | no solution, 2 of 2 |

Two things follow, and they are separate. The first is that **15 published
problems in these corpora are stipulated wrong**, and no engine will ever solve
them. The second is that MateProver refused all 15 by exhausting the search
rather than by running out of clock, and an independent prover confirms each
refusal -- which is a soundness result, obtained for the price of a script.

A sixteenth was found the same way at 66: the helpmate corpus records
`r3k3/8/8/2K5/1P6/8/8/8 b - -` as h#4 when the shortest is h#5, adjudicated
there by exhaustive python-chess rather than by Chest.

#### Two things this changes about how rates are computed

**A refusal is not a timeout.** They are different events and a percentage
renders them identically. Any position an engine refuses definitively, against a
corpus that expects a solution, is either a corpus error or a soundness bug, and
those must never be allowed to look alike in a table. Sixteen of them here were
corpus errors; the point is that nobody knew until they were adjudicated.

**Scoring must be presence, not equality.** 51 rows are `shorter` -- the true
solution is shallower than the stipulation. A harness that requires the reported
depth to equal the stipulated depth scores BOTH engines as failing on all 51,
because both correctly prove the shorter one. This was live in the first version
of the gate sweep below and was caught before it ran to completion.

A third defect was caught in the same pass and is worth recording because it
would have produced a confident zero: selfmate results carry the token `sfm`,
and the first sweep script matched `sm`, which is the STALEMATE token. `\bsm`
cannot match inside `sfm`, so every configuration would have scored 0/525 and
the honest-looking conclusion would have been "the gate changes nothing".

**Corpus labels also go stale.** The first row of `selfmate_deep` is marked
`unsolved`; it now solves in 2.7 s. The labels record what was true at import
against the engine of that day, and they are not re-derived. They are a
provenance record, not a live index.

### 68. The Preconditioner Gate: A Real Effect, Measured, And Rejected

The clean-room analysis carried one observation offered explicitly as a single
data point rather than a finding: an s#8 position that fails at the 20 s baseline
solves in 14 s with the DFPN preconditioner disabled. It was consistent with 54 --
proof numbers derived from move counts carry no signal in sparse positions,
because every branch looks alike -- and if the preconditioner were not merely
unhelpful but actively costly on that class, some share of the 36 selfmate misses
would be recoverable by a configuration change measured in hours rather than by
static theorems measured in weeks.

`--dfpn-min-men N` is that lever: skip the preconditioner when the position has
fewer than N men. This is its measurement.

**The experiment is confined by construction.** The gate is checked at the ROOT
of each depth iteration, so any position with N men or more is bit-identical to
the ungated engine. The sparse tail IS the experiment. Of the 904 selfmates, 377
have nine men or fewer, and the 36 misses sit at six and seven.

377 positions, 20 s a position -- the same baseline the original observation used
-- ungated against a gate of 9:

| | solved | gained | lost |
| --- | --- | --- | --- |
| `--dfpn-min-men 0` (default) | **232** / 377 | — | — |
| `--dfpn-min-men 9` | 185 / 377 | 12 | 59 |

**The effect is real and the lever is still wrong.** Twelve positions are
recovered by switching the preconditioner off, so the phenomenon the single data
point pointed at exists and is not noise. Fifty-nine are lost. Net **-47**, and
the default stays at 0.

What the number says is that material count cannot tell the two populations
apart. A men-based gate is a blunt instrument applied to a distinction that is
not about men: it switches off the preconditioner for the twelve positions that
want it off, and for 220 others that want it on. Anything that recovers those
twelve has to discriminate on something else -- and the discriminator has to be
cheaper than the search it is trying to save, which is the same bar 59 set for
route selection by material sparsity and failed.

**This is the sixth time a sample and a corpus have disagreed here**, and the
first where the sample was directionally right and quantitatively fatal. A
30-position draw invented a mate-in-8 loss; a 40-position draw overstated a
helpmate loss that was a dead heat; selfmate's sample understated its misses
twelvefold. Here one position was correctly diagnosed and the population
behaved the opposite way. The lesson is not "distrust single observations" --
this one was true, and it was labelled a single data point by the person who
made it. It is that the step from "this position improves" to "make it the
default" is a measurement, never an inference.

The flag stays, documented and off. It is the right tool for a position already
known to want it, and the wrong tool for a corpus.

**What this leaves for selfmate.** The 36 misses are not recoverable by
configuration, which is what this was testing. GAP-1's verdict lattice and the
K+P theorem -- the larger of the two two-unit defender classes at 11 of 36 --
remain the work, and they are weeks rather than hours. That is a worse answer
than the one hoped for, and it is now measured rather than assumed.

### 69. Six Corpora, Measured Together: Where Chest Still Wins, And Where It Is Wrong

Five loose ends closed in one pass, because coverage and speed come from the same
runs and two passes would let the two tables drift apart. Everything below is a
single trial at 10 s a position, mateprover on its shipped 256 MB against Chest on
2048 MB, both proving the shortest solution, scored on presence.

| goal | corpus | Chest | mateprover | only Chest | median speedup |
| --- | --- | --- | --- | --- | --- |
| mate-in-8 | all 200 | 146 | **168** | 9 | 10.9x |
| mate-in-10 | all 60 | 20 | **53** | 2 | 94.5x |
| stalemate | all 792 | 725 | **759** | 1 | 22.3x |
| helpmate | all 546 | **491** | 480 | **16** | 3.3x |
| helpstalemate | all 431 | 308 | **344** | **0** | 12.1x |

**Helpmate is a real loss and the two-ply split did not close it.** I expected it
to. 66 made hard cooperative positions 3.2x faster and helpmate was one position
behind, so the arithmetic looked settled before it was run. It was not: 480 to
491, with sixteen positions Chest reaches and this engine does not. The median
speedup on shared positions is 3.3x, the lowest of any goal, and on total time the
two are near parity at 1.13x. Chest is simply good at helpmates, and a faster
engine that is still behind on coverage is behind on coverage.

The prediction was wrong in the same way the samples of 62 were wrong -- reasoning
forward from a ratio instead of running the corpus. The fix made the row faster
without making it a win.

**Helpstalemate is now a clean sweep**: 344 to 308 with **nothing** that only Chest
solves, from a row that was one position behind.

**Stalemate moves off a development set.** The row was 40 positions from
`stalemate_dev40` -- a set used during development, which is the one kind of set a
headline number should never rest on. It is now the externally-sourced union of
`stalemate_pdb` and `stalemate_yacpdb`: 792 distinct after removing 47 duplicates
and the position already tagged illegal. 759 to 725, one position to Chest.

#### Chest returns fourteen wrong negatives

On stalemate, Chest reported "No solution" for 27 positions. Thirteen are corpus
errors already in `KNOWN_BAD.jsonl`. **The other fourteen are positions mateprover
solves** -- two provers flatly contradicting each other, which cannot be left
alone.

It is not a protocol artifact. Both engines agree the depth is 8; Chest still
refuses at 8. It is not a flag: `-r`, no `-r`, and `-S` all refuse, while a control
stalemate solves under the same flags. The positions are sparse queen endings such
as `2Q5/8/1k6/5b2/8/8/8/7K w - -`.

The disagreement resolves constructively. mateprover emits a certificate for that
position and `tools/verify_proof.py` -- a separate program, sharing no code with
the engine -- confirms it: **stalemate in 8**, every defender reply enumerated, the
terminal a real stalemate. A verified constructive proof beats an unexplained
negative, and this is the case certificates exist for. It is worth noting what
this costs Chest: those fourteen are counted as Chest failures above, and without
the certificate there would be no principled way to decide which engine to
believe.

#### The mate-in-8 misses are a budget, not a wall

The record said the ten mate-in-8 misses "survived a fourteen-configuration sweep
with a union of one" and were reached by nothing. That sweep varied
CONFIGURATION at a fixed budget. Nobody had varied the budget.

Nine positions Chest solves at 10 s and mateprover does not:

| budget | solved |
| --- | --- |
| 60 s | 7 of 9 |
| 300 s | 8 of 9 |
| 900 s | 8 of 9 |

**Eight of nine are constant-factor losses, not reach gaps.** GAP-5 -- a search
ordering deficiency rather than an algorithmic wall -- is the right shape for this
class after all, and the "reached by nothing" framing was an artifact of holding
the clock fixed while varying everything else.

One position resists ninety times its original budget:
`N1R2N2/1p6/6B1/2P5/8/P7/2P1p1K1/k7 w - -`. That one is a reach gap, and it is
now a population of one rather than of ten.

This does NOT say mateprover beats Chest at 60 s; Chest was not given 60 s. It
says the positions are reachable, which is the question GAP-5 turns on.

#### Twenty-two more corpus entries under suspicion

Chest refused 14 helpmate and 9 helpstalemate positions definitively. On both
corpora the contradiction count is **zero** -- there is no position Chest refuses
and mateprover solves. Twenty-two are new and are recorded in `KNOWN_BAD.jsonl`,
explicitly at a weaker evidence level than the original fifteen: there, mateprover
refuted and Chest agreed; here only Chest refuted, because mateprover ran out of
clock rather than exhausting the search. One prover's refusal is a lead, not a
finding, and the file says so.

#### Selfstalemate cannot be measured at all

The `selfstalemate` row claims "all 35". **There is no selfstalemate corpus in the
repository.** No file carries the goal, nothing in `tools/` mints one, and
`benchmarks/MANIFEST.json` covers only the matetrack sets. The row rests on a set
that cannot be rebuilt, which by this project's own standard at 55 makes it not
evidence. It is the last unreproducible number in `RESULTS.md` and it should be
either regenerated and committed or withdrawn.

### 70. The Harness Handicapped The Engine With Its Own Tuning Knob

The helpmate row of 69 was wrong, and the fault was mine rather than the engine's.

`tools/paired_corpus.py` passed `-M 256` to mateprover, on the belief that 256 MB
is the shipped default. `--print-config` does report `memory_mb 256`, which is
what made the belief comfortable. But **an explicit `-M` and an unset `-M` do not
mean the same thing**, and this project documented the difference itself: an
explicit `-M` is the budget for every table alive at once, split across portfolio
lanes; left unset it is 256 MB *per table*. A cooperative search runs nine
memory lanes, so `-M 256` hands it a ninth of what it ships with.

Measured on one of the positions from the miss list,
`8/8/8/8/3b4/k7/5r2/1r1b2NK b - -`:

| invocation | time |
| --- | --- |
| default (no `-M`) | 2.9 s |
| `-M 256` | 37.4 s |

**12.8x, from a flag intended to describe the default.** The whole helpmate row
was run that way.

| helpmate, 546 positions, 10 s | mateprover | chest | only chest |
| --- | --- | --- | --- |
| with `-M 256` (69) | 480 | **491** | 16 |
| engine defaults | **501** | 491 | **2** |

The row flips from the only loss in the table to a win, and the sixteen positions
"only Chest reaches" become two.

This is the `-M` defect of the CLI work, reintroduced by me in the harness that
measures it. The original was the engine charging `-M` per table so a stated 256
cost 1994 MB at four workers; the fix made `-M` a total. The failure mode
inverted -- now a user who states the number they believe is the default silently
starves the engine -- and a benchmark harness is exactly the place where that
inversion does the most damage, because it produces a confident table.

**The other rows in 69 were measured the same way and are therefore conservative.**
The handicap only ever disadvantaged mateprover, so every row it won it still
wins; the numbers understate the margin and the "only Chest" columns are upper
bounds. Helpmate was the only row whose CONCLUSION the handicap could reverse,
and it did.

Two smaller measurement asymmetries were checked at the same time and are worth
recording because both turned out not to matter much:

- **Chest's per-position time includes process startup and table allocation**,
  where mateprover's is its internal `acs`. Measured floor: **13 ms**, flat from
  64 MB to 2048 MB. Real, small, and it inflates ratios only on positions Chest
  answers in tens of milliseconds.
- **Thread count.** mateprover ran at its default 16 threads against a
  single-threaded Chest. Re-measuring single-threaded: mate-in-8 **169** against
  168, stalemate **759** against 759 -- the directmate and stalemate advantage is
  not core count at all. Helpmate is the exception: 451 single-threaded against
  480, so there the cores matter and the honest single-thread comparison is a
  loss.

#### Item by item

**Helpmate's sixteen are not a reach gap.** Every one solves within 60 s -- most
in 2 to 4 s -- and all are h#4, spanning 6 to 17 men, so there is no structural
class among them. With the memory handicap removed, fourteen of the sixteen come
back inside the original 10 s.

**Selfstalemate has a corpus again.** 35 problems exported from YACPDB, imported,
committed as `benchmarks/selfstalemate_yacpdb.jsonl`. mateprover proves 23 and
refutes 12; all 23 certificates verify. Paired against Chest: **23 to 23, nothing
either engine misses**, and Chest refuses the same 12 independently. Two provers
agreeing that twelve stipulations have no solution is the strongest evidence class
in `KNOWN_BAD.jsonl`. The historical "23/35" was right, and the twelve it never
explained were bad stipulations rather than hard positions.

Getting there needed two fixes. `tools/import_problems.py` knew only `#`, `=`,
`s#` and `h#`, so `s=` and `h=` -- selfstalemate and helpstalemate -- could not be
imported at all. And the first patch reported **35 of 35 refuted**: a heredoc had
written `\b` as a literal backspace byte, so three regexes required an invisible
control character and never matched. A confident zero, from a pattern that prints
indistinguishably from the correct one.

**The mate-in-8 reach position is real.** `N1R2N2/1p6/6B1/2P5/8/P7/2P1p1K1/k7 w - -`
resists all seven configurations tried at 120 s each: default, no preconditioner,
depth-first route, shallow-fast route, single thread, 2048 MB, no portfolio.
Chest solves it in under 10 s. A population of one, and the only position in the
project with that status.

### 71. GAP-2 Implemented, Sound, And Worth Nothing Yet

The perpetual-check refutation for a lone-queen defender, built on GAP-1's
lattice. `docs/GAP2_DERIVATION.md` has the theorem and the three corrections the
specification's sketch needed; this is what it measured.

**Sound.** The bar the specification sets is that the predicate must never fire
on a selfmate that has a solution — a single false positive is a critical bug,
not a regression. Run paired over all 319 solvable positions in the 904-corpus,
at 10 s each, with the gate on and off:

| | solved |
| --- | --- |
| gate off | 317 / 319 |
| gate on | 317 / 319 |
| **lost by enabling it** | **0** |
| gained | 0 |

Identical sets, not merely identical counts. The paired form is what matters: two
positions fail either way, and without the pairing they would have looked like
false positives. Both are budget effects — one solves in 10.1 s when run alone,
the other still times out at 60 s with the gate on rather than being refuted.

**And it converts nothing.** On the 22 unsolved roots with a king+queen defender,
which is precisely the class it was built for:

| | solved | refuted | timed out |
| --- | --- | --- | --- |
| gate off | 10 | 0 | 12 |
| gate on | 10 | **0** | 12 |

Zero on the 14 selfmates still open after independent adjudication, too. What it
does buy is a subtree prune where it fires — 18,329 nodes down to 15,063 on a
king+queen position, about 18% — and the predicate fires 69,452 times across the
solvable corpus, so it is doing real work deep in the tree. None of that reaches
a root.

**Why, and it was predicted.** Refuting a root needs EVERY attacker move to lead
to a refuted position, which is enormously stronger than the perpetual holding
somewhere in the tree. The derivation said as much before the code existed: 42
roots have this material against 127 for king+pawn, and of the 14 open positions
one is king+queen and three are king+pawn. GAP-2 was never the explanation of the
residue and the measurement agrees.

The specification called this "the highest value-per-line item in the document".
On this corpus it is not. It is sound, it is cheap, it prunes, and it converts
nothing — which is a result worth having, because the alternative was believing
it had.

**The composition gap.** A refutation at a defender node currently prunes its
subtree and stops there: the selfmate node routines do not yet propagate
`Refuted` upward the way the directmate ones do. That is the missing wiring
between GAP-2 and GAP-1, and it is why the "refuted" column above is zero rather
than small. Even wired, a root refutation needs the universal condition above,
so the honest expectation is small rather than transformative.

**What the residue actually needs.** King+pawn, 127 roots and the largest class
by a distance. The perpetual argument does not transfer at all — a lone pawn
cannot check repeatedly, so there is no perpetual to find, and the "decline
forever" mechanism is unavailable. A lone pawn can also promote and mate, so
there is no trivial impossibility either. That theorem is genuinely different,
genuinely open, and is not attempted here.

### 72. Closing The Cheap Half: One Win, One Loss Found, And Five Budgets

Five items that were all measurement or small code, run together.

**The verdict reached the wire.** GAP-1 computed "no solution at any depth" and no
caller could see it. Three negatives now read differently: no marker means none
within the depth searched, `; refuted` means none at any depth, `; timeout` means
no claim at all. A field added, so it sits inside the format's stability promise,
and the soundness rule travels with it — only an unrestricted search may assert
it, because a restricted lane's failure is silent about the moves it was not
allowed.

Wiring it found a defect that would have shipped quietly. The parallel portfolio
writes `results[i]` only for ACCEPTABLE results, and a refutation is never one, so
reading the verdict back out of `results` gave false every time. The token worked
perfectly under `--no-portfolio` and never appeared under the default — a bug that
passes a hand test and fails in production.

**Selfstalemate is a win, and the tie was a corpus artefact.** The row was 23-23
with nothing either engine missed, because 12 of 35 problems were bad stipulations
both provers refuse: there was nothing left to out-solve. Extending the corpus to
76 problems (52 sound, 13 refuted, 11 unsolved) gives **52 to 49**, one position to
Chest and four to mateprover. The tie was never a statement about the engines.

**Five residues, five budgets.** The uncharacterised only-Chest positions —
mate-in-10's two, stalemate's one, helpmate's two — all solve at a larger budget:
four inside 60 s and the fifth at 147 s. None is a reach gap.

**Mate-in-8 with the clock levelled.** The nine only-Chest positions, both engines
at 60 s rather than 10:

| | solved |
| --- | --- |
| mateprover | 7 / 9 |
| Chest | 9 / 9 |

Seven of the nine were the 10 s cap. Two survive: `N1R2N2/1p6/6B1/2P5/8/P7/2P1p1K1/k7`,
which already resisted 900 s and all seven configurations, and
`r3k2r/p2p4/p1pP2p1/5pN1/5p2/1Q3p2/PP4b1/KB6`, which solves at 300 s. So one is a
reach gap and one is a factor of five.

#### Helpmate is a per-core loss, and that is new

The single-thread comparison had been run under the `-M 256` handicap of 70, so it
was not evidence. Run clean:

| helpmate, 546 positions, 10 s | solved | only Chest |
| --- | --- | --- |
| Chest (single-threaded) | 491 | — |
| mateprover, 16 threads | **501** | 2 |
| mateprover, 1 thread | **464** | **30** |

**mateprover wins helpmate only by spending sixteen cores.** Per core it is behind
by 27 positions against a single-threaded 1999 program. Every other goal is the
opposite way round — mate-in-8 is 169 single-threaded against 168 at sixteen
threads, stalemate 759 against 759 — so this is specific to the cooperative
search, and it is consistent with the 3.3x median speedup being the lowest of any
goal.

That reframes 66. The two-ply split was worth 3.2x and was reported as fixing a
starvation problem, which it did. It did not make the cooperative search good; it
made a weak search parallel enough to win on a 32-core machine. The honest reading
is that helpmate remains this engine's weakest goal and the win is bought with
hardware.

Chest's own manual calls helpmate "notoriously hard to compute for CHEST". On a
per-core basis that is still too modest.

### 73. The Cooperative Search: One Idea Rejected, One Kept

72 established that helpmate is this engine's weakest goal and that the win was
bought with hardware — 501 at sixteen threads against Chest's single-threaded
491, but only **464 on one thread**. That is an algorithm problem, and threads
were never going to fix it.

**Attempt 1, rejected: order the last ply only.** 66 measured the ordering pass at
70% of the cooperative search's time. Ordering costs a make_move and an in_check
per candidate, and what it buys is finding a solution sooner — which a search
that spends most of its life proving branches empty should barely value. The last
ply is different, because the prune there reads the check bit scoring computes.
So: score at ply 1, plain legal moves everywhere else.

On the six-man h#4 it was worth 1.83x — 38.9 s to 21.2 s, and 53.6M nodes to
29.2M. On the corpus it **lost 8 positions**, 464 to 456.

Ordering was earning its cost. Coverage under a time cap is decided by how fast
the FIRST solution is found, not by throughput on the exhaustive remainder, and a
better move order buys exactly that. A 1.8x speedup that arrives at the answer in
a worse sequence is a loss. Rejected, and it is the seventh time here that a
single position and a corpus have disagreed.

**Attempt 2, kept: an admissible reachability bound.** The lesson from the first
attempt is that reordering a dead subtree is worth nothing and not searching it is
worth everything. So prune instead.

A helpmate ends in checkmate, so at the final position some unit of the mating
side attacks the mated king's square. Two relaxations make that checkable in a few
bitboard ANDs:

- the mated king ends within `their_moves` king-steps of where it stands, on an
  empty board;
- a mating unit attacks that square after at most `our_moves` of its own moves,
  on an empty board, promotion included.

Both relaxations only widen what is allowed, so the bound can rule a subtree out
but never rule one in — blockers can shorten a slider's reach and obstruct a
route, never create one. If no unit of the mating side can attack ANY square the
king could reach, no mate exists down that line and the subtree is dead. The test
runs before the move list is built, so it saves generation as well as recursion.

Scope, and both halves matter: **helpmate only**, since a helpstalemate needs no
check and the argument has nothing to stand on; and **only when the mating side
has three moves or fewer**, because the table stops at three and using it beyond
that would UNDERSTATE reach, which is the one way this becomes unsound.

| helpmate, 546, 10 s | before | after |
| --- | --- | --- |
| 1 thread | 464 | **473** |
| 16 threads | 501 | **506** |
| helpstalemate, 16 threads | 362 | **363** |

The prune fires 14.3 million times on a single six-man h#4 and takes it from
38.9 s to 29.0 s. All 412 checks pass, including the 240 cooperative negatives —
which are the ones that matter, because a bound that is too tight loses solutions
silently and would show up nowhere else.

**The gap is narrowed, not closed.** Single-threaded, 473 against Chest's 491:
eighteen positions, down from twenty-seven. The bound only applies in the last
three mating moves; extending the table deeper would widen its reach, and a
tighter bound — counting the moves needed to cover the king's flight squares
rather than merely to check it — is the obvious next step and a much harder
theorem to keep admissible.

### 74. Tightening The Reachability Bound Found It Was Unsound

73 added an admissible reachability bound to the cooperative search and reported
it sound on the evidence available: 412 checks passing, 240 cooperative negatives
among them, and no solvable position lost on the corpus. It was not sound. The
attempt to tighten it is what exposed that, which is worth recording, because the
bug had already shipped and every gate in place had passed it.

**The tightening.** Two candidates, both free:

- Exclude the mating KING from the "can attack the enemy king" test. A king
  cannot give check, so it can never be the unit attacking at the end — and a
  discovered check is delivered by the piece whose line opens, not by the king
  that vacated. Strictly tighter at zero cost.
- Extend the table from three of the mating side's moves to five, so the bound
  applies at shallower nodes too.

The second was **rejected on measurement**: 26.5 s to 29.9 s on the six-man h#4,
for 2% more prunes. The nodes it newly covers are the shallow ones, which are
few, and testing them costs more than the rare prune returns.

The first exposed the bug. With the king excluded, one position stopped solving:
`8/3bb3/6p1/3K1k2/5P2/7P/7q/8 b - -`, an h#4 where White mates with a king and
two pawns. Solvable in 0.36 s before the tightening; unsolvable after.

**A tighter admissible bound cannot lose a solution.** So either the tightening
was wrong — it is not, a king really cannot give check — or the bound was already
unsound and the king's very wide attack set had been masking it.

It was the second. The pawn model:

> Pawns are modelled with their forward moves (an empty board offers nothing to
> capture)

True of an empty board and **irrelevant**, because the table must be a superset
of what happens on a REAL board, and there a pawn captures diagonally and changes
file. Modelling only forward moves made the table an UNDERestimate for pawns,
which is the one direction that makes the bound unsound. The position above is
mated by a pawn that captures its way off its file, so the bound declared the
subtree dead. Including the king had been hiding it: a king near the action
attacks so much that the test almost never failed, whatever the pawns did.

The fix is three lines — a pawn's relaxed moves include both diagonals — and the
position returns in 0.43 s.

#### The gate that should have existed from the start

The bound is unconditional, so it cannot be tested by toggling a flag, and 73's
guard tested it only by consequence on a 30-position sample. That sample is what
caught this, at 26 of 30 against a threshold of 27 — a margin of one, on an
arbitrary threshold. It could as easily have passed.

So the bound was measured properly: a temporary switch, the whole 546 at 10 s,
bound on against bound off, comparing solved SETS rather than counts.

| | solved | |
| --- | --- | --- |
| bound off | 500 / 546 | |
| bound on | **508 / 546** | |
| **lost by enabling it** | **0** | |
| gained | 8 | |

(The switch had to be read once into a static. Read per node, `getenv` cost more
than the bound saved and dropped the same binary from 508 to 469 — a measurement
apparatus expensive enough to destroy the measurement.)

**Where it stands.** With the pawn model fixed and the king excluded:

| helpmate, 546, 10 s | 73 | now |
| --- | --- | --- |
| 1 thread | 473 | **476** |
| 16 threads | 506 | **508** |

Single-threaded, 476 against Chest's 491: fifteen positions, from twenty-seven
before any of this. The gap is two thirds closed and the remaining third still
wants the harder theorem — counting the moves needed to COVER the king's flight
squares, not merely to check it, which needs a coverage argument where one move
can cover several squares at once.

### 75. The Flight-Square Bound, And A Mate Delivered By Underpromotion

74 left the cooperative search fifteen positions behind Chest per core and named
the next step: count the moves needed to COVER the mated king's flight squares,
not merely to check it. `docs/HELPMATE_COVERAGE_DERIVATION.md` has the derivation;
this is what it cost and what it bought.

**Counting is the wrong question.** The obvious version — count unhandled flight
squares, require that many moves — is not a lower bound at all. One move can
handle several flights at once: a queen arriving beside the king covers three or
four, and a discovered line covers more with the covering piece not moving. There
is no useful constant either, since a queen attacks up to 27 squares.

**Containment is the right question.** Instead of "how many moves would covering
cost?", ask "is covering possible at all?" — which an empty-board relaxation
answers soundly and the combinatorial difficulty disappears. Build

    H = (squares the mating side can attack within its remaining moves)
      ∪ (squares either side can occupy within theirs)
      ∪ (squares occupied now)

and require, for at least one square the king can reach and a non-king mating
unit can attack, that all of its flights lie in H. If no such square exists, no
mate does either. The proof needs nothing about how many moves the covering costs,
or which unit covers what, or whether one move covers several.

Measured on the six-man h#4: **40.8 s with no bound, 13.7 s with this one** — three
times, against the check-only bound's 22 s.

#### It failed its own gate, and the reason was underpromotion

74 established the gate: an on/off switch read once into a static, the whole 546,
compare solved SETS, **zero lost** to pass. First run: **one lost**.

`6bq/1p1np2r/1p2k1pP/2bp2rp/4pK2/8/8/8 b - -`, solved in 0.24 s with the bound off
and refused in 0.28 s with it on — not a timing boundary, a wrong answer.

Debug output narrowed it to the CHECK half rather than the new flight half:
the candidate set was empty. The position has White with a king and one pawn on
h6, and the solution ends **g7xf8=N**. A knight on f8 attacks e6, where the black
king stands. **A queen on f8 does not.** The table modelled promotion as "becomes
a queen", so it concluded the pawn could never attack the king's square, and
pruned the mate away.

Rook and bishop need no modelling of their own — their attacks are subsets of the
queen's. The knight is the exception, and it is the whole exception.

This is the third instance of one pattern, and the pattern is worth naming: **a
modelling shortcut that is true of the common case and false in general**, in the
direction that UNDERSTATES what a piece can do, which is the only direction that
makes an admissible bound unsound.

| | shortcut | reality |
| --- | --- | --- |
| 74 | pawns move forward, an empty board offers nothing to capture | a real board does, and the pawn changes file |
| 75 | a promoting pawn becomes a queen | it may become a knight, whose attacks are not a subset |

Both were invisible on every position whose mate is ordinary. Both are now named
regression tests rather than comments.

**After the fix**, the gate passes:

| helpmate, 546, 10 s | solved |
| --- | --- |
| bound off | 502 |
| bound on | **513** |
| **lost** | **0** |
| gained | 11 |

| | 73 | 74 | now |
| --- | --- | --- | --- |
| 1 thread | 473 | 476 | **482** |
| 16 threads | 506 | 508 | **513** |

Single-threaded, 482 against Chest's 491. **Nine positions**, from twenty-seven
before any of this work. The bound is no longer obviously the limiting factor,
and what remains is not another relaxation of the same argument.

### 76. Characterising The Cooperative Residue: There Is No Class

75 left the cooperative search nine positions behind Chest per core and the
obvious next move was another theorem. Before proposing one, the residue was
characterised — because the mechanism had been predicted twice already and was
wrong about which half mattered both times.

**Nine was a net figure and the wrong number to reason from.** Chest solves 491
single-threaded, mateprover 482, but the sets are not nested: there are **14**
positions Chest reaches and single-threaded mateprover does not, and 5 the other
way. Fourteen is the set to characterise.

| | |
| --- | --- |
| solved by mateprover at 16 threads | **14 of 14** |
| solved single-threaded at 60 s | **14 of 14** |
| genuine reach gaps | **0** |

Every one falls to six times the clock, or to threads. Not one is a position this
engine cannot reach.

And there is no shape to them. Depths 3 and 4; six men to twenty; White holding
anything from a lone king and pawn to bishop-knight-pawn-rook; no repeated
material signature. Compare the selfmate residue, where king+pawn is 127 of 904
and king+queen 42 — a class with a name. This is not a class. It is fourteen
ordinary helpmates that happen to sit the wrong side of a ten-second cap.

#### Where the per-core deficit actually lives

On the 478 positions both engines solve single-threaded:

| | |
| --- | --- |
| total time, Chest | 260.7 s |
| total time, mateprover | 367.2 s |
| **aggregate** | **0.71x — mateprover is 1.4x SLOWER** |
| **median per position** | **1.44x — mateprover is faster** |
| positions where Chest is faster | 187 of 478 |

Those two rows point opposite ways and both are true. mateprover wins the typical
position and loses the aggregate, which can only mean it is much slower on the
HARD ones — and the hard ones are what a fixed cap decides. The deficit is not
spread across the corpus; it is concentrated in the deep tail.

#### What that rules out

**A better admissible bound is not the answer.** The bound of 75 is worth 3x and
none of the fourteen is out of reach — they are reachable, just not inside ten
seconds. A bound prunes dead subtrees; these positions lose their time in
subtrees that are alive.

**Nor is there a theorem to find.** A theorem needs a class, and there is no
class. Every structural axis tried — depth, material count, material signature,
which side holds what — separates nothing.

What remains is ordinary throughput on deep cooperative trees: the node cost
itself, or move ordering good enough to reach the solution sooner. 73 measured
ordering at 70% of the time and found that removing it LOST eight positions, so
ordering already pays for itself; the open question is whether a better order pays
more.

That is a constant-factor engineering problem with no clean theorem attached, and
it is worth saying so plainly rather than proposing a fourth mechanism. Two of the
three predictions made about this search were wrong about which half mattered. The
third was made only after measuring.

### 77. There Is No King+Pawn Theorem, And The Bound That Replaces It Does Not Pay

The selfmate residue is 15 positions and the largest defender class in the
904-corpus is **king+pawn: 127 roots, 100 unsolved at import**. The clean-room
analysis called it the larger and harder half and asked for the king+pawn
theorem.

**There is no king+pawn theorem, and the reason is structural.** In a selfmate
the roles invert: the attacker forces the DEFENDER to mate him, so the side that
must deliver mate is the defender and the side that gets mated is the attacker. A
king+pawn defender therefore means

> the mate must be delivered by a pawn, or by what that pawn promotes to, and by
> nothing else — the only other unit that side owns is a king, and a king cannot
> give check.

That is not a theorem needing its own mathematics. It is the reachability
argument of 75 evaluated on the smallest possible mating force. King+pawn is not
a class with a theorem; it is the class where the existing theorem has least to
work with.

So the work became: apply the 75 bound to selfmate with the roles swapped. The
derivation is `docs/SELFMATE_REACH_DERIVATION.md`, written first as the three
before it were, and it flags the two things that differ — who is mated, and that
the budgets are asymmetric (the recursion runs `attacker(d) → defender(d) →
attacker(d-1)`, so at a defender node the MATED side has one move fewer).

The two implementations were **factored into one function** rather than copied.
The last three soundness bugs in this project were all modelling divergence, and
two copies of this reasoning would eventually disagree. The refactor was verified
behaviour-preserving first: identical prune counts, to the unit, on the helpmate
benchmark.

#### It is sound, and it does not pay

It fires enormously — 39.9M, 49.9M and 6.9M prunes on the three king+pawn
positions from the open set — and converts none of them.

Over all 903 distinct selfmates at a 5 s cap:

| | solved |
| --- | --- |
| bound off | **573** |
| bound on | 570 |
| lost | 5 |
| gained | 2 |

**The five are not false prunes.** Every one solves with the bound enabled given
60 s, so nothing was pruned that should not have been — the bound is correct.
They are lost to its cost. Net **-3**, and it is defaulted OFF.

The derivation predicted the mechanism before the measurement, which is some
consolation for the result: in a selfmate the mated side is the ATTACKER, who
*wants* to be mated and self-blocks his own king's flights. That makes the handled
set enormous and the prune rare, while the test still costs at every node. The
same argument that is worth 3x on a helpmate is worth less than its own overhead
here, because the two goals put the cooperating side on opposite ends of it.

Kept, gated, default off, with the numbers — the same disposition as
`--dfpn-min-men` at 68. Two of this project's measured-and-rejected items now
share a shape: a mechanism that is sound, fires constantly, and converts nothing.

#### What this leaves

The 15 selfmate misses are not a reachability problem and not a lone-defender
problem. GAP-2 covered the queen case and converted nothing; this covers the pawn
case and converts nothing. Both were the classes the analysis named. Whatever
Chest is doing on these, it is not something either theorem describes, and the
next honest step is to find out what — not to write a third one.

### 78. Level Skipping Is Correct, Fires Zero Times, And Cannot Be Evaluated Alone

The clean-room search spec orders its items by expected value and puts §6.2 level
skipping near the top: *"Pure win, no risk."* It is neither, here — not because
the idea is wrong, but because its value is not a property of the idea.

#### What was built

Both halves of the spec's proposal, in full:

- **§6.1, the graded failure depth.** A failed search stops reporting a bare
  "no", and reports instead the largest depth for which absence is *proven*.
  `Proof.fail_depth` carried it, and it propagated through both node pairs with
  the arithmetic each one needs — a directmate OR node takes
  `min(reply.fail_depth + 1)` over all replies, its AND node takes
  `max(depth, child.fail_depth)`; the selfmate pair inverts the `+1` between them
  because in a selfmate the roles invert. The proof table stored the stronger of
  `depth` and `fail_depth` on a miss, so a bound survived eviction and reuse.
- **§6.2, level skipping.** The three iterative-deepening route loops stopped
  doing `++depth` and advanced to `fail_depth + 1` instead — past everything the
  failed depth actually disproved rather than one step past what it was asked.
  Bounded below by `depth + 1`, so it can only move forwards, and an abandoned or
  split depth leaves `fail_depth` at zero and the loop simply steps.

414 checks pass. A `levels_skipped` counter was added to say how much it bought.

#### It bought nothing, and "nothing" here is exact

On the target position — `1bR5/rPPPPPPP/n7/8/8/8/2pP4/KRqr3k w - -`, `sfm 6`,
which Chest answers in 0.06 s — the counter read **zero**, with any-depth
refutations both off and on.

One position is a sample, and this project has been reversed by sample-vs-corpus
seven times, so the counter was summed across four corpora and both goal
families:

| corpus | n | solved | nodes | `levels_skipped` |
| --- | --- | --- | --- | --- |
| matetrack d8 | 120 | 89 | 71,796,022 | **0** |
| matetrack d10 | 60 | 45 | 54,432,901 | **0** |
| stalemate union | 120 | 119 | 45,783 | **0** |
| selfmate deep | 120 | 94 | 429,091,197 | **0** |
| | **420** | | **555,365,903** | **0** |

Not "rarely". Not "less than the overhead". Zero, over half a billion nodes.

#### Why, and why it was predictable

Level skipping consumes over-proof: it pays exactly when a search proves *more*
than it was asked to. **MateProver never over-proves.** Every one of its
disproofs is a disproof of the question it was handed, because every one comes
from exhausting the move tree at that depth — and exhausting depth *d* says
nothing whatever about *d+1*, where the extra ply supplies moves that did not
exist.

The spec's own sources of over-proof are the reason it works there, and
MateProver has neither:

- **§7.6, material and endgame knowledge** — "this material cannot mate in *any*
  number of moves" is a statement about all depths at once. Absent here.
- **§7.1, anti-mate failing at the sentinel** — returns a disproof carrying a
  depth larger than the one asked. Absent here.

The one mechanism that *could* have been a source is the reachability bound of
75 and 77, and it is not: it proves "no mate within *d* plies" for the specific
*d* it was given, and more plies mean more reach, so it says nothing about *d+1*
either. The refutation lattice of GAP-1 does prove something depth-independent —
but that already short-circuits the whole root loop, which is strictly stronger
than skipping a level of it.

**So §6.2's value is not a property of §6.2.** It is a property of §7.1 and §7.6,
metered through §6.2. Ordering it as an early "pure win" is correct only in a
codebase that already has them. Read as a dependency rather than a ranking, the
spec's ordering is inverted for this engine.

#### The defender table, which is a real gap and still does not pay

The same work exposed something genuinely missing, and worth recording
separately: **`prove_selfmate_defender` had no transposition table at all.** Its
directmate counterpart has always memoised. That is not a tuning difference, it
is an absent mechanism, and adding it did what an absent mechanism should:

    sfm 6 nodes  85,100,000 -> 74,620,775     -12%

And it is *slower*. The paired corpus run — 903 unique selfmates, 5 s cap,
committed engine against the new build, chunk-resumable because this measure had
already been lost once to a killed job — ran 24 of 40 chunks before the trend
stopped being in question:

| chunk | 1 | 7 | 14 | 20 | 24 |
| --- | --- | --- | --- | --- | --- |
| committed | 21 | 136 | 251 | 345 | 401 |
| defender TT | 20 | 134 | 249 | 343 | 399 |

Dead flat at **-2** for twenty-four consecutive chunks, through s#5 to s#8. The
run was stopped there: a difference that has not moved across 400 positions and
four depth bands is not going to be decided by the remaining 500.

Twelve percent fewer nodes and two fewer solutions is the same shape as
`--dfpn-min-men` at 68 and the selfmate reach bound at 77 — a mechanism that is
sound, does what it claims, and converts none of it. The nodes it saves are
cheap ones; the cost is paid at every defender node including the ones that were
never going to repeat.

#### Disposition

Both reverted. Not gated-and-defaulted-off like 68 and 77, because neither has
the property that made gating worthwhile there — those two fire constantly and
might pay under a different budget, whereas level skipping fires *never* under
any budget this engine can currently produce, and the defender table's loss is
flat across every depth band measured. Dead code that no test can exercise is
worse than a documented absence. The patch is small and mechanical, and this
section is the specification for rebuilding it.

**The condition for revisiting is explicit: build §7.6 first.** Material and
endgame knowledge is the thing that makes a disproof depth-independent, and until
something in the engine can say "not in any number of moves", the graded failure
depth has nothing to grade and level skipping has nothing to skip. If §7.6 lands,
this comes back with it — and should be measured with it, never alone.

#### What this leaves

An eighth entry in the measured-and-rejected column, and the most useful thing in
it is not the result but the shape: **the spec's items are not independent, and
its ordering is by value-in-Chest, not value-in-isolation.** Two of the three
clean-room items evaluated so far (68's preconditioner gate, and this) have come
back negative for reasons that were visible in the derivation beforehand. That is
now the expected outcome for any spec item adopted without first checking which
of its neighbours it depends on, and future items should be read for their
dependencies before their rank.

### 79. The Engine Faults Under AVX, And It Is Not The Engine's Fault

Rebuilding the tree after the 78 revert turned up a crash that had nothing to do
with the revert: `g++ -O3 -march=native` produces a binary that dies inside move
generation. Perft does not reach depth 3.

It was found by accident, which is the interesting part — nothing in the test
suite could have found it, because the suite tests a binary someone else built.

#### Isolating it

Optimisation level was not the variable, and the crash depth moving with it was
the first clue that this was alignment rather than a bad transform:

| flags | result |
| --- | --- |
| `-O3` | ok |
| `-O1 -march=native` | faults at perft 2 |
| `-O2 -march=native` | faults at perft 3 |
| `-O3 -march=native` | faults at perft 3 |
| `-O2 -mavx2` | faults at perft 3 |
| `-O2 -march=native -mno-avx512f` | still faults |
| `-O2 -march=native -mno-avx2` | still faults |
| `-O2 -march=native -mno-avx` | **ok** |

Any AVX at all, at every optimisation level. Narrowing the vector width does not
help; only turning AVX off does.

#### The fault

Under gdb, on `-O2 -mavx2`:

    Thread 1 received signal SIGSEGV
    0x00007ff6f7d6e01d in mateprover::perft (b=..., depth=2) at report.h:44
    => vmovdqa %ymm0,0x40(%rsp)
    rsp  0x5ff430

That is the recursive call in `perft`, in `src/report.h`.

`vmovdqa` requires its destination to be 32-byte aligned. `rsp` is `0x5ff430`;
`0x430 mod 32 = 16`, so the slot at `0x40(%rsp)` is 16-byte aligned and the store
is a general protection fault.

The prologue says why:

    push %r15 ... push %rbx        (eight pushes)
    sub  $0x1e8,%rsp

No `and $-32,%rsp`. GCC used a 32-byte aligned spill slot without emitting the
realignment that would make the frame 32-byte aligned. Windows guarantees only
16, so whether the store faults depends on where the frame happens to land: the
entry `rsp` is 16-aligned, `-64-488` leaves it congruent to 0 or 16 mod 32
depending on the caller, and the coin flip is exactly why the crash depth moves
with the optimisation level and why it can look intermittent.

#### It is not this codebase's alignment request

Worth establishing rather than assuming, since "the compiler is wrong" is usually
the wrong answer:

- there is no `alignas` anywhere in `src/`, and no `__attribute__((aligned))`;
- `Board` is a plain aggregate whose widest member is a `std::uint64_t`, so its
  alignment is **8**;
- the faulting store is a compiler-generated copy into a compiler-allocated
  temporary, not into anything the source named.

So the engine asks for 8-byte alignment, GCC decided on 32 for its own spill
slot, and then did not arrange for it. `-mstackrealign` does not fix it.
`-mpreferred-stack-boundary=5` does not fix it. `-fno-tree-vectorize` does not
fix it either, which rules out the vectoriser and points at inline struct copy
expansion. This is a MinGW-w64 GCC 15.2.0 code generation defect.

#### The fix, and why it is free

AVX is disabled for the engine target on MinGW GCC, appended after
`CMAKE_CXX_FLAGS` so it wins over a user's `-march=native`, with
`MATEPROVER_ALLOW_AVX=ON` to override on a toolchain known to be fixed. It is
unconditional rather than conditional on detecting AVX, because on a baseline
x86-64 build `-mno-avx` is a no-op — there is no configuration where it removes
something the engine was using.

That last claim was measured rather than asserted. Against a plain `-O3` build:

| workload | `-O3` | `-O3 -march=native -mno-avx` |
| --- | --- | --- |
| perft (deterministic) | 7.073 s | 7.053 s |
| node-limited search, 5 paired runs | mean 39.3 s | mean 42.2 s |

0.3% on perft, and on search the machine's own run-to-run spread was ±14 s on a
40 s measurement — the same binary varied from 32.0 s to 46.6 s across runs, so
there is no signal there to find. Which is the expected result: this engine is
branchy pointer-chasing over a transposition table and a move list, with no
hand-vectorised kernel for wider registers to work on. `-march=native` was never
buying anything, so declining to use it costs nothing.

Verified end to end: `cmake -DCMAKE_CXX_FLAGS=-march=native` now configures with
the guard announced, builds warning-free, and passes all 414 checks — on the
exact flags that segfaulted at perft 2 before.

#### The gap this exposes, which is the real lesson

The suite could never have caught this, and neither could CI as described in the
README, because **every gate tests a binary rather than a build.** Correctness
here is a property of the compiled artefact, and the compiler is part of the
system under test. The README advertises builds on Linux/GCC, Linux/Clang,
macOS/Clang, Windows/MSVC and Windows/MinGW — none with `-march=native`, which is
the first flag anyone adds to a chess engine.

Two things follow. The direct-`g++` route stays a documented footgun no build
file can guard, so the README now says so explicitly. And the CI matrix wants a
native-flags entry per platform, which is on the release backlog and now has a
concrete reason to exist rather than a speculative one.

A cosmetic item was fixed on the way past: `mate_out_of_reach` took a
`const Search&` it never read — the prune counters are incremented by its three
callers, not by the function — and the parameter is gone, so the shipped build
is warning-free under `-Wall -Wextra -pedantic` again.

### 80. Material Knowledge: The Useful Form Is Unsound And The Sound Form Fires Zero Times

Section 78 rejected level skipping and named its prerequisite: material knowledge
is what makes a disproof depth-independent, so `7.6` had to be built before `6.1`
and `6.2` could be judged. This is that investigation, and it closes the line
rather than opening it.

#### What 7.6 asks for, bullet by bullet

| bullet | status |
| --- | --- |
| a bare attacker king can never mate | **already implemented** — `position_is_refuted_axiomatically` |
| two bare kings cannot stalemate | true, and worth nothing (below) |
| bare defender king against king-plus-one: minimum-depth table | applies to **2 of 2093** positions |
| king-and-bishops minimum-depth bound | same precondition, same problem |
| endgame tablebase | out of scope by standing instruction |

Applicability was measured before anything was written, across every mate-goal
corpus, with the mating side chosen per goal — the side to move for a directmate,
its opponent for a selfmate or helpmate, because in both of those the roles
invert. The stalemate corpora are excluded on purpose: the theorem is about
reaching a *checkmate*, and K+B cannot mate but stalemates easily.

    positions (mate goals)          2093
    bare defender king                18   (0.9%)
    bare defender AND attacker K+1     2   (0.1%)

Bullet 3 is the section's main lever and it is inapplicable. That is not a
tuning result that might change with a better implementation; the precondition is
simply almost never true outside composed miniatures.

#### The trap

There is an obvious way to make the theorem pay, and it is wrong. Drop the
requirement that the defender be bare and test the mating side's material alone —
"this side has only a king and a knight, so it cannot mate." That fires on **96**
positions instead of 2, thirty-three of which are selfmates the engine currently
loses to the clock. It is very attractive and it is unsound:

    6rk/5Npp/8/8/8/8/8/3K4 b - -      White has K+N. It is checkmate.

A smothered mate. The classical insufficient-material rule is a statement about
K+N against a **bare** king; against a king with men of its own to be entombed
by, a lone knight mates perfectly well. Every one of those 33 selfmates has a
piece-laden attacker, and in a selfmate the attacker *wants* to be mated and will
self-block deliberately — the single worst place to assume mates are impossible.

Shipping it would have produced 33 false refutations: positions reported as
having no solution when they have one. That is the one error class this project
treats as unrecoverable, and it is the fourth time this session that a rule true
of the common case has been false in general. The other three — the pawn model
that omitted diagonal captures, the promotion that considered only queens, the
retrograde generator that never checked legality — all shared the shape, and all
three understated what a piece can do. This one overstates what material cannot
do. The lesson generalises past the direction: **a chess rule quoted from
memory is a rule about the position it was learned on.**

#### What is actually true, checked by exhaustion

Since the arguments are cheap to settle by machine, they were, over every legal
placement rather than by reasoning:

| claim | positions examined | result |
| --- | --- | --- |
| two bare kings, any stalemate? | 7,224 | none — holds |
| K+B vs bare K, any checkmate? | 417,228 | none — holds |
| K+N vs bare K, any checkmate? | 429,440 | none — holds |

So the spec is correct on all three counts. The sound theorem is exactly "the
mating side's material is insufficient **and** the mated side is a bare king",
and its applicability across the same 2093 positions is:

> **0.**

Not small. None.

#### Disposition, and what it settles

Nothing implemented, on the same reasoning as 78: a mechanism that fires zero
times is dead code no test can exercise, and dead code is worse than a documented
absence. Bullet 2 is a fair illustration — the claim is true, but the search
already answers a two-bare-kings stalemate request in 7,591 nodes and no corpus
contains one, so a theorem for it would be pure surface area.

The consequence for 78 is the point of this section. Level skipping needed a
source of over-proof, and the two candidates the spec offers are `7.1` anti-mate
and `7.6` material knowledge. `7.6` is now measured: it cannot supply over-proof
here because it cannot fire here. **The `6.1`/`6.2` line is closed, not
deferred** — there is no longer a pending prerequisite that might unlock it, and
it should not be revisited on the strength of the spec's ordering alone.

Four clean-room items have now been implemented or measured and four have come
back negative: the preconditioner gate at 68, the selfmate reach bound at 77,
level skipping at 78, and material knowledge here. In every case the reason was
visible in the derivation before the measurement, and in every case the spec's
ordering assumed a codebase this one is not. The remaining gap with Chest is 15
selfmate positions with no structural class between them, and the honest next
step is a differential investigation of what Chest actually does on those, not a
fifth item read off the list.

### 81. The Selfmate Gap Is A Disproof Gap, And The Solution Search Was Never The Problem

Four clean-room items had been measured and rejected, each for a reason visible
beforehand, and 80 closed the last of them. What remained was a differential
investigation: stop reading items off a specification and find out what Chest
actually does better. This is that, and it moved the target completely.

Chest is treated strictly as a black box throughout — its published input format
and its reported times, nothing else. Its C source sits in the workspace and was
deliberately not opened, because the clean-room protocol requires the
specification to come from someone else; measuring behaviour is P-BLACKBOX and
allowed, reading the implementation is not.

#### Locating the gap

The gap on the selfmate corpus is 15 positions. It is worth stating the rest of
that comparison first, because it changes what the 15 mean: **MateProver solves
389, Chest 318**, and 86 of MateProver's are positions Chest cannot do at all.
The 15 are a residue, not a deficit.

The first clue came from the position 78 used as its target,
`1bR5/rPPPPPPP/n7/8/8/8/2pP4/KRqr3k w - -`, a selfmate in 6 that Chest answers
in 0.055 s while MateProver needed 20 s and 70 million nodes. Running it with
`--direct-depth` — prove *a* selfmate in 6 rather than *the shortest* — gives

    iterative     70,476,186 nodes    20.1 s
    direct depth     627,113 nodes     0.146 s

a 112-fold reduction, and 0.146 s against Chest's 0.055 s is the same order.
**Almost the entire cost was in the depths that found nothing.**

That could have meant Chest was answering an easier question, so the next step
was to establish what `zN` means to it rather than assume. Asked for a mate in 4
on a position whose shortest is 2, Chest reports `Solution (in 2 moves)`: it
finds the shortest, exactly as `--iterative-depth` does. The two engines are
being asked the same thing.

So the disproofs were timed directly, on the same position:

| depth | Chest | MateProver | ratio |
| --- | --- | --- | --- |
| 3 | 0.000 s | 0.058 s (270 K nodes) | — |
| 4 | 0.002 s | 0.981 s (2.9 M nodes) | ~490x |
| 5 | 0.009 s | 23.47 s (52.3 M nodes) | **~2600x** |
| 6 — *find* the solution | 0.055 s | 0.146 s | **2.7x** |

#### It generalises, which is the part that matters

One position is a sample. Every one of the 15 was asked the strictly easier
question one ply short of its real solution, so that both engines are disproving
and the solution search is removed from the comparison entirely:

    Chest disproved 14 of 15 in       5.75 s total
    MateProver needed               401.70 s, timing out on 12 of 15
    median ratio                         90x

The fifteenth is not a disproof at all — see below.

**The remaining selfmate gap is entirely a disproof gap.** MateProver's search
for a solution is competitive with Chest's, within a factor of three on the one
position where both complete it. Its proof of *absence* is one to three orders of
magnitude slower. Every one of the 15 misses is a position whose solution sits at
depth *N* and whose depth *N-1* disproof MateProver cannot finish.

#### What it is not

Two hypotheses were tested and neither survived, which is worth recording so they
are not tried again:

- **A different branching factor.** Growth per ply was measured on three
  positions at every depth both engines could reach. On the 7-pawn position
  Chest grows 4.5x a ply against MateProver's 24.5x, which looks decisive — but
  on the second position the two are 5.9-9.5x against 7.1-11x, which is no
  separation at all. Three positions, with MateProver timing out on the deepest
  points of each, cannot distinguish a widening ratio from a large constant one.
  The ratio does trend upward within each position (480x then 2600x on the
  first; 94x then 314x on the third), but the honest statement is a large factor,
  not a proven difference in growth.
- **The reach bound of 75 and 77.** It was rejected on the whole corpus, and the
  obvious suspicion was that the aggregate had been dominated by positions that
  solve quickly either way, hiding a win on exactly this class. It was not:
  enabling `--selfmate-bound` on the depth-5 disproof changes 52,253,608 nodes to
  52,254,837. It fires zero times here, for the reason 77 already gave — in a
  selfmate the mated side is the attacker, who self-blocks, so the handled set
  swallows everything.

The profile does show one asymmetry worth a note: 52.3 M nodes produce only
1.43 M table probes, because `prove_selfmate_defender` does not memoise at all
while `prove_selfmate_attacker` does. 78 measured adding it — 12% fewer nodes,
two positions lost — so it is not the answer either. A 12% effect is not a 90x
one, and this section's numbers say the search is losing three orders of
magnitude somewhere the table cannot reach.

#### A sixteenth bad corpus entry, found in passing

`1R2nkb1/p3p1R1/4Q2B/p5P1/Bp6/1Kp1PP2/2P5/8 w - -` is stipulated at selfmate in
7. It was the one position of the 15 where Chest, asked to disprove at 6,
returned a solution instead. MateProver agrees: `sfm 6` in 0.64 s directly, and
2.38 s iteratively, which exhausts depths 1 to 5 and so proves 6 is the shortest.
Two provers agree and one of them proved minimality, so it goes to
`KNOWN_BAD.jsonl` as a wrong stipulation rather than a missing solution — the
first entry of that kind.

#### What this redirects

Every optimisation this project has measured and rejected was aimed at the search
as a whole. This says the target is narrower and the work so far was aimed
slightly past it: **cheap disproof in the selfmate recursion**, and specifically
at shallow remaining depth, where MateProver spends 52 million nodes on a
question Chest settles in nine milliseconds.

It also says what not to do. The solution search needs no work — it is within 3x
of Chest and ahead on coverage, 389 to 318. Nothing in sections 68, 77, 78 or 80
would have helped, and now there is a reason rather than a run of bad luck: all
four were general-purpose pruning aimed at a search that is not where the time
goes.

The specification offers `7.1` anti-mate and `7.2` fatal check cutoff as its
disproof accelerators. Neither transfers unexamined — `7.1` is written for the
directmate orientation, where a defender mate refutes the attacker's move, and in
a selfmate that same event is a *success*; `7.2` is scoped to mate-in-two and its
selfmate variant wants a defender holding exactly two pieces, which none of these
positions has. Whatever Chest is doing here, the specification in hand does not
describe it, and the next honest step is a black-box characterisation of its
disproof behaviour rather than a sixth item read off the list.

### 82. Answer Ordering: A Real Win Through The Wrong Mechanism

Section 81 located the selfmate gap in disproof. A clean-room specification of the
defender answer heuristic then inverted the premise this project had been working
from, and it was right to. This section implements it, measures it, and reports
one win and two findings that the specification did not predict.

#### The premise that was wrong

81 measured 1.00 replies searched per defender node and concluded AND-node
ordering was already optimal with nothing to win. The rebuttal is exact: that
metric cannot distinguish *found a refutation* from *found a refutation that
proves failure three levels deeper*. Both terminate the loop, both cost one
subanalysis, both read as 1.00. Only the second collapses the parent's list.

#### The lazy reply scan

Independent of everything else, and the first thing 81's profile made obvious:
the node built a fully legality-filtered reply list and consumed one entry of it.
Interleaving instead -- test one pseudo-legal move for legality, search it, stop
when it refutes -- takes the depth-5 disproof from **809 million legality tests
to 60.3 million**, a factor of 13 in that work for **1.35x** overall. So the
filter was about a quarter of the runtime rather than most of it, which is worth
knowing: the estimate before measuring was closer to ten.

Legal replies are still visited in generation order with illegal ones skipped,
which is exactly the sequence the filtered list produced, so no verdict can move.

#### The diagnostic, which confirmed the diagnosis and then refused to move

The specification asks for a disproof-depth histogram before anything else:
record, at each AND node returning a disproof, how far the proven failure depth
exceeds the depth requested. A search with no preference among refutations should
spike at zero. On the depth-5 disproof, with the graded failure depth of 78
restored and propagating correctly:

| excess | count | share |
| --- | --- | --- |
| 0 | 25,913,581 | **99.98%** |
| 1 | 3,794 | 0.01% |
| 2 | 119 | 0.00% |
| 3+ | 0 | 0.00% |

That is the predicted signature, and it retro-explains 78 completely: level
skipping counted zero across 420 positions because there was never a single ply
of over-proof to consume.

**Two of the leaves were lying.** The attacker node returns a bare failure both
when GAP-1's axioms refute the position and when the attacker has no legal move
and the position is not the goal. Both are failures at *every* depth -- the first
by the axiom, the second because the line is simply over -- and both were
reporting a failure depth of zero. Since an OR node takes the MINIMUM over its
moves, one zero at the bottom pins every ancestor to exactly what it was asked
for. Both are now wired to an any-depth sentinel.

It changed nothing. The histogram stayed at 99.99% in bucket zero and
`levels_skipped` stayed at zero, and the reason is structural rather than a
missing source:

> An OR node's failure depth is the **minimum** over its moves, because the
> attacker needs only one move to work. For the node to over-prove, *every* one
> of its ~41 moves must over-prove. A single move refuted at exactly the depth
> asked for pins the node, and at least one always is.

So level skipping cannot fire at an OR node in any position that is not nearly
terminal, no matter what feeds it. **The per-move disproof array is not an
optimisation layered on top of level skipping; it is the only consumer of graded
failure depth that can work at all**, because it records the bound per move
instead of collapsing it to a minimum. That is the piece still missing, and it
needs per-move bounds carried with the position rather than a single node-level
bound in the table.

#### The estimator, and what it actually bought

Implemented as specified in shape: estimate how much room each reply leaves the
ATTACKER, try the narrowest first. Deliberately not special-cased for checking
replies -- counting all surviving attacker units without asking which are pinned
over-estimates a checking reply's width, which is the conservative direction and
avoids the failure the specification warns about, where a naive width ranks every
check as excellent.

One observation on the specified form: with a damping factor constant across
replies, the two-ply *product* is monotone in the one-ply estimate, so sorting by
the product and sorting by the first estimate give the same order. The product
only earns its place once checking replies get a different second-ply estimate.

On the depth-5 disproof, ordering off against on:

| | nodes | time |
| --- | --- | --- |
| off | 52,263,840 | 14.55 s |
| on | 11,069,176 | 3.18 s |

**4.7x fewer nodes, 4.6x faster**, and the depth-6 iterative solve returns a
byte-identical best move and principal variation. Not the 160x the specification
measures for Chest, which is unsurprising -- this estimator has none of the
per-piece exact counts, coverage tables, sole-attacker tracking or check
refinement that one has.

**And it is not working through the specified mechanism.** The histogram does not
move. The gain is entirely that a narrower attacker subtree is cheaper to refute
-- category 3 in 81's taxonomy, making each refutation cheaper -- not category 1,
destroying the parent's move list through propagated depth. The heuristic is
worth having on its own terms and the mechanism claimed for it is still not
reachable here without the per-move array.

#### Coverage, and a differential result worth reading carefully

250 selfmates at a 4 s budget:

| | solved |
| --- | --- |
| committed | 198 |
| lazy scan only | 199 |
| lazy scan + ordering | **201** |

On the 15 positions of 81 at 30 s: committed 9, lazy 11, ordered 10 -- ordering
costs one there, which is the familiar shape of a node reduction that does not
pay for its own cost on every position, and n=15 cannot settle it against +3 on
the corpus.

The identity check reported one depth disagreement, and it is not one.
`r2b2RK/4p1PP/1P2Q3/4n1Rp/4kp1P/1P1N1p2/p2P1B2/1B6 w - -` is a selfmate in 4 --
both builds find it at `-z 4`, neither finds anything at 3, and both agree
unrestricted. Asked for 6 with the portfolio running, the committed build reports
`sfm 6; via K2`: a restricted lane won the race, and a restricted lane's answer
is documented at 627 as possibly not the shortest, which is exactly what the
`via` marker exists to say. The new build is fast enough that the unrestricted
lane wins and returns the true minimum. The comparison was across lane types and
the check should have excluded results carrying a restriction marker; the answer
that changed changed for the better. Directmate identity over 80 positions: zero
disagreements.

#### Disposition

Promoted, both parts, default on, with `--no-answer-order` retained as the
differential test. 414 checks pass.

The honest summary is that three of the four things done here were worth doing
and none of them was the thing the specification identified as worth 160x. The
lazy scan is a constant factor nobody had noticed. The estimator is a real 4.7x
through a mechanism its author did not claim for it. The graded failure depth is
correct, now honestly sourced at the leaves, and still inert -- and 82 can at
least say why in one sentence where 78 could only say that it was.

### 83. Three Follow-Ups, Two Measured Out Before They Were Built

82 left four items. Three are settled here, and two of them were settled by a
measurement that cost minutes rather than by an implementation that would have
cost days. That is the point of the section as much as the results are.

#### The per-move disproof array, measured out for the cost of one counter

82 argued the per-move array is the only workable consumer of graded failure
depth, since an OR node collapses its moves to a minimum. That argument is
correct and it is not sufficient, because it says nothing about how often the
array would be *consulted*.

The array pays exactly when a node must be searched again at a greater depth than
the bound already stored for it -- the case where the table knows the position
but not strongly enough to settle the question. One counter on that branch of the
probe answers it:

| | |
| --- | --- |
| attacker expansions | 357,008 |
| table probes | 748,086 |
| known, but at too weak a bound | **22,284** |
| as a share of expansions | **6.24%** |

So the per-move array has an upper bound of about six percent, and would realise
only a fraction of that, since it skips *some* moves at those nodes rather than
all of them. Against an implementation carrying per-move bounds through the table
that is not worth building.

**Why it is so low is the useful part.** The array's value in the original comes
with per-node internal iterative deepening: a node that deepens itself re-enters
its own move list once per level, and the bounds accumulated on the earlier passes
skip work on the later ones. MateProver deepens only at the root, so a node is
entered once per depth it is reached at, and its table entry usually settles the
question outright -- 391,078 hits against 22,284 too-weak. The array is not a
missing optimisation; it is the second half of an architecture this engine does
not have.

**The condition for revisiting is therefore internal iterative deepening, not the
array.** Building the array first would be building the consumer of a producer
that does not exist, which is the same mistake as 78 in the opposite direction.

#### A stronger estimator, rejected on measurement

The obvious weakness of 82's estimator is that its mobility term is a static
per-type weight rather than a real move count, which is exactly what the
specification asks not to do. Replacing it with an occupancy-aware count -- rays
walked to the first blocker, a capture counted and the ray stopped, computed once
per node with a per-reply subtraction for captures, which is the base-plus-
adjustment shape specified -- makes it worse:

| depth-5 disproof | nodes | time |
| --- | --- | --- |
| static weights | 11,058,829 | 2.89 s |
| exact mobility | 14,035,427 | 4.21 s |

Two reasons, both visible afterwards. Exact mobility totals are several times
larger than the static weights, so the king-escape term -- three points per
escape, at most twenty-four -- goes from being comparable to the mobility term to
being swamped by it, and the escape term is where the signal is. And the static
version computes the width on the POST-reply board, so a capture is already
exactly reflected; the "improvement" replaced that with a subtraction from a base
computed before the reply, which is an approximation. The more faithful-looking
implementation was less accurate on the term that mattered and diluted the term
that mattered more.

Rejected; the static weights stay. The headroom against the 160x the
specification reports is still there, but it is not in this direction.

#### The band, swept rather than assumed

82 set the ordering gate at remaining depth 2 by assumption and then gained three
positions on one corpus while losing one on a harder subset -- the signature of a
threshold nobody measured. 200 selfmates at a 4 s budget:

| ordering runs at remaining depth | solved |
| --- | --- |
| off | 163 |
| **2 and above** | **166** |
| 3 and above | 164 |
| 4 and above | 164 |

The assumption was right, and the sweep says something the single setting could
not: moving the gate from 2 to 3 gives back two of the three positions gained, so
**most of the heuristic's value is at remaining depth exactly two** -- the level
where the subtree below each reply is small enough that choosing the cheapest one
decides the node's whole cost. That matches the specification's remark that a
specialised variant runs at exactly that depth, and it says where a better
estimator should be aimed.

The gate is now `--answer-order-min-depth`, defaulted to 2, so the next person
does not have to re-derive this.

The identity check reports the same single disagreement as 82 at bands 2 and 3
and none at band 4, and it is the same non-issue: a position where the committed
build returns a restricted lane's non-minimal answer marked `via K2`, documented
at 627, and the faster build returns the true minimum. It appears at exactly the
bands that are fast enough to win the race, which is confirmation rather than
concern.

#### The paired comparison, re-run

81's figures were measured against a build three changes old, so they were stale
before this section started. Re-run over all 903 distinct selfmates, 10 s and
2 GB a position for each engine:

| | solved |
| --- | --- |
| **MateProver** | **626** |
| Chest 3.19 | 416 |
| only MateProver | 238 |
| only Chest | 28 |

On the 388 both solve: 373.9 s against 838.9 s, **2.24x** in total and a **6.14x**
median per position -- the median being the larger figure because the total is
dominated by a handful of positions near the budget where both engines spend
almost all of it.

**81's 389-against-318 is not comparable to this and should not be quoted beside
it.** That run used a different budget; Chest itself scores 318 there and 416
here, so the change in MateProver's number is not all improvement. The honest
statement is the internal one: at an identical budget, 626 against 416, with the
Chest-only residue now 28 positions rather than 81's 15 -- a larger residue
measured against a much larger denominator, and the correct baseline for whatever
comes next.

Chest also returned a definitive "no solution" on 15 positions. That is evidence
about the CORPUS rather than about either engine, and those entries want the
same two-prover adjudication that produced the 71 in `KNOWN_BAD.jsonl`.

### 84. The Cache Is Provably Sound, The Residue Is Two Classes, And Ordering Has No Headroom Left

Four items from a clean-room specification on estimator refinements and the
deepening-cache interlock. The specification's most useful contribution was not a
feature to build: it was stating, as requirements rather than description, the two
properties the existing cache design silently depends on.

#### The cache's preconditions, now checked

MateProver's exact proof table is keyed by position with **no depth in the key**;
the entry carries `min_proved` and `max_disproved` bounds and `absorb()` keeps the
strongest of each. That is sound only because of two properties of the search:

1. a disproof at depth *d* is a bound over every depth **at or below** *d*, and
2. a reported proof depth is **minimal**.

Break either and the table answers questions it was never asked. The failure mode
is not a crash: it is a non-minimal mate reported months later on a position
nobody tested. **Nothing in the 414 checks could detect it**, because every one of
them runs a single configuration.

`--no-exact-tt` exists for exactly one test, and it is not a tuning knob. Running
each corpus with the table on and off and comparing reported DEPTHS rather than
coverage:

| corpus | solved both ways | depth mismatches |
| --- | --- | --- |
| matetrack d8 | 18 | **0** |
| stalemate union | 119 | **0** |
| selfmate deep | 58 | **0** |
| helpmate | 80 | **0** |
| | **275** | **0** |

Single-threaded and unrestricted throughout, because a restricted lane is
permitted to report a non-minimal depth -- it is marked `via` -- and would have
produced false positives.

Both preconditions hold across every goal. That is an assurance this engine could
not previously make about the design its whole table rests on, and it is worth
more than any of the three optimisations below.

The run also prices the table: 35 solved against 18 on directmate, 63 against 58
on selfmate.

#### The residue is two classes, and mostly not what was assumed

The 28 positions Chest solves and MateProver does not, characterised:

| | men | count | mating force |
| --- | --- | --- | --- |
| miniatures | 6-8 | 14 | K+P (7), K+Q (5), K+R (2) |
| heavy | 13-25 | 14 | large mixed |

Stipulated depths run 5 to 10. **These are not one population**, and half of them
are deep sparse endgames whose natural instrument is a tablebase -- excluded here
by standing instruction.

More usefully, the assumption that they are disproof-bound is largely wrong.
Given `--direct-depth`, which skips the shallow root disproofs entirely:

    iterative      2 of 28      miniature 0/14   heavy 2/14
    direct depth   5 of 28      miniature 2/14   heavy 3/14

**Removing every shallow disproof converts three positions.** So the whole
graded-failure-depth line -- 78, 82, and the per-move array of 83 -- has a ceiling
of about three positions on this residue. 81 was right that the *cost* is
concentrated in disproof; it does not follow that the *residue* is unlocked by
cheapening it, and this section is the measurement that separates those two
claims.

#### Ordering: any is worth three, sophistication is worth nothing

The specification corrected a plan before it was built: at remaining depth exactly
2 the original does not run its width estimator at all, but dispatches to a cheap
additive scorer sharing no machinery with it. So aiming the two refinements at
depth 2, as the band sweep of 83 suggested, would have ported depth-3 machinery
into a band the original deliberately handles differently.

It was ported instead, with every constant re-derived -- the specification records
that the original's author annotates his own values as underived, fitted against
1990s search behaviour, which is the strongest available argument for not adopting
them. Only the relative ordering of the check bonuses is inherited, that being the
specified part.

On the hard depth-5 disproof it looks decisive: 11,074,634 nodes and 2.89 s become
6,395,922 and 2.08 s. On 250 selfmates at a 4 s budget it is a tie:

| | solved |
| --- | --- |
| width estimator | 205 |
| depth-2 additive scorer | 205 |
| ordering off | 202 |

Zero depth disagreements between any pair. That answers the question the
specification posed for this port -- how much of the band's gap is explained by
*any* ordering versus by *sophisticated* ordering -- and the answer is that any
ordering is worth three positions and sophistication is worth none of it.

Kept, gated, default off, with the numbers: the same disposition as the DFPN gate
at 68 and the selfmate reach bound at 77.

#### What this rules out

The specification ranks sole-attacker tracking second and the check refinement
fourth, the latter being its largest piece of work, with the explicit condition
that the check refinement should be built only if the additive scorer leaves room.
**It leaves none.** Two orderings of completely different shape score identically,
three positions above no ordering at all, so the band's headroom above "order the
replies somehow" is zero on this corpus.

Sole-attacker tracking is separately blocked by cost rather than by value: it
requires per-square attacker sets maintained incrementally, which this board does
not have and which is a representation change substantially larger than the
refinement it would enable. With the band's measured headroom at zero, it cannot
earn that from here.

Both are therefore not-built, on measurement, and the estimator work is closed
unless something outside this band gives it a reason to reopen.

### 85. Internal Iterative Deepening, And The Closure Of The Graded-Failure-Depth Line

Built, measured, reverted. What makes this worth a section is not the result but
that it closes a line four sections long with a single root cause, confirmed three
independent ways.

#### What was built

83 rejected the per-move disproof array on the ground that a root-deepened search
re-enters a node at a greater depth only 6.24% of the time. That objection is
specific to root deepening: when the NODE deepens itself, the re-entry is the loop
body, and the array stops being something carried across calls through the table
and becomes a local variable. So the two were built together, which is the lesson
78 and 82 both taught -- neither half of an interlocking mechanism can be judged
alone.

The attacker node gained its own deepening loop, starting past any disproof bound
the table already held for the position, carrying a per-move array recording the
depth to which each move was known refuted, and skipping at each level every move
whose bound still covered it.

#### It does not fire

| | |
| --- | --- |
| attacker expansions | 242,780 |
| of those running an internal loop | **42** |
| attacker moves considered | 5,860,450 |
| skipped without execution | **837** (0.014%) |
| nodes | 11,067,090 -> 12,116,697 (**+9.5%**) |

Lowering the band from remaining depth 3 to 2 changes the count of participating
nodes not at all -- still 42 -- which identifies the cause precisely. **Internal
deepening needs levels to iterate, and only the top few plies have any.** The mass
of an AND/OR tree is at the bottom, where remaining depth is 1 or 2 and there is
nothing to deepen through. The mechanism applies exactly where the nodes are not.

And where it did fire, it skipped 0.014% of candidates.

#### The root cause, measured three ways

That 0.014% is not a coincidence. A move can only be skipped at level *d+1* if
its recorded bound reaches *d+1*, which requires the disproof to have proven
**more** than the level asked for. The disproof-excess histogram of 82 says that
happens 0.02% of the time. The two numbers are the same number.

So the whole line now reduces to one fact, arrived at from three unrelated
directions:

| section | measurement | value |
| --- | --- | --- |
| 82 | disproof-excess histogram, bucket 0 | 99.98% |
| 83 | per-move array consultation ceiling | 6.24% |
| 85 | internal-deepening skip rate | 0.014% |

**MateProver's disproofs prove exactly what they are asked, and nothing
downstream of a graded failure depth can fire until something changes that.** 78
found the symptom, 80 eliminated material knowledge as the source, 82 wired the
two honest any-depth leaves and found they were too rare to matter, 83 priced the
array, and this prices the architecture the array was supposed to need. Every
piece is now measured.

It is worth being precise about what this is not. It is not a defect: proving
exactly the depth asked for is the correct behaviour of an exhaustive AND/OR
search. Over-proof has to come from knowledge that holds independent of depth, and
the only candidates are material or endgame theory -- measured at zero
applicability in 80 -- and a tablebase, excluded by standing instruction. **The
line is closed for a reason, not merely unfinished.**

#### Disposition

Reverted entirely, on 78's rule: a mechanism that fires at 42 of 242,780 nodes and
costs 9.5% is dead code no test can exercise, and dead code is worse than a
documented absence. The `--internal-deepening` and `--iid-min-depth` switches go
with it. This section is the specification for rebuilding it, and the condition
for doing so is explicit and unchanged since 78: something must first make a
disproof prove more than it was asked.

### 86. The Attacker Rejection Test: The Mechanism Six Investigations Missed

A fifth clean-room specification corrected the third, which had stated there was
no static attacker-rejection test in Chest's selfmate search. There is, it is the
largest work-avoidance mechanism in that search, and it is the first thing from
any specification to move the residue.

#### Why it was missed, which matters more than that it was

The earlier conclusion rested on a flag ablation: disabling the fatal-check
cutoff, anti-mate and the mate-in-2 specialist changed selfmate node counts by
exactly zero, so no attacker-side static test was contributing. **The test has no
flag.** It is unconditional, undocumented, and lives inside a routine that reads
as a depth-1 special case rather than as a heuristic. An ablation over the
configurable options cannot see it.

> Ablation measures what is switchable, and a mature program's best ideas are
> often the ones nobody thought to make switchable.

That is the general lesson and it explains the run of six empty results at 68,
77, 78, 80, 83 and 85 more convincingly than any of the individual
post-mortems did.

#### What the test decides

A selfmate in one requires the defender to have at least one legal move and
**every** legal move to checkmate the attacker. The contrapositive is cheap: one
legal reply that does not mate refutes the attacker's move, and the cheapest
witness is a defender KING move. A king move is never itself a check, so any
flight square the king can reach without discovering check is a legal non-mating
reply.

#### Measured as an observer before being built

The specification's own first recommendation, and the right one: compute the
verdict, count it, act on nothing. On the reference disproof:

| | |
| --- | --- |
| depth-1 attacker moves | 4,865,361 |
| would be rejected | **4,514,383 (92.8%)** |
| depth-1 share of all attacker candidates | 91.2% |

So the test covers **84.7% of all attacker work**. A day's measurement settled
what six previous investigations had each spent days failing to find.

#### Implemented exactly rather than geometrically

The specification gives a board-free derivation of the flight set from
incrementally-maintained per-square attacker sets, which MateProver does not have
and which §2 correctly identifies as the real port. It also warns that the
approximation is unsound in the dangerous direction: **over**-estimating the
flight set rejects a real solution silently.

That warning is avoidable entirely. Making the attacker move and walking the
defender king's eight neighbours is exact -- it cannot over-estimate anything --
and it still avoids the whole defender node, which was generating a full reply
list to consume one entry of it. The board representation change is deferred
until the exact form stops paying, and may never be needed.

    sfm 5 disproof   11,059,528 nodes, 2.87 s  ->  1,867,551 nodes, 1.37 s
                          5.9x fewer nodes           2.1x faster

#### The failure the suite caught within seconds

Defaulting it on broke two selfstalemate checks immediately. The witness is "a
king move cannot be checkmate" -- true under a mate goal, false under a stalemate
one, where a quiet king move is exactly what MIGHT stalemate the attacker. The
selfmate and selfstalemate goals share this routine, and the test had been
applied to both.

This is precisely the failure mode 4.5 of the specification describes: a silent
loss of solutions, correct-looking on everything else. It survived zero minutes
against the regression suite, on a composed selfstalemate in 2. Now gated to
`Goal::Selfmate`.

#### Results

250 selfmates at a 4 s budget, and the 28-position residue at 30 s:

| | corpus | residue | miniatures | heavy |
| --- | ---: | ---: | ---: | ---: |
| committed | 205 | 10/28 | **0/14** | 10/14 |
| rejection on | **208** | **16/28** | **4/14** | **12/14** |

Zero depth disagreements against the committed build across 203 shared positions.

The residue result is the significant one. **Four of the fourteen miniatures
fall**, and every previous mechanism scored zero on them -- the reachability
bound, material knowledge, level skipping, the per-move array, internal
deepening, both estimators. The specification predicted exactly this, and for the
right reason: a lone defender king in open space has many flight squares, so the
witness is almost always available.

It also revises the residue itself. The 28 came from a 10 s paired run; at 30 s
the committed build already had 10 of them, so a good part of what looked
structural was budget.

#### Disposition

Promoted, default on, gated to selfmate, with `--no-attacker-reject` retained as
the differential test and `--reject-observer` kept as the measurement aid that
justified the work. 414 checks pass.

### 87. The Residue After The Rejection Test: Nine Positions, No Instrument

86 took the Chest-only selfmate residue from 28 to 16 at a 30 s budget. This
characterises what is left, tests the two mechanisms that looked most likely to
move it, and reports both as negative.

#### What remains

At 30 s, twelve. At 120 s, nine:

| | men | count | stipulation | defender's force |
| --- | --- | ---: | --- | --- |
| heavy | 24 | 1 | sfm 6 | large mixed |
| miniature | 6-8 | 8 | sfm 8 or 9 | K+Q (4), K+R (2), K+P (1), K+PP (1) |

Three of the twelve fell simply to a longer budget -- one heavy at 113M nodes and
two miniatures at 97M and 311M -- which is worth noting because it means part of
what has been called a residue all along was a budget artefact of the 10 s paired
run. The nine that remain burned 300-390M nodes at 120 s without converging, so
they are not marginal.

**The class is sharp.** Eight of nine are sparse endgame-shaped positions with a
deep forced sequence and a defender holding one strong unit. In a selfmate the
defender must be forced to deliver mate, so a lone queen or rook that can check
from dozens of squares is exactly the material that resists being forced -- the
same class GAP-2 was written for at 71, except that here a solution does exist
and the theorem correctly declines to fire.

#### Strengthening the rejection test would not help

The obvious next move after 86 is a stronger witness -- the specification's
board-free geometry, or looking past king moves. Neither pays, for a reason that
took one measurement to establish.

On a failing miniature the test already rejects **94.1%** of depth-1 attacker
moves, and those are 80.5% of all attacker candidates. It is not the limiter.

And extending the witness beyond king moves gains no coverage at all. A defender
node at depth 1 already returns on its first legal reply that fails to mate,
which is the general witness; the king-move test is a cheaper way to reach the
same verdict, not a way to reach more of them. **The test is a cost optimisation,
not a coverage one**, and that distinction was not obvious before measuring it.

#### The defender table, rejected a second time and on its best class

78 added a transposition table to the selfmate defender node, measured -2
positions on the corpus, and reverted it. The aggregate was dominated by
positions that solve either way, so the natural suspicion was that it hid a win
on some class -- and the class above is the one it should have been: a lone queen
shuffling through vast numbers of transposing positions is the textbook case for
memoising the defender side.

    sfm 8, one of the nine, 30 s
      defender table off   81,324,818 nodes
      defender table on    70,704,851 nodes      -13%, no change in time

Thirteen percent, again, and no wall-clock improvement, again. Two independent
measurements now -- once on the whole corpus and once on the class picked
specifically to favour it -- and it does not pay either time. It is not a hidden
class win, and that question is closed.

#### Where this leaves the nine

No mechanism in five clean-room specifications, and none of this project's own,
moves them. Their shape argues for backward analysis rather than forward search:
at 6-8 men the state space is small enough to enumerate in principle, and forward
AND/OR search over a 16-to-17-ply forced sequence is the wrong algorithm for it.

The arithmetic is unfriendly, though. A single 7-man material class is roughly
2x10^11 placements after symmetry, so about 200 GB for a one-byte-per-entry
selfmate table -- and the eight miniatures span seven distinct material classes.
Only one of the nine is a six-man position, where the table would be around 7 GB
and genuinely buildable. **A purpose-built selfmate table closes one of nine**,
which does not justify the subsystem.

So the honest position is that the residue is now nine, sharply characterised,
and without an instrument. That is a better place than 28 uncharacterised, and it
is not a solved problem.

### 88. Counter Evidence On The Direct-Mate Residue, And A Skip That Fires Constantly For Nothing

A sixth clean-room specification supplied the measurement the clean-room boundary
prevents this side from taking: Chest rebuilt with statistics, run on solved
direct-mate positions, counters reported. Two mechanisms dominate there and both
act **before any move is executed** -- the fatal-anti-check family, 3-8 million
calls per position at roughly 50% success, and the restricted mating generator,
returning **empty on 20-40% of calls**.

An empty generator result is not a small saving. It is a whole-node disproof at
depth 1 with no move executions at all.

#### The cheapest sound form, implemented

The generator's contract is that it may emit a superset of the true mating moves
but must never omit one. That makes a trivial first version available without any
of Chest's machinery: **a checkmate is a check**, so at depth 1 a move giving no
check cannot be a solution. That is chess rather than a borrowed heuristic, and
it satisfies the superset guarantee by construction.

MateProver already had the check bit -- the ordering pass computes it and
`ordered_check_shortcut` reads it -- but consulted it *after* making the move.
Moving the test in front of `make_move` converts a skipped mate test into a
skipped move execution.

It fires enormously:

| | |
| --- | --- |
| depth-1 moves skipped before execution | **56,498,545** |
| attacker candidates actually executed | 8,358,421 |
| share skipped | **87.1%** |

And it is worth nothing measurable. 46 of 60 solved either way with zero depth
disagreements, and on a hard mate-in-8 the node counts are 16,955,475 against
16,564,116 in the same wall clock -- a 2.3% difference that is noise.

**The reason is worth more than the result.** `ordered_check_shortcut` had already
removed the expensive half of the work for non-checking moves: reading the bit
made the mate test trivial, so all that remained to save was the `make_move`
itself, and at depth 1 that is not on the critical path. The specification's
larger payoff -- the empty-result whole-node disproof -- is real in Chest because
its generator avoids *generating* those moves; here they are already generated by
the full legal generator before anything can look at them. **Skipping work after
producing it is not the same optimisation as never producing it**, and the
distinction is exactly the value that went missing.

Kept: it is verdict-identical, does strictly less work, and is the structure any
refinement of the generator has to build on. But it is recorded as converting
nothing, and a genuine port would have to move the filter into move generation
rather than after it.

#### What the counters say remains

The fatal-anti-check family is untouched and is the larger of the two mechanisms
by call volume. Its cheapest variant -- king-direction precomputation, rejecting
attacker king moves before execution at remaining depth 2 -- is the only one that
prunes without executing, and is the honest next candidate for the twelve
direct-mate positions. It is also the fiddliest geometry in any of the six
specifications, and on this section's evidence its value should be measured with
an observer before it is built.

### 89. The Coverage-Table Early Exit, Measured Both Ways Before Building

A seventh clean-room specification corrected the sixth's ordering on exactly the
grounds 88 established, and supplied the mechanism 88 was reaching for. It also
gives the method that would have prevented 88, which is the more durable part.

#### The filter-placement ladder

Any pruning mechanism sits at one of four positions, and the position caps what
it can save:

| position | filter runs | can save |
| --- | --- | --- |
| 1 | after execution, before evaluation | the evaluation only |
| 2 | after generation, before execution | the execution and everything downstream |
| 3 | during generation | also the generation of the rejected move |
| 4 | before generation, node-level | **the entire node** |

88 moved a filter from 1 to 2 and measured nothing, because `ordered_check_shortcut`
had already collapsed the position-1 work to a bit read. Chest's largest
direct-mate win at depth 1 sits at position 4 -- and position 4 needs no
generator rewrite.

#### The mechanism, and the two questions

For a mate in one the enemy king must be checked with every escape covered, and a
single move moves a single piece. So if the escape squares cannot be covered by
*any* piece type from *any* square, no mate in one exists and the node fails
before a move is generated. That is decidable from the eight-bit escape mask
alone, which is why it precomputes into a 256-entry table -- derived here by
enumeration over the movement rules, not transcribed, since it contains nothing
empirical.

The specification's method requires answering two questions before building
anything, and 88 answered only the first:

    saving = (downstream work prevented)
           - (cost of the filter)
           - (work an existing mechanism already avoids)   <- the term 88 missed

Both are answerable here without subtree attribution, because at a depth-1 node
the avoided work is entirely local. Measured over 40 mate-in-8 positions:

| | |
| --- | --- |
| depth-1 nodes examined | 26,957,385 |
| early exits -- **Q1** | 4,095,560 (**15.2%**) |
| moves never generated -- **Q2** | **48,977,470** |
| all attacker candidates | 80,456,875 |
| Q2 as a share of them | **60.9%** |

**Q1 is modest and Q2 is enormous, and Q2 is what decides it.** A 15.2% fire rate
would look unpromising on its own, but the nodes it fires on are the ones with the
largest move lists, so 61% of every attacker candidate in the search would never
be generated, executed or tested. Against a cost of eight attack queries and one
array index per depth-1 node, that is comfortably worth building -- and it is the
opposite conclusion to 88, which fired at 87.1% and saved nothing.

That contrast is the section's point. **Fire rate does not predict value; marginal
work on the critical path does.**

#### What is not yet built, and the honest caveat

Only the observer. The predicate is computed and ignored.

The 15.2% is an **upper bound**, and the reason matters. A sound version must use
the *unconditional* escape set -- squares whose availability cannot be closed by
an indirect attack -- and this board has no indirect-attacker sets, so the
observer uses the full escape set instead. A larger mask is harder to cover, so
the exit fires more often than a sound implementation could. The true rate is at
most 15.2% and the true Q2 at most 60.9%.

Closing that gap is the king-escape infrastructure of the specification's §3, and
its cost should be amortised across both residue classes: the same five sets
about the enemy king feed this exit and the selfmate rejection test of 86. Two of
the three residue classes, 36 of the 38 positions, from one piece of
infrastructure.

Two soundness requirements are load-bearing and silent when violated: the table
must never claim inability where a piece can cover, and the exit must use the
unconditional escape set. Both produce correct answers on most positions and lose
mates on the ones that matter.

### 90. The Harness Becomes The Least-Verified Component, And Stops Being One

Four measurement defects arrived in one session. None was in the engine. At the
time the engine carried 414 automated checks and `tools/paired_corpus.py`, the
file every published number passes through, carried none.

The asymmetry is itself the finding. Verification had accumulated where it was
easy to add and where it kept coming back clean, and none of it had gone to the
component that was actually producing wrong numbers.

All four defects were the same bug -- a value attached to the wrong thing:

| Defect | Identity confused |
|---|---|
| `-M` handicap | a per-lane budget passed as a total budget |
| `sm` / `sfm` token | a stalemate line parsed as a selfmate result |
| swallowed output | a truncated stream, rows shifted against their results |
| state keyed by goal | two corpora sharing one resume file, so mate-in-10 reported mate-in-8's numbers under its own heading |

#### What changed

**Records are self-describing.** Every result carries a position digest, goal,
requested depth, engine, engine digest, memory budget, time budget, corpus
digest, harness commit, and schema version. A record is interpretable with no
reference to the file it came from or the loop variable that produced it.
Nothing is positional any more.

**State is keyed by the whole measurement.** The resume key is a hash over the
entire definition -- corpus digest, goal, depth bound, budget, both engine
digests, chunk size. Reopening a state file under a different definition is a
hard error with both definitions printed. The d8/d10 collision is now impossible
by construction rather than unlikely.

**Invariants are asserted at load, not discovered at analysis.** No position
twice; every reported depth inside the requested bound; every record inside the
corpus; a complete run holding exactly two records per row. And a ledger of
result fingerprints, because *two distinct measurements producing byte-identical
results* is the signature of defect four and of nothing else. That check is the
one that was previously performed by noticing, by eye, that two reports looked
the same.

**Parsing is strict and fails loudly.** The result line is split into fields and
matched on its own FEN and on an exact goal token; a foreign goal token is an
error, and so is a line that does not parse. Defect two was a permissive regex,
and a permissive regex is a silent-wrong-answer generator. Note what this buys
beyond the original bug: because each line must identify its own position, a
truncated stream can no longer shift rows against results either. One
requirement, two defects.

**The harness never invents a tuning parameter.** `--mateprover-mb` now defaults
to unset, meaning the engine's own default, and reaches the engine only when the
measurement definition says so -- in which case it is recorded in every record.

#### The checks

Twenty-nine of them, in the same suite and the same gate as the engine's, each
one a defect turned into an assertion. Three of the four original defects are
directly reproduced as failing inputs; the fourth, the `-M` handicap, is checked
structurally against the source, because "does not pass a flag" is not
observable from a parsed line.

The reason they live in `tests/run_tests.py` rather than in a file of their own
is the check-count gate: the suite counts its own checks and three documents
must agree on the number. A separate harness suite would have been a second
place to forget.

### 91. The King-Escape Module: One Analysis, Costed Against Two Residue Classes

`src/kingescape.h`. The reason it is one module and not two is the whole
argument for building it at all.

Two mechanisms wanted the same analysis, and each looked unaffordable while it
was paying for the analysis alone. Section 84 priced the attacker-set
infrastructure against the selfmate residue and rejected it. Section 89 priced
the same infrastructure against the direct-mate residue and could only produce
an upper bound. Costed against both together -- 36 of the 38 residue positions,
two of the three classes -- it is the cheapest remaining work rather than the
most expensive.

#### The five sets

For one king, against the other side's men:

- `flights` -- the directions it may legally step to
- `unconditional` -- the flights no single enemy move can close except by
  covering or occupying the square
- `closeable` -- the rest, with `closers`, the pieces whose indirect attacks
  could shut them
- `openable` -- directions denied right now by exactly one attacker with nothing
  behind it, with `openers`, those sole deniers

`flights` is EXACT and everything else is conservative. That split is the design.

#### The direction of error, which is not the intuitive one

Every consumer uses these sets to DISCARD work, so the safe direction is
*under*-estimating what the enemy king can do -- and the escape set is the one
place where under-estimating is safe and over-estimating is fatal. Claiming a
flight the king does not have makes a mate look impossible, and the node is
thrown away with the answer still in it. Silent, and only on positions nobody
thinks to test.

This runs opposite to the usual instinct that an over-approximating filter is
the safe one. It is worth stating in those terms because the instinct is right
almost everywhere else in this engine.

Two ways to get `flights` wrong, both of which have been made here before:

- **X-ray through the king.** A king stepping directly away from a checking
  slider is still on the line, and the square looks unattacked precisely because
  the king itself is blocking the ray. So every query runs against an occupancy
  with the king lifted off, which is why this module reaches for
  `attacked_on_planes` rather than `is_attacked`.
- **Capture of the attacker.** A neighbouring square holding an undefended enemy
  man is a flight, by capture. Treating occupancy as denial overstates the mate.

#### How it is checked

`--self-check` reads positions and compares the flight mask, square by square,
against `move_is_legal` -- the only authority there is. Over every position in
every corpus, 4,571 of them, **zero mismatches**. The same mode cross-checks the
256-entry coverage table against a second, naive computation of the same
answers, because a table checked only against itself is not checked.

### 92. The Coverage Exit, Built Sound: Three Over-Estimates Removed

Section 89 measured this at 15.2% fire rate and 60.9% of attacker candidates
saved, and called both upper bounds. It was right to, and the bound was loose in
three separate places, all in the same direction:

1. It used the **full** escape set rather than the unconditional one, so it
   counted squares a discovered attack could close.
2. It **excluded kings** from the coverage table. An attacker king two files
   from the defender king covers squares beside it, and a discovered-check mate
   delivered by the king itself is exactly the case that argument misses.
3. It ignored **occupation**. A piece standing on an escape square denies it
   without attacking it.

(2) and (3) were unsound as a mechanism, not merely loose as a measurement: both
would have made the table answer "no piece can cover this" where one can, and
that answer throws a node away. They were harmless only because the observer
ignored its own verdict. Building the mechanism is what forced them out.

The shipped predicate refuses castling and en passant outright, because both
break the premise the whole argument rests on -- one move, one piece. Castling
moves two men; en passant vacates two squares, and a one-blocker x-ray test
cannot see the second. Promotion needs no exception: the table enumerates every
piece type at every offset, so the promoted piece is already in it.

The mechanism sits **before** move generation, which is position 4 of section
88's ladder and the reason it can pay at all. Section 88's filter moved from
position 1 to position 2, fired on 87.1% of candidates, and converted none of
it, because `ordered_check_shortcut` had already reduced the work behind it to a
bit read. Nothing in this engine has a node-level "could a mate in one exist here
at all" predicate, so nothing has harvested this one.

The observer remains, sited after generation, because the mechanism must run
before generation to save anything and the observer must run after it to count
what was saved. Two flags, one predicate, two different questions.

#### The differential, and what it cost to run

260 positions of the mate-in-8 corpora at 20 s, the exit on and off, comparing
`bm`, `dm` and the full principal variation:

```
  with the exit    : 230 of 260 solved
  without          : 228 of 260 solved
  solved only WITH the exit    : 2
  solved only WITHOUT          : 0     <- must be zero
  disagreeing answers          : 0     <- must be zero
```

Every other line is byte-identical apart from the `via` field, which names the
portfolio lane that got there first and is a race between concurrent lanes rather
than a property of the search.

Two extra positions is a modest headline and the right one to quote: this is a
constant-factor mechanism, and the corpus is mostly positions decided long before
the budget runs out. What the differential establishes is the part that matters
more -- **no position moved the other way**. A pruning mechanism whose failure
mode is a silently lost solution is only as good as the evidence that it has not
lost one.

### 93. The Selfmate Node Exit, Rejected; And Where Selfmate Time Actually Goes

The same analysis read the other way round. Where the shipped per-move rejection
test asks "does THIS move leave the defender king a quiet step", the node
version asks "does EVERY move leave it one" -- and when that holds, the node is
finished before a move exists.

It holds almost never. Over sixty selfmate positions:

```
  selfmate depth-1 nodes      : 10,240,552
  node refutations      (Q1)  : 11,016      (0.1%)
  moves never generated (Q2)  : 245,546     (0.1% of attacker moves)
```

**Rejected.** Both questions answered, both negative, and the reason is
structural rather than incidental: requiring the condition to hold for every
move is a far stronger demand than requiring it for one, and the strengthening
costs three orders of magnitude. The predicate stays in the tree behind
`--selfmate-node-exit`, default off, with the number recorded here so nobody
re-derives it.

#### What the same run found instead

```
  rejection test calls        : 319,961,526
  rejected                    : 271,646,533  (84.9%)
```

320 million calls across sixty positions, answering yes 84.9% of the time -- and
still implemented the way it was first written as an observer, by making each
king move and looking.

So the fast path answers the same question with attack queries instead of board
copies:

- **no legal king move** -- exact, answer is no. One attack query per neighbour,
  no move executed.
- **no discovery available** -- lifting the defender king off the board leaves
  the attacker king unattacked, so no king move can give check, so any legal one
  is a witness. Exact, one further query.
- **otherwise** -- a discovery is possible for some king move, though not
  necessarily the one we would use, since a move ALONG the discovered line
  discovers nothing. Defer to the exact form.

The fallback is what keeps it honest. Guessing in the yes direction rejects an
attacker move that might be a solution, and that failure is silent. This is the
same asymmetry as section 91, in a different mechanism.

#### And what it is actually worth, which is less than the call count suggests

Sixty selfmate positions at 8 s, the 55 both configurations solve:

```
  calls to the test           : 347,188,892
  answered by the fast path   : 343,249,723   (98.9%)
  fell back to the exact form :   3,939,169   (1.1%)

  total time, fast            : 67.00 s
  total time, exact           : 70.31 s
  speedup                     : 1.049x total, 1.013x median
```

Identical verdicts on all sixty.

**5% total and 1.3% median, from removing 98.9% of 347 million board copies.**
That is a real gain and a free one -- the path is exact and the differential is
clean -- but it is an order of magnitude smaller than the call count implied, and
the reason is worth naming, because it is this document's own recurring lesson
turned on a change made here rather than a change considered here.

The 320-million figure answered Q1: how often is the predicate consulted. It did
not answer Q2: what share of the search's time those consultations are. They
turn out to be a small one. The old implementation exits at the first witness it
finds, and with a witness present 84.9% of the time it usually found one on the
first or second neighbour -- so the average call was already one or two board
copies, not eight. The saving per call was small, and 347 million times a small
number is still a small number next to the rest of the search.

Kept on by default, because 5% for no risk is worth having and the switch exists
to prove it changes nothing. But recorded here at its true size: **a call count
is a Q1 measurement wearing a Q2 costume,** and this is the third time in six
sections that the distinction has decided the answer.

Note the shape of this result: the measurement built to evaluate a candidate
mechanism rejected that mechanism and found a better target in the same numbers.
That is the second time instrumenting for one question has answered a different
one, and it is an argument for instrumenting more.

### 94. The Fatal-Anti-Check Family, Measured Against The Ordering It Would Replace

Three variants of one idea, all with real volume in Chest's counters: a defender
reply that checks the attacker in a way that leaves no mate-in-one available
refutes the attacker's move outright. Variant 3 searches for one within the reply
list (26-85% success in Chest), variant 2 tests for existence after the attacker
move is executed (35-77%), variant 1 precomputes king directions and prunes
before execution.

The earlier plan put variant 1 first, on the grounds that it is the only
pre-execution one. Section 88 falsified that reasoning about itself: position on
the filter ladder caps what a mechanism can save, it does not establish that
anything is there to save. So all three were measured first, and the measurement
was designed around the term section 88 missed.

#### The right question

What a fatal-anti-check test can save at a defender node is whatever the search
spends **before** it reaches the refuting reply. This engine already orders
replies to put refutations first -- answer ordering, and the refutation-hint
table. So the useful measurement is not the fire rate. It is:

- how many replies the search actually tries before it finds its refutation, and
- whether that refutation is a check anyway, since a quiet refutation is outside
  the mechanism's reach however early it arrives.

Both are counted at every defender node under `--fac-observer`.

#### The result

260 positions of the mate-in-8 corpora:

```
  defender nodes that refuted   : 69,055,404
  refuted on the FIRST reply    : 68,186,917   (98.7%)
  replies tried before refuting : 2,860,459    (mean 0.04)
  refutation was a check   (Q1) : 28,449,012   (41.2%)
  defender replies tried, total : 73,107,114
  Q2 ceiling, share of replies  : 3.9%
```

**98.7% of refutations arrive on the very first reply tried.** The mean number of
replies preceding a refutation is 0.04. The entire pool of work a
fatal-anti-check test could remove is 3.9% of defender replies, and that figure
is a ceiling twice over: it assumes the test itself is free, and it assumes the
test finds the refuter every time it exists -- where in fact only 41.2% of
refutations are checks at all.

**Variants 3 and 2 are rejected.** They target the same pool, and the pool is
already harvested. This is the saving equation from the specification with its
third term dominating:

```
saving = (replies not searched)          <=  3.9%
       - (cost of the fatal-check test)
       - (work answer ordering already avoids)   ~= 96% of the first term
```

**Variant 1 stays unbuilt, and last.** It sits at a different filter point -- it
rejects attacker KING moves before execution, at the attacker loop, rather than
shortening a defender reply list -- so this measurement does not settle it. But it
is the fiddliest geometry in six specifications, and the two variants aimed at
the larger pool have just been measured to nothing. It is now the least
attractive item on the list rather than the first.

#### What this is worth beyond the verdict

Section 88 measured Q1 at 87.1%, built the mechanism, and discovered the trap
afterwards. This one cost a counter and an afternoon and rejected two mechanisms
before either was written. The difference between the two is not care or
scepticism; it is that the second measurement was pointed at the existing engine
rather than at the candidate.

The general form, worth keeping: **when a candidate mechanism finds something
faster, measure how fast the engine already finds it, not how often the candidate
would fire.**

### 95. The First Auditable Table, And What A Re-Run Costs You

The seven-goal comparison re-run through the hardened harness of section 90.
3,008 positions, both engines, 5 s and 2 GB each, one session. Every row carries
a measurement identity and a result fingerprint in
`docs/measurement_ledger.jsonl`: a hash over the corpus digest, goal, depth
bound, both budgets and both engine digests, and a second hash over what was
found. These are the first numbers published here that can be distinguished from
a different measurement by anything other than the heading above them.

| goal (n) | MateProver | Chest | only MP | only Chest | refused |
|---|---:|---:|---:|---:|---:|
| mate d8 (200) | **158** | 126 | 42 | 10 | 0 |
| mate d10 (60) | **49** | 17 | 34 | 2 | 0 |
| stalemate (792) | **756** | 720 | 38 | 2 | 25 |
| selfstalemate (76) | **49** | 48 | 2 | 1 | 14 |
| helpmate (546) | **513** | 482 | 31 | 0 | 14 |
| helpstalemate (431) | **353** | 296 | 57 | 0 | 9 |
| selfmate (903) | **589** | 351 | 261 | 23 | 11 |
| **total (3,008)** | **2,467** | **2,040** | **465** | **38** | **73** |

#### Both engines lost ground, which is the useful part

| | now | before | |
|---|---:|---:|---|
| MateProver | 2,467 | 2,475 | -8 |
| Chest | 2,040 | 2,054 | **-14** |
| margin | **427** | 421 | +6 |
| only MateProver | **465** | 459 | +6 |
| only Chest | 38 | 38 | = |

Nothing in this repository can lower Chest's score. Chest fell by fourteen
anyway, so the absolute numbers are a property of the session -- thermal state,
scheduling, whatever else the machine was doing -- and not of either engine.

That is worth stating as method rather than as an excuse. **An absolute
coverage count is not reproducible across sessions and should never be quoted as
though it were.** What is reproducible is the paired quantity: the margin and the
exclusive-win count, measured on the same machine in the same session with both
engines seeing the same conditions. Those moved by +6 and +6, and every goal's
margin improved or held.

The result fingerprint makes this checkable rather than assertable. Two runs of
the same measurement identity producing different fingerprints is exactly the
session variation described above; the identity says the question was the same
and the fingerprint says the answer was not.

#### The coverage exit is not in this table

Mate-in-8 went **down** three positions. Section 92's mechanism acts precisely
there, so it is worth being blunt: **this sweep is not evidence for the coverage
exit, and reading the mate-in-8 row as a verdict on it would be reading noise as
signal.**

The exit's only clean evidence is the same-session A/B in section 92 -- one
binary, one session, the switch the sole difference -- which gave 230 against 228
at 20 s with zero losses and zero changed answers. A cross-session corpus sweep
cannot resolve a two-position effect. It resolves a four-hundred-position one,
which is what it is for, and that is the only claim this table supports.

The temptation was to quote the earlier, higher figures instead, since they came
from the same protocol and flattered the same conclusion. They came from a
different session, and the whole point of the previous section's work was to make
that distinction visible rather than convenient.

#### The residue

Unchanged at 38, with its composition shifted: selfmate 24 to 23, stalemate 1 to
2. At that scale individual positions move between runs and the total is the
stable quantity -- which is itself a caution against characterising a residue
position by position from a single sweep, as section 87 did with nine.

#### One bug, found by running it

The harness had been unit-tested and never run. Its first real invocation failed
inside the first chunk with a `CreateProcess` traceback: Windows will not resolve
a relative executable path against the working directory, and the default engine
path is absolute so nothing had ever exercised the other case. It now resolves
the path and checks both binaries exist before the first position.

Twenty-nine unit checks did not catch it, and no reasonable number of them would
have. A smoke run on twelve positions caught it in four seconds. Both kinds of
test earn their place, and the cheap one earns it first.

### 96. x-check As A Variant, Not A Seventh Goal

The design decision is the whole feature: **a way for the game to END is not the
same as a thing to force.** `Goal` stays at six and the check allowance is board
state, so "3-check selfmate" and "3-check helpmate" are ordinary jobs rather than
new modes. Adding it as a seventh goal would have meant six more the moment
anyone wanted the variant composed with the goals it already has.

`--checks N` or `--checks W:B`; a fifth Forsyth field on the input line states
checks REMAINING (`3+3`) or, Lichess-style, checks already given (`+1+0`), and
overrides the flag on the same principle as `-Z` against a line's own depth token.

#### The rule ends the game; the goal decides what that means

| goal | a check-count ending is |
|---|---|
| mate | **the win being forced** — a win in the variant's own terms. `--no-check-win` demands checkmate instead, for the problemist who meant mate |
| stalemate, selfmate, selfstalemate, helpmate, helpstalemate | a game that ended without reaching the named terminal. The line is **dead at any depth**, since nothing follows a finished game |

That table is why this is a variant. Every stipulation still names what must be
forced; x-check only changes what the board can do underneath all of them.

#### Where it was nearly free, and where it was not

The transposition key's context word had **fifteen spare bits** — the goal at
47-49, en passant at 39-45, and 50-63 unused. Two seven-bit counters fill it
exactly, which is the whole cost of the soundness-critical part: two positions
identical on the board but differing in checks remaining are DIFFERENT positions,
and a key that cannot tell them apart returns one's verdict for the other with
nothing wrong in the output to see. The limit is capped at 126 and refused above
it rather than clamped, because folding two states onto one key is precisely the
failure the goal bits were widened to prevent in section 22.

Four things cost more than expected.

**Five places decide "this move ends it now."** The two node routines, the DFPN
expander, the root-split probe, and its worker. Fixing the node routines first
produced a mechanism that worked at every depth except `-z 1`, which reaches the
same question by a different path. It is now one named predicate,
`check_win_reached`, and the five sites call it.

**The stipulated terminal must win the tie.** A move can be checkmate AND the
final check at once. Firing the check-count terminal first calls that line dead
and loses a real solution — silently, and only on the positions where both rules
bite. `check_limit_terminal` therefore defers whenever the side to move has no
legal move: every stipulated terminal here is "no legal move, and in check or
not", so deferring hands the decision to the routine that owns the goal. The test
is paid only when an allowance has already been exhausted.

**Shortcuts assuming mate is the only ending had to stand down.** The coverage
exit of section 92 proves "no mate in one exists here", which is not "no WIN in
one exists here" — the node it would discard can hold one. Likewise the
mate-reachability bounds and the shallow-fast route, whose two provers test for
mate in one and mate in two directly and know nothing of a check ending.

**The verifier needed teaching, and that was not optional.** A `checkwin` leaf it
could not check would have been a hole straight through the product's central
claim. python-chess has no notion of an allowance, so the verifier now tracks one
itself, spending it going down the tree and restoring it coming back up exactly
as it does the board. It rejects a claimed check win that gives no check, one
with the allowance unspent, and one with no allowance stated at all.

#### Inertness

Standard chess is untouched byte for byte: no fifth field is emitted unless the
rule is in force, the extra attack query in `make_move` sits behind a comparison
that standard play never passes, and a corpus annotation occupying exactly the
position the field would take — `tests/smoke.epd` puts `bm #1` there — is not
mistaken for one. That last is not fastidiousness: every corpus, every
differential in the suite and the harness's strict parser compare those strings.

#### What it is for

The variant changes answers in ways that are the point rather than a side effect.
`8/8/8/4B3/p7/8/1R1R4/k1KB4 w - -` is a selfmate in seven; at `5+2` it still is,
and at `3+3` it is not, because the solution delivers three checks on the way and
under those rules the game ends before the attacker can be mated. A selfmate
stipulation is not satisfied by a check win, and the engine says so.

### 97. The Second Variant Rule, And What The First One Cost To Generalise

x-capture: a side wins outright on its Nth capture, with independent per-side
quotas. `--captures N` or `--captures W:B`, composable with `--checks`, and a
fifth Forsyth field that now reads `chk3+3,cap5+2` -- tagged rather than
positional, because a sixth Forsyth field for the second rule would need a
seventh for the third.

The feature is small. The refactor it forced is the section.

#### The key had space; it was in the wrong field

The last section said the context word was full: goal at 47-49, en passant at
39-45, x-check taking 50-63, one bit spare. That reading was wrong, and wrong in
a way worth naming. **Depth occupied bits 0-31 -- thirty-two bits for a value
that never exceeds the requested search depth.** Reading a field's WIDTH as its
REQUIREMENT is the same error as reading a call count as a cost, which section
93 had already recorded about a different measurement.

Narrowing depth to eight bits freed twenty-four. The quotas now sit at 25-52 and
bits 53-63 are spare for the rules after these. Eight bits is a real bound rather
than a hopeful one, so it is asserted, and `-z` is refused above 127 at the
command line -- the cooperative key carries PLIES, twice the requested depth, so
127 is the largest depth that keys exactly.

#### One rule, one vocabulary entry

`Board` now carries `quota[colour * VR_COUNT + rule]` and everything downstream
is indexed by rule: `variant_active`, `variant_winner`, `variant_terminal`,
`variant_win_reached`. x-check became rule 0 with **no behaviour change**, which
is what made the refactor safe -- the thirty-two checks written for it in section
96 are the regression test, and nothing about them was allowed to move.

Two compatibility promises were kept deliberately. A check-only position still
emits the bare `3+3` it shipped with, because corpora and the suite hold that
spelling. And the certificate token stays `checkwin` rather than becoming a
generic `variantwin`, because `docs/PROOF_FORMAT.md` promises an existing field's
meaning will not change; captures get `capturewin` of their own.

#### The two rules disagree in exactly two places

Every goal-specific shortcut had to be re-audited against the second rule, and
the interesting result is that x-check and x-capture do not need the same gates:

| shortcut | sound under x-check | sound under x-capture |
|---|---|---|
| coverage exit ("no mate in one") | no | no |
| mate-reachability bounds | no | no |
| shallow-fast route | no | no |
| GAP-1, "a lone king cannot mate" | **yes** | **NO** |
| last-ply prune, "a winner must be a check" | **yes** | **NO** |

Both disagreements come from the same place: the reasoning depends on WHAT KIND
of event the quota counts. A lone king cannot give check, so the material axiom
survived x-check -- but a lone king can capture. A check quota is filled by a
check, so the last-ply prune survived x-check -- but a quiet capture fills a
capture quota and wins.

**That is the pattern to test first for any third rule.**

#### The bug the suite caught, and why it nearly did not

`move_can_reach_goal` discards moves at the last ply on the grounds that a
winning move must be a check. Six call sites use it: the attacker loop, two
defender last-ply prunes, the DFPN expander, and both root-split paths. Under a
capture quota every one of them was throwing the winning move away before it was
executed.

The first capture test passed anyway, because the move it used -- `Ra1xa8` --
happened to be a check as well as a capture and survived the filter. It took a
LONE KING capturing a pawn, which cannot be a check, to expose it. Two tests, one
of which was accidentally too weak, and only the second one carried the finding.

This was the sixth shortcut to need gating and the first that was not behind a
named predicate; it sat inside a loop condition. It is `last_ply_win_needs_check`
now. The refactor's value is precisely that there is one place to add rule three's
gating and a test that fails loudly when it is missing -- the gate for GAP-1 is
checked from BOTH directions, because a gate that simply disabled the axiom would
pass "the lone king still wins" and fail "the axiom still bites".

### 98. A Disproof Wants The Opposite Configuration To A Proof

The restriction portfolio is this engine's single largest source of reach --
section 43 measures it at +15 positions of 60 at mate-in-10, gained with none
lost -- and it turns itself on whenever `--time-limit` is set, which is the
right default for the question people usually ask.

It is exactly wrong for the opposite question, and the reason is in `solve.h`
already: **only the unrestricted lane may assert "no solution"**. A restriction
removes attacker options, so a restricted lane that finds a mate has found a
real one -- that is what makes the portfolio sound -- but a restricted lane that
finds nothing has proved nothing whatever, because it never looked at the moves
the restriction removed.

So when the answer is going to be "there is none", every lane but one is
structurally incapable of contributing, while competing for the same cores
throughout. Measured on an opening position from the x-check work: **2.3 seconds
with `--no-portfolio`, and not finished after 90 minutes without it.** Same
binary, same position, same depth.

Both halves of that comparison were taken through the preconditioner handicap
that 99 removes, so the absolute numbers are stale -- the same family of
position now resolves in about 0.15 s. The RATIO is what this section claims and
it is unaffected, since the handicap applied equally to both arms. Re-measured
after the gate, on the x-capture bench, the portfolio still costs 1.15x on a
position with a win and up to 5.9x on disproofs; see 101.

#### Why this is a documentation change and not a behavioural one

The obvious fix is to have the engine notice. It cannot: whether a search will
end in a proof or a disproof is not known until it ends, which is the whole
difficulty.

The next obvious fix is to weight the unrestricted lane more heavily, or to
cancel the others when it completes. The second saves nothing -- on a disproof
the unrestricted lane is the LAST to finish, since it searches the most -- and
the first is a change to the thread allocation that produced the +15, with no
measurement behind it. This project's promotion rule is that a change is
promoted on evidence or documented and rejected, and inventing a new lane
weighting on the strength of one position would be the kind of unmeasured
plausible improvement the rule exists to prevent.

What was actually missing was that a caller had no way to know. `--portfolio`
explained why a restricted lane's PROOF is sound and said nothing about what its
FAILURE is worth, which is the fact that decides whether to use it. It says so
now, in the help text, where the decision is made.

The general form is worth keeping: **a mechanism sound in one direction is not
therefore useful in both, and asymmetric soundness deserves asymmetric
documentation.**

#### The x-capture openings, and why the tempo stops mattering

Three games from the starting array, each side needing one capture to win
outright. All three answer **3**, and depths 1 and 2 are exhaustive refusals in
every case:

| game | answer |
|---|---|
| White wins on its first capture | White forces it on move 3 |
| Black wins on its first capture, White cannot | Black forces it on move 3, after every one of White's twenty first moves |
| either side wins on its first capture | White forces it on move 3 |

Set that beside the x-check answers to the same three questions, which were 5
for White and **not achievable in 6** for Black. One tempo was worth two or more
moves there and is worth nothing here.

The asymmetry is structural rather than accidental. A check requires reaching
the enemy king, which is difficult and roughly symmetric. **A capture requires
the opponent to have committed a man to a square you can take** -- and the side
that moves first is the side that has committed one. White's first move creates
Black's target. Moving second is not a handicap in a capture race; if anything
it is an advantage, and the third game confirms it from the other direction:
giving Black the same winning condition changes White's answer not at all.

### 99. Proof Numbers Measure The Wrong Game Under A Quota

The DFPN preconditioner is the default route's whole reason for existing, and
on plain directmates it earns that: `tests/mates.epd` costs 90,276 nodes with it
and 273,752 without, a 2x saving in wall clock as well as nodes. Nothing here
disturbs that.

Under a variant win rule it is a catastrophe, and the reason is not subtle once
stated. A proof number is an estimate of how hard a node is to PROVE, built
entirely from branching -- how many moves the attacker has, how many replies the
defender has. It contains no term for a capture quota or a check quota. Under
`--captures 3:126` the engine is searching a game in which White wins by
capturing three times, and the preconditioner is ranking nodes by how close they
look to a *mate*. It is not merely uninformative, it is a confident measure of a
question nobody asked, and the search follows it.

Measured on the fourteen-position x-capture bench, single-threaded, verdicts
identical in every row:

| | preconditioner on | off | ratio |
|---|---|---|---|
| start, quota 3, depth 5 | 3.03 s, 1,370,923 nodes | 0.137 s, 140,717 nodes | 22x, 9.7x |
| start, quota 4, depth 5 | 3.30 s | 0.128 s | 26x |
| ply-2 e3 Nh6, quota 3, depth 4 | 0.92 s | 0.030 s | 31x |
| geometric mean, 14 positions | -- | -- | **23.6x** |

The spread is 8x to 32x and the direction never reverses.

The same holds for the FIRST variant rule, which is worth stating separately
because the gate was designed against capture positions and could easily have
been overfitted to them. It is not. Proof numbers know nothing about a check
quota either, and x-check openings from the starting array pay the same tax,
growing the same way with depth:

| x-check, single-threaded | preconditioner on | off | ratio |
|---|---|---|---|
| 1 check, depth 3 | 0.0084 s | 0.0028 s | 3.0x |
| 1 check, depth 4 | 0.247 s | 0.068 s | 3.7x |
| 2 checks, depth 5 | 3.12 s | 0.148 s | 21x |
| 3 checks, depth 5 | 3.62 s | 0.135 s | 27x |
| 2 checks, depth 6 | **did not finish in 240 s** | 2.10 s | **>114x** |

The last row is the shape of the whole finding: the handicap is not a constant
factor, it grows with depth, so it did most of its damage exactly where the
engine was being asked the hardest questions. So the preconditioner
now stands down whenever a variant win rule is live -- meaning both that the rule
is enabled AND that a quota is present on the board, since either half alone is
inert. `--dfpn-under-variant` restores the old behaviour for measurement.

Two things make this worth more than its own 23.6x. First, it was invisible:
verdicts were correct throughout, so no correctness test could have found it, and
every x-capture measurement in this document that predates it was taken through a
23x handicap. Second, it was the load-bearing half of a different mystery -- 32
concluded that root-split threading contributes nothing, and one reason it looked
that way is that the preconditioner was 96% of the wall clock and every second of
it single-threaded. Remove it and the split is worth 3.5x. See 32a.

The general lesson is about scope, and it is the fourth time this codebase has
learned it: a heuristic written for one goal was left switched on for a goal it
had never been evaluated against. The axioms in `prove.h` carry explicit goal
guards for exactly this reason. The preconditioner did not, because it is not
unsound -- it is only wrong.

### 100. A Shared Table Makes The PV Stop Being A Function Of The Position

`--root-split` gives every worker a pointer to one shared proof table. That is
what makes it fast, and it has a consequence that took a failing test to notice:
the worker proving the accepted root move can probe a subtree that a SIBLING
worker proved, and continue down it. On `6k1/8/8/8/8/5K2/5Q1N/8`:

```
--single-thread         bm f2a7; dm 5; pv f2a7 g8f8 h2g4 f8g8 g4h6 g8h8 h6f5 h8g8 a7g7
--root-split --threads 8  bm f2a7; dm 5; pv f2a7 g8f8 h2f1 f8e8 f1e3 e8d8 e3d5 d8c8 a7c7
```

Same key move, same distance, different second move. Both certificates verify
against python-chess: every attacker move legal, every defender branch exhaustive
against an independent generator, every leaf a real checkmate, depth exactly 5.
Two genuine mates in five; the engine reports whichever one the table handed it.

This is worth being precise about, because "the parallel search gives a different
answer" and "the parallel search gives a different proof of the same answer" are
very different findings. The lowest-index acceptance rule (8v) guarantees the
first: whichever worker finishes first, the reported root move is the one the
sequential loop would have returned. It says nothing about the interior of the
tree, and nothing needs it to -- a proof is a proof.

What is lost is reproducibility. The PV stops being a function of the position
and becomes a function of the position and the thread count, which for a tool
whose output is meant to be checkable by someone else is a real cost even though
it is not a soundness cost. `--root-split` therefore stays off by default, and
the suite pins what actually matters: at 8 and 32 threads the split must report
the same key move and distance as the sequential search, and whatever PV it does
report must carry a certificate that verifies.

The alternative -- private tables per worker -- would restore determinism and
give up most of the speed. That trade is available and has not been taken,
because the flag is opt-in and a user who asks for it has asked for the speed.

### 101. Finding-Only Mode Is A Frontier Tool, And Costs On Everything Else

The restriction portfolio runs eight lanes: one unrestricted search plus seven
that ban attacker moves outside some class. A restricted lane can FIND a win --
restricting the attacker never manufactures one -- but it can never settle a
disproof, because a lane that fails has only shown that the win is not in the
class it was allowed to look at. Only lane 0 can say "no solution".

That asymmetry is the whole of its cost profile, and the x-capture bench makes
it plain. Fourteen positions, eight lanes at `-M 512` against a single
unrestricted search at the same budget, verdicts identical throughout:

| | normal | finding-only | cost |
|---|---|---|---|
| start, quota 3, depth 5 (no win) | 0.145 s | 0.805 s | 5.6x |
| start, quota 2, depth 5 (**win**) | 0.223 s | 0.258 s | 1.15x |
| ply-2 e3 Nh6, quota 3, depth 4 (no win) | 0.032 s | 0.187 s | 5.9x |

Finding-only is slower on all fourteen. Thirteen of them are disproofs, where
seven of the eight lanes are structurally incapable of contributing and spend
the budget anyway; the single position with a win is the cheapest row in the
table, because the unrestricted lane finds it about as fast on its own.

This is not an argument against the portfolio, it is a statement of where it
belongs. Its value is entirely at the frontier: a position the unrestricted
search cannot resolve inside any budget you are willing to give it, where a
restricted lane may still surface a win. On the quota-3 first moves that is
exactly the situation -- nineteen of twenty are refuted outright and the
twentieth resists every unrestricted attempt. Everywhere short of that, it is
seven-eighths overhead, and the default of `--no-portfolio` for measurement work
is right.

### 102. Quota Dominance Is Sound, And Cannot Pay

Built, measured, and rejected. The reasoning is sound and the mechanism is
inert, which is a more interesting combination than either alone.

A board carries, per side, the number of captures still OWED -- the remaining
quota. A disproof at remaining `r` disproves every larger remaining quota:

  Suppose the attacker COULD force a win needing `r'' > r` more captures, within
  the depth budget. That strategy reaches the `r`-th capture no later than the
  `r''`-th, and under the smaller quota the game ends there in his favour. It
  cannot instead end in a DEFENDER win, because ending earlier only removes the
  defender's opportunities, never adds one. So a win at `r''` implies a win at
  `r`, contradicting the stored disproof.

The contrapositive is the usable form: on a table miss at `r''`, consult
`r''-1`, `r''-2`, ... and accept a disproof. That was implemented, behind
`--quota-dominance`, with the proof half deliberately left out -- it generalises
the other way and carries a certificate problem, since a proof borrowed from a
higher quota is a valid win but the WRONG DOCUMENT, describing a game that
should have ended earlier.

**It never fires.** Across the fourteen-position x-capture bench: 900,000 probes,
**zero hits**, and a cost of roughly 10%.

The reason was already written down twelve lines above `tt_key`, in the comment
explaining why the quotas are keyed at all:

> Capture quotas are in fact derivable from material, since captures by one side
> are exactly the men the other has lost since the root.

Within a single root the board therefore FIXES the remaining capture quota. Two
lines reaching the same position have made exactly the same captures, so a
neighbouring entry cannot exist to be found. The design note this section
replaced asserted the opposite -- "two lines reaching the same board having made
different numbers of captures arrive with different remaining quotas" -- and
that is simply false. The engine records the fact that refutes it, and it took
900,000 probes returning nothing to go back and read it.

A CHECK quota is different in exactly the way that matters: checks leave no
trace on the board, so the same position genuinely can be reached having given
different numbers of them. So the probe was generalised to the check rule, where
the neighbours can exist, and measured on x-check openings from the starting
array:

| position | hits / probes | cost |
|---|---|---|
| 1 check, depth 3 and 4 | 0 / 36,544 | none |
| 2 checks, depth 5 | 0 / 202,095 | 1.4x |
| 2 checks, depth 6 | **3** / 2,265,741 | 1.08x |
| 3 checks, depth 6 | **3** / 2,039,839 | 1.17x |

Three hits in 2.3 million probes. Transpositions that reach one position with
differing check counts are possible, and at these depths they are vanishingly
rare, so the window is a pure tax: a hash lookup at every missing node, which is
most nodes, paying for an event that happens three times.

Rejected and removed rather than left switched off. A mechanism whose failure
mode is a silent false "no win" -- reverse the direction and disproofs
generalise the wrong way, with correct-looking output and no test able to see it
-- is not worth carrying as dead code on the strength of an argument that has
been measured not to pay. The soundness argument is preserved here; the code is
not.

The general lesson is the cheaper one to have learned first: **before building a
mechanism that exploits two states being distinguishable, check whether they are
ever actually distinct.** The information needed to predict this result was a
comment in the file the change was made in.

### 103. The Quota Ladder: The Premise Was Right And The Arithmetic Was Not

102 rejected quota dominance because it never fires: a capture quota is
derivable from material, so within one root the board fixes it and no
neighbouring table entry can exist. That argument turns entirely on "within one
root", and the ladder was the configuration built to break it.

Solve the position at a capture quota of 1, then 2, then the quota actually
asked for, keeping the exact table across the rungs. Two runs at quotas N1 < N2
assign the same board remaining N1-k and N2-k for the same k -- a constant
offset -- so every disproof a lower rung stored sits exactly one step from a
query the higher rung will make. **That part is correct, and it is the first
configuration in which the mechanism has ever fired:**

| position | dominance hits / probes |
|---|---|
| quota 3, depth 5 | 6,811 / 439,894 (1.5%) |
| quota 3, depth 6 | 15,171 / 6,505,046 (0.23%) |

Against 102's zero in 900,000, the re-derivation is confirmed. The ladder is
still worthless, for a reason that has nothing to do with the mechanism.

#### A lower capture quota is easier to WIN and more expensive to SOLVE

The ladder assumes the preparatory rungs are cheap. Measured at depth 6 from the
starting array, single-threaded:

| rung | result | time |
|---|---|---|
| quota 1 | **win**, 1.Nc3 | 2.39 s |
| quota 2 | **win**, 1.Nc3 | 5.41 s |
| quota 3 (the target) | no win | **2.00 s** |

**The preparation costs 7.80 s to accelerate 2.00 s of work.** The ladder can
never pay at these numbers, and the comment written while building it -- "the
lower rung is also strictly cheaper, which is what makes it worth running at
all" -- was an assumption stated as a fact and never checked.

The reason it is false is worth keeping. A lower quota is easier for the
attacker, so the search returns a PROOF, and proving a win means establishing
that every defender reply fails: a full tree. A higher quota is refused early by
the reachability bound, because three captures in six moves is close enough to
the arithmetic ceiling that most branches die on it. The harder game is the
cheaper question.

That also explains the hit rate independently. Cheap rungs produce mostly
proofs, and only DISPROOFS generalise upward, so the ladder accumulates exactly
the kind of knowledge that cannot transfer. The hit rate falls from 1.5% to
0.23% between depth 5 and depth 6, moving the wrong way with depth.

Measured 5x to 8x slower across five positions, verdicts identical throughout.
Rejected and removed, on the same grounds as 102: the failure mode of a reversed
dominance direction is a silent false "no win", and that is not worth carrying
as dead code once the mechanism is measured not to pay.

Two rejections in a row on the same idea, for two unrelated reasons -- the
states are never distinct, and when they are made distinct the preparation costs
more than the work. Both were cheap to measure and neither was predictable from
the argument, which is the case for measuring rather than reasoning about
performance changes.

### 104. d(3) >= 9, Confirmed Twice

White forces one capture from the starting array in three moves and two in five.
Three takes at least nine -- four more moves than two did, and the jump is large
enough to be worth stating how it was established.

| quota 3 | verdict | nodes | time |
|---|---|---|---|
| depth 6 | no win | 1,655,628 | 2.00 s |
| depth 7 | no win | 20,499,067 | 9.26 s |
| depth 8 | **no win** | 1,294,199,343 | 1,747 s |
| depth 8, re-run | **no win** | 1,292,432,074 | 2,093 s |

Every row is from the UNRESTRICTED lane, which is the only one permitted to
assert "no solution"; a restricted lane that finds nothing has proved nothing.
None is a timeout.

The two depth-8 runs differ in configuration -- the second has the any-depth
refutation axioms disabled -- and their node counts differ by 0.14%, which is
thread scheduling rather than a change of search. Depths 6 and 7 were also run
with the axioms off and produced node counts identical to the digit, so the
axioms contribute nothing to this result and cannot be the source of a false
refusal. Depth 7 was additionally cross-checked sequentially.

Depth 8 cost 188x depth 7, which itself cost 21x depth 6; the growth factor is
itself growing. **Depth 9 projects to between 90 and 240 hours** on 24 cores, so
d(3) exactly is out of reach by search alone. 102 and 103 were the two
candidates for closing that gap and both are rejected.

The context for how large this became: before 99, depth 6 alone ran twelve hours
and returned a timeout at 13.98 billion nodes. It now answers in two seconds.

### 105. A Progress Stream That Publishes Theorems, Not Estimates

A long search was entirely opaque. The depth-8 capture-quota run produced its
first and only output after twenty-nine minutes, and progress had to be inferred
from accumulated CPU time in the process table.

`--progress` writes a line to stderr each time an iterative-deepening depth
completes without finding a solution:

```
progress <fen4>; proven no solution within 5; acn 153926; acs 0.147851;
progress <fen4>; proven no solution within 6; acn 1796864; acs 2.07086;
progress <fen4>; proven no solution within 7; acn 23011374; acs 42.9529;
```

The distinction from a playing engine's `info` output is the whole point. Those
lines are a running conjecture, revised and withdrawn as the search changes its
mind -- which is how The Huntsman came to announce a mate in 18 in a dead-drawn
position. **Every line here is a theorem.** "No solution within 5" is exact and
permanent; nothing emitted is ever retracted. The growth factor per depth is
also visible as it happens, which is what makes a multi-hour run diagnosable
rather than merely long.

Three preconditions, all load-bearing. Only the UNRESTRICTED lane may publish,
since a restricted lane that fails has proved nothing (98). And the depth must
have COMPLETED: one abandoned on the clock or a node budget recorded no verdict,
so publishing a bound for it would be a false theorem. stderr keeps the stdout
result format untouched.

#### The test that could not fail, and why

The first version of the timeout test was worthless, and it took a mutation to
show it. Time out a position with no solution, then assert no bound names a
depth past the cutoff -- it passed against a binary with **both** guards
deliberately removed.

The reason is that on a position with no solution, every bound is TRUE whenever
it is published. Premature or not, "no solution within 6" is a correct statement
about a position that has no solution at any depth. The broken build emitted
only true statements, so no assertion about the content of those statements
could catch it.

A false theorem needs a position that HAS a solution, cut off inside the depth
that finds it. Capture quota 2 is a win in exactly five moves, so "no solution
within 5" is false there, and the mutant says it:

```
FAIL  an abandoned depth publishes no bound  published [1, 2, 3, 4, 5],
                                             but a mate in 5 exists
```

The budget is calibrated from a full run rather than hardcoded, so the cutoff
lands inside depth 5 on fast and slow machines alike.

Two further things the mutation exposed. The guards are REDUNDANT: the emit site
sits after the timed-out break, so each of the loop order and the in-function
guard is independently sufficient, and neither mutation alone changed any
observable behaviour. Both are kept -- defence in depth is right for a false
theorem -- but the comment claiming each was load-bearing on its own was wrong
before this was measured.

And the general form, which this project keeps relearning: **a test whose
assertions are satisfied by the broken build is not a weak test, it is not a
test.** The question to ask of a new test is not "does it pass?" but "what
change to the code makes it fail?" -- and if the answer is "none", it is
measuring nothing. See the honest note on the thread-agreement arm in 8v, which
has the same shape and is still unresolved.

### 106. The One Line In The Stream That Behaves Like Stockfish's

105 streams theorems. `--progress-moves` streams a STATUS, and the distinction
is worth a section because it is the only place this engine emits something that
can be superseded:

```
progress <fen4>; searching depth 5 root move 1/20 b1c3;  acn 1;      acs 0.00008;
progress <fen4>; searching depth 5 root move 2/20 b1a3;  acn 14468;  acs 0.01644;
...
progress <fen4>; searching depth 5 root move 20/20 h2h4; acn 139620; acs 0.13801;
progress <fen4>; proven no solution within 5;            acn 140717; acs 0.13877;
```

It asserts nothing about the position, so it cannot be false -- it is a report
of what the search is doing, not of what it has established. That is precisely
what a playing engine's `info currmove` is, and it is spelled `searching` rather
than `proven` so the two can never be read as the same kind of statement. The
flags are independent: a caller who wants a quiet stream of theorems and no
chatter keeps `--progress` alone.

The infrastructure was already there and unread. `WorkerSlot::current_root` has
always published the claimed root index as an atomic, for the split's
cancellation logic; nothing ever printed it. The sequential path needed one new
idea: the route stamps `iteration_depth` once per pass, and the ROOT attacker
node is the only one that can still have that much depth remaining, so the root
is recognised by `depth == s.iteration_depth` without threading a flag down the
recursion.

The node counts are what make it worth having. Reading the deltas above,
1.Nc3 cost 14,467 nodes and the last three moves cost about 1,600 between them
-- the table is doing the work by then. On the twenty-nine-minute depth-8 run
this would have shown which root move was being ground through and how fast the
front was moving, instead of nothing at all for half an hour.

Emission is per root move rather than on a timer. A timer would make the output
depend on the machine and could not be tested; a root list is short. Under a
split the lines interleave across workers and arrive out of order, which is
honest -- that is what the search is actually doing -- and the suite asserts
every index appears exactly once rather than that they appear in sequence.

#### What was NOT built, and why

A full improving PV, in the Stockfish shape, is the thing this most obviously
suggests and it is not available. Iterative deepening searches UPWARD, so the
first proof it finds is already the shortest and therefore already final: there
is no window in which a preliminary proof exists to be refined. Getting one
would mean searching DOWNWARD -- prove "mate within 12", publish it, then
attempt 11, 10, 9 -- which is a different algorithm, not a reporting change.

Every line of such a stream would be proven and certified, which would be
strictly better than a playing engine's revisable PV. But 103 rejected the quota
ladder on exactly the arithmetic that applies here: preparation that costs more
than the work it prepares. Proving "mate within 12" on a position whose answer
is 9 is not obviously cheaper than proving 9 outright, and may be dearer. That
is a measurement, not a design, and it has not been taken.

### 107. x-escape: The Rule That Is Measured Rather Than Counted

A side LOSES when its own king reaches an escape count of N. E is how many
squares that king could legally step to (see the escape count in board.h), so a
king walled in by its own men is at 0, the starting array is 0 for both sides,
and the number rises as the position opens:

| | E(White) |
|---|---|
| starting array | 0 |
| after 1.Nf3 | 0 -- g1 is not adjacent to e1 |
| after 1.e4 | **1** -- e2 empties, and nothing attacks it |
| after 1.Bd3 | **1** -- f1 empties |

At a limit of 1 those last two lose on the spot. The pressure is on your own
structure rather than on reaching the enemy, which is a different game from
either rule before it.

#### What it does NOT share with the first two rules

VR_ESCAPE occupies a slot in the rule enumerator and shares almost nothing else.
A check or capture quota counts EVENTS a side produces and decrements toward
zero; an escape threshold counts nothing. E is measured afresh at every position,
can rise as well as fall, and reaching the limit LOSES -- so the winner is the
other side. Both places that decide a winner have the inversion written out
rather than folded into the rule loop, because getting it backwards awards the
attacker a win for exposing his own king, which is precisely the losing
condition, and no verdict-based test would look wrong.

**`variant_reachable_within` has no bound to offer and says so.** Its whole
argument is that a side produces at most one qualifying event per move, so a
quota above the move budget is out of reach. E is not produced but measured, and
one capture of a shield -- or the withdrawal of a piece covering three ring
squares -- moves it by several at once, in either direction. A live escape rule
therefore always answers "reachable".

**The key gains no bits.** Six slots at seven bits each would run to bit 66 and
wrap. Skipping the escape slots is not a compromise: the threshold is a CONSTANT
of the search, identical at every node a table ever holds, and E is a pure
function of the position, so `k.board` already separates every value it can
take. Two nodes agreeing on the board agree on E.

**A root can be decided before anyone moves.** Unreachable for the first two
rules, whose quotas count down from at least one; routine here, since a supplied
position may simply already be over its limit. There is no way to say "mate in
0" -- a result line needs a key move -- so the position is reported as
`escapewin w; decided at root;` rather than searched and silently returned
empty, which is what it did at first.

#### The shortcut audit

97 established the pattern: every goal-specific shortcut must be re-audited
against a new rule, and the question to ask is WHAT KIND OF EVENT the quota
counts. All five verdicts for x-escape:

| shortcut | sound under x-check | x-capture | x-escape |
|---|---|---|---|
| coverage exit ("no mate in one") | no | no | **no** |
| mate-reachability bounds | no | no | **no** |
| shallow-fast route | no | no | **no** |
| GAP-1, "a lone king cannot mate" | yes | NO | **NO** |
| last-ply prune, "a winner must be a check" | yes | NO | **NO** |

x-escape stands down everything. GAP-1 fails for a reason x-capture already
taught: a lone king cannot mate, but it CAN capture the man shielding the enemy
king, and raising the enemy's E is the win. The last-ply prune fails because a
quiet withdrawal wins while giving no check at all. The first three fail through
`variant_reachable_within`, which under this rule can never report the position
safe -- so they are switched off structurally rather than by a rule-specific
test, and no separate gate was needed for them.

That last point is worth stating as a limitation rather than a result. The three
are off because the reachability predicate is conservative, not because each was
shown unsound on its own terms. Sharpening the predicate later would silently
re-enable all three, so any such change has to re-audit them first.

### 108. What x-escape Actually Costs, And One Hypothesis That Was Wrong

Three things were shipped unmeasured in 107. All three now have numbers, and one
of them says the opposite of what was predicted.

#### E costs 22% per node, and the shortcuts cost nothing

The first attempt at this compared standard chess against x-escape and reported
x-escape as ELEVEN TIMES cheaper, which is nonsense. The variants skip the DFPN
preconditioner (99) and standard chess does not, so the comparison was measuring
the gate, not the rule. With the preconditioner off on both sides, from the
starting array at depth 5:

| | nodes | nodes/sec |
|---|---|---|
| standard chess | 138,138 | 886,785 |
| x-escape, limit 8 | **138,138** | 728,226 |
| x-capture, quota 3 | 140,717 | 1,051,147 |

The node counts are IDENTICAL, which answers the shortcut question directly: all
five shortcuts standing down costs nothing here, because none of them was firing
on this position anyway. The whole cost of the rule is the per-node one, and it
is **22%** -- about 0.25 microseconds a node for the terminal predicate's two
escape counts. That is at the good end of the 0.5-to-5 microsecond bound 107 left
open, and better than the structural estimate.

#### The variant is playable, and the first answer is 3

| limit | depth 3 | depth 5 | depth 7 |
|---|---|---|---|
| 1 | **win in 3** | win in 5 | win in 7 |
| 2 | no win | no win | no win |
| 3 | no win | no win | no win |

**White forces Black's king to an escape count of 1 in three moves.** Depths 5
and 7 report 5 and 7 because `--direct-depth` searches the requested depth and
does not minimise. Limits 2 and 3 are unreached through depth 7, at 4.7M and
12.4M nodes -- so the variant has the same shape as x-capture, a cheap first
answer and a wall immediately after it.

#### The check bonus HELPS, and the argument that it would not was wrong

107 recorded a concern: the ordering pass scores checks +50000 on the assumption
that attacking near the enemy king is progress, and under x-escape an attacked
ring square does NOT count toward E, so checking near the enemy king reduces
their escape count and helps them. The heuristic looked actively wrong.

Measured at depth 7 from the starting array:

| | checks scored | check bonus off |
|---|---|---|
| x-escape, limit 1 | **5,419,320** | 5,832,287 |
| x-escape, limit 3 | **12,373,829** | 24,651,793 |

Scoring checks is better in both, and twice as good at limit 3. The reasoning was
sound about the GAME and irrelevant to the SEARCH, which is the distinction that
was missed: **move ordering does not rank moves by how much they advance the
goal, it ranks them by how quickly they resolve the subtree.** A check is
forcing -- it collapses the defender's reply list -- and forcing moves prune well
whether or not they are the winning idea. The +50000 is a proxy for forcing-ness,
and that proxy survives a rule change that inverts what progress means.

The correctness half of the same concern was real and is already handled: the
last-ply prune reads the same score as "this move gives check" and would have
discarded a quiet winning withdrawal, so 107 stands it down. Nothing further is
needed, and no change was made here.

The general lesson is the one this session keeps producing. An argument that a
heuristic is aimed at the wrong thing is not evidence that it performs badly, and
costs about a minute to check.

### 109. The Root Split Saturates Because One Move Owns The Tree

Two hypotheses about where a deep capture-quota search loses its speed. Both
were mine, both were confidently argued, and the measurements kill one outright
and redirect the other.

#### Table size is not a locality problem

The claim was that a deep search is memory-bound: a 10 GB table means every
probe is a round-trip, and per-node throughput collapses with depth. Depth 7,
quota 3, single-threaded:

| table | wall clock | nodes | nodes/sec |
|---|---|---|---|
| 64 MB | 243.7 s | 142,448,815 | 584,429 |
| 256 MB | 114.1 s | 51,685,520 | 453,060 |
| 1024 MB | 45.4 s | 23,609,331 | 520,217 |
| 4096 MB | 36.5 s | 20,158,828 | 552,403 |

A bigger table is worth **6.7x in wall clock**, and it buys that by cutting NODES
sevenfold -- re-search avoided, nothing else. Throughput is flat at roughly half
a million nodes a second across a 64-fold range of table sizes. There is no
locality cliff, and the `-M` knob already captures the entire effect. The
hypothesis was wrong, and the earlier "per-node throughput falls 5x with depth"
figure that motivated it was an inference from an unlike-for-like comparison, not
a measurement.

#### The split does not duplicate and is not contended

| threads | wall clock | nodes | speedup | nodes vs 1 thread |
|---|---|---|---|---|
| 1 | 39.74 s | 20,158,828 | 1.00x | 1.00x |
| 4 | 15.37 s | 20,432,735 | 2.58x | 1.01x |
| 8 | 10.89 s | 20,609,767 | 3.65x | 1.02x |
| 16 | 8.72 s | 20,637,714 | 4.56x | 1.02x |
| 24 | 8.36 s | 20,482,154 | **4.75x** | 1.02x |

**The node count moves 2%.** Workers are sharing the table, not re-searching each
other's work, so duplication is not the loss. Throughput rises from 507K to 2.45M
nodes a second, so the machine is not starved either -- it really is doing five
times the work per second. What stalls is the wall clock, and it stalls hard:
1 to 4 threads buys 2.58x, 16 to 24 buys 1.04x.

That leaves one explanation, and it is Amdahl rather than anything exotic. **The
root has twenty moves and they are wildly unequal.** Once every worker has a move,
the wall clock is the cost of the single most expensive one, and no further
thread can touch it. A 4.75x ceiling puts the largest root move at roughly a
fifth of the whole tree.

So the fix is not locality and not lock contention: it is that a one-ply
decomposition cannot divide work that one branch dominates. **Splitting the
dominant root move's own subtree at the next ply is the remaining lever**, and it
is the same two-ply decomposition proposed and abandoned earlier in this session
-- abandoned then because the evidence for it was confounded by the sequential
preconditioner, and supported now that the preconditioner is out of the way.

#### The split is now on by default

4.75x for anyone who did not know to ask for it. The cost is that the reported PV
becomes a function of the position AND the thread count (100), so the suite's
thread-agreement arm now compares the answer with the PV stripped, and a strict
line-for-line arm is kept beside it under `--no-root-split` -- which is the mode
a caller diffing against stored output wants. Relaxing the arm without adding
the strict one would have surrendered real coverage of the sequential path to
make room for a new default.

### 110. Memory Is Not A Lever Until The Depth Makes It One

39 concluded that memory is not a lever. 109 found a 6.7x swing from it and
called that conclusion suspect. Both are right, about different regimes, and the
useful statement is the one neither of them made.

Standard directmates, the whole of `tests/mates.epd`, single-threaded:

| table | nodes |
|---|---|
| 64 MB | 90,276 |
| 256 MB | 90,276 |
| 1024 MB | 90,276 |
| 4096 MB | 90,276 |

**Identical to the node.** The working set fits in 64 MB, so nothing above it is
ever consulted. 39 stands, and stands for the reason it gave.

The same question under a capture quota, at two depths:

| table | depth 6 nodes | depth 7 nodes |
|---|---|---|
| 64 MB | 2,584,693 | 142,448,815 |
| 256 MB | 1,655,628 | 51,685,520 |
| 1024 MB | 1,655,628 | 23,609,331 |
| 4096 MB | 1,655,628 | **20,158,828** |

At depth 6 the curve flattens at 256 MB and everything above it is wasted. At
depth 7 it is still improving at 4 GB, and 64 MB costs **seven times the nodes**.

So the working set grows with depth, and the shipped default of 256 MB a table is
correctly sized for the regime 39 measured and badly short for the one 109 hit.
Nothing is wrong with either measurement; what was missing is that the answer
depends on the depth, and the two sections were taken at different ones.

**Not changed to a larger default.** Every measurement in this document was taken
at 256 MB, and moving the default would silently invalidate the lot while helping
only deep variant searches -- which are exactly the runs whose operator is
already passing `-M` deliberately. The honest fix is that the guidance now
exists: for a capture-quota search past depth 6, memory is the largest single
knob available, worth more than any algorithmic change measured in this session
apart from 99.

The general shape is one this document keeps producing. "X is not a lever" is
never a property of X; it is a property of X ON THE WORKLOAD MEASURED. 32 said
root splitting contributed nothing and was measured on a route where it was
never called; 39 said memory contributed nothing and was measured where the
working set already fitted. Both were sound reports of the wrong regime.

### 111. Two-Ply Decomposition, And Why The Conjunction Argument Is Wrong

109 left one lever: the root split saturates at 4.75x because a single root move
owns about a fifth of the tree, so split that move's own subtree at the next ply.
The design note wrote itself, and it was wrong in a way worth keeping.

#### The argument

A defender node is a conjunction. The attacker must reach the goal after EVERY
reply, so every reply has to be proved whatever order they are taken in. That
makes it the one node type in an AND/OR search where extra threads are not
speculative -- unlike an OR node, where the first move that proves ends the node
and everything computed for the others is discarded. Workers that run out of root
indices should therefore take another root move's defender replies, and nothing
they compute can be wasted.

`--reply-split` implements exactly that. Owners publish their reply list to a
registry, idle workers claim from it, and the owner composes the per-reply
results back **in reply-index order**, so the branch certificates, the
representative line and which reply counts as the refutation are all identical to
what `prove_defender`'s sequential loop would have produced. That contract is
stronger than the root split's, which is allowed to move the PV (100), and it is
checked byte-for-byte by `test_reply_split_does_not_move_the_output`.

#### The measurement

Depth 7, capture quota 3, 24 threads, `-M 4096`:

| configuration | wall clock | nodes |
|---|---|---|
| 1 thread | 39.55 s | 20,443,117 |
| 24 threads, root split only | **7.08 s** | 21,137,589 |
| 24 threads, + reply split | 31.70 s | 56,931,750 |

**4.4x slower, and 2.7x the nodes.** Not a tuning miss -- a refutation of the
argument.

#### Why

"Every reply must be proved" is true of a defender node that ends up PROVED and
false of one that ends up refuted. A refuted node stops at its first refuting
reply, and the reply ordering and the refutation-hint table between them put that
reply first most of the time -- which is what 88's fatal-anti-check observer
already measured and what made that mechanism worthless too. So such a node costs
ONE subtree sequentially, and helpers charge in to prove twenty more that the
sequential search never looks at.

On a position with no solution, every node is refuted. And a position with no
solution is precisely the workload 109's 4.75x was measured on.

The general form is worth stating, because it is not specific to this engine:

> **Parallelise the node type whose children must all be visited, and which one
> that is depends on the verdict, which is what you are trying to find out.**
> Proving needs every AND child and one OR child. Disproving needs every OR child
> and one AND child. There is no node type that is safe to split unconditionally.

The root split escapes this only because it is at the root of a search that is
usually going to fail: every root move must be refuted before "no solution" can
be said, so the root's OR children genuinely all get visited.

#### The gate, and what it proves

If `next` is how many replies the owner has already resolved without refuting,
then `next` is direct evidence about which kind of node this is -- a refutation
closes the node, so a high `next` means the node is heading for a proof.
`--reply-split-min-proved N` withholds helpers until then. It removes the damage
completely, and the counters say how:

| gate | wall clock | nodes | replies claimed | claimed by a helper |
|---|---|---|---|---|
| 0 (ungated) | 32.95 s | 56,906,885 | 192 | 72 |
| 2 (default) | 7.22 s | 21,131,853 | 120 | **0** |
| 4 | 7.15 s | 21,154,698 | 120 | 0 |

Zero. The gate does not make the mechanism cheap, it makes it inert: on this
workload no defender node ever proves two replies before dying. That is the
finding restated as a counter, which is the strongest form it comes in.

Deep directmates, the other regime, twelve mate-in-10s at a 30 s cap:
141.6 s and 10/12 solved without, 153.2 s and 9/12 with. A loss there too.

#### What ships

`--reply-split`, default OFF, with the gate and the differential test. The
mechanism is sound and the code is kept: what is wrong with it is the PLY it
splits, not the way it splits, and the machinery -- the registry, the helper
loop, the index-ordered composition, the abandoned-branch rule -- is what any
correct version needs.

The lever 109 identified is still there and still unclaimed. The work that must
be completed exhaustively in a disproof lives in the ATTACKER nodes below the
refuting reply, not in the defender node itself, so the split has to go down an
OR node rather than an AND node. That is young-brothers-wait applied recursively:
search the first child sequentially, and split the rest only once it has failed
to settle the node. It is a real algorithm rather than an increment, and
`prove_attacker` -- 250 lines of coverage exits, restrictions, GAP-1 axioms and
`fail_depth` composition -- is a great deal more to reproduce faithfully than
`prove_defender`'s reply loop was.

The prediction that opened this section was mine, it was specific, and it was
measured to be backwards within an hour of being implemented. That is the
cheapest way this document has found to learn anything.

### 112. Young Brothers Wait, On The Node Type 111 Says To Split

111 ended with a rule and an unclaimed lever. The rule: parallelise the node type
whose children must ALL be visited, and which one that is depends on the verdict.
The lever: the root split's 4.75x ceiling is one root move, whose cost is one
defender reply, whose cost is the exhaustive refutation of every attacker move
below it. That last list is an OR node, and a failing OR node visits every child.

So this splits OR nodes, and it is the classical arrangement for the classical
reason: **the eldest child is searched alone, and only if it fails to settle the
node do the younger brothers go out to the other workers.** A node that is going
to succeed stops at its first working move, so sharing the rest out is
speculation -- 111's mistake, reached from the other side.

#### Written inside prove_attacker, not beside it

111's mechanism copied a 60-line loop and still needed a byte-for-byte
differential test to be trusted. `prove_attacker` is 250 lines of coverage exits,
restrictions, GAP-1 axioms and `fail_depth` composition, and a faithful second
copy of it is not a thing anyone should maintain.

So the split lives in the loop itself. The loop runs `ybw_first` children the way
it always has, and then, if it has not returned, hands the remainder to a split
point. The per-move work that CANNOT be reordered -- the immediate-win test --
still runs in move order in the owner; a move that wins outright is recorded as a
settling child at its own index rather than returned, because a lower index that
also settles would still be preferred. That is the one rule the whole design
rests on:

> The node is settled by the LOWEST index that settles it, children above that
> index are never claimed, and children below it are still worth finishing.

Applied to an AND node it reads "the first reply that fails"; applied to an OR
node, "the first move that works". Both are exactly where the sequential loop
would have stopped, which is why both splits report the same move, the same line
and the same certificate however the workers happen to interleave. Twelve
differential checks pin it, at the most aggressive settings of both mechanisms.

#### The split is flat, which disposes of the hard part

One registry slot per WORKER, and splitting is confined to the plies just below
the root (`--or-split-plies`, default 1). A worker publishes at most one node, so
an owner waiting for its helpers is never itself a helper, and no owner can ever
be waiting on another owner. The blocked-owner reasoning a general YBWC
implementation needs does not arise. Deeper is measurably worse: depth 7, quota
3, 24 threads gave 6.25 s at 1 ply, 7.07 s at 2 and 7.27 s at 3.

#### What it buys

Depth 7, capture quota 3, 24 threads, `-M 4096`, matched runs:

| configuration | wall clock | nodes | speedup |
|---|---:|---:|---:|
| 1 thread | 36.54 s | 20,443,117 | 1.00x |
| root split only | 6.22 s | 21,173,710 | 5.87x |
| + OR split | **5.44 s** | 21,813,359 | **6.72x** |

Helpers take 613 of the 2,125 children the split hands out -- 29% -- and the node
count moves 3%, so they are sharing the table rather than re-searching.

**1.14x, not the 2x predicted.** The prediction assumed the dominant root move's
cost would divide evenly once opened up; it does not, because the ply-3 children
are as unequal as the root moves were and one of them dominates in turn. The
ceiling moves from 5.87x to 6.72x rather than to 11x. Amdahl does not stop
applying just because the decomposition got finer -- it applies again, one ply
down, and this is what "each ply of decomposition buys progressively less" looks
like when measured rather than assumed.

#### The wait is worth 3.5%, and it is free where it is not worth anything

`ybw_first` defaults to 4 rather than 1, on measurement. Over twelve mate-in-10s,
where nearly every node succeeds:

| ybw_first | deep directmates | depth-7 capture quota |
|---|---|---|
| not splitting at all | 133.7 / 134.1 / 134.8 s | 6.22 s |
| 1 | 138.6 / 138.7 / 140.0 s | 6.79 s |
| 4 | **133.3 s** | 6.82 s |
| 8 | 132.7 s | -- |

One eldest child is not enough of a wait: at 1 the OR split costs 3.5% on proof
workloads, which is the reply split's failure in miniature. At 4 it gives all of
it back while costing nothing measurable on the disproof workload -- because a
node that never succeeds has no early exit for the wait to protect. The wait is
not a compromise between the two regimes; it is free on the one it does not help.

Both corpora solve the same positions in every arm. The regression bar is that a
change may ADD verdicts and must not CHANGE them, and this changes none.

### 113. Supply Follows Demand, And The Deeper Decomposition Was Right After All

112 closed by saying the next lever was "a different decomposition, not a deeper
one", on the evidence that `--or-split-plies 2` measured 7.07 s against 6.25 s
and calling the difference coordination cost. That reading was wrong, and it was
wrong about something the mechanism was already missing.

#### Young brothers wait has two conditions and 112 shipped one

The concept is *wait for the eldest child* **and** *split only if another
processor is idle*. Only the first was implemented. A fixed ply publishes
whether or not anyone is free to help, so two plies created a split point inside
every ply-3 task -- twenty of them against a handful of idle workers -- and most
of the pool became owners blocked at their own tails. The cost was not
coordination. It was **supply with no demand**.

So the registry now carries two atomics, `idle` and `open`, and a node publishes
only while `idle > open`: there is a worker with nothing to do, and it is not
already spoken for by a split point that exists. Two relaxed loads, re-read
before every child rather than decided once -- so a node deep in the tree that
becomes the last thing running opens up at the moment the pool empties out
around it, and never before.

#### And a floor, which is the part that was actually missing

Demand gating alone did not deliver. With the depth limit simply removed it
measured no better than not splitting at all, because a node two plies from the
leaves is a few microseconds of work and cannot pay for being published and
claimed. What was needed was a floor on remaining depth -- and an ABSOLUTE one,
not one relative to the root. The cost of publishing is fixed; the benefit
scales with the work underneath, which scales with remaining depth in its own
right and not with how far down the tree the node happens to sit.

Depth 7, capture quota 3, 24 threads, best of two:

| floor | wall clock |
|---|---|
| not splitting | 7.15 s |
| 2 | 6.80 s |
| **4** | **5.94 s** |
| 5 | 6.02 s |
| 6 -- one level, which is 112's fixed ply | 6.59 s |

#### What it buys

Three interleaved repetitions, since the machine drifts 15% between runs and a
non-interleaved comparison of this size would be measuring the drift:

| configuration | rep 1 | rep 2 | rep 3 | median | vs 1 thread |
|---|---|---|---|---|---|
| 1 thread | -- | -- | -- | 37.70 s | 1.00x |
| root split only | 6.38 | 6.95 | 6.60 | 6.60 s | 5.71x |
| + one level of OR split | 6.16 | 6.19 | 6.40 | 6.19 s | 6.09x |
| + three levels, demand-driven | 5.98 | 5.94 | 6.15 | **5.98 s** | **6.30x** |

The ORDERING is robust -- floor 4 is fastest in every repetition of every
comparison run -- and the MAGNITUDE is not well determined on this machine.
Against one level the gap measured between 1.03x and 1.19x depending on
conditions. The honest claim is the ordering plus "somewhere around a tenth",
not a figure to three digits.

The structural result is not noise-limited and is the better number:

| | children handed out | taken by a helper |
|---|---|---|
| 112, one fixed ply | 2,125 | 613 (29%) |
| 113, demand-driven | 4,659 | **3,645 (78%)** |

Twice as many children offered and helpers taking four fifths of them, with the
node count up 2.3%. The pool is being employed rather than merely being present.

Deep directmates do not regress: 133.6 s against 137.9 s without, 10 of 12
solved either way.

#### The correction

"The real lever past here is a different decomposition, not a deeper one" was
mine, it was confident, and it was the opposite of true. A deeper decomposition
pays perfectly well -- it just cannot pay while it is publishing work nobody is
waiting for, into nodes too small to be worth publishing. Two missing conditions
were being read as evidence about the shape of the problem.

That is the same error as 111's, in a different costume. There the argument was
that AND nodes are safe to split because they are conjunctions; here it was that
depth is the thing that limits splitting. Both times a property of the
IMPLEMENTATION was mistaken for a property of the SEARCH, and both times one
measurement settled it.

### 114. Four Leads, One Bug, And Three Rejections

The four items proposed at the end of 113, worked in order. Three reject on
measurement and the fourth was never a problem. The most valuable thing to come
out of it is a broken counter.

#### 1. Verdict-only table entries -- REJECTED

The claim: `TTEntry` carries a `std::vector<Move> pv` and a `std::string cert`
that the SEARCH never reads (probes consume the depth bounds and the refuted
flag), so dropping them fits more verdicts per megabyte, and 110 found node
counts acutely sensitive to exactly that. Estimated at "roughly halves entry
size". Measured, depth 7, quota 3, 24 threads, `-M 128`:

| | nodes | wall clock |
|---|---:|---:|
| lines stored | 27,944,188 | 8.45 s |
| lines suppressed | 28,180,252 | 8.41 s |

Nothing. And the reason is arithmetic I should have checked before proposing it:

    exact_tt_proof_stores      490,257
    exact_tt_disproof_stores 6,721,080

**93% of entries are disproofs, and a failed `Proof` carries no pv and no cert
already.** An empty vector and an empty string cost 56 bytes inline and no heap
allocation at all, so there was never a 2x to be had -- at most a few percent of
7% of the entries. The estimate was made by adding up field sizes without
checking either the proof/disproof ratio or that empty containers do not
allocate.

The saving grace is order of work: measuring first cost twenty minutes, and the
principal-variation reconstruction that would have been needed to ship it was
never written.

`--no-tt-lines` stays, off, because it is one line and it is the arm of the
measurement.

#### 2. Depth-aware replacement -- REJECTED, after fixing the instrument

The eviction path chose victims in hash order: a verdict that cost a hundred
million nodes was exactly as likely to be shed as one that cost fifty. Replacing
that with a depth-ranked histogram is straightforward and it is consistently,
slightly WORSE. Depth 7, quota 3, 24 threads, `-M 64`, interleaved:

| | rep 1 | rep 2 | rep 3 |
|---|---|---|---|
| generation-aged (shipped) | 33,799,906 | 33,788,216 | 34,193,314 |
| depth-ranked | 34,592,398 | 33,937,390 | 34,359,903 |

Depth is a good proxy for what an entry COST and a bad one for whether it will be
WANTED again, and a cache is for the second. Shallow entries are enormously more
numerous and are re-probed constantly; a deep entry is expensive and often probed
once. The generation-aged policy keeps what is being used, which is the better
signal, and it was already right.

**But the measurement was impossible until a counter was fixed, and that is the
real finding here.** The profile reported:

    -M 64    nodes 34,592,942   tt_evictions 0
    -M 128   nodes 27,864,409   tt_evictions 0
    -M 4096  nodes 21,828,062   tt_evictions 0

Zero evictions at every size, while the node count moves 60% with memory. Both
cannot be true. `shared_table` is handed to the WORKERS and never to the
enclosing search, so `s.shared_table ? shared->evictions() : s.tt.evictions` was
reading the main search's own table -- which barely gets used once the root
splits. Folding the shared table's count in at the end of each route:

    -M 64    tt_evictions 26,600,589
    -M 128   tt_evictions 20,847,636
    -M 4096  tt_evictions 0

Which explains 110's memory curve exactly, and is the number anyone tuning
memory actually wants. A counter that reads zero because it is pointed at the
wrong object is worse than no counter: it answers the question confidently.

#### 3. Restricted-proof to unrestricted-table sharing -- NOT ATTEMPTED

Named as "sound, asymmetric, small". The first two hold: a restricted lane's
PROOF is valid unrestricted, because a mate forced with fewer options available
is still a forced mate, while its DISPROOF is not, because it never looked at
the moves the restriction removed. Sharing one direction and not the other is
the correct design.

"Small" was wrong. The table key carries the goal, the attacker and the node
kind but NOT the restriction, so a shared table would have to refuse disproof
stores from restricted lanes and refuse disproof reads on their behalf -- a
second table type, or a per-store policy threaded through `merge` and `absorb`,
both of which are on the path where a mistake produces a false disproof rather
than a slow search.

Not started rather than half-done, on the same judgement that deferred the
two-ply decomposition earlier in this line of work: a soundness-critical change
begun with little room left to build its differential gate is the mistake the
promotion rule exists to prevent. It is the best-motivated item remaining.

#### 4. Owners help while waiting -- SOUND, AND THERE IS NOTHING FOR IT TO DO

A worker blocked on its own split used to be a lost thread. Letting it help
needs a termination argument, because the failure mode is a hang:

> Splits carry a sequence number from a monotonic counter, and a worker may only
> claim children from splits NEWER than the newest it owns.
>
> Let X be blocked on the split S it owns, seq(S) = x. Every outstanding child of
> S is held by some worker Y which, when it claimed, owned nothing newer than S.
> If Y is itself blocked, it is blocked on a split published after that claim, so
> strictly greater than x. Following "is waiting for" from any blocked worker
> therefore walks a strictly increasing sequence of integers, which over a finite
> set cannot revisit -- so the chain has no cycle, must end, and can only end at
> a worker that is computing. Stack depth is bounded by the same fact.

Implemented, and it measures nothing: 5.37 and 5.30 s against 5.29 and 5.27 s
without. The counters say why -- 3,102 helped claims against 3,004, so the
mechanism fires 98 times in an entire depth-7 search.

**113 had already removed the problem.** Demand gating does not create split
points nobody is waiting for, so it does not create blocked owners either; "every
extra split point converts a worker into a waiter" described the mechanism BEFORE
demand gating, and I carried the diagnosis forward without noticing the fix had
landed on it. Dropping the floor to 2 to manufacture blocked owners does exercise
it -- 187,049 helped claims of 346,970 -- and is slower anyway, because those
nodes are too small to pay.

Default off. Kept because it costs nothing when off, because the proof is written
down, and because any finer decomposition would need it.

#### The pattern, stated once

Three of four rejected, and each was rejected by a fact available before the work
started: the proof/disproof ratio, what a cache is for, and that 113 had already
fixed 114's problem. The measurements were cheap and the estimates were free,
which is the wrong way round -- an hour of arithmetic on the existing counters
would have killed items 1 and 4 before either was written.

### 115. The Idle Time Was Not Idle Time. It Was The Free.

114 ended by saying the remaining parallel loss had no diagnosis worth trusting,
and that the next honest step was to measure where the idle time went rather than
propose another fix for it. That was the right order, because there was almost no
idle time and the loss was not in the search at all.

#### Instrumenting it

Four counters, in microseconds of worker time: `split_work_micros` (inside
run_child), `split_park_micros` (helper loop with nothing to take),
`owner_wait_micros` (owner blocked on its own helpers), `root_work_micros`
(inside a root move of one's own). And the one that makes them meaningful,
`worker_micros`: how long a worker was ALIVE.

That denominator matters. `threads x wall clock` is the wrong one, because a
worker that has left `worker_body` is not idle, it has gone home, and counting
its absence as idleness invents a problem that is not there.

#### Workers are busy while they exist

Depth 7, capture quota 3, `-M 4096`, no portfolio:

| threads | wall | worker-seconds alive | alive / (threads x wall) | idle within alive |
|---|---:|---:|---:|---:|
| 4 | 13.43 s | 44.64 | 83.1% | 2.1% |
| 8 | 8.42 s | 46.96 | 69.7% | 1.7% |
| 16 | 6.32 s | 60.83 | 60.2% | 7.0% |
| 24 | 6.02 s | 69.75 | 48.3% | 5.3% |

**Idle inside a living worker is 2-7%.** Parking and owner-waiting together are
under four worker-seconds of a hundred and fifty. There was never a pool of idle
time to reclaim, which retires the whole line of thinking that produced 111, 112
and the owner-helping mechanism of 114.

What the table does show is that worker LIFETIME falls away from the wall clock
as threads are added. Dividing out gives the same figure at every thread count:

    4 threads    11.16 s in the split, 13.43 s wall   ->  2.3 s outside
    8 threads     5.87 s in the split,  8.42 s wall   ->  2.55 s outside
    16 threads    3.80 s in the split,  6.32 s wall   ->  2.52 s outside
    24 threads    3.49 s in the split,  6.02 s wall   ->  2.53 s outside

A constant two and a half seconds of wall clock outside the parallel region,
independent of the thread count. That is a serial fraction, and at 24 threads it
was 42% of the run.

#### It is the transposition table's destructor

Not thread churn: `--direct-depth`, which builds one pool instead of seven, has
the same 2.48 s. It scales with the TABLE:

| `-M` | wall | in the split | outside | nodes |
|---|---:|---:|---:|---:|
| 256 | 8.88 s | 8.47 s | 0.41 s | 23.1M |
| 1024 | 8.84 s | 7.97 s | 0.87 s | 21.5M |
| 4096 | 6.01 s | 3.36 s | **2.65 s** | 21.1M |

Same search, same node counts, and the time outside the split grows sixfold with
the memory budget. `shared_table` is a `unique_ptr` local to the route, so twenty
million entries across 256 hash maps are handed back one at a time when the
function returns -- inside the reported `acs`, after the verdict is already known.

#### Freeing in parallel

The shards share nothing, so the work divides perfectly. This is the one place in
the program where parallelism is embarrassing rather than speculative: no
ordering, no cutoffs, no verdicts, just deallocation.

| `-M` | outside, before | outside, after |
|---|---:|---:|
| 1024 | 0.87 s | 0.37 s |
| 4096 | 2.65 s | 0.58 s |

And on the whole search, depth 7, capture quota 3:

| threads | before | after | speedup over 1 thread |
|---|---:|---:|---:|
| 1 | 35.32 s | 35.32 s | 1.00x |
| 8 | 6.45 s | 6.45 s | 5.48x |
| 24 | 6.02 s | **3.92 s** | **9.01x** |

The ceiling moves from 6.00x to 9.01x. For comparison, everything else this
session did to the parallel search -- the OR-node split, demand gating, the
tuning of both -- was worth about 1.10x together.

Two caveats stated rather than buried. The single-threaded baseline still pays a
serial teardown of its own private table, which nothing here parallelises, so the
9.01x is measured against a baseline carrying an overhead the numerator no longer
has. And the gain is proportional to `-M`: at 256 MB there was almost nothing to
recover.

#### What this says about the earlier measurements

Every deep-search timing in this document taken at a large `-M` includes this
cost. 109's thread sweep, 110's memory curve, 113's interleaved repetitions --
all of them were partly measuring how long it takes to free the table. The
ORDERINGS in those tables survive, because every arm paid the same overhead, but
the magnitudes were understated and the memory curve in particular was flattered
against itself: a bigger table cut nodes AND added teardown, so 110's 6.7x was
the net of two effects pulling opposite ways.

The general lesson is the one 114 reached from the other side. Three sessions
were spent proposing fixes for a parallel loss that was, in the end, 44% a call
to `operator delete` -- and the instrument that would have shown it was four
counters and an afternoon. Measure the denominator before optimising the
numerator.

### 116. Sharing Proofs Between Lanes, And The Last Of The Free

Two items from 115, both landed.

#### The rest of the teardown

115 parallelised the shared table's destructor and left 0.58 s unaccounted.
Timing the teardown by container, depth 7 at 4 GB and 24 threads:

| | before | after |
|---|---:|---:|
| shared table | 2.65 s | 0.43 s |
| worker searches | 0.20 s | 0.07 s |
| wall clock | 6.01 s | **3.89 s** |

The workers' own maps were the other half: with a shared table their `tt` is
empty, but each still carries a DFPN table and two hint maps that grew all
search. They are independent objects, so they free concurrently for the same
reason the shards do, and the shard destructor and this now share one
`parallel_for_teardown`.

What is left is 0.50 s of 3.89 s -- 13%, against 44% before -- and most of it is
the shards themselves, which are already running on every core. The
single-threaded case is NOT fixed: with no root split there is no shared table,
the private one is freed by the `Search` destructor outside the route, and the
counters read zero because nothing instruments it. Stated rather than left to be
discovered.

#### Proofs cross lane boundaries; disproofs do not

The portfolio's lanes solve DIFFERENT problems -- each restriction is a different
question -- so they cannot share a table in general. They can share proofs,
because a proof is the one statement that survives the difference:

> A restriction only removes ATTACKER options. A mate forced with fewer options
> available is a mate forced with more, so a restricted lane's proof is valid
> unrestricted at the same depth bound. Its DISPROOF is valid for nothing but its
> own problem, because it never looked at the moves the restriction removed.

So there is one table every lane can write proofs to and read proofs from, and
nothing on the disproof side is ever written to it or read from it. Both halves
are enforced at the call sites rather than relied upon: the store path takes only
`proof.ok`, and the probe path takes only the `depth >= min_proved` branch.

**The hazard is minimality, not validity.** `probe_exact_proof_table`'s stated
precondition is that a proof depth is minimal, and a restricted lane can prove a
LONGER mate than exists, because its restriction forbids the short one. A
non-minimal bound still answers "is there a mate within d" correctly, so no
verdict can be wrong; what could move is the reported DEPTH, and a wrong `dm N`
is a wrong answer even when the mate is real.

That is why the gate compares (position, depth) pairs and nothing else. Over 72
positions -- the shipped corpus, 30 mate-in-8s and 30 selfmates -- no depth moved
and no position stopped being solved. One mate-in-8 went from timing out to
`dm 8`. Selfmate, where the restricted lanes do most of the work, was 30/30 both
ways with identical depths.

| corpus | without | with |
|---|---|---|
| tests/mates.epd | all solved, identical depths | identical |
| 30 mate-in-8, 5 s cap | 18/30 | **19/30** |
| 30 selfmate, 5 s cap | 30/30 | 30/30, identical depths |

Which LANE wins does change, and the principal variation follows it -- the lanes
race, and warming one changes who gets there first. That is the same contract the
root split has carried since 100: the answer is a function of the position, the
line is not. Neither is compared by the gate.

On by default, on the gate rather than on the argument: it adds a verdict and
changes none, which is the standing regression bar.

#### What the counters say about headroom

18 proof stores and 0 cross-lane hits on a mate-in-5 that resolves in
milliseconds -- the lanes finish before they can help each other. The gain is at
the other end, on positions near the time limit, which is exactly where the one
recovered mate-in-8 came from. Anyone tuning this should look at
`cross_lane_hits` on long positions and ignore it on short ones.
