#!/usr/bin/env python3
"""Automatic search over MateProver's configuration.

Two modes, one harness:

    tune      coordinate descent over declared configuration knobs
    ordering  a genetic search over the five move-ordering weights

WHY THE FITNESS IS NODES AND NOT SECONDS
========================================

This machine drifts about 15% between identical runs, and the effects worth
tuning are 3-10%. A wall-clock objective would be measuring the drift. Node
counts are exactly reproducible at `--threads 1`, so one repetition is enough
and a 1% difference is real. The engine's own `--node-limit` exists for this
reason (see the comment on `node_limit` in search_state.h); this tool is the
consumer that comment was anticipating.

The tuned configuration is then confirmed on wall clock and on a HELD-OUT
corpus, because a configuration that wins on the training set and loses on the
evaluation set has happened here five times (see architecture 65).

WHY THE GATE IS LEXICOGRAPHIC
=============================

A scalar fitness would happily accept a configuration that reports `dm 7` where
the answer is `dm 5`. So nothing is compared until correctness holds:

    1. every position solved by the baseline is still solved
    2. no position's reported depth changes
    3. (with --with-suite) the full test suite passes
    4. only then: maximise positions solved, then minimise nodes

The ordering mode is a special case worth stating: the five weights it searches
change the ORDER moves are tried and never the SET, so no assignment of them can
change a verdict. Steps 1-3 cannot fail there. They are still run, because that
claim is worth checking rather than trusting.
"""

import argparse
import json
import random
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# The verdict tokens, one per goal. A result line carries exactly one.
VERDICT_RE = re.compile(r"\b(dm|sm|sfm|ssm|hm|hsm) (\d+)")
NODES_RE = re.compile(r"\bacn (\d+)")


# --------------------------------------------------------------------------
# Running the engine

def load_corpus(path: Path, limit: int | None = None) -> list[str]:
    """Read an EPD or a JSONL benchmark into EPD lines."""
    lines = []
    text = path.read_text(encoding="utf-8-sig")
    for raw in text.splitlines():
        raw = raw.strip()
        if not raw or raw.startswith("#"):
            continue
        if raw.startswith("{"):
            rec = json.loads(raw)
            depth = rec.get("mate") or rec.get("moves")
            lines.append(f"{rec['fen4']} ; dm {depth}")
        else:
            lines.append(raw)
        if limit and len(lines) >= limit:
            break
    return lines


class Result:
    """One configuration's outcome on one corpus."""

    def __init__(self, verdicts: dict, nodes: int, seconds: float):
        self.verdicts = verdicts        # fen -> "dm 5" or None
        self.nodes = nodes
        self.seconds = seconds

    @property
    def solved(self) -> int:
        return sum(1 for v in self.verdicts.values() if v is not None)

    def key(self):
        """Lexicographic fitness: more solved first, then fewer nodes."""
        return (-self.solved, self.nodes)


def run(engine: Path, corpus: list[str], flags: list[str], node_limit: int,
        threads: int = 1) -> Result:
    """Evaluate one configuration. Deterministic by construction.

    `--node-limit` rather than `--time-limit`: a budget that is the same on
    every run and every machine, so a candidate is never rejected for having
    been unlucky with the clock. A position that exceeds it reports `timeout`
    and claims nothing, which the abort invariant guarantees is never mistaken
    for a disproof.
    """
    cmd = [str(engine), "--threads", str(threads), "--node-limit", str(node_limit),
           *flags, "-"]
    start = time.monotonic()
    proc = subprocess.run(cmd, input="\n".join(corpus) + "\n", capture_output=True,
                          text=True, timeout=3600)
    seconds = time.monotonic() - start
    verdicts, nodes = {}, 0
    for line in proc.stdout.splitlines():
        if ";" not in line:
            continue
        fen = line.split(";", 1)[0].strip()
        m = VERDICT_RE.search(line)
        verdicts[fen] = m.group(0) if m else None
        n = NODES_RE.search(line)
        if n:
            nodes += int(n.group(1))
    return Result(verdicts, nodes, seconds)


