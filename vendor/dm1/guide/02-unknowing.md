# 2. Unknowing

The five kinds of not-knowing (`K0`–`K4`) from [SE1 chapter 2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md),
applied to datasets and methods — and why data work is unusually good at producing the dangerous
kind. The labels are defined there and in [GLOSSARY.md](../GLOSSARY.md).

---

### <a id="u1"></a>DM1/U1. Mining is discovering, in the right order and at the right price, which category each column and each step belongs to

*Reframes [SE1/U2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u2).*

| From | To     | How, in data                                                                                       |
| ---- | ------ | -------------------------------------------------------------------------------------------------- |
| K0   | stays  | An assertion in the pipeline beats a sentence in a README, and both beat a fact in your head.       |
| K1   | K0     | Look: profile the column, plot the distribution, read the data dictionary, ask who collected it. The only category you can simply decide to spend money on, and some of it will not convert ([DM1/U11](#u11)). |
| K2   | K1     | Run end to end on real data early, plot the residuals, show a domain expert, score on a period you did not develop on. You cannot search for these; you can only arrange to be surprised sooner. |
| K3   | K0     | A check that could have failed, a plot you actually looked at, a baseline that could have won, a second person who does not share your assumption. |
| K4   | chosen | Name the falsehood, write down where it is load-bearing ([DM1/U4](#u4)).                             |

K2 in data work is not a risk; it is a certainty, and it usually arrives as a sentence: *that column
means something different before 2024*. Plan for the sentence.

> **Ask:** "Have I looked at this column, or only at its name?"

### <a id="u2"></a>DM1/U2. Data work manufactures K3, because the failure is a returned value, not an exception

*Reframes [SE1/U3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u3) and [SE1/U7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u7).*

Software that is wrong tends to stop. Analysis that is wrong tends to continue and produce a chart.

A join that silently duplicates rows returns a DataFrame. A timezone off by six hours returns
timestamps. A `dropna()` that removes exactly the interesting cases returns a smaller, cleaner,
entirely misleading table. A `try/except` around a fetch turns an outage into a quiet day. Every one
of these is a K3 factory in the sense of SE1/U7 — it keeps saying "fine" with no way to say "wrong" —
and pandas ships with dozens.

SE1/U7's *torn work* has a data shape, and it is the commonest of these. A pipeline interrupted
between stages leaves a partition half-written that reads as current. A model trained on an extract
that stopped on Tuesday is a model, with a score. A notebook run out of order holds a kernel state
that no top-to-bottom run of its cells produces, and every cell still returns. Nothing in the
output says which of these happened.

SE1/F4 says software has no mass, so nothing sags to warn you. Data has less than none: a wrong
number has the same shape as a right one, and the most likely thing to happen to it is that it gets
used.

The medicine is the one SE1/U7 names: give each of these a way to say *wrong*. Row counts asserted
across every join, keys asserted unique, ranges and units asserted at the boundary
([DM1/T2](06-testing.md#t2)). For torn work: every stage overwrites its own partition and never
appends ([DM1/D4](05-design.md#d4)), the cold run from raw is the fence ([DM1/W4](08-work.md#w4)),
and on resuming, what you find is torn until a run from raw agrees with it.

> **Ask:** "What here would return a plausible number if it were wrong?"

### <a id="u3"></a>DM1/U3. Leakage is the canonical K3: it feels like a good result

*New.*

Leakage is any path by which information about the answer reaches the model at training time that
will not be there at prediction time: a feature that encodes the label, a split with the same lot
and night on both sides, a normalization computed before the split, an "arrival time" column that is
really the time the gate scan was uploaded.

Leakage does not feel like a mistake. The metric improves and cross-validation agrees, because
**everything you would normally use to catch an error is the thing being fooled.** That is why
leakage is found by reading provenance — when each value became knowable ([DM1/R6](04-requirements.md#r6))
— not by reading the score.

Which gives a rule with no counterpart in software: **a result too good for the problem is a bug
report until proven otherwise.** "The AUC is 0.97" is K3 until you have traced one prediction back
to the rows that produced it and checked that each of them existed at the prediction point.

> **Ask:** "Could the model have known this at the moment it would have to predict?"

### <a id="u4"></a>DM1/U4. Every model is a chosen falsehood; name the direction it is wrong

*Reframes [SE1/U4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u4).*

Box's *all models are wrong, some are useful* is a sentence about K4, and the half that matters is
unspoken: **wrong in a specific, nameable direction.** A catalogue, every entry false and every
entry in use:

- The rows are independent. (They share a sensor, a night, an event, a household.)
- The sample represents the population. (It represents whoever was easy to measure.)
- The past distribution is the future distribution. (Stationarity is a hope with a p-value.)
- The label is the truth. (It is what a tired person clicked, under instructions from last year.)
- Missing is missing at random. (A sensor is most likely to drop off the network when the lot is
  busiest.)
- A day is twenty-four hours. (Two a year are not; [DM1/D5](05-design.md#d5).)
- De-identified means anonymous. (It means nobody has tried yet.)

You cannot analyze anything without these. The work is choosing them deliberately, writing them
down, and knowing the blast radius: independence fails and your intervals are too narrow;
stationarity fails and your backtest was fiction. **A K4 you have chosen is an asset; the same
falsehood held unknowingly is K3** — and the distance between them is one written sentence.

> **Ask:** "Which of these are we assuming, and where is it written?"

### <a id="u5"></a>DM1/U5. Estimate the category: time-box the cleaning, estimate the modeling

*Reframes [SE1/U5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u5).*

"Two days to clean this dataset" is a K0 statement about K2 work: you cannot estimate the time to
discover things you do not know are there. Cleaning and exploration get time-boxed, and each box ends
on a fence ([DM1/W4](08-work.md#w4)). Modeling from a known-good feature table, with a fixed metric
and split, estimates fine.

Say which kind you are handing over. "I'll know by Thursday whether the gate data is usable" is
honest. "The model will be done Thursday" usually is not, because it hides a cleaning box inside a
modeling estimate.

> **Ask:** "Is this an estimate, a range, or a time box — and is there cleaning hidden inside it?"

### <a id="u6"></a>DM1/U6. A model always answers; give it a way to say "I don't know"

*Reframes [SE1/U7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u7).*

A classifier asked about an input unlike anything it was trained on does not raise. It returns a
class, often with high confidence. That is the purest K3 factory in the field: it has no way to say
*wrong*, and no way to say *I have never seen this*. A checkpoint from a retrain that was interrupted
answers as confidently as the finished model would have, and no prediction says which it came
from: torn work (SE1/U7), served.

Three medicines, cheapest first:

- **Check the input against the training envelope** — ranges, categories never seen, a lot that
  did not exist last season — and refuse or flag outside it.
- **Calibrate**, so that 0.8 means right about 80% of the time, and check that it does on data you
  did not fit on.
- **Abstain**: let the model return *no answer* below a confidence, and count how often it does.
  A rising abstention rate is a drift alarm that needs no labels ([DM1/O3](07-operating.md#o3)).

> **Ask:** "What does this model say when it is shown something it has never seen?"

### <a id="u7"></a>DM1/U7. A summary statistic is a concussion: look at the rows

*Reframes [SE1/A11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a11).*

SE1/A11 says a summary of a session drops things without knowing what it dropped, and must be
checked from outside. A mean, a correlation, or an accuracy is the same thing for a dataset.
Anscombe's four datasets share their means, variances, correlation, and regression line, and look
nothing alike.

Checking from outside, here, means a plot and a handful of raw rows — chosen at random, and then
chosen at the extremes. Before you trust a summary, look at the rows that most disagree with it.

For a document, the text layer is a summary of the page. Look at the page: recognition garbles
handwriting, extraction collapses a table's empty columns so a figure lands in the wrong one, and a
shortened command's output hides the end of every page.

SE1/A11's later trials add a second injury, and it is this guide's problem in one sentence: a rule
freshly read was still left out of the work, and the only thing that got it in was a check that ran
on the work without the agent choosing to run it. *Look at the rows* is such a rule. Left as a
habit, it is the step the deadline skips ([DM1/W5](08-work.md#w5)). So have the command that
produces the number also produce the plot and print the ten rows that most disagree with it, every
run, and put the knowable-at check in the pipeline ([DM1/T2](06-testing.md#t2)), not in the
instructions.

> **Ask:** "What did this number average away — and have I looked at the rows that disagree?"

### <a id="u8"></a>DM1/U8. People ask Prob(result | me); the data holds Prob(result | the rows it counts as like me)

*New.*

Most questions put to a model are about one case, with a yes-or-no or categorical answer: will *I*
get the loan, will *this* lot fill tonight, is *this* reading a fault? What data can estimate is a
rate in a population: among rows like this one, how often did it happen? A model conditions on the
columns it has, which narrows the population — but never down to you. Which rows count as "like
you" is decided by which columns were recorded and how the model pools them, not by what is true
about you.

Two consequences, both easy to miss:

- **A probability for one binary outcome is a property of a reference class, not of the case.**
  "70%" means: of a hundred cases the model cannot tell apart from yours, about seventy happened.
  Yours will happen or it won't. Say it that way ([DM1/H6](09-people.md#h6)), and name the class.
- **Putting yourself in the data does not fix it.** Your row is one of *n*, so the estimate for you
  is mostly other people's outcomes. What makes you different from them is exactly what the columns
  do not hold.

This is the common K3 behind most bias. The number looks like an answer to the question asked, so
nobody checks that it answers a different one. And the gap is not spread evenly. A model pools sparse
groups toward the majority — shared parameters and regularization exist to do exactly that — so a
group that is small, under-recorded, or measured differently gets an answer shaped by everyone else.
For the majority, Prob(result | class) is close to Prob(result | me); for a minority it can be off
**systematically, in one direction.** That systematic gap is bias, and it is found only by looking
for it ([DM1/R10](04-requirements.md#r10), [DM1/O9](07-operating.md#o9)).

In the running example, a driver asks, "Will I find a space in lot C?" The model knows how often lot
C was full by 6:40 on nights like this. Whether *this* driver finds a space depends on when they
arrive, which entrance they use, and what else happens tonight — none of which is in the rows. And
the small lots, with few events of their own, get forecasts shaped by the big ones.

> **Ask:** "Is this question about one case or a population — and which reference class does this
> number actually describe?"

### <a id="u9"></a>DM1/U9. Whitespace is free on the inputs and spent on the outcome

*Reframes [SE1/U8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u8).*

SE1/U8 asks for time with no agenda, because an agenda finds only what it looks for, and K2 is what
nobody asked about. Data work needs that time more than software does: the sentence *that column
means something different before 2024* ([DM1/U1](#u1)) arrives only to someone looking at the
column with no question in hand. And it can afford it less, because here a look is not free: every
look at the outcome that changes what you try next spends evidence ([DM1/F4](03-feedback.md#f4),
[DM1/F5](03-feedback.md#f5)).

The two agree once you say what is being looked at. Whitespace spent on the inputs costs nothing:
the raw rows, the extremes, the datasheet, a column's distribution over time, the pipeline's
partitions, the world since the last session. That is where K2 lives, and a choice made from the
inputs alone, with the outcome out of view, does not select for a good score. Whitespace spent on
the outcome is a look like any other, a subgroup noticed, a residual plot pored over, and goes in
the ledger ([DM1/F6](03-feedback.md#f6)), because it is a search even when nobody meant it as one.
So leave the time, point it at the inputs, and write what it finds in FINDINGS or the datasheet,
not into a feature.

Wrong if an hour with the raw rows and no question in hand finds nothing a profiling script would
not.

> **Ask:** "When did someone last look at the raw rows with no question in hand, and did they look
> at the outcome while they were there?"

### <a id="u10"></a>DM1/U10. "None found" is a claim about the search

*Reframes [SE1/T3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/06-testing.md#t3).*

SE1/T3 warns of a tool that exits zero because it found nothing to check. In data work that tool is
any search for an absence. An absence is the cheapest K3 there is: it takes no evidence to report,
and it looks like diligence. A search that could not have found the thing returns the same answer as
a search that found nothing.

So every "none" carries how it was looked for, and what it would have looked like if it were there.
See the search find a planted case first ([DM1/T3](06-testing.md#t3)). Say what was not searched. A
"none found" in text means less on a page with no text layer, so report absences by how the page was
read ([DM1/T9](06-testing.md#t9)).

Before reporting that something is missing across many cases, open one by hand.

> **Ask:** "What would this search have returned if the thing were there, and have we seen it do
> that?"

### <a id="u11"></a>DM1/U11. Whether a result is worth acting on is a judgment: a metric taken for it is K3

*Reframes [SE1/U10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/02-unknowing.md#u10).*

SE1/U10 says some K1 no spike finishes: the answer is a judgment someone makes on meeting the thing.
Software meets this at "usable." Data work meets it often, because its work product
is usually a number, and a number reads like the answer. An AUC of 0.91 is K0. That this model is good enough to
change what the gate supervisor does on Saturday is not, and no further analysis settles it. Say
"the model passes" and the question stops being asked, with nobody dishonest and no search involved.
[DM1/F9](03-feedback.md#f9) is what the search does to a proxy; this is what the proxy does to you
before the search starts. An extracted claim has its own proxy: every quotation can check against its
page, and the brief can still fail the person deciding ([DM1/F10](03-feedback.md#f10)).

SE1/U10's rules hold: convert what converts, name the judge and the occasion, and let a proxy veto while
only the judge passes. In data work:

- **What converts is ordinary.** Calibration, latency, and the error rate on a named slice are requirements,
  and the bar is still specific enough to be rejected ([DM1/R3](04-requirements.md#r3)).
- **The judge reads the output in the decision's own terms** ([DM1/H6](09-people.md#h6)), usually the client
  at the read-back. In the question it is marked *(judged)*.
- **A bias measure is a proxy too.** Meeting `BIAS-1` says the chosen gap is within the chosen limit, and
  whether that is fair stays with people ([DM1/R10](04-requirements.md#r10)).
- **A verdict belongs to a version.** A new extract, a retrain, or a moved threshold makes it stale, unless the
  judge has approved a retrain policy in advance: which retrains, within what limits, and who is told. A retrain
  inside those limits keeps the verdict and still gets its checks ([DM1/O6](07-operating.md#o6)). One outside
  them makes it stale, and so does a moved threshold or a changed policy. An agent records a verdict and never
  gives one.

Drafted 2026-11-03 by Claude Opus 5.5 from SE1/U10, itself the author's decision without a trial of
its own, and untried here. The retrain policy is Warren MacEvoy's decision, 2026-11-03. Wrong if, across repositories and clients, no result that cleared its bar with room to
spare is ever turned down by the person who has to act on it.

> **Ask:** "When this score is perfect, what could still be wrong, and who would tell us?"

### <a id="u12"></a>DM1/U12. Trust data about observations over data about reflection: asking why is a K3 factory

*New.*

Data about what someone did and data about their reflection on it, why they did it, what they would prefer, what they
will do, are different instruments. Trust the first. Asking what you would prefer is far less useful than observing what
you choose: a survey of intentions is not a record of behavior, and a model trained on what people say learns what they
say.

The reason is structural. A set cannot contain itself, so no mind, a person's or an agent's, can hold a complete account of
itself, and being completely self-aware is not possible. An account of why arrives fluent and feels like knowledge, with
nothing in it to say what it left out. So asking why is a K3 factory for both parties: the one answering believes the
account, the one asking takes it as evidence, and neither has a way to hear that it is wrong.

The same holds for work. An analyst's "I only looked at the holdout once" and an agent's "the holdout was untouched" are
reflections, made in good faith and from inside. What was done is in the commands, the ledger, and the output, and it can be
checked by someone who was not there. Work that is good is good whether or not its author can cite the reason, and work
that cites every reason can still be wrong.

- **Prefer the record of a choice to a stated preference.** When reflection is all the data there is, a survey, an
  interview, a self-report, say the claim is about what people say, and name the observation that would check it.
- **Check an account against the record**, your own included: the count of looks in the ledger, not in the
  summary ([DM1/F6](03-feedback.md#f6)), the transcript behind the report.
- **To learn what changes behavior, change something and watch**: a trial on the same task, not a question about what
  influenced the work.
- **Asking why still gives a hypothesis.** It tells you where to look, and looking settles it.

In a [trial on 2026-11-05](https://github.com/wmacevoy/data-mining-self-improvement/blob/5032133b6e/trials/2026-11-05-quickref/RESULTS.md), three of nine agents' reports made a claim about their own work that their transcripts contradicted, such
as "touched once" after more than one evaluation of the holdout. The owner's word, 2026-11-05, with the reframe toward observation
over reflection the same day. Wrong if accounts of why turn out to predict what someone does next better than a record of what they did.

> **Ask:** "Is this a record of what was done, or an account of it, and what would check the account?"

---

Next: [3. Feedback](03-feedback.md) — what evidence costs, and why searching for it changes it.
