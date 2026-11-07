# Adopting the guide

How to make a repository doing data work follow this guide, whether the work starts today or already has
results. This file extends
[SE1's ADOPT.md](https://github.com/wmacevoy/software-engineering/blob/v1.7/ADOPT.md), which governs
everything about the code: the repository guide, the queue, findings, definition of done, CI. It adds
what is different about data, and one thing changes the order: **SE1 rebuilds requirements from the code,
which is safe. Rebuilding a question from results is not**, because the results choose the question
([DM1/F4](guide/03-feedback.md#f4)). So evidence is protected first, and anything written after the
outcome was seen says so.

**Not yet tried in full.** Written 2026-11-03 by Claude Opus 5.5 with Warren MacEvoy, after the two field reports
in [GAPS.md](GAPS.md), neither of which followed it. Step 2's commands ran on a copy of the
[fall 2026 course repository](https://github.com/wmacevoy/data-mining-wmacevoy-fall-2026) and on an empty one, and
`ml/`, in that repository's `main`, is the example below, from reading its code without running it. The course's own
migration is on its `adopt-se1-dm1` branch, and was not read for this.
Everything past step 3 is a design that nobody has followed yet. Wrong if a repository that
followed it still reports a number whose looks nobody can count, or spends more on this file than it
catches.

## For the owner, new work or old

1. **Say which questions this repository serves, then protect the evidence before any agent starts.** A question
   is one decision and the evidence spent on it, and each run of the work is one experiment on it
   ([DM1/T13](guide/06-testing.md#t13)). Two questions that share data share a holdout's looks, or split them in advance. A session can read whatever the user can, from its
   first command. Move the holdout out of the working directory or behind a permission the agent lacks,
   and decide whether rows about people may go to a hosted model
   ([DM1/A4](guide/10-agents.md#a4), [DM1/A8](guide/10-agents.md#a8)). If there is no holdout yet, decide
   where it will live before anyone splits anything; a person keeping it elsewhere is a plain answer. If
   reading the data is the task, write down what [DM1/A10](guide/10-agents.md#a10) asks before the first session.

2. **Vendor both guides, and check that they load.** From the repository's root, with clones of both
   guides beside it:

   ```sh
   SE=../software-engineering; DM=../data-mining
   for f in se se.ps1 se.py; do git -C "$SE" show v1.7:"$f" > "$f"; done
   git -C "$SE" show v1.7:AGENTS.md > AGENTS.md
   git -C "$SE" show v1.7:QUICKREF.md > QUICKREF.md
   for f in dm dm.ps1 dm.py; do git -C "$DM" show v1.3:"$f" > "$f"; done
   git -C "$DM" show v1.3:AGENTS.md > AGENTS-DM.md
   chmod +x se dm
   printf '@AGENTS.md\n@AGENTS-DM.md\n@QUICKREF.md\n' > CLAUDE.md
   ./se --hook --write
   ./dm --doctor
   ```

   An existing `AGENTS.md` or `CLAUDE.md` is not overwritten: keep SE1's copy under another name
   (`docs/SE1-AGENTS.md`), and add the three imports to the `CLAUDE.md` you have. `QUICKREF.md` is SE1's guide at one line per principle, about 5,000 words in every agent's context, and
   vendoring SE1's whole guide as its ADOPT.md describes is your choice. Do not edit the copies.
   Each copy says which release it came from; read *For copies* in the CHANGELOG before moving to a
   newer one. Then start a fresh session, and ask it to
   quote a line of each file without opening them, and look for `se --post`'s report
   ([SE1/A11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a11)).

   **Several questions in one repository:** the agreements and CI belong to the repository, and the rest to the
   question. Vendor once at the root, give each question a directory with its own `CONTRIBUTING.md` and `docs/`, and
   run `./dm --doctor --repo <directory>` for each; the agreements are found upward as far as the root.
   Start sessions at the root: the hook's `python3 se.py --post` cannot find `se.py` from a subdirectory,
   and a session started inside one has not been seen loading the agreements.

3. **Add a template when you reach its stage, not all at once.** A model card with no model is a failure
   the doctor will report, and it should. *None yet* is a true answer on day one, so **0 failed means
   nothing is wrong, not that you have adopted**; the checklist at the end is what says that. A row your kind of
   claim sets aside is *n/a*, with a reason in the question ([DM1/R12](guide/04-requirements.md#r12)).

Work that is only analysis lets the question and the plan stand in for SE1's requirements and
design, and SE1's *working set* row stays *none yet*; `QUEUE` and `FINDINGS` are still kept. Work
that also ships software (a dashboard, an API) keeps `REQUIREMENTS_vN` as well, and its mission points
at the question's `Q1/DECISION-1` rather than saying it a second time.

## The kind of claim decides what applies

Steps below are written for a prediction, the guide's commonest case. Name the kind of claim first: predicted,
described, explained, or extracted ([DM1/R12](guide/04-requirements.md#r12)). An extracted claim has no outcome,
holdout, or score: its question names the document and the unit of citation, *regenerate* means a script re-finds each
quotation on its page ([DM1/W7](guide/08-work.md#w7)), and the baseline is a full reading of some cases
([DM1/T12](guide/06-testing.md#t12)). Keep the datasheet for every kind. Where a step does not bind, say so and why, and
do not invent the field.

## Starting new work

In order, each from its template in [templates/](templates/):

4. **The question,** before looking at the outcome: the kind of claim, the decision, the unit, the outcome, the
   prediction point, the bar, and who judges what the bar cannot ([DM1/R1](guide/04-requirements.md#r1),
   [DM1/R2](guide/04-requirements.md#r2), [DM1/R3](guide/04-requirements.md#r3),
   [DM1/U11](guide/02-unknowing.md#u11)). If the answer would change nothing, stop here.
5. **A datasheet per source,** with when each column becomes knowable
   ([DM1/R6](guide/04-requirements.md#r6), [DM1/D13](guide/05-design.md#d13)). Look at the inputs freely;
   that is free ([DM1/U9](guide/02-unknowing.md#u9)).
6. **The plan and the split:** fence the holdout, choose the metric and slices, and score a dumb baseline
   ([DM1/T1](guide/06-testing.md#t1), [DM1/T9](guide/06-testing.md#t9)).
7. **One command from raw to report, and the ledger,** appended by the code that scores
   ([DM1/D6](guide/05-design.md#d6), [DM1/F6](guide/03-feedback.md#f6)). Both exist before there is
   anything worth reporting, when they are cheapest. From here each run is an experiment: small before large, and kept
   in proportion to the cost of running it again ([DM1/T13](guide/06-testing.md#t13)).
8. **The model last,** with a model card once it decides anything ([DM1/D10](guide/05-design.md#d10)).

## Refactoring work that already has results

Each step keeps what is known from being overstated. In this order, because each one changes what the
next can claim:

4. **Write down what has been spent.** Start the ledger with one row per look you can show, from run
   directories, logs, and commits, then a row saying the count before it is a minimum
   ([DM1/F6](guide/03-feedback.md#f6)). If the holdout has been looked at, it is validation data now.
   Name a new one, such as a later period or fresh rows. If none can exist, say so: the plan records that
   there is no holdout, and every result says it.
5. **Rebuild the question, marked as written after the fact.** Every line is *(inferred)* until the client
   confirms it, and the plan's *Outcome examined yet?* says *yes*, since when. A bar written now was
   not set in advance, and the old result does not count as clearing it
   ([DM1/R3](guide/04-requirements.md#r3)).
6. **Treat every existing number as unverified until it regenerates.** Move the method out of the notebooks
   into one command from raw to report ([DM1/D6](guide/05-design.md#d6),
   [DM1/D7](guide/05-design.md#d7)). Keep the old numbers as the golden output, and explain each one that
   moves ([DM1/T7](guide/06-testing.md#t7)). Anything derived that you find is torn until a run from raw
   agrees with it ([DM1/U2](guide/02-unknowing.md#u2)).
7. **Audit for leakage before trusting a score.** Write the datasheets, with when each column becomes
   knowable, and trace one prediction to its rows. Start with whatever looks best
   ([DM1/U3](guide/02-unknowing.md#u3), [DM1/R6](guide/04-requirements.md#r6)).
8. **Spend the new holdout once, and report the result, even if it is worse.** The drop from the old number
   is the migration's main finding: it goes in FINDINGS with what it changed
   ([DM1/F5](guide/03-feedback.md#f5), [DM1/W6](guide/08-work.md#w6)).

Do not delete old results, and do not rewrite history to look planned. A record that says when the plan
arrived is worth more than one that looks tidy.

### What this looks like: `ml/` in the course repository

An MNIST pipeline with a dashboard, read on 2026-11-03 in the course's `main`. This is what the steps would ask of it, not a
report of having done them.

- **Spent (step 4).** `train.py` scores the 10,000 test images when every run ends, and the dashboard
  compares variants on them, so the test set is in front of whoever chooses between models. The count is recoverable only
  from `runs/*/status.json` wherever runs still exist, and they are git-ignored, so it is a minimum.
- **No fence is possible (step 4).** MNIST's test set is public, and `mnist_data.py` caches it in one file
  with the training set, inside the working directory. The honest plan says there is no true holdout. The change that
  helps is in the code: score validation on every run, and make the test set a separate command that
  appends to the ledger.
- **A pass that expires (step 2).** The doctor said `data/mnist.npz: not here, and git-ignored` and passed,
  until the first download made it `present and readable` and failed. Run the doctor after data is fetched.

## For the agent, in a repository that is only partly adopted

SE1's section holds, and where one of these is missing, this replaces the rule that assumed it. Say each
absence once.

- **No question.** Treat the task's text as the question, marked *(inferred)*. Do not look at the outcome to
  write a better one: propose it from the inputs and the conversation, and ask.
- **No statement of where the holdout is.** Ask before evaluating anything. Do not go looking for it, since
  finding it is reading it ([DM1/A4](guide/10-agents.md#a4)).
- **No ledger.** Say so, and report your count of what you scored in this session as a minimum. Propose the
  ledger; do not reconstruct earlier looks from memory ([DM1/A3](guide/10-agents.md#a3)).
- **No datasheet.** Draft one as a proposal, with *Knowable at* marked *(inferred)* wherever you did not
  measure it ([DM1/R5](guide/04-requirements.md#r5)).
- **Results with no command behind them.** Report them as unverified, and do not build on them
  ([DM1/D6](guide/05-design.md#d6)).

## Adoption checklist

- [ ] The holdout was out of reach, or recorded as impossible, before the first agent session.
      ([DM1/A4](guide/10-agents.md#a4))
- [ ] `AGENTS.md` and `AGENTS-DM.md` are unedited copies that name their release, and a fresh session quoted
      a line of each. ([SE1/A11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a11))
- [ ] `se --post` ran at the start of a real session, and `./dm --doctor` reports no failure, after the
      data has been fetched. ([SE1/T2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t2))
- [ ] The question names its kind of claim and a judge, and whatever was written after the outcome was seen says so.
      ([DM1/U11](guide/02-unknowing.md#u11))
- [ ] The plan names the split, the baseline, and the slices. ([DM1/T1](guide/06-testing.md#t1))
- [ ] Every source has a datasheet with no column missing *Knowable at*. ([DM1/R6](guide/04-requirements.md#r6))
- [ ] One documented command goes from raw to every reported number, and CI runs the checks.
      ([DM1/D6](guide/05-design.md#d6))
- [ ] The code that scores appends to the ledger, and earlier looks are counted or marked as a minimum.
      ([DM1/F6](guide/03-feedback.md#f6))
- [ ] Refactoring: each old number regenerated or is labeled unverified, and the new holdout's result,
      even if worse, is in FINDINGS. ([DM1/W6](guide/08-work.md#w6))
- [ ] Everything in SE1's adoption checklist that applies.
