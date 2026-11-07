# 7. Operating

What happens to a pipeline and a model after they leave your machine: environments, CI, monitoring
when the true answers arrive late, shift, fallbacks, shadow deploys, dataset versions, security,
scale, and the feedback loop no rollback undoes. Read with
[SE1 chapter 7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md),
which covers the service around the model.

---

### <a id="o1"></a>DM1/O1. Record the environment and the hardware with every result; numbers depend on both

*Reframes [SE1/O1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o1) and [SE1/O2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o2).*

SE1/O1 and SE1/O2 make a build repeatable: describe the environment as constraints, record the exact
resolution, install with the command that refuses drift. Numerical work adds that the *result*, not
only the build, depends on the environment: a different BLAS, a GPU instead of a CPU, a new minor
version of a library with a changed default, and a metric moves in the third digit — or the second.

So every trained model and every reported number carries, beside it, the environment it was produced
in: the resolved package list, the platform, and the accelerator. Saving the output of the package
manager's list command into the run directory costs nothing and answers the question "what changed?"
months later, whether or not the lock file is committed.

> **Ask:** "Which library versions and which hardware produced this number — and where is that
> written?"

### <a id="o2"></a>DM1/O2. CI runs the pipeline from raw on the fixture, every change

*Reframes [SE1/O3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o3).*

