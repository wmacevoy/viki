# 9. People

The domain expert, the client, the team, and what passes between them when the product is a claim.
Read with [SE1 chapter 9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md),
all of which applies; the people outside the room are in [DM1/P3](01-purpose.md#p3) and
[DM1/R9](04-requirements.md#r9).

---

### <a id="h1"></a>DM1/H1. The domain expert is a feedback loop no test replaces; show them rows, not summaries

*Reframes [SE1/H5](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h5).*

SE1/H5: the client knows the domain and cannot see its edges. In data work, the person who knows the
domain is usually the only loop that can catch the most expensive errors ([DM1/F3](03-feedback.md#f3)):
the sensor that was being replaced, the column whose meaning changed, the event that was cancelled
after tickets sold, the label everybody knows is unreliable on Sundays.

They cannot see those things in a metric. Show them what they can judge:

- a plot over time, with the anomalies marked;
- ten raw rows, including the strangest five;
- five of the model's worst predictions, and five members of each cluster;
- the features the model leans on most, in their words.

Then ask what surprises them. Their surprise is your K2, found cheaply.

> **Ask:** "What would someone who has worked the lots for ten years say looking at this?"

### <a id="h2"></a>DM1/H2. Read your assumptions to the client, in their words

*Reframes [SE1/H6](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h6).*

Every analysis rests on K4s ([DM1/U4](02-unknowing.md#u4)), and most of them are statistical
sentences the client cannot evaluate as written. Translate each into a claim about their world, and
read it aloud:

- "Missing at random" → "We're assuming a sensor that goes quiet is no more likely to be at a busy
  lot than a quiet one. True?"
- "Stationarity" → "We're assuming next season's crowds behave like the last three. Anything
  changing?"
- "Label validity" → "We're treating 'attendant radioed full' as the lot being full. Is that how it
  works?"

The answer is often "no, because…" — and that sentence is worth more than any model you could train
before hearing it.

> **Ask:** "We're assuming X. True?"

### <a id="h3"></a>DM1/H3. Report a null the same week you find it, in the same tone as a win

*Reframes [SE1/H9](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h9).*

SE1/H9: with bad news, the delay is the cost. "The effect isn't there" in week two is a finding, and
an inexpensive one: the client can stop paying for the question or change it. The same sentence in
week eleven is a crisis — and the pressure of that crisis is what turns an honest null into a
tortured positive ([DM1/W5](08-work.md#w5)).

Say it with the same tone you would use for a win. A null that is announced apologetically teaches
the team that nulls are failures, and the next one will be found later, or not at all.

> **Ask:** "How many days between knowing the effect isn't there and saying so?"

### <a id="h4"></a>DM1/H4. One owner per definition: two people cleaning the same dataset produce three datasets

*Reframes [SE1/H4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h4).*

SE1/H4: the interface between two people is a contract. In data work the contract is usually a
definition — what counts as an event, what "occupied" means, which rows are excluded — and when two
people each clean "the" dataset, each makes those decisions privately, and a third version appears
when someone merges their work.

Give each shared definition one owner and one implementation ([DM1/D9](05-design.md#d9)), and each
cleaned dataset one producing command. Everyone else imports it. A meeting that keeps recurring about
why two dashboards disagree is a missing contract.

> **Ask:** "Who owns this definition, and is there exactly one place it is computed?"

### <a id="h5"></a>DM1/H5. Whoever owns the definition owns the conclusion

*Reframes [SE1/H1](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h1).*

SE1/H1 says the team's shape becomes the system's shape. In data, it becomes the *conclusions'*
shape: the team that owns the pipeline owns the metric definitions, the definitions decide the
results, and so the analysis comes to resemble the organization that produced it. The group that
defines "full" decides how often lots are full.

If that is going to happen anyway, choose it on purpose: put the definitions where the people they
affect can read and contest them, and do not let the team that is measured own the measure alone.

> **Ask:** "Who chose this definition, and who would have chosen differently?"

### <a id="h6"></a>DM1/H6. Present uncertainty in the client's units and decisions, not the model's

*New.*

"AUC 0.83" means nothing to the person deciding which lots to open. Translate every result into the
decision's own terms ([DM1/R1](04-requirements.md#r1)), with its uncertainty attached:

- not "MAE 11.4 minutes," but "for four events in five, lot C fills within fifteen minutes of the
  forecast; for the fifth, it can be off by forty";
- not "precision 0.9," but "of the times the sign says *full*, about one in ten there are still spaces";
- not a point, but a range: "fills between 6:20 and 6:50."

Frequencies ("one in five") are understood more reliably than probabilities ("20%"), and they say
honestly what the number is about: a reference class, not the one case the client is asking about
([DM1/U8](02-unknowing.md#u8)). And say what the
model cannot tell them, in the same breath.

> **Ask:** "Could the person making the decision act on this sentence, and know how often it will be
> wrong?"

### <a id="h7"></a>DM1/H7. A model demo is a test: choose the examples before the meeting, and include failures

*Reframes [SE1/H8](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h8).*

SE1/H8: a demo is a test, so run it like one. A model demo has a specific temptation: pick the
examples that work. Then the demo shows the client a model that does not exist, and the client's
objection — the most valuable output of the meeting — never comes.

Pick the examples beforehand and write them down: some at random, some typical, and the worst ones
the model gets wrong. Show the failures as clearly as the successes, and ask which failures matter
to them. That answer is a requirement ([DM1/R4](04-requirements.md#r4)).

> **Ask:** "Were these examples chosen before we knew how the model would do on them?"

### <a id="h8"></a>DM1/H8. Say out loud what you do not believe about the number; here, disbelief is data

*Reframes [SE1/H4](https://github.com/wmacevoy/software-engineering/blob/v1.7/guide/09-people.md#h4).*

SE1/H4's stand-up asks a fourth question: *do you notice discomfort? can you name it?* In data work
the discomfort has a short list of shapes, and each one is worth more said than kept. "This is too
good" is the first sign of leakage ([DM1/U3](02-unknowing.md#u3)). "I keep wanting to look at the
holdout" is a look about to be spent ([DM1/F5](03-feedback.md#f5)). "I don't trust the Sunday
labels" is a K4 about to become a K3 ([DM1/U4](02-unknowing.md#u4)). "The effect went away when I
changed the window" is a null being negotiated with ([DM1/W5](08-work.md#w5)). Each is cheaper to
say in week two than to find in week eleven, and none of them shows up in a metric.

So the stand-up in data work asks, per person: done, next, blocked, and *what about the
current number do you not believe?* One sentence. Noticing without a reason is still data, and a
disbelief nobody can name yet goes in FINDINGS as a question, not in a drawer.

> **Ask:** "What about this result do you not believe, and has anyone said it out loud?"

---

Next: [10. Agents](10-agents.md) — what changes when a fast analyst has no stake in the answer.
