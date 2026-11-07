# Ledger

`ledger.jsonl` beside this file holds one line per `stripped.py score`, appended by that code, never
by hand. Every score is a look, the controls' included. The count of lines is part of every result
reported from this question. A line's `split` says `pilot` (the fixture), `dev`, or `holdout`; a
holdout line carries `holdout_why`, the decision it served.

Fields: `when` (UTC), `who`, `question`, `what` (what was tried), `against` (what would count against
it), `split`, `seed`, `data` (source and sha256), `answer` and `answer_sha256`, `environment`,
`commit` (`+dirty` if `test/stripped` had uncommitted changes), `metrics`, `holdout_why`.
