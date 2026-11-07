# Gaps

Where the guide falls short. In SE1 this file holds field reports — what happened when the guide met
a real project — and each entry is closed by changing the guide, or by a sentence saying why not
([SE1/P4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p4)).

**There are two field reports**, below, and neither reaches prediction. The course projects are still expected to give
the first reports on that. The rest of this file lists where the guide is known to be thin, so that nobody mistakes silence
for coverage.

## Field report: a confidential document review, 2026-11-01

One person preparing a decision on dozens of confidential cases about people, from thousands of pages of PDFs: retrieval,
extraction, and a brief per case, written by a language model, with each quotation checked against its page. Nothing is trained,
and nothing is predicted. The project is private, so this report gives kinds and counts only. Each principle's verdict, and
exact proposals, were in a review, `REVIEW-v1.md`, removed on 2026-11-03 now that this guide incorporates it. **Redacted for
confidentiality:** before it was pushed, the review and this report were made more general about the project, so some evidence
here is vaguer than what was observed, and nothing has been checked against the project by anyone without access to it. The
redacted review stays in git history. Ordered by what each cost:

1. **A limit on length cost evidence, and every check still passed.** Briefs dropped evidence of several kinds to fit a cap, against the written procedure. Citations stayed exact, because a citation check can't see an
   omission. Comparing drafts with finals by script led to 37 audits, restoring from 0 to about 70 items per brief. Two authors'
   own accounts of their cuts had understated them. The limit was the agent's own idea. **Closed 2026-11-01:** [DM1/F10](guide/03-feedback.md#f10), and "a limit is a target too"
   in [DM1/F9](guide/03-feedback.md#f9).
2. **Absences were reported that weren't so.** A required document, reported missing across one group of cases, sat on the last
   page of another file. A scan for printed identifiers reported nothing because it had found nothing to search for, and a
   form's layout then hid another. **Closed 2026-11-01:** [DM1/U10](guide/02-unknowing.md#u10).
3. **The claim wasn't a prediction.** [DM1/R2](guide/04-requirements.md#r2) has no outcome or prediction point to fix here, and the
   doctor gave 8 of 23 rows as *none yet*, with no way to say they don't apply. **Closed 2026-11-01:** [DM1/R12](guide/04-requirements.md#r12), and the doctor's
   KIND-n and *n/a*.
4. **Identifiers reached derived data.** Identifiers printed on old forms went into a text cache and a search index, and
   deleted rows stayed in the store's old versions until purged. **Closed 2026-11-01:** [DM1/O8](guide/07-operating.md#o8).
5. **One read isn't enough.** Ten cases were read twice by independent readers, who saw no earlier brief until their own passed its check: two early
   briefs, then five chosen as highest priority and three at random, with a bar fixed before any result. Both bars were met. Every read missed something the
   other caught, and second reads missed as much as first ones. In the eight measured, the reads agreed on the main facts, and none reversed a finding. The
   clearest catch was a pair of marks on a table row, visible only on the page image, one of which would have changed what the row meant. Both reads
   are one model under one procedure, so a shared blind spot wouldn't show. **Closed 2026-11-01:** [DM1/A9](guide/10-agents.md#a9), [DM1/F10](guide/03-feedback.md#f10).
6. **Agents had to read the data.** [DM1/A8](guide/10-agents.md#a8) doesn't cover the case where reading is the task. **Closed
   2026-11-01:** [DM1/A10](guide/10-agents.md#a10).

**What held:** [DM1/U2](guide/02-unknowing.md#u2) named the pattern behind several faults before they were understood.
[DM1/F6](guide/03-feedback.md#f6) logs caught unread pages. [DM1/T1](guide/06-testing.md#t1) and
[DM1/D8](guide/05-design.md#d8) exposed that hybrid search has never been compared with keyword search, which is still open.
[DM1/A2](guide/10-agents.md#a2) described exactly what happened: a rule in the procedure was read and broken, and a script
changed the work.

**What this says nothing about:** trained models, holdouts spent at a decision, or anything in production. 27 of the 93 principles
didn't reach this project, and 20 more fit by argument only. **Closed:** each proposal is in the guide, pending the owner's read. Its status, by
item, was at the top of that review, and CHANGELOG lists it. **Still open:** hybrid search against keyword search, in that project.

## Field report: the course's migration, 2026-10-29, partial

The fall 2026 course repository, moving to SE1 and DM1: steps 0 to 3 of 5, so one project of five has a question, a plan, and
datasheets, and that one is descriptive and teaches. Its `FINDINGS.md` is the record, and its step 5 is still to send the full
report by ID (its `QUEUE.md`, N6). What it had asked of this guide so far:

1. **The doctor claimed what it had not done.** The agreement row said it had observed loading, having read the imports.
   **Closed 2026-11-01:** it says configured.
2. **A check that could not run passed.** The full check, as written, stopped with an ambiguous task name, and CI had been red
   for about two and a half months while the doctor passed the line. **Partly closed:** `raw to report` now says *configured, not run*,
   and asks what the command makes. No doctor row runs a command, so this is a sentence and not a defense. Running it as
   written, before writing it down, is what would have found this.
3. **The holdout row read every backtick as a path.** **Closed:** a *none* is a true none yet (`b6532cb`), and a command is not a
   path. The same error was then made again here: a fix on this branch had the raw row read every backtick, and on the course
   repository it reported 303 writable files where the truth is 80. A run on that repository caught it, not the fixtures, so the
   row reads the first path and names the rest.
4. **`field()` read only a field's first line.** **Closed:** `d0cd786`.
5. **The templates are shaped for a prediction.** About half the question and plan said *not applicable*. **Closed 2026-11-01:**
   [DM1/R12](guide/04-requirements.md#r12), a second project for the same finding, and the plan row takes a baseline that says so.
6. **One repository, five projects, and `docs/` is the published site.** The doctor read the root, the CONTRIBUTING template has
   one *Raw to report*, and a working set in `<project>/docs/` was invisible unless `--repo` pointed there, where the agreement rows
   then failed. **Closed 2026-11-03, the owner's word:** a run of the work is one experiment on one question ([DM1/T13](guide/06-testing.md#t13)), and *project* is no longer a term here. A doctor check reads one question's directory. This line had named as the owner's decision, on 2026-11-02, "one run is one project", which the owner did not remember, and no transcript here records. The agreement rows look upward to the repository's root,
   a check at the root points at any directory with a question, and each keeps its own `CONTRIBUTING.md` and `docs/`. A session started
   inside one has not been observed loading the agreements, and the row says so.
7. **This guide adds nothing at session start.** `dm.py` has no `--post` or `--hook`. **Open,** and possibly a null: whether
   `AGENTS-DM.md` loaded at start is enough.

**What held:** [DM1/R6](guide/04-requirements.md#r6), whose *Knowable at* column found what the course's own lesson table doesn't
list. [SE1/A11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a11), in that running the baseline
before trusting a handoff note found a failure the note lacked.

## Known thin places

### 1. Causal claims

The guide is about predictive and descriptive claims: what will happen, what patterns exist. It says
almost nothing about *why* — whether changing X would change Y. Most real decisions are causal
("if we open lot C earlier, will the queue at B shrink?"), and a predictive model answers them badly
and confidently. Confounding, experiments, and observational causal inference need principles of their
own.

**Open:** a chapter, or principles inside chapters 3 and 4, on when a claim is causal and what
evidence that requires.

### 2. Streaming and online learning

Chapter 7 assumes models trained in batches and deployed. A model updated continuously on live data
has no deploy to gate, and its feedback loop ([DM1/O11](guide/07-operating.md#o11)) runs at the speed
of the data.

### 3. Privacy techniques are named, not taught

[DM1/R9](guide/04-requirements.md#r9) and [DM1/O8](guide/07-operating.md#o8) name the threats
(re-identification, membership inference) and the cheapest defense (not keeping the data). They do not
cover aggregation thresholds, differential privacy, or synthetic data, or when each is enough.

### 4. Language models in the pipeline

[DM1/A9](guide/10-agents.md#a9) is one principle covering prompts, nondeterminism, judges, and
contamination. Retrieval systems — chunking, embedding choice, retrieval evaluation — get one line
in [DM1/D2](guide/05-design.md#d2), and now [DM1/T12](guide/06-testing.md#t12) on how to evaluate them.
Chunking and embedding are still untouched, and the course's retrieval lab is the obvious next field report.

### 5. Untested claims about readability

The guide's chapters are short because SE1 carries the long arguments. Whether a reader who has not
read SE1 can follow the *Reframes* links and come away with the idea — rather than a fragment of it —
has not been tested, with people or with agents.

### 6. Recording the environment without committing the lock

[DM1/O1](guide/07-operating.md#o1) asks that each result record the resolved environment, so that it
works whether or not a lock file is committed.
[SE1/O2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o2) says to
commit the lock. The course repository leaves its lock file uncommitted, because it resolves
differently across students' machines.

**Open:** whether that is a gap in SE1/O2, or a local exception that should be written down as one.

### 7. The doctor has met two repositories

`dm --doctor` (`dm.py`, beside SE1's `se.py`) was written on 2026-10-28 from the
principles, not from real work, and has since met the two above; neither trained a model. Every row is something this guide says must be mechanical rather
than told ([DM1/A4](guide/10-agents.md#a4), [DM1/D6](guide/05-design.md#d6),
[DM1/F6](guide/03-feedback.md#f6), [DM1/R6](guide/04-requirements.md#r6)), and every threshold and
heuristic in it is invented: which files count as the ledger, what a placeholder looks like, what
*readable* means for a holdout. Its tests are fixtures written beside it, which is the guard tested
against its own stub that SE1's GAPS warns about, and the real repositories keep finding what those
fixtures didn't: on 2026-11-03, unfilled templates and a copy of the course repository found four more
false passes (CHANGELOG). Which kind of claim may set aside which row is invented on the same terms.

Two things it exposed while being written:

- **The two AGENTS.md copies had no names.** Closed 2026-10-29: SE1's stays `AGENTS.md` at the root,
  where SE1's doctor and Claude Code's own read look for it, and this guide's is `AGENTS-DM.md`
  beside it, both imported from `CLAUDE.md` (AGENTS.md's header, the README, the CONTRIBUTING
  template). The doctor still tells them apart by which guide they link to, so a repository that chose
  other names is told the guide's, not failed. Whether two files loaded from one CLAUDE.md behave
  as one agreement is untested, like the rest.
- **"Readable from the working directory" has no definition** ([DM1/A4](guide/10-agents.md#a4)).
  The doctor treats a path outside the repository, or inside it but absent and git-ignored, as out
  of reach, and a tracked or present file as in reach. An agent with a shell can read any path the
  user can; the thing that would make a holdout truly unreachable, a permission or another host, is
  not something a doctor can see from the repository.

**Wrong if** the first course project runs it and every row that failed was one the owner already
knew about, or a row that passed hid the thing that went wrong. The first field report found the second: `raw to report` passed on a
command that doesn't make the project's main product. The row now asks what the command makes and says it didn't run it, which a
project can answer falsely. **Open:** work that trains a model.

### 8. Essential K1 in data work is a draft of a decision without a trial

[DM1/U11](guide/02-unknowing.md#u11), the *(judged)* mission in the question, and the verdict line came
over from SE1 v1.4 on 2026-11-03, where they are the author's decision with no trial of their own. Here they
are one step further from evidence: nobody has yet asked a client for a verdict under this rule, and
`dm --doctor` does not read one. The first field report comes near it: a passing citation check retired no fear about
omission. **Wrong if** the first verdicts recorded that only repeat the
bar, or nobody can name a judge. **Open:** a question with a recorded verdict.