def gate(baseline: Result, candidate: Result) -> str | None:
    """Return a reason to reject, or None to accept for comparison.

    This is the whole safety of the tool. A candidate that reports a different
    DEPTH is not a faster configuration, it is a wrong one, and the difference
    is invisible in a scalar score.
    """
    for fen, base in baseline.verdicts.items():
        got = candidate.verdicts.get(fen, "MISSING")
        if got == "MISSING":
            return f"{fen}: disappeared from the output"
        if base is not None and got is not None and base != got:
            return f"{fen}: {base} -> {got}"
        if base is not None and got is None:
            return f"{fen}: {base} -> unsolved"
    return None


def suite_passes(engine: Path) -> bool:
    proc = subprocess.run([sys.executable, str(ROOT / "tests" / "run_tests.py"),
                           "--engine", str(engine)],
                          capture_output=True, text=True, timeout=3600)
    return proc.returncode == 0


# --------------------------------------------------------------------------
# Tier 1: coordinate descent over configuration knobs
#
# Not a genetic search, deliberately. The knobs are nearly independent and the
# fitness is exact, so the cheapest thing that works is to sweep one knob at a
# time and keep what wins. A population would spend most of its budget
# rediscovering that the other 99 knobs did not matter.

KNOBS = [
    # (name, [candidate flag-lists]) -- each entry is one setting of one knob.
    ("ybw-first", [["--ybw-first", str(n)] for n in (0, 1, 2, 4, 8, 16)]),
    ("or-split-min-depth", [["--or-split-min-depth", str(n)] for n in (2, 3, 4, 5, 6)]),
    ("reply-split-min-proved", [["--reply-split-min-proved", str(n)] for n in (0, 2, 4)]),
    ("root-sequential-first", [["--root-sequential-first", str(n)] for n in (0, 1, 2, 4)]),
    ("answer-order-min-depth", [["--answer-order-min-depth", str(n)] for n in (1, 2, 3, 4)]),
    ("order-min-size", [["--order-min-size", str(n)] for n in (2, 3, 4, 8)]),
    ("dfpn-min-depth", [["--dfpn-min-depth", str(n)] for n in (1, 2, 3)]),
    ("dfpn-min-men", [["--dfpn-min-men", str(n)] for n in (0, 7, 9)]),
    ("dfpn-check-bias", [["--dfpn-check-bias", str(n)] for n in (1, 2, 4)]),
    # Booleans, as ON-or-baseline pairs. A `[]` entry means "leave it alone",
    # which is how a flag with no negation is still searchable: the engine has
    # --lazy-defender but no --no-lazy-defender, and naming a flag that does not
    # exist makes the engine print usage and no results. The gate catches that
    # ("disappeared from the output") rather than scoring an empty run as
    # infinitely fast, but the wasted evaluation is avoidable.
    ("lazy-defender", [[], ["--lazy-defender"]]),
    ("refutation-hints", [["--refutation-hints"], ["--no-refutation-hints"]]),
    ("proof-hints", [["--proof-hints"], ["--no-proof-hints"]]),
    ("bucket-order", [[], ["--bucket-order"]]),
    ("fast-check-score", [[], ["--fast-check-score"]]),
    ("coverage-exit", [["--coverage-exit"], ["--no-coverage-exit"]]),
    ("keep-iter-tt", [[], ["--keep-iter-tt"]]),
    ("score-mates", [[], ["--score-mates"]]),
]


