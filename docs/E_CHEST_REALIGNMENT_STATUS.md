# E Chest Realignment Status

## Current Status

E is now aligned around a route-neutral exact prover boundary:

- `run_route` dispatches the selected route;
- `--route depth-first` selects the current exact iterative depth-first route and remains the default;
- `--route shallow-fast` is an unpromoted exact route that tries direct mate-in-1 and mate-in-2 before falling back to depth-first from the first untried depth for deeper requests;
- `route_result_is_acceptable` is the single output guard for route results;
- normal output is emitted only for a non-empty accepted proof whose reported mate depth matches the representative PV and is within the requested depth;
- proof-tree output remains opt-in through `--emit-proof` and is independently verified by `benchmarks\scripts\e_verify_proof_tree.py`.

Implemented core pieces:

- perft and perft-divide as a permanent move-generation gate, which immediately found a castling-rights soundness bug the mate corpus had missed;
- a cross-platform CMake build and an in-repo dependency-free test suite wired into CTest, with CI staged for extraction;

- root-split internal parallelism, promoted as the default, returning the lowest-index successful root move so the key move matches sequential E exactly, with cooperative cancellation that never lets an abandoned subtree be cached as a disproof;
- a shared sharded exact proof table for the parallel path;
- a probe-don't-predict parallel cost gate that keeps trivially cheap positions sequential and reuses the abort invariant to make the sequential prelude free;

- exact directmate proof kernel;
- conservative array board plus packed board key cache;
- exact keyed TT and promoted iterative TT retention;
- centralized exact proof-table probe/store helpers with explicit proof/disproof entry kinds and counters, preserving current TT semantics while giving future routes one shared access boundary;
- opt-in depth-bound TT probe;
- proof-safe handcrafted ordering and proof/refutation hint probes;
- benchmark registry pinning for the promoted E binary.

Missing target architecture pieces:

- the shared exact proof table now has generation-aged replacement and honours `-M` as an entry ceiling, but it is still `unordered_map`-backed, so the bound is an estimate rather than a hard byte bound; the final packed, open-addressed, cache-line-aware layout is still outstanding and needs PV reconstruction by replay to drop stored PVs from entries;
- native DFPN-first route;
- promoted shallow fast route; the current `--route shallow-fast` implementation is available through `chest_E_shallow_fast_probe` for validation but is not the default;
- defender-refutation route distinct from ordering hints;
- threat/mating-net route;
- typed Chest/WinChest restriction model;
- persistent worker and batch scheduler for E;
- internal route portfolio parallelism; root-split parallelism is now promoted as the default (`--threads 8 --shared-tt`, gated by `--parallel-min-nodes 500`), but portfolio-style parallelism across different routes is still absent;
- neural ordering, which remains later-stage guidance only.

## Measured Bottleneck

Profiling, not intuition, now sets the search-performance order. Removing 7.4x
of defender child-board construction (`--lazy-defender`) bought only 3-5%
sequentially and nothing in parallel. Board copying is therefore not the
dominant cost; the attack scan is. Copy-avoidance work is deprioritised and the
bitboard/incremental-attack board is prioritised, because cheap attack queries
are what E is short of.

## Revised Impact Order

1. Keep the exact proof acceptance boundary small and route-neutral.
2. Give the shared exact proof table a hard byte bound via a fixed-size open-addressed layout. Sharing, entry kinds, generation aging and an estimate-based entry ceiling are done; inline payloads, PV reconstruction by replay, and cache-line-aware buckets are not.
3. Add a shallow fast exact route for mate-in-1 and cheap mate-in-2 wins, falling back to depth-first.
4. Add native DFPN-first search with proof reconstruction through the exact verifier.
5. Add defender-refutation and threat/mating-net routes as exact route implementations, not lookup replacements.
6. Add persistent worker and batch scheduler after route outputs are stable.
7. Add optional route portfolio parallelism only after route cancellation and proof ownership are deterministic.
8. Add trace-trained neural ordering only after handcrafted traces and held-out suites show diminishing returns.

## Validation Plan

Every promoted E route or table change must pass:

- PV replay on smoke, regression, held-out, and hard-tail suites;
- proof-tree verification for all solved cases where `--emit-proof` is used;
- no-mate and negative controls with no accepted false proofs;
- A/B/C/D/E comparisons for capability and output regressions;
- route-profile checks showing the route name and zero unexpected route rejections;
- directmate oracle verification against D where available.

Current shallow-route evidence:

- smoke proof-tree verification passed for explicit `--route shallow-fast`;
- smoke profile validation showed 9 attempts, 2 shallow hits, 7 depth-first fallbacks, and zero route rejections;
- the first registry comparison on the smoke suite was PV-clean and faster than default E in average wall time;
- regression-control comparison was PV-clean with 44/44 strict success for both default E and shallow-fast, with shallow-fast effectively tied but slightly faster in average wall time;
- an interleaved 60-case balanced no-EP subset was PV-clean with 60/60 strict success for both default E and shallow-fast, with shallow-fast slightly faster in average, median, and p95 wall time;
- negative-control comparison was clean with 33/33 strict success for both default E and shallow-fast, and neither variant emitted a false `bm`;
- an interleaved 40-case hard-holdout subset was PV-clean with 40/40 strict success for both variants, but shallow-fast was slower in average, median, and p95 wall time;
- duplicate mate-in-1/2 fallback work was removed by starting fallback at depth 3 after shallow probes fail;
- the skip-duplicate fallback pass was PV-clean on smoke, regression controls, balanced60, hard-holdout40, and negative controls, and proof-tree clean on smoke;
- skip-duplicate hard-holdout40 improved shallow-fast versus default E (`259.702 ms` average vs `282.847 ms`) with 40/40 strict success for both variants;
- skip-duplicate smoke improved shallow-fast versus default E (`93.377 ms` average vs `103.386 ms`) with 9/9 strict success for both variants;
- skip-duplicate regression controls and balanced60 were correctness-clean but not consistently faster, so shallow-fast remains unpromoted pending route gating or further route work.

## Depth-Bound TT Decision

The in-progress depth-bound TT remains parked as an unpromoted experiment.

It should not become the central E table design. The realigned architecture needs a shared proof/disproof TT whose entry semantics are explicit from the start. The current `--bound-tt` path can remain useful for measurements, but promoted E should keep `--exact-tt-only` semantics until the shared table is implemented and validated.
