#!/usr/bin/env python3
"""Search for CANDIDATE pruning theorems by falsifying the false ones at scale.

A pruning theorem says "no solution below here" without searching. Getting one
wrong does not make the engine slow, it makes it report a sound problem as
unsolvable, silently. Section 74 of the architecture document shipped an unsound
bound and this is the class of bug the whole engine exists to prevent.

SO THIS TOOL DOES NOT PROVE ANYTHING, AND CANNOT.

What it does is falsify. The engine evaluates a candidate at every attacker node
and then searches as though it had said nothing, so when the node returns its
true verdict is known:

    fired, and the node was PROVED    a counterexample. Exact, permanent, and one
                                      is enough to kill the candidate.
    fired, and the node FAILED        the subtree it would have skipped, which is
                                      what the candidate is worth IF it is ever
                                      proved by a person.

A candidate that survives a billion nodes with no counterexample is a conjecture
with evidence, not a theorem. Promoting one to a live prune needs a proof written
by a human, and this tool prints that in its output rather than leaving it to be
assumed.

THE HARNESS CHECKS ITSELF FIRST. The population is seeded with predicates whose
status is already known -- `amen<=1` is GAP-1's Axiom 1, "a bare attacker king
cannot mate", which is proved in docs/GAP1_DERIVATION.md, and `depth>=1` is a
deliberate falsehood. If the known-true one shows a counterexample or the
known-false one does not, the measurement is broken and nothing else it says
means anything.
"""

import argparse
import json
import random
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# name -> the range a random threshold is drawn from.
FEATURES = {
    "depth":    (1, 8),
    "men":      (2, 14),
    "amen":     (1, 8),
    "dmen":     (1, 8),
    "amat":     (0, 30),
    "dmat":     (0, 30),
    "aqueens":  (0, 2),
    "arooks":   (0, 3),
    "aminors":  (0, 3),
    "apawns":   (0, 4),
    "dflights": (0, 8),
    "aincheck": (0, 1),
    # RELATIONAL features. Everything above is one side counted in isolation, and
    # a generator over those can only find statistical accidents -- 47% of nodes
    # at any depth are winnable, so separating the failures from the wins is
    # inherently a statement about both sides at once.
    "dattacked": (0, 8),
    "aattacked": (0, 8),
    "dkingring": (0, 8),
    "acaptures": (0, 8),
    "mate1imp":  (0, 1),
    "contact":   (1, 4),
}
OPS = ["<=", ">=", "<", ">", "==", "!="]

# Predicates whose truth is already settled, used to check the instrument.
KNOWN_TRUE = "amen<=1"      # GAP-1 Axiom 1: a bare attacker king cannot mate
KNOWN_FALSE = "depth>=1"    # fires everywhere; obviously not a theorem

STAT = re.compile(r'"(pred_fires|pred_counterexamples|pred_nodes_saved|nodes)":(\d+)')


def load_corpus(path: Path, limit: int) -> str:
    lines = []
    for raw in path.read_text(encoding="utf-8-sig").splitlines():
        raw = raw.strip()
        if not raw or raw.startswith("#"):
            continue
        if raw.startswith("{"):
            rec = json.loads(raw)
            lines.append(f"{rec['fen4']} ; dm {rec.get('mate') or rec.get('moves')}")
        else:
            lines.append(raw)
        if len(lines) >= limit:
            break
    return "\n".join(lines) + "\n"


def measure(engine: Path, corpus: str, predicate: str, node_limit: int) -> dict:
    """Run the corpus with one candidate and total its observer counters."""
    proc = subprocess.run(
        [str(engine), "--no-portfolio", "--threads", "1", "--node-limit", str(node_limit),
         "--predicate", predicate, "--profile", "-"],
        input=corpus, capture_output=True, text=True, timeout=3600)
    if "Try 'mateprover --help'" in proc.stdout + proc.stderr:
        return None
    totals = {"pred_fires": 0, "pred_counterexamples": 0, "pred_nodes_saved": 0, "nodes": 0}
    for m in STAT.finditer(proc.stdout + proc.stderr):
        totals[m.group(1)] += int(m.group(2))
    return totals


def score(t: dict) -> tuple:
    """Rank candidates. Refuted ones sort last whatever they would have saved.

    Note the shape: this is not a weighted sum. A candidate with one
    counterexample is not a slightly worse candidate, it is a false statement,
    and no amount of saving trades against that.
    """
    if t is None or t["pred_counterexamples"] > 0:
        return (1, 0.0)
    return (0, -t["pred_nodes_saved"] / max(1, t["nodes"]))


def random_predicate(rng: random.Random, max_clauses: int) -> str:
    n = rng.randint(1, max_clauses)
    names = rng.sample(list(FEATURES), n)
    parts = []
    for name in names:
        lo, hi = FEATURES[name]
        parts.append(f"{name}{rng.choice(OPS)}{rng.randint(lo, hi)}")
    return "&".join(parts)


