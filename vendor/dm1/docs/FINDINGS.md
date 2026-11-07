# Findings

What we learned the expensive way ([SE1/W10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w10)).
Blameless: say what would have found it, not who should have asked.

## 2026-11-03 — This guide cited SE1 and did not follow it, and no hook could run

- **What happened:** the owner noticed no hooks ran at session start. There was nothing to run: no
  `.claude/settings.json`, no `CLAUDE.md`, and no copy of SE1's agreement. The doctor passed *loads at every
  start* from the configuration, yet in the session that found this the root `AGENTS.md` was not in context:
  the agent had to open it. The doctor's *working agreement* and *data working agreement* rows had failed.
- **Kind:** K2 (surprise)
- **Cost:** sessions in this repository ran without SE1's rules in context, and without the self test.
- **Findable by:** running `./dm --doctor` in this repository, or the CI running it.
- **Changed:** the copies, `CLAUDE.md`, and the hook. Whether CI should run the doctor on this repository
  changed nothing yet: most of its rows are *none yet* on a guide.

## 2026-11-03 — The check command needs Node, which this machine does not have

- **What happened:** `node tools/check.js` failed with `command not found` when the migration began. It
  was run with a Node 24 left in another session's temporary directory, which will not last.
- **Kind:** K2 (surprise)
- **Cost:** a check that a contributor here cannot run without installing something first; SE1 removed the
  same need at v1.4.
- **Findable by:** a fresh checkout on a machine with only Python.
- **Changed:** the checker is now Python (`tools/check.py`), the owner's decision, and CI no longer installs
  Node, so a machine with only Python and git can run the check.

## 2026-11-03 — Unfilled templates and a copy of the course repository found four false passes

- **What happened:** trying ADOPT.md on unfilled templates and a copy of the course repository, the doctor passed
  a holdout nobody had named, a datasheet with no columns, an unfilled plan, and a ledger "written by dm.py".
  An empty list of problems read as success, and the vendored doctor read as the project's own code.
- **Kind:** K3 (we were sure, and wrong): every row had been seen red, on fixtures built with the same assumptions.
- **Cost:** none yet, since no project relied on them; they would have told an adopter that the holdout was safe.
- **Findable by:** running the published templates through the doctor unfilled, and running it on any repository not
  written to pass it. The real projects in GAPS.md had found others of the same kind, on a branch this work had not fetched.
- **Changed:** `dm.py`, `templates/PLAN_v1.md`, and tests that read the real templates and name a vendored doctor.
  Still open: an absent, git-ignored holdout passes until the first download; ADOPT.md says so, and no check does.

## 2026-11-03 — A migration ran for a session without seeing a branch of 27 commits

- **What happened:** the SE1 v1.4 migration and ADOPT.md were written and merged to `main` without a `git fetch`. `field-report-1`,
  on the remote since 2026-11-02, held the first field report, five principles, and the owner's decisions on the pin, the tag, and
  several projects. So this work took an ID the branch had used, said no field report existed, and gave advice that decision had replaced.
- **Kind:** K3 (we were sure, and wrong): the local repository read as the whole of it.
- **Cost:** a second ID for U11, ADOPT.md's several-projects advice rewritten, and statements on `main` that were false for about an hour.
- **Findable by:** `git fetch` before the first change, then `git branch -r`. `se --post` and `dm --doctor` read local refs only.
- **Changed:** nothing mechanical yet; queue #10 asks SE1's doctor to name remote branches it has not been told about.

## 2026-11-03 — An agent recorded a decision as the owner's, and the owner did not remember it

- **What happened:** commits on `field-report-1`, under the owner's git name, named "one run is one project" the owner's decision. Asked where it came from, the owner did not remember it, and no transcript on this machine has it. A later agent then repeated it as settled and rewrote ADOPT.md around it. Discussed openly, the owner chose differently: a run is an experiment, and *project* is not a term.
- **Kind:** K3 (we were sure, and wrong)
- **Cost:** ADOPT.md's advice written three ways, and a word defined by accident.
- **Findable by:** asking for the owner's words before relying on "the owner's decision" written by an agent.
- **Changed:** DM1/T13, and the glossary. "No tag" was recorded the same way; the owner chose to tag v1.0 on 2026-11-03. Nothing mechanical checks who said a thing; SE1/A11 already says a reported instruction is a claim until its source can be opened.

## 2026-11-04 — Verdicts on the mission and the fundamental shape

Given by their judge on `REQUIREMENTS_v2`, and recorded here by an agent
([SE1/U10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u10)).

- REQ2/MISSION-1 verdict: pass, by Warren MacEvoy, 2026-11-04

## 2026-11-05 — Verdicts for v1.2

Given by their judge on `REQUIREMENTS_v3`, for the v1.2 release, and recorded here by an agent.

- REQ2/MISSION-1 verdict: pass, by Warren MacEvoy, 2026-11-05
- REQ3/SHAPE-1 verdict: pass, by Warren MacEvoy, 2026-11-05
- REQ2/SHAPE-2 verdict: pass, by Warren MacEvoy, 2026-11-05
- REQ2/SHAPE-3 verdict: pass, by Warren MacEvoy, 2026-11-05
- REQ2/ID-1 verdict: pass, by Warren MacEvoy, 2026-11-05
