# 8. Work

The shape of data-mining work: the six steps under their usual data names, where to slow down,
the first slice, time boxes, the deadline pressure to find something, and what *done* means for a
claim. Read with [SE1 chapter 8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md);
its queue and findings templates work unchanged.

---

### <a id="w1"></a>DM1/W1. CRISP-DM is the six steps, with the loop back to the question drawn in

*Reframes [SE1/W1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w1).*

The standard process model for data mining, CRISP-DM, is SE1/W1's six steps under other names:

| SE1/W1       | CRISP-DM                                   | The product                                              |
| ------------ | ------------------------------------------ | -------------------------------------------------------- |
| Requirements | Business understanding, data understanding | the question, the decision, the unit, the outcome, the datasheet |
| Design       | Data preparation (its plan)                | the pipeline, the split, the analysis plan                |
| Develop      | Data preparation, modeling                 | transforms and models a second person can re-run          |
| Test         | Evaluation                                 | evidence the claim holds, against a baseline, with intervals |
| Deploy       | Deployment                                 | the claim or model in use, and a way back                 |
| Maintain     | (monitoring; usually left out)             | is it still true                                          |

CRISP-DM draws one arrow SE1/W1 only implies: from *evaluation* back to *business understanding*. In
data work that arrow is the common case. Evaluation regularly shows that the question was wrong — the
outcome was the wrong definition, the decision needed a different horizon — and the right next step
is to go back and rewrite the question, not to tune the model.

> **Ask:** "Did the evaluation answer the question — or tell us the question was wrong?"

### <a id="w2"></a>DM1/W2. Slow down around the one-way decisions: the question, the unit, the outcome, the split, the labels

*Reframes [SE1/W4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w4).*

SE1/W4 chooses how carefully to work per decision, by the cost of being wrong once. Data work has an
unusual distribution of those costs: the expensive decisions all arrive early, when nothing has been
built and they feel cheap.

