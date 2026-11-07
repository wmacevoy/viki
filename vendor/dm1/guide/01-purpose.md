# 1. Purpose

What data mining is for, what makes a claim worth acting on, and who is affected by an analysis
without ever seeing it. Read with [SE1 chapter 1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md),
which still applies; this chapter says where the noun changes.

---

### <a id="p1"></a>DM1/P1. Data miners produce claims worth acting on, on time, within the data and compute they have, managing discomfort for the team, the client, and the people in the data

*Reframes [SE1/P1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p1).*

Software engineering asks *does the thing work*. Data mining asks *is the claim true* — and, before
that, *would anyone do anything differently if it were*. Mostly the same discipline pointed at a
different noun.

The four constraints of SE1/P1 still trade against each other: reliability, time, resources, and
discomfort. Data work adds a fifth that is usually the honest one to move: **the question.**
Narrowing it — one lot instead of all of them, the next two hours instead of the season, "which lots
fill" instead of "find patterns" — is not failure. It is the most common form of success.

Resources here are mostly not people. They are rows, labels, compute, and the hours of whoever knows
what the columns mean.

> **Ask:** "Which of these is fixed — and is the question the one that should move?"

### <a id="p2"></a>DM1/P2. Accurate is not true: a model is correct about a dataset; a claim is true about a world

*Reframes [SE1/P2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p2).*

Accuracy is a property of a model measured against a held-out sample. *True* is a property of a
claim that has to survive a world containing selection bias, drifting definitions, revised
measurements, labels that are proxies for what you care about, and people who behave differently
once the model is watching.

The gap between those two sentences is where most of the work is. A forecast that scores well on
last season's games says nothing about the first season with a new concert venue next door, and
nothing in the score will say so.

SE1/P2 says reliable means *it says so when it doesn't work*. The data form is the same: a claim
worth acting on carries its interval, its population, and the conditions under which it stops
holding.

> **Ask:** "Is this a fact about the dataset, or about the world — and what would have to be true
> for it to carry over?"

### <a id="p3"></a>DM1/P3. The people in the data are not in the room

*Reframes [SE1/P3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p3).*

SE1/P3 names the team, the client, and the users. Data work adds a party who appears in none of
those: **the people whose rows you are modeling.** In software the user and the affected person are
usually the same. In data they routinely are not. The person in the row did not agree to the
question, will never see the output, and meets only its consequences.

| Whose                  | What it looks like                                     | What reduces it                                    |
| ---------------------- | ------------------------------------------------------ | -------------------------------------------------- |
| The client             | a number that moved and nobody can say why               | frequent, honest, boring status — including nulls  |
| The team               | re-running everything at 2am, nobody daring to touch it  | slack, small steps, one command back to raw         |
| The people in the data | being measured, misclassified, or surprised by a decision they cannot see | minimization, aggregation, recourse |

The transition is invisible in code. `df.groupby("lot")` and `df.groupby("plate")` look the same; the
second one is about people ([DM1/R9](04-requirements.md#r9)).

Discomfort is still a leading indicator. When someone stops wanting to re-run the pipeline, that is
information about the pipeline.

> **Ask:** "Whose rows are these, and what happens to them because of this?"

### <a id="p4"></a>DM1/P4. A practice that relieves the fear without reducing the risk is worse than nothing, and data work is full of them

*Reframes [SE1/P4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p4).*

Every practice retires a named fear (SE1/P4). In data work:

| Practice                              | The fear it retires                                       |
| ------------------------------------- | --------------------------------------------------------- |
| Recorded seeds                         | "The result was luck."                                     |
| One command back to raw                | "I can't reproduce the number in the slide."               |
| A dumb baseline                        | "I don't know if my model beats the mean."                 |
| A holdout, touched once                | "I fit the thing I was measuring with."                    |
| Grouped and time-ordered splits        | "My folds shared a subject, a site, or a future."          |
| Row-count assertions across a join     | "I silently multiplied my dataset by three."               |
| An analysis plan written first         | "I found this by looking forty times."                     |
| Showing a domain expert the plot       | "That sensor was being replaced that week."                |

The dangerous ones look exactly like these and retire nothing: a train/test split that leaks; a
cross-validation whose folds share a lot and a night; 99% accuracy on a 99%-negative class; a
dashboard tracking a proxy that stopped correlating last quarter; a reproducibility badge on a
pipeline nobody has re-run from cold. They spend the alarm and leave the danger in place — and
unlike in software, there is often no later moment when reality forces the issue. A wrong bridge
falls down. **A wrong number just gets used.**

> **Ask:** "Which fear does this retire — and would it still retire it if the split leaked?"

### <a id="p5"></a>DM1/P5. The deliverable is the claim and the means to re-derive it, not the notebook

*Reframes [SE1/P5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/01-purpose.md#p5).*

The deliverable is the claim, plus the ability of someone else to reproduce it, challenge it, and
update it next quarter. "It ran in a kernel I have since restarted" is a claim about a kernel.

A notebook is an excellent instrument and a poor artifact: it records the cells, not the order you
ran them in, and the order is the program. Keep notebooks for looking; keep the method in code a
command can run from raw data ([DM1/D6](05-design.md#d6), [DM1/D7](05-design.md#d7)). Where the claim
is prose, the means is a check instead of a re-derivation ([DM1/W7](08-work.md#w7)).

> **Ask:** "Could someone who has never met me regenerate this number from raw data, and say why it
> is what it is?"

---

Next: [2. Unknowing](02-unknowing.md) — where data work manufactures erroneous certainty.
