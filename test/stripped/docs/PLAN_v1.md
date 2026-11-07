# Analysis plan — v1

Question: `QUESTION_v1` · Written: 2026-11-07 · Outcome examined yet? no (no librarian has been scored
on a FINDINGS pile; one pilot was scored on the fixture)

**Draft, inferred.** Written by Claude Opus 5.5 before any librarian result on FINDINGS.md existed.

## Split

- **Grouping:** a pile is one seed. Every pile shares the 7 declared corrections (QUESTION_v1 K4s).
- **Dev:** seeds 2 to 9, for the prompt and the arms. Seed 1 is excluded from both splits: two runs
  read it before being stopped (Departures).
- **Holdout:** seeds of 1,000,000 or more, chosen and kept by Warren in `~/.viki-holdout/`, outside the
  working directory. Only Warren may read it, and he spends it once, at DECISION-1; the scorer refuses
  a holdout seed without a stated reason. The generator is in every clone, so a seed's secrecy is
  the whole fence: anyone who knows one can rebuild its key.

## Metric and baseline

- **Primary metric:** `stripped.py score`'s *score*, `1 - cost/cost_of_silence`, because its cost
  table is the price list in QUESTION_v1.
- **Second metric**, the one the prompt search is not told about: `lie_wins`, the planted lies the
  librarian adopted as current. Plus Brier score in arm `p`.
- **Baseline:** controls `null` and `lexical` — scored on 2026-11-07, dev seeds 2 to 5: see
  `ledger.jsonl` (lexical 0.222 on seed 1, before seed 1 was excluded).
- **Interval:** one run per seed per arm. Report every seed; a difference inside the seed spread is
  not a result.

## Slices

| Slice | Why it matters | Expected *n* |
| ----- | -------------- | ------------ |
| corrects (declared) | the source's own structure; the hardest kind | 7 per pile |
| same (planted echoes) | false consensus | 6 per pile |
| contradicts (planted lies) | the lie adopted is the costliest error | 6 per pile |
| unkeyed claims | right-and-undeclared, or wrong; a person reads them | varies |

## Search budget

- **Librarian:** `claude-sonnet-5-5`, through `librarian/run.sh`, in the sandbox.
- **Arms:** `nop` (bare answers) and `p` (each answer carries a probability).
- **Prompt:** `librarian/TASK.txt` v1, and at most 2 revisions, each on dev only.
- **Runs:** at most 2 arms × 3 prompt versions × 4 dev seeds = 24 dev runs. The ledger records the
  actual count.

## Missing data

| Input | Why missing | Treatment |
| ----- | ----------- | --------- |
| an answer naming an id not in the pile | the librarian invented it | scored as unkeyed |
| a run with no `answer.json` | the session failed | rerun once with a new run id; both runs in the evidence |

## Departures

- 2026-11-07 — before this plan: two Sonnet runs (arms `nop` and `p`) read seed 1's pile from inside a
  worktree of this repository, where `FINDINGS.md` was in reach. Both were stopped by Warren's
  instruction, nothing was scored, and their partial output was not opened. Seed 1 is excluded.
  `librarian/run.sh` and the sandbox are the fix.
