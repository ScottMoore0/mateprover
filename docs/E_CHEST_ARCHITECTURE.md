# E Chest Architecture

## Purpose

E Chest is a from-scratch exact directmate prover designed to exceed the current D line in core single-position speed, hard-position proof speed, and batch/service throughput while reducing the chance of proof errors.

The current D line wins mostly through orchestration: batching, route selection, memory sizing, load balancing, and worker fanout. E aims to improve the core prover itself and then regain or exceed D's orchestration advantages.

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
- `benchmarks\scripts\e_verify_proof_tree.py` independently verifies certificates with python-chess.

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
- `--profile` emits stderr JSON counters for TT probes, hits, stores, table size, node split, move-list sizes, and ordering/refutation activity, and `benchmarks\scripts\collect_e_profiles.py` stores those rows as case-labelled JSONL;
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

- `benchmarks/scripts/e_mine_deep_mates.py` mines directmates deeper than the frozen suites, using the engine's own iterative deepening to establish the exact mate distance and grading positions by measured node cost;
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
| `types.h` | 270 | colours, moves, boards, proofs, statistics, table keys |
| `table.h` | 192 | bounded and shared proof tables, memory-budget conversion |
| `search_state.h` | 215 | search configuration, per-search state, cancellation |
| `board.h` | 400 | geometry, attack tables, FEN parsing, attack queries |
| `movegen.h` | 312 | pseudo-legal generation, make_move, legality, planes |
| `ordering.h` | 293 | ordering scores and ordered move-list generators |
| `prooftable.h` | 96 | centralised exact proof-table probe and store |
| `search.h` | 1361 | proof kernel, DFPN preconditioner, routes, parallel search |

**Compilation remains a unity build, deliberately.** The modules are headers included in their original order by one translation unit, so the preprocessed result is textually equivalent to the previous single file. Two reasons: the search depends on cross-module inlining in its hottest paths, and preserving textual order makes the refactor behaviour-preserving *by construction* rather than by inspection.

That was verified rather than assumed: node counts, key moves and PVs are identical to the pre-split binary on all four suites, and paired 3-trial timing shows -1.5% and -0.3%, both inside noise. 92/92 in-repo, ctest green, Linux green, `-Werror` clean.

The split was performed by a script that cuts on declaration boundaries and backs up over preceding comment blocks and `template` headers, so no cut orphans a comment from the code it documents. The first attempt did exactly that -- it separated `template <typename MoveSink>` from `gen_pseudo` -- which is why the boundary logic exists.

`search.h` remains large at 1,361 lines and is the obvious candidate for a further split into kernel, routes, and parallel scheduling.

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

## Promotion Rule

No E search feature is promoted by intuition. A feature is promoted only after:

1. semantic correctness is unchanged;
2. PV validation is clean;
3. no-mate controls remain clean;
4. speed improves on the relevant frozen suite;
5. regressions are documented and intentionally accepted or rejected.
