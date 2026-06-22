# E Chest Realignment Status

## Current Status

E is now aligned around a route-neutral exact prover boundary:

- `run_route` dispatches the selected route;
- `--route depth-first` selects the current exact iterative depth-first route and remains the default;
- `route_result_is_acceptable` is the single output guard for route results;
- normal output is emitted only for a non-empty accepted proof whose reported mate depth matches the representative PV and is within the requested depth;
- proof-tree output remains opt-in through `--emit-proof` and is independently verified by `benchmarks\scripts\e_verify_proof_tree.py`.

Implemented core pieces:

- exact directmate proof kernel;
- conservative array board plus packed board key cache;
- exact keyed TT and promoted iterative TT retention;
- opt-in depth-bound TT probe;
- proof-safe handcrafted ordering and proof/refutation hint probes;
- benchmark registry pinning for the promoted E binary.

Missing target architecture pieces:

- shared proof/disproof TT as the primary table design;
- native DFPN-first route;
- shallow fast route distinct from the current depth-first route;
- defender-refutation route distinct from ordering hints;
- threat/mating-net route;
- typed Chest/WinChest restriction model;
- persistent worker and batch scheduler for E;
- internal route portfolio parallelism;
- neural ordering, which remains later-stage guidance only.

## Revised Impact Order

1. Keep the exact proof acceptance boundary small and route-neutral.
2. Replace route-local TT experiments with a shared proof/disproof table design.
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

## Depth-Bound TT Decision

The in-progress depth-bound TT remains parked as an unpromoted experiment.

It should not become the central E table design. The realigned architecture needs a shared proof/disproof TT whose entry semantics are explicit from the start. The current `--bound-tt` path can remain useful for measurements, but promoted E should keep `--exact-tt-only` semantics until the shared table is implemented and validated.
