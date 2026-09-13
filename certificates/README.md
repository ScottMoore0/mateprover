# MateProver certificate corpus

3,018 machine-checkable proofs of forced mate, one per position, for positions
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
python tools/verify_proof.py certificates/certificates.epd --require-proof --expect 3018
```

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

`--iterative-depth --no-portfolio --emit-proof --threads 1 --parallel-positions 12
--node-limit 4000000`, one position at a time, over the 6,528 positions in
matetrack with a positive `bm` (the 26 with a negative `bm`, where the side to
move is the one being mated, are not directmate problems and were skipped). The
node budget, not a clock, bounds each search, so the same binary produces the
same corpus on any machine. 3,510 positions hit the budget and carry no
certificate. Exact engine, corpus and file hashes are in `MANIFEST.json`.

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