def tune(args, engine, corpus, evalset):
    base_flags = list(args.base)
    baseline = run(engine, corpus, base_flags, args.node_limit)
    print(f"baseline: {baseline.solved}/{len(corpus)} solved, "
          f"{baseline.nodes:,} nodes, {baseline.seconds:.1f}s")
    best_flags, best = base_flags, baseline

    for sweep in range(args.sweeps):
        improved = False
        for name, settings in KNOBS:
            if args.only and name not in args.only:
                continue
            for setting in settings:
                cand_flags = best_flags + setting
                cand = run(engine, corpus, cand_flags, args.node_limit)
                why = gate(baseline, cand)
                if why:
                    print(f"  [{name}] {' '.join(setting):32s} REJECTED  {why}")
                    continue
                mark = " "
                if cand.key() < best.key():
                    best_flags, best, improved, mark = cand_flags, cand, True, "*"
                print(f"  [{name}] {' '.join(setting):32s} {mark} "
                      f"{cand.solved}/{len(corpus)}  {cand.nodes:,} nodes")
        print(f"sweep {sweep + 1}: best {best.solved}/{len(corpus)}, {best.nodes:,} nodes")
        if not improved:
            break

    report(args, engine, corpus, evalset, baseline, best_flags, best)


# --------------------------------------------------------------------------
# Tier 2: a genetic search over the move-ordering weights
#
# The one search space in this program where correctness cannot be at stake:
# these weights change the order moves are tried and never the set. So the
# search is free to be as blunt as it likes.

def clamp_weights(w: list[int]) -> list[int]:
    """Repair a genome to satisfy the engine's invariant.

    The static terms must stay below 50000, because prove.h reads a score of
    that magnitude as "this move gives check". The CLI refuses a violating
    assignment, so the search repairs rather than wastes an evaluation on it.
    """
    w = [max(0, min(40000, int(x))) for x in w]
    while w[0] + w[1] + max(w[2], w[3], w[4]) >= 50000:
        w[0] = int(w[0] * 0.8)
        w[1] = int(w[1] * 0.8)
    return w


def weight_flags(w: list[int]) -> list[str]:
    return ["--order-weights", ",".join(str(x) for x in w)]


