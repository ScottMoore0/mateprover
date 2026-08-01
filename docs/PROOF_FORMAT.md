# echest Proof Certificate Format

Version 1. This document specifies the certificate emitted by `--emit-proof`
precisely enough to write an independent verifier without reading the engine.

The point of the certificate is that you should not have to trust the prover.
A verifier written from this document, using any chess library, can establish
the engine's claim from the position alone.

## Wire format

With `--emit-proof`, a solved position's output line gains a `proof` token:

```
<fen4>; acn <nodes>; acs <seconds>; bm <move>; dm <depth>; pv <moves>; proof <json>
```

`<json>` runs to the end of the line and is a single JSON value with no
newlines. Unsolved positions carry no `proof` token. The trailing `;` after the
JSON is part of the line, not the JSON.

## Grammar

```
AttackerNode := { "a": Move, "mate": true }        // mating move, a leaf
              | { "a": Move, "d": [ Reply, ... ] } // move, then every defence
Reply        := { "r": Move, "p": AttackerNode }
Move         := UCI coordinate string, e.g. "e2e4", "e7e8q"
```

The root of a certificate is an `AttackerNode` and the side to move in the
position is the attacker. Promotion pieces are lowercase (`q`, `r`, `b`, `n`).

Both forms carry `"a"`. A node has *either* `"mate": true` *or* `"d"`, never
neither. Unknown keys may be present and must be ignored; readers must not
assume `"d"` is absent merely because `"mate"` is.

The order of entries in `"d"` is unspecified and carries no meaning. Compare
reply sets, never sequences.

## What a verifier must check

Replaying from the given position, at every `AttackerNode`:

1. `"a"` parses as a move and is **legal** in the current position.
2. If `"mate": true`, the position after `"a"` is **checkmate**. This is a leaf.
3. Otherwise `"d"` is present and non-empty, and the multiset of `"r"` values is
   **exactly** the legal replies after `"a"` — no reply missing, none listed
   that is not legal, and none listed twice.
4. Each `"p"` is recursively a valid `AttackerNode` in the position after `"r"`.

An empty `"d"` is never valid. A position with no legal replies is mate or
stalemate, and mate must be stated as a leaf with `"mate": true` so that it is
checked. This is not pedantry: a verifier that accepts an empty reply list will
accept a **stalemate** as a forced mate, because "the listed replies are exactly
the legal replies" is vacuously true when both are empty, and the recursion then
adds a ply and succeeds. The reference verifier had exactly this hole until the
obligation was written down here.

Obligation 3 is the one that matters. A proof of a forced mate is a claim about
*every* defence, so a certificate that quietly omits an inconvenient reply would
otherwise verify. Any verifier that checks only that the listed lines end in
mate is not checking the thing being claimed.

The depth of a certificate is the number of attacker moves on its longest line.
It must equal the `dm` value on the same output line.

## What the certificate does and does not claim

It claims: **from this position the attacker can force mate in at most N**,
where N is the certificate's own depth, against every legal defence.

It does **not** claim minimality. Nothing in the structure rules out a shorter
mate. When the engine runs in its default iterative-deepening mode the reported
`dm` is minimal, but that follows from the search discipline -- no depth is
reported before every shorter depth has been refuted -- and is not a property a
verifier can confirm from the certificate alone. Under `--direct-depth` the
engine searches the requested depth directly and minimality is not claimed at
all.

A proof found under a restriction (`-C`, `-K`, `-P`, `-X`, `-R`) is an ordinary
proof and carries no trace of the restriction: a restriction only removes
attacker options, so any mate found under one is a mate outright. The output
line names the restriction with a `via` field, but the certificate needs no
special handling and is verified identically.

It does not claim that the proof is the only one, or a canonical one. Under
`--portfolio-parallel` the particular proof returned may differ between runs of
the same position; each is independently valid.

## Reference verifier

`tools/verify_proof.py` implements exactly the obligations above using
python-chess, in about 210 lines. It reads engine output on stdin and exits
non-zero if any certificate fails:

```
echest --emit-proof -z 5 - < positions.epd | python tools/verify_proof.py -
```

It is deliberately a separate program from the engine, sharing no code with it,
so that agreement between the two is evidence rather than tautology. The test
suite forges genuine certificates in six distinct ways -- omitting a defence,
inventing an illegal one, listing one twice, claiming a non-mating leaf is mate,
making the attacker's move illegal, emptying a reply list -- and requires the
verifier to reject each. A separate check confirms that a stalemate presented as
a mate is rejected by the node logic itself, not merely by the principal-variation
check that happens to sit alongside it.

## Versioning

This is version 1. The grammar above is the compatibility surface. Additive
changes (new optional keys) will not change the version; any change to the
meaning of an existing key, or to the obligations, will.
