# Changelog

Versions follow [semantic versioning](https://semver.org). The version lives in
`src/echest.cpp`; `CMakeLists.txt` parses it from there so the two cannot drift.

The two external contracts carry their own version numbers, documented in
`docs/OUTPUT_FORMAT.md` and `docs/PROOF_FORMAT.md`. Fields may be *added* to
either without a major bump; the meaning of an existing field will not change
without one.

## 1.0.0 — unreleased

First complete version. Exact directmate prover: given a position and a depth,
it either proves a forced mate and emits a machine-checkable certificate, or
reports that no mate exists, or reports that it ran out of budget — three
outcomes it never conflates.

**Capability.** Measured on evaluation positions used once and never consulted
during development:

- mate-in-8, default configuration: 80.0% at 15 s, 90.5% at 60 s, 96.0% at 240 s
  (200 positions).
- mate-in-10, 30 s, 32 threads, `--direct-depth`: 73.3% with the restriction
  portfolio against 48.3% without (60 positions).

`docs/RESULTS.md` explains where that capability comes from and what was measured
and rejected; `tools/reproduce_results.py` re-runs the figures.

**Correctness.** Every proof is a certificate verifiable by a separate program
sharing no code with the engine. 224 automated checks cover perft against
reference counts, negative controls, restriction soundness, the abort invariant
under stress, order and batching independence, the CLI contract, and six ways of
forging a certificate.

**Interfaces.** Output format and proof format are specified. Defaults are the
measured-best configuration, so a bare invocation performs like a tuned one.
`--print-config` reports the effective settings for reproducibility.

**Not included.** Endgame tablebases (measured: they would reach 1% of proof
nodes, near the leaves), a DFPN route (measured: slower at every depth), and a
bitboard rewrite (measured: no concentrated hotspot to justify it). Each is
recorded with its numbers in `docs/E_CHEST_ARCHITECTURE.md` rather than left as
an implied roadmap.
