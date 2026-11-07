# 5. Design

What a data system is made of. Every principle here reframes the one with the same number in
[SE1 chapter 5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md): each
says what changes when the nouns become datasets, features, and models. The
[DESIGN template](https://github.com/wmacevoy/software-engineering/blob/v1.7/templates/DESIGN_v1.md)
from SE1 works as is; its *State* table is where datasets, feature tables, models, and indexes go.

---

### <a id="d1"></a>DM1/D1. Data is the state, and a trained model is state too

*Reframes [SE1/D1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d1).*

SE1/D1 says software's material is state, not code. In a data system that is literal: the code is
small, and the state is everything else — raw pulls, cleaned tables, feature tables, splits, trained
weights, vector indexes, cached API responses. All four properties of state apply:

- **It persists.** A feature table built by last month's code is read by this month's.
- **It is shared.** Two analysts read "the clean table" and mean different versions.
- **It ages.** A model trained on last season is a fact about last season.
- **It changes while observed.** A table refreshed mid-run gives you half of each.

**A trained model is state, not code.** It cannot be read, only re-derived or tested, and a change
to it reaches every prediction at once.

> **Ask:** "List every dataset and model this pipeline holds: who builds it, from what, and who
> reads it?"

### <a id="d2"></a>DM1/D2. A trained model is a cache of the world at training time

*Reframes [SE1/D2](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d2).*

SE1/D2: anything that might be out of date is a cache. By that definition nearly everything in a
data system is one:

- **A trained model** is a cache of the relationship between inputs and outcome, as of the training
  window. Drift is cache staleness with a statistical name.
- **A local copy of an API pull** is a cache of a source that may revise its values. Without a fetch
  time and a refresh rule it is a time machine: the pipeline "ran green this morning" and has not
  contacted reality in a month.
- **A vector index** is a cache of the corpus *under one embedding model*. Change the embedding model
  and every vector is stale at once; store the model's name with the index.
- **A feature table** is a cache of a computation over raw data, as of the code that built it.

For each: what is it a copy of, how stale may it get, and what does a user see while it is wrong?
Put the age in the data — a `fetched_at`, a `trained_through` — where a test can see it.

> **Ask:** "What is this a copy of, as of when — and what tells us it has gone stale?"

### <a id="d3"></a>DM1/D3. There is no single "now" in a training set: join as of the prediction point

*Reframes [SE1/D3](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d3).*

Every row in a training set has its own "now" — its prediction point ([DM1/R2](04-requirements.md#r2)) —
and each feature must be the value as it was known *then*, not as it is known today. An ordinary join
on today's tables gives every historical row the benefit of every later correction. That is leakage
by construction ([DM1/U3](02-unknowing.md#u3)).

The mechanism is an **as-of (point-in-time) join**: for each row, take the latest value whose
*available-at* time is before the prediction point. It needs two times per value — when it was true,
and when it became known — and most sources only store the first. Where you cannot get the second,
write down the lag you are assuming ([DM1/R6](04-requirements.md#r6)).

> **Ask:** "For this feature, which version would the model have seen at prediction time?"

### <a id="d4"></a>DM1/D4. Make every stage idempotent: re-running it gives the same table

*Reframes [SE1/D4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d4).*

A pipeline gets re-run — after a crash, after a fix, by someone who was not sure it had finished. A
stage that **appends** doubles its output on the second run, and nothing downstream will say so; a
count of events just looks busier. A stage that **overwrites** its own partition (this day, this
lot, this run) can be run any number of times.

Incremental fetches need the same care: fetch from the last *recorded* high-water mark, de-duplicate
on a key, and make a full re-fetch a flag rather than a deletion.

> **Ask:** "What happens to the table if this stage runs twice?"

### <a id="d5"></a>DM1/D5. Pick one clock and one definition of "day," and write them down

*Reframes [SE1/D5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d5).*

Timestamps are data, not formatting. Store and compute in UTC with explicit offsets; convert to local
time at the edges, once. Then decide what a "day" is — a UTC day, or a local day — because a daily
aggregate over UTC shows a stadium's evening event split across two "days," and the chart beside it
in local time disagrees with it.

Two local days a year are 23 and 25 hours long. Across those boundaries a naive local-time series
shows a phantom one-hour gap in spring and a negative step in fall; a gap detector reports an outage
that did not happen. Test across both boundaries ([DM1/T6](06-testing.md#t6)).

> **Ask:** "Which clock is this column in, and what is a day here?"

### <a id="d6"></a>DM1/D6. Raw is the authority; everything else is derived by one command

*Reframes [SE1/D6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d6).*

Keep raw data immutable, exactly as it arrived, with where and when it came from. Everything
downstream — clean tables, features, splits, models, figures, the number in the slide — is derived
from raw by code, and can be deleted and rebuilt with one command.

A model is a function of four things: **the data version, the code version, the seed, and the
environment.** Record all four with every trained artifact. A model you cannot rebuild from them is
an orphan, and the first time it misbehaves you will not be able to tell whether the data, the code,
or the luck changed.

Sometimes raw isn't yours and can change: the source keeps only its current copy, or its owner
replaces a file. Then immutable is out of your hands, so hash each file on arrival and keep the hashes
tracked. That gives detection rather than prevention, and it is what you can have. The record of
arrival and any replaced copies are state ([DM1/D1](#d1)), since a reset that rebuilds from raw loses
them. A record *about* raw may sit beside it as a sidecar; say which files count as raw.

> **Ask:** "If I delete everything but raw, what command brings this number back — and does it
> come back the same?"

### <a id="d7"></a>DM1/D7. Pure transforms over frames; the notebook only calls them

*Reframes [SE1/D7](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d7).*

Keep the logic away from the I/O (SE1/D7). In data: each transform is a function from frames to a
frame, with no fetching, no file paths, no global state, no clock, and no unseeded randomness inside
it. Fetching, caching, and plotting sit at the edges and pass values in.

A notebook is a fine place to *call* those functions and look at what comes back. It is a poor place
to *define* them: hidden state, out-of-order cells, and a kernel nobody restarts make the notebook's
real program unknowable. When a cell is worth keeping, move its logic into a module and test it.

> **Ask:** "Could I test this transform with a five-row frame I typed by hand?"

### <a id="d8"></a>DM1/D8. A model's complexity must be paid for by its margin over the baseline

*Reframes [SE1/D8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d8).*

SE1/D8 separates essential complexity, which the problem demands, from accidental complexity, which
the solution chose. For a model, the measure is the baseline. A gradient-boosted ensemble with 400
features that beats *same as last comparable event* by two minutes, inside the seed-to-seed spread
([DM1/F7](03-feedback.md#f7)), is all accidental complexity. It has to be carried, monitored,
explained, and retrained, and it bought nothing.

Prefer the simplest model whose margin over the baseline survives the interval. Every feature is a
dependency on a source that can go absent, late, wrong, or lying.

> **Ask:** "What does this model buy over the baseline — and is that larger than the noise?"

### <a id="d9"></a>DM1/D9. One definition per feature: training and serving compute it with the same code

*Reframes [SE1/D9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d9).*

Conceptual integrity, in a data system, is that each named quantity has one definition. Its most
expensive violation is **train/serve skew**: a feature computed one way in the training pipeline and
another way where predictions are served — a different window, a different clock, a different
handling of missing values. The model is then trained on one thing and fed another, and no metric
measured offline can see it.

The medicine is structural: one function, imported by both. The same holds for metric definitions, the
outcome, and "day" ([DM1/D5](#d5)). Two people with two definitions of "occupancy" produce two
dashboards that disagree, and the meeting about which is right is about vocabulary.

Coherent around what? SE1/D9 answers: a written mission and, beside it, the fundamental shape. In data work
the mission is the decision the work serves ([DM1/R1](04-requirements.md#r1)), and the fundamental shape
holds guidelines such as raw is read-only, one definition per feature, and every score goes in the ledger. Both go at the top of the question
([template](../templates/QUESTION_v1.md)).

> **Ask:** "Is the feature the model sees in production computed by the same code it was trained
> on?"

### <a id="d10"></a>DM1/D10. A model's contract says what it was trained on and how it fails

*Reframes [SE1/D10](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d10).*

SE1/D10 asks that a part can be used correctly without reading inside it, and that its contract says
how it fails. A model cannot be read inside at all, so its contract carries more weight: what inputs
it expects and in what units; the population and period it was trained on; its metrics by slice
([DM1/T9](06-testing.md#t9)) with intervals; what it does outside its envelope
([DM1/U6](02-unknowing.md#u6)); and the uses it is not for ([DM1/R11](04-requirements.md#r11)).

That is a model card ([MODEL_CARD template](../templates/MODEL_CARD.md)). A dataset gets the
equivalent: a datasheet ([DATASHEET template](../templates/DATASHEET.md)).

> **Ask:** "Could someone use this model correctly from its card alone — including knowing when not
> to?"

### <a id="d11"></a>DM1/D11. A metric someone tracks is a promise

*Reframes [SE1/D11](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d11).*

Once a team has tracked a number for a year, its definition is an interface with dependents:
comparisons, targets, reports, decisions. Changing it silently destroys the comparability that was
the point of tracking it. You may change it; you may not change it silently. Name the new one, run
both for a while, and say when the old one stops.

The same is true of a published number. Once someone has acted on it, it has an installed base of at
least one decision.

> **Ask:** "Who already compares against this number, and will they know it changed?"

### <a id="d12"></a>DM1/D12. A model is capital you carry, with no expiry date printed on it

*Reframes [SE1/D12](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d12).*

Every deployed model carries a weekly cost: monitoring, retraining, its data dependencies, its
feature pipeline, the person who has to answer for it. In a machine learning system the model code
is the small box; the configuration, data collection, feature extraction, verification, and serving
around it are the rest (Sculley et al., in [READING.md](../READING.md)).

A model is also a perishable good: the world moves, columns change meaning, and nothing announces
it. Someone has to own the question of whether last year's model is still true, with a date on which
they will ask it.

> **Ask:** "Who owns this model, when will they next check that it is still true — and what can we
> delete?"

### <a id="d13"></a>DM1/D13. Provenance at every layer: value, dataset, label, result

*Reframes [SE1/D13](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/05-design.md#d13).*

A number with no lineage is a rumor. The question SE1/D13 asks — *who said this, and can I check it
later without asking them again?* — has the same answer at four layers:

- **A value:** measured, imputed, or defaulted? Once all three live in one column they are the same
  value forever. Keep a flag. From a document, also how it was read: by text layer, by recognition,
  or by eye.
- **A dataset:** collected by whom, of whom, under what consent, and what was excluded before it
  reached you? The exclusions are invisible in the file and usually the most important thing in it.
- **A label:** applied by whom, under which instructions, with what agreement rate
  ([DM1/R7](04-requirements.md#r7)).
- **A result:** which data version, code commit, seed, and environment ([DM1/D6](#d6)). This is the
  layer people skip, and it is why a number that reproduced in September stops reproducing in March.

Provenance is the mechanical answer to K3. "I'm sure this is right" is a feeling. "Here is the
command that regenerates it from raw, and here it is producing the same number" is evidence.

> **Ask:** "Where did this number come from, and could I regenerate it without asking anyone?"

---

Next: [6. Testing](06-testing.md) — checks worth trusting, for pipelines and for models.
