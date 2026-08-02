# Benchmark positions

**The position sets are not shipped. This directory records what they were and
how to rebuild them.**

They are drawn from a public mate-tracking EPD corpus that is published under a
copyleft licence. Redistributing derived subsets of it would carry that
licence's obligations into this tree; deriving them locally does not. Almost
nothing is lost in reproducibility: a set is determined by its corpus revision,
depth, count and seed, and `MANIFEST.json` records all four — except for two
early sets minted before seeds were recorded, which are marked as such below.

Rebuild any set in two steps:

    python ../tools/fetch_corpus.py
    python ../tools/mint_eval_set.py --corpus corpus/matetrack.epd \
        --depth <N> --count <N> --seed <N> --name <stem>

taking the arguments from `MANIFEST.json`. One JSON object per line: `fen4` is
the position, `mate` the true mate distance.

**The corpus revision is part of the recipe.** The upstream corpus is
maintained -- illegal positions removed, wrong mate values corrected -- so the
same seed against a later revision draws a *different* set, silently.
`fetch_corpus.py` therefore pins a commit and verifies a SHA-256, and
`MANIFEST.json` records `corpus_commit` alongside the seed. A set is
reproducible as (revision, depth, count, seed), not as (depth, count, seed).

Two early sets, `d8_holdout60` and `d10_holdout24`, predate that discipline and
carry `"rebuildable": false`: no seed was recorded, so they can be resampled but
never reproduced. The two development sets were re-minted with seeds on
2026-08-02 and their reference values re-measured; the numbers they had before
described sets that no longer exist.

## Evaluation sets — used once

| set | positions | depth |
|---|---:|---|
| `d8_eval200` | 200 | mate in 8 |
| `d10_eval60` | 60 | mate in 10 |

The reach figures in `../docs/RESULTS.md` come from these. They were minted after
all development was complete and measured exactly once. **Do not tune against
them.** Their only value is that nothing has been decided by looking at them, and
that is spent the first time it is.

## Development sets — consulted repeatedly

| set | positions | depth |
|---|---:|---|
| `d8_holdout60` | 60 | mate in 8 |
| `d10_holdout24` | 24 | mate in 10 |

These were held out from *tuning* but were used for promote-or-reject decisions
throughout development — about ten times for the mate-in-8 set. That is enough to
make them optimistic: the same configuration scores 86.7% on the mate-in-8 set
here and 79.5% on the 200-position evaluation set, with the former outside the
latter's confidence interval. They remain useful for comparing two builds, which
is what they were used for; they are no longer evidence of absolute reach.

## The protocol

A reach figure is evidence about the engine only if nothing about the engine was
chosen by looking at it. That property is consumed by use, silently, and it does
not require anything as deliberate as tuning — ten promote-or-reject decisions
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

Both evaluation sets are marked spent: they produced the figures in
`../docs/RESULTS.md` and cannot honestly produce another.

Reproduce the documented figures with `../tools/reproduce_results.py`, after
rebuilding the sets it names.