SE1/O3: CI runs the command you run. For a data repository that command goes from raw to report: the
committed fixture through every stage, the assertions ([DM1/T2](06-testing.md#t2)), the property tests
([DM1/T8](06-testing.md#t8)), and the golden-output diff ([DM1/T7](06-testing.md#t7)). Minutes, not
hours, because the fixture is small.

What CI should not do is touch the holdout or the full data. It tests that the method runs and is
correct on known cases; whether the method is *good* is a decision made deliberately, and spent
([DM1/F5](03-feedback.md#f5)).

A repository that may not have a remote, because its data may not leave, can still have one script
that every change passes through. Call it what it is: a gate you can skip, with no machine but yours
to say it ran.

> **Ask:** "Does CI run every stage, from raw to the final number, on every change?"

### <a id="o3"></a>DM1/O3. A model in production needs signals that do not wait for labels

*Reframes [SE1/O4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o4).*

SE1/O4: every failure needs a signal a person will see and believe. A model's characteristic
failure is to keep answering, confidently and wrongly, and the signal you want — accuracy — needs the
true outcome, which arrives late or never. Whether the lot actually filled is known tonight; whether
a loan defaulted is known in years; whether a driver who was turned away would have found a space is
never known.

So monitor what you can see now:

- **Inputs:** the distribution of each feature against training, the rate of missing and
  out-of-envelope values ([DM1/U6](02-unknowing.md#u6)).
- **Outputs:** the distribution of predictions and the abstention rate. A model that suddenly calls
  everything full has changed, whatever the cause.
- **Upstream health:** the freshness of each source, the lag of each upload.
- **Then, as labels arrive:** realized error and calibration, by slice ([DM1/T9](06-testing.md#t9)).

> **Ask:** "If this model went wrong tonight, what would tell us before the labels arrive?"

### <a id="o4"></a>DM1/O4. Name the shift: inputs, base rates, or the relationship itself

*New.*

"Drift" is three different failures with three different detectors:

| Shift            | What changed                                   | In the running example                          | Detectable without labels? |
| ---------------- | ---------------------------------------------- | ----------------------------------------------- | -------------------------- |
| **Covariate**    | the inputs' distribution                        | more weekday events, a new ticketing system      | yes: compare input distributions |
| **Prior (label)** | how common each outcome is                     | a winning season; more sell-outs                 | partly: the prediction rate moves |
| **Concept**      | the relationship between inputs and outcome     | a new transit line; same tickets, fewer cars     | no: only realized error shows it |

The third is the dangerous one: inputs look normal, predictions look normal, and the model is wrong.
Only labeled outcomes catch it, which is why their latency belongs in the requirements
([DM1/R8](04-requirements.md#r8)).

> **Ask:** "Which kind of shift would hurt this model most — and which signal would show it?"

### <a id="o5"></a>DM1/O5. Every model has a fallback that is not a model

*Reframes [SE1/O5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o5).*

SE1/O5: handle failure on purpose, with a stated fallback. When the model's input is missing, out of
envelope, or late; when the model service is down; when monitoring says it has drifted — something
must still answer. That something should be simple enough to be trusted without monitoring: the
baseline ([DM1/T1](06-testing.md#t1)), a rule the operators already use, or "no forecast" shown
honestly.

Decide the fallback with the client, write it in the requirements, and test the switch to it. A model
with no fallback turns every upstream outage into a confident wrong answer.

> **Ask:** "When this model cannot answer well, what answers instead — and has that switch been
> tested?"

### <a id="o6"></a>DM1/O6. Run a new model in shadow before it decides anything; a retrain is a deploy

*Reframes [SE1/O6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o6).*

SE1/O6: a deploy is a test whose failure is an outage. For models there is a cheaper test first:
**shadow mode** — the new model receives live inputs and logs its predictions, and the old one still
decides. Compare them on live data, including on the slices, before switching. Then switch in a way
that can be undone, and keep the old model loadable.

A scheduled retrain is a deploy too, even though no code changed: new data, new weights, new
behavior. It gets the same checks as a code change — the fixture, the properties, the comparison with
the current model — before it serves anything. A retrain inside a policy the judge approved keeps the
judge's verdict; it does not skip these checks ([DM1/U11](02-unknowing.md#u11)).

> **Ask:** "Has this model's prediction been compared with the current one's on live data — and can
> we go back in a minute?"

### <a id="o7"></a>DM1/O7. Version datasets, and change a feature in steps that can each be undone

*Reframes [SE1/O7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o7).*

SE1/O7: change data and interfaces in steps that can each be undone. A feature table and a training
set are interfaces to every model trained on them. Give each a version — a content hash, a dated
snapshot, a tag — and record it with every model ([DM1/D6](05-design.md#d6)).

Changing a feature's definition is a migration: add the new column beside the old, backfill it,
retrain and compare, switch readers, and only then remove the old one. Overwriting a feature in place
silently changes the meaning of every model that reads it — including the one in production.

> **Ask:** "Can the model currently serving still be rebuilt, and still be fed, after this change?"

### <a id="o8"></a>DM1/O8. Secure the data, the copies, and the model

*Reframes [SE1/O8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o8).*

Everything in SE1/O8 applies, and a data system has more copies to secure than its owners usually
count: the raw pull, every cache, every intermediate Parquet file, every extract on a laptop, every
notebook output cell, and every prompt that pasted rows into a hosted model ([DM1/A8](10-agents.md#a8)).
Each is the data, with weaker protection. Two kinds get forgotten: an agent's transcripts and tool
output ([DM1/A10](10-agents.md#a10)), and the old versions inside a store, where a deleted row stays
until they are purged.

Identifiers can arrive where no column names them, printed on a scanned form and carried into a text
cache and a search index. A scrub lasts only if it sits where the data is derived, or a rebuild brings
it back ([DM1/D4](05-design.md#d4)). And run a check that shows it searched for something
([DM1/U10](02-unknowing.md#u10)).

Two threats are specific to models: a model can disclose its training data (membership inference,
memorized records in a generative model), and anyone who can write to the training inputs can steer
the model (poisoning). Treat training data as an input from outside the trust boundary, and treat a
model trained on personal data as personal data.

> **Ask:** "How many copies of this data exist right now, and who can read each one?"

### <a id="o9"></a>DM1/O9. Unbiased is not a default: audit for bias like an attacker, before and after deploy

*Reframes [SE1/O8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o8).*

SE1/O8 says secure by default, and asks what an attacker can reach and whether anyone has tried.
Bias needs the same posture, because no step of a pipeline is unbiased by default. Every source in
[DM1/R10](04-requirements.md#r10) is present until it is checked, and a model that passed an audit at
training time can fail it later, as the population it is used on moves away from the one it learned
from.

- **Before deploy:** compute the per-group measures the requirements chose, on the holdout, with
  intervals and *n* ([DM1/T9](06-testing.md#t9)), including calibration per group. Then try to make
  it look worse: search for the group where it does worst. That search is a search
  ([DM1/F4](03-feedback.md#f4)), so the worst of many slices is partly noise; confirm what it finds
  on fresh data.
- **After deploy:** monitor the same per-group gaps as labels arrive ([DM1/O3](#o3)), and compare who
  the model is being used on with who it was trained on.
- **When you find a gap:** it is a finding, recorded, and the options go to the client: better data
  for the group, features that distinguish it, a separate model, a threshold chosen per decision, or
  a narrower use ([DM1/R11](04-requirements.md#r11)). Removing the sensitive column is rarely a fix,
  because other columns stand in for it.

> **Ask:** "Who is this model worst for — have we tried to find out — and are we still checking?"

### <a id="o10"></a>DM1/O10. Test at the size you will have: a sample hides what the full table does

*Reframes [SE1/O10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o10).*

A pipeline developed on a sample meets three problems only at full size: memory (a frame that fit
becomes one that doesn't), joins (a key that was unique in the sample isn't across all seasons, and a
join multiplies), and time (a stage that took seconds takes hours, so nobody re-runs it). Run the full
pipeline at the real size early — once, timed — and keep the time in the repository guide. It is also the cost of
running it again, which decides how much of each run to keep ([DM1/T13](06-testing.md#t13)).

Measure before optimizing (SE1/O10). The slow stage is rarely the model.

> **Ask:** "Has this run end to end on all the data we have, and how long did it take?"

### <a id="o11"></a>DM1/O11. A model in production changes the data it will be trained on next

*Reframes [SE1/O11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/07-operating.md#o11).*

You can roll back a model. You cannot roll back the data it generated or the behavior it taught
people. In the running example, the app shows drivers which lots are forecast to fill, so drivers go
elsewhere, so those lots fill later — and next season's training data says the model was wrong to
predict they would fill. Recommenders train on clicks they caused; risk scores produce the outcomes
they predict by changing who gets the loan, the interview, or the patrol car.

That is a one-way door that does not look like one at deploy time (SE1/O11). Ship it like one:
record what the model decided alongside what happened; keep a small random slice that does not
follow the model, so there is still data about the world without it; and say in the model card what
the model changes.

> **Ask:** "What will this model change about the data we train the next one on?"

---

Next: [8. Work](08-work.md) — the shape of the work, and the deadline pressure to find something.
