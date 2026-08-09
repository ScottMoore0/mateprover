# GAP-1: the any-depth verdict lattice — derivation

**Status: derivation only. No code depends on this document yet.**

This is written and committed *before* the implementation, deliberately. GAP-1 is
the one change in the parity analysis that can make the engine produce a **wrong
answer** rather than a slow one: an unsound "no solution at any depth" makes it
confidently declare sound problems unsolvable. The specification supplies the
requirement and explicitly leaves the design here; so the reasoning is recorded
first, where it can be checked independently of whether the code happens to pass
its tests.

Provenance: **P-INFERRED** that a competitor benefits from such a verdict class,
**P-CHESS-THEORY** that such positions exist. Nothing below is taken from any
other program; it is a fixed-point argument about a reachability game.

---

## 1. The problem

Every verdict this engine produces is depth-bounded. A search either proves a
solution within N or fails within N. There is no representable verdict meaning
*no solution exists at any depth*.

That costs time under iterative deepening: a position that is provably unsolvable
at every depth is re-searched from scratch at depth 1, 2, 3, … each time to
exhaustion, until the clock runs out. The engine then reports "not found within
budget", which is both weaker and slower than the truth.

It also blocks GAP-2, GAP-3 and GAP-4, each of which is a static theorem whose
conclusion is precisely *unsolvable at any depth*. Without somewhere to put that
conclusion, a theorem that fires buys nothing: the next iteration re-derives it.

---

## 2. The game, stated precisely

Fix a goal G (directmate, selfmate, …). Positions form a directed graph; edges are
legal moves. Two kinds of node:

- **OR nodes** — the attacker moves. The attacker chooses.
- **AND nodes** — the defender moves. The defender chooses.

Cooperative goals (helpmate, helpstalemate) have no AND layer at all: both sides
are OR nodes. Everything below still applies, with the AND rule simply never used.

Define the attacker's objective as *reaching a terminal state satisfying G*.
Write **W** for the **winning region**: the set of nodes from which the attacker
can force a G-terminal in some finite number of plies.

W is the least fixed point of

- OR node *x* ∈ W ⟺ **some** successor of *x* is in W (or *x* is itself a
  G-terminal);
- AND node *x* ∈ W ⟺ **every** successor of *x* is in W (and *x* has at least
  one successor — a node with none is terminal and settled by G directly).

*Least* is the load-bearing word. The attacker must reach the goal in **finite**
time, so membership of W must be well-founded: each node's membership is
justified by successors whose membership is justified by theirs, terminating at
G-terminals. An infinite regress is not a win.

**The complement.** Write **A = ¬W**, the nodes from which the attacker can
*never* force G. A is the **greatest** fixed point of the dual operator, and it
is exactly the verdict GAP-1 wants:

> *disproved at every depth* at node *x* ⟺ *x* ∈ A.

Two things follow immediately, and both matter.

**(a) Depth exhaustion is not membership of A.** "I searched to depth N and found
nothing" says nothing about depth N+1. Running out of depth, nodes, or clock must
**never** yield the absorbing verdict. This is the single most important rule in
the design, and the easiest to violate by accident, because the code path for
"failed" and the code path for "impossible" meet at the same `return`.

**(b) Cyclic justification is valid for A and invalid for W.** Because A is a
*greatest* fixed point, a defender who shuffles forever inside a region
containing no G-terminal is genuinely avoiding the goal forever — the cycle is
the proof. Because W is a *least* fixed point, the attacker may not argue "x is
won because y is won because x is won"; that is circular and unsound.

This asymmetry is the trap. It is discussed further in §5.

---

## 3. The lattice

    Verdict ::= Proved(d)        the goal is forced, in d plies
              | Disproved(d)     no solution within d plies (says nothing beyond d)
              | Refuted          no solution at ANY depth        [absorbing]
              | Unknown          budget exhausted; no claim made

