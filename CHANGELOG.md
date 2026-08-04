# Changelog

Versions follow [semantic versioning](https://semver.org). The version lives in
`src/mateprover.cpp`; `CMakeLists.txt` parses it from there so the two cannot drift.

The two external contracts carry their own version numbers, documented in
`docs/OUTPUT_FORMAT.md` and `docs/PROOF_FORMAT.md`. Fields may be *added* to
either without a major bump; the meaning of an existing field will not change
without one.

## 1.0.0 — 2026-08-02

Published as **MateProver**. The project was developed under the working name
"E Chest" because it began as a reimplementation line measured against Heiner
Marxen's Chest, which it is still benchmarked against below. That name was never
suitable for release: it borrows the identity of a separate program that this
one publishes head-to-head results against. `mateprover` names what the tool is
— a prover, not a solver — which is the distinction the certificates make real.

One wire-format token moved with the name: the `--profile` diagnostic line is
prefixed `% mateprover_profile` rather than `% e_profile`. Nothing had been
released, so no consumer existed to break; the position, PV, `dm`/`acn`/`acs`
and certificate formats are untouched.

First complete version. Exact directmate prover: given a position and a depth,
it either proves a forced mate and emits a machine-checkable certificate, or
reports that no mate exists, or reports that it ran out of budget — three
outcomes it never conflates.

**Capability.** Measured on evaluation positions used once and never consulted
during development:

- mate-in-8, default configuration: **78.0%** at 15 s (200 positions). Budget
  scaling, measured under the previous default route: 80.0% at 15 s, 90.5% at
  60 s, 96.0% at 240 s.
- mate-in-10, 30 s, 32 threads, `--direct-depth`: **90.0%** (60 positions), and
  the decline with depth is gradual rather than a wall: 82.5% at mate-in-12,
  75.0% at mate-in-14, 70.0% at mate-in-16, 57.5% at mate-in-20.
- Against Chest 3.19 on the same machine, positions, memory and time cap, both
  single-threaded: 40/40 against 39/40 at mate-in-8 and about four times faster,
  37/40 against 17/40 at mate-in-10, 33/40 against 8/40 at mate-in-12.

`docs/RESULTS.md` explains where that capability comes from and what was measured
and rejected; `tools/reproduce_results.py` re-runs the figures.

**Correctness.** Every proof is a certificate verifiable by a separate program
sharing no code with the engine. 360 automated checks cover perft against
reference counts, negative controls, restriction soundness, the abort invariant
under stress, order and batching independence, the CLI contract, and six ways of
forging a certificate.

**Interfaces.** Output format and proof format are specified. Defaults are the
measured-best configuration, so a bare invocation performs like a tuned one.
`--print-config` reports the effective settings for reproducibility. An explicit
`-M` is the budget for every table alive at once, split across portfolio lanes
and `--parallel-positions` workers; it previously applied per table, so a stated
256 MB cost 1994 MB at four workers. Left unset it stays per table at the tuned
256 MB, so raising `--parallel-positions` cannot shrink it. Input
tolerates a leading UTF-8 BOM: Notepad and PowerShell's `Set-Content -Encoding
utf8` both emit one, and without this the first position of such a file failed
as `error input` while the rest of the file succeeded. `tools/verify_proof.py`
reads certificates as `utf-8-sig` for the same reason.

**Also included.** A DFPN preconditioner behind `--route dfpn`. It was long
recorded here as rejected for being slower at every depth; that was measuring a
defect, not the algorithm -- its transposition key omitted the remaining depth, so
it burned ten million nodes on a mate-in-2. Repaired -- and with two further
fixes, preconditioning only the deepest iteration and dropping per-child work
that computed a value already known -- it is now the **default route**. On freshly
minted positions it solved 90.0% of mate-in-10 against the previous default's
61.7%, gaining seventeen and losing none, and 85.5% against 83.5% at mate-in-8.
(Those are the figures from the evaluation sets current at the time. The headline
reach numbers above were later re-measured on re-minted sets; this route
comparison is left as it was measured, on one set, which is what makes it a
comparison.)

`--node-limit N` gives a deterministic budget. Wall-clock limits made every
comparison noisy at the scale of the effects being measured; a node cap gives the
same answer on every run and machine.

**Not included.** Endgame tablebases (measured: they would reach 1% of proof
nodes, and near the leaves where the subtree beneath is already almost free) and
a bitboard rewrite (measured: no concentrated hotspot to justify it). Each is
recorded with its numbers in `docs/ARCHITECTURE.md` rather than left as
an implied roadmap.