| Decision                                     | Cost of being wrong once                  | So                                              |
| -------------------------------------------- | ----------------------------------------- | ----------------------------------------------- |
| Model class, hyperparameters, a feature      | a re-run                                   | loop fast                                       |
| Plot styling, report layout                  | minutes                                    | loop fast                                       |
| Outcome definition, unit, prediction point   | every feature, label, and result rebuilt   | write it, read it to the client, get an objection |
| The split strategy                           | every reported number invalid              | write it, have someone try to break it          |
| The labeling scheme                          | the labels, and the budget that bought them | pilot on fifty rows, measure agreement          |
| Spending the holdout                         | the only unbiased estimate you had         | once, at a decision ([DM1/F5](03-feedback.md#f5)) |

Most arguments in this field run at the wrong intensity: hours on the model class, minutes on the
definition of the outcome.

> **Ask:** "If this choice is wrong, what do we have to redo?"

### <a id="w3"></a>DM1/W3. Build a thin end-to-end slice first: one source, one feature, a baseline, the real chart

*Reframes [SE1/W6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w6).*

The usual plan is horizontal: clean all the data, then engineer all the features, then model, then
report. Horizontal layers all finish at the same time — never — and the first contact between the
question and the data happens in the last week.

Instead, on day one: fetch one lot, compute one feature, score the baseline on a proper split, and
produce the actual chart the client will look at. Show it. Then thicken: more lots, more features, a
real model, each a larger experiment that the last one cleared ([DM1/T13](06-testing.md#t13)). Every K2 the slice exposes — the column that means something else, the timezone, the
missing peak — is exposed while there is time to respond.

> **Ask:** "Is there a version, however bad, that runs from raw data to the client's chart today?"

### <a id="w4"></a>DM1/W4. Time-box exploration and cleaning, and end each box on a fence

*Reframes [SE1/W8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w8) and [SE1/W9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w9).*

Exploration is the data form of SE1/W8's spike, and it falls down the waterfall the same way: one
more plot, one more join, one more week, and no claim at the end. Give each exploration and cleaning
pass a time box ([DM1/U5](02-unknowing.md#u5)), and end it on a **fence** — something proved, not
something attended:

- a baseline actually scored on the real split;
- a datasheet entry written for a source, including when each column becomes knowable;
- a plot a domain expert actually objected to;
- the pipeline run from a cold cache, from raw, with exit status zero;
- a null result written down.

A status meeting is not a fence. Neither is a notebook cell that ran.

> **Ask:** "What will we know when this box ends that we don't know now?"

### <a id="w5"></a>DM1/W5. The deadline pathology here is finding something; change the terrain, not the willpower

*Reframes [SE1/W7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w7).*

SE1/W7's avoidance gradient applies, and in data work the behavior the spike produces is specific:
one more peek at the holdout, one more subgroup where the effect is significant, one more outlier
rule that helps, the sanity plot skipped, the null quietly not mentioned. **The pressure to find
something** is this field's characteristic deadline pathology, and it never feels like cheating
while it is happening. It feels like trying harder.

When the deliverable is a summary, there is a second pathology: the pressure is to be concise, and
what it costs is omission ([DM1/F10](03-feedback.md#f10)). That also feels like doing the job well.

Change the terrain:

- **Make re-running cheap.** If raw to report is one command and a few minutes, "don't touch it, it
  works" stops being the safe move.
- **Write the plan and the bar first** ([DM1/R3](04-requirements.md#r3)), so a late change shows up
  as a departure.
- **Let the ledger count** ([DM1/F6](03-feedback.md#f6)), so the forty-first try is visibly the
  forty-first.
- **Report nulls when they happen** ([DM1/H3](09-people.md#h3)), so week eleven is not the first time
  anyone hears the effect isn't there.

> **Ask:** "What would make the last week before this deadline boring?"

### <a id="w6"></a>DM1/W6. Write down what you tried that did not work

*Reframes [SE1/W10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w10) and [SE1/W13](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w13).*

SE1/W10 keeps the working set in the repository. For data work the working set adds three things:
the question ([QUESTION template](../templates/QUESTION_v1.md)), the analysis plan
([PLAN template](../templates/PLAN_v1.md)), and the ledger of everything scored
([LEDGER template](../templates/LEDGER.md)).

The cheapest documentation in data work, and the one nobody writes, is what did not work. Without
it, the next person — often you, months later — re-runs the same dead end, and the count of things
tried, which is part of the result ([DM1/F4](03-feedback.md#f4)), is lost with it. A dead end is a
finding: write it in FINDINGS with what it cost and what would have found it sooner, and close it
with what it changed, or why it changed nothing. In data work the change that lasts is an assertion in
the pipeline, a column note in the datasheet, or a line in the plan; a dead end remembered by the
person who walked it leaves with them.

> **Ask:** "If someone repeated this work from our repository, which dead ends would they walk
> into again?"

### <a id="w7"></a>DM1/W7. Done means someone else regenerated the number from raw and could argue with it

*Reframes [SE1/W11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/08-work.md#w11).*

SE1/W11: done means all of it, one concern per change, reviewed by someone else. For a claim, *all of
it* is:

- the number, with its interval, its slices, and the count of things tried;
- the command that regenerates it from raw, run by someone other than the author, producing the same
  number within the stated tolerance;
- the datasheet and, for a model, the model card, updated;
- the K4s written down, and read to the client ([DM1/H2](09-people.md#h2));
- a reviewer who read the *data handling* — the joins, filters, splits, and fills — not only the model
  code ([DM1/A6](10-agents.md#a6));
- for what the question marks *(judged)*, the named judge's verdict on this version, recorded on one
  line with the versioned ID, `verdict: pass` or `verdict: fail`, `by` the judge, and the date
  ([DM1/U11](02-unknowing.md#u11));
- a description naming what the change must not break, by ID and its words (`Q1/PRED-1`, the split,
  the holdout), so the reviewer can ask about each.

For an extracted claim ([DM1/R12](04-requirements.md#r12)), *regenerate* means a script re-finds each
quotation on its cited page, and a second reader reads a sample whole. Prose can be re-checked, not
re-derived. That checks words and pages, not numbers or omissions ([DM1/F10](03-feedback.md#f10)).

> **Ask:** "Has anyone but the author regenerated this from raw?"

---

Next: [9. People](09-people.md) — the expert, the client, and the person in the row.
