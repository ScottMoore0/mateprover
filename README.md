# E Chest

E Chest is the architectural rewrite line for an exact directmate prover.

It is a new implementation. Other mate solvers are used for testing,
benchmarking and verification, but the prover core is written from scratch
and uses no code from any of them.

## Current Status

This initial checkpoint establishes:

- standalone source tree;
- standalone executable interface compatible with the benchmark harness;
- legal move generation and directmate proof scaffolding;
- proof-carrying output in UCI move format;
- exact PV replay compatibility with the existing validator;
- opt-in recursive proof-tree output verified independently with python-chess;
- documentation for the full rewrite path.

The first implementation is intentionally conservative. It prioritizes correctness and auditability over maximum speed while the proof kernel and board representation are brought up under tests.

## Build

From the repository root:

```powershell
g++ -std=c++17 -O3 -DNDEBUG -Wall -Wextra -pedantic -o chest-e\build\echest.exe chest-e\src\echest.cpp
```

## CLI Shape

E accepts the Chest-style subset needed by the existing benchmark harness:

```text
echest.exe -b -1 -5 -M 64 -z 2 -
```

Supported now:

- `-b`: accepted for compatibility;
- `-1`: accepted for compatibility;
- `-5`: output UCI-style coordinate moves;
- `-M N`: accepted for compatibility;
- `-z N`: requested mate depth;
- `--emit-proof`: append a recursive JSON proof certificate for solved positions;
- `-`: read EPD/FEN lines from stdin.

Unsupported options are currently ignored only when they are harmless compatibility flags. Native typed support for WinChest/Chest restriction options is a later E milestone.

## Output

Solved positions print:

```text
<fen4>; acn <nodes>; acs <seconds>; bm <uci>; dm <depth>; pv <uci ...>;
```

With `--emit-proof`, solved positions additionally print:

```text
proof {"a":"<attacker-move>","mate":true}
```

or, for non-leaf attacker nodes:

```text
proof {"a":"<attacker-move>","d":[{"r":"<defender-reply>","p":<child-proof>}, ...]}
```

The proof tree is verified by `benchmarks\scripts\e_verify_proof_tree.py`. A valid proof must enumerate exactly every legal defender reply at every defender node and each leaf must end in checkmate.

Proof-certificate construction is opt-in. Normal benchmark/search runs do not build recursive proof JSON internally unless `--emit-proof` is passed.

No-mate or unproved positions print no `dm` token, so the existing harness treats them as no mate.