def mutate(rng: random.Random, predicate: str, max_clauses: int) -> str:
    clauses = predicate.split("&")
    action = rng.random()
    if action < 0.25 and len(clauses) < max_clauses:
        clauses.append(random_predicate(rng, 1))
    elif action < 0.45 and len(clauses) > 1:
        clauses.pop(rng.randrange(len(clauses)))
    else:
        i = rng.randrange(len(clauses))
        m = re.match(r"([a-z]+)(<=|>=|==|!=|<|>)(-?\d+)", clauses[i])
        if not m:
            return random_predicate(rng, max_clauses)
        name, op, value = m.group(1), m.group(2), int(m.group(3))
        pick = rng.random()
        if pick < 0.4:
            lo, hi = FEATURES[name]
            value = max(lo, min(hi, value + rng.choice([-2, -1, 1, 2])))
        elif pick < 0.7:
            op = rng.choice(OPS)
        else:
            name = rng.choice(list(FEATURES))
            lo, hi = FEATURES[name]
            value = rng.randint(lo, hi)
        clauses[i] = f"{name}{op}{value}"
    # Duplicate features in one conjunction are usually contradictions; drop them.
    seen, keep = set(), []
    for c in clauses:
        head = re.match(r"[a-z]+", c)
        if head and head.group(0) not in seen:
            seen.add(head.group(0))
            keep.append(c)
    return "&".join(keep) if keep else random_predicate(rng, max_clauses)


def crossover(rng: random.Random, a: str, b: str, max_clauses: int) -> str:
    pool = a.split("&") + b.split("&")
    rng.shuffle(pool)
    seen, keep = set(), []
    for c in pool:
        head = re.match(r"[a-z]+", c)
        if head and head.group(0) not in seen and len(keep) < max_clauses:
            seen.add(head.group(0))
            keep.append(c)
    return "&".join(keep)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--engine", type=Path, default=ROOT / "build" / "mateprover.exe")
    p.add_argument("--corpus", type=Path, default=ROOT / "tests" / "mates.epd")
    p.add_argument("--limit", type=int, default=12)
    p.add_argument("--node-limit", type=int, default=2_000_000)
    p.add_argument("--population", type=int, default=16)
    p.add_argument("--generations", type=int, default=6)
    p.add_argument("--max-clauses", type=int, default=3)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--out", type=Path)
    args = p.parse_args()

    rng = random.Random(args.seed)
    corpus = load_corpus(args.corpus, args.limit)

    # ---- Check the instrument before trusting anything it says.
    print("=== instrument check ===")
    ok = True
    t = measure(args.engine, corpus, KNOWN_TRUE, args.node_limit)
    print(f"  known TRUE  {KNOWN_TRUE:16s} fires {t['pred_fires']:>8,}  "
          f"counterexamples {t['pred_counterexamples']}")
    if t["pred_counterexamples"] != 0:
        print("  !! a proved theorem was refuted: the observer is wrong, not the theorem")
        ok = False
    f = measure(args.engine, corpus, KNOWN_FALSE, args.node_limit)
    print(f"  known FALSE {KNOWN_FALSE:16s} fires {f['pred_fires']:>8,}  "
          f"counterexamples {f['pred_counterexamples']}")
    if f["pred_counterexamples"] == 0:
        print("  !! a false claim was not refuted: the observer has no falsifying power here")
        ok = False
    if not ok:
        return 2
    print("  instrument OK\n")

    population = [random_predicate(rng, args.max_clauses) for _ in range(args.population)]
    seen = {}

    for generation in range(args.generations):
        scored = []
        for cand in population:
            if cand not in seen:
                seen[cand] = measure(args.engine, corpus, cand, args.node_limit)
            scored.append((score(seen[cand]), cand, seen[cand]))
        scored.sort(key=lambda x: x[0])
        alive = [(s, c, t) for s, c, t in scored if s[0] == 0 and t["pred_fires"] > 0]
        best = alive[0] if alive else None
        print(f"gen {generation + 1}: {len(alive)}/{len(scored)} unrefuted"
              + (f"  best {best[1]}  saves {-best[0][1]:.1%} of nodes" if best else ""))
        parents = [c for _, c, _ in scored[:max(3, args.population // 3)]]
        population = list(dict.fromkeys(parents))
        while len(population) < args.population:
            child = mutate(rng, crossover(rng, rng.choice(parents), rng.choice(parents),
                                          args.max_clauses), args.max_clauses)
            population.append(child)

    # ---- Report
    survivors = sorted(
        ((t["pred_nodes_saved"] / max(1, t["nodes"]), c, t)
         for c, t in seen.items()
         if t and t["pred_counterexamples"] == 0 and t["pred_fires"] > 0),
        reverse=True)
    print(f"\n=== {len(survivors)} candidates with no counterexample "
          f"({len(seen)} evaluated) ===")
    print("These are CONJECTURES. Each one claims there is no solution below any")
    print("node where it fires; none of them has been proved, and a single missed")
    print("mate is worse than every second they would save. A candidate is worth")
    print("a human's time only if it both survives and saves something.\n")
    print(f"{'candidate':44s} {'fires':>10s} {'saves':>8s}")
    for saving, cand, t in survivors[:args.keep if hasattr(args, 'keep') else 15]:
        print(f"{cand:44s} {t['pred_fires']:>10,} {saving:>7.1%}")
    if not survivors:
        print("(none -- every candidate that fired was refuted by counterexample)")

    if args.out and survivors:
        args.out.write_text("\n".join(json.dumps({
            "predicate": c, "fires": t["pred_fires"], "nodes": t["nodes"],
            "nodes_saved": t["pred_nodes_saved"], "counterexamples": 0,
            "status": "UNPROVED CONJECTURE",
        }) for _, c, t in survivors) + "\n", encoding="utf-8")
        print(f"\nwrote {len(survivors)} conjectures to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
