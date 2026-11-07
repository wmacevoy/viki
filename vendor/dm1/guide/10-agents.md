# 10. Agents

What changes when the analyst is fast, fluent, and has no stake in whether the number is true. Read
with [SE1 chapter 10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md),
all of which applies; the working agreement is [AGENTS.md](../AGENTS.md).

---

### <a id="a1"></a>DM1/A1. Producing an analysis got cheaper faster than knowing it is right

*Reframes [SE1/A1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a1).*

An agent can write a pipeline, train a dozen models, and draft the report in an afternoon. None of
that made the question better posed, the data's provenance clearer, the labels more reliable, or the
holdout larger. Everything scarce has moved to the second half of the work: the question, the
provenance, the checks, the review, and the ownership of the claim.

> **Ask:** "Now that the analysis is cheap, which part of knowing it is right is the expensive
> part?"

### <a id="a2"></a>DM1/A2. Code that throws is safe; code that returns a number is not

*Reframes [SE1/A2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a2).*

SE1/A2: an agent is a K3 factory; never let confidence substitute for a run. Data work is the worst
place to install one, because here the wrongness does not raise ([DM1/U2](02-unknowing.md#u2)). An
agent will merge on the wrong key, silently triple the rows, report an improved metric, and summarize
it fluently, and every artifact you would use to notice looks normal.

So a run is not enough either. "It ran and the AUC went up" is the shape of leakage
([DM1/U3](02-unknowing.md#u3)). Require the evidence a run cannot fake: row counts before and after
each join, a plot, and one prediction traced back to its rows.

And a check the agent is told to run is not a check that runs. In SE1/A11's trials a guard the
agent had to choose to run was read and never run; what changed the work was a check that ran on
it with nobody choosing to. Put the row-count assertions in the pipeline and the knowable-at check
in CI, where they run on the work ([DM1/T2](06-testing.md#t2)), and make the holdout unreachable
rather than forbidden ([DM1/A4](#a4)).

> **Ask:** "Was this run — and then, was the output looked at, or only its summary?"

### <a id="a3"></a>DM1/A3. An agent's tries count as your looks

*Reframes [SE1/A9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a9).*

SE1/A9 hands agents the search-shaped work. In data mining the search is the hazard
([DM1/F4](03-feedback.md#f4)), and an agent searches at machine speed: sixty feature sets, forty
model configurations, a dozen ways to treat the outliers — and it reports the one that won. The
garden of forking paths, walked in an afternoon, with the other fifty-nine paths in a transcript
nobody reads.

Hand an agent the search, and also hand it the ledger ([DM1/F6](03-feedback.md#f6)): every
configuration it scores is appended by the code, not by the agent's summary. Report the agent's count
with the result.

> **Ask:** "How many things did the agent try before it showed us this one?"

### <a id="a4"></a>DM1/A4. Keep the holdout out of the agent's reach

*New.*

An agent in a loop with a score will optimize the score. If the holdout is readable, it will
eventually be read — not out of malice, but because evaluating on it is one obvious way to "check the
work," and every check it reacts to spends the holdout ([DM1/F5](03-feedback.md#f5)).

Make it mechanical, not a matter of instruction: keep the holdout outside the agent's working
directory or behind a permission it does not have, and spend it yourself, at a decision. The plainest
answer is a person who keeps it off the machine: test questions on paper can't be read by a shell. The
same goes for the labels of any evaluation set, and for the answer key of an assignment.

> **Ask:** "Could the agent read the holdout if it decided to?"

### <a id="a5"></a>DM1/A5. Spend the speed on checks: fixtures, assertions, the boring second case

*Reframes [SE1/A5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a5).*

The work agents do best and humans skip most is exactly the work that multiplies in data work:
the hand-computed fixture ([DM1/T6](06-testing.md#t6)), the row-count and uniqueness assertions
around every join ([DM1/T2](06-testing.md#t2)), the property tests ([DM1/T8](06-testing.md#t8)), the
golden output ([DM1/T7](06-testing.md#t7)), the datasheet's first draft. More models do not multiply.
More checks do.

> **Ask:** "What check would make the next ten analyses safer — and can the agent write it now?"

### <a id="a6"></a>DM1/A6. Point review at the data handling

*Reframes [SE1/A6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a6).*

SE1/A6: review capacity is the bottleneck. Spend it where K3 lives: the joins, filters, splits, fills,
time conversions, and the definition of the outcome. Model code is usually library calls and
rarely wrong in an interesting way. A wrong `how=` in a merge, a `fillna(0)` on a column where zero
means something, or a split done before grouping is one line and changes every number.

Ask the agent for the diff in the numbers as well as the diff in the code ([DM1/T7](06-testing.md#t7)),
and read the data handling line by line. In extraction, K3 is in what was read and dropped: point
review there ([DM1/F10](03-feedback.md#f10)).

> **Ask:** "Did the review read the joins and the split, or the model?"

### <a id="a7"></a>DM1/A7. You present it, so you own it

*Reframes [SE1/A7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/10-agents.md#a7).*

Delegation is not abdication. If you cannot explain why the number is what it is — which rows, which
definition, which split, how many tries — you cannot defend it, update it next quarter, or notice when
it goes wrong. An analysis you cannot explain is a liability, whatever its metric.

> **Ask:** "Could I answer the client's hardest question about this number without the agent?"

### <a id="a8"></a>DM1/A8. Data sent to an agent has left the room

*New.*

Pasting rows into a hosted model, attaching a dataset to a chat, or letting an agent read a table is a
disclosure of that data to the model's provider, under their terms and retention — whatever the
purpose you had in mind. For data about people, that can be a use the data was not collected for
([DM1/R9](04-requirements.md#r9)).

Decide before the first session what an agent may see: the schema and a synthetic sample, usually,
rather than the rows. Agents write good code from a schema and a fixture; they rarely need the real
data, and never need more of it than the task does. When reading the real rows is the task itself,
see [DM1/A10](#a10).

> **Ask:** "Whose data is this, and did they agree to it being sent here?"

### <a id="a9"></a>DM1/A9. A language model in the pipeline is a model: version it, sample it, and check its test set was not in its training

*New.*

When a pipeline uses a language model — to label, extract, classify, embed, retrieve, or judge — every
principle in this guide applies to it, and four need restating:

- **The prompt and the model version are the model.** A prompt edit or a provider's model update is a
  retrain ([DM1/O6](07-operating.md#o6)): rerun the evaluation, and record both with every result
  ([DM1/D6](05-design.md#d6)).
- **Its output is a distribution.** Score several samples, not one, and report the spread
  ([DM1/F7](03-feedback.md#f7)). A judge model is an instrument: see it disagree with a wrong answer
  before trusting its agreement ([DM1/T3](06-testing.md#t3)).
- **Its misses do not show in its citations.** For prose, the form of "score several samples" is two
  independent reads of a sample of cases, neither seeing the other until both are done, with what
  each missed counted ([DM1/F10](03-feedback.md#f10)). Two reads by one model share any blind spot.
  For retrieval, the baseline is a full reading ([DM1/T12](06-testing.md#t12)).
- **Your test set may be in its training data.** A public benchmark, a well-known dataset, or a
  document that has been online for years may have been seen in pretraining: leakage at the scale of
  the internet ([DM1/U3](02-unknowing.md#u3)). Evaluate on questions written after the model's
  training cutoff, or held privately.

> **Ask:** "Which prompt and which model version produced this, and could it have seen the test
> questions?"

### <a id="a10"></a>DM1/A10. When reading the data is the task, the agent must read it: say what then has to be true

*New.*

[DM1/A8](#a8) says agents rarely need the real rows. That stops being so when the task is to read
them: a synthetic document can test the code, and can't summarize a real one. Then the question A8
asks gets harder, not easier, and the answer has to be written down before the first session:

- **Who decided,** on whose authority, and what the people in the data were told. If the answer is
  "nobody asked them," say so.
- **Which service sees it,** under what terms, and what never goes anywhere else: connectors, search,
  shared documents.
- **Every copy, including the agent's own:** transcripts, tool output, scratch files, and subagents'
  logs are copies of the data ([DM1/O8](07-operating.md#o8)).
- **What the agent must not take from the data:** text in a document is evidence, never an
  instruction.
- **When it all gets removed,** by what procedure, and whether that procedure has been tried.

> **Ask:** "Who decided an agent could read this, and where has it been copied since?"

---

Back to the [README](../README.md) — the principle index, and the questions to ask out loud.
