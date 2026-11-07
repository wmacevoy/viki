# Contributing

How to work on this repository, for a new teammate and for an agent, who both arrive knowing
nothing ([SE1/A3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a3)).
A change that makes a line here untrue updates it in the same change. `templates/CONTRIBUTING.md` is
the template this guide publishes for data projects; this file is about the guide itself.

## Run it

- **Prerequisites:** Python 3.9 or later and git. SE1 is at `vendor/se1`. Nothing to install.
- **Build:** none; the repository is Markdown and scripts. GitHub Pages renders `main` with the theme named in `_config.yml`, and there is no local build.
- **Run:** `./dm --doctor` for this repository's state; on a guide, the data rows say *none yet*, which
  is true.
- **Reset to a known state:** `git status`, then `git restore` on what you do not want. Nothing is generated.

## Check it

- **The full check:** `python3 -I tools/check.py && python3 -I tools/test_check.py && python3 -I tools/test_dm.py`, the same commands CI runs
- **On Windows,** where an installer may give `python` and not `python3`, use `python` in the same commands.
- **Success prints:** `0 problems` from the checker, then `OK` from each set of tests
- **Instruments:** the checker and the tests. The doctor's rows were each seen red on a fixture, and
  `tools/test_check.py` plants a breakage per checker rule in a copy of this repository.

## Rules for this repository

- The root `AGENTS.md` is this guide's published agreement. SE1's is the unedited copy at
  `docs/SE1-AGENTS.md`, and `vendor/se1/`, `se`, `se.ps1`, and `se.py` are unedited copies too; change them only by
  re-copying from a release.
- Bumping SE1 follows *Changing this guide* in the README: one constant, the links, CI, the copies here,
  and a CHANGELOG entry by ID. `HANDOFF-*` notes keep the links they were written with.
- Python 3.9 or later and git are the only dependencies, for contributors and adopters alike: no Node, no
  third-party package, nothing that needs installing. The owner's decision, 2026-11-03.
- A change in a requirement's meaning is a new version.

## Where things are

- **Working set:** `docs/`: `REQUIREMENTS_vN`, `DESIGN_vN`, `QUEUE`, `FINDINGS`.
- **How a data project adopts this guide:** `ADOPT.md`, whose vendoring commands should be re-run when they change.
- **Agents:** `CLAUDE.md` imports both agreements; read this file before acting.

## AGENTS.md's nouns, here

| AGENTS.md says                 | In this repository                                     |
| ------------------------------ | ------------------------------------------------------ |
| the repository guide           | `CONTRIBUTING.md`                                      |
| the queue                      | `docs/QUEUE.md`                                        |
| findings                       | `docs/FINDINGS.md`                                     |
| the check command              | the full check above; success prints `0 problems`, `OK` |
| the handoff check              | `se --post`, a `SessionStart` hook; not yet seen to fire on a compaction here |
| `Non-goals`, `Scope decisions` | sections of `docs/REQUIREMENTS_v1.md`                  |
| the question, the plan, the ledger, the holdout | none yet: a guide has no data of its own |
