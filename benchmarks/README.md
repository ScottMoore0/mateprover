# Held-out benchmark positions

Two position sets, drawn from the public `matetrack.epd` corpus, used for the
reach figures quoted in `../docs/RESULTS.md`.

| file | positions | depth |
|---|---:|---|
| `matetrack_d8_holdout60.jsonl` | 60 | mate in 8 |
| `matetrack_d10_holdout24.jsonl` | 24 | mate in 10 |

Both are **held out**: no tuning, lane selection or parameter choice used them.
The restriction portfolio was derived on a disjoint training set, and these were
spent only on promotion decisions. That is the property that makes the numbers
worth quoting, and re-tuning against them would destroy it.

One JSON object per line: `fen4` is the position, `mate` the true mate distance.

Reproduce the documented figures with `../tools/reproduce_results.py`.
