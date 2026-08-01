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

Reproduce the documented figures with `../tools/reproduce_results.py`.
