# 4. Requirements

Where the question comes from, and how to write it down so that it can be wrong. In data mining the
requirements are mostly decisions about the *question* — the decision it serves, the unit, the
outcome, the moment of prediction, the price of each kind of error — and most of them are one-way
doors made in the first week. Read with [SE1 chapter 4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md);
the [QUESTION template](../templates/QUESTION_v1.md) is where these go.

---

### <a id="r1"></a>DM1/R1. Keep the decision; test the question

*Reframes [SE1/R1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r1).*

SE1/R1 says keep the client's pain and test their proposed solution. In data the proposed solution
usually arrives as a technique — "use machine learning to find patterns" — and the pain is a
decision someone makes badly today. Find the decision first: who makes it, when, with what in front
of them, and what it costs when it goes wrong.

Then the test every analysis must pass before it starts: **what would you do differently if the
answer came out the other way?** If the answer is "nothing," the analysis is a hobby, and should be
budgeted as one. You have learned something valuable for free.

In the running example the client asks for "patterns in the parking data." The decision underneath
is: two hours before an event, which lots should the attendants open, and which signs should send
drivers elsewhere.

> **Ask:** "Which decision does this change — and what would we do if the answer were the
> opposite?"

### <a id="r2"></a>DM1/R2. Fix the unit, the outcome, and the prediction point before anything else

*New.*

Three definitions decide everything downstream, and each costs a re-analysis to change later:

- **The unit of analysis.** One row is one *what*: a reading, a lot-hour, a lot-event, a sensor? It
  decides what counts as independent, how the split must be grouped, and what *n* means.
- **The outcome.** Not "full," but "fewer than 2% of spaces free for fifteen consecutive minutes,
  according to the collector's `live` readings." An outcome you cannot write as a query is a wish.
