# MateProver certificate corpus

4,218 machine-checkable proofs of forced mate, one per position, for positions
drawn from matetrack. Each line is a MateProver result line with its certificate
attached. A certificate is a complete AND/OR proof tree: at every defender node
it lists every legal reply, and every branch ends in checkmate.

**This directory is GPL-3.0, unlike the rest of the repository.** See
[Licence](#licence).

## Checking it

You need neither the engine nor any trust in it -- only python-chess and the
checker in this repository. From the repository root:

```
xz -dk certificates/certificates.epd.xz
python tools/verify_proof.py certificates/certificates.epd --require-proof --expect 4218
```

The compressed file is 2.6 MB; decompressed it is 2.3 GB, because a deep mate's
certificate lists every defence at every defender node. Four certificates account
for most of that, the largest 1.2 GB on its own, and the checker holds each
certificate in memory whole, so check the corpus on a machine with memory to
spare. A full check took 18 minutes on the machine that produced it.

The checker re-derives every legal move itself and never consults the engine.
It rejects a certificate that omits a defence, marks a non-mating leaf as mate,
or overstates the depth. The log of the last full check is in `VERIFY.log`.

To confirm the files are the ones described in `MANIFEST.json`, run
`sha256sum -c SHA256SUMS` in this directory. The manifest also records the
hash of the decompressed file.

## What a certificate does and does not prove

A certificate proves that the stated side forces mate within the stated number
of moves. It does **not** prove that no shorter mate exists: absence has no
certificate format. These were generated with `--iterative-depth --no-portfolio`,
so MateProver's own search established each depth as the shortest -- but that
part is the engine's claim, not something the checker verifies.

## How it was made

`--iterative-depth --no-portfolio --emit-proof --threads 1`, one position at a
time, over the 6,528 positions in matetrack with a positive `bm` (the 26 with a
negative `bm`, where the side to move is the one being mated, are not directmate
problems and were skipped), in two passes:

| pass | budget per position | attempted | certified |
|---|---:|---:|---:|
| 1 | 4,000,000 nodes | 6,528 | 3,018 |
| 2 | 64,000,000 nodes | 3,510 | 1,200 |

The second pass ran only the positions the first left uncertified. A node
budget, not a clock, bounds each search, so a position certified within 4,000,000
nodes is certified identically within 64,000,000: the corpus is every position
MateProver proves within 64,000,000 nodes. The second pass's binary reproduced
24 first-pass certificates byte for byte before it ran. 2,310 positions hit the
larger budget and carry no certificate. Mates run from 1 to 24 moves. Exact
engines, corpus and file hashes are in `MANIFEST.json`.

## Licence

This directory is distributed under the GNU General Public License, version 3.
The full text is in `COPYING`.

Every certificate contains the position it proves, and the positions come from
[matetrack](https://github.com/vondele/matetrack), which is GPL-3.0. The corpus
therefore carries the same licence.

The licence covers this directory only. The rest of MateProver -- including the
engine that generated these certificates and the checker that verifies them --
is MIT; see `LICENSE` in the repository root. Nothing outside this directory is
derived from it or reads from it. Running the engine or the checker does not
bring either under GPL-3.0; redistributing these files, alone or inside
something else, carries GPL-3.0's terms for them.