Ordering, by *information content*:

    Unknown  <  Disproved(d)  <  Disproved(d')  <  Refuted      for d < d'

`Proved(d)` is not comparable to the disproof chain; a node is one or the other,
never both, and a search that derives both has a bug.

`Refuted` is the top of the disproof chain, and **absorbing**: once a node is
Refuted it is Refuted for every depth, for every later iteration, forever.

`Unknown` is the bottom. It is what an aborted search records — which is the
existing abort invariant, unchanged and now expressible: *an aborted search
records no verdict* is exactly *an aborted search records Unknown*.

---

## 4. Composition, and why each rule is what it is

Let `succ(x)` be the legal successors of x.

### OR node (attacker to move)

    x is Refuted  ⟸  succ(x) ≠ ∅  ∧  every s ∈ succ(x) is Refuted
                     ∧  x is not itself a G-terminal

*Why.* The attacker achieves G from x only by choosing some successor from which
he achieves G. If from every successor he can never achieve it, he can never
achieve it from x. If `succ(x)` is empty the node is terminal and G decides it
directly — an OR node with no moves is not "refuted by composition", it is
settled.

### AND node (defender to move)

    x is Refuted  ⟸  some s ∈ succ(x) is Refuted

*Why.* The attacker achieves G from x only if he achieves it after **every**
defender reply. One reply from which he can never achieve it is a permanent
escape, and the defender will take it. Note the asymmetry with the OR rule: one
witness suffices here, all successors are needed there. Getting these the wrong
way round produces unsound "no solution" claims, which is the failure mode the
specification calls far worse than a slow one.

### Everything else

    x is Refuted  ⟸  a gated static theorem proves it        (GAP-2, GAP-3, GAP-4)

These are the **axioms** — the only way `Refuted` enters the system from outside
composition. Each is a separate theorem with its own proof obligation and its own
flag.

**No other rule may produce Refuted.** In particular, `Disproved(d)` never
becomes `Refuted` however large d is, and no accumulation of failed iterations
does either.

---

## 5. The cycle hazard, and the conservative rule

§2(b) says cyclic justification is *sound* for A. That is true of the fixed point
and **not** true of a naive depth-first search with memoisation, which is what
this engine is.

Consider a DFS that is expanding x, descends to y, and y transposes back to x.
x's verdict is not yet known, so the table holds nothing for x. If the
implementation treats "no entry" or "in progress" as anything other than
`Unknown`, y may conclude something about x's status and hand it back — and x
will then justify itself with a value derived from itself. For the *proof*
direction this is the classical graph-history-interaction problem. For the
*refutation* direction it is worse, because the answer looks like a theorem.

The fixed-point argument in §2 licenses the cycle only when the whole region is
resolved together, as a greatest fixed point. A DFS resolves nodes one at a time,
in stack order, and does not compute that.

**Rule (conservative).** A node currently on the search stack has verdict
`Unknown` for all purposes. When composing a node's verdict, a child whose result
was obtained from — or depends on — a node on the current stack contributes
`Unknown`, never `Refuted`.

Consequences, stated honestly:

- **Sound.** Every `Refuted` is then justified by a well-founded derivation
  bottoming out in axioms. No circular derivation survives.
- **Incomplete.** Genuine forever-avoidance that is *only* provable by a cyclic
  argument — the defender shuffling in a closed region — will not be derived by
  composition. It has to come from an axiom instead. This is precisely why GAP-2
  is a separate theorem rather than a consequence of the lattice: perpetual check
  is the cyclic case, and the lattice deliberately cannot see it.

Incomplete is the correct trade. A missed refutation costs time; an invented one
costs correctness.

---

## 6. Storage

The verdict lives in the same table as today, under the same key, which already
includes the goal. Goal-awareness therefore comes for free and must be kept:
*unsolvable at any depth* for a directmate is a different statement from the same
words about a selfmate, and they must not share an entry.

Two requirements on the entry:

1. **Refuted dominates.** Merging a new result into an existing entry:
   `Refuted` ⊔ anything = `Refuted`. A deeper search may not overwrite `Refuted`
   with `Disproved(d)`, and must not overwrite it with `Proved(d)` either — if
   that ever happens, one of the two is a bug, and the merge must fail loudly
   rather than pick a winner.
2. **Refuted survives table ageing.** Depth-bounded entries may be evicted or
   aged out; a `Refuted` entry is a theorem and evicting it merely costs the work
   of re-deriving it. That is acceptable, but it must never be *downgraded* in
   place.

---

## 7. Why the inert case is byte-identical by construction

The acceptance criterion is: with the theorems disabled, no behaviour change —
byte-identical verdicts across the 903-position selfmate corpus and the
200-position mate-in-8 set.

That is obtained structurally, not by testing:

> `Refuted` enters the system **only** through gated axioms (§4). With every gate
> off, no node is ever axiomatically `Refuted`. The composition rules of §4 all
> require at least one `Refuted` child. By induction on the search, no node ever
> becomes `Refuted`, so no branch is ever cut, no table entry ever differs, and
> the search executes exactly as before.

The test still gets written — a structural argument is a reason to expect the
result, not a substitute for observing it — but the argument is what makes the
result trustworthy rather than lucky.

---

## 8. What this changes, and in what order

1. Verdict type and the lattice operations. Inert.
2. Table entry, merge rule, ageing rule. Inert.
3. Composition at OR and AND nodes, plus the on-stack rule of §5. Inert, because
   nothing produces the axiom.
4. Iterative deepening: a `Refuted` root stops the loop instead of trying the next
   depth. This is the first place the lattice pays, and it pays only once an axiom
   exists.
5. The first axiom: **a bare attacker king cannot mate** (GAP-3, item 1). It is
   trivially sound, costs one popcount, and is the right first customer because it
   exercises every rule above while being impossible to get wrong.
6. GAP-2's perpetual-check theorem, which is the cyclic case §5 deliberately
   cannot derive, and the reason the lattice needs axioms at all.

---

## 9. Obligations this document places on the implementation

- Depth exhaustion, node-limit exhaustion, and abort must all yield `Unknown`.
  There must be a test that a timed-out search never reports `Refuted`.
- The AND rule and the OR rule must be tested against each other's shape; swapping
  them is the likely bug and it is invisible on positives.
- Any axiom must be tested to **never fire** on a corpus of positions with known
  solutions. A single false positive is a critical bug, not a regression.
- The on-stack rule must be exercised by a position that transposes back into its
  own subtree, with an axiom enabled.
