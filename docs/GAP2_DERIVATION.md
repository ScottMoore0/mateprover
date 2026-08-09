# GAP-2: perpetual-check refutation for a lone-queen defender — derivation

**Status: derivation only. No code depends on this document yet.**

Written and committed before the implementation, as GAP-1's was, and for the same
reason: this feeds the `Refuted` verdict, so an error here makes the engine
declare a sound problem unsolvable.

The specification states the theorem and then says: *"Do that. Set up a board."*
This document is that exercise. **The sketch does not survive it unmodified.**
Three corrections follow, one of which is a soundness bug in the sketch as
written.

Provenance: **P-CHESS-THEORY**. The argument below is about chess and can be
reconstructed at a board.

---

## 1. The claim

In a selfmate the attacker must force the defender to deliver mate. The
specification's claim: where the defender is to move with exactly king and queen
and has a checking queen move meeting a list of local conditions, the defender
can check forever, so the attacker can never force mate, at any depth.

The core of it is sound and is worth stating on its own, because everything else
depends on it:

> **Lemma (capturing the queen is fatal).** In a selfmate, if the defender is
> reduced to a bare king, the attacker can never win. A lone king cannot give
> check, mate requires check, so the defender can never deliver mate. The
> defender's material never increases — he has no pawn to promote — so this is
> permanent.

So the attacker will not capture the queen: doing so loses on the spot. That
much is exactly right, and it is what makes the rest possible.

---

## 2. Correction 1 — the local conditions are not sufficient

The sketch argues that after the attacker's king moves, *"the queen can check
again from a square that restores the previous relationship — the geometry that
made the first check available is reproducible because the attacker's king has
only moved to an adjacent square and the queen's mobility dominates that
neighbourhood."*

**This does not follow.** The attacker has other pieces — the defender is limited
to king and queen, the attacker is not. The attacker's king may step to a square
where every queen line to it is blocked by the attacker's *own* men. Queen
mobility dominating a neighbourhood on an empty board says nothing about a
neighbourhood walled in by four pawns.

If the defender cannot check on some reachable square, the attacker is no longer
forced to reply, is free to manoeuvre, and may well force the mate. The perpetual
therefore has to hold over **every position the attacker's king can reach**, not
merely the one in front of us. A condition on a single move cannot establish a
property of an infinite play.

**The claim is about a region, not a move.** It is a greatest-fixed-point
property — precisely the shape GAP-1's derivation identified as the one the
verdict lattice cannot obtain by composition, which is why it has to arrive as
an axiom.

---

## 3. Correction 2 — the region is finite, and that is what makes it computable

The repair is also what makes the theorem cheap, and it comes from an observation
the sketch does not make:

> **If the attacker is in check after every defender move, and cannot block and
> will not capture, then every attacker move is a king move — so every other
> attacker piece is FROZEN for the entire line.**

The position is then fully described by three squares: the attacker's king, the
defender's queen, the defender's king. Everything else is constant. The state
space is bounded by 64³, and in practice by the handful of squares the attacker's
king can actually reach.

So the theorem becomes a closure computation on a small graph.

> **Theorem.** Let *S* be a set of positions with the defender to move, all
> sharing one fixed placement of the attacker's non-king men. Suppose for every
> *p* ∈ *S* there is a legal defender queen move *m* giving check such that:
>
> 1. *m* is **not** mate — the attacker has at least one legal reply;
> 2. every legal attacker reply to *m* either
>    a. captures the defender's queen (attacker loses, by the Lemma), or
>    b. is a king move to a position in *S* in which the defender is **not** in
>       check.
>
> Then from any *p* ∈ *S* the defender can check forever without ever mating, and
> the attacker cannot force the selfmate at any depth.

*Proof.* Condition 1 keeps the defender from delivering the mate the attacker
wants. Condition 2 keeps every continuation inside *S*, where 1 applies again. By
induction the play never terminates in a mate delivered by the defender, so no
finite depth contains a solution. Branch 2a ends the game immediately in the
attacker's favour — that is, against him. ∎

Two details the sketch handles by hand and this handles by construction:

- **Pins.** The sketch requires the queen be unpinned, and unpinned even with the
  attacker's king removed. Generating the defender's *legal* moves settles pins
  exactly, with no special case.
- **Discovered check against the defender.** The sketch requires the attacker's
  king be unable to discover check. Condition 2b states it directly — if a reply
  leaves the defender in check, the defender must answer the check instead of
  giving one, and the closure fails.

---

## 4. Correction 3 — this must NOT be applied to selfstalemate

The specification says the predicate is for *"selfmate and selfstalemate
searches."* **That is unsound for selfstalemate**, and it is the one error here
that would produce wrong answers rather than missed ones.

The entire argument rests on the Lemma: capturing the queen is fatal because a
bare king cannot mate. **A bare king can perfectly well stalemate.** Under a
selfstalemate goal the attacker is trying to be *stalemated*, which needs no
check and no material at all — the attacker may be delighted to capture the
queen. Branch 2a of the theorem collapses, the attacker is not forced to keep
moving his king, and the perpetual evaporates.

The predicate is therefore restricted to `Goal::Selfmate`. Goal scope is checked
first, before material.

---

## 5. What it is worth, measured before building

The specification asks for this check before any code. Defender material across
the 904-position selfmate corpus:

| defender | roots | note |
| --- | --- | --- |
| king + pawn | **127** | the largest class by a distance |
| king + 2 pawns | 60 | |
| king + rook | 48 | |
| **king + queen** | **42** | GAP-2's target; 22 unsolved at import |
| king + bishop | 35 | |

And of the 14 selfmate positions still unresolved after independent
adjudication, **one** has a king+queen defender and **three** have king+pawn.

So the class is real but it is not the big one, and the corpus confirms what the
clean-room reply argued: **king+pawn is the larger and harder half**. GAP-2 is
worth doing — 42 roots, 22 of them unsolved, plus every mid-search position that
reaches king+queen after captures, which the root count cannot see. It is not,
on this evidence, the explanation of the residue.

The perpetual-check argument does not transfer to king+pawn at all: a lone pawn
cannot check repeatedly, so there is no perpetual to find. That theorem is a
different one and is not attempted here.

---

## 6. A second axiom, free on the way past

The Lemma is worth installing on its own, independently of the closure:

> In a selfmate, a **bare defender king** means no solution at any depth.

Zero roots in the corpus have it, which is why it looks worthless — but it is
reached constantly *mid-search*, every time a line captures the defender's last
piece, and each such node is currently searched to the depth limit at every
iteration. It is also the Lemma the closure leans on, so having the engine know
it directly keeps the two consistent.

---

## 7. Obligations on the implementation

- **Goal scope first**, before material: selfmate only, never selfstalemate.
- The closure must be **bounded**, and must return "no refutation" when it hits
  the bound. Running out of budget is not a proof; this is GAP-1's rule again.
- Any attacker reply that is neither a queen capture nor a king move — a block, a
  capture of some other piece — fails the closure. Conservative and correct.
- The predicate must **never fire** on a selfmate with a known solution. That is
  a critical-bug test, not a regression test.
- It must be gated, default off, behind the same flag as GAP-1's axioms.