def evolve_ordering(args, engine, corpus, evalset):
    rng = random.Random(args.seed)
    default = [10000, 8000, 50, 40, 30]
    base_flags = list(args.base)
    baseline = run(engine, corpus, base_flags + weight_flags(default), args.node_limit)
    print(f"baseline weights {default}: {baseline.solved}/{len(corpus)} solved, "
          f"{baseline.nodes:,} nodes")

    def mutate(w):
        child = list(w)
        for i in range(len(child)):
            if rng.random() < 0.4:
                # Multiplicative for the large terms and additive for the small
                # ones, so a mutation is a comparable perturbation at both
                # scales. A purely additive step never moves 10000 and a purely
                # multiplicative one can never reach 0.
                if child[i] > 200:
                    child[i] = int(child[i] * rng.uniform(0.5, 2.0))
                else:
                    child[i] = child[i] + rng.randint(-40, 40)
        return clamp_weights(child)

    def crossover(a, b):
        return clamp_weights([a[i] if rng.random() < 0.5 else b[i] for i in range(len(a))])

    population = [default] + [mutate(default) for _ in range(args.population - 1)]
    scored = []
    best_flags, best = base_flags + weight_flags(default), baseline

    for generation in range(args.generations):
        scored = []
        for genome in population:
            cand = run(engine, corpus, base_flags + weight_flags(genome), args.node_limit)
            why = gate(baseline, cand)
            if why:
                # Cannot happen if the soundness-neutrality claim holds. Checked
                # rather than assumed: if this ever fires, the claim is wrong and
                # that is far more important than the tuning.
                print(f"  !! ORDERING CHANGED A VERDICT: {genome} {why}")
                continue
            scored.append((cand.key(), genome, cand))
        scored.sort(key=lambda t: t[0])
        if scored and scored[0][0] < best.key():
            _, genome, cand = scored[0]
            best_flags, best = base_flags + weight_flags(genome), cand
        print(f"gen {generation + 1}: best {scored[0][1]} "
              f"{scored[0][2].solved}/{len(corpus)} {scored[0][2].nodes:,} nodes")
        # Elitism plus offspring. Half the survivors, half fresh crosses.
        elite = [g for _, g, _ in scored[:max(2, args.population // 4)]]
        population = list(elite)
        while len(population) < args.population:
            population.append(mutate(crossover(rng.choice(elite), rng.choice(elite))))

    report(args, engine, corpus, evalset, baseline, best_flags, best)


# --------------------------------------------------------------------------

def report(args, engine, corpus, evalset, baseline, best_flags, best):
    print("\n=== best on the training corpus ===")
    print("flags:", " ".join(best_flags) or "(default)")
    print(f"train: {baseline.solved} -> {best.solved} solved, "
          f"{baseline.nodes:,} -> {best.nodes:,} nodes "
          f"({100 * (1 - best.nodes / max(1, baseline.nodes)):+.1f}%)")

    if evalset:
        # THE HELD-OUT SET. A configuration that wins on the training corpus and
        # loses on the evaluation corpus has happened five times in this
        # project's history; the tuner does not get to skip this.
        base_eval = run(engine, evalset, list(args.base), args.node_limit)
        best_eval = run(engine, evalset, best_flags, args.node_limit)
        why = gate(base_eval, best_eval)
        print(f"eval:  {base_eval.solved} -> {best_eval.solved} solved, "
              f"{base_eval.nodes:,} -> {best_eval.nodes:,} nodes "
              f"({100 * (1 - best_eval.nodes / max(1, base_eval.nodes)):+.1f}%)")
        if why:
            print(f"eval:  REJECTED on the held-out set -- {why}")
        elif best_eval.key() >= base_eval.key():
            print("eval:  NO GAIN on the held-out set. Overfitted; do not ship.")
        else:
            print("eval:  confirmed on the held-out set.")

    if args.with_suite:
        print("suite:", "PASS" if suite_passes(engine) else "FAIL")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", choices=["tune", "ordering"])
    p.add_argument("--engine", type=Path, default=ROOT / "build" / "mateprover.exe")
    p.add_argument("--corpus", type=Path, default=ROOT / "benchmarks" / "matetrack_d8_train60.jsonl")
    p.add_argument("--eval", type=Path, default=ROOT / "benchmarks" / "matetrack_d8_eval200_r2.jsonl")
    p.add_argument("--limit", type=int, default=20, help="positions from the training corpus")
    p.add_argument("--eval-limit", type=int, default=40)
    p.add_argument("--node-limit", type=int, default=2_000_000,
                   help="deterministic per-position budget")
    p.add_argument("--base", nargs="*", default=["--no-portfolio"],
                   help="flags held fixed for every candidate")
    p.add_argument("--sweeps", type=int, default=2)
    p.add_argument("--only", nargs="*", help="restrict tune to these knob names")
    p.add_argument("--population", type=int, default=8)
    p.add_argument("--generations", type=int, default=6)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--with-suite", action="store_true",
                   help="also run the full test suite on the winner")
    p.add_argument("--no-eval", action="store_true")
    args = p.parse_args()

    corpus = load_corpus(args.corpus, args.limit)
    evalset = None if args.no_eval else load_corpus(args.eval, args.eval_limit)
    print(f"{len(corpus)} training positions"
          + (f", {len(evalset)} held out" if evalset else ""))

    # THE FITNESS SATURATES IF THE CORPUS IS TOO HARD FOR THE BUDGET.
    #
    # A position that exceeds --node-limit reports exactly the limit, so a
    # corpus where most positions time out scores the same for every candidate:
    # the total is the cap times the count, and the search is optimising a
    # constant. The first run of this tool did precisely that -- 7 of 8
    # positions capped, and 40 evaluations moved the objective by 7 nodes out
    # of 2.2 million.
    #
    # This is not a warning to be read and ignored. Either raise --node-limit
    # or pick a corpus the budget can finish.
    probe = run(args.engine, corpus, list(args.base), args.node_limit)
    if probe.solved < 0.6 * len(corpus):
        print(f"\nREFUSING: the baseline solves only {probe.solved}/{len(corpus)} "
              f"within --node-limit {args.node_limit:,}.")
        print("Capped positions all score the same, so the fitness carries no signal.")
        print("Raise --node-limit, or choose a corpus this budget can finish.")
        return 2

    if args.mode == "tune":
        tune(args, args.engine, corpus, evalset)
    else:
        evolve_ordering(args, args.engine, corpus, evalset)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
