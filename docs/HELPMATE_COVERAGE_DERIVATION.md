# The helpmate flight-square bound — derivation

**Status: derivation only. No code depends on this document yet.**

Written before the implementation, as GAP-1's and GAP-2's were, and for a sharper
reason than either: 74 shipped an *unsound* version of the weaker bound that
every existing gate passed, and the error was in a modelling assumption stated in
a comment rather than in any line of logic. So the modelling assumptions are
written out here first, where each one can be checked against the question "is
this a superset of what a real board allows?"

Provenance: **P-CHESS-THEORY**.

---

## 1. What is already there, and why it is weak

The bound in 73/74 asks one question:

> Can any mating-side unit, other than the king, attack any square the mated king
> could reach, within the moves remaining?

If not, no mate is possible and the subtree is dead. It is sound and it is worth
about eleven positions of the 546. It is weak because **giving check is a small
part of delivering mate**. A rook that can reach a checking square but can do
nothing about the king's eight flight squares still cannot mate, and the bound
happily lets that subtree run.

## 2. The obvious strengthening, and why it is invalid

The natural next step is to count: the king's flight squares must each be
attacked or blocked, so count the unhandled ones and require that many moves.

**This is invalid.** A single move can handle several flight squares at once — a
queen arriving beside the king can cover three or four of them in one move, and a
discovered line can cover more without the covering piece moving at all. "One
move per unhandled square" is not a lower bound, it is a guess, and a bound that
is not a lower bound prunes real solutions.

There is no useful constant either. A queen attacks up to 27 squares, so
"one move covers at most c squares" is true only for uselessly large c.

**So do not count moves. Ask a different question.**

## 3. The move that makes it work: containment, not counting

The counting question is "how many moves would covering cost?". Replace it with

> **Is covering possible at all?**

That is a *set containment* question, and containment is exactly what an
empty-board relaxation answers soundly. Nothing has to be counted, and the
combinatorial difficulty disappears.

### The theorem

Fix a node with the mated side to move or not, `w` moves remaining for the mating
side and `b` for the mated side. Write:

- **A(w)** — squares some mating unit **other than the king** can ATTACK within
  `w` of its own moves, on an empty board. (The king is excluded because a king
  cannot give check; a discovered check is delivered by the piece whose line
  opens, not by the king that vacated.)
- **U(w)** — squares some mating unit, **including** the king, can attack within
  `w` moves. The king cannot check, but it can perfectly well cover a flight
  square, so it belongs here and not in A.
- **Rm(w)** — squares some mating unit can OCCUPY within `w` moves.
- **Rd(b)** — squares some mated-side unit can OCCUPY within `b` moves.
- **O** — squares occupied right now.
- **D(b)** — squares within `b` king-steps of the mated king.

Define the **handled set**

    H = U(w) ∪ Rm(w) ∪ Rd(b) ∪ O

and for a square k let flights(k) be its on-board neighbours.

> **Theorem.** If for every k ∈ D(b) ∩ A(w) some flight of k lies outside H, then
> no mate exists from this node at any continuation of this length, and the
> subtree is dead.

### Proof

Suppose a mate exists, with the mated king finally on square k.

The king ends within `b` king-steps of where it stands, so k ∈ D(b). It is in
check, and the checker is not a king, so k ∈ A(w). Hence k ∈ D(b) ∩ A(w).

Now take any flight f of k. In the final position the king cannot move to f, so
one of these holds:

- f is attacked by the mating side — then f ∈ U(w);
- f holds a mating unit — then f ∈ Rm(w), or f ∈ O if it never moved;
- f holds a mated unit — then f ∈ Rd(b), or f ∈ O if it never moved.

Every case puts f ∈ H. So all of flights(k) ⊆ H, contradicting the hypothesis. ∎

Note what the proof did **not** need: how many moves the covering costs, which
unit covers which square, or whether one move covers several. Containment does
not care.

## 4. Every relaxation, and its direction

The theorem is only as good as the claim that each set is a **superset** of the
truth. Each one, and the direction it errs in:

| set | relaxation | direction |
| --- | --- | --- |
| A, U, Rm, Rd | computed on an empty board | superset — blockers shorten a slider's reach and obstruct a route, they never create one |
| Rd(b) | the mated king's own moves are not deducted from `b` | superset — the real budget for other units is smaller |
| D(b) | the king is given `b` moves even though other units may use them | superset |
| O | a currently occupied square counts as handled forever | permissive — it may empty, but treating it as handled only *prevents* a prune |
| a mating unit on f counts as handling f | ignores that the king may capture it if undefended | permissive |

Every one errs toward **not pruning**. That is the only safe direction: a bound
that is too loose costs time, a bound that is too tight costs correctness.

**And the trap from 74, restated so it is not walked into twice.** A pawn's
relaxed move set must include its diagonal captures. On an empty board a pawn has
nothing to capture — but the table models what a pawn may do on a REAL board,
where it captures sideways and changes file. Omitting the diagonals makes the
pawn sets an *underestimate*, which is the one direction that makes the whole
thing unsound. This is exactly the bug 74 found, and the new sets Rm and Rd have
the same exposure.

## 5. Cost

All of it is bitboards.

    H          one OR per unit, computed once per node
    candidates D(b) & A(w)
    test       for each candidate k: (flights(k) & ~H) == 0

`flights(k)` is the existing king table. The candidate loop is a popcount walk
over what is usually a handful of squares, and it exits on the first survivor.

The `w ≤ 3` restriction stays. Extending the table to five moves was measured in
74 and rejected: the nodes it newly covers are shallow, which are few, and the
test costs more there than the rare prune returns.

## 6. What it should be worth, and how it will be judged

The weak bound is worth eleven positions of 546 single-threaded. This one adds
the flight-square requirement, which is where most of the work of a mate lives,
so it should prune considerably more — but 73 predicted a gain and lost eight
positions instead, so no number is claimed here.

Judged by the gate 74 established, which is now the standard for anything that
can only lose solutions silently:

- an explicit on/off switch, read **once** into a static — read per node, `getenv`
  cost more than the bound saved and moved the result by 39 positions;
- the whole 546-position corpus, both settings;
- compare solved **sets**, not counts;
- **zero lost** is the pass condition. Any loss means unsound, and it comes out.

A consequence test on a sample is not sufficient. That is what 73 used, and it
caught 74's unsoundness by a margin of one position against an arbitrary
threshold.
