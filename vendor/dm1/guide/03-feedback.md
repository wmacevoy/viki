# 3. Feedback

How you find out you are wrong when the thing being tested is a claim: what evidence costs, which
loop catches which error, and the fact with no counterpart in software — **searching for a result
changes it.** Read with [SE1 chapter 3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/03-feedback.md).

---

### <a id="f1"></a>DM1/F1. Tests of code are free; tests of a claim are not

*Reframes [SE1/F1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/03-feedback.md#f1).*

Data work inherits software's free tests for its *code* and emphatically does not inherit them for
its *evidence*. Both regimes live in the same afternoon:

| Practically free                    | Expensive                          | One-way                                       |
| ----------------------------------- | ---------------------------------- | --------------------------------------------- |
| Re-run a transform on 1,000 rows     | A full training run                 | Looking at the holdout ([DM1/F5](#f5))         |
| Plot a distribution                  | A labeling campaign                 | Publishing a number someone acts on            |
| Swap a model class                   | A season of live traffic            | Deploying a model that changes its own inputs ([DM1/O11](07-operating.md#o11)) |
| Try another feature                  | An hour of a domain expert's time   | A survey you can field once                    |

So the method is chosen per decision, as SE1/F1 says — and the expensive, one-way decisions in data
work are mostly made in the first week, while they still feel cheap: the question, the unit of
analysis, the outcome definition, the split, and the labeling scheme ([DM1/W2](08-work.md#w2)).

> **Ask:** "Is this a test of the code, or of the claim — and which column of the table is it in?"

### <a id="f2"></a>DM1/F2. The cost of checking a result is designed, not given

*Reframes [SE1/F2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/03-feedback.md#f2).*

The toolkit, in data:

- pure functions over frames instead of one long stateful notebook ([DM1/D7](05-design.md#d7));
- a tiny fixture dataset committed with answers worked out by hand ([DM1/T6](06-testing.md#t6));
- seeds that are fixed *and recorded* with the result;
- one command from raw data to every reported number ([DM1/D6](05-design.md#d6));
- assertions on what joins and filters break: row counts, key uniqueness, monotone time index,
  units, plausible ranges ([DM1/T2](06-testing.md#t2));
- caches that record *when* and *from where*, so they are a speedup rather than a time machine
  ([DM1/D2](05-design.md#d2)).

**Lowering the cost of checking a result multiplies against every future question.** And the
inverse is diagnostic: *if a result is hard to check, that is information about the analysis, not
about checking.*

> **Ask:** "How long does it take to regenerate this number from raw — and what would make it
> minutes?"

### <a id="f3"></a>DM1/F3. Push each class of error down to the cheapest loop that can catch it

*Reframes [SE1/F3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/03-feedback.md#f3).*

| Loop                                  | Latency   | Catches                                                   |
| ------------------------------------- | --------- | --------------------------------------------------------- |
| Look at the data: head, describe, plot | seconds   | wrong units, impossible values, the file didn't parse      |
| Assertions in the pipeline            | seconds   | a join that multiplied, a key that stopped being unique    |
| Unit test on a transform              | seconds   | broken logic, an off-by-one in a window                    |
| Fixture pipeline, end to end          | minutes   | wrong assumptions between stages                           |
| Baseline comparison                   | minutes   | "the model is worse than last week's average"              |
| Holdout evaluation                    | spendable | overfitting to the validation set                          |
| Human review of the *claim*           | hours     | the metric does not measure the decision                   |
| A domain expert looking at output     | days      | "that lot was closed for paving in August"                 |
| Prospective use                       | live      | drift, feedback loops, and everything you didn't imagine   |

Some errors are catchable only by an expensive loop. No test suite detects "we measured the wrong
thing," and no metric detects "this dataset does not contain the people we care about." Only a person
who knows the domain does ([DM1/H1](09-people.md#h1)).

> **Ask:** "What is the cheapest loop that could have caught this?"

### <a id="f4"></a>DM1/F4. The search is part of the result

*New.*

In software, searching is free. Try forty implementations, keep the one that passes the tests, and
you have one correct program; the search left no residue.

In data, searching contaminates. Try forty features, forty model classes, forty subgroups, or forty
outlier rules, keep the one that looked best, and *what you kept was selected for looking good* —
some of which was noise. You have not found an effect; you have run a tournament and crowned the
luckiest contestant. **A result is real to the degree it would have survived being the only thing
you tried.**

This is data mining's central hazard, because in data mining the search is the method. Association
rules, subgroup discovery, clustering with a chosen *k*, and anomaly thresholds are all searches over
thousands of candidates. A rule that is significant at 5% among two thousand candidates is expected,
not discovered. Correct for the number of candidates (a permutation test, a false-discovery-rate
procedure), and confirm what survives on data the search never saw.

Your hyperparameter search is an adversary you hired. It is tireless, has no stake in the truth, and
optimizes against exactly the measurement you intend to trust. Given enough freedom, it will find
your leakage and hand it back as a win.

> **Ask:** "Would this result have survived if it were the only thing we tried?"

### <a id="f5"></a>DM1/F5. The holdout is a consumable

*New.*

You cannot un-see the test set. Each time you evaluate on it and then change something in response,
a little of its information moves into the model by way of your judgment. Do that twenty times and
you have a validation set wearing a test set's name. The set is not used up by being touched — it is
used up by being touched *and reacted to*.

This is data mining's mask set and wind tunnel (SE1/F1): a test with a real cost in a field where
nearly every other test is free, spent by a mechanism — your own updated beliefs — that leaves no
trace in git. So:

- Split it off first, before exploring, and keep it where casual code cannot reach it — a separate
  file, or a separate person, on paper or elsewhere ([DM1/A4](10-agents.md#a4)).
- Develop on a validation split or cross-validation; spend the holdout at a decision.
- When it is spent, say so, and get a fresh one: a later period, a new season, a new site.

> **Ask:** "How many times has this holdout been looked at — and did we change anything after?"

### <a id="f6"></a>DM1/F6. Count your looks, and say the number

*New.*

"We tried one model" and "we tried sixty" are different claims about the same score, and the second
is almost never reported. The number of things tried is part of the result ([DM1/F4](#f4)), so
record it mechanically, not from memory:

- **A ledger**: one line appended by the code each time anything is scored — what was tried, on
  which split, with which seed, and the result ([LEDGER template](../templates/LEDGER.md)). In
  retrieval and extraction, the looks are the searches run and the pages read: log those.
- **An analysis plan written before the outcome is examined**: not as ceremony, as a record of how
  many degrees of freedom you had ([PLAN template](../templates/PLAN_v1.md)). Departing from it is
  normal. Departing from it silently is the problem.

Report the count with the result: *"best of 23 configurations on validation; one look at the
holdout."*

> **Ask:** "How many things did we try before this one — and where is that number written?"

### <a id="f7"></a>DM1/F7. A point estimate is half a result

*New.*

A number without an interval is not more certain than one with an interval; it is equally uncertain
and less honest. A model that scores 0.84 against one that scores 0.82 is not better if the
difference is inside the noise of a 1,200-row test set. Reporting two digits without the spread is
how a coin flip becomes a roadmap.

Three sources of spread, all routinely ignored:

- **The sample** — a bootstrap or an analytic interval on the metric.
- **The seed** — train five times with five seeds; a gain smaller than the seed-to-seed spread is
  not a gain.
- **The split** — a different fold, or a different season, is a different answer. Time-ordered data
  gets time-ordered splits, and rows that share a lot, a night, or an event stay on one side.

> **Ask:** "What is the interval — and does the difference survive a different seed?"

### <a id="f8"></a>DM1/F8. A large sample does not rescue a biased one; it disguises it

*New.*

As a sample grows, variance shrinks and bias does not. A large biased sample converges confidently
on the wrong number: the error bar tightens around a value that was never right. This is the most
counterintuitive fact in applied statistics, and the reason "we have millions of rows" is not an
answer to "who is missing from the rows?"

In the running example, readings exist only while a sensor is reachable, and sensors drop off the
network most when a lot is busiest. Five years of readings describe the quiet hours very precisely.

> **Ask:** "Who or what is missing from these rows — and does more of them fix that?"

### <a id="f9"></a>DM1/F9. When a metric becomes a target, it stops measuring — and the search is the hardest optimizer you will meet

*Reframes [SE1/F5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/03-feedback.md#f5).*

SE1/F5 is about people gaming a measure. In data mining the optimizer is usually not a person. It is
the training loop, the hyperparameter search, the feature selection, the agent re-running until the
score goes up — each pointed at the metric with no idea what the metric was a proxy for.

- Aim at accuracy on an imbalanced class, and you get a model that predicts the majority.
- Aim at a leaderboard, and you get a model of the leaderboard.
- Aim at "lots predicted correctly as full," and you get a model that calls everything full.
- Aim at a grading rubric, and you get the rubric.

The defense is the one SE1/F5 gives — keep the reason attached — plus one more: **report more than
one metric**, including the one the search was not told about, and look at a sample of individual
predictions.

A limit is a target too. Cap a summary's length and the summaries crowd just under the cap, whatever
the evidence needs; move the cap and they move with it. Telling the writer that the cap gives way to
evidence is a rule read and not followed ([DM1/A2](10-agents.md#a2)). Comparing the drafts by script
is what changes the work ([DM1/F10](#f10)).

> **Ask:** "If the search could cheat on this metric, how would it — and what second number would
> catch it?"

### <a id="f10"></a>DM1/F10. A faithful summary can still lose the finding: check what left, not only what stayed

*New.*

A citation check holds what a summary says to its source. It cannot see what the summary stopped
saying. Every quotation can be exact, every page right, and the most important sentence in the
source gone. Fidelity is precision. Nothing about it measures recall.

Any limit on length is a filter on the data, chosen by nobody and applied differently per case
([DM1/F9](#f9)). And a summary's author is a poor witness to its own cuts: ask, and you get what it
remembers.

So keep the drafts, and compare a draft with its final by script. Ask a second reader, with no sight
of the first, to read a sample of cases whole, and count what each found that the other missed.
Include a random sample, and expect misses in both directions. Name what may never be cut for
length, in advance. When a limit and the evidence conflict, the limit gives way.

> **Ask:** "What did this summary drop, how do we know, and who decided?"

---

Next: [4. Requirements](04-requirements.md) — the question, and what it costs to be wrong.
