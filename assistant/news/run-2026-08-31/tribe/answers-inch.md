# Calibration quiz answers — inch

### 1
verdict: FALSE
status: k0
confidence: 93
why: The store's 95% figure is the rate at which models *engaged with* the untenable premise (i.e., failed to reject it) under the agentic/study-design framing, not a rejection rate — the actual outright-rejection rate reported is 0.93±0.004 under direct factual challenge, a different number under a different condition, so the proposition inverts what TRACES found.

### 2
verdict: TRUE
status: k0
confidence: 95
why: The store states directly that OpenAI published on Feb 23, 2026 that it had stopped evaluating frontier models on SWE-bench Verified, citing contamination and broken tests.

### 3
verdict: TRUE
status: k0
confidence: 95
why: The store states the ICML 2026 reproduction hackathon was run by "Hugging Face + AlphaXiv," sourced to Hugging Face's own blog.

### 4
verdict: FALSE
status: k0
confidence: 92
why: The store reports the same campaign "failed completely against maltose-binding protein (0 of 90 designs bound)," cited specifically as evidence the overall result isn't cherry-picked — so it did not succeed on every target attempted.

### 5
verdict: FALSE
status: k0
confidence: 90
why: The store explicitly labels the Terminal-Bench 2.1 figures "vendor/tracker-reported, not independently audited" and states no source found in the search independently re-ran or audited them.

### 6
verdict: UNKNOWN
status: k1
confidence: 88
why: The store's own "WHAT I AM UNSURE OF" section says explicitly it found no source reconciling whether the AISI 10/122 figure and Anthropic's 3/141,006 figure describe the same testing effort or two separate ones.

### 7
verdict: UNKNOWN
status: k1
confidence: 87
why: The store explicitly says it did not find a Hugging-Face-side statement on customer impact, only "credentials exfiltrated" and "production Kubernetes clusters reached" language from other sources.

### 8
verdict: UNKNOWN
status: k1
confidence: 75
why: The store found the "8 of 105, UChicago" claim only inside one aggregator, could not locate the underlying report, and explicitly flags it as unverifiable rather than confirming or debunking it — so whether such an audit exists and established that figure is not answered by the store either way.

### 9
verdict: UNKNOWN
status: k1
confidence: 85
why: The store explicitly lists this as an open question — the GPT-5.6-Sol attribution came via CNBC/aggregator language, and the store says it did not find an OpenAI-sourced confirmation, without asserting one doesn't exist.

### 10
verdict: UNKNOWN
status: k1
confidence: 88
why: The store gives no total-compute-cost figure for the Hugging Face/AlphaXiv hackathon at all; the only dollar figure in the store ($2.2M) is attached to the separate, unverified "University of Chicago" claim that the store itself says doesn't match this effort.

### 11
verdict: UNKNOWN
status: k1
confidence: 90
why: The store gives the count (30 models, 10 runs each) for TRACES but never breaks down how many were open-weight versus closed, so this isn't addressed at all.

### 12
verdict: UNKNOWN
status: k1
confidence: 90
why: The store never mentions any statement, public or otherwise, from Hugging Face's CEO — it only notes the absence of a "Hugging Face-side statement on customer impact" generally, with no timing detail.

### 13
verdict: TRUE
status: k0
confidence: 93
why: The store states "Two independent labs, Adaptyv Bio and Twist Bioscience, physically synthesized and tested the designs (not just simulated)."

## What I am least sure of

- **Item 1** is the one I'd most expect to get flipped on a careless read: the store reports two different percentages (93% outright-rejection under direct challenge, 95% engagement-with-the-premise under agentic framing) that are close in magnitude but opposite in meaning, attached to different test conditions in the same sentence. My verdict rests on correctly matching each number to its condition, and that's exactly the kind of pairing an agent's summarizing instinct tends to smooth over into "95% correctly rejected."
- **Item 8** is a genuine judgment call I could have gotten wrong the other way. The store doesn't say the UChicago 8/105 claim is false — it says it's unverified beyond one weak aggregator and inconsistent with the better-sourced HF/AlphaXiv numbers. I called this UNKNOWN rather than FALSE because "unverifiable" isn't the same as "debunked," but a grader could reasonably have wanted FALSE here (treating "not locatable, contradicted by better data" as good enough to call the claim false rather than merely unresolved).
- **Item 9** has the same FALSE-vs-UNKNOWN tension as item 8: the store's language ("only attributed... via secondhand reporting, not an OpenAI-sourced confirmation") leans toward "OpenAI has NOT itself confirmed this," which could justify FALSE with moderate confidence instead of the UNKNOWN I gave. I chose UNKNOWN because the store frames it as something it didn't resolve rather than something it affirmatively checked and found absent, but I'm not fully confident that's the right side of the line.
