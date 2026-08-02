# Benchmark positions

Position sets drawn from the public `matetrack.epd` corpus. One JSON object per
line: `fen4` is the position, `mate` the true mate distance.

## Evaluation sets -- used once

| file | positions | depth |
|---|---:|---|
| `matetrack_d8_eval200.jsonl` | 200 | mate in 8 |
| `matetrack_d10_eval60.jsonl` | 60 | mate in 10 |

The reach figures in `../docs/RESULTS.md` come from these. They were minted after
all development was complete and measured exactly once. **Do not tune against
them.** Their only value is that nothing has been decided by looking at them, and
that is spent the first time it is.

## Development sets -- consulted repeatedly

| file | positions | depth |
|---|---:|---|
| `matetrack_d8_holdout60.jsonl` | 60 | mate in 8 |
| `matetrack_d10_holdout24.jsonl` | 24 | mate in 10 |

These were held out from *tuning* but were used for promote-or-reject decisions
throughout development -- about ten times for the mate-in-8 set. That is enough to
make them optimistic: the same configuration scores 86.7% on the mate-in-8 set
here and 79.5% on the 200-position evaluation set, with the former outside the
latter's confidence interval. They remain useful for comparing two builds, which
is what they were used for; they are no longer evidence of absolute reach.

## The protocol

A reach figure is evidence about the engine only if nothing about the engine was
chosen by looking at it. That property is consumed by use, silently, and it does
not require anything as deliberate as tuning -- ten promote-or-reject decisions
were enough to overstate mate-in-8 reach by seven points.

So, for any future work that could change reach:

1. **Mint the set first**, with `../tools/mint_eval_set.py`, before starting the
   work it will judge. The tool excludes every position any existing set already
   contains and records the draw in `MANIFEST.json`.
2. **Do not look at it** while working. Use the development sets for
   promote-or-reject decisions; that is what they are for, and comparing two
   builds is a use that repetition does not spoil.
3. **Measure once**, when the work is finished, and mark the set `spent` in
   `MANIFEST.json`.

`MANIFEST.json` records what each set is for and whether it has been spent. Both
evaluation sets currently shipped are spent: they produced the figures in
`../docs/RESULTS.md` and cannot honestly produce another.

Reproduce the documented figures with `../tools/reproduce_results.py`.
