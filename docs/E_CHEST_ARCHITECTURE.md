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

The first E checkpoint uses a simpler array board to establish correctness. It should be replaced or supplemented by a bitboard board once the verifier suite is stable.

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
- `--bound-tt` adds a separate depth-bound TT probe keyed without depth but with the full board, side to move, attacker color, node type, castling, and en-passant context;
- bound reuse is monotonic: a proven entry may satisfy only equal-or-greater requested depth, and if failed-node bounds are enabled a failed entry may refute only equal-or-smaller requested depth;
- failed-node bounds are off by default under `--bound-tt` because the first smoke profile showed only two failed-bound hits from more than two hundred thousand bound probes; `--bound-tt-failures` keeps that path available for harder-suite experiments;
- `--bound-tt-ok-only` restores the positive-bound-only probe behavior;
- `--exact-tt-only` restores the promoted exact-depth-only behavior while bound-TT validation is in progress;
- the first guarded positive-bound probe stayed correctness-clean but was not promoted because the balanced no-EP suite slowed down despite smoke/regression average improvements;
- `--profile` emits stderr JSON counters for TT probes, hits, stores, table size, node split, move-list sizes, and ordering/refutation activity, and `benchmarks\scripts\collect_e_profiles.py` stores those rows as case-labelled JSONL;
- this is correctness groundwork for later packed/bucketed TT work, not yet the final high-performance TT design.

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

### 11. Verification Harness

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

## Promotion Rule

No E search feature is promoted by intuition. A feature is promoted only after:

1. semantic correctness is unchanged;
2. PV validation is clean;
3. no-mate controls remain clean;
4. speed improves on the relevant frozen suite;
5. regressions are documented and intentionally accepted or rejected.
