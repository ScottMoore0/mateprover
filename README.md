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
- `-`: read EPD/FEN lines from stdin.

Unsupported options are currently ignored only when they are harmless compatibility flags. Native typed support for WinChest/Chest restriction options is a later E milestone.

## Output

Solved positions print:

```text
<fen4>; acn <nodes>; acs <seconds>; bm <uci>; dm <depth>; pv <uci ...>;
```

No-mate or unproved positions print no `dm` token, so the existing harness treats them as no mate.
