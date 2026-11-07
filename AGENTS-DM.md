# AGENTS.md — data work

A working agreement for AI agents doing data work — analysis, pipelines, models, evaluation — in a
repository that follows this guide. It **extends** the
[SE1 working agreement](https://github.com/wmacevoy/software-engineering/blob/v1.7/AGENTS.md) at
SE1 v1.7, which still governs everything about the code: ownership, authority, stop/ask/proceed, writing for human
attention, verifying, finishing. This file adds what data work needs on top, and where the two
overlap, the stricter rule wins. Each rule cites the principle behind it by versioned ID — (DM1/F5),
(SE1/T3) — so that it can be argued with rather than merely obeyed.

The DM1 IDs are defined in the guide at https://github.com/wmacevoy/data-mining/tree/v1.3; its
[principle index](https://github.com/wmacevoy/data-mining/blob/v1.3/README.md#principle-index) lists
them all, and each has an anchor in its chapter (for example, `guide/03-feedback.md#f5`), and a grep for `DM1/F5.` lands on it. This copy is
from **v1.3**; what changed since, by ID, is in the guide's
[CHANGELOG](https://github.com/wmacevoy/data-mining/blob/v1.3/CHANGELOG.md). The Markdown in that repository is the
canonical text; the site at https://wmacevoy.github.io/data-mining/ renders the latest version for human readers.

Drafted 2026-10-27 by a Claude model with Warren MacEvoy, refactoring the course document
`DATA-SCIENCE.md` (drafted 2026-08-27 with Claude Opus 5); which model did the refactoring was not
recorded. Revised with Claude Fable 5.1 on 2026-10-28 against SE1 v1.3; the human signs each
release. Revised on 2026-11-01 with Claude Opus 5.5, from the field reports below, and on 2026-11-03 with the
same model, against SE1 v1.4, on 2026-11-04 against SE1 v1.5, and on 2026-11-05 against SE1 v1.6 and v1.7. These are rules
an agent wrote for agents (SE1/U9), and **most of them are untried**. One field report (`GAPS.md`)
met the 93 principles of that date with a private document review, and by that one model's judgment
46 reached real work, 20 fit only by argument, and 27 did not reach it; five of today's principles
come from it and rest on that one project. A second report, partial, is the fall 2026 course's
migration. [A trial of 2026-11-05](https://github.com/wmacevoy/data-mining-self-improvement/blob/5032133b6e/trials/2026-11-05-quickref/RESULTS.md) ran Claude agents through one synthetic task with parts of this guide loaded, and trained models on it. Nothing has yet tested a rule here on a real project's trained model, spent holdout, or production, and no person and no other model family has reviewed them. The rest are inferences from SE1's trials
and from the author's teaching, marked here once rather than on every line (SE1/W12). An agent or a
person who finds a rule wrong for their case should say so and propose the change, with what was
observed.

**Load both files at every start, from copies kept inside the working directory**: SE1's as
`AGENTS.md` at the root, where SE1's doctor and Claude Code's own read look for it, and this one as
`AGENTS-DM.md` beside it, both imported by name from `CLAUDE.md` with SE1's `QUICKREF.md`; an import from outside the working
directory may load nothing and say nothing. Then check that they loaded: a fresh session, asked without opening any file, can
quote a line of each. A rule in a file you would have to open is a rule you will not follow, and
in SE1's trials a rule that was loaded was still not applied unless a check ran on the work
(SE1/A11). So the checks this file asks for belong in the pipeline and in CI, not only here.

The repository's own guide (`CONTRIBUTING.md` or equivalent) wins over this file where they disagree.
It knows the local mechanics — where raw data lives, which split is the holdout, how to reset — and
this file only knows the principles.

## What is different about data work

**The failure is a returned value, not an exception** (DM1/U2). Code that throws is safe. Code that
returns a plausible number is not, and a confident summary of it is worse (DM1/A2). Everything below
follows from that: you are asked for evidence a run alone cannot provide.

## Authority, additions

SE1's order holds. In data work, the *question* — `QUESTION_vN.md`: the decision, the unit, the
outcome, the prediction point, the bar, and what the model is not for — sits with the requirements,
and the *analysis plan* — `PLAN_vN.md`: the split, the metric, the baseline, the slices — sits with
the design. A task that contradicts either is a question for the human, not a task. (DM1/R2, DM1/R3)

## Before you start

- **Where the repository is only partly adopted,** the guide's
  [ADOPT.md](https://github.com/wmacevoy/data-mining/blob/v1.3/ADOPT.md) says what to do for each missing
  thing: say so once, and do not invent it. (SE1/W12)
- **If the repository has `./dm`, run `./dm --doctor` once**, and in a repository of several questions
  once for each you touch, with `--repo <directory>`. It is SE1's doctor plus this guide's
  rows: both working agreements loaded, the question's one-way doors, the plan, where the holdout
  lives and whether it is in reach, every column's knowable-at, raw read-only, the ledger's writer.
  Work by what it finds; a *none yet* row is reported once, not invented to satisfy a rule. A row
  that does not bind this kind of claim is set aside in the question, with a reason, and never by
  leaving it to read as work to do. (SE1/T2, DM1/A2, DM1/R12)
- **Name the kind of claim first**: predicted, described, explained, or extracted, and what it will
  be checked against. If the question does not say, propose it. (DM1/R12)
- **Read the datasheet for every source the task touches**, and in particular, per column: units,
  sentinels, and *when the value becomes knowable*. If there is no datasheet, say so; draft one as a
  proposal rather than guess, for every kind of claim: for documents, what each file holds, how each
  page was made, and what might be printed on it. (DM1/R5, DM1/R6, DM1/U7)
- **Find where the holdout is, and do not read it.** If it is reachable from your working
  directory, say so before doing anything else. (DM1/F5, DM1/A4)
- **Find the command from raw to report.** If there isn't one, that is the first thing to raise:
  you cannot check a number you cannot regenerate. (DM1/D6)
- **Look at the data before writing code against it**: the schema, a profile, ten rows at random and
  the extremes. Never assert something about a column you have not looked at. (DM1/U1, DM1/U7)

## After a gap, additions

SE1's *After a gap* holds: a summary is recalled, not observed. In data work the summary's claims
have records to check against, and derived data is one of the things a gap tears.

- **The count of looks is in the ledger, not in the summary.** "Best of 12, holdout untouched" is
  a claim; the ledger says how many, and a look only the summary remembers was not counted. If the
  summary reports a holdout score, that look happened: count it. (DM1/F5, DM1/F6, DM1/A3)
- **Derived data you find is torn until a run from raw agrees with it.** A partition, a feature
  table, a model file: regenerate it before building on it, or say that you did not. (DM1/U2,
  DM1/D4)
- **A notebook's kernel state is a summary.** Restart and run all before trusting any cell's
  output. (DM1/U2)

## Stop and ask, additions

Before doing any of the work, stop and ask when the task would:

- **Read, evaluate on, or change the holdout**, or any evaluation set's labels. (DM1/F5, DM1/A4)
- **Change the kind of claim, the outcome definition, the unit of analysis, the prediction point, the split strategy,
  the labeling scheme, or the primary metric.** Each invalidates every result built on it.
  (DM1/R2, DM1/W2)
- **Change a metric or feature definition someone already tracks or a model already reads.**
  (DM1/D11, DM1/O7)
- **Add a data source**, or a column whose availability time you cannot state. (DM1/R6, DM1/D13)
- **Drop, filter, impute, or deduplicate rows** in a way not already in the plan — say how many rows
  and which kind, and why they are missing before how to fill them. (DM1/R5)
- **Touch data about people**: add an identifier, join to one, widen what is kept, or send rows to a
  hosted model or service, including yourself. Work from the schema and a synthetic sample unless
  told otherwise. If reading the real rows is the task itself, the answers DM1/A10 asks for are
  written first: who decided, which service sees it, every copy including yours, and when it all
  gets removed. (DM1/R9, DM1/A8, DM1/A10)
- **Change the decision threshold.** It is a business decision with prices attached. (DM1/R4)
- **Ship or retrain a model that scores people, or that the requirements name groups for, without
  the bias audit the requirements ask for** — or change which bias measure is used. Choosing between
  fairness measures belongs to people. (DM1/R10, DM1/O9)

## Raise once, then proceed, additions

- A result that looks too good for the problem: say so, and trace one prediction to its rows before
  reporting it as a win. (DM1/U3)
- A K4 you are relying on that is not written down — independence, stationarity, missing at random.
  Name it and propose where it goes. (DM1/U4)
- A slice where the model is much worse, or too small to judge — a possible bias, reported with
  its *n* and interval, not fixed silently. (DM1/T9, DM1/R10)
- A question about one case being answered with a population rate: name the reference class the
  number describes. (DM1/U8)

## Write the method, not the notebook

- **Pure transforms over frames**; I/O, clocks, and randomness at the edges and passed in. A notebook
  may call the method; it may not be the only copy of it. (DM1/D7)
- **Every stage idempotent**: overwrite its own partition, never append on re-run. (DM1/D4)
- **UTC inside, local time at the edges, one definition of "day."** (DM1/D5)
- **One definition per feature**, imported by training and serving alike. (DM1/D9)
- **Raw data is read-only.** Derived data is rebuilt, never hand-edited. (DM1/D6)

## Verify, additions

SE1's *Verify* holds — run it, watch the test fail, never weaken an assertion. In addition:

- **Assert around every join and filter you write**: row counts, key uniqueness, ranges. (DM1/T2)
- **Report the diff in the numbers**, not only the diff in the code: regenerate the golden output
  and show what moved. "Shouldn't change anything" is a hypothesis; test it. (DM1/T7)
- **The bar can veto; it cannot say yes.** What the question marks *(judged)* is passed by its named
  judge on this version: a new extract or a retrain makes the verdict stale, unless a retrain policy the
  judge approved covers it. Record it on one line
  with the versioned ID, `verdict: pass` or `verdict: fail`, `by` the judge, and the date; never give
  one. (DM1/U11, SE1/W11)
- **Name what the change must not break**, by ID and its words: `Q1/PRED-1`, the split, the holdout.
  (DM1/W7, SE1/W11)
- **Treat a run as an experiment.** Say before it what result would count against the claim, start small, and keep
  what it would cost to lose: every run gets a ledger line, and what is attached scales with the cost of running it
  again. A holdout look cannot be repeated, so everything you may keep is kept; a limit on keeping data about
  people comes first. (DM1/T13, DM1/R9, DM1/A10)
- **Compare against the baseline** on the same split, every time a model is reported. (DM1/T1)
- **Give an interval, and the seed spread** for anything trained. A difference inside the noise is
  not a result. (DM1/F7)
- **Let the code append to the ledger** for every configuration you score; never summarize the
  search from memory. (DM1/F6, DM1/A3)
- **Record data version, code version, seed, and environment** with every artifact. (DM1/D6, DM1/O1)
- **Look at the output**, not only its summary: a plot, and the rows that most disagree with the
  number. For a document, the page and not only its text layer. (DM1/U7)
- **A "none found" says how it was looked for**, and the search has been seen to find a planted
  case; open one by hand before reporting something missing across many. (DM1/U10)
- **For a summary, check what left, not only what stayed**: keep the drafts, compare them with the
  final by script, and report what a second, independent read found that the first did not. A limit
  on length gives way to the evidence. (DM1/F10, DM1/F9)
- **For retrieval, a full reading of some cases is the baseline.** (DM1/T12)

## Advising, additions

- **Say how many things were tried** before the one you are presenting: "best of 23 on validation,
  holdout untouched." (DM1/F6)
- **Say what is missing from the data** before saying what the data shows. (DM1/F8)
- **Translate results into the decision's terms**, with how often they will be wrong. (DM1/H6)
- **Report nulls plainly and promptly.** "No effect at this sample size" is a finding. (DM1/H3)
- **Label claims by source**: what you ran, what you looked at, what you only inferred. (SE1/U3)
- **Observe, do not ask.** Trust a record of what was done over an account of it: a stated preference, a reason
  given, a report of work, your own included. (DM1/U12)

## Finishing, additions

- **The number regenerates from raw** with the documented command, and you have run it. For an
  extracted claim, a script re-finds each quotation on its cited page. (DM1/W7)
- **Update the datasheet and model card** the change made untrue, in the same change. (DM1/D10)
- **Write the dead ends** in FINDINGS: what was tried, what it cost, why it failed, and what it changed,
  or why it changed nothing. (DM1/W6, SE1/W13)
- **Answer the retro questions** (SE1/A10), and add one: what did you have to assume about the data
  because nobody wrote it down?
