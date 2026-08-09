# The selfmate reachability bound — derivation

**Status: derivation only. No code depends on this document yet.**

Written before the implementation, as the three before it were.

Provenance: **P-CHESS-THEORY**.

---

## 1. The class this was supposed to be about, and why it is not

The residue after GAP-2 is 15 selfmate positions, and the largest defender class
in the 904-position corpus is **king + pawn: 127 roots, 100 of them unsolved at
import**. The clean-room analysis called it the larger and harder half, and asked
for "the king+pawn theorem".

There is no king+pawn theorem, and the reason is worth stating because it changes
what gets built.

**In a selfmate the roles are inverted.** The attacker forces the *defender* to
mate him. So the side that must deliver mate is the DEFENDER, and the side that
gets mated is the ATTACKER. A "king+pawn defender" therefore means:

> the mate must be delivered by a pawn, or by what that pawn promotes to, and by
> nothing else — because the only other unit that side owns is a king, and a king
> cannot give check.

That is not a special theorem. It is the ordinary reachability argument of
`HELPMATE_COVERAGE_DERIVATION.md` evaluated on the smallest possible mating
force. King+pawn is not a class needing its own mathematics; it is the class
where the existing mathematics has the least to work with and therefore bites
hardest.

So: the same theorem, with the roles swapped, applied to selfmate.

## 2. The theorem, restated for selfmate

Everything in `HELPMATE_COVERAGE_DERIVATION.md` §3 carries over verbatim once the
roles are read correctly. For a node with `w` moves left for the mating side
(here the DEFENDER) and `m` for the mated side (here the ATTACKER):

- **A(w)** — squares some **non-king defender** unit can attack within `w` moves,
  on an empty board. The check must come from one of these.
- **H** — the handled set: squares the defender can attack (king included, since
  a king may cover a flight even though it cannot check), plus squares either
  side can occupy within its own budget, plus squares occupied now.
- **D(m)** — squares within `m` king-steps of the attacker's king.

> If every k ∈ D(m) ∩ A(w) has a flight square outside H, no selfmate exists from
> this node within these moves.

The proof is the one already given: the mated king ends somewhere in D(m), it is
in check so that square lies in A(w), and each of its flights must be attacked or
occupied, so each lies in H.

## 3. What is genuinely different, and must not be got wrong

**(a) Who is mated.** The mated side is `s.attacker`; the mating side is
`other(s.attacker)`. This is the opposite of every other goal in the program, and
reading it the usual way round would build a bound about the wrong king.

**(b) The move budgets are asymmetric, and differ by node type.** The engine's
selfmate recursion runs `attacker(d) → defender(d) → attacker(d-1)`, so:

| node | mating side (defender) | mated side (attacker) |
| --- | --- | --- |
| attacker to move, depth d | d | d |
| defender to move, depth d | d | **d − 1** |

The mated side has one fewer move at a defender node because the attacker has
already spent his move to arrive there. Using `d` for both would OVERSTATE the
mated king's disc, which is the safe direction, but understating any of these is
not — so they are written out rather than approximated.

**(c) The attacker cooperates in being mated.** In a selfmate the mated side
*wants* to be mated and will self-block its own king's flights. That is not a
problem for the bound: its own occupancy reach is already in H, exactly as the
mated side's is for a helpmate. It does mean H is usually large and the bound will
fire less often than it does on helpmates — which is a prediction to measure, not
a reason to skip it.

**(d) The bound is depth-bounded, not a refutation.** It says "no selfmate within
these moves", not "at any depth", so it returns an ordinary failure and does not
touch GAP-1's `Refuted`. Nothing here may set that verdict: the argument depends
on the remaining move counts, and a deeper search has more of them.

## 4. The relaxation traps, which are shared

The table is the same one, so its two known traps are already fixed and must stay
fixed:

- a pawn's relaxed moves include its **diagonal captures**, because the table
  models a real board even though an empty one offers nothing to capture (74);
- a promoting pawn may become a **knight**, whose attacks are not a subset of a
  queen's (75).

Both matter more here than they did for helpmate. When the mating force is a lone
pawn, the whole bound is a statement about what that one pawn can do, and both
bugs understate exactly that.

## 5. How it will be judged

The gate is the one established at 74 and used at 75, and nothing weaker:

- an on/off switch, `--no-selfmate-bound`, read once, shipped so the gate is
  reproducible rather than a one-off script;
- the whole 904-position corpus, both settings;
- compare solved **sets**, not counts;
- **zero lost** to pass.

No gain is predicted. GAP-2 was called the highest value-per-line item in the
parity analysis and converted nothing; §73 predicted a gain and lost eight
positions. The prediction record on this engine is poor enough that the honest
position is to measure and report.
