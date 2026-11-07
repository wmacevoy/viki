# Ledger

`ledger.jsonl` beside this file holds one line per `stripped.py score`, appended by that code, never
by hand. Every score is a look, the controls' included. The count of lines is part of every result
reported from this question. A line's `split` says `pilot` (the fixture), `dev`, or `holdout`; a
holdout line carries `holdout_why`, the decision it served.

Fields: `when` (UTC), `who`, `question`, `what` (what was tried), `against` (what would count against
it), `split`, `seed`, `data` (source and sha256), `answer` and `answer_sha256`, `environment`,
`commit` (`+dirty` if `test/stripped` had uncommitted changes), `metrics`, `holdout_why`.

**Known fault, 2026-11-07.** Lines 2 to 9, the baselines on dev seeds 2 to 5, say `8f1f57b+dirty`.
The code was clean at `8f1f57b`; the dirty check counted the ledger file itself, which was
untracked. Fixed in the next commit. The lines stay as written, because the ledger is append-only.
