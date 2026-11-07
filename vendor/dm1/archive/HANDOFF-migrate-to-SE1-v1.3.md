# Handoff: migrating this guide to SE1 v1.3

> **Done 2026-10-28**, steps 0 to 6, by Claude Fable 5.1 with Warren MacEvoy: git initialized with
> the pre-migration state as its first commit; one commit per step or SE1 change, the checker green
> after each. What was reframed, declined, confirmed, and not reviewed is in
> [CHANGELOG.md](../CHANGELOG.md) under v1.0, by SE1 ID. Step 7, `archive/`, waits for the first review
> or checked source; the GitHub remote was created later that day and CI passed on six platform and
> Python combinations; the three `se` defects below were fixed in SE1 v1.3.1 (2026-10-28), each seen
> red on a fixture there. The note is kept as written, with its SE1 links pinned to v1.3 like the rest.

Written 2026-10-28 by Claude Fable 5.1, from a read-only survey made from the SE1 checkout beside
this directory; nothing here was changed. **This is a handoff note, so everything in it is recalled,
not observed** ([SE1/A11](https://github.com/wmacevoy/software-engineering/blob/v1.3/guide/10-agents.md#a11)): before acting on any
line, run the two commands under *Baseline* and open the files named. Where this note and the
repository disagree, the repository wins. One concern per change, the checker after each.

## What this repository is (observed 2026-10-28)

- A companion guide, **DM1**: ten chapters in SE1's shape, 91 principles, 18 new and 73 that
  reframe an SE1 principle (each says which on the line under its heading); its own AGENTS.md,
  which *extends* SE1's rather than copying it; CHECKLISTS, GAPS, GLOSSARY, READING, llms.txt, six
  templates (QUESTION_v1, PLAN_v1, LEDGER, DATASHEET, MODEL_CARD, CONTRIBUTING), a checker, CI.
  CHANGELOG says v1.0, unreleased. GAPS says there are no field reports yet.
- **Not a git repository**: no `.git`. README, CHANGELOG, and AGENTS.md link to
  https://github.com/wmacevoy/data-mining, which returned HTTP 404 when checked.
- **Built against SE1 as it stood on 2026-10-27 on its v1.2 branch**: DM1/U7 reframes SE1/A11,
  which no tagged SE1 had that day, and nothing here knows SE1's U8, U9, torn work, ADOPT.md,
  `archive/`, or `se`, all of which came after. The file dates are 2026-10-27.
- **Every reference to SE1 floats.** All 175 links name `blob/main`; llms.txt names `main`;
  `tools/check.js` has `SE_BLOB` at `main`; CI checks out SE1's default branch. SE1 has moved
  three releases since, and nothing here says so: a citation with no version is a K3 factory
  ([SE1/U7](https://github.com/wmacevoy/software-engineering/blob/v1.3/guide/02-unknowing.md#u7),
  [SE1/R10](https://github.com/wmacevoy/software-engineering/blob/v1.3/guide/04-requirements.md#r10)).
- Its checker resolves SE1 IDs against the local checkout, now v1.3, and passes. IDs resolve;
  **meaning was not reviewed**, by anyone, against the changes.

## Baseline: run these first

- `node tools/check.js` gave, on 2026-10-28: `23 files, 91 principles (18 new), 74 SE1 principles
  reframed, SE1 IDs checked against ../software-engineering/README.md, 0 problems`.
- `../software-engineering/se --doctor --offline --repo .` gave 11 checks, 1 failed. **The failure,
  "AGENTS.md carries no release label", is a defect in `se`** (this file's label is line-wrapped
  and `se`'s pattern does not allow that), not in this guide. The "none yet" rows are true: a guide
  has no working set, queue, or requirements of its own.

## What changed in SE1 between then and v1.3

The source is SE1's CHANGELOG (v1.2, v1.2.1, v1.3) and `git -C ../software-engineering diff
d256d10..v1.3 --stat`; this is the list to walk, not the record.

- **Principles changed:** A3 (an agent may ask; a private agent memory is a head), U7 (torn work;
  *medicine* not *cure*), H4 (a fourth stand-up question: do you notice discomfort? can you name
  it?), W7 (Brown's experiment as he ran it), A11 (the summary is not the only injury: a
  requirement freshly read is still not applied without a check that runs; the import boundary is
  the working directory). **New:** U8 (leave whitespace), U9 (rules change: who made it, when, what
  would show it wrong). Wording only: R8, W2, T5.
- **AGENTS.md:** *After a gap*; the owner's note (a copy inside the working directory; the handoff
  check is a hook, not a line to follow); a pointer to ADOPT.md; who drafted it; the notice that
  all testing was with Claude; the `./se` bullet (look for its report; run it if absent); wording
  from an adversarial read (no working set; a version update is a chore; what a new noun is).
- **New files:** ADOPT.md; `archive/` (sources with checksums and whether each was opened;
  reviews); `se`, `se.py`, `se.ps1`; `.gitattributes`; README's *Why to trust this, and why to
  doubt it*.
- **Checker:** a principle's text ends at a horizontal rule; copies must not cite an ID their
  labeled release lacks; a patch release changes no principle, AGENTS.md, or template. **CI:**
  three platforms, two Pythons.

## The review, in order

0. **Stop and ask first: git, and the GitHub repository.** Nothing below should be done without a
   history to put it in ([SE1/U9](https://github.com/wmacevoy/software-engineering/blob/v1.3/guide/02-unknowing.md#u9): git keeps what
   a rule's text cannot). Initializing a repository and creating the remote are the owner's
   decisions.
1. **Pin SE1 to v1.3.** One constant in `tools/check.js` (say `SE_RELEASE = 'v1.3'`) that builds
   `SE_BLOB`; the 175 links rewritten to `blob/v1.3/`; llms.txt to the tag; CI checking out SE1
   with `ref: v1.3`; CHANGELOG's sentence about where copies link. Then the checker. Every later
   bump is one edit.
2. **Reframe or decline each SE1 change, and say which on the basis line and in CHANGELOG.**
   Torn work (SE1/U7) into DM1/U2 and DM1/U6: what is torn in data work (a pipeline half-run, a
   model trained on a partial extract, a notebook run out of order). *Medicine* wherever this
   guide says *cure*. H4's fourth question into the H chapter. A11's "knowing is not doing" into
   DM1/U7 and the A chapter: a leakage check the agent must choose to run will be read and not
   run. U8, whitespace, against DM1/F4–F6, where every look spends evidence: the tension is real
   and deserves a sentence or a principle, not silence. U9 against DATASHEET and MODEL_CARD, which
   already carry who and when; what they lack is the falsifier. A3: this guide's A chapter does
   not say "the client never talks to the agent", so confirm and move on. W7: DM1/W5 cites the
   gradient and does not restate the experiment; confirm and move on.
3. **AGENTS.md here.** Say who drafted it and when (SE1/U9). Say, more bluntly than SE1 does,
   that nothing in this guide has been tried: no field report, no trial, no review by anyone, of
   any model family. Align "both copies kept in the repository" with SE1's working-directory
   wording. CHECKLISTS has no *Handing a task to an agent* section; add one or say why not. Add
   the `./se` bullet only once `se` knows this guide's documents (below).
4. **tools/check.js.** Port what applies: the rule at `---`; copies must not cite IDs their label
   lacks; the patch-release rule; markers once there is a release to mark against.
5. **README.** A *Why to trust this, and why to doubt it* section, in this guide's own voice:
   refactored from a course document by an agent, no field reports, no review, the first reports
   expected from the fall 2026 projects.
6. **CI.** `fetch-depth: 0` once there are tags; SE1 at `ref: v1.3`.
7. **archive/** when the first review or the first checked source arrives.

## Found while surveying, and belonging to SE1, not here

Three defects in `se`, to be fixed in SE1 as a patch: its label pattern fails on a line-wrapped
label; it takes a guide's own AGENTS.md (labeled with that guide's release) for a copy of SE1's;
and its working-set rows do not know this guide's documents (QUESTION_vN, PLAN_vN, LEDGER,
DATASHEET, MODEL_CARD), so in a DM1 project it would say "none yet" wrongly. Until then, read the
doctor's *working agreement* and *working set* rows here as unreliable.

## What this note did not do, and the retro

No file was changed, no repository initialized, no remote created, and none of the 73 reframings
was reviewed for meaning; that is the work. Read that should not have been needed: the checker's
source, to learn that SE1 IDs are resolved against a local checkout, so a passing checker says
nothing about the changes; one line in README saying so would have done. Assumed because nobody
wrote it down: which SE1 commit this guide was written against; CHANGELOG should say.
