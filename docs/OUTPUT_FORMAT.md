# echest Output Format

Version 1. One line of stdout per input line, in input order. This document
specifies what a consumer may rely on.

## Shape

A result line is a sequence of `;`-terminated fields:

```
<fen4>; acn <nodes>; acs <seconds>[; <result fields>];
```

The first field is the position, normalised to four FEN fields. `acn` is nodes
searched, `acs` is wall-clock seconds. Both are always present. Fields after
`acs` depend on the outcome, and always appear in the order given below.

## The four outcomes

A consumer must distinguish exactly four cases. They are mutually exclusive.

**1. Proved.** A forced mate was found and accepted:

```
<fen4>; acn N; acs S; bm <move>; dm <depth>; pv <move ...>[; proof <json>][; via <name>];
```

`bm` is the key move, `dm` the mate depth, `pv` the principal variation as
space-separated UCI moves. `proof` appears only under `--emit-proof` and is
specified in [PROOF_FORMAT.md](PROOF_FORMAT.md). `via` appears only when a
restricted portfolio lane proved it, naming the restriction; its absence means
the unrestricted search proved it.

**2. Disproved.** The search completed and there is no mate within the requested
depth. The line **ends after `acs`**, with no further fields:

```
<fen4>; acn N; acs S;
```

This is a positive claim, not a non-answer. It is expressed by the *absence* of
any marker, which is worth stating plainly because it is the one case a naive
consumer will get wrong.

**3. Gave up.** The budget expired before the question was settled -- either the
wall-clock budget of `--time-limit` or the node budget of `--node-limit`:

```
<fen4>; acn N; acs S; timeout;
```

Nothing is claimed. In particular this is **not** a disproof, and treating it as
one is the single most damaging misreading of this format: it converts "I do not
know" into "there is no mate".

**4. Bad input.** The line could not be parsed as a legal position:

```
<original line>; acn 0; acs 0; error input;
```

The original text is echoed unchanged. Positions are rejected for being
unparseable *or* illegal -- two kings per side, a side not to move standing in
check, pawns on the back rank, and so on -- so a rejected line is not
necessarily malformed, it may be impossible.

## What decides disproof versus timeout

Only the unrestricted search can settle case 2. The portfolio's restricted lanes
are sound but **incomplete**: a restriction removes attacker options, so a mate
found under one is real, but failing to find one under a restriction proves
nothing at all. A lane running out of time therefore says nothing about whether a
mate exists.

The engine reports case 2 only when the unrestricted search itself ran to
completion. This is a real hazard rather than a hypothetical one: when the
portfolio became the default, every genuine disproof was briefly reported as a
timeout because any lane's failure was being treated as the search running out
of time.

## Persistent service

The engine may be kept running and fed positions one at a time. Each result line
is flushed as it is produced, so a client that writes one position and waits
receives the answer before sending the next; there is no protocol beyond the
line format above, and no restart cost between positions.

Answers do not depend on batching or on order. A position produces the same
result whether it is sent alone, first in a batch, or last, because each position
is searched with fresh state -- nothing is carried between them. That is gated,
not merely intended.

The consequence of the same property is that a long-lived process gains nothing
from work already done: there is no cross-position cache to warm. Restarting per
position costs only process startup.

It also means positions can be solved concurrently. `--parallel-positions N`
solves N at a time and emits each result, in input order, as soon as it is ready
-- a slow position delays the results behind it but not the ones before it. It is
off by default because a position's answer can then be held back waiting for an
earlier one, which service mode depends on not happening.

Pair it with `--node-limit` rather than `--time-limit` for corpus work. A node
budget is per-position, so batching costs nothing: eight at a time gives 2.2x
throughput with **identical** answers. A wall clock is shared, so positions
competing for cores each get less of it -- 4.1x throughput, but a few deep
positions fall out (34, 35).

## Other modes

`--perft N` emits one line per depth, and `--perft-divide N` emits `move count`
pairs followed by `total <count>`:

```
<fen4>; perft <depth>; nodes <count>; acs <seconds>;
<original line>; perft error input;
```

`--list-legal` emits the legal moves:

```
<fen4>; legal_count <n>; legal <move ...>;
<original line>; legal_count 0; error input;
```

`--profile` writes one `% e_profile {json}` line per position to **stderr**.
Those counters are diagnostics, not a stable interface, and may change without
a version bump. Everything on stdout is covered by this document.

## Stability

Fields may be **added** to a result line without a version bump; a consumer must
ignore unknown fields rather than fail. The meaning of an existing field, the
four-outcome distinction, and the field order will not change without one.

Input lines beginning with `#` are comments and produce no output line, so
consumers matching outputs to inputs positionally must skip them as well.
