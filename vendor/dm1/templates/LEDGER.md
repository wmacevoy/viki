# Ledger

Every configuration scored, and in retrieval or extraction every search run and page read, appended
**by the code that did it**, never from memory. The count of lines is part of every result reported
from this work
([DM1/F6](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/03-feedback.md#f6),
[DM1/A3](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/10-agents.md#a3)).
Keep it as a machine-written file (`ledger.jsonl` or `ledger.csv`) beside this description; a
Markdown table is fine for small work. Logs of reads and searches count; name them on the *The
ledger* line of `CONTRIBUTING.md` so the doctor can find them.

Each run whose result someone looked at or used is an experiment: say what would count against the claim, and how much it would cost to run it again,
which decides what else to keep beside the line
([DM1/T13](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/06-testing.md#t13)).

A holdout row is a spending event: say what decision it served
([DM1/F5](https://github.com/wmacevoy/data-mining/blob/v1.3/guide/03-feedback.md#f5)).

| When (UTC) | Who / agent | Question | What was tried | Would count against it | Split | Seed | Data version | Environment | Commit | Metric(s) | Cost to run again | Holdout? why |
| ---------- | ----------- | -------- | -------------- | ---------------------- | ----- | ---- | ------------ | ----------- | ------ | --------- | ----------------- | ------------ |
| _2026-10-27T18:02Z_ | _name_ | _Q1/BAR-1_ | _baseline: same as last comparable event_ | _MAE above 25 min: the data is not ready_ | _validation_ | _—_ | _raw@…_ | _pixi.lock_ | _abc123_ | _MAE 19.2 min_ | _seconds_ | _no_ |

Report a result as, for example: *"best of 23 configurations on validation; holdout spent once, on
2026-11-10, to choose between the final two."*
