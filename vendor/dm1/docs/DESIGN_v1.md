# Design — v1

How the repository satisfies its requirements, read off the code on 2026-11-03; confirmed by
the owner on 2026-11-04 ([SE1/W12](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w12)).

## Components

| Component | Contract (one line) | Holds state? |
| --------- | ------------------- | ------------ |
| `guide/`, `README.md`, the root documents | the principles, their index, and the map to SE1 | the text |
| `AGENTS.md`, `templates/` | what adopters copy; they name a release | the text |
| GitHub Pages, `_config.yml` | renders `main` with the named theme, and nothing else | none |
| `tools/check.py` | the documents agree with each other and with SE1 at `SE_RELEASE`; REQ2/CHK-1, REQ1/PIN-1 | none |
| `dm`, `dm.ps1`, `dm.py` | SE1's doctor plus the data rows; REQ1/DOC-1 | none |
| `se`, `se.ps1`, `se.py`, `docs/SE1-AGENTS.md`, `vendor/se1/` | unedited copies from SE1 v1.7 | none |
| `.claude/settings.json` | runs `se --post` at startup, resume, and compaction | none |

## One-way doors

- **Principle IDs and anchors** are cited from outside; an ID is never reused.
- **The root `AGENTS.md` path** is published: adopters and links name it.

## Decisions

- **The two agreements are both imported from `CLAUDE.md`** — REQ1/AGR-1 — a file to open is a rule not
  followed, and a `CLAUDE.md` switches off Claude Code's own read of `AGENTS.md` — rejected: the root copy
  alone, which loads no SE1 rules — source: ADOPT.md step 2.
- **All of SE1 is vendored at `vendor/se1`, and the checker reads it there** — REQ1/PIN-1 — a contributor and CI need
  nothing beside this repository, and the checker holds the vendored tree to `SE_RELEASE` and the copies to the tree —
  rejected: a clone beside this one, which every contributor and CI job had to fetch, and which the owner's
  decision of 2026-11-03 had chosen — source: the owner, 2026-11-05.

## Deferred

- *Not yet:* `archive/` — no review or checked source has arrived.