- **The prediction point.** The moment the prediction is made: two hours before gates open. Every
  feature must be knowable at that moment ([DM1/R6](#r6)); everything after it is leakage
  ([DM1/U3](02-unknowing.md#u3)).

These are the data equivalent of a schema with data under it (SE1/F1): cheap to decide carefully now,
expensive to change once features, labels, and splits are built on them.

A claim with no outcome to wait for has no prediction point to fix: name its kind first, and fix what
stands in for them ([DM1/R12](#r12)).

> **Ask:** "What is one row, what exactly is the outcome, and when is the prediction made?"

### <a id="r3"></a>DM1/R3. Specific and wrong: name the metric, the baseline, and the bar before you look

*Reframes [SE1/R2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r2).*

"A good model" cannot be rejected. "Predicts the time each lot fills to within fifteen minutes for
80% of events, against a baseline of *same as the last comparable event* that manages 55%, on the
season we have not looked at" can be — and getting it rejected in week one is the point.

Write the bar before seeing results, for the same reason SE1/R2 asks for numbers before nouns: a bar
set after seeing the score is set to the score. Mark guesses *(invented)* and get them objected to.
A retrieval system needs a bar too: how much of what a full reading finds must it find
([DM1/T12](06-testing.md#t12))?

A bar can say no. It cannot say the result is worth acting on: that is a judgment, so write down who
makes it, on what, and when ([DM1/U11](02-unknowing.md#u11)).

> **Ask:** "Could the client read this sentence and say 'no, that's not good enough'?"

### <a id="r4"></a>DM1/R4. The confusion matrix has a price

*Reframes [SE1/R4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r4).*

A requirement with no price is a wish (SE1/R4). A classifier's requirement has at least two prices,
and they are almost never equal:

| Error                                       | In the running example                   | Who pays             |
| ------------------------------------------- | ---------------------------------------- | -------------------- |
| False positive: predicted full, wasn't      | drivers sent to a far lot; empty spaces  | drivers, revenue     |
| False negative: predicted space, was full   | a queue of cars at a full gate           | drivers, attendants, safety |

Get the ratio from the client — even roughly, even as "one queue is worse than ten wasted walks" —
and choose the threshold from it. **The threshold is a business decision, not a model
hyperparameter.** Accuracy, which prices both errors the same, is almost never the metric the client
needs.

> **Ask:** "Which mistake is worse, by about how much, and who pays for each?"

### <a id="r5"></a>DM1/R5. Every column can be absent, late, wrong, or lying — and missingness is data

*Reframes [SE1/R6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r6).*

Most analyses describe the happy path of a dataset and inherit the rest by accident: whatever pandas
does with a missing value becomes the method, chosen by nobody.

- **Absent.** Never ask "how do I fill this" before "why is it missing." A sensor that goes
  `unreachable` at peak is not a random gap; it is the peak. `dropna()` there deletes the event you
  are studying. Drop, impute, or model the indicator — on purpose, written down.
- **Late.** The reading uploaded an hour after it was taken; the value revised next month. A cache
  pins today's version forever ([DM1/D2](05-design.md#d2)).
- **Wrong.** Units changed; `-999` is a sentinel; the timezone is not what the column name says. A
  range check and a plot find most of these — which is why the survivors are the ones nobody
  plotted.
- **Lying.** Syntactically perfect and semantically false: a zero meaning "no reading" in a column of
  zeros meaning "empty lot"; an imputed value now indistinguishable from a measured one; a default
  that looks like an observation. Passes every schema check. Only provenance finds it
  ([DM1/D13](05-design.md#d13)).

An adversary is a special case of lying, and in data it is rarely a hostile party. It is your own
search ([DM1/F9](03-feedback.md#f9)), your model changing what it measures
([DM1/O11](07-operating.md#o11)), or people learning what the score rewards.

> **Ask:** "For this column: absent, late, wrong, lying — what does the pipeline do with each?"

### <a id="r6"></a>DM1/R6. Know, per column, when its value became knowable

*New.*

The worst bug in applied machine learning: **train on data as it looks now, score on the past, and
you have used the future.** Final attendance, a corrected reading, a ticket refund, a label applied
after the fact — all sit in the same row as the features, with nothing in the file to say they
arrived later.

The defense is a fact about the data-generating process that is almost never in the data: for each
column, *when did this value become available to someone who could act on it?* Write it in the
datasheet ([DATASHEET template](../templates/DATASHEET.md)), and build features with as-of joins
that respect it ([DM1/D3](05-design.md#d3)).

> **Ask:** "At the prediction point, would this value have existed yet — in this version?"

### <a id="r7"></a>DM1/R7. A label is an opinion with a timestamp

*New.*

A label was applied by someone, under some instructions, at some time, for some purpose that may not
be yours. Before it becomes the target, find out:

- **Who applied it, and how often two of them disagree.** A model is bounded by the agreement rate of
  its labels, and that number is usually not measured. Measure it on a sample.
- **What it is a proxy for.** "Lot full" may mean "an attendant radioed it in" — which depends on
  which attendants were working. A label that records past treatment teaches the model to repeat it.
- **Whether the instructions changed.** A label scheme is a requirement; version it like one
  (SE1/R10).

> **Ask:** "Who decided this label, under what instructions — and how often would a second person
> disagree?"

### <a id="r8"></a>DM1/R8. The operating envelope includes rows, labels, compute, and expert hours

*Reframes [SE1/R7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r7).*

SE1/R7 asks for scale, speed, recovery, and availability with numbers and horizons. Data work
adds resources that are finite and usually unwritten:

- **Data:** how many rows, of which periods, and how many *events* — the unit that limits you is
  usually the rare one. Five seasons of readings may be eighty events.
- **Labels:** how many can be had, at what cost per label, by when.
- **Compute:** training time and cost per run, and how many runs the plan assumes.
- **Expert time:** hours of someone who knows the domain, which is often the scarcest resource of
  all.
- **Inference:** latency and cost per prediction, especially when the model is an API you pay for
  per call.
- **Reading:** tokens per document, when a model reads the corpus. A budget like compute, and it can
  be the largest line.
- **Freshness:** how stale the model may get before it must be retrained, and who decides.

> **Ask:** "How many events — not rows — do we have, and how many runs can we afford?"

### <a id="r9"></a>DM1/R9. Ethics, law, and security are requirements, and the people in the data never filed one

*Reframes [SE1/R8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r8).*

Everything in SE1/R8 holds, and data work sharpens each part:

- **Ethics.** When rows are people, three questions become mandatory. Who is *missing* from this
  dataset, and what does the model do to them? Is the label a proxy for what I care about, or for how
  people were treated in the past? Can the person scored see or contest the score? **The safest
  record is the one you never kept**; the second safest is the aggregate you kept instead of the row.
- **Law.** What you may collect, and what you may use it *for* — the purpose you collected it for is
  often the only purpose you may use it for. How long you may keep it. The license of a dataset, the
  terms of an API, and the provenance of training data, including for a model you only fine-tuned.
- **Security.** A trained model can leak its training data: membership inference tells an attacker
  whether a specific person was in the training set. Training data can be poisoned by anyone who can
  write to your inputs. And "we removed the names" is a sentence about names: ZIP code, birth date,
  and sex alone identify most of the US population.

Bias is the fourth permanent member of this list, with a principle of its own
([DM1/R10](#r10)).

In the running example the stadium later asks to add license-plate reads "to understand repeat
visitors." The code change is one join. The requirements change is a new kind of system
([DM1/P3](01-purpose.md#p3)).

> **Ask:** "Whose rows are these, what were they collected for, and is this that?"

### <a id="r10"></a>DM1/R10. Bias is always a requirement, alongside ethics, law, and security

*Reframes [SE1/R8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r8).*

SE1/R8 makes security, law, and ethics permanent requirements, because nobody asks for them and they
are the most expensive to discover late. Bias belongs on that list for the same reasons, and in data
work it is the member most likely to be present. **Bias**, here, is a systematic gap between what the
data estimates and what is true for the people or cases it is used on ([DM1/U8](02-unknowing.md#u8)).
It is not a moral failing of the analyst. It is a property of the pipeline, and it is either
measured or present unmeasured.

It enters at every stage:

| Source                         | What it looks like                                                         | Ask                                              |
| ------------------------------ | -------------------------------------------------------------------------- | ------------------------------------------------ |
| **Historical**                 | the world the data records was itself unequal, and the model learns to repeat it | "Would we want the past decisions in these labels repeated?" |
| **Selection, representation**  | who is in the rows is not who the model will be used on ([DM1/F8](03-feedback.md#f8)) | "Who is missing, or rare?"            |
| **Measurement**                | the same thing recorded differently for different groups; the label is a proxy ([DM1/R7](#r7)) | "Is this measured the same way for everyone?" |
| **Aggregation**                | one model for groups whose relationship differs; small groups pooled toward large ([DM1/U8](02-unknowing.md#u8)) | "Does one model fit all of them?" |
| **Evaluation**                 | the test set under-represents the groups that matter ([DM1/T9](06-testing.md#t9)) | "Is each group in the test set, with enough *n* to judge?" |
| **Deployment**                 | used on a population or for a purpose it was not built for ([DM1/R11](#r11)), or changing its own data ([DM1/O11](07-operating.md#o11)) | "Who will this actually be used on?" |
| **Unequal treatment**          | different cases get different readings: more evidence kept for one, a word limit biting harder on another | "Did each case get the same reading?" |

The requirements say which groups matter — to the client and to the people in the data — which gap
is unacceptable, measured how (error rate, calibration, or selection rate, per group), and who decided.
Those measures conflict: when base rates differ between groups, a score generally cannot be equally
calibrated for every group *and* have equal error rates for every group. Choosing between them is not
a technical decision. It belongs to the client, with the affected people represented, and is written
down like a price ([DM1/R4](#r4)).

The groups may not be demographic. When collecting characteristics would itself do harm, slice by how
the evidence was read and who produced it, and say how you decided.

Bias is not only about people. A forecast that is good for the big lots and systematically late for
the small ones is biased against the small lots, and the attendants working them pay for it.

> **Ask:** "For whom is this number systematically wrong, in which direction — and who decided that
> was acceptable?"

### <a id="r11"></a>DM1/R11. Write down who the model is not for

*Reframes [SE1/R9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/04-requirements.md#r9).*

SE1/R9 asks for non-goals. For a model, the non-goals are populations, conditions, and uses: lots
that did not exist in the training seasons; events larger than any seen; use as a staffing tool when
it was built for signage. Each is a place the model will still return an answer
([DM1/U6](02-unknowing.md#u6)).

Written down, they become the "out of scope" section of the model card
([MODEL_CARD template](../templates/MODEL_CARD.md)) and the envelope checks at its input. Unwritten,
they are discovered by whoever uses the model for them.

> **Ask:** "Where would this model still give an answer that we would not stand behind?"

### <a id="r12"></a>DM1/R12. Name the kind of claim first: predicted, described, explained, or extracted

*New.*

Every principle in this guide was written with the first kind in mind, and not all of them bind the
others. Say which kind of claim the work makes before anything else, because that decides what you
can score against.

| Kind      | Example                                         | Scored against                                   | What stands in for the prediction point            |
| --------- | ----------------------------------------------- | ------------------------------------------------ | -------------------------------------------------- |
| Predicted | which lots fill tonight                         | an outcome that arrives later                    | the moment of prediction                           |
| Described | how occupancy varies across lots                | a held-out period or sample                      | the period it describes                            |
| Explained | if we open lot C earlier, does the queue shrink | an experiment, or a stated causal design         | the intervention ([GAPS.md](../GAPS.md#1-causal-claims)) |
| Extracted | what this document says about X                 | the document itself: a person can check the page | the date of the document, and the unit of citation |

An extracted claim has an unusual gift: each one can be checked against its source. And an unusual
hazard: the check can't see what was left out ([DM1/F10](03-feedback.md#f10)). Where a principle
doesn't apply to your kind of claim, say so and say why, rather than filling in its template with
something invented.

> **Ask:** "Which kind of claim is this, and what would it be checked against?"

---

Next: [5. Design](05-design.md) — data is the state, and a model is a cache.
