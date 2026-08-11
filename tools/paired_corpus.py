#!/usr/bin/env python3
# MateProver -- an exact directmate prover with machine-checkable proofs.
# Copyright (C) 2026 Scott Moore
#
# Released under the MIT License. See LICENSE for the full text.

"""Paired coverage AND speed over a whole corpus: this engine against Chest 3.19.

The point of this file is that it runs a CORPUS. On every goal where a sample was
replaced by the full set, the two disagreed -- mate-in-8's sample invented a loss,
selfmate's understated the misses twelvefold, helpmate's overstated a loss that
was really a dead heat. Samples of 30 to 40 against effects of 1 to 4% are the
wrong instrument.

Coverage and timing come from ONE pass. Every mateprover result line carries its
own `acs`, so a batched run still yields per-position times; Chest is invoked per
position and timed directly. Two passes would cost twice as much and let the two
tables drift apart.

Three scoring rules, each of which has been got wrong here at least once:

  presence, not equality   51 corpus rows have a true solution shallower than
                           stipulated. Demanding the reported depth equal the
                           stipulated one scores BOTH engines as failing on all
                           51, for correctly proving the shorter mate.

  refusal is not timeout   Chest reporting "No solution" and Chest running out of
                           clock are different events. A position both engines
                           refuse is evidence about the CORPUS, not either
                           engine. See benchmarks/KNOWN_BAD.jsonl.

  the goal must be set     Fed an EPD stipulation like `=2`, Chest silently
                           solves the position as an orthodox DIRECTMATE and
                           reports `dm 1`. The job type has to be set with
                           j<letter> or the run measures the wrong goal.

THE HARNESS IS THE LEAST-VERIFIED COMPONENT AND EVERY PUBLISHED NUMBER PASSES
THROUGH IT. Four measurement defects arrived in one session and none of them was
in the engine, which by then had 414 automated checks against this file's zero.
All four were the same bug -- a value attached to the wrong thing:

    -M handicap        a per-lane budget passed as a total budget
    sm / sfm token     a stalemate line parsed as a selfmate result
    swallowed output   a truncated stream, rows shifted against their results
    state keyed by goal   two corpora sharing one resume file, so mate-in-10
                       silently reported mate-in-8's numbers under its heading

So identity is now explicit everywhere and checked rather than assumed:

  1. Every record is self-describing -- position digest, goal, requested depth,
     engine, engine digest, budget, corpus digest, harness schema. A record is
     interpretable with no reference to the file it came from or the loop that
     produced it. Nothing is positional.

  2. Resume state is keyed by a hash of the whole measurement definition, not by
     any one field. Reopening a state file under a different definition is a hard
     error, which makes the d8/d10 collision impossible rather than unlikely.

  3. Invariants are asserted at load, not discovered at analysis: no position
     twice, every reported depth within the requested bound, every record inside
     the corpus, count equal to input count. And a ledger of result fingerprints,
     because two DISTINCT measurements producing byte-identical results is the
     signature of defect four and nothing else.

  4. Parsing is strict and fails loudly. The result line is split into fields and
     matched on its own FEN and on an exact goal token; a line that does not
     parse raises instead of quietly scoring a non-solution. A permissive regex
     is a silent-wrong-answer generator.

  5. The harness never invents a tuning parameter. A flag reaches an engine only
     if it is part of the measurement definition and recorded in every record.

Resumable. State is written after every chunk, because long runs here get killed
and three earlier sweeps were lost whole.

Usage:
    python tools/paired_corpus.py --corpus benchmarks/stalemate_pdb.jsonl \
        --goal stalemate --seconds 10 --state run.json
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(HERE)
CHEST_DIR = os.path.join(ROOT, "chest-3.19-original", "build")
CHEST = os.path.join(CHEST_DIR, "dchest_original.exe")

# Bump when the record shape or the identity inputs change. A state file written
# under an older schema is refused rather than reinterpreted.
SCHEMA = 2

LEDGER = os.path.join(HERE, "benchmarks", "measurement_ledger.jsonl")

# token: the exact first word of the result field the engine emits for this goal.
# chest_job: the job-type directive Chest needs, without which it silently solves
# an orthodox directmate and reports a plausible, wrong number.
GOALS = {
    "mate":          {"token": "dm",  "chest_job": "jo"},
    "stalemate":     {"token": "sm",  "chest_job": "jO"},
    "selfmate":      {"token": "sfm", "chest_job": "js"},
    "selfstalemate": {"token": "ssm", "chest_job": "jS"},
    "helpmate":      {"token": "hm",  "chest_job": "jh"},
    "helpstalemate": {"token": "hsm", "chest_job": "jH"},
}
ALL_TOKENS = frozenset(g["token"] for g in GOALS.values())


class HarnessError(Exception):
    """A measurement is not interpretable. Never downgraded to a non-solution."""


# ---------------------------------------------------------------- identity ---

def sha256_file(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_fen4(fen: str) -> str:
    """The four-field Forsyth line with its whitespace normalised.

    Two spellings of one position must not become two rows, and a row whose
    castling or en-passant field is missing is a DIFFERENT position rather than
    an untidy presentation of the same one -- so a short line is an error.
    """
    fields = fen.split()
    if len(fields) != 4:
        raise HarnessError(f"expected a four-field FEN, got {fen!r}")
    return " ".join(fields)


def position_id(fen: str) -> str:
    return hashlib.sha256(canonical_fen4(fen).encode()).hexdigest()[:16]


def measurement_identity(definition: dict) -> str:
    """A hash over the WHOLE definition, so no single field can alias two runs.

    Keyed by goal alone, mate-in-8 and mate-in-10 shared a resume file and the
    second run reported the first one's results. Keyed by this, they cannot.
    """
    payload = json.dumps(definition, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode()).hexdigest()[:32]


def engine_version(engine: str) -> str:
    try:
        out = subprocess.run([engine, "--version"], capture_output=True,
                             timeout=60).stdout.decode("latin-1").strip()
    except (OSError, subprocess.SubprocessError):
        out = "unknown"
    return out.splitlines()[0] if out else "unknown"


def harness_commit() -> str:
    try:
        out = subprocess.run(["git", "-C", HERE, "rev-parse", "--short", "HEAD"],
                             capture_output=True, timeout=60)
        return out.stdout.decode().strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


# ----------------------------------------------------------------- parsing ---

def split_result_fields(line: str) -> tuple[str, list[tuple[str, str]]]:
    """('<fen4>', [(key, rest), ...]) from one engine result line.

    Field-splitting rather than pattern-hunting. A regex loose enough to find
    `sm` inside a line is loose enough to find it inside another goal's token,
    and that is defect two; a parser that has to name the field it read cannot
    make that mistake.
    """
    parts = [p.strip() for p in line.strip().split(";")]
    if len(parts) < 2 or not parts[0]:
        raise HarnessError(f"unparseable result line: {line!r}")
    fields = []
    for part in parts[1:]:
        if not part:
            continue
        key, _, rest = part.partition(" ")
        fields.append((key, rest.strip()))
    return parts[0].strip(), fields


def parse_mateprover_line(line: str, fen4: str, goal: str,
                          requested_depth: int) -> dict:
    """{'verdict', 'depth', 'seconds'} -- or raise. Never a silent non-solution.

    The line must identify itself. Zipping a batch's rows against a batch's
    output lines is only correct while the stream is whole, and the one time it
    was not whole every result after the cut was attributed to the wrong row.
    """
    if goal not in GOALS:
        raise HarnessError(f"unknown goal {goal!r}")
    want = GOALS[goal]["token"]
    reported_fen, fields = split_result_fields(line)
    if canonical_fen4(reported_fen) != canonical_fen4(fen4):
        raise HarnessError(
            f"result line is for a different position: asked {fen4!r}, "
            f"line reports {reported_fen!r}")

    keys = [k for k, _ in fields]
    seen_goal_tokens = ALL_TOKENS.intersection(keys)
    if seen_goal_tokens - {want}:
        raise HarnessError(
            f"line for goal {goal!r} carries a foreign goal token "
            f"{sorted(seen_goal_tokens - {want})}: {line!r}")

    seconds = 0.0
    for key, rest in fields:
        if key == "acs":
            try:
                seconds = float(rest)
            except ValueError as exc:
                raise HarnessError(f"bad acs field {rest!r}: {line!r}") from exc
        elif key == "error":
            return {"verdict": "error", "depth": None, "seconds": seconds,
                    "detail": rest}

    if want not in keys:
        # No token and no error is the engine's honest "not within the budget".
        return {"verdict": "unsolved", "depth": None, "seconds": seconds}

    raw = dict(fields)[want]
    depth_text = raw.split()[0] if raw.split() else ""
    if not depth_text.isdigit():
        raise HarnessError(f"goal token {want!r} has no depth: {line!r}")
    depth = int(depth_text)
    if not 1 <= depth <= requested_depth:
        # Presence, not equality -- a shallower proof is a correct answer. But a
        # DEEPER one than was asked for means the engine was not asked what the
        # row says it was asked, and that is a harness fault, not a result.
        raise HarnessError(
            f"reported depth {depth} outside the requested bound "
            f"1..{requested_depth}: {line!r}")
    return {"verdict": "solved", "depth": depth, "seconds": seconds}


def classify_chest_output(text: str) -> str:
    """'solved' | 'refused' | 'timeout'. Refusal is evidence about the CORPUS."""
    if "No solution" in text:
        return "refused"
    return "solved" if "Solution" in text else "timeout"


# ------------------------------------------------------------- invariants ---

def assert_invariants(records: list[dict], rows: list[dict],
                      identity: str, complete: bool) -> None:
    """Everything that must hold, checked at load rather than at analysis.

    Defect four was found by eye, because two runs printed byte-identical
    output. Check three below is that observation turned into an assertion.
    """
    corpus_ids = {position_id(r["fen4"]): r["mate"] for r in rows}

    seen = set()
    for record in records:
        if record.get("schema") != SCHEMA:
            raise HarnessError(f"record written under schema "
                               f"{record.get('schema')!r}, this is {SCHEMA}")
        if record.get("measurement") != identity:
            raise HarnessError(
                "record belongs to a different measurement: record says "
                f"{record.get('measurement')!r}, this run is {identity!r}")
        key = (record["position"], record["engine"])
        if key in seen:
            raise HarnessError(f"position {record['position']} recorded twice "
                               f"for engine {record['engine']}")
        seen.add(key)
        if record["position"] not in corpus_ids:
            raise HarnessError(f"record for {record['position']} is not in the "
                               f"corpus this state claims to cover")
        depth = record.get("depth")
        if depth is not None:
            bound = corpus_ids[record["position"]]
            if not 1 <= depth <= bound:
                raise HarnessError(
                    f"{record['engine']} reports depth {depth} for "
                    f"{record['position']}, requested bound was {bound}")

    if complete:
        expected = 2 * len(rows)
        if len(records) != expected:
            raise HarnessError(f"complete run holds {len(records)} records, "
                               f"corpus of {len(rows)} needs {expected}")


def results_fingerprint(records: list[dict]) -> str:
    """A digest over WHAT WAS FOUND, ignoring how long it took.

    Timings drift between runs; the solved set does not. Two distinct
    measurements sharing this value did not both run.
    """
    solved = sorted(f"{r['engine']}:{r['position']}:{r['depth']}"
                    for r in records if r["verdict"] == "solved")
    return hashlib.sha256("\n".join(solved).encode()).hexdigest()[:32]


def ledger_check(identity: str, fingerprint: str, definition: dict,
                 path: str = LEDGER) -> None:
    """Refuse a result set some other measurement already produced.

    Two corpora cannot legitimately yield the same solved set. When mate-in-10
    resumed mate-in-8's state file this is exactly what happened, and nothing in
    the pipeline objected.
    """
    entries = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                if line.strip():
                    entries.append(json.loads(line))
    for entry in entries:
        if entry["fingerprint"] == fingerprint and entry["measurement"] != identity:
            raise HarnessError(
                "this result set is byte-identical to an earlier, DIFFERENT "
                f"measurement ({entry['corpus']} goal {entry['goal']} depth "
                f"bound {entry.get('depth_bound')}). Two distinct corpora do "
                "not solve the same set; a state file has been shared.")
        if entry["measurement"] == identity:
            return
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a", encoding="utf-8") as handle:
        handle.write(json.dumps({"measurement": identity,
                                 "fingerprint": fingerprint,
                                 **definition}, sort_keys=True) + "\n")


# ------------------------------------------------------------------- runs ---

def mateprover_chunk(engine: str, goal: str, rows: list[dict], seconds: float,
                     memory_mb: int | None) -> dict:
    """{fen4: parsed record} for this chunk. Raises rather than losing a row."""
    name = goal
    out = {}
    by_depth = collections.defaultdict(list)
    for row in rows:
        by_depth[row["mate"]].append(row)
    for depth, group in by_depth.items():
        # Only what the measurement definition declares. An explicit -M is the
        # budget for every table alive at once; passing the per-lane figure of
        # 256 starves the engine of the default it ships with, and on a
        # cooperative search that was worth 12.8x on a measured position (2.9 s
        # became 37 s). Handicapping the engine under test with its own tuning
        # knob is exactly the defect this project already fixed once, in the
        # CLI, and it was reintroduced here. The flag now reaches the engine
        # only when it is part of the definition, and it is recorded when it is.
        argv = [engine, "--goal", name, "-z", str(depth),
                "--time-limit", str(seconds)]
        if memory_mb is not None:
            argv += ["-M", str(memory_mb)]
        argv.append("-")
        proc = subprocess.run(
            argv, input="".join(r["fen4"] + "\n" for r in group).encode(),
            capture_output=True, timeout=seconds * len(group) + 900)
        lines = [l for l in proc.stdout.decode().splitlines() if l.strip()]
        if len(lines) != len(group):
            raise HarnessError(
                f"asked for {len(group)} positions at depth {depth}, the "
                f"stream carried {len(lines)} result lines. A truncated stream "
                f"shifts every row after the cut against its result.")
        for row, line in zip(group, lines):
            out[row["fen4"]] = parse_mateprover_line(line, row["fen4"], goal,
                                                     row["mate"])
    return out


def chest_job(goal: str, row: dict) -> str:
    board, stm, castling, ep = canonical_fen4(row["fen4"]).split()
    # The board-only Forsyth line carries no castling rights, and dropping them
    # changes the position rather than its presentation.
    extra = ""
    for flag, code in (("K", "cws"), ("Q", "cwl"), ("k", "cbs"), ("q", "cbl")):
        if flag in castling:
            extra += code + "\n"
    if ep != "-":
        extra += "e" + ep + "\n"
    return f"LE\nf {board}\n{extra}{GOALS[goal]['chest_job']}\nz{row['mate']}{stm}\n..\n"


def chest_one(goal: str, row: dict, seconds: float, memory_mb: int) -> tuple[str, float]:
    job = chest_job(goal, row)
    start = time.time()
    try:
        proc = subprocess.run([CHEST, "-r", "-M", str(memory_mb)], cwd=CHEST_DIR,
                              input=job.encode(), capture_output=True, timeout=seconds)
    except subprocess.TimeoutExpired:
        return "timeout", seconds
    elapsed = time.time() - start
    return classify_chest_output(proc.stdout.decode("latin-1")), elapsed


def make_record(definition: dict, identity: str, row: dict, engine: str,
                verdict: str, depth: int | None, elapsed: float) -> dict:
    """One self-describing measurement. Interpretable with no context at all."""
    return {
        "schema": SCHEMA,
        "measurement": identity,
        "position": position_id(row["fen4"]),
        "fen4": canonical_fen4(row["fen4"]),
        "goal": definition["goal"],
        "requested_depth": row["mate"],
        "engine": engine,
        "engine_version": definition[f"{engine}_version"],
        "engine_digest": definition[f"{engine}_digest"],
        "memory_mb": definition[f"{engine}_memory_mb"],
        "seconds_budget": definition["seconds"],
        "corpus": definition["corpus"],
        "corpus_digest": definition["corpus_digest"],
        "harness_commit": definition["harness_commit"],
        "verdict": verdict,
        "depth": depth,
        "elapsed": elapsed,
    }


def load_corpus(path: str) -> list[dict]:
    rows, seen = [], set()
    for number, line in enumerate(open(path, encoding="utf-8"), 1):
        if not line.strip():
            continue
        row = json.loads(line)
        if "fen4" not in row or "mate" not in row:
            raise HarnessError(f"{path}:{number}: row needs fen4 and mate")
        fen = canonical_fen4(row["fen4"])
        if not isinstance(row["mate"], int) or row["mate"] < 1:
            raise HarnessError(f"{path}:{number}: mate must be a positive int")
        if fen in seen:          # a duplicate is counted twice by any rate
            continue
        seen.add(fen)
        rows.append({"fen4": fen, "mate": row["mate"]})
    if not rows:
        raise HarnessError(f"{path}: no rows")
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--goal", required=True, choices=sorted(GOALS))
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--engine", default=os.path.join(HERE, "build", "mateprover.exe"))
    ap.add_argument("--mateprover-mb", type=int, default=None,
                    help="pass -M to mateprover; omitted means the shipped "
                         "default, which is what a fair run wants")
    ap.add_argument("--chest-mb", type=int, default=2048)
    ap.add_argument("--chunk", type=int, default=20)
    ap.add_argument("--state", required=True)
    args = ap.parse_args()

    rows = load_corpus(args.corpus)
    chunks = [rows[i:i + args.chunk] for i in range(0, len(rows), args.chunk)]

    definition = {
        "schema": SCHEMA,
        "corpus": os.path.basename(args.corpus),
        "corpus_digest": sha256_file(args.corpus)[:32],
        "goal": args.goal,
        "depth_bound": max(r["mate"] for r in rows),
        "seconds": args.seconds,
        "chunk": args.chunk,
        "mateprover_version": engine_version(args.engine),
        "mateprover_digest": sha256_file(args.engine)[:16],
        "mateprover_memory_mb": args.mateprover_mb,
        "chest_version": "3.19",
        "chest_digest": sha256_file(CHEST)[:16] if os.path.exists(CHEST) else "absent",
        "chest_memory_mb": args.chest_mb,
        "harness_commit": harness_commit(),
    }
    identity = measurement_identity(definition)

    state = {"identity": identity, "definition": definition,
             "records": [], "refused": [], "done": []}
    if os.path.exists(args.state):
        state = json.load(open(args.state, encoding="utf-8"))
        if state.get("identity") != identity:
            raise HarnessError(
                f"{args.state} was written for a different measurement.\n"
                f"  it holds : {json.dumps(state.get('definition'), indent=4)}\n"
                f"  this run : {json.dumps(definition, indent=4)}\n"
                "Refusing to resume. Two corpora sharing one state file is how "
                "mate-in-10 came to report mate-in-8's numbers.")
        assert_invariants(state["records"], rows, identity,
                          complete=len(state["done"]) == len(chunks))

    print(f"{definition['corpus']}: {len(rows)} distinct, goal {args.goal}, "
          f"{args.seconds:g}s a position, {len(chunks)} chunks, "
          f"{len(state['done'])} done", flush=True)
    print(f"  measurement {identity}", flush=True)

    for index, chunk in enumerate(chunks):
        if index in state["done"]:
            continue
        start = time.time()
        parsed = mateprover_chunk(args.engine, args.goal, chunk, args.seconds,
                                  args.mateprover_mb)
        for row in chunk:
            result = parsed[row["fen4"]]
            state["records"].append(make_record(
                definition, identity, row, "mateprover", result["verdict"],
                result["depth"], result["seconds"]))
            verdict, elapsed = chest_one(args.goal, row, args.seconds, args.chest_mb)
            if verdict == "refused":
                state["refused"].append(position_id(row["fen4"]))
            state["records"].append(make_record(
                definition, identity, row, "chest", verdict, None, elapsed))
        state["done"].append(index)
        assert_invariants(state["records"], rows, identity, complete=False)
        with open(args.state, "w", encoding="utf-8") as handle:
            json.dump(state, handle)
        solved = collections.Counter(r["engine"] for r in state["records"]
                                     if r["verdict"] == "solved")
        print(f"  chunk {index+1}/{len(chunks)}: mateprover {solved['mateprover']}  "
              f"chest {solved['chest']}  ({time.time()-start:.0f}s)", flush=True)

    assert_invariants(state["records"], rows, identity, complete=True)
    fingerprint = results_fingerprint(state["records"])
    ledger_check(identity, fingerprint, definition)
    report(state["records"], rows, args, fingerprint)
    return 0


def report(records: list[dict], rows: list[dict], args, fingerprint: str) -> None:
    by_engine = {"mateprover": {}, "chest": {}}
    fens = {}
    for record in records:
        fens[record["position"]] = record["fen4"]
        if record["verdict"] == "solved":
            by_engine[record["engine"]][record["position"]] = record["elapsed"]
    mine, theirs = set(by_engine["mateprover"]), set(by_engine["chest"])
    both = sorted(mine & theirs)
    refused = {r["position"] for r in records
               if r["engine"] == "chest" and r["verdict"] == "refused"}

    print(f"\nCOVERAGE over {len(rows)} distinct positions at {args.seconds:g}s")
    print(f"  fingerprint {fingerprint}")
    print(f"  mateprover {len(mine)}    chest {len(theirs)}")
    print(f"  only mateprover {len(mine - theirs)}    only chest {len(theirs - mine)}")
    print(f"  chest refused definitively (evidence about the CORPUS): {len(refused)}")
    for position in sorted(theirs - mine, key=lambda p: fens[p])[:25]:
        print(f"    only chest: {fens[position]}")

    # Speed is compared only on positions BOTH solved. A ratio that includes a
    # timeout is comparing a number against a cap.
    if both:
        mine_t = [max(by_engine["mateprover"][p], 1e-6) for p in both]
        their_t = [max(by_engine["chest"][p], 1e-6) for p in both]
        ratios = [t / m for m, t in zip(mine_t, their_t)]
        print(f"\nSPEED on the {len(both)} both solved")
        print(f"  total   chest {sum(their_t):.1f}s  mateprover {sum(mine_t):.1f}s  "
              f"= {sum(their_t)/max(sum(mine_t),1e-9):.2f}x")
        print(f"  median per-position speedup: {statistics.median(ratios):.2f}x")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HarnessError as error:
        print(f"\nHARNESS ERROR: {error}", file=sys.stderr)
        raise SystemExit(3)
