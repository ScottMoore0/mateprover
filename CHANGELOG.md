# Changelog

Versions follow [semantic versioning](https://semver.org). The version lives in
`src/mateprover.cpp`; `CMakeLists.txt` parses it from there so the two cannot drift.

The two external contracts carry their own version numbers, documented in
`docs/OUTPUT_FORMAT.md` and `docs/PROOF_FORMAT.md`. Fields may be *added* to
either without a major bump; the meaning of an existing field will not change
without one.

## Unreleased

**The cooperative split now runs two plies deep.** Splitting on the root move
alone made as many parallel tasks as there were root moves — around thirty — and
cooperative subtrees are wildly uneven, so one task held most of the work and the
other threads idled. Sixteen threads were SLOWER than four. Pairing each root move
with each reply gives hundreds of tasks instead of tens: 20.1 s to 6.2 s on a hard
h#4 at the default thread count, which is 6.0x against a single thread and matches
the scaling the same search already achieved on workloads with no early exit.

It also brings the answer closer to the sequential one, since lexicographic
(first, second) order is the order a sequential depth-first search visits those
subtrees in.

**Meet-in-the-middle for the cooperative goals is rejected**, on measurement
rather than on taste. A backward frontier has to start from an explicit goal set,
and checkmate is a predicate rather than a state, so the mate positions must be
enumerated first: 1.3x10^10 placements at six men against a forward search of
2.3x10^7 states, and roughly 10^13 at the corpus median for h#4. Where the goal
set is cheap to enumerate the forward search is already instant, and where the
forward search needs help the goal set cannot be enumerated. `docs/ARCHITECTURE.md`
§66 has the numbers. The retrograde generator added below keeps its value on its
own terms; it has no bidirectional search to carry.


**Retrograde move generation** (`--list-unmoves`). Given a position, lists every
position from which one legal move reaches it. This is groundwork for a
bidirectional cooperative search, and is shipped separately from it because the
generator has to be measured before anything rests on it.

The guarantee is exactly one ply and it holds in both directions: every emitted
predecessor is a legal position with a legal move to the target, and every such
predecessor is emitted. Both are gated by tests against an independent adjudicator
(`python-chess`) rather than by the engine agreeing with itself. It does not
decide whether a predecessor is reachable from the initial array — 0.7% of output
is legal one ply back and impossible overall — which `docs/ARCHITECTURE.md` §65
states as the contract rather than leaving to be discovered.

The first version of this passed a 96% completeness round-trip while emitting
predecessors that a knight had reached by sliding down a file: it replayed the
retracted move without first checking the move was legal, and a completeness
test is structurally blind to a spurious predecessor. Both properties are now
measured. Castling, castling-rights forfeiture, en-passant capture, and
en-passant squares that no pawn can use are handled; the last of these is a
disagreement about what a position is, and is resolved in favour of the FEN
convention the corpora use.

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
sharing no code with the engine. 412 automated checks cover perft against
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
